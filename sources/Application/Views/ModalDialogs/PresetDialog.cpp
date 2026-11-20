#include "PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Player/Player.h"
#include "Application/Utils/Latin1.h"
#include "System/FileSystem/FileSystem.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fstream>
#include <vector>

#define PD_WIDTH 28
#define PD_ROWS 12
#define LIST_ROWS 6
#define TREE_ROWS 8
#define PREVIEW_WAIT 380   // ms quieto en un preset antes de que suene

static const char *previewTmp = "root:presetpreview.wav" ;

#ifdef SDL2
static Uint32 previewWake(Uint32, void *) {
	SDL_Event e ;
	memset(&e, 0, sizeof(e)) ;
	e.type = SDL_WINDOWEVENT ;
	e.window.event = SDL_WINDOWEVENT_EXPOSED ;
	SDL_PushEvent(&e) ;
	return 0 ;
}
#endif

PresetDialog::PresetDialog(View &view, bool forSave, int kind)
    : ModalView(view), forSave_(forSave), kind_(kind), selected_(0), top_(0),
      emptyOptions_(false), col_(1), treeSel_(0), treeTop_(0), previewAt_(0) {
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
	buildTree() ;
	// sin nada que elegir a la derecha, se empieza por las carpetas
	bool any = false ;
	for (size_t i = 0; i < entries_.size(); i++) if (entries_[i].kind != K_UP) any = true ;
	col_ = any ? 1 : 0 ;
}

PresetDialog::~PresetDialog() {
	stopPreview() ;
}

std::string PresetDialog::full(const std::string &n) {
	return folder_.empty() ? n : folder_ + "/" + n ;
}

bool PresetDialog::folderOk(const std::string &f) {
	return kind_ < 0 || forSave_ || InstrumentPresets::FolderHas(f, kind_) ;
}

static int depthOf(const std::string &f) {
	if (f.empty()) return 0 ;
	int d = 1 ;
	for (size_t i = 0; i < f.size(); i++) if (f[i] == '/') d++ ;
	return d ;
}

// la columna de carpetas: las de arriba, y abierta la que tiene la carpeta
// actual (con sus subcarpetas)
void PresetDialog::buildTree() {
	tree_.clear() ;
	std::vector<std::string> folders, presets ;
	InstrumentPresets::ListFolder("", folders, presets) ;
	std::string topSel = folder_.substr(0, folder_.find('/')) ;
	Node n ;
	if (!presets.empty() || forSave_) {   // presets sueltos en la raíz
		n.path = "" ; n.label = "Sueltos" ; n.depth = 0 ;
		tree_.push_back(n) ;
	}
	for (size_t i = 0; i < folders.size(); i++) {
		if (!folderOk(folders[i])) continue ;
		n.path = folders[i] ; n.label = folders[i] ; n.depth = 0 ;
		tree_.push_back(n) ;   // trk2als: solo las categorías; lo de adentro, a la derecha
	}
	// el elegido: la carpeta actual o su antepasado más cercano en el árbol
	treeSel_ = 0 ;
	size_t best = 0 ;
	for (size_t i = 0; i < tree_.size(); i++) {
		const std::string &p = tree_[i].path ;
		bool prefix = p.empty() ? folder_.empty()
		              : (folder_ == p || folder_.compare(0, p.size() + 1, p + "/") == 0) ;
		if (prefix && p.size() + 1 > best) { best = p.size() + 1 ; treeSel_ = int(i) ; }
	}
}

