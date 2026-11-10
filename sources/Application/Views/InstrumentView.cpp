#include "InstrumentView.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Config.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Foundation/Variables/Variable.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "System/System/System.h"

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnFocus();
}

InstrumentView::InstrumentView(GUIWindow &w,ViewData *data):FieldView(w,data) {

	project_=data->project_ ;
	lastFocusID_=0 ;
	current_=0 ;
	lastSampleIdx_=-2 ;
	wfSample_=-1 ;
	wfSize_=0 ;
	onInstrumentChange() ;
}

InstrumentView::~InstrumentView() {
}

InstrumentType InstrumentView::getInstrumentType() {
	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instrument=bank->GetInstrument(i) ;
    return instrument->GetType() ;
} ;

void InstrumentView::onInstrumentChange() {

	ClearFocus() ;

	I_Instrument *old=current_ ;

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	current_=bank->GetInstrument(i) ;

	// trk2als: al cambiar el sample de este instrumento, detectar el BPM del loop
	if (current_->GetType()==IT_SAMPLE) {
		SampleInstrument *si=(SampleInstrument *)current_ ;
		int idx=si->GetSampleIndex() ;
		if (current_==old && lastSampleIdx_!=-2 && idx!=lastSampleIdx_ && idx>=0) {
			// 0 si no parece un loop: así no arrastra el BPM del sample anterior
			si->FindVariable(SIP_LOOPBPM)->SetInt(si->DetectLoopBpm()) ;
		}
		lastSampleIdx_=idx ;
	} else {
		lastSampleIdx_=-2 ;
	}

	if (current_!=old) {
		current_->RemoveObserver(*this) ;
	} ;
	T_SimpleList<UIField>::Empty() ;

	InstrumentType it=getInstrumentType() ;

    switch (it) {
		case IT_MIDI:
			fillMidiParameters() ;
			break ;
		case IT_SAMPLE:
			fillSampleParameters() ;
			break ;
	} ;

	SetFocus(T_SimpleList<UIField>::GetFirst()) ;
	IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator()) ;
	for (it2->Begin();!it2->IsDone();it2->Next()) {
        UIIntVarField &field=(UIIntVarField &)it2->CurrentItem() ;
        if (field.GetVariableID()==lastFocusID_) {
            SetFocus(&field) ;
            break ;
        }
    } ;
	if (current_!=old) {
		current_->AddObserver(*this) ;
	}
} ;

