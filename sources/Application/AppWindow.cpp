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
#include "Foundation/Variables/WatchedVariable.h"
#include "Player/Player.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"
#include "Views/UIController.h"
#include <string.h>

AppWindow *instance = 0;

GUIColor AppWindow::backgroundColor_(0x1D, 0x0A, 0x1F);
GUIColor AppWindow::normalColor_(0xF5, 0xEB, 0xFF);
GUIColor AppWindow::borderColor_(0xFF, 0x00, 0x8C);
GUIColor AppWindow::songviewfeColor_(0xA5, 0x5B, 0x8F);
GUIColor AppWindow::songview00Color_(0x85, 0x3B, 0x6F);
GUIColor AppWindow::highlightColor_(0xB7, 0x50, 0xD1);
GUIColor AppWindow::highlight2Color_(0xDB, 0x33, 0xDB);
GUIColor AppWindow::consoleColor_(0x00, 0xFF, 0x00);
GUIColor AppWindow::cursorColor_(0xFF, 0x00, 0x8C);
GUIColor AppWindow::playColor_(0xFF, 0x00, 0x8C);
GUIColor AppWindow::muteColor_(0xF5, 0xEB, 0xFF);
GUIColor AppWindow::rownumberColor_(0xBA, 0x28, 0xF9);
GUIColor AppWindow::rownumber2Color_(0xFF, 0x00, 0xFF);
GUIColor AppWindow::majorbeatColor_(0xBA, 0x28, 0xF9);
GUIColor AppWindow::columnTitleColor_(0xA5, 0x5B, 0x8F);

int AppWindow::charWidth_ = 8;
int AppWindow::charHeight_ = 8;

// #define _FORCE_SDL_EVENT_

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

    // Init all members

    _statusLine[0] = 0;

    _currentView = 0;
    _viewData = 0;
    _songView = 0;
    _chainView = 0;
    _phraseView = 0;
    _projectView = 0;
    _instrumentView = 0;
    _tableView = 0;
    _nullView = 0;
    _mixerView = 0;
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
        (!autoLoadEnabled || // Default to yes if not in config
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
    memset(_charScreen, ' ', 1200);
    memset(_preScreen, ' ', 1200);
    memset(_charScreenProp, 0, 1200);
    memset(_preScreenProp, 0, 1200);

    Redraw();
};

AppWindow::~AppWindow() { MidiService::GetInstance()->Close(); }

void AppWindow::DrawString(const char *string, GUIPoint &pos,
                           GUITextProperties &props, bool force) {

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

    GUITextProperties props;
    GUIPoint pos;

    ColorDefinition color = (ColorDefinition)-1;
    pos._x = 0;
    pos._y = 0;

    int count = 0;

    // trk2als: celdas tapadas por figuras. Las figuras no se dibujan si hay un
    // diálogo encima o si la plataforma no sabe dibujarlas.
    bool shapesOn = !_shapes.empty() && SupportsShapes() && _currentView &&
                    !_currentView->HasModal();
    memset(_shapeCells, 0, sizeof(_shapeCells));
    if (shapesOn) {
        for (size_t i = 0; i < _shapes.size(); i++) {
            Shape &sh = _shapes[i];
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

    // Las celdas tapadas solo se redibujan si las figuras cambiaron o si cambió
    // el texto de alguna de ellas (entonces se repinta todo el bloque de figuras)
    if (shapesOn != _prevShapesOn) _shapesDirty = true;
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

    if (shapesOn && _shapesDirty) {
        for (size_t i = 0; i < _shapes.size(); i++) {
            Shape &sh = _shapes[i];
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
    }
    memcpy(_prevShapeCells, _shapeCells, sizeof(_shapeCells));
    _shapesDirty = false;
    _prevShapesOn = shapesOn;

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

    // Load the sample pool

    SamplePool *pool = SamplePool::GetInstance();

    unsigned int load_result = pool->Load();

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

    _chainView = new ChainView((*this), _viewData);
    _chainView->AddObserver((*this));

    _phraseView = new PhraseView((*this), _viewData);
    _phraseView->AddObserver((*this));

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

    _currentView = _songView;
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
    SAFE_DELETE(_chainView);
    SAFE_DELETE(_phraseView);
    SAFE_DELETE(_projectView);
    SAFE_DELETE(_instrumentView);
    SAFE_DELETE(_tableView);

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
        bool repeat = (_mask & v) != 0;
        _mask |= v;
        if (_currentView)
            joyButton(v, true, repeat);
        break;
    }

    case ET_PADBUTTONUP:

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
    if (_loadAfterResume) {
        _loadAfterResume = false;
        _isDirty = true;
        LoadProject(_newProjectToLoad.c_str());
        return;
    }

    // Call AnimationUpdate periodically (~10-25Hz depending on frame rate)
    static unsigned int animTick = 0;
    if ((animTick++ % 2) == 0) {  // every 2nd call, roughly 25Hz at 50Hz main loop
        if (_currentView) {
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
    if (phys & EPBM_SELECT) m |= (phys & EPBM_START) ? EPBM_R : EPBM_L;
    if ((phys & EPBM_START) && (phys & EPBM_LEFT) && !(phys & EPBM_SELECT)) {
        m &= ~EPBM_LEFT;   // ← + Start = la fila (L + Start)
        m |= EPBM_L;
    }
    return m;
}

void AppWindow::joyButton(unsigned short v, bool pressed, bool repeat) {
    unsigned short phys = _mask;
    if (_currentView->HasModal()) {   // las ventanitas: A, B y flechas tal cual
        _currentView->ProcessButton(phys & ~EPBM_SELECT, pressed);
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
        switch (*vt) {
        case VT_SONG:
            _currentView = _songView;
            break;
        case VT_CHAIN:
            _currentView = _chainView;
            break;
        case VT_PHRASE:
            _currentView = _phraseView;
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
        }
        _shapes.clear(); // trk2als: las figuras son de la pantalla anterior
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
    _shapes.clear();
    _shapesDirty = true;
}

void AppWindow::AddRect(int x, int y, int w, int h, GUIColor color, int radius,
                        int thick) {
    Shape sh;
    sh.type = 0;
    sh.x = x; sh.y = y; sh.w = w; sh.h = h;
    sh.radius = radius; sh.thick = thick;
    sh.color = color;
    _shapes.push_back(sh);
    _shapesDirty = true;
}

void AppWindow::AddText(int x, int y, const char *text, GUIColor color, int scale) {
    Shape sh;
    sh.type = 1;
    sh.x = x; sh.y = y;
    sh.text = text;
    sh.scale = scale;
    sh.color = color;
    _shapes.push_back(sh);
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
        if (strstr(buffer, "lgpt_") != NULL) { // Ensure it's an lgpt project
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
