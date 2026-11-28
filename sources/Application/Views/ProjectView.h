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
  enum { CHIPS = 11 };
  UIField *chipField_[CHIPS];
  // trk2als: la latencia del audio (config.xml; toma efecto al reiniciar)
  Variable *latVar_;
  int latLast_;
  Variable *themeVar_;   // trk2als: el tema (chancho6/temas)
  int themeLast_;
  char *themeNames_[32];
  int themeCount_;
  Variable *fontVar_;   // trk2als: la fuente (chancho6/fuentes)
  int fontLast_;
  char *fontNames_[32];
  int fontCount_;
  void drawBigNumber(int x, int y, const char *txt, int h, GUIColor c);
  // trk2als: el Proyecto como las Settings de Live: secciones a la izquierda,
  // sus ajustes a la derecha, todo con letra grande
  struct MItem { const char *label; Variable *v; int kind, min, max, small, big, action; };
  enum { MK_VAR, MK_ACTION, MK_WARP, MK_WEB, MK_SCENENEXT };
  enum { MSECS = 9, MITEMS = 8 };
  MItem mItem_[MSECS][MITEMS];
  int mCount_[MSECS];
  const char *mSec_[MSECS];
  int mSec_i_, mItem_i_[MSECS], mCol_;
  void buildMenu();
  void drawMenu();
  bool menuInput(unsigned short mask);
  std::string menuValue(const MItem &it);
  void menuAction(int fourcc);
  void menuChange(MItem &it, int delta);
  UIField *btnField_[7];
  std::string msg_;
  unsigned long msgAt_;
  const char *btnName_[7];
} ;
#endif
