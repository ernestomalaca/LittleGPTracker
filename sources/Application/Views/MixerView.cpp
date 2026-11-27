#include "MixerView.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "UIController.h"
#include "VuMeterUtil.h"
#include <iostream>
#include <sstream>
#include <string>
#include "Services/Audio/AudioOutDriver.h"
#include "Application/Model/Config.h"
#include <stdlib.h>
#include <stdio.h>

// trk2als: carga del audio (pico del último segundo) y su estimado en la
// consola de referencia: CONSOLEFACTOR en config.xml (cuántas veces más lenta
// es, 9 para la RG353V contra una Mac con Apple Silicon)
static int loadShown = -1;   // la carga que se pintó en la mixer
static void loadText(char *out) {
    static int factor = -1;
    if (factor < 0) {
        const char *v = Config::GetInstance()->GetValue("CONSOLEFACTOR");
        factor = v ? atoi(v) : 9;
        if (factor < 1) factor = 1;
    }
    int now, peak;
    GetAudioLoad(now, peak);
    int est = peak * factor;
    if (est > 999) est = 999;
    (void)peak;   // solo cabe el estimado de la consola (5 caracteres)
    sprintf(out, "rg%3d", est > 999 ? 999 : est);
}

MixerView::MixerView(GUIWindow &w,ViewData *viewData):View(w,viewData) {
	clipboard_.active_=false ;
	clipboard_.data_=0 ;
	invertBatt_=false;
    soloChannel_ = -1;
    mixerRow_ = 1; // start on volume row
    for (int i = 0; i < 9; i++) vuL_[i] = vuR_[i] = 0;
    for (int i = 0; i < 18; i++) vuShown_[i] = 0;
    lastVu_ = 0; masterOn_ = false; tapKey_ = 0; gesture_ = 0;
    previousViewType_ = VT_SONG; // Default to SongView
    for (int i = 0; i < 9; i++) {
        vuBarHeightsL_[i] = 0;
        vuBarHeightsR_[i] = 0;
    }
}

MixerView::~MixerView() {}

void MixerView::onStart() {
	Player *player=Player::GetInstance() ;
	unsigned char from=viewData_->songX_ ;
	unsigned char to=from ;
	//if (clipboard_.active_) {
	//	GUIRect r=getSelectionRect();
	//	from=r.Left() ;
	//	to=r.Right() ;
	//}
	player->OnStartButton(PM_SONG,from,false,to) ;
} ;

void MixerView::onStop() {
	Player *player=Player::GetInstance() ;
	unsigned char from=viewData_->songX_ ;
	unsigned char to=from ;
	player->OnStartButton(PM_SONG,from,true,to) ;
}

// Stay on the same column as SongView
void MixerView::OnFocus() { viewData_->mixerCol_ = viewData_->songX_; }

void MixerView::updateCursor(int dx,int dy) {
    if (dy != 0) {
        // UP/DOWN cycles between rows 1, 2, 3
        if (dy < 0) {
            mixerRow_ = (mixerRow_ == 1) ? 3 : mixerRow_ - 1;
        } else {
            mixerRow_ = (mixerRow_ == 3) ? 1 : mixerRow_ + 1;
        }
		isDirty_ = true;
    }
    if (dx != 0) {
        // LEFT/RIGHT switches between channels (0-7 only, Master is
        // display-only)
        int x = viewData_->mixerCol_;
		x += dx;
		if (x < 0) x = 0;
		if (x > 7) x = 7;
		{   // trk2als: solo las columnas que hay (las de la Session)
			int tc = viewData_->project_->TrackCount();
			int n = tc > Project::MaxTracks() ? tc : Project::MaxTracks();
			if (x > n - 1) x = n - 1;
		}
		viewData_->mixerCol_ = x;
		isDirty_ = true;
	}
}

void MixerView::toggleMute() {
    int col = viewData_->mixerCol_;
    UIController::GetInstance()->ToggleMute(col, col);
    isDirty_ = true;
};

void MixerView::toggleSolo() {
	int col=viewData_->mixerCol_ ;
	bool entering=(soloChannel_!=col) ;
	UIController::GetInstance()->SwitchSoloMode(col,col,entering) ;
    soloChannel_ = entering ? col : -1;
    isDirty_ = true;
}

