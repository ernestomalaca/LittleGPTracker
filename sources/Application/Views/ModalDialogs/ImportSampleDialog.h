#ifndef _IMPORT_SAMPLE_DIALOG_H_
#define _IMPORT_SAMPLE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include "Foundation/T_SimpleList.h"
#include "System/FileSystem/FileSystem.h"
#include <string>

class ImportSampleDialog:public ModalView {
public:
	// audioOnly (trk2als): para un loop de una pista de audio: sin SoundFonts y
	// el preview a tempo. GetReturnCode(): 1 = importó, 0 = salió
	// sf2Only (trk2als): instrumento soundfont: solo .sf2
	ImportSampleDialog(View &view, bool audioOnly = false, bool sf2Only = false) ;
	virtual ~ImportSampleDialog() ;

	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;

protected:
	void setCurrentFolder(Path *path) ;
	void warpToNextSample(int dir) ;
	void import(Path &element) ;
	void preview(Path &element) ;
	void endPreview() ;
private:
	Path *getImportElement();
	bool isSampleLibRoot();
	void setCurrent(Path *element, unsigned short mask);
	T_SimpleList<Path> sampleList_ ;
	int currentSample_ ;
	int topIndex_ ;
	int toInstr_ ;
	int selected_ ;
	bool audioOnly_ ;
	bool sf2Only_ ;
	static bool initStatic_ ;
	static Path sampleLib_ ;
	static Path currentPath_ ;

} ;


#endif

