#include <unistd.h>
#include "AppWindow.h"
#include "Application/Commands/ApplicationCommandDispatcher.h"
#include "Application/Commands/EventDispatcher.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/char.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/SelectProjectDialog.h"
#include "Application/Views/ModalDialogs/PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Player/Player.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"
#include "Views/UIController.h"
#include <string.h>
#include <stdlib.h>
#if defined(__unix__) || defined(__APPLE__)
#include <utime.h>
#endif
#include "Resources/hdfont.h"
#include "Application/Model/Config.h"
#include "Application/Utils/ProjectName.h"
#include "Application/Utils/Latin1.h"

AppWindow *instance = 0;

// trk2als: tema Lavanda (se cambia en config.xml)
GUIColor AppWindow::backgroundColor_(0x10, 0x10, 0x18);
GUIColor AppWindow::normalColor_(0xF0, 0xEE, 0xFA);
GUIColor AppWindow::borderColor_(0x8C, 0x9E, 0xFF);
GUIColor AppWindow::songviewfeColor_(0x60, 0x60, 0x7E);
GUIColor AppWindow::songview00Color_(0x3C, 0x3C, 0x52);
GUIColor AppWindow::highlightColor_(0xB8, 0xC3, 0xFF);
GUIColor AppWindow::highlight2Color_(0x8C, 0x9E, 0xFF);
GUIColor AppWindow::consoleColor_(0x00, 0xFF, 0x00);
GUIColor AppWindow::cursorColor_(0x8C, 0x9E, 0xFF);
GUIColor AppWindow::playColor_(0x8C, 0x9E, 0xFF);
GUIColor AppWindow::muteColor_(0xFF, 0x7A, 0x8A);
GUIColor AppWindow::rownumberColor_(0x60, 0x60, 0x7E);
GUIColor AppWindow::rownumber2Color_(0x8C, 0x9E, 0xFF);
GUIColor AppWindow::majorbeatColor_(0xB8, 0xC3, 0xFF);
GUIColor AppWindow::columnTitleColor_(0x8A, 0x88, 0xA8);

int AppWindow::charWidth_ = 8;
int AppWindow::charHeight_ = 8;

// #define _FORCE_SDL_EVENT_

#ifdef SDL2
static Uint32 splashDone(Uint32, void *) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_EXPOSED;
    SDL_PushEvent(&e);
    return 0;
}
#endif

static void ProjectSelectCallback(View &v, ModalView &dialog) {

    SelectProjectDialog &spd = (SelectProjectDialog &)dialog;
    if (dialog.GetReturnCode() > 0) {
        Path selected = spd.GetSelection();
        instance->SaveLastProject(selected);
        instance->LoadProject(selected.GetPath().c_str());
    } else {
        System::GetInstance()->PostQuitMessage();
    }
};

void AppWindow::defineColor(const char *colorName, GUIColor &color) {

    Config *config = Config::GetInstance();
    const char *value = config->GetValue(colorName);
    if (value) {
        unsigned char r;
        char2hex(value, &r);
        unsigned char g;
        char2hex(value + 2, &g);
        unsigned char b;
        char2hex(value + 4, &b);
        color = GUIColor(r, g, b);
    }
}

AppWindow::AppWindow(I_GUIWindowImp &imp) : GUIWindow(imp) {

    instance = this;

    // trk2als: pantalla de arranque
    {
        const char *hi = Config::GetInstance()->GetValue("HELPIDLE");
        _helpIdleMs = hi ? atoi(hi) : 0;   // trk2als: la ayuda va con L2+R2 (HELPIDLE para la de reposo)
        _overlayOn = _overlayDirty = _helpShown = false;
        _lastInput = System::GetInstance()->GetClock();
        _swallow = 0;
        _helpMask = 0;
        _r2Solo = false;
        const char *sp = Config::GetInstance()->GetValue("SPLASH");
        int ms = sp ? atoi(sp) : 2000;
        _splashUntil = ms > 0 ? System::GetInstance()->GetClock() + ms : 0;
#ifdef SDL2
        // al terminar, un "expose" para que se dibuje la lista sin esperar un botón
        if (ms > 0) SDL_AddTimer(ms + 60, splashDone, 0);
#endif
        _splashDrawn = false;
    }

    // Init all members

    _statusLine[0] = 0;

    _currentView = 0;
    _viewData = 0;
    _songView = 0;
    _projectView = 0;
    _instrumentView = 0;
    _tableView = 0;
    _nullView = 0;
    _mixerView = 0;
    _clipView = 0;
    _grooveView = 0;
    _closeProject = 0;
    _loadAfterSaveAsProject = 0;
    _loadAfterResume = 0;
    _lastA = 0;
    _lastB = 0;
    _mask = 0;
    _bGesture = false;
    _selectFirst = false;
    colorIndex_ = CD_NORMAL;

    EventDispatcher *ed = EventDispatcher::GetInstance();
    ed->SetWindow(this);

    Status::Install(this);

    // Init midi services
    MidiService::GetInstance()->Init();

    defineColor("BACKGROUND", backgroundColor_);
    defineColor("FOREGROUND", normalColor_);
    defineColor("BORDER", borderColor_);
    defineColor("SONGVIEW_FE", songviewfeColor_);
    defineColor("SONGVIEW_00", songview00Color_);
    defineColor("HICOLOR1", highlightColor_);
    defineColor("HICOLOR2", highlight2Color_);
    defineColor("CURSORCOLOR", cursorColor_);
    defineColor("PLAYCOLOR", playColor_);
    defineColor("MUTECOLOR", muteColor_);
    defineColor("ROWCOLOR1", rownumberColor_);
    defineColor("ROWCOLOR2", rownumber2Color_);
    defineColor("MAJORBEAT", majorbeatColor_);
    defineColor("COL_TITLE", columnTitleColor_);
    defineColor("CONSOLE", consoleColor_);

    GUIWindow::Clear(backgroundColor_);

    _nullView = new NullView((*this), 0);
    _currentView = _nullView;
    _nullView->SetDirty(true);

    Config *config = Config::GetInstance(); // Possible to disable autoloading
    const char *autoLoadEnabled = config->GetValue("AUTO_LOAD_LAST");
    bool shouldAutoLoad =
        (autoLoadEnabled && // trk2als: por omisión arranca en la lista de proyectos
         strcmp(autoLoadEnabled, "YES") == 0);

    SelectProjectDialog *spd = new SelectProjectDialog(*_currentView);
    Path lastProjectPath = GetLastProjectPath();
    if (shouldAutoLoad && lastProjectPath.Exists()) {
        Trace::Log("AppWindow", "Auto-loading last project: %s",
                   lastProjectPath.GetPath().c_str());
        _newProjectToLoad = lastProjectPath.GetPath().c_str();
        _loadAfterResume = true;
        delete spd;
    } else { // Show project selection dialog
        _currentView->DoModal(spd, ProjectSelectCallback);
    }

    memset(_shapeCells, 0, sizeof(_shapeCells));
    memset(_prevShapeCells, 0, sizeof(_prevShapeCells));
    _shapesDirty = true;
    _prevShapesOn = false;
    _prevViewShapesOn = false;
    _layer = 0;
    memset(_charScreen, ' ', 1200);
    memset(_preScreen, ' ', 1200);
    memset(_charScreenProp, 0, 1200);
    memset(_preScreenProp, 0, 1200);

    Redraw();
};

