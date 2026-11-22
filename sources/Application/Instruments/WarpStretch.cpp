#include "WarpStretch.h"
#include "System/System/System.h"
#include <stddef.h>

#if __cplusplus >= 201103L
#include "signalsmith-stretch/signalsmith-stretch.h"
typedef signalsmith::stretch::SignalsmithStretch<float> Stretch ;
#define IMPL ((Stretch *)impl_)

WarpStretch::WarpStretch():impl_(new Stretch()),channels_(0) {}
WarpStretch::~WarpStretch() { delete IMPL ; }
bool WarpStretch::Available() { return true ; }

static int quality_=0 ;
static bool split_=true ;
void WarpStretch::SetQuality(int quality,bool split) { quality_=quality ; split_=split ; }

void WarpStretch::Configure(int channels,int sampleRate) {
	if (quality_==1) IMPL->presetCheaper(channels,(float)sampleRate,split_) ;
	else IMPL->presetDefault(channels,(float)sampleRate,split_) ;
	channels_=channels ;
}
void WarpStretch::Reset() { IMPL->reset() ; }
void WarpStretch::SetTranspose(float factor) { IMPL->setTransposeFactor(factor) ; }
int WarpStretch::InputLatency() { return IMPL->inputLatency() ; }
int WarpStretch::OutputLatency() { return IMPL->outputLatency() ; }
void WarpStretch::Seek(float **in,int inSamples,double rate) { IMPL->seek(in,inSamples,rate) ; }
void WarpStretch::Process(float **in,int inSamples,float **out,int outSamples) {
	IMPL->process(in,inSamples,out,outSamples) ;
}

/************************************************************
 trk2als: pre-render del warp complex (ver WarpStretch.h)
 ************************************************************/
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <deque>
#include <cmath>
#include <cstring>
#include <cstdio>
#ifdef __linux__
#include <sys/resource.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <cstdlib>
#include <chrono>
#include <functional>
#include <set>
#include <map>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

