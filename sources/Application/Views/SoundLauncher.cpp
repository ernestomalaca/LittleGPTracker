#include "SoundLauncher.h"
#include "Application/Views/ViewData.h"
#include "Application/Player/Player.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Project.h"
#include "System/Console/Trace.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Instruments/InstrumentBank.h"
#include <ctype.h>
#include <string.h>
#ifdef SDL2
#include <SDL2/SDL.h>
#endif

static SoundLauncher *current = 0 ;
static SampleInstrument *spare() ;

#ifdef SDL2
// mientras algo está en camino, la interfaz se despierta seguido para
// avanzar la barrita y entrar a tiempo en el 1
static volatile bool wakeSet = false ;
static Uint32 wake(Uint32, void *) {
	wakeSet = false ;
	SDL_Event e ;
	memset(&e, 0, sizeof(e)) ;
	e.type = SDL_WINDOWEVENT ;
	e.window.event = SDL_WINDOWEVENT_EXPOSED ;
	SDL_PushEvent(&e) ;
	return 0 ;
}
static void wakeSoon() {
	if (wakeSet) return ;
	wakeSet = true ;
	if (!SDL_AddTimer(10, wake, 0)) wakeSet = false ;
}
static unsigned long ticks() { return SDL_GetTicks() ; }
#else
static void wakeSoon() {}
static unsigned long ticks() { return 0 ; }
#endif

static std::string leafOf(const std::string &name) {
	size_t p = name.rfind('/') ;
	return p == std::string::npos ? name : name.substr(p + 1) ;
}

SoundLauncher::SoundLauncher(ViewData *vd, LaunchHost *owner)
	: vd_(vd), owner_(owner), target_(-1), host_(0), visitUndo_(false), job_(0), instr_(-1),
	  jobHost_(0), jobFirst_(false), phase_(0), prevStep_(-1), step15At_(0), lastDraw_(0),
	  pjob_(0), ptrack_(-1), pkeep_(false), prelease_(false), quietAt_(0) {
	current = this ;
}

SoundLauncher::~SoundLauncher() {
	InstrumentPresets::PreloadRelease(job_) ;
	InstrumentPresets::PreloadRelease(pjob_) ;
	if (ptrack_ >= 0) Player::GetInstance()->SetPreviewInstrument(ptrack_, 0) ;
	Player::GetInstance()->KillInstrument(spare()) ;
	if (current == this) current = 0 ;
}

SoundLauncher *SoundLauncher::Current() { return current ; }

std::string SoundLauncher::DisplayName(const std::string &name) {
	std::string leaf = leafOf(name) ;
	if (InstrumentPresets::IsLib(name) && leaf.size() > 4) leaf = leaf.substr(0, leaf.size() - 4) ;
	return leaf ;
}

void SoundLauncher::Open(int instrument, LaunchHost *host) {
	target_ = instrument ;
	host_ = host ;
	visitUndo_ = false ;   // lo que se pruebe en esta visita se deshace de una vez
}

int SoundLauncher::trackOf(int instrument) {
	if (instrument < 0) return -1 ;
	Project *p = vd_->project_ ;
	for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++)
		if (p->TrackExists(ch) && p->GetTrackInstrument(ch) == instrument) return ch ;
	return -1 ;
}

// el sonido que tiene el instrumento: el preset con su carpeta si se cargó en
// esta sesión (y su pista sigue llamándose así); si no, el nombre de la pista
std::string SoundLauncher::currentOf(int instrument) {
	int tr = trackOf(instrument) ;
	std::string shown = tr >= 0 ? vd_->project_->GetTrackName(tr) : "" ;
	std::map<int, std::string>::iterator it = preset_.find(instrument) ;
	if (it == preset_.end() || it->second.empty()) return shown ;
	if (tr >= 0 && shown_[instrument] != shown) return shown ;   // se cambió por otro lado
	return it->second ;
}

bool SoundLauncher::LaunchPlaying() { return Player::GetInstance()->IsRunning() ; }

std::string SoundLauncher::LaunchCurrent() { return currentOf(target_) ; }