void PresetDialog::load() {
	std::vector<std::string> folders, presets ;
	InstrumentPresets::ListFolder(folder_, folders, presets) ;
	entries_.clear() ;
	Entry e ;
	e.ptype = -1 ; e.zones = 0 ;
	if (forSave_) {
		e.kind = K_HERE ;
		e.name = "[guardar aquí]" ;
		entries_.push_back(e) ;
	}
	// trk2als: la opción vacía de la categoría (Kits, Multicapa, Samples)
	if (emptyOptions_ && !forSave_ && depthOf(folder_) == 1) {
		std::string cat = folder_.substr(0, folder_.find('/')) ;
		int code = 0 ;
		if (cat == InstrumentPresets::KindFolder(InstrumentPresets::PK_KIT)) code = PD_EMPTY_KIT ;
		else if (cat == InstrumentPresets::KindFolder(InstrumentPresets::PK_MULTI)) code = PD_IMPORT_SF2 ;
		else if (cat == InstrumentPresets::KindFolder(InstrumentPresets::PK_SAMPLE)) code = PD_EMPTY_SAMPLE ;
		if (code) {
			e.kind = K_EMPTY ;
			e.zones = code ;
			e.name = code == PD_EMPTY_KIT ? "Kit vacío" : code == PD_IMPORT_SF2 ? "Importar .sf2" : "Sample vacío" ;
			entries_.push_back(e) ;
		}
	}
	// las subcategorías (en la raíz, las categorías están a la izquierda)
	if (depthOf(folder_) >= 1) {
		for (size_t i = 0; i < folders.size(); i++) {
			if (!folderOk(full(folders[i]))) continue ;
			e.kind = K_FOLDER ;
			e.name = folders[i] + "/" ;
			entries_.push_back(e) ;
		}
	}
	if (!forSave_) {
		for (size_t i = 0; i < presets.size(); i++) {
			InstrumentPresets::Info info ;
			InstrumentPresets::Describe(full(presets[i]), info) ;
			if (kind_ >= 0 && info.kind != kind_) continue ;
			e.kind = K_PRESET ;
			e.name = presets[i] ;
			e.ptype = info.kind ;
			e.zones = info.zones ;
			entries_.push_back(e) ;
		}
	}
	selected_ = 0 ;
	top_ = 0 ;
}

// trk2als: atrás (← o B): a la carpeta de arriba, o a las categorías
void PresetDialog::goBack() {
	stopPreview() ;
	if (depthOf(folder_) >= 2) {
		size_t slash = folder_.find_last_of('/') ;
		std::string child = folder_.substr(slash + 1) + "/" ;
		folder_ = folder_.substr(0, slash) ;
		load() ;
		for (size_t i = 0; i < entries_.size(); i++)
			if (entries_[i].kind == K_FOLDER && entries_[i].name == child) selected_ = int(i) ;
		return ;
	}
	col_ = 0 ;
}

// --- la muestra ------------------------------------------------------------

void PresetDialog::schedulePreview() {
	stopPreview() ;
	if (forSave_ || col_ != 1 || entries_.empty()) return ;
	if (entries_[selected_].kind != K_PRESET) return ;
	previewAt_ = SDL_GetTicks() + PREVIEW_WAIT ;
#ifdef SDL2
	SDL_AddTimer(PREVIEW_WAIT + 20, previewWake, 0) ;
#endif
}

void PresetDialog::stopPreview() {
	previewAt_ = 0 ;
	if (!playing_.empty()) {
		Player::GetInstance()->StopStreaming() ;
		playing_.clear() ;
	}
}

bool PresetDialog::Tick() {
	if (previewAt_ && SDL_GetTicks() >= previewAt_) {
		previewAt_ = 0 ;
		startPreview() ;
		return true ;
	}
	return false ;
}

static void put16(std::ofstream &f, int v) { f.put(char(v & 255)) ; f.put(char((v >> 8) & 255)) ; }
static void put32(std::ofstream &f, long v) { put16(f, int(v & 0xFFFF)) ; put16(f, int((v >> 16) & 0xFFFF)) ; }

static bool hasWord(const std::string &name, const char *const *words) {
	std::string low(name) ;
	for (size_t i = 0; i < low.size(); i++) low[i] = tolower(low[i]) ;
	for (int i = 0; words[i]; i++) if (low.find(words[i]) != std::string::npos) return true ;
	return false ;
}

