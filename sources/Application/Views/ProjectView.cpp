#include "ProjectView.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Utils/ProjectName.h"
#include "Application/Utils/Latin1.h"
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
#include <unistd.h>
#include "System/Console/Trace.h"
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
#define ACTION_COLLECT          MAKE_FOURCC('R','C','P','L')   // trk2als: recopilar y guardar

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

        // trk2als: la lista de samples por referencia va con el proyecto
        Path refSrc(path_srcprjdir.GetPath() + "samples.ref");
        if (refSrc.Exists())
            FSS.Copy(refSrc, Path(path_dstprjdir.GetPath() + "/samples.ref"));

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
    if (dialog.GetReturnCode()==MBL_YES) {
#if defined(__linux__)
        // trk2als: seguro: si algo se traba al salir, en 5 s el sistema cierra Chancho
        Trace::Log("SALIR", "confirmado");
        fflush(stdout);
        fsync(1);
        alarm(5);
#endif
    }
    MixerService::GetInstance()->SetRenderMode(0);
    if (dialog.GetReturnCode()==MBL_YES) {
		((ProjectView &)v).OnQuit() ;
	}
} ;

static void PurgeCallback(View &v,ModalView &dialog) {
	((ProjectView &)v).OnPurgeInstruments(dialog.GetReturnCode()==MBL_YES) ;
} ;

