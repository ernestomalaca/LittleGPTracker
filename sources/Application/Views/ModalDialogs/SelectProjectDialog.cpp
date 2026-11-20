
#include "SelectProjectDialog.h"
#include "Application/Utils/ProjectName.h"
#include "NewProjectDialog.h"
#include "System/Console/Trace.h"
#include "Application/Views/ModalDialogs/MessageBox.h"

#include <algorithm>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>

#define LIST_SIZE 20
#define LIST_WIDTH 32

static const char *buttonText[3] = {"Abrir", "Nueva", "Salir"};

Path SelectProjectDialog::lastFolder_("root:") ;
int SelectProjectDialog::lastProject_ = 0 ;
std::string SelectProjectDialog::lastName_ ;

static bool isProject(const std::string &name) { return IsProjectFolder(name) ; }
static std::string projectTitle(const std::string &p) { return ProjectNiceName(p) ; }

// cuándo se usó por última vez: lo más nuevo entre la carpeta (se toca al
// abrirla) y el lgptsav.dat (al guardar)
static long lastUsed(const Path &dir) {
	Path d(dir) ;
	std::string c=d.GetCanonicalPath() ;
	long t=0 ;
	struct stat st ;
	if (stat(c.c_str(),&st)==0) t=(long)st.st_mtime ;
	std::string sav=c+"/lgptsav.dat" ;
	if (stat(sav.c_str(),&st)==0 && (long)st.st_mtime>t) t=(long)st.st_mtime ;
	return t ;
}

// "hace 3 horas", "ayer", "hace 2 semanas"...
static std::string ago(long t) {
	if (t<=0) return "" ;
	long d=(long)time(0)-t ;
	char b[48] ;
	if (d<0) d=0 ;
	if (d<90) return "hace un momento" ;
	if (d<3600) { snprintf(b,sizeof(b),"hace %ld min",d/60) ; return b ; }
	if (d<86400) { long h=d/3600 ; snprintf(b,sizeof(b),h==1?"hace 1 hora":"hace %ld horas",h) ; return b ; }
	long days=d/86400 ;
	if (days==1) return "ayer" ;
	if (days<14) { snprintf(b,sizeof(b),"hace %ld días",days) ; return b ; }
	if (days<60) { snprintf(b,sizeof(b),"hace %ld semanas",days/7) ; return b ; }
	snprintf(b,sizeof(b),"hace %ld meses",days/30) ;
	return b ;
}

struct Entry { Path *p; long t; int kind; std::string name; };

// un color por proyecto, siempre el mismo (del nombre)
static GUIColor hueColor(unsigned h,int sat,int val) {
	// h en 0..359, sat y val en 0..100
	int c=val*sat/100 ;
	int x=c*(60-abs((int)(h%120)-60))/60 ;
	int m=val-c ;
	int r=0,g=0,b=0 ;
	switch ((h/60)%6) {
		case 0: r=c;g=x;break ; case 1: r=x;g=c;break ; case 2: g=c;b=x;break ;
		case 3: g=x;b=c;break ; case 4: r=x;b=c;break ; default: r=c;b=x;break ;
	}
	return GUIColor((r+m)*255/100,(g+m)*255/100,(b+m)*255/100) ;
}
static unsigned nameHash(const std::string &s) {
	unsigned h=2166136261u ;
	for (size_t i=0;i<s.size();i++) { h^=(unsigned char)s[i] ; h*=16777619u ; }
	return h ;
}


static void ExitCallback(View &v, ModalView &dialog) {
	if (dialog.GetReturnCode() == MBL_YES) ((SelectProjectDialog &)v).Quit() ;
}

static void DeleteProjectCallback(View &v, ModalView &dialog) {
    SelectProjectDialog &spd = (SelectProjectDialog&) v;
	if (dialog.GetReturnCode() == MBL_YES) {
        Path projectPath = spd.GetCurrentProjectPath();
        Result result = spd.OnDeleteProject(projectPath);
		if (result.Failed()) {
			Trace::Error(result.GetDescription().c_str());
		}
	}
}

