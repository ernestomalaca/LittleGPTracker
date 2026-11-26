#ifndef _SAMPLE_INSTRUMENT_H_
#define _SAMPLE_INSTRUMENT_H_

#include "I_Instrument.h"
#include "SampleRenderingParams.h"
#include "SRPUpdaters.h"

#include "SoundSource.h"
#include <string>
#include <vector>
#include <map>
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
// trk2als: la parte fina del inicio y el fin (centésimas de %, 0..99): los
// pone la edición sobre la onda; las cajas siguen en % enteros
#define SIP_S_STARTF MAKE_FOURCC('S', 'S', 'T', 'F')
#define SIP_S_ENDF   MAKE_FOURCC('S', 'E', 'N', 'F')
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
// trk2als: filtro del Sampler (12 dB/oct, como el Clean de Simpler)
#define SIP_S_FHZ   MAKE_FOURCC('S', 'F', 'H', 'Z')   // corte en Hz
#define SIP_S_FQ    MAKE_FOURCC('S', 'F', 'R', 'Q')   // resonancia 0..100 %
#define SIP_S_FST   MAKE_FOURCC('S', 'F', 'S', 'T')   // envolvente en semitonos
#define SIP_S_FVER  MAKE_FOURCC('S', 'F', 'V', 'R')   // 1 = ya con el filtro nuevo
// trk2als: envolvente propia del filtro (como en Simpler) y modulaciones
#define SIP_S_FATK  MAKE_FOURCC('S', 'F', 'A', 'T')
#define SIP_S_FDEC  MAKE_FOURCC('S', 'F', 'D', 'C')
#define SIP_S_FSUS  MAKE_FOURCC('S', 'F', 'S', 'U')   // dB
#define SIP_S_FREL  MAKE_FOURCC('S', 'F', 'R', 'L')
#define SIP_S_FKEY  MAKE_FOURCC('S', 'F', 'K', 'Y')   // key tracking 0..100 %
#define SIP_S_FVEL  MAKE_FOURCC('S', 'F', 'V', 'L')   // velocity -> corte 0..100 %
#define SIP_S_FEVER MAKE_FOURCC('S', 'F', 'E', 'V')   // 1 = ya con su envolvente
#define SIP_S_ETAB  MAKE_FOURCC('S', 'E', 'T', 'B')   // pantalla: amp / filter
#define SIP_S_POLY MAKE_FOURCC('S', 'P', 'L', 'Y') // voces: notas a la vez (1 = mono, con glide)
#define SIP_S_SNAP MAKE_FOURCC('S', 'S', 'N', 'P') // snap: al cruce por cero más cercano
#define SIP_S_SLICEBY MAKE_FOURCC('S', 'S', 'B', 'Y') // slice by: region / manual
#define SIP_S_MARKS MAKE_FOURCC('S', 'M', 'R', 'K')   // marcas manuales (cuadros, "a,b,c")
#define SIP_S_KIT MAKE_FOURCC('S', 'K', 'I', 'T')   // trk2als: las zonas son pads de batería (one-shot)
#define SIP_S_KITNAME MAKE_FOURCC('S', 'K', 'N', 'M')   // trk2als: el nombre del kit (la carpeta del preset)
#define SIP_S_PADS MAKE_FOURCC('S', 'P', 'D', 'S')  // trk2als: ajustes por pad "nota:dB:semitonos:pan:decay ms:choke:inicio %;..."
struct PadParams {
	int gain ;     // dB (-36..+12)
	int tune ;     // semitonos (-24..+24)
	int pan ;      // -50..+50
	int decay ;    // ms (0 = completo)
	int choke ;    // 0 = ninguno, 1..4
	int start ;    // % del sample (0..99)
	PadParams():gain(0),tune(0),pan(0),decay(0),choke(0),start(0) {}
} ;
#define SIP_S_ZONES MAKE_FOURCC('S', 'Z', 'O', 'N')   // trk2als: zonas "lo,hi,raíz,cents,loopIni,loopFin,sample;..."
#define SIP_CLIP MAKE_FOURCC('C', 'L', 'I', 'P')      // trk2als: 1 = es un loop de pista de audio (cargado desde la Song)