// un compás de kit: bombo, hat, caja, hat... al tempo del proyecto
static bool kitPreview(const InstrumentPresets::Info &info, int tempo, const std::string &out) {
	static const char *const kicks[] = {"kick", "bd", "bombo", "kik", 0} ;
	static const char *const snares[] = {"snare", "sd", "caja", "clap", "rim", 0} ;
	static const char *const hats[] = {"hat", "hh", "shaker", "maraca", "guira", "ride", 0} ;
	int pick[3] = {-1, -1, -1} ;
	const char *const *lists[3] = {kicks, snares, hats} ;
	int nz = (int)info.zoneFiles.size() ;
	if (nz == 0) return false ;
	for (int k = 0; k < 3; k++)
		for (int i = 0; i < nz && pick[k] < 0; i++)
			if (hasWord(info.zoneFiles[i], lists[k])) pick[k] = i ;
	// si los nombres no ayudan: los primeros pads
	for (int k = 0; k < 3; k++) if (pick[k] < 0) pick[k] = k < nz ? k : 0 ;
	// 8 corcheas: B H C H B B C H
	static const int pattern[8] = {0, 2, 1, 2, 0, 0, 1, 2} ;
	const int SR = 44100 ;
	if (tempo < 40) tempo = 120 ;
	long step = long(SR * 30.0 / tempo) ;          // una corchea
	long len = step * 8 + SR / 2 ;
	std::vector<int> mix(len * 2, 0) ;
	for (int k = 0; k < 3; k++) {
		std::string path = info.dir + "/" + info.zoneFiles[pick[k]] ;
		WavFile *w = WavFile::Open(path.c_str()) ;
		if (!w) continue ;
		long n = w->GetSize(-1) ;
		int ch = w->GetChannelCount(-1), sr = w->GetSampleRate(-1) ;
		if (n <= 0 || sr <= 0 || !w->GetBuffer(0, n)) { delete w ; continue ; }
		short *src = (short *)w->GetSampleBuffer(-1) ;
		long outN = long(double(n) * SR / sr) ;
		if (outN > step * 4) outN = step * 4 ;      // los largos, recortados
		for (int s = 0; s < 8; s++) {
			if (pattern[s] != k) continue ;
			long at = step * s ;
			for (long i = 0; i < outN && at + i < len; i++) {
				long j = long(double(i) * sr / SR) ;
				if (j >= n) break ;
				int l = src[j * ch], r = src[j * ch + (ch > 1 ? 1 : 0)] ;
				mix[(at + i) * 2] += l ;
				mix[(at + i) * 2 + 1] += r ;
			}
		}
		delete w ;
	}
	std::ofstream f(out.c_str(), std::ios::binary) ;
	if (!f) return false ;
	long bytes = len * 4 ;
	f.write("RIFF", 4) ; put32(f, 36 + bytes) ; f.write("WAVEfmt ", 8) ;
	put32(f, 16) ; put16(f, 1) ; put16(f, 2) ; put32(f, SR) ; put32(f, SR * 4) ;
	put16(f, 4) ; put16(f, 16) ; f.write("data", 4) ; put32(f, bytes) ;
	for (long i = 0; i < len * 2; i++) {
		int v = mix[i] * 3 / 4 ;
		if (v > 32767) v = 32767 ;
		if (v < -32768) v = -32768 ;
		put16(f, v) ;
	}
	return true ;
}

void PresetDialog::startPreview() {
	if (entries_.empty() || entries_[selected_].kind != K_PRESET) return ;
	std::string name = full(entries_[selected_].name) ;
	InstrumentPresets::Info info ;
	if (!InstrumentPresets::Describe(name, info)) return ;
	Player *player = Player::GetInstance() ;
	// 1. su muestra, si la trae
	Path own(info.dir + "/preview.wav") ;
	if (own.Exists()) {
		player->StartStreaming(own) ;
		playing_ = entries_[selected_].name ;
		isDirty_ = true ;
		return ;
	}
	// 2. un kit: un compás con sus pads
	if (info.kind == InstrumentPresets::PK_KIT) {
		Path tmp(previewTmp) ;
		if (kitPreview(info, viewData_->project_ ? viewData_->project_->GetTempo() : 120, tmp.GetPath())) {
			player->StartStreaming(tmp) ;
			playing_ = entries_[selected_].name ;
			isDirty_ = true ;
		}
		return ;
	}
	// 3. el sample (en una multicapa, el principal; si no, la primera zona)
	std::string file = info.sample ;
	if (file.empty() && !info.zoneFiles.empty()) file = info.zoneFiles[0] ;
	if (file.empty()) return ;
	Path p(info.dir + "/" + file) ;
	if (!p.Exists() && !info.zoneFiles.empty()) p = Path(info.dir + "/" + info.zoneFiles[0]) ;
	if (!p.Exists()) return ;
	player->StartStreaming(p) ;
	playing_ = entries_[selected_].name ;
	isDirty_ = true ;
}

// --- dibujo ----------------------------------------------------------------

void PresetDialog::DrawView() {
	if (SupportsShapes()) drawShapes() ;
	else drawGrid() ;
}

