#include "SF2Import.h"
#include "SF2Reader.h"
#include "SampleInstrument.h"
#include "SamplePool.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Console/Trace.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <map>

// .wav de 16 bits
static bool writeWav(const char *path,const std::vector<short> &frames,int channels,int rate) {
	I_File *f=FileSystem::GetInstance()->Open(path,"w") ;
	if (!f) return false ;
	int dataBytes=int(frames.size()*2) ;
	unsigned char h[44] ;
	memcpy(h,"RIFF",4) ;
	int v=36+dataBytes ; memcpy(h+4,&v,4) ;
	memcpy(h+8,"WAVEfmt ",8) ;
	v=16 ; memcpy(h+16,&v,4) ;
	short sv=1 ; memcpy(h+20,&sv,2) ;
	sv=(short)channels ; memcpy(h+22,&sv,2) ;
	memcpy(h+24,&rate,4) ;
	v=rate*channels*2 ; memcpy(h+28,&v,4) ;
	sv=(short)(channels*2) ; memcpy(h+32,&sv,2) ;
	sv=16 ; memcpy(h+34,&sv,2) ;
	memcpy(h+36,"data",4) ;
	memcpy(h+40,&dataBytes,4) ;
	f->Write(h,1,44) ;
	if (dataBytes) f->Write(&frames[0],1,dataBytes) ;
	f->Close() ;
	delete f ;
	return true ;
}

// nombre de archivo seguro: minúsculas, letras, números y '_'
static std::string safeName(const std::string &s) {
	std::string out ;
	for (size_t i=0;i<s.size();i++) {
		unsigned char c=(unsigned char)s[i] ;
		if (c>='A' && c<='Z') c=c-'A'+'a' ;
		if ((c>='a' && c<='z') || (c>='0' && c<='9')) out+=char(c) ;
		else if (!out.empty() && out[out.size()-1]!='_') out+='_' ;
	}
	while (!out.empty() && out[out.size()-1]=='_') out.erase(out.size()-1) ;
	return out.empty()?std::string("sf2"):out ;
}

static int clampi(float v,int lo,int hi) {
	int i=int(floorf(v+0.5f)) ;
	return i<lo?lo:(i>hi?hi:i) ;
}

// zonas seguidas con el mismo sample, raíz, afinación y loop se juntan (en el
// .sf2 a veces solo cambia la envolvente, que aquí es una para todo)
static void mergeZones(std::vector<SF2Zone> &zones) {
	std::vector<SF2Zone> out ;
	for (size_t i=0;i<zones.size();i++) {
		const SF2Zone &z=zones[i] ;
		if (!out.empty()) {
			SF2Zone &p=out[out.size()-1] ;
			if (p.hi+1==z.lo && p.sample==z.sample && p.link==z.link && p.start==z.start &&
			    p.end==z.end && p.root==z.root && p.cents==z.cents && p.loop==z.loop &&
			    p.loopStart==z.loopStart && p.loopEnd==z.loopEnd) {
				p.hi=z.hi ;
				continue ;
			}
		}
		out.push_back(z) ;
	}
	zones.swap(out) ;
}

// ¿el loop calza con la nota? Un loop corto (pocos ciclos) fija el tono del
// sonido sostenido: si no dura un número entero de ciclos de la raíz, la cola
// sale en otro tono. Pasa en .sf2 muy comprimidos, como el sample agudo de la
// marimba: 13 muestras de loop para un ciclo de 13.5 (70 cents arriba)
static bool badLoop(const SF2Zone &z) {
	if (!z.loop) return false ;
	float hz=440.0f*powf(2.0f,(z.root+z.cents/100.0f-69)/12.0f) ;
	float period=z.rate/hz ;
	float cycles=(z.loopEnd-z.loopStart)/period ;
	if (cycles>4) return false ;   // loop largo: no fija el tono
	float n=floorf(cycles+0.5f) ;
	if (n<1) n=1 ;
	float cents=1200.0f*log2f(n/cycles) ;
	return fabsf(cents)>30.0f ;
}

