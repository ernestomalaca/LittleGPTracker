
#ifndef _PLAYER_CHANNEL_H_
#define _PLAYER_CHANNEL_H_

#include "Services/Audio/AudioModule.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Mixer/MixBus.h"

class PlayerChannel: public AudioModule {
public:
	PlayerChannel(int index) ;
	virtual ~PlayerChannel() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	void StartInstrument(I_Instrument *instr,unsigned char note,bool cleanStart) ;
	void StopInstrument() ;
	void ChokeInstrument() ;
	void KillInstrument() { instr_=0 ; }   // trk2als: deja de mezclarlo ya (sin cola)
	void StartVoice(I_Instrument *instr,int voice,unsigned char note,int velocity) ;   // trk2als
	void StopVoice(int voice) ;
	I_Instrument *GetInstrument() ;
	void SetMute(bool muted) ;
	bool IsMuted() ;
	void SetMixBus(int i) ;
    void SetVolume(fixed volume);
    void SetHPFMode(unsigned char mode);
    void ApplyHPF(fixed *buffer, int samplecount);
    void SetLPFFreq(unsigned short freq);
    void Reset();

  private:
	int index_ ;
	I_Instrument *instr_ ;
	bool muted_ ;
	float muteGain_ ;   // trk2als: mute / solo con fundido (sin click): 1 suena, 0 callado
    fixed volume_;
    int busIndex_ ;
	MixBus *mixBus_ ;
    fixed hpfPrevInput_[2];
    fixed hpfPrevOutput_[2];
	fixed hpfAlpha_;
	unsigned char hpfMode_;
    fixed lpfPrevOutput_[2];
    fixed lpfAlpha_;
    unsigned short lpfFreq_;
};

#endif
