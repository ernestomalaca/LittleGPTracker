#ifndef _SF2_IMPORT_H_
#define _SF2_IMPORT_H_

#include <string>

class SampleInstrument ;

// trk2als: convierte un instrumento de un .sf2 en un instrumento de zonas:
// escribe sus samples como .wav en la carpeta samples del proyecto, los
// carga y deja en 'si' las zonas, la envolvente y el volumen del .sf2.
// presetIndex: índice en SF2File::Presets(). Devuelve false si no pudo; en
// 'message' queda qué pasó (o el nombre del instrumento)
bool SF2Import(const char *sf2Path,int presetIndex,SampleInstrument *si,std::string &message) ;

// Lo mismo, con el índice del instrumento en el archivo (el de phdr, como
// lo numera SoundFontPreset)
bool SF2ImportRaw(const char *sf2Path,int phdrIndex,SampleInstrument *si,std::string &message) ;

// Para escuchar antes de convertir: escribe en 'wavPath' el sample de la
// tecla 'note' del instrumento (o el más cercano)
bool SF2PreviewNote(const char *sf2Path,int presetIndex,int note,const char *wavPath) ;

#endif
