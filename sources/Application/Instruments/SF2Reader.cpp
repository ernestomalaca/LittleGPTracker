#include "SF2Reader.h"
#include "System/FileSystem/FileSystem.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>

// trk2als: ver SF2Reader.h. Formato: RIFF 'sfbk' con LIST INFO, LIST sdta
// (smpl: audio de 16 bits) y LIST pdta (presets, instrumentos, zonas y
// cabeceras de samples, registros de tamaño fijo).

// generadores que usamos (numeración de la especificación SoundFont 2.01)
enum {
	G_START=0, G_END=1, G_STARTLOOP=2, G_ENDLOOP=3, G_STARTCOARSE=4,
	G_ENDCOARSE=12, G_PAN=17, G_ATTACK=34, G_HOLD=35, G_DECAY=36,
	G_SUSTAIN=37, G_RELEASE=38, G_INSTRUMENT=41, G_KEYRANGE=43,
	G_VELRANGE=44, G_STARTLOOPCOARSE=45, G_ATTEN=48, G_ENDLOOPCOARSE=50,
	G_COARSE=51, G_FINE=52, G_SAMPLEID=53, G_SAMPLEMODES=54, G_ROOTKEY=58,
	G_COUNT=61
} ;

struct ByBank {
	bool operator()(const SF2Preset &a,const SF2Preset &b) const {
		return (a.bank!=b.bank)?a.bank<b.bank:a.program<b.program ;
	}
} ;

struct Cand { SF2Zone z ; int velLo,velHi ; } ;

static unsigned u16(const unsigned char *p) { return p[0]|(p[1]<<8) ; }
static unsigned u32(const unsigned char *p) { return p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned)p[3]<<24) ; }

SF2File::SF2File():smplPos_(-1),smplSize_(0) {
}

SF2File::~SF2File() {
}

bool SF2File::readChunk(long pos,long size,std::vector<unsigned char> &out) {
	I_File *f=FileSystem::GetInstance()->Open(path_.c_str(),"r") ;
	if (!f) return false ;
	out.resize(size) ;
	f->Seek(pos,SEEK_SET) ;
	int got=size?f->Read(&out[0],1,(int)size):0 ;
	f->Close() ;
	delete f ;
	return got==size ;
}

bool SF2File::Open(const char *path) {
	path_=path ;
	presets_.clear() ;
	I_File *f=FileSystem::GetInstance()->Open(path,"r") ;
	if (!f) return false ;
	f->Seek(0,SEEK_END) ;
	long fileSize=f->Tell() ;
	f->Seek(0,SEEK_SET) ;
	unsigned char head[12] ;
	if (f->Read(head,1,12)!=12 || memcmp(head,"RIFF",4) || memcmp(head+8,"sfbk",4)) {
		f->Close() ; delete f ;
		return false ;
	}
	// recorrer los LIST de primer nivel y los chunks de sdta y pdta
	long pos=12 ;
	struct Want { const char *id ; std::vector<unsigned char> *dst ; } ;
	Want wants[]={{"phdr",&phdr_},{"pbag",&pbag_},{"pgen",&pgen_},{"inst",&inst_},
	              {"ibag",&ibag_},{"igen",&igen_},{"shdr",&shdr_}} ;
	while (pos+8<=fileSize) {
		unsigned char ch[12] ;
		f->Seek(pos,SEEK_SET) ;
		if (f->Read(ch,1,8)!=8) break ;
		long size=(long)u32(ch+4) ;
		if (!memcmp(ch,"LIST",4)) {
			if (f->Read(ch+8,1,4)!=4) break ;
			long sub=pos+12, end=pos+8+size ;
			while (sub+8<=end) {
				unsigned char sc[8] ;
				f->Seek(sub,SEEK_SET) ;
				if (f->Read(sc,1,8)!=8) break ;
				long ssize=(long)u32(sc+4) ;
				if (!memcmp(sc,"smpl",4)) {
					smplPos_=sub+8 ;
					smplSize_=ssize ;
				}
				for (unsigned w=0;w<sizeof(wants)/sizeof(wants[0]);w++) {
					if (!memcmp(sc,wants[w].id,4)) {
						wants[w].dst->resize(ssize) ;
						if (ssize) f->Read(&(*wants[w].dst)[0],1,(int)ssize) ;
					}
				}
				sub+=8+ssize+(ssize&1) ;
			}
		}
		pos+=8+size+(size&1) ;
	}
	f->Close() ;
	delete f ;
	if (smplPos_<0 || phdr_.size()<38*2 || shdr_.size()<46 || inst_.size()<22*2) return false ;
	// el último registro de cada tabla es el terminador (EOP / EOI / EOS)
	int count=int(phdr_.size()/38)-1 ;
	for (int i=0;i<count;i++) {
		const unsigned char *r=&phdr_[i*38] ;
		SF2Preset p ;
		char name[21] ;
		memcpy(name,r,20) ;
		name[20]=0 ;
		p.name=name ;
		while (!p.name.empty() && (p.name[p.name.size()-1]==' ')) p.name.erase(p.name.size()-1) ;
		p.program=u16(r+20) ;
		p.bank=u16(r+22) ;
		p.index=i ;
		presets_.push_back(p) ;
	}
	std::sort(presets_.begin(),presets_.end(),ByBank()) ;
	return true ;
}