namespace {

// ms desde que arrancó (para empatar el log con lo que se oye)
long warpMs() { return (long)System::GetInstance()->GetClock() ; }

struct Entry {
	WarpCacheKey key ;
	unsigned fingerprint ;
	std::vector<short> src ;    // copia del pedazo del sample, mientras se estira
	int srcLo ;                 // cuadro del sample donde empieza 'src'
	std::vector<short> frames ;
	int length ;
	int loopStart ;
	std::atomic<int> refs ;
	unsigned long lastUse ;
	bool ready ;
	bool failed ;               // no cabe o no aplica: no volver a intentarlo
	bool diskTried ;            // ya se buscó en la tarjeta
	std::string diskMine ;      // apartado en la tarjeta para guardarlo al terminar
	Entry():srcLo(0),length(0),loopStart(-1),refs(0),lastUse(0),ready(false),failed(false),diskTried(false) {}
} ;

std::mutex mtx ;
std::condition_variable cv ;
std::vector<Entry *> entries ;
std::deque<Entry *> pending ;
size_t bytes=0 ;
size_t budget=size_t(256)*1024*1024 ;
unsigned long useClock=0 ;
bool enabled=true ;
bool workerStarted=false ;

// unas pocas muestras repartidas: distingue un sample de otro que quedó en
// la misma dirección de memoria
unsigned fingerprintOf(const WarpCacheKey &k) {
	unsigned h=2166136261u ;
	int len=k.loopEnd-k.loopStart ;
	for (int i=0;i<16;i++) {
		int f=k.loopStart+int((long long)len*i/16) ;
		for (int c=0;c<k.channels;c++) {
			h^=(unsigned short)k.data[f*k.channels+c] ;
			h*=16777619u ;
		}
	}
	return h ;
}

bool sameKey(const Entry *e,const WarpCacheKey &k,unsigned fp) {
	const WarpCacheKey &a=e->key ;
	return a.data==k.data && a.channels==k.channels && a.start==k.start &&
	       a.loopStart==k.loopStart && a.loopEnd==k.loopEnd && a.simpler==k.simpler &&
	       a.loop==k.loop && a.speed==k.speed && a.transpose==k.transpose &&
	       e->fingerprint==fp ;
}

// suelta lo menos usado (que ninguna voz esté leyendo) hasta caber
void evictLocked(Entry *keep) {
	while (bytes>budget) {
		Entry *victim=0 ;
		size_t vi=0 ;
		for (size_t i=0;i<entries.size();i++) {
			Entry *e=entries[i] ;
			if (e==keep || !e->ready || e->refs.load()>0) continue ;
			if (!victim || e->lastUse<victim->lastUse) { victim=e ; vi=i ; }
		}
		if (!victim) return ;
		bytes-=victim->frames.size()*sizeof(short) ;
		entries.erase(entries.begin()+vi) ;
		delete victim ;
	}
}

// lo mismo que hace la voz en tiempo real (SampleInstrument::renderComplex),
// pero de una vez y fuera del hilo de audio. 'src' es el pedazo del sample
// que empieza en el cuadro 'srcLo'. progress(hecho) se llama en cada bloque;
// si devuelve false, se deja
bool stretchCore(const WarpCacheKey &k,const short *src,int srcLo,std::vector<short> &out,
                 long long &total,long long &loopAt,const std::function<bool(long long)> &progress) {
	const int ch=k.channels ;
	Stretch st ;
	if (quality_==1) st.presetCheaper(ch,44100.0f,split_) ;
	else st.presetDefault(ch,44100.0f,split_) ;
	st.setTransposeFactor(k.transpose) ;

	double first=(k.loopEnd-k.start)/k.speed ;      // cuadros de salida hasta el fin del loop
	loopAt=-1 ;
	if (k.loop) {
		loopAt=llround(first) ;
		total=loopAt+llround((k.loopEnd-k.loopStart)/k.speed) ;
	} else {
		total=llround(first) ;
	}
	if (total<=0 || loopAt==total || (unsigned long long)total*ch*sizeof(short)>budget/2) return false ;

	std::vector<float> in0,in1,out0(512),out1(512) ;
	int pos=k.start ;
	bool once=k.simpler && !k.loop ;
	// como SampleInstrument::readLoop, pero sobre la copia
	auto readLoop=[&](int n) {
		if ((int)in0.size()<n) { in0.resize(n) ; in1.resize(n) ; }
		for (int i=0;i<n;i++) {
			if (k.simpler) {
				if (pos>=k.loopEnd) {
					if (once) { in0[i]=in1[i]=0 ; pos++ ; continue ; }
					pos=k.loopStart ;
				}
			} else if (pos>=k.loopEnd || pos<k.loopStart) pos=k.loopStart ;
			const short *f=&src[(pos-srcLo)*ch] ;
			in0[i]=f[0]/32768.0f ;
			if (ch>1) in1[i]=f[1]/32768.0f ;
			pos++ ;
		}
	} ;
	float *ins[2], *outs[2]={&out0[0],&out1[0]} ;
	double acc=0 ;
	st.reset() ;
	int pre=st.inputLatency() ;
	readLoop(pre) ;
	ins[0]=in0.empty()?0:&in0[0] ; ins[1]=in1.empty()?0:&in1[0] ;
	st.seek(ins,pre,k.speed) ;
	int skip=st.outputLatency() ;
	while (skip>0) {
		int n=skip<512?skip:512 ;
		acc+=n*k.speed ;
		int nin=int(acc) ;
		acc-=nin ;
		readLoop(nin) ;
		ins[0]=in0.empty()?0:&in0[0] ; ins[1]=in1.empty()?0:&in1[0] ;
		st.process(ins,nin,outs,n) ;
		skip-=n ;
	}
	out.assign(size_t(total)*ch,0) ;
	for (long long done=0;done<total;) {
		if (progress && !progress(done)) return false ;
		int n=int(total-done<512?total-done:512) ;
		acc+=n*k.speed ;
		int nin=int(acc) ;
		acc-=nin ;
		readLoop(nin) ;
		ins[0]=in0.empty()?0:&in0[0] ; ins[1]=in1.empty()?0:&in1[0] ;
		st.process(ins,nin,outs,n) ;
		for (int i=0;i<n;i++) {
			for (int c=0;c<ch;c++) {
				float v=outs[c][i]*32768.0f ;
				if (v>32767.0f) v=32767.0f ;
				if (v<-32768.0f) v=-32768.0f ;
				out[(done+i)*ch+c]=short(v) ;
			}
		}
		done+=n ;
	}
	if (progress) progress(total) ;
	return true ;
}

/************************************************************
 trk2als: el pre-render en la tarjeta
 ************************************************************/

std::string diskDir ;
size_t diskMax=0 ;
std::atomic<int> diskGen(0) ;
std::mutex diskMtx ;                     // archivos a medio hacer + el índice
std::condition_variable diskCv ;
std::set<std::string> inflight ;         // nombres que alguien está estirando
std::map<std::string,std::string> indexMap ;   // "tempo|tamaño|ruta" -> nombre
bool indexLoaded=false ;

unsigned long long fnv64(const void *p,size_t n,unsigned long long h=1469598103934665603ULL) {
	const unsigned char *b=(const unsigned char *)p ;
	for (size_t i=0;i<n;i++) { h^=b[i] ; h*=1099511628211ULL ; }
	return h ;
}

// el contenido del pedazo de sample (rápido: palabras de 64 bits)
unsigned long long contentHash(const short *p,size_t shorts) {
	unsigned long long h=1469598103934665603ULL ;
	size_t words=shorts/4 ;
	const unsigned char *b=(const unsigned char *)p ;
	for (size_t i=0;i<words;i++) {
		unsigned long long w ;
		memcpy(&w,b+i*8,8) ;
		h^=w ; h*=1099511628211ULL ; h^=h>>29 ;
	}
	return fnv64(p+words*4,(shorts-words*4)*sizeof(short),h) ;
}

// el nombre del archivo: el contenido, la región y cómo se estira
std::string diskName(const WarpCacheKey &k,unsigned long long content,int srcLo,long long speedQ) {
	long long f[10]={(long long)content,k.channels,k.start-srcLo,k.loopStart-srcLo,k.loopEnd-srcLo,
	                 k.simpler,k.loop,speedQ,llround(k.transpose*10000.0),quality_} ;
	char b[40] ;
	snprintf(b,sizeof(b),"%016llx.cw",fnv64(f,sizeof(f))) ;
	return b ;
}

// la velocidad en escalones de 0.05% (el reproductor redondea el compás a
// ticks enteros: la misma velocidad sale distinta por ~0.1%)
long long speedQ(double speed) { return llround(log(speed)/log(1.0005)) ; }

struct DiskHdr { char magic[4] ; int ch ; long long length ; long long loopAt ; } ;

bool diskLoad(const std::string &name,int ch,std::vector<short> &frames,long long &length,long long &loopAt) {
	if (diskDir.empty()) return false ;
	std::string path=diskDir+"/"+name ;
	FILE *f=fopen(path.c_str(),"rb") ;
	if (!f) return false ;
	DiskHdr h ;
	bool ok=fread(&h,sizeof(h),1,f)==1 && !memcmp(h.magic,"CW01",4) && h.ch==ch && h.length>0 &&
	        (unsigned long long)h.length*ch*sizeof(short)<=budget/2 ;
	if (ok) {
		frames.resize(size_t(h.length)*ch) ;
		ok=fread(&frames[0],sizeof(short)*ch,size_t(h.length),f)==size_t(h.length) ;
	}
	fclose(f) ;
	if (!ok) { remove(path.c_str()) ; return false ; }
	length=h.length ;
	loopAt=h.loopAt ;
	utime(path.c_str(),0) ;   // lo usado al último es lo que se queda
	return true ;
}

// suelta lo menos usado hasta caber
void diskTrim() {
	DIR *d=opendir(diskDir.c_str()) ;
	if (!d) return ;
	std::vector<std::pair<time_t,std::string> > files ;
	unsigned long long total=0 ;
	while (struct dirent *de=readdir(d)) {
		std::string n=de->d_name ;
		if (n.size()<4 || n.compare(n.size()-3,3,".cw")) continue ;
		struct stat st ;
		std::string p=diskDir+"/"+n ;
		if (stat(p.c_str(),&st)) continue ;
		total+=st.st_size ;
		files.push_back(std::make_pair(st.st_mtime,p)) ;
	}
	closedir(d) ;
	if (total<=diskMax) return ;
	std::sort(files.begin(),files.end()) ;
	for (size_t i=0;i<files.size() && total>diskMax;i++) {
		struct stat st ;
		if (!stat(files[i].second.c_str(),&st)) total-=st.st_size ;
		remove(files[i].second.c_str()) ;
	}
}

void diskSave(const std::string &name,int ch,const std::vector<short> &frames,long long length,long long loopAt) {
	if (diskDir.empty()) return ;
	std::string path=diskDir+"/"+name, tmp=path+".tmp" ;
	FILE *f=fopen(tmp.c_str(),"wb") ;
	if (!f) return ;
	DiskHdr h ;
	memcpy(h.magic,"CW01",4) ;
	h.ch=ch ; h.length=length ; h.loopAt=loopAt ;
	bool ok=fwrite(&h,sizeof(h),1,f)==1 &&
	        fwrite(&frames[0],sizeof(short)*ch,size_t(length),f)==size_t(length) ;
	ok=(fclose(f)==0) && ok ;
	if (!ok || rename(tmp.c_str(),path.c_str())) { remove(tmp.c_str()) ; return ; }
	diskTrim() ;
	diskGen++ ;
}

// lo busca en la tarjeta (también hasta 0.15% de velocidad de distancia: no
// se oye, y es lo que ya acepta WarpCache::Matches). Si alguien lo está estirando, espera a que termine. Si no está,
// queda apartado a nombre de quien llama (soltarlo con diskDone)
bool diskFind(const WarpCacheKey &k,unsigned long long content,int srcLo,std::vector<short> &frames,
              long long &length,long long &loopAt,std::string &mine,std::string *hit=0) {
	mine.clear() ;
	if (diskDir.empty()) return false ;
	long long q=speedQ(k.speed) ;
	std::string own=diskName(k,content,srcLo,q) ;
	{
		std::unique_lock<std::mutex> lk(diskMtx) ;
		diskCv.wait(lk,[&]{ return !inflight.count(own) ; }) ;
		inflight.insert(own) ;
	}
	for (int d=0;d<7;d++) {
		long long qq=q+((d+1)/2)*(d%2?-1:1) ;
		std::string nm=d?diskName(k,content,srcLo,qq):own ;
		if (diskLoad(nm,k.channels,frames,length,loopAt)) {
			if (hit) *hit=nm ;
			std::lock_guard<std::mutex> lk(diskMtx) ;
			inflight.erase(own) ;
			diskCv.notify_all() ;
			return true ;
		}
	}
	mine=own ;
	return false ;
}

void diskDone(const std::string &mine) {
	if (mine.empty()) return ;
	std::lock_guard<std::mutex> lk(diskMtx) ;
	inflight.erase(mine) ;
	diskCv.notify_all() ;
}

std::deque<Entry *> diskQueue ;    // por buscar en la tarjeta (antes de estirar)
std::condition_variable diskQCv ;

void finish(Entry *e,std::vector<short> &frames,long long total,long long loopAt,bool fromDisk,
            std::chrono::steady_clock::time_point t0) {
	const WarpCacheKey &k=e->key ;
	std::lock_guard<std::mutex> lk(mtx) ;
	e->frames.swap(frames) ;
	e->length=int(total) ;
	e->loopStart=int(loopAt) ;
	bytes+=e->frames.size()*sizeof(short) ;
	e->ready=true ;
	evictLocked(e) ;
	if (getenv("TRK_LOADLOG")) fprintf(stderr,"[WARP] %ld listo %p sp %.5f en %ld ms (%lld cuadros)%s\n",warpMs(),(void*)k.data,k.speed,
	                 (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count(),total,
	                 fromDisk?" del disco":"") ;
}

void build(Entry *e) {
	const WarpCacheKey &k=e->key ;
	bool dbg=getenv("TRK_LOADLOG")!=0 ;
	auto t0=std::chrono::steady_clock::now() ;
	std::vector<short> frames ;
	long long total=0, loopAt=-1 ;
	std::string mine=e->diskMine ;
	bool fromDisk=false ;
	if (!e->diskTried) {
		unsigned long long content=contentHash(&e->src[0],e->src.size()) ;
		fromDisk=diskFind(k,content,e->srcLo,frames,total,loopAt,mine) ;
	}
	if (!fromDisk) {
		if (dbg) fprintf(stderr,"[WARP] %ld estirando %p st %d loop %d-%d sp %.5f tr %.4f loop %d\n",warpMs(),(void*)k.data,k.start,k.loopStart,k.loopEnd,k.speed,k.transpose,k.loop) ;
		if (!stretchCore(k,&e->src[0],e->srcLo,frames,total,loopAt,std::function<bool(long long)>())) {
			diskDone(mine) ;
			e->failed=true ;
			return ;
		}
		diskSave(mine,k.channels,frames,total,loopAt) ;
		diskDone(mine) ;
	}
	finish(e,frames,total,loopAt,fromDisk,t0) ;
}

// trk2als: los hilos de estirado corren solo con lo que sobra del procesador
// (SCHED_IDLE): nunca le quitan el turno al audio ni a la interfaz
void idleThread() {
#ifdef __linux__
	struct sched_param sp ;
	sp.sched_priority=0 ;
	if (sched_setscheduler(0,SCHED_IDLE,&sp)!=0)
		setpriority(PRIO_PROCESS,(id_t)syscall(SYS_gettid),19) ;
#endif
}

void worker() {
	idleThread() ;
	for (;;) {
		Entry *e ;
		{
			std::unique_lock<std::mutex> lk(mtx) ;
			cv.wait(lk,[]{ return !pending.empty() ; }) ;
			e=pending.front() ;
			pending.pop_front() ;
		}
		build(e) ;
		std::lock_guard<std::mutex> lk(mtx) ;
		std::vector<short>().swap(e->src) ;
	}
}

// trk2als: primero la tarjeta, en su propio hilo: lo que ya estaba preparado
// no espera detrás de lo que se está estirando
void diskLoader() {
	for (;;) {
		Entry *e ;
		{
			std::unique_lock<std::mutex> lk(mtx) ;
			diskQCv.wait(lk,[]{ return !diskQueue.empty() ; }) ;
			e=diskQueue.front() ;
			diskQueue.pop_front() ;
		}
		auto t0=std::chrono::steady_clock::now() ;
		std::vector<short> frames ;
		long long total=0, loopAt=-1 ;
		std::string mine ;
		unsigned long long content=contentHash(&e->src[0],e->src.size()) ;
		if (diskFind(e->key,content,e->srcLo,frames,total,loopAt,mine)) {
			finish(e,frames,total,loopAt,true,t0) ;
			std::lock_guard<std::mutex> lk(mtx) ;
			std::vector<short>().swap(e->src) ;
			continue ;
		}
		std::lock_guard<std::mutex> lk(mtx) ;
		e->diskMine=mine ;
		e->diskTried=true ;
		pending.push_back(e) ;
		cv.notify_one() ;
	}
}

} // namespace

void *WarpCache::AcquireAny(const WarpCacheKey &k) {
	if (!enabled || !k.data) return 0 ;
	unsigned fp=fingerprintOf(k) ;
	std::lock_guard<std::mutex> lk(mtx) ;
	Entry *best=0 ;
	double bd=0 ;
	for (size_t i=0;i<entries.size();i++) {
		Entry *e=entries[i] ;
		const WarpCacheKey &a=e->key ;
		if (!e->ready || a.data!=k.data || a.channels!=k.channels || a.start!=k.start ||
		    a.loopStart!=k.loopStart || a.loopEnd!=k.loopEnd || a.loop!=k.loop || e->fingerprint!=fp) continue ;
		double d=a.speed-k.speed ;
		if (d<0) d=-d ;
		if (!best || d<bd) { best=e ; bd=d ; }
	}
	if (best) { best->refs++ ; best->lastUse=++useClock ; }
	return best ;
}

bool WarpCache::Building(const WarpCacheKey &k) {
	if (!enabled || !k.data) return false ;
	unsigned fp=fingerprintOf(k) ;
	std::lock_guard<std::mutex> lk(mtx) ;
	for (size_t i=0;i<entries.size();i++) {
		Entry *e=entries[i] ;
		if (sameKey(e,k,fp)) return !e->ready && !e->failed ;
	}
	return false ;
}

void *WarpCache::Acquire(const WarpCacheKey &k) {
	if (!enabled || !k.data || k.channels<1 || k.channels>2) return 0 ;
	if (k.loopEnd<=k.loopStart || k.speed<=0 || k.start<0 || k.start>=k.loopEnd) return 0 ;
	unsigned fp=fingerprintOf(k) ;
	std::unique_lock<std::mutex> lk(mtx) ;
	for (size_t i=0;i<entries.size();i++) {
		Entry *e=entries[i] ;
		if (!sameKey(e,k,fp)) continue ;
		if (!e->ready) return 0 ;    // estirándose, o no se pudo
		e->refs++ ;
		e->lastUse=++useClock ;
		return e ;
	}
	// nuevo: copiar el pedazo del sample (el original puede cambiar mientras se estira)
	int lo=k.simpler?(k.start<k.loopStart?k.start:k.loopStart):k.loopStart ;
	if (lo<0) lo=0 ;
	size_t n=size_t(k.loopEnd-lo)*k.channels ;
	Entry *e=new Entry() ;
	e->key=k ;
	e->fingerprint=fp ;
	e->lastUse=++useClock ;
	entries.push_back(e) ;
	if (n*sizeof(short)>budget/4) {
		e->failed=true ;    // demasiado largo para estirarlo de antemano
		return 0 ;
	}
	e->srcLo=lo ;
	e->src.assign(k.data+size_t(lo)*k.channels,k.data+size_t(lo)*k.channels+n) ;
	// trk2als: lo del mismo loop a otra velocidad que todavía no empieza ya
	// no sirve (al mover el tempo de 1 en 1 se formaban todos los de en medio)
	for (size_t i=0;i<pending.size();) {
		Entry *o=pending[i] ;
		const WarpCacheKey &a=o->key ;
		if (o!=e && a.data==k.data && a.channels==k.channels && a.start==k.start &&
		    a.loopStart==k.loopStart && a.loopEnd==k.loopEnd && a.loop==k.loop &&
		    o->fingerprint==fp && o->refs.load()==0) {
			pending.erase(pending.begin()+i) ;
			for (size_t j=0;j<entries.size();j++) if (entries[j]==o) { entries.erase(entries.begin()+j) ; break ; }
			diskDone(o->diskMine) ;   // lo apartado en la tarjeta, libre
			delete o ;
			continue ;
		}
		i++ ;
	}
	diskQueue.push_back(e) ;
	diskQCv.notify_one() ;
	if (!workerStarted) {
		std::thread(diskLoader).detach() ;
		// un hilo por núcleo libre (la RG353V tiene 4: el audio usa uno)
		workerStarted=true ;
		// la RG353V tiene 4 núcleos: uno para el audio, otro para la interfaz
		unsigned n=std::thread::hardware_concurrency() ;
		n=(n>2)?n-2:1 ;
		if (n>2) n=2 ;
		for (unsigned i=0;i<n;i++) std::thread(worker).detach() ;
	}
	return 0 ;
}

void WarpCache::AddRef(void *h) { if (h) ((Entry *)h)->refs++ ; }
void WarpCache::Release(void *h) { if (h) ((Entry *)h)->refs-- ; }
const short *WarpCache::Frames(void *h) { return &((Entry *)h)->frames[0] ; }
int WarpCache::Length(void *h) { return ((Entry *)h)->length ; }
int WarpCache::LoopStart(void *h) { return ((Entry *)h)->loopStart ; }
int WarpCache::Start(void *h) { return ((Entry *)h)->key.start ; }
double WarpCache::Speed(void *h) { return ((Entry *)h)->key.speed ; }
bool WarpCache::Matches(void *h,double speed,float transpose) {
	Entry *e=(Entry *)h ;
	// trk2als: una diferencia de menos de 0.15% no se oye: el mismo
	double d=e->key.speed-speed ;
	if (d<0) d=-d ;
	return d<=speed*0.0015 && e->key.transpose==transpose ;
}
void WarpCache::SetBudget(int mb) {
	std::lock_guard<std::mutex> lk(mtx) ;
	budget=size_t(mb>0?mb:1)*1024*1024 ;
}
void WarpCache::SetEnabled(bool on) { enabled=on ; }
bool WarpCache::Enabled() { return enabled ; }


/************************************************************
 trk2als: pre-escucha de un loop en complex
 ************************************************************/
#include "Application/Instruments/WavFile.h"
#include <fstream>

namespace {
std::atomic<int> pvGen(0), pvDone(0), pvFail(0) ;

void pvPut16(std::ofstream &f,int v) { f.put(char(v&255)) ; f.put(char((v>>8)&255)) ; }
void pvPut32(std::ofstream &f,long v) { pvPut16(f,int(v&0xFFFF)) ; pvPut16(f,int((v>>16)&0xFFFF)) ; }

bool pvWrite(const std::string &out,const short *frames,int ch,long total) {
	std::ofstream f(out.c_str(),std::ios::binary) ;
	if (!f) return false ;
	long bytes=long(total*ch*2) ;
	f.write("RIFF",4) ; pvPut32(f,36+bytes) ; f.write("WAVEfmt ",8) ;
	pvPut32(f,16) ; pvPut16(f,1) ; pvPut16(f,ch) ; pvPut32(f,44100) ;
	pvPut32(f,44100L*ch*2) ; pvPut16(f,ch*2) ; pvPut16(f,16) ;
	f.write("data",4) ; pvPut32(f,bytes) ;
	f.write((const char *)frames,bytes) ;
	f.close() ;
	return true ;
}

std::mutex pvFinish ;   // lo que se termina de preparar, de uno en uno

void indexPut(const std::string &file,int tempo,const std::string &name) ;

// la prueba: una vuelta a tempo (lo mismo que la primera vuelta del
// pre-render de un clip que empieza en el 0). Cuando ya sonó, sigue con la
// segunda vuelta y lo guarda en la tarjeta: el loop queda preparado
void pvJob(int gen,std::string in,std::string out,double ratio,float transpose,int tempo) {
	idleThread() ;
	WavFile *w=WavFile::Open(in.c_str()) ;
	long n=w?w->GetSize(-1):0 ;
	int ch=w?w->GetChannelCount(-1):0 ;
	if (!w || n<=0 || ch<1 || ch>2 || ratio<=0 || !w->GetBuffer(0,n)) {
		delete w ;
		if (pvGen.load()==gen) pvFail.store(gen) ;
		return ;
	}
	std::vector<short> src((short *)w->GetSampleBuffer(-1),(short *)w->GetSampleBuffer(-1)+n*ch) ;
	delete w ;
	if (pvGen.load()!=gen) return ;   // ya pidieron otro
	WarpCacheKey k ;
	k.data=&src[0] ; k.channels=ch ; k.start=0 ; k.loopStart=0 ; k.loopEnd=int(n) ;
	k.simpler=false ; k.loop=true ; k.speed=ratio ; k.transpose=transpose ;
	std::vector<short> frames ;
	long long total=0, loopAt=-1 ;
	unsigned long long content=contentHash(&src[0],src.size()) ;
	std::string mine, hit ;
	if (diskFind(k,content,0,frames,total,loopAt,mine,&hit)) {
		if (pvGen.load()!=gen) return ;
		if (loopAt>0 && pvWrite(out,&frames[0],ch,long(loopAt))) {
			indexPut(in,tempo,hit) ;
			if (pvGen.load()==gen) pvDone.store(gen) ;
		} else if (pvGen.load()==gen) pvFail.store(gen) ;
		return ;
	}
	bool sounded=false, finishing=false ;
	std::unique_lock<std::mutex> fin(pvFinish,std::defer_lock) ;
	auto progress=[&](long long done)->bool {
		if (!sounded) {
			if (pvGen.load()!=gen) return false ;
			if (loopAt>0 && done>=loopAt) {   // la primera vuelta: a sonar
				sounded=true ;
				if (pvWrite(out,&frames[0],ch,long(loopAt))) { if (pvGen.load()==gen) pvDone.store(gen) ; }
				else if (pvGen.load()==gen) pvFail.store(gen) ;
			}
			return true ;
		}
		if (!finishing) { finishing=true ; fin.lock() ; }   // la segunda, sin prisa
		return true ;
	} ;
	bool ok=stretchCore(k,&src[0],0,frames,total,loopAt,progress) ;
	if (ok && !sounded) {   // (sin segunda vuelta: no pasa con loop)
		if (pvGen.load()==gen) pvFail.store(gen) ;
		ok=false ;
	}
	if (ok && !mine.empty()) {
		diskSave(mine,ch,frames,total,loopAt) ;
		indexPut(in,tempo,mine) ;
	} else if (!ok && !sounded && pvGen.load()==gen) pvFail.store(gen) ;
	diskDone(mine) ;
}

// --- el índice del navegador: qué archivo quedó preparado a qué tempo ------

std::string indexKey(const std::string &file,int tempo) {
	struct stat st ;
	long long size=stat(file.c_str(),&st)?-1:(long long)st.st_size ;
	char b[48] ;
	snprintf(b,sizeof(b),"%d|%lld|",tempo,size) ;
	return b+file ;
}

void indexLoadLocked() {
	if (indexLoaded || diskDir.empty()) return ;
	indexLoaded=true ;
	std::ifstream f((diskDir+"/index.txt").c_str()) ;
	std::string line ;
	while (std::getline(f,line)) {
		size_t t=line.rfind('\t') ;
		if (t!=std::string::npos) indexMap[line.substr(0,t)]=line.substr(t+1) ;
	}
}

void indexPut(const std::string &file,int tempo,const std::string &name) {
	if (diskDir.empty() || tempo<=0 || name.empty()) return ;
	std::string key=indexKey(file,tempo) ;
	{
		std::lock_guard<std::mutex> lk(diskMtx) ;
		indexLoadLocked() ;
		if (indexMap.count(key) && indexMap[key]==name) return ;
		indexMap[key]=name ;
		if (indexMap.size()>4000) {   // muy grande: se reescribe sin lo que ya no está
			std::ofstream f((diskDir+"/index.txt").c_str(),std::ios::trunc) ;
			for (std::map<std::string,std::string>::iterator it=indexMap.begin();it!=indexMap.end();) {
				struct stat st ;
				if (stat((diskDir+"/"+it->second).c_str(),&st)) { indexMap.erase(it++) ; continue ; }
				f<<it->first<<'\t'<<it->second<<'\n' ;
				++it ;
			}
		} else {
			std::ofstream f((diskDir+"/index.txt").c_str(),std::ios::app) ;
			f<<key<<'\t'<<name<<'\n' ;
		}
	}
	diskGen++ ;
}
}

int WarpPreview::Request(const char *in,const char *out,double ratio,float transpose,int tempo) {
	int gen=++pvGen ;
	std::thread(pvJob,gen,std::string(in),std::string(out),ratio,transpose,tempo).detach() ;
	return gen ;
}
bool WarpPreview::Ready(int job) { return job>0 && pvDone.load()==job && pvGen.load()==job ; }
bool WarpPreview::Failed(int job) { return job>0 && pvFail.load()==job ; }

void WarpDisk::SetDir(const char *dir,int mb) {
	std::lock_guard<std::mutex> lk(diskMtx) ;
	if (!dir || mb<=0) { diskDir.clear() ; return ; }
	mkdir(dir,0755) ;
	struct stat st ;
	if (stat(dir,&st) || !S_ISDIR(st.st_mode)) { diskDir.clear() ; return ; }
	diskDir=dir ;
	diskMax=size_t(mb)*1024*1024 ;
	indexLoaded=false ;
	indexMap.clear() ;
}

bool WarpDisk::Prepared(const char *file,int tempo) {
	if (!file) return false ;
	std::string name ;
	{
		std::lock_guard<std::mutex> lk(diskMtx) ;
		if (diskDir.empty()) return false ;
		indexLoadLocked() ;
		std::map<std::string,std::string>::iterator it=indexMap.find(indexKey(file,tempo)) ;
		if (it==indexMap.end()) return false ;
		name=it->second ;
	}
	struct stat st ;
	return !stat((diskDir+"/"+name).c_str(),&st) ;
}

int WarpDisk::Generation() { return diskGen.load() ; }

#else   // compilador viejo: sin complex (se toca como re-pitch)

WarpStretch::WarpStretch():impl_(NULL),channels_(0) {}
WarpStretch::~WarpStretch() {}
bool WarpStretch::Available() { return false ; }
void WarpStretch::SetQuality(int,bool) {}
void *WarpCache::Acquire(const WarpCacheKey &) { return 0 ; }
bool WarpCache::Building(const WarpCacheKey &) { return false ; }
void *WarpCache::AcquireAny(const WarpCacheKey &) { return 0 ; }
int WarpPreview::Request(const char *,const char *,double,float,int) { return 0 ; }
void WarpDisk::SetDir(const char *,int) {}
bool WarpDisk::Prepared(const char *,int) { return false ; }
int WarpDisk::Generation() { return 0 ; }
bool WarpPreview::Ready(int) { return false ; }
bool WarpPreview::Failed(int) { return true ; }
void WarpCache::AddRef(void *) {}
void WarpCache::Release(void *) {}
const short *WarpCache::Frames(void *) { return 0 ; }
int WarpCache::Length(void *) { return 0 ; }
int WarpCache::LoopStart(void *) { return -1 ; }
int WarpCache::Start(void *) { return 0 ; }
bool WarpCache::Matches(void *,double,float) { return false ; }
double WarpCache::Speed(void *) { return 1.0 ; }
void WarpCache::SetBudget(int) {}
void WarpCache::SetEnabled(bool) {}
bool WarpCache::Enabled() { return false ; }
void WarpStretch::Configure(int channels,int) { channels_=channels ; }
void WarpStretch::Reset() {}
void WarpStretch::SetTranspose(float) {}
int WarpStretch::InputLatency() { return 0 ; }
int WarpStretch::OutputLatency() { return 0 ; }
void WarpStretch::Seek(float **,int,double) {}
void WarpStretch::Process(float **,int,float **,int) {}

#endif
