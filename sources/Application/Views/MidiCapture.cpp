#include "MidiCapture.h"
#include "ViewData.h"
#include "Application/Model/Project.h"
#include "Application/Model/Song.h"
#include "Application/Model/Clip.h"
#include "Application/Player/Player.h"
#include "System/System/System.h"
#include <vector>
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Ev {
	unsigned long ms ;
	int ch, pitch, vel ;   // vel 0 = se soltó
	bool running ;
	double song ;          // paso de la canción (si sonaba)
	int clip ;             // el clip MIDI que tocaba la pista (o -1)
	double clipStep ;      // en qué paso de ese clip
} ;

std::vector<Ev> evs ;
unsigned long capturedUpTo=0 ;   // lo anterior ya se capturó
char msg[96] ;

// una nota ya armada (con su largo)
struct Hit {
	unsigned long ms, offMs ;
	int pitch, vel ;
	bool running ;
	double song, offSong ;
	int clip ;
	double clipStep ;
} ;

bool byMs(const Hit &a,const Hit &b) { return a.ms<b.ms ; }

void hitsOf(int ch,std::vector<Hit> &out) {
	out.clear() ;
	unsigned long now=System::GetInstance()->GetClock() ;
	for (size_t i=0;i<evs.size();i++) {
		const Ev &e=evs[i] ;
		if (e.ch!=ch || e.vel==0 || e.ms<=capturedUpTo) continue ;
		Hit h ;
		h.ms=e.ms ; h.pitch=e.pitch ; h.vel=e.vel ; h.running=e.running ;
		h.song=e.song ; h.clip=e.clip ; h.clipStep=e.clipStep ;
		h.offMs=now ; h.offSong=-1 ;
		for (size_t j=i+1;j<evs.size();j++) {   // cuándo se soltó
			const Ev &o=evs[j] ;
			if (o.ch!=ch || o.pitch!=e.pitch) continue ;
			h.offMs=o.ms ;
			h.offSong=o.song ;
			break ;
		}
		out.push_back(h) ;
	}
	std::stable_sort(out.begin(),out.end(),byMs) ;
}

// el tempo de lo que tocaste sin nada sonando: el que pone más golpes en la
// rejilla de corcheas y deja la frase de 1, 2, 4, 8 o 16 compases justos
// (cerca del tempo del proyecto si dudas). 0 = no se pudo
double guessTempo(const std::vector<double> &t,double lastEnd,int projTempo,double &quality) {
	quality=1 ;
	if (t.size()<4) return 0 ;
	double bestT=0, best=1e9 ;
	for (double T=75.0;T<=165.0;T+=0.25) {
		double eighth=30.0/T, bar=240.0/T ;
		double err=0 ;
		for (size_t i=1;i<t.size();i++) {
			double x=t[i]/eighth, d=x-floor(x+0.5) ;
			err+=d*d ;
		}
		err/=double(t.size()-1) ;   // 0 (todo en la rejilla) .. 0.083 (al azar)
		double bars=(lastEnd+eighth*0.5)/bar ;
		double p=1 ;
		while (p<bars-0.02 && p<16) p*=2 ;
		double fill=bars/p ;   // la frase llena sus compases
		if (fill>1) fill=1 ;
		double score=err+0.03*(1-fill)+0.01*fabs(log(T/projTempo)/log(2.0)) ;
		if (score<best) { best=score ; bestT=T ; quality=err ; }
	}
	return bestT ;
}

// si la frase se repite (cada 2, 4 u 8 compases), cuántos compases mide
int repeatBars(const std::vector<int> &step,const std::vector<int> &pitch,int pages) {
	for (int B=2;B<=8;B*=2) {
		if (B*2>pages) break ;
		int L=B*CLIP_STEPS_PER_PAGE, end=pages*CLIP_STEPS_PER_PAGE ;
		int a=0, b=0, both=0 ;
		for (size_t i=0;i<step.size();i++) {
			if (step[i]>=end-L) {   // la última vuelta
				a++ ;
				for (size_t j=0;j<step.size();j++)
					if (step[j]>=end-2*L && step[j]<end-L && pitch[j]==pitch[i] && abs(step[j]+L-step[i])<=1) { both++ ; break ; }
			} else if (step[i]>=end-2*L) b++ ;
		}
		int m=a>b?a:b ;
		if (m>=2 && both*10>=m*6) return B ;
	}
	return 0 ;
}