void InstrumentView::fillSampleParameters() {

	// trk2als: pantalla reacomodada. Arriba (filas 1-9) va la forma de onda
	// dibujada con pinceles; los campos van en dos columnas (x=1 y x=21) por
	// bloques: LOOP, SOUND, COLOR / FILTER y FINE TUNE. Las etiquetas de los
	// bloques se dibujan en drawWaveform(). "church wet / pad" ya no se muestra
	// pero sigue guardado en el proyecto.
	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	SampleInstrument *instrument=(SampleInstrument *)instr  ;
	const int L=1, R=21 ;
	GUIPoint p(L,10) ;
	Variable *v ;
	UIIntVarField *f1 ;
	UIStaticField *sf ;
#define ADD(f) T_SimpleList<UIField>::Insert(f)

	// el primero de la lista (A en él = vaciar instrumento)
	v=instrument->FindVariable(SIP_SAMPLE) ;
	SamplePool *sp=SamplePool::GetInstance() ;
	f1=new UIIntVarField(p,*v,"sample: %s",0,sp->GetNameListSize()-1,1,0x10) ;
	ADD(f1) ;
	f1->SetFocus() ;

	// LOOP
	p=GUIPoint(L,12) ;
	v=instrument->FindVariable(SIP_FROMBAR) ;
	ADD(new UIIntVarField(p,*v,"from bar: %d",1,64,1,4)) ;
	p=GUIPoint(R,12) ;
	v=instrument->FindVariable(SIP_LOOPBARS) ;
	ADD(new UIIntVarField(p,*v,"bars: %d",1,16,1,4)) ;

	p=GUIPoint(L,13) ;
	v=instrument->FindVariable(SIP_LOOPBPM) ;
	ADD(new UIIntVarField(p,*v,"bpm: %3d",0,300,1,10)) ;
	bpmInfoPos_=GUIPoint(L+10,13) ;

	p=GUIPoint(L,14) ;
	v=instrument->FindVariable(SIP_LOOPMODE) ;
	ADD(new UIIntVarField(p,*v,"loop mode: %s",0,SILM_LAST-1,1,1)) ;
	p=GUIPoint(28,14) ;
	v=instrument->FindVariable(SIP_SLICES) ;
	ADD(new UIIntVarField(p,*v,"slices: %2.2X",1,0xFF,1,0x10)) ;

	// SOUND
	p=GUIPoint(L,16) ;
	v=instrument->FindVariable(SIP_VOLUME) ;
	ADD(new UIIntVarField(p,*v,"volume: %d [%2.2X]",0,255,1,10)) ;
	p=GUIPoint(R,16) ;
	v=instrument->FindVariable(SIP_PAN) ;
	ADD(new UIIntVarField(p,*v,"pan: %2.2X",0,0xFE,1,0x10)) ;

	p=GUIPoint(L,17) ;
	v=instrument->FindVariable(SIP_ROOTNOTE) ;
	ADD(new UINoteVarField(p,*v,"root note: %s",0,0x7F,1,0x0C)) ;
	p=GUIPoint(R,17) ;
	v=instrument->FindVariable(SIP_FINETUNE) ;
	ADD(new UIIntVarField(p,*v,"detune: %2.2X",0,255,1,0x10)) ;

	// COLOR / FILTER
	p=GUIPoint(L,19) ;
	v=instrument->FindVariable(SIP_CRUSH) ;
	ADD(new UIIntVarField(p,*v,"crush: %d",1,0x10,1,4)) ;
	p=GUIPoint(L+10,19) ;
	v=instrument->FindVariable(SIP_CRUSHVOL) ;
	ADD(new UIIntVarField(p,*v,"drive: %2.2X",0,0xFF,1,0x10)) ;
	p=GUIPoint(R,19) ;
	v=instrument->FindVariable(SIP_DOWNSMPL) ;
	ADD(new UIIntVarField(p,*v,"downsample: %d",0,8,1,4)) ;

	p=GUIPoint(L,20) ;
	sf=new UIStaticField(p,"flt cut/res:") ;
	ADD(sf) ;
	p=GUIPoint(L+13,20) ;
	v=instrument->FindVariable(SIP_FILTCUTOFF) ;
	ADD(new UIIntVarField(p,*v,"%2.2X",0,0xFF,1,0x10)) ;
	p=GUIPoint(L+16,20) ;
	v=instrument->FindVariable(SIP_FILTRESO) ;
	ADD(new UIIntVarField(p,*v,"%2.2X",0,0xFF,1,0x10)) ;
	p=GUIPoint(R,20) ;
	v=instrument->FindVariable(SIP_FILTMIX) ;
	ADD(new UIIntVarField(p,*v,"type: %2.2X",0,0xFF,1,0x10)) ;

	p=GUIPoint(L,21) ;
	v=instrument->FindVariable(SIP_FILTMODE) ;
	ADD(new UIIntVarField(p,*v,"mode: %s",0,2,1,1)) ;
	p=GUIPoint(R,21) ;
	v=instrument->FindVariable(SIP_ATTENUATE) ;
	ADD(new UIIntVarField(p,*v,"atten: %d [%2.2X]",1,0xFF,1,0x10)) ;

	p=GUIPoint(L,22) ;
	sf=new UIStaticField(p,"fb tune/mix:") ;
	ADD(sf) ;
	p=GUIPoint(L+13,22) ;
	v=instrument->FindVariable(SIP_FBTUNE) ;
	ADD(new UIIntVarField(p,*v,"%2.2X",0,0xFF,1,0x10)) ;
	p=GUIPoint(L+16,22) ;
	v=instrument->FindVariable(SIP_FBMIX) ;
	ADD(new UIIntVarField(p,*v,"%2.2X",0,0xFF,1,0x10)) ;
	p=GUIPoint(R,22) ;
	v=instrument->FindVariable(SIP_INTERPOLATION) ;
	ADD(new UIIntVarField(p,*v,"interp: %s",0,1,1,1)) ;

	// FINE TUNE
	int last=instrument->GetSampleSize()-1 ;
	p=GUIPoint(L,24) ;
	v=instrument->FindVariable(SIP_START) ;
	ADD(new UIBigHexVarField(p,*v,7,"start: %7.7X",0,last,16)) ;
	p=GUIPoint(R,24) ;
	v=instrument->FindVariable(SIP_LOOPSTART) ;
	ADD(new UIBigHexVarField(p,*v,7,"lp start: %7.7X",0,last,16)) ;
	p=GUIPoint(L,25) ;
	v=instrument->FindVariable(SIP_END) ;
	ADD(new UIBigHexVarField(p,*v,7,"loop end: %7.7X",0,last,16)) ;

	p=GUIPoint(L,27) ;
	v=instrument->FindVariable(SIP_TABLEAUTO) ;
	ADD(new UIIntVarField(p,*v,"automation: %s",0,1,1,1)) ;
	// el último de la lista (A en él = quitar la tabla)
	p=GUIPoint(R,27) ;
	v=instrument->FindVariable(SIP_TABLE) ;
	ADD(new UIIntVarOffField(p,*v,"table: %2.2X",0x00,0x7F,1,0x10)) ;
#undef ADD
} ;