void MixerView::unMuteAll() {
    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
    isDirty_ = true;
}

void MixerView::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (SupportsShapes() && !View::miniLayout_) {
        // trk2als: R sola (tocar y soltar): el master encima de la última
        // pista; L sola: de vuelta. Y al soltar una tecla no se hace nada más
        unsigned short p = View::physMask_;
        if (!(p & EPBM_B)) gesture_ = 0;
        if (pressed) tapKey_ = (p == EPBM_R || p == EPBM_L) ? p : 0;
        else {
            if (p == 0 && tapKey_) { masterOn_ = (tapKey_ == EPBM_R); isDirty_ = true; }
            if (p == 0) tapKey_ = 0;
            return;
        }
        // mute y solo como en la Session (el gesto de LSDJ): B + Select mutea
        // la pista elegida, B + Start la deja sola; B sola quita lo que haya
        if (p == (EPBM_B | EPBM_SELECT) || p == (EPBM_B | EPBM_START)) {
            if (gesture_ != p) {
                gesture_ = p;
                if (p & EPBM_SELECT) toggleMute(); else toggleSolo();
            }
            return;
        }
        if (p == EPBM_B) {
            bool any = soloChannel_ >= 0;
            for (int i = 0; i < SONG_CHANNEL_COUNT && !any; i++) any = Player::GetInstance()->IsChannelMuted(i);
            if (any) { unMuteAll(); soloChannel_ = -1; }
            return;
        }
        // (los atajos de LGPT con R ya no van aquí; con flechas sí pasa: es
        // cambiar de pantalla)
        if ((mask & (EPBM_R | EPBM_L)) && !(mask & (EPBM_UP | EPBM_DOWN | EPBM_LEFT | EPBM_RIGHT))) return;
    }

    if (clipboard_.active_) {
		viewMode_=VM_SELECTION ;
	} ;
	// Process selection related keys

    if (viewMode_ == VM_SELECTION) {
        if (clipboard_.active_==false) {
            clipboard_.active_=true ;
            clipboard_.x_ = viewData_->songX_;
            clipboard_.y_=viewData_->songY_ ;
            clipboard_.offset_=viewData_->songOffset_ ;
			saveX_=clipboard_.x_ ;
			saveY_=clipboard_.y_ ;
			saveOffset_=clipboard_.offset_ ;
        }
        processSelectionButtonMask(mask) ;
    } else {

        // Switch back to normal mode

        viewMode_=VM_NORMAL ;
        processNormalButtonMask(mask);
    }
};

/******************************************************
 processNormalButtonMask:
        process button mask in the case there is no
        selection active
 ******************************************************/

