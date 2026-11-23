#ifndef _CLIP_VIEW_H_
#define _CLIP_VIEW_H_

// trk2als: el clip MIDI como piano roll (como el editor de Joy2Live).
// El clip es la chain; cada fila de la chain es una página de 16 pasos.
// Monofónico: cada nota dura hasta la siguiente, un KILL o el final del
// clip; el velocity va en MVEL (0-127 x2). Todo se guarda en las phrases de
// siempre, así el tracker y el exportador lo entienden igual.

#include "BaseClasses/View.h"
#include "ViewData.h"
#include "Application/Model/Clip.h"
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
        int tick;   // nudge: 0..23 (1/24 de paso) después de 'start'
        int rat;    // subdivisiones (1 = la nota entera)
        int ramp;   // rampa de velocity de las subdivisiones (-4..4)
        Note() : start(0), len(1), pitch(60), vel(100), instr(0xFF), tick(0), rat(1), ramp(0) {}
    };

  private:
    // modelo
    int pages();                    // filas de la chain con phrase (desde la 0)
    int total() { return pages() * 16; }
    Clip &clip();
    void readNotes(std::vector<Note> &out);
    void writeNotes(std::vector<Note> &notes);
    int noteAt(std::vector<Note> &notes, int step, int pitch);   // la que suena ahí
    int noteStarting(std::vector<Note> &notes, int step, int pitch);
    bool stolen_;   // writeNotes cortó una nota: había más de 4 a la vez
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
        Clip clip;
    };
    void takeSnap(Snap &s);
  public:
    // trk2als: algo de fuera cambió un clip (la captura de R2): Select + L lo deshace
    void PushUndo(int chain, const Clip &before) {
        Snap s; s.chain = chain; s.clip = before;
        undo_.push_back(s);
        if (undo_.size() > 64) undo_.erase(undo_.begin());
        redo_.clear();
    }
  private:
    void restoreSnap(const Snap &s);
    std::vector<Snap> undo_, redo_;
    bool pasting_;
    void loopButton(bool left);   // B + L / B + R
    // compases: B + A + → / ← duplica el compás, B + A + ↑ lo borra
    bool duplicatePage(bool right);
    void askDeletePage();
    void removePage(int page);
    void cropRange(int &k0, int &k1);
    void askCrop();
  public:
    void deletePageAnswer(bool yes);
    void cropAnswer(bool yes);
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
    bool targets(std::vector<Note> &notes, std::vector<int> &idx);
    void nudge(int d);
    void quantize();   // trk2als: X + Y: a la rejilla (lo que diga el Proyecto)
    bool bTapGhost_;   // trk2als: B sola: olvidar lo tocado (la silueta de la captura)
    int ghostCount_;
    int followPage_;   // trk2als: follow: la página que se va siguiendo (-1 = no)
    void ratchet(int d);
    void rampVel(int d);
    void moveCursor(int dt, int dp);
    // trk2als: la curva de un parámetro (A + L desde el instrumento)
    bool curveOn();
    int curveInstrValue();
    double curveNorm(int v);
    void curveText(int v, char *buf, int size, bool shortForm);
    int curveNudge(int v, int dir, bool big);
    bool curveButton(unsigned short mask, unsigned short v);
    int curveNodeAt(int step);
    void curveSyncCursor();
    void curvePutNode();
    void curveMoveNode(int dt, int dv, bool big);
    void curveRange(int &s0, int &s1);
    void curveCopy(bool cut);
    void curvePaste();
    void curveClear(int from, int to);
    void curveSimplify();
    void curveWrite(const std::vector<AutoPoint> &seg, int from, int len);
    bool curveTake(int from, int to, std::vector<AutoPoint> &seg);
    int curveDenorm(double n);
    void curveTransform(int mode, int d);
    bool curveAPending_;
    bool curveYPending_;   // Y tocado sola con algo marcado: simplificar al soltar
    bool curveBAPending_;           // B + A: borra al soltar (si no vino ↑ ↓)
    void curveDuplicate();
    bool curveAPut_;                // A sola en un paso vacío: el nodo se pone al soltar
    int curveSel_;                  // el nodo elegido con Y + ← → (índice), -1 ninguno
    int curveAccel_;                // sostener una flecha: de a más
    int accelN_;
    unsigned short accelV_;
    unsigned long accelT_;            // A sola con algo marcado: al soltar, cancela la marca
    bool curvePasting_;             // Select + A sostenido: se ve dónde cae
    bool curveBPending_;            // B con algo marcado: copia al soltar (si no vino A)
    void curveToggle();
    int curveVal_;                  // el punto: su valor (su paso es cursorStep_)
    unsigned int curveShownID_;     // la curva que se estaba viendo
    bool curveGrab_;                // A agarró un nodo
    bool curveMarking_;
    int curveAnchor_;
    std::vector<AutoPoint> curveClip_;   // nodos copiados (desde el paso 0)
    int curveClipLen_;
    bool selTapC_;                  // Select tocado solo
    void drawCurves();
    void drawPages(int page, int npages);
    void audition(int step);
    void stopAudition();
    void switchView(ViewType vt);
    void keepVisible();
    // kit de batería: 16 filas fijas, una por pad (como el Move)
    class SampleInstrument *kit();
    int kitBase();
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
    bool dupDone_;                  // se acaba de duplicar: Select+B empieza otra marca
    int markLevel_;                 // 1 libre, 2 pasos completos, 3 todo el clip
    int anchorStep_, anchorPitch_;
    bool aPending_, bPending_;      // A / B tocados solos (al soltar)
    std::string msg_;
    unsigned long msgTime_;
    int chainShown_;
};

#endif
