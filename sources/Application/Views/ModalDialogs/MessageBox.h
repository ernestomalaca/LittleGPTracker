#ifndef _MESSAGE_BOX_H_
#define _MESSAGE_BOX_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>

#ifdef MessageBox
#undef MessageBox
#endif

enum MessageBoxList {
	MBL_OK=0,
	MBL_YES,
	MBL_CANCEL,
	MBL_NO,
	MBL_LAST
} ; 

enum MessageBoxButtonFlag {
	MBBF_OK=1,
	MBBF_YES=2,
	MBBF_CANCEL=4,
	MBBF_NO=8
} ;

class MessageBox:public ModalView {
public:
	MessageBox(View &view,const char *message,int btnFlags=MBBF_OK) ;
	virtual ~MessageBox() ;

	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual bool UsesShapes() { return SupportsShapes() ; }
	// trk2als: título en el color de acento (si no, la primera línea de un
	// mensaje de varias líneas hace de título) y nombres propios a los botones
	MessageBox &Title(const char *t) { title_=t ; return *this ; }
	MessageBox &Label(int which,const char *text) ;
private:
	void drawShapes() ;
	std::string title_ ;
	std::string label_[MBL_LAST] ;
	std::string message_ ;	
	int button_[4] ;
	int buttonCount_ ;
	int selected_ ;
} ;
#endif