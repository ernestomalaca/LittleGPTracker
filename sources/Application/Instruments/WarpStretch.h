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
	// calidad: 0 = alta (la de siempre), 1 = económica (bloques más chicos,
	// menos cómputo). 'split' reparte el cómputo entre buffers para que no
	// haya picos (agrega unos ms de latencia, que ya se compensan)
	static void SetQuality(int quality,bool split) ;
private:
	void *impl_ ;
	int channels_ ;
} ;

// trk2als: pre-render del warp complex. Un loop en complex casi siempre
// suena igual (mismo sample, tempo y nota): se estira una sola vez en un hilo
// aparte y las voces lo leen como un sample normal. Mientras no está listo,
// la voz estira en tiempo real como siempre.
struct WarpCacheKey {
	const short *data ;   // sample (cuadros intercalados)
	int channels ;        // 1 o 2
	int start ;           // primer cuadro que suena
	int loopStart,loopEnd ;
	bool simpler ;        // camino del Sampler (puede empezar antes del loop)
	bool loop ;           // false: al llegar a loopEnd se termina
	double speed ;        // cuadros del sample por cuadro de salida
	float transpose ;     // factor que se le da al time-stretch
} ;

class WarpCache {
public:
	// Si ya está estirado, devuelve una referencia (soltarla con Release). Si
	// no, pide que se estire (una sola vez) y devuelve 0
	static void *Acquire(const WarpCacheKey &k) ;
	// pedido y estirándose (todavía no está, pero va a estar)
	static bool Building(const WarpCacheKey &k) ;
	// el mismo loop ya estirado a otra velocidad (el más cercano), o 0
	static void *AcquireAny(const WarpCacheKey &k) ;
	static void AddRef(void *h) ;
	static void Release(void *h) ;
	static const short *Frames(void *h) ;   // intercalado, 'channels' por cuadro
	static int Length(void *h) ;            // cuadros (con loop: hasta el fin de la vuelta)
	static int LoopStart(void *h) ;
	static int Start(void *h) ;            // cuadro del sample donde arranca         // cuadro donde vuelve; -1 sin loop
	static bool Matches(void *h,double speed,float transpose) ;
	static double Speed(void *h) ;          // la velocidad con que se estiró
	static void SetBudget(int megabytes) ;
	static void SetEnabled(bool on) ;
	static bool Enabled() ;
} ;

// trk2als: la pre-escucha de un loop en complex (el navegador): el archivo
// se estira entero en un hilo aparte a un .wav de 44.1 kHz que dura justo
// una vuelta a tempo. Uno a la vez: pedir otro deja el anterior sin efecto
class WarpPreview {
public:
	// ratio: cuadros del archivo por cuadro de salida; transpose: el factor
	// para que suene en su altura (frecuencia del archivo / 44100)
	static int Request(const char *in,const char *out,double ratio,float transpose) ;
	static bool Ready(int job) ;     // ya está escrito (y es el último pedido)
	static bool Failed(int job) ;
} ;

#endif