// Recursive helper to delete directory and all contents
static void RecursiveDeleteDirectory(const Path &dirPath) {
	FileSystem *fs = FileSystem::GetInstance();
	FileType type = fs->GetFileType(dirPath.GetPath().c_str());
	
	if (type == FT_DIR) {
		I_Dir *dir = fs->Open(dirPath.GetPath().c_str());
		if (dir) {
			dir->GetContent("*");
			
			// Collect all items first to avoid iterator invalidation
			T_SimpleList<Path> itemsToDelete(false);
			IteratorPtr<Path> it(dir->GetIterator());
			
			for (it->Begin(); !it->IsDone(); it->Next()) {
				Path itemCopy = it->CurrentItem();
				std::string name = itemCopy.GetName();
				
				// Skip . and .. entries
				if (name != "." && name != "..") {
					Path *ptrCopy = new Path(itemCopy);
					itemsToDelete.Insert(ptrCopy);
				}
			}
			
			delete dir;
			
			// Now delete all collected items
			IteratorPtr<Path> deleteIt(itemsToDelete.GetIterator());
			for (deleteIt->Begin(); !deleteIt->IsDone(); deleteIt->Next()) {
				const Path &item = deleteIt->CurrentItem();
				RecursiveDeleteDirectory(item);
			}
		}
	}

    fs->Delete(dirPath.GetPath().c_str());
}

SelectProjectDialog::SelectProjectDialog(View &view)
    : ModalView(view), content_(true) {}

SelectProjectDialog::~SelectProjectDialog() {
}

void SelectProjectDialog::DrawView() {
	if (SupportsShapes()) drawShapes() ;
	else drawGrid() ;
}

int SelectProjectDialog::pageSize() { return SupportsShapes()?6:LIST_SIZE ; }

void SelectProjectDialog::drawGrid() {

	SetWindow(LIST_WIDTH,LIST_SIZE+3) ;

	GUITextProperties props ;

	SetColor(CD_NORMAL) ;
    View::EnableNotification();

    // Draw projects

    int x = 1;
    int y = 1;

    if (currentProject_ < topIndex_) {
        topIndex_ = currentProject_;
    };
    if (currentProject_>=topIndex_+LIST_SIZE) {
		topIndex_=currentProject_-LIST_SIZE+1 ;
	} ;

	IteratorPtr<Path> it(content_.GetIterator()) ;
	int count=0 ;
	char buffer[256] ;
	for(it->Begin();!it->IsDone();it->Next()) {
		if ((count>=topIndex_)&&(count<topIndex_+LIST_SIZE)) {
			Path &current=it->CurrentItem() ;
			std::string p=current.GetName() ;

			if(isProject(p))
      {
				p=" "+projectTitle(p) ;
      }
      else
      {
				std::string t=p ;
				p="[" ;
				p+=t ;
				p+="]" ;
      };

			if (count==currentProject_) {
				SetColor(CD_HILITE2) ;
				props.invert_=true ;
			} else {
				SetColor(CD_NORMAL) ;
				props.invert_=false ;
			}
			strcpy(buffer,p.c_str()) ;
			buffer[LIST_WIDTH-1]=0 ;
			DrawString(x,y,buffer,props) ;
			y+=1 ;
		}
		count++ ;
	} ;

	y=LIST_SIZE+2 ;
	int offset=LIST_WIDTH/4 ;

	SetColor(CD_NORMAL) ;

	for (int i=0;i<3;i++) {
		const char *text=buttonText[i] ;
		x=offset*(i+1)-strlen(text)/2 ;
		props.invert_=(i==selected_)?true:false ;
		DrawString(x,y,text,props) ;
    }
};

