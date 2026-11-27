#include "PlaitsVoice.h"
#include <string.h>
#include "plaits/dsp/voice.h"

class PlaitsVoiceImpl {
public:
	plaits::Voice voice ;
	char ram[16384] ;
	int engine ;
	int trig ;      // bloques que le quedan al pulso de disparo
} ;

static const char *names[PlaitsVoice::ENGINES]={
	"filtro","fase","6 op A","6 op B","6 op C","terreno","cuerdas","chip",
	"analogo","plegado","fm","formante","armonico","tabla","acordes","voz",
	"enjambre","ruido","particulas","pulsada","campana","bombo","tarola","hihat"
} ;

const char *PlaitsVoice::EngineName(int i) {
	if (i<0) i=0 ;
	if (i>=ENGINES) i=ENGINES-1 ;
	return names[i] ;
}

PlaitsVoice::PlaitsVoice() {
	impl_=new PlaitsVoiceImpl ;
	stmlib::BufferAllocator alloc(impl_->ram,sizeof(impl_->ram)) ;
	impl_->voice.Init(&alloc) ;
	impl_->engine=-1 ;
	impl_->trig=0 ;
}

PlaitsVoice::~PlaitsVoice() { delete impl_ ; }

void PlaitsVoice::Trigger() { impl_->trig=-1 ; }   // (-1: primero un bloque en bajo, para que vea el flanco)

void PlaitsVoice::Render(float note,int engine,float harmonics,float timbre,float morph,
                         short *out,int frames,int stride) {
	if (engine<0) engine=0 ;
	if (engine>=ENGINES) engine=ENGINES-1 ;
	plaits::Patch p ;
	memset(&p,0,sizeof(p)) ;
	p.note=note ;
	p.harmonics=harmonics ;
	p.timbre=timbre ;
	p.morph=morph ;
	p.engine=engine ;
	p.decay=0.5f ;
	p.lpg_colour=0.5f ;
	plaits::Modulations m ;
	memset(&m,0,sizeof(m)) ;
	m.trigger_patched=true ;   // los de golpe esperan su disparo
	m.level_patched=true ;     // el volumen lo pone la envolvente de Chancho
	m.level=1.0f ;
	plaits::Voice::Frame fr[plaits::kMaxBlockSize] ;
	while (frames>0) {
		int n=frames>12?12:frames ;
		if (impl_->trig<0) { m.trigger=0.0f ; impl_->trig=40 ; }
		else if (impl_->trig>0) { m.trigger=1.0f ; impl_->trig-- ; }
		else m.trigger=0.0f ;
		impl_->voice.Render(p,m,fr,n) ;
		for (int i=0;i<n;i++) { *out=fr[i].out ; out+=stride ; }
		frames-=n ;
	}
}
