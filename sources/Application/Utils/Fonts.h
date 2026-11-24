#ifndef _CHANCHO_FONTS_H_
#define _CHANCHO_FONTS_H_
// trk2als: la fuente elegida en Proyecto > FUENTE (chancho6/fuentes/<nombre>.fnt,
// ver Resources/mkfnt.py). Sin archivo (o "dejavu") se usan las de siempre.
// Se vuelve a leer cuando cambia Config::Generation().
#include "Application/Model/Config.h"
#include "System/FileSystem/FileSystem.h"
#include <stdio.h>
#include <string.h>
#include <string>

namespace Fonts {

struct Data {
	unsigned char hd[256][256] ;     // 16x16
	unsigned char small[256][96] ;   // 8x12
} ;

inline Data *Current() {
	static Data *data = 0 ;
	static int gen = -1 ;
	if (gen == Config::Generation()) return data ;
	gen = Config::Generation() ;
	const char *name = Config::GetInstance()->GetValue("FONT") ;
	if (!name || !*name || !strcmp(name, "dejavu")) { delete data ; data = 0 ; return 0 ; }
	std::string fn = std::string("bin:fuentes/") + name + ".fnt" ;
	Path path(fn.c_str()) ;
	FILE *f = fopen(path.GetPath().c_str(), "rb") ;
	Data *d = 0 ;
	if (f) {
		char magic[4] ;
		d = new Data ;
		if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "CHF1", 4) ||
		    fread(d->hd, 1, sizeof(d->hd), f) != sizeof(d->hd) ||
		    fread(d->small, 1, sizeof(d->small), f) != sizeof(d->small)) {
			delete d ; d = 0 ;
		}
		fclose(f) ;
	}
	Trace::Log("FONTS", "fuente %s: %s", name, d ? "ok" : "no se pudo leer") ;
	delete data ;
	data = d ;
	return data ;
}

}

#endif