void InstrumentView::fillMidiParameters() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	MidiInstrument *instrument=(MidiInstrument *)instr  ;
	GUIPoint position=GetAnchor() ;

	Variable *v=instrument->FindVariable(MIP_CHANNEL) ;
	UIIntVarField* f1=new UIIntVarField(position,*v,"channel: %2.2d",0,0x0F,1,0x04,1) ;
	T_SimpleList<UIField>::Insert(f1) ;
	f1->SetFocus() ;

	position._y+=1;
	v=instrument->FindVariable(MIP_VOLUME) ;
	f1=new UIIntVarField(position,*v,"volume: %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1;
	v=instrument->FindVariable(MIP_NOTELENGTH) ;
	f1=new UIIntVarField(position,*v,"length: %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	v=instrument->FindVariable(MIP_TABLEAUTO) ;
	position._y+=2 ;
	UIIntVarField *f2=new UIIntVarField(position,*v,"automation: %s",0,1,1,1) ;
	T_SimpleList<UIField>::Insert(f2) ;

	position._y+=1;
	v=instrument->FindVariable(MIP_TABLE) ;
	f1=new UIIntVarOffField(position,*v,"table: %2.2X",0,0x7F,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

} ;


void InstrumentView::warpToNext(int offset) {
	int instrument=viewData_->currentInstrument_+offset ;
	if (instrument>=MAX_INSTRUMENT_COUNT) {
		instrument=instrument-MAX_INSTRUMENT_COUNT ;
	} ;
	if (instrument<0) {
		instrument=MAX_INSTRUMENT_COUNT+instrument ;
	} ;
	viewData_->currentInstrument_=instrument ;
	onInstrumentChange() ;
	isDirty_=true ;
} ;

void InstrumentView::ProcessButtonMask(unsigned short mask,bool pressed) {

	if (!pressed) return ;

	isDirty_=false ;

	if (viewMode_==VM_NEW) {
		if (mask==EPBM_A) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			Variable &v=field->GetVariable() ;
			switch(v.GetID()) {
				case SIP_SAMPLE:
				 {
                    // First check if the samplelib exists

					 Path sampleLib(SamplePool::GetInstance()->GetSampleLib()) ;
					 if (FileSystem::GetInstance()->GetFileType(sampleLib.GetPath().c_str())!=FT_DIR) {
						 MessageBox *mb=new MessageBox(*this,"Can't access the samplelib",MBBF_OK) ;
						 DoModal(mb) ;
					 } else { ;
						// Go to import sample

						 ImportSampleDialog *isd=new ImportSampleDialog(*this) ;
                         DoModal(isd, ImportSampleDialogCallback);
                     }
                    break ;
				 }
				case SIP_TABLE:
				 {
					int next=TableHolder::GetInstance()->GetNext() ;
					if (next!=NO_MORE_TABLE) {
						v.SetInt(next) ;
						isDirty_=true ;
					}
					break ;
                }
                case SIP_PRINTFX: {
                    FxPrinter printer(viewData_);
                    isDirty_ = printer.Run();
                    View::SetNotification(printer.GetNotification());
                    break;
                }
                default:
                    break ;
			}
			mask&=(0xFFFF-EPBM_A) ;
		}
	}

	if (viewMode_==VM_CLONE) {
        if ((mask&EPBM_A)&&(mask&EPBM_L)) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			mask&=(0xFFFF-EPBM_A) ;
			Variable &v=field->GetVariable() ;
			int current=v.GetInt() ;
			if (current==-1) return ;

			int next=TableHolder::GetInstance()->Clone(current) ;
			if (next!=NO_MORE_TABLE) {
				v.SetInt(next) ;
				isDirty_=true ;
			}
		}
		mask&=(0xFFFF-(EPBM_A|EPBM_L)) ;
	} ;

	if (viewMode_==VM_SELECTION) {
	} else {
		viewMode_=VM_NORMAL ;
	}

	// trk2als: si cambian "from bar" o "bars", la región salta a esos compases
	FourCC editedID=0 ;
	int editedBefore=0 ;
	if (GetFocus() && current_ && current_->GetType()==IT_SAMPLE) {
		UIIntVarField *ff=(UIIntVarField *)GetFocus() ;
		if (!ff->IsStatic()) {
			editedID=ff->GetVariableID() ;
			editedBefore=ff->GetVariable().GetInt() ;
		}
	}
	FieldView::ProcessButtonMask(mask) ;
	if ((editedID==SIP_FROMBAR || editedID==SIP_LOOPBARS) &&
	    current_->FindVariable(editedID)->GetInt()!=editedBefore) {
		((SampleInstrument *)current_)->ApplyBarRegion() ;
		isDirty_=true ;
	}

    Player *player=Player::GetInstance() ;
	// B Modifier

    if (mask & EPBM_B) {
        if (mask&EPBM_LEFT) warpToNext(-1) ;
		if (mask&EPBM_RIGHT) warpToNext(+1);
		if (mask&EPBM_DOWN) warpToNext(-16) ;
		if (mask&EPBM_UP) warpToNext(+16);
		if (mask&EPBM_A) { // Allow cut instrument
		   if (getInstrumentType()==IT_SAMPLE) {
                if (GetFocus()==T_SimpleList<UIField>::GetFirst()) {
	               int i=viewData_->currentInstrument_ ;
	               InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	               I_Instrument *instr=bank->GetInstrument(i) ;
					instr->Purge() ;
//                   Variable *v=instr->FindVariable(SIP_SAMPLE) ;
//                   v->SetInt(-1) ;
                   isDirty_=true ;
                }
           }

		   // Check if on table
		   if (GetFocus()==T_SimpleList<UIField>::GetLast()) {
	            int i=viewData_->currentInstrument_ ;
	            InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	            I_Instrument *instr=bank->GetInstrument(i) ;
                Variable *v=instr->FindVariable(SIP_TABLE) ;
                v->SetInt(-1) ;
                isDirty_=true ;
		   } ;
        }
        if (mask&EPBM_L) {
            viewMode_=VM_CLONE ;
        } ;
    } else {

        // A modifier

        if (mask == EPBM_A) {
            FourCC varID = ((UIIntVarField *)GetFocus())->GetVariableID();
            if ((varID == SIP_TABLE) || (varID == MIP_TABLE) ||
                (varID == SIP_SAMPLE) || (varID == SIP_PRINTFX)) {
                viewMode_ = VM_NEW;
			}
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    // trk2als: si vino de un loop de la song, regresa a la song
                    ViewType vt = viewData_->instrumentFromSong_ ? VT_SONG : VT_PHRASE;
                    viewData_->instrumentFromSong_ = false;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {

                    // Go to table view

                    ViewType vt = VT_TABLE2;

                    int i = viewData_->currentInstrument_;
                    InstrumentBank *bank =
                        viewData_->project_->GetInstrumentBank();
                    I_Instrument *instr = bank->GetInstrument(i);
                    int table = instr->GetTable();
                    if (table != VAR_OFF) {
                        viewData_->currentTable_ = table;
                    }
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                // if (mask&EPBM_RIGHT) {

                //	// Go to import sample

                //		ViewType vt=VT_IMPORT ;
                //		ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
                //		SetChanged();
                //		NotifyObservers(&ve) ;
                //}

                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
            } else {
                // No modifier
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                                          viewData_->chainRow_);
                }
            }
        }
    }

    UIIntVarField *field = (UIIntVarField *)GetFocus();
    if (field) {
	   lastFocusID_=field->GetVariableID() ;
    }

} ;

