#ifndef _SAMPLE_INSTRUMENT_H_
#define _SAMPLE_INSTRUMENT_H_

#include "I_Instrument.h"
#include "SampleRenderingParams.h"
#include "SRPUpdaters.h"

#include "SoundSource.h"
#include "WarpStretch.h"
#include <vector>
#include "Application/Model/Song.h" 
#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/WatchedVariable.h"

enum SampleInstrumentLoopMode {
    SILM_ONESHOT = 0,
    SILM_LOOP,
    SILM_LOOP_PINGPONG,
    SILM_OSC,
    //	SILM_OSCFINE,
    SILM_LOOPSYNC,
    SILM_LAST
};

#define NO_SAMPLE (-1)
#define SIP_VOLUME    		MAKE_FOURCC('V','O','L','M')
#define SIP_CRUSH 	  		MAKE_FOURCC('C','R','S','H')
#define SIP_CRUSHVOL 	  	MAKE_FOURCC('C','R','S','V')
#define SIP_DOWNSMPL 	  	MAKE_FOURCC('D','S','P','L')
#define SIP_ROOTNOTE  		MAKE_FOURCC('R','O','O','T')
#define SIP_FINETUNE  		MAKE_FOURCC('F','N','T','N')
#define SIP_PAN 	  		MAKE_FOURCC('P','A','N','_')
#define SIP_START       	MAKE_FOURCC('S','T','R','T')
#define SIP_END       		MAKE_FOURCC('E','N','D','_')
#define SIP_LOOPMODE  		MAKE_FOURCC('L','M','O','D')
#define SIP_LOOPSTART 		MAKE_FOURCC('L','S','T','A')
#define SIP_LOOPLEN 		MAKE_FOURCC('L','L','E','N')
#define SIP_INTERPOLATION   MAKE_FOURCC('I','N','T','P')
#define SIP_SAMPLE 		    MAKE_FOURCC('S','M','P','L')
#define SIP_SLICES 		    MAKE_FOURCC('S','L','C','S')
#define SIP_FILTMODE		MAKE_FOURCC('F','I','M','O') 
#define SIP_ATTENUATE		MAKE_FOURCC('F','I','A','T')
#define SIP_FILTMIX			MAKE_FOURCC('F','M','I','X')
#define SIP_FILTCUTOFF		MAKE_FOURCC('F','C','U','T')
#define SIP_FILTRESO		MAKE_FOURCC('F','R','E','S')
#define SIP_TABLE			MAKE_FOURCC('T','A','B','L')
#define SIP_TABLEAUTO		MAKE_FOURCC('T','B','L','A')
#define SIP_FBTUNE			MAKE_FOURCC('F','B','T','U')
#define SIP_FBMIX			MAKE_FOURCC('F','B','M','X')
#define SIP_PRINTFX MAKE_FOURCC('P', 'R', 'F', 'X')
#define SIP_IR_PAD MAKE_FOURCC('I', 'R', 'P', 'D')
#define SIP_IR_WET MAKE_FOURCC('I', 'R', 'W', 'T')
// trk2als: compases que dura la region del loop en modo looper sync
#define SIP_LOOPBARS MAKE_FOURCC('L', 'B', 'A', 'R')
// trk2als: BPM original del loop (0 = compases a mano)
#define SIP_LOOPBPM MAKE_FOURCC('L', 'B', 'P', 'M')
// trk2als: compás del sample donde empieza el loop (1 = el primero)
#define SIP_FROMBAR MAKE_FOURCC('L', 'F', 'R', 'M')
// trk2als: cómo sigue el loop al tempo: 0 re-pitch (cambia el tono), 1 complex (time-stretch)
#define SIP_WARP MAKE_FOURCC('W', 'A', 'R', 'P')
// trk2als: semitonos del modo complex (en re-pitch el tono lo pone el tempo)
#define SIP_TRANSPOSE MAKE_FOURCC('T', 'R', 'N', 'S')
// trk2als: cuántos compases dura la celda en la Song (0 = los mismos que el loop)
#define SIP_CLIPBARS MAKE_FOURCC('C', 'L', 'E', 'N')
// trk2als: Sampler estilo Simpler (instrumentos de pistas MIDI)
#define SIP_S_INIT  MAKE_FOURCC('S', 'I', 'N', 'I')
#define SIP_S_MODE  MAKE_FOURCC('S', 'M', 'O', 'D')
#define SIP_S_START MAKE_FOURCC('S', 'S', 'T', 'A')
#define SIP_S_END   MAKE_FOURCC('S', 'E', 'N', 'D')
#define SIP_S_LOOP  MAKE_FOURCC('S', 'L', 'O', 'N')
#define SIP_S_LLEN  MAKE_FOURCC('S', 'L', 'L', 'E')
#define SIP_S_TRIG  MAKE_FOURCC('S', 'T', 'R', 'G')
#define SIP_S_SLICES MAKE_FOURCC('S', 'S', 'L', 'N')
#define SIP_S_TRANSP MAKE_FOURCC('S', 'T', 'R', 'N')
#define SIP_S_DETUNE MAKE_FOURCC('S', 'D', 'E', 'T')
#define SIP_S_GLIDE MAKE_FOURCC('S', 'G', 'L', 'D')
#define SIP_S_FTYPE MAKE_FOURCC('S', 'F', 'T', 'Y')
#define SIP_S_FFREQ MAKE_FOURCC('S', 'F', 'F', 'Q')
#define SIP_S_FRES  MAKE_FOURCC('S', 'F', 'R', 'S')
#define SIP_S_FENV  MAKE_FOURCC('S', 'F', 'E', 'N')
#define SIP_S_VOL   MAKE_FOURCC('S', 'V', 'O', 'L')
#define SIP_S_PAN   MAKE_FOURCC('S', 'P', 'A', 'N')
#define SIP_S_ATK   MAKE_FOURCC('S', 'A', 'T', 'K')
#define SIP_S_DEC   MAKE_FOURCC('S', 'D', 'E', 'C')
#define SIP_S_SUS   MAKE_FOURCC('S', 'S', 'U', 'S')
#define SIP_S_REL   MAKE_FOURCC('S', 'R', 'E', 'L')
#define SIP_S_FIN   MAKE_FOURCC('S', 'F', 'I', 'N')
#define SIP_S_FOUT  MAKE_FOURCC('S', 'F', 'O', 'U')
#define SIP_S_VEL   MAKE_FOURCC('S', 'V', 'E', 'L')
#define SIP_S_WARP  MAKE_FOURCC('S', 'W', 'R', 'P')
#define SIP_S_BPM   MAKE_FOURCC('S', 'B', 'P', 'M')
#define SIP_S_VOICES MAKE_FOURCC('S', 'V', 'O', 'I')