void MixerView::processNormalButtonMask(unsigned int mask) {

    // R Modifier
    if (mask & EPBM_R) {
        if (mask & EPBM_B) {
            toggleMute();
        } else if (mask & EPBM_A) {
            toggleSolo();
        } else if (mask & EPBM_L) {
            unMuteAll();
        } else if (mask & EPBM_UP) {
            // R + UP = go back to previous view
            ViewEvent ve(VET_SWITCH_VIEW, &previousViewType_);
            // Go back to the same column as in MixerView
            viewData_->songX_ = viewData_->mixerCol_;
            SetChanged();
            NotifyObservers(&ve);
        } else if (mask & EPBM_RIGHT) {
            // trk2als: las tablas de LGPT ya no tienen puerta
        } else if (mask & EPBM_START) {
            onStop();
        }
    } else if (mask & EPBM_B) {
        if (mask & EPBM_A) {
            // B + A = cut: reset volume to full
            Mixer::GetInstance()->SetChannelVolume(viewData_->mixerCol_, 0xFF);
            isDirty_ = true;
        }
    } else if (mask & EPBM_A) {
        if (mixerRow_ == 3) {
            // On LPF row: A+Up/Down = coarse ×2/÷2, A+Left/Right = fine
            // ±10Hz A alone (no direction) = toggle off/1000Hz
            Mixer *m = Mixer::GetInstance();
            int col = viewData_->mixerCol_;
            unsigned short freq = m->GetChannelLPF(col);
            unsigned short newFreq = freq;
            if (mask & EPBM_UP) {
                if (freq == 0)
                    newFreq = 20;
                else {
                    unsigned short step = freq / 10 < 1 ? 1 : freq / 10;
                    newFreq =
                        (unsigned short)(freq + step > 20000 ? 0
                                                                : freq + step);
                }
            } else if (mask & EPBM_DOWN) {
                if (freq == 0)
                    newFreq = 20000;
                else {
                    unsigned short step = freq / 10 < 1 ? 1 : freq / 10;
                    newFreq = (freq <= step)
                                    ? 0
                                    : (unsigned short)(freq - step < 20
                                                            ? 0
                                                            : freq - step);
                }
            } else if (mask & EPBM_RIGHT) {
                if (freq == 0)
                    newFreq = 20;
                else
                    newFreq =
                        (unsigned short)(freq + 10 > 20000 ? 0 : freq + 10);
            } else if (mask & EPBM_LEFT) {
                if (freq == 0)
                    newFreq = 20000;
                else
                    newFreq =
                        (freq <= 10)
                            ? 0
                            : (unsigned short)(freq - 10 < 20 ? 0
                                                                : freq - 10);
            }
            if (newFreq != freq) {
                m->SetChannelLPF(col, newFreq);
                isDirty_ = true;
            }
            // show notification
            if (newFreq == 0) {
                SetNotification("       Low Pass Filter: OFF");
            } else {
                char notifBuf[40];
                sprintf(notifBuf, "       Low Pass Filter: %dHz",
                        (int)newFreq);
                SetNotification(notifBuf);
            }
        } else if (mixerRow_ == 2) {
            // On HPF row: A+Right cycles forward, A+Left cycles backward
            if (mask & (EPBM_RIGHT | EPBM_LEFT)) {
                Mixer *m = Mixer::GetInstance();
                int col = viewData_->mixerCol_;
                int mode = m->GetChannelHPF(col);
                if (mask & EPBM_RIGHT) {
                    mode = (mode + 1) % 3;
                } else {
                    mode = (mode + 2) % 3;
                }
                m->SetChannelHPF(col, mode);
                isDirty_ = true;
                const char *modeStr = (mode == 0)   ? "OFF"
                                        : (mode == 1) ? "20Hz"
                                                    : "90Hz";
                std::string notif =
                    std::string("      High Pass Filter: ") + modeStr;
                SetNotification(notif.c_str());
            }
        } else if (mixerRow_ == 1) {
            // On volume row: A adjusts volume
            Mixer *mixer = Mixer::GetInstance();
            int col = viewData_->mixerCol_;
            int currentVol = mixer->GetChannelVolume(col);
            int newVol = currentVol;

            // Coarse adjustment (UP/DOWN)
            if (SupportsShapes() && !View::miniLayout_ && (mask & (EPBM_UP | EPBM_DOWN))) {
                // trk2als: con figuras se lee en dB: de 1 dB en 1 dB
                float db = (currentVol > 0) ? 20.0f * log10f(currentVol / 255.0f) : -49.0f;
                db = floorf(db + 0.5f) + ((mask & EPBM_UP) ? 1.0f : -1.0f);
                newVol = (db < -48.0f) ? 0 : int(255.0f * powf(10.0f, db / 20.0f) + 0.5f);
                if ((mask & EPBM_UP) && newVol <= currentVol) newVol = currentVol + 1;
                if ((mask & EPBM_DOWN) && newVol >= currentVol) newVol = currentVol - 1;
            } else {
            if (mask & EPBM_UP) {
                newVol = currentVol + 16;
            }
            if (mask & EPBM_DOWN) {
                newVol = currentVol - 16;
            }
            }

            // Fine adjustment (RIGHT/LEFT)
            if (mask & EPBM_RIGHT) {
                newVol = currentVol + 1;
            }
            if (mask & EPBM_LEFT) {
                newVol = currentVol - 1;
            }

            // Clamp to valid range (0-255)
            if (newVol < 0)
                newVol = 0;
            if (newVol > 255)
                newVol = 255;

            if (newVol != currentVol) {
                mixer->SetChannelVolume(col, newVol);
                isDirty_ = true;
            }
        }
    } else {
        // Normal cursor movement (no modifier)
		// This works everywhere, including Master channel
		bool strips = SupportsShapes() && !View::miniLayout_;   // trk2als: solo volúmenes: no hay filas
		if ((mask & EPBM_UP) && !strips)
			updateCursor(0, -1);
		if ((mask & EPBM_DOWN) && !strips)
			updateCursor(0, 1);
		if (mask & EPBM_LEFT)
			updateCursor(-1, 0);
		if (mask & EPBM_RIGHT)
			updateCursor(1, 0);

		if (mask & EPBM_START) {
			if (strips) {   // play / stop, como en las demás pantallas
				Player::GetInstance()->EditorStart(viewData_->songX_, viewData_->songY_);
				isDirty_ = true;
			} else onStart();
		}
    }
}

