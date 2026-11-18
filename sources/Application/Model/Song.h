#ifndef _SONG_H_
#define _SONG_H_

#include "Chain.h"
#include "Phrase.h"
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

class Song:Persistent {
public:
	Song() ;
	~Song() ;

	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);

	unsigned char *data_ ;
	Chain *chain_ ;
	Phrase *phrase_ ;
	ClipBank *clips_ ;   // trk2als: los clips MIDI propios

	// trk2als: nombre de cada chain ("woodblock 1"); vacío = sin nombre
	const std::string &GetChainName(int chain) { return chainName_[chain] ; }
	void SetChainName(int chain,const std::string &name) { chainName_[chain]=name ; }
	// trk2als: loop del clip (filas de la chain, como las páginas del Move);
	// sin loop suena entero
	bool GetChainLoop(int chain,int &start,int &end) {
		start=loopStart_[chain] ; end=loopEnd_[chain] ;
		return start!=0xFF ;
	}
	void SetChainLoop(int chain,int start,int end) {
		loopStart_[chain]=(unsigned char)start ; loopEnd_[chain]=(unsigned char)end ;
	}
	void ClearChainLoop(int chain) { loopStart_[chain]=loopEnd_[chain]=0xFF ; }
private:
	unsigned char loopStart_[CHAIN_COUNT] ;
	unsigned char loopEnd_[CHAIN_COUNT] ;
	std::string chainName_[CHAIN_COUNT] ;
} ;

#endif
