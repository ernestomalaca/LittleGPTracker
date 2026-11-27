// trk2als: el bus de los envíos. Cada pista le suma una parte de lo suyo
// (cuánto al ECO, cuánto a la REVERB); al final de la mezcla, el bus pasa eso
// por los dos efectos y devuelve solo lo mojado al master.
#ifndef _SEND_BUS_H_
#define _SEND_BUS_H_

#include "Services/Audio/AudioModule.h"
#include <vector>

class SendFx ;

class SendBus: public AudioModule {
public:
	static SendBus *Get() ;
	virtual ~SendBus() ;
	// una pista manda su audio (ya con su volumen): eco y rev de 0 a 1
	void Add(const fixed *buffer,int frames,float eco,float rev) ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	void SetPregain(float g) { pregain_=g ; }
	// tempo; eco: cada cuánto (0..5: 1/16, 1/8, 1/8., 1/4, 1/4., 1/2), cuánto
	// regresa y su brillo (0..100); reverb: tamaño y brillo (0..100)
	void SetParams(int tempo,int ecoTime,int ecoBack,int ecoTone,int revSize,int revTone) ;
	static float EcoBeats(int idx) ;
	static const char *EcoName(int idx) ;
	enum { ECO_TIMES=6 } ;
private:
	SendBus() ;
	SendFx *fx_ ;
	std::vector<float> eco_, rev_, out_ ;
	bool ecoOn_, revOn_ ;
	float pregain_ ;
	int last_[6] ;
} ;

#endif
