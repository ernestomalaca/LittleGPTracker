#include "TrackTypeDialog.h"
#include "Application/Model/Project.h"
#include <stdio.h>
#include <string.h>

// pistas nuevas: los dos tipos de Live
static const int addType[2] = {TT_MIDI, TT_AUDIO} ;
static const char *addName[2] = {"Pista MIDI", "Pista de audio"} ;
static const char *addInfo[2] = {"notas", "loops"} ;
static const char *addWhere[2] = {"notas · sonidos", "loops · samples"} ;   // trk2als: cortos, en grande

// opciones de una pista MIDI (la de audio solo tiene la última)
static const int trackCode[4] = {TTD_LOAD, TTD_NEWKIT, TTD_EDIT, TTD_DELETE} ;
static const char *trackOption[4] = {"Cargar preset", "Nuevo kit", "Editar instrumento", "Borrar pista"} ;

TrackTypeDialog::TrackTypeDialog(View &view, int channel, bool adding, const char *name, bool midi)
	: ModalView(view), channel_(channel), adding_(adding), midi_(midi),
	  count_(midi ? 4 : 1), selected_(0), confirming_(false), yes_(false) {
	snprintf(name_, sizeof(name_), "%s", name ? name : "") ;
}

TrackTypeDialog::~TrackTypeDialog() {
}

void TrackTypeDialog::DrawView() {
	const int width = 30 ;
	GUITextProperties props ;
	char line[64] ;
	SetColor(CD_NORMAL) ;
	if (adding_ && SupportsShapes()) { drawAdd() ; return ; }
	if (adding_) {
		SetWindow(width, 7) ;
		const char *title = "Agregar pista" ;
		DrawString((width - strlen(title)) / 2, 0, title, props) ;
		for (int t = 0; t < 2; t++) {
			snprintf(line, sizeof(line), " %-16s%s", addName[t], addInfo[t]) ;
			snprintf(line + strlen(line), sizeof(line) - strlen(line), "%*s",
			         int(width - strlen(line)), "") ;
			props.invert_ = (t == selected_) ;
			DrawString(0, 2 + t, line, props) ;
			props.invert_ = false ;
		}
		DrawString((width - 23) / 2, 5, "A: elegir   B: cancelar", props) ;
	} else if (!confirming_) {
		SetWindow(width, 4 + count_) ;
		snprintf(line, sizeof(line), "Pista %d: %s", channel_ + 1, name_) ;
		DrawString((width - strlen(line)) / 2, 0, line, props) ;
		for (int i = 0; i < count_; i++) {
			int o = midi_ ? i : 3 ;
			snprintf(line, sizeof(line), " %-*s", width - 1, trackOption[o]) ;
			props.invert_ = (i == selected_) ;
			DrawString(0, 2 + i, line, props) ;
			props.invert_ = false ;
		}
		DrawString((width - 23) / 2, 3 + count_, "A: elegir   B: cancelar", props) ;
	} else {
		SetWindow(width, 6) ;
		snprintf(line, sizeof(line), "Borrar la pista %d (%s)", channel_ + 1, name_) ;
		DrawString(0, 0, line, props) ;
		DrawString(0, 1, "con todo lo que tiene?", props) ;
		DrawString(0, 2, "No se puede deshacer.", props) ;
		props.invert_ = yes_ ;
		DrawString(6, 4, "Borrar", props) ;
		props.invert_ = !yes_ ;
		DrawString(20, 4, "No", props) ;
	}
}

void TrackTypeDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {
}

void TrackTypeDialog::OnFocus() {
}

void TrackTypeDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
	if (!pressed) return ;   // soltar el botón que abrió la ventana no cuenta
	if (adding_) {
		if (mask & EPBM_B) {
			EndModal(-1) ;
		} else if (mask & (EPBM_UP | EPBM_DOWN)) {
			selected_ = 1 - selected_ ;
		} else if (mask & EPBM_A) {
			EndModal(addType[selected_]) ;
		}
	} else if (!confirming_) {
		if (mask & EPBM_B) {
			EndModal(-1) ;
		} else if (mask & EPBM_UP) {
			selected_ = (selected_ + count_ - 1) % count_ ;
		} else if (mask & EPBM_DOWN) {
			selected_ = (selected_ + 1) % count_ ;
		} else if (mask & EPBM_A) {
			int code = trackCode[midi_ ? selected_ : 3] ;
			if (code == TTD_DELETE) {
				confirming_ = true ;   // avisar antes de borrar
				yes_ = false ;
			} else {
				EndModal(code) ;
			}
		}
	} else {
		if (mask & EPBM_B) {
			confirming_ = false ;
		} else if (mask & (EPBM_LEFT | EPBM_RIGHT)) {
			yes_ = !yes_ ;
		} else if (mask & EPBM_A) {
			if (yes_) EndModal(TTD_DELETE) ;
			else confirming_ = false ;
		}
	}
	isDirty_ = true ;
}

void TrackTypeDialog::drawAdd() {
	const int W = 480, H = 260 ;
	int x = (640 - W) / 2, y = (480 - H) / 2 ;
	DrawPanel(x, y, W, H) ;
	GUIColor bg = ColorOf(CD_BACKGROUND), fg = ColorOf(CD_NORMAL) ;
	GUIColor acc = ColorOf(CD_HILITE2), dim = Mix(bg, fg, 50), card = Mix(bg, fg, 10) ;
	DrawText(x + 32, y + 28, "AGREGAR PISTA", acc, 1) ;
	for (int t = 0; t < 2; t++) {
		int ry = y + 68 + t * 64 ;
		bool sel = (t == selected_) ;
		if (sel) {
			FillRect(x + 24, ry, W - 48, 56, card, 8) ;
			FrameRect(x + 24, ry, W - 48, 56, acc, 8, 2) ;
		}
		DrawText(x + 40, ry + 10, addName[t], sel ? fg : dim, 1) ;
		DrawText(x + 40, ry + 31, addWhere[t], dim, 1) ;
	}
	DrawText(x + 32, y + H - 36, "A elige · B cancela", dim, 1) ;
}
