#ifndef _INSTRUMENT_PRESETS_H_
#define _INSTRUMENT_PRESETS_H_

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
	bool Exists(const std::string &name) ;
	// guarda el instrumento (sobrescribe si ya existe). false si falló
	bool Save(SampleInstrument *instr, const std::string &name) ;
	// carga el preset en el instrumento; si el sample no está en el
	// proyecto, lo copia. false si falló
	bool Load(SampleInstrument *instr, const std::string &name) ;
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
	// nombre sugerido para guardar: el del sample, sin extensión
	std::string SuggestedName(SampleInstrument *instr) ;
}

#endif
