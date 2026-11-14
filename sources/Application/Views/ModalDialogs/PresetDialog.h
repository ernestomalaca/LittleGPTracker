#ifndef _PRESET_DIALOG_H_
#define _PRESET_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>
#include <vector>

// trk2als: presets por categorías (carpetas dentro de presets/).
// Para cargar: A entra a una carpeta o carga el preset; GetSelected() da el
// nombre con su carpeta ("Drums/KICK1").
// Para guardar (forSave): solo carpetas, y "[guardar aquí]" elige la carpeta;
// GetFolder() da la carpeta elegida.
// GetReturnCode(): 1 = listo; 0 = cancelar
class PresetDialog : public ModalView {
public:
	PresetDialog(View &view, bool forSave = false) ;
	virtual ~PresetDialog() ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
	std::string GetSelected() ;
	std::string GetFolder() { return folder_ ; }
private:
	enum Kind { K_UP, K_HERE, K_FOLDER, K_PRESET } ;
	struct Entry {
		Kind kind ;
		std::string name ;
	} ;
	void load() ;
	bool forSave_ ;
	std::string folder_ ;
	std::vector<Entry> entries_ ;
	std::string chosen_ ;
	int selected_ ;
	int top_ ;
} ;
#endif
