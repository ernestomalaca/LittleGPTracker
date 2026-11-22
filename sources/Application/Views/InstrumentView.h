#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/Views/SoundLauncher.h"
#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"
#include "Foundation/Variables/Variable.h"
#include <string>
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Model/Clip.h"

class SampleInstrument ;

class InstrumentView: public FieldView, public I_Observer, public LaunchHost {
public:
	InstrumentView(GUIWindow &w,ViewData *data) ;
	virtual ~InstrumentView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	void processButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() ;
	virtual bool Tick() ;
	virtual void AnimationUpdate() ;

protected:
	void warpToNext(int offset) ;
	bool ownedByTrack() ;
	bool sf2Pending_ ;   // trk2als: se eligió un .sf2: convertirlo al soltar A
	void convertSf2(bool refresh) ;
	bool sfMode_ ;       // instrumento soundfont (zonas)
	void drawZoneMap(SampleInstrument *si,int X,int Y,int W,int H) ;
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
	void drawWaveformBody() ;
	std::string topInfo_ ;   // trk2als: va a la barra de arriba
	const char *helpText_ ; // y esto a la de abajo
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
	std::string presetFolder_ ;   // trk2als: categoría donde se guarda el preset
	Variable markVar_ ;      // trk2als: cursor de las marcas (slice manual)
	bool markTap_ ;
	// trk2als: slice manual, editar marcas sobre la onda
	bool waveEdit_ ;
	int waveZoom_ ;
	int waveSel_ ;            // 0 nada, 1 la marca del cursor, 2 todas
	unsigned long lastAPress_ ;
	bool previewing_ ;
	int previewPos_ ;
	bool tapPlay_ ;           // suena toda la región: A marca
	bool songByTap_ ;         // la canción la arrancó la edición
	void startTapPlay(int from) ;
	void stopTapPlay() ;
	void enterWaveEdit() ;
	void exitWaveEdit() ;
	bool processWaveEdit(unsigned short mask,bool pressed) ;
	void startPreview(int from=-1,int end=-1) ;
	void sliceAt(int frame,int &from,int &to) ;
	bool slicePlay_ ;          // suena una rebanada (sigue al soltar A)
	void stopPreview() ;
	int nearMark(const std::vector<int> &m) ;
	void openPresetLoad() ;
	void openPresetSave() ;
public:
	void OnPresetLoad(const std::string &name) ;
	void OnPresetSave(const std::string &name) ;
	void OnPresetFolder(const std::string &folder) ;
	void OnPadImport(bool ok) ;
	void OnKitLoad(const std::string &name) ;
	void OnBrowserClosed(bool kit) ;   // trk2als: se cerró un navegador que lanza
	// trk2als: los sonidos lanzados desde aquí (entran en el 1)
	virtual void LaunchBefore(int instrument, int track, bool firstOfVisit) ;
	virtual void LaunchAfter(int instrument, int track, const std::string &name, bool ok) ;
	virtual void LaunchPurged() ;
	virtual void LaunchNotify(const char *msg) ;
private:
	// trk2als: editor de kits: una rejilla de 4x4 pads (como el Move); los
	// campos editan el pad elegido y se escriben en 's pads' / 's zones'
	bool kitMode_ ;
	Variable padSelVar_, padSmpVar_, padGainVar_, padTuneVar_, padPanVar_,
	         padDecayVar_, padChokeVar_, padStartVar_ ;
	int padShown_ ;          // el pad que muestran los campos
	bool padBusy_ ;          // cambiando el kit: no reconstruir a medias
	bool padImport_ ;        // el navegador se abrió para un pad
	int padImportPrev_ ;
	bool padClip_ ;          // hay un pad copiado
	std::string padClipWav_ ;
	PadParams padClipP_ ;
	int padNote() ;
	bool isPadField(FourCC id) ;
	void loadPadVars() ;
	void kitSync() ;
	void refreshKit() ;
	void previewPad() ;
	bool processKit(unsigned short mask) ;
	bool padShortcut(unsigned short mask) ;
	void openPadBrowser() ;
	bool padClipCut_ ;
	bool padAPending_ ;      // A en un pad vacío: el navegador se abre al soltarla       // lo de la mano salió de un corte
	// deshacer en el kit: fotos de todas sus variables
	std::vector<std::vector<std::string> > kitUndo_, kitRedo_ ;
	std::vector<std::string> kitState_ ;
	I_Instrument *kitOwner_ ;
	void kitTake(std::vector<std::string> &snap) ;
	void kitRestore(const std::vector<std::string> &snap) ;
	void kitTrack() ;
	void drawPadGrid(SampleInstrument *si,int X,int Y,int W,int H) ;
	// trk2als: automatización. Y + Start arma la grabación; A sostenido en un
	// parámetro escribe en la lane del clip que suena (como "touch" en Live);
	// un puntito marca lo automatizado y el valor se mueve mientras suena
	static bool automatable(FourCC id) ;
	int autoChannel() ;            // la pista dueña de este instrumento
	int autoClip(int ch) ;         // el clip que suena en ella (o el de la celda)
	void toggleAutoRec() ;
	void beginTouch() ;
	void recordTouch() ;
	void takeOver() ;
	bool openCurve() ;
	void cancelTouch() ;
	void kitUndo(bool undo) ;
	void endTouch() ;
	bool deleteLane() ;           // B+A: quita la automatización del campo
	bool undoLane() ;
	void drawAutoMarks() ;
	bool touching_ ;
	FourCC touchID_ ;
	int touchCh_, touchClip_, touchLast_ ;
	AutoLane touchOld_ ;
	bool touchHadOld_ ;
	bool laneUndo_ ;               // hay una lane borrada que se puede recuperar
	int laneUndoClip_ ;
	unsigned long laneUndoTime_ ;
	bool selTap_ ;
	AutoLane laneUndoLane_ ;
	// deshacer / rehacer la automatización (Select + L / R): una foto de las
	// lanes del clip antes de cada toque grabado o lane borrada
	// (y el valor de la perilla que se movió, para que vuelva a donde estaba)
	struct LaneSnap { int clip ; std::vector<AutoLane> lanes ; FourCC var ; int value ; I_Instrument *instr ; } ;
	int preTouchVal_ ;   // la perilla antes de este toque
	std::vector<LaneSnap> autoUndo_, autoRedo_ ;
	void pushLaneSnap(int clip) ;
	// captura (R2, como Capture en Live): lo que se mueve sin grabar se anota
	// por tramos; al capturar, cada tramo se escribe en su lugar y el resto de
	// la curva queda como estaba
	struct CapSeg { FourCC id ; int ch ; int clip ; int base ; unsigned long tEnd ; std::vector<AutoPoint> pts ; } ;
	std::vector<CapSeg> capSegs_ ;
	CapSeg capCur_ ;
	bool capping_ ;
	int capLast_ ;
	void capBegin() ;
	void capTick() ;
	void capEnd() ;
	void capWrite(const CapSeg &s) ;
public:
	virtual bool Undo(bool redo) ;
	// R2: escribe lo anotado; devuelve el aviso
	// track < 0: la pista del instrumento que se ve
	const char *CaptureAutomation(int track) ;
private:
	std::vector<int> autoShown_ ;  // los valores en vivo que se dibujaron
	int recShown_ ;   // cómo se dibujó el REC (0 no, 1/3 parpadeo, 2 grabando)
} ;
#endif