AppWindow::~AppWindow() { MidiService::GetInstance()->Close(); }

void AppWindow::DrawString(const char *utf8, GUIPoint &pos,
                           GUITextProperties &props, bool force) {
    // trk2als: tildes y ñ (UTF-8 -> la letra de 256)
    std::string latin = ToLatin1(utf8);
    const char *string = latin.c_str();

    // we know we don't have mode than 40 chars

    char buffer[41];
    int len = strlen(string);
    int offset = (pos._x < 0) ? -pos._x / 8 : 0;
    len -= offset;
    int available = 40 - ((pos._x < 0) ? 0 : pos._x);
    len = MIN(len, available);
    memcpy(buffer, string + offset, len);
    buffer[len] = 0;

    NAssert((pos._x < 40) && (pos._y < 30));
    int index = pos._x + 40 * pos._y;
    memcpy(_charScreen + index, buffer, len);
    unsigned char prop = colorIndex_ + (props.invert_ ? PROP_INVERT : 0);
    memset(_charScreenProp + index, prop, len);
};

void AppWindow::Clear(bool all) {
    memset(_charScreen, ' ', 1200);
    memset(_charScreenProp, 0, 1200);
    if (all) {
        memset(_preScreen, ' ', 1200);
        memset(_preScreenProp, 0, 1200);
    };
};

void AppWindow::ClearRect(GUIRect &r) {

    int x = r.Left();
    int y = r.Top();
    int w = r.Width();
    int h = r.Height();

    unsigned char *st = _charScreen + x + (40 * y);
    unsigned char *pr = _charScreenProp + x + (40 * y);
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            *st++ = ' ';
            *pr++ = 0;
        }
        st += (40 - w);
        pr += (40 - w);
    }
};

//
// Redraws the screen and flush it.
//

void AppWindow::Redraw() {

    SysMutexLocker locker(drawMutex_);

    if (_currentView) {
        _currentView->Redraw();
        Invalidate();
    }
};

//
// Flush current screen to display
//