// trk2als: la lista como los "Sets" de Ableton Note: un cuadrito de color por
// proyecto, el nombre completo y hace cuánto se usó
void SelectProjectDialog::drawShapes() {
	const int PX=8, PY=8, PW=624, PH=464 ;
	const int ROWH=56, LY=64, RX=24, RW=592 ;
	int rows=pageSize() ;
	DrawPanel(PX,PY,PW,PH) ;
	GUIColor bg=ColorOf(CD_BACKGROUND), fg=ColorOf(CD_NORMAL) ;
	GUIColor acc=ColorOf(CD_HILITE2), dim=Mix(bg,fg,50), card=Mix(bg,fg,10) ;

	int size=content_.Size() ;
	if (currentProject_<topIndex_) topIndex_=currentProject_ ;
	if (currentProject_>=topIndex_+rows) topIndex_=currentProject_-rows+1 ;
	if (topIndex_<0) topIndex_=0 ;

	DrawText(RX+8,28,"CANCIONES",acc,1) ;
	char b[64] ;
	if (size>0) {
		snprintf(b,sizeof(b),"%d de %d",currentProject_+1,size) ;
		DrawText(RX+RW-8-8*(int)strlen(b),30,b,dim,0) ;
	}

	IteratorPtr<Path> it(content_.GetIterator()) ;
	int count=0 ;
	for (it->Begin();!it->IsDone();it->Next(),count++) {
		if (count<topIndex_ || count>=topIndex_+rows) continue ;
		Path &cur=it->CurrentItem() ;
		std::string name=cur.GetName() ;
		bool proj=isProject(name) ;
		bool here=(count==currentProject_) ;
		int y=LY+(count-topIndex_)*ROWH ;
		if (here) {
			FillRect(RX,y,RW,ROWH-6,card,8) ;
			if (selected_==0) FrameRect(RX,y,RW,ROWH-6,acc,8,2) ;
		}
		// el cuadrito
		int tx=RX+8, ty=y+5, ts=40 ;
		if (proj) {
			unsigned h=nameHash(name) ;
			unsigned hue=h%360 ;
			GUIColor c1=hueColor(hue,45,85), c2=hueColor((hue+40+(h>>9)%60)%360,60,70) ;
			FillRect(tx,ty,ts,ts,c1,7) ;
			FillRect(tx+12,ty+12,ts-12,ts-12,Mix(c1,c2,55),9) ;
			FillRect(tx+20,ty+20,ts-20,ts-20,c2,8) ;
		} else {
			FrameRect(tx,ty,ts,ts,dim,7,2) ;
			DrawText(tx+(name==".."?12:12),ty+12,name==".."?"<":"/",dim,1) ;
		}
		// el nombre y cuándo
		std::string title=proj?projectTitle(name):(name==".."?"volver":name) ;
		int room=(RW-(tx-RX)-ts-16-56)/16 ;
		if ((int)title.size()>room) title=title.substr(0,room-2)+".." ;
		int nx=tx+ts+16 ;
		DrawText(nx,y+7,title.c_str(),(here||proj)?fg:dim,1) ;
		std::string sub=proj?ago(times_[count]):(name==".."?"carpeta de arriba":"carpeta") ;
		if (proj && sub.empty()) sub="sin guardar" ;
		DrawText(nx,y+29,sub.c_str(),dim,0) ;
		// el botoncito de abrir en el elegido
		if (here && selected_==0) {
			int cx=RX+RW-44, cy=y+(ROWH-6)/2-15 ;
			FillRect(cx,cy,30,30,acc,15) ;
			if (proj) {
				for (int i=0;i<10;i++) {   // un triangulito de play
					int hh=(10-i)*2-1 ;
					FillRect(cx+11+i,cy+15-hh/2,1,hh,bg) ;
				}
			} else {
				DrawText(cx+7,cy+7,name==".."?"<":">",bg,1) ;
			}
		}
	}
	if (size==0) {
		DrawText(RX+8,LY+8,"Todavía no hay proyectos",fg,1) ;
		DrawText(RX+8,LY+34,"Nueva para empezar uno",dim,0) ;
	}

	// barrita de lugar, si hay más de los que caben
	if (size>rows) {
		int bh=rows*ROWH-6, sy=LY+bh*topIndex_/size, sh=bh*rows/size ;
		if (sh<12) sh=12 ;
		FillRect(RX+RW+6,LY,3,bh,card,1) ;
		FillRect(RX+RW+6,sy,3,sh,dim,1) ;
	}

	// los botones, como en las otras ventanas
	const int BH=34, BY=PY+PH-24-BH, GAP=16 ;
	int bx=RX+RW-3*112-2*GAP ;
	for (int i=0;i<3;i++) {
		const char *t=buttonText[i] ;
		int tx2=bx+(112-16*(int)strlen(t))/2 ;
		if (i==selected_) {
			FillRect(bx,BY,112,BH,acc,6) ;
			DrawText(tx2,BY+9,t,bg,1) ;
		} else {
			FrameRect(bx,BY,112,BH,dim,6,2) ;
			DrawText(tx2,BY+9,t,fg,1) ;
		}
		bx+=112+GAP ;
	}
	DrawText(RX+8,BY+5,"A abre  <> botones",dim,0) ;
	DrawText(RX+8,BY+19,"A+B borra",dim,0) ;

	// los avisos (la rejilla queda tapada)
	if ((SDL_GetTicks()-notificationTime_)<=NOTIFICATION_TIMEOUT && !displayNotification_.empty()) {
		std::string n=displayNotification_ ;
		size_t nl=n.find('\n') ;
		if (nl!=std::string::npos) n=n.substr(0,nl) ;
		if (n.size()>38) n=n.substr(0,38) ;
		DrawText(RX+8+176,30,n.c_str(),fg,0) ;
	}
}

