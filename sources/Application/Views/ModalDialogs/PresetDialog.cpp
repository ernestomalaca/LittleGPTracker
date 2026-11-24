#include "Application/Instruments/SamplePool.h"
#include "PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Instruments/WarpStretch.h"
#include "Application/Player/Player.h"
#include "Application/Utils/Latin1.h"
#include "NewProjectDialog.h"
#include "ImportSampleDialog.h"
#include "System/FileSystem/FileSystem.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <fstream>
#include <vector>
#include <algorithm>

#define PD_WIDTH 28
#define PD_ROWS 12
#define LIST_ROWS 6
#define TREE_ROWS 8
#define PREVIEW_WAIT 380   // ms quieto en un preset antes de que suene

static const char *previewTmp = "root:presetpreview.wav" ;

// trk2als: la pre-escucha se prende y apaga con X; vale para todos los
// proyectos (chancho6/preescucha.txt). Prendida de entrada
static int previewOn = -1 ;
static bool previewEnabled() {
	if (previewOn < 0) {
		std::ifstream i(Path("bin:preescucha.txt").GetPath().c_str()) ;
		int v = 1 ;
		if (!(i >> v)) v = 1 ;
		previewOn = v ? 1 : 0 ;
	}
	return previewOn == 1 ;
}
static void setPreviewEnabled(bool on) {
	previewOn = on ? 1 : 0 ;
	std::ofstream o(Path("bin:preescucha.txt").GetPath().c_str()) ;
	o << previewOn << "\n" ;
}

// trk2als: Y: cerrar el navegador al lanzar un sonido (se recuerda, como la pre-escucha)
static int autoCloseOn = -1 ;
static bool autoClose() {
	if (autoCloseOn < 0) {
		std::ifstream i(Path("bin:autocerrar.txt").GetPath().c_str()) ;
		int v = 0 ;
		if (!(i >> v)) v = 0 ;
		autoCloseOn = v ? 1 : 0 ;
	}
	return autoCloseOn == 1 ;
}
static void setAutoClose(bool on) {
	autoCloseOn = on ? 1 : 0 ;
	std::ofstream o(Path("bin:autocerrar.txt").GetPath().c_str()) ;
	o << autoCloseOn << "\n" ;
}

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
      emptyOptions_(false), col_(1), treeSel_(0), treeTop_(0), previewAt_(0), diskGen_(-1) {
	launcher_ = 0 ;
	inClip_ = false ;
	bAt_ = 0 ;
	bHint_ = false ;
	marqLen_ = marqRoom_ = marqShown_ = 0 ;
	pick_ = PICK_NONE ;
	pickInstr_ = -1 ;
	InstrumentPresets::ForgetLibCounts() ;   // por si se agregaron samples (p. ej. desde chancho.local)
	folder_ = InstrumentPresets::LastFolder() ;   // se abre donde se quedó
	if (kind_ >= 0) {   // solo un tipo: en su carpeta (Kits...) si la hay
		const char *kf = InstrumentPresets::KindFolder(kind_) ;
		if (folder_.compare(0, strlen(kf), kf) != 0 || !InstrumentPresets::FolderHas(folder_, kind_))
			folder_ = InstrumentPresets::FolderHas(kf, kind_) || forSave_ ? kf : "" ;
		if (forSave_) {   // trk2als: se guarda en su categoría (se crea si no existe)
			Path base("root:presets") ;
			if (!base.Exists()) FileSystem::GetInstance()->MakeDir(base.GetPath().c_str()) ;
			Path cat = base.Descend(kf) ;
			if (!cat.Exists()) FileSystem::GetInstance()->MakeDir(cat.GetPath().c_str()) ;
			if (folder_.compare(0, strlen(kf), kf) != 0) folder_ = kf ;
		}
	}
	load() ;
	if (entries_.empty() && !folder_.empty()) {    // la carpeta ya no existe
		folder_ = "" ;
		load() ;
	}
	buildTree() ;
	// si la carpeta no es de ninguna categoría a la vista (p. ej. la raíz sin
	// sueltos), se abre la primera categoría
	if (!tree_.empty()) {
		const std::string &p = tree_[treeSel_].path ;
		bool inside = p.empty() ? folder_.empty()
		              : (folder_ == p || folder_.compare(0, p.size() + 1, p + "/") == 0) ;
		if (!inside) {
			folder_ = p ;
			load() ;
		}
	}
	// sin nada que elegir a la derecha, se empieza por las carpetas
	bool any = false ;
	for (size_t i = 0; i < entries_.size(); i++) if (entries_[i].kind != K_UP) any = true ;
	col_ = any ? 1 : 0 ;
}

// la última carpeta de la samplelib (aparte de la de los presets)
static std::string &pickLast() {
	static std::string last ;
	return last ;
}

int PresetDialog::pickWhat() {
	using namespace InstrumentPresets ;
	return pick_ == PICK_SF2 ? LIB_SF2 : pick_ == PICK_ANY ? (LIB_WAV | LIB_SF2) : LIB_WAV ;
}

