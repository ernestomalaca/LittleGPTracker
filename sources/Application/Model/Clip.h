#ifndef _CLIP_H_
#define _CLIP_H_

// trk2als: el clip MIDI propio (como en Live). Ya no usa las phrases ni las
// chains de LGPT: guarda sus notas directo, cada una con su inicio en la
// rejilla de 1/16, un nudge de 1/24 de paso, su largo, velocity,
// subdivisiones y rampa. El largo del clip va en compases (páginas de 16
// pasos). El número de clip es lo que va en la celda de la Session.

#include <vector>
#include <string>

#define CLIP_COUNT 0xFF
#define NO_MORE_CLIP 0x100
#define CLIP_MAX_PAGES 16
#define CLIP_STEPS_PER_PAGE 16
#define CLIP_TICKS 24          // nudge: 1/24 de paso
#define VOICE_COUNT 4          // notas a la vez por pista (voces del canal)

class TiXmlNode ;
class TiXmlElement ;

struct ClipNote {
	int start ;   // paso (0 .. pasos del clip - 1)
	int tick ;    // nudge 0..23 después de 'start'
	int len ;     // en pasos (>= 1)
	int pitch ;   // 0..127
	int vel ;     // 1..127
	int rat ;     // subdivisiones 1..8
	int ramp ;    // rampa de velocity de las subdivisiones -4..4
	ClipNote():start(0),tick(0),len(1),pitch(60),vel(100),rat(1),ramp(0) {}
	bool operator==(const ClipNote &o) const {
		return start==o.start && tick==o.tick && len==o.len && pitch==o.pitch &&
		       vel==o.vel && rat==o.rat && ramp==o.ramp ;
	}
} ;

// trk2als: automatización (como las envolventes de un clip en la Session de
// Live). Cada lane mueve un parámetro del instrumento de la pista ('target',
// el FourCC de su variable; 'pad' >= 0 = un pad del kit). Sus puntos van en
// 1/24 de paso; antes del primero vale el primero y después del último, el
// último. Con 'ramp' el valor llega deslizándose al punto siguiente.
struct AutoPoint {
	int pos ;      // en 1/24 de paso desde el inicio del clip
	int value ;    // el valor tal cual de la variable
	bool ramp ;    // de aquí al siguiente: rampa (si no, escalón)
	bool inv ;     // escalón invertido: salta ya al valor del siguiente
	AutoPoint():pos(0),value(0),ramp(false),inv(false) {}
	AutoPoint(int p,int v,bool r,bool i=false):pos(p),value(v),ramp(r),inv(i) {}
	bool operator==(const AutoPoint &o) const { return pos==o.pos && value==o.value && ramp==o.ramp && inv==o.inv ; }
} ;

struct AutoLane {
	unsigned int target ;
	int pad ;
	std::vector<AutoPoint> points ;   // ordenados por pos
	AutoLane():target(0),pad(-1) {}
	bool operator==(const AutoLane &o) const { return target==o.target && pad==o.pad && points==o.points ; }
	// el valor en esa posición (1/24 de paso); false si no hay puntos
	bool ValueAt(int pos,int &value) const ;
	void Sort() ;
} ;

class Clip {
public:
	Clip():pages(0),loopStart(-1),loopEnd(-1) {}
	void Clear() { pages=0 ; notes.clear() ; lanes.clear() ; name.clear() ; loopStart=loopEnd=-1 ; }
	bool IsEmpty() const { return pages==0 ; }
	int Steps() const { return pages*CLIP_STEPS_PER_PAGE ; }
	bool operator==(const Clip &o) const {
		return pages==o.pages && notes==o.notes && lanes==o.lanes &&
		       loopStart==o.loopStart && loopEnd==o.loopEnd ;
	}
	bool operator!=(const Clip &o) const { return !(*this==o) ; }
	int pages ;                    // 0 = clip vacío (sin crear)
	std::vector<ClipNote> notes ;  // ordenadas por inicio
	std::string name ;             // "woodblock 1"; vacío = sin nombre
	int loopStart,loopEnd ;        // compases; -1 = sin loop (suena entero)
	std::vector<AutoLane> lanes ;  // automatización
	AutoLane *FindLane(unsigned int target,int pad) ;
	AutoLane &GetLane(unsigned int target,int pad) ;   // la crea si no existe
	void RemoveLane(unsigned int target,int pad) ;
	// compases: duplicar uno (la copia entra después) o borrarlo
	void DuplicatePageLanes(int page) ;
	void DeletePageLanes(int page) ;
} ;

class ClipBank {
public:
	ClipBank() ;
	Clip &Get(int i) { return clip_[i] ; }
	// el primero sin usar (ni en la Session ni con notas)
	unsigned short GetNext(const unsigned char *songData,int songSize) ;
	// los que ya no están en ninguna celda se vacían
	void Purge(const unsigned char *songData,int songSize) ;
	void Copy(int from,int to) { clip_[to]=clip_[from] ; }
	void Save(TiXmlNode *node) ;
	void Restore(TiXmlElement *element) ;
private:
	Clip clip_[CLIP_COUNT] ;
} ;

#endif
