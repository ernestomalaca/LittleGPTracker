#include "PlayerMixer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/char.h"
#include "Application/Utils/fixed.h"
#include "Services/Midi/MidiService.h"
#include "SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>
#include <stdlib.h>
#include "Application/Model/Config.h"
#include <stdlib.h>

PlayerMixer::PlayerMixer() {

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        lastInstrument_[i] = 0;
		channel_[i] = new PlayerChannel(i);
		isChannelPlaying_[i] = false;
		map_[i] = i;
		hold_[i] = false;
    }
}

bool PlayerMixer::Init(Project *project) {

	MixerService *ms=MixerService::GetInstance() ;
	if (!ms->Init()) {
			return false ;
	}

	AudioMixer *mixer=ms->GetMixBus(STREAM_MIX_BUS) ;
	mixer->Insert(fileStreamer_) ;
	mixer->Insert(metronome_) ;   // trk2als

	project_=project ;

	// Init states

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        lastInstrument_[i]=0 ;
	} ;

	clipped_=false ;
	return true ;
} ;

void PlayerMixer::Close()  {

	MixerService *lock=MixerService::GetInstance() ;
	lock->Lock() ;   // trk2als: que el audio no esté a media mezcla
	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channel_[i]->Reset() ;
		lastInstrument_[i]=0 ;   // eran del proyecto que se cierra
	}
	lock->Unlock() ;


	MixerService *ms=MixerService::GetInstance() ;
	ms->Close() ;

}

bool PlayerMixer::Start() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->AddObserver(*this) ;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        notes_[i]=0xFF ;
    } ;

	return ms->Start() ;
} ;

void PlayerMixer::Stop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Stop() ;
	ms->RemoveObserver(*this) ;
} ;

void PlayerMixer::StartChannel(int channel) {
	isChannelPlaying_[Phys(channel)]=true ;
} ;

void PlayerMixer::PreFade(int channel,int delay,int len) {   // trk2als
	channel_[Phys(channel)]->PreFade(delay,len) ;
}

void PlayerMixer::ChokeChannel(int channel) {   // trk2als
	channel_[Phys(channel)]->ChokeInstrument() ;
} ;

void PlayerMixer::KillInstrument(I_Instrument *in) {   // trk2als
	for (int p=0;p<SONG_CHANNEL_COUNT;p++) {
		if (channel_[p]->GetInstrument()==in) channel_[p]->KillInstrument() ;
		if (lastInstrument_[p]==in) lastInstrument_[p]=0 ;
	}
}

void PlayerMixer::StopChannel(int channel) {
	int p=Phys(channel) ;
    channel_[p]->StopInstrument() ;
    notes_[p]=0xFF ;
	isChannelPlaying_[p]=false ;
} ;


bool PlayerMixer::IsChannelPlaying(int channel) {
	return isChannelPlaying_[Phys(channel)] ;
} ;

I_Instrument *PlayerMixer::GetLastInstrument(int channel) {
	return lastInstrument_[Phys(channel)] ;
} ;

// trk2als: la pista 'track' se va; las de la derecha toman su lugar en el
// mapa y su canal (ya callado) queda al final, libre
void PlayerMixer::RemoveTrack(int track) {
	if (track<0 || track>=SONG_CHANNEL_COUNT) return ;
	int p=map_[track] ;
	channel_[p]->ChokeInstrument() ;
	channel_[p]->StopInstrument() ;
	channel_[p]->SetMute(false) ;
	notes_[p]=0xFF ;
	isChannelPlaying_[p]=false ;
	lastInstrument_[p]=0 ;
	for (int c=track;c<SONG_CHANNEL_COUNT-1;c++) map_[c]=map_[c+1] ;
	map_[SONG_CHANNEL_COUNT-1]=p ;
}

void PlayerMixer::ResetMap() {
	bool identity=true ;
	for (int k=0;k<SONG_CHANNEL_COUNT;k++) if (map_[k]!=k) identity=false ;
	if (identity) return ;
	bool mute[SONG_CHANNEL_COUNT] ;   // el mute es de la pista: que siga con ella
	for (int k=0;k<SONG_CHANNEL_COUNT;k++) mute[k]=channel_[map_[k]]->IsMuted() ;
	for (int k=0;k<SONG_CHANNEL_COUNT;k++) map_[k]=k ;
	for (int k=0;k<SONG_CHANNEL_COUNT;k++) channel_[k]->SetMute(mute[k]) ;
}


bool PlayerMixer::Clipped() {
     return clipped_ ;
}

