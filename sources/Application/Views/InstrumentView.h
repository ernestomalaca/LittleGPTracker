#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"

class InstrumentView: public FieldView, public I_Observer {
public:
	InstrumentView(GUIWindow &w,ViewData *data) ;
	virtual ~InstrumentView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() ;
	virtual void AnimationUpdate() ;

protected:
	void warpToNext(int offset) ;
	void onInstrumentChange() ;
	void fillSampleParameters() ;
	void fillMidiParameters() ;
	InstrumentType getInstrumentType() ;
	void Update(Observable &o,I_ObservableData *d) ;

private:
	Project *project_ ;
	FourCC lastFocusID_ ;
	I_Instrument *current_ ;
	int lastSampleIdx_ ;     // trk2als: para detectar el BPM al cambiar de sample
	GUIPoint bpmInfoPos_ ;   // trk2als: dónde dibujar '85>100 +2.8st'
	// trk2als: forma de onda del sample (picos precalculados por barra)
	void drawWaveform() ;
	int wfSample_ ;
	int wfSize_ ;
	enum { WF_BARS=76 } ;   // barras de 5 px cada 8 px = 608 px
	unsigned char wfPeaks_[WF_BARS] ;
	int playheadPx_ ;        // dónde se dibujó el cursor de reproducción (-1 = no)
	bool audioMode_ ;        // loop de pista de audio: pantalla simplificada
	int labelCount_ ;        // etiquetas de los bloques de campos
	int labelRow_[8] ;
	const char *labelText_[8] ;
} ;
#endif
