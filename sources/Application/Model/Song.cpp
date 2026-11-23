#include "Song.h"
#include "System/System/System.h"
#include "Application/Utils/HexBuffers.h"
#include <string.h>

Song::Song():Persistent("SONG") {
	data_=(unsigned char *)SYS_MALLOC(SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
	memset(data_,0xFF,SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
	clips_=new ClipBank() ;
	memset(sceneRep_,0,sizeof(sceneRep_)) ;
} ;

Song::~Song() {
	if (data_!=NULL) SYS_FREE (data_) ;
	delete clips_ ;
} ;

void Song::SaveContent(TiXmlNode *node) {
	saveHexBuffer(node,"SONG",data_,SONG_ROW_COUNT*SONG_CHANNEL_COUNT) ;
	clips_->Save(node) ;
	bool any=false ;
	for (int i=0;i<SONG_ROW_COUNT;i++) any|=sceneRep_[i]!=0 ;
	if (any) saveHexBuffer(node,"SCENEREP",sceneRep_,SONG_ROW_COUNT) ;
} ;

void Song::RestoreContent(TiXmlElement *element) {
	TiXmlElement *current=element->FirstChildElement() ;
	while(current) {
		const char *value=current->Value() ;
		if (!strcmp("SONG",value)) restoreHexBuffer(current,data_) ;
		if (!strcmp("CLIPS",value)) clips_->Restore(current) ;
		if (!strcmp("SCENEREP",value)) restoreHexBuffer(current,sceneRep_) ;
		current=current->NextSiblingElement() ;
	}
} ;
