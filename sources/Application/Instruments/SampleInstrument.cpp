#include "SampleInstrument.h"
#include "SamplePool.h"
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

fixed SampleInstrument::feedback_[SONG_CHANNEL_COUNT][FB_BUFFER_LENGTH*2] ;

bool SampleInstrument::useDirtyDownsampling_ = false;

#define SHOULD_KILL_CLICKS false

bool SampleInstrument::syncRequest_[SONG_CHANNEL_COUNT]= {false,false,false,false,false,false,false,false} ;

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

int SampleInstrument::lastMidiNote_[SONG_CHANNEL_COUNT]= {
	-1,-1,-1,-1,-1,-1,-1,-1
} ;

#define KRATE_SAMPLE_COUNT 100

SampleInstrument::SampleInstrument() {
	 for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {   // trk2als
		 voiceClock_[ch]=0 ;
		 stretch_[ch]=0 ;
		 lastWarpT_[ch]=warpTarget_[ch]=1.0f ;
		 renderParams_[ch].simpler_=false ;
		 renderParams_[ch].finished_=true ;
		 renderParams_[ch].envStage_=4 ;
		 renderParams_[ch].envLevel_=0 ;
		 renderParams_[ch].glideLeft_=0 ;
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
	 static const char *simplerModes[3]={"classic","one-shot","slice"} ;
	 static const char *offOn[2]={"off","on"} ;
	 static const char *trigModes[2]={"trigger","gate"} ;
	 static const char *filterTypes[3]={"LP","BP","HP"} ;
	 Insert(new Variable("s init",SIP_S_INIT,0)) ;
	 Insert(new Variable("s mode",SIP_S_MODE,simplerModes,3,SM_CLASSIC)) ;
	 Insert(new Variable("s start",SIP_S_START,0)) ;
	 Insert(new Variable("s end",SIP_S_END,100)) ;
	 Insert(new Variable("s loop",SIP_S_LOOP,offOn,2,0)) ;
	 Insert(new Variable("s loop len",SIP_S_LLEN,100)) ;
	 Insert(new Variable("s trigger",SIP_S_TRIG,trigModes,2,0)) ;
	 Insert(new Variable("s slices",SIP_S_SLICES,4)) ;
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

     for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
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
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) delete stretch_[ch] ; // trk2als
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

  if (source_==0) return false ;

  // Get Rendering params for current voice & fill init data

  renderParams *rp=renderParams_+channel ;

  rp->midiNote_=midinote ;
  
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
     rp->forceSync_=syncRequest_[channel] ; // trk2als: pista de audio
     // trk2als: en las pistas MIDI el instrumento es el Sampler estilo Simpler
     // (salvo instrumentos viejos en looper sync)
     bool wasSounding=rp->simpler_ && !rp->finished_ && rp->envStage_<4 &&
                     voiceClock_[channel]!=0 ;
     fixed prevSpeed=rp->speed_ ;
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
    if (rp->simpler_ && wasSounding && GetSimpler(SIP_S_GLIDE)>0) {
        rp->glideFrom_=prevSpeed ;
        rp->glideLen_=rp->glideLeft_=int(GetSimpler(SIP_S_GLIDE)*44.1f) ;
        rp->speed_=prevSpeed ;
    }
    // trk2als: Sampler con warp. La región dura lo mismo en tiempo musical
    // a cualquier tempo; re-pitch la acelera/frena (y no sigue las notas),
    // complex la estira y transpone por separado
    rp->sWarp_=SW_OFF ;
    rp->warpTranspose_=1.0f ;
    if (rp->simpler_ && GetSimpler(SIP_S_MODE)!=SM_SLICE && GetSimpler(SIP_S_WARP)!=SW_OFF) {
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
		for (int k=0;k<5;k++) rp->sLive_[k]=-9999 ;
		simplerBlock(rp,0) ;
	}
	snapshotLive(rp) ; // trk2als
	return true ;
}

void SampleInstrument::Stop(int channel) {

	// Get Rendering params for current voice & fill init data

	 renderParams *rp=renderParams_+channel ;
	 running_=false ;
	 // trk2als: el Sampler suelta la nota con su release (o la deja sonar
	 // completa en modo trigger)
	 if (rp->simpler_ && !rp->finished_) {
		 if (rp->gate_ && rp->envStage_<3) {
			 rp->envReleaseFrom_=rp->envLevel_ ;
			 rp->envStage_=3 ;
			 rp->envClock_=0 ;
		 }
		 return ;
	 }
	 voiceClock_[channel]=0 ;
}

bool SampleInstrument::Releasing(int channel) {
	renderParams *rp=renderParams_+channel ;
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

bool SampleInstrument::Render(int channel, fixed *buffer, int size, int flags) {

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
		fixed fixedpanl=panlaw[pan] ;
		fixed fixedpanr=panlaw[254-pan] ;

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
						fixed fixedpanl=panlaw[pan] ;
						fixed fixedpanr=panlaw[254-pan] ;

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
    rp->justWrapped_=(rp->position_<posBefore) ;

		// Update feedback position

		rp->feedbackIn_=(feedbackIn-feedbackStart)/2 ;
		rp->feedbackOut_=(feedbackPick-feedbackStart)/2 ;
		somethingToMix=true ;
		if (rp->simpler_) simplerEnvelope(rp,buffer,size) ; // trk2als
    }

    return somethingToMix;
};

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
	float semisFactor=rp->simpler_?rp->warpTranspose_:float(pow(2.0,transpose_->GetInt()/12.0)) ;
	ws->SetTranspose(rate/mixRate*semisFactor) ;
	double speed=fp2fl(rp->speed_) ;     // cuadros del sample por cuadro de salida
	if (speed<0.01) speed=0.01 ;

	if ((int)warpOut2_[0].size()<size) {
		warpOut2_[0].resize(size) ;
		warpOut2_[1].resize(size) ;
	}
	float *ins[2]={0,0}, *outs[2]={&warpOut2_[0][0],&warpOut2_[1][0]} ;

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