void InstrumentView::DrawView() {

	Clear() ;
	ClearShapes() ; // trk2als
    View::EnableNotification();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    // Draw title

    char title[20];
    SetColor(CD_NORMAL);
    sprintf(title, "Instrument %2.2X", viewData_->currentInstrument_);
    DrawString(pos._x, pos._y, title, props);

    // Draw fields

    FieldView::Redraw();
    drawMap() ;

    // trk2als: cuánto se acelera o frena el loop para ir al tempo del proyecto
    if (current_ && current_->GetType()==IT_SAMPLE) {
        SampleInstrument *si=(SampleInstrument *)current_ ;
        int bpm=si->FindVariable(SIP_LOOPBPM)->GetInt() ;
        int bars=si->FindVariable(SIP_LOOPBARS)->GetInt() ;
        float secs=si->GetLoopSeconds() ;
        bool sync=viewData_->instrumentFromSong_ ||
                  si->FindVariable(SIP_LOOPMODE)->GetInt()==SILM_LOOPSYNC ;
        if (sync && bpm>0 && secs>0 && bars>0) {
            int tempo=viewData_->project_->GetTempo() ;
            float ratio=secs/(bars*240.0f/tempo) ; // >1 = se acelera
            char info[32] ;
            sprintf(info,"%d>%d %+.1fst",bpm,tempo,12.0f*log2f(ratio)) ;
            SetColor(CD_HILITE1) ;
            DrawString(bpmInfoPos_._x,bpmInfoPos_._y,info,props) ;
            SetColor(CD_NORMAL) ;
        }
    }
    drawWaveform() ; // trk2als
} ;

