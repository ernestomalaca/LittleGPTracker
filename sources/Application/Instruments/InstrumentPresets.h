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