void addNote(Clip &c,int step,int tick,int len,int pitch,int vel) {
	int total=c.Steps() ;
	if (step<0 || step>=total) return ;
	if (len<1) len=1 ;
	if (step+len>total) len=total-step ;
	for (size_t k=0;k<c.notes.size();k++)   // la misma (paso y altura): gana la nueva
		if (c.notes[k].start==step && c.notes[k].pitch==pitch) { c.notes.erase(c.notes.begin()+k) ; break ; }
	ClipNote n ;
	n.start=step ; n.tick=tick ; n.len=len ; n.pitch=pitch ; n.vel=vel<1?1:(vel>127?127:vel) ;
	size_t at=0 ;
	while (at<c.notes.size() && c.notes[at].start<=step) at++ ;
	c.notes.insert(c.notes.begin()+at,n) ;
}

// paso + nudge (1/24) de una posición con fracción
void splitPos(double pos,int &step,int &tick) {
	if (pos<0) pos=0 ;
	step=int(floor(pos)) ;
	tick=int(floor((pos-step)*CLIP_TICKS+0.5)) ;
	if (tick>=CLIP_TICKS) { step++ ; tick=0 ; }
}

// lo que caería encima del clip 'id' (sonando): la última vuelta de su loop,
// contada desde el último golpe en ese clip
void overdubNotes(Clip &c,const std::vector<Hit> &hits,int id,int tempo,std::vector<ClipNote> &out) {
	out.clear() ;
	if (c.IsEmpty()) return ;
	int lastI=-1 ;
	for (size_t i=0;i<hits.size();i++) if (hits[i].running && hits[i].clip==id) lastI=int(i) ;
	if (lastI<0) return ;
	double lastSong=hits[lastI].song ;
	int ls=0, le=c.pages-1 ;
	if (c.loopStart>=0 && c.loopEnd>=c.loopStart && c.loopEnd<c.pages) { ls=c.loopStart ; le=c.loopEnd ; }
	int L=(le-ls+1)*CLIP_STEPS_PER_PAGE, L0=ls*CLIP_STEPS_PER_PAGE ;
	for (size_t i=0;i<hits.size();i++) {
		const Hit &h=hits[i] ;
		if (!h.running || h.clip!=id || h.song<lastSong-L+0.001) continue ;
		int step, tick ;
		splitPos(h.clipStep,step,tick) ;
		if (step>=L0+L) step=L0+(step-L0)%L ;
		double offSong=h.offSong>=0?h.offSong:h.song+(h.offMs-h.ms)*tempo/15000.0 ;
		int len=int(floor(offSong-h.song+0.5)) ;
		if (len<1) len=1 ;
		if (len>L) len=L ;
		ClipNote n ;
		n.start=step ; n.tick=tick ; n.len=len ; n.pitch=h.pitch ; n.vel=h.vel ;
		out.push_back(n) ;
	}
}

}


void MidiCapture::Held(int ch,std::vector<int> &pitches) {
	pitches.clear() ;
	bool down[128] ;
	memset(down,0,sizeof(down)) ;
	for (size_t i=0;i<evs.size();i++)
		if (evs[i].ch==ch && evs[i].pitch>=0 && evs[i].pitch<128) down[evs[i].pitch]=evs[i].vel>0 ;
	for (int p=0;p<128;p++) if (down[p]) pitches.push_back(p) ;
}

void MidiCapture::HeldClasses(int out[12]) {
	int down[128] ;
	for (int p=0;p<128;p++) down[p]=-1 ;
	for (size_t i=0;i<evs.size();i++) {
		int p=evs[i].pitch ;
		if (p<0 || p>127) continue ;
		if (evs[i].vel>0) down[p]=evs[i].ch ;
		else if (down[p]==evs[i].ch) down[p]=-1 ;
	}
	for (int k=0;k<12;k++) out[k]=-1 ;
	for (int p=0;p<128;p++) if (down[p]>=0) out[p%12]=down[p] ;
}

bool MidiCapture::Forget(int ch) {
	bool any=false ;
	for (size_t i=0;i<evs.size();i++) if (evs[i].ch==ch && evs[i].vel>0 && evs[i].ms>capturedUpTo) any=true ;
	if (!evs.empty()) capturedUpTo=evs.back().ms ;
	return any ;
}

