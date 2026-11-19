#include "AudioOut.h"
#include "Application/Player/SyncMaster.h"

AudioOut::AudioOut():AudioMixer("AudioOut"),sampleOffset_(0) {
} ;

AudioOut::~AudioOut() {
} ;


// trk2als: cuántas muestras se han calculado desde que arrancó (el reloj de
// audio con el que Link sabe cuándo se va a oír cada rebanada)
double g_renderedSamples = 0 ;

int AudioOut::getPlaySampleCount() {
	sampleOffset_+=SyncMaster::GetInstance()->GetPlaySampleCount() ;
	int count=int(sampleOffset_) ;
	sampleOffset_-=count ;
	g_renderedSamples+=count ;
	return count ;
} ;