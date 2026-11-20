#include "AudioWatch.h"
#include "MixerService.h"
#include "Application/Audio/DummyAudioOut.h"
#include "Application/Model/Config.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Project.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"

MixerService::MixerService() : out_(0), sync_(0), depth_(0), owner_(0), mixed_(0), maxWait_(0), maxWork_(0), isRendering_(false) {
    mode_ = MSRM_PLAYBACK;
};

MixerService::~MixerService(){};

/*
 * initializes the mixer service, config changes depending if we're in sequencer or render mode
 */
bool MixerService::Init() {
    // create the output depending on rendering mode
    out_ = 0;
	switch (mode_) {
    case MSRM_STEREO:
    case MSRM_STEMS:
        out_ = new DummyAudioOut();
        break;
    default:
        Audio *audio = Audio::GetInstance();
        out_ = audio->GetFirst();
        break;
	}

	for (int i=0;i<MAX_BUS_COUNT;i++) {
		master_.Insert(bus_[i]);
	}

	bool result = false;
	if (out_) {
		result = out_->Init();
		if (result) {
			out_->Insert(master_);
		}

        initRendering(mode_);
        out_->AddObserver(*MidiService::GetInstance());
	}

	sync_=SDL_CreateMutex();
	NAssert(sync_);

	if (result) {
		Trace::Log("MixerService", "output initialized");
	} else {
		Trace::Log("MixerService", "failed to initialize output");
	}
	return (result);
};

void MixerService::initRendering(MixerServiceRenderMode mode) {
    switch(mode) {
    case MSRM_PLAYBACK:
        break;
    case MSRM_STEREO:
        out_->SetFileRenderer("project:mixdown.wav");
        break;
    case MSRM_STEMS:
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            char buffer[1024];
            sprintf(buffer, "project:channel%d.wav", i);
            bus_[i].SetFileRenderer(buffer);
        }
        break;
    }
}

void MixerService::Close() {
	if (out_) {
    out_->RemoveObserver(*MidiService::GetInstance());
		out_->Close() ;
		out_->Empty() ;
		master_.Empty() ;

		switch(mode_) {
        case MSRM_STEMS:
        case MSRM_STEREO:
            break;
        default:
            break;
        }
    }
   for (int i=0;i<MAX_BUS_COUNT;i++) {
	   bus_[i].Empty() ;
   }
	out_=0 ;
	SDL_DestroyMutex(sync_) ;
	sync_=0 ;
} ;

void MixerService::SetRenderMode(int mode) {
    mode_ = MixerServiceRenderMode(mode);
}

bool MixerService::IsRendering() { return isRendering_; }

bool MixerService::Start() {
    MidiService::GetInstance()->Start();
    if (out_) {
        out_->AddObserver(*this);
        out_->Start();
     }
	return true ;
} ;

void MixerService::Stop() {
	MidiService::GetInstance()->Stop() ;
     if (out_) {
      out_->Stop() ;
      out_->RemoveObserver(*this) ;
     }
}

MixBus *MixerService::GetMixBus(int i) { return &(bus_[i]); }

unsigned int MixerService::GetMasterPeakLevel() const {
    return master_.GetPeakLevel();
}

void MixerService::Update(Observable &o, I_ObservableData *d) {
    AudioDriver::Event *event = (AudioDriver::Event *)d;
    if (event->type_ == AudioDriver::Event::ADET_BUFFERNEEDED) {
        Uint32 a = SDL_GetTicks();
        AudioWatchWaitBegin();
        Lock();
        AudioWatchWaitEnd();
        Uint32 b = SDL_GetTicks();
        mixed_++;
        SetChanged();
        NotifyObservers();

        out_->Trigger();
        Unlock();
        AudioWatchMixDone();
        Uint32 c = SDL_GetTicks();
        // trk2als: para el log: lo más que esperó el candado y lo más que tardó en mezclar
        if (b - a > maxWait_) maxWait_ = b - a;
        if (c - b > maxWork_) maxWork_ = c - b;
    }
}

bool MixerService::Clipped() {
     return out_->Clipped() ;
} ;

void MixerService::SetPregain(int vol) {
    Mixer *mixer = Mixer::GetInstance();

    fixed masterVolume = fp_mul(i2fp(vol), fl2fp(0.01f));

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        bus_[i].SetVolume(masterVolume);
  }
};

void MixerService::SetSoftclip(int clip, int gain) {
    out_->SetSoftclip(clip, gain);
}

void MixerService::SetMasterVolume(int attn) { out_->SetMasterVolume(attn); }

