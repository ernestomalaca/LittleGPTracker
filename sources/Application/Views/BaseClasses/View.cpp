#include "View.h"
#include <string.h>
#include "System/Console/Trace.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "ModalView.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Model/Scale.h"

bool View::initPrivate_=false ;

int View::margin_=0 ;
int View::songRowCount_; //=21 sets screen height among other things
bool View::miniLayout_=false ;
int View::altRowNumber_ = 4;

View::View(GUIWindow &w,ViewData *viewData):
	w_(w),
	modalView_(0),
	modalViewCallback_(0),
	hasFocus_(false),
	topTab_(-2),
	topRunning_(false),
	topLinks_(-1),
	topTempo_(0)
{
  if (!initPrivate_) 
  {
	   GUIRect rect=w.GetRect() ;
     miniLayout_=(rect.Width()<320);
	   View::margin_=0 ;
		// trk2als: la Session es una rejilla de bloques grandes de 8 escenas
		// (SESSIONROWS en config.xml: 8 o 12)
		{
			const char *sr=Config::GetInstance()->GetValue("SESSIONROWS") ;
			int rows=sr?atoi(sr):8 ;
			if (rows!=12) rows=8 ;
			songRowCount_ = miniLayout_ ? 16:rows;
		}

		const char *altRowStr = Config::GetInstance()->GetValue("ALTROWNUMBER");
		if (altRowStr) {
			altRowNumber_ = atoi(altRowStr);
		}

     initPrivate_=true ;
  }
	mask_=0 ;
	viewMode_=VM_NORMAL ;
	locked_=false ;
	viewData_=viewData;
	NOTIFICATION_TIMEOUT = 1000;
	displayNotification_ = "";
    // Initialize VU meter inertia tracking
    for (int i = 0; i < 8; i++) {
        prevLeftVU_[i] = 0;
        prevRightVU_[i] = 0;
    }
} ;

GUIPoint View::GetAnchor() {
	int width=40 ;
	int height=30 ;
	return GUIPoint((width-SONG_CHANNEL_COUNT*3)/2+2,(height-View::songRowCount_)/2) ;
}

GUIPoint View::GetTitlePosition() {
#ifndef PLATFORM_CAANOO
	return GUIPoint(0,0) ;
#else
	return GUIPoint(0,1) ;
#endif
} ;

bool View::Lock() {
	if (locked_) return false ;
	locked_=true ;
	return true ;
} ;

void View::WaitForObject() {
	while (locked_) {} ;
}

void View::Unlock() {
	locked_=false ;
}

void View::drawMap() {
    // trk2als: el mapa de pantallas se quitó para ganar espacio;
    // SCREENMAP=YES en config.xml lo vuelve a mostrar
    static int showMap=-1 ;
    if (showMap<0) {
        const char *s=Config::GetInstance()->GetValue("SCREENMAP") ;
        showMap=(s && !strcmp(s,"YES"))?1:0 ;
    }
    if (!showMap) return ;
    if (!miniLayout_) {
        GUIPoint anchor=GetAnchor() ;
		GUIPoint pos(View::margin_,anchor._y);
    	GUITextProperties props ;

		//draw entire map
		SetColor(CD_HILITE1) ;
    	char buffer[5] ;
		props.invert_=true ;
		//row1
		sprintf(buffer,"P G ");
        DrawString(pos._x,pos._y,buffer,props) ;
		pos._y++ ;		
		//row2
		sprintf(buffer,"SCPI");
        DrawString(pos._x,pos._y,buffer,props) ;
		pos._y++ ;		
		//row3
        sprintf(buffer, " MTT");
        DrawString(pos._x,pos._y,buffer,props) ;

		//draw current screen on map
		SetColor(CD_HILITE2) ;
		pos._y = anchor._y;
		switch(viewType_)
		{
		case VT_PROJECT:
	        DrawString(pos._x,pos._y,"P",props) ;
			break;
		case VT_INSTRUMENT:
			pos._x+=3;
			pos._y+=1;
	        DrawString(pos._x,pos._y,"I",props) ;
			break;
		case VT_TABLE: //under phrase
			pos._x+=2;
			pos._y+=2;
	        DrawString(pos._x,pos._y,"T",props) ;
			break;
		case VT_TABLE2: //under instrument
			pos._x+=3;
			pos._y+=2;
	        DrawString(pos._x,pos._y,"T",props) ;
			break;
		case VT_GROOVE:
			pos._x+=2;
	        DrawString(pos._x,pos._y,"G",props) ;
			break;
        case VT_MIXER:
            pos._x+=1;
			pos._y+=2;
	        DrawString(pos._x,pos._y,"M",props) ;
			break;
		default: //VT_SONG
			pos._y+=1;
	        DrawString(pos._x,pos._y,"S",props) ;
			int foo=0;
		}

	}//!minilayout
}

