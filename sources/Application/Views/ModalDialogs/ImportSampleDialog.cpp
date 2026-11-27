#include "ImportSampleDialog.h"
#include "Application/Instruments/WarpStretch.h"
#include "Application/Model/Config.h"
#include <strings.h>
#include <math.h>
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Player/Player.h"
#include <math.h>
#include <stdio.h>
#include <string>
#include "Application/Instruments/SF2Import.h"
#include "System/io/Status.h"
#include "System/Console/Trace.h"
#include "Application/Views/ViewData.h"

#define LIST_SIZE 15
#define LIST_WIDTH 28

bool ImportSampleDialog::initStatic_=false ;
Path ImportSampleDialog::sampleLib_("") ;
Path ImportSampleDialog::currentPath_("") ;

static const char *buttonText[3]= {
	"Listen",
	"Import",
	"Exit"	
} ;

ImportSampleDialog::ImportSampleDialog(View &view, bool audioOnly, bool sf2Only, bool wavOnly):ModalView(view),audioOnly_(audioOnly),sf2Only_(sf2Only),wavOnly_(wavOnly) {
	if (!initStatic_) {
		const char *slpath=SamplePool::GetInstance()->GetSampleLib() ;
		sampleLib_=Path(slpath) ;
		currentPath_=Path(slpath) ;
		initStatic_=true ;
	}
	selected_=0 ;
} ;

ImportSampleDialog::~ImportSampleDialog() {
} ;

