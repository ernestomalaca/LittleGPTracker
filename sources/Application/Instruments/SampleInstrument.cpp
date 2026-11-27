#include "SampleInstrument.h"
#include <algorithm>
#include <ctype.h>
#include <strings.h>
#include <stdlib.h>
#include "SamplePool.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "System/io/Status.h"
#include "CommandList.h"
#include <assert.h>
#include "Application/Player/PlayerMixer.h" // For MIX_BUFFER_SIZE.. kick out pls
#include "Application/Player/SyncMaster.h"
#include "Application/Instruments/Filters.h"
#include "Application/Model/Table.h"
#include "Services/Audio/Audio.h"
#include "SampleVariable.h"
#include "System/System/System.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "SampleInstrumentDatas.h"
#include "Application/Player/SyncMaster.h"

fixed SampleInstrument::feedback_[SI_VOICE_COUNT][FB_BUFFER_LENGTH*2] ;

bool SampleInstrument::useDirtyDownsampling_ = false;

#define SHOULD_KILL_CLICKS false

bool SampleInstrument::syncRequest_[SONG_CHANNEL_COUNT]= {false,false,false,false,false,false,false,false} ;

unsigned long SampleInstrument::sliceClock_=0 ;

#include "PlaitsVoice.h"
// trk2als: las voces del sinte, una por voz de render (al primer uso)
static PlaitsVoice *synthVoices[SI_VOICE_COUNT] ;

// trk2als: el "sample" del sinte cuando el Sampler está vacío: silencio
namespace {
class SilentSource : public SoundSource {
public:
	SilentSource() { memset(buf_,0,sizeof(buf_)) ; }
	virtual int GetSize(int) { return SIZE ; }
	virtual int GetSampleRate(int) { return 44100 ; }
	virtual int GetChannelCount(int) { return 1 ; }
	virtual void *GetSampleBuffer(int) { return buf_ ; }
	virtual bool IsMulti() { return false ; }
	virtual int GetRootNote(int) { return 60 ; }
private:
	enum { SIZE=44100 } ;
	short buf_[SIZE] ;
} ;
SilentSource silentSource ;
}

void SampleInstrument::SetChannelSync(int channel, bool sync) {
	if (channel>=0 && channel<SONG_CHANNEL_COUNT) syncRequest_[channel]=sync ;
}

SampleInstrumentLoopMode SampleInstrument::effectiveLoopMode(renderParams *rp) {
	if (rp->forceSync_) return SILM_LOOPSYNC ;
	if (rp->simpler_) return (SampleInstrumentLoopMode)rp->simplerLoop_ ;
	return (SampleInstrumentLoopMode)loopMode_->GetInt() ;
}

// trk2als: parámetros en vivo. Start() copia los valores del instrumento a la
// voz; si después cambian en la pantalla del instrumento, se aplican a la voz que
// ya está sonando (útil para loops largos de las pistas de audio)
void SampleInstrument::snapshotLive(renderParams *rp) {
	rp->liveVol_=volume_->GetInt() ;
	rp->livePan_=pan_->GetInt() ;
	rp->liveCut_=cutoff_->GetInt() ;
	rp->liveRes_=reso_->GetInt() ;
	rp->liveCrush_=crush_->GetInt() ;
	rp->liveDrive_=drive_->GetInt() ;
	rp->liveDs_=downsample_->GetInt() ;
	rp->liveAtt_=attenuate_->GetInt() ;
	rp->liveFbTun_=fbTune_->GetInt() ;
	rp->liveFbMix_=fbMix_->GetInt() ;
}

void SampleInstrument::applyLive(renderParams *rp) {
	int v ;
	if ((v=volume_->GetInt())!=rp->liveVol_) { rp->liveVol_=v ; rp->volume_=rp->baseVolume_=i2fp(v) ; }
	if ((v=pan_->GetInt())!=rp->livePan_) { rp->livePan_=v ; rp->pan_=rp->basePan_=i2fp(v) ; }
	if ((v=cutoff_->GetInt())!=rp->liveCut_) { rp->liveCut_=v ; rp->cutoff_=rp->baseFCut_=fl2fp(v/255.0f) ; }
	if ((v=reso_->GetInt())!=rp->liveRes_) { rp->liveRes_=v ; rp->reso_=rp->baseFRes_=fl2fp(v/255.0f) ; }
	if ((v=crush_->GetInt())!=rp->liveCrush_) { rp->liveCrush_=v ; rp->crush_=v ; }
	if ((v=drive_->GetInt())!=rp->liveDrive_) { rp->liveDrive_=v ; rp->drive_=v ; }
	if ((v=downsample_->GetInt())!=rp->liveDs_) { rp->liveDs_=v ; rp->downsample_=v ; }
	if ((v=attenuate_->GetInt())!=rp->liveAtt_) { rp->liveAtt_=v ; rp->attenuate_=i2fp(v) ; }
	if ((v=fbTune_->GetInt())!=rp->liveFbTun_) { rp->liveFbTun_=v ; rp->fbTun_=rp->baseFbTun_=fl2fp(v/255.0f) ; }
	if ((v=fbMix_->GetInt())!=rp->liveFbMix_) { rp->liveFbMix_=v ; rp->fbMix_=rp->baseFbMix_=fl2fp(v/255.0f) ; }
}

int SampleInstrument::lastMidiNote_[SI_VOICE_COUNT] ;   // trk2als: -1 en el constructor

#define KRATE_SAMPLE_COUNT 100

SampleInstrument::SampleInstrument() {
	 memset(varCacheId_,0,sizeof(varCacheId_)) ;   // trk2als
	 memset(autoOn_,0,sizeof(autoOn_)) ;
	 memset(autoVal_,0,sizeof(autoVal_)) ;
	 zoned_=false ;
	 tonalIdx_=-2 ; tonal_=true ;   // trk2als
	 zonesPoolSize_=-1 ;
	 lastNote_=-1 ;
	 auditionFrom_=-1 ;
	 static bool warpCfg=false ;   // trk2als: WARPQUALITY / WARPSPLIT de config.xml
	 if (!warpCfg) {
		 warpCfg=true ;
		 const char *q=Config::GetInstance()->GetValue("WARPQUALITY") ;
		 const char *sp=Config::GetInstance()->GetValue("WARPSPLIT") ;
		 WarpStretch::SetQuality((q && !strcasecmp(q,"cheaper"))?1:0,
		                         !(sp && !strcasecmp(sp,"off"))) ;
		 const char *wc=Config::GetInstance()->GetValue("WARPCACHE") ;   // MB; 0 = sin pre-render
		 if (wc) {
			 int mb=atoi(wc) ;
			 if (mb<=0) WarpCache::SetEnabled(false) ;
			 else WarpCache::SetBudget(mb) ;
		 }
		 // trk2als: lo estirado también en la tarjeta (WARPDISK = MB; 0 = no)
		 const char *wd=Config::GetInstance()->GetValue("WARPDISK") ;
		 WarpDisk::SetDir(Path("root:warpcache").GetPath().c_str(),wd?atoi(wd):1024) ;
		 // trk2als: WARPWARM / WARPHOT (°C): cuándo moderarse con el calor
		 const char *ww=Config::GetInstance()->GetValue("WARPWARM") ;
		 const char *wh=Config::GetInstance()->GetValue("WARPHOT") ;
		 WarpCache::SetHeat(ww?atoi(ww):75,wh?atoi(wh):80) ;
	 }
	 kitStarting_=false ;
	 for (int i=0;i<SONG_CHANNEL_COUNT;i++) lastKitSlot_[i]=i ;
	 for (int i=0;i<SI_VOICE_COUNT;i++) kitStartClock_[i]=0 ;
	 memset(padHit_,0,sizeof(padHit_)) ;
	 static bool notesInit=false ;
	 if (!notesInit) {
		 notesInit=true ;
		 for (int ch=0;ch<SI_VOICE_COUNT;ch++) lastMidiNote_[ch]=-1 ;
	 }
	 for (int ch=0;ch<SI_VOICE_COUNT;ch++) {   // trk2als
		 voiceClock_[ch]=0 ;
		 stretch_[ch]=0 ;
		 lastWarpT_[ch]=warpTarget_[ch]=1.0f ;
		 renderParams_[ch].simpler_=false ;
		 renderParams_[ch].warpCache_=0 ;
		 renderParams_[ch].cacheIdx_=0 ;
		 renderParams_[ch].cacheFrac_=0 ;
		 renderParams_[ch].olaCache_=0 ;
		 renderParams_[ch].finished_=true ;
		 renderParams_[ch].envStage_=4 ;
		 renderParams_[ch].envLevel_=0 ;
		 renderParams_[ch].glideLeft_=0 ;
		 renderParams_[ch].audioClip_=false ;
		 stolenLeft_[ch]=0 ; stolen_[ch][0]=stolen_[ch][1]=0 ; lastOut_[ch][0]=lastOut_[ch][1]=0 ; lastOutClock_[ch]=0 ;
		 renderParams_[ch].declickIn_=-1 ;
		 renderParams_[ch].declickOut_=renderParams_[ch].declickOutLen_=0 ;
		 renderParams_[ch].declickDelay_=0 ;
		 renderParams_[ch].chokeMs_=0 ; renderParams_[ch].padDecay_=0 ; renderParams_[ch].padClock_=0 ; renderParams_[ch].padChoke_=0 ; renderParams_[ch].startDelay_=0 ;
	 }


// Initialize instruments settings

     source_=0 ;
     dirty_=false ;
     running_=false ;
     fxPresets[0] = "room";
     fxPresets[1] = "hall";
     fxPresets[2] = "spring";
     fxPresets[3] = "church";

// Initialize exported variables

	 WatchedVariable *wv=new SampleVariable("sample",SIP_SAMPLE) ;
	 Insert(wv) ;
	 wv->AddObserver(*this) ;
	 
	 volume_=new Variable("volume",SIP_VOLUME,0x80) ;
	 Insert(volume_) ;

	 interpolation_=new Variable("interpol",SIP_INTERPOLATION,interpolationTypes,2,0) ;
	 Insert(interpolation_) ;
	 
	 crush_=new Variable("crush",SIP_CRUSH,16) ;
	 Insert(crush_) ;

	 drive_=new Variable("crushdrive",SIP_CRUSHVOL,0xFF) ;
	 Insert(drive_) ;

	 downsample_=new Variable("downsample",SIP_DOWNSMPL,0) ;
	 Insert(downsample_) ;

	 rootNote_=new Variable("root note",SIP_ROOTNOTE,60) ;
	 Insert(rootNote_) ;

	 fineTune_=new Variable("fine tune",SIP_FINETUNE,0x7F) ;
	 Insert(fineTune_) ;

	 pan_=new Variable("pan",SIP_PAN,0x7F) ;
	 Insert(pan_) ;

	 cutoff_=new Variable("filter cut",SIP_FILTCUTOFF,0xFF) ;
	 Insert(cutoff_) ;

	 reso_=new Variable("filter res",SIP_FILTRESO,0x00) ;
	 Insert(reso_) ;

	 filterMix_=new Variable("filter type",SIP_FILTMIX,0x00) ;
	 Insert(filterMix_) ;

	 filterMode_=new Variable("filter mode",SIP_FILTMODE,filterMode,3,0) ;
	 Insert(filterMode_) ;

	 attenuate_=new Variable("attenuate",SIP_ATTENUATE,0xFF) ;
	 Insert(attenuate_) ;

	 start_=new WatchedVariable("start",SIP_START,0) ;
	 Insert(start_) ;
	 start_->AddObserver(*this) ;

	 loopMode_=new Variable("loopmode",SIP_LOOPMODE,loopTypes,SILM_LAST,0) ;
	 Insert(loopMode_) ;
	 loopMode_->SetInt(0) ;

	 slices_=new Variable("slices",SIP_SLICES,1) ;
	 Insert(slices_) ;

	 loopBars_=new Variable("loop bars",SIP_LOOPBARS,1,16) ;
	 Insert(loopBars_) ;

	 loopBpm_=new WatchedVariable("loop bpm",SIP_LOOPBPM,0) ;
	 Insert(loopBpm_) ;
	 loopBpm_->AddObserver(*this) ;

	 fromBar_=new Variable("from bar",SIP_FROMBAR,1) ;
	 Insert(fromBar_) ;

	 static const char *warpTypes[2]={"re-pitch","complex"} ;
	 warp_=new Variable("warp",SIP_WARP,warpTypes,2,0) ;
	 Insert(warp_) ;
	 transpose_=new Variable("transpose",SIP_TRANSPOSE,0) ;
	 Insert(transpose_) ;
	 clipBars_=new Variable("length",SIP_CLIPBARS,0) ;
	 Insert(clipBars_) ;

	 // trk2als: Sampler estilo Simpler (pistas MIDI). Se guardan aparte de los
	 // de LGPT; InitSimpler() los deduce de ellos la primera vez
	 static const char *simplerModes[4]={"classic","one-shot","slice","sinte"} ;
	 static const char *offOn[2]={"off","on"} ;
	 static const char *trigModes[2]={"trigger","gate"} ;
	 static const char *filterTypes[3]={"LP","BP","HP"} ;
	 Insert(new Variable("s init",SIP_S_INIT,0)) ;
	 Insert(new Variable("s mode",SIP_S_MODE,simplerModes,4,SM_CLASSIC)) ;
	 Insert(new Variable("s start",SIP_S_START,0)) ;
	 Insert(new Variable("s end",SIP_S_END,100)) ;
	 Insert(new Variable("s startf",SIP_S_STARTF,0)) ;
	 Insert(new Variable("s endf",SIP_S_ENDF,0)) ;
	 Insert(new Variable("s loop",SIP_S_LOOP,offOn,2,0)) ;
	 Insert(new Variable("s loop len",SIP_S_LLEN,100)) ;
	 Insert(new Variable("s trigger",SIP_S_TRIG,trigModes,2,0)) ;
	 Insert(new Variable("s slices",SIP_S_SLICES,4)) ;
	 static const char *sliceBy[2]={"region","manual"} ;
	 Insert(new Variable("s slice by",SIP_S_SLICEBY,sliceBy,2,SSB_REGION)) ;
	 Insert(new Variable("s marks",SIP_S_MARKS,"")) ;
	 Insert(new Variable("s snap",SIP_S_SNAP,offOn,2,1)) ;
	 Insert(new Variable("s motor",SIP_S_SENG,0)) ;      // trk2als: sinte: motor (0..15) y macros (0..100)
	 Insert(new Variable("s armonicos",SIP_S_SHARM,50)) ;
	 Insert(new Variable("s timbre",SIP_S_STIMB,50)) ;
	 Insert(new Variable("s morph",SIP_S_SMORPH,50)) ;
	 Insert(new Variable("s poly",SIP_S_POLY,4)) ;   // voces: hasta 4 notas a la vez   // por defecto: al cruce por cero
	 Insert(new Variable("audio clip",SIP_CLIP,0)) ;
	 Insert(new Variable("s zones",SIP_S_ZONES,"")) ;
	 Insert(new Variable("s kit",SIP_S_KIT,0)) ;
	 Insert(new Variable("s pads",SIP_S_PADS,"")) ;
	 Insert(new Variable("s kitname",SIP_S_KITNAME,"")) ;
	 Insert(new Variable("s transpose",SIP_S_TRANSP,0)) ;
	 Insert(new Variable("s detune",SIP_S_DETUNE,0)) ;
	 Insert(new Variable("s glide",SIP_S_GLIDE,0)) ;
	 Insert(new Variable("s filter",SIP_S_FTYPE,filterTypes,3,0)) ;
	 Insert(new Variable("s freq",SIP_S_FFREQ,127)) ;
	 Insert(new Variable("s res",SIP_S_FRES,0)) ;
	 Insert(new Variable("s env",SIP_S_FENV,0)) ;
	 Insert(new Variable("s volume",SIP_S_VOL,0)) ;
	 Insert(new Variable("s pan",SIP_S_PAN,0)) ;
	 Insert(new Variable("s attack",SIP_S_ATK,0)) ;
	 Insert(new Variable("s decay",SIP_S_DEC,600)) ;
	 Insert(new Variable("s sustain",SIP_S_SUS,0)) ;
	 Insert(new Variable("s release",SIP_S_REL,50)) ;
	 Insert(new Variable("s fade in",SIP_S_FIN,0)) ;
	 Insert(new Variable("s fade out",SIP_S_FOUT,0)) ;
	 Insert(new Variable("s velocity",SIP_S_VEL,35)) ;
	 static const char *simplerWarps[3]={"off","re-pitch","complex"} ;
	 Insert(new Variable("s warp",SIP_S_WARP,simplerWarps,3,SW_OFF)) ;
	 Insert(new Variable("s bpm",SIP_S_BPM,0)) ;
	 Insert(new Variable("s voices",SIP_S_VOICES,1)) ;
	 Insert(new Variable("s cutoff",SIP_S_FHZ,22000)) ;
	 Insert(new Variable("s reso",SIP_S_FQ,0)) ;
	 Insert(new Variable("s fenv",SIP_S_FST,0)) ;
	 Insert(new Variable("s filter v",SIP_S_FVER,0)) ;
	 Insert(new Variable("s f attack",SIP_S_FATK,0)) ;
	 Insert(new Variable("s f decay",SIP_S_FDEC,600)) ;
	 Insert(new Variable("s f sustain",SIP_S_FSUS,0)) ;
	 Insert(new Variable("s f release",SIP_S_FREL,50)) ;
	 Insert(new Variable("s key",SIP_S_FKEY,0)) ;
	 Insert(new Variable("s f velocity",SIP_S_FVEL,0)) ;
	 Insert(new Variable("s fenv v",SIP_S_FEVER,0)) ;
	 static const char *envTabs[2]={"amp","filter"} ;
	 Insert(new Variable("s env tab",SIP_S_ETAB,envTabs,2,0)) ;
	 Insert(new Variable("a gain",SIP_A_GAIN,0)) ;
	 Insert(new Variable("a detune",SIP_A_DET,0)) ;
	 static const char *onOff[2]={"off","on"} ;
	 Insert(new Variable("a fade",SIP_A_FADE,onOff,2,1)) ;
	 Insert(new Variable("a init",SIP_A_INIT,0)) ;

	 loopStart_=new WatchedVariable("loopstart",SIP_LOOPSTART,0) ;
	 Insert(loopStart_) ;
	 loopStart_->AddObserver(*this) ;

	 loopEnd_=new WatchedVariable("end",SIP_END,0) ;
	 Insert(loopEnd_) ;
	 loopEnd_->AddObserver(*this) ;

	 table_=new Variable("table",SIP_TABLE,-1) ;
	 Insert(table_) ;

	 tableAuto_=new Variable("table automation",SIP_TABLEAUTO,false) ;
	 Insert(tableAuto_) ;

	 fbTune_=new Variable("feedback tune",SIP_FBTUNE,0xB0) ;
	 Insert(fbTune_) ;

	 fbMix_=new Variable("feedback mix",SIP_FBMIX,0x00) ;
	 Insert(fbMix_) ;

     printFx_ = new Variable("print fx", SIP_PRINTFX, fxPresets, 4, 3);
     Insert(printFx_);

     irPad_ = new Variable("pad with silence", SIP_IR_PAD, 0);
     Insert(irPad_);

     irWet_ = new Variable("effect amount", SIP_IR_WET, 45);
     Insert(irWet_);

     // Initalize instrument's voices update list

     for (int i = 0; i < SI_VOICE_COUNT; i++) {
         renderParams *rp = renderParams_ + i;
         rp->updaters_.push_back(&rp->volumeRamp_);
         rp->updaters_.push_back(&rp->panner_);
         rp->updaters_.push_back(&rp->cutRamp_);
         rp->updaters_.push_back(&rp->resRamp_);
         rp->updaters_.push_back(&rp->fbMixRamp_);
         rp->updaters_.push_back(&rp->fbTunRamp_);
         rp->updaters_.push_back(&rp->arp_);
         rp->updaters_.push_back(&rp->speedRamp_);
         rp->updaters_.push_back(&rp->legato_);
         rp->updaters_.push_back(&rp->pfin_);
	} ;

 // Reset table state

	tableState_.Reset() ;
}

SampleInstrument::~SampleInstrument() {
	for (int ch=0;ch<SI_VOICE_COUNT;ch++) {   // trk2als
		delete stretch_[ch] ;
		dropCache(renderParams_+ch) ;
	}
}

bool SampleInstrument::Init() {

	SamplePool *pool=SamplePool::GetInstance() ;
    Variable *vSample=FindVariable(SIP_SAMPLE) ;
	NAssert(vSample) ;
	int index=vSample->GetInt() ;
	source_=(index>=0)?pool->GetSource(index):0 ;
	tableState_.Reset() ;
	return false ;
}

void SampleInstrument::OnStart() {
	tableState_.Reset() ;
} ;