void SelectProjectDialog::OnPlayerUpdate(PlayerEventType,
                                         unsigned int currentTick) {};

void SelectProjectDialog::OnFocus() {

	setCurrentFolder(lastFolder_) ;
    currentProject_ = lastProject_;
    // trk2als: la lista va por fecha: el último que se abrió, por su nombre
    if (!lastName_.empty()) {
        int i = 0;
        IteratorPtr<Path> it(content_.GetIterator());
        for (it->Begin(); !it->IsDone(); it->Next(), i++)
            if (it->CurrentItem().GetName() == lastName_) { currentProject_ = i; break; }
    }
    if (currentProject_ >= content_.Size()) currentProject_ = 0;
    // la primera vez, el más reciente (no el "volver")
    if (lastName_.empty() && currentProject_ == 0 && content_.Size() > 1 &&
        GetCurrentProjectPath().GetName() == "..")
        currentProject_ = 1;
};

void SelectProjectDialog::ProcessButtonMask(unsigned short mask,bool pressed) {
	if (!pressed) return ;

    if (mask&EPBM_B) {
        // Handle A + B combination for delete
        if (mask & EPBM_A) {
            int count = 0;
            Path *current = 0;

            IteratorPtr<Path> it(content_.GetIterator());
            for (it->Begin(); !it->IsDone(); it->Next()) {
                if (count == currentProject_) {
                    current = &it->CurrentItem();
                    break;
                }
                count++;
            }

            // trk2als: solo proyectos (nunca ".." ni otras carpetas)
            if (current != 0 && !isProject(current->GetName())) {
                View::SetNotification("Solo se borran proyectos", 0);
                isDirty_ = true;
                return;
            }
            if (current != 0) {
                std::string message =
                    "¿Borrar el proyecto?\n" + projectTitle(current->GetName()) + "\nNo se puede deshacer";
                MessageBox *mb =
                    new MessageBox(*this, message.c_str(), MBBF_YES | MBBF_NO);
                mb->Label(MBL_YES, "Borrar").Label(MBL_NO, "Mejor no");
                DoModal(mb, DeleteProjectCallback);
                DrawView();
            }
            return;
        }
        if (mask & EPBM_UP)
            warpToNextProject(-pageSize());
        if (mask&EPBM_DOWN) warpToNextProject(pageSize()) ;
    } else {

        // A modifier
        if (mask & EPBM_A) {
            switch (selected_) {
			case 0: // load
				{
                // locate folder user had selected when they hit a
                int count = 0;
                Path *current = 0;

                IteratorPtr<Path> it(content_.GetIterator());
                for (it->Begin(); !it->IsDone(); it->Next()) {
                    if (count == currentProject_) {
                        current = &it->CurrentItem();
                        break;
                    }
                    count++;
                }

					if (!current) break ;   // trk2als: lista vacía
					//check if folder is a project, indicated by 'lgpt' being the first 4 characters of the folder name
					std::string name = current->GetName() ;
					if(isProject(name)){
						//ugly hack to make the "name" include subdirectories
						//we pass along everything past the root dir
						selection_ = *current ;
						lastFolder_=currentPath_ ;
						lastProject_=currentProject_ ;
						lastName_=name ;
						//load the project
						EndModal(1) ;
					} else {
						if (current->GetName() == "..") {
							Path parent=currentPath_.GetParent() ;
							setCurrentFolder(parent) ;
						} else {
							Path newdir=*current ;
							setCurrentFolder(newdir) ;
						}
					}
				break ;
			}
			case 1: // new
			{
                // trk2als: sin preguntar: un nombre al azar y directo a la Song
                std::string name = NewProjectDialog::RandomProjectName(currentPath_);
                Result result = OnNewProject(name);
                if (result.Failed()) {
                    Trace::Error(result.GetDescription().c_str());
                }
				break ;
            }
            case 2: { // Exit: trk2als: pregunta antes
                MessageBox *mb = new MessageBox(*this, "", MBBF_YES | MBBF_NO);
                mb->Title("¿Salir y perder la fe?").Label(MBL_YES, "Salir").Label(MBL_NO, "Me quedo");
                DoModal(mb, ExitCallback);
				break ;
            }
		}
        } else {

            // R Modifier

            if (mask & EPBM_R) {
            } else {
                // No modifier
				if (mask==EPBM_UP) warpToNextProject(-1) ;
				if (mask==EPBM_DOWN) warpToNextProject(1) ;
				if (mask==EPBM_LEFT) {
					selected_-- ;
					if (selected_<0) selected_+=3 ;
					isDirty_=true ;
				}
				if (mask==EPBM_RIGHT) {
					selected_=(selected_+1)%3 ;
					isDirty_=true ;
				}
            }
        }
    }
};

