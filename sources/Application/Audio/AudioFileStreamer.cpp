#include "AudioFileStreamer.h"
#include "Application/Utils/fixed.h"
#include "System/Console/Trace.h"
#include "Application/Model/Config.h"
#include "Services/Audio/Audio.h"
#include <string.h>

AudioFileStreamer::AudioFileStreamer() {
	wav_=0 ;
	shift_=1 ;
	mode_=AFSM_STOPPED ;
	newPath_=false ;
	position_=0 ;
	frac_=0 ;
} ;

AudioFileStreamer::~AudioFileStreamer() {
	SAFE_DELETE(wav_) ;
} ;
 
bool AudioFileStreamer::Start(const Path &path) {
  Trace::Debug("Starting to stream %s",path.GetPath().c_str());
	path_=path ;
	const char *shift=Config::GetInstance()->GetValue("PRELISTENATTENUATION") ;
	shift_=(shift)?atoi(shift):1 ;
  Trace::Debug("Streaming shift is %d",shift_);
	newPath_=true ;
	mode_=AFSM_PLAYING ;
	return true ;
} ;

void AudioFileStreamer::Stop() {
	mode_=AFSM_STOPPED ;
  Trace::Debug("Streaming stopped");
} ;

bool AudioFileStreamer::Render(fixed *buffer,int samplecount) {

	// See if we're playing

	if (mode_==AFSM_STOPPED) {
		SAFE_DELETE(wav_) ;
		return false ;
	}

	// Do we need to get a new file ?

	if (newPath_) {
		SAFE_DELETE(wav_) ;
		newPath_=false ;
	}

	// new look if we need to load the file

	if (!wav_) 
  {
		wav_=WavFile::Open(path_.GetPath().c_str()) ;
		if (!wav_) 
    {
      Trace::Error("Failed to open streaming of %s",path_.GetPath().c_str());
			mode_=AFSM_STOPPED ;
			return false ;
		}
		position_=0 ;
		frac_=0 ;
	}

	// we are playing a valid file
	// trk2als: el archivo puede tener otra frecuencia (p. ej. 22050 Hz); se
	// avanza en proporción para que el preview suene a su altura real
	long size=wav_->GetSize(-1) ;
	int fileRate=wav_->GetSampleRate(-1) ;
	int outRate=Audio::GetInstance()->GetSampleRate() ;
	double step=(fileRate>0 && outRate>0)?double(fileRate)/double(outRate):1.0 ;

	int need=int(samplecount*step+frac_)+2 ;
	int count=need ;
	if (position_+count>size) {
		count=size-position_ ;
	}
	memset(buffer,0,2*samplecount*sizeof(fixed)) ;
	if (count<=0) {
		mode_=AFSM_STOPPED ;
		return true ;
	}
	wav_->GetBuffer(position_,count) ;
	fixed *dst=buffer ;
	short *src=(short *)wav_->GetSampleBuffer(-1) ;
	int channel=wav_->GetChannelCount(-1) ;

	int i=0 ;
	for (;i<samplecount;i++) {
		int idx=int(frac_+i*step) ;
		if (idx>=count) break ;
		short *s=src+idx*channel ;
		fixed v=*dst++=i2fp((*s)>>(1+shift_)) ;
		*dst++=(channel==2)?i2fp((s[1])>>(1+shift_)):v ;
	}
	double advance=frac_+samplecount*step ;
	int whole=int(advance) ;
	frac_=advance-whole ;
	position_+=whole ;
	if (i<samplecount || position_>=size) {
		Trace::Debug("Reached the end of %d samples",size);
		mode_=AFSM_STOPPED ;
	}
	return true ;
}