bool SampleInstrument::Start(int channel, unsigned char midinote, int flags) {
    bool cleanstart = flags & 1;

    // Look if we're dirty & need to update this instrument's data

  if (dirty_) 
  {
  updateInstrumentData(false) ;
  }

  running_=true ;

  // trk2als: el sinte no necesita sample: en un Sampler vacío suena sobre un
  // segundo de silencio (lo que se oye lo escribe el sinte)
  {
      bool synthMode=!zoned_ && FindVariable(SIP_S_MODE) && FindVariable(SIP_S_MODE)->GetInt()==SM_SYNTH ;
      if (source_==0 && synthMode) source_=&silentSource ;
      else if (source_==&silentSource && !synthMode) source_=0 ;
  }
  if (source_==0) return false ;

  // trk2als: kit: el golpe va a una voz libre del canal (no corta a los otros)
  if (channel<SONG_CHANNEL_COUNT && !kitStarting_ && IsKit()) {
      return startKit(channel,midinote,-1,flags) ;
  }

  // Get Rendering params for current voice & fill init data

  renderParams *rp=renderParams_+channel ;

  // trk2als: la nota que sonaba pasa a la voz de cola ANTES de tocar rp:
  // así se lleva su volumen, paneo, nota y región (si no, la cola sonaba
  // con los valores de la nota nueva: click y voces raras)
     bool toTail=false ;   // la nota que sonaba siguió en la voz de cola
     bool wasSounding=rp->simpler_ && !rp->finished_ && rp->envStage_<4 &&
                     voiceClock_[channel]!=0 ;
     fixed prevSpeed=rp->speed_ ;
     // trk2als: glide solo de una nota ENCIMADA con la anterior (legato). Antes
     // de cada nota el reproductor siempre suelta la que hubiera, así que una
     // sola soltada = seguía apretada; dos = ya había terminado (aunque su
     // cola o su sample sigan sonando) y entonces no se desliza
     // ... salvo que acabara justo donde empieza esta (notas pegadas, sin
     // hueco): eso también es legato
     bool legato=(rp->stopsSinceStart_<=1) ||
                 (rp->stopsSinceStart_==2 && sliceClock_-rp->stopSlice_<=1 &&
                  System::GetInstance()->GetClock()-rp->stopMs_<=60) ;
     rp->stopsSinceStart_=0 ;
     {
         static int dbgG = -1 ;
         if (dbgG < 0) dbgG = getenv("DBGTAIL") ? 1 : 0 ;
         if (dbgG && channel<SONG_CHANNEL_COUNT) fprintf(stderr,"[DIAG] glide: sonaba=%d legato=%d\n",(int)wasSounding,(int)legato) ;
     }
     // trk2als: con 2 voces, la nota que sonaba sigue como cola (su release,
     // o completa en trigger) en vez de cortarse; con glide no: se desliza
     // trk2als: clip de audio que sigue sonando: pasa a la voz de cola y se
     // apaga en 4 ms mientras entra el nuevo (sin click)
     {   // trk2als: diagnóstico (DIAG=YES): cómo estaba la voz al relanzar
         static int dbg = -1 ;
         if (dbg < 0) dbg = getenv("DBGTAIL") ? 1 : 0 ;
         if (dbg && channel<SONG_CHANNEL_COUNT)
             fprintf(stderr,"[DIAG] %lu start ch%d %s clip=%d fin=%d sale=%d/%d hace=%lums pos=%.0f\n",
                     System::GetInstance()->GetClock(),channel,GetName(),(int)rp->audioClip_,(int)rp->finished_,
                     rp->declickOut_,rp->declickOutLen_,voiceClock_[channel]?System::GetInstance()->GetClock()-voiceClock_[channel]:0,
                     (float)rp->position_) ;
     }
     if (!kitStarting_ && channel<SONG_CHANNEL_COUNT && rp->audioClip_ && !rp->finished_ &&
         voiceClock_[channel]!=0 &&
         System::GetInstance()->GetClock()-voiceClock_[channel]<200) {
         startTail(channel) ;
         toTail=true ;
         renderParams *tp=renderParams_+channel+SONG_CHANNEL_COUNT ;
         tp->declickOut_=tp->declickOutLen_=176 ;
     }
     if (channel<SONG_CHANNEL_COUNT && !kitStarting_) {
         int t=channel+SONG_CHANNEL_COUNT ;
         unsigned long now=System::GetInstance()->GetClock() ;
         // una cola que nadie renderiza hace rato (cambió el instrumento del
         // canal) no debe reaparecer
         if (!renderParams_[t].finished_ && (voiceClock_[t]==0 || now-voiceClock_[t]>200)) {
             renderParams_[t].finished_=true ;
         }
         // con glide la nota nueva también arranca desde start: la anterior
         // se apaga en la cola (sin click) y solo el tono se desliza
         if (wasSounding) {
             // glide: solo en mono, y nunca entre rebanadas (como Live)
             bool glide=legato && GetSimpler(SIP_S_GLIDE)>0 && GetSimpler(SIP_S_POLY)<2 &&
                        (IsZoned() || GetSimpler(SIP_S_MODE)!=SM_SLICE) ;
             startTail(channel) ;
             toTail=true ;
             // slice: una rebanada a la vez (la nueva corta a la anterior)
             bool oneVoice=voices()<2 ;
             if (oneVoice || glide) {
                 // una voz: la nota anterior no se corta de golpe (click),
                 // se apaga en 4 ms mientras entra la nueva
                 renderParams *tp=renderParams_+channel+SONG_CHANNEL_COUNT ;
                 // (también si ya venía soltándose, en gate: desde el nivel
                 // que lleva; si no, al acortarle el release caía a cero de
                 // golpe: el click de la nota cercana)
                 if (tp->envStage_<4) {
                     tp->envReleaseFrom_=tp->envLevel_ ;
                     tp->envStage_=3 ;
                     tp->envClock_=0 ;
                 }
                 tp->chokeMs_=10 ;
             }
             if (!glide) wasSounding=false ;
         }
     }

  // trk2als: la voz se va a pisar y lo que sonaba no pasó a la cola (voces de
  // acorde, golpes del kit, la vuelta del clip): su último valor se apaga en
  // 5 ms en vez de cortarse en seco (click)
  if (!toTail) stealVoice(channel) ;
  ensureZones() ;   // trk2als
 rp->midiNote_=midinote ;
  lastNote_=midinote ;   // trk2als: para el mapa de zonas
  
  if (lastMidiNote_[channel] == -1) // To prevent First LEGA to go bonkers
  {
    lastMidiNote_[channel]=midinote ;
  }

 // Duplicate variable value to local rendering
 // to alter with commands

	 rp->sampleBuffer_=source_->GetSampleBuffer(rp->midiNote_) ;
	 if (rp->sampleBuffer_==0) {
		 return false ;
	 } ;
	 rp->channelCount_=source_->GetChannelCount(rp->midiNote_) ;

	 int rootNote=(rootNote_->GetInt()-60)+source_->GetRootNote(rp->midiNote_) ;

	 rp->volume_=rp->baseVolume_=i2fp(volume_->GetInt()) ;
	 rp->attenuate_=i2fp(attenuate_->GetInt()) ;
	 // trk2als: clip de audio: la ganancia va en dB (0 dB = el 128 de LGPT)
	 // (en una pista de audio la voz va en looper sync aunque el loop mode del
	 // instrumento diga otra cosa: los clips de audio casi siempre dicen "none")
	 bool audioClip=(loopMode_->GetInt()==SILM_LOOPSYNC) ||
	                (channel<SONG_CHANNEL_COUNT && syncRequest_[channel]) ;
	 if (audioClip) {
		 InitAudioClip() ;
		 float lin=128.0f*powf(10.0f,GetSimpler(SIP_A_GAIN)/20.0f) ;
		 if (lin>255) lin=255 ;
		 rp->volume_=rp->baseVolume_=fl2fp(lin) ;
	 }

	 rp->pan_=rp->basePan_=i2fp(pan_->GetInt()) ;

	 if (!source_->IsMulti()) {
		 rp->rendLoopStart_=loopStart_->GetInt();
 		 rp->rendLoopEnd_=loopEnd_->GetInt() ;
	 } else {
		 long start=source_->GetLoopStart(rp->midiNote_)  ;
		 if (start>0) {
			 rp->rendLoopStart_=source_->GetLoopStart(rp->midiNote_); // hack at the moment
			 rp->rendLoopEnd_=source_->GetLoopEnd(rp->midiNote_) ;
			 loopMode_->SetInt(SILM_LOOP) ;
		 } else {
			 rp->rendLoopStart_=0; // hack at the moment
			 rp->rendLoopEnd_=source_->GetSize(rp->midiNote_) ; // hack at the moment
			 loopMode_->SetInt(SILM_ONESHOT) ;
		 } ;
	 } 
     rp->forceSync_=(channel<SONG_CHANNEL_COUNT)?syncRequest_[channel]:false ; // trk2als: pista de audio
     // trk2als: en las pistas MIDI el instrumento es el Sampler estilo Simpler
     // (salvo instrumentos viejos en looper sync)
     rp->audioClip_=false ;
     rp->declickIn_=-1 ;
     rp->declickOut_=rp->declickOutLen_=0 ;   // (si no, la salida de la voz anterior callaba a la nueva)
     rp->declickDelay_=0 ;
     if (audioClip && GetSimpler(SIP_A_FADE)) {
         rp->audioClip_=true ;
         rp->declickIn_=0 ;
     }
     rp->simpler_=!rp->forceSync_ && loopMode_->GetInt()!=SILM_LOOPSYNC &&
                  !source_->IsMulti() ;
     int firstPos=start_->GetInt() ;
     bool simplerSliced=false ;
     float simplerOffset=0 ;
     if (rp->simpler_) {
         InitSimpler() ;
         if (!startSimpler(rp,channel,midinote,firstPos,simplerSliced,simplerOffset)) {
             return false ;   // nota fuera de las rebanadas
         }
     }
     SampleInstrumentLoopMode loopmode=effectiveLoopMode(rp) ;

/*	 if (loopmode==SILM_OSCFINE) {
		if (rp->rendLoopEnd_>source_->GetSize()-1) { // check for older instrument that were not correctly handled
			rp->rendLoopEnd_=source_->GetSize()-1 ;
		}
	 }*/
	 rp->reverse_=false ;

     float driverRate = float(Audio::GetInstance()->GetSampleRate());
     int isSliced = !rp->simpler_ && slices_->GetInt() > 1;
     if (isSliced) {
		 if (rp->midiNote_ > slices_->GetInt() - 1) return false; // No sound outside of slice range
		 int slice = rp->rendLoopEnd_ / slices_->GetInt();
		 rp->rendLoopStart_ = rp->midiNote_ * slice;
		 rp->rendLoopEnd_ = (rp->midiNote_ + 1) * slice;
	 }

	 switch (loopmode) {
		 case SILM_ONESHOT:
		 case SILM_LOOP:
         case SILM_LOOP_PINGPONG:
             // Compute speed factor
             // if instrument sampled below 44.1Khz, should
             // travel slower in sample

             rp->rendFirst_ =
                 (isSliced) ? rp->rendLoopStart_ : firstPos;
             rp->position_ = float(rp->rendFirst_);
             rp->baseSpeed_ =
                 fl2fp(source_->GetSampleRate(rp->midiNote_) / driverRate);
             rp->reverse_ = (rp->rendLoopEnd_ < rp->position_);

             break;

         case SILM_OSC:
             //		case SILM_OSCFINE:
             {

                 float freq = 261.6255653006f; // C3
                 /*			if (loopmode==SILM_OSCFINE) {
                                 freq=float(pow(2.0,-0.75))*440; // C3
                             }*/
                 int length = rp->rendLoopEnd_ - rp->rendLoopStart_;
                 if (length == 0)
                     length = 1;
                 if (length < 0) {
                     rp->reverse_ = true;
                     length = -length;
                 };
                 rp->baseSpeed_ = fl2fp((freq * length) / driverRate);
                 rp->rendFirst_ = rp->rendLoopStart_;

                 if (cleanstart) {
                     rp->position_ = float(rp->rendFirst_);
                 }
                 break;
             }
		case SILM_LOOPSYNC:
		{
			int length=rp->rendLoopEnd_-rp->rendLoopStart_ ;
			if (length<0) {
				 rp->reverse_=true ;
				 length=-length ;
			} ;
			SyncMaster *sm=SyncMaster::GetInstance() ;
			int sampleCount=int(sm->GetTickSampleCount()) ;
			sampleCount*=(6*16) ; 
			{ // trk2als: la region dura 'loop bars' compases (1 por defecto)
				int bars=loopBars_->GetInt() ;
				sampleCount*=(bars<1)?1:bars ;
			}
			rp->baseSpeed_=fl2fp(length/float(sampleCount)) ;
			rp->syncTickSamples_=sm->GetTickSampleCount() ; // trk2als: para seguir cambios de tempo
      rp->rendFirst_ = rp->rendLoopStart_;
      if (cleanstart) {
        rp->position_= float(rp->rendFirst_);
      }
      break ;
        }
        case SILM_LAST:
			NAssert(0) ;
			break ;
        }

        // Compute octave & note difference from root
        float fineTune = float(fineTune_->GetInt() - 0x7F);
        fineTune /= float(0x80);
        int offset = midinote - rootNote;
        if (isSliced) {
            offset = rootNote - source_->GetRootNote(rp->midiNote_);
    }
    while (offset > 127) {
        offset -= 12;
    }

    float semis=offset+fineTune ;
    if (rp->simpler_) semis=simplerOffset ;   // trk2als: transpose/detune del Sampler
    fixed freqFactor=fl2fp(float(pow(2.0,semis/12.0))) ;
	rp->baseSpeed_=fp_mul(rp->baseSpeed_,freqFactor) ;
    rp->speed_=rp->baseSpeed_ ;

    // trk2als: glide desde la nota anterior (si seguía sonando)
    rp->glideLeft_=0 ;
    if (rp->simpler_ && wasSounding && legato && GetSimpler(SIP_S_GLIDE)>0 && GetSimpler(SIP_S_POLY)<2 &&
        (IsZoned() || GetSimpler(SIP_S_MODE)!=SM_SLICE)) {
        rp->glideFrom_=prevSpeed ;
        rp->glideLen_=rp->glideLeft_=int(GetSimpler(SIP_S_GLIDE)*44.1f) ;
        rp->speed_=prevSpeed ;
    }
    // trk2als: Sampler con warp. La región dura lo mismo en tiempo musical
    // a cualquier tempo; re-pitch la acelera/frena (y no sigue las notas),
    // complex la estira y transpone por separado
    rp->sWarp_=SW_OFF ;
    rp->warpTranspose_=1.0f ;
    if (rp->simpler_ && !zoned_ && GetSimpler(SIP_S_WARP)!=SW_OFF) {   // también en slice; las zonas no
        int bpm=GetSimpler(SIP_S_BPM) ;
        if (bpm<=0) bpm=GuessSimplerBpm() ;
        float tick=SyncMaster::GetInstance()->GetTickSampleCount() ;
        if (bpm>0 && tick>0) {
            rp->sWarp_=GetSimpler(SIP_S_WARP) ;
            if (rp->sWarp_==SW_COMPLEX && !WarpStretch::Available()) rp->sWarp_=SW_REPITCH ;
            float srcRate=float(source_->GetSampleRate(rp->midiNote_)) ;
            // cuadros del sample por cuadro de salida: un beat del sample
            // (srcRate*60/bpm) en lo que dura un beat del proyecto (24 ticks)
            float tempoSpeed=srcRate*60.0f/bpm/(24.0f*tick) ;
            float prevT=rp->warpTranspose_ ;
            rp->baseSpeed_=rp->speed_=fl2fp(tempoSpeed) ;
            rp->syncTickSamples_=tick ;
            if (rp->sWarp_==SW_COMPLEX) {
                float t=float(pow(2.0,simplerOffset/12.0)) ;
                if (rp->glideLeft_>0) {
                    rp->glideFromT_=lastWarpT_[channel] ;
                    prevT=rp->glideFromT_ ;
                }
                rp->warpTranspose_=(rp->glideLeft_>0)?prevT:t ;
                warpTarget_[channel]=t ;
            } else {
                rp->glideLeft_=0 ;   // re-pitch no sigue las notas: no hay glide
            }
        }
    }
    
  // Init k rate counter
 
    rp->krateCount_=0 ;

	// We allow processing

	rp->finished_=false ;
	if (loopmode!=SILM_LOOPSYNC && rp->sWarp_==SW_OFF) rp->syncTickSamples_=0 ;
	rp->complexPrimed_=false ; // trk2als
	dropCache(rp) ;
	rp->warpFresh_=true ;
	rp->warpReqSpeed_=0 ;
	rp->warpReqAt_=0 ;
	rp->warpWaited_=0 ;
	rp->warpWaitSince_=0 ;
	rp->warpFadeIn_=0 ;
	rp->warpMode_=warp_->GetInt() ;
	rp->justWrapped_=false ;

  // Initialize feedback data

	memset(feedback_[channel],0,FB_BUFFER_LENGTH*2*sizeof(fixed)) ;

	rp->feedbackIn_=0 ;
	rp->feedbackMode_=FB_NONE ;

	rp->baseFbTun_=rp->fbTun_=fl2fp(fbTune_->GetInt()/255.0f) ; 
	rp->baseFbMix_=rp->fbMix_=fl2fp(fbMix_->GetInt()/255.0f) ; 

  // If we do a clean start (there was a instr number on the line)

	if (cleanstart) 
  {

	// Clear retrigger data
	  
		rp->retrig_=false ;
		rp->retrigLoop_=0 ;
		rp->retrigCount_=0 ;
		rp->retrigOffset_=0 ;

	// Could click

		rp->couldClick_=SHOULD_KILL_CLICKS ;

	// Init filter params

		rp->cutoff_=rp->baseFCut_=fl2fp(cutoff_->GetInt()/255.0f); 
		rp->reso_=rp->baseFRes_=fl2fp(reso_->GetInt()/255.0f) ;

	// Init crush params

		rp->crush_=crush_->GetInt() ;
		rp->drive_=drive_->GetInt() ;

	// Init downsampling

		rp->downsample_=downsample_->GetInt() ;

		// Disable all updaters for new voice
		
		std::vector<I_SRPUpdater *>::iterator it ;

		for (it=rp->updaters_.begin();it!=rp->updaters_.end();it++) {
			I_SRPUpdater *current=*it ;
			current->Disable() ;
		}

		rp->activeUpdaters_.clear() ;
	}	
	if (rp->simpler_) {
		// trk2als: el Sampler no tiene crush, drive, downsample ni feedback
		rp->crush_=16 ;
		rp->drive_=0xFF ;
		rp->downsample_=0 ;
		rp->fbMix_=rp->baseFbMix_=0 ;
		rp->attenuate_=i2fp(0xFF) ;
		rp->envStage_=0 ;
		rp->envLevel_=0 ;
		rp->envClock_=0 ;
		rp->chokeMs_=0 ;
		rp->fenvStage_=0 ;
		rp->fenvLevel_=0 ;
		rp->fenvClock_=0 ;
		if (!wasSounding) {
			rp->svfIc1_[0]=rp->svfIc1_[1]=rp->svfIc2_[0]=rp->svfIc2_[1]=0 ;
			rp->svfCur_=-1 ;   // la nota nueva toma el tipo sin fundido
			rp->svfHzS_=0 ;    // ... y el corte sin deslizar
			rp->svfXf_=0 ;
		}
		for (int k=0;k<5;k++) rp->sLive_[k]=-9999 ;
		rp->startDelay_=0 ;
		rp->padDecay_=0 ;   // trk2als: los del pad los pone startKit después
		rp->padClock_=0 ;
		rp->padChoke_=0 ;
		simplerBlock(rp,0) ;
	}
	snapshotLive(rp) ; // trk2als
	return true ;
}

// trk2als: un golpe del kit. Voces del canal: la principal, la de cola y las
// tres de acorde. El mismo pad se reinicia en su voz; si no, una libre; si no
// hay, la que empezó antes
bool SampleInstrument::startKit(int channel,unsigned char note,int velocity,int flags) {
	if (!IsZoned()) return false ;   // kit sin pads: silencio
	int slots[2+SI_CHORD_VOICES] ;
	for (int k=0;k<2+SI_CHORD_VOICES;k++) slots[k]=channel+k*SONG_CHANNEL_COUNT ;
	int pick=-1 ;
	for (int k=0;k<2+SI_CHORD_VOICES && pick<0;k++) {
		renderParams *rp=renderParams_+slots[k] ;
		if (!rp->finished_ && rp->envStage_<4 && rp->midiNote_==note) pick=slots[k] ;
	}
	for (int k=0;k<2+SI_CHORD_VOICES && pick<0;k++) {
		renderParams *rp=renderParams_+slots[k] ;
		if (rp->finished_ || (rp->simpler_ && rp->envStage_>=4)) pick=slots[k] ;
	}
	if (pick<0) {
		pick=slots[0] ;
		for (int k=1;k<2+SI_CHORD_VOICES;k++)
			if (kitStartClock_[slots[k]]<kitStartClock_[pick]) pick=slots[k] ;
	}
	PadParams pp=GetPad(note) ;
	if (pp.choke>0) {   // el pad calla a los otros de su grupo (hat abierto / cerrado)
		for (int k=0;k<2+SI_CHORD_VOICES;k++) {
			renderParams *o=renderParams_+slots[k] ;
			if (slots[k]==pick || o->finished_ || o->envStage_>=3) continue ;
			if (o->padChoke_==pp.choke) {
				o->envReleaseFrom_=o->envLevel_ ;
				o->envStage_=3 ;
				o->envClock_=0 ;
				o->chokeMs_=8 ;
			}
		}
	}
	kitStarting_=true ;
	bool ok=Start(pick,note,flags) ;
	kitStarting_=false ;
	if (!ok) return false ;
	renderParams *rp=renderParams_+pick ;
	rp->gate_=false ;   // one-shot: no importa el largo de la nota
	rp->padChoke_=pp.choke ;
	rp->padClock_=0 ;
	rp->padDecay_=pp.decay>0?pp.decay*44.1f:0 ;
	if (pp.gain!=0) {   // volumen del pad
		float vol=fp2fl(rp->baseVolume_)*powf(10.0f,pp.gain/20.0f) ;
		if (vol>255.0f) vol=255.0f ;
		rp->volume_=rp->baseVolume_=fl2fp(vol) ;
	}
	if (pp.pan!=0) {
		int pan=int(fp2fl(rp->basePan_))+pp.pan*127/50 ;
		rp->pan_=rp->basePan_=i2fp(pan<0?0:(pan>254?254:pan)) ;
	}
	kitStartClock_[pick]=System::GetInstance()->GetClock()+1 ;
	if (note<128) padHit_[note]=kitStartClock_[pick] ;
	lastKitSlot_[channel]=pick ;
	if (velocity>=0 && velocity!=100) ProcessCommand(pick,I_CMD_MVEL,(ushort)(velocity*2)) ;
	return true ;
}

std::string SampleInstrument::PadName(int note) {
	ensureZones() ;
	for (size_t i=0;i<zones_.zones_.size();i++) {
		const ZoneSource::Zone &z=zones_.zones_[i] ;
		if (note>=z.lo && note<=z.hi) {
			std::string n=z.name ;
			size_t sl=n.rfind('/') ;
			if (sl!=std::string::npos) n=n.substr(sl+1) ;
			size_t dot=n.rfind('.') ;
			if (dot!=std::string::npos) n=n.substr(0,dot) ;
			return n ;
		}
	}
	return "" ;
}