std::string SF2File::SampleName(int sample) {
	if (sample<0 || (size_t)(sample+1)*46>shdr_.size()) return "" ;
	char name[21] ;
	memcpy(name,&shdr_[sample*46],20) ;
	name[20]=0 ;
	return name ;
}

// valores de una zona: los que trae (y si los trae)
struct GenSet {
	short v[G_COUNT] ;
	bool has[G_COUNT] ;
	GenSet() { memset(v,0,sizeof(v)) ; memset(has,0,sizeof(has)) ; }
	void set(unsigned oper,unsigned short amount) {
		if (oper<G_COUNT) { v[oper]=(short)amount ; has[oper]=true ; }
	}
	int lo(int g) const { return has[g]?(v[g]&0xFF):0 ; }
	int hi(int g) const { return has[g]?((unsigned short)v[g]>>8):127 ; }
} ;

static void readGens(const std::vector<unsigned char> &gens,int from,int to,GenSet &out) {
	for (int g=from;g<to && (size_t)(g+1)*4<=gens.size();g++) {
		out.set(u16(&gens[g*4]),(unsigned short)u16(&gens[g*4+2])) ;
	}
}

static float tcToMs(int tc) {   // timecents -> ms
	if (tc<=-12000) return 0 ;
	return 1000.0f*powf(2.0f,tc/1200.0f) ;
}

