#include "AudioFileStreamer.h"
#include "Application/Utils/fixed.h"
#include "System/Console/Trace.h"
#include "Application/Model/Config.h"
#include "Services/Audio/Audio.h"
#include <string.h>

#define STREAM_FADE 256   // salir: ~6 ms; entrar: ~1.5 ms

AudioFileStreamer::AudioFileStreamer() {
	wav_=0 ;
	end_=-1 ;
	shift_=1 ;
	mode_=AFSM_STOPPED ;
	newPath_=false ;
	position_=0 ;
	frac_=0 ;
	speed_=1.0 ;
	startFrac_=0 ;
	delay_=0 ;
	loop_=false ;
	gain_=0 ;
	stopping_=false ;
} ;

AudioFileStreamer::~AudioFileStreamer() {
	SAFE_DELETE(wav_) ;
} ;
 
bool AudioFileStreamer::Start(const Path &path) {
	return Start(path,1.0,0.0,0.0,false) ;
} ;

bool AudioFileStreamer::Start(const Path &path, double speed, double delaySecs, double startFrac, bool loop) {
  Trace::Debug("Starting to stream %s",path.GetPath().c_str());
	path_=path ;
	const char *shift=Config::GetInstance()->GetValue("PRELISTENATTENUATION") ;
	shift_=(shift)?atoi(shift):1 ;
  Trace::Debug("Streaming shift is %d",shift_);
	speed_=(speed>0)?speed:1.0 ;
	startFrac_=(startFrac>=0 && startFrac<1)?startFrac:0.0 ;
	delay_=long(delaySecs*Audio::GetInstance()->GetSampleRate()) ;
	loop_=loop ;
	end_=-1 ;
	newPath_=true ;
	stopping_=false ;
	mode_=AFSM_PLAYING ;
	return true ;
} ;

void AudioFileStreamer::Stop() {
	// trk2als: no se corta de golpe: baja unos ms y luego para (en Render)
	if (mode_==AFSM_PLAYING && wav_ && gain_>0) stopping_=true ;
	else mode_=AFSM_STOPPED ;
  Trace::Debug("Streaming stopped");
} ;

bool AudioFileStreamer::Render(fixed *buffer,int samplecount) {

	// See if we're playing

	if (mode_==AFSM_STOPPED) {
		SAFE_DELETE(wav_) ;
		return false ;
	}

	// Do we need to get a new file ?
	// trk2als: si lo de antes suena, primero baja (este bloque) y luego cambia
	bool switching=newPath_ && wav_ && gain_>0 ;
	bool fadeOut=stopping_ || switching ;
	if (newPath_ && !switching) {
		SAFE_DELETE(wav_) ;
		newPath_=false ;
		gain_=0 ;
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
		position_=long(startFrac_*wav_->GetSize(-1)) ;
		frac_=0 ;
		gain_=0 ;
	}

	// we are playing a valid file
	// trk2als: el archivo puede tener otra frecuencia (p. ej. 22050 Hz); se
	// avanza en proporción para que el preview suene a su altura real
	long size=wav_->GetSize(-1) ;
	long full=size ;
	if (end_>0 && end_<size) size=end_ ;   // trk2als: termina antes (una rebanada)
	int fileRate=wav_->GetSampleRate(-1) ;
	int outRate=Audio::GetInstance()->GetSampleRate() ;
	double step=(fileRate>0 && outRate>0)?double(fileRate)/double(outRate):1.0 ;
	step*=speed_ ;

	// trk2als: silencio hasta el siguiente tiempo
	memset(buffer,0,2*samplecount*sizeof(fixed)) ;
	int skip=0 ;
	if (delay_>0 && !fadeOut) {
		skip=(delay_>samplecount)?samplecount:int(delay_) ;
		delay_-=skip ;
		if (skip==samplecount) return true ;
	}
	int frames=samplecount-skip ;

	int need=int(frames*step+frac_)+2 ;
	int count=need ;
	if (position_+count>size) {
		count=size-position_ ;
	}
	if (count<=0) {
		if (switching) { SAFE_DELETE(wav_) ; return true ; }   // lo de antes ya había acabado
		if (loop_ && size>0) { position_=0 ; frac_=0 ; return true ; }
		mode_=AFSM_STOPPED ;
		return true ;
	}
	wav_->GetBuffer(position_,count) ;
	fixed *dst=buffer+2*skip ;
	short *src=(short *)wav_->GetSampleBuffer(-1) ;
	int channel=wav_->GetChannelCount(-1) ;

	int i=0 ;
	for (;i<frames;i++) {
		int idx=int(frac_+i*step) ;
		if (idx>=count) break ;
		short *s=src+idx*channel ;
		int l=(*s)>>(1+shift_) ;
		int r=(channel==2)?(s[1])>>(1+shift_):l ;
		// trk2als: al cortar antes del final, sin click: baja en ~3 ms
		if (size<full) {
			long left=size-(position_+idx) ;
			if (left<128) { l=int(l*left/128) ; r=int(r*left/128) ; }
		}
		// trk2als: el fade de entrada / salida
		if (fadeOut) { if (gain_>0) gain_-- ; }
		else if (gain_<STREAM_FADE) { gain_+=4 ; if (gain_>STREAM_FADE) gain_=STREAM_FADE ; }   // entra en ~1.5 ms (el golpe no se pierde)
		if (gain_<STREAM_FADE) { l=l*gain_/STREAM_FADE ; r=r*gain_/STREAM_FADE ; }
		*dst++=i2fp(l) ;
		*dst++=i2fp(r) ;
		if (fadeOut && gain_==0) { i++ ; break ; }
	}
	if (fadeOut && gain_==0) {   // ya bajó: para, o el que sigue en el próximo bloque
		SAFE_DELETE(wav_) ;
		if (stopping_) { stopping_=false ; mode_=AFSM_STOPPED ; }
		return true ;
	}
	double advance=frac_+i*step ;
	int whole=int(advance) ;
	frac_=advance-whole ;
	position_+=whole ;
	if (i<frames || position_>=size) {
		if (loop_) {            // trk2als: el preview de un loop se repite
			position_=0 ;
			frac_=0 ;
		} else {
			Trace::Debug("Reached the end of %d samples",size);
			mode_=AFSM_STOPPED ;
		}
	}
	return true ;
}