void MidiCapture::Note(int ch,int pitch,int vel) {
	Ev e ;
	e.ms=System::GetInstance()->GetClock() ;
	e.ch=ch ; e.pitch=pitch ; e.vel=vel ;
	e.clip=-1 ; e.song=-1 ; e.clipStep=0 ;
	e.running=Player::GetInstance()->CapturePos(ch,e.song,e.clip,e.clipStep) ;
	evs.push_back(e) ;
	// lo de hace más de un minuto (o demasiado) se olvida
	size_t drop=0 ;
	while (drop<evs.size() && (e.ms-evs[drop].ms>60000 || evs.size()-drop>2000)) drop++ ;
	if (drop) evs.erase(evs.begin(),evs.begin()+drop) ;
}

unsigned long MidiCapture::LastNoteMs() {
	for (size_t i=evs.size();i>0;i--)
		if (evs[i-1].vel>0) return evs[i-1].ms>capturedUpTo?evs[i-1].ms:0 ;
	return 0 ;
}

// un clip nuevo con la última frase (sin pausas de más de 3 s): sin nada
// sonando, el primer golpe en el 1.1.1 y el tempo adivinado ('tempo' entra con
// el del proyecto y sale con el que quedó; 'guessed' = lo adivinó); sonando,
// en la rejilla de la canción. Si se repite, el loop en la última vuelta
static bool buildNew(const std::vector<Hit> &hits,int &tempo,bool &guessed,Clip &c,int &loopBars) {
	guessed=false ;
	if (hits.empty()) return false ;
	const Hit &last=hits.back() ;
	size_t first=hits.size()-1 ;
	while (first>0 && hits[first].ms-hits[first-1].ms<=3000) first-- ;
	bool running=last.running && hits[first].running ;
	std::vector<double> pos, len ;
	if (running) {
		double origin=floor((hits[first].song+1.0)/CLIP_STEPS_PER_PAGE)*CLIP_STEPS_PER_PAGE ;
		for (size_t i=first;i<hits.size();i++) {
			const Hit &h=hits[i] ;
			pos.push_back(h.song-origin) ;
			double off=h.offSong>=0?h.offSong:h.song+(h.offMs-h.ms)*tempo/15000.0 ;
			len.push_back(off-h.song) ;
		}
	} else {
		std::vector<double> t ;
		double lastEnd=0 ;
		for (size_t i=first;i<hits.size();i++) {
			double s=(hits[i].ms-hits[first].ms)/1000.0 ;
			t.push_back(s) ;
			if (s>lastEnd) lastEnd=s ;
		}
		double q ;
		double T=guessTempo(t,lastEnd,tempo,q) ;
		if (T>0 && q<0.03) {   // seguro: ese es el tempo
			int ti=int(floor(T+0.5)) ;
			guessed=ti!=tempo ;
			tempo=ti ;
		}
		double stepMs=15000.0/tempo ;
		for (size_t i=first;i<hits.size();i++) {
			pos.push_back((hits[i].ms-hits[first].ms)/stepMs) ;   // el primer golpe, en el 1.1.1
			len.push_back((hits[i].offMs-hits[i].ms)/stepMs) ;
		}
	}
	// el largo: hasta el compás del último golpe (una nota larga al final no
	// pide otro compás; se recorta)
	double lastOn=0 ;
	for (size_t i=0;i<pos.size();i++) {
		if (pos[i]<0) pos[i]=0 ;
		if (pos[i]>lastOn) lastOn=pos[i] ;
	}
	int bars=int(floor(lastOn/CLIP_STEPS_PER_PAGE))+1 ;
	if (bars<1) bars=1 ;
	int pages=1 ;
	while (pages<bars && pages<CLIP_MAX_PAGES) pages*=2 ;
	int shift=0 ;   // más de 16 compases: los últimos
	if (bars>CLIP_MAX_PAGES) shift=(bars-CLIP_MAX_PAGES)*CLIP_STEPS_PER_PAGE ;
	c.Clear() ;
	c.pages=pages ;
	std::vector<int> st, pi ;
	for (size_t i=0;i<pos.size();i++) {
		int step, tick ;
		if (pos[i]-shift<0) continue ;
		splitPos(pos[i]-shift,step,tick) ;
		const Hit &h=hits[first+i] ;
		addNote(c,step,tick,int(floor(len[i]+0.5)),h.pitch,h.vel) ;
		st.push_back(step) ;
		pi.push_back(h.pitch) ;
	}
	int B=repeatBars(st,pi,pages) ;
	if (B>0) { c.loopStart=pages-B ; c.loopEnd=pages-1 ; }   // el loop, en la última vuelta
	loopBars=B>0?B:pages ;
	return true ;
}

