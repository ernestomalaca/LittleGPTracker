#include "PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "System/FileSystem/FileSystem.h"
#include <stdio.h>
#include <string.h>

#define PD_WIDTH 28
#define PD_ROWS 12

PresetDialog::PresetDialog(View &view, bool forSave, int kind)
    : ModalView(view), forSave_(forSave), kind_(kind), selected_(0), top_(0) {
	folder_ = InstrumentPresets::LastFolder() ;   // se abre donde se quedó
	if (kind_ >= 0) {   // solo un tipo: en su carpeta (Kits...) si la hay
		const char *kf = InstrumentPresets::KindFolder(kind_) ;
		if (folder_.compare(0, strlen(kf), kf) != 0 || !InstrumentPresets::FolderHas(folder_, kind_))
			folder_ = InstrumentPresets::FolderHas(kf, kind_) || forSave_ ? kf : "" ;
		if (forSave_ && !Path(std::string("root:presets/") + kf).Exists()) folder_ = "" ;
	}
	load() ;
	if (entries_.empty() && !folder_.empty()) {    // la carpeta ya no existe
		folder_ = "" ;
		load() ;
	}
}

PresetDialog::~PresetDialog() {
}

void PresetDialog::load() {
	std::vector<std::string> folders, presets ;
	InstrumentPresets::ListFolder(folder_, folders, presets) ;
	entries_.clear() ;
	Entry e ;
	if (forSave_) {
		e.kind = K_HERE ;
		e.name = "[guardar aquí]" ;
		entries_.push_back(e) ;
	}
	if (!folder_.empty()) {
		e.kind = K_UP ;
		e.name = ".." ;
		entries_.push_back(e) ;
	}
	for (size_t i = 0; i < folders.size(); i++) {
		if (kind_ >= 0 && !forSave_ &&
		    !InstrumentPresets::FolderHas(folder_.empty() ? folders[i] : folder_ + "/" + folders[i], kind_))
			continue ;
		e.kind = K_FOLDER ;
		e.name = folders[i] + "/" ;
		entries_.push_back(e) ;
	}
	if (!forSave_) {
		for (size_t i = 0; i < presets.size(); i++) {
			if (kind_ >= 0 &&
			    InstrumentPresets::KindOf(folder_.empty() ? presets[i] : folder_ + "/" + presets[i]) != kind_)
				continue ;
			e.kind = K_PRESET ;
			e.name = presets[i] ;
			entries_.push_back(e) ;
		}
	}
	selected_ = 0 ;
	top_ = 0 ;
	// al cargar, de entrada en el primer preset (no en "..")
	if (!forSave_) {
		for (size_t i = 0; i < entries_.size(); i++) {
			if (entries_[i].kind != K_UP) { selected_ = int(i) ; break ; }
		}
	}
}

void PresetDialog::DrawView() {
	GUITextProperties props ;
	char line[PD_WIDTH + 1] ;
	std::string title = forSave_ ? "Guardar en: " : (kind_ == InstrumentPresets::PK_KIT ? "Cambiar de kit: " : "Cargar preset: ") ;
	title += folder_.empty() ? "/" : folder_ ;
	if (title.size() > PD_WIDTH - 2) title = "..." + title.substr(title.size() - (PD_WIDTH - 5)) ;
	int n = (int)entries_.size() ;
	if (n == 0) {
		SetWindow(PD_WIDTH, 5) ;
		SetColor(CD_NORMAL) ;
		DrawString(1, 0, title.c_str(), props) ;
		DrawString(1, 2, kind_ == InstrumentPresets::PK_KIT ? "Todavía no hay kits." : "Todavía no hay presets.", props) ;
		DrawString(1, 4, "B: cerrar", props) ;
		return ;
	}
	int rows = n < PD_ROWS ? n : PD_ROWS ;
	SetWindow(PD_WIDTH, rows + 4) ;
	SetColor(CD_NORMAL) ;
	DrawString(1, 0, title.c_str(), props) ;
	if (selected_ < top_) top_ = selected_ ;
	if (selected_ >= top_ + PD_ROWS) top_ = selected_ - PD_ROWS + 1 ;
	for (int i = 0; i < rows; i++) {
		int k = top_ + i ;
		snprintf(line, sizeof(line), " %-*s", PD_WIDTH - 1, entries_[k].name.c_str()) ;
		line[PD_WIDTH] = 0 ;
		props.invert_ = (k == selected_) ;
		SetColor(k == selected_ ? CD_HILITE2 : (entries_[k].kind == K_PRESET ? CD_NORMAL : CD_HILITE1)) ;
		DrawString(0, 2 + i, line, props) ;
	}
	props.invert_ = false ;
	SetColor(CD_NORMAL) ;
	DrawString(1, rows + 3, forSave_ ? "A: elegir   B: cancelar" : "A: abrir/cargar  B: cancelar", props) ;
}

void PresetDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {
}

void PresetDialog::OnFocus() {
}

void PresetDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
	if (!pressed) return ;   // soltar el botón que abrió la ventana no cuenta
	int n = (int)entries_.size() ;
	if (mask & EPBM_B) {
		EndModal(0) ;
		return ;
	}
	if (n == 0) {
		if (mask & EPBM_A) EndModal(0) ;
		return ;
	}
	if (mask & EPBM_A) {
		Entry &e = entries_[selected_] ;
		switch (e.kind) {
		case K_UP: {
			size_t slash = folder_.find_last_of('/') ;
			folder_ = (slash == std::string::npos) ? "" : folder_.substr(0, slash) ;
			load() ;
			break ;
		}
		case K_FOLDER:
			folder_ = folder_.empty() ? e.name.substr(0, e.name.size() - 1)
			                          : folder_ + "/" + e.name.substr(0, e.name.size() - 1) ;
			load() ;
			break ;
		case K_HERE:
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(1) ;
			return ;
		case K_PRESET:
			chosen_ = folder_.empty() ? e.name : folder_ + "/" + e.name ;
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(1) ;
			return ;
		}
		isDirty_ = true ;
		return ;
	}
	if (mask & EPBM_UP) selected_ = (selected_ + n - 1) % n ;
	if (mask & EPBM_DOWN) selected_ = (selected_ + 1) % n ;
	if (mask & EPBM_LEFT) selected_ = selected_ >= PD_ROWS ? selected_ - PD_ROWS : 0 ;
	if (mask & EPBM_RIGHT) selected_ = selected_ + PD_ROWS < n ? selected_ + PD_ROWS : n - 1 ;
	isDirty_ = true ;
}

std::string PresetDialog::GetSelected() {
	return chosen_ ;
}
