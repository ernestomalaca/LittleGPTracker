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
    position = GUIPoint(30, 3);
    transpField_ = new UIIntVarField(position, *v, "%d", -48, 48, 0x1, 0xC);
    T_SimpleList<UIField>::Insert(transpField_);

    v = project_->FindVariable(VAR_FOLD);
    position = GUIPoint(34, 3);
    foldField_ = new UIIntVarField(position, *v, "%s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(foldField_);

    // las fichas
    struct Chip { FourCC var; int x, y, min, max, xo, yo; };
    const Chip chips[7] = {
        {VAR_MASTERVOL, 1, 6, 10, 100, 1, 10}, {VAR_METRONOME, 10, 6, 0, 1, 1, 1},
        {VAR_PREGAIN, 19, 6, 10, 200, 1, 10},  {VAR_SOFTCLIP, 28, 6, 0, 4, 1, 4},
        {VAR_SOFTCLIP_GAIN, 1, 7, 0, 1, 1, 1}, {VAR_MIDIDEVICE, 10, 7, 0, 0, 1, 1},
        {VAR_LINK, 19, 7, 0, 2, 1, 1},
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
    const Btn btns[5] = {{"CARGAR", ACTION_LOAD, 1}, {"GUARDAR", ACTION_SAVE, 8},
                         {"GUARDAR COMO", ACTION_SAVE_AS, 16}, {"LIMPIAR", ACTION_PURGE_INSTRUMENT, 27},
                         {"SALIR", ACTION_QUIT, 34}};
    for (int k = 0; k < 5; k++) {
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

    // tempo
    bool fT = focus == tempoField_;
    FillRect(24, 46, 288, 150, card, 8);
    if (fT) FrameRect(24, 46, 288, 150, acc, 8, 3);
    DrawText(40, 60, "TEMPO", acc, 0);
    snprintf(b, sizeof(b), "%d", project_->GetTempo());
    DrawText(40, 84, b, fg, 4);
    DrawText(40, 172, fT ? "A+<> 1   A+^v 10   B tap" : "bpm", dim, 0);

    // escala
    bool fS = focus == rootField_ || focus == scaleField_ || focus == transpField_ ||
              focus == foldField_;
    FillRect(328, 46, 288, 150, card, 8);
    if (fS) FrameRect(328, 46, 288, 150, acc, 8, 3);
    DrawText(344, 60, "ESCALA", acc, 0);
    std::string root = project_->FindVariable(VAR_SCALEROOT)->GetString();
    std::string scale = project_->FindVariable(VAR_SCALE)->GetString();
    if (scale.size() > 14) scale = scale.substr(0, 14);
    if (focus == rootField_) FillRect(338, 80, 32 * (int)root.size() + 20, 44, acc, 4);
    DrawText(348, 86, root.c_str(), focus == rootField_ ? bg : fg, 2);
    if (focus == scaleField_) FillRect(338, 132, 16 * (int)scale.size() + 20, 24, acc, 4);
    DrawText(348, 136, scale.c_str(), focus == scaleField_ ? bg : fg, 1);
    snprintf(b, sizeof(b), "transp %+d", project_->FindVariable(VAR_TRANSPOSE)->GetInt());
    if (focus == transpField_) FillRect(476, 82, 8 * (int)strlen(b) + 12, 18, acc, 3);
    DrawText(482, 85, b, focus == transpField_ ? bg : dim, 0);
    snprintf(b, sizeof(b), "fold   %s", project_->FindVariable(VAR_FOLD)->GetInt() ? "si" : "no");
    if (focus == foldField_) FillRect(476, 106, 8 * (int)strlen(b) + 12, 18, acc, 3);
    DrawText(482, 109, b, focus == foldField_ ? bg : dim, 0);
    if (fS) DrawText(344, 172, "<> elegir   A+<> cambiar", dim, 0);

    // fichas
    static const char *chipName[7] = {"VOLUMEN", "METRONOMO", "DRIVE", "TIPO", "NIVEL", "MIDI", "ABLETON LINK"};
    for (int k = 0; k < 7; k++) {
        int x = 24 + (k % 4) * 150, y = 212 + (k / 4) * 74;
        int cw = k == 6 ? 292 : 142;   // Link: ficha doble
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
        if (val == "on+play") val = "si + play";
        if (k == 6 && v->GetInt() > 0) {   // Link: con cuántos está
            int n = Player::GetInstance()->LinkPeers();
            if (n == 0) snprintf(b, sizeof(b), "  buscando");
            else snprintf(b, sizeof(b), "  %d %s", n, n == 1 ? "app" : "apps");
            val += b;
        }
        if (val.empty() || val == "(null)") val = "--";
        if (val.size() > (k == 6 ? 16u : 7u)) val = val.substr(0, k == 6 ? 16 : 7);
        DrawText(x + 12, y + 33, val.c_str(), fg, 1);
    }

    // botones
    int x = 24;
    for (int k = 0; k < 5; k++) {
        int w = 8 * (int)strlen(btnName_[k]) + 28;
        bool f = focus == btnField_[k];
        if (f) FillRect(x, 380, w, 40, acc, 6);
        else FrameRect(x, 380, w, 40, rule, 6, 2);
        DrawText(x + 14, 394, btnName_[k], f ? bg : (k == 4 ? dim : fg), 0);
        x += w + 8;
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