void View::drawNotes(int x0, int dx) {

    if (!miniLayout_) {

		refreshTopBar() ;
		GUIPoint anchor=GetAnchor() ;
		int initialX = (x0 >= 0) ? x0 : View::margin_+10 ;
		int initialY = anchor._y+23 ;
		GUIPoint pos(initialX,initialY) ;
		GUITextProperties props ;

        Player *player=Player::GetInstance() ;
		
		//column banger refactor
		props.invert_= true;
        for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
			if (i==viewData_->songX_) {
				SetColor(CD_HILITE2) ;
			} else {
				SetColor(CD_HILITE1) ;
			}
			if (player->IsRunning() && viewData_->playMode_ != PM_AUDITION) {
				DrawString(pos._x,pos._y,player->GetPlayedNote(i),props) ; //row for the note values
				pos._y++ ;
				DrawString(pos._x,pos._y,player->GetPlayedOctive(i),props) ; //row for the octive values
				// trk2als: el número de instrumento ya no (la pista lo dice)
			} else {
				DrawString(pos._x,pos._y,"  ",props) ; //row for the note values
				pos._y++ ;
				DrawString(pos._x,pos._y,"  ",props) ; //row for the octive values
			}
			pos._y = initialY ;
			pos._x+= dx;
		}
     }
}

void View::DoModal(ModalView *view,ModalViewCallback cb) {
	modalView_=view ;
	modalView_->OnFocus() ;
	modalViewCallback_=cb ;
	isDirty_=true ;
} ;

void View::Redraw() {
	AppWindow &aw=(AppWindow &)w_ ;
	int layer=aw.ShapeLayer() ;
	if (modalView_) {
		if (isDirty_) {
			ClearShapes() ;
			DrawView() ;
		}
		// trk2als: la ventana dibuja sus figuras en su propia capa, encima
		aw.SetShapeLayer(layer+1) ;
		modalView_->Redraw() ;
		aw.SetShapeLayer(layer) ;
	} else {
		ClearShapes() ;   // trk2als: cada vista vuelve a poner sus figuras
		DrawView() ;
		aw.TrimShapeLayers(layer) ;   // las ventanas que ya se cerraron
	}
	isDirty_=false ;
} ;

bool View::ModalChainShapes() {
	if (!modalView_) return true ;
	return modalView_->UsesShapes() && modalView_->ModalChainShapes() ;
}

void View::SetDirty(bool isDirty) {
	isDirty_=true ;
} ;

void View::ProcessButton(unsigned short mask, bool pressed) {
	isDirty_=false ;
	if (modalView_) {
		modalView_->ProcessButton(mask,pressed);
		modalView_->isDirty_;
		if (modalView_->IsFinished()) {
			// process callback sending the modal dialog
			// trk2als: el callback puede abrir otra ventana (DoModal): esa se queda
			ModalView *done=modalView_ ;
			if (modalViewCallback_) {
				modalViewCallback_(*this,*done) ;
			}
			if (modalView_==done) modalView_=0 ;
			delete done ;
			isDirty_=true ;
		}
	} else {
		ProcessButtonMask(mask,pressed);
	}
	if (isDirty_) ((AppWindow &)w_).SetDirty() ;
} ;

