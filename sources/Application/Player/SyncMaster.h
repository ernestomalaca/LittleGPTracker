#ifndef _SYNC_MASTER_H_
#define _SYNC_MASTER_H_

#include "Foundation/T_Singleton.h"

// Provide basic functionalities to compute various
// setting regarding tempo, buffer sizes, ticks

class SyncMaster: public T_Singleton<SyncMaster> {
public:
	SyncMaster() ;
	void Start() ;
	void Stop() ;
	void SetTempo(int tempo) ;
	// trk2als: Link. El tempo exacto de la sesión (lo que usan los loops y el
	// warp) y, aparte, el largo de la rebanada que corrige la fase (solo
	// mueve cuándo suenan las notas, no la velocidad del audio)
	void SetTempoExact(float bpm) ;
	void SetPlaySampleCount(float samples) ;
	int GetTempo() ;
	void NextSlice() ;
	bool MajorSlice() ;
	bool TableSlice() ;
	bool MidiSlice() ;
	float GetPlaySampleCount() ;
	float GetTickSampleCount() ;
	int GetTableRatio() ;
	void SetTableRatio(int ratio) ;
	unsigned int GetBeatCount() ;
	float GetTickTime() ;
private:
	int tempo_ ;
	int currentSlice_ ;
	int tableRatio_ ;
	unsigned int beatCount_ ;
	float playSampleCount_ ;
	float tickSampleCount_ ;

} ;
#endif
