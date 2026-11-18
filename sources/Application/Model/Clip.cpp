#include "Clip.h"
#include "Externals/TinyXML/tinyxml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// trk2als: ver Clip.h

ClipBank::ClipBank() {
}

unsigned short ClipBank::GetNext(const unsigned char *songData,int songSize) {
	bool inSong[CLIP_COUNT+1]={false} ;
	for (int k=0;k<songSize;k++) inSong[songData[k]]=true ;
	for (int i=0;i<CLIP_COUNT;i++)
		if (clip_[i].IsEmpty() && !inSong[i]) return (unsigned short)i ;
	// si ya no hay vacíos, uno que se borró de la Session
	for (int i=0;i<CLIP_COUNT;i++)
		if (!inSong[i]) return (unsigned short)i ;
	return NO_MORE_CLIP ;
}

void ClipBank::Purge(const unsigned char *songData,int songSize) {
	bool inSong[CLIP_COUNT+1]={false} ;
	for (int k=0;k<songSize;k++) inSong[songData[k]]=true ;
	for (int i=0;i<CLIP_COUNT;i++)
		if (!inSong[i]) clip_[i].Clear() ;
}

// <CLIPS><CLIP ID="03" PAGES="2" NOTES="ssttllppvvrrmm..."/></CLIPS>
// cada nota son 7 bytes en hex: inicio, nudge, largo-1, nota, velocity,
// subdivisiones-1 y rampa (con signo)
void ClipBank::Save(TiXmlNode *node) {
	TiXmlElement clips("CLIPS") ;
	for (int i=0;i<CLIP_COUNT;i++) {
		Clip &c=clip_[i] ;
		if (c.IsEmpty()) continue ;
		TiXmlElement e("CLIP") ;
		char id[4] ;
		sprintf(id,"%02X",i) ;
		e.SetAttribute("ID",id) ;
		e.SetAttribute("PAGES",c.pages) ;
		std::string hex ;
		for (size_t k=0;k<c.notes.size();k++) {
			const ClipNote &n=c.notes[k] ;
			unsigned char b[7]={(unsigned char)n.start,(unsigned char)n.tick,
				(unsigned char)(n.len-1),(unsigned char)n.pitch,(unsigned char)n.vel,
				(unsigned char)(n.rat-1),(unsigned char)(signed char)n.ramp} ;
			char h[16] ;
			for (int j=0;j<7;j++) { sprintf(h,"%02X",b[j]) ; hex+=h ; }
		}
		e.SetAttribute("NOTES",hex.c_str()) ;
		clips.InsertEndChild(e) ;
	}
	node->InsertEndChild(clips) ;
}

static int hexByte(const char *p) {
	char h[3]={p[0],p[1],0} ;
	return int(strtol(h,0,16)) ;
}

void ClipBank::Restore(TiXmlElement *element) {
	for (TiXmlElement *e=element->FirstChildElement("CLIP");e;e=e->NextSiblingElement("CLIP")) {
		const char *id=e->Attribute("ID") ;
		int c=id?int(strtol(id,0,16)):-1 ;
		if (c<0 || c>=CLIP_COUNT) continue ;
		Clip &clip=clip_[c] ;
		clip.Clear() ;
		int pages=0 ;
		e->Attribute("PAGES",&pages) ;
		if (pages<1) pages=1 ;
		if (pages>CLIP_MAX_PAGES) pages=CLIP_MAX_PAGES ;
		clip.pages=pages ;
		const char *hex=e->Attribute("NOTES") ;
		if (!hex) continue ;
		size_t len=strlen(hex) ;
		for (size_t k=0;k+14<=len;k+=14) {
			ClipNote n ;
			n.start=hexByte(hex+k) ;
			n.tick=hexByte(hex+k+2) ;
			n.len=hexByte(hex+k+4)+1 ;
			n.pitch=hexByte(hex+k+6) ;
			n.vel=hexByte(hex+k+8) ;
			n.rat=hexByte(hex+k+10)+1 ;
			n.ramp=(signed char)hexByte(hex+k+12) ;
			if (n.start>=clip.Steps() || n.pitch>127) continue ;
			if (n.tick>=CLIP_TICKS) n.tick=CLIP_TICKS-1 ;
			if (n.vel<1) n.vel=1 ;
			if (n.vel>127) n.vel=127 ;
			if (n.rat>8) n.rat=8 ;
			if (n.ramp<-4) n.ramp=-4 ;
			if (n.ramp>4) n.ramp=4 ;
			clip.notes.push_back(n) ;
		}
	}
}
