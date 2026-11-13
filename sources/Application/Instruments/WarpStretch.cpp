#include "WarpStretch.h"
#include <stddef.h>

#if __cplusplus >= 201103L
#include "signalsmith-stretch/signalsmith-stretch.h"
typedef signalsmith::stretch::SignalsmithStretch<float> Stretch ;
#define IMPL ((Stretch *)impl_)

WarpStretch::WarpStretch():impl_(new Stretch()),channels_(0) {}
WarpStretch::~WarpStretch() { delete IMPL ; }
bool WarpStretch::Available() { return true ; }

void WarpStretch::Configure(int channels,int sampleRate) {
	IMPL->presetDefault(channels,(float)sampleRate) ;
	channels_=channels ;
}
void WarpStretch::Reset() { IMPL->reset() ; }
void WarpStretch::SetTranspose(float factor) { IMPL->setTransposeFactor(factor) ; }
int WarpStretch::InputLatency() { return IMPL->inputLatency() ; }
int WarpStretch::OutputLatency() { return IMPL->outputLatency() ; }
void WarpStretch::Seek(float **in,int inSamples,double rate) { IMPL->seek(in,inSamples,rate) ; }
void WarpStretch::Process(float **in,int inSamples,float **out,int outSamples) {
	IMPL->process(in,inSamples,out,outSamples) ;
}

#else   // compilador viejo: sin complex (se toca como re-pitch)

WarpStretch::WarpStretch():impl_(NULL),channels_(0) {}
WarpStretch::~WarpStretch() {}
bool WarpStretch::Available() { return false ; }
void WarpStretch::Configure(int channels,int) { channels_=channels ; }
void WarpStretch::Reset() {}
void WarpStretch::SetTranspose(float) {}
int WarpStretch::InputLatency() { return 0 ; }
int WarpStretch::OutputLatency() { return 0 ; }
void WarpStretch::Seek(float **,int,double) {}
void WarpStretch::Process(float **,int,float **,int) {}

#endif
