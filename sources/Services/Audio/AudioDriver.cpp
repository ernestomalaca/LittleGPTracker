
#include "AudioDriver.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"

// trk2als: la fila del driver (ver AudioDriver.h)
static AudioDriver *currentDriver_=0 ;
volatile int AudioDriver::underruns_=0 ;

AudioDriver::AudioDriver(AudioSettings &settings) {
  currentDriver_=this ;   // trk2als
	settings_=settings ;
}

AudioDriver::~AudioDriver() {
}

bool AudioDriver::Init() {

  // Clear all buffers
	
   for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
     pool_[i].buffer_=0 ;
     pool_[i].size_=0 ;
   } ;
   isPlaying_=false;	 

   return InitDriver() ;
}

void AudioDriver::Close() {
	CloseDriver() ;
};

bool AudioDriver::Start() {

    isPlaying_=true ; 

    for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
  	  SAFE_FREE(pool_[i].buffer_) ;
    } ;
	 
    poolQueuePosition_=0 ;
    poolPlayPosition_=0 ;
	hasData_=false ;

    return StartDriver() ;
};

void AudioDriver::Stop() {
     isPlaying_=false ;
	hasData_=false ;
     StopDriver() ;
}

void AudioDriver::AddBuffer(short *buffer,int samplecount) {
  
  int len=samplecount*2*sizeof(short) ;

  if (!isPlaying_) return ;

  if (len>SOUND_BUFFER_MAX) {
      Trace::Error("Alert: buffer size exceeded") ;
  }

  if (pool_[poolQueuePosition_].buffer_!=0) {
  NInvalid ;
  Trace::Error("Audio overrun, please report") ;
  SAFE_FREE(pool_[poolQueuePosition_].buffer_) ;
  return ;
  }	

  pool_[poolQueuePosition_].buffer_=(char*) ((short *)SYS_MALLOC(len)) ;

  SYS_MEMCPY(pool_[poolQueuePosition_].buffer_,(char *)buffer,len) ;
  pool_[poolQueuePosition_].size_=len ;
  poolQueuePosition_=(poolQueuePosition_+1)%SOUND_BUFFER_COUNT ;
	hasData_=true ;
}


AudioDriver *AudioDriver::Current() { return currentDriver_ ; }

int AudioDriver::QueuedFrames() {
	int bytes=0 ;
	int p=poolPlayPosition_ ;
	for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
		char *b=pool_[p].buffer_ ;
		if (!b) break ;
		bytes+=pool_[p].size_ ;
		p=(p+1)%SOUND_BUFFER_COUNT ;
	}
	int rest=bufferSize_-bufferPos_ ;
	if (rest>0) bytes+=rest ;
	return bytes/4 ;
}

void AudioDriver::OnNewBufferNeeded() {
  SetChanged() ;
  Event event(Event::ADET_BUFFERNEEDED);
  NotifyObservers(&event) ;
} ;

void AudioDriver::onAudioBufferTick()
{
  SetChanged() ;
  Event event(Event::ADET_DRIVERTICK);
  NotifyObservers(&event) ;
}

bool AudioDriver::hasData() {
	return hasData_ ;
}  ;

AudioSettings AudioDriver::GetAudioSettings() {
	return settings_ ;
} ;
