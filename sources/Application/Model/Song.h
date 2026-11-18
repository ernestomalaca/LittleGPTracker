#ifndef _SONG_H_
#define _SONG_H_

#include "Clip.h"
#include "Application/Persistency/Persistent.h"
#include <string>

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 256

#define MAX_SAMPLEINSTRUMENT_COUNT 0x80
// trk2als: los loops de las pistas de audio usan los instrumentos desde aquí
// (A00 = 0x40); los de las pistas MIDI, los de abajo
#define AUDIO_CLIP_BASE 0x40
#define MAX_MIDIINSTRUMENT_COUNT 0x10

#define MAX_INSTRUMENT_COUNT (MAX_SAMPLEINSTRUMENT_COUNT+MAX_MIDIINSTRUMENT_COUNT)

// trk2als: la Session. Cada celda de una pista MIDI es un clip propio
// (Model/Clip.h); en una pista de audio, el instrumento con el loop.
class Song:Persistent {
public:
	Song() ;
	~Song() ;

	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);

	unsigned char *data_ ;
	ClipBank *clips_ ;

	const std::string &GetClipName(int clip) { return clips_->Get(clip).name ; }
	void SetClipName(int clip,const std::string &name) { clips_->Get(clip).name=name ; }
	// loop del clip en compases (como las páginas del Move); sin loop suena entero
	bool GetClipLoop(int clip,int &start,int &end) {
		Clip &c=clips_->Get(clip) ;
		start=c.loopStart ; end=c.loopEnd ;
		return start>=0 ;
	}
	void SetClipLoop(int clip,int start,int end) {
		Clip &c=clips_->Get(clip) ;
		c.loopStart=start ; c.loopEnd=end ;
	}
	void ClearClipLoop(int clip) { Clip &c=clips_->Get(clip) ; c.loopStart=c.loopEnd=-1 ; }
} ;

#endif