void MidiCapture::Preview(ViewData *d,int clip,std::vector<ClipNote> &out,int *pages) {
	out.clear() ;
	std::vector<Hit> hits ;
	hitsOf(d->songX_,hits) ;
	if (hits.empty() || clip<0) return ;
	int tempo=d->project_->GetTempo() ;
	if (tempo<20) tempo=120 ;
	if (d->virtualCell_>=0 && clip==d->currentChain_) {   // el clip vacío: lo que se crearía
		Clip c ;
		bool g ;
		int lb ;
		if (buildNew(hits,tempo,g,c,lb)) {
			out=c.notes ;
			if (pages) *pages=c.pages ;
		}
		return ;
	}
	overdubNotes(d->song_->clips_->Get(clip),hits,clip,tempo,out) ;
}

const char *MidiCapture::Capture(ViewData *d,int &beforeClip,Clip &before,int &playRow) {
	beforeClip=-1 ;
	playRow=-1 ;
	Project *project=d->project_ ;
	Song *song=d->song_ ;
	int ch=d->songX_ ;
	std::vector<Hit> hits ;
	hitsOf(ch,hits) ;
	if (hits.empty()) return 0 ;
	if (project->GetTrackInstrument(ch)<0) return 0 ;   // pista de audio
	const Hit &last=hits.back() ;
	int tempo=project->GetTempo() ;
	if (tempo<20) tempo=120 ;
	bool virt=d->virtualCell_>=0 && d->virtualCell_%SONG_CHANNEL_COUNT==ch ;

	// --- sonando, con un clip MIDI en la pista: encima, la última vuelta ---
	if (!virt && last.running && last.clip>=0) {
		int id=last.clip ;
		Clip &c=song->clips_->Get(id) ;
		if (c.IsEmpty()) return 0 ;
		capturedUpTo=last.ms ;
		beforeClip=id ;
		before=c ;
		std::vector<ClipNote> add ;
		overdubNotes(c,hits,id,tempo,add) ;   // (lo mismo que se ve de silueta)
		int n=int(add.size()) ;
		for (size_t i=0;i<add.size();i++) addNote(c,add[i].start,add[i].tick,add[i].len,add[i].pitch,add[i].vel) ;
		if (!n) { beforeClip=-1 ; return 0 ; }
		snprintf(msg,sizeof(msg),n==1?"Capturada 1 nota en el clip":"Capturadas %d notas en el clip",n) ;
		return msg ;
	}

	// --- un clip nuevo ---
	// la celda: la del clip vacío que estás viendo, o la del cursor en esa
	// pista (o la siguiente vacía hacia abajo)
	int row ;
	unsigned short id ;
	if (virt) {
		row=d->virtualCell_/SONG_CHANNEL_COUNT ;
		id=(unsigned short)d->currentChain_ ;
	} else {
		row=d->songOffset_+d->songY_ ;
		while (row<SONG_ROW_COUNT && song->data_[ch+SONG_CHANNEL_COUNT*row]!=0xFF) row++ ;
		if (row>=SONG_ROW_COUNT) { snprintf(msg,sizeof(msg),"No hay celda libre en la pista") ; return msg ; }
		id=song->clips_->GetNext(song->data_,SONG_CHANNEL_COUNT*SONG_ROW_COUNT) ;
		if (id==NO_MORE_CLIP) { snprintf(msg,sizeof(msg),"No quedan clips libres") ; return msg ; }
	}
	Clip c ;
	bool guessed ;
	int loopBars ;
	if (!buildNew(hits,tempo,guessed,c,loopBars)) return 0 ;
	capturedUpTo=last.ms ;
	if (guessed) project->FindVariable(VAR_TEMPO)->SetInt(tempo) ;
	if (getenv("TRK_LOADLOG")) fprintf(stderr,"[CAPTURA] %d notas, tempo %d, %d compases\n",int(c.notes.size()),tempo,c.pages) ;
	song->clips_->Get(id)=c ;
	song->data_[ch+SONG_CHANNEL_COUNT*row]=(unsigned char)id ;
	if (virt) d->virtualCell_=-1 ;   // ya es un clip de verdad
	playRow=row ;   // (sin nada sonando arranca ya; sonando, entra en el siguiente compás)
	if (guessed)
		snprintf(msg,sizeof(msg),"Capturado: %d compas%s a %d bpm",loopBars,loopBars==1?"":"es",tempo) ;
	else
		snprintf(msg,sizeof(msg),"Capturado: %d compas%s",loopBars,loopBars==1?"":"es") ;
	return msg ;
}
