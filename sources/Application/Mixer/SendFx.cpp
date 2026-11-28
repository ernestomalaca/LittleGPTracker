#include "SendFx.h"
#include <math.h>
#include <string.h>
#include "rings/dsp/fx/reverb.h"

static const float SR=44100.0f ;
static const int ECO_MAX=int(SR*2.2f) ;     // hasta una blanca a ~55 bpm

SendFx::SendFx() {
	dw_=0 ; dlen_=dtarget_=SR*0.375f ;
	fb_=0.45f ; lpL_=lpR_=hpL_=hpR_=0 ; lpK_=0.45f ;
	bpm_=120.0f ; beats_=0.75f ;
	ecoTail_=0 ; revTail_=0 ;
	revMem_=new unsigned short[32768] ;
	memset(revMem_,0,32768*sizeof(unsigned short)) ;
	rev_=new rings::Reverb ;
	// trk2als: el reverb de Rings no pone en cero sus filtros internos (en el
	// módulo original nace en memoria limpia); aquí nacía con basura y la
	// primera vez que sonaba soltaba una ráfaga de agudos
	memset(rev_,0,sizeof(rings::Reverb)) ;
	rev_->Init(revMem_) ;
	rev_->set_amount(1.0f) ;            // sale solo lo mojado: lo seco ya va por su pista
	rev_->set_input_gain(0.2f) ;
	rev_->set_diffusion(0.7f) ;
	revSecs_=2.0f ;
	SetReverb(0.5f,0.6f) ;
	SetTempo(120.0f) ;
}

SendFx::~SendFx() {
	delete rev_ ;
	delete[] revMem_ ;
}

void SendFx::SetTempo(float bpm) {
	if (bpm<20.0f) bpm=20.0f ;
	bpm_=bpm ;
	float len=beats_*60.0f/bpm_*SR ;
	if (len>ECO_MAX-4) len=float(ECO_MAX-4) ;
	if (len<64.0f) len=64.0f ;
	dtarget_=len ;
}

void SendFx::SetEco(float beats,float feedback,float tone) {
	beats_=beats ;
	if (feedback<0) feedback=0 ;
	if (feedback>1) feedback=1 ;
	fb_=feedback*0.88f ;                 // nunca se queda pegado
	if (tone<0) tone=0 ;
	if (tone>1) tone=1 ;
	lpK_=0.08f+0.82f*tone*tone ;         // 0: muy opaco (cinta vieja) … 1: casi limpio
	SetTempo(bpm_) ;
}

void SendFx::SetReverb(float size,float tone) {
	if (size<0) size=0 ;
	if (size>1) size=1 ;
	if (tone<0) tone=0 ;
	if (tone>1) tone=1 ;
	// qué tanto dura: de medio segundo (cuartito) a unos diez (nave), y de
	// ahí cuánto regresa en cada vuelta (la vuelta dura ~0.34 s)
	revSecs_=0.5f*powf(20.0f,size) ;
	revTime_=powf(10.0f,-3.0f*0.34f/revSecs_) ;
	revLp_=0.35f+0.6f*tone ;
	rev_->set_time(revTime_) ;
	rev_->set_lp(revLp_) ;
}

void SendFx::Clear() {
	std::fill(dl_.begin(),dl_.end(),0.0f) ;
	std::fill(dr_.begin(),dr_.end(),0.0f) ;
	lpL_=lpR_=hpL_=hpR_=0 ;
	rev_->Clear() ;
	ecoTail_=revTail_=0 ;
}

static inline float softClip(float x) {   // la repetición se satura suave (no revienta)
	if (x>1.5f) return 1.0f ;
	if (x<-1.5f) return -1.0f ;
	return x-(4.0f/27.0f)*x*x*x ;
}

bool SendFx::Process(const float *ecoIn,const float *revIn,float *out,int frames) {
	bool any=false ;
	// --- eco
	bool ecoSig=false ;
	if (ecoIn) for (int i=0;i<frames*2;i++) if (ecoIn[i]>1e-5f || ecoIn[i]<-1e-5f) { ecoSig=true ; break ; }
	if (ecoSig) {
		// cuánto dura la cola: hasta que la repetición baja 60 dB
		float reps=(fb_>0.02f)?(logf(0.001f)/logf(fb_)):1.0f ;
		if (reps>40.0f) reps=40.0f ;
		ecoTail_=int(dtarget_*(reps+1.0f))+int(SR*0.2f) ;
	}
	if (ecoSig || ecoTail_>0) {
		if (dl_.empty()) { dl_.assign(ECO_MAX,0.0f) ; dr_.assign(ECO_MAX,0.0f) ; }
		for (int i=0;i<frames;i++) {
			dlen_+=(dtarget_-dlen_)*0.0004f ;      // el largo se desliza (~50 ms): cambia de tono como cinta
			float rp=float(dw_)-dlen_ ;
			while (rp<0) rp+=ECO_MAX ;
			int i0=int(rp), i1=(i0+1)%ECO_MAX ;
			float fr=rp-i0 ;
			float tapL=dl_[i0]+(dl_[i1]-dl_[i0])*fr ;
			float tapR=dr_[i0]+(dr_[i1]-dr_[i0])*fr ;
			// la repetición: cada vuelta más opaca y sin graves
			lpL_+=(tapL-lpL_)*lpK_ ; lpR_+=(tapR-lpR_)*lpK_ ;
			hpL_+=(lpL_-hpL_)*0.02f ; hpR_+=(lpR_-hpR_)*0.02f ;
			float fL=lpL_-hpL_, fR=lpR_-hpR_ ;
			float in=ecoIn?(ecoIn[2*i]+ecoIn[2*i+1])*0.5f:0.0f ;
			// de rebote: entra por la izquierda, lo que sale de un lado alimenta al otro
			dl_[dw_]=softClip(in+fR*fb_) ;
			dr_[dw_]=softClip(fL*fb_) ;
			// el primer rebote llega a medio camino por la derecha
			if (++dw_>=ECO_MAX) dw_=0 ;
			out[2*i]+=tapL ;
			out[2*i+1]+=tapR ;
		}
		if (!ecoSig) { ecoTail_-=frames ; if (ecoTail_<=0) { ecoTail_=0 ; std::fill(dl_.begin(),dl_.end(),0.0f) ; std::fill(dr_.begin(),dr_.end(),0.0f) ; lpL_=lpR_=hpL_=hpR_=0 ; } }
		any=true ;
	}
	// --- reverb
	bool revSig=false ;
	if (revIn) for (int i=0;i<frames*2;i++) if (revIn[i]>1e-5f || revIn[i]<-1e-5f) { revSig=true ; break ; }
	if (revSig) {
		float t=revSecs_*1.3f ;
		revTail_=int(SR*(t+0.5f)) ;
	}
	if (revSig || revTail_>0) {
		if (int(tl_.size())<frames) { tl_.resize(frames) ; tr_.resize(frames) ; }
		for (int i=0;i<frames;i++) { tl_[i]=revIn?revIn[2*i]:0.0f ; tr_[i]=revIn?revIn[2*i+1]:0.0f ; }
		rev_->Process(&tl_[0],&tr_[0],frames) ;
		for (int i=0;i<frames;i++) { out[2*i]+=tl_[i] ; out[2*i+1]+=tr_[i] ; }
		if (!revSig) { revTail_-=frames ; if (revTail_<=0) { revTail_=0 ; rev_->Clear() ; } }
		any=true ;
	}
	return any ;
}