void ImportSampleDialog::DrawView() {

    SetWindow(LIST_WIDTH, LIST_SIZE + 3);

    GUITextProperties props;

    // Draw title

    //	char title[40] ;

    SetColor(CD_NORMAL);

    //	sprintf(title,"Sample Import from %s",currentPath_.GetName()) ;
    //	w_.DrawString(title,pos,props) ;

    // Draw samples

    int x = 1;
    int y=1 ;

	if (currentSample_<topIndex_) {
		topIndex_=currentSample_ ;
	} ;
	if (currentSample_>=topIndex_+LIST_SIZE) {
		topIndex_=currentSample_ ;
	} ;

	IteratorPtr<Path> it(sampleList_.GetIterator()) ;
	int count=0 ;
	char buffer[256] ;
	for(it->Begin();!it->IsDone();it->Next()) {
		if ((count>=topIndex_)&&(count<topIndex_+LIST_SIZE)) {
			Path &current=it->CurrentItem() ;
			const std::string p=current.GetName() ;

			if (count==currentSample_) {
				SetColor(CD_HILITE2) ;
				props.invert_=true ;
			} else {
				SetColor(CD_NORMAL) ;
                props.invert_ = current.Matches("*.sf2");
            }
			if (!current.IsDirectory()) {
				strcpy(buffer,p.c_str()) ;
			} else {
				buffer[0]='[' ;
				strcpy(buffer+1,p.c_str()) ;
				strcat(buffer,"]") ;
			}
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
} ;

void ImportSampleDialog::warpToNextSample(int direction) {

	currentSample_+=direction ;
	int size=sampleList_.Size() ;
	if (currentSample_ < 0) currentSample_ = 0;
	if (currentSample_ >= size) currentSample_ = size - 1;
	endPreview();
	isDirty_=true ;
}

void ImportSampleDialog::OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) {
} ;

void ImportSampleDialog::OnFocus() {
	Path current(currentPath_) ;
	setCurrentFolder(&current) ;
	toInstr_=viewData_->currentInstrument_ ;
} ;

// trk2als: la pre-escucha de un archivo (también la usa el navegador nuevo):
// un .sf2 suena su C3; un loop, al tempo y en fase con la canción
// trk2als: la prueba en complex que se está estirando
static int cplxJob_=0, cplxBars_=1, cplxTempo_=120 ;
static Path cplxOut_("root:loopcplx0.wav") ;

// cuando la prueba en complex ya está: a sonar en loop, en fase con la canción
bool SampleFilePreviewTick() {
	if (!cplxJob_) return false ;
	if (WarpPreview::Failed(cplxJob_)) { cplxJob_=0 ; return false ; }
	if (!WarpPreview::Ready(cplxJob_)) return false ;
	cplxJob_=0 ;
	Player *player=Player::GetInstance() ;
	double delay=0,startFrac=0 ;
	if (player->IsRunning()) {
		double beat=60.0/cplxTempo_ ;
		double t=player->GetPlayTime() ;
		long next=long(floor(t/beat))+1 ;
		delay=next*beat-t ;
		startFrac=double(next%(4*cplxBars_))/(4.0*cplxBars_) ;
	}
	player->StartStreaming(cplxOut_,1.0,delay,startFrac,true) ;
	return true ;
}

void SampleFilePreviewCancel() { cplxJob_=0 ; }

void SampleFilePreview(Path element, ViewData *viewData_, int toInstr_, bool audioOnly_, int sfPreset) {
	cplxJob_=0 ;   // lo que se estaba estirando ya no
	// trk2als: de un .sf2 se escucha su sample del C3 (se escribe aparte)
	if (element.Matches("*.sf2")) {
		Path tmp("root:sf2preview.wav") ;
		if (SF2PreviewNote(element.GetPath().c_str(),sfPreset,60,tmp.GetPath().c_str())) {
			Player::GetInstance()->StartStreaming(tmp) ;
		}
		return ;
	}
	// trk2als: si es un loop, el preview suena al tempo del proyecto, en loop
	// y en fase: con la canción sonando entra en el siguiente tiempo, en el
	// punto del loop que corresponde a ese tiempo del compás
	Player *player=Player::GetInstance() ;
	// solo para loops: el instrumento de una pista de audio, o uno en looper
	// sync. En una pista de tracker el sample se oye a su velocidad original
	bool loopContext=audioOnly_ || viewData_->project_->IsAudioInstrument(toInstr_) ;
	I_Instrument *instr=viewData_->project_->GetInstrumentBank()->GetInstrument(toInstr_) ;
	if (instr && instr->GetType()==IT_SAMPLE &&
	    instr->FindVariable(SIP_LOOPMODE)->GetInt()==SILM_LOOPSYNC) {
		loopContext=true ;
	}
	WavFile *wav=loopContext?WavFile::Open(element.GetPath().c_str()):0 ;
	if (wav) {
		float secs=(wav->GetSampleRate(-1)>0)?float(wav->GetSize(-1))/wav->GetSampleRate(-1):0 ;
		delete wav ;
		// el nombre manda; si se abrió desde una pista de audio, también se adivina
		int bpm=SampleInstrument::DetectBpm(element.GetName().c_str(),secs,
		                                    audioOnly_ || viewData_->project_->IsAudioInstrument(toInstr_)) ;
		if (bpm>0) {
			int tempo=viewData_->project_->GetTempo() ;
			int bars=int(secs*bpm/240.0f+0.5f) ;
			if (bars<1) bars=1 ;
			double speed=(secs*tempo)/(bars*240.0) ; // la región dura 'bars' compases
			// trk2als: si los loops entran en complex, la prueba también: se
			// estira por detrás y suena cuando está (SampleFilePreviewTick)
			const char *lw=Config::GetInstance()->GetValue("LOOPWARP") ;
			if (lw && !strcasecmp(lw,"complex") && WarpStretch::Available()) {
				WavFile *w2=WavFile::Open(element.GetPath().c_str()) ;
				int rate=w2?w2->GetSampleRate(-1):44100 ;
				long frames=w2?w2->GetSize(-1):0 ;
				delete w2 ;
				// trk2als: la misma velocidad que usará el clip (el compás del
				// reproductor son ticks enteros: SampleInstrument, SILM_LOOPSYNC),
				// para que lo preparado aquí le sirva tal cual
				double ratio=double(rate)/44100.0*speed ;
				int tick=int(60.0f*44100.0f*2.0f/tempo/8.0f/6.0f) ;
				if (frames>0 && tick>0) ratio=double(frames)/(double(tick)*96.0*bars) ;
				player->StopStreaming() ;
				cplxBars_=bars ;
				cplxTempo_=tempo ;
				cplxOut_=Path((cplxJob_+1)%2?"root:loopcplx1.wav":"root:loopcplx0.wav") ;
				cplxJob_=WarpPreview::Request(element.GetPath().c_str(),cplxOut_.GetPath().c_str(),
				                              ratio,float(rate)/44100.0f,tempo) ;
				return ;
			}
			cplxJob_=0 ;
			double delay=0,startFrac=0 ;
			if (player->IsRunning()) {
				double beat=60.0/tempo ;
				double t=player->GetPlayTime() ;
				long next=long(floor(t/beat))+1 ;            // siguiente tiempo de la canción
				delay=next*beat-t ;
				startFrac=double(next%(4*bars))/(4.0*bars) ; // ese tiempo dentro del loop
			}
			player->StartStreaming(element,speed,delay,startFrac,true) ;
			return ;
		}
	}
	player->StartStreaming(element) ;
}

void ImportSampleDialog::preview(Path &element) {
	SampleFilePreview(element, viewData_, toInstr_, audioOnly_) ;
}

void ImportSampleDialog::endPreview() {
	Player::GetInstance()->StopStreaming() ;
}

// trk2als: lo que hace "importar" (también desde el navegador nuevo): un .sf2
// se convierte en zonas; un .wav entra al pool (por referencia) y queda como
// el sample del instrumento. false con el porqué en err
bool SampleFileImport(Path element, ViewData *viewData_, int toInstr_, std::string &err, int sfPreset) {
	I_Instrument *instr=viewData_->project_->GetInstrumentBank()->GetInstrument(toInstr_) ;
	if (!instr || instr->GetType()!=IT_SAMPLE) { err="no es un instrumento de samples" ; return false ; }
	if (element.Matches("*.sf2")) {
		std::string msg ;
		Status::Set("Convirtiendo %s", element.GetName().c_str()) ;
		bool ok=SF2Import(element.GetPath().c_str(),sfPreset,(SampleInstrument *)instr,msg) ;
		if (ok) {
			char buf[64] ;
			snprintf(buf,sizeof(buf),"%s: %d zonas",msg.c_str(),((SampleInstrument *)instr)->ZoneCount()) ;
			err=buf ;   // (es el aviso, no un error)
		} else err=msg ;
		return ok ;
	}
	int sampleID=SamplePool::GetInstance()->ImportSample(element) ;
	if (sampleID<0) {
		err=(sampleID == -SLOAD_ERR_MAX_SAMPLES) ? "Ya no caben más samples"
		   : (sampleID == -SLOAD_ERR_MAX_SOUNDFONTS) ? "Ya no caben más soundfonts"
		   : (sampleID == -SLOAD_ERR_INVALID_DIR) ? "Carpeta no válida"
		   : "No se pudo abrir el archivo" ;
		Trace::Error(err.c_str()) ;
		return false ;
	}
	// trk2als: el instrumento se compromete con este sample
	((SampleInstrument *)instr)->AssignSample(sampleID) ;
	return true ;
}

void ImportSampleDialog::import(Path &element) {
	std::string msg ;
	bool ok=SampleFileImport(element,viewData_,toInstr_,msg) ;
	endPreview() ;
	if (ok) {
		if (!msg.empty()) View::SetNotification(msg.c_str()) ;
		EndModal(1) ;
		return ;
	}
	if (element.Matches("*.sf2")) {
		View::SetNotification(msg.c_str()) ;
	} else {
		MessageBox *mb = new MessageBox(*this, msg.c_str());
		View::DoModal(mb);
	}
	isDirty_=true ;
}

void ImportSampleDialog::ProcessButtonMask(unsigned short mask,bool pressed) {

	if (!pressed) return ;

	if (mask&EPBM_B) {  
		if (mask&EPBM_UP) warpToNextSample(-LIST_SIZE) ;
		if (mask&EPBM_DOWN) warpToNextSample(LIST_SIZE) ;
		if (mask==EPBM_B) endPreview() ;   // trk2als: B solo: ya oí suficiente
	} else if (mask&EPBM_A) {
		// Allow browse preview
		if (mask&EPBM_UP) warpToNextSample(-1) ;
		if (mask&EPBM_DOWN) warpToNextSample(1) ;

		Path *element = getImportElement();
		setCurrent(element, mask);

		switch(selected_) {
			case 0: // preview
                if (!element->IsDirectory()) { // trk2als: un .sf2 también (su C3)
                    preview(*element);
                }
                break;
            case 1: // import
				if(!element->IsDirectory()) { // Don't import folders
					import(*element);
				}
				break ;
			case 2: // Exit ;
				endPreview(); // Stop playback when exiting
				EndModal(0) ;
				break ;
		}
	} else if (mask&EPBM_START) { // START Modifier
		if (mask&EPBM_UP) warpToNextSample(-1);
		if (mask&EPBM_DOWN) warpToNextSample(1);
		Path *element = getImportElement();
		setCurrent(element, mask);
		if(!element->IsDirectory()) {
			preview(*element);
		}
		if (mask&EPBM_RIGHT) { // Load sample
			if (!element->IsDirectory()) {
				endPreview();
				import(*element);
			}
		}
		if (mask&EPBM_LEFT) { // Navigate up
			if (isSampleLibRoot()) {
			} else {
				Path parent = element->GetParent().GetParent();
				setCurrentFolder(&parent);
				isDirty_=true;
			}
		}
	} else { // No modifier
		if (mask==EPBM_UP) warpToNextSample(-1);
		if (mask==EPBM_DOWN) warpToNextSample(1);
		if (mask==EPBM_LEFT) {
			selected_-=1;
			if (selected_<0) selected_+=3;
			isDirty_=true;
		}
		if (mask==EPBM_RIGHT) {
			selected_=(selected_+1)%3;
			isDirty_=true;
		}
	}
} ;

bool ImportSampleDialog::isSampleLibRoot()
{
    // return sampleLib_.GetPath().find(currentPath_.GetPath()) != std::string::npos; // Causes issues in Win, Miyoo
	return currentPath_.GetPath()==sampleLib_.GetPath();
};

Path* ImportSampleDialog::getImportElement() {
	IteratorPtr<Path> it(sampleList_.GetIterator());
	int count = 0;
	Path *element = 0;
	for(it->Begin(); !it->IsDone(); it->Next()) {
		if (count++ == currentSample_) {
			return &it->CurrentItem();
		}
	}
}

void ImportSampleDialog::setCurrent(Path *element, unsigned short mask) {
	if (selected_ != 2 && element->IsDirectory() && // Folders
		!(mask&EPBM_UP||mask&EPBM_DOWN)) { // Don't browse preview folders
			if (element->GetName()=="..") {
				if (isSampleLibRoot()) {
				} else {
					Path parent = element->GetParent().GetParent();
					setCurrentFolder(&parent);
				}
		} else {
			setCurrentFolder(element);
		}
		isDirty_ = true;
		return;
	}
}

void ImportSampleDialog::setCurrentFolder(Path *path) {

	Path formerPath(currentPath_) ;

	topIndex_=0 ;
	currentSample_=0 ;

	currentPath_=Path(*path) ;
	sampleList_.Empty() ;
	if (path) {
		int count=0 ;
		I_Dir *dir=FileSystem::GetInstance()->Open(path->GetPath().c_str()) ;	
		if (dir) {
			dir->GetContent("*") ;
			dir->Sort() ;
			IteratorPtr<Path>it(dir->GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Path &current=it->CurrentItem() ;
		 		if (current.IsDirectory()) {
					if (current.GetName().substr(0,1)!="." || current.GetName()=="..") {
						Path *sample=new Path(current) ;
						sampleList_.Insert(sample) ;
						if (!formerPath.Compare(current)) {
							currentSample_=count ;
						}
						count++ ;
					}
				}
			}
			for (it->Begin();!it->IsDone();it->Next()) {
                Path &current = it->CurrentItem();
                if (!current.IsDirectory()) {
                                  if (((current.Matches("*.wav") && !sf2Only_) || (!audioOnly_ && !wavOnly_ && current.Matches("*.sf2"))) && current.GetName()[0]!='.') {
						Path *sample=new Path(current) ;
						sampleList_.Insert(sample) ;
					}
				};
			}
		}
	}
} ;