int SampleInstrument::KitBase() {
	ensureZones() ;
	int lo=127, hi=0 ;
	for (size_t i=0;i<zones_.zones_.size();i++) {
		if (zones_.zones_[i].lo<lo) lo=zones_.zones_[i].lo ;
		if (zones_.zones_[i].hi>hi) hi=zones_.zones_[i].hi ;
	}
	if (zones_.zones_.empty() || (lo>=36 && hi<=51)) return 36 ;
	return lo>112?112:lo ;
}

// los mismos valores que deja adg2kit.py en un kit convertido
void SampleInstrument::MakeEmptyKit() {
	Purge() ;
	FindVariable(SIP_SAMPLE)->SetInt(-1) ;
	const char *params[][2]={{"s init","1"},{"s mode","classic"},{"s trigger","trigger"},
		{"s kit","1"},{"s attack","0"},{"s decay","0"},{"s sustain","0"},{"s release","0"},
		{"s volume","0"},{"s voices","2"},{"s warp","off"},{"s pads",""},{"s kitname","KIT NUEVO"}} ;
	for (unsigned k=0;k<sizeof(params)/sizeof(params[0]);k++) {
		IteratorPtr<Variable> it(GetIterator()) ;
		for (it->Begin();!it->IsDone();it->Next()) {
			Variable &var=it->CurrentItem() ;
			if (!strcmp(var.GetName(),params[k][0])) { var.SetString(params[k][1]) ; break ; }
		}
	}
	SetZones("") ;
	padsStr_="" ; pads_.clear() ;
}

void SampleInstrument::parsePads() {
	const char *str=FindVariable(SIP_S_PADS)->GetString() ;
	if (padsStr_==str) return ;
	padsStr_=str ;
	pads_.clear() ;
	std::string s=str ;
	size_t pos=0 ;
	while (pos<s.size()) {
		size_t e=s.find(';',pos) ;
		if (e==std::string::npos) e=s.size() ;
		std::string item=s.substr(pos,e-pos) ;
		int n,g,t,p,d,c,st ;
		if (sscanf(item.c_str(),"%d:%d:%d:%d:%d:%d:%d",&n,&g,&t,&p,&d,&c,&st)==7) {
			PadParams pp ;
			pp.gain=g ; pp.tune=t ; pp.pan=p ; pp.decay=d ; pp.choke=c ; pp.start=st ;
			pads_[n]=pp ;
		}
		pos=e+1 ;
	}
}

PadParams SampleInstrument::GetPad(int note) {
	parsePads() ;
	std::map<int,PadParams>::iterator it=pads_.find(note) ;
	return it==pads_.end()?PadParams():it->second ;
}

void SampleInstrument::SetPad(int note,const PadParams &p) {
	parsePads() ;
	pads_[note]=p ;
	std::string out ;
	char buf[64] ;
	for (std::map<int,PadParams>::iterator it=pads_.begin();it!=pads_.end();it++) {
		const PadParams &q=it->second ;
		if (q.gain==0 && q.tune==0 && q.pan==0 && q.decay==0 && q.choke==0 && q.start==0) continue ;
		snprintf(buf,sizeof(buf),"%d:%d:%d:%d:%d:%d:%d",it->first,q.gain,q.tune,q.pan,q.decay,q.choke,q.start) ;
		if (!out.empty()) out+=";" ;
		out+=buf ;
	}
	FindVariable(SIP_S_PADS)->SetString(out.c_str()) ;
	padsStr_=out ;
}

std::string SampleInstrument::PadSample(int note) {
	ensureZones() ;
	for (size_t i=0;i<zones_.zones_.size();i++)
		if (zones_.zones_[i].lo==note && zones_.zones_[i].hi==note) return zones_.zones_[i].name ;
	return "" ;
}

// cambia (o quita) el sample de un pad y reescribe 's zones'
void SampleInstrument::SetPadSample(int note,const std::string &wav) {
	ensureZones() ;
	std::string out ;
	char buf[32] ;
	bool done=false ;
	std::vector<std::pair<int,std::string> > pads ;
	for (size_t i=0;i<zones_.zones_.size();i++) {
		const ZoneSource::Zone &z=zones_.zones_[i] ;
		if (z.lo==note) { done=true ; if (!wav.empty()) pads.push_back(std::make_pair(note,wav)) ; continue ; }
		pads.push_back(std::make_pair(z.lo,z.name)) ;
	}
	if (!done && !wav.empty()) pads.push_back(std::make_pair(note,wav)) ;
	std::sort(pads.begin(),pads.end()) ;
	for (size_t i=0;i<pads.size();i++) {
		snprintf(buf,sizeof(buf),"%d,%d,%d,0.0,-1,-1,",pads[i].first,pads[i].first,pads[i].first) ;
		if (!out.empty()) out+=";" ;
		out+=buf ;
		out+=pads[i].second ;
	}
	SetZones(out) ;
	if (!pads.empty()) {   // el 'sample' del instrumento: el del primer pad (para la lista)
		int idx=SamplePool::GetInstance()->IndexOf(pads[0].second.c_str()) ;
		if (idx>=0) FindVariable(SIP_SAMPLE)->SetInt(idx) ;
	}
}

void SampleInstrument::Stop(int channel) {
	if (channel<SONG_CHANNEL_COUNT && IsKit()) return ;   // trk2als: kit: los golpes suenan completos

	// Get Rendering params for current voice & fill init data

	 renderParams *rp=renderParams_+channel ;
	 running_=false ;
	 if (rp->stopsSinceStart_==0) { rp->stopSlice_=sliceClock_ ; rp->stopMs_=System::GetInstance()->GetClock() ; }
	 rp->stopsSinceStart_++ ;
	 // trk2als: el Sampler suelta la nota con su release (o la deja sonar
	 // completa en modo trigger)
	 // trk2als: clip de audio con fade: se apaga en 10 ms en vez de cortarse
	 if (rp->audioClip_ && !rp->finished_) {
		 if (rp->declickOutLen_==0) rp->declickOut_=rp->declickOutLen_=441 ;
		 rp->declickDelay_=0 ;   // (si había uno anunciado, ya)
		 return ;
	 }
	 if (rp->simpler_ && !rp->finished_) {
		 if (rp->gate_ && rp->envStage_<3) {
			 rp->envReleaseFrom_=rp->envLevel_ ;
			 rp->envStage_=3 ;
			 rp->envClock_=0 ;
			 rp->fenvReleaseFrom_=rp->fenvLevel_ ;
			 rp->fenvStage_=3 ;
			 rp->fenvClock_=0 ;
		 }
		 return ;
	 }
	 voiceClock_[channel]=0 ;
}

// trk2als: voces de acorde. Cada una es una voz más del canal (sin cola:
// suelta con su release y, si vuelve a sonar, arranca de nuevo)
bool SampleInstrument::StartVoice(int channel,int voice,unsigned char note,int velocity,int flags) {
	if (voice<1 || voice>SI_CHORD_VOICES || channel<0 || channel>=SONG_CHANNEL_COUNT) return false ;
	if (IsKit()) return startKit(channel,note,velocity,flags) ;
	int idx=ChordIdx(channel,voice) ;
	bool ok=Start(idx,note,flags) ;
	if (ok && velocity>=0 && velocity!=100) ProcessCommand(idx,I_CMD_MVEL,(ushort)(velocity*2)) ;
	return ok ;
}

void SampleInstrument::StopVoice(int channel,int voice) {
	if (voice<1 || voice>SI_CHORD_VOICES || channel<0 || channel>=SONG_CHANNEL_COUNT) return ;
	if (IsKit()) return ;   // los golpes suenan completos
	Stop(ChordIdx(channel,voice)) ;
}

void SampleInstrument::Choke(int channel) {
	for (int v=0;v<2+SI_CHORD_VOICES;v++) {
		int idx=channel+v*SONG_CHANNEL_COUNT ;
		if (idx>=SI_VOICE_COUNT) break ;
		renderParams *rp=renderParams_+idx ;
		if (rp->simpler_ && !rp->finished_ && rp->envStage_<4) {
			rp->envReleaseFrom_=rp->envLevel_ ;
			rp->envStage_=3 ;
			rp->envClock_=0 ;
			rp->fenvReleaseFrom_=rp->fenvLevel_ ;
			rp->fenvStage_=3 ;
			rp->fenvClock_=0 ;
			rp->chokeMs_=10 ;
		}
		if (rp->audioClip_ && !rp->finished_ && rp->declickOutLen_==0) {
			rp->declickOut_=rp->declickOutLen_=441 ;
		}
	}
}

static bool voiceAlive(renderParams *rp) {   // trk2als
	if (rp->finished_) return false ;
	if (rp->simpler_) return rp->envStage_<4 ;
	return true ;
}

bool SampleInstrument::Releasing(int channel) {
	renderParams *rp=renderParams_+channel ;
	if (channel<SONG_CHANNEL_COUNT && tailActive(channel)) return true ;
	if (channel<SONG_CHANNEL_COUNT)   // trk2als: un acorde que sigue sonando
		for (int v=1;v<=SI_CHORD_VOICES;v++)
			if (voiceAlive(renderParams_+ChordIdx(channel,v))) return true ;
	if (rp->audioClip_ && !rp->finished_ && rp->declickOutLen_>0) return true ;
	return rp->simpler_ && !rp->finished_ && rp->envStage_<4 ;
}

int SampleInstrument::GetPlayPosition() {
	unsigned long now=System::GetInstance()->GetClock() ;
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
		if (voiceClock_[ch] && now-voiceClock_[ch]<250) {
			return int(renderParams_[ch].position_) ;
		}
	}
	return -1 ;
}

void SampleInstrument::doTickUpdate(int channel) {

  // Process updaters

	renderParams *rp=renderParams_+channel ;
	std::vector<I_SRPUpdater *>::iterator it ;

	for (it=rp->activeUpdaters_.begin();it!=rp->activeUpdaters_.end();it++) {
		I_SRPUpdater *current=*it ;
		current->Trigger(true) ;
	}
} ;

void SampleInstrument::doKRateUpdate(int channel) {

	renderParams *rp=renderParams_+channel ;
	std::vector<I_SRPUpdater *>::iterator it ;

	for (it=rp->activeUpdaters_.begin();it!=rp->activeUpdaters_.end();it++) {
		I_SRPUpdater *current=*it;
		current->Trigger(false) ;
	}
	
} ;

void SampleInstrument::updateFeedback(renderParams *rp) {

	 SampleInstrumentLoopMode loopMode=effectiveLoopMode(rp) ;

	 if (rp->fbMix_!=0) {
		int offset=fp2i(fp_mul(rp->fbTun_,fl2fp(255.0f))) ;
		
		switch(loopMode) {
			case SILM_ONESHOT:
			case SILM_LOOP:
            case SILM_LOOP_PINGPONG:
            case SILM_LOOPSYNC:
                rp->feedbackMode_ = FB_ADD;
                if (offset < 0x80) {
                    offset = FB_BUFFER_LENGTH - offset - 1;
                } else {
                    offset = FB_BUFFER_LENGTH - offset * 10 - 1;
                }
                break;
			case SILM_OSC:
//			case SILM_OSCFINE:
				if (offset<0x80) {
					offset=FB_BUFFER_LENGTH-offset-1 ;
					rp->feedbackMode_=FB_ADD ;
				} else {
					offset=FB_BUFFER_LENGTH-(0x100-offset) ;
					rp->feedbackMode_=FB_SUB ;
				}
				break ;
			case SILM_LAST:
				NAssert(0) ;
				break ;
		}
		rp->feedbackOut_=rp->feedbackIn_+offset ;
		if (rp->feedbackOut_>=FB_BUFFER_LENGTH) {
			rp->feedbackOut_-=FB_BUFFER_LENGTH ;
		}
	} ;
}

// Size in samples

// trk2als: la voz del canal y, si la hay, la cola de la nota anterior
bool SampleInstrument::Render(int channel, fixed *buffer, int size, int flags) {
    bool main=renderVoice(channel,buffer,size,flags) ;
    if (channel<SONG_CHANNEL_COUNT) {   // trk2als: las voces del acorde
        for (int v=1;v<=SI_CHORD_VOICES;v++) {
            int idx=ChordIdx(channel,v) ;
            if (!voiceAlive(renderParams_+idx)) continue ;
            if ((int)tailBuffer_.size()<size*2) tailBuffer_.resize(size*2) ;
            bool on=renderVoice(idx,&tailBuffer_[0],size,flags) ;
            if (!on) continue ;
            if (!main) memcpy(buffer,&tailBuffer_[0],size*2*sizeof(fixed)) ;
            else for (int i=0;i<size*2;i++) buffer[i]+=tailBuffer_[i] ;
            main=true ;
        }
    }
    if (channel>=SONG_CHANNEL_COUNT || !tailActive(channel)) return main ;
    int t=channel+SONG_CHANNEL_COUNT ;
    if ((int)tailBuffer_.size()<size*2) tailBuffer_.resize(size*2) ;
    bool tail=renderVoice(t,&tailBuffer_[0],size,flags) ;
    if (!tail) return main ;
    if (!main) {
        memcpy(buffer,&tailBuffer_[0],size*2*sizeof(fixed)) ;
    } else {
        for (int i=0;i<size*2;i++) buffer[i]+=tailBuffer_[i] ;
    }
    return true ;
}

bool SampleInstrument::tailActive(int channel) {
    renderParams *tp=renderParams_+channel+SONG_CHANNEL_COUNT ;
    if (tp->finished_) return false ;
    if (tp->simpler_) return tp->envStage_<4 ;
    return tp->audioClip_ && tp->declickOut_>0 ;   // clip de audio apagándose
}

// trk2als: clip de audio con 'fade: on': 2 ms de entrada y, al cortarse o
// detenerse, una salida corta (como los fades de clip de Live)
void SampleInstrument::declickVoice(renderParams *rp,fixed *buffer,int size) {
	const int inLen=88 ;
	for (int i=0;i<size;i++) {
		float g=1.0f ;
		if (rp->declickIn_>=0) {
			g=float(rp->declickIn_)/inLen ;
			if (++rp->declickIn_>=inLen) rp->declickIn_=-1 ;
		}
		if (rp->declickOutLen_>0 && rp->declickDelay_>0) {
			rp->declickDelay_-- ;     // el fade anunciado todavía no empieza
		} else if (rp->declickOutLen_>0) {
			if (rp->declickOut_<=0) {
				g=0 ;                     // ya se apagó: silencio hasta el final del bloque
				rp->declickOut_=-1 ;
			} else {
				// curva suave (coseno): la bajada lineal corta se oía como "tic"
				g*=0.5f*(1.0f-cosf(3.14159265f*float(rp->declickOut_)/rp->declickOutLen_)) ;
				rp->declickOut_-- ;
				if (rp->declickOut_==0) rp->declickOut_=-1 ;   // terminó
			}
		}
		if (g<1.0f) {
			fixed f=fl2fp(g) ;
			buffer[2*i]=fp_mul(buffer[2*i],f) ;
			buffer[2*i+1]=fp_mul(buffer[2*i+1],f) ;
		}
	}
	if (rp->declickOut_<0) {   // la salida terminó: la voz se calla
		rp->declickOut_=0 ;
		rp->declickOutLen_=0 ;
		rp->finished_=true ;
	}
}

// trk2als: el clip de audio del canal va a cambiar (relanzarse, otro clip o
// detenerse) en el compás: se apaga en len muestras terminando justo ahí. Si
// no, la nota que el clip toca en ese uno alcanzaba a sonar unos ms: click
void SampleInstrument::PreFade(int channel,int delay,int len) {
	if (channel<0 || channel>=SONG_CHANNEL_COUNT) return ;
	renderParams *rp=renderParams_+channel ;
	if (!rp->audioClip_ || rp->finished_ || rp->declickOutLen_>0) return ;
	if (delay<0) delay=0 ;
	rp->declickDelay_=delay ;
	rp->declickOut_=rp->declickOutLen_=len ;
}

void SampleInstrument::InitAudioClip() {
	if (GetSimpler(SIP_A_INIT)) return ;
	FindVariable(SIP_A_INIT)->SetInt(1) ;
	int vol=volume_->GetInt() ;
	int db=(vol<=0)?-36:int(roundf(20.0f*log10f(vol/128.0f))) ;
	FindVariable(SIP_A_GAIN)->SetInt(db<-36?-36:(db>6?6:db)) ;
	// trk2als: con qué warp entran los loops nuevos (config.xml:
	// <LOOPWARP value="complex"/>; si no, re-pitch)
	const char *lw=Config::GetInstance()->GetValue("LOOPWARP") ;
	if (lw && !strcasecmp(lw,"complex") && WarpStretch::Available()) warp_->SetInt(1) ;
}

void SampleInstrument::startTail(int channel) {
    int t=channel+SONG_CHANNEL_COUNT ;
    renderParams *rp=renderParams_+channel ;
    renderParams *tp=renderParams_+t ;
    stealVoice(t) ;   // si la cola aún sonaba, no se corta en seco
    std::vector<I_SRPUpdater *> own=tp->updaters_ ;   // los de la cola apuntan a la cola
    dropCache(tp) ;
    *tp=*rp ;
    WarpCache::AddRef(tp->warpCache_) ;   // la cola sigue leyendo el mismo pre-render
    tp->updaters_=own ;
    tp->activeUpdaters_.clear() ;   // los comandos de la phrase siguen con la nota nueva
    memcpy(feedback_[t],feedback_[channel],sizeof(feedback_[t])) ;
    copy_filter(channel,t) ;
    if (t<SI_VOICE_COUNT && channel<SI_VOICE_COUNT) {   // el sinte también: la nota que sonaba sigue con sus osciladores (si no, click)
        PlaitsVoice *pv=synthVoices[t] ;
        synthVoices[t]=synthVoices[channel] ;
        synthVoices[channel]=pv ;
    }
    WarpStretch *ws=stretch_[t] ;   // el time-stretch con su historia se va con la cola
    stretch_[t]=stretch_[channel] ;
    stretch_[channel]=ws ;
    lastWarpT_[t]=lastWarpT_[channel] ;
    warpTarget_[t]=warpTarget_[channel] ;
    voiceClock_[t]=voiceClock_[channel] ;
    if (tp->gate_ && tp->envStage_<3) {   // en trigger, la nota sigue completa
        tp->envReleaseFrom_=tp->envLevel_ ;
        tp->envStage_=3 ;
        tp->envClock_=0 ;
        tp->fenvReleaseFrom_=tp->fenvLevel_ ;
        tp->fenvStage_=3 ;
        tp->fenvClock_=0 ;
    }
}

// trk2als: una voz que empieza a media vuelta del bloque (subdivisiones):
// silencio hasta su muestra y lo demás del bloque ya sonando
bool SampleInstrument::renderVoice(int channel, fixed *buffer, int size, int flags) {
    bool r = renderVoiceDelayed(channel, buffer, size, flags);
    unsigned long now = System::GetInstance()->GetClock();
    if (stolenLeft_[channel] > 0) {   // lo que quedó de la voz pisada, apagándose
        if (!r) memset(buffer, 0, size * 2 * sizeof(fixed));
        int left = stolenLeft_[channel];
        for (int i = 0; i < size && left > 0; i++, left--) {
            float g = 0.5f * (1.0f - cosf(3.14159265f * float(left) / STEAL_LEN));
            buffer[2 * i] += fl2fp(stolen_[channel][0] * g);
            buffer[2 * i + 1] += fl2fp(stolen_[channel][1] * g);
        }
        stolenLeft_[channel] = left;
        r = true;
    }
    if (r) {
        lastOut_[channel][0] = fp2fl(buffer[2 * size - 2]);
        lastOut_[channel][1] = fp2fl(buffer[2 * size - 1]);
        lastOutClock_[channel] = now;
    }
    return r;
}

// la voz (o cola) que está por pisarse: si sonó en el último bloque, su
// último valor queda como residuo que se apaga en STEAL_LEN muestras
void SampleInstrument::stealVoice(int idx) {
    if (idx < 0 || idx >= SI_VOICE_COUNT) return;
    unsigned long now = System::GetInstance()->GetClock();
    if (lastOutClock_[idx] == 0 || now - lastOutClock_[idx] > 80) { stolenLeft_[idx] = 0; return; }
    stolen_[idx][0] = lastOut_[idx][0];   // (incluye lo que quedara de un residuo anterior)
    stolen_[idx][1] = lastOut_[idx][1];
    stolenLeft_[idx] = STEAL_LEN;
    lastOutClock_[idx] = 0;
}

// trk2als: una voz que empieza a media vuelta del bloque (subdivisiones):
// silencio hasta su muestra y lo demás del bloque ya sonando
bool SampleInstrument::renderVoiceDelayed(int channel, fixed *buffer, int size, int flags) {
    renderParams *rp = renderParams_ + channel;
    int d = rp->startDelay_;
    if (d <= 0) return renderVoiceNow(channel, buffer, size, flags);
    if (d >= size) { rp->startDelay_ -= size; return false; }
    rp->startDelay_ = 0;
    bool r = renderVoiceNow(channel, buffer + 2 * d, size - d, flags);
    if (r) memset(buffer, 0, d * 2 * sizeof(fixed));
    return r;
}

void SampleInstrument::SetStartDelay(int channel, int voice, int samples) {
    if (channel < 0 || channel >= SONG_CHANNEL_COUNT || samples <= 0) return;
    int idx;
    if (IsKit()) idx = lastKitSlot_[channel];
    else if (voice <= 0) idx = channel;
    else if (voice <= SI_CHORD_VOICES) idx = ChordIdx(channel, voice);
    else return;
    if (idx < 0 || idx >= SI_VOICE_COUNT) return;
    renderParams_[idx].startDelay_ = samples;
}

