#include "ProjectView.h"
#include "Application/Player/Player.h"
#ifdef WEBSERVER
#include "Services/Web/WebServer.h"
#endif
#include "Application/Mixer/MixerService.h"
#include "Application/Model/ProjectDatas.h"
#include "Application/Model/Scale.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/NewProjectDialog.h"
#include "Application/Views/ModalDialogs/SelectProjectDialog.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UITempoField.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include "Application/Model/Config.h"
#include <cstdlib>
#include <cstring>
#include <string>

#define ACTION_PURGE            MAKE_FOURCC('P','U','R','G')
#define ACTION_SAVE             MAKE_FOURCC('S','A','V','E')
#define ACTION_SAVE_AS          MAKE_FOURCC('S','V','A','S')
#define ACTION_LOAD             MAKE_FOURCC('L','O','A','D')
#define ACTION_QUIT             MAKE_FOURCC('Q','U','I','T')
#define ACTION_PURGE_INSTRUMENT MAKE_FOURCC('P','R','G','I')
#define ACTION_TEMPO_CHANGED    MAKE_FOURCC('T','E','M','P')
#define ACTION_EXPORT_LIVE      MAKE_FOURCC('A','L','S','X')
#define ACTION_WEB              MAKE_FOURCC('W','E','B','!')   // trk2als: chancho.local

static void SaveAsProjectCallback(View &v,ModalView &dialog) {

    FileSystemService FSS;
    NewProjectDialog &npd=(NewProjectDialog &)dialog;

    if (dialog.GetReturnCode()>0) {
		std::string str_dstprjdir;
		std::string str_dstsmpdir;

		Path root("root:");
		str_dstprjdir = root.GetName() + "/" + npd.GetName();
		str_dstsmpdir = str_dstprjdir + "/samples/";

		Path path_srcprjdir("project:");
		Path path_srcsmpdir("project:samples");
		Path path_dstprjdir = Path(str_dstprjdir);
		Path path_dstsmpdir = Path(str_dstsmpdir);

        Path path_srclgptdatsav = path_srcprjdir.GetPath() + "lgptsav_tmp.dat";
        Path path_dstlgptdatsav = path_dstprjdir.GetPath() + "/lgptsav.dat";

		if (path_dstprjdir.Exists()) {
			Trace::Log("ProjectView", "Dst Dir '%s' Exist == true",
			path_dstprjdir.GetPath().c_str());
		} else {
			if (FileSystem::GetInstance()->MakeDir(path_dstprjdir.GetPath().c_str()).Failed()) {
				Trace::Log("ProjectView", "Failed to create dir '%s'", path_dstprjdir.GetPath().c_str());
				return;
			};

		if (FileSystem::GetInstance()->MakeDir(path_dstsmpdir.GetPath().c_str()).Failed()) {
			Trace::Log("ProjectView", "Failed to create sample dir '%s'", path_dstprjdir.GetPath().c_str());
			return;
		};

        if (FSS.Copy(path_srclgptdatsav, path_dstlgptdatsav) > -1) {
            FSS.Delete(path_srclgptdatsav);
        }

        I_Dir *idir_srcsmpdir =
            FileSystem::GetInstance()->Open(path_srcsmpdir.GetPath().c_str());
        if (idir_srcsmpdir) {
				idir_srcsmpdir->GetContent("*");
				idir_srcsmpdir->Sort();
				IteratorPtr<Path>it(idir_srcsmpdir->GetIterator());
				for (it->Begin();!it->IsDone();it->Next()) {
					Path &current=it->CurrentItem();
					if (current.IsFile()) {
						Path dstfile = Path((str_dstsmpdir+current.GetName()).c_str());
						Path srcfile = Path(current.GetPath());
						FSS.Copy(srcfile.GetPath(),dstfile.GetPath());
					}
				}
			}

		((ProjectView &)v).OnSaveAsProject((char*)str_dstprjdir.c_str());
		}
    }
}