void SoundLauncher::Launch(const std::string &name) {
	if (name.empty() || target_ < 0 || target_ >= MAX_SAMPLEINSTRUMENT_COUNT) return ;
	InstrumentPresets::PreloadRelease(job_) ;   // si había otro en camino: gana este
	job_ = InstrumentPresets::StartPreload(name) ;
	if (!job_) {
		phase_ = 0 ;
		notify(host_, "No se pudo cargar el preset") ;
		return ;
	}
	name_ = name ;
	instr_ = target_ ;
	jobHost_ = host_ ;
	phase_ = 1 ;
	prevStep_ = -1 ;
	step15At_ = 0 ;
	wakeSoon() ;
}

int SoundLauncher::LaunchState(const std::string &name, int &permille) {
	permille = 0 ;
	if (phase_ && name == name_ && instr_ == target_) {
		if (phase_ == 1) {
			permille = InstrumentPresets::PreloadPermille(job_) ;
			return LS_LOADING ;
		}
		permille = Player::GetInstance()->BarStep() * 1000 / 16 ;
		return LS_WAITING ;
	}
	std::string cur = currentOf(target_) ;
	if (!cur.empty()) {
		if (cur == name || (cur.find('/') == std::string::npos && cur == leafOf(name))) return LS_SOUNDING ;
	}
	return LS_NONE ;
}

int SoundLauncher::PendingTrack() { return phase_ ? trackOf(instr_) : -1 ; }

int SoundLauncher::Permille() {
	if (phase_ == 1) return InstrumentPresets::PreloadPermille(job_) ;
	if (phase_ == 2) return Player::GetInstance()->BarStep() * 1000 / 16 ;
	return 0 ;
}

void SoundLauncher::notify(LaunchHost *h, const char *msg) {
	if (!h) h = owner_ ;
	if (h) h->LaunchNotify(msg) ;
}

void SoundLauncher::cancel() {
	InstrumentPresets::PreloadRelease(job_) ;
	job_ = 0 ;
	phase_ = 0 ;
}

void SoundLauncher::TrackRemoved(int track) {
	Project *p = vd_->project_ ;
	if (!p->TrackExists(track)) return ;
	int instr = p->GetTrackInstrument(track) ;
	if (phase_ && instr_ == instr) cancel() ;
	if (ptrack_ == track) {
		InstrumentPresets::PreloadRelease(pjob_) ;
		pjob_ = 0 ;
		previewOff() ;
	}
	preset_.erase(instr) ;
	shown_.erase(instr) ;
}

void SoundLauncher::SetPreset(int instrument, const std::string &name) {
	preset_[instrument] = name ;
	int tr = trackOf(instrument) ;
	shown_[instrument] = tr >= 0 ? vd_->project_->GetTrackName(tr) : "" ;
}

