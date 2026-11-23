#ifndef _UI_STATIC_FIELD_H_
#define _UI_STATIC_FIELD_H_

#include "UIField.h"

class UIStaticField: public UIField {

public:

	UIStaticField(GUIPoint &position,const char *string) ;
	virtual ~UIStaticField() {} ;
	virtual void Draw(GUIWindow &w, int offset=0) ;
	virtual void ProcessArrow(unsigned short mask) ;
	virtual void OnClick() {} ;

	virtual bool IsStatic() ;
	const char *Text() { return string_ ; }   // trk2als

protected:

protected:
	const char *string_ ;

} ;

#endif
