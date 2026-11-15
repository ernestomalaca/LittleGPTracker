#include "View.h"
#include <string.h>
#include "System/Console/Trace.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "ModalView.h"

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
	topTempo_(0)
{
  if (!initPrivate_) 
  {
	   GUIRect rect=w.GetRect() ;
     miniLayout_=(rect.Width()<320);
	   View::margin_=0 ;
		songRowCount_ = miniLayout_ ? 16:22; // 22 is row display count among other things

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
		case VT_CHAIN:
			pos._x+=1;
			pos._y+=1;
	        DrawString(pos._x,pos._y,"C",props) ;
			break;
		case VT_PHRASE:
			pos._x+=2;
			pos._y+=1;
	        DrawString(pos._x,pos._y,"P",props) ;
			break;
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
	if (modalView_) {
		if (isDirty_) {
			ClearShapes() ;
			DrawView() ;
		}
		modalView_->Redraw() ;
	} else {
		ClearShapes() ;   // trk2als: cada vista vuelve a poner sus figuras
		DrawView() ;
	}
	isDirty_=false ;
} ;

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

void View::refreshTopBar() {
	if (topTab_<-1) return ;
	Player *player=Player::GetInstance() ;
	bool running=player && player->IsRunning() ;
	if (running!=topRunning_ || viewData_->project_->GetTempo()!=topTempo_) {
		std::string ctx=topCtx_ ;
		drawTopBar(topTab_,ctx.c_str()) ;
	}
}

bool View::drawTopBar(int tab, const char *context) {
	if (miniLayout_ || !SupportsShapes()) { topTab_=-2 ; return false ; }
	topTab_=tab ;
	topCtx_=context?context:"" ;
	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi=ColorOf(CD_HILITE1) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	GUIColor dim=Mix(bg,fg,55) ;
	const int H=26 ;
	FillRect(0,0,640,H,Mix(bg,fg,10)) ;
	FillRect(0,H,640,1,Mix(bg,fg,20)) ;
	int x=10 ;
	// el set
	std::string set=projectName_.empty()?"CHANCHO 6":projectName_ ;
	for (size_t i=0;i<set.size();i++) if (set[i]>='a' && set[i]<='z') set[i]=set[i]-'a'+'A' ;
	if (set.size()>12) set=set.substr(0,12) ;
	DrawText(x,7,set.c_str(),hi2,0) ;
	x+=8*int(set.size())+14 ;
	// pestañas
	static const char *tabs[4]={"SES","CLIP","INST","MIX"} ;
	for (int t=0;t<4;t++) {
		int w=8*int(strlen(tabs[t]))+10 ;
		if (t==tab) {
			FillRect(x,4,w,H-8,hi2,3) ;
			DrawText(x+5,7,tabs[t],bg,0) ;
		} else {
			DrawText(x+5,7,tabs[t],dim,0) ;
		}
		x+=w+4 ;
	}
	x+=10 ;
	// a la derecha: tempo y reproducción
	Player *player=Player::GetInstance() ;
	char right[32] ;
	snprintf(right,sizeof(right),"%d BPM",viewData_->project_->GetTempo()) ;
	int rx=640-10-8*int(strlen(right)) ;
	bool running=player && player->IsRunning() ;
	topRunning_=running ;
	topTempo_=viewData_->project_->GetTempo() ;
	int px=rx-22 ;
	if (running) {   // triángulo de play
		for (int k=0;k<10;k++) FillRect(px+k,8+k/2,1,10-k,ColorOf(CD_PLAY)) ;
	} else {         // cuadrado de stop
		FillRect(px,8,9,9,dim) ;
	}
	DrawText(rx,7,right,dim,0) ;
	// el contexto, en lo que queda
	if (context && *context) {
		int room=(px-10-x)/8 ;
		std::string c=context ;
		if (room>0 && (int)c.size()>room) c=c.substr(0,room) ;
		if (room>0) DrawText(x,7,c.c_str(),fg,0) ;
	}
	return true ;
}

bool View::drawHelpBar(const char *items) {
	if (miniLayout_ || !SupportsShapes() || !items) return false ;
	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor dim=Mix(bg,fg,55) ;
	const int Y=480-16 ;
	FillRect(0,Y,640,16,Mix(bg,fg,10)) ;
	FillRect(0,Y-1,640,1,Mix(bg,fg,20)) ;
	int x=10 ;
	std::string all=items ;
	size_t pos=0 ;
	while (pos<all.size()) {
		size_t end=all.find(';',pos) ;
		if (end==std::string::npos) end=all.size() ;
		std::string item=all.substr(pos,end-pos) ;
		pos=end+1 ;
		size_t colon=item.find(':') ;
		std::string key=(colon==std::string::npos)?item:item.substr(0,colon) ;
		std::string what=(colon==std::string::npos)?"":item.substr(colon+1) ;
		int w=8*int(key.size()+1+what.size()) ;
		if (x+w>630) break ;
		DrawText(x,Y+2,key.c_str(),fg,0) ;
		DrawText(x+8*int(key.size()+1),Y+2,what.c_str(),dim,0) ;
		x+=w+16 ;
	}
	return true ;
}