bool SoundLauncher::Tick() {
	bool previewDraw = tickPreview() ;
	if (!phase_) return previewDraw ;
	unsigned long now = ticks() ;
	bool redraw = previewDraw ;
	wakeSoon() ;
	if (phase_ == 1 && InstrumentPresets::PreloadDone(job_)) {
		bool ok = InstrumentPresets::PreloadAdopt(job_) ;
		if (!ok) {
			// el pool se llenó con samples de sonidos de antes: se quitan los
			// que nadie usa y otra vez (lo que se podía deshacer se pierde)
			InstrumentPresets::PreloadRelease(pjob_) ;
			pjob_ = 0 ;
			previewOff() ;   // el repuesto no es del banco: que no se quede con índices viejos
			Player::GetInstance()->KillInstrument(spare()) ;
			quietAt_ = 0 ;
			padded_.clear() ;
			prelease_ = false ;
			vd_->project_->PurgeInstruments(true) ;
			if (owner_) owner_->LaunchPurged() ;
			if (jobHost_ && jobHost_ != owner_) jobHost_->LaunchPurged() ;
			ok = InstrumentPresets::PreloadAdopt(job_) ;
		}
		if (!ok) {
			cancel() ;
			notify(jobHost_, "Ya no caben más samples en el proyecto") ;
			return true ;
		}
		phase_ = 2 ;
		prevStep_ = -1 ;
		step15At_ = 0 ;
		redraw = true ;
	}
	if (phase_ == 2) {
		Player *pl = Player::GetInstance() ;
		if (!pl->IsRunning()) {
			apply() ;
			return true ;
		}
		// el 1: se ve empezar el último 1/16 del compás y se aplica justo
		// antes de que termine (lo que suena ahí apenas se recorta)
		int step = pl->BarStep() ;
		if (step == 15 && prevStep_ >= 0 && prevStep_ != 15) step15At_ = now ;
		int tempo = vd_->project_->GetTempo() ;
		unsigned long stepMs = tempo > 0 ? 15000 / tempo : 125 ;
		unsigned long margin = 35 ;
		bool due = step15At_ && now + margin >= step15At_ + stepMs ;
		bool late = step15At_ && step != 15 ;   // ya pasó el 1: cuanto antes
		prevStep_ = step ;
		if (due || late) {
			apply() ;
			return true ;
		}
	}
	if (redraw || now - lastDraw_ >= 60) {
		lastDraw_ = now ;
		return true ;
	}
	return false ;
}

void SoundLauncher::apply() {
	Project *project = vd_->project_ ;
	int instr = instr_ ;
	std::string name = name_ ;
	LaunchHost *host = jobHost_ ;
	cancel() ;
	if (instr < 0 || instr >= MAX_SAMPLEINSTRUMENT_COUNT) return ;
	I_Instrument *ins = project->GetInstrumentBank()->GetInstrument(instr) ;
	if (!ins || ins->GetType() != IT_SAMPLE) return ;
	SampleInstrument *si = (SampleInstrument *)ins ;
	int tr = trackOf(instr) ;
	bool first = !visitUndo_ ;
	visitUndo_ = true ;
	unsigned long t0 = ticks() ;
	if (host) host->LaunchBefore(instr, tr, first) ;
	else if (tr >= 0) Player::GetInstance()->HoldTrack(tr, true) ;
	bool ok = InstrumentPresets::Load(si, name) ;
	if (ok) {
		if (tr >= 0) project->SetTrackName(tr, DisplayName(name).c_str()) ;
		SetPreset(instr, name) ;
	}
	// lo que se oía en el clip ya es el sonido de la pista
	if (ptrack_ >= 0 && (pkeep_ || (tr == ptrack_ && name == pname_))) {
		InstrumentPresets::PreloadRelease(pjob_) ;
		pjob_ = 0 ;
		previewOff() ;
	}
	if (ok) previewRelease(false) ;   // los suyos ya son de la pista: no se quitan
	if (host) host->LaunchAfter(instr, tr, name, ok) ;
	else if (tr >= 0) Player::GetInstance()->HoldTrack(tr, false) ;
	Trace::Log("PRESET", "%s: entró en %s %d (paso %d del compás), callado %lu ms", name.c_str(),
	           tr >= 0 ? "la pista" : "el instrumento", tr >= 0 ? tr + 1 : instr,
	           Player::GetInstance()->BarStep(), ticks() - t0) ;
	if (!ok) notify(host, "No se pudo cargar el preset") ;
}

// --- la pre-escucha en el clip ---------------------------------------------

// el instrumento de repuesto: uno solo, que no se borra nunca (el reproductor
// pudo quedarse con su puntero en algún canal)
static SampleInstrument *spare() {
	static SampleInstrument *s = 0 ;
	if (!s) s = new SampleInstrument() ;
	return s ;
}

static bool libNotWav(const std::string &name) {
	if (!InstrumentPresets::IsLib(name)) return false ;
	std::string low(name) ;
	for (size_t i = 0; i < low.size(); i++) low[i] = tolower(low[i]) ;
	return low.size() < 4 || low.compare(low.size() - 4, 4, ".wav") != 0 ;
}

