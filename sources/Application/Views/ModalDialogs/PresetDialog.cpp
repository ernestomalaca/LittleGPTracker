#include "PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include <stdio.h>
#include <string.h>

#define PD_WIDTH 26
#define PD_ROWS 12

PresetDialog::PresetDialog(View &view) : ModalView(view), selected_(0), top_(0) {
	names_ = InstrumentPresets::List() ;
}

PresetDialog::~PresetDialog() {
}

void PresetDialog::DrawView() {
	GUITextProperties props ;
	char line[PD_WIDTH + 1] ;
	if (names_.empty()) {
		SetWindow(PD_WIDTH, 5) ;
		SetColor(CD_NORMAL) ;
		DrawString(1, 0, "Cargar preset", props) ;
		DrawString(1, 2, "Todavia no hay presets.", props) ;
		DrawString(1, 4, "B: cerrar", props) ;
		return ;
	}
	int rows = (int)names_.size() < PD_ROWS ? (int)names_.size() : PD_ROWS ;
	SetWindow(PD_WIDTH, rows + 4) ;
	SetColor(CD_NORMAL) ;
	DrawString(1, 0, "Cargar preset", props) ;
	if (selected_ < top_) top_ = selected_ ;
	if (selected_ >= top_ + PD_ROWS) top_ = selected_ - PD_ROWS + 1 ;
	for (int i = 0; i < rows; i++) {
		int k = top_ + i ;
		snprintf(line, sizeof(line), " %-*s", PD_WIDTH - 1, names_[k].c_str()) ;
		line[PD_WIDTH] = 0 ;
		props.invert_ = (k == selected_) ;
		SetColor(k == selected_ ? CD_HILITE2 : CD_NORMAL) ;
		DrawString(0, 2 + i, line, props) ;
	}
	props.invert_ = false ;
	SetColor(CD_NORMAL) ;
	DrawString(1, rows + 3, "A: cargar   B: cancelar", props) ;
}

void PresetDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {
}

void PresetDialog::OnFocus() {
}

void PresetDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
	if (!pressed) return ;   // soltar el botón que abrió la ventana no cuenta
	int n = (int)names_.size() ;
	if (mask & EPBM_B) {
		EndModal(0) ;
		return ;
	}
	if (n == 0) {
		if (mask & EPBM_A) EndModal(0) ;
		return ;
	}
	if (mask & EPBM_A) {
		EndModal(1) ;
		return ;
	}
	if (mask & EPBM_UP) selected_ = (selected_ + n - 1) % n ;
	if (mask & EPBM_DOWN) selected_ = (selected_ + 1) % n ;
	if (mask & EPBM_LEFT) selected_ = selected_ >= PD_ROWS ? selected_ - PD_ROWS : 0 ;
	if (mask & EPBM_RIGHT) selected_ = selected_ + PD_ROWS < n ? selected_ + PD_ROWS : n - 1 ;
	isDirty_ = true ;
}

std::string PresetDialog::GetSelected() {
	if (selected_ < 0 || selected_ >= (int)names_.size()) return "" ;
	return names_[selected_] ;
}