void PresetDialog::drawGrid() {
	GUITextProperties props ;
	char line[PD_WIDTH + 8] ;
	std::string title = forSave_ ? "Guardar en: " : (kind_ == InstrumentPresets::PK_KIT ? "Cambiar de kit: " : "Cargar preset: ") ;
	title += folder_.empty() ? "/" : folder_ ;
	if (title.size() > PD_WIDTH - 2) title = "..." + title.substr(title.size() - (PD_WIDTH - 5)) ;
	int n = (int)entries_.size() ;
	int rows = n < PD_ROWS ? (n ? n : 1) : PD_ROWS ;
	SetWindow(PD_WIDTH, rows + 4) ;
	SetColor(CD_NORMAL) ;
	DrawString(1, 0, title.c_str(), props) ;
	if (selected_ < top_) top_ = selected_ ;
	if (selected_ >= top_ + PD_ROWS) top_ = selected_ - PD_ROWS + 1 ;
	for (int i = 0; i < rows && top_ + i < n; i++) {
		int k = top_ + i ;
		snprintf(line, sizeof(line), " %-*s", PD_WIDTH - 1, entries_[k].name.c_str()) ;
		line[PD_WIDTH] = 0 ;
		props.invert_ = (k == selected_) ;
		SetColor(k == selected_ ? CD_HILITE2 : (entries_[k].kind == K_PRESET ? CD_NORMAL : CD_HILITE1)) ;
		DrawString(0, 2 + i, line, props) ;
	}
	props.invert_ = false ;
	SetColor(CD_NORMAL) ;
	DrawString(1, rows + 3, "A: elegir   B: cancelar", props) ;
}

static GUIColor kindColor(int kind, bool dark) {
	switch (kind) {
	case InstrumentPresets::PK_KIT: return dark ? GUIColor(0xB9, 0x77, 0x2F) : GUIColor(0xE8, 0xB8, 0x6D) ;
	case InstrumentPresets::PK_MULTI: return dark ? GUIColor(0x3D, 0x9C, 0x8C) : GUIColor(0x7F, 0xD1, 0xC2) ;
	default: return dark ? GUIColor(0xB0, 0x4F, 0x7C) : GUIColor(0xE5, 0x8F, 0xB4) ;
	}
}

// el tipo que hay en una carpeta de arriba (para su cuadrito)
static int folderKind(const std::string &f) {
	if (f == InstrumentPresets::KindFolder(InstrumentPresets::PK_KIT)) return InstrumentPresets::PK_KIT ;
	if (f == InstrumentPresets::KindFolder(InstrumentPresets::PK_MULTI)) return InstrumentPresets::PK_MULTI ;
	if (f == InstrumentPresets::KindFolder(InstrumentPresets::PK_SAMPLE)) return InstrumentPresets::PK_SAMPLE ;
	return -1 ;
}