enum SimplerSliceBy { SSB_REGION=0, SSB_MANUAL=1 } ;
// trk2als: clip de audio estilo Live
#define SIP_A_GAIN  MAKE_FOURCC('A', 'G', 'A', 'N')   // ganancia del clip en dB
#define SIP_A_DET   MAKE_FOURCC('A', 'D', 'E', 'T')   // detune en cents (complex)
#define SIP_A_FADE  MAKE_FOURCC('A', 'F', 'A', 'D')   // microfades en los bordes
#define SIP_A_INIT  MAKE_FOURCC('A', 'I', 'N', 'I')   // 1 = ya convertido

// trk2als: cada canal tiene su voz y, con 'voices: 2', una voz más para la
// cola de la nota anterior (índice canal+SONG_CHANNEL_COUNT); después, las
// voces 1..3 de los acordes (canal+(1+voz)*SONG_CHANNEL_COUNT)
#define SI_CHORD_VOICES 3
#define SI_VOICE_COUNT (SONG_CHANNEL_COUNT*(2+SI_CHORD_VOICES))
enum SimplerMode { SM_CLASSIC = 0, SM_ONESHOT, SM_SLICE };
// trk2als: warp del Sampler (como en Simpler): sin warp, re-pitch o complex
enum SimplerWarp { SW_OFF = 0, SW_REPITCH, SW_COMPLEX };

#define FB_BUFFER_LENGTH 3500 // (in samples)

