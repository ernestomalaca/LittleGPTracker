#include "MessageBox.h"
#include "Application/Utils/Latin1.h"
#include <vector>
#include <string>
#include <string.h>

static const char *buttonText[MBL_LAST] = {
	"Va",
	"Sí",
	"Cancelar",
	"No"
} ;

MessageBox::MessageBox(View &view,const char *message,int btnFlags):
	ModalView(view),
	message_(message) {

	buttonCount_=0 ;
	for (int i=0;i<MBL_LAST;i++) {
		if (btnFlags&(1<<(i))) {
			button_[buttonCount_]=i ;
			buttonCount_++ ;
		}
	}
	selected_=buttonCount_-1 ;
	NAssert(buttonCount_!=0) ;
	for (int i=0;i<MBL_LAST;i++) label_[i]=buttonText[i] ;
} ;

MessageBox &MessageBox::Label(int which,const char *text) {
	if (which>=0 && which<MBL_LAST) label_[which]=text ;
	return *this ;
}

MessageBox::~MessageBox() {
} ;

void MessageBox::DrawView() {
	if (SupportsShapes()) { drawShapes() ; return ; }
// message size (trk2als: puede tener varias líneas, separadas por '\n')
	std::vector<std::string> lines ;
	{
		std::string rest=message_ ;
		size_t nl ;
		while ((nl=rest.find('\n'))!=std::string::npos) {
			lines.push_back(rest.substr(0,nl)) ;
			rest=rest.substr(nl+1) ;
		}
		lines.push_back(rest) ;
	}
	int size=0 ;
	for (size_t i=0;i<lines.size();i++) if ((int)lines[i].size()>size) size=lines[i].size() ;

// compute space needed for buttons
// and set window size

	int btnSize=5 ;
	int width=buttonCount_*(btnSize+1)+1 ;
	width=(size>width)?size:width ;
	SetWindow(width,2+lines.size()) ;

// draw text

	int y=0 ;
	GUITextProperties props ;
	SetColor(CD_NORMAL) ;
	for (size_t i=0;i<lines.size();i++) {
		int x=(width-lines[i].size())/2;
		DrawString(x,y++,lines[i].c_str(),props) ;
	}
	
	y=lines.size()+1 ;
	int x ;
	int offset=width/(buttonCount_+1) ;

	for (int i=0;i<buttonCount_;i++) {
		const char *text=label_[button_[i]].c_str() ;
		x=offset*(i+1)-strlen(text)/2 ;
		props.invert_=(i==selected_)?true:false ;
		DrawString(x,y,text,props) ;
	}	
} ;

void MessageBox::OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) {
} ;
void MessageBox::OnFocus() {
} ;
void MessageBox::ProcessButtonMask(unsigned short mask,bool pressed) {
	if (!pressed) return ; // trk2als: soltar botones que abrieron el diálogo no lo cierra
	if (mask&EPBM_A) {
		EndModal(button_[selected_]) ;
	}
	// trk2als: izquierda / derecha como se ven los botones
	if (mask&EPBM_RIGHT) {
		selected_=(selected_+1) ;
		if (selected_>=buttonCount_) {
			selected_=0 ;
		}
	} 
	if (mask&EPBM_LEFT) {
		selected_=(selected_-1) ;
		if (selected_<0) {
			selected_=buttonCount_-1 ;
		}
	} 
	isDirty_=true ;
} ;


// trk2als: la ventana como la de la ayuda: fondo oscuro, borde claro, el
// título en el color de acento, el texto en claro y los botones como
// pastillas (la elegida, rellena)
void MessageBox::drawShapes() {
	std::vector<std::string> lines ;
	{
		std::string rest=message_ ;
		size_t nl ;
		while ((nl=rest.find('\n'))!=std::string::npos) {
			lines.push_back(rest.substr(0,nl)) ;
			rest=rest.substr(nl+1) ;
		}
		lines.push_back(rest) ;
	}
	std::string title=title_ ;
	if (!title.empty() && message_.empty()) lines.clear() ;   // solo el título
	if (title.empty() && lines.size()>1) {
		title=lines[0] ;
		lines.erase(lines.begin()) ;
	}
	const int PAD=32, LINE=24, BH=36, GAP=16 ;
	// el título puede tener varias líneas (todas en el color de acento)
	std::vector<std::string> tl ;
	{
		std::string rest=title ;
		size_t nl ;
		while ((nl=rest.find('\n'))!=std::string::npos) { tl.push_back(rest.substr(0,nl)) ; rest=rest.substr(nl+1) ; }
		if (!rest.empty()) tl.push_back(rest) ;
	}
	int textW=0 ;
	for (size_t i=0;i<tl.size();i++) if (TextLen(tl[i])*16>textW) textW=TextLen(tl[i])*16 ;
	for (size_t i=0;i<lines.size();i++)
		if (TextLen(lines[i])*16>textW) textW=TextLen(lines[i])*16 ;
	int bw[4], btnW=0 ;
	for (int i=0;i<buttonCount_;i++) {
		bw[i]=(TextLen(label_[button_[i]])+2)*16 ;
		if (bw[i]<96) bw[i]=96 ;
		btnW+=bw[i]+(i?GAP:0) ;
	}
	int w=(textW>btnW?textW:btnW)+2*PAD ;
	if (w<352) w=352 ;
	if (w>624) w=624 ;
	int h=PAD+(tl.empty()?0:(int)tl.size()*LINE+8)+(int)lines.size()*LINE+16+BH+PAD ;
	int x=(640-w)/2, y=(480-h)/2 ;
	DrawPanel(x,y,w,h) ;

	GUIColor bg=ColorOf(CD_BACKGROUND), fg=ColorOf(CD_NORMAL) ;
	GUIColor acc=ColorOf(CD_HILITE2), dim=Mix(bg,fg,40) ;
	int ty=y+PAD ;
	for (size_t i=0;i<tl.size();i++) {
		DrawText(x+(w-TextLen(tl[i])*16)/2,ty,tl[i].c_str(),acc,1) ;
		ty+=LINE ;
	}
	if (!tl.empty()) ty+=8 ;
	for (size_t i=0;i<lines.size();i++) {
		DrawText(x+(w-TextLen(lines[i])*16)/2,ty,lines[i].c_str(),fg,1) ;
		ty+=LINE ;
	}
	// todo centrado: título, texto y botones
	int bx=x+(w-btnW)/2, by=y+h-PAD-BH ;
	for (int i=0;i<buttonCount_;i++) {
		const std::string &t=label_[button_[i]] ;
		int tx=bx+(bw[i]-TextLen(t)*16)/2, tyb=by+(BH-16)/2 ;
		if (i==selected_) {
			FillRect(bx,by,bw[i],BH,acc,6) ;
			DrawText(tx,tyb,t.c_str(),bg,1) ;
		} else {
			FrameRect(bx,by,bw[i],BH,dim,6,2) ;
			DrawText(tx,tyb,t.c_str(),fg,1) ;
		}
		bx+=bw[i]+GAP ;
	}
}
