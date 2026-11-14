#include "WarpStretch.h"
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
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <cstdlib>

namespace {

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
	Entry():srcLo(0),length(0),loopStart(-1),refs(0),lastUse(0),ready(false),failed(false) {}
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
// pero de una vez y fuera del hilo de audio
void build(Entry *e) {
	const WarpCacheKey &k=e->key ;
	if (getenv("TRK_LOADLOG")) fprintf(stderr,"[WARP] estirando %p st %d loop %d-%d sp %.5f tr %.4f loop %d\n",(void*)k.data,k.start,k.loopStart,k.loopEnd,k.speed,k.transpose,k.loop) ;
	const int ch=k.channels ;
	Stretch st ;
	if (quality_==1) st.presetCheaper(ch,44100.0f,split_) ;
	else st.presetDefault(ch,44100.0f,split_) ;
	st.setTransposeFactor(k.transpose) ;

	double first=(k.loopEnd-k.start)/k.speed ;      // cuadros de salida hasta el fin del loop
	long long total, loopAt=-1 ;
	if (k.loop) {
		loopAt=llround(first) ;
		total=loopAt+llround((k.loopEnd-k.loopStart)/k.speed) ;
	} else {
		total=llround(first) ;
	}
	if (total<=0 || loopAt==total || (unsigned long long)total*ch*sizeof(short)>budget/2) {
		e->failed=true ;
		return ;
	}

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
			const short *f=&e->src[(pos-e->srcLo)*ch] ;
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
		ins[0]=&in0[0] ; ins[1]=&in1[0] ;
		st.process(ins,nin,outs,n) ;
		skip-=n ;
	}
	std::vector<short> frames(size_t(total)*ch) ;
	for (long long done=0;done<total;) {
		int n=int(total-done<512?total-done:512) ;
		acc+=n*k.speed ;
		int nin=int(acc) ;
		acc-=nin ;
		readLoop(nin) ;
		ins[0]=&in0[0] ; ins[1]=&in1[0] ;
		st.process(ins,nin,outs,n) ;
		for (int i=0;i<n;i++) {
			for (int c=0;c<ch;c++) {
				float v=outs[c][i]*32768.0f ;
				if (v>32767.0f) v=32767.0f ;
				if (v<-32768.0f) v=-32768.0f ;
				frames[(done+i)*ch+c]=short(v) ;
			}
		}
		done+=n ;
	}
	std::lock_guard<std::mutex> lk(mtx) ;
	e->frames.swap(frames) ;
	e->length=int(total) ;
	e->loopStart=int(loopAt) ;
	bytes+=e->frames.size()*sizeof(short) ;
	e->ready=true ;
	evictLocked(e) ;
}

void worker() {
#ifdef __linux__
	setpriority(PRIO_PROCESS,(id_t)syscall(SYS_gettid),10) ;   // el audio primero
#endif
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

} // namespace

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
	pending.push_back(e) ;
	if (!workerStarted) {
		// un hilo por núcleo libre (la RG353V tiene 4: el audio usa uno)
		workerStarted=true ;
		unsigned n=std::thread::hardware_concurrency() ;
		n=(n>1)?n-1:1 ;
		if (n>3) n=3 ;
		for (unsigned i=0;i<n;i++) std::thread(worker).detach() ;
	}
	cv.notify_one() ;
	return 0 ;
}

void WarpCache::AddRef(void *h) { if (h) ((Entry *)h)->refs++ ; }
void WarpCache::Release(void *h) { if (h) ((Entry *)h)->refs-- ; }
const short *WarpCache::Frames(void *h) { return &((Entry *)h)->frames[0] ; }
int WarpCache::Length(void *h) { return ((Entry *)h)->length ; }
int WarpCache::LoopStart(void *h) { return ((Entry *)h)->loopStart ; }
int WarpCache::Start(void *h) { return ((Entry *)h)->key.start ; }
bool WarpCache::Matches(void *h,double speed,float transpose) {
	Entry *e=(Entry *)h ;
	return e->key.speed==speed && e->key.transpose==transpose ;
}
void WarpCache::SetBudget(int mb) {
	std::lock_guard<std::mutex> lk(mtx) ;
	budget=size_t(mb>0?mb:1)*1024*1024 ;
}
void WarpCache::SetEnabled(bool on) { enabled=on ; }
bool WarpCache::Enabled() { return enabled ; }

#else   // compilador viejo: sin complex (se toca como re-pitch)

WarpStretch::WarpStretch():impl_(NULL),channels_(0) {}
WarpStretch::~WarpStretch() {}
bool WarpStretch::Available() { return false ; }
void WarpStretch::SetQuality(int,bool) {}
void *WarpCache::Acquire(const WarpCacheKey &) { return 0 ; }
void WarpCache::AddRef(void *) {}
void WarpCache::Release(void *) {}
const short *WarpCache::Frames(void *) { return 0 ; }
int WarpCache::Length(void *) { return 0 ; }
int WarpCache::LoopStart(void *) { return -1 ; }
int WarpCache::Start(void *) { return 0 ; }
bool WarpCache::Matches(void *,double,float) { return false ; }
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