// trk2als: cada canal tiene su voz y, con 'voices: 2', una voz más para la
// cola de la nota anterior (índice canal+SONG_CHANNEL_COUNT)
#define SI_VOICE_COUNT (SONG_CHANNEL_COUNT*2)
enum SimplerMode { SM_CLASSIC = 0, SM_ONESHOT, SM_SLICE };
// trk2als: warp del Sampler (como en Simpler): sin warp, re-pitch o complex
enum SimplerWarp { SW_OFF = 0, SW_REPITCH, SW_COMPLEX };

#define FB_BUFFER_LENGTH 3500 // (in samples)

class SampleInstrument: public I_Instrument,I_Observer {

public:
       SampleInstrument() ;
       virtual ~SampleInstrument() ;
       // I_Instrument implementation
	   virtual bool Init() ;
       virtual bool Start(int channel, unsigned char note, int flags = 1);
       virtual void Stop(int channel) ;
       virtual bool Releasing(int channel) ;
       virtual void Choke(int channel) ;
       // trk2als: Sampler estilo Simpler
       void InitSimpler() ;       // la primera vez: traducir lo de LGPT
       int GetSimpler(FourCC id) { return FindVariable(id)->GetInt() ; } ;
       int GuessSimplerBpm() ;   // tempo probable de la región (compases enteros)
       void RefreshSample() { updateInstrumentData(true) ; } ; // trk2als: presets
       virtual bool Render(int channel, fixed *buffer, int size, int flags);
       virtual bool IsInitialized() ;
	   virtual bool IsEmpty() ;

	   virtual InstrumentType GetType() { return IT_SAMPLE ; } ;
  	   virtual void ProcessCommand(int channel,FourCC cc,ushort value) ;
	   virtual void Purge() ;
	   virtual int GetTable() ;
	   virtual bool GetTableAutomation();
	   virtual void GetTableState(TableSaveState &state) ;	 
	   virtual void SetTableState(TableSaveState &state) ;	 

	   bool IsMulti() ;

	  // Engine playback  start callback

	  virtual void OnStart() ;

