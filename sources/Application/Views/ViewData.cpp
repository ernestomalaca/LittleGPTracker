#include "ViewData.h"
#include "BaseClasses/View.h"

ViewData::ViewData(Project *project) {

	project_=project ;
	song_=project->song_ ;
	currentChain_=0 ;
	virtualCell_=-1 ;
	songX_=0 ;
	songY_=0 ;
	songOffset_=0 ;
	chainCol_=0 ;
	chainRow_=0 ;
	currentTable_=0 ;
	currentInstrument_=0 ;
	instrumentFromSong_=false ;
	curveID_=0 ;
	curveInstr_=-1 ;
	curveMin_=0 ;
	curveMax_=1 ;
	curveStep_=1 ;
	curveBig_=1 ;
	curveDisp_=0 ;
	curveLog_=false ;
	curveTime_=false ;
	curveList_=0 ;
	curveListN_=0 ;
	curveName_[0]=0 ;
	curveFmt_[0]=0 ;
	curveFocus_=0 ;
	curveLastID_=0 ;
	kitPitch_=-1 ; kitPitchCh_=-1 ;
	currentGroove_=0 ;
	mixerCol_=0 ;
	mixerRow_=0 ;
    renderMode_ = 0;
    isRendering_ = false;
} ;

ViewData::~ViewData() {
	delete project_ ;
} ;

unsigned char ViewData::UpdateSongChain(int offset) {
	unsigned char *c=song_->data_+songX_+8*(songOffset_+songY_) ;
	updateData(c,offset,CLIP_COUNT-1,false) ;
	return *c ;

}

void ViewData::SetSongChain(unsigned char value) {
	unsigned char *c=song_->data_+songX_+8*(songOffset_+songY_) ;
	*c=value ;
}

void ViewData::UpdateSongOffset(int offset) {
	songOffset_+=offset ;
	checkSongBoundaries() ;
}

void ViewData::UpdateSongCursor(int dx,int dy) {
	songX_+=dx;
	songY_+=dy ;
	checkSongBoundaries() ;
}

void ViewData::checkSongBoundaries() {
	if (songX_>7) songX_=7 ;
	if (songX_<0) songX_=0 ;
	if (songY_<0) {
		songOffset_+=songY_ ;
		songY_=0 ;
	} ;
	if (songY_>View::songRowCount_-1) {
		songOffset_+=songY_-View::songRowCount_+1 ;
		songY_=View::songRowCount_-1 ;
	} ;
	if (songOffset_>232) {
		songOffset_=232 ;
	}
	if (songOffset_<0) {
		songOffset_=0 ;
	}
}

unsigned char *ViewData::GetCurrentSongPointer() {
	return song_->data_+songX_+8*(songOffset_+songY_) ;
} ;