static void LoadCallback(View &v,ModalView &dialog) {
    MixerService::GetInstance()->SetRenderMode(0);
    if (dialog.GetReturnCode()==MBL_YES) {
		((ProjectView &)v).OnLoadProject() ;
	}
} ;

static void QuitCallback(View &v,ModalView &dialog) {
    MixerService::GetInstance()->SetRenderMode(0);
    if (dialog.GetReturnCode()==MBL_YES) {
		((ProjectView &)v).OnQuit() ;
	}
} ;

static void PurgeCallback(View &v,ModalView &dialog) {
	((ProjectView &)v).OnPurgeInstruments(dialog.GetReturnCode()==MBL_YES) ;
} ;

ProjectView::ProjectView(GUIWindow &w,ViewData *data):FieldView(w,data) {

    lastClock_ = 0;
    lastTick_ = 0;

	project_=data->project_ ;

	// trk2als: el Proyecto como tablero (ver drawBoard). Los campos siguen
	// haciendo el trabajo (editar, acciones); sus posiciones son lógicas,
	// para que las flechas se muevan por el tablero.
	Variable *v=project_->FindVariable(VAR_TEMPO) ;
	GUIPoint position(1, 3);
    UITempoField *f = new UITempoField(ACTION_TEMPO_CHANGED, position, *v,
                                       "%d", 60, 400, 1, 10);
    T_SimpleList<UIField>::Insert(f) ;
	f->AddObserver(*this) ;
	tempoField_=f ;

    // la escala: raíz, escala, transponer, fold (en el mismo renglón)
    v = project_->FindVariable(VAR_SCALEROOT);
    if (v->GetInt() < 0) v->SetInt(0);
    position = GUIPoint(20, 3);
    rootField_ = new UIIntVarField(position, *v, "%s", 0, 11, 1, 12);
    T_SimpleList<UIField>::Insert(rootField_);

    v = project_->FindVariable(VAR_SCALE);
    if (v->GetInt() < 0) v->SetInt(0);
    position = GUIPoint(24, 3);
    scaleField_ = new UIIntVarField(position, *v, "%s", 0, scaleCount - 1, 1, 10);
    T_SimpleList<UIField>::Insert(scaleField_);

    v = project_->FindVariable(VAR_TRANSPOSE);
    position = GUIPoint(20, 4);
    transpField_ = new UIIntVarField(position, *v, "%d", -48, 48, 0x1, 0xC);
    T_SimpleList<UIField>::Insert(transpField_);

    v = project_->FindVariable(VAR_FOLD);
    position = GUIPoint(24, 4);
    foldField_ = new UIIntVarField(position, *v, "%s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(foldField_);

    // las fichas
    struct Chip { FourCC var; int x, y, min, max, xo, yo; };
    // el metrónomo va en el cuadro del tempo (↓ desde el tempo)
    v = project_->FindVariable(VAR_METRONOME);
    position = GUIPoint(1, 4);
    metroField_ = new UIIntVarField(position, *v, "%s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(metroField_);

    const Chip chips[7] = {
        {VAR_MASTERVOL, 1, 6, 10, 100, 1, 10},  {VAR_PREGAIN, 10, 6, 10, 200, 1, 10},
        {VAR_SOFTCLIP, 19, 6, 0, 4, 1, 4},      {VAR_SOFTCLIP_GAIN, 28, 6, 0, 1, 1, 1},
        {VAR_MIDIDEVICE, 1, 7, 0, 0, 1, 1},     {VAR_LINK, 10, 7, 0, 2, 1, 1},
        {VAR_LINKOFS, 19, 7, -250, 250, 5, 25},
    };
    for (int k = 0; k < 7; k++) {
        v = project_->FindVariable(chips[k].var);
        NAssert(v);
        int mx = chips[k].var == VAR_MIDIDEVICE ? MidiService::GetInstance()->Size() : chips[k].max;
        position = GUIPoint(chips[k].x, chips[k].y);
        UIIntVarField *cf = new UIIntVarField(position, *v, "%s", chips[k].min, mx, chips[k].xo, chips[k].yo);
        chipField_[k] = cf;
        T_SimpleList<UIField>::Insert(cf);
    }

    // la barra de botones
    struct Btn { const char *name; FourCC action; int x; };
    const Btn btns[6] = {{"CARGAR", ACTION_LOAD, 1}, {"GUARDAR", ACTION_SAVE, 8},
                         {"GUARDAR COMO", ACTION_SAVE_AS, 16}, {"LIMPIAR", ACTION_PURGE_INSTRUMENT, 27},
                         {"WEB", ACTION_WEB, 31}, {"SALIR", ACTION_QUIT, 34}};
    for (int k = 0; k < 6; k++) {
        position = GUIPoint(btns[k].x, 9);
        UIActionField *a = new UIActionField(btns[k].name, btns[k].action, position);
        a->AddObserver(*this);
        T_SimpleList<UIField>::Insert(a);
        btnField_[k] = a;
        btnName_[k] = btns[k].name;
    }
    SetFocus(tempoField_);
}

ProjectView::~ProjectView() {
}

void ProjectView::ProcessButtonMask(unsigned short mask,bool pressed) {

    if (!pressed)
        return;

    FieldView::ProcessButtonMask(mask);

    if (mask & EPBM_R) {
        if (mask&EPBM_DOWN) {
			ViewType vt=VT_SONG;
			ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
			SetChanged();
            NotifyObservers(&ve);
        }
    } else {
        if (mask&EPBM_START) {
            Player *player = Player::GetInstance();

            int renderMode = viewData_->renderMode_;
			if (renderMode > 0 && !player->IsRunning()) {
				viewData_->isRendering_ = true;
				View::SetNotification("Rendering started!");
			} else if (viewData_->isRendering_ && player->IsRunning()) {
				viewData_->isRendering_ = false;
				View::SetNotification("Rendering done!");
			}

			player->OnStartButton(PM_SONG,viewData_->songX_,false,viewData_->songX_) ;
		}
    };
} ;

void ProjectView::DrawView() {

    Clear() ;

    // trk2als: el tablero, todo con figuras
    if (drawTopBar(-1, "")) {
        drawBoard();
    } else {
        GUITextProperties props ;
        GUIPoint pos=GetTitlePosition() ;
        char projectString[80] ;
        sprintf(projectString, "Project (Build %s.%s.%s)", PROJECT_NUMBER,
                PROJECT_RELEASE, BUILD_COUNT);
        SetColor(CD_NORMAL);
        DrawString(pos._x,pos._y,projectString,props) ;
        FieldView::Redraw();
    }
    drawMap();

    int currentMode = project_->GetRenderMode();
    if ((viewData_->renderMode_ != currentMode) && !MixerService::GetInstance()->IsRendering()) {
        // Mode changed
        if (currentMode > 0 && viewData_->renderMode_ == 0) {
            View::SetNotification("Rendering on, press start");
        } else if (currentMode == 0 && viewData_->renderMode_ > 0) {
            View::SetNotification("Rendering off");
        }
        viewData_->renderMode_ = currentMode;
        MixerService::GetInstance()->SetRenderMode(currentMode);
    }

    View::EnableNotification();
} ;

void ProjectView::Update(Observable &,I_ObservableData *data) {

	if (!hasFocus_) {
		return ;
	}

# ifdef _64BIT
	int fourcc=*((int*)data);
#else
    int fourcc = (unsigned int)data;
#endif

    UIField *focus = GetFocus();
    if (fourcc!= ACTION_TEMPO_CHANGED) {
		// trk2als: el tablero no dibuja los campos en la rejilla
	} else {
		focus=tempoField_ ;
	}
    Player *player = Player::GetInstance();
    switch (fourcc) {
		case ACTION_PURGE:
			project_->Purge() ;
			break ;
		case ACTION_PURGE_INSTRUMENT:
		{
            MessageBox *mb = new MessageBox(*this, "Purge unused samples?",
                                            MBBF_YES | MBBF_NO);
            DoModal(mb,PurgeCallback) ;
			break ;
		}
        case ACTION_SAVE: {
            MixerService::GetInstance()->SetRenderMode(0);
            PersistencyService *service = PersistencyService::GetInstance();
            service->Save();
            break;
        }
        case ACTION_SAVE_AS: {
            PersistencyService *service = PersistencyService::GetInstance();
            service->Save("project:lgptsav_tmp.dat");
            NewProjectDialog *mb = new NewProjectDialog(*this, "root:");
            DoModal(mb, SaveAsProjectCallback);
            break;
        }
        case ACTION_EXPORT_LIVE: {
            // Guarda y llama al convertidor lgpt2als.py (repo trk2als).
            // config.xml: <TRK2ALS_DIR value="/ruta/a/trk2als/tracker"/>
            const char *dir = Config::GetInstance()->GetValue("TRK2ALS_DIR");
            if (!dir) {
                View::SetNotification("Set TRK2ALS_DIR in config.xml");
                break;
            }
            MixerService::GetInstance()->SetRenderMode(0);
            PersistencyService::GetInstance()->Save();

            std::string prj = Path("project:").GetPath();
            while (!prj.empty() && prj[prj.size() - 1] == '/')
                prj.erase(prj.size() - 1);
            std::string name = prj.substr(prj.find_last_of('/') + 1);
            if (name.compare(0, 5, "lgpt_") == 0)
                name = name.substr(5);
            std::string root = Path("root:").GetPath();
            while (!root.empty() && root[root.size() - 1] == '/')
                root.erase(root.size() - 1);
            std::string outDir = root + "/LiveExports/" + name + " Project";
            std::string log = root + "/LiveExports/export.log";
            std::string cmd = "mkdir -p '" + outDir + "' && python3 '" +
                              std::string(dir) + "/lgpt2als.py' '" + prj +
                              "' '" + std::string(dir) + "/plantilla.als' '" +
                              outDir + "/" + name + ".als' > '" + log +
                              "' 2>&1";
            Trace::Log("ProjectView", "Export Live: %s", cmd.c_str());
            int rc = system(cmd.c_str());
            View::SetNotification(rc == 0 ? "Live set exported!"
                                          : "Export failed, see export.log");
            break;
        }
        case ACTION_LOAD: {
            MessageBox *mb = new MessageBox(
                *this, "Load song and lose changes ?", MBBF_YES | MBBF_NO);
            DoModal(mb, LoadCallback);
            break;
        }
        case ACTION_QUIT: {
            MessageBox *mb = new MessageBox(*this, "Quit and lose faith ?",
                                            MBBF_YES | MBBF_NO);
            DoModal(mb, QuitCallback);
            break;
        }
        case ACTION_TEMPO_CHANGED:
			break ;
#ifdef WEBSERVER
        case ACTION_WEB: {   // trk2als: prende / apaga chancho.local
            WebServer *web = WebServer::GetInstance();
            if (!web->Allowed()) {
                View::SetNotification("WEB apagado en config.xml");
                break;
            }
            web->Toggle();
            View::SetNotification(web->IsRunning() ? "chancho.local prendido" : "chancho.local apagado");
            isDirty_ = true;
            break;
        }
#endif
		default:
			NInvalid ;
			break ;
	} ;
    focus->Draw(w_) ;
	isDirty_=true ;
} ;

void ProjectView::OnPurgeInstruments(bool removeFromDisk) {
	project_->PurgeInstruments(removeFromDisk) ;
} ;

void ProjectView::OnLoadProject() {
	ViewEvent ve(VET_QUIT_PROJECT) ;
	SetChanged();
	NotifyObservers(&ve) ;
} ;

void ProjectView::OnSaveAsProject(char * data) {
        ViewEvent ve(VET_SAVEAS_PROJECT,data) ;
	SetChanged();
	NotifyObservers(&ve) ;
} ;

void ProjectView::OnQuit() {
	ViewEvent ve(VET_QUIT_APP) ;
	SetChanged();
	NotifyObservers(&ve) ;
} ;

/************************************************************
 trk2als: el tablero
 ************************************************************/

void ProjectView::drawBoard() {
    GUIColor bg = ColorOf(CD_BACKGROUND);
    GUIColor fg = ColorOf(CD_NORMAL);
    GUIColor acc = ColorOf(CD_HILITE2);
    GUIColor card = Mix(bg, fg, 7);
    GUIColor rule = Mix(bg, fg, 16);
    GUIColor dim = Mix(bg, fg, 50);
    UIField *focus = GetFocus();
    char b[48];

    // tempo: el número con dígitos propios (nítidos a cualquier tamaño) y
    // el metrónomo abajo
    bool fT = focus == tempoField_, fM = focus == metroField_;
    FillRect(24, 46, 288, 150, card, 8);
    if (fT || fM) FrameRect(24, 46, 288, 150, acc, 8, 3);
    DrawText(40, 60, "TEMPO", acc, 0);
    snprintf(b, sizeof(b), "%d", project_->GetTempo());
    drawBigNumber(40, 80, b, 60, fg);
    DrawText(200, 118, "bpm", dim, 0);
    snprintf(b, sizeof(b), "metronomo %s", project_->FindVariable(VAR_METRONOME)->GetInt() ? "si" : "no");
    if (fM) FillRect(34, 150, 8 * (int)strlen(b) + 12, 18, acc, 3);
    DrawText(40, 153, b, fM ? bg : dim, 0);
    if (fT) DrawText(40, 176, "A+<> 1   A+^v 10   B tap", dim, 0);

    // escala: nota y escala arriba (<>), transponer y fold abajo (v)
    bool fS = focus == rootField_ || focus == scaleField_ || focus == transpField_ ||
              focus == foldField_;
    FillRect(328, 46, 288, 150, card, 8);
    if (fS) FrameRect(328, 46, 288, 150, acc, 8, 3);
    DrawText(344, 60, "ESCALA", acc, 0);
    std::string root = project_->FindVariable(VAR_SCALEROOT)->GetString();
    std::string scale = project_->FindVariable(VAR_SCALE)->GetString();
    if (scale.size() > 11) scale = scale.substr(0, 11);
    int rw = 32 * (int)root.size();
    if (focus == rootField_) FillRect(338, 82, rw + 20, 44, acc, 4);
    DrawText(348, 88, root.c_str(), focus == rootField_ ? bg : fg, 2);
    int sx = 348 + rw + 22;
    if (focus == scaleField_) FillRect(sx - 10, 92, 16 * (int)scale.size() + 20, 26, acc, 4);
    DrawText(sx, 97, scale.c_str(), focus == scaleField_ ? bg : fg, 1);
    snprintf(b, sizeof(b), "transp %+d", project_->FindVariable(VAR_TRANSPOSE)->GetInt());
    if (focus == transpField_) FillRect(338, 150, 8 * (int)strlen(b) + 12, 18, acc, 3);
    DrawText(344, 153, b, focus == transpField_ ? bg : dim, 0);
    snprintf(b, sizeof(b), "fold %s", project_->FindVariable(VAR_FOLD)->GetInt() ? "si" : "no");
    if (focus == foldField_) FillRect(sx - 6, 150, 8 * (int)strlen(b) + 12, 18, acc, 3);
    DrawText(sx, 153, b, focus == foldField_ ? bg : dim, 0);
    if (fS) DrawText(344, 176, "<> v elegir   A+<> cambiar", dim, 0);

    // fichas
    static const char *chipName[7] = {"VOLUMEN", "DRIVE", "TIPO", "NIVEL", "MIDI", "LINK", "DESFASE"};
    for (int k = 0; k < 7; k++) {
        int x = 24 + (k % 4) * 150, y = 212 + (k / 4) * 74;
        int cw = 142;
        bool f = focus == chipField_[k];
        FillRect(x, y, cw, 66, card, 6);
        if (f) FrameRect(x, y, cw, 66, acc, 6, 3);
        DrawText(x + 12, y + 9, chipName[k], acc, 0);
        Variable *v = &((UIIntVarField *)chipField_[k])->GetVariable();
        std::string val;
        if (v->GetType() == Variable::INT) { snprintf(b, sizeof(b), "%d", v->GetInt()); val = b; }
        else val = v->GetString() ? v->GetString() : "--";
        if (val == "off") val = "no";
        if (val == "on") val = "si";
        if (val == "on+play") val = "si+play";   // (LINK: el modo; arriba a la derecha se ve si conectó)
        if (k == 6) {   // desfase en ms; + si Chancho suena atrasado
            snprintf(b, sizeof(b), "%+d ms", v->GetInt());
            val = b;
            if (f) DrawText(x + 90, y + 13, "+ atrasa", dim, 0);
        }
        if (val.empty() || val == "(null)") val = "--";
        if (val.size() > 7u) val = val.substr(0, 7);
        DrawText(x + 12, y + 33, val.c_str(), fg, 1);
    }

    // botones
    int x = 24;
    for (int k = 0; k < 6; k++) {
        int w = 8 * (int)strlen(btnName_[k]) + 26;
        bool f = focus == btnField_[k];
        bool webOn = false;
#ifdef WEBSERVER
        webOn = k == 4 && WebServer::GetInstance()->IsRunning();
#endif
        if (f) FillRect(x, 380, w, 40, acc, 6);
        else if (webOn) FillRect(x, 380, w, 40, Mix(bg, acc, 35), 6);   // prendido
        else FrameRect(x, 380, w, 40, rule, 6, 2);
        DrawText(x + 13, 394, btnName_[k], f ? bg : (k == 5 ? dim : fg), 0);
        x += w + 7;
    }

    snprintf(b, sizeof(b), "Chancho 6 - %s.%s.%s", PROJECT_NUMBER, PROJECT_RELEASE, BUILD_COUNT);
    DrawText(24, 452, b, Mix(bg, fg, 30), 0);
#ifdef WEBSERVER
    // chancho.local: dónde abrirlo desde la compu o el celular
    std::string addr = WebServer::GetInstance()->Address();
    if (!addr.empty()) {
        DrawText(616 - 8 * (int)addr.size(), 452, addr.c_str(), acc, 0);
    }
#endif
}

// dígitos de tablero (siete segmentos redondeados), nítidos a cualquier alto
void ProjectView::drawBigNumber(int x, int y, const char *txt, int h, GUIColor c) {
    static const unsigned char seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    int t = h / 8;                 // grosor
    int w = h * 6 / 10;            // ancho de un dígito
    int r = t / 2;
    int mid = (h - t) / 2;
    for (const char *p = txt; *p; p++) {
        if (*p < '0' || *p > '9') { x += w / 2; continue; }
        unsigned char m = seg[*p - '0'];
        int hl = w - 2 * t + 2;    // largo de las horizontales
        int vl = mid - t + 2;      // largo de las verticales
        if (m & 0x01) FillRect(x + t - 1, y, hl, t, c, r);                 // a
        if (m & 0x02) FillRect(x + w - t, y + t - 1, t, vl, c, r);         // b
        if (m & 0x04) FillRect(x + w - t, y + mid + t - 1, t, vl, c, r);   // c
        if (m & 0x08) FillRect(x + t - 1, y + h - t, hl, t, c, r);         // d
        if (m & 0x10) FillRect(x, y + mid + t - 1, t, vl, c, r);           // e
        if (m & 0x20) FillRect(x, y + t - 1, t, vl, c, r);                 // f
        if (m & 0x40) FillRect(x + t - 1, y + mid, hl, t, c, r);           // g
        x += w + t + 2;
    }
}
