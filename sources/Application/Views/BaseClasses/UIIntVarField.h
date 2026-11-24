#ifndef _UI_INT_VAR_FIELD_H_
#define _UI_INT_VAR_FIELD_H_

#include "UIField.h"
#include "Foundation/Variables/Variable.h"

class UIIntVarField: public UIField {

public:

	UIIntVarField(
    GUIPoint &position,
    Variable &v,
    const char *format,
    int min,
    int max,
    int xOffset,
    int yOffset,
    int displayOffset = 0);
  
	virtual ~UIIntVarField() {} ;
	virtual void Draw(GUIWindow &w,int offset=0) ;
	virtual void ProcessArrow(unsigned short mask) ;
	virtual void OnClick() {} ;
  
  FourCC GetVariableID() ;
  // trk2als: el texto que dibujaría (para dibujarlo de otra forma) y su rango
  void Text(char *buffer, int size) ;
  int Min() { return min_ ; }
  int Max() { return max_ ; }
	Variable &GetVariable() ;
	const char *GetFormat() { return format_ ; }
	int GetDrawnLength() { return drawnLen_ ; }
	// trk2als: para dibujar su curva en el clip
	int GetMin() { return min_ ; }
	int GetMax() { return max_ ; }
	int GetStep() { return xOffset_ ; }
	int GetBigStep() { return yOffset_ ; }
	int GetDisplayOffset() { return displayOffset_ ; }
	virtual bool IsLog() { return false ; }   // la frecuencia del filtro   // trk2als: cuántas letras se dibujaron (el puntito va después)
protected:
	Variable &src_ ;
	const char *format_ ;
	int min_ ;
	int max_ ;
	int xOffset_ ;
	int yOffset_ ;
  int displayOffset_;
	int drawnLen_ ;
} ;

#endif