/******************************************************
 processSelectionButtonMask:
        process button mask in the case there is a
        selection active
 ******************************************************/

void MixerView::processSelectionButtonMask(unsigned int mask) {

	// B Modifier

    if (mask & EPBM_B) {

    } else {

        // A modifier

        if (mask & EPBM_A) {

        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask&EPBM_START) {
				    onStop() ;
                }
            } else {

                // No modifier
                if (mask & EPBM_START) {
                    onStart();
                }
            }
        }
    }
}

void MixerView::DrawView() {

    Clear();

    GUITextProperties props;
    GUIPoint pos=GetTitlePosition() ;
	GUIPoint anchor=GetAnchor() ;
	char hex[3] ;

// Draw title

	SetColor(CD_NORMAL) ;

    Player *player = Player::GetInstance();

    std::ostringstream os;

    os << ((player->GetSequencerMode() == SM_SONG) ? "Song" : "Live");

    std::string buffer(os.str());

    if (!drawTopBar(TAB_MIX, ""))
        DrawString(pos._x, pos._y, buffer.c_str(), props);
    else {   // trk2als: con figuras, la mixer es otra: tiras de volumen
        mixerRow_ = 1;
        drawStrips();
        drawHelpBar("<>:elegir pista;A+^v:volumen;A+<>:volumen, fino;B+A:volumen a tope;B+SEL:mute;B+START:solo;B:quitar mutes y solos;R:ver el master;L:quitarlo;START:play / stop;SEL+<>:otra pantalla");
        return;
    }

    // Draw mixer grid with row labels
    pos = anchor;
    short dx = 3;
    Mixer *mixer = Mixer::GetInstance();
    Player *playerInst = Player::GetInstance();
    GUITextProperties rowLabelProps;

    // Row 0: Track number / Solo / Mute
    if (mixerRow_ == 0) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 3, pos._y, " ", rowLabelProps);

    for (int i = 0; i < 8; i++) {
        props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == 0);
        SetColor((i == viewData_->mixerCol_ && mixerRow_ == 0) ? CD_HILITE2
                                                               : CD_COL_TITLE);

        if (playerInst->IsChannelMuted(i)) {
            DrawString(pos._x, pos._y, "M ", props);
        } else {
            int bus = mixer->GetBus(i);
            hex2char(bus, hex);
            DrawString(pos._x, pos._y, hex, props);
        }
        pos._x += dx;
    }

    // Row 1: volumes
    pos = anchor;
    pos._y += 1;
    if (mixerRow_ == 1) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 5, pos._y, "VOL", rowLabelProps);

    pos._x = anchor._x;
    for (int i = 0; i < 8; i++) {
        props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == 1);
        SetColor((i == viewData_->mixerCol_ && mixerRow_ == 1) ? CD_HILITE2
                                                               : CD_NORMAL);

        int vol = mixer->GetChannelVolume(i);
        hex2char(vol, hex);
        DrawString(pos._x, pos._y, hex, props);
        pos._x += dx;
    }

    // Row 2: HPF mode
    pos = anchor;
    pos._y += 3;
    if (mixerRow_ == 2) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 5, pos._y, "HPF", rowLabelProps);

    pos._x = anchor._x;
    for (int i = 0; i < 8; i++) {
        props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == 2);
        SetColor((i == viewData_->mixerCol_ && mixerRow_ == 2) ? CD_HILITE2 : CD_NORMAL);

        int hpf = mixer->GetChannelHPF(i);
        char code[4];
        if (hpf == 0) {
            code[0] = '-';
            code[1] = '-';
            code[2] = ' ';
            code[3] = '\0';
        } else if (hpf == 1) {
            code[0] = '2'; code[1] = '0'; code[2] = ' '; code[3] = '\0';
        } else {
            code[0] = '9'; code[1] = '0'; code[2] = ' '; code[3] = '\0';
        }
        DrawString(pos._x, pos._y, code, props);
        pos._x += dx;
    }

    // Row 3: LPF frequency
    pos = anchor;
    pos._y += 4;
    if (mixerRow_ == 3) {
        rowLabelProps.invert_ = true;
        SetColor(CD_HILITE2);
    } else {
        rowLabelProps.invert_ = false;
        SetColor(CD_NORMAL);
    }
    DrawString(pos._x - 5, pos._y, "LPF", rowLabelProps);

    pos._x = anchor._x;
    for (int i = 0; i < 8; i++) {
        props.invert_ = (i == viewData_->mixerCol_ && mixerRow_ == 3);
        SetColor((i == viewData_->mixerCol_ && mixerRow_ == 3) ? CD_HILITE2 : CD_NORMAL);

        unsigned short lpf = mixer->GetChannelLPF(i);
        char lpfCode[5];
        if (lpf == 0) {
            lpfCode[0] = '-'; lpfCode[1] = '-'; lpfCode[2] = ' '; lpfCode[3] = '\0';
        } else if (lpf < 100) {
            lpfCode[0] = '0' + (lpf / 10);
            lpfCode[1] = '0' + (lpf % 10);
            lpfCode[2] = ' '; lpfCode[3] = '\0';
        } else if (lpf < 1000) {
            lpfCode[0] = '0' + (lpf / 100);
            lpfCode[1] = 'h';
            lpfCode[2] = ' '; lpfCode[3] = '\0';
        } else if (lpf < 10000) {
            lpfCode[0] = '0' + (lpf / 1000);
            lpfCode[1] = 'k';
            lpfCode[2] = ' '; lpfCode[3] = '\0';
        } else {
            lpfCode[0] = '0' + (lpf / 10000);
            lpfCode[1] = '0' + (lpf % 10000) / 1000;
            lpfCode[2] = 'k';
            lpfCode[3] = ' '; lpfCode[4] = '\0';
        }
        DrawString(pos._x, pos._y, lpfCode, props);
        pos._x += dx;
    }

    drawMap() ;
	drawNotes() ;
    EnableNotification();

    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    }

    // Draw VU bars at the end so they appear on top of everything
    // They will always show the empty dashes and smoothly decay when paused
    DrawVuBars();
    drawHelpBar("A+flechas:ajustar;SEL+<>:pantalla");
};