void SelectProjectDialog::warpToNextProject(int amount) {

    int offset = currentProject_ - topIndex_;
    int size = content_.Size();
    currentProject_+=amount ;
	if (currentProject_<0) currentProject_+=size ;
	if (currentProject_>=size) currentProject_-=size ;

	if ((amount>1)||(amount<-1)) {
		topIndex_=currentProject_-offset ;
		if (topIndex_<0) {
			topIndex_=0 ;
		} ;
	}
    isDirty_ = true;
}

Path SelectProjectDialog::GetSelection() {
	return selection_ ;
}

Result SelectProjectDialog::OnNewProject(std::string &name) {

    Path path = currentPath_.Descend(name);
    if (path.Exists()) {
        Trace::Log("SelectProjectDialog:OnNewProj","path already exists %s", path.GetPath().c_str());
		std::string res("Ya hay uno que se llama " + name);
		View::SetNotification(res.c_str(), 0);
        return Result(res);
    }
    Trace::Log("TMP","creating project at %s",path.GetPath().c_str());
	selection_ = path ;
	Result result = FileSystem::GetInstance()->MakeDir(path.GetPath().c_str()) ;
	RETURN_IF_FAILED(result, ("Failed to create project dir for '%s", path.GetPath().c_str()));

	path = path.Descend("samples");
	Trace::Log("TMP","creating samples dir at %s",path.GetPath().c_str());
	result = FileSystem::GetInstance()->MakeDir(path.GetPath().c_str()) ;
	RETURN_IF_FAILED(result, ("Failed to create samples dir for '%s'", path.GetPath().c_str()));

	EndModal(1) ;
  return Result::NoError;
} ;

