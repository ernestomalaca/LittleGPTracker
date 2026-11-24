
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
#include <stdio.h>
#undef fopen   // (el stub de TinyXML los redefine)
#undef fclose
#undef FILE
#undef fwrite

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
// tiempo de CPU de este hilo (para medir sin el ruido de otros procesos)
static double cpuSeconds() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
#else
    return fineSeconds();
#endif
}
extern int g_warpRealtimeBlocks, g_warpCacheBlocks;
static double g_busy = 0, g_busyCpu = 0, g_span = 0;

void GetAudioLoad(int &current, int &peak) {
    current = g_loadNow;
    peak = g_loadPeak;
}

void AudioOutDriver::Trigger() {

    double before = fineSeconds();
    double beforeCpu = cpuSeconds();

    prepareMixBuffers();
    hasSound_=AudioMixer::Render(primarySoundBuffer_,sampleCount_) ;
    clipToMix();

    // trk2als: carga = tiempo de cálculo / duración del bloque
    double now = fineSeconds();
    if (sampleCount_ > 0) {
        int load = int((now - before) * 44100.0 / sampleCount_ * 100.0 + 0.5);
        g_busy += now - before;
        g_busyCpu += cpuSeconds() - beforeCpu;
        g_span += sampleCount_ / 44100.0;
        g_loadNow = load;
        // trk2als: diagnóstico (TRK_LOADLOG): cada bloque que llegó tarde
        static int dbgLate = -1;
        if (dbgLate < 0) dbgLate = getenv("TRK_LOADLOG") ? 1 : 0;
        if (dbgLate && load > 100)
            fprintf(stderr, "[LOAD] %lu bloque tarde: %d%%\n", (unsigned long)System::GetInstance()->GetClock(), load);
        if (load > g_loadWindow) g_loadWindow = load;
        if (now - g_loadSince >= 1.0) {      // el pico del último segundo
            g_loadPeak = g_loadWindow;
            g_loadWindow = 0;
            g_loadSince = now;
            static int logIt = -1;          // TRK_LOADLOG=1: una línea por segundo
            if (logIt < 0) logIt = getenv("TRK_LOADLOG") ? 1 : 0;
            if (logIt) Trace::Log("AUDIO", "carga pico %d%% media %.1f%% cpu %.1f%% warp rt %d pre %d", g_loadPeak,
                                  g_span > 0 ? g_busy / g_span * 100 : 0, g_span > 0 ? g_busyCpu / g_span * 100 : 0,
                                  g_warpRealtimeBlocks, g_warpCacheBlocks);
            g_warpRealtimeBlocks = g_warpCacheBlocks = 0;
            g_busy = g_busyCpu = g_span = 0;
        }
    }
    diagRecord();
    driver_->AddBuffer(mixBuffer_,sampleCount_) ;
}

// trk2als: diagnóstico (DIAG=YES en config.xml): guarda en memoria los últimos
// 30 s de lo que sale y lo escribe en chancho6/diag.wav al salir con SALIR.
// También anota los bloques tarde y cuando la salida se quedó sin audio.
static short *g_diag = 0;
static long g_diagLen = 0, g_diagPos = 0, g_diagTotal = 0;
static unsigned long g_diagClock = 0;
void AudioOutDriver::diagRecord() {
    static int on = -1;
    if (on < 0) {
        on = getenv("TRK_DIAG") ? 1 : 0;
        if (on) {
            g_diagLen = 44100L * 30;
            g_diag = (short *)calloc(g_diagLen * 2, sizeof(short));
            if (!g_diag) on = 0;
        }
    }
    if (!on || sampleCount_ <= 0) return;
    static int lastUnder = 0;
    if (AudioDriver::underruns_ != lastUnder) {
        fprintf(stderr, "[DIAG] %lu sin audio (underrun %d)\n", (unsigned long)System::GetInstance()->GetClock(), AudioDriver::underruns_);
        lastUnder = AudioDriver::underruns_;
    }
    for (int i = 0; i < sampleCount_; i++) {
        g_diag[2 * g_diagPos] = mixBuffer_[2 * i];
        g_diag[2 * g_diagPos + 1] = mixBuffer_[2 * i + 1];
        if (++g_diagPos >= g_diagLen) g_diagPos = 0;
    }
    g_diagTotal += sampleCount_;
    g_diagClock = System::GetInstance()->GetClock();
}

void AudioOutDriver::DiagDump(const char *path) {
    if (!g_diag || g_diagTotal == 0) return;
    long n = g_diagTotal < g_diagLen ? g_diagTotal : g_diagLen;
    long start = g_diagTotal < g_diagLen ? 0 : g_diagPos;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    unsigned int dataBytes = n * 4, v;
    fwrite("RIFF", 1, 4, f); v = 36 + dataBytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    unsigned short fmt[2] = {1, 2}; fwrite(fmt, 2, 2, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    unsigned short ba[2] = {4, 16}; fwrite(ba, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (long i = 0; i < n; i++) fwrite(&g_diag[2 * ((start + i) % g_diagLen)], 2, 2, f);
    fclose(f);
    fprintf(stderr, "[DIAG] diag.wav: %ld muestras, la última generada en el reloj %lu\n", n, g_diagClock);
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
