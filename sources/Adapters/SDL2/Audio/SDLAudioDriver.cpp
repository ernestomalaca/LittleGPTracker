#include "SDLAudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "Application/Model/Config.h"
#include <string.h>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

// trk2als: los hilos del audio en tiempo real (SCHED_FIFO), para que copiar
// samples a la tarjeta u otra carga del sistema no los deje sin turno (se
// oían huecos). Si el sistema no lo permite, sigue como antes.
// config.xml: <AUDIORT value="NO"/> lo apaga
static void audioRealtime(const char *who, int prio) {
#if defined(__linux__)
    const char *cfg = Config::GetInstance()->GetValue("AUDIORT");
    if (cfg && !strcmp(cfg, "NO")) return;
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    int r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (r == 0) Trace::Log("AUDIO", "%s en tiempo real (FIFO %d)", who, prio);
    else Trace::Log("AUDIO", "%s sin tiempo real (error %d)", who, r);
#endif
}

void sdl_callback(void *userdata, Uint8 *stream, int len) {
    static bool rt = false;
    if (!rt) { rt = true; audioRealtime("la salida de audio", 70); }
    SDLAudioDriver *sound = (SDLAudioDriver *)userdata;
    sound->OnChunkDone(stream, len);
};

SDLAudioDriverThread::SDLAudioDriverThread(SDLAudioDriver *driver) {
    // Uncap the semaphore to prevent dropped requests during long OS stalls.
    semaphore_ = SysSemaphore::Create(0, 1024);
    driver_ = driver;
};

bool SDLAudioDriverThread::Execute() {
    audioRealtime("el mezclador", 60);
    while (!shouldTerminate()) {
        semaphore_->Wait();
        driver_->OnNewBufferNeeded();
    };
    SysSemaphore *semaphore = semaphore_;
    semaphore_ = 0;
    delete semaphore;
    return true;
};

void SDLAudioDriverThread::Notify() {
    if (semaphore_) {
        semaphore_->Post();
    }
};

void SDLAudioDriverThread::RequestTermination() {
    SysThread::RequestTermination();
    // post to be sure we're not locked
    semaphore_->Post();
    // Wait for thread to finish
    SDL_Delay(10);
}

//-------------------------------------------------------------------------------------------------

SDLAudioDriver::SDLAudioDriver(AudioSettings &settings)
    : AudioDriver(settings), unalignedMain_(0), miniBlank_(0) {
    isPlaying_ = false;
    thread_ = 0;
}

SDLAudioDriver::~SDLAudioDriver() {}

struct SDL_AudioSpec input;
struct SDL_AudioSpec returned;

bool SDLAudioDriver::InitDriver() {

    // set sound
    input.freq = settings_.sampleRate_;
    input.format = AUDIO_S16SYS;
    input.channels = 2;
    input.callback = sdl_callback;
    input.samples = settings_.bufferSize_;
    input.userdata = this;

    SDL_SetHint("SDL_APP_NAME", "LittleGPTracker");
    SDL_SetHint("SDL_AUDIO_DEVICE_APP_NAME", "LittleGPTracker");

    // On my machine this wasn't working.
    // SDL_AudioDeviceID deviceId =
    // SDL_OpenAudioDevice(NULL,0,&input,&returned,0); The above may return 0
    // meaning an error or success.
    int ret = SDL_OpenAudio(&input, &returned);
    if (ret != 0) {
        Trace::Error("AUDIO", "Couldn't open sdl audio: %s", SDL_GetError());
        return false;
    }
    const char *driverName = SDL_GetCurrentAudioDriver();

    fragSize_ = returned.size;
    sampleRate_ = returned.freq;
    // Allocates a rotating sound buffer
    unalignedMain_ = (char *)SYS_MALLOC(fragSize_ + SOUND_BUFFER_MAX);
    // Make sure the buffer is aligned
#ifdef _64BIT
    mainBuffer_ = (char *)unalignedMain_;
#else
    mainBuffer_ = (char *)((((int)unalignedMain_) + 1) & (0xFFFFFFFC));
#endif

    Trace::Log("AUDIO", "%s successfully opened with %d samples %d", driverName,
               fragSize_ / 4, returned.freq);

    // Create mini blank buffer in case of underruns

    miniBlank_ = (char *)malloc(fragSize_);
    SYS_MEMSET(miniBlank_, 0, fragSize_);

    return true;
};