// las teclas de una zona con el loop desafinado las toca la zona vecina que
// suena bien (la más cercana; si hay dos, la de abajo)
static void fixBadLoops(std::vector<SF2Zone> &zones) {
	std::vector<bool> bad(zones.size()) ;
	for (size_t i=0;i<zones.size();i++) bad[i]=badLoop(zones[i]) ;
	for (size_t i=0;i<zones.size();i++) {
		if (!bad[i]) continue ;
		int best=-1, bestD=1000 ;
		for (size_t j=0;j<zones.size();j++) {
			if (bad[j]) continue ;
			int d=(zones[j].hi<zones[i].lo)?zones[i].lo-zones[j].hi:(zones[j].lo>zones[i].hi)?zones[j].lo-zones[i].hi:0 ;
			if (d<bestD) { bestD=d ; best=int(j) ; }
		}
		if (best<0) continue ;
		int lo=zones[i].lo, hi=zones[i].hi ;
		zones[i]=zones[best] ;
		zones[i].lo=lo ;
		zones[i].hi=hi ;
	}
}

static const SF2Zone *zoneFor(const std::vector<SF2Zone> &zones,int note) {
	const SF2Zone *best=0 ;
	int bestD=1000 ;
	for (size_t i=0;i<zones.size();i++) {
		int d=(note<zones[i].lo)?zones[i].lo-note:(note>zones[i].hi)?note-zones[i].hi:0 ;
		if (d<bestD) { bestD=d ; best=&zones[i] ; }
	}
	return best ;
}

