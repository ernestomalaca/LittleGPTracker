#ifndef _PROJECT_VIEW_H_
#define _PROJECT_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"
#include <string>


class ProjectView: public FieldView,public I_Observer {
public:
	ProjectView(GUIWindow &w,ViewData *data) ;
	virtual ~ProjectView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() {} ;
	virtual bool Tick() ;
	// trk2als: los avisos de esta pantalla van abajo, en el espacio libre
	void notify(const char *msg) ;

	// Observer for action callback

	void Update(Observable &,I_ObservableData *) ;

	void OnLoadProject() ;
	void OnSaveAsProject(char * data) ;
	void OnPurgeInstruments(bool removeFromDisk) ;
	void OnQuit() ;

protected:
private:
  Project *project_;
  // Debug
  unsigned long lastTick_;
  unsigned long lastClock_;
  UIField *tempoField_;
  // trk2als: el tablero
  void drawBoard();
  UIField *rootField_, *scaleField_, *linkField_, *ofsField_;
  UIField *chipField_[8];
  // trk2als: la latencia del audio (config.xml; toma efecto al reiniciar)
  Variable *latVar_;
  int latLast_;
  void drawBigNumber(int x, int y, const char *txt, int h, GUIColor c);
  UIField *btnField_[7];
  std::string msg_;
  unsigned long msgAt_;
  const char *btnName_[7];
} ;
#endif
