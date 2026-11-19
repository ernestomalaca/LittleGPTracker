#include "ProjectView.h"
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

	// trk2als: dos columnas de tarjetas (ver DrawView). Los renglones de las
	// dos columnas coinciden para que ↑↓ se quede en su columna.
	const int L = 2, R = 23;
	const char *F;
	style_ = 1;
	if (const char *ps = Config::GetInstance()->GetValue("PROJECTSTYLE")) style_ = atoi(ps);
	if (style_ < 1 || style_ > 4) style_ = 1;
	labelCount_ = 0;
	GUIPoint position(L, 4);

	Variable *v=project_->FindVariable(VAR_TEMPO) ;
    F = fmt(position._x, position._y, "Tempo", "%d   ", position._x);
    UITempoField *f = new UITempoField(ACTION_TEMPO_CHANGED, position, *v,
                                       F, 60, 400, 1, 10);
    T_SimpleList<UIField>::Insert(f) ;
	f->AddObserver(*this) ;
	tempoField_=f ;

    v = project_->FindVariable(VAR_MASTERVOL);
    position = GUIPoint(L, 5);
    F = fmt(position._x, position._y, "Volumen", "%d", position._x);
    UIIntVarField *field = new UIIntVarField(position, *v, F, 10, 100, 1, 10);
    T_SimpleList<UIField>::Insert(field);

    v = project_->FindVariable(VAR_METRONOME);
    position = GUIPoint(L, 6);
    F = fmt(position._x, position._y, "Metronomo", "%s", position._x);
    field = new UIIntVarField(position, *v, F, 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(field);

    v = project_->FindVariable(VAR_PREGAIN);
    position = GUIPoint(L, 10);
    F = fmt(position._x, position._y, "Drive", "%d", position._x);
    field = new UIIntVarField(position, *v, F, 10, 200, 1, 10);
    T_SimpleList<UIField>::Insert(field);

    position = GUIPoint(L, 11);
    v = project_->FindVariable(VAR_SOFTCLIP);
    F = fmt(position._x, position._y, "Tipo", "%s", position._x);
    field = new UIIntVarField(position, *v, F, 0, 4, 1, 4);
    T_SimpleList<UIField>::Insert(field);

    v = project_->FindVariable(VAR_SOFTCLIP_GAIN);
    position = GUIPoint(L + 13, 11);
    if (style_ == 4) {   // en su renglón, con su etiqueta
        position = GUIPoint(L, 13);
        F = fmt(position._x, position._y, "Nivel", "%s", position._x);
    }
    field = new UIIntVarField(position, *v, style_ == 4 ? F : "%s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(field);

    position = GUIPoint(L, 12);
    v = project_->FindVariable(VAR_RENDER);
    NAssert(v);
    F = fmt(position._x, position._y, "Render", "%s", position._x);
    field = new UIIntVarField(position, *v, F, 0,
                              project_->MAX_RENDER_MODE - 1, 1, 2);
    T_SimpleList<UIField>::Insert(field);

    v = project_->FindVariable(VAR_TRANSPOSE);
    position = GUIPoint(L, 16);
    F = fmt(position._x, position._y, "Transponer", "%3.2d", position._x);
    UIIntVarField *f2=new UIIntVarField(position,*v,F,-48,48,0x1,0xC) ;
	T_SimpleList<UIField>::Insert(f2) ;

    v = project_->FindVariable(VAR_SCALE);
	if (v->GetInt() < 0) {
		v->SetInt(0);
    }
    position = GUIPoint(L, 17);
    F = fmt(position._x, position._y, "Escala", "%s", position._x);
    field =
        new UIIntVarField(position, *v, F, 0, scaleCount - 1, 1, 10);
    T_SimpleList<UIField>::Insert(field);

    v = project_->FindVariable(VAR_SCALEROOT);
    if (v->GetInt() < 0) v->SetInt(0);
    position = GUIPoint(L, 18);
    F = fmt(position._x, position._y, "Raiz", "%s", position._x);
    field = new UIIntVarField(position, *v, F, 0, 11, 1, 12);
    T_SimpleList<UIField>::Insert(field);

    // el piano roll solo con las notas de la escala (Fold de Live)
    v = project_->FindVariable(VAR_FOLD);
    position = GUIPoint(L, 19);
    F = fmt(position._x, position._y, "Fold", "%s", position._x);
    field = new UIIntVarField(position, *v, F, 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(field);

    // derecha: archivo
    position = GUIPoint(R, 4);
    UIActionField *a1 = new UIActionField("Cargar", ACTION_LOAD, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

    position = GUIPoint(R, 5);
    a1 = new UIActionField("Guardar", ACTION_SAVE, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

    position = GUIPoint(R, 6);
    a1 = new UIActionField("Guardar como", ACTION_SAVE_AS, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

    position = GUIPoint(R, 7);
    a1 = new UIActionField("Exportar a Live", ACTION_EXPORT_LIVE, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

    // limpieza
    position = GUIPoint(R, 10);
    a1 = new UIActionField("Compactar clips", ACTION_PURGE, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

    position = GUIPoint(R, 11);
    a1 = new UIActionField("Compactar inst.", ACTION_PURGE_INSTRUMENT, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

    // sistema
    v = project_->FindVariable(VAR_MIDIDEVICE);
    NAssert(v);
    position = GUIPoint(R, 16);
    F = fmt(position._x, position._y, "MIDI", "%s", position._x);
    field = new UIIntVarField(position, *v, F, 0,
                              MidiService::GetInstance()->Size(), 1, 1);
    T_SimpleList<UIField>::Insert(field);

    position = GUIPoint(R, 17);
    a1 = new UIActionField("Salir", ACTION_QUIT, position);
    a1->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a1);

}

// con el estilo 4 la etiqueta va aparte (tenue) y el campo es solo el valor,
// alineado en su columna
const char *ProjectView::fmt(int col, int row, const char *label, const char *value, long &x) {
    static char bufs[16][32];
    if (style_ != 4) {
        char *b = bufs[labelCount_ < 16 ? labelCount_ : 15];
        snprintf(b, 32, "%s: %s", label, value);
        labelCount_++;
        return b;
    }
    if (labelCount_ < 16) {
        labels_[labelCount_].col = col;
        labels_[labelCount_].row = row;
        labels_[labelCount_].text = label;
        labelCount_++;
    }
    x = col + (col < 20 ? 11 : 6);
    return value;
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

    // trk2als: la barra de arriba y las tarjetas (debajo del texto de los campos)
    bool shapes = drawTopBar(-1, "PROYECTO");
    if (shapes) {
        GUIColor bg = ColorOf(CD_BACKGROUND);
        GUIColor fg = ColorOf(CD_NORMAL);
        GUIColor hi = ColorOf(CD_HILITE2);
        GUIColor card = Mix(bg, fg, 7);
        GUIColor dim = Mix(bg, fg, 50);
        struct Card { int col, row0, row1, w; const char *name; };
        const Card cards[] = {
            {2, 3, 6, 19, "CANCION"},  {23, 3, 7, 16, "ARCHIVO"},
            {2, 9, style_ == 4 ? 13 : 12, 19, "SALIDA"},  {23, 9, 11, 16, "LIMPIEZA"},
            {2, 15, 19, 19, "ESCALA"}, {23, 15, 17, 16, "SISTEMA"},
        };
        for (size_t k = 0; k < sizeof(cards) / sizeof(cards[0]); k++) {
            const Card &c = cards[k];
            int x = c.col * 16 - 10, y = c.row0 * 16 - 4;
            int w = c.w * 16 + 18, h = (c.row1 - c.row0 + 1) * 16 + 12;
            switch (style_) {
            case 1:   // tarjetas
                FillRect(x, y, w, h, card, 6);
                FillRect(x + 10, y + 16, w - 20, 1, Mix(bg, fg, 16));
                DrawText(c.col * 16, c.row0 * 16 + 1, c.name, hi, 0);
                break;
            case 2:   // separadores: el título y una línea que sigue
                DrawText(c.col * 16, c.row0 * 16 + 1, c.name, hi, 0);
                FillRect(c.col * 16 + 8 * (int)strlen(c.name) + 8, c.row0 * 16 + 7,
                         w - 18 - 8 * (int)strlen(c.name) - 8, 1, Mix(bg, fg, 22));
                break;
            case 3:   // riel: una barrita de color a la izquierda
                FillRect(x, y + 2, 3, h - 4, hi, 1);
                DrawText(c.col * 16, c.row0 * 16 + 1, c.name, hi, 0);
                break;
            case 4:   // etiqueta / valor: separadores y renglones con guía
                DrawText(c.col * 16, c.row0 * 16 + 1, c.name, hi, 0);
                FillRect(c.col * 16, c.row0 * 16 + 15, w - 18, 1, Mix(bg, fg, 22));
                break;
            }
        }
        if (style_ == 4)
            for (int k = 0; k < labelCount_ && k < 16; k++)
                DrawText(labels_[k].col * 16, labels_[k].row * 16 + 2, labels_[k].text, dim, 1);
        // abajo: la versión, chiquita
        char b[64];
        snprintf(b, sizeof(b), "Chancho 6 - build %s.%s.%s", PROJECT_NUMBER, PROJECT_RELEASE, BUILD_COUNT);
        DrawText(22, 452, b, dim, 0);
    } else {
        GUITextProperties props ;
        GUIPoint pos=GetTitlePosition() ;
        char projectString[80] ;
        sprintf(projectString, "Project (Build %s.%s.%s)", PROJECT_NUMBER,
                PROJECT_RELEASE, BUILD_COUNT);
        SetColor(CD_NORMAL);
        DrawString(pos._x,pos._y,projectString,props) ;
    }

    FieldView::Redraw();
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
		focus->ClearFocus() ;
		focus->Draw(w_) ;
		w_.Flush() ;
		focus->SetFocus() ;
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
