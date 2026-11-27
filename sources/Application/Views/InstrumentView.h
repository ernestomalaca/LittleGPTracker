#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

class UIIntVarField ;
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
	bool gridMode_ ;         // trk2als: la rejilla de 8 (como las perillas del MiniLab)
	void drawGrid() ;
	void drawArc(int cx,int cy,int r,float from,float to,GUIColor c,int th) ;
	// trk2als: la rejilla en páginas (L / R): pestañas arriba de las cajas
	int page_[2] ;           // la página de cada pantalla: 0 clip de audio, 1 Sampler
	int pageCount_ ;
	const char *pageName_[5] ;
	int pageTap_ ;           // L o R tocado solo: al soltar, cambia de página
	void turnPage(int dir) ;
	int gridSlot(UIField &f) ;
	void gridBox(int k,int &x,int &y,int &w,int &h) ;
	bool simplerView_ ;      // Sampler estilo Simpler (pistas MIDI)
	int ampLabelRow_ ;       // dónde va el dibujo de la envolvente
	void drawSimplerWave(SampleInstrument *si,int X,int Y,int W,int H,int size,SoundSource *zsrc=0) ;
	// trk2als: una curva como las de automatización: trazo continuo, columna
	// por columna; [hiFrom,hiTo) más grueso (el tramo que se está editando)
	void drawCurveLine(const std::vector<int> &ys,int x0,GUIColor c,int hiFrom,int hiTo,int xmin=0,int xmax=640) ;
	void drawZoneStrip(SampleInstrument *si,int X,int Y,int W,int H,int cur) ;
	int labelCount_ ;        // etiquetas de los bloques de campos
	int labelRow_[8] ;
	const char *labelText_[8] ;
	// trk2als: presets (campos "cargar"/"guardar" al final del Sampler)
	Variable presetLoadVar_, presetSaveVar_ ;
	std::string presetFolder_ ;   // trk2als: categoría donde se guarda el preset
	Variable markVar_ ;      // trk2als: cursor de las marcas (slice manual)
	bool markTap_ ;
	bool manualPending_ ;   // llegó a slice manual con A apretada: al soltar entra a la onda
	// trk2als: slice manual, editar marcas sobre la onda
	bool waveEdit_ ;
	int waveZoom_ ;
	int waveSel_ ;            // 0 nada, 1 la marca agarrada (A), 2 todas, 3 marcando (Select, B)
	int waveAnchor_ ;         // marcando: desde dónde
	int waveFlag_ ;           // A sobre una bandera: 1 el inicio, 2 el final
	// la parada elegida en la onda (L / R): 0 el inicio, 3 el final; en
	// classic también 1 el decay y 2 el sustain
	int waveStop_ ;
	FourCC waveStopID() ;     // el parámetro de 'cómo' de esa parada (0: ninguno)
	int tapPending_ ;         // Start en slice: tras la precuenta suena desde aquí
	bool canMark() ;          // slice a mano: se pueden poner marcas
	bool waveAPut_ ;          // A en un lugar vacío: la marca se pone al soltar (o al moverla)
	void toggleDraw() ;
	void waveARelease() ;
	// trk2als: one-shot, página AMP: Select entra a los fades (L / R los
	// eligen; B + L / B + R con ← → los mueven y va sonando)
	bool fadeEdit_ ;
	int fadeSel_ ;            // 0 el fade in, 1 el fade out
	bool processFadeEdit(unsigned short mask,bool pressed) ;
	void fadeAudition() ;
	// trk2als: la envolvente (FILTRO, o AMP en classic / slice): Select entra;
	// L / R solos caminan por attack, decay, sustain y release; A + ← → el
	// tiempo, A + ↑ ↓ el nivel; B + L / B + R: directo al attack / release
	bool envEdit_ ;
	bool envFilter_ ;         // la del filtro (si no, la de volumen)
	int envSel_ ;             // 0 attack, 1 decay, 2 sustain, 3 release
	unsigned long envOffAt_ ; // cuándo se suelta la nota de la escucha (0: no suena)
	int envCh_ ;
	int envNote_ ;            // C3; en slice, C1 (la primera rebanada)
	FourCC envID(int part) ;
	bool processEnvEdit(unsigned short mask,bool pressed) ;
	void envAudition(int holdMs=1000) ;
	bool clipSounding() ;     // la pista ya suena con las notas de su clip
	void envNoteOff() ;
	bool drawEditing() { return waveEdit_ || fadeEdit_ || envEdit_ || filtEdit_ ; }
	bool filtEdit_ ;      // el dibujo del filtro (su curva de respuesta)
	FourCC filtBox_ ;     // la caja que se movió al último ahí
	bool processFiltEdit(unsigned short mask,bool pressed) ;
	void drawFilterCurve(SampleInstrument *si,int X,int Y,int W,int H,GUIColor c) ;
	// deshacer / rehacer en la onda (Select + L / R): inicio, fin y marcas
	struct WaveSnap { int s,sf,e,ef,fi,fo,ak,dc,su,rl ; std::vector<int> marks ;
		bool operator==(const WaveSnap &o) const { return s==o.s && sf==o.sf && e==o.e && ef==o.ef && fi==o.fi && fo==o.fo &&
			ak==o.ak && dc==o.dc && su==o.su && rl==o.rl && marks==o.marks ; } } ;
	std::vector<WaveSnap> waveUndo_, waveRedo_ ;
	bool waveGesture_ ;       // ya se guardó la foto de este gesto
	WaveSnap waveNow() ;
	void wavePush() ;
	unsigned long lastAPress_ ;
	bool previewing_ ;
	int previewPos_ ;
	bool tapPlay_ ;           // suena toda la región: A marca
	bool songByTap_ ;         // la canción la arrancó la edición
	void startTapPlay(int from,bool countIn=true) ;
	void stopTapPlay() ;
	void enterWaveEdit(bool play=true) ;
	void drawKit(SampleInstrument *si) ;
	void hopDrawing(int dir) ;
	void drawCapture(FourCC id,int before) ;
	void beginTouchID(FourCC id) ;
	int drawRep_ ; unsigned short drawRepMask_ ;   // repeticiones de la misma tecla en un dibujo
	void autoDraw() ;
	bool boxMode_ ;   // Sampler: Select abrió las cajas (si no, se está en el dibujo)
	unsigned char kitWf_[80] ;   // la onda del pad elegido
	int kitWfIndex_, kitWfSize_ ;
	unsigned long padHoldAt_ ;   // A apretada en un pad (sostenida: sale el +)
	bool padPlus_ ;
	bool padEdit_ ;              // kit, pestaña PAD: Select desbloqueó las cajas del pad
	int kitLitShown_ ;           // los pads prendidos que se dibujaron
	void waveNotice(FourCC id) ;
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
	// cómo se ve la curva de un parámetro en el clip (rango, formato...)
	struct CurveMeta { int instr, min, max, step, big, disp, listN ; bool log, time ; char name[24], fmt[24] ; char **list ; } ;
	void curveMeta(UIIntVarField *fld,CurveMeta &m) ;
	void applyCurveMeta(const CurveMeta &m) ;
	struct CapSeg { FourCC id ; int ch ; int clip ; int base ; unsigned long tEnd ; std::vector<AutoPoint> pts ; CurveMeta meta ; } ;
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
	// la pista donde se movió algo por última vez (track no se usa)
	const char *CaptureAutomation(int track) ;
	unsigned long LastCaptureMove() ;   // trk2als: cuándo se movió algo por capturar (0 = nada)
private:
	std::vector<int> autoShown_ ;  // los valores en vivo que se dibujaron
	int recShown_ ;   // cómo se dibujó el REC (0 no, 1/3 parpadeo, 2 grabando)
} ;
#endif
