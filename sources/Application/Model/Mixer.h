
#ifndef _MIXER_H_
#define _MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Persistency/Persistent.h"

#include "Song.h"
#include "Application/Utils/fixed.h"

class Mixer:public T_Singleton<Mixer>,Persistent {
public:
	Mixer() ;
	~Mixer() ;
	void Clear() ;

	inline int GetBus(int i) { return channelBus_[i]  ; } ;
    inline void SetBus(int i, int value) { channelBus_[i] = value; };

    inline int GetChannelVolume(int i) { return channelVolume_[i]; };
    inline void SetChannelVolume(int i, int value) { channelVolume_[i] = (unsigned char)value; };
    inline int GetChannelHPF(int i) { return channelHPF_[i]; };
    inline void SetChannelHPF(int i, int value) { channelHPF_[i] = (unsigned char)value; };
    inline unsigned short GetChannelLPF(int i) { return channelLPF_[i]; };
    inline void SetChannelLPF(int i, unsigned short value) { channelLPF_[i] = value; };

    // trk2als: los envíos de cada pista (0..100) y cómo están los dos efectos
    enum { SEND_ECO = 0, SEND_REV = 1, SEND_COUNT = 2 };
    enum { FX_ECO_TIME = 0, FX_ECO_BACK, FX_ECO_TONE, FX_REV_SIZE, FX_REV_TONE, FX_COUNT };
    inline int GetSend(int which, int i) { return channelSend_[which][i]; }
    inline void SetSend(int which, int i, int value) { channelSend_[which][i] = (unsigned char)(value < 0 ? 0 : value > 100 ? 100 : value); }
    inline int GetFx(int p) { return fx_[p]; }
    inline void SetFx(int p, int value) { fx_[p] = (unsigned char)value; }

	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);
private:
	char channelBus_[SONG_CHANNEL_COUNT] ;
    unsigned char channelVolume_[SONG_CHANNEL_COUNT];
    unsigned char channelHPF_[SONG_CHANNEL_COUNT];
    unsigned short channelLPF_[SONG_CHANNEL_COUNT];
    unsigned char channelSend_[SEND_COUNT][SONG_CHANNEL_COUNT];
    unsigned char fx_[FX_COUNT];
} ;	

#endif