	   // I_Observer
       virtual void Update(Observable &o,I_ObservableData *d);
       // Additional
       void AssignSample(int i) ;
	   int GetSampleIndex() ;
	   // trk2als: tempo de los loops
	   float GetLoopSeconds() ;   // duración de la región loop start..end
	   int DetectLoopBpm() ;      // BPM del nombre del archivo o adivinado por la duración
	   // name: nombre del archivo; secs: duración; allowGuess: adivinar si el nombre no dice
	   static int DetectBpm(const char *name, float secs, bool allowGuess) ;
	   void UpdateLoopBars() ;    // compases = duración x BPM / 240
	   // Elegir el loop por compases (necesita el bpm del loop)
	   float GetBarSamples() ;    // muestras por compás de 4/4; 0 si no hay bpm
	   float GetSampleBars() ;    // cuántos compases dura el sample completo
	   void ApplyBarRegion() ;    // start/loop start/loop end según from bar y bars
	   int GetPlayPosition() ;    // dónde va la voz que suena (muestras); -1 si no suena
	   // El Player avisa si el canal es pista de audio: entonces la voz toca en
	   // looper sync sin cambiar el loop mode del instrumento (que puede usarse en MIDI)
	   static void SetChannelSync(int channel, bool sync) ;
	   int GetVolume() ;
	   void SetVolume(int) ;
	   int GetSampleSize(int channel=-1) ;
       int GetLoopEnd();
       virtual const char *GetName();
       virtual const char *GetFileName();
 
  static void EnableDownsamplingLegacy();

protected:
		void updateInstrumentData(bool search) ;
		void doTickUpdate(int channel) ;
		void doKRateUpdate(int channel) ;
		void updateFeedback(renderParams *rp) ;

private:
       SoundSource *source_ ;
       struct renderParams renderParams_[SI_VOICE_COUNT] ;
       bool running_ ;
       bool dirty_ ;
	   TableSaveState tableState_ ;
	   
	   static int lastMidiNote_[SI_VOICE_COUNT] ;
	   static bool syncRequest_[SONG_CHANNEL_COUNT] ; // trk2als
	   unsigned long voiceClock_[SI_VOICE_COUNT] ; // trk2als: último render de cada voz (ms)
	   SampleInstrumentLoopMode effectiveLoopMode(renderParams *rp) ;
	   void snapshotLive(renderParams *rp) ;  // trk2als
	   void applyLive(renderParams *rp) ;     // trk2als
	   // trk2als: warp complex; deja size cuadros estirados en warpOut_
	   void renderComplex(int channel,renderParams *rp,int size) ;
	   void readLoop(renderParams *rp,int n) ;
	   bool startSimpler(renderParams *rp,int channel,unsigned char note,int &first,bool &sliced,float &offset) ;
	   void simplerBlock(renderParams *rp,int size) ; // envolvente -> corte, glide
	   void simplerEnvelope(renderParams *rp,fixed *buffer,int size) ;
	   float envMs(FourCC id) ;
	   WarpStretch *stretch_[SI_VOICE_COUNT] ;
	   float lastWarpT_[SI_VOICE_COUNT], warpTarget_[SI_VOICE_COUNT] ; // glide con warp complex
	   std::vector<float> warpIn_[2], warpOut2_[2] ;
	   std::vector<short> warpOut_ ;
	   static fixed lastSample_[SI_VOICE_COUNT][2] ;
	   static fixed feedback_[SI_VOICE_COUNT][FB_BUFFER_LENGTH*2] ;
	   bool renderVoice(int voice, fixed *buffer, int size, int flags) ;
	   void startTail(int channel) ;      // la nota que sonaba pasa a la voz de cola
	   bool tailActive(int channel) ;
	   std::vector<fixed> tailBuffer_ ;

	   Variable *volume_ ;
	   Variable *crush_ ;
	   Variable *cutoff_ ;
	   Variable *reso_ ;
	   Variable *table_ ;
	   Variable *tableAuto_ ;
	   Variable *downsample_ ;
	   Variable *rootNote_ ;
	   Variable *fineTune_ ;
	   Variable *drive_ ;
	   Variable *fbMix_ ;
	   Variable *fbTune_ ;
	   WatchedVariable *start_ ;
	   WatchedVariable *loopStart_ ;
	   WatchedVariable *loopEnd_ ;
	   Variable *filterMix_ ;
	   Variable *filterMode_ ;
	   Variable *attenuate_ ;
	   Variable *pan_ ;
	   Variable *loopMode_ ;
	   Variable *slices_ ;
	   Variable *loopBars_ ;
	   WatchedVariable *loopBpm_ ;
	   Variable *fromBar_ ;
	   Variable *warp_ ;
	   Variable *transpose_ ;
	   Variable *clipBars_ ;
	   Variable *interpolation_ ;
       Variable *printFx_;
       Variable *irPad_;
       Variable *irWet_;

       static bool useDirtyDownsampling_;
       char *fxPresets[4];
} ;
#endif
