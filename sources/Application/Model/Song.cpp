#include "Song.h"
#include "System/System/System.h"
#include "Application/Utils/HexBuffers.h"
#include <string.h>

Song::Song():Persistent("SONG") {
	data_=(unsigned char *)SYS_MALLOC(SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
	memset(data_,0xFF,SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
	clips_=new ClipBank() ;
} ;

Song::~Song() {
	if (data_!=NULL) SYS_FREE (data_) ;
	delete clips_ ;
} ;

void Song::SaveContent(TiXmlNode *node) {
	saveHexBuffer(node,"SONG",data_,SONG_ROW_COUNT*SONG_CHANNEL_COUNT) ;
	clips_->Save(node) ;
} ;

void Song::RestoreContent(TiXmlElement *element) {
	TiXmlElement *current=element->FirstChildElement() ;
	while(current) {
		const char *value=current->Value() ;
		if (!strcmp("SONG",value)) restoreHexBuffer(current,data_) ;
		if (!strcmp("CLIPS",value)) clips_->Restore(current) ;
		current=current->NextSiblingElement() ;
	}
} ;
