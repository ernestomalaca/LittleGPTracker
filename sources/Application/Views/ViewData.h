
#ifndef _VIEW_DATA_
#define _VIEW_DATA_

#include "Application/Model/Project.h"
#include "System/Console/Trace.h"

enum PlayMode {
	PM_SONG,
	PM_CHAIN,
	PM_PHRASE,   // trk2als: repite el compás actual del clip
	PM_LIVE,
	PM_AUDITION
} ;


class ViewData {

public:
	ViewData(Project *project) ;
	~ViewData() ;

	unsigned char UpdateSongChain(int offset) ;
	void UpdateSongOffset(int offset) ;
	void UpdateSongCursor(int dx,int dy) ;
	void SetSongChain(unsigned char) ;
	unsigned char *GetCurrentSongPointer() ;


protected:

	void checkSongBoundaries() ;

	inline void updateData(unsigned char *c,int offset,unsigned char limit,bool wrap) {
		int v=*c ;
		if ((v==0xFF)&&(limit!=0xFF))
    { 
			v=0 ;
		}
		v+=offset ;
		if (v<0) v=(wrap?(limit+1+v):0) ;
		if (v>limit) v=(wrap?v-(limit+1):limit) ;
		*c=v ;
	}

public:

	// Data

	Project *project_ ;
	Song *song_;

	// Editor settings

	int songX_ ;				// .Current song screen position in the editor
	int songY_ ;				//
	int songOffset_ ;			// .Current song offset (top screen row) in editor

	int chainRow_ ;  			// .Current row in chain editor 
	int chainCol_ ;			    // .Current column in chain editor

	int currentChain_ ;         // .trk2als: el clip que se edita

	int currentInstrument_ ;    // .Current edited instrument
	bool instrumentFromSong_ ;  // .trk2als: se abrió desde un loop de la song
	// trk2als: la curva de un parámetro en el clip (A + L en el instrumento).
	// curveID_ 0 = el clip muestra sus notas
	unsigned int curveID_ ;
	int curveInstr_ ;           // de qué instrumento es
	int curveMin_, curveMax_, curveStep_, curveBig_, curveDisp_ ;
	bool curveLog_ ;
	char curveName_[24] ;       // "freq"
	char curveFmt_[24] ;        // "%+ddB" (lo de después de "nombre: ")
	unsigned int curveFocus_ ;  // al volver al instrumento: el foco ahí
	unsigned int curveLastID_ ; // la última curva (Select tocado en el clip vuelve a ella)

	int currentTable_ ;			// .Current edited table

	int currentGroove_ ;			// .Current edited groove

	int mixerCol_ ;				//
	int mixerRow_ ;

    // Render Settings
	
	int renderMode_ ;           // Current render mode (0=audio, 1=mixdown, 2=split)
	bool isRendering_ ;         // True when actively rendering (playing with mode > 0)
	
	// Player Settings

	PlayMode playMode_ ;
	int songPlayPos_[SONG_CHANNEL_COUNT] ;                    // .Play position of each channel
	unsigned char currentPlayChain_[SONG_CHANNEL_COUNT] ;    // .Current played chain for each channel
	int chainPlayPos_[SONG_CHANNEL_COUNT] ;                   // .Play position in chain for each channel
	unsigned char currentPlayPhrase_[SONG_CHANNEL_COUNT] ;   // .Current played phrase for each channel
	int phrasePlayPos_[SONG_CHANNEL_COUNT]    ;               // .Play position in phrase for each channel
	int phraseCurPos_;                      				// current UI cursor row position
} ; 
#endif
