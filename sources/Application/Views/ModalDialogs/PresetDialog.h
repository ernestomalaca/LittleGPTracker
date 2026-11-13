#ifndef _PRESET_DIALOG_H_
#define _PRESET_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>
#include <vector>

// trk2als: lista de presets para cargar en el instrumento.
// GetReturnCode(): 1 = cargar GetSelected(); 0 = cancelar
class PresetDialog : public ModalView {
public:
	PresetDialog(View &view) ;
	virtual ~PresetDialog() ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
	std::string GetSelected() ;
private:
	std::vector<std::string> names_ ;
	int selected_ ;
	int top_ ;
} ;
#endif
