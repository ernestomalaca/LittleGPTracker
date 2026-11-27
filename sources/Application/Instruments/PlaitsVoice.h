// trk2als: una voz del sinte de Chancho: Plaits, de Mutable Instruments (MIT).
// 24 motores; cada uno se maneja con tres macros (ARMONICOS, TIMBRE, MORPH).
#ifndef _PLAITS_VOICE_H_
#define _PLAITS_VOICE_H_

class PlaitsVoiceImpl ;

class PlaitsVoice {
public:
	PlaitsVoice() ;
	~PlaitsVoice() ;
	void Trigger() ;      // nota nueva: los motores de golpe (cuerda, campana, tambores) se excitan
	// nota: en semitonos MIDI (60 = C3 de Chancho), con decimales. Macros 0..1.
	// Deja 'frames' samples mono de 16 bits en out (uno cada 'stride')
	void Render(float note,int engine,float harmonics,float timbre,float morph,
	            short *out,int frames,int stride) ;
	enum { ENGINES=24 } ;
	static const char *EngineName(int i) ;
private:
	PlaitsVoiceImpl *impl_ ;
} ;

#endif
