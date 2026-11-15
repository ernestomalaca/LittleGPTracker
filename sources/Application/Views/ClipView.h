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
    void moveNote(int dt, int dp);
    void changeLength(int d);
    void changeVelocity(int d);
    void moveCursor(int dt, int dp);
    void audition(int step);
    void stopAudition();
    void switchView(ViewType vt);
    void keepVisible();
    std::string noteName(int pitch);

    int cursorStep_, cursorPitch_;
    int topPitch_;          // la fila de arriba
    int lastLen_, lastVel_;
    bool aUsed_, xUsed_, bUsed_;
    bool auditioning_;
    int playStep_;
    bool hasClip_;
    Note clip_;             // lo que cortó B + A
    int chainShown_;
};

#endif
