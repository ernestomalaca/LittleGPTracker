#ifndef _TRACK_TYPE_DIALOG_H_
#define _TRACK_TYPE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"

// trk2als: elegir el tipo de una pista desde la cabecera de la Song.
// Si el cambio deja celdas que no sirven en el nuevo tipo, pregunta antes de
// borrarlas. GetReturnCode(): -1 = cancelar; tipo | TTD_CLEAR = vaciar antes.

#define TTD_CLEAR 0x100

class TrackTypeDialog : public ModalView {
public:
	TrackTypeDialog(View &view, int channel, int current, bool hasContent) ;
	virtual ~TrackTypeDialog() ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
private:
	int channel_ ;
	int current_ ;
	bool hasContent_ ;
	int selected_ ;
	bool confirming_ ;   // segunda página: ¿borrar las celdas?
	bool yes_ ;
} ;
#endif