bool SF2File::Zones(int presetIndex,std::vector<SF2Zone> &out) {
	out.clear() ;
	int presetCount=int(phdr_.size()/38)-1 ;
	if (presetIndex<0 || presetIndex>=presetCount) return false ;
	const int velocity=100 ;   // la capa que se toma (forte medio)
	int pb0=u16(&phdr_[presetIndex*38+24]), pb1=u16(&phdr_[(presetIndex+1)*38+24]) ;
	int nPbag=int(pbag_.size()/4) ;
	GenSet pGlobal ;
	std::vector<Cand> cands ;
	for (int b=pb0;b<pb1 && b+1<nPbag;b++) {
		GenSet pz ;
		readGens(pgen_,u16(&pbag_[b*4]),u16(&pbag_[(b+1)*4]),pz) ;
		if (!pz.has[G_INSTRUMENT]) {
			if (b==pb0) pGlobal=pz ;   // zona global del preset
			continue ;
		}
		int instr=(unsigned short)pz.v[G_INSTRUMENT] ;
		if ((size_t)(instr+2)*22>inst_.size()) continue ;
		// lo del preset se suma a lo del instrumento (afinación, volumen, envolvente)
		GenSet padd=pGlobal ;
		for (int g=0;g<G_COUNT;g++) if (pz.has[g]) { padd.v[g]=pz.v[g] ; padd.has[g]=true ; }
		int pKeyLo=padd.lo(G_KEYRANGE), pKeyHi=padd.hi(G_KEYRANGE) ;
		int pVelLo=padd.lo(G_VELRANGE), pVelHi=padd.hi(G_VELRANGE) ;
		int ib0=u16(&inst_[instr*22+20]), ib1=u16(&inst_[(instr+1)*22+20]) ;
		int nIbag=int(ibag_.size()/4) ;
		GenSet iGlobal ;
		for (int ib=ib0;ib<ib1 && ib+1<nIbag;ib++) {
			GenSet iz=iGlobal ;
			GenSet own ;
			readGens(igen_,u16(&ibag_[ib*4]),u16(&ibag_[(ib+1)*4]),own) ;
			if (!own.has[G_SAMPLEID]) {
				if (ib==ib0) iGlobal=own ;   // zona global del instrumento
				continue ;
			}
			for (int g=0;g<G_COUNT;g++) if (own.has[g]) { iz.v[g]=own.v[g] ; iz.has[g]=true ; }
			int s=(unsigned short)iz.v[G_SAMPLEID] ;
			if ((size_t)(s+2)*46>shdr_.size()) continue ;
			const unsigned char *h=&shdr_[s*46] ;
			int type=u16(h+44) ;
			if (type&0x8000) continue ;   // samples en ROM: no hay audio
			SF2Zone z ;
			z.lo=std::max(iz.lo(G_KEYRANGE),pKeyLo) ;
			z.hi=std::min(iz.hi(G_KEYRANGE),pKeyHi) ;
			if (z.lo>z.hi) continue ;
			int velLo=std::max(iz.lo(G_VELRANGE),pVelLo), velHi=std::min(iz.hi(G_VELRANGE),pVelHi) ;
			if (velLo>velHi) continue ;
			int origPitch=h[40] ;
			signed char corr=(signed char)h[41] ;
			z.root=iz.has[G_ROOTKEY] && iz.v[G_ROOTKEY]>=0 ? iz.v[G_ROOTKEY] : (origPitch<=127?origPitch:60) ;
			float coarse=float(iz.v[G_COARSE]+(padd.has[G_COARSE]?padd.v[G_COARSE]:0)) ;
			float fine=float(iz.v[G_FINE]+(padd.has[G_FINE]?padd.v[G_FINE]:0)) ;
			z.cents=coarse*100+fine+corr ;
			z.sample=s ;
			z.link=-1 ;
			long sStart=(long)u32(h+20), sEnd=(long)u32(h+24) ;
			long lStart=(long)u32(h+28), lEnd=(long)u32(h+32) ;
			sStart+=iz.v[G_START]+32768L*iz.v[G_STARTCOARSE] ;
			sEnd+=iz.v[G_END]+32768L*iz.v[G_ENDCOARSE] ;
			lStart+=iz.v[G_STARTLOOP]+32768L*iz.v[G_STARTLOOPCOARSE] ;
			lEnd+=iz.v[G_ENDLOOP]+32768L*iz.v[G_ENDLOOPCOARSE] ;
			if (sEnd<=sStart) continue ;
			z.start=int(sStart) ;
			z.end=int(sEnd) ;
			z.loopStart=int(lStart-sStart) ;
			z.loopEnd=int(lEnd-sStart) ;
			int modes=iz.v[G_SAMPLEMODES]&3 ;
			z.loop=(modes==1 || modes==3) && z.loopEnd>z.loopStart && z.loopStart>=0 && z.loopEnd<=z.end-z.start ;
			z.rate=int(u32(h+36)) ;
			if (z.rate<=0) z.rate=44100 ;
			float atten=float(iz.v[G_ATTEN]+(padd.has[G_ATTEN]?padd.v[G_ATTEN]:0)) ;
			z.attenDb=atten/10.0f ;
			// envolvente: timecents (por defecto -12000 = nada) más lo del preset
			int tc[4]={G_ATTACK,G_HOLD,G_DECAY,G_RELEASE} ;
			float ms[4] ;
			for (int k=0;k<4;k++) {
				int t=iz.has[tc[k]]?iz.v[tc[k]]:-12000 ;
				if (padd.has[tc[k]]) t+=padd.v[tc[k]] ;
				ms[k]=tcToMs(t) ;
			}
			z.attackMs=ms[0] ; z.holdMs=ms[1] ; z.decayMs=ms[2] ; z.releaseMs=ms[3] ;
			int sus=iz.v[G_SUSTAIN]+(padd.has[G_SUSTAIN]?padd.v[G_SUSTAIN]:0) ;
			z.sustainDb=-sus/10.0f ;
			Cand c ;
			c.z=z ;
			c.velLo=velLo ;
			c.velHi=velHi ;
			cands.push_back(c) ;
			(void)type ;
		}
	}
	if (cands.empty()) return false ;
	// estéreo: un canal derecho con su izquierdo en las mismas teclas se junta
	std::vector<bool> used(cands.size(),false) ;
	for (size_t i=0;i<cands.size();i++) {
		int type=u16(&shdr_[cands[i].z.sample*46+44]) ;
		if (!(type&4)) continue ;   // solo los izquierdos buscan pareja
		int link=u16(&shdr_[cands[i].z.sample*46+42]) ;
		for (size_t j=0;j<cands.size();j++) {
			if (j==i || used[j] || cands[j].z.sample!=link) continue ;
			if (cands[j].z.lo!=cands[i].z.lo || cands[j].z.hi!=cands[i].z.hi) continue ;
			if (cands[j].velLo!=cands[i].velLo) continue ;
			if (cands[j].z.end-cands[j].z.start!=cands[i].z.end-cands[i].z.start) continue ;
			cands[i].z.link=link ;
			used[j]=true ;
			break ;
		}
	}
	// por cada tecla, la zona de la capa más cercana a 'velocity'
	int chosen[128] ;
	for (int k=0;k<128;k++) {
		chosen[k]=-1 ;
		int best=1000 ;
		for (size_t i=0;i<cands.size();i++) {
			if (used[i]) continue ;   // canal derecho ya juntado con su izquierdo
			if (k<cands[i].z.lo || k>cands[i].z.hi) continue ;
			int d=(velocity<cands[i].velLo)?cands[i].velLo-velocity:
			      (velocity>cands[i].velHi)?velocity-cands[i].velHi:0 ;
			if (d<best) { best=d ; chosen[k]=int(i) ; }
		}
	}
	// teclas seguidas con la misma zona = una zona
	for (int k=0;k<128;) {
		if (chosen[k]<0) { k++ ; continue ; }
		int c=chosen[k], e=k ;
		while (e+1<128 && chosen[e+1]==c) e++ ;
		SF2Zone z=cands[c].z ;
		z.lo=k ;
		z.hi=e ;
		out.push_back(z) ;
		k=e+1 ;
	}
	return !out.empty() ;
}

bool SF2File::readSamples(int sample,int start,int end,std::vector<short> &out) {
	(void)sample ;
	long pos=smplPos_+2L*start, size=2L*(end-start) ;
	if (start<0 || end<=start || 2L*end>smplSize_) return false ;
	std::vector<unsigned char> raw ;
	if (!readChunk(pos,size,raw)) return false ;
	out.resize(end-start) ;
	for (int i=0;i<end-start;i++) out[i]=(short)u16(&raw[i*2]) ;
	return true ;
}

bool SF2File::ReadZone(const SF2Zone &z,std::vector<short> &frames,int &channels) {
	std::vector<short> left ;
	if (!readSamples(z.sample,z.start,z.end,left)) return false ;
	channels=1 ;
	if (z.link>=0) {
		const unsigned char *h=&shdr_[z.link*46] ;
		int rs=int(u32(h+20)) ;
		std::vector<short> right ;
		if (readSamples(z.link,rs,rs+(z.end-z.start),right)) {
			channels=2 ;
			frames.resize(left.size()*2) ;
			for (size_t i=0;i<left.size();i++) {
				frames[i*2]=left[i] ;
				frames[i*2+1]=right[i] ;
			}
			return true ;
		}
	}
	frames.swap(left) ;
	return true ;
}