void AppWindow::Flush() {

    SysMutexLocker locker(drawMutex_);

    Lock();
    long flushStart = System::GetInstance()->GetClock();

    // trk2als: mientras dura la pantalla de arranque no se dibuja nada más
    if (_splashUntil) {
        if (SupportsShapes() && System::GetInstance()->GetClock() < _splashUntil) {
            if (!_splashDrawn) {
                drawSplash();
                _splashDrawn = true;
                GUIWindow::Flush();
            }
            Unlock();
            return;
        }
        _splashUntil = 0;           // terminó: todo se vuelve a dibujar
        memset(_preScreen, 0xFE, sizeof(_preScreen));
        _shapesDirty = true;
        GUIColor bg = GetColor(CD_BACKGROUND);
        GUIRect all(0, 0, 640, 480);
        GUIWindow::FillRoundRect(all, 0, bg);
    }

    GUITextProperties props;
    GUIPoint pos;

    ColorDefinition color = (ColorDefinition)-1;
    pos._x = 0;
    pos._y = 0;

    int count = 0;

    // trk2als: celdas tapadas por figuras. Las figuras no se dibujan si hay un
    // diálogo encima o si la plataforma no sabe dibujarlas.
    // trk2als: celdas tapadas por figuras. Las de la pantalla no se dibujan si
    // hay una ventana de rejilla encima (las ventanas con figuras sí las dejan
    // ver) o si la plataforma no sabe dibujarlas.
    bool shapesOn = !_shapes.empty() && SupportsShapes() && _currentView &&
                    _currentView->ModalChainShapes();
    bool layersOn = false;
    if (SupportsShapes())
        for (int l = 1; l < 4; l++) if (!_layers[l].empty()) layersOn = true;
    memset(_shapeCells, 0, sizeof(_shapeCells));
    if (shapesOn) markCells(_shapes);
    if (layersOn) for (int l = 1; l < 4; l++) markCells(_layers[l]);
    // trk2als: el aviso de abajo (en todas las pantallas, donde la Session lo
    // pone): una franja con fondo encima de todo; tapa las filas 27 a 29
    std::string notice;
    bool noticeOn = SupportsShapes() && _currentView && _currentView->NoticeText(notice);
    if (noticeOn)
        for (int cell = 27 * 40; cell < 1200; cell++) _shapeCells[cell] = true;
    bool anyOn = shapesOn || layersOn || noticeOn;

    // Las celdas tapadas solo se redibujan si las figuras cambiaron o si cambió
    // el texto de alguna de ellas (entonces se repinta todo el bloque de figuras)
    if (anyOn != _prevShapesOn || shapesOn != _prevViewShapesOn) _shapesDirty = true;
    if (!_shapesDirty) {
        for (int cell = 0; cell < 1200; cell++) {
            if ((_shapeCells[cell] || _prevShapeCells[cell]) &&
                (_charScreen[cell] != _preScreen[cell] ||
                 _charScreenProp[cell] != _preScreenProp[cell])) {
                _shapesDirty = true;
                break;
            }
        }
    }

    unsigned char *current = _charScreen;
    unsigned char *previous = _preScreen;
    unsigned char *currentProp = _charScreenProp;
    unsigned char *previousProp = _preScreenProp;
    for (int y = 0; y < 30; y++) {
        for (int x = 0; x < 40; x++) {
            int cell = x + 40 * y;
            bool covered = _shapeCells[cell];
            bool dirty = (*current != *previous) || (*currentProp != *previousProp) ||
                         ((covered || _prevShapeCells[cell]) && _shapesDirty);
#ifdef _LGPT_NO_SCREEN_CACHE_
            dirty = true;
#endif
            if (dirty) {
                props.invert_ = (*currentProp & PROP_INVERT) != 0;
                if (((*currentProp) & 0x7F) != color) {
                    color = (ColorDefinition)((*currentProp) & 0x7F);
                    GUIColor gcolor = GetColor(color);
                    GUIWindow::SetColor(gcolor);
                }
                if (covered) {
                    // solo el fondo; el texto va encima de las figuras
                    GUIRect r(x * 16, y * 16, x * 16 + 16, y * 16 + 16);
                    GUIColor bg = GetColor(CD_BACKGROUND);
                    GUIWindow::FillRoundRect(r, 0, bg);
                } else {
                    GUIWindow::DrawChar(*current, pos, props);
                }
                count++;
            }
            current++;
            previous++;
            currentProp++;
            previousProp++;
            pos._x += AppWindow::charWidth_;
        }
        pos._y += AppWindow::charHeight_;
        pos._x = 0;
    }

    if (anyOn && _shapesDirty) {
        if (shapesOn) paintShapes(_shapes);
        // el texto de la rejilla que cae sobre figuras, encima de ellas
        for (int cell = 0; cell < 1200; cell++) {
            if (!_shapeCells[cell]) continue;
            unsigned char ch = _charScreen[cell];
            unsigned char prop = _charScreenProp[cell];
            GUIColor fg = GetColor((ColorDefinition)(prop & 0x7F));
            int x = (cell % 40) * 16, y = (cell / 40) * 16;
            if (prop & PROP_INVERT) {
                GUIRect r(x, y, x + 16, y + 16);
                GUIWindow::FillRoundRect(r, 0, fg);
                fg = GetColor(CD_BACKGROUND);
            }
            if (ch != ' ') {
                char str[2] = {(char)ch, 0};
                GUIPoint p(x, y);
                GUIWindow::BlendText(str, p, fg, 1);
            }
        }
        // las ventanas con figuras, encima de todo lo de abajo
        if (layersOn) for (int l = 1; l < 4; l++) paintShapes(_layers[l]);
    }
    if (noticeOn) {
        GUIColor bg = GetColor(CD_BACKGROUND), fg = GetColor(CD_NORMAL);
        GUIRect band(0, 432, 640, 480);
        GUIWindow::FillRoundRect(band, 0, bg);
        // siempre con la letra grande: si no cabe en una línea, en dos (se
        // corta entre palabras); más de dos, se recorta
        const size_t W = 36;
        std::string l1 = notice, l2;
        size_t nl = notice.find('\n');
        if (nl != std::string::npos) { l1 = notice.substr(0, nl); l2 = notice.substr(nl + 1); }
        else if (notice.size() > W) {
            size_t cut = notice.rfind(' ', W);
            if (cut == std::string::npos || cut < W / 2) cut = W;
            l1 = notice.substr(0, cut);
            l2 = notice.substr(cut);
        }
        while (!l2.empty() && l2[0] == ' ') l2.erase(0, 1);
        if (l1.size() > W) l1 = l1.substr(0, W);
        if (l2.size() > W) l2 = l2.substr(0, W - 2) + "..";
        GUIPoint p(44, l2.empty() ? 450 : 436);
        GUIWindow::BlendText(l1.c_str(), p, fg, 1);
        if (!l2.empty()) { p._y = 456; GUIWindow::BlendText(l2.c_str(), p, fg, 1); }
    }
    // trk2als: la ayuda va encima de todo (rejilla y figuras)
    if (_overlayOn && (_overlayDirty || count > 0 || (anyOn && _shapesDirty))) {
        for (size_t i = 0; i < _overlay.size(); i++) {
            Shape &sh = _overlay[i];
            if (sh.type == 0) {
                GUIRect r(sh.x, sh.y, sh.x + sh.w, sh.y + sh.h);
                if (sh.thick > 0)
                    GUIWindow::FrameRoundRect(r, sh.radius, sh.thick, sh.color);
                else
                    GUIWindow::FillRoundRect(r, sh.radius, sh.color);
            } else {
                GUIPoint p(sh.x, sh.y);
                GUIWindow::BlendText(sh.text.c_str(), p, sh.color, sh.scale);
            }
        }
        _overlayDirty = false;
    }
    memcpy(_prevShapeCells, _shapeCells, sizeof(_shapeCells));
    _shapesDirty = false;
    _prevShapesOn = anyOn;
    _prevViewShapesOn = shapesOn;

    long flushEnd = System::GetInstance()->GetClock();
    GUIWindow::Flush();
    Unlock();
    memcpy(_preScreen, _charScreen, 1200);
    memcpy(_preScreenProp, _charScreenProp, 1200);
};

