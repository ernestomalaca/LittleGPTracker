#ifndef _SONG_H_
#define _SONG_H_

#include "Chain.h"
#include "Phrase.h"
#include "Application/Persistency/Persistent.h"
#include <string>

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 256

#define MAX_SAMPLEINSTRUMENT_COUNT 0x80
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

	// trk2als: nombre de cada chain ("woodblock 1"); vacío = sin nombre
	const std::string &GetChainName(int chain) { return chainName_[chain] ; }
	void SetChainName(int chain,const std::string &name) { chainName_[chain]=name ; }
private:
	std::string chainName_[CHAIN_COUNT] ;
} ;

#endif
