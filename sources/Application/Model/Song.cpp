#include "Song.h"
#include "System/io/Status.h"
#include "Application/Utils/HexBuffers.h"
#include "Application/Instruments/CommandList.h"
#include "Table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>


Song::Song():Persistent("SONG") {

	data_=(unsigned char *)SYS_MALLOC(SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
	memset(data_,0xFF,SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
	
	chain_=new Chain() ;   // Allocate chain datas
	phrase_=new Phrase() ; // Allocate phrase datas
	memset(loopStart_,0xFF,sizeof(loopStart_)) ;
	memset(loopEnd_,0xFF,sizeof(loopEnd_)) ;
} ;

Song::~Song() {
	if (data_!=NULL) SYS_FREE (data_) ;
	delete chain_ ;
	delete phrase_ ;
} ;

void Song::SaveContent(TiXmlNode *node) {
	for (int i=0; i<PHRASE_COUNT*16; i++)
	{
		phrase_->param1_[i] = Swap16(phrase_->param1_[i]);
		phrase_->param2_[i] = Swap16(phrase_->param2_[i]);
	}	
	saveHexBuffer(node,"SONG",data_,SONG_ROW_COUNT*SONG_CHANNEL_COUNT) ;
	saveHexBuffer(node,"CHAINS",chain_->data_,CHAIN_COUNT*16) ;
	saveHexBuffer(node,"TRANSPOSES",chain_->transpose_,CHAIN_COUNT*16) ;
	saveHexBuffer(node,"NOTES",phrase_->note_,PHRASE_COUNT*16) ;
	saveHexBuffer(node,"INSTRUMENTS",phrase_->instr_,PHRASE_COUNT*16) ;
	saveHexBuffer(node,"COMMAND1",phrase_->cmd1_,PHRASE_COUNT*16) ;
	saveHexBuffer(node,"PARAM1",phrase_->param1_,PHRASE_COUNT*16) ;
	saveHexBuffer(node,"COMMAND2",phrase_->cmd2_,PHRASE_COUNT*16) ;
	saveHexBuffer(node,"PARAM2",phrase_->param2_,PHRASE_COUNT*16) ;
	// trk2als: las capas de voces (polifonía), solo si hay alguna nota
	{
		bool any=false ;
		for (int i=0;i<VOICE_LAYERS*PHRASE_COUNT*16 && !any;i++) any=phrase_->voiceNote_[i]!=0xFF ;
		if (any) {
			saveHexBuffer(node,"VNOTES",phrase_->voiceNote_,VOICE_LAYERS*PHRASE_COUNT*16) ;
			saveHexBuffer(node,"VVELS",phrase_->voiceVel_,VOICE_LAYERS*PHRASE_COUNT*16) ;
			saveHexBuffer(node,"VFLAGS",phrase_->voiceFlags_,VOICE_LAYERS*PHRASE_COUNT*16) ;
		}
	}
	// trk2als: nudge y subdivisiones, solo si hay alguno
	{
		bool any=false ;
		for (int i=0;i<VOICE_COUNT*PHRASE_COUNT*16 && !any;i++) any=phrase_->timing_[i]!=0 ;
		if (any) saveHexBuffer(node,"NTIMING",phrase_->timing_,VOICE_COUNT*PHRASE_COUNT*16) ;
	}
	// trk2als: nombres de los chains
	TiXmlElement names("CHAINNAMES") ;
	for (int i=0;i<CHAIN_COUNT;i++) {
		if (chainName_[i].empty()) continue ;
		TiXmlElement n("NAME") ;
		char id[4] ;
		sprintf(id,"%02X",i) ;
		n.SetAttribute("ID",id) ;
		n.SetAttribute("VALUE",chainName_[i].c_str()) ;
		names.InsertEndChild(n) ;
	}
	node->InsertEndChild(names) ;
	// trk2als: loops de los clips
	TiXmlElement loops("CHAINLOOPS") ;
	for (int i=0;i<CHAIN_COUNT;i++) {
		if (loopStart_[i]==0xFF) continue ;
		TiXmlElement l("LOOP") ;
		char id[4] ;
		sprintf(id,"%02X",i) ;
		l.SetAttribute("ID",id) ;
		l.SetAttribute("START",loopStart_[i]) ;
		l.SetAttribute("END",loopEnd_[i]) ;
		loops.InsertEndChild(l) ;
	}
	node->InsertEndChild(loops) ;

} ;

void Song::RestoreContent(TiXmlElement *element) {

	TiXmlElement *current=element->FirstChildElement() ;
	while(current) {
		const char *value=current->Value() ;
		if (!strcmp("SONG",value)) {
			restoreHexBuffer(current,data_) ;
		} ;
		if (!strcmp("CHAINS",value)) {
			restoreHexBuffer(current,chain_->data_) ;
		} ;
		if (!strcmp("TRANSPOSES",value)) {
			restoreHexBuffer(current,chain_->transpose_) ;
		} ;
		if (!strcmp("NOTES",value)) {
			restoreHexBuffer(current,phrase_->note_) ;
		} ;
		if (!strcmp("INSTRUMENTS",value)) {
			restoreHexBuffer(current,phrase_->instr_) ;
		} ;
		if (!strcmp("COMMAND1",value)) {
			restoreHexBuffer(current,(uchar *)phrase_->cmd1_) ;
		} ;
		if (!strcmp("PARAM1",value)) {
			restoreHexBuffer(current,(uchar *)phrase_->param1_) ;
		} ;
		if (!strcmp("COMMAND2",value)) {
			restoreHexBuffer(current,(uchar *)phrase_->cmd2_) ;
		} ;
		if (!strcmp("CHAINNAMES",value)) {   // trk2als
			for (TiXmlElement *n=current->FirstChildElement("NAME");n;n=n->NextSiblingElement("NAME")) {
				const char *id=n->Attribute("ID") ;
				const char *v=n->Attribute("VALUE") ;
				int c=id?int(strtol(id,0,16)):-1 ;
				if (c>=0 && c<CHAIN_COUNT && v) chainName_[c]=v ;
			}
		} ;
		if (!strcmp("VNOTES",value)) restoreHexBuffer(current,phrase_->voiceNote_) ;   // trk2als
		if (!strcmp("VVELS",value)) restoreHexBuffer(current,phrase_->voiceVel_) ;
		if (!strcmp("VFLAGS",value)) restoreHexBuffer(current,phrase_->voiceFlags_) ;
		if (!strcmp("NTIMING",value)) restoreHexBuffer(current,phrase_->timing_) ;
		if (!strcmp("CHAINLOOPS",value)) {   // trk2als
			for (TiXmlElement *l=current->FirstChildElement("LOOP");l;l=l->NextSiblingElement("LOOP")) {
				const char *id=l->Attribute("ID") ;
				int c=id?int(strtol(id,0,16)):-1 ;
				int s=-1,e=-1 ;
				l->Attribute("START",&s) ;
				l->Attribute("END",&e) ;
				if (c>=0 && c<CHAIN_COUNT && s>=0 && s<16 && e>=s && e<16) {
					loopStart_[c]=(unsigned char)s ;
					loopEnd_[c]=(unsigned char)e ;
				}
			}
		} ;
		if (!strcmp("PARAM2",value)) {
			restoreHexBuffer(current,(uchar *)phrase_->param2_) ;
		} ;
		
		
		for (int i=0; i<PHRASE_COUNT*16; i++)
		{
			phrase_->param1_[i] = Swap16(phrase_->param1_[i]);
			phrase_->param2_[i] = Swap16(phrase_->param2_[i]);
		}
		
		current=current->NextSiblingElement() ;
	} ;
	
	Status::Set("Restoring allocation") ;

	// Restore chain & phrase allocation table

	unsigned char *data=data_ ;
	for (int i=0;i<256*SONG_CHANNEL_COUNT;i++) {
		if (*data!=0xFF) {
			if (*data<0x80) {
				chain_->SetUsed(*data) ;
			}	
		}
	 	data++ ;
	}

    data=chain_->data_ ;

	for (int i=0;i<CHAIN_COUNT;i++) {
        for (int j=0;j<16;j++) {
            if (*data!=0xFF) {
                chain_->SetUsed(i) ;
                phrase_->SetUsed(*data) ;
			}
            data++ ;
        } ;
    }

    data=phrase_->note_ ;

	FourCC *table1=phrase_->cmd1_ ;
	FourCC *table2=phrase_->cmd2_ ;

	ushort *param1=phrase_->param1_ ;
	ushort *param2=phrase_->param2_ ;

	TableHolder *th=TableHolder::GetInstance() ;

	for (int i=0;i<PHRASE_COUNT;i++) {
        for (int v=1;v<VOICE_COUNT;v++)   // trk2als: una phrase con notas solo en las capas también se usa
            for (int j=0;j<16;j++)
                if (phrase_->voiceNote_[Phrase::VIdx(v,i,j)]!=0xFF) phrase_->SetUsed(i) ;
        for (int j=0;j<16;j++) {
            if (*data!=0xFF) {
                phrase_->SetUsed(i) ;
            }
			if (*table1==I_CMD_TABL) {
				*param1&=0x7F ;
				th->SetUsed((*param1)) ;
			} ;
			if (*table2==I_CMD_TABL) {
				*param2&=0x7F ;
				th->SetUsed((*param2)) ;
			} ;
			table1++ ;
			table2++ ;
			param1++ ;
			param2++ ;
            data++ ;
    	} ;
    }
};