bool SampleInstrument::renderVoiceNow(int channel, fixed *buffer, int size, int flags) {

    voiceClock_[channel] = System::GetInstance()->GetClock(); // trk2als: cursor de reproducción
    bool somethingToMix = false;
    bool updateTick = flags & 1;

    // Get Current render parameters

    renderParams *rp = renderParams_ + channel;
    lastMidiNote_[channel] = rp->midiNote_;
    bool *rpFinished = &(rp->finished_);

    if (source_) {

        if (*rpFinished)
            return false;

        // clear the fixed point buffer

        SYS_MEMSET(buffer, 0, size * 2 * sizeof(fixed));

        applyLive(rp); // trk2als: cambios del instrumento en vivo
        if (rp->simpler_) simplerBlock(rp, size); // envolvente -> filtro, glide

        // trk2als: looper sync sigue al tempo en tiempo real. Si cambió la
        // duración del tick, se reescala la velocidad (con su transposición)
        if (rp->syncTickSamples_>0) {
            float now=SyncMaster::GetInstance()->GetTickSampleCount() ;
            if (now>0 && now!=rp->syncTickSamples_) {
                fixed scale=fl2fp(rp->syncTickSamples_/now) ;
                rp->baseSpeed_=fp_mul(rp->baseSpeed_,scale) ;
                rp->speed_=fp_mul(rp->speed_,scale) ;
                rp->syncTickSamples_=now ;
            }
        }

        bool hasUpdaters = !(rp->activeUpdaters_.empty());

        int filterMix = rp->simpler_ ? rp->filterMix_ : filterMix_->GetInt();
        FilterMode filterMode =
            rp->simpler_ ? FM_ORIGINAL : (FilterMode)filterMode_->GetInt();
        bool filterBoost = (filterMode == FM_SCREAM);
        bool bassyFilter = (filterMode == FM_BASSY);

        // Be sure filters are properly initialized

        set_filter(channel, FLT_LOWPASS, rp->cutoff_, rp->reso_, filterMix,
                   bassyFilter);

        filter_t *flt = get_filter(channel);
        bool filtering = (rp->cutoff_ < i2fp(1)) || (rp->reso_ > i2fp(0));

        // Process tick-level updates

        if (updateTick) {

            if (hasUpdaters) {

                doTickUpdate(channel);

				struct RUParams rup ;
				rup.cutOffset_=rup.resOffset_=rup.volumeOffset_=rup.panOffset_=0 ;
				rup.speedOffset_=FP_ONE ;

				std::vector<I_SRPUpdater *>::iterator it ;

				for (it=rp->activeUpdaters_.begin();it!=rp->activeUpdaters_.end();it++) {
					I_SRPUpdater *current=*it ;
					current->UpdateSRP(rup) ;
				}

				rp->volume_=rp->baseVolume_+rup.volumeOffset_ ;
				rp->speed_=fp_mul(rp->baseSpeed_,rup.speedOffset_) ;
				rp->pan_=rp->basePan_+rup.panOffset_ ;
            }

            // Process retrig

            if (rp->retrig_) {
                if (rp->retrigCount_ == 0) {
                    int ticks = rp->retrigOffset_ - rp->retrigLoop_;
                    long offset =
                        long(ticks *
                             SyncMaster::GetInstance()->GetTickSampleCount());
                    rp->position_ += offset * fp2fl(rp->speed_);
                    if (rp->position_ < 0) {
                        rp->position_ = 0;
                    };
                    rp->retrigCount_ = rp->retrigLoop_;
                }
            rp->retrigCount_-- ;
		} ;
        }

        // Get additional parameters from variables

        // Crush

        int shift = 16 - rp->crush_;
        fixed mask = 0xFFFFFFFF;
        if (shift != 0) {
            mask <<= FIXED_SHIFT + shift;
        }

		 // Crush vol

		 int crushvol=rp->drive_ ;
		 fixed fpcrushvol=fl2fp(crushvol/255.0F) ;

		 // downsample

		 int downsmpl=rp->downsample_ ;
		 unsigned int dsMask=0xFFFFFFFF<<downsmpl ;

		 // Loop mode

		 SampleInstrumentLoopMode loopMode=effectiveLoopMode(rp) ;

		 // Interpolation

		 int interpol=interpolation_->GetInt() ;

		 // Get sound characteristics
 

		char *wavbuf=(char *)rp->sampleBuffer_ ;

		int channelCount=rp->channelCount_ ;

		int count=size ; // number of samples to treat

		fixed *result=buffer ;

		// Get volume factor and pan

		fixed volscale=fl2fp(0.003921568627450980392156862745098f) ;
		fixed volfactor=fp_mul(rp->volume_,volscale) ;

		// Filter attenuate
		fixed fpattenuate=fp_mul(rp->attenuate_,volscale) ;

	  int pan=fp2i(rp->pan_) ;
		// trk2als: panlaw sube con el índice; pan 00 = izquierda, como en Live
		fixed fixedpanl=panlaw[254-pan] ;
		fixed fixedpanr=panlaw[pan] ;

		// filter constants

		fixed f_k=fl2fp( 1.0F / 3.0F ) ;
		fixed f_s=FP_ONE - f_k ;

		// Get pan multiplicators, and take volume into account

    int n=int(rp->position_) ;
    short *input=(short *)(wavbuf+2*channelCount*n) ; // input is the current
                                                     // sample to the left of position

		fixed fpPos=fl2fp(rp->position_-n) ;  // fpPos is current pos from input
		fixed fpSpeed=rp->speed_ ;     // speed in fixed
		if (rp->reverse_)
    {
			fpSpeed=-rp->speed_ ; 
		} 

    fixed s1,s2,t2,eta,inveta;
		s2=0 ; t2=0 ;

    short* loopPosition=(short *)(wavbuf+rp->rendLoopStart_*2*channelCount) ;
    short* lastSample=(short *)(wavbuf+(rp->rendLoopEnd_-1)*2*channelCount) ;

    if (/*(loopMode==SILM_OSCFINE)||*/(rp->reverse_))
    {
			lastSample=(short *)(wavbuf+rp->rendLoopEnd_*2*channelCount) ;
		}
        
    fixed zerofive=fl2fp(0.5f) ;

		// Get feedback pointer position & boundary

		fixed *feedbackIn=feedback_[channel]+rp->feedbackIn_*2 ;
		fixed *feedbackStart=feedback_[channel] ;
		fixed *feedbackEnd=feedback_[channel]+FB_BUFFER_LENGTH*2 ;

		// Update feedback data if necessary

		updateFeedback(rp) ;

		fixed *feedbackPick=feedback_[channel]+rp->feedbackOut_*2 ;
		fixed feedbackEta=fp_mul(rp->fbMix_,fl2fp(4.0f));

		fixed f_32767=i2fp(32767/4) ;
		fixed f_m32768=i2fp(-32768/4) ;

		// try to speed up access using pointers rather than structure access

		bool rpReverse=rp->reverse_ ;
		int rpKrateCount=rp->krateCount_ ;
		FeedbackMode rpFeedbackMode=rp->feedbackMode_ ;

		fixed *fltSpeed=flt->speed ;
		fixed *fltHeight=flt->height ;
		fixed fltMix=flt->mix ;
		fixed fltMixInv=FP_ONE-fltMix ;
		fixed *fltDelay=flt->hipdelay ;
		fixed fltParm1=flt->freq ;
		fixed fltParm2=flt->reso ;
		fixed fltDirt=flt->dirt ;

		fixed *fltSpeedPtr=0 ;
		fixed *fltDelayPtr=0 ;
		fixed *fltHeightPtr=0 ;

    short *dsBasePtr = ((short *)wavbuf) + rp->rendFirst_* channelCount;

    // trk2als: warp complex. El time-stretch deja 'size' cuadros ya estirados
    // (y transpuestos) en warpOut_; el resto del camino (feedback, crush,
    // filtro, volumen) los lee como si fueran el sample a velocidad 1.
    // Cambiar el warp mientras suena brincaría: el cambio espera a que el
    // loop vuelva a empezar
    float posBefore=rp->position_ ;
    if (rp->warpMode_!=warp_->GetInt() && rp->justWrapped_) {
        rp->warpMode_=warp_->GetInt() ;
        rp->complexPrimed_=false ;   // el time-stretch arranca desde aquí
    }
    bool complex=(loopMode==SILM_LOOPSYNC && rp->warpMode_==1 &&
                  WarpStretch::Available() && !rpReverse) ;
    if (rp->simpler_) complex=(rp->sWarp_==SW_COMPLEX && !rpReverse) ;
    // trk2als: un loop que ya está al tempo del proyecto (y sin transponer) no
    // tiene nada que estirar: suena tal cual, sin caché ni procesador (si
    // luego cambia el tempo, el warp entra desde donde va)
    if (complex && !rp->simpler_) {
        float rate=source_?float(source_->GetSampleRate(rp->midiNote_)):44100.0f ;
        double tf=rate/44100.0*pow(2.0,(transpose_->GetInt()+GetSimpler(SIP_A_DET)/100.0)/12.0) ;
        double sp=fp2fl(rp->speed_) ;
        if (fabs(sp-1.0)<0.0006 && fabs(tf-1.0)<0.0006) {
            complex=false ;
            if (rp->warpCache_) dropCache(rp) ;
            rp->complexPrimed_=false ;
            rp->warpFresh_=false ;
        }
    }
    // trk2als: el sinte: igual que el warp, deja 'size' cuadros en warpOut_ y el
    // resto del camino (filtro, envolvente, volumen) los lee a velocidad 1
    bool synth=(rp->simpler_ && !zoned_ && GetSimpler(SIP_S_MODE)==SM_SYNTH) ;
    if (synth) {
        complex=false ;
        rpReverse=false ;
        renderSynth(channel,rp,size,channelCount) ;
        wavbuf=(char *)&warpOut_[0] ;
        input=(short *)wavbuf ;
        fpPos=0 ;
        fpSpeed=FP_ONE ;
        loopPosition=input ;
        lastSample=input+(size+1)*channelCount ;
        dsBasePtr=input ;
    }
    if (complex) {
        renderComplex(channel,rp,size) ;
        wavbuf=(char *)&warpOut_[0] ;
        input=(short *)wavbuf ;
        fpPos=0 ;
        fpSpeed=FP_ONE ;
        loopPosition=input ;
        lastSample=input+(size+1)*channelCount ;
        dsBasePtr=input ;
    }
       
		while (count>0) {

			// look where we are, if we need to

			if (!rpReverse) { //Looping forward 
				if (input>=lastSample/*-((loopMode==SILM_OSCFINE)?1:0)*/) {
					switch(loopMode) {
                    case SILM_ONESHOT:
                        *rpFinished = true;
                        break;
                    case SILM_LOOP:
                    case SILM_OSC:
                    case SILM_LOOPSYNC:
                        input = loopPosition;
                        rpReverse = (loopPosition > lastSample);
                        if (rpReverse) {
                            fpSpeed = -rp->speed_;
                        } else {
                            fpSpeed = rp->speed_;
                        }
                        break;
                    case SILM_LOOP_PINGPONG:
                        if ((loopPosition > lastSample)) {
                            if (input <= lastSample || input >= loopPosition) {
                                rpReverse = !rpReverse;
                                fpSpeed = -fpSpeed;
                            }
                        } else {
                            if (input >= lastSample || input <= loopPosition) {
                                rpReverse = !rpReverse;
                                fpSpeed = -fpSpeed;
                            }
                        }
                        break;
                        /*						case SILM_OSCFINE:
                                                {
                                                    int
                           offset=(input-lastSample)/channelCount ;
                                                    rpReverse=(loopPosition>lastSample)
                           ; if (rpReverse) { fpSpeed=-rp->speed_ ;
                                                        input=loopPosition-offset
                           ; } else { fpSpeed=rp->speed_ ;
                                                        input=loopPosition+offset
                           ;
                                                    }
                                                    break ;
                                                }*/
                    case SILM_LAST:
                        NAssert(0);
                        break;
					} ;
				}
			} else { // Looping backward
				if (input<lastSample) {
					switch(loopMode) {
                    case SILM_ONESHOT:
                        *rpFinished = true;
                        break;
                    case SILM_LOOP:
                    case SILM_OSC:
                    case SILM_LOOPSYNC:
                        input = loopPosition;
                        rpReverse = (loopPosition > lastSample);
                        if (rpReverse) {
                            fpSpeed = -rp->speed_;
                        } else {
                            fpSpeed = rp->speed_;
                        }
                        break;
                    case SILM_LOOP_PINGPONG:
                        if ((loopPosition > lastSample)) {
                            if (input <= lastSample || input >= loopPosition) {
                                rpReverse = !rpReverse;
                                fpSpeed = -fpSpeed;
                            }
                        } else {
                            if (input >= lastSample || input <= loopPosition) {
                                rpReverse = !rpReverse;
                                fpSpeed = -fpSpeed;
                            }
                        }
                        break;
                        /*						case SILM_OSCFINE:
                                                {
                                                    int
                           offset=(lastSample-input)/channelCount ;
                                                    rpReverse=(loopPosition>lastSample)
                           ; if (rpReverse) { fpSpeed=-rp->speed_ ;
                                                        input=loopPosition-offset
                           ; } else { fpSpeed=rp->speed_ ;
                                                        input=loopPosition+offset
                           ;
                                                    }
                                                    break ;
                                                }*/
                    case SILM_LAST:
                        NAssert(0);
                        break;
					} ;
				}
			};

			
      if (*rpFinished) {
				count=-1 ;
			} else {

	  	    // See if time to process k-rate change

			  if (rpKrateCount--==0) {
					rpKrateCount=KRATE_SAMPLE_COUNT ;

					if (hasUpdaters) {
						doKRateUpdate(channel) ;
						struct RUParams rup ;
						rup.cutOffset_=rup.resOffset_=rup.volumeOffset_=rup.panOffset_=rup.fbMixOffset_=rup.fbTunOffset_=0 ;
						rup.speedOffset_=FP_ONE;
				
						std::vector<I_SRPUpdater *>::iterator it ;

						for (it=rp->activeUpdaters_.begin();it!=rp->activeUpdaters_.end();it++) {
							I_SRPUpdater *current=*it ;
							current->UpdateSRP(rup) ;
						}
						
						rp->volume_=rp->baseVolume_+rup.volumeOffset_ ;
						rp->pan_=rp->basePan_+rup.panOffset_ ;
						rp->speed_=fp_mul(rp->baseSpeed_,rup.speedOffset_) ;
						rp->cutoff_=rp->baseFCut_+rup.cutOffset_ ;
						rp->reso_=rp->baseFRes_+rup.resOffset_ ;
						rp->fbMix_=rp->baseFbMix_+rup.fbMixOffset_ ;
						rp->fbTun_=rp->baseFbTun_+rup.fbTunOffset_ ;

						set_filter(channel,FLT_LOWPASS,rp->cutoff_,rp->reso_,filterMix,bassyFilter);
						filtering=(rp->cutoff_<i2fp(1))||(rp->reso_>i2fp(0)) ;

						rp->feedbackIn_ = (feedbackIn-feedback_[channel])/2 ;

						updateFeedback(rp) ;
						feedbackPick=feedback_[channel]+rp->feedbackOut_*2 ;
						feedbackEta=fp_mul(rp->fbMix_,fl2fp(4.0f));

						volfactor=fp_mul(rp->volume_,volscale) ;
						fpattenuate=fp_mul(rp->attenuate_,volscale) ;
						pan=fp2i(rp->pan_) ;
						// trk2als: panlaw sube con el índice; pan 00 = izquierda, como en Live
						// (sin 'fixed': antes declaraba variables nuevas y el
						// paneo no cambiaba a media nota)
						fixedpanl=panlaw[254-pan] ;
						fixedpanr=panlaw[pan] ;

						if (complex) {
							fpSpeed=FP_ONE ;   // trk2als: la velocidad la pone el time-stretch
						} else if (rpReverse) {
							fpSpeed=-rp->speed_ ;
						} else {
							fpSpeed=rp->speed_ ;
						}
					}
			  }

		    // get input sample to interpolate from
			  // s= left channel
		    // t= right channel

			  short *i1=input;
        if (dsMask!=0xFFFFFFFF)
        {
	        if (useDirtyDownsampling_)
	        {
#ifdef _64BIT
	          i1 =(short *)(((long)input)&dsMask);
#else
	          i1 =(short *)(((unsigned int)input)&dsMask);
#endif
	        }
          else
	        {
            unsigned int distance = (unsigned int)(input - dsBasePtr) /channelCount;
            i1 = dsBasePtr+(distance&dsMask)*channelCount ;
          }
        }

        short *i2=i1+channelCount ;

				if (filtering) 
        {
					fltSpeedPtr=fltSpeed ;
					fltHeightPtr=fltHeight ;
					fltDelayPtr=fltDelay ;
				}

				for (int i=0;i<channelCount;i++) 
        {
					t2=s2 ; // move L to R if necessary
					s1=i2fp(*i1++) ;
          s2=i2fp(*i2++) ;

					switch(interpol) {

						case 0: // Linear interpolation
						
							eta=fpPos ;
							inveta=fp_sub(FP_ONE,eta) ;
		           
							// interpolate

			  				s1=fp_mul(s1,inveta) ;
							s2=fp_mul(s2,eta) ;

							// Compute interpolated sample

    					s1+=s2 ;
							break ;
							
						case 1: // Nearest neighbor
						
							if (fpPos>zerofive) {
								s1=s2 ;
							} ;
							break ;
					}

				// apply feedback mix

					switch(rpFeedbackMode) {
						case FB_NONE:

							feedbackPick++ ;
							break ;

						case FB_ADD:

							s2=*feedbackPick++ ;
							if (s2>f_32767) s2=f_32767 ;
							if (s2<f_m32768) s2=f_m32768 ;
							s2=fp_mul(s2,feedbackEta) ;
							s1=fp_add(s1,s2) ;
							break ;

						case FB_SUB:

							s2=*feedbackPick++ ;
							if (s2>f_32767) s2=f_32767 ;
							if (s2<f_m32768) s2=f_m32768 ;
							s2=fp_mul(s2,feedbackEta) ;
							s1=fp_sub(s1,s2) ;
							break ;
					}

				// crush predrive

					s2=fp_mul(s1,fpcrushvol) ;

				// store result, applying crush

					s2=(s2&mask);

				// apply volume

          s2=fp_mul(s2,volfactor) ;

				// apply filtering if needed

					if (filtering) {

						fixed lpin =fp_mul(s2,fltMixInv) ;
      			fixed hpin = -fp_mul(s2,fltMix) ;
							
    				fixed difr = fp_sub(lpin,*fltHeightPtr);

						// Introduce non-linearity if screamin'

						if (filterBoost) {
							if (*fltSpeedPtr<-FP_ONE) {
								*fltSpeedPtr=-f_s ;
							} else if (fltSpeed[i]>FP_ONE) {
								*fltSpeedPtr=f_s ;
							};
							*fltSpeedPtr=fp_mul(*fltSpeedPtr,fltDirt) ;
						}

						*fltSpeedPtr = fp_mul(*fltSpeedPtr,fltParm2);		//mul by res, it's some kind of inertia. caution to feedback
/*HOG:5*/				*fltSpeedPtr = fp_add(*fltSpeedPtr,fp_mul(difr,fltParm1)); //mul by cutoff, less cutoff = no sound, so it's better not be 0.

						*fltHeightPtr += *fltSpeedPtr ;
						*fltHeightPtr += *fltDelayPtr-hpin ;
						s2=*fltHeightPtr ;

						*fltDelayPtr=hpin ;
						fltDelayPtr++ ;
						fltHeightPtr++ ;
						fltSpeedPtr++ ;
					}
					// apply attenuation
					s2=fp_mul(s2,fpattenuate) ;
				}

				if (channelCount==1) {
					t2=s2 ;
					feedbackPick++ ;
				} else {
					// trk2als: el bucle deja el canal izquierdo en t2 y el
					// derecho en s2; sin esto el estéreo salía invertido
					fixed tmp=s2 ;
					s2=t2 ;
					t2=tmp ;
				}

				if (feedbackPick>=feedbackEnd) {
					feedbackPick=feedbackStart ;
				} ;

				// introduce panning & vol - store result

				s2=fp_mul(s2,fixedpanl) ;
				t2=fp_mul(t2,fixedpanr) ;

				*result++=s2 ;
				*result++=t2 ;

				*feedbackIn++=s2 ;
				*feedbackIn++=t2 ;

				if (feedbackIn>=feedbackEnd) {
					feedbackIn=feedbackStart ;
				} ;

			// Computes new pos for next input sample
			// fpPos is always relative to 'input' pointer
		
			   fpPos=fp_add(fpPos,fpSpeed) ;
         int delta=fp2i(fpPos) ;
         input+=channelCount*delta;
         fpPos = fp_sub(fpPos,i2fp(delta)) ;
			   count-- ;
			}
    }
    // Update 'reverse' mode if changed

    rp->reverse_ = rpReverse;

		// Update final sample position
    rp->position_=(((char *)input)-wavbuf)/(2*channelCount)+fp2fl(fpPos) ;
    if (complex) {   // trk2als: lo que suena va 'complexLatency_' cuadros atrás
        int p=rp->complexPos_-rp->complexLatency_ ;
        int len=rp->rendLoopEnd_-rp->rendLoopStart_ ;
        if (rp->simpler_) {
            // Sampler: la región puede empezar antes del loop; sin loop, al
            // llegar al final la voz termina
            if (rp->simplerLoop_!=SILM_LOOP && p>=rp->rendLoopEnd_) {
                *rpFinished=true ;
            } else if (p<rp->complexFirst_) {
                p+=len ;
            }
        } else {
            while (len>0 && p<rp->rendLoopStart_) p+=len ;
        }
        rp->position_=float(p) ;
    }
    if (synth) rp->position_=float(rp->rendLoopStart_) ;   // (no avanza: no hay sample que recorrer)
    rp->justWrapped_=(rp->position_<posBefore) ;

		// Update feedback position

		rp->feedbackIn_=(feedbackIn-feedbackStart)/2 ;
		rp->feedbackOut_=(feedbackPick-feedbackStart)/2 ;
		somethingToMix=true ;
		if (rp->simpler_) {   // trk2als
			simplerFilter(rp,buffer,size) ;
			simplerEnvelope(rp,buffer,size) ;
		} else if (rp->audioClip_) {
			declickVoice(rp,buffer,size) ;
		}
    }

    return somethingToMix;
};

/************************************************************
 trk2als: el sinte (Plaits, de Mutable Instruments)
 ************************************************************/