ProjectView::ProjectView(GUIWindow &w,ViewData *data):FieldView(w,data) {
    msgAt_ = 0;

    lastClock_ = 0;
    lastTick_ = 0;

	project_=data->project_ ;

	// trk2als: el Proyecto como tablero (ver drawBoard). Los campos siguen
	// haciendo el trabajo (editar, acciones); sus posiciones son lógicas,
	// para que las flechas se muevan por el tablero.
	Variable *v=project_->FindVariable(VAR_TEMPO) ;
	GUIPoint position(1, 3);
    UITempoField *f = new UITempoField(ACTION_TEMPO_CHANGED, position, *v,
                                       "%d", 20, 999, 1, 10);   // trk2als: como Live (20 a 999)
    T_SimpleList<UIField>::Insert(f) ;
	f->AddObserver(*this) ;
	tempoField_=f ;

    // trk2als: "tres protagonistas": arriba tempo | escala (nota, escala) |
    // Link (modo, desfase), todo en el renglón 3 (‹› recorre los tres
    // cuadros); abajo ocho fichas y los botones
    v = project_->FindVariable(VAR_SCALEROOT);
    if (v->GetInt() < 0) v->SetInt(0);
    position = GUIPoint(12, 3);
    rootField_ = new UIIntVarField(position, *v, "%s", 0, 11, 1, 12);
    T_SimpleList<UIField>::Insert(rootField_);

    v = project_->FindVariable(VAR_SCALE);
    if (v->GetInt() < 0) v->SetInt(0);
    position = GUIPoint(16, 3);
    scaleField_ = new UIIntVarField(position, *v, "%s", 0, scaleCount - 1, 1, 10);
    T_SimpleList<UIField>::Insert(scaleField_);

    v = project_->FindVariable(VAR_LINK);
    position = GUIPoint(28, 3);
    linkField_ = new UIIntVarField(position, *v, "%s", 0, 2, 1, 1);
    T_SimpleList<UIField>::Insert(linkField_);

    v = project_->FindVariable(VAR_LINKOFS);
    position = GUIPoint(32, 3);
    ofsField_ = new UIIntVarField(position, *v, "%d", -250, 250, 5, 25);
    T_SimpleList<UIField>::Insert(ofsField_);

    // las fichas
    struct Chip { FourCC var; int x, y, min, max, xo, yo; };
    const Chip chips[9] = {
        {VAR_MASTERVOL, 1, 6, 10, 100, 1, 10},  {VAR_PREGAIN, 10, 6, 10, 200, 1, 10},
        {VAR_QUANTIZE, 19, 6, 0, 100, 5, 25},   {VAR_SWING, 28, 6, 50, 75, 1, 5},   // trk2als (el softclip va al mixer)
        {VAR_MIDIDEVICE, 1, 7, 0, 0, 1, 1},     {VAR_METRONOME, 10, 7, 0, 1, 1, 1},
        {VAR_TRANSPOSE, 19, 7, -48, 48, 1, 12}, {VAR_FOLD, 28, 7, 0, 1, 1, 1},
        {VAR_FOLLOW, 1, 8, 0, 1, 1, 1},   // trk2als: la tercera fila (aquí van entrando los ajustes nuevos)
    };
    // trk2als: la ficha 4 (era la salida MIDI de LGPT, que aquí no hay) es
    // la latencia: baja 256/2, media 512/2, alta 512/4 (bloque / reserva)
    {
        static char *latNames[3] = {(char *)"baja", (char *)"media", (char *)"alta"};
        Config *cfg = Config::GetInstance();
        const char *bs = cfg->GetValue("AUDIOBUFFERSIZE"), *pb = cfg->GetValue("AUDIOPREBUFFERCOUNT");
        int b = bs ? atoi(bs) : 512, p = pb ? atoi(pb) : 4;
        latLast_ = b <= 256 ? 0 : (p <= 2 ? 1 : 2);
        latVar_ = new Variable("latencia", 0, latNames, 3, latLast_);
    }
    for (int k = 0; k < CHIPS; k++) {
        v = (k == 4) ? latVar_ : project_->FindVariable(chips[k].var);
        NAssert(v);
        int mx = k == 4 ? 2 : chips[k].max;
        position = GUIPoint(chips[k].x, chips[k].y);
        UIIntVarField *cf = new UIIntVarField(position, *v, "%s", chips[k].min, mx, chips[k].xo, chips[k].yo);
        chipField_[k] = cf;
        T_SimpleList<UIField>::Insert(cf);
    }

    // la barra de botones
    struct Btn { const char *name; FourCC action; int x; };
    const Btn btns[7] = {{"CARGAR", ACTION_LOAD, 1}, {"GUARDAR", ACTION_SAVE, 8},
                         {"GUARDAR COMO", ACTION_SAVE_AS, 16}, {"RECOPILAR", ACTION_COLLECT, 23},
                         {"LIMPIAR", ACTION_PURGE_INSTRUMENT, 27},
                         {"WEB", ACTION_WEB, 31}, {"SALIR", ACTION_QUIT, 34}};
    for (int k = 0; k < 7; k++) {
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

    // trk2als: la latencia cambió: a config.xml (al reiniciar toma efecto)
    if (latVar_ && latVar_->GetInt() != latLast_) {
        latLast_ = latVar_->GetInt();
        static const char *bsz[3] = {"256", "512", "512"}, *pre[3] = {"2", "2", "4"};   // baja: 2 de reserva (con 1, clicks sueltos)
        Config *cfg = Config::GetInstance();
        bool ok = cfg->SetPersistent("AUDIOBUFFERSIZE", bsz[latLast_]) &&
                  cfg->SetPersistent("AUDIOPREBUFFERCOUNT", pre[latLast_]);
        notify(ok ? "Latencia guardada: reinicia Chancho" : "No se pudo guardar la latencia");
    }

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
				notify("Rendering started!");
			} else if (viewData_->isRendering_ && player->IsRunning()) {
				viewData_->isRendering_ = false;
				notify("Rendering done!");
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
            notify("Rendering on, press start");
        } else if (currentMode == 0 && viewData_->renderMode_ > 0) {
            notify("Rendering off");
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
            MessageBox *mb = new MessageBox(
                *this, "¿Limpiar los samples?\nSe borran los que\nno tienen instrumento",
                MBBF_YES | MBBF_NO);
            mb->Label(MBL_YES, "Limpiar");
            DoModal(mb,PurgeCallback) ;
			break ;
		}
        case ACTION_COLLECT: {   // trk2als: copiar al proyecto lo que usa de fuera, y guardar
            SamplePool *pool = SamplePool::GetInstance();
            int refs = pool->ReferenceCount();
            if (refs == 0) {
                notify("Todo está en el proyecto");
                break;
            }
            MixerService *ms = MixerService::GetInstance();
            int held = ms->ReleaseAll();   // copiar tarda: el audio sigue
            int n = pool->Collect();
            ms->Reacquire(held);
            MixerService::GetInstance()->SetRenderMode(0);
            PersistencyService::GetInstance()->Save();
            char m[64];
            if (n == refs) snprintf(m, sizeof(m), "Recopilado y guardado: %d samples", n);
            else snprintf(m, sizeof(m), "Recopilados %d de %d (ver log)", n, refs);
            notify(m);
            break;
        }
        case ACTION_SAVE: {
            MixerService::GetInstance()->SetRenderMode(0);
            PersistencyService *service = PersistencyService::GetInstance();
            service->Save();
            notify("Proyecto guardado");   // trk2als: que se note
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
                notify("Set TRK2ALS_DIR in config.xml");
                break;
            }
            MixerService::GetInstance()->SetRenderMode(0);
            PersistencyService::GetInstance()->Save();

            std::string prj = Path("project:").GetPath();
            while (!prj.empty() && prj[prj.size() - 1] == '/')
                prj.erase(prj.size() - 1);
            std::string name = prj.substr(prj.find_last_of('/') + 1);
            name = ProjectNiceName(name);
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
            notify(rc == 0 ? "Live set exported!"
                                          : "Export failed, see export.log");
            break;
        }
        case ACTION_LOAD: {
            MessageBox *mb = new MessageBox(*this, "", MBBF_YES | MBBF_NO);
            mb->Title("¿Cargar nuevo proyecto\ny perder los cambios?")
                .Label(MBL_YES, "Ok").Label(MBL_NO, "Volver");
            DoModal(mb, LoadCallback);
            break;
        }
        case ACTION_QUIT: {
            // trk2als: con sabor de aquí
            MessageBox *mb = new MessageBox(
                *this, "", MBBF_YES | MBBF_NO);
            mb->Title("¿Salir y perder la fe?").Label(MBL_YES, "Salir").Label(MBL_NO, "Me quedo");
            DoModal(mb, QuitCallback);
            break;
        }
        case ACTION_TEMPO_CHANGED:
			break ;