bool SF2Import(const char *sf2Path,int presetIndex,SampleInstrument *si,std::string &message) {
	SF2File sf ;
	if (!sf.Open(sf2Path)) { message="no pude leer el .sf2" ; return false ; }
	if (presetIndex<0 || presetIndex>=(int)sf.Presets().size()) { message="no existe ese instrumento" ; return false ; }
	const SF2Preset &preset=sf.Presets()[presetIndex] ;
	std::vector<SF2Zone> zones ;
	if (!sf.Zones(preset.index,zones)) { message="el instrumento no trae samples" ; return false ; }
	fixBadLoops(zones) ;
	mergeZones(zones) ;
	SamplePool *pool=SamplePool::GetInstance() ;
	std::string base=safeName(preset.name) ;
	// un .wav por sample distinto del .sf2
	std::map<std::string,std::string> written ;   // clave del sample -> nombre del .wav
	std::string list ;
	std::string firstName ;
	int n=0 ;
	for (size_t i=0;i<zones.size();i++) {
		SF2Zone &z=zones[i] ;
		char key[64] ;
		sprintf(key,"%d/%d/%d/%d",z.sample,z.link,z.start,z.end) ;
		std::string wav ;
		if (written.count(key)) {
			wav=written[key] ;
		} else {
			char num[16] ;
			sprintf(num,"_%02d",++n) ;
			wav=base+num+".wav" ;
			if (pool->IndexOf(wav.c_str())<0) {   // convertido antes: se reusa
				std::vector<short> frames ;
				int ch=1 ;
				if (!sf.ReadZone(z,frames,ch)) { message="no pude leer un sample" ; return false ; }
				std::string path=std::string("samples:")+wav ;
				Path p(path.c_str()) ;
				if (!writeWav(p.GetPath().c_str(),frames,ch,z.rate)) { message="no pude escribir en samples" ; return false ; }
				if (pool->LoadProjectSample(wav.c_str())<0) { message="no pude cargar un sample" ; return false ; }
			}
			written[key]=wav ;
		}
		if (firstName.empty()) firstName=wav ;
		char item[320] ;
		snprintf(item,sizeof(item),"%s%d,%d,%d,%.1f,%d,%d,%s",list.empty()?"":";",z.lo,z.hi,z.root,z.cents,
		         z.loop?z.loopStart:-1,z.loop?z.loopEnd:-1,wav.c_str()) ;
		list+=item ;
	}
	// el instrumento: classic, sin región ni warp; envolvente y volumen del
	// .sf2 (los de la zona del C3)
	const SF2Zone *c3=zoneFor(zones,60) ;
	si->FindVariable(SIP_S_INIT)->SetInt(1) ;
	si->FindVariable(SIP_S_MODE)->SetInt(SM_CLASSIC) ;
	si->FindVariable(SIP_S_START)->SetInt(0) ;
	si->FindVariable(SIP_S_END)->SetInt(100) ;
	si->FindVariable(SIP_S_LOOP)->SetInt(0) ;
	si->FindVariable(SIP_S_WARP)->SetInt(SW_OFF) ;
	si->FindVariable(SIP_S_TRANSP)->SetInt(0) ;
	si->FindVariable(SIP_S_DETUNE)->SetInt(0) ;
	si->FindVariable(SIP_S_VOICES)->SetInt(2) ;
	if (c3) {
		si->FindVariable(SIP_S_ATK)->SetInt(clampi(c3->attackMs,0,5000)) ;
		si->FindVariable(SIP_S_DEC)->SetInt(clampi(c3->holdMs+c3->decayMs,0,10000)) ;
		si->FindVariable(SIP_S_SUS)->SetInt(clampi(c3->sustainDb,-60,0)) ;
		si->FindVariable(SIP_S_REL)->SetInt(clampi(c3->releaseMs,0,10000)) ;
		// la atenuación del .sf2, como la aplican los reproductores comunes (x0.4)
		si->FindVariable(SIP_S_VOL)->SetInt(clampi(-0.4f*c3->attenDb,-36,6)) ;
		// filtro pasa-bajos y su envolvente (muchos .sf2 dependen de él: tapa
		// el zumbido de loops muy cortos, como en la marimba)
		si->FindVariable(SIP_S_FVER)->SetInt(1) ;
		si->FindVariable(SIP_S_FEVER)->SetInt(1) ;
		si->FindVariable(SIP_S_FTYPE)->SetInt(0) ;   // LP
		si->FindVariable(SIP_S_FHZ)->SetInt(clampi(c3->filterHz,30,22000)) ;
		si->FindVariable(SIP_S_FQ)->SetInt(clampi(c3->filterQDb*4.0f,0,100)) ;   // 25 dB -> 100 %
		si->FindVariable(SIP_S_FST)->SetInt(clampi(c3->filterEnvSt,-72,72)) ;
		si->FindVariable(SIP_S_FATK)->SetInt(clampi(c3->fAttackMs,0,5000)) ;
		si->FindVariable(SIP_S_FDEC)->SetInt(clampi(c3->fDecayMs,0,10000)) ;
		si->FindVariable(SIP_S_FSUS)->SetInt(clampi(c3->fSustainDb,-60,0)) ;
		si->FindVariable(SIP_S_FREL)->SetInt(clampi(c3->fReleaseMs,0,10000)) ;
		si->FindVariable(SIP_S_FKEY)->SetInt(0) ;
		si->FindVariable(SIP_S_FVEL)->SetInt(0) ;
	}
	int idx=pool->IndexOf(firstName.c_str()) ;
	si->SetZones(list) ;
	si->FindVariable(SIP_SAMPLE)->SetInt(idx) ;   // el de la primera zona: activa las zonas
	si->SetZones(list) ;
	message=preset.name ;
	Trace::Log("SF2","%s: %d zonas: %s",preset.name.c_str(),(int)zones.size(),list.c_str()) ;
	return si->IsZoned() ;
}

bool SF2ImportRaw(const char *sf2Path,int phdrIndex,SampleInstrument *si,std::string &message) {
	SF2File sf ;
	if (!sf.Open(sf2Path)) { message="no pude leer el .sf2" ; return false ; }
	for (size_t i=0;i<sf.Presets().size();i++) {
		if (sf.Presets()[i].index==phdrIndex) return SF2Import(sf2Path,int(i),si,message) ;
	}
	message="no existe ese instrumento" ;
	return false ;
}

bool SF2PreviewNote(const char *sf2Path,int presetIndex,int note,const char *wavPath) {
	SF2File sf ;
	if (!sf.Open(sf2Path)) return false ;
	if (presetIndex<0 || presetIndex>=(int)sf.Presets().size()) return false ;
	std::vector<SF2Zone> zones ;
	if (!sf.Zones(sf.Presets()[presetIndex].index,zones)) return false ;
	const SF2Zone *z=zoneFor(zones,note) ;
	if (!z) return false ;
	std::vector<short> frames ;
	int ch=1 ;
	if (!sf.ReadZone(*z,frames,ch)) return false ;
	return writeWav(wavPath,frames,ch,z->rate) ;
}
