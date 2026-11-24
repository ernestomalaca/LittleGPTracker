#ifndef _AUDIO_OUT_DRIVER_H_
#define _AUDIO_OUT_DRIVER_H_

#include "AudioOut.h"
#include "Foundation/Observable.h"
#include "Application/Instruments/WavFileWriter.h"

class AudioDriver ;

#define MIX_BUFFER_SIZE 40000
#define MAX_POSITIVE_FIXED i2fp(32767)
#define MAX_NEGATIVE_FIXED i2fp(-32768)

// trk2als: carga del audio en %: lo que tarda en calcularse un bloque contra
// lo que dura sonando. current = el último bloque; peak = el máximo del último
// segundo. Más de 100 = no alcanza (underrun)
void GetAudioLoad(int &current, int &peak) ;

class AudioOutDriver: public AudioOut,protected I_Observer {
  public:
	static void DiagDump(const char *path) ;
    AudioOutDriver(AudioDriver &) ;
    virtual ~AudioOutDriver() ;

    virtual bool Init() ;
    virtual void Close() ;
    virtual bool Start() ;
    virtual void Stop() ;

    virtual void Trigger();
    virtual void SetMasterVolume(int volume);

    virtual int GetPlayedBufferPercentage() ;

    AudioDriver *GetDriver() ;

    virtual std::string GetAudioAPI() ;
    virtual std::string GetAudioDevice() ;
    virtual int GetAudioBufferSize() ;
    virtual int GetAudioRequestedBufferSize() ;
    virtual int GetAudioPreBufferCount() ;
    virtual double GetStreamTime() ;

  protected:

    virtual void Update(Observable &o,I_ObservableData *d) ;

    void prepareMixBuffers();
    void clipToMix();

  private:
	void diagRecord() ;
    AudioDriver *driver_;
    bool hasSound_;

    fixed *primarySoundBuffer_ ;
    short *mixBuffer_ ;
    int sampleCount_ ;       
} ;
#endif