// trk2als: varios samples repartidos por teclas (un instrumento de un .sf2,
// un piano, un kit). Se presenta como un solo SoundSource cuyo audio depende
// de la nota, así el Sampler no cambia: cada voz toma el sample de su tecla.
class ZoneSource: public SoundSource {
public:
	struct Zone {
		int lo,hi ;          // teclas
		int root ;           // tecla en que suena en su tono
		float cents ;
		int loopStart,loopEnd ; // cuadros; -1 sin loop
		std::string name ;   // sample del pool
		SoundSource *src ;
	} ;
	std::vector<Zone> zones_ ;
	const Zone *Find(int note) ;   // la de la tecla, o la más cercana; note<0: la del C3
	virtual int GetSize(int note) { const Zone *z=Find(note) ; return (z&&z->src)?z->src->GetSize(-1):0 ; }
	virtual int GetSampleRate(int note) { const Zone *z=Find(note) ; return (z&&z->src)?z->src->GetSampleRate(-1):44100 ; }
	virtual int GetChannelCount(int note) { const Zone *z=Find(note) ; return (z&&z->src)?z->src->GetChannelCount(-1):1 ; }
	virtual void *GetSampleBuffer(int note) { const Zone *z=Find(note) ; return (z&&z->src)?z->src->GetSampleBuffer(-1):0 ; }
	virtual bool IsMulti() { return false ; }
	virtual int GetRootNote(int note) { const Zone *z=Find(note) ; return z?z->root:60 ; }
	virtual int GetLoopStart(int note) { const Zone *z=Find(note) ; return z?z->loopStart:-1 ; }
	virtual int GetLoopEnd(int note) { const Zone *z=Find(note) ; return z?z->loopEnd:-1 ; }
} ;

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
       virtual void PreFade(int channel,int delay,int len) ;
       virtual bool StartVoice(int channel,int voice,unsigned char note,int velocity,int flags=1) ;
       virtual void StopVoice(int channel,int voice) ;
       virtual void SetStartDelay(int channel,int voice,int samples) ;
       static int ChordIdx(int channel,int voice) { return channel+(1+voice)*SONG_CHANNEL_COUNT ; }
       // trk2als: Sampler estilo Simpler
       void InitSimpler() ;       // la primera vez: traducir lo de LGPT
   private:
       FourCC varCacheId_[128] ;
       Variable *varCache_[128] ;
       void initSimplerFromLGPT() ;
   public:
       // trk2als: los parámetros se leen miles de veces por segundo desde el
       // render; FindVariable recorre la lista, así que se recuerda dónde está
       // cada uno (las variables viven lo mismo que el instrumento)
       int GetSimpler(FourCC id) {
           unsigned h=simplerSlot(id) ;
           // trk2als: si el clip que suena lo automatiza, manda la automatización
           return autoOn_[h]?autoVal_[h]:varCache_[h]->GetInt() ;
       } ;
       // trk2als: automatización. El valor de la variable no se toca: al
       // quitarla vuelve a sonar lo que tiene el instrumento
       void SetAutomated(FourCC id,int value) {
           unsigned h=simplerSlot(id) ;
           if (!varCache_[h]) return ;
           autoVal_[h]=value ;
           autoOn_[h]=true ;
       }
       bool GetAutomated(FourCC id,int &value) {
           unsigned h=simplerSlot(id) ;
           value=autoVal_[h] ;
           return autoOn_[h] ;
       }
       void ClearAutomated(FourCC id) { autoOn_[simplerSlot(id)]=false ; }
       void ClearAllAutomated() { for (int i=0;i<128;i++) autoOn_[i]=false ; }
   private:
       bool autoOn_[128] ;
       int autoVal_[128] ;
       unsigned simplerSlot(FourCC id) {
           unsigned h=((unsigned)id*2654435761u)>>25 ;   // 128 casillas
           while (varCacheId_[h]!=id) {
               if (varCacheId_[h]==0) {
                   varCacheId_[h]=id ;
                   varCache_[h]=FindVariable(id) ;
                   break ;
               }
               h=(h+1)&127 ;
           }
           return h ;
       }
   public:
       int GuessSimplerBpm() ;
       // trk2als: warp con el que arranca un sample nuevo en el Sampler
       // (DEFAULTWARP en config.xml: off, re-pitch, complex o auto). Deja el
       // bpm en *bpm si lo encontró
       int DefaultWarp(int *bpm) ;
       // trk2als: rebanadas. Región start..end en cuadros (en cruces por
       // cero) y los bordes de cada rebanada: a, ..., b (n rebanadas = n+1 bordes)
       SoundSource *currentSource() ;   // el sample elegido (aunque source_ no se haya actualizado)
       bool SimplerRegion(int &a,int &b) ;
       // el inicio (o el fin) en esta posición del sample, fino; sin cruzarse
       void SetRegionPoint(bool start,int pos) ;
       int RegionPoint(bool start) ;   // sin acomodar al cruce por cero
       void SliceBounds(std::vector<int> &bounds) ;
       std::vector<int> GetMarks() ;
       void SetMarks(const std::vector<int> &marks) ;
       int AddMark(int frame) ;              // devuelve dónde quedó (en el cruce por cero); -1 si no cabe
       int MinMarkGap() ;                    // cuadros mínimos entre marcas (30 ms)
       bool RemoveMarkNear(int frame,int tolerance) ;
       void InitAudioClip() ;    // trk2als: volumen de LGPT -> gain en dB (una vez)
       void declickVoice(renderParams *rp,fixed *buffer,int size) ;   // tempo probable de la región (compases enteros)
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
       // trk2als: zonas (instrumento de varios samples)
       ZoneSource zones_ ;
       bool zoned_ ;            // source_ son las zonas
       int zonesPoolSize_ ;     // tamaño del pool cuando se resolvieron los nombres
       std::string zonesStr_ ;  // la lista ya resuelta
       void rebuildZones() ;
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
	   public: void SetZones(const std::string &zones) ; bool IsZoned() { ensureZones() ; return zoned_ ; } int ZoneCount() { ensureZones() ; return (int)zones_.zones_.size() ; }
	   void ensureZones() ;   // al día con 's zones' y el pool (al abrir un proyecto llegan después del sample)
	   const std::vector<ZoneSource::Zone> &Zones() { ensureZones() ; return zones_.zones_ ; }
	   std::string ZoneSetName() ;   // "marimba" (de marimba_01.wav)
	   int LastNote() { return lastNote_ ; }   // la última nota que empezó
	   // trk2als: Sampler en slice: una rebanada a la vez (la nueva roba la voz)
	   // cuántas notas a la vez (las que se enciman en el clip): slice 1; lo
	   // demás, lo que diga VOCES (1 = mono: la nueva reemplaza, y con glide)
	   int MaxVoices() { if (IsKit()) return 4 ; if (IsSliceMono()) return 1 ;
	                     int n=GetSimpler(SIP_S_POLY) ; return n<1?1:(n>4?4:n) ; }
	   bool IsSliceMono() { return !IsKit() && !IsZoned() && GetSimpler(SIP_S_INIT) && GetSimpler(SIP_S_MODE)==SM_SLICE ; }
	   // trk2als: la próxima nota (one-shot) empieza aquí y no en el inicio:
	   // para escuchar el fade out sin esperar todo el sample
	   void AuditionFrom(int pos) { auditionFrom_=pos ; }
	   int auditionFrom_ ;
	   // trk2als: kit de batería. Cada zona es un pad (una tecla) que suena
	   // completo; los golpes se reparten en las 5 voces del canal
	   bool IsKit() { return GetSimpler(SIP_S_KIT)!=0 && (IsZoned() || ZoneCount()==0) ; }
	   void MakeEmptyKit() ;   // trk2als: un kit nuevo, sin pads
	   int KitBase() ;         // la nota del primer pad de los 16 (36 = C1, como en Live)
	   std::string PadName(int note) ;   // nombre corto del sample del pad, o ""
	   PadParams GetPad(int note) ;
	   void SetPad(int note,const PadParams &p) ;
	   std::string PadSample(int note) ;          // el .wav del pad (nombre en el pool) o ""
	   void SetPadSample(int note,const std::string &wav) ;   // "" = vaciar el pad
	   private:
	   void parsePads() ;
	   std::string padsStr_ ;
	   std::map<int,PadParams> pads_ ;
	   public:
	   private:
	   bool startKit(int channel,unsigned char note,int velocity,int flags) ;
	   bool kitStarting_ ;
	   int lastKitSlot_[SONG_CHANNEL_COUNT] ;
	   unsigned long kitStartClock_[SI_VOICE_COUNT] ;
	   unsigned long padHit_[128] ;   // cuándo sonó cada nota del kit (para prender su pad)
	public:
	   unsigned long PadHit(int note) { return (note>=0 && note<128)?padHit_[note]:0 ; }
	private:
	   public:
	   private:
	   int lastNote_ ;
	   bool cacheKey(renderParams *rp,int start,double speed,float transpose,WarpCacheKey &k) ;
	   void readCache(renderParams *rp,int size,double speed) ;
	   // trk2als: a otra velocidad; pitch: corrección del tono (transposición nueva / la del pre-render)
	   void readCacheVar(renderParams *rp,int size,double ratio,double pitch) ;
	   void readCacheOla(renderParams *rp,int size,double ratio,double pitch) ;   // ...sin cambiar el tono
	   bool keepPitch() ;   // trk2als: el sample es tonal (o LOOPKEEPPITCH)
	   int tonalIdx_ ;
	   bool tonal_ ;
	   void dropCache(renderParams *rp) ;
	   bool startSimpler(renderParams *rp,int channel,unsigned char note,int &first,bool &sliced,float &offset) ;
	   void simplerBlock(renderParams *rp,int size) ; // envolvente -> corte, glide
	   void simplerFilter(renderParams *rp,fixed *buffer,int size) ;
	   void filterEnvBlock(renderParams *rp,int size) ;
	   int snapZero(int frame,int lo,int hi,unsigned char note,SoundSource *src=0) ;   // cruce por cero más cercano
	   void simplerEnvelope(renderParams *rp,fixed *buffer,int size) ;
	   float envMs(FourCC id) ;
	   WarpStretch *stretch_[SI_VOICE_COUNT] ;
	   float lastWarpT_[SI_VOICE_COUNT], warpTarget_[SI_VOICE_COUNT] ; // glide con warp complex
	   std::vector<float> warpIn_[2], warpOut2_[2] ;
	   std::vector<short> warpOut_ ;
	   static fixed lastSample_[SI_VOICE_COUNT][2] ;
	   static fixed feedback_[SI_VOICE_COUNT][FB_BUFFER_LENGTH*2] ;
	   bool renderVoice(int voice, fixed *buffer, int size, int flags) ;
	   bool renderVoiceNow(int voice, fixed *buffer, int size, int flags) ;
	   void startTail(int channel) ;      // la nota que sonaba pasa a la voz de cola
	   bool tailActive(int channel) ;
	   // el Sampler es siempre mono (la nota nueva apaga a la anterior); solo
	   // el multicapa (.sf2) deja la cola sonando
	   int voices() { return IsZoned()?GetSimpler(SIP_S_VOICES):1 ; }
	   bool renderVoiceDelayed(int voice, fixed *buffer, int size, int flags) ;
	   void stealVoice(int idx) ;
	   enum { STEAL_LEN=220 } ;                // 5 ms
	   float lastOut_[SI_VOICE_COUNT][2] ;     // último valor que dio cada voz
	   unsigned long lastOutClock_[SI_VOICE_COUNT] ;
	   float stolen_[SI_VOICE_COUNT][2] ;      // voz pisada: lo que se apaga
	   int stolenLeft_[SI_VOICE_COUNT] ;
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