void AppWindow::LoadProject(const Path &p) {
    Trace::Log("LoadProject", "%s\n", p.GetPath().c_str());
    _root = p;

    _closeProject = false;
    _loadAfterSaveAsProject = false;

    PersistencyService *persist = PersistencyService::GetInstance();

    TablePlayback::Reset();

    Path::SetAlias("project", _root.GetPath().c_str());
    Path::SetAlias("samples", "project:samples");
#if defined(__unix__) || defined(__APPLE__)
    {   // trk2als: la carpeta recuerda cuándo se abrió (el navegador dice "hace 3 horas")
        Path r(_root);
        std::string c = r.GetCanonicalPath();
        utime(c.c_str(), NULL);
    }
#endif

    // Load the sample pool

    SamplePool *pool = SamplePool::GetInstance();

    unsigned int load_result = pool->Load();
    // trk2als: y los que el proyecto usa desde la librería o los presets
    int missingSamples = pool->LoadRefs();

    Project *project = new Project();

    bool succeeded = persist->Load();
    if (!succeeded) {
        project->GetInstrumentBank()->AssignDefaults();
    };

    // Project

    WatchedVariable::Disable();

    project->GetInstrumentBank()->Init();

    WatchedVariable::Enable();

    ApplicationCommandDispatcher::GetInstance()->Init(project);

    // Create view data

    _viewData = new ViewData(project);

    // Create & observe the player
    Player *player = Player::GetInstance();
    bool playerOK = player->Init(project, _viewData);
    player->AddObserver(*this);

    // Create the controller
    UIController *controller = UIController::GetInstance();
    controller->Init(project, _viewData);

    // Create & observe all views
    _songView = new SongView((*this), _viewData, _root.GetName().c_str());
    _songView->AddObserver((*this));



    _projectView = new ProjectView((*this), _viewData);
    _projectView->AddObserver((*this));

    _instrumentView = new InstrumentView((*this), _viewData);
    _instrumentView->AddObserver((*this));

    _tableView = new TableView((*this), _viewData);
    _tableView->AddObserver((*this));

    _grooveView = new GrooveView((*this), _viewData);
    _grooveView->AddObserver(*this);

    _mixerView = new MixerView((*this), _viewData);
    _mixerView->AddObserver(*this);

    _clipView = new ClipView((*this), _viewData);
    _clipView->AddObserver(*this);

    _currentView = _songView;
    if (missingSamples > 0) {   // trk2als: se movió o borró algo de la librería
        char m[96];
        snprintf(m, sizeof(m), "%s\nSe movieron o borraron de la\nlibrería o de los presets",
                 missingSamples == 1 ? "Falta 1 sample" : "Faltan samples");
        if (missingSamples > 1) {
            char t[32];
            snprintf(t, sizeof(t), "Faltan %d samples", missingSamples);
            snprintf(m, sizeof(m), "%s\nSe movieron o borraron de la\nlibrería o de los presets", t);
        }
        MessageBox *mb = new MessageBox(*_songView, m, MBBF_OK);
        _songView->DoModal(mb);
    }
    _currentView->OnFocus();

    if (!playerOK) {
        MessageBox *mb =
            new MessageBox(*_songView, "Failed to initialize audio", MBBF_OK);
        _songView->DoModal(mb);
    }

    // Report on sample & SoundFont load fails
    if (load_result) {
      const char *err_str = (load_result == SLOAD_ERR_MAX_SAMPLES) ? "Maximum number of samples exceeded"
	: (load_result == SLOAD_ERR_MAX_SOUNDFONTS) ? "Maximum number of SoundFonts exceeded"
	: (load_result == SLOAD_ERR_MAX_SAMPLES | SLOAD_ERR_MAX_SOUNDFONTS) ? "Maximum number of samples and SoundFonts exceeded"	
	: (load_result == SLOAD_ERR_INVALID_DIR) ? "Sample directory could not be opened"
	: "Unknown error loading sample pool";
      Trace::Error(err_str) ;
      MessageBox *mb =
            new MessageBox(*_currentView, err_str);	  
    _currentView->DoModal(mb);
    }
    
    Redraw();
}

void AppWindow::CloseProject() {

    _closeProject = false;
    Player *player = Player::GetInstance();
    player->Stop();
    player->RemoveObserver(*this);

    player->Reset();

    SamplePool *pool = SamplePool::GetInstance();
    pool->Reset();

    TableHolder::GetInstance()->Reset();
    TablePlayback::Reset();

    ApplicationCommandDispatcher::GetInstance()->Close();

    SAFE_DELETE(_songView);
    SAFE_DELETE(_projectView);
    SAFE_DELETE(_instrumentView);
    SAFE_DELETE(_tableView);
    SAFE_DELETE(_clipView);

    UIController *controller = UIController::GetInstance();
    controller->Reset();

    SAFE_DELETE(_viewData);

    _currentView = _nullView;
    _nullView->SetDirty(true);

    SelectProjectDialog *spd = new SelectProjectDialog(*_currentView);
    _currentView->DoModal(spd, ProjectSelectCallback);
};

AppWindow *AppWindow::Create(GUICreateWindowParams &params) {
    I_GUIWindowImp &imp =
        I_GUIWindowFactory::GetInstance()->CreateWindowImp(params);
    AppWindow *w = new AppWindow(imp);
    return w;
};

void AppWindow::SetDirty() { _isDirty = true; };

bool AppWindow::onEvent(GUIEvent &event) {

    // We need to tell the app to quit once we're out of the
    // mixer lock, otherwise the windows driver will never return

    _shouldQuit = false;

    _isDirty = false;

    unsigned short v = 1 << event.GetValue();

    MixerService *sm = MixerService::GetInstance();
    sm->Lock();

    switch (event.GetType()) {

    case ET_PADBUTTONDOWN: {
        _lastInput = System::GetInstance()->GetClock();
        // trk2als: L2+R2 juntos o el botón del frente prenden y apagan la
        // ayuda; esos botones no llegan a las pantallas
        if (v & ((1 << EPBT_L2) | (1 << EPBT_R2) | (1 << EPBT_HELP))) {
            bool again = (_helpMask & v) != 0;
            // R2 solo (sin L2 en ningún momento): captura al soltarlo
            if (v & (1 << EPBT_R2)) { if (!again) _r2Solo = !(_helpMask & (1 << EPBT_L2)) && !_overlayOn; }
            else _r2Solo = false;
            _helpMask |= v;
            bool combo = (_helpMask & (1 << EPBT_L2)) && (_helpMask & (1 << EPBT_R2));
            if (!again && (combo || (v & (1 << EPBT_HELP)))) {
                if (_overlayOn) hideHelp();
                else if (_currentView && _currentView != _nullView && !_currentView->HasModal() &&
                         SupportsShapes() && !_currentView->GetHelpItems().empty()) showHelp();
                _isDirty = true;
            }
            break;
        }
        _helpShown = false;
        if (_overlayOn) {   // trk2als: cierra la ayuda y nada más
            hideHelp();
            _swallow |= v;
            _isDirty = true;
            break;
        }
        bool repeat = (_mask & v) != 0;
        _mask |= v;
        if (_currentView)
            joyButton(v, true, repeat);
        break;
    }

    case ET_PADBUTTONUP:
        _lastInput = System::GetInstance()->GetClock();
        if (v & ((1 << EPBT_L2) | (1 << EPBT_R2) | (1 << EPBT_HELP))) {
            _helpMask &= ~v;
            if ((v & (1 << EPBT_R2)) && _r2Solo) {
                _r2Solo = false;
                if (!_overlayOn && _instrumentView && _currentView && !_currentView->HasModal()) {
                    // la pista donde estás: la del instrumento abierto o la del cursor
                    int track = (_currentView == _instrumentView) ? -1 : _viewData->songX_;
                    const char *m = _instrumentView->CaptureAutomation(track);
                    if (m) _currentView->SetNotification(m);
                    if (_currentView != _instrumentView) _currentView->SetDirty(true);
                    _isDirty = true;
                }
            }
            break;
        }
        if (_swallow & v) {   // el que cerró la ayuda
            _swallow &= ~v;
            break;
        }
        _mask &= (0xFFFF - v);
        if (_currentView)
            joyButton(v, false, false);
        break;

    case ET_SYSQUIT:
        _shouldQuit = true;
        break;

        /*		case ET_KEYDOWN:
            if
           (event.GetValue()==EKT_ESCAPE&&!Player::GetInstance()->IsRunning()) {
                if (_currentView!=_listView) {
                    CloseProject() ;
                    _isDirty=true ;
                } else {
                    System::GetInstance()->PostQuitMessage() ;
                };
            } ;*/

    default:
        break;
    }
    sm->Unlock();

    if (_shouldQuit) {
        onQuitApp();
    }
    if (_closeProject) {
        CloseProject();
        _isDirty = true;
    }
    if (_loadAfterSaveAsProject) {
        CloseProject();
        _isDirty = true;
        LoadProject(_newProjectToLoad.c_str());
    }
#ifdef _SHOW_GP2X_
    Redraw();
#else
    if (_isDirty)
        Redraw();
#endif
    return false;
};