void PresetDialog::SetPick(int mode, int toInstr, int current) {
	pick_ = mode ;
	pickInstr_ = toInstr ;
	pickCurrent_.clear() ;
	// el archivo que tiene ahora, si viene de la samplelib: palomita y flechas
	SamplePool *pool = SamplePool::GetInstance() ;
	if (current >= 0 && current < pool->GetNameListSize()) {
		std::string src = pool->SourcePath(current) ;
		std::string lib = InstrumentPresets::LibPath(InstrumentPresets::LibFolder()).GetPath() ;
		if (src.size() > lib.size() && src.compare(0, lib.size() + 1, lib + "/") == 0)
			pickCurrent_ = InstrumentPresets::LibFolder() + src.substr(lib.size()) ;
	}
	folder_ = pickLast() ;
	if (!InstrumentPresets::IsLib(folder_) || !InstrumentPresets::LibPath(folder_).Exists())
		folder_ = InstrumentPresets::LibFolder() ;
	if (!pickCurrent_.empty())   // se abre donde está lo que tiene
		folder_ = pickCurrent_.substr(0, pickCurrent_.rfind('/')) ;
	load() ;
	if (entries_.empty() && folder_ != InstrumentPresets::LibFolder()) {   // ya no hay nada ahí
		folder_ = InstrumentPresets::LibFolder() ;
		load() ;
	}
	buildTree() ;
	for (size_t i = 0; i < entries_.size(); i++)
		if (entries_[i].kind == K_PRESET && full(entries_[i].name) == pickCurrent_) selected_ = int(i) ;
	if (selected_ >= LIST_ROWS) top_ = selected_ - LIST_ROWS + 1 ;
	col_ = entries_.empty() ? 0 : 1 ;
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
	if (pick_) {   // trk2als: la samplelib: su raíz y sus carpetas de arriba
		Node n ;
		std::string lf = InstrumentPresets::LibFolder() ;
		n.path = lf ; n.label = "Librería" ; n.depth = 0 ;
		tree_.push_back(n) ;
		std::vector<std::string> folders, files ;
		InstrumentPresets::ListLib(lf, folders, files, pickWhat()) ;
		for (size_t i = 0; i < folders.size(); i++) {
			if (!InstrumentPresets::CountLib(lf + "/" + folders[i], pickWhat())) continue ;
			n.path = lf + "/" + folders[i] ; n.label = folders[i] ; n.depth = 0 ;
			tree_.push_back(n) ;
		}
		treeSel_ = 0 ;
		size_t best = 0 ;
		for (size_t i = 0; i < tree_.size(); i++) {
			const std::string &p = tree_[i].path ;
			bool prefix = folder_ == p || folder_.compare(0, p.size() + 1, p + "/") == 0 ;
			if (prefix && p.size() + 1 > best) { best = p.size() + 1 ; treeSel_ = int(i) ; }
		}
		return ;
	}
	std::vector<std::string> folders, presets ;
	InstrumentPresets::ListFolder("", folders, presets) ;
	std::string topSel = folder_.substr(0, folder_.find('/')) ;
	Node n ;
	bool locked = forSave_ && kind_ >= 0 ;   // guardar: solo su categoría
	if (!presets.empty() && !forSave_) {   // presets sueltos en la raíz
		n.path = "" ; n.label = "Sueltos" ; n.depth = 0 ;
		tree_.push_back(n) ;
	}
	for (size_t i = 0; i < folders.size(); i++) {
		if (!folderOk(folders[i])) continue ;
		if (locked && folders[i] != InstrumentPresets::KindFolder(kind_)) continue ;
		n.path = folders[i] ; n.label = folders[i] ; n.depth = 0 ;
		tree_.push_back(n) ;   // trk2als: solo las categorías; lo de adentro, a la derecha
	}
	// trk2als: Samples siempre que haya samplelib (aunque no haya presets de
	// sample): ahí están "Sample vacío" y la Librería
	const char *sf = InstrumentPresets::KindFolder(InstrumentPresets::PK_SAMPLE) ;
	if (!forSave_ && (kind_ < 0 || kind_ == InstrumentPresets::PK_SAMPLE) &&
	    std::find(folders.begin(), folders.end(), std::string(sf)) == folders.end() &&
	    InstrumentPresets::LibExists()) {
		n.path = sf ; n.label = sf ; n.depth = 0 ;
		tree_.push_back(n) ;
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
	entries_.clear() ;
	Entry e ;
	e.ptype = -1 ; e.zones = 0 ;
	if (InstrumentPresets::IsLib(folder_)) {   // trk2als: dentro de la samplelib
		int what = pick_ ? pickWhat() : (int)InstrumentPresets::LIB_WAV ;
		InstrumentPresets::ListLib(folder_, folders, presets, what) ;
		for (size_t i = 0; i < folders.size(); i++) {
			e.kind = K_FOLDER ;
			e.name = folders[i] + "/" ;
			e.zones = InstrumentPresets::CountLib(full(folders[i]), what) ;
			if (e.zones) entries_.push_back(e) ;   // carpetas sin .wav: no estorban
		}
		for (size_t i = 0; i < presets.size(); i++) {   // cada .wav: como un preset de sample
			e.kind = K_PRESET ;
			e.name = presets[i] ;
			std::string low = presets[i] ;
			for (size_t k = 0; k < low.size(); k++) low[k] = tolower(low[k]) ;
			bool sf2 = low.size() > 4 && low.compare(low.size() - 4, 4, ".sf2") == 0 ;
			e.ptype = sf2 ? InstrumentPresets::PK_MULTI : InstrumentPresets::PK_SAMPLE ;
			e.zones = 0 ;
			entries_.push_back(e) ;
		}
		selected_ = 0 ;
		top_ = 0 ;
		return ;
	}
	InstrumentPresets::ListFolder(folder_, folders, presets) ;
	if (forSave_) {
		e.kind = K_HERE ;
		e.name = "[guardar aquí]" ;
		entries_.push_back(e) ;
		if (!folder_.empty()) {
			e.kind = K_NEWDIR ;
			e.name = "Nueva carpeta" ;
			entries_.push_back(e) ;
		}
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
	// trk2als: en Samples, la samplelib: cualquier .wav como instrumento
	if (!forSave_ && (kind_ < 0 || kind_ == InstrumentPresets::PK_SAMPLE) &&
	    folder_ == InstrumentPresets::KindFolder(InstrumentPresets::PK_SAMPLE) && InstrumentPresets::LibExists()) {
		e.kind = K_FOLDER ;
		std::string lf = InstrumentPresets::LibFolder() ;
		e.name = lf.substr(lf.find('/') + 1) + "/" ;
		e.zones = InstrumentPresets::CountLib(lf) ;
		entries_.push_back(e) ;
		e.zones = 0 ;
	}
	// las subcategorías (en la raíz, las categorías están a la izquierda)
	if (depthOf(folder_) >= 1) {
		for (size_t i = 0; i < folders.size(); i++) {
			if (!folderOk(full(folders[i]))) continue ;
			e.kind = K_FOLDER ;
			e.name = folders[i] + "/" ;
			e.zones = countSounds(full(folders[i])) ;   // cuántos sonidos tiene
			entries_.push_back(e) ;
			e.zones = 0 ;
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

// el nombre corto de un sonido (sin carpeta ni .wav)
static std::string SoundLauncherShort(const std::string &n) {
	std::string leaf = n.substr(n.rfind('/') == std::string::npos ? 0 : n.rfind('/') + 1) ;
	if (leaf.size() > 4 && (leaf.compare(leaf.size() - 4, 4, ".wav") == 0 || leaf.compare(leaf.size() - 4, 4, ".WAV") == 0))
		leaf = leaf.substr(0, leaf.size() - 4) ;
	return leaf ;
}

// trk2als: el sonido que ya tiene la pista: palomita (y en sus carpetas)
bool PresetDialog::isCurrent(const Entry &e) {
	if (pick_) return e.kind == K_PRESET && !pickCurrent_.empty() && full(e.name) == pickCurrent_ ;
	if (!launcher_ || e.kind != K_PRESET) return false ;
	std::string cur = launcher_->LaunchCurrent() ;
	if (cur.empty()) return false ;
	if (cur.find('/') == std::string::npos) return cur == e.name ;
	return cur == full(e.name) ;
}

bool PresetDialog::holdsCurrent(const std::string &folder) {
	if (folder.empty() || (!launcher_ && !pick_)) return false ;
	std::string cur = pick_ ? pickCurrent_ : launcher_->LaunchCurrent() ;
	return cur.size() > folder.size() && cur.compare(0, folder.size(), folder) == 0 && cur[folder.size()] == '/' ;
}

static void NewFolderCallback(View &v, ModalView &dialog) {
	if (dialog.GetReturnCode() > 0)
		((PresetDialog &)v).OnNewFolder(((NewProjectDialog &)dialog).GetName()) ;
}

void PresetDialog::OnNewFolder(const std::string &name) {
	if (name.empty()) return ;
	Path p(std::string("root:presets/") + full(name)) ;
	if (!p.Exists()) FileSystem::GetInstance()->MakeDir(p.GetPath().c_str()) ;
	folder_ = full(name) ;
	load() ;
	selected_ = 0 ;   // "Guardar aquí"
	isDirty_ = true ;
}

// cuántos sonidos hay en una carpeta (con lo de sus subcarpetas)
int PresetDialog::countSounds(const std::string &f) {
	std::vector<std::string> folders, presets ;
	InstrumentPresets::ListFolder(f, folders, presets) ;
	int n = 0 ;
	for (size_t i = 0; i < presets.size(); i++)
		if (kind_ < 0 || InstrumentPresets::KindOf(f + "/" + presets[i]) == kind_) n++ ;
	for (size_t i = 0; i < folders.size(); i++) n += countSounds(f + "/" + folders[i]) ;
	return n ;
}

// trk2als: atrás (← o B): a la carpeta de arriba, o a las categorías
void PresetDialog::goBack() {
	stopPreview() ;
	if (depthOf(folder_) >= 2 && !(pick_ && folder_ == InstrumentPresets::LibFolder())) {
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
	if (forSave_ || col_ != 1 || entries_.empty() || !previewEnabled()) return ;
	// trk2als: la pre-escucha la decide X (también con la música andando:
	// los kits suenan a tempo y en fase; en un jam conviene apagarla)
	if (entries_[selected_].kind != K_PRESET) return ;
	previewAt_ = SDL_GetTicks() + PREVIEW_WAIT ;
#ifdef SDL2
	SDL_AddTimer(PREVIEW_WAIT + 20, previewWake, 0) ;
#endif
}

void PresetDialog::stopPreview() {
	previewAt_ = 0 ;
	SampleFilePreviewCancel() ;   // trk2als: la prueba en complex que se estiraba ya no
	if (inClip_) {
		inClip_ = false ;
		if (launcher_) launcher_->LaunchPreviewStop() ;
		playing_.clear() ;
	}
	if (!playing_.empty()) {
		Player::GetInstance()->StopStreaming() ;
		playing_.clear() ;
	}
}

static const unsigned long B_HOLD = 450 ;   // ms con B apretado: cierra (al soltar)

static bool bLong(unsigned long at) { return at && SDL_GetTicks() - at >= B_HOLD ; }

bool PresetDialog::Tick() {
	if (marqLen_ > marqRoom_ && marqRoom_ > 0 && marqueeOffset(marqLen_) != marqShown_) {
		isDirty_ = true ;   // la marquesina avanza
		return true ;
	}
	if (!bHint_ && bLong(bAt_)) {   // ya cuenta como largo: se avisa
		bHint_ = true ;
		isDirty_ = true ;
		return true ;
	}
	if (previewAt_ && SDL_GetTicks() >= previewAt_) {
		previewAt_ = 0 ;
		startPreview() ;
		return true ;
	}
	if (SampleFilePreviewTick()) return true ;   // trk2als: la prueba en complex ya está
	if (pick_ == PICK_LOOP && WarpDisk::Generation() != diskGen_) {   // se preparó algo: el puntito
		diskGen_ = WarpDisk::Generation() ;
		isDirty_ = true ;
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

// trk2als: la pre-escucha a tempo: golpes sueltos, no un beat (que no
// enganche): "un baterista probando su batería"
struct Hit {
	std::string file ;
	double at ;       // en tiempos (negras) desde el inicio
	double pitch ;    // 1 = su altura; 2 = una octava arriba
} ;

static bool renderHits(const std::vector<Hit> &hits, int beats, int tempo, const std::string &out, bool exact) {
	const int SR = 44100 ;
	if (tempo < 20) tempo = 120 ;
	double beatF = SR * 60.0 / tempo ;
	long len = long(beatF * beats) + (exact ? 0 : SR / 2) ;   // en loop con la música: justo
	std::vector<int> mix(len * 2, 0) ;
	for (size_t h = 0; h < hits.size(); h++) {
		WavFile *w = WavFile::Open(hits[h].file.c_str()) ;
		if (!w) continue ;
		long n = w->GetSize(-1) ;
		int ch = w->GetChannelCount(-1), sr = w->GetSampleRate(-1) ;
		if (n <= 0 || sr <= 0 || !w->GetBuffer(0, n)) { delete w ; continue ; }
		short *src = (short *)w->GetSampleBuffer(-1) ;
		double rate = double(sr) / SR * hits[h].pitch ;   // cuadros del archivo por cuadro de salida
		long outN = long(double(n) / rate) ;
		long maxN = long(beatF * 0.95) ;   // cada golpe dura a lo más un tiempo
		if (outN > maxN) outN = maxN ;
		long at = long(hits[h].at * beatF) ;
		for (long i = 0; i < outN && at + i < len; i++) {
			long j = long(i * rate) ;
			if (j >= n) break ;
			int l = src[j * ch], r = src[j * ch + (ch > 1 ? 1 : 0)] ;
			long left = outN - i ;   // sin click al recortar
			if (left < 256) { l = int(l * left / 256) ; r = int(r * left / 256) ; }
			mix[(at + i) * 2] += l ;
			mix[(at + i) * 2 + 1] += r ;
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

// un kit: bombo, bombo, caja, caja | hat-hat, hat-hat, tom, platillo (2 compases)
static int kitHits(const InstrumentPresets::Info &info, std::vector<Hit> &hits) {
	static const char *const kicks[] = {"kick", "bd", "bombo", "kik", 0} ;
	static const char *const snares[] = {"snare", "sd", "caja", "clap", "rim", 0} ;
	static const char *const hats[] = {"hat", "hh", "shaker", "maraca", "guira", 0} ;
	static const char *const toms[] = {"tom", "conga", "bongo", "perc", 0} ;
	static const char *const cyms[] = {"crash", "cymbal", "ride", "splash", "china", 0} ;
	const char *const *lists[5] = {kicks, snares, hats, toms, cyms} ;
	int nz = (int)info.zoneFiles.size() ;
	if (nz == 0) return 0 ;
	int pick[5] = {-1, -1, -1, -1, -1} ;
	for (int k = 0; k < 5; k++)
		for (int i = 0; i < nz && pick[k] < 0; i++)
			if (hasWord(info.zoneFiles[i], lists[k])) pick[k] = i ;
	for (int k = 0; k < 5; k++) if (pick[k] < 0) pick[k] = k < nz ? k : nz - 1 ;   // los nombres no ayudan: los primeros pads
	// 0 bombo, 1 caja, 2 hat, 3 tom, 4 platillo; en tiempos
	static const struct { int piece ; double at ; } seq[] = {
		{0, 0}, {0, 1}, {1, 2}, {1, 3}, {2, 4}, {2, 4.5}, {2, 5}, {2, 5.5}, {3, 6}, {4, 7}} ;
	for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		Hit h ;
		h.file = info.dir + "/" + info.zoneFiles[pick[seq[i].piece]] ;
		h.at = seq[i].at ;
		h.pitch = 1 ;
		hits.push_back(h) ;
	}
	return 8 ;
}

// multicapa: una zona grave, una media y una aguda; un sample: su nota, una
// quinta y una octava. Negras sueltas en un compás (no es una melodía)
static int noteHits(const InstrumentPresets::Info &info, std::vector<Hit> &hits) {
	Hit h ;
	h.pitch = 1 ;
	int nz = (int)info.zoneFiles.size() ;
	if (nz > 0) {
		int z[4] = {0, nz / 2, nz - 1, nz / 2} ;
		for (int i = 0; i < 4; i++) { h.file = info.dir + "/" + info.zoneFiles[z[i]] ; h.at = i ; hits.push_back(h) ; }
		return 4 ;
	}
	if (info.sample.empty()) return 0 ;
	h.file = info.dir + "/" + info.sample ;
	static const double p[4] = {1.0, 1.4983, 2.0, 1.4983} ;   // 0, +7, +12, +7 semitonos
	for (int i = 0; i < 4; i++) { h.at = i ; h.pitch = p[i] ; hits.push_back(h) ; }
	return 4 ;
}

void PresetDialog::startPreview() {
	if (entries_.empty() || entries_[selected_].kind != K_PRESET) return ;
	if (pick_) {   // trk2als: el archivo (un loop, a tempo y en fase; un .sf2, su C3)
		SampleFilePreview(InstrumentPresets::LibPath(full(entries_[selected_].name)), viewData_, pickInstr_,
		                  pick_ == PICK_LOOP) ;
		playing_ = entries_[selected_].name ;
		isDirty_ = true ;
		return ;
	}
	std::string name = full(entries_[selected_].name) ;
	// 0. si en la pista suena un clip MIDI: el clip con este sonido
	if (launcher_ && launcher_->LaunchPreview(name)) {
		inClip_ = true ;
		playing_ = entries_[selected_].name ;
		isDirty_ = true ;
		return ;
	}
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
	// 2. la prueba de sonido a tempo: un kit, sus piezas una por una; una
	// multicapa o un sample, unas notas sueltas. Un .wav de la librería, tal cual
	if (!InstrumentPresets::IsLib(name)) {
		std::vector<Hit> hits ;
		int beats = info.kind == InstrumentPresets::PK_KIT ? kitHits(info, hits) : noteHits(info, hits) ;
		Path tmp(previewTmp) ;
		int tempo = viewData_->project_ ? viewData_->project_->GetTempo() : 120 ;
		bool running = player->IsRunning() ;
		if (beats && renderHits(hits, beats, tempo, tmp.GetPath(), running)) {
			if (running) {   // con la música andando: a tempo, en fase y en loop
				double beat = 60.0 / tempo ;
				double t = player->GetPlayTime() ;
				long next = long(floor(t / beat)) + 1 ;
				player->StartStreaming(tmp, 1.0, next * beat - t, double(next % beats) / beats, true) ;
			} else
				player->StartStreaming(tmp) ;
			playing_ = entries_[selected_].name ;
			isDirty_ = true ;
		}
		return ;
	}
	// 3. un .wav de la librería: el archivo
	Path p(info.dir + "/" + info.sample) ;
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
	// el tema puede darlos con ICONCOLORS="sample,kit,multicapa" (el oscuro sale solo)
	GUIColor t(0, 0, 0) ;
	int ti = kind == InstrumentPresets::PK_KIT ? 1 : kind == InstrumentPresets::PK_MULTI ? 2 : 0 ;
	if (View::ThemeColor("ICONCOLORS", t, ti))
		return dark ? GUIColor(t._r * 3 / 4, t._g * 3 / 4, t._b * 3 / 4) : t ;
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

// trk2als: una palomita en un circulito (el sonido que tiene la pista)
void PresetDialog::checkBadge(int bx, int by, GUIColor fg, GUIColor bg) {
	FillRect(bx - 2, by - 2, 19, 19, bg, 9) ;
	FillRect(bx, by, 15, 15, fg, 7) ;
	for (int k = 0; k < 3; k++) FillRect(bx + 3 + k, by + 7 + k, 2, 2, bg) ;
	for (int k = 0; k < 6; k++) FillRect(bx + 5 + k, by + 9 - k, 2, 2, bg) ;
}

void PresetDialog::drawShapes() {
	marqLen_ = marqRoom_ = 0 ;
	const int PX = 8, PY = 8, PW = 624, PH = 464 ;
	DrawPanel(PX, PY, PW, PH) ;
	GUIColor bg = ColorOf(CD_BACKGROUND), fg = ColorOf(CD_NORMAL) ;
	GUIColor acc = ColorOf(CD_HILITE2), dim = Mix(bg, fg, 50), card = Mix(bg, fg, 10) ;
	GUIColor colBg = Mix(bg, fg, 4), selBg = Mix(bg, acc, 18) ;

	// título y dónde estamos
	const char *t = pick_ == PICK_SF2 ? "SOUNDFONTS" : pick_ == PICK_LOOP ? "LOOPS" : pick_ ? "SAMPLES" :
	                forSave_ ? "GUARDAR EN" : (kind_ == InstrumentPresets::PK_KIT ? "KITS" : "SONIDOS") ;
	DrawText(32, 24, t, acc, 1) ;
	std::string where = folder_.empty() ? std::string("sueltos") : folder_ ;
	if (pick_ && InstrumentPresets::IsLib(where)) where = where.substr(where.find('/') + 1) ;   // "Librería/Drums"
	for (size_t i = 0; (i = where.find('/', i)) != std::string::npos; i += 3) where.replace(i, 1, " / ") ;
	{   // trk2als: grande, en lo que deja el título
		int room = (PX + PW - 24 - (32 + 16 * TextLen(t) + 24)) / 16 ;
		std::string lw = ToLatin1(where) ;
		if (room > 2 && (int)lw.size() > room) lw = ".." + lw.substr(lw.size() - (room - 2)) ;
		where = FromLatin1(lw) ;
		DrawText(PX + PW - 24 - 16 * TextLen(where), 24, where.c_str(), dim, 1) ;
	}

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
		int room = (TX + TW - (holdsCurrent(n.path) ? 30 : 10) - x) / 16 ;
		if (TextLen(label) > room) label = label.substr(0, room > 2 ? room - 2 : 1) + ".." ;
		GUIColor c = sel ? (col_ == 0 ? bg : acc) : (n.depth == 0 ? fg : dim) ;
		DrawText(x, y + 1, label.c_str(), c, 1) ;
		if (holdsCurrent(n.path))   // por aquí está lo que suena: una flechita (la palomita es solo del sonido)
			DrawText(TX + TW - 22, y + 1, ">", sel && col_ == 0 ? bg : acc, 1) ;
	}

	// la lista
	const int LX = 232, LW = 384, LY = 56, RH = 54 ;
	int n = (int)entries_.size() ;
	if (selected_ < top_) top_ = selected_ ;
	if (selected_ >= top_ + LIST_ROWS) top_ = selected_ - LIST_ROWS + 1 ;
	if (n == 0) {
		DrawText(LX + 8, LY + 12, kind_ == InstrumentPresets::PK_KIT ? "Todavía no hay kits aquí" : "Elige una carpeta", fg, 1) ;
		DrawText(LX + 8, LY + 36, "a la izquierda", dim, 1) ;
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
		bool folderHolds = false ;
		if (e.kind == K_PRESET) {
			GUIColor c1 = kindColor(e.ptype, false), c2 = kindColor(e.ptype, true) ;
			FillRect(sx, sy, ss, ss, c1, 7) ;
			FillRect(sx + 13, sy + 13, ss - 13, ss - 13, c2, 9) ;
			if (isCurrent(e)) checkBadge(sx + ss - 15, sy + ss - 15, fg, bg) ;   // el que tiene la pista
			char b[32] ;
			if (e.ptype == InstrumentPresets::PK_KIT) snprintf(b, sizeof(b), "%d pads", e.zones) ;
			else if (e.ptype == InstrumentPresets::PK_MULTI) snprintf(b, sizeof(b), "%d zonas", e.zones) ;
			else b[0] = 0 ;   // un sample: ya lo dice su apartado
			sub = b ;
			if (InstrumentPresets::IsLib(folder_)) {   // un archivo de la samplelib
				if (name.size() > 4) name = name.substr(0, name.size() - 4) ;
				sub = e.ptype == InstrumentPresets::PK_MULTI ? "soundfont" : "" ;   // un sample: ya se sabe
			}
		} else if (e.kind == K_FOLDER || e.kind == K_NEWDIR) {
			// una carpetita, del color de su categoría
			int fk = folderKind(folder_.substr(0, folder_.find('/'))) ;
			GUIColor c1 = fk >= 0 ? kindColor(fk, false) : Mix(bg, fg, 55) ;
			GUIColor c2 = fk >= 0 ? kindColor(fk, true) : Mix(bg, fg, 35) ;
			if (e.kind == K_NEWDIR) {   // la nueva: solo el contorno y un +
				FrameRect(sx + 2, sy + 4, 16, 10, c1, 3, 2) ;
				FrameRect(sx, sy + 10, ss, ss - 12, c1, 6, 2) ;
				DrawText(sx + 11, sy + 15, "+", c1, 1) ;
			} else {
				FillRect(sx + 2, sy + 4, 16, 10, c2, 3) ;
				FillRect(sx, sy + 10, ss, ss - 12, c1, 6) ;
				FillRect(sx, sy + 10, ss, 5, c2, 2) ;
				name = name.substr(0, name.size() - 1) ;
				char b[24] ;
				snprintf(b, sizeof(b), e.zones == 1 ? "1 sonido" : "%d sonidos", e.zones) ;
				sub = b ;
				if (holdsCurrent(full(name))) {   // aquí adentro está lo que suena: cuál
					folderHolds = true ;
					sub += std::string(" · ") + SoundLauncherShort(pick_ ? pickCurrent_ : launcher_->LaunchCurrent()) ;
				}
			}
			if (e.kind == K_NEWDIR) sub = "una subcategoría aquí" ;
		} else {
			FrameRect(sx, sy, ss, ss, dim, 7, 2) ;
			const char *ic = e.kind == K_UP ? "<" : e.kind == K_HERE ? ">" : "+" ;
			DrawText(sx + 11, sy + 11, ic, dim, 1) ;
			if (e.kind == K_UP) { name = "volver" ; sub = "carpeta de arriba" ; }
			if (e.kind == K_HERE) { name = "Guardar aquí" ; sub = folder_.empty() ? "presets" : folder_ ; }
			if (e.kind == K_EMPTY)
				sub = e.zones == PD_EMPTY_KIT ? "armarlo pad por pad" :
				      e.zones == PD_IMPORT_SF2 ? "Carga un soundfont" : "eliges el sample tú" ;
		}
		// trk2als: lanzado a la pista: cargando / esperando el 1 / sonando
		int lst = PresetLauncher::LS_NONE, lpm = 0 ;
		if (launcher_ && e.kind == K_PRESET) lst = launcher_->LaunchState(full(e.name), lpm) ;
		if (lst == PresetLauncher::LS_LOADING) sub = "cargando..." ;
		else if (lst == PresetLauncher::LS_WAITING) sub = "entra en el 1" ;
		else if (lst == PresetLauncher::LS_SOUNDING) sub = "en la pista" ;
		if (lst != PresetLauncher::LS_NONE) {
			GUIColor lc = lst == PresetLauncher::LS_SOUNDING ? fg : acc ;
			if (!sel) FrameRect(LX, y, LW, RH - 6, lst == PresetLauncher::LS_SOUNDING ? dim : acc, 8, 2) ;
			if (lst != PresetLauncher::LS_SOUNDING) {
				// la barrita, como la de un clip: lo que falta se va acortando
				int bx = sx + ss + 14, bw = LX + LW - 56 - bx, by = y + RH - 11 ;
				int done = bw * lpm / 1000 ;
				FillRect(bx, by, bw, 3, Mix(bg, fg, 18), 1) ;
				if (done > 0) FillRect(bx, by, done, 3, lc, 1) ;
			}
		}
		int nx = sx + ss + 14 ;
		int room = (LX + LW - 50 - nx) / 16 ;
		if (TextLen(name) > room) {
			// trk2als: el elegido, al quedarse quieto, pasa completo en marquesina
			std::string ln = ToLatin1(name) ;
			int off = (sel && col_ == 1) ? marqueeOffset((int)ln.size()) : 0 ;
			if (sel && col_ == 1) { marqLen_ = (int)ln.size() ; marqRoom_ = room ; marqShown_ = off ; }
			if (off > 0) {
				std::string loop = ln + "   " + ln ;
				name = FromLatin1(loop.substr(off, room)) ;
			} else name = FromLatin1(ln.substr(0, room - 2)) + ".." ;
		}
		DrawText(nx, sub.empty() ? y + (RH - 6 - 16) / 2 : y + 7, name.c_str(), fg, 1) ;   // sin subtítulo, al centro
		// trk2als: un loop ya preparado en la tarjeta para este tempo (como
		// el .asd de Live): entra sin esperar. Un puntito verde
		// (un aro del mismo verde: preparado, pero para otro tempo)
		int ready = 0 ;
		if (pick_ == PICK_LOOP && e.kind == K_PRESET)
			ready = WarpDisk::Prepared(InstrumentPresets::LibPath(full(e.name)).GetPath().c_str(),
			                           viewData_->project_->GetTempo()) ;
		if (ready) {
			int dx = nx + 16 * TextLen(name) + 10 ;
			GUIColor green = LinkGreen() ;
			if (ready == 2) FillRect(dx, y + 15, 9, 9, green, 4) ;
			else FrameRect(dx, y + 15, 9, 9, green, 4, 2) ;
		}
		{
			int sroom = (LX + LW - 50 - nx) / 16 ;
			if (TextLen(sub) > sroom) sub = FromLatin1(ToLatin1(sub).substr(0, sroom)) ;
			DrawText(nx, y + 27, sub.c_str(), dim, 1) ;
		}
		if (lst == PresetLauncher::LS_SOUNDING && !(sel && col_ == 1)) {   // en la pista: el play de los clips
			int tx = LX + LW - 30, ty = y + 15 ;
			for (int k = 0; k < 9; k++) FillRect(tx + k, ty + k / 2, 1, 10 - (k / 2) * 2, fg) ;
		}
		if (folderHolds && !(sel && col_ == 1)) DrawText(LX + LW - 34, y + 14, ">", acc, 1) ;
		if (sel && col_ == 1) {
			int cx = LX + LW - 42, cy = y + 9 ;
			bool sounding = e.kind == K_PRESET && playing_ == e.name ;
			FillRect(cx, cy, 30, 30, acc, 15) ;
			if (lst == PresetLauncher::LS_SOUNDING || lst == PresetLauncher::LS_LOADING || lst == PresetLauncher::LS_WAITING) {
				// ya está en la pista (o va): A cierra, una palomita
				for (int k = 0; k < 5; k++) FillRect(cx + 8 + k, cy + 14 + k, 3, 3, bg) ;
				for (int k = 0; k < 9; k++) FillRect(cx + 12 + k, cy + 18 - k, 3, 3, bg) ;
			} else if (sounding) {   // sonando: dos barritas
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
	// trk2als: todo con letra grande: ayudas cortas, y X / Y como palabras que
	// se prenden (acento) o se apagan (gris)
	const int R = 616 - 12 ;
	if (bHint_) {   // B apretado un rato
		FillRect(24, BY, 592, 34, Mix(bg, acc, 22), 6) ;
		DrawText(36, BY + 9, "suelta B: sale del navegador", acc, 1) ;
	} else if (!playing_.empty()) {
		const char *act = launcher_ ? "A lanza" : pick_ ? "A elige" : "A carga" ;
		std::string s = (inClip_ ? "en el clip: " : "sonando: ") + playing_ ;
		int room = (R - 16 * 7 - 16 - 36) / 16 ;
		if (TextLen(s) > room) s = FromLatin1(ToLatin1(s).substr(0, room)) ;
		FillRect(24, BY, 592, 34, Mix(bg, acc, 14), 6) ;
		DrawText(36, BY + 9, s.c_str(), acc, 1) ;
		DrawText(R - 16 * 7, BY + 9, act, fg, 1) ;
	} else {
		const char *h ;
		if (forSave_) h = col_ == 0 ? "A entra · B cancela" : "A elige · < atrás" ;
		else if (launcher_) h = col_ == 0 ? "A entra · B cierra" : "A lanza · B atrás" ;
		else h = col_ == 0 ? "A entra · B cierra" : pick_ ? "A elige · < atrás" : "A carga · < atrás" ;
		DrawText(32, BY + 9, h, dim, 1) ;
		if (!forSave_) {
			int x = R ;
			if (launcher_) {
				x -= 16 * 8 ;
				DrawText(x, BY + 9, "Y cierra", autoClose() ? acc : dim, 1) ;
				x -= 16 ;
			}
			x -= 16 * 5 ;
			DrawText(x, BY + 9, "X oye", previewEnabled() ? acc : dim, 1) ;
		}
	}
}

// --- botones ---------------------------------------------------------------

void PresetDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {
}

void PresetDialog::OnFocus() {
	schedulePreview() ;
}

void PresetDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
	// trk2als: B apretado un rato y soltado: sale del navegador desde
	// cualquier carpeta (al apretarlo ya subió una; donde quedó se recuerda)
	if (!pressed) {
		if (bAt_ && !(mask & EPBM_B)) {
			bool hold = bLong(bAt_) ;
			bAt_ = 0 ;
			bHint_ = false ;
			isDirty_ = true ;
			if (hold && !IsFinished()) {
				if (pick_) pickLast() = bFolder_ ; else InstrumentPresets::LastFolder() = bFolder_ ;
				stopPreview() ;
				EndModal(launcher_ ? PD_CLOSED : 0) ;
			}
		}
		return ;   // soltar el botón que abrió la ventana no cuenta
	}
	if (mask & EPBM_B) {
		if (bAt_) return ;   // B sigue apretado (repetición): no sube más
		bAt_ = SDL_GetTicks() ;
		bFolder_ = folder_ ;
		if (!bAt_) bAt_ = 1 ;
#ifdef SDL2
		SDL_AddTimer(B_HOLD + 15, previewWake, 0) ;
#endif
	}
	isDirty_ = true ;
	int n = (int)entries_.size() ;
	if (mask == EPBM_Y && launcher_ && !forSave_) {   // cerrar al cargar: sí / no
		setAutoClose(!autoClose()) ;
		return ;
	}
	if (mask == EPBM_X && !forSave_) {   // pre-escucha sí / no
		setPreviewEnabled(!previewEnabled()) ;
		if (previewEnabled()) schedulePreview() ;
		else stopPreview() ;
		return ;
	}
	if (col_ == 0) {   // las categorías
		if (mask & EPBM_B) {   // ya afuera: se cierra
			stopPreview() ;
			EndModal(launcher_ ? PD_CLOSED : 0) ;
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
		case K_NEWDIR: {
			NewProjectDialog *nd = new NewProjectDialog(*this, Path(std::string("root:presets/") + folder_)) ;
			nd->SetPrefix("") ;
			nd->SetTitle("Nueva subcategoría") ;
			DoModal(nd, NewFolderCallback) ;
			return ;
		}
		case K_EMPTY:
			stopPreview() ;
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(e.zones) ;
			return ;
		case K_PRESET:
			chosen_ = full(e.name) ;
			// la del clip sigue hasta que el sonido entre (se apaga después de lanzar)
			if (!(launcher_ && inClip_)) stopPreview() ;
			if (pick_) {   // trk2als: se importa y listo
				pickLast() = folder_ ;
				std::string msg ;
				if (SampleFileImport(InstrumentPresets::LibPath(chosen_), viewData_, pickInstr_, msg)) {
					if (!msg.empty()) View::SetNotification(msg.c_str()) ;
					EndModal(1) ;
				} else {
					View::SetNotification(msg.c_str()) ;
				}
				return ;
			}
			if (launcher_) {
				// trk2als: como un clip: se lanza y el navegador sigue; A sobre
				// el que ya está (sonando o por entrar) cierra
				int pm ;
				int st = launcher_->LaunchState(chosen_, pm) ;
				if (st == PresetLauncher::LS_NONE) {
					InstrumentPresets::LastFolder() = folder_ ;
					launcher_->Launch(chosen_) ;
					stopPreview() ;
					if (!autoClose()) return ;
				}
				stopPreview() ;
				EndModal(PD_CLOSED) ;
				return ;
			}
			InstrumentPresets::LastFolder() = folder_ ;
			EndModal(1) ;
			return ;
		}
	}
}

std::string PresetDialog::GetSelected() {
	return chosen_ ;
}