void PresetDialog::drawShapes() {
	const int PX = 8, PY = 8, PW = 624, PH = 464 ;
	DrawPanel(PX, PY, PW, PH) ;
	GUIColor bg = ColorOf(CD_BACKGROUND), fg = ColorOf(CD_NORMAL) ;
	GUIColor acc = ColorOf(CD_HILITE2), dim = Mix(bg, fg, 50), card = Mix(bg, fg, 10) ;
	GUIColor colBg = Mix(bg, fg, 4), selBg = Mix(bg, acc, 18) ;

	// título y dónde estamos
	const char *t = forSave_ ? "GUARDAR EN" : (kind_ == InstrumentPresets::PK_KIT ? "KITS" : "SONIDOS") ;
	DrawText(32, 24, t, acc, 1) ;
	std::string where = folder_.empty() ? std::string("sueltos") : folder_ ;
	for (size_t i = 0; (i = where.find('/', i)) != std::string::npos; i += 3) where.replace(i, 1, " / ") ;
	if (TextLen(where) > 40) where = ".." + where.substr(where.size() - 38) ;
	DrawText(PX + PW - 24 - 8 * TextLen(where), 28, where.c_str(), dim, 0) ;

	// columna de carpetas
	const int TX = 24, TY = 56, TW = 196, TH = 340, TR = 40 ;
	FillRect(TX, TY, TW, TH, colBg, 8) ;
	if (treeSel_ < treeTop_) treeTop_ = treeSel_ ;
	if (treeSel_ >= treeTop_ + TREE_ROWS) treeTop_ = treeSel_ - TREE_ROWS + 1 ;
	for (int i = 0; i < TREE_ROWS && treeTop_ + i < (int)tree_.size(); i++) {
		const Node &n = tree_[treeTop_ + i] ;
		int y = TY + 10 + i * TR ;
		bool sel = (treeTop_ + i == treeSel_) ;
		if (sel) {
			FillRect(TX + 6, y - 4, TW - 12, 30, col_ == 0 ? acc : selBg, 6) ;
		}
		std::string label = n.label ;
		int x = TX + 14 ;
		int fk = n.depth == 0 ? folderKind(n.path) : -1 ;
		if (fk >= 0) {
			FillRect(x, y + 3, 14, 14, kindColor(fk, false), 4) ;
			x += 22 ;
		}
		if (n.depth > 0) x += 12 ;   // las de adentro, corridas
		int room = (TX + TW - 10 - x) / 16 ;
		if (TextLen(label) > room) label = label.substr(0, room > 2 ? room - 2 : 1) + ".." ;
		GUIColor c = sel ? (col_ == 0 ? bg : acc) : (n.depth == 0 ? fg : dim) ;
		DrawText(x, y + 1, label.c_str(), c, 1) ;
	}

	// la lista
	const int LX = 232, LW = 384, LY = 56, RH = 54 ;
	int n = (int)entries_.size() ;
	if (selected_ < top_) top_ = selected_ ;
	if (selected_ >= top_ + LIST_ROWS) top_ = selected_ - LIST_ROWS + 1 ;
	if (n == 0) {
		DrawText(LX + 8, LY + 12, kind_ == InstrumentPresets::PK_KIT ? "Todavía no hay kits aquí" : "Elige una carpeta", fg, 1) ;
		DrawText(LX + 8, LY + 36, "a la izquierda", dim, 0) ;
	}
	for (int i = 0; i < LIST_ROWS && top_ + i < n; i++) {
		const Entry &e = entries_[top_ + i] ;
		int y = LY + i * RH ;
		bool sel = (top_ + i == selected_) ;
		if (sel) {
			FillRect(LX, y, LW, RH - 6, card, 8) ;
			if (col_ == 1) FrameRect(LX, y, LW, RH - 6, acc, 8, 2) ;
		}
		int sx = LX + 8, sy = y + 5, ss = 38 ;
		std::string name = e.name, sub ;
		if (e.kind == K_PRESET) {
			GUIColor c1 = kindColor(e.ptype, false), c2 = kindColor(e.ptype, true) ;
			FillRect(sx, sy, ss, ss, c1, 7) ;
			FillRect(sx + 13, sy + 13, ss - 13, ss - 13, c2, 9) ;
			char b[32] ;
			if (e.ptype == InstrumentPresets::PK_KIT) snprintf(b, sizeof(b), "%d pads", e.zones) ;
			else if (e.ptype == InstrumentPresets::PK_MULTI) snprintf(b, sizeof(b), "multicapa · %d zonas", e.zones) ;
			else snprintf(b, sizeof(b), "sample") ;
			sub = b ;
		} else {
			FrameRect(sx, sy, ss, ss, dim, 7, 2) ;
			const char *ic = e.kind == K_UP ? "<" : (e.kind == K_HERE || e.kind == K_EMPTY) ? "+" : "/" ;
			DrawText(sx + 11, sy + 11, ic, dim, 1) ;
			if (e.kind == K_FOLDER) { name = name.substr(0, name.size() - 1) ; sub = "carpeta" ; }
			if (e.kind == K_UP) { name = "volver" ; sub = "carpeta de arriba" ; }
			if (e.kind == K_HERE) { name = "Guardar aquí" ; sub = folder_.empty() ? "presets" : folder_ ; }
			if (e.kind == K_EMPTY)
				sub = e.zones == PD_EMPTY_KIT ? "armarlo pad por pad" :
				      e.zones == PD_IMPORT_SF2 ? "un soundfont de la samplelib" : "eliges el sample tú" ;
		}
		int nx = sx + ss + 14 ;
		int room = (LX + LW - 50 - nx) / 16 ;
		if (TextLen(name) > room) name = name.substr(0, room - 2) + ".." ;
		DrawText(nx, y + 7, name.c_str(), fg, 1) ;
		DrawText(nx, y + 29, sub.c_str(), dim, 0) ;
		if (sel && col_ == 1) {
			int cx = LX + LW - 42, cy = y + 9 ;
			bool sounding = e.kind == K_PRESET && playing_ == e.name ;
			FillRect(cx, cy, 30, 30, acc, 15) ;
			if (sounding) {   // sonando: dos barritas
				FillRect(cx + 9, cy + 8, 4, 14, bg) ;
				FillRect(cx + 17, cy + 8, 4, 14, bg) ;
			} else {
				for (int k = 0; k < 10; k++) {
					int hh = (10 - k) * 2 - 1 ;
					FillRect(cx + 11 + k, cy + 15 - hh / 2, 1, hh, bg) ;
				}
			}
		}
	}
	if (n > LIST_ROWS) {   // barrita de lugar
		int bh = LIST_ROWS * RH - 6, sy = LY + bh * top_ / n, sh = bh * LIST_ROWS / n ;
		if (sh < 12) sh = 12 ;
		FillRect(LX + LW + 4, LY, 3, bh, card, 1) ;
		FillRect(LX + LW + 4, sy, 3, sh, dim, 1) ;
	}

	// abajo: lo que suena y los botones
	const int BY = 410 ;
	if (!playing_.empty()) {
		std::string s = "sonando: " + playing_ ;
		FillRect(24, BY, 592, 34, Mix(bg, acc, 14), 6) ;
		DrawText(36, BY + 11, s.c_str(), acc, 0) ;
		DrawText(616 - 12 - 8 * 18, BY + 11, "A carga   B atrás", fg, 0) ;
	} else if (forSave_) {
		DrawText(32, BY + 11, "<> columnas   A elige   B cancela", dim, 0) ;
	} else {
		DrawText(32, BY + 11, col_ == 0 ? "A o derecha entra   B cierra" : "A entra / carga   izquierda o B atrás", dim, 0) ;
	}
}