Result SelectProjectDialog::OnDeleteProject(const Path &projectPath) {

    Trace::Log("SelectProjectDialog:OnDelProj","deleting project at %s", projectPath.GetPath().c_str());
	
	// Make a non-const copy to check existence
	Path pathCopy = projectPath;
	
	// Check if project exists before deletion
    if (!pathCopy.Exists()) {
        std::string errMsg = "No encontré el proyecto";
        View::SetNotification(errMsg.c_str(), 0);
        return Result("Project not found");
    }

    // Recursively delete the project directory and all contents
	RecursiveDeleteDirectory(projectPath);
	
	// Project deleted successfully, refresh the project list
	std::string successMsg = "Borrado: " + projectTitle(pathCopy.GetName());
	View::SetNotification(successMsg.c_str(), 0);
	
	// Refresh current folder to update the list while preserving position
	int savedProject = currentProject_;
	int savedTopIndex = topIndex_;
	Path currentPathCopy = currentPath_;
	setCurrentFolder(currentPathCopy);
	
	// Restore position (adjust if necessary if we're at the end of list)
	int listSize = content_.Size();
	if (savedProject >= listSize && listSize > 0) {
		currentProject_ = listSize - 1;
	} else if (listSize > 0) {
		currentProject_ = savedProject;
	}
    topIndex_ = savedTopIndex;
    isDirty_ = true;
		
	return Result::NoError;
};

//copy-paste-mutilate'd from ImportSampleDialog
void SelectProjectDialog::setCurrentFolder(Path &path) {

	//get ready
	selected_=0 ;
	currentPath_=path ;
	content_.Empty() ;
	times_.clear() ;
	
	// Let's read all the directory in the root

	I_Dir *dir=FileSystem::GetInstance()->Open(currentPath_.GetPath().c_str()) ;

  if (dir) 
  {

		// Get all lgpt something

		dir->GetContent("*");
    dir->Sort();
      
		// trk2als: primero "..", luego los proyectos del más reciente al más
		// viejo (como en Note), luego las demás carpetas
		std::vector<Entry> list ;
		IteratorPtr<Path> it(dir->GetIterator()) ;
		for(it->Begin();!it->IsDone();it->Next())
    {
			Path &path=it->CurrentItem() ;

			if (path.IsDirectory())
      {
				std::string name=path.GetName() ;
				// trk2als: solo los proyectos (ni "..", ni otras carpetas)
				if (isProject(name))
        {
					Entry e ;
					e.p=new Path(path) ;
					e.name=name ;
					e.kind=(name=="..")?0:(isProject(name)?1:2) ;
					e.t=(e.kind==1)?lastUsed(path):0 ;
					list.push_back(e) ;
				}
      }
		}
		for (size_t i=1;i<list.size();i++) {   // por inserción: son pocos
			Entry e=list[i] ;
			size_t j=i ;
			while (j>0) {
				const Entry &o=list[j-1] ;
				bool before=(e.kind<o.kind)||(e.kind==o.kind&&e.kind==1&&e.t>o.t) ;
				if (!before) break ;
				list[j]=list[j-1] ;
				j-- ;
			}
			list[j]=e ;
		}
		times_.clear() ;
		for (size_t i=0;i<list.size();i++) {
			content_.Insert(list[i].p) ;
			times_.push_back(list[i].t) ;
		}
		delete (dir) ;
  }

	//reset & redraw screen
	topIndex_=0 ;
	currentProject_=0 ;
    isDirty_ = true;
}

Path SelectProjectDialog::GetCurrentProjectPath() {
	int count = 0;
	IteratorPtr<Path> it(content_.GetIterator());
	for (it->Begin(); !it->IsDone(); it->Next()) {
		if (count == currentProject_) {
			return it->CurrentItem();
		}
		count++;
	}
	return Path();
}
