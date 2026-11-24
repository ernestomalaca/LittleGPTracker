#ifndef _SONG_H_
#define _SONG_H_

#include "Clip.h"
#include "Application/Persistency/Persistent.h"
#include <string>

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 256

// trk2als: 0x00-0x3F, los sonidos de las pistas MIDI; 0x40-0xFE, los clips de
// las pistas de audio (A00-ABE: la celda guarda ese número; 0xFF = vacía).
// Antes eran 0x80 (64 clips de audio) y los MIDI out iban en 0x80-0x8F: los
// proyectos viejos se leen igual (ver InstrumentBank::RestoreContent)
#define MAX_SAMPLEINSTRUMENT_COUNT 0xFF
#define AUDIO_CLIP_BASE 0x40
#define OLD_MIDIINSTRUMENT_BASE 0x80
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
	// trk2als: cuántas veces suena cada escena antes de pasar a la de abajo
	// (como las follow actions de escena de Live); 0 = se queda ahí
	unsigned char sceneRep_[SONG_ROW_COUNT] ;
	// B + Y en la columna de escenas: las repeticiones (xN) apagadas, para
	// tocar las escenas a mano; los números se quedan guardados
	bool followOff_ ;

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