void SampleInstrument::renderSynth(int channel,renderParams *rp,int size,int ch) {
	PlaitsVoice **voices=synthVoices ;   // una por voz, al primer uso
	if (channel<0 || channel>=SI_VOICE_COUNT) return ;
	if (!voices[channel]) voices[channel]=new PlaitsVoice() ;
	PlaitsVoice *v=voices[channel] ;
	bool fresh=rp->synthTrig_ ;
	if (rp->synthTrig_) { v->Trigger() ; rp->synthTrig_=false ; }
	if (ch<1) ch=1 ;
	if ((int)warpOut_.size()<(size+2)*ch) warpOut_.resize((size+2)*ch) ;
	v->Render(rp->synthNote_,GetSimpler(SIP_S_SENG),GetSimpler(SIP_S_SHARM)/100.0f,
	          GetSimpler(SIP_S_STIMB)/100.0f,GetSimpler(SIP_S_SMORPH)/100.0f,&warpOut_[0],size,ch) ;
	// la nota nueva entra en 2 ms: los osciladores corren libres y con el
	// ataque en 0 arrancaban a media onda (click)
	if (fresh) {
		int n=size<88?size:88 ;
		for (int i=0;i<n;i++) warpOut_[i*ch]=short(int(warpOut_[i*ch])*i/n) ;
	}
	for (int i=0;i<size;i++) for (int c=1;c<ch;c++) warpOut_[i*ch+c]=warpOut_[i*ch] ;
	for (int c=0;c<ch;c++) {
		warpOut_[size*ch+c]=warpOut_[(size-1)*ch+c] ;
		warpOut_[(size+1)*ch+c]=warpOut_[(size-1)*ch+c] ;
	}
}

/************************************************************
 trk2als: warp complex (time-stretch de Signalsmith)
 ************************************************************/

// Copia n cuadros del loop a warpIn_ (como float), dando la vuelta en el final
void SampleInstrument::readLoop(renderParams *rp,int n) {
	int ch=rp->channelCount_ ;
	if ((int)warpIn_[0].size()<n) {
		warpIn_[0].resize(n) ;
		warpIn_[1].resize(n) ;
	}
	short *wav=(short *)rp->sampleBuffer_ ;
	int pos=rp->complexPos_ ;
	bool simplerOnce=rp->simpler_ && rp->simplerLoop_!=SILM_LOOP ;
	for (int i=0;i<n;i++) {
		if (rp->simpler_) {
			// Sampler: arranca en 'start' (antes del loop) y, sin loop, se
			// queda en silencio al pasar el final
			if (pos>=rp->rendLoopEnd_) {
				if (simplerOnce) {
					warpIn_[0][i]=warpIn_[1][i]=0 ;
					pos++ ;
					continue ;
				}
				pos=rp->rendLoopStart_ ;
			}
		} else if (pos>=rp->rendLoopEnd_ || pos<rp->rendLoopStart_) pos=rp->rendLoopStart_ ;
		short *frame=wav+pos*ch ;
		warpIn_[0][i]=frame[0]/32768.0f ;
		if (ch>1) warpIn_[1][i]=frame[1]/32768.0f ;
		pos++ ;
	}
	rp->complexPos_=pos ;
}

int g_warpRealtimeBlocks=0, g_warpCacheBlocks=0 ;

void SampleInstrument::renderComplex(int channel,renderParams *rp,int size) {
	int ch=rp->channelCount_>1?2:1 ;
	WarpStretch *ws=stretch_[channel] ;
	if (!ws) {
		ws=stretch_[channel]=new WarpStretch() ;
	}
	const int mixRate=44100 ;
	if (ws->Channels()!=ch) {
		ws->Configure(ch,mixRate) ;
		rp->complexPrimed_=false ;
	}
	// el time-stretch trabaja en cuadros; para que suene en su tono original
	// hay que compensar la frecuencia del archivo contra la de la mezcla
	float rate=source_?float(source_->GetSampleRate(rp->midiNote_)):mixRate ;
	float semisFactor=rp->simpler_?rp->warpTranspose_:
	    float(pow(2.0,(transpose_->GetInt()+GetSimpler(SIP_A_DET)/100.0)/12.0)) ;
	ws->SetTranspose(rate/mixRate*semisFactor) ;
	double speed=fp2fl(rp->speed_) ;     // cuadros del sample por cuadro de salida
	if (speed<0.01) speed=0.01 ;

	if ((int)warpOut2_[0].size()<size) {
		warpOut2_[0].resize(size) ;
		warpOut2_[1].resize(size) ;
	}
	float *ins[2]={0,0}, *outs[2]={&warpOut2_[0][0],&warpOut2_[1][0]} ;

	// trk2als: pre-render. Si la voz ya lee uno y nada cambió, solo copiar
	float transpose=rate/mixRate*semisFactor ;
	if (rp->warpCache_ && rp->complexPrimed_) {
		if (WarpCache::Matches(rp->warpCache_,speed,transpose)) {
			g_warpCacheBlocks++ ;
			readCache(rp,size,speed) ;
			return ;
		}
		// trk2als: un clip de audio nunca estira en tiempo real (con varias
		// pistas no alcanza). Cambió el tempo: mientras se estira la versión
		// nueva, la que ya tiene suena a la nueva velocidad (como re-pitch, a
		// tiempo); cuando está, se pasa a ella en el mismo punto del sample
		if (!rp->simpler_) {
			WarpCacheKey nk ;
			void *old=rp->warpCache_ ;
			if (cacheKey(rp,WarpCache::Start(old),speed,transpose,nk)) {
				// moviendo el tempo de 1 en 1: se pide cuando lleva 350 ms quieto
				unsigned long nowc=System::GetInstance()->GetClock() ;
				double dv=speed-rp->warpReqSpeed_ ;
				if (dv<0) dv=-dv ;
				if (dv>speed*0.0015) { rp->warpReqSpeed_=speed ; rp->warpReqAt_=nowc ; }
				void *h2=(nowc-rp->warpReqAt_>=350)?WarpCache::Acquire(nk):0 ;
				if (h2) {
					double p=rp->complexPos_ ;
					// lo que sonaría con la de antes en este bloque, para el
					// fundido cruzado (sin escalón: era un click)
					readCacheVar(rp,size,speed/WarpCache::Speed(old),transpose/WarpCache::Transpose(old)) ;
					std::vector<short> prev(warpOut_.begin(),warpOut_.begin()+(size+2)*ch) ;
					int ls2=WarpCache::LoopStart(h2), st2=WarpCache::Start(h2) ;
					double idx2=(ls2>=0 && p>=rp->rendLoopStart_)?ls2+(p-rp->rendLoopStart_)/speed:(p-st2)/speed ;
					if (idx2<0) idx2=0 ;
					dropCache(rp) ;
					rp->warpCache_=h2 ;
					rp->cacheIdx_=int(idx2) ;
					rp->cacheFrac_=0 ;
					readCache(rp,size,speed) ;
					int xf=size<256?size:256 ;   // ~6 ms (o lo que dure el bloque)
					for (int i=0;i<size+2;i++) {
						float g=i>=xf?1.0f:float(i)/xf ;
						for (int c=0;c<ch;c++)
							warpOut_[i*ch+c]=short(prev[i*ch+c]*(1.0f-g)+warpOut_[i*ch+c]*g) ;
					}
					if (getenv("TRK_LOADLOG")) fprintf(stderr,"[WARP] %lu canal %d: pasa al tempo nuevo\n",System::GetInstance()->GetClock(),channel) ;
					return ;
				}
				readCacheVar(rp,size,speed/WarpCache::Speed(old),transpose/WarpCache::Transpose(old)) ;
				return ;
			}
		}
		// cambió el tempo o la nota (glide, comandos): seguir en tiempo real
		// desde donde va
		dropCache(rp) ;
		rp->complexPrimed_=false ;
	}
	WarpCacheKey key ;
	if (!rp->complexPrimed_) {
		if (cacheKey(rp,int(rp->position_),speed,transpose,key)) {
			// trk2als: clip de audio que arranca: si ya hay una versión de otro
			// tempo, a sonar con ella ya (a la nueva velocidad); la nueva se
			// pide en el camino de "cambió el tempo"
			if (!rp->simpler_ && rp->warpFresh_ && rp->warpWaited_==0) {
				void *hn=WarpCache::AcquireAny(key) ;
				if (hn && !WarpCache::Matches(hn,speed,transpose)) {
					rp->warpCache_=hn ;
					rp->cacheIdx_=0 ;
					rp->cacheFrac_=0 ;
					rp->complexPrimed_=true ;
					rp->warpFresh_=false ;
					readCacheVar(rp,size,speed/WarpCache::Speed(hn),transpose/WarpCache::Transpose(hn)) ;
					return ;
				}
				if (hn) WarpCache::Release(hn) ;
			}
			void *h=WarpCache::Acquire(key) ;
			bool waitBar=false ;
			// un clip de audio que ya esperó: entra solo en un 1 (en fase con
			// donde habría ido)
			// trk2als: si tardó poquito (salió de la tarjeta), entra ya, en fase
			// (con 4 ms de fundido); perder un compás entero se nota más
			if (h && rp->warpWaited_>11025) {
				long bar=long(96.0f*SyncMaster::GetInstance()->GetTickSampleCount()+0.5f) ;
				if (bar<1) bar=1 ;
				long next=((rp->warpWaited_+bar-1)/bar)*bar ;
				if (next-rp->warpWaited_>=size) {   // listo: falta llegar al 1
					WarpCache::Release(h) ;
					h=0 ;
					waitBar=true ;
				}
			}
			if (h) {
				rp->warpCache_=h ;
				rp->cacheIdx_=rp->warpWaited_ ;
				rp->cacheFrac_=0 ;
				rp->complexPrimed_=true ;
				if (rp->warpWaited_>0) rp->warpFadeIn_=176 ;   // 4 ms, sin click
				if (rp->warpWaited_>0 && getenv("TRK_LOADLOG")) {
					long bar=long(96.0f*SyncMaster::GetInstance()->GetTickSampleCount()+0.5f) ;
					fprintf(stderr,"[WARP] %lu esperó %d cuadros (%.2f compases): entra ya estirado\n",System::GetInstance()->GetClock(),rp->warpWaited_,bar>0?double(rp->warpWaited_)/bar:0.0) ;
				}
				rp->warpFresh_=false ;
				readCache(rp,size,speed) ;
				if (rp->warpFadeIn_>0) {
					for (int i=0;i<size && rp->warpFadeIn_>0;i++,rp->warpFadeIn_--) {
						float g=1.0f-float(rp->warpFadeIn_)/176.0f ;
						for (int c=0;c<ch;c++) warpOut_[i*ch+c]=short(warpOut_[i*ch+c]*g) ;
					}
				}
				return ;
			}
			// trk2als: un clip de audio que recién arranca y su pre-render va
			// en camino: en silencio, sin estirar en tiempo real (el arranque
			// del time-stretch es un pico que con latencia baja hace saltar el
			// audio). Las notas del Sampler no esperan: tienen que sonar ya
			unsigned long now=System::GetInstance()->GetClock() ;
			// trk2als: el mismo loop ya está estirado a otro tempo (se acaba de
			// mover): suena ya con ese, a la nueva velocidad, y se pasa al
			// nuevo cuando esté (ver arriba). Así no se calla al cambiar el tempo
			if (!rp->simpler_ && rp->warpFresh_ && !waitBar && WarpCache::Building(key)) {
				void *hn=WarpCache::AcquireAny(key) ;
				if (hn) {
					double ratio=speed/WarpCache::Speed(hn) ;
					rp->warpCache_=hn ;
					double at=rp->warpWaited_*ratio ;
					rp->cacheIdx_=int(at) ;
					rp->cacheFrac_=at-int(at) ;
					rp->complexPrimed_=true ;
					rp->warpFresh_=false ;
					readCacheVar(rp,size,ratio,transpose/WarpCache::Transpose(hn)) ;
					return ;
				}
			}
			if (waitBar || (!rp->simpler_ && rp->warpFresh_ && WarpCache::Building(key))) {
				if (!rp->warpWaitSince_) rp->warpWaitSince_=now ;
				if (now-rp->warpWaitSince_<15000) {
					if ((int)warpOut_.size()<(size+2)*ch) warpOut_.resize((size+2)*ch) ;
					for (int i=0;i<(size+2)*ch;i++) warpOut_[i]=0 ;
					rp->complexPos_=int(rp->position_) ;
					rp->complexLatency_=0 ;
					rp->complexFirst_=rp->rendLoopStart_ ;
					rp->warpWaited_+=size ;
					return ;
				}
			}
		}
	} else if (rp->justWrapped_ && cacheKey(rp,rp->complexFirst_,speed,transpose,key) && key.loop) {
		// el loop acaba de dar la vuelta en tiempo real: si el pre-render ya
		// está, seguir desde ahí con él
		void *h=WarpCache::Acquire(key) ;
		if (h) {
			int ls=WarpCache::LoopStart(h), period=WarpCache::Length(h)-ls ;
			double into=(rp->position_-key.loopStart)/speed ;
			if (into<0) into=0 ;
			rp->warpCache_=h ;
			rp->cacheIdx_=ls+int(fmod(into,double(period))) ;
			rp->cacheFrac_=0 ;
			readCache(rp,size,speed) ;
			return ;
		}
	}
	g_warpRealtimeBlocks++ ;   // trk2als: para TRK_LOADLOG
	if (!rp->complexPrimed_ && getenv("TRK_LOADLOG")) fprintf(stderr,"[WARP] %lu arranque en tiempo real (canal %d)\n",System::GetInstance()->GetClock(),channel) ;
	rp->warpFresh_=false ;
	if (!rp->complexPrimed_) {
		ws->Reset() ;
		rp->complexPos_=int(rp->position_) ;
		rp->complexFirst_=rp->complexPos_ ;
		rp->complexAcc_=0 ;
		// darle por adelantado lo que necesita para que el tiempo de
		// proceso quede justo donde arranca la voz...
		int pre=ws->InputLatency() ;
		readLoop(rp,pre) ;
		ins[0]=&warpIn_[0][0] ; ins[1]=&warpIn_[1][0] ;
		ws->Seek(ins,pre,speed) ;
		rp->complexLatency_=pre ;
		// ... y tirar la salida de relleno, para que suene a tiempo
		int skip=ws->OutputLatency() ;
		while (skip>0) {
			int n=skip<size?skip:size ;
			rp->complexAcc_+=n*speed ;
			int nin=int(rp->complexAcc_) ;
			rp->complexAcc_-=nin ;
			readLoop(rp,nin) ;
			ins[0]=&warpIn_[0][0] ; ins[1]=&warpIn_[1][0] ;
			ws->Process(ins,nin,outs,n) ;
			skip-=n ;
		}
		rp->complexPrimed_=true ;
	}

	rp->complexAcc_+=size*speed ;
	int nin=int(rp->complexAcc_) ;
	rp->complexAcc_-=nin ;
	readLoop(rp,nin) ;
	ins[0]=&warpIn_[0][0] ; ins[1]=&warpIn_[1][0] ;
	ws->Process(ins,nin,outs,size) ;

	// a 16 bits, intercalado como el sample, con un cuadro extra para la interpolación
	if ((int)warpOut_.size()<(size+2)*ch) warpOut_.resize((size+2)*ch) ;
	for (int i=0;i<size;i++) {
		for (int c=0;c<ch;c++) {
			float v=warpOut2_[c][i]*32768.0f ;
			if (v>32767.0f) v=32767.0f ;
			if (v<-32768.0f) v=-32768.0f ;
			warpOut_[i*ch+c]=short(v) ;
		}
	}
	for (int c=0;c<ch;c++) {
		warpOut_[size*ch+c]=warpOut_[(size-1)*ch+c] ;
		warpOut_[(size+1)*ch+c]=warpOut_[(size-1)*ch+c] ;
	}
}


// trk2als: la llave del pre-render para esta voz, con el mismo inicio y
// vueltas que usa readLoop
bool SampleInstrument::cacheKey(renderParams *rp,int start,double speed,float transpose,WarpCacheKey &k) {
	if (!WarpCache::Enabled() || !rp->sampleBuffer_) return false ;
	k.data=(const short *)rp->sampleBuffer_ ;
	k.channels=rp->channelCount_>1?2:1 ;
	if (rp->channelCount_>2) return false ;
	k.loopStart=rp->rendLoopStart_ ;
	k.loopEnd=rp->rendLoopEnd_ ;
	k.simpler=rp->simpler_ ;
	k.loop=!(rp->simpler_ && rp->simplerLoop_!=SILM_LOOP) ;
	if (!k.simpler && (start<k.loopStart || start>=k.loopEnd)) start=k.loopStart ;
	k.start=start ;
	k.speed=speed ;
	k.transpose=transpose ;
	return k.loopEnd>k.loopStart && start>=0 && start<k.loopEnd ;
}

void SampleInstrument::dropCache(renderParams *rp) {
	if (rp->warpCache_) WarpCache::Release(rp->warpCache_) ;
	rp->warpCache_=0 ;
}

// Copia 'size' cuadros del pre-render a warpOut_ (como renderComplex) y deja
// complexPos_ en el cuadro del sample que está sonando
void SampleInstrument::readCache(renderParams *rp,int size,double speed) {
	void *h=rp->warpCache_ ;
	const short *f=WarpCache::Frames(h) ;
	int len=WarpCache::Length(h), ls=WarpCache::LoopStart(h) ;
	int ch=rp->channelCount_>1?2:1 ;
	if ((int)warpOut_.size()<(size+2)*ch) warpOut_.resize((size+2)*ch) ;
	int idx=rp->cacheIdx_ ;
	for (int i=0;i<size+2;i++) {   // dos cuadros extra para la interpolación
		int j=idx+i ;
		if (j>=len) {
			if (ls<0) {
				for (int c=0;c<ch;c++) warpOut_[i*ch+c]=0 ;
				continue ;
			}
			j=ls+(j-ls)%(len-ls) ;
		}
		for (int c=0;c<ch;c++) warpOut_[i*ch+c]=f[j*ch+c] ;
	}
	idx+=size ;
	if (ls>=0 && idx>=len) idx=ls+(idx-ls)%(len-ls) ;
	rp->cacheIdx_=idx ;
	rp->olaCache_=0 ;   // los granos, si vuelven, empiezan de nuevo
	double p ;
	if (ls>=0 && idx>=ls) p=rp->rendLoopStart_+(idx-ls)*speed ;
	else p=WarpCache::Start(h)+idx*speed ;
	if (ls<0 && idx>=len) p=rp->rendLoopEnd_ ;   // sin loop: se acabó
	rp->complexPos_=int(p) ;
	rp->complexLatency_=0 ;
	// que el ajuste de después (p<complexFirst_ -> +len) no la mueva
	int st=WarpCache::Start(h) ;
	rp->complexFirst_=st<rp->rendLoopStart_?st:rp->rendLoopStart_ ;
}

// trk2als: como readCache, pero avanzando 'ratio' cuadros del pre-render por
// cuadro de salida (interpolado): el pre-render de otro tempo, a tiempo
static inline long wrapCache(long j,int len,int ls) {
	if (j<len) return j ;
	if (ls<0) return -1 ;
	return ls+(j-ls)%(len-ls) ;
}

// trk2als: ¿el sample es de percusión? (por el nombre). Con percusión, el
// pre-render de otro tempo se oye mejor solo más rápido o más lento (un
// poquito más agudo o grave); con lo tonal se conserva el tono.
// LOOPKEEPPITCH en config.xml: auto (así), on (siempre), off (nunca)
static bool drumName(const char *name) {
	static const char *words[]={"drum","perc","beat","break","kick","snare","hat","conga","bongo",
	                            "clap","groove","tom","rim","shaker","cajon","timbal","guiro","bombo",
	                            "tambor","bateria","batería","cymbal","ride","crash",0} ;
	std::string n(name?name:"") ;
	for (size_t i=0;i<n.size();i++) n[i]=char(tolower((unsigned char)n[i])) ;
	for (int i=0;words[i];i++) if (n.find(words[i])!=std::string::npos) return true ;
	return false ;
}

bool SampleInstrument::keepPitch() {
	static int mode=-1 ;   // 0 auto, 1 siempre, 2 nunca
	if (mode<0) {
		const char *v=Config::GetInstance()->GetValue("LOOPKEEPPITCH") ;
		mode=(v && !strcmp(v,"on"))?1:(v && !strcmp(v,"off"))?2:0 ;
	}
	if (mode) return mode==1 ;
	int idx=GetSampleIndex() ;
	if (idx!=tonalIdx_) {   // (solo cuando cambia el sample)
		tonalIdx_=idx ;
		char **names=SamplePool::GetInstance()->GetNameList() ;
		tonal_=idx<0 || !names || !drumName(names[idx]) ;
	}
	return tonal_ ;
}

static const int OLA_G=2048, OLA_H=1024 ;   // grano de ~46 ms, cruzados a la mitad

struct OlaSrc { const short *f ; int len, ls, ch ; } ;

static inline int olaAt(const OlaSrc &s,long j,int c) {
	if (j<0) return 0 ;
	long k=wrapCache(j,s.len,s.ls) ;
	return k<0?0:s.f[k*s.ch+c] ;
}

static inline float olaAtF(const OlaSrc &s,double x,int c) {
	long j=(long)floor(x) ;
	float fr=float(x-j) ;
	int a=olaAt(s,j,c), b=olaAt(s,j+1,c) ;
	return a+(b-a)*fr ;
}

static inline int olaMono(const OlaSrc &s,long j) {
	return s.ch>1?(olaAt(s,j,0)+olaAt(s,j,1))>>1:olaAt(s,j,0) ;
}