bool SoundLauncher::LaunchPreview(const std::string &name) {
	if (name.empty() || target_ < 0 || target_ >= MAX_SAMPLEINSTRUMENT_COUNT || libNotWav(name)) return false ;
	int tr = trackOf(target_) ;
	if (tr < 0 || !Player::GetInstance()->ClipPlaying(tr)) return false ;
	I_Instrument *ins = vd_->project_->GetInstrumentBank()->GetInstrument(target_) ;
	if (!ins || ins->GetType() != IT_SAMPLE) return false ;
	InstrumentPresets::PreloadRelease(pjob_) ;
	pjob_ = InstrumentPresets::StartPreload(name) ;
	if (!pjob_) return false ;
	if (ptrack_ >= 0 && ptrack_ != tr) spareOff(ptrack_) ;
	pname_ = name ;
	ptrack_ = tr ;
	pkeep_ = false ;
	wakeSoon() ;
	return true ;
}

void SoundLauncher::LaunchPreviewStop() {
	InstrumentPresets::PreloadRelease(pjob_) ;
	pjob_ = 0 ;
	if (ptrack_ < 0) return ;
	// se lanzó lo mismo que se oye: sigue sonando así hasta que entre en el 1
	if (phase_ && name_ == pname_ && trackOf(instr_) == ptrack_) {
		pkeep_ = true ;
		return ;
	}
	previewOff() ;
}

// la pista vuelve a su instrumento; el repuesto se apaga con su cola corta
// y hasta que calle no se le cambia ni se le quita nada
void SoundLauncher::spareOff(int track) {
	Player *pl = Player::GetInstance() ;
	if (track < 0 || pl->GetPreviewInstrument(track) != spare()) return ;
	pl->SetPreviewInstrument(track, 0) ;
	quietAt_ = ticks() + 80 ;
	if (!quietAt_) quietAt_ = 1 ;
}

// true: ya nadie mezcla el repuesto (si seguía con cola, se corta aquí)
bool SoundLauncher::spareQuiet() {
	if (!quietAt_) return true ;
	if (ticks() < quietAt_) { wakeSoon() ; return false ; }
	quietAt_ = 0 ;
	Player::GetInstance()->KillInstrument(spare()) ;
	return true ;
}

void SoundLauncher::previewOff() {
	spareOff(ptrack_) ;
	ptrack_ = -1 ;
	pname_.clear() ;
	pkeep_ = false ;
	if (!padded_.empty()) prelease_ = true ;
}

static void markUsed(SampleInstrument *si, std::vector<bool> &used) {
	SamplePool *pool = SamplePool::GetInstance() ;
	int index = si->GetSampleIndex() ;
	if (index >= 0 && index < (int)used.size()) used[index] = true ;
	if (si->IsZoned()) {
		const std::vector<ZoneSource::Zone> &zs = si->Zones() ;
		for (size_t z = 0; z < zs.size(); z++) {
			int k = pool->IndexOf(zs[z].name.c_str()) ;
			if (k >= 0 && k < (int)used.size()) used[k] = true ;
		}
	}
}

