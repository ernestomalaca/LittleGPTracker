
#ifndef _SONG_VIEW_H_
#define _SONG_VIEW_H_

#include <string>
#include <vector>
#include "BaseClasses/View.h"
#include <map>
#include "Application/Model/Clip.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "Application/Views/ModalDialogs/PresetDialog.h"
#include "Application/Views/SoundLauncher.h"

class SongView;

class SongView : public View, public LaunchHost {
  public:
    SongView(GUIWindow &w, ViewData *viewData, const char *song);
    ~SongView();

    // View implementation
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual bool Undo(bool redo);
  private:
    bool lsdjGesture(bool solo);
  public:
    // trk2als: la captura de R2 (AppWindow): antes y, si cambió algo, a la historia
    void CaptureBegin();
    void CaptureCommit();
    void openVirtualClip();
    virtual bool InterceptButton(unsigned short phys, unsigned short v, bool pressed,
                                 bool repeat);
    virtual void DrawView();
    virtual void AnimationUpdate();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();
    virtual bool Tick();
    // trk2als: lanzar sonidos desde el navegador (como clips): lo que pasa
    // alrededor del cambio cuando lo lanzó la Session
    virtual void LaunchBefore(int instrument, int track, bool firstOfVisit);
    virtual void LaunchAfter(int instrument, int track, const std::string &name, bool ok);
    virtual void LaunchPurged();
    virtual void LaunchNotify(const char *msg);

  protected:
    void processNormalButtonMask(unsigned int mask);
    void processSelectionButtonMask(unsigned int mask);

    void extendSelection();
    void updateChain(int offset);
    void updateSongOffset(int offset);
    void updateCursor(int dx, int dy);
    void setChain(unsigned char);
    void cutPosition();
    void clonePosition();
    void deepClonePosition();
    void pasteLast();
    void fillClipboardData();
    GUIRect getSelectionRect();
    void copySelection();
    void pasteClipboard();
    void cutSelection();

    void unMuteAll();
    void toggleMute();
    void switchSoloMode();

    void onStart();
    void startCurrentRow();
    void startImmediate();
    void onStop();

    void jumpToNextSection(int dir);

  private:
    bool updatingChain_; // .Flag that tells we're updating chain
    // trk2als: navegación de Joy2Live
    bool aOpenPending_;  // A sobre un clip: al soltar (sin moverse) se abre
    bool bTap_;          // B tocada sola: al soltar quita mute y solo
    void openCell();     // abre la phrase (MIDI) o el clip (audio) de la celda
    void createClip();   // celda vacía: clip nuevo de un compás
    unsigned short newClipId();
                         //  so we don't allocate chains while
                         //  doing multiple A+ARROWS

    int updateX_; // . Position where update is happening
    int updateY_; //

    unsigned char lastChain_; // .Last chain clipboard
    unsigned char lastClip_;  // .trk2als: último loop usado en una pista de audio

    bool isAudioColumn(int x); // .trk2als: la columna es una pista de audio
  public: // trk2als: tipo de pista desde la cabecera de la columna
    void ApplyTrackType(int channel, int type, bool clear);
    void clampTrackCursor();
    GUIPoint gridAnchor();
    std::string cellName();
    void nameNewChain(unsigned char chain, int col = -1);
    void LoadTrackPreset(const std::string &name);
    int presetCh_;
    bool newTrack_;   // trk2als: la pista se acaba de agregar (cancelar la quita)
    void openTrackBrowser(int ch, bool isNew);
    void OnTrackBrowser(int code, const std::string &name);
    void askDeleteTrack(int ch);
    void OnSf2Done(bool ok);
    void goToInstrument(int ch);
    void pickAudio(int cell = -1);
    void placeAudio();
    void stepAudio(int dir);
    bool isLoadedClip(int i);
    std::string clipLabel(unsigned char id);
    std::vector<unsigned char> audioClips();
    int newSlotCell_;   // celda que muestra '+' mientras se tiene A
    void OnAudioPicked(bool imported);
    int pickClip_, pickCell_;
    int pendingTypeCh_, pendingType_; // .cambio que espera confirmación
    void launchTrackRemoved(int ch);   // trk2als: se borró una pista: lo lanzado se ajusta
  private:
    // trk2als: el sonido lanzado: se lee por detrás y entra en el 1
    SoundLauncher *launcher_;   // el de todo el proyecto (también lo usa el instrumento)
  public:
  private:
    bool headerMode_; // .el cursor está en la cabecera de las columnas
    bool rowMode_;   // trk2als: el cursor en la columna de filas (como LSDJ)
    void processRowButtonMask(unsigned int mask);
    void processHeaderButtonMask(unsigned int mask);
    void prepareClip(unsigned char clip); // .pone el instrumento en looper sync
    unsigned char freeClip(); // .primer instrumento que no usa ninguna phrase ni pista de audio