#ifdef WEBSERVER
        case ACTION_WEB: {   // trk2als: prende / apaga chancho.local
            WebServer *web = WebServer::GetInstance();
            if (!web->Allowed()) {
                notify("WEB apagado en config.xml");
                break;
            }
            web->Toggle();
            notify(web->IsRunning() ? "chancho.local prendido" : "chancho.local apagado");
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

    // los tres protagonistas
    // (la escala, más ancha: sus nombres son largos)
    const int TY = 46, TH = 120, TW = 150, EW = 264;
    // tempo
    bool fT = focus == tempoField_;
    FillRect(24, TY, TW, TH, card, 8);
    if (fT) FrameRect(24, TY, TW, TH, acc, 8, 3);
    DrawText(36, TY + 12, "TEMPO", acc, 0);
    snprintf(b, sizeof(b), "%d", project_->GetTempo());
    DrawText(36, TY + 46, b, fg, 2);
    DrawText(36 + 24 * (int)strlen(b) + 8, TY + 62, "bpm", dim, 0);
    if (fT) DrawText(36, TY + 98, "A+<> 1  A+^v 10", dim, 0);

    // escala: nota y escala (‹›)
    bool fS = focus == rootField_ || focus == scaleField_;
    int ex = 24 + TW + 14;
    FillRect(ex, TY, EW, TH, card, 8);
    if (fS) FrameRect(ex, TY, EW, TH, acc, 8, 3);
    DrawText(ex + 12, TY + 12, "ESCALA", acc, 0);
    std::string root = project_->FindVariable(VAR_SCALEROOT)->GetString();
    std::string scale = project_->FindVariable(VAR_SCALE)->GetString();
    int rw = 24 * (int)root.size();
    if (focus == rootField_) FillRect(ex + 6, TY + 40, rw + 12, 44, acc, 4);
    DrawText(ex + 12, TY + 46, root.c_str(), focus == rootField_ ? bg : fg, 2);
    int sx = ex + 12 + rw + 12;
    // el nombre al lado de la nota; si no cabe, en dos renglones (y si aún
    // así no cabe, se corta con ..)
    int room = (ex + EW - 12 - sx) / 16;
    std::string l1 = scale, l2;
    if ((int)scale.size() > room) {
        size_t sp = scale.rfind(' ', room);
        if (sp == std::string::npos) sp = scale.find(' ');
        if (sp != std::string::npos) { l1 = scale.substr(0, sp); l2 = scale.substr(sp + 1); }
        if ((int)l1.size() > room) l1 = l1.substr(0, room - 2) + "..";
        if ((int)l2.size() > room) l2 = l2.substr(0, room - 2) + "..";
    }
    int lw = 16 * (int)(l1.size() > l2.size() ? l1.size() : l2.size());
    bool fSc = focus == scaleField_;
    if (l2.empty()) {
        if (fSc) FillRect(sx - 6, TY + 52, lw + 12, 26, acc, 4);
        DrawText(sx, TY + 57, l1.c_str(), fSc ? bg : fg, 1);
    } else {
        if (fSc) FillRect(sx - 6, TY + 42, lw + 12, 44, acc, 4);
        DrawText(sx, TY + 46, l1.c_str(), fSc ? bg : fg, 1);
        DrawText(sx, TY + 66, l2.c_str(), fSc ? bg : fg, 1);
    }
    if (fS) DrawText(ex + 12, TY + 98, "<> elige  A+<> cambia", dim, 0);

    // Link: el modo y el desfase (‹›)
    bool fL = focus == linkField_, fO = focus == ofsField_;
    int lx = ex + EW + 14;
    FillRect(lx, TY, TW, TH, card, 8);
    if (fL || fO) FrameRect(lx, TY, TW, TH, acc, 8, 3);
    DrawText(lx + 12, TY + 12, "ABLETON LINK", acc, 0);
    int lm = project_->FindVariable(VAR_LINK)->GetInt();
    const char *lmode = lm == 0 ? "no" : lm == 1 ? "sí" : "sí+play";
    if (fL) FillRect(lx + 6, TY + 40, (lm == 2 ? 16 : 24) * TextLen(lmode) + 12, 44, acc, 4);
    DrawText(lx + 12, TY + 46, lmode, fL ? bg : fg, lm == 2 ? 1 : 2);
    snprintf(b, sizeof(b), "desfase %+d ms", project_->FindVariable(VAR_LINKOFS)->GetInt());
    if (fO) FillRect(lx + 6, TY + 92, 8 * (int)strlen(b) + 12, 18, acc, 3);
    DrawText(lx + 12, TY + 95, b, fO ? bg : dim, 0);
    if (fO) DrawText(lx + 12, TY + 76, "+ si va atrasado", dim, 0);

    // fichas
    static const char *chipName[CHIPS] = {"VOLUMEN", "DRIVE", "CUANTIZAR", "SWING", "LATENCIA", "METRÓNOMO", "TRANSP", "FOLD", "FOLLOW"};
    for (int k = 0; k < CHIPS; k++) {
        // trk2als: tres filas (más bajitas) para que quepan los ajustes nuevos
        int x = 24 + (k % 4) * 150, y = 176 + (k / 4) * 58;
        int cw = 142, ch = 52;
        bool f = focus == chipField_[k];
        FillRect(x, y, cw, ch, card, 6);
        if (f) FrameRect(x, y, cw, ch, acc, 6, 3);
        DrawText(x + 12, y + 6, chipName[k], acc, 0);
        Variable *v = &((UIIntVarField *)chipField_[k])->GetVariable();
        std::string val;
        if (k == 6) { snprintf(b, sizeof(b), "%+d", v->GetInt()); val = b; }   // transponer
        else if (k == 3 && v->GetInt() <= 50) val = "no";   // swing apagado (parejo)
        else if (k == 2 || k == 3) { snprintf(b, sizeof(b), "%d%%", v->GetInt()); val = b; }   // cuantizar, swing
        else if (k == 7 || k == 8) val = v->GetInt() ? "sí" : "no";              // fold, follow
        else if (v->GetType() == Variable::INT) { snprintf(b, sizeof(b), "%d", v->GetInt()); val = b; }
        else val = v->GetString() ? v->GetString() : "--";
        if (val == "off") val = "no";
        if (val == "on") val = "sí";
        if (val.empty() || val == "(null)") val = "--";
        if (val.size() > 7u) val = val.substr(0, 7);
        DrawText(x + 12, y + 24, val.c_str(), fg, 1);
    }

    // botones
    // repartidos de orilla a orilla
    int bw[7], total = 0;
    for (int k = 0; k < 7; k++) { bw[k] = 8 * (int)strlen(btnName_[k]) + 22; total += bw[k]; }
    int gap = (592 - total) / 6;
    int x = 24;
    for (int k = 0; k < 7; k++) {
        int w = bw[k];
        bool f = focus == btnField_[k];
        bool webOn = false;
#ifdef WEBSERVER
        webOn = k == 5 && WebServer::GetInstance()->IsRunning();
#endif
        if (f) FillRect(x, 356, w, 42, acc, 6);
        else if (webOn) FillRect(x, 356, w, 42, Mix(bg, acc, 35), 6);   // prendido
        else FrameRect(x, 356, w, 42, rule, 6, 2);
        DrawText(x + 11, 371, btnName_[k], f ? bg : (k == 6 ? dim : fg), 0);
        x += w + gap;
    }

    // el aviso de lo que acaba de pasar (guardar, recopilar...), en el espacio de abajo
    if (!msg_.empty()) {   // letras blancas, como los avisos de la Session
        std::string m = msg_;
        if (m.size() > 36) m = m.substr(0, 36);
        DrawText(28, 414, m.c_str(), fg, 1);
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

// trk2als: aviso abajo (con figuras); sin figuras, el de siempre arriba
void ProjectView::notify(const char *msg) {
    // trk2als: abajo, como en todas las pantallas
    View::SetNotification(msg);
    return;
    if (!SupportsShapes()) {
        View::SetNotification(msg);
        return;
    }
    msg_ = msg ? msg : "";
    msgAt_ = SDL_GetTicks();
    isDirty_ = true;
#ifdef SDL2
    // para que se borre solo aunque no se toque nada
    struct W { static Uint32 wake(Uint32, void *) {
        SDL_Event e; memset(&e, 0, sizeof(e));
        e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_EXPOSED;
        SDL_PushEvent(&e); return 0; } };
    SDL_AddTimer(2100, W::wake, 0);
#endif
}

bool ProjectView::Tick() {
    // trk2als: si el tempo cambió desde afuera (Link), el número grande también
    static int shownTempo = -1;
    int t = project_ ? project_->GetTempo() : -1;
    if (t != shownTempo) {
        shownTempo = t;
        return true;
    }
    if (!msg_.empty() && SDL_GetTicks() - msgAt_ >= 2000) {
        msg_.clear();
        return true;
    }
    return false;
}