void AppWindow::onUpdate() {
    // trk2als: se acabó la pantalla de arranque: a dibujar lo de abajo (la
    // lista de proyectos) sin esperar a que se apriete un botón
    if (_splashUntil && System::GetInstance()->GetClock() >= _splashUntil) {
        if (_currentView) _currentView->SetDirty(true);
        Redraw();
        Flush();
    }
    // trk2als: Ableton Link (tempo y play/stop con la sesión)
    if (Player::GetInstance()) {
        Player *pl = Player::GetInstance();
        pl->LinkPoll();
        // si cambian los links conectados, se repinta (la barra y la ficha)
        static int lastLinks = -2;
        int links = pl->LinkOn() ? pl->LinkPeers() : -1;
        if (links != lastLinks) {
            lastLinks = links;
            // (se repinta ya: SetDirty solo se atiende cuando llega un botón)
            if (_currentView && _currentView != _nullView) Redraw();
        }
    }
    if (_currentView && _currentView->NoticeExpired()) {   // trk2als: se va el aviso de abajo
        _currentView->SetDirty(true);
        Redraw();
    }
    if (_currentView && _currentView->TickChain()) {   // trk2als
        _currentView->SetDirty(true);
        Redraw();
    }
    // trk2als: un sonido lanzado sigue su camino aunque se esté en otra pantalla
    if (_songView && _currentView != _songView && _songView->Tick() && _currentView) {
        _currentView->SetDirty(true);   // la barrita del navegador (o lo que entró)
        Redraw();
    }
    // trk2als: pruebas: TRK_PRESETS=1 abre el navegador de sonidos al empezar
    {
        static bool once = false;
        const char *tp = getenv("TRK_PRESETS");
        if (!once && tp && *tp && _currentView == _songView && !_currentView->HasModal()) {
            once = true;
            // TRK_PRESETS=save: la de guardar un kit
            _currentView->DoModal(strcmp(tp, "save") == 0
                ? new PresetDialog(*_currentView, true, InstrumentPresets::PK_KIT)
                : new PresetDialog(*_currentView));
            _currentView->SetDirty(true);
            Redraw();
        }
    }
    if (_loadAfterResume) {
        _loadAfterResume = false;
        _isDirty = true;
        LoadProject(_newProjectToLoad.c_str());
        return;
    }

    // trk2als: pruebas sin teclado (p. ej. el binario de ARM en un emulador):
    // TRK_AUTOPLAY=segundos toca la Session desde el inicio y sale
    {
        static int autoplay = -2;
        static unsigned long autoStart = 0;
        if (autoplay == -2) {
            const char *e = getenv("TRK_AUTOPLAY");
            autoplay = e ? atoi(e) : -1;
        }
        if (autoplay > 0 && _currentView == _songView) {
            unsigned long now = System::GetInstance()->GetClock();
            if (autoStart == 0) {
                autoStart = now;
                Player::GetInstance()->OnSongStartButton(0, 7, false, false);
            } else if (now - autoStart > (unsigned long)autoplay * 1000) {
                autoplay = -1;
                Player::GetInstance()->Stop();
                System::GetInstance()->PostQuitMessage();
            }
        }
    }

    // trk2als: ayuda en reposo
    if (!_overlayOn && !_helpShown && _helpIdleMs > 0 && _currentView &&
        _currentView != _nullView && !_currentView->HasModal() && SupportsShapes() &&
        !(Player::GetInstance() && Player::GetInstance()->IsRunning()) && _mask == 0 &&
        System::GetInstance()->GetClock() - _lastInput > (unsigned long)_helpIdleMs &&
        !_currentView->GetHelpItems().empty()) {
        showHelp();
    }

    // Call AnimationUpdate periodically (~10-25Hz depending on frame rate)
    static unsigned int animTick = 0;
    if ((animTick++ % 2) == 0) {  // every 2nd call, roughly 25Hz at 50Hz main loop
        if (_currentView) {
            // trk2als: el audio dibuja la posición (OnPlayerUpdate) con este
            // mismo candado: sin él, los dos pisaban la lista de figuras
            SysMutexLocker locker(drawMutex_);
            _currentView->AnimationUpdate();
        }
    }
    Flush();
}

void AppWindow::LayoutChildren() {};

/************************************************************
 trk2als: botones como en Joy2Live / LSDJ

   Select + flechas   pantallas, como el R + flechas de LGPT
   Select + A         pegar          (L + A de LGPT)
   Select, B          marcar / clonar (L + B de LGPT)
   Select + Start     detener        (R + Start de LGPT)
   ← + Select + Start detener todo
   ← + Start          tocar la fila  (L + Start de LGPT)
   B, luego Select    mute (suelta B primero para dejarlo fijo)
   B, luego Start     solo (igual)
 Los atajos de LGPT con L y R siguen funcionando.
 ************************************************************/

// un paso de LGPT: R + flecha (y soltar)
void AppWindow::joyStep(unsigned short dir) {
    _currentView->ProcessButton(EPBM_R | dir, true);
    _currentView->ProcessButton(0, false);
}

