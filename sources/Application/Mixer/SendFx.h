// trk2als: los dos efectos de envío de Chancho: REVERB (la de Rings, de
// Mutable Instruments, MIT) y ECO (un eco a tempo, de rebote, con la repetición
// cada vez más opaca). Trabajan en float, estéreo intercalado, a 44.1 kHz.
#ifndef _SEND_FX_H_
#define _SEND_FX_H_

#include <vector>
#include <stddef.h>

namespace rings { class Reverb ; }

class SendFx {
public:
	SendFx() ;
	~SendFx() ;
	void SetTempo(float bpm) ;
	// eco: cada cuánto repite (en tiempos: 0.25 = semicorchea, 0.75 = corchea
	// con puntillo...), cuánto regresa (0..1) y qué tan brillante (0..1)
	void SetEco(float beats,float feedback,float tone) ;
	// reverb: qué tan larga (0..1) y qué tan brillante (0..1)
	void SetReverb(float size,float tone) ;
	// lo que mandaron las pistas a cada efecto -> out (se SUMA a out).
	// Devuelve false si no salió nada (todo en silencio: no hay cola)
	bool Process(const float *ecoIn,const float *revIn,float *out,int frames) ;
	void Clear() ;
private:
	// eco
	std::vector<float> dl_, dr_ ;
	int dw_ ;
	float dlen_, dtarget_ ;      // largo actual (se desliza al nuevo: como cinta)
	float fb_, lpL_, lpR_, hpL_, hpR_, lpK_ ;
	float bpm_, beats_ ;
	int ecoTail_ ;
	// reverb
	rings::Reverb *rev_ ;
	unsigned short *revMem_ ;
	float revTime_, revLp_, revSecs_ ;
	int revTail_ ;
	std::vector<float> tl_, tr_ ;
} ;

#endif