void SDLAudioDriver::CloseDriver() {

    if (miniBlank_) {
        SYS_FREE(miniBlank_);
        miniBlank_ = 0;
    }

    if (unalignedMain_) {
        SYS_FREE(unalignedMain_);
        unalignedMain_ = 0;
    };
    SDL_CloseAudio();
};

bool SDLAudioDriver::StartDriver() {

    thread_ = new SDLAudioDriverThread(this);
    thread_->Start();

    short blank[4000];
    SYS_MEMSET(blank, 0, 4000);
    bufferPos_ = 0;
    bufferSize_ = 0;

    for (int i = 0; i < settings_.preBufferCount_; i++) {
        AddBuffer((short *)miniBlank_, fragSize_ / 4);
        MidiService::GetInstance()->AdvancePlayQueue();
    }
    if (settings_.preBufferCount_ == 0) {
        thread_->Notify();
    }

    SDL_PauseAudio(0);
    startTime_ = SDL_GetTicks();

    return 1;
};

void SDLAudioDriver::StopDriver() {
    if (thread_) {
        thread_->RequestTermination();
        SysThread *thread = thread_;
        thread_ = 0;
        SDL_PauseAudio(1);
        delete thread;
    };
};

double SDLAudioDriver::GetStreamTime() {
    return (SDL_GetTicks() - startTime_) / 1000.0;
}

void SDLAudioDriver::OnChunkDone(Uint8 *stream, int len) {

    // Look if we have enough data in main buffer

    while (bufferSize_ - bufferPos_ < len) {

        // First move remaining bytes at the front
        memmove(mainBuffer_, mainBuffer_ + bufferPos_,
                bufferSize_ - bufferPos_);

        // then get next queued buffer and copy data from it

        if (pool_[poolPlayPosition_].buffer_ == 0) {
            if (isPlaying_) underruns_++;   // trk2als: se anota (Link lo compensa)
            // trk2als: la consola se atrasó y no hay audio listo: suena un
            // poco de silencio, pero NO se avanza ni se pide un bloque de más
            // (antes sí, y cada tropiezo dejaba el audio un pedazo más atrasado
            // para siempre, y luego desbordaba la fila). El bloque que falta
            // sigue en camino y entra en cuanto esté
            SYS_MEMCPY(mainBuffer_ + bufferSize_ - bufferPos_, miniBlank_,
                       fragSize_);
            bufferSize_ = bufferSize_ - bufferPos_ + fragSize_;
            bufferPos_ = 0;
            onAudioBufferTick();
            MidiService::GetInstance()->Flush();
            continue;
        } else {

            memcpy(mainBuffer_ + bufferSize_ - bufferPos_,
                   pool_[poolPlayPosition_].buffer_,
                   pool_[poolPlayPosition_].size_);

            // Adapt buffer variables

            bufferSize_ =
                bufferSize_ - bufferPos_ + pool_[poolPlayPosition_].size_;
            bufferPos_ = 0;

            SYS_FREE(pool_[poolPlayPosition_].buffer_);

            pool_[poolPlayPosition_].buffer_ = 0;
        }
        // Tick the engine and flush MIDI for every logical audio block
        // processed. This ensures consistent timing for application logic and
        // MIDI output, even during underruns where silence is played.
        onAudioBufferTick();
        MidiService::GetInstance()->Flush();

        // Advance poolPlayPosition_ and notify the thread after consuming
        // a logical chunk, regardless of whether it was real data or silence.
        poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
        if (thread_) {
            thread_->Notify();
        }
    }
    // Now dump audio to the device

    SYS_MEMCPY(stream, (short *)(mainBuffer_ + bufferPos_), len);
    bufferPos_ += len;
}

int SDLAudioDriver::GetPlayedBufferPercentage() {
    //	return
    // 100-(bufferSize_-bufferPos_-fragSize_)*100/(bufferSize_-fragSize_) ;
    return 0;
};

int SDLAudioDriver::GetSampleRate() {
    if (sampleRate_ == 0) {
        Trace::Error(
            "AUDIO",
            "Sample rate requested before audio driver is initialised!");
    }
    return sampleRate_;
};
