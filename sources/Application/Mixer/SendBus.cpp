#include "SendBus.h"
#include "SendFx.h"
#include <string.h>

static const float FIXED_ONE=32768.0f*32768.0f ;   // un sample a tope, en "fixed"

SendBus *SendBus::Get() {
	static SendBus *bus=0 ;
	if (!bus) bus=new SendBus() ;
	return bus ;
}

SendBus::SendBus() {
	fx_=new SendFx() ;
	ecoOn_=revOn_=false ;
	pregain_=1.0f ;
	for (int i=0;i<6;i++) last_[i]=-1 ;
}

SendBus::~SendBus() { delete fx_ ; }

float SendBus::EcoBeats(int idx) {
	static const float b[ECO_TIMES]={0.25f,0.5f,0.75f,1.0f,1.5f,2.0f} ;
	if (idx<0) idx=0 ;
	if (idx>=ECO_TIMES) idx=ECO_TIMES-1 ;
	return b[idx] ;
}

const char *SendBus::EcoName(int idx) {
	static const char *n[ECO_TIMES]={"1/16","1/8","1/8.","1/4","1/4.","1/2"} ;
	if (idx<0) idx=0 ;
	if (idx>=ECO_TIMES) idx=ECO_TIMES-1 ;
	return n[idx] ;
}

void SendBus::SetParams(int tempo,int ecoTime,int ecoBack,int ecoTone,int revSize,int revTone) {
	int now[6]={tempo,ecoTime,ecoBack,ecoTone,revSize,revTone} ;
	if (!memcmp(now,last_,sizeof(now))) return ;
	memcpy(last_,now,sizeof(now)) ;
	fx_->SetTempo(float(tempo)) ;
	fx_->SetEco(EcoBeats(ecoTime),ecoBack/100.0f,ecoTone/100.0f) ;
	fx_->SetReverb(revSize/100.0f,revTone/100.0f) ;
}

void SendBus::Add(const fixed *buffer,int frames,float eco,float rev) {
	int n=frames*2 ;
	if (eco>0.0f) {
		if (int(eco_.size())<n) eco_.resize(n) ;
		if (!ecoOn_) { memset(&eco_[0],0,n*sizeof(float)) ; ecoOn_=true ; }
		float g=eco/FIXED_ONE ;
		for (int i=0;i<n;i++) eco_[i]+=float(buffer[i])*g ;
	}
	if (rev>0.0f) {
		if (int(rev_.size())<n) rev_.resize(n) ;
		if (!revOn_) { memset(&rev_[0],0,n*sizeof(float)) ; revOn_=true ; }
		float g=rev/FIXED_ONE ;
		for (int i=0;i<n;i++) rev_[i]+=float(buffer[i])*g ;
	}
}

bool SendBus::Render(fixed *buffer,int samplecount) {
	int n=samplecount*2 ;
	if (int(out_.size())<n) out_.resize(n) ;
	memset(&out_[0],0,n*sizeof(float)) ;
	bool any=fx_->Process((ecoOn_ && int(eco_.size())>=n)?&eco_[0]:0,(revOn_ && int(rev_.size())>=n)?&rev_[0]:0,&out_[0],samplecount) ;
	ecoOn_=revOn_=false ;
	if (!any) return false ;
	float g=FIXED_ONE*pregain_ ;
	for (int i=0;i<n;i++) {
		float v=out_[i] ;
		if (v>1.9f) v=1.9f ;        // (que no se desborde el entero; el master ya recorta)
		if (v<-1.9f) v=-1.9f ;
		buffer[i]=fixed(v*g) ;
	}
	return true ;
}
