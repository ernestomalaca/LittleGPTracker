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
	bool Exists(const std::string &name) ;
	// guarda el instrumento (sobrescribe si ya existe). false si falló
	bool Save(SampleInstrument *instr, const std::string &name) ;
	// carga el preset en el instrumento; si el sample no está en el
	// proyecto, lo copia. false si falló
	bool Load(SampleInstrument *instr, const std::string &name) ;
	// nombre sugerido para guardar: el del sample, sin extensión
	std::string SuggestedName(SampleInstrument *instr) ;
}

#endif