// los granos: cada uno lee a velocidad 'pitch' (1 = el tono no cambia; otro
// valor corrige un cambio de transposición); uno nuevo cada OLA_H cuadros,
// desde donde va el tiempo, en el punto (±6 ms) que mejor empata con lo que
// está sonando (WSOLA)
void SampleInstrument::readCacheOla(renderParams *rp,int size,double ratio,double pitch) {
	static float win[OLA_G] ;
	static bool winOk=false ;
	if (!winOk) {
		for (int i=0;i<OLA_G;i++) { float sn=sinf(3.14159265f*i/OLA_G) ; win[i]=sn*sn ; }
		winOk=true ;
	}
	void *h=rp->warpCache_ ;
	OlaSrc src ;
	src.f=WarpCache::Frames(h) ;
	src.len=WarpCache::Length(h) ;
	src.ls=WarpCache::LoopStart(h) ;
	src.ch=rp->channelCount_>1?2:1 ;
	int ch=src.ch ;
	double pos=rp->cacheIdx_+rp->cacheFrac_ ;
	if (rp->olaCache_!=h) {   // empieza: como si un grano ya fuera a la mitad
		rp->olaCache_=h ;
		rp->olaT_=0 ;
		rp->olaNew_=floor(pos) ;
		rp->olaOld_=floor(pos)-OLA_H*pitch ;
	}
	int t=rp->olaT_ ;
	double gN=rp->olaNew_, gO=rp->olaOld_ ;
	for (int i=0;i<size+2;i++) {
		if (i==size) { rp->olaT_=t ; rp->olaNew_=gN ; rp->olaOld_=gO ; }   // (+2: solo para interpolar)
		if (t>=OLA_H) {
			gO=gN ;
			long target=long(pos+i*ratio), nat=long(gO+OLA_H*pitch) ;
			long best=target ;
			long long bestC=0 ;
			bool first=true ;
			for (int d=-256;d<=256;d+=4) {
				long long c=0 ;
				for (int k=0;k<128;k+=2) c+=(long long)olaMono(src,target+d+k)*olaMono(src,nat+k) ;
				if (first || c>bestC) { bestC=c ; best=target+d ; first=false ; }
			}
			gN=double(best) ;
			t=0 ;
		}
		float wn=win[t], wo=win[t+OLA_H] ;
		for (int c=0;c<ch;c++) {
			float v=wn*olaAtF(src,gN+t*pitch,c)+wo*olaAtF(src,gO+(t+OLA_H)*pitch,c) ;
			if (v>32767.0f) v=32767.0f ;
			if (v<-32768.0f) v=-32768.0f ;
			warpOut_[i*ch+c]=short(v) ;
		}
		t++ ;
	}
}

void SampleInstrument::readCacheVar(renderParams *rp,int size,double ratio,double pitch) {
	void *h=rp->warpCache_ ;
	const short *f=WarpCache::Frames(h) ;
	int len=WarpCache::Length(h), ls=WarpCache::LoopStart(h) ;
	int ch=rp->channelCount_>1?2:1 ;
	double cs=WarpCache::Speed(h) ;
	if ((int)warpOut_.size()<(size+2)*ch) warpOut_.resize((size+2)*ch) ;
	double pos=rp->cacheIdx_+rp->cacheFrac_ ;
	// trk2als: lo tonal conserva el tono; y si cambió la transposición, se
	// corrige (también en percusión: si no, el primer tiempo sonaba en el tono de antes)
	if (keepPitch() || fabs(pitch-1.0)>0.0001) readCacheOla(rp,size,ratio,pitch) ;
	else for (int i=0;i<size+2;i++) {
		double q=pos+i*ratio ;
		long j0=long(q) ;
		float fr=float(q-j0) ;
		long a=wrapCache(j0,len,ls), b=wrapCache(j0+1,len,ls) ;
		for (int c=0;c<ch;c++) {
			float va=a<0?0:f[a*ch+c], vb=b<0?0:f[b*ch+c] ;
			warpOut_[i*ch+c]=short(va+(vb-va)*fr) ;
		}
	}
	pos+=size*ratio ;
	long idx=long(pos) ;
	rp->cacheFrac_=pos-idx ;
	if (ls>=0 && idx>=len) idx=ls+(idx-ls)%(len-ls) ;
	rp->cacheIdx_=int(idx) ;
	double p ;
	if (ls>=0 && idx>=ls) p=rp->rendLoopStart_+(idx-ls)*cs ;
	else p=WarpCache::Start(h)+idx*cs ;
	if (ls<0 && idx>=len) p=rp->rendLoopEnd_ ;
	rp->complexPos_=int(p) ;
	rp->complexLatency_=0 ;
	int st=WarpCache::Start(h) ;
	rp->complexFirst_=st<rp->rendLoopStart_?st:rp->rendLoopStart_ ;
}

/************************************************************
 trk2als: Sampler estilo Simpler
 ************************************************************/

// La primera vez que el instrumento se usa como Sampler, sus parámetros se
// deducen de los de LGPT para que suene parecido a como sonaba
void SampleInstrument::InitSimpler() {
	initSimplerFromLGPT() ;
	// trk2als: del filtro de LGPT (0..127) al nuevo (Hz, %, semitonos), con
	// la misma conversión que ya usaba el export a Live
	// trk2als: un sonido que llega con el filtro abierto del todo y sin
	// envolvente (cualquiera recién cargado): su envolvente queda lista para
	// oírse, +24 st con forma de golpe. Con el corte abierto no cambia nada;
	// se oye en cuanto se baja el corte
	bool fresh=false ;
	if (!GetSimpler(SIP_S_FVER)) {
		fresh=(GetSimpler(SIP_S_FFREQ)>=127 && GetSimpler(SIP_S_FENV)==0 && GetSimpler(SIP_S_FTYPE)==0) ;
		FindVariable(SIP_S_FVER)->SetInt(1) ;
		float f=float(GetSimpler(SIP_S_FFREQ)) ;
		FindVariable(SIP_S_FHZ)->SetInt(int(30.0f*powf(22000.0f/30.0f,f/127.0f)+0.5f)) ;
		FindVariable(SIP_S_FQ)->SetInt(GetSimpler(SIP_S_FRES)*100/127) ;
		int st=int(GetSimpler(SIP_S_FENV)*0.9f+(GetSimpler(SIP_S_FENV)>=0?0.5f:-0.5f)) ;
		FindVariable(SIP_S_FST)->SetInt(st<-72?-72:(st>72?72:st)) ;
	}
	// trk2als: antes el corte seguía la envolvente de volumen; la del filtro
	// arranca como copia para que suene igual
	// trk2als: slice pasó de ADSR a fades: su attack de antes es su fade in
	if (GetSimpler(SIP_S_FEVER)==1) {
		FindVariable(SIP_S_FEVER)->SetInt(2) ;
		if (GetSimpler(SIP_S_MODE)==SM_SLICE && GetSimpler(SIP_S_FIN)==0 && GetSimpler(SIP_S_ATK)>0) {
			int a=GetSimpler(SIP_S_ATK) ;
			FindVariable(SIP_S_FIN)->SetInt(a>2000?2000:a) ;
		}
	}
	if (!GetSimpler(SIP_S_FEVER)) {
		FindVariable(SIP_S_FEVER)->SetInt(2) ;
		if (GetSimpler(SIP_S_MODE)==SM_ONESHOT) {
			FindVariable(SIP_S_FATK)->SetInt(GetSimpler(SIP_S_FIN)) ;
			FindVariable(SIP_S_FDEC)->SetInt(0) ;
			FindVariable(SIP_S_FSUS)->SetInt(0) ;
			FindVariable(SIP_S_FREL)->SetInt(GetSimpler(SIP_S_FOUT)) ;
		} else {
			FindVariable(SIP_S_FATK)->SetInt(GetSimpler(SIP_S_ATK)) ;
			FindVariable(SIP_S_FDEC)->SetInt(GetSimpler(SIP_S_DEC)) ;
			FindVariable(SIP_S_FSUS)->SetInt(GetSimpler(SIP_S_SUS)) ;
			FindVariable(SIP_S_FREL)->SetInt(GetSimpler(SIP_S_REL)) ;
		}
	}
	if (fresh) {
		FindVariable(SIP_S_FST)->SetInt(24) ;
		FindVariable(SIP_S_FATK)->SetInt(0) ;
		FindVariable(SIP_S_FDEC)->SetInt(600) ;
		FindVariable(SIP_S_FSUS)->SetInt(-60) ;
	}
}

void SampleInstrument::initSimplerFromLGPT() {
	if (GetSimpler(SIP_S_INIT)) return ;
	FindVariable(SIP_S_INIT)->SetInt(1) ;
	int size=(source_ && source_->GetSize(-1)>0)?source_->GetSize(-1):0 ;
	int lm=loopMode_->GetInt() ;
	if (slices_->GetInt()>1) {
		FindVariable(SIP_S_MODE)->SetInt(SM_SLICE) ;
		FindVariable(SIP_S_SLICES)->SetInt(slices_->GetInt()) ;
	} else if (lm==SILM_LOOP || lm==SILM_LOOP_PINGPONG) {
		FindVariable(SIP_S_MODE)->SetInt(SM_CLASSIC) ;
		FindVariable(SIP_S_LOOP)->SetInt(1) ;
	} else {
		FindVariable(SIP_S_MODE)->SetInt(SM_ONESHOT) ;
	}
	if (size>0) {
		int end=loopEnd_->GetInt() ;
		if (end<=0 || end>size) end=size ;
		FindVariable(SIP_S_START)->SetInt(int(start_->GetInt()*100.0f/size)) ;
		FindVariable(SIP_S_END)->SetInt(int(end*100.0f/size+0.5f)) ;
		int len=end-loopStart_->GetInt() ;
		if (len>0) FindVariable(SIP_S_LLEN)->SetInt(int(len*100.0f/size+0.5f)) ;
	}
	FindVariable(SIP_S_TRANSP)->SetInt(60-rootNote_->GetInt()) ;
	FindVariable(SIP_S_DETUNE)->SetInt(int((fineTune_->GetInt()-0x7F)*100.0f/128.0f)) ;
	int vol=volume_->GetInt() ;
	FindVariable(SIP_S_VOL)->SetInt(vol>0?int(20.0f*log10f(vol/128.0f)-0.5f*(vol<128)):-36) ;
	FindVariable(SIP_S_PAN)->SetInt(int((pan_->GetInt()-0x7F)*50/127)) ;
	FindVariable(SIP_S_FFREQ)->SetInt(cutoff_->GetInt()/2) ;
	FindVariable(SIP_S_FRES)->SetInt(reso_->GetInt()/2) ;
	int mix=filterMix_->GetInt() ;
	FindVariable(SIP_S_FTYPE)->SetInt(mix<0x40?0:(mix<0xC0?1:2)) ;
	// LGPT no tiene envolvente: que suene igual, sin cola
	FindVariable(SIP_S_SUS)->SetInt(0) ;
	FindVariable(SIP_S_REL)->SetInt(0) ;
}

// Región, modo de loop y afinación de la nota que empieza
bool SampleInstrument::startSimpler(renderParams *rp,int channel,unsigned char note,
                                    int &first,bool &sliced,float &offset) {
	if (zoned_) {
		rp->auditionTail_=false ;
		// trk2als: el sample de la tecla entero, en su tono en su raíz, con su
		// loop si lo trae (sin región, slices ni warp)
		const ZoneSource::Zone *z=zones_.Find(rp->midiNote_) ;
		if (!z || !z->src) return false ;
		if (GetSimpler(SIP_S_KIT) && (int(rp->midiNote_)<z->lo || int(rp->midiNote_)>z->hi))
			return false ;   // trk2als: kit: fuera de los pads no suena
		int zsize=z->src->GetSize(-1) ;
		if (zsize<=0) return false ;
		offset=GetSimpler(SIP_S_TRANSP)+GetSimpler(SIP_S_DETUNE)/100.0f+
		       float(int(note)-z->root)+z->cents/100.0f ;
		first=0 ;
		if (GetSimpler(SIP_S_KIT)) {   // trk2als: afinación e inicio del pad
			PadParams pp=GetPad(rp->midiNote_) ;
			offset+=pp.tune ;
			first=int(long(zsize)*pp.start/100) ;
		}
		rp->gate_=true ;
		rp->simplerLoop_=SILM_ONESHOT ;
		rp->rendLoopStart_=0 ;
		rp->rendLoopEnd_=zsize ;
		if (z->loopStart>=0 && z->loopEnd>z->loopStart && z->loopEnd<=zsize) {
			rp->rendLoopStart_=z->loopStart ;
			rp->rendLoopEnd_=z->loopEnd ;
			rp->simplerLoop_=SILM_LOOP ;
		}
		sliced=false ;
		return true ;
	}
	int size=source_->GetSize(rp->midiNote_) ;
	int mode=GetSimpler(SIP_S_MODE) ;
	int a,b ;
	SimplerRegion(a,b) ;
	rp->auditionTail_=false ;
	offset=GetSimpler(SIP_S_TRANSP)+GetSimpler(SIP_S_DETUNE)/100.0f ;
	rp->simplerLoop_=SILM_ONESHOT ;
	rp->gate_=true ;
	switch (mode) {
		case SM_CLASSIC: {
			offset+=note-60 ;          // C3 = tono original, como Simpler
			first=a ;
			if (GetSimpler(SIP_S_LOOP)) {
				int len=int((long long)size*GetSimpler(SIP_S_LLEN)/100) ;
				int ls=b-len ;
				if (ls<a) ls=a ;
				ls=snapZero(ls,a,b-1,rp->midiNote_) ;
				if (ls>=b) ls=b-1 ;
				rp->rendLoopStart_=ls ;
				rp->simplerLoop_=SILM_LOOP ;
			} else {
				rp->rendLoopStart_=a ;
			}
			rp->rendLoopEnd_=b ;
			break ;
		}
		case SM_SYNTH:   // trk2als: el sinte: el "sample" lo va escribiendo él (nunca se acaba)
			rp->synthNote_=float(note)+offset ;
			rp->synthTrig_=true ;
			offset=0 ;
			first=a ;
			rp->rendLoopStart_=a ;
			rp->rendLoopEnd_=b ;
			rp->simplerLoop_=SILM_LOOP ;
			break ;
		case SM_ONESHOT:
			offset+=note-60 ;
			first=a ;
			if (auditionFrom_>a && auditionFrom_<b) { first=auditionFrom_ ; rp->auditionTail_=true ; }
			rp->rendLoopStart_=a ;
			rp->rendLoopEnd_=b ;
			rp->gate_=(GetSimpler(SIP_S_TRIG)==1) ;
			break ;
		case SM_SLICE: {
			std::vector<int> bounds ;
			SliceBounds(bounds) ;
			int idx=int(note)-36 ;     // C1 = rebanada 1, como Simpler
			if (idx<0 || idx+1>=(int)bounds.size()) return false ;
			if (bounds[idx+1]<=bounds[idx]) return false ;
			rp->rendLoopStart_=bounds[idx] ;
			rp->rendLoopEnd_=bounds[idx+1] ;
			first=rp->rendLoopStart_ ;
			rp->gate_=(GetSimpler(SIP_S_TRIG)==1) ;
			break ;
		}
	}
	auditionFrom_=-1 ;
	sliced=(mode==SM_SLICE) ;
	return true ;
}

// trk2als: región start..end en cuadros, como el Snap de Simpler: inicio y
// final en el cruce por cero más cercano, para que no truene al entrar, al
// terminar ni al dar la vuelta
bool SampleInstrument::SimplerRegion(int &a,int &b) {
	a=b=0 ;
	SoundSource *src=currentSource() ;
	if (!src) return false ;
	int size=src->GetSize(-1) ;
	if (size<=0) return false ;
	a=int((long long)size*(GetSimpler(SIP_S_START)*100+GetSimpler(SIP_S_STARTF))/10000) ;
	b=int((long long)size*(GetSimpler(SIP_S_END)*100+GetSimpler(SIP_S_ENDF))/10000) ;
	if (b>size) b=size ;
	a=snapZero(a,0,size,60,src) ;
	if (b<size) b=snapZero(b,a+1,size,60,src) ;
	if (b<=a) b=(a+1<size)?a+1:size ;
	return true ;
}

int SampleInstrument::RegionPoint(bool start) {
	SoundSource *src=currentSource() ;
	int size=src?src->GetSize(-1):0 ;
	int h=start?GetSimpler(SIP_S_START)*100+GetSimpler(SIP_S_STARTF)
	           :GetSimpler(SIP_S_END)*100+GetSimpler(SIP_S_ENDF) ;
	if (h>10000) h=10000 ;
	return int((long long)size*h/10000) ;
}

void SampleInstrument::SetRegionPoint(bool start,int pos) {
	SoundSource *src=currentSource() ;
	int size=src?src->GetSize(-1):0 ;
	if (size<=0) return ;
	int h=int(((long long)pos*10000+size/2)/size) ;   // centésimas de %
	int sh=GetSimpler(SIP_S_START)*100+GetSimpler(SIP_S_STARTF) ;
	int eh=GetSimpler(SIP_S_END)*100+GetSimpler(SIP_S_ENDF) ;
	if (start) { if (h>eh-10) h=eh-10 ; if (h<0) h=0 ; }
	else { if (h<sh+10) h=sh+10 ; if (h>10000) h=10000 ; }
	FindVariable(start?SIP_S_START:SIP_S_END)->SetInt(h/100) ;
	FindVariable(start?SIP_S_STARTF:SIP_S_ENDF)->SetInt(h%100) ;
}

// Bordes de las rebanadas dentro de la región: partes iguales (region) o las
// marcas que puso el usuario (manual). La primera empieza en start
void SampleInstrument::SliceBounds(std::vector<int> &bounds) {
	bounds.clear() ;
	int a,b ;
	if (!SimplerRegion(a,b)) return ;
	bounds.push_back(a) ;
	if (GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL) {
		std::vector<int> m=GetMarks() ;
		int gap=MinMarkGap() ;   // marcas demasiado pegadas no hacen rebanada
		for (size_t i=0;i<m.size() && bounds.size()<128;i++) {
			if (m[i]>=bounds.back()+gap && m[i]<=b-gap) bounds.push_back(m[i]) ;
		}
	} else {
		int n=GetSimpler(SIP_S_SLICES) ;
		if (n<1) n=1 ;
		int len=(b-a)/n ;
		for (int k=1;k<n && len>0;k++) {
			int x=snapZero(a+k*len,bounds.back()+1,b-1,60,currentSource()) ;
			if (x>bounds.back() && x<b) bounds.push_back(x) ;
		}
	}
	bounds.push_back(b) ;
}

std::vector<int> SampleInstrument::GetMarks() {
	std::vector<int> m ;
	const char *s=FindVariable(SIP_S_MARKS)->GetString() ;
	while (s && *s) {
		char *end ;
		long v=strtol(s,&end,10) ;
		if (end==s) break ;
		if (v>0) m.push_back(int(v)) ;
		s=end ;
		while (*s==',' || *s==' ') s++ ;
	}
	std::sort(m.begin(),m.end()) ;
	m.erase(std::unique(m.begin(),m.end()),m.end()) ;
	return m ;
}

void SampleInstrument::SetMarks(const std::vector<int> &marks) {
	std::string out ;
	char num[16] ;
	for (size_t i=0;i<marks.size();i++) {
		sprintf(num,i?",%d":"%d",marks[i]) ;
		out+=num ;
	}
	FindVariable(SIP_S_MARKS)->SetString(out.c_str()) ;
}

int SampleInstrument::AddMark(int frame) {
	SoundSource *src=currentSource() ;
	if (!src) return -1 ;
	int size=src->GetSize(-1) ;
	if (frame<=0 || frame>=size) return -1 ;
	frame=snapZero(frame,1,size-1,60,src) ;
	std::vector<int> m=GetMarks() ;
	if (m.size()>=127) return -1 ;          // Live: hasta 128 rebanadas
	// no tan pegada a otra marca ni a start/end (si no, la rebanada no se oye)
	int gap=MinMarkGap() ;
	int a,b ;
	if (SimplerRegion(a,b) && (frame<a+gap || frame>b-gap)) return -1 ;
	for (size_t i=0;i<m.size();i++) if (abs(m[i]-frame)<gap) return -1 ;
	m.push_back(frame) ;
	std::sort(m.begin(),m.end()) ;
	m.erase(std::unique(m.begin(),m.end()),m.end()) ;
	SetMarks(m) ;
	return frame ;
}

// distancia mínima entre marcas: 30 ms del sample. En config.xml,
// <SLICEMARKGAP value="10"/> la cambia (en ms) y value="0" la quita
int SampleInstrument::MinMarkGap() {
	static int ms=-1 ;
	if (ms<0) {
		const char *v=Config::GetInstance()->GetValue("SLICEMARKGAP") ;
		ms=v?atoi(v):30 ;
		if (ms<0) ms=0 ;
	}
	if (ms==0) return 1 ;       // sin mínimo: solo que no caigan en el mismo cuadro
	SoundSource *src=currentSource() ;
	int rate=src?src->GetSampleRate(-1):44100 ;
	if (rate<=0) rate=44100 ;
	return rate*ms/1000 ;
}

bool SampleInstrument::RemoveMarkNear(int frame,int tolerance) {
	std::vector<int> m=GetMarks() ;
	int best=-1, dist=tolerance+1 ;
	for (size_t i=0;i<m.size();i++) {
		int d=abs(m[i]-frame) ;
		if (d<dist) { dist=d ; best=int(i) ; }
	}
	if (best<0) return false ;
	m.erase(m.begin()+best) ;
	SetMarks(m) ;
	return true ;
}

// trk2als: el cruce por cero más cercano a 'frame' (dentro de lo..hi), donde
// la onda cambia de signo; busca hasta ~25 ms a cada lado. Con estéreo mira
// la suma de los canales
SoundSource *SampleInstrument::currentSource() {
	int index=GetSampleIndex() ;
	SoundSource *src=(index>=0)?SamplePool::GetInstance()->GetSource(index):0 ;
	return src?src:source_ ;
}

int SampleInstrument::snapZero(int frame,int lo,int hi,unsigned char note,SoundSource *src) {
	if (!GetSimpler(SIP_S_SNAP)) return frame ;   // snap apagado: libre
	if (!src) src=source_ ;
	if (!src) return frame ;
	short *wav=(short *)src->GetSampleBuffer(note) ;
	int size=src->GetSize(note) ;
	int ch=src->GetChannelCount(note) ;
	if (!wav || size<2 || ch<1) return frame ;
	if (lo<1) lo=1 ;
	if (hi>size-1) hi=size-1 ;
	if (frame<lo || frame>hi) return frame ;
	#define SMP(i) (ch==1?int(wav[i]):int(wav[(i)*ch])+int(wav[(i)*ch+1]))
	for (int d=0;d<1100;d++) {
		for (int s=-1;s<=1;s+=2) {
			int i=frame+s*d ;
			if (i<lo || i>hi) continue ;
			int p=SMP(i-1), q=SMP(i) ;
			if (q==0 || (p<0)!=(q<0)) {
				// de las dos muestras del cruce, la más cercana a cero
				return (abs(p)<abs(q))?i-1:i ;
			}
		}
	}
	#undef SMP
	return frame ;
}

