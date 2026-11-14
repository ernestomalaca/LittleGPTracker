
#ifndef _APPLICATION_MIXER_H_
#define _APPLICATION_MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Model/Project.h"
#include "Application/Views/ViewData.h"
#include "Application/Utils/fixed.h"
#include "Application/Audio/AudioFileStreamer.h"
#include "PlayerChannel.h"
#include "Foundation/Observable.h"
#include "Services/Audio/AudioOut.h"

#define STREAM_MIX_BUS 8

// trk2als: el click del metrónomo (se dispara al principio de un bloque)
class MetronomeModule : public AudioModule {
public:
	MetronomeModule() : left_(0), pos_(0), accent_(false) {}
	void Click(bool accent) { accent_=accent ; pos_=0 ; left_=1600 ; }
	virtual bool Render(fixed *buffer,int samplecount) ;
private:
	int left_, pos_ ;
	bool accent_ ;
} ;

class PlayerMixer: public T_Singleton<PlayerMixer>,public Observable,public I_Observer {
public:
	PlayerMixer() ;
	virtual ~PlayerMixer() {} ;

	bool Start() ;
	void Stop() ;
	bool Init(Project *project) ;
	void Close() ;

	void OnPlayerStart() ;
	void OnPlayerStop() ;

	void StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument) ;
	void StopInstrument(int channel) ;

	int GetChannelNote(int Channel) ;

	I_Instrument *GetInstrument(int channel) ;

	I_Instrument *GetLastInstrument(int channel) ;
	
	void StartChannel(int channel) ;
	void StopChannel(int channel) ;
	void ChokeChannel(int channel) ; // trk2als

	bool IsChannelPlaying(int channel) ;
	
	void StartStreaming(const Path &) ;
	void StartStreaming(const Path &, double speed, double delaySecs, double startFrac, bool loop) ;
	void StopStreaming()  ;
	int GetStreamPosition() { return fileStreamer_.GetPosition() ; } // trk2als
	void SetStreamEnd(int frame) { fileStreamer_.SetEnd(frame) ; }

	bool Clipped() ;

	void Update(Observable &o,I_ObservableData *d) ;
	int GetPlayedBufferPercentage() ;   

	void SetChannelMute(int channel,bool mute) ;
    bool IsChannelMuted(int channel);

    char *GetPlayedNote(int channel);
    char *GetPlayedOctive(int channel) ;
	
	AudioOut *GetAudioOut() ;

	void Lock() ;
	void Unlock() ;

private:

	Project *project_ ;
	bool clipped_ ;
	
    I_Instrument *lastInstrument_[SONG_CHANNEL_COUNT] ;
	bool isChannelPlaying_[SONG_CHANNEL_COUNT] ;

	AudioFileStreamer fileStreamer_ ;
	MetronomeModule metronome_ ;
public:
	void MetronomeClick(bool accent) { metronome_.Click(accent) ; }
	PlayerChannel *channel_[SONG_CHANNEL_COUNT] ;

	// store trigger notes, 0xFF = none
	
    unsigned char notes_[SONG_CHANNEL_COUNT] ;
} ;

#endif