unsigned short AppWindow::joyLegacy(unsigned short phys) {
    if (_bGesture) {   // B + Select = R + B (mute), B + Start = R + A (solo)
        unsigned short m = phys & ~(EPBM_B | EPBM_SELECT | EPBM_START);
        if (phys & EPBM_B) m |= EPBM_R;
        if (phys & EPBM_SELECT) m |= EPBM_B;
        if (phys & EPBM_START) m |= EPBM_A;
        return m;
    }
    unsigned short m = phys & ~EPBM_SELECT;
    // trk2als: en el instrumento Select es Select (como en LSDJ) y L es el
    // gatillo; en las demás pantallas Select sigue siendo el L de LGPT
    if ((phys & EPBM_SELECT) && _currentView == _instrumentView && !(phys & EPBM_START))
        return m | EPBM_SELECT;
    if (phys & EPBM_SELECT) m |= (phys & EPBM_START) ? EPBM_R : EPBM_L;
    return m;
}

void AppWindow::joyButton(unsigned short v, bool pressed, bool repeat) {
    unsigned short phys = _mask;
    View::physMask_ = phys;
    if (_currentView->HasModal()) {   // las ventanitas: A, B y flechas tal cual
        _currentView->ProcessButton(phys & ~EPBM_SELECT, pressed);
        return;
    }
    if (_currentView->InterceptButton(phys, v, pressed, repeat)) {   // trk2als
        _isDirty = true;
        if (!pressed && v == EPBM_B) _bGesture = false;
        if (!pressed && v == EPBM_SELECT) _selectFirst = false;
        return;
    }
    if (_currentView->WantsRawButtons()) {   // trk2als: el piano roll
        View::pressedButton_ = pressed ? v : 0;
        _currentView->ProcessButton(phys, pressed);
        _bGesture = false;
        _selectFirst = false;
        return;
    }
    // trk2als: Select + L deshace, Select + R rehace (como en el clip)
    if ((_currentView == _songView || _currentView == _instrumentView) && (phys & EPBM_SELECT) && !(phys & EPBM_B) &&
        (v == EPBM_L || v == EPBM_R)) {
        if (pressed && !repeat && _currentView->Undo(v == EPBM_R)) _isDirty = true;
        return;
    }
    const unsigned short arrows = EPBM_UP | EPBM_DOWN | EPBM_LEFT | EPBM_RIGHT;
    if (pressed && !repeat) {
        if (v == EPBM_SELECT && !(phys & EPBM_B)) _selectFirst = true;
        if ((v == EPBM_SELECT || v == EPBM_START) && (phys & EPBM_B) && !_selectFirst)
            _bGesture = true;
    }
    // pantallas: Select + flechas = el R + flechas de LGPT (Song ⇄ Chain ⇄
    // Phrase ⇄ Instrument ⇄ Table; en la Song, ↑ Project y ↓ Mixer)
    if ((phys & EPBM_SELECT) && !_bGesture && (v & arrows)) {
        if (pressed && !repeat) joyStep(v);
        return;
    }
    _currentView->ProcessButton(joyLegacy(phys), pressed);
    if (!pressed && v == EPBM_B) _bGesture = false;
    if (!pressed && v == EPBM_SELECT) _selectFirst = false;
}

void AppWindow::Update(Observable &o, I_ObservableData *d) {

    ViewEvent *ve = (ViewEvent *)d;

    switch (ve->GetType()) {

    case VET_SWITCH_VIEW: {
        ViewType *vt = (ViewType *)ve->GetData();
        if (_currentView) {
            _currentView->LooseFocus();
        }
        // trk2als: la curva del clip es solo mientras se está ahí
        if (*vt != VT_CLIP && _viewData && _viewData->curveID_) {
            _viewData->curveLastID_ = _viewData->curveID_;
            _viewData->curveID_ = 0;
        }
        switch (*vt) {
        case VT_SONG:
            _currentView = _songView;
            break;
        case VT_PROJECT:
            _currentView = _projectView;
            break;
        case VT_INSTRUMENT:
            _currentView = _instrumentView;
            break;
        case VT_TABLE:
            _currentView = _tableView;
            break;
        case VT_TABLE2:
            _currentView = _tableView;
            break;
        case VT_GROOVE:
            _currentView = _grooveView;
            break;
        case VT_MIXER:
            _currentView = _mixerView;
            break;
        case VT_CLIP:
            _currentView = _clipView;
            break;
        }
        _shapes.clear(); // trk2als: las figuras son de la pantalla anterior
        TrimShapeLayers(0);
        _shapesDirty = true;
        _currentView->SetFocus(*vt);
        _isDirty = true;
        GUIWindow::Clear(backgroundColor_, true);
        Clear(true);
        Redraw();
        break;
    }

    case VET_PLAYER_POSITION_UPDATE: {
        PlayerEvent *pt = (PlayerEvent *)ve;

        if (_currentView) {
            SysMutexLocker locker(drawMutex_);
            _currentView->OnPlayerUpdate(pt->GetType(), pt->GetTickCount());
            Invalidate();
        }

        break;
    }

        /*	  case VET_LIST_SELECT:
              {
                char *name=(char*)ve->GetData() ;
                LoadProject(name) ;
                break ;
              } */

    case VET_SAVEAS_PROJECT: {
        char *name = (char *)ve->GetData();
        _loadAfterSaveAsProject = true;
        _newProjectToLoad = name;
        break;
    }

    case VET_QUIT_PROJECT: {
        // defer event to after we got out of the view
        _closeProject = true;
        break;
    }
    case VET_QUIT_APP:
        _shouldQuit = true;
        break;
    }
}

void AppWindow::onQuitApp() {
#if defined(__linux__)
    // trk2als: salir sin desarmar nada (desarmar el audio y Link al final
    // dejaba la consola en negro); el sistema cierra el audio y los archivos
    Trace::Log("SALIR", "saliendo");
    fflush(stdout);
    fflush(stderr);
    fsync(1);
    _exit(0);
#endif
    Player *player = Player::GetInstance();
    player->Stop();
    player->RemoveObserver(*this);

    player->Reset();
    System::GetInstance()->PostQuitMessage();
}
void AppWindow::Print(char *line) {

    //	GUIWindow::Clear(View::backgroundColor_,true) ;
    Clear();
    strcpy(_statusLine, line);
    // unwrapped for gcc
    int position = 40;
    position -= strlen(_statusLine);
    position /= 2;
    GUIPoint pos(position, 12);
    //
    GUITextProperties props;
    SetColor(CD_NORMAL);
    DrawString(_statusLine, pos, props);
    char buildString[80];
    sprintf(buildString, "Piggy build %s.%s.%s", PROJECT_NUMBER,
            PROJECT_RELEASE, BUILD_COUNT);
    pos._y = 28;
    pos._x = (40 - strlen(buildString)) / 2;
    DrawString(buildString, pos, props);
    Flush();
};

