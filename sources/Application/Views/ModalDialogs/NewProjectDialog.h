#ifndef _NEW_PROJECT_DIALOG_H_
#define _NEW_PROJECT_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>

#define MAX_NAME_LENGTH 16   // trk2als: antes 12 (para "Precordillera...")

// trk2als: Guardar como (y guardar presets). A pantalla completa, en dos
// pasos: el nombre con Azar / Ok / Cancelar, y A sobre el nombre abre el
// teclado. No se guarda sin nombre.
class NewProjectDialog:public ModalView {
public:
  NewProjectDialog(View &view, Path currentPath = "root:");
  virtual ~NewProjectDialog();

  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick);
  virtual void OnFocus();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual bool UsesShapes() { return SupportsShapes(); }

  std::string GetName();   // con el prefijo
  // trk2als: un nombre al azar que no exista en dir (con el prefijo de proyecto)
  static std::string RandomProjectName(const Path &dir);
  // trk2als: también sirve para nombrar presets
  void SetPrefix(const std::string &p) { prefix_ = p; }
  void SetTitle(const std::string &t) { title_ = t; }
  void SetInitialName(const std::string &n) { initial_ = n; }
  void SetAllowExisting(bool b) { allowExisting_ = b; }

private:
  void drawShapes();
  void drawGrid();
  void typeKey();
  void backspace();
  void tryOk();
  void randomName();
  void warn(const char *msg);
  char keyAt(int row, int col);
  int specialAt(int col);   // fila de abajo: 0 mayus, 1 espacio, 2 borrar, 3 listo

  std::string prefix_;
  std::string title_;
  std::string initial_;
  bool allowExisting_;
  Path currentPath_;
  std::string name_;
  int cursor_;        // dónde se escribe (0..largo)
  int selected_;      // 0 el nombre, 1 azar, 2 ok, 3 cancelar
  bool keyboard_;
  bool upper_;
  int krow_, kcol_;
  std::string warn_;
  unsigned long warnTime_;
};
#endif
