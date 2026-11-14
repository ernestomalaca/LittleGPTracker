#ifndef _AUDIO_FILE_STREAMER_H_
#define _AUDIO_FILE_STREAMER_H_

#include "Services/Audio/AudioModule.h"
#include "System/FileSystem/FileSystem.h"
#include "Application/Instruments/WavFile.h"

enum AudioFileStreamerMode {
	AFSM_STOPPED,
	AFSM_PLAYING
} ;

class AudioFileStreamer: public AudioModule {
public:
	AudioFileStreamer() ;
	virtual ~AudioFileStreamer() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	bool Start(const Path &) ;
	// trk2als: preview a tempo. speed multiplica la velocidad, delaySecs espera
	// antes de sonar, startFrac es el punto de arranque (0-1) y loop repite
	bool Start(const Path &, double speed, double delaySecs, double startFrac, bool loop) ;
	void Stop() ;
	// trk2als: cuadro que suena; -2 = arrancando (aún sin abrir), -1 = parado
	int GetPosition() { return (mode_!=AFSM_PLAYING)?-1:(wav_ && !newPath_?position_:-2) ; }
protected:
	AudioFileStreamerMode mode_ ;
	Path path_ ;
	bool newPath_ ;
	WavFile *wav_ ;
	int position_ ;
	double speed_ ;
	double startFrac_ ;
	long delay_ ;     // frames de silencio antes de sonar
	bool loop_ ;
	double frac_ ;   // trk2als: posición fraccionaria para respetar la frecuencia del archivo
	int shift_ ;
} ;

#endif 
