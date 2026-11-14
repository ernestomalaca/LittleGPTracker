#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"
#include "Foundation/Variables/Variable.h"
#include <string>

class SampleInstrument ;

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
	void resetFocusedField() ;
	int wfSample_ ;
	int wfSize_ ;
	int wfFrom_, wfTo_ ;    // trk2als: cuadros que muestra la onda (zoom a la región)
	enum { WF_BARS=76 } ;   // barras de 5 px cada 8 px = 608 px
	unsigned char wfPeaks_[WF_BARS] ;
	int playheadPx_ ;        // dónde se dibujó el cursor de reproducción (-1 = no)
	bool audioMode_ ;        // loop de pista de audio: pantalla simplificada
	bool simplerView_ ;      // Sampler estilo Simpler (pistas MIDI)
	int ampLabelRow_ ;       // dónde va el dibujo de la envolvente
	void drawSimplerWave(SampleInstrument *si,int X,int Y,int W,int H,int size) ;
	int labelCount_ ;        // etiquetas de los bloques de campos
	int labelRow_[8] ;
	const char *labelText_[8] ;
	// trk2als: presets (campos "cargar"/"guardar" al final del Sampler)
	Variable presetLoadVar_, presetSaveVar_ ;
	Variable markVar_ ;      // trk2als: cursor de las marcas (slice manual)
	bool markTap_ ;
	// trk2als: slice manual, editar marcas sobre la onda
	bool waveEdit_ ;
	int waveZoom_ ;
	int waveSel_ ;            // 0 nada, 1 la marca del cursor, 2 todas
	unsigned long lastAPress_ ;
	bool previewing_ ;
	int previewPos_ ;
	void enterWaveEdit() ;
	void exitWaveEdit() ;
	bool processWaveEdit(unsigned short mask,bool pressed) ;
	void startPreview() ;
	void stopPreview() ;
	int nearMark(const std::vector<int> &m) ;
	void openPresetLoad() ;
	void openPresetSave() ;
public:
	void OnPresetLoad(const std::string &name) ;
	void OnPresetSave(const std::string &name) ;
} ;
#endif