void PlayerMixer::Update(Observable &o,I_ObservableData *d) {

  // Notifies the player so that pattern data is processed
  SetChanged();
  NotifyObservers();

  // Transfer the mixer data
  Mixer *mixer = Mixer::GetInstance();

  for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
      PlayerChannel *pc=channel_[map_[i]] ;   // la pista i suena en su canal
      pc->SetMixBus(mixer->GetBus(i));
      pc->SetVolume(fl2fp(mixer->GetChannelVolume(i) / 255.0f));
      pc->SetHPFMode((unsigned char)mixer->GetChannelHPF(i));
      pc->SetLPFFreq(mixer->GetChannelLPF(i));
  }
  MixerService *ms=MixerService::GetInstance();
  ms->SetPregain(project_->GetPregain());
  ms->SetSoftclip(project_->GetSoftclip(), project_->GetSoftclipGain());
  ms->SetMasterVolume(project_->GetMasterVolume());
  clipped_=ms->Clipped();
} ;


void PlayerMixer::StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument)  {
	int p=Phys(channel) ;
	if (hold_[p]) return ;
	channel_[p]->StartInstrument(instrument,note,newInstrument) ;
	lastInstrument_[p]=instrument ;
	notes_[p]=note ;

} ;

void PlayerMixer::StartVoice(int channel,I_Instrument *instrument,int voice,unsigned char note,int velocity) {
	int p=Phys(channel) ;
	if (hold_[p]) return ;
	channel_[p]->StartVoice(instrument,voice,note,velocity) ;
	lastInstrument_[p]=instrument ;
}

void PlayerMixer::StopVoice(int channel,int voice) {
	channel_[Phys(channel)]->StopVoice(voice) ;
}

void PlayerMixer::StopInstrument(int channel) {
    int p=Phys(channel) ;
    channel_[p]->StopInstrument() ;
    notes_[p]=0xFF ;
}

I_Instrument *PlayerMixer::GetInstrument(int channel) {
    return channel_[Phys(channel)]->GetInstrument();
}

int PlayerMixer::GetPlayedBufferPercentage() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetPlayedBufferPercentage() ;
};

void PlayerMixer::SetChannelMute(int channel,bool mode) {
     channel_[Phys(channel)]->SetMute(mode) ;
}

bool PlayerMixer::IsChannelMuted(int channel) {
     return channel_[Phys(channel)]->IsMuted() ;
}

// trk2als: con el candado: el audio lee la ruta mientras mezcla, y la
// pre-escucha arranca desde el Tick (sin botón, sin candado): cambiarla a
// la vez rompía la memoria (double free)
void PlayerMixer::StartStreaming(const Path &path) {
	Lock() ;
	fileStreamer_.Start(path) ;
	Unlock() ;
} ;

void PlayerMixer::StartStreaming(const Path &path, double speed, double delaySecs, double startFrac, bool loop) {
	Lock() ;
	fileStreamer_.Start(path,speed,delaySecs,startFrac,loop) ;
	Unlock() ;
} ;

void PlayerMixer::StopStreaming() {
	Lock() ;
	fileStreamer_.Stop() ;
	Unlock() ;
} ;

void PlayerMixer::OnPlayerStart() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStart();
}

void PlayerMixer::OnPlayerStop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStop();
}

static char noteBuffer[5] ;

int PlayerMixer::GetChannelNote(int channel) {
	return notes_[Phys(channel)] ;
}

char *PlayerMixer::GetPlayedNote(int channel) {

    int p=Phys(channel) ;
    if (notes_[p]!=0xFF) {
		note2visualizer(notes_[p],noteBuffer) ; 
		return noteBuffer ;
    }
    return "  " ;
} ;

char *PlayerMixer::GetPlayedOctive(int channel) {
    int p=Phys(channel) ;
    if (notes_[p]!=0xFF) {
		if (!IsChannelMuted(channel)) {
	        oct2visualizer(notes_[p],noteBuffer) ; 
	        return noteBuffer ;
		} else {
			return "--" ;
		}
    }
    return "  " ;
} ;

AudioOut *PlayerMixer::GetAudioOut() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetAudioOut();
} ;

void PlayerMixer::Lock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
} ;

void PlayerMixer::Unlock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Unlock() ;
};

// trk2als: metrónomo: un tono corto que se apaga en ~35 ms (1 kHz, y
// 1.5 kHz en el 1 del compás), a -12 dB
bool MetronomeModule::Render(fixed *buffer,int samplecount) {
	if (left_<=0) return false ;
	// config.xml: <METRONOMEVOLUME value="50"/>, 0..100 (50 = normal)
	static float vol=-1 ;
	if (vol<0) {
		const char *cv=Config::GetInstance()->GetValue("METRONOMEVOLUME") ;
		int v=cv?atoi(cv):50 ;
		if (v<0) v=0 ;
		if (v>100) v=100 ;
		vol=v/50.0f ;
	}
	float freq=accent_?1500.0f:1000.0f ;
	float w=2.0f*3.14159265f*freq/44100.0f ;
	for (int i=0;i<samplecount;i++) {
		float v=0 ;
		if (left_>0) {
			float env=float(left_)/1600.0f ;
			v=0.25f*vol*env*env*sinf(w*pos_) ;
			pos_++ ;
			left_-- ;
		}
		fixed f=fl2fp(v*32767.0f*0.5f) ;
		buffer[2*i]=f ;
		buffer[2*i+1]=f ;
	}
	return true ;
}
