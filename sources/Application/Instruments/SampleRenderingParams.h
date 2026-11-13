
#ifndef _SAMPLE_RENDER_PARAMS_H_
#define _SAMPLE_RENDER_PARAMS_H_

#include "Foundation/Types/Types.h"
#include "SRPUpdaters.h"
#include <vector>

enum FeedbackMode {
	FB_NONE,
	FB_ADD,
	FB_SUB
} ;

struct renderParams {

	void *sampleBuffer_ ; // wavdata
	int channelCount_ ;

	int krateCount_ ;   // K-rate counter
    float position_;    // Position in the sample stream
    int rendFirst_;     // position of the first sample (can be either start or loop depending on the mode)
    int rendLoopStart_ ;// Loop start position
    int rendLoopEnd_ ;  // Loop end position
    
	fixed baseSpeed_ ;  // The base speed with respect to current note
    fixed speed_ ;      // speed at which we currently travel the stream
    // trk2als: últimos valores del instrumento aplicados a esta voz, para que
    // los cambios en la pantalla del instrumento suenen mientras la voz suena
    int liveVol_, livePan_, liveCut_, liveRes_, liveCrush_, liveDrive_ ;
    int liveDs_, liveAtt_, liveFbTun_, liveFbMix_ ;
    bool forceSync_ ;   // trk2als: esta voz toca en looper sync porque es una pista de audio
    float syncTickSamples_ ; // trk2als: duración del tick con la que se calculó el looper sync (0 = no aplica)
    bool complexPrimed_ ;    // trk2als: el time-stretch de esta voz ya arrancó
    int complexPos_ ;        // trk2als: siguiente cuadro del sample que se le da al time-stretch
    double complexAcc_ ;     // trk2als: fracción de cuadro pendiente
    int complexLatency_ ;    // trk2als: cuadros que el time-stretch va adelantado
    int warpMode_ ;          // trk2als: warp con el que suena la voz (el cambio espera al inicio del loop)
    bool justWrapped_ ;      // trk2als: el loop acaba de volver al principio
	fixed baseVolume_ ;  // Base volume the instrument was triggered with
	fixed volume_ ;     // Current volume
	bool reverse_ ;     // true if we we go backwards in stream

	bool retrig_ ;       // true if we're retriggering
	int retrigLoop_ ;   // number of ticks before retrig
	int retrigCount_ ;  // current tick countdown before retrig
	int retrigOffset_ ; // offset in ticks after retrig
    int printFx_;       // Impulse response-based printable reverb

    bool finished_; // the instrument has cut off

    fixed baseFCut_;
    fixed baseFRes_ ;

	fixed cutoff_ ; // filter cutoff
	fixed reso_ ;   // filter reso

	fixed baseFbTun_ ;
	fixed baseFbMix_ ;

	fixed fbTun_ ;
	fixed fbMix_ ;
	
	int feedbackIn_ ;  // Position in ring buffer where start of feedback is
	int feedbackOut_ ; // Position in ring buffer where we 'pick' the samples
	FeedbackMode feedbackMode_ ;
	unsigned char crush_ ; // crush
	unsigned char drive_ ; // crush drive
	fixed attenuate_ ; // filter attenuate

	unsigned char downsample_ ; // downsampling

	fixed basePan_ ; // panning
	fixed pan_ ;

	std::vector<I_SRPUpdater *> updaters_ ;
	std::vector<I_SRPUpdater *> activeUpdaters_ ;

	VolumeRamp volumeRamp_ ;
	Panner panner_ ;
	FCRamp cutRamp_ ;
	FRRamp resRamp_ ;
	FBMixRamp fbMixRamp_ ;
	FBTunRamp fbTunRamp_ ;
	LinSpeedRamp speedRamp_ ;
	LogSpeedRamp legato_ ;
	LogSpeedRamp pfin_ ;
	Arp arp_ ;

	bool couldClick_ ;

	char midiNote_ ;  // Current midi note
} ;
#endif
