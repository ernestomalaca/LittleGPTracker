
#ifndef _APP_WINDOW_H_
#define _APP_WINDOW_H_

#include "Application/Views/ChainView.h"
#include "Application/Views/ConsoleView.h"
#include "Application/Views/GrooveView.h"
#include "Application/Views/InstrumentView.h"
#include "Application/Views/MixerView.h"
#include "Application/Views/NullView.h"
#include "Application/Views/PhraseView.h"
#include "Application/Views/ProjectView.h"
#include "Application/Views/SongView.h"
#include "Application/Views/TableView.h"
#include "Application/Views/ViewData.h"
#include "Foundation/Observable.h"
#include "System/Process/SysMutex.h"
#include "System/io/Status.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

#include <vector>
#include <string>

#define PROP_INVERT 0x80

class AppWindow : public GUIWindow, I_Observer, Status {
  protected:
    AppWindow(I_GUIWindowImp &imp);
    virtual ~AppWindow();

  public:
    static AppWindow *Create(GUICreateWindowParams &);
    void LoadProject(const Path &path);
    void SaveLastProject(const Path &p);
    void CloseProject();

    virtual void Clear(bool all = false);
    virtual void ClearRect(GUIRect &rect);
    virtual void SetColor(ColorDefinition cd);
    void SetDirty();

    // trk2als: capa de figuras (pinceles) sobre la rejilla de caracteres.
    // Coordenadas de 640x480. Las figuras se quedan hasta ClearShapes(); al
    // cambiar de pantalla se borran solas. Se dibujan encima del fondo de las
    // celdas y debajo de su texto.
    bool SupportsShapes();
    void ClearShapes();
    void AddRect(int x, int y, int w, int h, GUIColor color, int radius = 0,
                 int thick = 0);
    void AddText(int x, int y, const char *text, GUIColor color, int scale = 1);
    GUIColor GetColor(ColorDefinition cd);

  protected: // GUIWindow implementation
    virtual bool onEvent(GUIEvent &event);
    virtual void onUpdate();
    virtual void LayoutChildren();
    virtual void Flush();
    virtual void Redraw();

    // override draw string to avoid going too far off
    // the screen.
    virtual void DrawString(const char *string, GUIPoint &pos,
                            GUITextProperties &props, bool overlay = false);

    // I_Observer implementation

    virtual void Update(Observable &o, I_ObservableData *d);

    // Status implementation

    virtual void Print(char *);

    void defineColor(const char *colorName, GUIColor &color);

    void onQuitApp();

  private:
    View *_currentView;
    // trk2als: botones como en Joy2Live (LSDJ): Select para zonas y edición,
    // B + Select / B + Start para mute y solo. Se traducen a lo que ya
    // entienden las pantallas (R = zonas, L = marcar/pegar)
    bool _bGesture;      // B se sostuvo antes de Select/Start
    bool _selectFirst;   // Select se sostuvo antes que B
    void joyButton(unsigned short v, bool pressed, bool repeat);
    unsigned short joyLegacy(unsigned short phys);
    void joyNavigate(unsigned short dir);
    void joyStep(unsigned short dir);
    void joySwitch(int viewType);
    int joyCurrentType();
    ViewData *_viewData;
    SongView *_songView;
    ChainView *_chainView;
    PhraseView *_phraseView;
    ProjectView *_projectView;
    InstrumentView *_instrumentView;
    TableView *_tableView;
    GrooveView *_grooveView;
    NullView *_nullView;
    MixerView *_mixerView;

    Path _root;

    bool _isDirty;
    bool _closeProject;
    bool _loadAfterSaveAsProject;
    bool _loadAfterResume;
    bool _shouldQuit;
    unsigned short _mask;
    unsigned long _lastA;
    unsigned long _lastB;
    char _statusLine[80];
    std::string _newProjectToLoad;
    unsigned char _charScreen[1200];
    unsigned char _charScreenProp[1200];
    unsigned char _preScreen[1200];
    unsigned char _preScreenProp[1200];

    struct Shape {
        int type; // 0 = rectángulo, 1 = texto
        int x, y, w, h, radius, thick, scale;
        GUIColor color;
        std::string text;
        Shape() : type(0), x(0), y(0), w(0), h(0), radius(0), thick(0),
                  scale(1), color(0, 0, 0) {}
    };
    std::vector<Shape> _shapes;
    bool _shapeCells[1200];     // celdas tapadas por figuras en este cuadro
    bool _prevShapeCells[1200]; // ... y en el anterior (hay que redibujarlas)
    bool _shapesDirty;          // las figuras cambiaron: redibujar sus celdas
    bool _prevShapesOn;

    static GUIColor backgroundColor_;
    static GUIColor normalColor_;
    static GUIColor borderColor_;
    static GUIColor songviewfeColor_;
    static GUIColor songview00Color_;
    static GUIColor highlight2Color_;
    static GUIColor highlightColor_;
    static GUIColor consoleColor_;
    static GUIColor cursorColor_;
    static GUIColor playColor_;
    static GUIColor muteColor_;
    static GUIColor rownumberColor_;
    static GUIColor rownumber2Color_;
    static GUIColor majorbeatColor_;
    static GUIColor columnTitleColor_;
#define LAST_PROJECT_NAME "bin:last_project"

    ColorDefinition colorIndex_;

    static int charWidth_;
    static int charHeight_;

    SysMutex drawMutex_;

    Path GetLastProjectPath();
};

#endif