void View::Clear() {
	((AppWindow &)w_).Clear() ;
}

void View::SetColor(ColorDefinition cd) {
	((AppWindow &)w_).SetColor(cd) ;
} ;

void View::ClearRect(int x,int y,int w,int h) {
	GUIRect rect(x,y,(x+w),(y+h)) ;
	w_.ClearRect(rect) ;
} ;

// trk2als: pinceles
bool View::SupportsShapes() { return ((AppWindow &)w_).SupportsShapes(); }
void View::ClearShapes() { ((AppWindow &)w_).ClearShapes(); }
void View::FillRect(int x, int y, int w, int h, GUIColor c, int radius) {
	((AppWindow &)w_).AddRect(x, y, w, h, c, radius, 0);
}
void View::FrameRect(int x, int y, int w, int h, GUIColor c, int radius, int thick) {
	((AppWindow &)w_).AddRect(x, y, w, h, c, radius, thick < 1 ? 1 : thick);
}
void View::DrawText(int x, int y, const char *txt, GUIColor c, int scale) {
	((AppWindow &)w_).AddText(x, y, txt, c, scale);
}
GUIColor View::LinkGreen() { return GUIColor(0x3E, 0xCF, 0xAA); }   // el verde de Link

GUIColor View::ColorOf(ColorDefinition cd) { return ((AppWindow &)w_).GetColor(cd); }
GUIColor View::Mix(GUIColor a, GUIColor b, int t) {
	// t en porcentaje: 0 = a, 100 = b
	return GUIColor((a._r * (100 - t) + b._r * t) / 100, (a._g * (100 - t) + b._g * t) / 100,
	                (a._b * (100 - t) + b._b * t) / 100);
}

void View::DrawString(int x,int y,const char *txt,GUITextProperties &props) {
	GUIPoint pos(x,y) ;
	w_.DrawString(txt,pos,props) ;
} ;

/*
	Displays the saved notification for 1 second
*/
void View::EnableNotification() {
	if ((SDL_GetTicks() - notificationTime_) <= NOTIFICATION_TIMEOUT) {
		SetColor(CD_NORMAL);
		GUITextProperties props;
        int xOffset = 4;
        // support single-line or two-line notifications separated by '\n'
        size_t nl = displayNotification_.find('\n');
		if (nl == std::string::npos) {
			DrawString(xOffset, notiDistY_, displayNotification_.c_str(), props);
		} else {
			std::string line1 = displayNotification_.substr(0, nl);
			std::string line2 = displayNotification_.substr(nl + 1);
			DrawString(xOffset, notiDistY_, line1.c_str(), props);
			DrawString(xOffset, notiDistY_ + 1, line2.c_str(), props);
		}
    } else {
		displayNotification_ = "";
	}
}

/*
    Set displayed notification
    Saves the current time
    Optionally set display y offset if not in a project (default == 2)
    Allows negative offsets, use with care!
*/
void View::SetNotification(const char *notification, int offset) {
    notificationTime_ = SDL_GetTicks();
    displayNotification_ = notification;
    notiDistY_ = offset;
    isDirty_ = true;
}

