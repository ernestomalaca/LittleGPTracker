#ifndef _TRACK_TYPE_DIALOG_H_
#define _TRACK_TYPE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"

// trk2als: ventana de la cabecera de la Song. En el '+': elegir si la pista
// nueva es MIDI o de audio. En una pista MIDI: cargar un preset en su
// instrumento, editarlo o borrar la pista; en una de audio, borrarla.
// Como en Live, una pista no cambia de tipo: se borra y se agrega otra.
// GetReturnCode(): -1 = cancelar; TT_MIDI / TT_AUDIO = agregar;
// TTD_DELETE / TTD_LOAD / TTD_EDIT

#define TTD_DELETE 0x200
#define TTD_LOAD 0x300
#define TTD_EDIT 0x400
#define TTD_NEWKIT 0x500   // el instrumento de la pista pasa a un kit vacío

class TrackTypeDialog : public ModalView {
public:
	// adding: ventana del '+'; si no, de la pista 'channel' llamada 'name'
	TrackTypeDialog(View &view, int channel, bool adding, const char *name, bool midi) ;
	virtual ~TrackTypeDialog() ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
private:
	int channel_ ;
	bool adding_ ;
	bool midi_ ;
	int count_ ;          // opciones de la pista
	char name_[24] ;
	int selected_ ;
	bool confirming_ ;   // segunda página: ¿borrar la pista?
	bool yes_ ;
} ;
#endif