// Filtro del Sampler: state variable de 12 dB/oct (forma TPT de Cytomic),
// pasabajos, pasabanda o pasaaltos de verdad, en Hz
void SampleInstrument::simplerFilter(renderParams *rp,fixed *buffer,int size) {
	// cambiar de tipo (o prender / apagar el filtro) de golpe hace click: la
	// salida pasa de uno a otro en ~6 ms. El SVF da LP, BP y HP a la vez
	const int XF=256 ;
	int want=(rp->svfType_<0)?3:rp->svfType_ ;
	if (rp->svfCur_<0 || rp->svfCur_>3) { rp->svfCur_=want ; rp->svfXf_=0 ; }
	else if (want!=rp->svfCur_) {
		if (rp->svfCur_==3 && rp->svfXf_==0)   // venía sin filtro: estado limpio
			rp->svfIc1_[0]=rp->svfIc1_[1]=rp->svfIc2_[0]=rp->svfIc2_[1]=0 ;
		rp->svfPrev_=rp->svfCur_ ;
		rp->svfCur_=want ;
		rp->svfXf_=XF ;
	}
	if (rp->svfCur_==3 && rp->svfXf_==0) return ;
	float k=rp->svfK_ ;
	int type=rp->svfCur_, prev=rp->svfPrev_ ;
	// por tramos de 32 muestras (< 1 ms): la envolvente mueve el corte sin
	// escalones audibles
	for (int at=0;at<size;at+=32) {
		int n=(size-at<32)?size-at:32 ;
		filterEnvBlock(rp,n) ;
		// el corte no salta de golpe (un escalón de la curva hacía click): se
		// acerca en ~4 ms, en octavas
		float hz=rp->svfHz_ ;
		if (hz<20) hz=20 ;
		if (!(rp->svfHzS_>=20 && rp->svfHzS_<=22050)) rp->svfHzS_=hz ;   // nota nueva: sin deslizar
		else if (rp->svfHzS_!=hz) {
			float r=hz/rp->svfHzS_ ;
			if (r>0.995f && r<1.005f) rp->svfHzS_=hz ;
			else rp->svfHzS_*=powf(r,0.17f*n/32.0f) ;
		}
		hz=rp->svfHzS_ ;
		if (rp->svfSt_!=0) hz*=powf(2.0f,rp->svfSt_*rp->fenvLevel_/12.0f) ;
		if (hz<20) hz=20 ;
		if (hz>20000) hz=20000 ;
		float g=tanf(3.14159265f*hz/44100.0f) ;
		float a1=1.0f/(1.0f+g*(g+k)), a2=g*a1, a3=g*a2 ;
		for (int c=0;c<2;c++) {
			float ic1=rp->svfIc1_[c], ic2=rp->svfIc2_[c] ;
			int xf=rp->svfXf_ ;
			fixed *p=buffer+2*at+c ;
			for (int i=0;i<n;i++,p+=2) {
				float v0=fp2fl(*p) ;
				float v3=v0-ic2 ;
				float v1=a1*ic1+a2*v3 ;
				float v2=ic2+a2*ic1+a3*v3 ;
				ic1=2*v1-ic1 ;
				ic2=2*v2-ic2 ;
				// pasabanda normalizado: 0 dB en la frecuencia de corte
				float out=(type==0)?v2:((type==1)?k*v1:((type==2)?v0-k*v1-v2:v0)) ;
				if (xf>0) {
					float old=(prev==0)?v2:((prev==1)?k*v1:((prev==2)?v0-k*v1-v2:v0)) ;
					float t=float(xf)/XF ;
					out=out+(old-out)*t ;
					xf-- ;
				}
				*p=fl2fp(out) ;
			}
			// sin ruido de números diminutos
			if (fabsf(ic1)<1e-12f) ic1=0 ;
			if (fabsf(ic2)<1e-12f) ic2=0 ;
			rp->svfIc1_[c]=ic1 ;
			rp->svfIc2_[c]=ic2 ;
		}
		rp->svfXf_=(rp->svfXf_>n)?rp->svfXf_-n:0 ;
	}
}

// Envolvente del filtro: ADSR propio, avanza por bloque (el corte se
// calcula por bloque); se suelta junto con la de volumen
void SampleInstrument::filterEnvBlock(renderParams *rp,int size) {
	const float msr=44.1f ;
	float atk=envMs(SIP_S_FATK)*msr ;
	float dec=envMs(SIP_S_FDEC)*msr ;
	float rel=envMs(SIP_S_FREL)*msr ;
	int db=GetSimpler(SIP_S_FSUS) ;
	float sus=(db<=-60)?0:powf(10.0f,db/20.0f) ;
	float l=rp->fenvLevel_ ;
	switch (rp->fenvStage_) {
		case 0:
			l=(atk<1)?1:float(rp->fenvClock_)/atk ;
			if (l>=1) { l=1 ; rp->fenvStage_=1 ; rp->fenvClock_=0 ; }
			break ;
		case 1:
			l=(dec<1)?sus:1.0f-(1.0f-sus)*float(rp->fenvClock_)/dec ;
			if (l<=sus || dec<1) { l=sus ; rp->fenvStage_=2 ; rp->fenvClock_=0 ; }
			break ;
		case 2:
			l=sus ;
			break ;
		case 3:
			l=(rel<1)?0:rp->fenvReleaseFrom_*(1.0f-float(rp->fenvClock_)/rel) ;
			if (l<=0) { l=0 ; rp->fenvStage_=4 ; }
			break ;
		default:
			l=0 ;
	}
	rp->fenvLevel_=l ;
	rp->fenvClock_+=size ;
}

float SampleInstrument::envMs(FourCC id) {
	int v=GetSimpler(id) ;
	return v<0?0:float(v) ;
}

// Una vez por bloque: parámetros en vivo, glide y la envolvente sobre el filtro
void SampleInstrument::simplerBlock(renderParams *rp,int size) {
	// trk2als: cuánto le falta a la voz para el final de la región, antes de
	// renderizar este bloque (para el fade del final)
	{
		float sp=fp2fl(rp->speed_) ;
		rp->toEnd_=(sp>0)?(rp->rendLoopEnd_-rp->position_)/sp:1e9f ;
	}
	// solo lo que cambió en la pantalla, para no pisar los comandos de la phrase
	int v ;
	if ((v=GetSimpler(SIP_S_VOL))!=rp->sLive_[0]) {
		rp->sLive_[0]=v ;
		float lin=128.0f*powf(10.0f,v/20.0f) ;
		if (lin>255) lin=255 ;
		rp->volume_=rp->baseVolume_=fl2fp(lin) ;
	}
	if ((v=GetSimpler(SIP_S_PAN))!=rp->sLive_[1]) {
		rp->sLive_[1]=v ;
		int pan=0x7F+v*127/50 ;
		rp->pan_=rp->basePan_=i2fp(pan<0?0:(pan>254?254:pan)) ;
	}
	// el filtro de LGPT queda abierto: el Sampler usa el suyo (simplerFilter)
	if (rp->sLive_[3]!=1) {
		rp->sLive_[3]=1 ;
		rp->cutoff_=rp->baseFCut_=i2fp(1) ;
		rp->reso_=rp->baseFRes_=0 ;
		rp->filterMix_=0 ;
	}
	// coeficientes del filtro (TPT / state variable, 12 dB/oct). La envolvente
	// de volumen mueve el corte 'fenv' semitonos
	{
		int type=GetSimpler(SIP_S_FTYPE) ;
		float hz=float(GetSimpler(SIP_S_FHZ)) ;
		int st=GetSimpler(SIP_S_FST) ;
		float q=GetSimpler(SIP_S_FQ)/100.0f ;
		if (q<0) q=0 ;
		if (q>1) q=1 ;
		// key tracking: 100 % = el corte sube un semitono por semitono (C3 = 0)
		int key=GetSimpler(SIP_S_FKEY) ;
		if (key!=0) hz*=powf(2.0f,(rp->midiNote_-60)*key/100.0f/12.0f) ;
		// sin filtro si no cambia nada (pasabajos abierto, sin resonancia)
		if (type==0 && hz>=19000 && q<=0 && st<=0 && key<=0) {
			rp->svfType_=-1 ;
			filterEnvBlock(rp,size) ;   // la envolvente sigue corriendo
		} else {
			rp->svfType_=(type<0||type>2)?0:type ;
			rp->svfHz_=hz ;
			rp->svfSt_=float(st) ;
			// Q de 0.707 (0 %) a ~25 (100 %), exponencial
			float Q=0.7071f*powf(25.0f/0.7071f,q) ;
			rp->svfK_=1.0f/Q ;
			// el corte se calcula en simplerFilter cada 32 muestras
		}
	}
	int ch=int(rp-renderParams_) ;
	if (rp->glideLeft_>0 && size>0) {
		rp->glideLeft_-=size ;
		if (rp->glideLeft_<0) rp->glideLeft_=0 ;
		float k=1.0f-float(rp->glideLeft_)/rp->glideLen_ ;
		if (rp->sWarp_==SW_COMPLEX) {
			// con warp complex el glide es de transposición, no de velocidad
			float from=rp->glideFromT_, to=warpTarget_[ch] ;
			rp->warpTranspose_=from*powf(to/from,k) ;
		} else {
			float from=fp2fl(rp->glideFrom_), to=fp2fl(rp->baseSpeed_) ;
			rp->speed_=fl2fp(from+(to-from)*k) ;
		}
	}
	if (rp->sWarp_==SW_COMPLEX) lastWarpT_[ch]=rp->warpTranspose_ ;
}

// Envolvente de amplitud, muestra por muestra. Classic y slice: ADSR;
// one-shot: fade in y fade out (al soltar, en modo gate)
void SampleInstrument::simplerEnvelope(renderParams *rp,fixed *buffer,int size) {
	const float msr=44.1f ;   // muestras por milisegundo
	// one-shot y slice: fade in / fade out (como Simpler); classic y multicapa: ADSR
	int emode=GetSimpler(SIP_S_MODE) ;
	bool oneShot=!IsZoned() && (emode==SM_ONESHOT || emode==SM_SLICE) ;
	float atk=(oneShot?envMs(SIP_S_FIN):envMs(SIP_S_ATK))*msr ;
	// el sample casi nunca empieza en silencio (sobre todo con 'start' a media
	// onda): 1.5 ms de entrada como mínimo, para que no haga click
	if (atk<1.5f*msr || rp->auditionTail_) atk=1.5f*msr ;
	float dec=oneShot?0:envMs(SIP_S_DEC)*msr ;
	float sus=1.0f ;
	if (!oneShot) {
		int db=GetSimpler(SIP_S_SUS) ;
		sus=(db<=-60)?0:powf(10.0f,db/20.0f) ;
	}
	float rel=(oneShot?envMs(SIP_S_FOUT):envMs(SIP_S_REL))*msr ;
	// al soltar la nota nunca de golpe: 8 ms como mínimo (el click del final;
	// con graves, 3 ms todavía tronaba)
	if (rel<8*msr) rel=8*msr ;
	// las rampas cortas (hasta 20 ms) van en S (coseno): una recta tan corta
	// sobre un grave suena a click; las largas siguen rectas
	bool softRel=rel<=20*msr, softAtk=atk<=20*msr ;
	// STOP o nota cortada: un release corto (no de golpe: eso es un click)
	if (rp->chokeMs_>0 && rel>rp->chokeMs_*msr) rel=rp->chokeMs_*msr ;
	// one-shot: el fade out cae al final de la región (como en Simpler); sin
	// loop, en cualquier modo, al menos 3 ms antes del final para que la voz
	// no se corte en seco. Muestras de salida que le faltan a la voz al
	// terminar este bloque
	float fadeOut=oneShot?envMs(SIP_S_FOUT)*msr:0 ;
	if (rp->simplerLoop_!=SILM_LOOP && fadeOut<6*msr) fadeOut=6*msr ;
	bool softOut=fadeOut<=20*msr ;
	float leftEnd=rp->toEnd_ ;   // calculado antes de renderizar el bloque
	for (int i=0;i<size;i++) {
		float l=rp->envLevel_ ;
		switch (rp->envStage_) {
			case 0:   // ataque
				l=(atk<1)?1:float(rp->envClock_)/atk ;
				if (l>=1) { l=1 ; rp->envStage_=1 ; rp->envClock_=0 ; }
				else if (softAtk) l=0.5f*(1.0f-cosf(3.14159265f*l)) ;
				break ;
			case 1:   // decay hacia el sustain
				l=(dec<1)?sus:1.0f-(1.0f-sus)*float(rp->envClock_)/dec ;
				if (l<=sus || dec<1) { l=sus ; rp->envStage_=2 ; rp->envClock_=0 ; }
				break ;
			case 2:
				l=sus ;
				break ;
			case 3:   // release
				{
					float t=(rel<1)?1:float(rp->envClock_)/rel ;
					if (t>=1) { l=0 ; rp->envStage_=4 ; }
					else l=rp->envReleaseFrom_*(softRel?0.5f*(1.0f+cosf(3.14159265f*t)):(1.0f-t)) ;
				}
				break ;
			default:
				l=0 ;
		}
		rp->envLevel_=l ;
		rp->envClock_++ ;
		if (rp->padDecay_>0) {   // trk2als: decay del pad: el golpe se apaga en ese tiempo
			float t=float(rp->padClock_++)/rp->padDecay_ ;
			float g=t>=1?0:(1-t)*(1-t) ;
			l*=g ;
			if (t>=1) rp->envStage_=4 ;
		}
		if (fadeOut>=1) {
			float left=leftEnd-i ;
			if (left<fadeOut) {
				float t=(left>0)?left/fadeOut:0 ;
				l*=softOut?0.5f*(1.0f-cosf(3.14159265f*t)):t ;
			}
		}
		fixed f=fl2fp(l) ;
		buffer[2*i]=fp_mul(buffer[2*i],f) ;
		buffer[2*i+1]=fp_mul(buffer[2*i+1],f) ;
	}
	if (rp->envStage_>=4) rp->finished_=true ;   // terminó la cola
}

// Tempo probable de la región start..end: el que da un número entero de
// compases (o, si no, de beats) entre 85 y 170 bpm. 0 si no hay sample
int SampleInstrument::GuessSimplerBpm() {
	int index=GetSampleIndex() ;
	if (index<0) return 0 ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(index) ;
	if (!src || src->GetSampleRate(-1)<=0 || src->GetSize(-1)<=0) return 0 ;
	float secs=src->GetSize(-1)*(GetSimpler(SIP_S_END)-GetSimpler(SIP_S_START))/100.0f/src->GetSampleRate(-1) ;
	if (secs<=0.05f) return 120 ;
	static const int bars[5]={1,2,4,8,16} ;
	for (int k=0;k<5;k++) {
		float bpm=240.0f*bars[k]/secs ;
		if (bpm>=85 && bpm<170) return int(bpm+0.5f) ;
	}
	for (int beats=1;beats<=64;beats++) {
		float bpm=60.0f*beats/secs ;
		if (bpm>=85 && bpm<170) return int(bpm+0.5f) ;
	}
	return 120 ;
}

void SampleInstrument::AssignSample(int i) {

	 Variable *v=FindVariable(SIP_SAMPLE) ;
	 v->SetInt(i) ;
} ;

/************************************************************
 trk2als: tempo de los loops
 ************************************************************/

float SampleInstrument::GetLoopSeconds() {
	int index=GetSampleIndex() ;
	if (index<0) return 0 ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(index) ;
	if (!src || src->GetSampleRate(-1)<=0) return 0 ;
	int a=loopStart_->GetInt() ;
	int b=loopEnd_->GetInt() ;
	if (b<=a) b=src->GetSize(-1) ;
	return float(b-a)/float(src->GetSampleRate(-1)) ;
}

void SampleInstrument::UpdateLoopBars() {
	int bpm=loopBpm_->GetInt() ;
	float secs=GetLoopSeconds() ;
	if (bpm<=0 || secs<=0) return ;          // 0 = compases a mano
	int bars=int(secs*bpm/240.0f+0.5f) ;
	loopBars_->SetInt(bars<1?1:(bars>16?16:bars)) ;
	float bar=GetBarSamples() ;
	if (bar>0) {
		int from=int(loopStart_->GetInt()/bar+0.5f)+1 ;
		fromBar_->SetInt(from<1?1:from) ;
	}
}

float SampleInstrument::GetBarSamples() {
	int bpm=loopBpm_->GetInt() ;
	int index=GetSampleIndex() ;
	if (bpm<=0 || index<0) return 0 ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(index) ;
	if (!src || src->GetSampleRate(-1)<=0) return 0 ;
	return src->GetSampleRate(-1)*240.0f/bpm ;
}

float SampleInstrument::GetSampleBars() {
	float bar=GetBarSamples() ;
	int index=GetSampleIndex() ;
	if (bar<=0 || index<0) return 0 ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(index) ;
	return src ? src->GetSize(-1)/bar : 0 ;
}

// El usuario movió "from bar" o "bars": la región salta a esos compases
void SampleInstrument::ApplyBarRegion() {
	float bar=GetBarSamples() ;
	if (bar<=0) return ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(GetSampleIndex()) ;
	int size=src->GetSize(-1) ;
	int total=int(size/bar+0.5f) ;          // compases completos (con tolerancia)
	if (total<1) total=1 ;
	int from=fromBar_->GetInt() ;
	if (from>total) from=total ;
	if (from<1) from=1 ;
	int bars=loopBars_->GetInt() ;
	if (from-1+bars>total) bars=total-from+1 ;
	if (bars<1) bars=1 ;
	int a=int((from-1)*bar+0.5f) ;
	int b=int((from-1+bars)*bar+0.5f) ;
	if (b>size) b=size ;
	if (a>=b) return ;
	fromBar_->SetInt(from) ;
	start_->SetInt(a) ;
	loopEnd_->SetInt(b) ;          // primero el final para que loop start < end
	loopStart_->SetInt(a) ;
	loopBars_->SetInt(bars) ;
}

// Busca "85bpm" o un número 60–200 suelto en el nombre ("loop_85_Am");
// si no hay, el tempo entre 80 y 160 que dé 1, 2, 4, 8 o 16 compases
int SampleInstrument::DetectLoopBpm() {
	int index=GetSampleIndex() ;
	float secs=GetLoopSeconds() ;
	if (index<0 || secs<=0) return 0 ;
	return DetectBpm(SamplePool::GetInstance()->GetNameList()[index],secs,true) ;
}