int MixerService::GetPlayedBufferPercentage() {
	return out_->GetPlayedBufferPercentage() ;
}

void MixerService::toggleRendering(bool enable) {
    isRendering_ = enable;
    switch (mode_) {
    case MSRM_PLAYBACK:
        initRendering(MSRM_PLAYBACK);
        break;
    case MSRM_STEREO:
        initRendering(MSRM_STEREO);
        out_->EnableRendering(enable);
        break;
    case MSRM_STEMS:
        initRendering(MSRM_STEMS);
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            bus_[i].EnableRendering(enable);
        };
        break;
    }
}

void MixerService::OnPlayerStart() {
	toggleRendering(true) ;
} ;

void MixerService::OnPlayerStop() {
	toggleRendering(false) ;
} ;

void MixerService::Execute(FourCC id,float value) {
     if (value>0.5) {
        Audio *audio=Audio::GetInstance() ;
        int volume=audio->GetMixerVolume() ;
        switch(id) {
           case TRIG_VOLUME_INCREASE:
                if (volume<100) volume+=1 ;
                break ;
           case TRIG_VOLUME_DECREASE:
                if (volume>0) volume-=1 ;
                break ;                       
        } ;
        audio->SetMixerVolume(volume) ;
     } ;
}

AudioOut *MixerService::GetAudioOut() {
	return out_ ;
} ;


void MixerService::Lock() {
	if (!sync_) return ;
	SDL_LockMutex(sync_) ;
	SDL_threadID me = SDL_ThreadID() ;
	if (depth_ > 0 && owner_ == me) depth_++ ;
	else { owner_ = me ; depth_ = 1 ; }
}

// trk2als: un Unlock sin su Lock (de este hilo) se ignora: antes descuadraba
// la cuenta y el candado parecía libre cuando la interfaz lo tenía
void MixerService::Unlock() {
	if (!sync_) return ;
	if (depth_ <= 0 || owner_ != SDL_ThreadID()) {
		static int told = 0 ;
		if (told++ < 3) Trace::Log("AUDIO", "candado: Unlock sin Lock (ignorado)") ;
		return ;
	}
	if (--depth_ == 0) owner_ = 0 ;
	SDL_UnlockMutex(sync_) ;
}

// trk2als: cada botón llega con el mezclador trabado (así venía de LGPT).
// Lo que tarda (leer los samples de un kit) suelta el candado mientras
// espera, para que el audio siga mezclando; si no, se queda sin turno.
int MixerService::ReleaseAll() {
	if (!sync_ || depth_ == 0 || owner_ != SDL_ThreadID()) return 0 ;
	int n = depth_ ;
	for (int i = 0; i < n; i++) Unlock() ;
	return n ;
}

void MixerService::Reacquire(int n) {
	for (int i = 0; i < n; i++) Lock() ;
}

// --- trk2als: AudioWatch ----------------------------------------------------
static volatile Uint32 awLastOut = 0, awLastMix = 0, awWaitSince = 0 ;
static volatile Uint32 awOutGap = 0, awMixGap = 0, awLockWait = 0 ;

void AudioWatchOut() {
	Uint32 now = SDL_GetTicks() ;
	if (awLastOut && now - awLastOut > awOutGap) awOutGap = now - awLastOut ;
	awLastOut = now ;
}
void AudioWatchWaitBegin() { awWaitSince = SDL_GetTicks() ; }
void AudioWatchWaitEnd() {
	Uint32 w = SDL_GetTicks() - awWaitSince ;
	if (w > awLockWait) awLockWait = w ;
	awWaitSince = 0 ;
}
void AudioWatchMixDone() {
	Uint32 now = SDL_GetTicks() ;
	if (awLastMix && now - awLastMix > awMixGap) awMixGap = now - awLastMix ;
	awLastMix = now ;
}
void AudioWatchReset() { awOutGap = awMixGap = awLockWait = 0 ; }
void AudioWatchTake(unsigned &outGap, unsigned &mixGap, unsigned &lockWait) {
	Uint32 now = SDL_GetTicks() ;
	// lo que va del hueco actual también cuenta
	Uint32 o = awOutGap, m = awMixGap, l = awLockWait ;
	if (awLastOut && now - awLastOut > o) o = now - awLastOut ;
	if (awLastMix && now - awLastMix > m) m = now - awLastMix ;
	Uint32 ws = awWaitSince ;
	if (ws && now - ws > l) l = now - ws ;
	outGap = o ; mixGap = m ; lockWait = l ;
}
