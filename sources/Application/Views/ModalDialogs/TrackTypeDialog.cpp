#include "TrackTypeDialog.h"
#include "Application/Model/Project.h"
#include <stdio.h>
#include <string.h>

// en la ventana solo hay dos tipos, como en Live
static const int optionType[2] = {TT_MIDI, TT_AUDIO} ;
static const char *optionName[2] = {"MIDI", "Audio"} ;
static const char *optionInfo[2] = {"notas e instrumentos", "loops al tempo"} ;

TrackTypeDialog::TrackTypeDialog(View &view, int channel, int current, bool hasContent)
	: ModalView(view), channel_(channel), current_(current), hasContent_(hasContent),
	  selected_(current == TT_AUDIO ? 1 : 0), confirming_(false), yes_(false) {
	current_ = (current == TT_AUDIO) ? 1 : 0 ;   // índice de la opción
}

TrackTypeDialog::~TrackTypeDialog() {
}

void TrackTypeDialog::DrawView() {
	const int width = 30 ;
	GUITextProperties props ;
	char line[48] ;
	if (!confirming_) {
		SetWindow(width, 7) ;
		SetColor(CD_NORMAL) ;
		snprintf(line, sizeof(line), "Tipo de la pista %d", channel_) ;
		DrawString((width - strlen(line)) / 2, 0, line, props) ;
		for (int t = 0; t < 2; t++) {
			// el tipo actual lleva '*'; el elegido va resaltado
			snprintf(line, sizeof(line), "%c %-7s%s", t == current_ ? '*' : ' ', optionName[t],
			         optionInfo[t]) ;
			snprintf(line + strlen(line), sizeof(line) - strlen(line), "%*s",
			         int(width - strlen(line)), "") ;
			props.invert_ = (t == selected_) ;
			DrawString(0, 2 + t, line, props) ;
			props.invert_ = false ;
		}
		SetColor(CD_NORMAL) ;
		DrawString((width - 23) / 2, 5, "A: elegir   B: cancelar", props) ;
	} else {
		SetWindow(width, 6) ;
		SetColor(CD_NORMAL) ;
		snprintf(line, sizeof(line), "La pista %d tiene celdas", channel_) ;
		DrawString(0, 0, line, props) ;
		snprintf(line, sizeof(line), "que no sirven como %s.", optionName[selected_]) ;
		DrawString(0, 1, line, props) ;
		DrawString(0, 2, "Hay que borrarlas.", props) ;
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
	if (!confirming_) {
		if (mask & EPBM_B) {
			EndModal(-1) ;
		} else if (mask & EPBM_UP) {
			selected_ = 1 - selected_ ;
		} else if (mask & EPBM_DOWN) {
			selected_ = 1 - selected_ ;
		} else if (mask & EPBM_A) {
			if (selected_ == current_) {
				EndModal(-1) ;
			} else if (hasContent_) {   // de chains a loops o al revés
				confirming_ = true ;   // ahora sí: avisar antes de borrar
				yes_ = false ;
			} else {
				EndModal(optionType[selected_]) ;
			}
		}
	} else {
		if (mask & EPBM_B) {
			confirming_ = false ;   // regresar a la lista
		} else if (mask & (EPBM_LEFT | EPBM_RIGHT)) {
			yes_ = !yes_ ;
		} else if (mask & EPBM_A) {
			if (yes_) {
				EndModal(optionType[selected_] | TTD_CLEAR) ;
			} else {
				confirming_ = false ;
			}
		}
	}
	isDirty_ = true ;
}
