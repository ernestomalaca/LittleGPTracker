
#ifndef _VIEW_H_
#include <string>
#define _VIEW_H_

#include "Application/Model/Config.h"
#include "Application/Model/Project.h"
#include "Application/Player/Player.h"
#include "Foundation/T_SimpleList.h"
#include "I_Action.h"
#include "UIFramework/Interfaces/I_GUIGraphics.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "ViewEvent.h"
#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif

#define VU_METER_HEIGHT 8
#define VU_METER_CLIP_LEVEL 7
#define VU_METER_WARN_LEVEL 5

enum GUIEventPadButtonMasks {
    EPBM_LEFT = 1,
    EPBM_DOWN = 2,
    EPBM_RIGHT = 4,
    EPBM_UP = 8,
    EPBM_L = 16,
    EPBM_B = 32,
    EPBM_A = 64,
    EPBM_R = 128,
    EPBM_START = 256,
    EPBM_SELECT = 512,
    EPBM_DOUBLE_A = 1024,
    EPBM_DOUBLE_B = 2048,
    EPBM_X = 4096,   // trk2als
    EPBM_Y = 8192
};

enum ViewType {
    VT_SONG,
    VT_PROJECT,
    VT_INSTRUMENT,
    VT_TABLE,  // Table screen (bajo el clip)
    VT_TABLE2, // Table screen under instrument
    VT_GROOVE,
    VT_MIXER,
    VT_CLIP   // trk2als: piano roll del clip MIDI
};

enum ViewMode {
    VM_NORMAL,
    VM_NEW,
    VM_CLONE,
    VM_SELECTION,
    VM_MUTEON,
    VM_SOLOON
};

enum ColorDefinition {
    CD_BACKGROUND,
    CD_NORMAL,
    CD_BORDER,
    CD_HILITE1,
    CD_HILITE2,
    CD_CONSOLE,
    CD_CURSOR,
    CD_PLAY,
    CD_MUTE,
    CD_SONGVIEWFE,
    CD_SONGVIEW00,
    CD_ROW,
    CD_ROW2,
    CD_MAJORBEAT,
    CD_COL_TITLE
};

enum ViewUpdateDirection { VUD_LEFT = 0, VUD_RIGHT, VUD_UP, VUD_DOWN };

class View;
class ModalView;

typedef void (*ModalViewCallback)(View &v, ModalView &d);

class View : public Observable {
  public:
    View(GUIWindow &w, ViewData *viewData);
    View(View &v);

    void SetFocus(ViewType vt) {
        viewType_ = vt;
        hasFocus_ = true;
        OnFocus();
    };

    void LooseFocus() { hasFocus_ = false; };

    void Clear();

    void ProcessButton(unsigned short mask, bool pressed);

    void Redraw();

    // Override in subclasses

    virtual void DrawView() = 0;
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick) = 0;
    virtual void OnFocus() = 0;
    virtual void AnimationUpdate() {}
    // trk2als: la vista recibe los botones tal cual (Select, L, R, X, Y), sin
    // la traducción a los atajos de LGPT; pressedButton_ = el que se acaba de apretar
    virtual bool WantsRawButtons() { return false; }
    // trk2als: si cambiaron la escala, ofrece pasar las notas (Fit to Scale)
    void checkScaleChange();
    void FitScaleAnswer(bool yes);
    static unsigned short pressedButton_;
    static unsigned short physMask_;   // los botones de verdad (antes de traducirlos a LGPT)

    void SetDirty(bool dirty);

    // Primitive locking mechanism

    bool Lock();
    void WaitForObject();
    void Unlock();

    // Char based draw routines

    virtual void SetColor(ColorDefinition cd);
    virtual void ClearRect(int x, int y, int w, int h);
    // trk2als: pinceles, en pixeles de 640x480 (una celda de texto = 16x16)
    bool SupportsShapes();
    bool HasModal() { return modalView_ != 0; };
    void ClearShapes();
    void FillRect(int x, int y, int w, int h, GUIColor c, int radius = 0);
    void FrameRect(int x, int y, int w, int h, GUIColor c, int radius = 0,
                   int thick = 2);
    void DrawText(int x, int y, const char *txt, GUIColor c, int scale = 1); // scale 0 = letra chica 8x12
    GUIColor ColorOf(ColorDefinition cd);
    static GUIColor Mix(GUIColor a,GUIColor b,int amount); // amount en % (0 = a, 100 = b)

    virtual void DrawString(int x, int y, const char *txt,
                            GUITextProperties &props);

    void DoModal(ModalView *view, ModalViewCallback cb = 0);

    void EnableNotification();
    void SetNotification(const char *notification, int offset = 2);

  protected:
    virtual void ProcessButtonMask(unsigned short mask, bool pressed) = 0;

    // to remove once everything got to viewdata

    inline void updateData(unsigned char *c, int offset, unsigned char limit,
                           bool wrap) {
        int v = *c;
        if (v == 0xFF) { // Uninitiaized data
            v = 0;
        }
        v += offset;
        if (v < 0)
            v = (wrap ? (limit + 1 + v) : 0);
        if (v > limit)
            v = (wrap ? v - (limit + 1) : limit);
        *c = v;
    }

    GUIPoint GetAnchor();
    GUIPoint GetTitlePosition();

    void drawMap();
    void drawNotes(int x0 = -1, int dx = 3) ; // trk2als: x0/dx de la grilla de la Song
    // trk2als: pinceles compartidos (como los mockups). Barra superior: el set,
    // las pestañas SES CLIP INST MIX (tab = la activa; -1 ninguna), el
    // contexto de la vista y a la derecha tempo y reproducción. Devuelve false
    // si la pantalla no dibuja formas (entonces la vista pone su título de antes)
    enum { TAB_SES = 0, TAB_CLIP, TAB_INST, TAB_MIX };
    bool drawTopBar(int tab, const char *context);
    // Barra de ayuda abajo: "A:poner nota;B+<>:página" (tecla:acción, separados por ';')
    bool drawHelpBar(const char *items);
public:
    // trk2als: los atajos de la pantalla ("A:nota;X:largo"); ya no van en una
    // barra fija: se muestran en un recuadro si uno se queda quieto
    const std::string &GetHelpItems() { return helpItems_; }
protected:
    std::string helpItems_;
    static std::string projectName_;   // lo pone la Song al abrir el proyecto
    // al cambiar play/stop o el tempo se repinta sola (desde drawNotes)
    void refreshTopBar();
    int topTab_;
    std::string topCtx_;
    bool topRunning_;
    int topTempo_;
    void drawVUMeter(uint8_t leftBars, uint8_t rightBars, GUIPoint pos,
                     GUITextProperties props, int vuIndex,
                     bool forceRedraw = false);

  public: // temp hack for modl windo constructors
    GUIWindow &w_;
    ViewData *viewData_;

  protected:
    ViewMode viewMode_;
    bool isDirty_; // .Do we need to redraw screeen
    ViewType viewType_;
    bool hasFocus_;
    uint8_t prevLeftVU_[8]; // inertia tracking for VU (8 channels)
    uint8_t prevRightVU_[8];

  private:
    unsigned short mask_;
    bool locked_;
    uint32_t notificationTime_;
    uint16_t NOTIFICATION_TIMEOUT;
    std::string displayNotification_;
    int notiDistY_;
    static bool initPrivate_;
    ModalView *modalView_;
    ModalViewCallback modalViewCallback_;

  public:
    static int margin_;
    static int songRowCount_;
    static bool miniLayout_;
    static int altRowNumber_;
};

#endif
