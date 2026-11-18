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

class Clip {
public:
	Clip():pages(0) {}
	void Clear() { pages=0 ; notes.clear() ; }
	bool IsEmpty() const { return pages==0 ; }
	int Steps() const { return pages*CLIP_STEPS_PER_PAGE ; }
	bool operator==(const Clip &o) const { return pages==o.pages && notes==o.notes ; }
	bool operator!=(const Clip &o) const { return !(*this==o) ; }
	int pages ;                    // 0 = clip vacío (sin crear)
	std::vector<ClipNote> notes ;  // ordenadas por inicio
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
