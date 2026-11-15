#ifndef _PHRASE_H_
#define _PHRASE_H_

#include "Foundation/Types/Types.h"
#define PHRASE_COUNT 0xFF
#define NO_MORE_PHRASE 0x100
// trk2als: polifonía. Además de su columna de notas (la voz 0), cada phrase
// tiene 3 capas más (voces 1-3): nota, velocity y si la voz se corta ahí
#define VOICE_LAYERS 3
#define VOICE_COUNT (VOICE_LAYERS+1)
#define VF_KILL 1        // la nota que sonaba en esa voz termina en este paso
#define VVEL_DEFAULT 0xFF

class Phrase {
public:
	Phrase() ;
	~Phrase() ;
	unsigned short GetNext() ;
	bool IsUsed(uchar i) { return isUsed_[i] ; } ;
	void SetUsed(uchar c) ;
	void ClearAllocation() ;

	uchar *note_ ;
	uchar *instr_ ;
	FourCC *cmd1_ ;
	ushort *param1_ ;
	FourCC *cmd2_ ;
	ushort *param2_ ;
	// trk2als: capas de voces (voz 1..3 = capa 0..2)
	uchar *voiceNote_ ;
	uchar *voiceVel_ ;
	uchar *voiceFlags_ ;
	static int VIdx(int voice,int phrase,int pos) { return ((voice-1)*PHRASE_COUNT+phrase)*16+pos ; }
	// trk2als: tiempo fino de cada nota (voces 0..3): bits 0-3 = nudge en
	// ticks (0..5, 1/6 de paso), bits 4-6 = subdivisiones - 1 (1..8)
	uchar *timing_ ;
	static int TIdx(int voice,int phrase,int pos) { return (voice*PHRASE_COUNT+phrase)*16+pos ; }
	int Tick(int voice,int phrase,int pos) { return timing_[TIdx(voice,phrase,pos)]&0x0F ; }
	int Ratchet(int voice,int phrase,int pos) { return ((timing_[TIdx(voice,phrase,pos)]>>4)&7)+1 ; }
	// rampa de velocity de las subdivisiones: -4..4 (+ crece, - se apaga)
	uchar *ramp_ ;
	// nudge fino: cuartos de tick (0..3) además de Tick(): 1/24 de paso
	uchar *fine_ ;
	int Fine(int voice,int phrase,int pos) { return fine_[TIdx(voice,phrase,pos)]&3 ; }
	int Ramp(int voice,int phrase,int pos) { return (signed char)ramp_[TIdx(voice,phrase,pos)] ; }
	void ClearVoices(int phrase) ;
	void CopyVoices(int from,int to) ;
	
private:
	bool isUsed_[PHRASE_COUNT] ;

} ;

#endif