void AppWindow::SetColor(ColorDefinition cd) { colorIndex_ = cd; };

/************************************************************
 trk2als: pinceles
 ************************************************************/

GUIColor AppWindow::GetColor(ColorDefinition cd) {
    switch (cd) {
    case CD_BACKGROUND: return backgroundColor_;
    case CD_BORDER: return borderColor_;
    case CD_HILITE1: return highlightColor_;
    case CD_HILITE2: return highlight2Color_;
    case CD_CONSOLE: return consoleColor_;
    case CD_CURSOR: return cursorColor_;
    case CD_PLAY: return playColor_;
    case CD_MUTE: return muteColor_;
    case CD_SONGVIEWFE: return songviewfeColor_;
    case CD_SONGVIEW00: return songview00Color_;
    case CD_ROW: return rownumberColor_;
    case CD_ROW2: return rownumber2Color_;
    case CD_MAJORBEAT: return majorbeatColor_;
    case CD_COL_TITLE: return columnTitleColor_;
    default: return normalColor_;
    }
}

bool AppWindow::SupportsShapes() { return GUIWindow::SupportsShapes(); }

void AppWindow::ClearShapes() {
    target().clear();
    _shapesDirty = true;
}

void AppWindow::TrimShapeLayers(int keep) {
    for (int l = keep + 1; l < 4; l++) {
        if (!_layers[l].empty()) {
            _layers[l].clear();
            _shapesDirty = true;
        }
    }
}

void AppWindow::markCells(std::vector<Shape> &v) {
    for (size_t i = 0; i < v.size(); i++) {
        Shape &sh = v[i];
        int w = sh.w, h = sh.h;
        if (sh.type == 1) {
            w = (sh.scale > 0 ? 16 * sh.scale : 8) * (int)sh.text.size();
            h = sh.scale > 0 ? 16 * sh.scale : 12;
        }
        int cx0 = MAX(0, sh.x / 16), cy0 = MAX(0, sh.y / 16);
        int cx1 = MIN(39, (sh.x + w - 1) / 16), cy1 = MIN(29, (sh.y + h - 1) / 16);
        for (int cy = cy0; cy <= cy1; cy++)
            for (int cx = cx0; cx <= cx1; cx++)
                _shapeCells[cx + 40 * cy] = true;
    }
}

void AppWindow::paintShapes(std::vector<Shape> &v) {
    for (size_t i = 0; i < v.size(); i++) {
        Shape &sh = v[i];
        if (sh.type == 0) {
            GUIRect r(sh.x, sh.y, sh.x + sh.w, sh.y + sh.h);
            if (sh.thick > 0)
                GUIWindow::FrameRoundRect(r, sh.radius, sh.thick, sh.color);
            else
                GUIWindow::FillRoundRect(r, sh.radius, sh.color);
        } else {
            GUIPoint p(sh.x, sh.y);
            GUIWindow::BlendText(sh.text.c_str(), p, sh.color, sh.scale);
        }
    }
}

void AppWindow::AddRect(int x, int y, int w, int h, GUIColor color, int radius,
                        int thick) {
    Shape sh;
    sh.type = 0;
    sh.x = x; sh.y = y; sh.w = w; sh.h = h;
    sh.radius = radius; sh.thick = thick;
    sh.color = color;
    target().push_back(sh);
    _shapesDirty = true;
}

void AppWindow::AddText(int x, int y, const char *text, GUIColor color, int scale) {
    Shape sh;
    sh.type = 1;
    sh.x = x; sh.y = y;
    sh.text = ToLatin1(text);
    sh.scale = scale;
    sh.color = color;
    target().push_back(sh);
    _shapesDirty = true;
}

Path AppWindow::GetLastProjectPath() {
    Path lastProjectFile(LAST_PROJECT_NAME);
    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(lastProjectFile.GetPath().c_str(), "r");

    if (!file) {
        return Path();
    }

    // Get file size
    file->Seek(0, SEEK_END);
    int length = file->Tell();
    
    if (length <= 0) {
        file->Close();
        delete file;
        return Path();
    }

    // Allocate buffer and seek back to start
    char *buffer = (char *)SYS_MALLOC(length + 1);
    memset(buffer, 0, length + 1);

    file->Seek(0, SEEK_SET); // Seek back to start
    int bytes = file->Read(buffer, 1, length); // Read full length
    file->Close();
    delete file;

    if (bytes <= 0) {
        Trace::Error("GetLastProject: Failed to read last project file");
        SYS_FREE(buffer);
        return Path();
    }

    buffer[bytes] = 0; // Null terminate

    // Remove newline if present
    int len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = 0;
    }

    Path result;
    if (strlen(buffer) > 0) {
        if (IsProjectFolder(Path(buffer).GetName())) { // trk2als: chancho_ o lgpt_
            result = Path(buffer);
        } else {
            Trace::Error("GetLastProject: Invalid project path format: %s",
                         buffer);
        }
    }
    if (!result.IsDirectory()) {
        Trace::Error("GetLastProject: path does not exist: %s", result.GetPath().c_str());
    }

    SYS_FREE(buffer);
    return result;
}

void AppWindow::SaveLastProject(const Path &p) {
    Path lastProjectFile(LAST_PROJECT_NAME);
    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(lastProjectFile.GetPath().c_str(), "w");

    if (!file) {
        Trace::Error("SaveLastProject: Failed to open %s for writing",
                     LAST_PROJECT_NAME);
        return;
    }

    std::string pathStr = p.GetPath();
    file->Write(pathStr.c_str(), 1, pathStr.length());
    file->Write("\n", 1, 1);
    file->Close();
    delete file;

    Trace::Log("SaveLastProject", "Saved last project: %s",
               p.GetPath().c_str());
}

/************************************************************
 trk2als: pantalla de arranque. La ficha del doble seis (el
 "chancho 6") con el nombre de lado, de abajo hacia arriba.
 ************************************************************/