// purge: quita del proyecto los samples que se trajeron para la pre-escucha y
// ya no usa nadie (ni el banco ni lo que se está oyendo). Sin purge: solo se
// olvidan los que ya usa el banco (entraron con el sonido: son de la pista)
void SoundLauncher::previewRelease(bool purge) {
	if (padded_.empty()) { prelease_ = false ; return ; }
	if (purge && !spareQuiet()) { prelease_ = true ; return ; }
	SamplePool *pool = SamplePool::GetInstance() ;
	std::vector<bool> used(MAX_PIG_SAMPLES, false), bank(MAX_PIG_SAMPLES, false) ;
	InstrumentBank *b = vd_->project_->GetInstrumentBank() ;
	for (int i = 0; i < MAX_SAMPLEINSTRUMENT_COUNT; i++) {
		I_Instrument *in = b->GetInstrument(i) ;
		if (in && in->GetType() == IT_SAMPLE) markUsed((SampleInstrument *)in, bank) ;
	}
	used = bank ;
	if (ptrack_ >= 0) markUsed(spare(), used) ;
	std::vector<std::string> keep ;
	int purged = 0 ;
	for (size_t k = 0; k < padded_.size(); k++) {
		int idx = pool->IndexOf(padded_[k].c_str()) ;
		if (idx < 0) continue ;                 // ya no está
		if (bank[idx]) continue ;               // es de alguna pista: se olvida
		if (used[idx] || !purge) { keep.push_back(padded_[k]) ; continue ; }
		if (!pool->IsReference(idx)) continue ; // del proyecto: nunca se borra
		MixerService::GetInstance()->Lock() ;
		pool->PurgeSample(idx) ;
		MixerService::GetInstance()->Unlock() ;
		// los índices de después se recorren: se vuelve a marcar
		std::vector<bool> nb(MAX_PIG_SAMPLES, false) ;
		for (int i = 0; i < MAX_PIG_SAMPLES - 1; i++) {
			int from = i < idx ? i : i + 1 ;
			nb[i] = bank[from] ;
		}
		bank = nb ;
		std::vector<bool> nu(MAX_PIG_SAMPLES, false) ;
		for (int i = 0; i < MAX_PIG_SAMPLES - 1; i++) nu[i] = used[i < idx ? i : i + 1] ;
		used = nu ;
		purged++ ;
	}
	padded_ = keep ;
	if (purge) prelease_ = false ;
	if (purged) Trace::Log("PRESET", "pre-escucha: se quitaron %d samples que solo eran para oír", purged) ;
}

// true = hay que redibujar
bool SoundLauncher::tickPreview() {
	if (pjob_) {
		wakeSoon() ;
		if (!InstrumentPresets::PreloadDone(pjob_)) return false ;
		// el que se oía se apaga y, ya callado, se le cambia el sonido
		spareOff(ptrack_) ;
		if (!spareQuiet()) return false ;
		SamplePool *pool = SamplePool::GetInstance() ;
		int before = pool->GetNameListSize() ;
		bool ok = InstrumentPresets::PreloadAdopt(pjob_) ;
		InstrumentPresets::PreloadRelease(pjob_) ;
		pjob_ = 0 ;
		Player *pl = Player::GetInstance() ;
		I_Instrument *own = (target_ >= 0 && target_ < MAX_SAMPLEINSTRUMENT_COUNT)
		                    ? vd_->project_->GetInstrumentBank()->GetInstrument(target_) : 0 ;
		if (ok && ptrack_ >= 0 && own && own->GetType() == IT_SAMPLE) {
			SampleInstrument *sp = spare() ;
			// como quedaría la pista: lo suyo y encima el sonido
			sp->Purge() ;
			IteratorPtr<Variable> it(own->GetIterator()) ;
			for (it->Begin(); !it->IsDone(); it->Next()) {
				Variable &v = it->CurrentItem() ;
				Variable *d = sp->FindVariable(v.GetID()) ;
				if (d) d->CopyFrom(v) ;
			}
			ok = InstrumentPresets::Load(sp, pname_) ;
			int after = pool->GetNameListSize() ;
			char **names = pool->GetNameList() ;
			for (int i = before; i < after; i++)
				if (names[i]) padded_.push_back(names[i]) ;
			if (ok && pl->ClipPlaying(ptrack_)) {
				pl->SetPreviewInstrument(ptrack_, sp) ;
				Trace::Log("PRESET", "pre-escucha en el clip de la pista %d: %s", ptrack_ + 1, pname_.c_str()) ;
			}
		}
		if (!ok) {
			Trace::Log("PRESET", "pre-escucha en el clip: no se pudo leer %s", pname_.c_str()) ;
			previewOff() ;
		}
		// lo de la pre-escucha anterior que ya no se oye
		if (!phase_) previewRelease(true) ;
		else if (!padded_.empty()) prelease_ = true ;
		return true ;
	}
	if (prelease_ && !phase_) { wakeSoon() ; previewRelease(true) ; }
	else if (quietAt_ && spareQuiet()) {}
	return false ;
}