/************************************************************
 trk2als: forma de onda con los pinceles, a todo lo ancho (filas 1-9).
 Compases numerados (si hay bpm), la región del loop en naranja con su marco,
 rayitas de cada tiempo dentro de la región y una línea verde donde arranca.
 También dibuja las etiquetas de los bloques de campos.
 ************************************************************/

void InstrumentView::drawWaveform() {
	if (!SupportsShapes() || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;

	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi=ColorOf(CD_HILITE1) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	GUIColor dim=Mix(bg,fg,27) ;
	GUIColor dim2=Mix(bg,fg,55) ;

	// etiquetas de los bloques
	DrawText(16,11*16+4,"LOOP",dim2,0) ;
	DrawText(16,15*16+4,"SOUND",dim2,0) ;
	DrawText(16,18*16+4,"COLOR / FILTER",dim2,0) ;
	DrawText(16,23*16+4,"FINE TUNE",dim2,0) ;

	const int X=16, Y=22, W=WF_BARS*8, H=124 ;
	FillRect(X-8,Y-4,W+16,H+8,Mix(bg,fg,9),8) ;

	int index=si->GetSampleIndex() ;
	if (index<0) return ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(index) ;
	if (!src) return ;
	int size=src->GetSize(-1) ;
	short *data=(short *)src->GetSampleBuffer(-1) ;
	int channels=src->GetChannelCount(-1) ;
	if (size<=0 || !data || channels<1) return ;

	if (index!=wfSample_ || size!=wfSize_) {
		for (int b=0;b<WF_BARS;b++) {
			int from=int((long long)size*b/WF_BARS) ;
			int to=int((long long)size*(b+1)/WF_BARS) ;
			int step=(to-from)/200+1 ;      // basta con unas 200 lecturas por barra
			int peak=0 ;
			for (int i=from;i<to;i+=step) {
				int v=data[i*channels] ;
				if (v<0) v=-v ;
				if (v>peak) peak=v ;
			}
			wfPeaks_[b]=(unsigned char)(peak>>7) ;   // 0..255
		}
		wfSample_=index ;
		wfSize_=size ;
	}

	int st=si->FindVariable(SIP_START)->GetInt() ;
	int ls=si->FindVariable(SIP_LOOPSTART)->GetInt() ;
	int le=si->FindVariable(SIP_END)->GetInt() ;
	if (le<=0 || le>size) le=size ;
	if (ls<0 || ls>=le) ls=0 ;
	int s0=X+int((long long)ls*W/size) ;
	int s1=X+int((long long)le*W/size) ;
	if (s1<s0+2) s1=s0+2 ;

	// duración y compases arriba a la derecha
	float bar=si->GetBarSamples() ;
	int rate=src->GetSampleRate(-1) ;
	if (rate>0) {
		char info[32] ;
		int cs=int((long long)size*100/rate) ;
		if (bar>0) {
			sprintf(info,"%d.%02ds  %d bars",cs/100,cs%100,int(size/bar+0.5f)) ;
		} else {
			sprintf(info,"%d.%02ds",cs/100,cs%100) ;
		}
		DrawText(632-8*int(strlen(info)),3,info,dim2,0) ;
	}

	// región seleccionada (fondo)
	FillRect(s0,Y-4,s1-s0,H+8,Mix(bg,hi,20),8) ;

	// barras de la onda
	int mid=Y+20+(H-20)/2 ;
	for (int b=0;b<WF_BARS;b++) {
		int c=X+b*8+4 ;
		bool inLoop=(c>=s0 && c<s1) ;
		int h=(wfPeaks_[b]*(H-30))/255 ;
		if (h<4) h=4 ;
		FillRect(c-3,mid-h/2,5,h,inLoop?hi:dim,2) ;
	}

	// compases: líneas, números y tiempos dentro de la región
	if (bar>0) {
		float px=bar*W/size ;                     // ancho de un compás en pixeles
		int total=int(size/bar+0.999f) ;
		int every=1 ;                             // numerar sin amontonar
		while (px*every<22) every*=2 ;
		for (int i=0;i<total;i++) {
			int x=X+int(i*px) ;
			if (x>=X+W) break ;
			if (i>0) FillRect(x,Y+18,1,H-20,Mix(bg,fg,28)) ;
			if (i%every==0) {
				char num[8] ;
				sprintf(num,"%d",i+1) ;
				int pos=int(i*bar) ;
				bool sel=(pos>=ls && pos<le) ;
				DrawText(x+5,Y+1,num,sel?hi2:dim2,0) ;
			}
		}
		if (px/4>=4) {                            // rayitas de los tiempos
			for (float t=ls+bar/4;t<le-1;t+=bar/4) {
				if (int((t-ls)/(bar/4)+0.5f)%4==0) continue ;
				int x=X+int((long long)t*W/size) ;
				FillRect(x,Y+H-8,1,6,hi2) ;
			}
		}
	}

	FrameRect(s0,Y-4,s1-s0,H+8,hi,8,2) ;
	// dónde arranca el sample
	int sx=X+int((long long)st*W/size) ;
	FillRect(sx-1,Y-4,2,H+8,ColorOf(CD_PLAY)) ;
}

void InstrumentView::OnFocus() { onInstrumentChange(); }

void InstrumentView::Update(Observable &o,I_ObservableData *d) {
	onInstrumentChange() ;
}
