#include "SoundLauncher.h"
#include "Application/Views/ViewData.h"
#include "Application/Player/Player.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Project.h"
#include "System/Console/Trace.h"
#include <string.h>
#ifdef SDL2
#include <SDL2/SDL.h>
#endif

static SoundLauncher *current = 0 ;

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
	  jobHost_(0), jobFirst_(false), phase_(0), prevStep_(-1), step15At_(0), lastDraw_(0) {
	current = this ;
}

SoundLauncher::~SoundLauncher() {
	InstrumentPresets::PreloadRelease(job_) ;
	if (current == this) current = 0 ;
}

SoundLauncher *SoundLauncher::Current() { return current ; }

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
	if (tr >= 0 && leafOf(it->second) != shown) return shown ;   // se cambió por otro lado
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
	preset_.erase(instr) ;
}

void SoundLauncher::SetPreset(int instrument, const std::string &name) {
	preset_[instrument] = name ;
}

bool SoundLauncher::Tick() {
	if (!phase_) return false ;
	unsigned long now = ticks() ;
	bool redraw = false ;
	wakeSoon() ;
	if (phase_ == 1 && InstrumentPresets::PreloadDone(job_)) {
		bool ok = InstrumentPresets::PreloadAdopt(job_) ;
		if (!ok) {
			// el pool se llenó con samples de sonidos de antes: se quitan los
			// que nadie usa y otra vez (lo que se podía deshacer se pierde)
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
		preset_[instr] = name ;
		if (tr >= 0) project->SetTrackName(tr, leafOf(name).c_str()) ;
	}
	if (host) host->LaunchAfter(instr, tr, name, ok) ;
	else if (tr >= 0) Player::GetInstance()->HoldTrack(tr, false) ;
	Trace::Log("PRESET", "%s: entró en %s %d (paso %d del compás), callado %lu ms", name.c_str(),
	           tr >= 0 ? "la pista" : "el instrumento", tr >= 0 ? tr + 1 : instr,
	           Player::GetInstance()->BarStep(), ticks() - t0) ;
	if (!ok) notify(host, "No se pudo cargar el preset") ;
}
