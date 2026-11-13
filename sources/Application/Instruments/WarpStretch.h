#ifndef _WARP_STRETCH_H_
#define _WARP_STRETCH_H_

// trk2als: envoltura del time-stretch de Signalsmith (modo de warp "complex").
// Se compila aparte con C++11 (ver projects/Makefile); el resto del tracker
// sigue en C++03 y solo ve esta clase.

class WarpStretch {
public:
	WarpStretch() ;
	~WarpStretch() ;
	// canales del sample (1 o 2) y frecuencia de la mezcla
	void Configure(int channels,int sampleRate) ;
	int Channels() { return channels_ ; } ;
	void Reset() ;
	// factor de frecuencia (1 = igual; 2 = una octava arriba)
	void SetTranspose(float factor) ;
	int InputLatency() ;
	int OutputLatency() ;
	// in/out: un puntero por canal
	void Seek(float **in,int inSamples,double rate) ;
	void Process(float **in,int inSamples,float **out,int outSamples) ;
	static bool Available() ;   // false si se compiló sin C++11
private:
	void *impl_ ;
	int channels_ ;
} ;

#endif