int SampleInstrument::DetectBpm(const char *name, float secs, bool allowGuess) {
	if (!name || secs<=0) return 0 ;
	int candidates[16] ;
	int n=0 ;
	for (const char *p=name;*p && n<16;) {
		if (*p>='0' && *p<='9') {
			int v=0 ;
			const char *q=p ;
			while (*q>='0' && *q<='9') { v=v*10+(*q-'0') ; q++ ; }
			bool sepBefore=(p==name) || !((p[-1]>='a'&&p[-1]<='z')||(p[-1]>='A'&&p[-1]<='Z')) ;
			bool bpmAfter=(strncasecmp(q,"bpm",3)==0) || (*q==' ' && strncasecmp(q+1,"bpm",3)==0) ;
			bool sepAfter=bpmAfter || !((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')) ;
			if (v>=60 && v<=200 && ((sepBefore && sepAfter) || bpmAfter)) {
				if (bpmAfter) { candidates[0]=v ; n=1 ; break ; }
				candidates[n++]=v ;
			}
			p=q ;
		} else {
			p++ ;
		}
	}
	for (int i=0;i<n;i++) {        // el número tiene que dar compases casi enteros
		float bars=secs*candidates[i]/240.0f ;
		int r=int(bars+0.5f) ;
		if (bars>=0.5f && fabsf(bars-r)<0.05f*(r>1?r:1)) return candidates[i] ;
	}
	if (!allowGuess) return 0 ;
	for (int bars=1;bars<=16;bars*=2) {
		float bpm=bars*240.0f/secs ;
		if (bpm>=80 && bpm<=160) return int(bpm+0.5f) ;
	}
	return 0 ;
}

// auto: los loops (bpm en el nombre, o "loop" en el nombre y una duración de
// compases enteros) con complex; lo demás (one-shots, notas) sin warp
int SampleInstrument::DefaultWarp(int *bpm) {
	static int mode=-2 ;          // -1 = auto
	if (mode==-2) {
		const char *v=Config::GetInstance()->GetValue("DEFAULTWARP") ;
		mode=-1 ;
		if (v) {
			if (!strcasecmp(v,"off")) mode=SW_OFF ;
			else if (!strcasecmp(v,"re-pitch") || !strcasecmp(v,"repitch")) mode=SW_REPITCH ;
			else if (!strcasecmp(v,"complex")) mode=SW_COMPLEX ;
		}
	}
	int index=GetSampleIndex() ;
	SoundSource *src=currentSource() ;
	if (bpm) *bpm=0 ;
	if (index<0 || !src || src->GetSampleRate(-1)<=0) return SW_OFF ;
	const char *name=SamplePool::GetInstance()->GetNameList()[index] ;
	float secs=float(src->GetSize(-1))/src->GetSampleRate(-1) ;
	int found=DetectBpm(name,secs,false) ;
	if (!found) {
		std::string low(name) ;
		for (size_t i=0;i<low.size();i++) low[i]=tolower(low[i]) ;
		if (low.find("loop")!=std::string::npos) found=DetectBpm(name,secs,true) ;
	}
	if (bpm) *bpm=found ;
	if (mode>=0) return mode ;
	return found?SW_COMPLEX:SW_OFF ;
}

int SampleInstrument::GetSampleIndex() {
	 Variable *v=FindVariable(SIP_SAMPLE) ;
	 return v->GetInt() ;
} ;

void SampleInstrument::SetVolume(int volume) {
	Variable *v=FindVariable(SIP_VOLUME) ;
	v->SetInt(volume) ;
} ;

int SampleInstrument::GetVolume() {
	Variable *v=FindVariable(SIP_VOLUME) ;
	return v->GetInt() ;
} ;

int SampleInstrument::GetSampleSize(int channel) {
	if (source_) {
		if (channel<0) return source_->GetSize(-1) ;   // trk2als: antes leía renderParams_[-1]
		 renderParams *rp=renderParams_+channel ;
		return source_->GetSize(rp->midiNote_) ;
	} ;
	return 0 ;
} ;

int SampleInstrument::GetLoopEnd() { return loopEnd_->GetInt(); }

bool SampleInstrument::IsInitialized() {
    return (source_!=0) ;
} ;

void SampleInstrument::updateInstrumentData(bool search) {

	SamplePool *pool=SamplePool::GetInstance() ;

	// Get the source index

  Variable *vSample=FindVariable(SIP_SAMPLE) ;
	int index=vSample->GetInt() ;
	int instrSize=0 ;

	if (index!=NO_SAMPLE)
  {
		source_=pool->GetSource(index) ;
		if (source_&&(!source_->IsMulti()))
    {
    		instrSize=source_->GetSize(-1) ;
		}
	}
	rebuildZones() ;   // trk2als: si el sample es el de las zonas, source_ son las zonas

	Variable *v=FindVariable(SIP_END) ;
	v->SetInt(instrSize) ;
	v=FindVariable(SIP_LOOPSTART) ;
	v->SetInt(0) ;
	v=FindVariable(SIP_START) ;
	v->SetInt(0) ;
	dirty_=false ;
} ;

void SampleInstrument::Update(Observable &o,I_ObservableData *d)
{
	WatchedVariable &v=(WatchedVariable &)o ;
	FourCC id=v.GetID() ;

	switch(id) {
		case SIP_SAMPLE:	
		{
			if (running_) {
				dirty_=true ; // we'll update later, when instrument gets re-triggered
				// trk2als: pero el largo del sample nuevo ya se ve (y con él
				// el bpm del loop); 'running_' se queda prendido después de
				// tocar en una pista MIDI, y así el bpm salía del sample anterior
				SoundSource *src=0 ;
				int index=v.GetInt() ;
				if (index!=NO_SAMPLE) src=SamplePool::GetInstance()->GetSource(index) ;
				int size=(src && !src->IsMulti())?src->GetSize(-1):0 ;
				FindVariable(SIP_END)->SetInt(size) ;
				FindVariable(SIP_LOOPSTART)->SetInt(0) ;
				FindVariable(SIP_START)->SetInt(0) ;
			} else {
				updateInstrumentData(true) ;
				SetChanged() ;
				NotifyObservers();
			} ;
		}
			break ;



		case SIP_LOOPBPM:
		case SIP_LOOPSTART:
		case SIP_END:
			UpdateLoopBars() ; // trk2als
			break ;

		default:
   //         Trace::Dump("Got notification from %c%c%c%c",fourcc[0],fourcc[1],fourcc[2],fourcc[3]) ;
            break ;
	};
} ;

void SampleInstrument::ProcessCommand(int channel,FourCC cc,ushort value) {
	// trk2als: en un kit, el velocity de la nota va a la voz donde cayó el golpe
	if (channel<SONG_CHANNEL_COUNT && (cc==I_CMD_MVEL || cc==I_CMD_VOLM) && IsKit())
		channel=lastKitSlot_[channel] ;
	
 	renderParams *rp=renderParams_+channel ;
	if (!source_) return ;

	switch(cc) {
    case I_CMD_LPOF: {
        // LPOF (Loop Offset) shifts the loop window (rendLoopStart_,
        // rendLoopEnd_) within the sample. Two coexisting use cases:
        //
        // 1) Wavetable / single-cycle synthesis (SILM_OSC):
        //    The loop length defines the oscillator's pitch (freq * length /
        //    driverRate). LPOF is used to scan across stored waveforms or
        //    modulate timbre (e.g. PWM) WITHOUT changing pitch. We must NOT
        //    move the playhead here — doing so makes pitch glide as the bounds
        //    shift mid-cycle, producing unwanted microtonal artifacts.
        //
        // 2) Granular stretching (every other mode — loop, ping-pong, loopsync,
        //    oneshot, sliced or not):
        //    Advancing the playhead alongside the loop window drags playback
        //    through the source faster than the note's natural rate, decoupling
        //    stretch speed from pitch. With short loops + LPOF + HOP in a
        //    table, this produces timestretch / breakbeat-style scrubbing.
        //
        //    In oneshot or slice modes the playhead may cross into territory
        //    the user didn't anticipate (notes ending early, slices bleeding
        //    into neighbors). That's intentional — these are useful artifacts,
        //    not bugs.

        SampleInstrumentLoopMode loopmode = effectiveLoopMode(rp);
        bool dragPlayhead = (loopmode != SILM_OSC);

        if (value > 0x8000) {
            // Backward shift (two's complement): 0xFFFF = -1, 0x8001 = -32767
            int shift = (int)(0x10000 - value);
            if (shift > rp->rendLoopStart_) { // Don't push start below sample 0
                shift = rp->rendLoopStart_;
            }
            rp->rendLoopEnd_ -= shift;
            rp->rendLoopStart_ -= shift;
            if (dragPlayhead) {
                rp->position_ -= shift;
            }
        } else if (value > 0) { // LPOF 0000 is a no-op
            int sampleSize = source_->GetSize(rp->midiNote_);
            int shift = (int)value;
            // Clamp so rendLoopEnd_ doesn't escape the sample. When the window
            // hits the end, further forward LPOFs become no-ops — the loop is
            // parked at the boundary until something resets it.
            if (rp->rendLoopEnd_ + shift >= sampleSize) {
                shift = sampleSize - rp->rendLoopEnd_;
            }
            if (shift > 0) {
                rp->rendLoopEnd_ += shift;
                rp->rendLoopStart_ += shift;
                if (dragPlayhead) {
                    rp->position_ += shift;
                }
            }
        }
        break;
    }
    case I_CMD_PLOF: {
        if (!source_)
            return;
        int wavSize = source_->GetSize(rp->midiNote_);
        float chkSize = wavSize / 256.0f;
        int absShft = value >> 8;
        if (absShft != 0) {
            rp->position_ = chkSize * absShft;
        };
        int relSfht = value & 0xFF;
        if (relSfht > 127)
            relSfht = relSfht - 256;
        rp->position_ += relSfht * chkSize;
        while (rp->position_ < 0) {
            rp->position_ = wavSize + rp->position_;
        };
        while (rp->position_ >= wavSize) {
            rp->position_ -= wavSize;
        };
        rp->couldClick_ = SHOULD_KILL_CLICKS;
			}
			break ;

		case I_CMD_ARPG:
			{
				rp->arp_.SetData(value) ;
				if (!rp->arp_.Enabled()) {
					rp->arp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->arp_) ;
				}
			}
			break ;

		case I_CMD_VOLM:
			{
				float targetVolume=float(value&0xFF) ;
				float speed=float(value>>8) ;
                float startVolume=fp2fl(rp->volume_) ;
				float baseVolume=fp2fl(rp->baseVolume_) ;
                
				int sampleCount=int(4*SyncMaster::GetInstance()->GetTickSampleCount()) ;
				speed=(speed==0)?0:fabs(targetVolume-startVolume)*KRATE_SAMPLE_COUNT/float(speed)/sampleCount ;
				rp->volumeRamp_.SetData(targetVolume-baseVolume,speed,startVolume-baseVolume) ;
				if (!rp->volumeRamp_.Enabled()) {
					rp->volumeRamp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->volumeRamp_) ;
				}
			}
			break ;

		case I_CMD_MVEL:   // trk2als: velocity de la nota (0-127 x2; 100 = sin cambio)
			{
				float v=float(value&0xFF)/2.0f/100.0f ;
				float vol=fp2fl(rp->baseVolume_)*v*v ;
				if (vol>255.0f) vol=255.0f ;
				rp->volume_=rp->baseVolume_=fl2fp(vol) ;
			}
			break ;

		case I_CMD_PAN_:
			{
				float targetPan=float(value&0xFF) ;
				if (targetPan==0xFF) {
					targetPan=0xFE ;
				}
				float basePan=fp2fl(rp->basePan_) ;
				float speed=float(value>>8) ;
				float startPan=fp2fl(rp->pan_) ;
				int sampleCount=int(4*SyncMaster::GetInstance()->GetTickSampleCount()) ;
				speed=(speed==0)?0:fabs(targetPan-startPan)*KRATE_SAMPLE_COUNT/float(speed)/sampleCount ;
				rp->panner_.SetData(targetPan-basePan,speed,startPan-basePan) ;
				if (!rp->panner_.Enabled()) {
					rp->panner_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->panner_) ;
				}
			}
			break ;

		case I_CMD_FCUT:
			{
				float target=float(value&0xFF)/255.0f ;
				float speed=float(value>>8) ;
                float start=fp2fl(rp->cutoff_) ;
                float baseCut=fp2fl(rp->baseFCut_) ;
				int sampleCount=int(4*SyncMaster::GetInstance()->GetTickSampleCount()) ;
				speed=(speed==0)?0:fabs(target-start)*KRATE_SAMPLE_COUNT/float(speed)/sampleCount ;
 				rp->cutRamp_.SetData(target-baseCut,speed,start-baseCut) ;
				if (!rp->cutRamp_.Enabled()) {
					rp->cutRamp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->cutRamp_) ;
				}
			}
			break ;

		case I_CMD_FRES:
			{
				float target=float(value&0xFF)/255.0f ;
				float speed=float(value>>8) ;
                float start=fp2fl(rp->reso_) ;
				float baseRes=fp2fl(rp->baseFRes_) ;                
				int sampleCount=int(4*SyncMaster::GetInstance()->GetTickSampleCount()) ;
				speed=(speed==0)?0:fabs(target-start)*KRATE_SAMPLE_COUNT/float(speed)/sampleCount ;
 				rp->resRamp_.SetData(target-baseRes,speed,start-baseRes) ;
				if (!rp->resRamp_.Enabled()) {
					rp->resRamp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->resRamp_) ;
				}
			}
			break ;

			// Feedback mix

		case I_CMD_FBMX:
			{
				float target=float(value&0xFF)/255.0f ;
				float speed=float(value>>8) ;
                float start=fp2fl(rp->fbMix_) ;
				float baseMix=fp2fl(rp->baseFbMix_) ;                
				int sampleCount=int(4*SyncMaster::GetInstance()->GetTickSampleCount()) ;
				speed=(speed==0)?0:fabs(target-start)*KRATE_SAMPLE_COUNT/float(speed)/sampleCount ;
 				rp->fbMixRamp_.SetData(target-baseMix,speed,start-baseMix) ;
				if (!rp->fbMixRamp_.Enabled()) {
					rp->fbMixRamp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->fbMixRamp_) ;
				}
			}
			break ;

		// Feedback tune

		case I_CMD_FBTN:
			{
				float target=float(value&0xFF)/255.0f ;
				float speed=float(value>>8) ;
                float start=fp2fl(rp->fbTun_) ;
				float baseTune=fp2fl(rp->baseFbTun_) ;                
				int sampleCount=int(4*SyncMaster::GetInstance()->GetTickSampleCount()) ;
				speed=(speed==0)?0:fabs(target-start)*KRATE_SAMPLE_COUNT/float(speed)/sampleCount ;
 				rp->fbTunRamp_.SetData(target-baseTune,speed,start-baseTune) ;
				if (!rp->fbTunRamp_.Enabled()) {
					rp->fbTunRamp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->fbTunRamp_) ;
				}
			}
			break ;

		case I_CMD_PTCH:
			{
				int pitch=(char)(value&0xFF) ; // number of semi tones
				float speed=float(value>>8) ;   // get speed parameter
				if (pitch>127) pitch=pitch-256 ;

			 // Target speed for the ramp
			 
				float targetSpeed=float(pow(2.0,(pitch)/12.0)) ;
				float srcSpeed=fp2fl(rp->speed_)/fp2fl(rp->baseSpeed_) ;

			// speed of ramp
			
				speed=(speed==0)?0.0f:fp2fl(rp->speed_)*255.0f/speed/KRATE_SAMPLE_COUNT/32.0f ;

			 // Fill ramp data & enable
			 
        rp->speedRamp_.SetData(targetSpeed,speed,srcSpeed) ;
				if (!rp->speedRamp_.Enabled()) {
					rp->speedRamp_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->speedRamp_) ;
				}
			} ;
			break ;

		case I_CMD_LEGA:
			{
				int pitch=(char)(value&0xFF) ; // number of semi tones
				float speed=float(value>>8) ;   // get speed parameter

				if (pitch>127) pitch=pitch-256 ;
				
			 // Target speed for the ramp, taken from channel' last note
			 // if no pitch is given
			 
        float targetSpeed,initSpeed ;
        if (pitch==0) 
        {
          pitch=lastMidiNote_[channel]-rp->midiNote_ ;
          targetSpeed=1.0 ;
          initSpeed=float(pow(2.0f,pitch/12.0f)) ;
        } 
        else
        {
          initSpeed=fp2fl(rp->speed_)/fp2fl(rp->baseSpeed_) ;
          targetSpeed=float(pow(2.0f,pitch/12.0f)) ;                    
        }
                
			// speed of ramp
			
				speed=(speed==0)?0.0f:float(1+50.0/KRATE_SAMPLE_COUNT/speed) ;

			 // Fill ramp data & enable
			 
        rp->legato_.SetData(targetSpeed,speed,initSpeed) ;
				if (!rp->legato_.Enabled()) {
					rp->legato_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->legato_) ;
				}
			} ;
			break ;
			
		case I_CMD_PFIN:
			{

				float semi=(value&0xFF)/float(0x80) ; // number of semi tones
				if (semi>1) semi=semi-2 ;

				float speed=float(value>>8) ;   // get speed parameter
				
				float initSpeed=rp->pfin_.Enabled()?rp->pfin_.GetCurrent():1 ;
        float targetSpeed=float(pow(2.0f,semi/12.0f)) ;                    
                
			// speed of ramp
			
				speed=(speed==0)?0.0f:float(1+50.0/KRATE_SAMPLE_COUNT/speed) ;

			 // Fill ramp data & enable
			 
        rp->pfin_.SetData(targetSpeed,speed,initSpeed) ;

				if (!rp->pfin_.Enabled()) {
					rp->pfin_.Enable() ;
					rp->activeUpdaters_.push_back(&rp->pfin_) ;
				}
			} ;
			break ;
			
		case I_CMD_RTRG:
            {
				unsigned char loop=(value&0xFF) ; // number of ticks before repeat
				unsigned char offset=(value>>8) ; // number of ticks to offset at each repeat
                if (loop!=0) {
                    rp->retrig_=true ;
                    rp->retrigLoop_=loop ;
                    rp->retrigCount_=loop ;
                    rp->retrigOffset_=offset ;
					rp->couldClick_=SHOULD_KILL_CLICKS ;
                } else {
                    rp->retrig_=false ;
                }
            }
            break ;
		case I_CMD_FLTR:
    		{
    			float cut=(value>>8)/255.0f;	// cutoff frequency (FF=all pass, 0=none pass)
    			float res=(value&0xFF)/255.0f;	// resonance, aka Q (0=none) so default is FF00
				rp->cutoff_=rp->baseFCut_=fl2fp(cut) ;
				rp->reso_=rp->baseFRes_=fl2fp(res) ;
				if (rp->cutRamp_.Enabled()) {
					rp->cutRamp_.Disable() ;
					std::vector<I_SRPUpdater *>::iterator it=rp->activeUpdaters_.begin() ;
					while (it!=rp->activeUpdaters_.end()) {
						if (*it==&rp->cutRamp_) {
							it=rp->activeUpdaters_.erase(it) ;
							break ;
						}
						it++ ;
					}
				}
				if (rp->resRamp_.Enabled()) {
					rp->resRamp_.Disable() ;
					std::vector<I_SRPUpdater *>::iterator it=rp->activeUpdaters_.begin() ;
					while (it!=rp->activeUpdaters_.end()) {
						if (*it==&rp->resRamp_) {
							it=rp->activeUpdaters_.erase(it) ;
							break ;
						}
						it++ ;
					}
				}

    		}
			break ;
		case I_CMD_CRSH:
			{
    			unsigned char drive=(value>>8);
    			unsigned char crush=(value&0x0F);
				if (drive >0 ) rp->drive_=drive ;
				if (crush >0 ) rp->crush_=crush ;
			}
		default:
			break;
	} ;
} ;

/*
 * Return whole file name
 * Intended for file operations where a complete name is necessary
 */
const char *SampleInstrument::GetFileName() {
    Variable *v = FindVariable(SIP_SAMPLE);
    const char *src = v->GetString();
    return src;
}

/*
* Return cropped name
* Intended for printing where the whole name doesn't fit on screen
*/
const char *SampleInstrument::GetName() {
    Variable *v = FindVariable(SIP_SAMPLE);
    const char *src = v->GetString();

    // Shorten names if they're too long to display
    if (strlen(src) > Variable::MAX_NAME_LENGTH) {
	    static char shortName[Variable::MAX_NAME_LENGTH + 1];
        strncpy(shortName, src, Variable::MAX_NAME_LENGTH);
        shortName[Variable::MAX_NAME_LENGTH] = '\0';
        return shortName;
    }

    return src;
}

void SampleInstrument::Purge() {
	IteratorPtr<Variable> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &v=it->CurrentItem() ;
		v.Reset() ;
	}
  source_ = NULL;
/*    Variable *v=FindVariable(SIP_SAMPLE) ;
	if (v->GetInt()!=-1) {
	    v->SetInt(-1) ;
	}*/
} ;

bool SampleInstrument::IsEmpty() {
    Variable *v=FindVariable(SIP_SAMPLE) ;
	return (v->GetInt()==-1) ;
} ;

int SampleInstrument::GetTable() {
	int result=table_->GetInt() ;
	if (result>TABLE_COUNT) {
		return VAR_OFF ;
	}
	return result ;
} ;

bool SampleInstrument::GetTableAutomation() {
	return tableAuto_->GetBool() ;
} ;

void SampleInstrument::GetTableState(TableSaveState &state) {
	memcpy(state.hopCount_,tableState_.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(state.position_,tableState_.position_,sizeof(int)*3) ;
} ;

void SampleInstrument::SetTableState(TableSaveState &state) {
	memcpy(tableState_.hopCount_,state.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(tableState_.position_,state.position_,sizeof(int)*3) ;
} ;

bool SampleInstrument::IsMulti() {
	return source_->IsMulti() ;
}

void SampleInstrument::EnableDownsamplingLegacy()
{
  useDirtyDownsampling_ = true;
  Trace::Log("CONFIG","Enabling downsampling legacy");
}


/************************************************************
 trk2als: zonas (varios samples por teclas)
 ************************************************************/

const ZoneSource::Zone *ZoneSource::Find(int note) {
	if (zones_.empty()) return 0 ;
	if (note<0) note=60 ;
	const Zone *best=0 ;
	int bestD=1000 ;
	for (size_t i=0;i<zones_.size();i++) {
		const Zone &z=zones_[i] ;
		int d=(note<z.lo)?z.lo-note:(note>z.hi)?note-z.hi:0 ;
		if (d<bestD) { bestD=d ; best=&z ; }
		if (d==0) break ;
	}
	return best ;
}

// Lee "s zones" y resuelve cada nombre en el pool. Las zonas valen mientras
// el sample del instrumento sea el de la primera zona (si se elige otro
// sample a mano, vuelve a ser un instrumento de un sample)
void SampleInstrument::rebuildZones() {
	SamplePool *pool=SamplePool::GetInstance() ;
	zonesPoolSize_=pool->GetNameListSize() ;
	const char *str=FindVariable(SIP_S_ZONES)->GetString() ;
	zonesStr_=str?str:"" ;
	std::vector<ZoneSource::Zone> zones ;
	std::string all=str?str:"" ;
	size_t pos=0 ;
	while (pos<all.size()) {
		size_t end=all.find(';',pos) ;
		if (end==std::string::npos) end=all.size() ;
		std::string item=all.substr(pos,end-pos) ;
		pos=end+1 ;
		ZoneSource::Zone z ;
		char name[256] ;
		name[0]=0 ;
		if (sscanf(item.c_str(),"%d,%d,%d,%f,%d,%d,%255[^;]",&z.lo,&z.hi,&z.root,&z.cents,
		           &z.loopStart,&z.loopEnd,name)<7) continue ;
		z.name=name ;
		z.src=0 ;
		char **names=pool->GetNameList() ;
		for (int i=0;i<zonesPoolSize_;i++) {
			if (names[i] && z.name==names[i]) { z.src=pool->GetSource(i) ; break ; }
		}
		zones.push_back(z) ;
	}
	bool on=false ;
	int index=FindVariable(SIP_SAMPLE)->GetInt() ;
	if (!zones.empty() && index>=0 && index<zonesPoolSize_) {
		char **names=pool->GetNameList() ;
		on=(names[index] && zones[0].name==names[index]) ;
	}
	zones_.zones_.swap(zones) ;
	zoned_=on ;
	if (zoned_) source_=&zones_ ;
	else if (source_==&zones_) source_=(index>=0)?pool->GetSource(index):0 ;
}

void SampleInstrument::SetZones(const std::string &zones) {
	FindVariable(SIP_S_ZONES)->SetString(zones.c_str()) ;
	rebuildZones() ;
}

std::string SampleInstrument::ZoneSetName() {
	if (zones_.zones_.empty()) return "" ;
	std::string n=zones_.zones_[0].name ;
	size_t dot=n.rfind('.') ;
	if (dot!=std::string::npos) n=n.substr(0,dot) ;
	size_t us=n.rfind('_') ;
	if (us!=std::string::npos && us+1<n.size() && isdigit((unsigned char)n[us+1])) n=n.substr(0,us) ;
	return n ;
}

// trk2als: zonas al día si cambiaron (al abrir un proyecto llegan después del
// sample) o si cambió el pool (se cargaron samples después)
void SampleInstrument::ensureZones() {
	const char *zs=FindVariable(SIP_S_ZONES)->GetString() ;
	if (!zs) zs="" ;
	if ((*zs || !zonesStr_.empty()) &&
	    (zonesStr_!=zs || zonesPoolSize_!=SamplePool::GetInstance()->GetNameListSize())) rebuildZones() ;
}
