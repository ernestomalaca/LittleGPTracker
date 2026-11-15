#ifndef _SF2_READER_H_
#define _SF2_READER_H_

// trk2als: lector mínimo de SoundFont 2. Lee solo el índice (pdta) para
// listar los instrumentos y sacar las zonas de uno; el audio se lee del
// archivo solo para los samples que ese instrumento usa. Así una librería
// de cientos de MB no se carga entera en memoria.

#include <string>
#include <vector>

struct SF2Preset {
	std::string name ;
	int bank ;
	int program ;
	int index ;          // en la tabla phdr
} ;

// Una zona ya resuelta (preset + instrumento): qué sample suena en qué teclas
struct SF2Zone {
	int lo,hi ;          // rango de teclas
	int root ;           // tecla en la que el sample suena en su tono
	float cents ;        // afinación fina total (coarse, fine, corrección del sample)
	int sample ;         // índice en shdr (izquierdo o mono)
	int link ;           // canal derecho (estéreo), o -1
	int start,end ;      // cuadros en el bloque smpl
	int loopStart,loopEnd ;
	bool loop ;
	int rate ;
	float attenDb ;
	float attackMs,holdMs,decayMs,sustainDb,releaseMs ;
} ;

class SF2File {
public:
	SF2File() ;
	~SF2File() ;
	bool Open(const char *path) ;
	const std::vector<SF2Preset> &Presets() { return presets_ ; }   // ordenados por banco/programa
	// zonas del preset (capa de velocity ~100), una por rango de teclas
	bool Zones(int presetIndex,std::vector<SF2Zone> &out) ;
	// audio de una zona, intercalado (1 o 2 canales)
	bool ReadZone(const SF2Zone &z,std::vector<short> &frames,int &channels) ;
	std::string SampleName(int sample) ;
private:
	struct Gen { unsigned short oper ; unsigned short amount ; } ;
	bool readChunk(long pos,long size,std::vector<unsigned char> &out) ;
	bool readSamples(int sample,int start,int end,std::vector<short> &out) ;
	std::string path_ ;
	long smplPos_,smplSize_ ;
	std::vector<unsigned char> phdr_,pbag_,pgen_,inst_,ibag_,igen_,shdr_ ;
	std::vector<SF2Preset> presets_ ;
} ;

#endif
