#ifndef _NEW_PROJECT_DIALOG_H_
#define _NEW_PROJECT_DIALOG_H_

#include "Application/Utils/KeyboardLayout.h"
#include "Application/Views/BaseClasses/ModalView.h"
#include <string>

#define MAX_NAME_LENGTH 12
#define BUTTONS_LENGTH 3

class NewProjectDialog:public ModalView {
public:
  NewProjectDialog(View &view, Path currentPath = "root:");
  virtual ~NewProjectDialog();

  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick);
  virtual void OnFocus();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);

  std::string GetName();
  // trk2als: también sirve para nombrar presets
  void SetPrefix(const std::string &p) { prefix_ = p; }
  void SetTitle(const std::string &t) { title_ = t; }
  void SetInitialName(const std::string &n) { initial_ = n; }
  void SetAllowExisting(bool b) { allowExisting_ = b; }

private:
  std::string prefix_;
  std::string title_;
  std::string initial_;
  bool allowExisting_;
  Path currentPath_;
  int selected_;
  int lastChar_;
  char name_[MAX_NAME_LENGTH + 1];
  int currentChar_;
  bool keyboardMode_;
  int keyboardRow_;
  int keyboardCol_ ;
  void moveCursor(int direction);
};
#endif
