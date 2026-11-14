#ifndef _TRACK_TYPE_DIALOG_H_
#define _TRACK_TYPE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"

// trk2als: ventana de la cabecera de la Song. En el '+': elegir si la pista
// nueva es MIDI o de audio. En una pista: borrarla (con confirmación).
// Como en Live, una pista no cambia de tipo: se borra y se agrega otra.
// GetReturnCode(): -1 = cancelar; TT_MIDI / TT_AUDIO = agregar; TTD_DELETE = borrar

#define TTD_DELETE 0x200

class TrackTypeDialog : public ModalView {
public:
	// adding: ventana del '+'; si no, de la pista 'channel' llamada 'name'
	TrackTypeDialog(View &view, int channel, bool adding, const char *name) ;
	virtual ~TrackTypeDialog() ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
private:
	int channel_ ;
	bool adding_ ;
	char name_[24] ;
	int selected_ ;
	bool confirming_ ;   // segunda página: ¿borrar la pista?
	bool yes_ ;
} ;
#endif