void View::drawVUMeter(uint8_t leftBars, uint8_t rightBars, GUIPoint pos,
                       GUITextProperties props, int vuIndex, bool forceRedraw) {
    // Clamp bars to VU meter height
    leftBars = (leftBars > VU_METER_HEIGHT) ? VU_METER_HEIGHT : leftBars;
    rightBars = (rightBars > VU_METER_HEIGHT) ? VU_METER_HEIGHT : rightBars;

    // Apply inertia effect: fast rise, slow fall
    const int maxStepUp = 2;    // fast rise (up to 2 bars per update)
    const int maxStepDown = 1;  // slow fall (only 1 bar per update)

    // Left channel inertia
    if (leftBars > prevLeftVU_[vuIndex]) {
        // Rising: allow faster response (up to maxStepUp)
        int diff = leftBars - prevLeftVU_[vuIndex];
        if (diff > maxStepUp) {
            leftBars = prevLeftVU_[vuIndex] + maxStepUp;
        }
    } else if (leftBars < prevLeftVU_[vuIndex]) {
        // Falling: limit to maxStepDown for inertia
        int diff = prevLeftVU_[vuIndex] - leftBars;
        if (diff > maxStepDown) {
            leftBars = prevLeftVU_[vuIndex] - maxStepDown;
        }
    }

    // Right channel inertia
    if (rightBars > prevRightVU_[vuIndex]) {
        int diff = rightBars - prevRightVU_[vuIndex];
        if (diff > maxStepUp) {
            rightBars = prevRightVU_[vuIndex] + maxStepUp;
        }
    } else if (rightBars < prevRightVU_[vuIndex]) {
        int diff = prevRightVU_[vuIndex] - rightBars;
        if (diff > maxStepDown) {
            rightBars = prevRightVU_[vuIndex] - maxStepDown;
        }
    }

    // Skip redraw if values unchanged (unless forced)
    if (!forceRedraw && leftBars == prevLeftVU_[vuIndex] &&
        rightBars == prevRightVU_[vuIndex]) {
        return;
    }

    // Store current values for next update
    prevLeftVU_[vuIndex] = leftBars;
    prevRightVU_[vuIndex] = rightBars;

    // Draw the VU meter bars (simple bar representation)
    std::string bar = "";
    for (int i = 0; i < VU_METER_HEIGHT; i++) {
        if (i < leftBars) {
            bar += "|";
        } else {
            bar += " ";
        }
    }
    bar += " ";
    for (int i = 0; i < VU_METER_HEIGHT; i++) {
        if (i < rightBars) {
            bar += "|";
        } else {
            bar += " ";
        }
    }
    DrawString(pos._x, pos._y, bar.c_str(), props);
}


/************************************************************
 trk2als: pinceles compartidos: barra superior y barra de ayuda
 ************************************************************/

std::string View::projectName_ ;
unsigned short View::pressedButton_=0 ;
unsigned short View::physMask_=0 ;

void View::refreshTopBar() {
	if (topTab_<-1) return ;
	Player *player=Player::GetInstance() ;
	bool running=player && player->IsRunning() ;
	int links=(player && player->LinkOn())?player->LinkPeers():-1 ;
	if (running!=topRunning_ || viewData_->project_->GetTempo()!=topTempo_ || links!=topLinks_) {
		std::string ctx=topCtx_ ;
		drawTopBar(topTab_,ctx.c_str()) ;
	}
}