    // trk2als: la Session como rejilla de bloques (figuras)
    bool blockGrid();                 // se dibuja así (plataformas con figuras)
    void drawBlockGrid();
    std::string blockState();         // lo que cambia al sonar (para redibujar)
    void blockRedraw();
    bool noticeOn();
    std::string lastBlockState_;
    int blockVu_[8];
    bool soloed_[SONG_CHANNEL_COUNT];   // trk2als: pistas en solo (la "S" de la cabecera)
    bool inBlockRedraw_;
    unsigned long lastVuDraw_;
    int lastPlayedPosition_[8]; // .Last position played for song
                                //  used for drawing purpose

    int lastQueuedPosition_[8]; // .Last live queued position for song
                                //  used for drawing purpose

    struct {                  // .Clipboard structure
        bool active_;         // .If currently making a selection
        unsigned char *data_; // .Null if clipboard empty
        int x_;               // .Current selection positions
        int y_;               // .
        int offset_;          // .
        int width_;           // .Size of selection
        int height_;          // .
        int srcLeft_;         // .trk2als: primera columna de donde se copió
    } clipboard_;

    int saveX_;
    int saveY_;
    int saveOffset_;
    std::string songname_;
    bool invertBatt_;
    bool needClear_;
    bool canDeepClone_;
    void nudgeTempo(int direction);
    void DrawVuBars();   // Draw VU bars - called from both DrawView and
                         // AnimationUpdate
    uint8_t jumpLength_; // When jumping columns with B
    int vuBarHeightsL_[1]; // Master channel stereo VU meter
    int vuBarHeightsR_[1];
  private:
    // trk2als: deshacer en la Session: la rejilla y el contenido de los clips
    // que estaban en ella (se toma una foto cada vez que la rejilla cambia)
    struct SesSnap {
        std::vector<unsigned char> grid;
        std::vector<unsigned char> reps;   // trk2als: las repeticiones de cada escena
        std::map<int, Clip> clips;
        // trk2als: el sonido de una pista (cargar preset / kit vacío / ...)
        int instrTrack;
        std::string trackName;
        InstrumentPresets::Snapshot instr;
        int tempo;   // trk2als: > 0: también el tempo (la captura con R2 lo cambia)
        SesSnap() : instrTrack(-1), tempo(-1) {}
    };
    SesSnap capBefore_;
    void snapInstr(SesSnap &s, int track);
    void beginSoundChange(int track);   // a la historia, y la pista en espera
    void endSoundChange(int track);
    void takeSesSnap(SesSnap &s);
    void restoreSesSnap(const SesSnap &s);
    void checkHistory();
    void handleButtonMask(unsigned short mask, bool pressed);
    // trk2als: edición como en el clip (Select,B marca; B copia; B+A corta;
    // Select+A duplica o pega; A+flechas mueve)
    struct SesCell {
        int dx, dy;
        bool audio;
        unsigned char value;
        Clip clip;
    };
    bool sesMarking_, sesPasting_;
    bool sesBPending_, sesAPending_, sesAMoved_, sesDupDone_;
    std::vector<SesCell> sesClip_;
    GUIRect markRect();
    void startMark();
    void endMark();
    void copyMark(std::vector<SesCell> &out);
    int pasteCells(const std::vector<SesCell> &cells, int col, int row, int &skipped);
    bool moveMark(int dx, int dy);
    void duplicateMark();
    bool queueBlink();
    void clampRows();
    int playFraction(int ch, int &total);   // cuánto lleva el loop (en 1/1000)
    // trk2als: editar escenas (en la columna de escenas): Select,B marca; B
    // copia; Select+A duplica lo marcado abajo o pega; B+A borra. Pegar y
    // duplicar recorren lo de abajo hacia abajo; borrar lo sube
    bool scnMarking_, scnBPending_;
    int scnMarkRow_;
    std::vector<SesCell> scnClip_;
    std::vector<unsigned char> scnReps_;
    int scnRows_;
    bool sceneButton(unsigned short phys, unsigned short v, bool pressed, bool repeat);
    void scnRange(int &top, int &bot);
    void scnCopy(int top, int bot, std::vector<SesCell> &cells, std::vector<unsigned char> &reps);
    bool scnInsert(int at, int n);
    void scnDelete(int top, int bot);
    bool scnPaste(int at, const std::vector<SesCell> &cells, const std::vector<unsigned char> &reps, int n);
    SesSnap sesNow_;
    bool sesNowValid_;
    std::vector<SesSnap> sesUndo_, sesRedo_;
};

#endif