void MixerView::OnPlayerUpdate(PlayerEventType, unsigned int tick) {

    Player *player = Player::GetInstance();
    if (SupportsShapes() && !View::miniLayout_) return;   // trk2als: con figuras, solo las tiras

    // Draw clipping indicator & CPU usage

    GUIPoint anchor = GetAnchor();
    GUIPoint pos=anchor ;

    GUITextProperties props;
    SetColor(CD_NORMAL) ;

    if (View::miniLayout_) {
      pos._y=0 ;
      pos._x=25 ;
    } else {
      pos=anchor ;
      pos._x+=25 ;
    }

    if (player->Clipped()) {
        DrawString(pos._x, pos._y, "clip", props);
    } else {
        DrawString(pos._x, pos._y, "----", props);
    }
    char strbuffer[24];
    pos._y += 1;
    loadText(strbuffer);   // trk2als: carga en la Mac y estimado en la consola
    DrawString(pos._x, pos._y, strbuffer, props);

    System *sys = System::GetInstance();
    int batt=sys->GetBatteryLevel() ;
    if (batt>=0) {
		if (batt<90) {
			SetColor(CD_HILITE2) ;
			invertBatt_=!invertBatt_ ;
		} else {
			invertBatt_=false ;
		} ;
		props.invert_=invertBatt_ ;

	    pos._y+=1 ;
        sprintf(strbuffer, "%3.3d", batt);
        DrawString(pos._x,pos._y,strbuffer,props) ;
    }
    SetColor(CD_NORMAL);
    props.invert_=false ;
    int time=int(player->GetPlayTime()) ;
    int mi=time/60 ;
    int se=time-mi*60 ;
    sprintf(strbuffer, "%2.2d:%2.2d", mi, se);
    pos._y += 1;
    DrawString(pos._x,pos._y,strbuffer,props) ;

    drawNotes() ;
};

