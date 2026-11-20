#ifndef _INSTRUMENT_PRESETS_H_
#define _INSTRUMENT_PRESETS_H_

#include "System/FileSystem/FileSystem.h"
#include <string>
#include <vector>

class SampleInstrument ;

// trk2als: presets de instrumento. Cada preset es una carpeta en
// root:presets/<nombre>/ con preset.xml (todos los parámetros, como en
// lgptsav.dat) y una copia del sample, para que funcione en cualquier proyecto.
namespace InstrumentPresets {
	// nombres de los presets guardados, en orden alfabético
	std::vector<std::string> List() ;
	// trk2als: categorías. Lo que hay en una carpeta de presets ("" = la
	// raíz, "Drums", "Drums/Kicks"...): subcarpetas y presets, cada uno en
	// orden alfabético. Los nombres de preset en Save/Load/Exists pueden
	// llevar su carpeta ("Drums/KICK1")
	void ListFolder(const std::string &folder, std::vector<std::string> &folders,
	                std::vector<std::string> &presets) ;
	// la última carpeta usada (para guardar donde se estaba buscando)
	std::string &LastFolder() ;
	// trk2als: qué es un preset: sample (Sampler de un sample), multicapa
	// (zonas/soundfont) o kit (16 pads); y si una carpeta tiene alguno de ese tipo
	enum Kind { PK_SAMPLE=0, PK_MULTI=1, PK_KIT=2 } ;
	int KindOf(const std::string &name) ;
	bool FolderHas(const std::string &folder,int kind) ;
	const char *KindFolder(int kind) ;   // "Samples", "Multicapa", "Kits"
	// trk2als: la samplelib dentro del navegador, como una carpeta más de
	// Samples: "Samples/Librería/Drums/kick.wav" es samplelib/Drums/kick.wav.
	// Un .wav de ahí se carga como un preset de sample (tipo Simpler)
	std::string LibFolder() ;                     // "Samples/Librería"
	bool IsLib(const std::string &name) ;         // ¿es la librería o algo dentro?
	Path LibPath(const std::string &name) ;       // la ruta real
	bool LibExists() ;
	void ListLib(const std::string &folder, std::vector<std::string> &folders,
	             std::vector<std::string> &wavs) ;
	int CountLib(const std::string &folder) ;     // .wav adentro (con subcarpetas)
	void ForgetLibCounts() ;                      // al abrir el navegador: se vuelven a contar
	bool Exists(const std::string &name) ;
	// guarda el instrumento (sobrescribe si ya existe). false si falló
	bool Save(SampleInstrument *instr, const std::string &name) ;
	// carga el preset en el instrumento; si el sample no está en el
	// proyecto, lo copia. false si falló
	bool Load(SampleInstrument *instr, const std::string &name) ;
	// trk2als: lo lento de Load (copiar e importar sus samples al proyecto),
	// sin tocar el instrumento: se puede hacer con la pista sonando y luego
	// Load solo aplica (rápido)
	bool Prepare(const std::string &name) ;
	// trk2als: lo que hace falta para mostrar y pre-escuchar un preset sin
	// cargarlo: tipo, cuántas zonas o pads, su carpeta, el sample principal
	// y los .wav de las zonas (en orden)
	struct Info {
		int kind ;
		int zones ;
		std::string dir ;      // la carpeta del preset (ruta completa)
		std::string sample ;
		std::vector<std::string> zoneFiles ;
		Info() : kind(PK_SAMPLE), zones(0) {}
	} ;
	bool Describe(const std::string &name, Info &out) ;
	// trk2als: una foto del instrumento en memoria (para deshacer un cambio
	// de sonido); los samples siguen en el pool
	struct Snapshot {
		std::string sample ;   // por nombre: el índice cambia si se limpia el pool
		std::vector<std::pair<std::string, std::string> > params ;
	} ;
	void Capture(SampleInstrument *instr, Snapshot &out) ;
	void Restore(SampleInstrument *instr, const Snapshot &s) ;
	// trk2als: leer los samples de un preset en otro hilo, sin tocar nada del
	// proyecto (la interfaz y el audio siguen). Cuando Done(), Adopt() los
	// pasa al pool (rápido) y luego Load() ya no lee nada.
	struct Preload ;
	Preload *StartPreload(const std::string &name) ;   // 0 si el preset no sirve
	bool PreloadDone(Preload *p) ;
	int PreloadPermille(Preload *p) ;                   // cuánto lleva leído (0..1000)
	const std::string &PreloadName(Preload *p) ;
	// pasa lo leído al pool; false si no cupo (lo que no entró se descarta)
	bool PreloadAdopt(Preload *p) ;
	// ya no se necesita: si el hilo sigue, él lo borra al terminar
	void PreloadRelease(Preload *p) ;
	// nombre sugerido para guardar: el del sample, sin extensión
	std::string SuggestedName(SampleInstrument *instr) ;
}

#endif
