#ifndef _IMPORT_SAMPLE_DIALOG_H_
#define _IMPORT_SAMPLE_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include "Foundation/T_SimpleList.h"
#include "System/FileSystem/FileSystem.h"
#include <string>

class ViewData ;
// trk2als: escuchar / importar un archivo de la samplelib (lo usan este
// diálogo y el navegador nuevo)
void SampleFilePreview(Path element, ViewData *vd, int toInstr, bool audioOnly) ;
bool SampleFileImport(Path element, ViewData *vd, int toInstr, std::string &msg) ;

class ImportSampleDialog:public ModalView {
public:
	// audioOnly (trk2als): para un loop de una pista de audio: sin SoundFonts y
	// el preview a tempo. GetReturnCode(): 1 = importó, 0 = salió
	// sf2Only (trk2als): instrumento soundfont: solo .sf2
	// wavOnly (trk2als): un pad de kit: solo .wav
	ImportSampleDialog(View &view, bool audioOnly = false, bool sf2Only = false, bool wavOnly = false) ;
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
	bool wavOnly_ ;
	static bool initStatic_ ;
	static Path sampleLib_ ;
	static Path currentPath_ ;

} ;


#endif