bool View::drawTopBar(int tab, const char *context) {
	if (miniLayout_ || !SupportsShapes()) { topTab_=-2 ; return false ; }
	topTab_=tab ;
	topCtx_=context?context:"" ;
	// trk2als: barra de 32 px (dos renglones de la rejilla) con la letra
	// grande: 16 px por letra, 40 letras a lo ancho
	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	GUIColor dim=Mix(bg,fg,55) ;
	const int H=32 ;
	const int TY=8 ;   // renglón del texto
	FillRect(0,0,640,H-2,Mix(bg,fg,10)) ;
	FillRect(0,H-2,640,2,Mix(bg,fg,20)) ;
	int x=8 ;
	// el proyecto (8 letras)
	std::string set=projectName_.empty()?"CHANCHO6":projectName_ ;
	for (size_t i=0;i<set.size();i++) if (set[i]>='a' && set[i]<='z') set[i]=set[i]-'a'+'A' ;
	// trk2als: en el Proyecto (tab<0) va el nombre completo y sin pestañas
	if (set.size()>(tab<0?26u:8u)) set=set.substr(0,tab<0?26:8) ;
	DrawText(x,TY,set.c_str(),hi2,1) ;
	x+=16*int(set.size())+12 ;
	// pestañas de tres letras
	static const char *tabs[4]={"SES","CLP","INS","MIX"} ;
	for (int t=0;t<4 && tab>=0;t++) {
		int w=16*3+8 ;
		if (t==tab) {
			FillRect(x,3,w,H-8,hi2,3) ;
			DrawText(x+4,TY,tabs[t],bg,1) ;
		} else {
			DrawText(x+4,TY,tabs[t],dim,1) ;
		}
		x+=w+2 ;
	}
	x+=8 ;
	// a la derecha: tempo y reproducción
	Player *player=Player::GetInstance() ;
	char right[16] ;
	snprintf(right,sizeof(right),"%d",viewData_->project_->GetTempo()) ;
	int rx=640-8-16*int(strlen(right)) ;
	bool running=player && player->IsRunning() ;
	topRunning_=running ;
	topTempo_=viewData_->project_->GetTempo() ;
	int px=rx-20 ;
	if (running) {   // triángulo de play
		for (int k=0;k<12;k++) FillRect(px+k,9+k/2,1,13-k,ColorOf(CD_PLAY)) ;
	} else {         // cuadrado de stop
		FillRect(px,10,11,11,dim) ;
	}
	DrawText(rx,TY,right,fg,1) ;
	// trk2als: Ableton Link: "1 LINK" en verde cuando hay alguien; si está
	// prendido y buscando, solo el contorno
	topLinks_=-1 ;
	if (player && player->LinkOn()) {
		int n=player->LinkPeers() ;
		topLinks_=n ;
		char lk[16] ;
		if (n>0) snprintf(lk,sizeof(lk),"%d LINK%s",n,n==1?"":"S") ;
		else snprintf(lk,sizeof(lk),"LINK") ;
		int lw=8*int(strlen(lk))+14 ;
		px-=lw+10 ;
		if (n>0) {
			FillRect(px,6,lw,19,LinkGreen(),4) ;
			DrawText(px+7,TY+2,lk,bg,0) ;
		} else {
			FrameRect(px,6,lw,19,Mix(bg,LinkGreen(),55),4,2) ;
			DrawText(px+7,TY+2,lk,Mix(bg,LinkGreen(),70),0) ;
		}
	}
	// el contexto (p. ej. el kit), en letra chica en lo que queda
	if (context && *context) {
		int room=(px-8-x)/8 ;
		std::string c=context ;
		if (room>0 && (int)c.size()>room) c=c.substr(0,room) ;
		if (room>0) DrawText(x,TY+2,c.c_str(),fg,0) ;
	}
	return true ;
}

bool View::drawHelpBar(const char *items) {
	// trk2als: ya no se dibuja abajo; AppWindow la muestra como recuadro
	// cuando uno se queda quieto (HELPIDLE en config.xml)
	if (miniLayout_ || !SupportsShapes() || !items) return false ;
	helpItems_=items ;
	return true ;
}

/******************************************************
 trk2als: Fit to Scale. Al volver a la Song o al piano roll después de
 cambiar la escala o la raíz, si hay notas fuera de la nueva escala se
 pregunta si se pasan a la nota más cercana (no se puede deshacer)
 ******************************************************/

static void FitScaleCallback(View &v, ModalView &dialog) {
    v.FitScaleAnswer(dialog.GetReturnCode() == MBL_YES);
}

void View::checkScaleChange() {
    Project *project = viewData_->project_;
    if (!project || !project->ScaleChanged() || modalView_) return;
    int n = project->OutOfScaleNotes();
    if (n == 0) {
        project->AcceptScale();
        return;
    }
    int scale = project->GetScale(), root = project->GetScaleRoot();
    char buf[160];
    snprintf(buf, sizeof(buf), "Pasar las notas a %s %s?\n%d notas quedaron fuera\nNo se puede deshacer",
             scaleNotes[root], scaleNames[scale], n);
    MessageBox *mb = new MessageBox(*this, buf, MBBF_YES | MBBF_NO);
    DoModal(mb, FitScaleCallback);
}

void View::FitScaleAnswer(bool yes) {
    Project *project = viewData_->project_;
    if (yes) {
        int n = project->FitToScale();
        char buf[48];
        snprintf(buf, sizeof(buf), "%d notas ajustadas", n);
        View::SetNotification(buf);
    }
    project->AcceptScale();
    isDirty_ = true;
}
