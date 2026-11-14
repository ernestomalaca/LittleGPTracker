
#include "AudioOutDriver.h"
#include <stdlib.h>
#include "System/Console/Trace.h"
#include "Application/Model/Project.h"
#include "Application/Player/SyncMaster.h" // Should be installable
#include "AudioDriver.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>

AudioOutDriver::AudioOutDriver(AudioDriver &driver) {
    driver_ = &driver;
    driver.AddObserver(*this) ;
    primarySoundBuffer_=0 ;
    mixBuffer_ = 0;
    SetOwnership(false);
}

AudioOutDriver::~AudioOutDriver() {
    driver_->RemoveObserver(*this);
    delete driver_ ;
};

bool AudioOutDriver::Init() {
	primarySoundBuffer_=(fixed *)SYS_MALLOC(MIX_BUFFER_SIZE*sizeof(fixed)/2) ;
	mixBuffer_=(short *)SYS_MALLOC(MIX_BUFFER_SIZE) ;
    return driver_->Init();
} ;

void AudioOutDriver::Close() {
    driver_->Close();
    SAFE_FREE(primarySoundBuffer_) ;
	SAFE_FREE(mixBuffer_) ;     
}

bool AudioOutDriver::Start() {
    sampleCount_=0 ;
	return driver_->Start() ;
}

void AudioOutDriver::Stop() {
	driver_->Stop() ;
}

static volatile int g_loadNow = 0, g_loadPeak = 0, g_loadWindow = 0;
static double g_loadSince = 0;

// reloj fino (el de TimeService va en milisegundos)
#if defined(__APPLE__) || defined(__linux__)
#include <time.h>
static double fineSeconds() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
#else
static double fineSeconds() { return TimeService::GetInstance()->GetTime(); }
#endif

void GetAudioLoad(int &current, int &peak) {
    current = g_loadNow;
    peak = g_loadPeak;
}

void AudioOutDriver::Trigger() {

    double before = fineSeconds();

    prepareMixBuffers();
    hasSound_=AudioMixer::Render(primarySoundBuffer_,sampleCount_) ;
    clipToMix();

    // trk2als: carga = tiempo de cálculo / duración del bloque
    double now = fineSeconds();
    if (sampleCount_ > 0) {
        int load = int((now - before) * 44100.0 / sampleCount_ * 100.0 + 0.5);
        g_loadNow = load;
        if (load > g_loadWindow) g_loadWindow = load;
        if (now - g_loadSince >= 1.0) {      // el pico del último segundo
            g_loadPeak = g_loadWindow;
            g_loadWindow = 0;
            g_loadSince = now;
            static int logIt = -1;          // TRK_LOADLOG=1: una línea por segundo
            if (logIt < 0) logIt = getenv("TRK_LOADLOG") ? 1 : 0;
            if (logIt) Trace::Log("AUDIO", "carga pico %d%%", g_loadPeak);
        }
    }
    driver_->AddBuffer(mixBuffer_,sampleCount_) ;
}

void AudioOutDriver::Update(Observable &o,I_ObservableData *d) 
{
    SetChanged();
    NotifyObservers(d) ;
}

void AudioOutDriver::prepareMixBuffers() {
    sampleCount_ = getPlaySampleCount();
} ;

void AudioOutDriver::SetMasterVolume(int volume) {
    AudioMixer::SetMasterVolume(volume);
}

void AudioOutDriver::clipToMix() {

    bool interlaced = driver_->Interlaced();

    if (!hasSound_) {
        SYS_MEMSET(mixBuffer_, 0, sampleCount_ * 2 * sizeof(short));
    } else {
        short *s1 = mixBuffer_;
		short *s2 = (interlaced) ? s1 + 1 : s1 + sampleCount_;
		int offset = (interlaced) ? 2 : 1;

        fixed *p = primarySoundBuffer_;

        for (int i = 0; i < sampleCount_; i++) {

            fixed leftSample = *p++;
            fixed rightSample = *p++;

            *s1 = short(fp2i(leftSample));
            s1 += offset;
			*s2 = short(fp2i(rightSample));
			s2 += offset;
        };
    }
} ;

int AudioOutDriver::GetPlayedBufferPercentage() {
	return driver_->GetPlayedBufferPercentage() ;
} ;

AudioDriver *AudioOutDriver::GetDriver() { return driver_; };

std::string AudioOutDriver::GetAudioAPI() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.audioAPI_ ;
} ;

std::string AudioOutDriver::GetAudioDevice() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.audioDevice_ ;
} ;

int AudioOutDriver::GetAudioBufferSize() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.bufferSize_ ;
} ;

int AudioOutDriver::GetAudioRequestedBufferSize() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.bufferSize_ ;
}

int AudioOutDriver::GetAudioPreBufferCount() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.preBufferCount_ ;
} ;
double AudioOutDriver::GetStreamTime() { return driver_->GetStreamTime(); };
