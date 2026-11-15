#include "Phrase.h"
#include "System/System/System.h"
#include <stdlib.h>
#include <string.h>

Phrase::Phrase() {

	int size=PHRASE_COUNT*16 ; // PHRASE_COUNT phrases of 0x10 steps
	note_=(unsigned char *)SYS_MALLOC(size) ;
	memset(note_,0xFF,size) ;
	instr_=(unsigned char *)SYS_MALLOC(size) ;
	memset(instr_,0xFF,size) ;

	cmd1_=(FourCC *)SYS_MALLOC(size*sizeof(FourCC)) ;
	memset(cmd1_,'-',size*sizeof(FourCC)) ;
	param1_=(unsigned short *)SYS_MALLOC(size*sizeof(short)) ;
	memset(param1_,0x00,size*sizeof(short)) ;

	cmd2_=(FourCC *)SYS_MALLOC(size*sizeof(FourCC)) ;
	memset(cmd2_,'-',size*sizeof(FourCC)) ;
	param2_=(unsigned short *)SYS_MALLOC(size*sizeof(short)) ;
	memset(param2_,0x00,size*sizeof(short)) ;

	int vsize=VOICE_LAYERS*PHRASE_COUNT*16 ;   // trk2als
	voiceNote_=(uchar *)SYS_MALLOC(vsize) ;
	memset(voiceNote_,0xFF,vsize) ;
	voiceVel_=(uchar *)SYS_MALLOC(vsize) ;
	memset(voiceVel_,VVEL_DEFAULT,vsize) ;
	voiceFlags_=(uchar *)SYS_MALLOC(vsize) ;
	memset(voiceFlags_,0,vsize) ;
	timing_=(uchar *)SYS_MALLOC(VOICE_COUNT*PHRASE_COUNT*16) ;
	memset(timing_,0,VOICE_COUNT*PHRASE_COUNT*16) ;

	for (int i=0;i<PHRASE_COUNT;i++) {
		isUsed_[i]=false ;
	}
} ;

void Phrase::ClearVoices(int phrase) {
	for (int v=1;v<VOICE_COUNT;v++) {
		int i=VIdx(v,phrase,0) ;
		memset(voiceNote_+i,0xFF,16) ;
		memset(voiceVel_+i,VVEL_DEFAULT,16) ;
		memset(voiceFlags_+i,0,16) ;
	}
	for (int v=0;v<VOICE_COUNT;v++) memset(timing_+TIdx(v,phrase,0),0,16) ;
}

void Phrase::CopyVoices(int from,int to) {
	for (int v=1;v<VOICE_COUNT;v++) {
		memcpy(voiceNote_+VIdx(v,to,0),voiceNote_+VIdx(v,from,0),16) ;
		memcpy(voiceVel_+VIdx(v,to,0),voiceVel_+VIdx(v,from,0),16) ;
		memcpy(voiceFlags_+VIdx(v,to,0),voiceFlags_+VIdx(v,from,0),16) ;
	}
	for (int v=0;v<VOICE_COUNT;v++) memcpy(timing_+TIdx(v,to,0),timing_+TIdx(v,from,0),16) ;
}

Phrase::~Phrase() {
	if (note_) SYS_FREE(note_) ;
	if (instr_) SYS_FREE(instr_) ;
    /* CMDS_HERE
	if (cmd_) SYS_FREE(cmd_) ;
	if (cmdData_) SYS_FREE(cmdData_) ;
	*/
} ;

unsigned short Phrase::GetNext() {
	for (int i=0;i<PHRASE_COUNT;i++) {
		if (!isUsed_[i]) {
			isUsed_[i]=true ;
			return i ;
		}
	}
	return NO_MORE_PHRASE ;
} ;

void Phrase::SetUsed(unsigned char c) {
	isUsed_[c]=true ;
}

void Phrase::ClearAllocation() {

	for (int i=0;i<PHRASE_COUNT;i++) {
		isUsed_[i]=false ;
	}
} ;
