#ifndef _CLIP_VIEW_H_
#define _CLIP_VIEW_H_

// trk2als: el clip MIDI como piano roll (como el editor de Joy2Live).
// El clip es la chain; cada fila de la chain es una página de 16 pasos.
// Monofónico: cada nota dura hasta la siguiente, un KILL o el final del
// clip; el velocity va en MVEL (0-127 x2). Todo se guarda en las phrases de
// siempre, así el tracker y el exportador lo entienden igual.

#include "BaseClasses/View.h"
#include "ViewData.h"
#include <vector>

class ClipView : public View {
  public:
    ClipView(GUIWindow &w, ViewData *viewData);
    ~ClipView();
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();
    virtual bool WantsRawButtons() { return true; }
    virtual void AnimationUpdate();

    struct Note {
        int start, len, pitch, vel, instr;
    };

  private:
    // modelo
    int pages();                    // filas de la chain con phrase (desde la 0)
    int total() { return pages() * 16; }
    unsigned char *stepPtr(int g, int &phrase, int &pos);
    void readNotes(std::vector<Note> &out);
    void writeNotes(std::vector<Note> &notes);
    int noteAt(std::vector<Note> &notes, int step);      // la que suena en ese paso
    int noteStarting(std::vector<Note> &notes, int step);
    bool addPage();

    // edición
    void putNote();
    void cutNote();
    void pasteNote();
    void pastePreview(std::vector<Note> &add, int &dropped);
    void handleButton(unsigned short mask, bool pressed);
    // deshacer: una foto del clip antes de cada cambio
    struct Snap {
        int chain;
        std::vector<unsigned char> data;
    };
    void takeSnap(Snap &s);
    void restoreSnap(const Snap &s);
    std::vector<Snap> undo_, redo_;
    bool pasting_;
    void loopButton(bool left);   // B + L / B + R
    // compases: B + A + → / ← duplica el compás, B + A + ↑ lo borra
    bool duplicatePage(bool right);
    void askDeletePage();
  public:
    void deletePageAnswer(bool yes);
  private:
    bool baPending_;   // B + A sin flecha: corta la nota al soltar A
    unsigned long lastLoopL_;
    bool prevHas_;            // cómo estaba el loop antes del primer toque
    int prevS_, prevE_;   // Select + A sostenido: se ve dónde va a caer lo copiado
    // marcar bloques, como en Joy2Live
    void selRect(int &s0, int &s1, int &p0, int &p1);
    void selectedNotes(std::vector<Note> &notes, std::vector<int> &idx);
    void copySelection(bool cut);
    void moveSelection(int dt, int dir, int octave);
    void duplicateSelection();
    void placeNotes(std::vector<Note> &notes, std::vector<Note> &add);
    // escala del proyecto (como Scale Awareness de Live)
    int stepPitch(int pitch, int dir);
    int degree(int pitch);
    int fromDegree(int d);
    int shiftPitch(int pitch, int fromRef, int toRef);
    void message(const char *fmt, int n = -1);
    void moveNote(int dt, int dp);
    void changeLength(int d);
    void changeVelocity(int d);
    void moveCursor(int dt, int dp);
    void audition(int step);
    void stopAudition();
    void switchView(ViewType vt);
    void keepVisible();
    // fold: solo las filas de la escala (y las que tienen notas)
    void buildRows();
    int rowOf(int pitch);
    std::vector<int> rows_;   // de arriba abajo
    bool allowed_[128];
    std::string noteName(int pitch);

    int cursorStep_, cursorPitch_;
    int topPitch_;          // la fila de arriba
    int lastLen_, lastVel_;
    bool aUsed_, xUsed_, bUsed_;
    bool auditioning_;
    int playStep_;
    std::vector<Note> clipboard_;   // lo copiado o cortado
    int copyStep_, copyPitch_;      // dónde estaba el cursor al copiar
    bool marking_;
    int markLevel_;                 // 1 libre, 2 pasos completos, 3 todo el clip
    int anchorStep_, anchorPitch_;
    bool aPending_, bPending_;      // A / B tocados solos (al soltar)
    std::string msg_;
    unsigned long msgTime_;
    int chainShown_;
};

#endif