void AppWindow::drawSplash() {
    GUIColor bg(0x14, 0x14, 0x14);
    GUIColor fg(0xE8, 0xE6, 0xE3);
    GUIRect all(0, 0, 640, 480);
    GUIWindow::FillRoundRect(all, 0, bg);

    // la ficha: 90 x 204, centrada junto con el texto
    const int s = 2;                     // escala de la letra (16 px -> 32)
    const int adv = 11;                  // avance por letra (más juntas que la rejilla)
    const char *name = "CHANCHO 6";
    int len = (int)strlen(name);
    int textH = len * adv * s;
    const int w = 90, h = 204, gap = 16;
    int x0 = (640 - (w + gap + 16 * s)) / 2, y0 = (480 - h) / 2;
    GUIRect frame(x0, y0, x0 + w, y0 + h);
    GUIWindow::FrameRoundRect(frame, 11, 3, fg);
    GUIRect bar(x0 + 10, y0 + h / 2 - 2, x0 + w - 10, y0 + h / 2 + 2);
    GUIWindow::FillRoundRect(bar, 2, fg);
    // seis puntos en cada mitad
    const int r = 10;
    int cx[2] = {x0 + 26, x0 + w - 26};
    for (int half = 0; half < 2; half++) {
        int top = y0 + half * (h / 2);
        for (int k = 0; k < 3; k++) {
            int cy = top + (h / 2) * (2 * k + 1) / 6 + (half ? 2 : -2);
            for (int c = 0; c < 2; c++) {
                GUIRect dot(cx[c] - r, cy - r, cx[c] + r, cy + r);
                GUIWindow::FillRoundRect(dot, r, fg);
            }
        }
    }
    // el nombre, girado: se lee de abajo hacia arriba
    int tx = x0 + w + gap;
    int ty = y0 + h / 2 + textH / 2;     // la base, abajo
    for (int i = 0; i < len; i++) {
        unsigned char ch = ((unsigned char)name[i]) & 127;
        const unsigned char *g = hdfont[ch];
        for (int gy = 0; gy < 16; gy++) {
            for (int gx = 0; gx < 16; gx++) {
                if (g[gy * 16 + gx] < 128) continue;
                int px = tx + gy * s;
                int py = ty - (i * adv + gx - 2) * s;
                GUIRect px1(px, py, px + s, py + s);
                GUIWindow::FillRoundRect(px1, 0, fg);
            }
        }
    }
}

/************************************************************
 trk2als: ayuda en reposo (ver AppWindow.h)
 ************************************************************/

void AppWindow::showHelp() {
    std::string all = _currentView->GetHelpItems();
    std::vector<std::string> keys, whats;
    size_t pos = 0, keyLen = 0;
    while (pos < all.size() && keys.size() < 15) {
        size_t end = all.find(';', pos);
        if (end == std::string::npos) end = all.size();
        std::string item = all.substr(pos, end - pos);
        pos = end + 1;
        if (item.empty()) continue;
        size_t colon = item.find(':');
        std::string k = colon == std::string::npos ? item : item.substr(0, colon);
        std::string w = colon == std::string::npos ? "" : item.substr(colon + 1);
        keys.push_back(ToLatin1(k));   // trk2als: con tildes (y el largo bien contado)
        whats.push_back(ToLatin1(w));
        if (keys.back().size() > keyLen) keyLen = keys.back().size();
    }
    if (keys.empty()) return;
    _helpShown = true;
    GUIColor bg = GetColor(CD_BACKGROUND);
    GUIColor fg = GetColor(CD_NORMAL);
    GUIColor hi = GetColor(CD_HILITE1);
    GUIColor hi2 = GetColor(CD_HILITE2);
    GUIColor dim((bg._r * 45 + fg._r * 55) / 100, (bg._g * 45 + fg._g * 55) / 100,
                 (bg._b * 45 + fg._b * 55) / 100);
    GUIColor panel((bg._r * 88 + fg._r * 12) / 100, (bg._g * 88 + fg._g * 12) / 100,
                   (bg._b * 88 + fg._b * 12) / 100);
    const int X = 32, W = 640 - 2 * X, LINE = 24;
    int h = 20 + 16 + 16 + int(keys.size()) * LINE + 8 + 16 + 20;
    int Y = (480 - h) / 2;
    if (Y < 36) Y = 36;
    _overlay.clear();
    Shape sh;
    sh.type = 0; sh.thick = 0; sh.radius = 6;
    // trk2als: la ventana clásica: fondo oscuro, borde blanco, atajos y
    // título en el color de acento, la descripción en blanco
    sh.x = X; sh.y = Y; sh.w = W; sh.h = h; sh.color = bg;
    _overlay.push_back(sh);
    sh.thick = 3; sh.color = fg;
    _overlay.push_back(sh);
    Shape t;
    t.type = 1; t.scale = 1;
    t.x = X + 24; t.y = Y + 20; t.text = "AYUDA"; t.color = hi2;
    _overlay.push_back(t);
    int kx = X + 24, wx = kx + 16 * int(keyLen) + 24;
    for (size_t i = 0; i < keys.size(); i++) {
        int y = Y + 20 + 16 + 16 + int(i) * LINE;
        t.scale = 1;
        t.x = kx; t.y = y; t.text = keys[i]; t.color = hi2;
        _overlay.push_back(t);
        std::string w = whats[i];
        int room = (X + W - 16 - wx) / 16;
        if (room > 0 && (int)w.size() > room) w = w.substr(0, room);
        t.x = wx; t.text = w; t.color = fg;
        _overlay.push_back(t);
    }
    t.scale = 1;   // con la letra de siempre (la chica no tiene tildes)
    t.x = X + 24; t.y = Y + h - 28; t.text = ToLatin1("L2+R2 o cualquier botón la cierra"); t.color = dim;
    _overlay.push_back(t);
    _overlayOn = true;
    _overlayDirty = true;
    Flush();
}

void AppWindow::hideHelp() {
    _overlayOn = false;
    _overlay.clear();
    // todo lo que tapaba se vuelve a dibujar
    memset(_preScreen, 0xFE, sizeof(_preScreen));
    _shapesDirty = true;
    GUIColor bg = GetColor(CD_BACKGROUND);
    GUIRect all(0, 0, 640, 480);
    GUIWindow::FillRoundRect(all, 0, bg);
}