/******************************************************
 DrawVuBars:
        Draw VU meters below the mixer controls
        Called from both AnimationUpdate and DrawView to ensure
        bars are always visible and never flicker
 ******************************************************/

void MixerView::DrawVuBars() {
    Player *player = Player::GetInstance();

    GUIPoint vuPos = GetAnchor();
    vuPos._y += 16; // Position below the row labels (bus, vol, hpf, lpf)

    GUITextProperties vuProps;
    vuProps.invert_ = true;

    short dx = 3;             // 3 chars per channel (L and R + 1 space)
    short masterSpacing = 1;  // Extra spacing before Master column (matches grid)

    // Read peak levels for all 9 channels
    float peakLevelsL[9];
    float peakLevelsR[9];

    if (!player->IsRunning()) {
        for (int i = 0; i < 9; i++) {
            peakLevelsL[i] = peakLevelsR[i] = 0.0f;
        }
    } else {
        MixerService *ms = MixerService::GetInstance();
        
        // Channels 0-7: Read pre-volume peaks
        for (int i = 0; i < 8; i++) {
            MixBus *bus = ms->GetMixBus(i);
            if (bus) {
                uint32_t level = bus->GetPreMasterVolumePeakLevel();
                peakLevelsL[i] = (float)((level >> 16) & 0xFFFF) / 32767.0f;
                peakLevelsR[i] = (float)(level & 0xFFFF) / 32767.0f;
            } else {
                peakLevelsL[i] = peakLevelsR[i] = 0.0f;
            }
        }
        
        // Channel 8 (Master): Read post-volume peaks
        uint32_t masterLevel = ms->GetMasterPeakLevel();
        peakLevelsL[8] = (float)((masterLevel >> 16) & 0xFFFF) / 32767.0f;
        peakLevelsR[8] = (float)(masterLevel & 0xFFFF) / 32767.0f;
    }

    // Update bar heights with slew rate decay for all 9 channels using utility
    int displayHeightsL[9];
    int displayHeightsR[9];
    UpdateVuBarHeights(vuBarHeightsL_, displayHeightsL, peakLevelsL, 9);
    UpdateVuBarHeights(vuBarHeightsR_, displayHeightsR, peakLevelsR, 9);

    // Draw vertical VU bars for all 9 channels (L and R for each)
    for (int row = 0; row < VU_METER_HEIGHT; row++) {
        SetColor(GetVuBarColor(row));

        // Draw channels 0-7
        for (int i = 0; i < 8; i++) {
            GUIPoint pos = vuPos;
            pos._x += i * dx;
            pos._y -= row;

            // Left channel
            DrawVuBarRow(this, pos, row, displayHeightsL[i], vuProps, GetVuBarColor(row));

            // Right channel at x+1
            pos._x += 1;
            DrawVuBarRow(this, pos, row, displayHeightsR[i], vuProps,
                         GetVuBarColor(row));
        }

        // Draw Master channel with spacing
        GUIPoint masterPos = vuPos;
        masterPos._x += 8 * dx + masterSpacing;
        masterPos._y -= row;

        // Master left channel
        DrawVuBarRow(this, masterPos, row, displayHeightsL[8], vuProps, GetVuBarColor(row));

        // Master right channel at x+1
        masterPos._x += 1;
        DrawVuBarRow(this, masterPos, row, displayHeightsR[8], vuProps, GetVuBarColor(row));
    }

    SetColor(CD_NORMAL);
}

void MixerView::AnimationUpdate() {
    if (SupportsShapes() && !View::miniLayout_) {
        // los medidores: ~12 veces por segundo y solo si alguno cambió a la
        // vista (repintar toda la pantalla a cada paso ahogaba al audio)
        if (HasModal()) return;
        unsigned long now = System::GetInstance()->GetClock();
        if (now - lastVu_ < 80) return;
        lastVu_ = now;
        updateVu();
        bool changed = false;
        for (int i = 0; i < 9; i++) {
            int l = int(vuL_[i] * 10 + 0.5f), r = int(vuR_[i] * 10 + 0.5f);   // 10 escalones, como en la Session
            if (l != vuShown_[i * 2] || r != vuShown_[i * 2 + 1]) changed = true;
        }
        {   // la carga: se revisa una vez por segundo
            static int tick = 0;
            if (++tick >= 12) {
                tick = 0;
                char lt[24];
                loadText(lt);
                if (atoi(lt + 2) != loadShown) changed = true;
            }
        }
        if (changed) { ClearShapes(); DrawView(); }   // (sin vaciar, las figuras se iban encimando sin fin)
        return;
    }
    DrawVuBars();
}

