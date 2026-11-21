#include "Clip.h"
#include "Externals/TinyXML/tinyxml.h"
#include "Foundation/Types/Types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

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

// el FourCC como sus 4 letras ("SVOL"): el número cambia con la plataforma
static void fourccStr(unsigned int v,char *out) {
	unsigned int a=MAKE_FOURCC('A',0,0,0) ;
	for (int i=0;i<4;i++) out[i]=(char)((a=='A')?(v>>(8*i)):(v>>(8*(3-i)))) ;
	out[4]=0 ;
}

static unsigned int strFourcc(const char *s) {
	if (strlen(s)!=4) return 0 ;
	return MAKE_FOURCC(s[0],s[1],s[2],s[3]) ;
}

/************************************************************
 automatización
 ************************************************************/

bool AutoLane::ValueAt(int pos,int &value) const {
	if (points.empty()) return false ;
	if (pos<=points[0].pos) { value=points[0].value ; return true ; }
	// el último punto en o antes de pos
	size_t i=0 ;
	while (i+1<points.size() && points[i+1].pos<=pos) i++ ;
	const AutoPoint &a=points[i] ;
	if (a.inv && !a.ramp && i+1<points.size() && pos>a.pos) { value=points[i+1].value ; return true ; }
	if (!a.ramp || i+1>=points.size()) { value=a.value ; return true ; }
	const AutoPoint &b=points[i+1] ;
	int span=b.pos-a.pos ;
	if (span<=0) { value=b.value ; return true ; }
	// redondeo hacia el valor más cercano (también con negativos)
	double v=a.value+double(b.value-a.value)*(pos-a.pos)/span ;
	value=int(v<0?v-0.5:v+0.5) ;
	return true ;
}

static bool byPos(const AutoPoint &a,const AutoPoint &b) { return a.pos<b.pos ; }

void AutoLane::Sort() {
	std::stable_sort(points.begin(),points.end(),byPos) ;
	// dos puntos en la misma posición: gana el último
	std::vector<AutoPoint> out ;
	for (size_t i=0;i<points.size();i++) {
		if (!out.empty() && out.back().pos==points[i].pos) out.back()=points[i] ;
		else out.push_back(points[i]) ;
	}
	points=out ;
}

AutoLane *Clip::FindLane(unsigned int target,int pad) {
	for (size_t i=0;i<lanes.size();i++)
		if (lanes[i].target==target && lanes[i].pad==pad) return &lanes[i] ;
	return 0 ;
}

AutoLane &Clip::GetLane(unsigned int target,int pad) {
	if (AutoLane *l=FindLane(target,pad)) return *l ;
	AutoLane l ;
	l.target=target ;
	l.pad=pad ;
	lanes.push_back(l) ;
	return lanes.back() ;
}

void Clip::RemoveLane(unsigned int target,int pad) {
	for (size_t i=0;i<lanes.size();i++)
		if (lanes[i].target==target && lanes[i].pad==pad) { lanes.erase(lanes.begin()+i) ; return ; }
}

void Clip::DuplicatePageLanes(int page) {
	int f=page*CLIP_STEPS_PER_PAGE*CLIP_TICKS,t=f+CLIP_STEPS_PER_PAGE*CLIP_TICKS,d=t-f ;
	for (size_t i=0;i<lanes.size();i++) {
		std::vector<AutoPoint> &p=lanes[i].points ;
		std::vector<AutoPoint> out ;
		for (size_t k=0;k<p.size();k++) {
			AutoPoint a=p[k] ;
			if (a.pos>=t) a.pos+=d ;
			out.push_back(a) ;
			if (p[k].pos>=f && p[k].pos<t) out.push_back(AutoPoint(p[k].pos+d,p[k].value,p[k].ramp,p[k].inv)) ;
		}
		p=out ;
		lanes[i].Sort() ;
	}
}

void Clip::DeletePageLanes(int page) {
	int f=page*CLIP_STEPS_PER_PAGE*CLIP_TICKS,t=f+CLIP_STEPS_PER_PAGE*CLIP_TICKS,d=t-f ;
	for (size_t i=0;i<lanes.size();) {
		std::vector<AutoPoint> &p=lanes[i].points ;
		std::vector<AutoPoint> out ;
		for (size_t k=0;k<p.size();k++) {
			if (p[k].pos>=f && p[k].pos<t) continue ;
			AutoPoint a=p[k] ;
			if (a.pos>=t) a.pos-=d ;
			out.push_back(a) ;
		}
		p=out ;
		if (p.empty()) lanes.erase(lanes.begin()+i) ;
		else i++ ;
	}
}

// <CLIPS><CLIP ID="03" PAGES="2" NAME=".." LOOPSTART="0" LOOPEND="1" NOTES="ssttllppvvrrmm..."/></CLIPS>
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
		if (!c.name.empty()) e.SetAttribute("NAME",c.name.c_str()) ;
		if (c.loopStart>=0) {
			e.SetAttribute("LOOPSTART",c.loopStart) ;
			e.SetAttribute("LOOPEND",c.loopEnd) ;
		}
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
		// <LANE TARGET="SVOL" PAD="-1" POINTS="pos,valor,rampa;..."/>
		for (size_t l=0;l<c.lanes.size();l++) {
			const AutoLane &lane=c.lanes[l] ;
			if (lane.points.empty()) continue ;
			TiXmlElement le("LANE") ;
			char t[8] ;
			fourccStr(lane.target,t) ;
			le.SetAttribute("TARGET",t) ;
			le.SetAttribute("PAD",lane.pad) ;
			std::string pts ;
			for (size_t k=0;k<lane.points.size();k++) {
				char b[48] ;
				sprintf(b,"%s%d,%d,%d",k?";":"",lane.points[k].pos,lane.points[k].value,lane.points[k].ramp?1:(lane.points[k].inv?2:0)) ;
				pts+=b ;
			}
			le.SetAttribute("POINTS",pts.c_str()) ;
			e.InsertEndChild(le) ;
		}
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
		if (const char *nm=e->Attribute("NAME")) clip.name=nm ;
		// ojo: este TinyXML pone 0 si el atributo no está
		const char *lsa=e->Attribute("LOOPSTART"),*lea=e->Attribute("LOOPEND") ;
		int ls=lsa?atoi(lsa):-1,le=lea?atoi(lea):-1 ;
		if (ls>=0 && le>=ls && le<pages) { clip.loopStart=ls ; clip.loopEnd=le ; }
		for (TiXmlElement *le=e->FirstChildElement("LANE");le;le=le->NextSiblingElement("LANE")) {
			const char *t=le->Attribute("TARGET"),*pa=le->Attribute("PAD"),*pts=le->Attribute("POINTS") ;
			if (!t || !pts) continue ;
			AutoLane lane ;
			lane.target=strFourcc(t) ;
			if (!lane.target) continue ;
			lane.pad=pa?atoi(pa):-1 ;
			const char *p=pts ;
			while (*p) {
				int pos,val,r ;
				if (sscanf(p,"%d,%d,%d",&pos,&val,&r)==3 && pos>=0 && pos<clip.Steps()*CLIP_TICKS)
					lane.points.push_back(AutoPoint(pos,val,r==1,r==2)) ;
				const char *n=strchr(p,';') ;
				if (!n) break ;
				p=n+1 ;
			}
			lane.Sort() ;
			if (!lane.points.empty()) clip.lanes.push_back(lane) ;
		}
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