/************************************************************
 trk2als: Sampler estilo Simpler
 ************************************************************/

// La primera vez que el instrumento se usa como Sampler, sus parámetros se
// deducen de los de LGPT para que suene parecido a como sonaba
void SampleInstrument::InitSimpler() {
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
	int size=source_->GetSize(rp->midiNote_) ;
	int mode=GetSimpler(SIP_S_MODE) ;
	int a=int((long long)size*GetSimpler(SIP_S_START)/100) ;
	int b=int((long long)size*GetSimpler(SIP_S_END)/100) ;
	if (b>size) b=size ;
	if (b<=a) b=(a+1<size)?a+1:size ;
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
				if (ls>=b) ls=b-1 ;
				rp->rendLoopStart_=ls ;
				rp->simplerLoop_=SILM_LOOP ;
			} else {
				rp->rendLoopStart_=a ;
			}
			rp->rendLoopEnd_=b ;
			break ;
		}
		case SM_ONESHOT:
			offset+=note-60 ;
			first=a ;
			rp->rendLoopStart_=a ;
			rp->rendLoopEnd_=b ;
			rp->gate_=(GetSimpler(SIP_S_TRIG)==1) ;
			break ;
		case SM_SLICE: {
			int n=GetSimpler(SIP_S_SLICES) ;
			if (n<1) n=1 ;
			int idx=int(note)-36 ;     // C1 = rebanada 1, como Simpler
			if (idx<0 || idx>=n) return false ;
			int len=(b-a)/n ;
			if (len<1) return false ;
			rp->rendLoopStart_=a+idx*len ;
			rp->rendLoopEnd_=a+(idx+1)*len ;
			first=rp->rendLoopStart_ ;
			rp->gate_=(GetSimpler(SIP_S_TRIG)==1) ;
			break ;
		}
	}
	sliced=(mode==SM_SLICE) ;
	return true ;
}

float SampleInstrument::envMs(FourCC id) {
	int v=GetSimpler(id) ;
	return v<0?0:float(v) ;
}

// Una vez por bloque: parámetros en vivo, glide y la envolvente sobre el filtro
void SampleInstrument::simplerBlock(renderParams *rp,int size) {
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
	if ((v=GetSimpler(SIP_S_FTYPE))!=rp->sLive_[2]) {
		static const int mixes[3]={0x00,0x80,0xFF} ;
		rp->sLive_[2]=v ;
		rp->filterMix_=mixes[(v<0||v>2)?0:v] ;
	}
	if ((v=GetSimpler(SIP_S_FRES))!=rp->sLive_[4]) {
		rp->sLive_[4]=v ;
		rp->reso_=rp->baseFRes_=fl2fp(v/127.0f) ;
	}
	int env=GetSimpler(SIP_S_FENV) ;
	if ((v=GetSimpler(SIP_S_FFREQ))!=rp->sLive_[3] || env!=0) {
		rp->sLive_[3]=v ;
		float cut=v/127.0f+env/127.0f*rp->envLevel_ ;
		if (cut<0) cut=0 ;
		if (cut>1) cut=1 ;
		rp->cutoff_=rp->baseFCut_=fl2fp(cut) ;
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
	bool oneShot=(GetSimpler(SIP_S_MODE)==SM_ONESHOT) ;
	float atk=(oneShot?envMs(SIP_S_FIN):envMs(SIP_S_ATK))*msr ;
	float dec=oneShot?0:envMs(SIP_S_DEC)*msr ;
	float sus=1.0f ;
	if (!oneShot) {
		int db=GetSimpler(SIP_S_SUS) ;
		sus=(db<=-60)?0:powf(10.0f,db/20.0f) ;
	}
	float rel=(oneShot?envMs(SIP_S_FOUT):envMs(SIP_S_REL))*msr ;
	for (int i=0;i<size;i++) {
		float l=rp->envLevel_ ;
		switch (rp->envStage_) {
			case 0:   // ataque
				l=(atk<1)?1:float(rp->envClock_)/atk ;
				if (l>=1) { l=1 ; rp->envStage_=1 ; rp->envClock_=0 ; }
				break ;
			case 1:   // decay hacia el sustain
				l=(dec<1)?sus:1.0f-(1.0f-sus)*float(rp->envClock_)/dec ;
				if (l<=sus || dec<1) { l=sus ; rp->envStage_=2 ; rp->envClock_=0 ; }
				break ;
			case 2:
				l=sus ;
				break ;
			case 3:   // release
				l=(rel<1)?0:rp->envReleaseFrom_*(1.0f-float(rp->envClock_)/rel) ;
				if (l<=0) { l=0 ; rp->envStage_=4 ; }
				break ;
			default:
				l=0 ;
		}
		rp->envLevel_=l ;
		rp->envClock_++ ;
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