// --- botones ---------------------------------------------------------------

void PresetDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {
}

void PresetDialog::OnFocus() {
	schedulePreview() ;
}

void PresetDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
	if (!pressed) return ;   // soltar el botón que abrió la ventana no cuenta
	isDirty_ = true ;
	int n = (int)entries_.size() ;
	if (col_ == 0) {   // las categorías
		if (mask & EPBM_B) {   // ya afuera: se cierra
			stopPreview() ;
			EndModal(0) ;
			return ;
		}
		int tn = (int)tree_.size() ;
		if (tn == 0) { if (mask & EPBM_A) EndModal(0) ; return ; }
		if (mask == EPBM_UP || mask == EPBM_DOWN) {
			treeSel_ = (treeSel_ + (mask == EPBM_UP ? tn - 1 : 1)) % tn ;
			folder_ = tree_[treeSel_].path ;
			load() ;
			stopPreview() ;
			return ;
		}
		if (mask == EPBM_RIGHT || mask == EPBM_A) {
			if (!entries_.empty()) { col_ = 1 ; schedulePreview() ; }
			return ;
		}
		return ;
	}
	// la lista
	if (mask == EPBM_B || mask == EPBM_LEFT) { goBack() ; return ; }
	if (n == 0) return ;
	if (mask == EPBM_UP) { selected_ = (selected_ + n - 1) % n ; schedulePreview() ; return ; }
	if (mask == EPBM_DOWN) { selected_ = (selected_ + 1) % n ; schedulePreview() ; return ; }
	if (mask == EPBM_L) { selected_ = selected_ >= LIST_ROWS ? selected_ - LIST_ROWS : 0 ; schedulePreview() ; return ; }
	if (mask == EPBM_R) { selected_ = selected_ + LIST_ROWS < n ? selected_ + LIST_ROWS : n - 1 ; schedulePreview() ; return ; }
	if (mask == EPBM_RIGHT && entries_[selected_].kind == K_FOLDER) mask = EPBM_A ;   // → entra
	if (mask == EPBM_A) {
		Entry &e = entries_[selected_] ;
		switch (e.kind) {
		case K_UP:
			goBack() ;
			break ;
		case K_FOLDER:
			folder_ = full(e.name.substr(0, e.name.size() - 1)) ;
			load() ;
			schedulePreview() ;
			break ;
		case K_HERE:
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(1) ;
			return ;
		case K_EMPTY:
			stopPreview() ;
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(e.zones) ;
			return ;
		case K_PRESET:
			stopPreview() ;
			chosen_ = full(e.name) ;
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(1) ;
			return ;
		}
	}
}

std::string PresetDialog::GetSelected() {
	return chosen_ ;
}