// trk2als: de 0..1 (pico) a la altura de la barra: en dB, 48 dB de recorrido
static float vuFrac(float p) {
    if (p <= 0.004f) return 0;
    float f = (20.0f * log10f(p) + 48.0f) / 48.0f;
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

void MixerView::updateVu() {
    Player *player = Player::GetInstance();
    // los picos (antes del volumen de la pista; el master, ya con todo)
    float pl[9], pr[9];
    for (int i = 0; i < 9; i++) pl[i] = pr[i] = 0;
    if (player->IsRunning()) {
        MixerService *ms = MixerService::GetInstance();
        for (int i = 0; i < 8; i++) {
            MixBus *bus = ms->GetMixBus(i);
            if (!bus) continue;
            uint32_t level = bus->GetPreMasterVolumePeakLevel();
            pl[i] = (float)((level >> 16) & 0xFFFF) / 32767.0f;
            pr[i] = (float)(level & 0xFFFF) / 32767.0f;
        }
        uint32_t ml = ms->GetMasterPeakLevel();
        pl[8] = (float)((ml >> 16) & 0xFFFF) / 32767.0f;
        pr[8] = (float)(ml & 0xFFFF) / 32767.0f;
    }
    for (int i = 0; i < 9; i++) {   // suben de golpe, bajan suave
        float l = vuFrac(pl[i]), r = vuFrac(pr[i]);
        vuL_[i] = (l > vuL_[i]) ? l : vuL_[i] * 0.85f + l * 0.15f;
        vuR_[i] = (r > vuR_[i]) ? r : vuR_[i] * 0.85f + r * 0.15f;
    }
}

void MixerView::drawStrips() {
    GUIColor bg = ColorOf(CD_BACKGROUND), fg = ColorOf(CD_NORMAL);
    GUIColor panel = Mix(bg, fg, 9), slot = Mix(bg, fg, 16), dim = Mix(bg, fg, 50);
    Player *player = Player::GetInstance();
    Mixer *mixer = Mixer::GetInstance();
    Project *project = viewData_->project_;
    for (int i = 0; i < 9; i++) { vuShown_[i * 2] = int(vuL_[i] * 10 + 0.5f); vuShown_[i * 2 + 1] = int(vuR_[i] * 10 + 0.5f); }
    // (la parte de arriba de la pantalla queda libre: ahí irán los envíos)
    const int PY = 200, PH = 272, TOP = PY + 40, HT = PH - 78;
    // las mismas columnas que la Session (su columna de escenas queda libre)
    const int GAP = 4, X0 = 6 + 60 + GAP + 2;
    int trackCount = project->TrackCount();
    const int NCOL = trackCount > Project::MaxTracks() ? trackCount : Project::MaxTracks();
    const int COLW = (640 - 6 - X0 + GAP) / NCOL;
    const int w = COLW - GAP;
    // el master: en la última columna si está libre; si no, encima de ella con R (L lo quita)
    bool lastFree = !project->TrackExists(NCOL - 1);
    bool master = lastFree || masterOn_;
    const int fw = (w >= 80) ? 32 : 24, mw = (w >= 80) ? 12 : 8;
    const int fx0 = (w - fw - 6 - mw) / 2;
    for (int i = 0; i < NCOL && i < 8; i++) {
        if (master && i == NCOL - 1) break;
        int x = X0 + i * COLW;
        bool exists = project->TrackExists(i);
        bool muted = player->IsChannelMuted(i);
        GUIColor tc = TrackColor(i);
        FillRect(x, PY, w, PH, exists ? panel : Mix(bg, fg, 4), 8);
        if (i == viewData_->mixerCol_) FrameRect(x - 3, PY - 3, w + 6, PH + 6, fg, 10, 3);
        // el nombre: tres letras, en el color de la pista
        char nm[4] = "---";
        if (exists) {
            const char *tn = project->GetTrackName(i);
            int n = 0;
            for (const char *q = tn; q && *q && n < 3; q++)
                if (*q != ' ' && (unsigned char)*q < 0x80) nm[n++] = toupper((unsigned char)*q);
            nm[n] = 0;
            // sin nombre propio ("MIDI 3", "AUDIO 5"): la letra y su número
            if (tn && (!strncmp(tn, "MIDI ", 5) || !strncmp(tn, "AUDIO ", 6))) {
                snprintf(nm, sizeof(nm), "%c%d", tn[0], (i + 1) % 10);
                n = int(strlen(nm));
            }
            FillRect(x + 4, PY + 6, w - 8, 24, tc, 5);
            DrawText(x + (w - 16 * n) / 2 + 1, PY + 10, nm, bg, 1);
        } else DrawText(x + (w - 48) / 2, PY + 10, nm, Mix(bg, fg, 18), 1);
        // el volumen (en dB: 48 dB de recorrido) y, al lado, el medidor
        int vol = mixer->GetChannelVolume(i);
        float db = (vol > 0) ? 20.0f * log10f(vol / 255.0f) : -99.0f;
        float fr = (db + 48.0f) / 48.0f;
        if (fr < 0) fr = 0;
        FillRect(x + fx0, TOP, fw, HT, slot, 5);
        if (!exists) continue;
        int h = int(HT * fr);
        if (h > 0) FillRect(x + fx0, TOP + HT - (h < 10 ? 10 : h), fw, h < 10 ? 10 : h, muted ? Mix(bg, fg, 35) : fg, 5);
        FillRect(x + fx0 + fw + 6, TOP, mw, HT, slot, 3);
        // (a escalones: lo mismo que se compara para decidir si se repinta)
        int vq = vuShown_[i * 2] > vuShown_[i * 2 + 1] ? vuShown_[i * 2] : vuShown_[i * 2 + 1];
        int vh = HT * vq / 10;
        if (vh > 3) FillRect(x + fx0 + fw + 6, TOP + HT - vh, mw, vh, tc, 3);
        if (muted || soloChannel_ == i) {
            // los mismos colores que la M y la S de la Session
            bool solo = (soloChannel_ == i);
            FillRect(x + 6, TOP + HT + 4, w - 12, 22, solo ? ColorOf(CD_HILITE2) : ColorOf(CD_MUTE), 4);
            DrawText(x + (w - 16) / 2 + 1, TOP + HT + 7, solo ? "S" : "M", bg, 1);
        } else {
            char t[8];
            if (vol <= 0) strcpy(t, "off");
            else snprintf(t, sizeof(t), "%+d", int(db < 0 ? db - 0.5f : db + 0.5f));
            DrawText(x + (w - 16 * int(strlen(t))) / 2 + 1, TOP + HT + 7, t, fg, 1);
        }
    }
    {   // la carga del audio (lo que era "rg"), donde la pone la Session
        char lt[24], ld[8];
        loadText(lt);
        int load = atoi(lt + 2);
        loadShown = load;
        if (load >= 100) snprintf(ld, sizeof(ld), "MAX"); else snprintf(ld, sizeof(ld), "%d%%", load);
        bool clip = player->Clipped();
        const char *txt = clip ? "CLP" : ld;
        FillRect(6, 442, 60, 32, panel, 3);
        DrawText(6 + (60 - 16 * int(strlen(txt))) / 2, 442 + 8, txt,
                 (clip || load >= 80) ? ColorOf(CD_MUTE) : dim, 1);
    }
    if (master) {   // el master: sus dos medidores
        int x = X0 + (NCOL - 1) * COLW;
        FillRect(x, PY, w, PH, panel, 8);
        if (!lastFree) FrameRect(x, PY, w, PH, dim, 8, 2);   // encima de una pista: se nota que es otra cosa
        DrawText(x + (w - 48) / 2, PY + 10, "MAS", dim, 1);
        int bw = (w - 30) / 2;
        for (int k = 0; k < 2; k++) {
            int mx = x + 12 + k * (bw + 6);
            FillRect(mx, TOP, bw, HT, slot, 4);
            int vh = HT * vuShown_[16 + k] / 10;
            if (vh > 3) FillRect(mx, TOP + HT - vh, bw, vh, player->Clipped() ? ColorOf(CD_HILITE2) : fg, 4);
        }
    }
}
