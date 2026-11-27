#include "InstrumentView.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Instruments/SF2Import.h"
#include "Application/Instruments/SoundFontPreset.h"
#include "Application/AppWindow.h"
#include <math.h>
#include <ctype.h>
#include <stdlib.h>


// trk2als: campo sample que se salta los .wav de las zonas de un soundfont
// (son partes internas de ese instrumento, no samples para usar sueltos)
class UISampleField: public UIIntVarField {
public:
	UISampleField(GUIPoint &p,Variable &v,int max,const std::vector<bool> &hidden)
		:UIIntVarField(p,v,"sample: %s",0,max,1,0x10),hidden_(hidden) {}
	virtual void ProcessArrow(unsigned short mask) {
		int value=src_.GetInt() ;
		int step=0 ;
		switch (mask) {
			case EPBM_UP: step=yOffset_ ; break ;
			case EPBM_DOWN: step=-yOffset_ ; break ;
			case EPBM_LEFT: step=-xOffset_ ; break ;
			case EPBM_RIGHT: step=xOffset_ ; break ;
		}
		if (!step) return ;
		int dir=step>0?1:-1 ;
		int target=value+step ;
		if (target<min_) target=min_ ;
		if (target>max_) target=max_ ;
		// si cae en uno escondido, sigue en la misma dirección
		while (target>=min_ && target<=max_ && isHidden(target)) target+=dir ;
		if (target<min_ || target>max_) return ;
		src_.SetInt(target) ;
	}
private:
	bool isHidden(int i) { return i>=0 && i<(int)hidden_.size() && hidden_[i] ; }
	std::vector<bool> hidden_ ;
} ;

// trk2als: en un instrumento soundfont el campo sample muestra el nombre y
// cuántas zonas tiene, y no se recorre con las flechas (A,A abre el
// navegador solo con .sf2)
class UISoundfontField: public UIIntVarField {
public:
	UISoundfontField(GUIPoint &p,Variable &v,const std::string &label,int idx,bool fixed=false)
		:UIIntVarField(p,v,"sample: %s",idx,idx,1,1),label_(label),fixed_(fixed) {}
	// trk2als: el sonido de una pista se elige en su cabecera: aquí solo se
	// muestra (no se puede posar el cursor)
	virtual bool IsStatic() { return fixed_ ; }
	virtual void Draw(GUIWindow &w,int offset=0) {
		GUITextProperties props ;
		GUIPoint position=GetPosition() ;
		position._y+=offset ;
		if (focus_) {
			((AppWindow&)w).SetColor(CD_HILITE2) ;
			props.invert_=true ;
		} else {
			((AppWindow&)w).SetColor(CD_NORMAL) ;
		}
		std::string t=(label_.compare(0,5,"kit: ")==0?"":"sample: ")+label_ ;
		w.DrawString(t.c_str(),position,props) ;
	}
	virtual void ProcessArrow(unsigned short) {}
private:
	std::string label_ ;
	bool fixed_ ;
} ;

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Model/Config.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Foundation/Variables/Variable.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "ModalDialogs/NewProjectDialog.h"
#include "ModalDialogs/PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "System/System/System.h"
#include "Services/Audio/Audio.h"

// trk2als: frecuencia de corte en Hz. A+izq/der: un semitono; A+arr/aba:
// una octava. Se muestra en Hz o kHz
// trk2als: rebanadas del Sampler en un solo campo: "slices: 8" (partes
// iguales) o "slices: manual" (marcas a mano, como Slice by: Manual de
// Simpler). Por debajo de 2 pasa a manual; desde manual, → regresa a partes
// iguales. Es el campo de "slice by" para la vista (GetVariableID)
class UISlicesField : public UIIntVarField {
public:
	UISlicesField(GUIPoint &p, Variable &by, Variable &count) : UIIntVarField(p,by,"",0,1,1,1), count_(count) {}
	virtual void Draw(GUIWindow &w,int offset=0) {
		GUITextProperties props ;
		GUIPoint position=GetPosition() ;
		position._y+=offset ;
		if (focus_) {
			((AppWindow&)w).SetColor(CD_HILITE2) ;
			props.invert_=true ;
		} else {
			((AppWindow&)w).SetColor(CD_NORMAL) ;
		}
		char buffer[32] ;
		if (src_.GetInt()==SSB_MANUAL) strcpy(buffer,"slices: manual") ;
		else sprintf(buffer,"slices: %d",count_.GetInt()) ;
		w.DrawString(buffer,position,props) ;
	}
	virtual void ProcessArrow(unsigned short mask) {
		int n=count_.GetInt() ;
		if (n<2) n=2 ;
		if (src_.GetInt()==SSB_MANUAL) {
			if (mask==EPBM_RIGHT || mask==EPBM_UP) src_.SetInt(SSB_REGION) ;
			return ;
		}
		switch (mask) {
			case EPBM_RIGHT: n+=1 ; break ;
			case EPBM_LEFT:  n-=1 ; break ;
			case EPBM_UP:    n+=4 ; break ;
			case EPBM_DOWN:  n-=4 ; break ;
			default: return ;
		}
		if (n<2) {                    // menos de 2: marcas a mano
			src_.SetInt(SSB_MANUAL) ;
			return ;
		}
		if (n>64) n=64 ;
		count_.SetInt(n) ;
	}
private:
	Variable &count_ ;
} ;

// trk2als: el MODO del Sampler. A + ← → cambia de modo; en slice, A + ↑ ↓
// elige cómo se rebana ahí mismo: 2, 4, 8 o a mano (lo último hacia arriba)
class UIModeField : public UIIntVarField {
public:
	UIModeField(GUIPoint &p, Variable &mode, Variable &by, Variable &count, I_Instrument *inst) :
		UIIntVarField(p,mode,"MODO: %s",0,2,1,1), by_(by), count_(count), inst_(inst) {}
	static Variable *fresh_ ;   // el modo que acaba de llegar a slice (aún sin rebanar)
	virtual void ProcessArrow(unsigned short mask) {
		if (mask!=EPBM_UP && mask!=EPBM_DOWN) {
			int before=src_.GetInt() ;
			UIIntVarField::ProcessArrow(mask) ;
			// recién llegado a slice: solo "slice", todavía no se rebana nada
			if (src_.GetInt()!=before) fresh_=(src_.GetInt()==SM_SLICE)?&src_:0 ;
			return ;
		}
		if (src_.GetInt()!=SM_SLICE) return ;
		if (fresh_==&src_) {   // la primera vez: fades de fábrica y a 2 rebanadas
			fresh_=0 ;
			Variable *fi=inst_->FindVariable(SIP_S_FIN), *fo=inst_->FindVariable(SIP_S_FOUT) ;
			if (fi) fi->SetInt(0) ;
			if (fo) fo->SetInt(0) ;
			by_.SetInt(SSB_REGION) ;
			count_.SetInt(2) ;
			return ;
		}
		int n=count_.GetInt() ;
		int idx=(by_.GetInt()==SSB_MANUAL)?3:(n<=2)?0:(n<=4)?1:2 ;
		idx+=(mask==EPBM_UP)?1:-1 ;
		if (idx<0) idx=0 ;
		if (idx>3) idx=3 ;
		static const int counts[3]={2,4,8} ;
		if (idx==3) by_.SetInt(SSB_MANUAL) ;
		else { by_.SetInt(SSB_REGION) ; count_.SetInt(counts[idx]) ; }
	}
private:
	Variable &by_ ;
	Variable &count_ ;
	I_Instrument *inst_ ;
} ;
Variable *UIModeField::fresh_=0 ;

class UIFreqVarField : public UIIntVarField {
public:
	UIFreqVarField(GUIPoint &p, Variable &v) : UIIntVarField(p,v,"freq: %d",30,22000,1,12) {}
	virtual bool IsLog() { return true ; }
	virtual void Draw(GUIWindow &w,int offset=0) {
		GUITextProperties props ;
		GUIPoint position=GetPosition() ;
		position._y+=offset ;
		if (focus_) {
			((AppWindow&)w).SetColor(CD_HILITE2) ;
			props.invert_=true ;
		} else {
			((AppWindow&)w).SetColor(CD_NORMAL) ;
		}
		char buffer[32] ;
		int hz=src_.GetInt() ;
		if (hz>=10000) sprintf(buffer,"freq: %dkHz",(hz+500)/1000) ;
		else if (hz>=1000) sprintf(buffer,"freq: %d.%dkHz",hz/1000,(hz%1000+50)/100>9?9:(hz%1000+50)/100) ;
		else sprintf(buffer,"freq: %dHz",hz) ;
		drawnLen_=int(strlen(buffer)) ;
		w.DrawString(buffer,position,props) ;
	}
	virtual void ProcessArrow(unsigned short mask) {
		float hz=float(src_.GetInt()) ;
		float f=1.0f ;
		switch (mask) {
			case EPBM_RIGHT: f=1.0594631f ; break ;
			case EPBM_LEFT:  f=1.0f/1.0594631f ; break ;
			case EPBM_UP:    f=2.0f ; break ;
			case EPBM_DOWN:  f=0.5f ; break ;
			default: return ;
		}
		int v=int(hz*f+0.5f) ;
		if (v==src_.GetInt()) v+=(f>1)?1:-1 ;
		if (v<min_) v=min_ ;
		if (v>max_) v=max_ ;
		src_.SetInt(v) ;
	}
} ;

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnFocus();
}

static void KitLoadCallback(View &v, ModalView &dialog) {
    if (dialog.GetReturnCode()==1)
        ((InstrumentView &)v).OnKitLoad(((PresetDialog &)dialog).GetSelected());
    else if (dialog.GetReturnCode()==PD_CLOSED)
        ((InstrumentView &)v).OnBrowserClosed(true);
}

static void PadImportCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnPadImport(dialog.GetReturnCode()==1);
}

#define IV_PRESET_LOAD MAKE_FOURCC('P','R','S','L')
#define IV_PRESET_SAVE MAKE_FOURCC('P','R','S','S')
#define IV_MARK MAKE_FOURCC('M','R','K','C')
// trk2als: campos del editor de kits (no son del instrumento: se copian a él)
#define IV_PADSEL MAKE_FOURCC('P','D','S','L')
#define IV_PADSMP MAKE_FOURCC('P','D','S','M')
#define IV_PADGAIN MAKE_FOURCC('P','D','G','N')
#define IV_PADTUNE MAKE_FOURCC('P','D','T','N')
#define IV_PADPAN MAKE_FOURCC('P','D','P','N')
#define IV_PADDEC MAKE_FOURCC('P','D','D','C')
#define IV_PADCHK MAKE_FOURCC('P','D','C','K')
#define IV_PADST MAKE_FOURCC('P','D','S','T')

static const char *noteNames[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"} ;
static std::string padNoteName(int n) {
	char b[8] ;
	snprintf(b,sizeof(b),"%s%d",noteNames[n%12],n/12-2) ;   // 36 = C1, como en Live
	return b ;
}

// trk2als: campos del kit que se dibujan distinto: el pad (nota y sample),
// el sample del pad (toda la lista del pool), decay (0 = completo) y choke
// trk2als: el largo del golpe de un pad. 0 = todo, que es lo más largo: hacia
// la derecha / arriba se alarga hasta 'todo', hacia la izquierda se acorta
static const int padLongs[]={0,5000,3000,2000,1500,1000,750,500,400,300,250,200,150,100,75,50,40,30,20,10} ;
static int padLongStep(int cur,int steps) {   // steps > 0: más corto
	const int n=sizeof(padLongs)/sizeof(int) ;
	int k=0 ;
	if (cur>0) { k=1 ; while (k<n-1 && padLongs[k]>cur) k++ ; }
	k+=steps ;
	if (k<0) k=0 ;
	if (k>n-1) k=n-1 ;
	return padLongs[k] ;
}
class UILargoField : public UIIntVarField {
public:
	UILargoField(GUIPoint &p,Variable &v) : UIIntVarField(p,v,"LARGO: %dms",0,5000,10,100) {}
	virtual void ProcessArrow(unsigned short mask) {
		int st=(mask==EPBM_LEFT)?1:(mask==EPBM_RIGHT)?-1:(mask==EPBM_DOWN)?3:(mask==EPBM_UP)?-3:0 ;
		if (st) src_.SetInt(padLongStep(src_.GetInt(),st)) ;
	}
} ;

class UIPadField : public UIIntVarField {
public:
	enum Kind { PAD, SAMPLE, DECAY, CHOKE } ;
	UIPadField(GUIPoint &p,Variable &v,Kind k,SampleInstrument *si,int min,int max,int xo,int yo)
		:UIIntVarField(p,v,"",min,max,xo,yo),kind_(k),si_(si) {}
	virtual void Draw(GUIWindow &w,int offset=0) {
		if (kind_==PAD) return ;   // es la rejilla: la dibuja InstrumentView
		GUITextProperties props ;
		GUIPoint position=GetPosition() ;
		position._y+=offset ;
		if (focus_) {
			((AppWindow&)w).SetColor(CD_HILITE2) ;
			props.invert_=true ;
		} else {
			((AppWindow&)w).SetColor(CD_NORMAL) ;
		}
		char buf[64] ;
		int v=src_.GetInt() ;
		switch (kind_) {
			case PAD: {
				int note=si_->KitBase()+v ;
				std::string n=si_->PadName(note) ;
				snprintf(buf,sizeof(buf),"pad %2d: %s %s",v+1,padNoteName(note).c_str(),n.empty()?"(vacío)":n.c_str()) ;
				break ;
			}
			case SAMPLE: {
				SamplePool *sp=SamplePool::GetInstance() ;
				const char *n=(v>=0 && v<sp->GetNameListSize())?sp->GetNameList()[v]:0 ;
				snprintf(buf,sizeof(buf),"sample: %.30s",n?n:"(vacío)") ;
				break ;
			}
			case DECAY:
				if (v<=0) strcpy(buf,"decay: todo") ;
				else snprintf(buf,sizeof(buf),"decay: %dms",v) ;
				break ;
			case CHOKE:
				if (v<=0) strcpy(buf,"choke: no") ;
				else snprintf(buf,sizeof(buf),"choke: %d",v) ;
				break ;
		}
		w.DrawString(buf,position,props) ;
	}
	virtual void ProcessArrow(unsigned short mask) {
		int v=src_.GetInt() ;
		switch (mask) {
			case EPBM_UP: v+=yOffset_ ; break ;
			case EPBM_DOWN: v-=yOffset_ ; break ;
			case EPBM_LEFT: v-=xOffset_ ; break ;
			case EPBM_RIGHT: v+=xOffset_ ; break ;
			default: return ;
		}
		if (kind_==SAMPLE) max_=SamplePool::GetInstance()->GetNameListSize()-1 ;
		if (v<min_) v=min_ ;
		if (v>max_) v=max_ ;
		src_.SetInt(v) ;
	}
private:
	Kind kind_ ;
	SampleInstrument *si_ ;
} ;

static void PresetLoadCallback(View &v, ModalView &dialog) {
	if (dialog.GetReturnCode()==1) {
		((InstrumentView &)v).OnPresetLoad(((PresetDialog &)dialog).GetSelected()) ;
	} else if (dialog.GetReturnCode()==PD_CLOSED) {
		((InstrumentView &)v).OnBrowserClosed(false) ;
	}
}

static void PresetFolderCallback(View &v, ModalView &dialog) {
	if (dialog.GetReturnCode()==1) {
		((InstrumentView &)v).OnPresetFolder(((PresetDialog &)dialog).GetFolder()) ;
	}
}

static void PresetSaveCallback(View &v, ModalView &dialog) {
	if (dialog.GetReturnCode()>0) {
		((InstrumentView &)v).OnPresetSave(((NewProjectDialog &)dialog).GetName()) ;
	}
}

InstrumentView::InstrumentView(GUIWindow &w,ViewData *data):FieldView(w,data),
	presetLoadVar_("preset load",IV_PRESET_LOAD,(int)0,0),
	presetSaveVar_("preset save",IV_PRESET_SAVE,(int)0,0),
	markVar_("mark",IV_MARK,(int)0,0),
	padSelVar_("pad",IV_PADSEL,(int)0,0),
	padSmpVar_("pad sample",IV_PADSMP,(int)-1,0),
	padGainVar_("pad volume",IV_PADGAIN,(int)0,0),
	padTuneVar_("pad tune",IV_PADTUNE,(int)0,0),
	padPanVar_("pad pan",IV_PADPAN,(int)0,0),
	padDecayVar_("pad decay",IV_PADDEC,(int)0,0),
	padChokeVar_("pad choke",IV_PADCHK,(int)0,0),
	padStartVar_("pad start",IV_PADST,(int)0,0) {
	kitMode_=false ;
	padShown_=-1 ;
	padBusy_=false ;
	padImport_=false ;
	padImportPrev_=-1 ;
	padClip_=false ;
	padClipCut_=false ;
	padAPending_=false ;
	kitOwner_=0 ;
	touching_=false ;   // trk2als: automatización
	touchID_=0 ;
	touchCh_=touchClip_=touchLast_=-1 ;
	touchHadOld_=false ;
	laneUndo_=false ;
	preTouchVal_=0 ;
	laneUndoClip_=-1 ;
	laneUndoTime_=0 ;
	selTap_=false ;
	capping_=false ;
	capLast_=-1 ;
	recShown_=0 ;
	markTap_=false ;
	manualPending_=false ;
	kitWfIndex_=-1 ; kitWfSize_=0 ;
	padHoldAt_=0 ; padPlus_=false ; kitLitShown_=0 ; padEdit_=false ; boxMode_=false ;
	drawRep_=0 ; drawRepMask_=0 ;
	waveEdit_=false ;
	waveZoom_=1 ;
	waveSel_=0 ;
	waveAnchor_=0 ;
	waveFlag_=0 ;
	waveStop_=0 ;
	fadeEdit_=false ;
	fadeSel_=0 ;
	envEdit_=false ; filtEdit_=false ;
	envFilter_=false ;
	envSel_=0 ;
	envOffAt_=0 ;
	envCh_=0 ;
	envNote_=60 ;
	tapPending_=-1 ;
	waveGesture_=false ;
	waveAPut_=false ;
	lastAPress_=0 ;
	previewing_=false ;
	previewPos_=-1 ;
	tapPlay_=false ;
	songByTap_=false ;
	slicePlay_=false ;

	project_=data->project_ ;
	lastFocusID_=0 ;
	current_=0 ;
	lastSampleIdx_=-2 ;
	sf2Pending_=false ;
	sfMode_=false ;
	wfSample_=-1 ;
	wfSize_=0 ;
	wfFrom_=wfTo_=0 ;
	simplerView_=false ;
	ampLabelRow_=-1 ;
	playheadPx_=-1 ;
	audioMode_=false ;
	gridMode_=false ;
	page_[0]=page_[1]=0 ;
	pageCount_=0 ;
	pageTap_=0 ;
	labelCount_=0 ;
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
		if (current_==old && lastSampleIdx_!=-2 && idx!=lastSampleIdx_ && idx>=0 && !si->IsKit()) {
			// 0 si no parece un loop: así no arrastra el BPM del sample anterior
			si->FindVariable(SIP_LOOPBPM)->SetInt(si->DetectLoopBpm()) ;
			// el warp con el que arranca (DEFAULTWARP en config.xml; auto: los
			// loops con complex y su bpm, lo demás sin warp)
			if (!audioMode_ && si->FindVariable(SIP_LOOPMODE)->GetInt()!=SILM_LOOPSYNC) {
				int bpm=0 ;
				si->FindVariable(SIP_S_WARP)->SetInt(si->DefaultWarp(&bpm)) ;
				si->FindVariable(SIP_S_BPM)->SetInt(bpm>0?bpm:si->GuessSimplerBpm()) ;
			}
			// las marcas del slice manual eran del sample anterior
			si->SetMarks(std::vector<int>()) ;
			markVar_.SetInt(0) ;
			if (waveEdit_) exitWaveEdit() ;
		}
		lastSampleIdx_=idx ;
		// trk2als: un instrumento de .sf2 elegido de la lista (o de antes) se
		// convierte a zonas, para que tenga ADSR y todo el Sampler. Al elegirlo
		// se espera a soltar A (si no, recorrer la lista convertiría todo)
		SoundSource *src=(idx>=0)?SamplePool::GetInstance()->GetSource(idx):0 ;
		sf2Pending_=(src && src->IsMulti()) ;
		if (sf2Pending_ && current_!=old) convertSf2(false) ;   // abrir uno de antes
	} else {
		lastSampleIdx_=-2 ;
		sf2Pending_=false ;
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
	{   // trk2als: si el primero es solo para mirar, el primero que se edita
		IteratorPtr<UIField> it3(T_SimpleList<UIField>::GetIterator()) ;
		for (it3->Begin();!it3->IsDone();it3->Next())
			if (!it3->CurrentItem().IsStatic()) { SetFocus(&it3->CurrentItem()) ; break ; }
	}
	IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator()) ;
	for (it2->Begin();!it2->IsDone();it2->Next()) {
        if (it2->CurrentItem().IsStatic()) continue ;   // trk2als: etiquetas, no son de variable
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
	gridMode_=false ;

	// trk2als: pantalla dibujada con pinceles: arriba (filas 1-9) la forma de
	// onda; los campos en dos columnas (x=1 y x=21) por bloques con etiqueta.
	// Hay dos variantes:
	//  - loop de una pista de audio: LOOP primero (compases, length, warp);
	//    no hay loop mode, root note, detune ni ajuste fino (ahí no hacen nada)
	//  - pista de tracker: los campos de LGPT de siempre, LOOP al final; bars
	//    y bpm solo en looper sync
	// "church wet / pad" ya no se muestra. Todo sigue guardado en el proyecto.
	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	SampleInstrument *instrument=(SampleInstrument *)instr  ;
	audioMode_=viewData_->project_->IsAudioInstrument(i) ;
	bool sync=instrument->FindVariable(SIP_LOOPMODE)->GetInt()==SILM_LOOPSYNC ;
	const int L=1, R=21 ;
	// el Sampler deja dos renglones más (presets y pestañas de envolvente):
	// la onda es un poco más baja
	int row=(!audioMode_ && !sync)?8:10 ;
	if (!audioMode_ && !sync && instrument->IsKit()) row=2 ;   // kit: el nombre arriba y abajo la rejilla
	labelCount_=0 ;
	Variable *v ;
	UIIntVarField *f1 ;
	GUIPoint pt ;
	bpmInfoPos_=GUIPoint(-1,-1) ;
#define ADD(f) T_SimpleList<UIField>::Insert(f)
#define AT(x) (pt=GUIPoint(x,row))
	// un renglón vacío antes de cada bloque
	int gaps=4 ;
#define LABEL(t) { if (gaps>0) { row++ ; gaps-- ; } \
                   labelRow_[labelCount_]=row ; labelText_[labelCount_++]=t ; row++ ; }

	// el primero de la lista (A en él = vaciar instrumento)
	v=instrument->FindVariable(SIP_SAMPLE) ;
	SamplePool *sp=SamplePool::GetInstance() ;
	kitMode_=!audioMode_ && !sync && instrument->IsKit() ;
	sfMode_=!audioMode_ && !sync && (instrument->IsZoned() || kitMode_) ;
	if (sfMode_) {   // trk2als: soundfont
		char label[48] ;
		bool kit=instrument->IsKit() ;   // trk2als: kit de batería
		const char *kn=instrument->FindVariable(SIP_S_KITNAME)->GetString() ;
		std::string shown=(kit && kn && *kn)?std::string(kn):
		                  (instrument->ZoneCount()?instrument->ZoneSetName():std::string("kit nuevo")) ;
		if (kit && !(kn && *kn) && ownedByTrack())   // kits de antes: el nombre de la pista
			shown=viewData_->project_->GetTrackName(viewData_->songX_) ;
		if (kit) shown="kit: "+shown ;
		snprintf(label,sizeof(label),"%s (%d %s%s)",shown.c_str(),
		         instrument->ZoneCount(),kit?"pad":"zona",instrument->ZoneCount()==1?"":"s") ;
		f1=new UISoundfontField(AT(L),*v,label,v->GetInt(),ownedByTrack()) ;
	} else {
		// los .wav de las zonas de cualquier soundfont del proyecto no salen
		std::vector<bool> hidden(sp->GetNameListSize(),false) ;
		for (int k=0;k<MAX_SAMPLEINSTRUMENT_COUNT;k++) {
			I_Instrument *other=bank->GetInstrument(k) ;
			if (!other || other->GetType()!=IT_SAMPLE) continue ;
			const char *zs=((SampleInstrument *)other)->FindVariable(SIP_S_ZONES)->GetString() ;
			if (!zs || !*zs) continue ;
			std::string all=zs ;
			size_t pos=0 ;
			while (pos<all.size()) {
				size_t end=all.find(';',pos) ;
				if (end==std::string::npos) end=all.size() ;
				std::string item=all.substr(pos,end-pos) ;
				pos=end+1 ;
				int commas=0 ;
				for (size_t c=0;c<item.size();c++) {
					if (item[c]==',' && ++commas==6) {
						int idx=sp->IndexOf(item.substr(c+1).c_str()) ;
						if (idx>=0) hidden[idx]=true ;
						break ;
					}
				}
			}
		}
		f1=new UISampleField(AT(L),*v,sp->GetNameListSize()-1,hidden) ;
	}
	ADD(f1) ;
	f1->SetFocus() ;
	row++ ;

	// Sampler estilo Simpler: el instrumento de las pistas MIDI (salvo los
	// viejos en looper sync, que siguen con la pantalla de LGPT)
	simplerView_=!audioMode_ && !sync ;
	if (simplerView_) {
		instrument->InitSimpler() ;
		int mode=instrument->GetSimpler(SIP_S_MODE) ;
		gaps=4 ;
		if (kitMode_) {
			// trk2als: kit: la rejilla de arriba es el editor (un solo campo:
			// las flechas van de pad en pad); abajo, lo de todo el kit
			loadPadVars() ;
			if (SupportsShapes()) {
				// trk2als: el kit en la rejilla, como el Sampler: los 16 pads y la
				// onda del pad elegido arriba; abajo PAD (lo de ese golpe),
				// FILTRO y KIT (lo de todo el kit). Las flechas recorren pads y
				// cajas como una sola rejilla
				gridMode_=true ;
				T_SimpleList<UIField>::Remove(*f1,true) ;   // el kit se cambia desde la cabecera
				pageCount_=0 ;
				pageName_[pageCount_++]="PAD" ;
				pageName_[pageCount_++]="FILTRO" ;
				pageName_[pageCount_++]="KIT" ;
				int &pg=page_[1] ;
				if (pg<0 || pg>=pageCount_) pg=0 ;
				// PAD: las cajas se ven, pero la cruz solo anda por los pads;
				// Select las desbloquea (y Select otra vez regresa a los pads)
				if (pg!=0) padEdit_=false ;
				if (lastFocusID_==0) lastFocusID_=IV_PADSEL ;
				if (pg==0 && padEdit_ && lastFocusID_==IV_PADSEL) lastFocusID_=IV_PADST ;
				if (pg==0 && !padEdit_) lastFocusID_=IV_PADSEL ;
				const int KX[4]={1,11,21,31}, KY[2]={20,26} ;
				#define KAT(k) (pt=GUIPoint(KX[(k)%4],KY[(k)/4]))
				pt=GUIPoint(1,12) ;
				ADD(new UIPadField(pt,padSelVar_,UIPadField::PAD,instrument,0,15,1,4)) ;
				if (pg==0) {
					ADD(new UIIntVarField(KAT(1),padStartVar_,"INICIO: %d%%",0,99,1,10)) ;
					ADD(new UILargoField(KAT(2),padDecayVar_)) ;
					ADD(new UIIntVarField(KAT(3),padChokeVar_,"CHOKE: %d",0,16,1,1)) ;
					ADD(new UIIntVarField(KAT(4),padTuneVar_,"AFINA: %+dst",-24,24,1,12)) ;
					ADD(new UIIntVarField(KAT(5),padGainVar_,"VOLUMEN: %+ddB",-36,12,1,6)) ;
					ADD(new UIIntVarField(KAT(6),padPanVar_,"PAN: %+d",-50,50,1,10)) ;
				} else if (pg==1) {
					v=instrument->FindVariable(SIP_S_FTYPE) ;
					ADD(new UIIntVarField(KAT(0),*v,"FILTRO: %s",0,2,1,1)) ;
					v=instrument->FindVariable(SIP_S_FHZ) ;
					ADD(new UIFreqVarField(KAT(1),*v)) ;
					v=instrument->FindVariable(SIP_S_FQ) ;
					ADD(new UIIntVarField(KAT(2),*v,"RESO: %d%%",0,100,1,10)) ;
				} else {
					v=instrument->FindVariable(SIP_S_VOL) ;
					ADD(new UIIntVarField(KAT(0),*v,"VOLUMEN: %+ddB",-36,6,1,6)) ;
					v=instrument->FindVariable(SIP_S_TRANSP) ;
					ADD(new UIIntVarField(KAT(1),*v,"AFINA: %+dst",-48,48,1,12)) ;
					v=instrument->FindVariable(SIP_S_PAN) ;
					ADD(new UIIntVarField(KAT(2),*v,"PAN: %+d",-50,50,1,10)) ;
					ADD(new UIIntVarField(KAT(7),presetSaveVar_,"PRESET: guardar",0,0,1,1)) ;
				}
				#undef KAT
				return ;
			}
			if (lastFocusID_==0 || (isPadField(lastFocusID_) && lastFocusID_!=IV_PADSEL)) lastFocusID_=IV_PADSEL ;
			{
				int keep=row ;
				row=7 ;
				ADD(new UIPadField(AT(L),padSelVar_,UIPadField::PAD,instrument,0,15,1,4)) ;
				row=keep ;
			}
			row=15 ;
			gaps=1 ;
			LABEL("KIT") ;
			v=instrument->FindVariable(SIP_S_VOL) ;
			ADD(new UIIntVarField(AT(L),*v,"volume: %+ddB",-36,6,1,6)) ;
			v=instrument->FindVariable(SIP_S_PAN) ;
			ADD(new UIIntVarField(AT(R),*v,"pan: %+d",-50,50,1,10)) ;
			row++ ;
			v=instrument->FindVariable(SIP_S_FTYPE) ;
			ADD(new UIIntVarField(AT(L),*v,"filter: %s",0,2,1,1)) ;
			v=instrument->FindVariable(SIP_S_FHZ) ;
			ADD(new UIFreqVarField(AT(R),*v)) ;
			row++ ;
			v=instrument->FindVariable(SIP_S_FQ) ;
			ADD(new UIIntVarField(AT(L),*v,"res: %d%%",0,100,1,10)) ;
			v=instrument->FindVariable(SIP_S_TRANSP) ;
			ADD(new UIIntVarField(AT(R),*v,"transpose: %+d",-48,48,1,12)) ;
			row++ ;
			ADD(new UIIntVarField(AT(L),presetSaveVar_,"> guardar preset",0,0,1,1)) ;
			row++ ;
			return ;
		}
		if (SupportsShapes()) {
			// trk2als: el Sampler en la rejilla de 8, por páginas (L / R).
			// SONIDO (la región y el tono), WARP, FILTRO (con su envolvente) y
			// AMP. Un multicapa no tiene región ni warp
			gridMode_=true ;
			T_SimpleList<UIField>::Remove(*f1,true) ;   // el sonido: se cambia desde la cabecera
			pageCount_=0 ;
			// SAMPLER (la región, el tono, volumen y pan), FILTRO (con su
			// envolvente) y MÁS (warp, rebanadas, glide, voces, guardar). La
			// envolvente de volumen se edita sobre la onda (Select); solo un
			// multicapa, que no tiene onda editable, conserva AMP
			pageName_[pageCount_++]="SAMPLER" ;
			if (sfMode_) pageName_[pageCount_++]="AMP" ;
			pageName_[pageCount_++]="FILTRO" ;
			if (!sfMode_) pageName_[pageCount_++]="MÁS" ;
			int &pg=page_[1] ;
			if (pg<0 || pg>=pageCount_) pg=0 ;
			const char *pn=pageName_[pg] ;
			bool fpage=!strcmp(pn,"FILTRO") ;
			// la curva de arriba: la del filtro en FILTRO, la de volumen en lo demás
			if (instrument->GetSimpler(SIP_S_ETAB)!=(fpage?1:0))
				instrument->FindVariable(SIP_S_ETAB)->SetInt(fpage?1:0) ;
			bool warped=instrument->GetSimpler(SIP_S_WARP)!=SW_OFF ;
			const int GX[4]={1,11,21,31}, GY[2]={20,26} ;
			#define GAT(k) (pt=GUIPoint(GX[(k)%4],GY[(k)/4]))
			if (!strcmp(pn,"SAMPLER")) {
				int r2=4 ;
				if (!sfMode_) {
					v=instrument->FindVariable(SIP_S_MODE) ;
					ADD(new UIModeField(GAT(0),*v,*instrument->FindVariable(SIP_S_SLICEBY),
					                    *instrument->FindVariable(SIP_S_SLICES),instrument)) ;
					if (mode==SM_CLASSIC) {   // classic no tiene trigger: ahí va su loop
						v=instrument->FindVariable(SIP_S_LOOP) ;
						ADD(new UIIntVarField(GAT(1),*v,"LOOP: %s",0,1,1,1)) ;
					} else {
						v=instrument->FindVariable(SIP_S_TRIG) ;
						ADD(new UIIntVarField(GAT(1),*v,"TRIGGER: %s",0,1,1,1)) ;
					}
					v=instrument->FindVariable(SIP_S_START) ;
					ADD(new UIIntVarField(GAT(2),*v,"INICIO: %d%%",0,99,1,10)) ;
					v=instrument->FindVariable(SIP_S_END) ;
					ADD(new UIIntVarField(GAT(3),*v,"FIN: %d%%",1,100,1,10)) ;
				} else r2=0 ;   // multicapa: el tono arriba
				v=instrument->FindVariable(SIP_S_TRANSP) ;
				ADD(new UIIntVarField(GAT(r2),*v,"TRANSP: %+dst",-48,48,1,12)) ;
				v=instrument->FindVariable(SIP_S_DETUNE) ;
				ADD(new UIIntVarField(GAT(r2+1),*v,"DETUNE: %+dct",-50,50,1,10)) ;
				v=instrument->FindVariable(SIP_S_VOL) ;
				ADD(new UIIntVarField(GAT(r2+2),*v,"VOLUMEN: %+ddB",-36,6,1,6)) ;
				v=instrument->FindVariable(SIP_S_PAN) ;
				ADD(new UIIntVarField(GAT(r2+3),*v,"PAN: %+d",-50,50,1,10)) ;
				if (sfMode_) {
					v=instrument->FindVariable(SIP_S_GLIDE) ;
					ADD(new UIIntVarField(GAT(4),*v,"GLIDE: %dms",0,2000,10,100)) ;
					v=instrument->FindVariable(SIP_S_POLY) ;
					ADD(new UIIntVarField(GAT(5),*v,"VOCES: %d",1,4,1,1)) ;
					ADD(new UIIntVarField(GAT(7),presetSaveVar_,"PRESET: guardar",0,0,1,1)) ;
				}
			} else if (!strcmp(pn,"MÁS")) {
				// cada cosa siempre en su caja: arriba WARP · BPM · GLIDE · VOCES;
				// abajo lo del modo (el largo del loop, o los fades de las
				// rebanadas) y, al final, guardar
				v=instrument->FindVariable(SIP_S_WARP) ;
				ADD(new UIIntVarField(GAT(0),*v,"WARP: %s",0,2,1,1)) ;
				if (warped) {
					if (instrument->GetSimpler(SIP_S_BPM)<=0)
						instrument->FindVariable(SIP_S_BPM)->SetInt(instrument->GuessSimplerBpm()) ;
					v=instrument->FindVariable(SIP_S_BPM) ;
					ADD(new UIIntVarField(GAT(1),*v,"BPM: %d",40,300,1,10)) ;
				}   // sin warp no hay bpm: la caja queda vacía (el cursor no se atora)
				if (mode!=SM_SLICE) {   // VOCES: notas a la vez; con 1 es mono y actúa el glide
					v=instrument->FindVariable(SIP_S_GLIDE) ;
					ADD(new UIIntVarField(GAT(2),*v,"GLIDE: %dms",0,2000,10,100)) ;
					v=instrument->FindVariable(SIP_S_POLY) ;
					ADD(new UIIntVarField(GAT(3),*v,"VOCES: %d",1,4,1,1)) ;
				}
				if (mode==SM_CLASSIC && instrument->GetSimpler(SIP_S_LOOP)) {
					v=instrument->FindVariable(SIP_S_LLEN) ;
					ADD(new UIIntVarField(GAT(4),*v,"LARGO: %d%%",1,100,1,10)) ;
				} else if (mode==SM_SLICE) {   // los fades de las rebanadas (los mismos para todas)
					v=instrument->FindVariable(SIP_S_FIN) ;
					ADD(new UIIntVarField(GAT(4),*v,"FADE IN: %dms",0,2000,5,50)) ;
					v=instrument->FindVariable(SIP_S_FOUT) ;
					ADD(new UIIntVarField(GAT(5),*v,"FADE OUT: %dms",0,2000,5,50)) ;
				}
				v=instrument->FindVariable(SIP_S_SNAP) ;
				ADD(new UIIntVarField(GAT(6),*v,"SNAP: %s",0,1,1,1)) ;
				ADD(new UIIntVarField(GAT(7),presetSaveVar_,"PRESET: guardar",0,0,1,1)) ;
			} else if (fpage) {
				v=instrument->FindVariable(SIP_S_FTYPE) ;
				ADD(new UIIntVarField(GAT(0),*v,"FILTRO: %s",0,2,1,1)) ;
				v=instrument->FindVariable(SIP_S_FHZ) ;
				ADD(new UIFreqVarField(GAT(1),*v)) ;
				v=instrument->FindVariable(SIP_S_FQ) ;
				ADD(new UIIntVarField(GAT(2),*v,"RESO: %d%%",0,100,1,10)) ;
				v=instrument->FindVariable(SIP_S_FST) ;
				ADD(new UIIntVarField(GAT(3),*v,"ENV: %+dst",-72,72,1,12)) ;
				v=instrument->FindVariable(SIP_S_FATK) ;
				ADD(new UIIntVarField(GAT(4),*v,"ATTACK: %dms",0,5000,5,100)) ;
				v=instrument->FindVariable(SIP_S_FDEC) ;
				ADD(new UIIntVarField(GAT(5),*v,"DECAY: %dms",0,10000,10,100)) ;
				v=instrument->FindVariable(SIP_S_FSUS) ;
				ADD(new UIIntVarField(GAT(6),*v,"SUSTAIN: %ddB",-60,0,1,6)) ;
				v=instrument->FindVariable(SIP_S_FREL) ;
				ADD(new UIIntVarField(GAT(7),*v,"RELEASE: %dms",0,10000,10,100)) ;
			} else {   // AMP (solo multicapa)
				v=instrument->FindVariable(SIP_S_ATK) ;
				ADD(new UIIntVarField(GAT(0),*v,"ATTACK: %dms",0,5000,5,100)) ;
				v=instrument->FindVariable(SIP_S_DEC) ;
				ADD(new UIIntVarField(GAT(1),*v,"DECAY: %dms",0,10000,10,100)) ;
				v=instrument->FindVariable(SIP_S_SUS) ;
				ADD(new UIIntVarField(GAT(2),*v,"SUSTAIN: %ddB",-60,0,1,6)) ;
				v=instrument->FindVariable(SIP_S_REL) ;
				ADD(new UIIntVarField(GAT(3),*v,"RELEASE: %dms",0,10000,10,100)) ;
			}
			#undef GAT
			return ;
		}
		if (sfMode_) {
			// soundfont: cada tecla con su sample entero, sin región, slices
			// ni warp (en Live es un Sampler con zonas)
			ADD(new UIStaticField(AT(L),instrument->IsKit()?"mode: kit":"mode: soundfont")) ;
			row++ ;
		}
		if (!sfMode_) {   // mode, warp y PLAYBACK: solo con un sample
		v=instrument->FindVariable(SIP_S_MODE) ;
		ADD(new UIIntVarField(AT(L),*v,"mode: %s",0,2,1,1)) ;
		// warp como en Simpler (también en slice: las rebanadas van a tempo)
		bool warped=instrument->GetSimpler(SIP_S_WARP)!=SW_OFF ;
		v=instrument->FindVariable(SIP_S_WARP) ;
		ADD(new UIIntVarField(AT(R),*v,"warp: %s",0,2,1,1)) ;
		if (warped && instrument->GetSimpler(SIP_S_BPM)<=0) {
			instrument->FindVariable(SIP_S_BPM)->SetInt(instrument->GuessSimplerBpm()) ;
		}
		row++ ;

		LABEL("PLAYBACK") ;
		if (mode==SM_SLICE) {
			// partes iguales o marcas a mano (en manual se editan en la onda)
			ADD(new UISlicesField(AT(L),*instrument->FindVariable(SIP_S_SLICEBY),
			                      *instrument->FindVariable(SIP_S_SLICES))) ;
			v=instrument->FindVariable(SIP_S_TRIG) ;
			ADD(new UIIntVarField(AT(R),*v,"trigger: %s",0,1,1,1)) ;
			row++ ;
		}
		v=instrument->FindVariable(SIP_S_START) ;
		ADD(new UIIntVarField(AT(L),*v,"start: %d%%",0,99,1,10)) ;
		v=instrument->FindVariable(SIP_S_END) ;
		ADD(new UIIntVarField(AT(R),*v,"end: %d%%",1,100,1,10)) ;
		row++ ;
		if (mode==SM_CLASSIC) {
			v=instrument->FindVariable(SIP_S_LOOP) ;
			ADD(new UIIntVarField(AT(L),*v,"loop: %s",0,1,1,1)) ;
			if (instrument->GetSimpler(SIP_S_LOOP)) {
				v=instrument->FindVariable(SIP_S_LLEN) ;
				ADD(new UIIntVarField(AT(R),*v,"loop len: %d%%",1,100,1,10)) ;
			}
			row++ ;
			if (warped) {
				v=instrument->FindVariable(SIP_S_BPM) ;
				ADD(new UIIntVarField(AT(L),*v,"bpm: %d",40,300,1,10)) ;
				row++ ;
			}
		} else if (mode==SM_SLICE) {
			if (warped) {
				v=instrument->FindVariable(SIP_S_BPM) ;
				ADD(new UIIntVarField(AT(L),*v,"bpm: %d",40,300,1,10)) ;
				row++ ;
			}
		} else if (mode==SM_ONESHOT) {
			v=instrument->FindVariable(SIP_S_TRIG) ;
			ADD(new UIIntVarField(AT(L),*v,"trigger: %s",0,1,1,1)) ;
			if (warped) {
				v=instrument->FindVariable(SIP_S_BPM) ;
				ADD(new UIIntVarField(AT(R),*v,"bpm: %d",40,300,1,10)) ;
			}
			row++ ;
		}

		}   // !sfMode_
		LABEL("PITCH") ;
		v=instrument->FindVariable(SIP_S_TRANSP) ;
		ADD(new UIIntVarField(AT(L),*v,"transpose: %+d",-48,48,1,12)) ;
		v=instrument->FindVariable(SIP_S_DETUNE) ;
		ADD(new UIIntVarField(AT(R),*v,"detune: %+dct",-50,50,1,10)) ;
		row++ ;
		v=instrument->FindVariable(SIP_S_GLIDE) ;
		ADD(new UIIntVarField(AT(L),*v,"glide: %dms",0,2000,10,100)) ;
		// 2 voces: la nota anterior termina su cola en vez de cortarse
		v=instrument->FindVariable(SIP_S_POLY) ;
		ADD(new UIIntVarField(AT(R),*v,"voices: %d",1,4,1,1)) ;
		row++ ;

		LABEL("FILTER") ;
		v=instrument->FindVariable(SIP_S_FTYPE) ;
		ADD(new UIIntVarField(AT(L),*v,"type: %s",0,2,1,1)) ;
		v=instrument->FindVariable(SIP_S_FHZ) ;
		ADD(new UIFreqVarField(AT(R),*v)) ;     // pasos de semitono / octava
		row++ ;
		v=instrument->FindVariable(SIP_S_FQ) ;
		ADD(new UIIntVarField(AT(L),*v,"res: %d%%",0,100,1,10)) ;
		row++ ;

		// envolventes como pestañas (como en Simpler): el mismo orden en las
		// dos para que el ADSR quede en su lugar al cambiar
		bool ftab=(instrument->GetSimpler(SIP_S_ETAB)==1) ;
		LABEL(ftab?"FILTER ENVELOPE":"AMP") ;
		ampLabelRow_=row-1 ;
		v=instrument->FindVariable(SIP_S_ETAB) ;
		ADD(new UIIntVarField(AT(L),*v,"envelope: %s",0,1,1,1)) ;
		if (ftab) {
			v=instrument->FindVariable(SIP_S_FST) ;
			ADD(new UIIntVarField(AT(R),*v,"amount: %+dst",-72,72,1,12)) ;
		}
		row++ ;
		if (ftab) {
			v=instrument->FindVariable(SIP_S_FATK) ;
			ADD(new UIIntVarField(AT(L),*v,"attack: %dms",0,5000,5,100)) ;
			v=instrument->FindVariable(SIP_S_FDEC) ;
			ADD(new UIIntVarField(AT(R),*v,"decay: %dms",0,10000,10,100)) ;
			row++ ;
			v=instrument->FindVariable(SIP_S_FSUS) ;
			ADD(new UIIntVarField(AT(L),*v,"sustain: %ddB",-60,0,1,6)) ;
			v=instrument->FindVariable(SIP_S_FREL) ;
			ADD(new UIIntVarField(AT(R),*v,"release: %dms",0,10000,10,100)) ;
			row++ ;
			v=instrument->FindVariable(SIP_S_FKEY) ;
			ADD(new UIIntVarField(AT(L),*v,"key: %d%%",0,100,1,10)) ;
			v=instrument->FindVariable(SIP_S_FVEL) ;
			ADD(new UIIntVarField(AT(R),*v,"velocity: %d%%",0,100,1,10)) ;
			row++ ;
		} else if (mode==SM_ONESHOT) {
			v=instrument->FindVariable(SIP_S_FIN) ;
			ADD(new UIIntVarField(AT(L),*v,"fade in: %dms",0,2000,5,50)) ;
			v=instrument->FindVariable(SIP_S_FOUT) ;
			ADD(new UIIntVarField(AT(R),*v,"fade out: %dms",0,2000,5,50)) ;
			row++ ;
			row++ ;   // en one-shot no hay decay/sustain: el renglón queda vacío
		} else {
			v=instrument->FindVariable(SIP_S_ATK) ;
			ADD(new UIIntVarField(AT(L),*v,"attack: %dms",0,5000,5,100)) ;
			v=instrument->FindVariable(SIP_S_DEC) ;
			ADD(new UIIntVarField(AT(R),*v,"decay: %dms",0,10000,10,100)) ;
			row++ ;
			v=instrument->FindVariable(SIP_S_SUS) ;
			ADD(new UIIntVarField(AT(L),*v,"sustain: %ddB",-60,0,1,6)) ;
			v=instrument->FindVariable(SIP_S_REL) ;
			ADD(new UIIntVarField(AT(R),*v,"release: %dms",0,10000,10,100)) ;
			row++ ;
		}
		if (!ftab) {   // volumen y pan al final, donde la pestaña filter tiene key/velocity
			v=instrument->FindVariable(SIP_S_VOL) ;
			ADD(new UIIntVarField(AT(L),*v,"volume: %+ddB",-36,6,1,6)) ;
			v=instrument->FindVariable(SIP_S_PAN) ;
			ADD(new UIIntVarField(AT(R),*v,"pan: %+d",-50,50,1,10)) ;
			row++ ;
		}
		// presets: A abre la lista o el nombre
		// el instrumento de una pista se cambia desde su cabecera: aquí solo se
		// guarda (en proyectos de antes, sin dueño, también se carga)
		bool owned=false ;
		for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
			if (viewData_->project_->GetTrackInstrument(ch)==i) owned=true ;
		}
		if (owned) {
			ADD(new UIIntVarField(AT(L),presetSaveVar_,"> guardar preset",0,0,1,1)) ;
		} else {
			ADD(new UIIntVarField(AT(L),presetLoadVar_,"> cargar preset",0,0,1,1)) ;
			ADD(new UIIntVarField(AT(R),presetSaveVar_,"> guardar preset",0,0,1,1)) ;
		}
		row++ ;
		return ;
	}

	// trk2als: clip de audio, como la vista de clip de Live: CLIP, WARP y GAIN.
	// Lo de LGPT (crush, filtro, feedback, tabla...) no existe en un clip de
	// Live; el volumen y el pan son de la pista
	gridMode_=false ;
	if (audioMode_ && SupportsShapes()) {
		// trk2als: la rejilla de 8 (2 x 4, como las perillas del MiniLab), en
		// páginas: CLIP (la región, el warp y el gain) y ARCHIVO (su bpm y
		// guardar el preset). El nombre del sample va en la barra de arriba
		instrument->InitAudioClip() ;
		gridMode_=true ;
		pageCount_=0 ;
		pageName_[pageCount_++]="CLIP" ;
		pageName_[pageCount_++]="ARCHIVO" ;
		if (page_[0]<0 || page_[0]>=pageCount_) page_[0]=0 ;
		const int GX[4]={1,11,21,31}, GY[2]={20,26} ;
		#define GAT(k) (pt=GUIPoint(GX[(k)%4],GY[(k)/4]))
		T_SimpleList<UIField>::Remove(*f1,true) ;   // el sample: se cambia desde la Session
		if (page_[0]==0) {
			v=instrument->FindVariable(SIP_FROMBAR) ;
			ADD(new UIIntVarField(GAT(0),*v,"DESDE: %d",1,64,1,4)) ;
			v=instrument->FindVariable(SIP_LOOPBARS) ;
			ADD(new UIIntVarField(GAT(1),*v,"LOOP: %dcomp",1,16,1,4)) ;
			v=instrument->FindVariable(SIP_CLIPBARS) ;
			if (v->GetInt()<=0) v->SetInt(instrument->FindVariable(SIP_LOOPBARS)->GetInt()) ;
			ADD(new UIIntVarField(GAT(2),*v,"LARGO: %dcomp",1,16,1,4)) ;
			v=instrument->FindVariable(SIP_WARP) ;
			ADD(new UIIntVarField(GAT(3),*v,"WARP: %s",0,1,1,1)) ;
			if (instrument->FindVariable(SIP_WARP)->GetInt()==1) {
				v=instrument->FindVariable(SIP_TRANSPOSE) ;
				ADD(new UIIntVarField(GAT(4),*v,"TRANSP: %+dst",-24,24,1,12)) ;
				v=instrument->FindVariable(SIP_A_DET) ;
				ADD(new UIIntVarField(GAT(5),*v,"DETUNE: %+dct",-50,50,1,10)) ;
			} else {   // re-pitch: la altura la decide el tempo (como en Live)
				ADD(new UIStaticField(GAT(4),"TRANSP: --")) ;
				ADD(new UIStaticField(GAT(5),"DETUNE: --")) ;
			}
			v=instrument->FindVariable(SIP_A_GAIN) ;
			ADD(new UIIntVarField(GAT(6),*v,"GAIN: %+ddB",-36,6,1,6)) ;
			v=instrument->FindVariable(SIP_A_FADE) ;
			ADD(new UIIntVarField(GAT(7),*v,"FADE: %s",0,1,1,1)) ;
		} else {
			v=instrument->FindVariable(SIP_LOOPBPM) ;
			ADD(new UIIntVarField(GAT(0),*v,"BPM: %d",0,300,1,10)) ;
			ADD(new UIIntVarField(GAT(3),presetSaveVar_,"PRESET: guardar",0,0,1,1)) ;
		}
		#undef GAT
		return ;
	}
	if (audioMode_) {
		instrument->InitAudioClip() ;
		LABEL("CLIP") ;
		v=instrument->FindVariable(SIP_FROMBAR) ;
		ADD(new UIIntVarField(AT(L),*v,"from bar: %d",1,64,1,4)) ;
		v=instrument->FindVariable(SIP_LOOPBARS) ;
		ADD(new UIIntVarField(AT(R),*v,"loop: %d bars",1,16,1,4)) ;
		row++ ;
		// cuánto dura la celda en la Song (el loop se repite adentro)
		v=instrument->FindVariable(SIP_CLIPBARS) ;
		if (v->GetInt()<=0) v->SetInt(instrument->FindVariable(SIP_LOOPBARS)->GetInt()) ;
		ADD(new UIIntVarField(AT(L),*v,"length: %d bars",1,16,1,4)) ;
		row++ ;

		LABEL("WARP") ;
		bool complex=(instrument->FindVariable(SIP_WARP)->GetInt()==1) ;
		v=instrument->FindVariable(SIP_WARP) ;
		ADD(new UIIntVarField(AT(L),*v,"warp: %s",0,1,1,1)) ;
		v=instrument->FindVariable(SIP_LOOPBPM) ;
		ADD(new UIIntVarField(AT(R),*v,"bpm: %3d",0,300,1,10)) ;
		// complex: "124>120" junto al bpm; re-pitch: cuánto sube o baja la
		// altura por el tempo, en el lugar de detune
		bpmInfoPos_=complex?GUIPoint(R+9,row):GUIPoint(R,row+1) ;
		row++ ;
		if (complex) {
			v=instrument->FindVariable(SIP_TRANSPOSE) ;
			ADD(new UIIntVarField(AT(L),*v,"transpose: %+dst",-24,24,1,12)) ;
			v=instrument->FindVariable(SIP_A_DET) ;
			ADD(new UIIntVarField(AT(R),*v,"detune: %+dct",-50,50,1,10)) ;
		} else {
			// en re-pitch la altura la decide el tempo (como en Live)
			ADD(new UIStaticField(AT(L),"transpose: --")) ;
		}
		row++ ;

		LABEL("GAIN") ;
		v=instrument->FindVariable(SIP_A_GAIN) ;
		ADD(new UIIntVarField(AT(L),*v,"gain: %+ddB",-36,6,1,6)) ;
		v=instrument->FindVariable(SIP_A_FADE) ;
		ADD(new UIIntVarField(AT(R),*v,"fade: %s",0,1,1,1)) ;
		row++ ;
		row++ ;
		// el instrumento de una pista se cambia desde su cabecera: aquí solo se
		// guarda (en proyectos de antes, sin dueño, también se carga)
		bool owned=false ;
		for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
			if (viewData_->project_->GetTrackInstrument(ch)==i) owned=true ;
		}
		if (owned) {
			ADD(new UIIntVarField(AT(L),presetSaveVar_,"> guardar preset",0,0,1,1)) ;
		} else {
			ADD(new UIIntVarField(AT(L),presetLoadVar_,"> cargar preset",0,0,1,1)) ;
			ADD(new UIIntVarField(AT(R),presetSaveVar_,"> guardar preset",0,0,1,1)) ;
		}
		row++ ;
		return ;
	}

	LABEL("SOUND") ;
	v=instrument->FindVariable(SIP_VOLUME) ;
	ADD(new UIIntVarField(AT(L),*v,"volume: %d [%2.2X]",0,255,1,10)) ;
	v=instrument->FindVariable(SIP_PAN) ;
	ADD(new UIIntVarField(AT(R),*v,"pan: %2.2X",0,0xFE,1,0x10)) ;
	row++ ;
	if (!audioMode_) {
		v=instrument->FindVariable(SIP_ROOTNOTE) ;
		ADD(new UINoteVarField(AT(L),*v,"root note: %s",0,0x7F,1,0x0C)) ;
		v=instrument->FindVariable(SIP_FINETUNE) ;
		ADD(new UIIntVarField(AT(R),*v,"detune: %2.2X",0,255,1,0x10)) ;
		row++ ;
	}

	LABEL("COLOR / FILTER") ;
	v=instrument->FindVariable(SIP_CRUSH) ;
	ADD(new UIIntVarField(AT(L),*v,"crush: %d",1,0x10,1,4)) ;
	v=instrument->FindVariable(SIP_CRUSHVOL) ;
	ADD(new UIIntVarField(AT(L+10),*v,"drive: %2.2X",0,0xFF,1,0x10)) ;
	v=instrument->FindVariable(SIP_DOWNSMPL) ;
	ADD(new UIIntVarField(AT(R),*v,"downsample: %d",0,8,1,4)) ;
	row++ ;
	ADD(new UIStaticField(AT(L),"flt cut/res:")) ;
	v=instrument->FindVariable(SIP_FILTCUTOFF) ;
	ADD(new UIIntVarField(AT(L+13),*v,"%2.2X",0,0xFF,1,0x10)) ;
	v=instrument->FindVariable(SIP_FILTRESO) ;
	ADD(new UIIntVarField(AT(L+16),*v,"%2.2X",0,0xFF,1,0x10)) ;
	v=instrument->FindVariable(SIP_FILTMIX) ;
	ADD(new UIIntVarField(AT(R),*v,"type: %2.2X",0,0xFF,1,0x10)) ;
	row++ ;
	v=instrument->FindVariable(SIP_FILTMODE) ;
	ADD(new UIIntVarField(AT(L),*v,"mode: %s",0,2,1,1)) ;
	v=instrument->FindVariable(SIP_ATTENUATE) ;
	ADD(new UIIntVarField(AT(R),*v,"atten: %d [%2.2X]",1,0xFF,1,0x10)) ;
	row++ ;
	ADD(new UIStaticField(AT(L),"fb tune/mix:")) ;
	v=instrument->FindVariable(SIP_FBTUNE) ;
	ADD(new UIIntVarField(AT(L+13),*v,"%2.2X",0,0xFF,1,0x10)) ;
	v=instrument->FindVariable(SIP_FBMIX) ;
	ADD(new UIIntVarField(AT(L+16),*v,"%2.2X",0,0xFF,1,0x10)) ;
	v=instrument->FindVariable(SIP_INTERPOLATION) ;
	ADD(new UIIntVarField(AT(R),*v,"interp: %s",0,1,1,1)) ;
	row++ ;

	if (!audioMode_) {
		LABEL("LOOP") ;
		v=instrument->FindVariable(SIP_LOOPMODE) ;
		ADD(new UIIntVarField(AT(L),*v,"loop mode: %s",0,SILM_LAST-1,1,1)) ;
		v=instrument->FindVariable(SIP_SLICES) ;
		ADD(new UIIntVarField(AT(28),*v,"slices: %2.2X",1,0xFF,1,0x10)) ;
		row++ ;
		if (sync) {
			v=instrument->FindVariable(SIP_LOOPBARS) ;
			ADD(new UIIntVarField(AT(L),*v,"bars: %d",1,16,1,4)) ;
			v=instrument->FindVariable(SIP_LOOPBPM) ;
			ADD(new UIIntVarField(AT(L+10),*v,"bpm: %3d",0,300,1,10)) ;
			bpmInfoPos_=GUIPoint(R,row) ;
			row++ ;
		}
		int last=instrument->GetSampleSize()-1 ;
		v=instrument->FindVariable(SIP_START) ;
		ADD(new UIBigHexVarField(AT(L),*v,7,"start: %7.7X",0,last,16)) ;
		v=instrument->FindVariable(SIP_LOOPSTART) ;
		ADD(new UIBigHexVarField(AT(R),*v,7,"lp start: %7.7X",0,last,16)) ;
		row++ ;
		v=instrument->FindVariable(SIP_END) ;
		ADD(new UIBigHexVarField(AT(L),*v,7,"loop end: %7.7X",0,last,16)) ;
		row++ ;
	}

	LABEL("TABLE") ;
	v=instrument->FindVariable(SIP_TABLEAUTO) ;
	ADD(new UIIntVarField(AT(L),*v,"automation: %s",0,1,1,1)) ;
	// el último de la lista (A en él = quitar la tabla)
	v=instrument->FindVariable(SIP_TABLE) ;
	ADD(new UIIntVarOffField(AT(R),*v,"table: %2.2X",0x00,0x7F,1,0x10)) ;
#undef ADD
#undef AT
#undef LABEL
} ;

void InstrumentView::fillMidiParameters() {
	gridMode_=false ;

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




// trk2als: ¿el instrumento que se ve es el de una pista? Entonces solo se
// cambia desde la cabecera de la pista, en la Song
// trk2als: la pista que toca este instrumento (-1 si ninguna)
static int ownerTrack(ViewData *vd) {
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++)
		if (vd->project_->GetTrackInstrument(ch)==vd->currentInstrument_) return ch ;
	return -1 ;
}

bool InstrumentView::ownedByTrack() {
	Project *p=viewData_->project_ ;
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
		if (p->GetTrackInstrument(ch)==viewData_->currentInstrument_) return true ;
	}
	return false ;
}

void InstrumentView::warpToNext(int offset) {
	if (ownedByTrack()) {
		View::SetNotification("se cambia en la cabecera de la pista") ;
		return ;
	}
	int instrument=viewData_->currentInstrument_+offset ;
	if (instrument>=MAX_INSTRUMENT_COUNT) {
		instrument=instrument-MAX_INSTRUMENT_COUNT ;
	} ;
	if (instrument<0) {
		instrument=MAX_INSTRUMENT_COUNT+instrument ;
	} ;
	viewData_->currentInstrument_=instrument ;
	onInstrumentChange() ;
	autoDraw() ;
	isDirty_=true ;
} ;

static FourCC focusID(FieldView &fv) ;

void InstrumentView::ProcessButtonMask(unsigned short mask,bool pressed) {
	// trk2als: Select tocado solo (sin nada más): de las cajas al dibujo y de
	// regreso, como notas <-> curva en el clip
	if (pressed) selTap_=(mask==EPBM_SELECT) ;
	else if (selTap_ && mask==0) {
		// (Sampler: del dibujo a las cajas, y ahí se queda hasta otro Select)
		selTap_=false ;
		bool was=drawEditing() ;
		toggleDraw() ;
		if (!kitMode_) boxMode_=was ;
		isDirty_=true ;
		return ;
	}
	// trk2als: L o R solos (tocar y soltar, sin nada más): la página de al lado
	if (gridMode_ && pageCount_>1 && !drawEditing()) {
		if (pressed) pageTap_=(mask==EPBM_L)?-1:(mask==EPBM_R)?1:0 ;
		else if (pageTap_ && mask==0) { int d=pageTap_ ; pageTap_=0 ; turnPage(d) ; return ; }
	}
	// en la onda, L / R solos (tocar y soltar): la parada de al lado.
	// One-shot y slice por región: el inicio / el final. Classic: inicio,
	// decay, sustain, final. Slice a mano: de rebanada en rebanada
	if (waveEdit_ && current_ && current_->GetType()==IT_SAMPLE) {
		if (pressed) pageTap_=(mask==EPBM_L)?-1:(mask==EPBM_R)?1:0 ;
		else if (pageTap_ && mask==0) {
			SampleInstrument *si=(SampleInstrument *)current_ ;
			int dir=pageTap_ ;
			pageTap_=0 ;
			waveSel_=0 ;
			isDirty_=true ;
			int smode=si->GetSimpler(SIP_S_MODE) ;
			if (canMark()) {
				int fs=si->RegionPoint(true), fe=si->RegionPoint(false), c=markVar_.GetInt() ;
				std::vector<int> pts, m=si->GetMarks() ;
				pts.push_back(fs) ;
				for (size_t i=0;i<m.size();i++) if (m[i]>fs && m[i]<fe) pts.push_back(m[i]) ;
				pts.push_back(fe) ;
				int to=-1 ;
				for (size_t i=0;i<pts.size();i++) {
					if (dir>0 && pts[i]>c && (to<0 || pts[i]<to)) to=pts[i] ;
					if (dir<0 && pts[i]<c && pts[i]>to) to=pts[i] ;
				}
				if (to>=0) {
					markVar_.SetInt(to) ;
					if (to<fe) { int f,t ; sliceAt(to,f,t) ; startPreview(to,t) ; }   // suena esa rebanada
				}
				return ;
			}
			if (smode==SM_CLASSIC) {
				waveStop_+=dir ;
				if (waveStop_<0) waveStop_=0 ;
				if (waveStop_>3) waveStop_=3 ;
			} else waveStop_=(dir<0)?0:3 ;
			markVar_.SetInt(si->RegionPoint(waveStop_!=3)) ;
			if (smode==SM_ONESHOT) { fadeSel_=(waveStop_==3)?1:0 ; fadeAudition() ; }   // la nota, con sus fades
			else if (smode==SM_CLASSIC && (waveStop_==1 || waveStop_==2)) envAudition() ;
			else { waveFlag_=(waveStop_==3)?2:1 ; waveARelease() ; }   // el sample: desde el inicio, o el último segundo
			return ;
		}
	}
	if (fadeEdit_ && processFadeEdit(mask,pressed)) return ;
	if (!pressed && mask==0 && drawEditing()) {   // soltó todo en un dibujo: termina el tramo
		if (capping_) capEnd() ;
		if (touching_) endTouch() ;
	}
	// cuántas veces seguidas va la misma tecla en un dibujo (mantenerla la
	// repite): donde un gesto no tiene su par grueso, empieza fino y acelera
	if (drawEditing()) {
		if (!pressed) { if (mask==0) { drawRep_=0 ; drawRepMask_=0 ; } }
		else if (mask==drawRepMask_) drawRep_++ ;
		else { drawRepMask_=mask ; drawRep_=0 ; }
	}
	if (filtEdit_ && processFiltEdit(mask,pressed)) return ;
	if (envEdit_ && processEnvEdit(mask,pressed)) return ;
	// trk2als: automatización
	if (pressed && mask==(EPBM_Y|EPBM_START)) { toggleAutoRec() ; return ; }
	// A + gatillo L: la curva de este parámetro en el clip (si no se
	// automatiza, o no hay clip, no pasa nada)
	if (pressed && mask==(EPBM_A|EPBM_L)) { openCurve() ; return ; }
	if (!pressed && touching_ && !(mask&EPBM_A)) endTouch() ;
	if (kitMode_ && padHoldAt_) {   // A sostenida en un pad: salió el +; al soltar, el navegador
		if (pressed && mask!=EPBM_A) { padHoldAt_=0 ; padPlus_=false ; }
		else if (!pressed && !(mask&EPBM_A)) {
			bool plus=padPlus_ ;
			padHoldAt_=0 ; padPlus_=false ;
			isDirty_=true ;
			if (plus) { openPadBrowser() ; return ; }
		}
	}
	if (kitMode_ && padAPending_) {   // A tocada sola en un pad vacío (sin flechas)
		if (pressed && mask!=EPBM_A) padAPending_=false ;
		else if (!pressed && !(mask&EPBM_A)) {
			padAPending_=false ;
			openPadBrowser() ;
			return ;
		}
	}
	if (pressed && kitMode_ && processKit(mask)) { kitTrack() ; return ; }
	// A + flechas en un parámetro automatizado: se toma desde lo que suena
	if (pressed && (mask&EPBM_A) && !(mask&EPBM_B) && !touching_) {   // para deshacer: cómo estaba la perilla
		FourCC id=focusID(*this) ;
		Variable *v=(current_ && id) ? current_->FindVariable(id) : 0 ;
		preTouchVal_=v ? v->GetInt() : 0 ;
	}
	if (pressed && (mask&EPBM_A) && (mask&(EPBM_LEFT|EPBM_RIGHT|EPBM_UP|EPBM_DOWN)) && !(mask&EPBM_B))
		takeOver() ;
	if (!pressed && capping_ && !(mask&EPBM_A)) capEnd() ;
	// trk2als: en las cajas del Sampler, cada cambio de valor suena (la nota,
	// un segundo), salvo que el clip de la pista ya esté sonando con notas
	Variable *audV=0 ;
	int audBefore=0 ;
	if (pressed && (mask&EPBM_A) && !(mask&EPBM_B) && gridMode_ && simplerView_ && !kitMode_ && !audioMode_ &&
	    GetFocus() && !GetFocus()->IsStatic() && current_ && current_->GetType()==IT_SAMPLE) {
		audV=&((UIIntVarField *)GetFocus())->GetVariable() ;
		audBefore=audV->GetInt() ;
		if (audV->GetID()==presetSaveVar_.GetID()) audV=0 ;
	}
	int audID=audV?audV->GetID():0 ;
	processButtonMask(mask,pressed) ;
	if (audID && current_ && current_->GetType()==IT_SAMPLE) {
		Variable *v=current_->FindVariable(audID) ;   // (la pantalla pudo rearmarse)
		if (v && v->GetInt()!=audBefore && !waveEdit_) envAudition() ;
	}
	// sin grabar: lo que se mueve con A + flechas se anota para la captura
	if (pressed && (mask&EPBM_A) && (mask&(EPBM_LEFT|EPBM_RIGHT|EPBM_UP|EPBM_DOWN)) && !(mask&EPBM_B) &&
	    !Player::GetInstance()->IsAutoRec()) {
		if (!capping_) capBegin() ;
		if (capping_) capTick() ;
	}
	if (pressed) { kitSync() ; kitTrack() ; }
	// A sostenido en un parámetro mientras se graba: se escribe la lane
	if (pressed && (mask&EPBM_A) && !(mask&EPBM_B) && Player::GetInstance()->IsAutoRec()) {
		if (!touching_) beginTouch() ;
		if (touching_) recordTouch() ;
	}
}

void InstrumentView::processButtonMask(unsigned short mask,bool pressed) {

	// trk2als: slice manual: con "slice by: manual" se edita la onda
	// (processWaveEdit). Se entra al elegir manual o con A sobre "manual"
	if (waveEdit_) {
		if (processWaveEdit(mask,pressed)) return ;
	}
	bool onSliceBy=GetFocus() && !GetFocus()->IsStatic() &&
	            ((UIIntVarField *)GetFocus())->GetVariableID()==SIP_S_SLICEBY &&
	            current_ && current_->GetType()==IT_SAMPLE &&
	            ((SampleInstrument *)current_)->GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL ;
	if (!pressed) {
		if (sf2Pending_ && !(mask&EPBM_A)) {   // trk2als: soltó A sobre un .sf2
			convertSf2(true) ;
			return ;
		}
		if (markTap_ && !(mask&EPBM_A)) {
			markTap_=false ;
			enterWaveEdit(false) ;
			isDirty_=true ;
		}
		// soltó A habiendo llegado a manual (y sigue ahí): a la onda, sin tocar nada
		if (manualPending_ && !(mask&EPBM_A)) {
			manualPending_=false ;
			if (current_ && current_->GetType()==IT_SAMPLE &&
			    ((SampleInstrument *)current_)->GetSimpler(SIP_S_MODE)==SM_SLICE &&
			    ((SampleInstrument *)current_)->GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL) {
				if (gridMode_ && page_[1]!=0) { page_[1]=0 ; lastFocusID_=0 ; onInstrumentChange() ; }
				enterWaveEdit(false) ;
			}
			isDirty_=true ;
		}
		return ;
	}
	markTap_=onSliceBy && mask==EPBM_A ;

	isDirty_=false ;

	// trk2als: en los campos de presets, A abre la ventana; lo demás, igual
	if (GetFocus() && !GetFocus()->IsStatic()) {
		FourCC fid=((UIIntVarField *)GetFocus())->GetVariableID() ;
		if (fid==IV_PRESET_LOAD || fid==IV_PRESET_SAVE) {
			viewMode_=VM_NORMAL ;
			if (mask==EPBM_A) {
				if (fid==IV_PRESET_LOAD && ownedByTrack()) {
					View::SetNotification("se carga en la cabecera de la pista") ;
				} else if (fid==IV_PRESET_LOAD) openPresetLoad() ; else openPresetSave() ;
				return ;
			}
		}
	}

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

						// trk2als: el navegador nuevo (la samplelib, con pre-escucha)
						 PresetDialog *pd=new PresetDialog(*this) ;
						 pd->SetPick(sfMode_?PresetDialog::PICK_SF2:PresetDialog::PICK_ANY,
						             viewData_->currentInstrument_,v.GetInt()) ;
                         DoModal(pd, ImportSampleDialogCallback);
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
        if ((mask&EPBM_A)&&(mask&EPBM_SELECT)) {
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
		mask&=(0xFFFF-(EPBM_A|EPBM_SELECT)) ;
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
	int sliceByBefore=(current_ && current_->GetType()==IT_SAMPLE)?current_->FindVariable(SIP_S_SLICEBY)->GetInt():-1 ;
	FieldView::ProcessButtonMask(mask) ;
	if (audioMode_ && (editedID==SIP_FROMBAR || editedID==SIP_LOOPBARS) &&
	    current_->FindVariable(editedID)->GetInt()!=editedBefore) {
		Variable *len=current_->FindVariable(SIP_CLIPBARS) ;
		int lenBefore=len->GetInt() ;
		((SampleInstrument *)current_)->ApplyBarRegion() ;
		// si la celda duraba lo mismo que el loop, sigue durando lo mismo
		if (editedID==SIP_LOOPBARS && lenBefore==editedBefore) {
			len->SetInt(current_->FindVariable(SIP_LOOPBARS)->GetInt()) ;
		}
		isDirty_=true ;
	}
	// cambiar el warp muestra u oculta 'transpose'
	// ... y el loop mode (en pistas de tracker) muestra u oculta bars y bpm
	bool toManual=(sliceByBefore>=0 && sliceByBefore!=SSB_MANUAL && current_ && current_->GetType()==IT_SAMPLE &&
	     current_->FindVariable(SIP_S_SLICEBY)->GetInt()==SSB_MANUAL &&
	     current_->FindVariable(SIP_S_MODE)->GetInt()==SM_SLICE) ;
	if (editedID==SIP_S_MODE) isDirty_=true ;   // (el número de rebanadas se ve en su caja)
	if ((editedID==SIP_WARP || editedID==SIP_LOOPMODE || editedID==SIP_S_MODE ||
	     editedID==SIP_S_LOOP || editedID==SIP_S_WARP || editedID==SIP_S_ETAB ||
	     editedID==SIP_S_SLICEBY) &&
	    current_->FindVariable(editedID)->GetInt()!=editedBefore) {
		lastFocusID_=editedID ;
		onInstrumentChange() ;
		isDirty_=true ;
	}
	if (toManual) { markTap_=false ; manualPending_=true ; isDirty_=true ; }   // al soltar A, a la onda

    Player *player=Player::GetInstance() ;
	// B Modifier

    if (mask & EPBM_B) {
        if (mask&EPBM_LEFT) warpToNext(-1) ;
		if (mask&EPBM_RIGHT) warpToNext(+1);
		if (mask&EPBM_DOWN) warpToNext(-16) ;
		if (mask&EPBM_UP) warpToNext(+16);
		if (mask&EPBM_A) { // Allow cut instrument
		   // trk2als: solo sobre el campo del sample (con cajas ya no existe: la
		   // primera caja de la página es un parámetro más, y B+A sobre ella
		   // vaciaba el instrumento entero)
		   bool onSample=!gridMode_ && GetFocus() && !GetFocus()->IsStatic() &&
		       ((UIIntVarField *)GetFocus())->GetVariableID()==SIP_SAMPLE ;
		   if (getInstrumentType()==IT_SAMPLE) {
                if (onSample) {
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
		   bool onTable=!GetFocus()->IsStatic() &&
		       ((UIIntVarField *)GetFocus())->GetVariableID()==SIP_TABLE ;
		   if (onTable) {
	            int i=viewData_->currentInstrument_ ;
	            InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	            I_Instrument *instr=bank->GetInstrument(i) ;
                Variable *v=instr->FindVariable(SIP_TABLE) ;
                v->SetInt(-1) ;
                isDirty_=true ;
		   } ;

		   // trk2als: B+A en cualquier otro campo lo regresa a su valor por defecto
		   if (!onSample && !onTable && !GetFocus()->IsStatic()) {
				// trk2als: si está automatizado, B+A quita la automatización
				if (!deleteLane()) resetFocusedField() ;
		   }
        }
        if (mask&EPBM_SELECT) {
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
                    ViewType vt = viewData_->instrumentFromSong_ ? VT_SONG : VT_CLIP;
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
                // No modifier: como en la Session (trk2als)
                if (mask & EPBM_START) {
                    player->EditorStart(viewData_->songX_, viewData_->songY_);
                }
            }
        }
    }

    UIIntVarField *field = (UIIntVarField *)GetFocus();
    if (field) {
	   lastFocusID_=field->GetVariableID() ;
    }

} ;

/************************************************************
 trk2als: regresar el campo con el foco a su valor por defecto (B+A)
 ************************************************************/

void InstrumentView::resetFocusedField() {
	UIIntVarField *field=(UIIntVarField *)GetFocus() ;
	Variable &v=field->GetVariable() ;
	FourCC id=v.GetID() ;
	if (current_->GetType()==IT_SAMPLE) {
		SampleInstrument *si=(SampleInstrument *)current_ ;
		switch (id) {
			case SIP_LOOPBPM:       // el bpm se vuelve a detectar
				v.SetInt(si->DetectLoopBpm()) ;
				isDirty_=true ;
				return ;
			case SIP_LOOPBARS: {    // de 'from bar' hasta el final del sample
				float total=si->GetSampleBars() ;
				if (total>0) {
					v.SetInt(int(total+0.5f)-si->FindVariable(SIP_FROMBAR)->GetInt()+1) ;
					si->ApplyBarRegion() ;
					isDirty_=true ;
					return ;
				}
				break ;
			}
			case SIP_END:           // el final del sample, no 0
				v.SetInt(si->GetSampleSize()) ;
				isDirty_=true ;
				return ;
			default:
				break ;
		}
	}
	v.Reset() ;
	v.SetInt(v.GetInt()) ;          // Reset() no avisa; así se entera quien escucha
	if (current_->GetType()==IT_SAMPLE && (id==SIP_FROMBAR || id==SIP_LOOPBARS)) {
		((SampleInstrument *)current_)->ApplyBarRegion() ;
	}
	isDirty_=true ;
}

void InstrumentView::DrawView() {

	Clear() ;
	ClearShapes() ; // trk2als
    View::EnableNotification();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    // Draw title

    char title[20];
    SetColor(CD_NORMAL);
    sprintf(title, simplerView_ ? "Sampler %2.2X" : (audioMode_ ? "Clip %2.2X" : "Instrument %2.2X"),
            viewData_->currentInstrument_);
    if (!SupportsShapes()) DrawString(pos._x, pos._y, title, props);

    // Draw fields
    // trk2als: lo automatizado se ve con el valor que va sonando
    std::vector<Variable *> tmpVar ;
    std::vector<int> tmpOld ;
    autoShown_.clear() ;
    if (current_ && current_->GetType()==IT_SAMPLE && Player::GetInstance()->IsRunning()) {
        SampleInstrument *si=(SampleInstrument *)current_ ;
        IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
        for (it->Begin();!it->IsDone();it->Next()) {
            UIField &f=it->CurrentItem() ;
            if (f.IsStatic()) continue ;
            Variable &v=((UIIntVarField &)f).GetVariable() ;
            int av ;
            if (!automatable(v.GetID()) || !si->GetAutomated(v.GetID(),av)) continue ;
            if (touching_ && v.GetID()==touchID_) continue ;   // lo que se está moviendo
            tmpVar.push_back(&v) ;
            tmpOld.push_back(v.GetInt()) ;
            v.SetInt(av) ;
            autoShown_.push_back(av) ;
        }
    }
    if (gridMode_) {
        if (GetFocus()==0) SetFocus(T_SimpleList<UIField>::GetFirst()) ;
        drawGrid() ;
    } else FieldView::Redraw();
    for (size_t k=0;k<tmpVar.size();k++) tmpVar[k]->SetInt(tmpOld[k]) ;
    recShown_=Player::GetInstance()->AutoRecState() ;
    if (tapPending_>=0) recShown_=0 ;   // precuenta para rebanar: no graba nada
    if (recShown_==1) recShown_=(System::GetInstance()->GetClock()/250)%2 ? 1 : 3 ;
    drawMap() ;

    // trk2als: cuánto se acelera o frena el loop para ir al tempo del proyecto
    if (current_ && current_->GetType()==IT_SAMPLE) {
        SampleInstrument *si=(SampleInstrument *)current_ ;
        int bpm=si->FindVariable(SIP_LOOPBPM)->GetInt() ;
        int bars=si->FindVariable(SIP_LOOPBARS)->GetInt() ;
        float secs=si->GetLoopSeconds() ;
        bool sync=bpmInfoPos_._x>=0 && (audioMode_ ||
                  si->FindVariable(SIP_LOOPMODE)->GetInt()==SILM_LOOPSYNC) ;
        if (sync && !gridMode_ && bpm>0 && secs>0 && bars>0) {
            int tempo=viewData_->project_->GetTempo() ;
            float ratio=secs/(bars*240.0f/tempo) ; // >1 = se acelera
            char info[32] ;
            if (audioMode_ && si->FindVariable(SIP_WARP)->GetInt()==1) {
                sprintf(info,"%d>%d",bpm,tempo) ;   // complex: el tono no cambia
            } else {
                sprintf(info,"%d>%d %+.1fst",bpm,tempo,12.0f*log2f(ratio)) ;
            }
            SetColor(CD_HILITE1) ;
            DrawString(bpmInfoPos_._x,bpmInfoPos_._y,info,props) ;
            SetColor(CD_NORMAL) ;
        }
    }
    drawWaveform() ; // trk2als
} ;

/************************************************************
 trk2als: la rejilla de 8 cajas, en páginas (como las 8 perillas del
 MiniLab). Arriba las pestañas (L / R); cada caja dice solo dos cosas: el
 nombre y el valor (el número grande, su unidad al lado). Una rayita abajo
 dice cuánto va. El cursor: marco; moviéndolo (A apretada): en negativo
 ************************************************************/

void InstrumentView::drawArc(int cx,int cy,int r,float from,float to,GUIColor c,int th) {
	// sin primitiva de arco: puntitos cada ~4 px a lo largo del aro
	float len=(to-from)*3.14159f/180.0f*r ;
	int n=int(len/3.0f)+1 ;
	for (int i=0;i<=n;i++) {
		float a=(from+(to-from)*i/(n?n:1))*3.14159f/180.0f ;
		int x=cx+int(cosf(a)*r+0.5f), y=cy+int(sinf(a)*r+0.5f) ;
		FillRect(x-th/2,y-th/2,th,th,c,th/2) ;
	}
}

static const int GRID_TABS_Y=250, GRID_Y=284, GRID_W=150, GRID_H=92, GRID_GAP=4, GRID_X=8 ;

int InstrumentView::gridSlot(UIField &f) {
	GUIPoint p=f.GetPosition() ;
	int col=(p._x-1)/10 ;
	if (col<0) col=0 ;
	if (col>3) col=3 ;
	return col+((p._y>=26)?4:0) ;
}

void InstrumentView::gridBox(int k,int &x,int &y,int &w,int &h) {
	w=GRID_W ;
	h=GRID_H ;
	x=GRID_X+(k%4)*(GRID_W+GRID_GAP) ;
	y=GRID_Y+(k/4)*(GRID_H+GRID_GAP) ;
}

void InstrumentView::turnPage(int dir) {
	int kind=audioMode_?0:1 ;
	int slot=GetFocus()?gridSlot(*GetFocus()):0 ;
	page_[kind]=(page_[kind]+dir+pageCount_)%pageCount_ ;
	lastFocusID_=0 ;
	onInstrumentChange() ;
	// el cursor se queda en la misma caja (si en esta página hay algo ahí)
	UIField *best=0 ;
	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		UIField &f=it->CurrentItem() ;
		if (f.IsStatic()) continue ;
		if (!best) best=&f ;
		if (gridSlot(f)==slot) { best=&f ; break ; }
	}
	if (best) { ClearFocus() ; SetFocus(best) ; }
	UIField *f=GetFocus() ;
	if (f && !f->IsStatic()) lastFocusID_=((UIIntVarField *)f)->GetVariableID() ;
	isDirty_=true ;
}

void InstrumentView::drawGrid() {
	GUIColor bg=ColorOf(CD_BACKGROUND), fg=ColorOf(CD_NORMAL) ;
	GUIColor card=bg, dim=Mix(bg,fg,50) ;   // sin cajita: del color del fondo
	bool aHeld=(View::physMask_&EPBM_A)!=0 ;
	UIField *focus=GetFocus() ;
	// dibujando: el cursor sobre una bandera enciende su caja (INICIO / FIN)
	FourCC flagID=0 ;
	if (waveEdit_ && current_ && current_->GetType()==IT_SAMPLE) {
		int ra,rb ;
		if (((SampleInstrument *)current_)->SimplerRegion(ra,rb)) {
			int c=markVar_.GetInt() ;
			SampleInstrument *fsi=(SampleInstrument *)current_ ;
			(void)ra ; (void)rb ;
			if (!canMark()) flagID=(waveStop_==0)?SIP_S_START:(waveStop_==3)?(FourCC)SIP_S_END:0 ;
			else if (waveFlag_==1 || c==fsi->RegionPoint(true)) flagID=SIP_S_START ;
			else if (waveFlag_==2 || c==fsi->RegionPoint(false)) flagID=SIP_S_END ;
		}
	}

	// las pestañas
	int kind=audioMode_?0:1 ;
	int tx=10 ;
	for (int i=0;i<pageCount_;i++) {
		int nl=0 ;
		for (const char *q=pageName_[i];*q;q++) if ((*q&0xC0)!=0x80) nl++ ;
		int w=16*nl+24 ;
		// (en el dibujo: la pestaña donde se está sigue encendida; las otras, apagadas)
		if (drawEditing() && i!=page_[kind]) DrawText(tx+12,GRID_TABS_Y+6,pageName_[i],Mix(bg,fg,18),1) ;
		else if (i==page_[kind]) {
			FillRect(tx,GRID_TABS_Y,w,28,fg,14) ;
			DrawText(tx+12,GRID_TABS_Y+6,pageName_[i],bg,1) ;
			// la puntita del globo: hacia dónde va lo que se mueve. Arriba, el
			// dibujo (o los pads del kit); abajo, las cajas
			bool up=drawEditing() ;
			if (kitMode_) {
				UIField *kf=GetFocus() ;
				up=kf && !kf->IsStatic() && ((UIIntVarField *)kf)->GetVariableID()==IV_PADSEL ;
			}
			int px=tx+w/2 ;
			for (int q=0;q<8;q++) {
				int tw=2*q+2 ;
				if (up) FillRect(px-tw/2,GRID_TABS_Y-8+q,tw,1,fg) ;
				else FillRect(px-tw/2,GRID_TABS_Y+28+(7-q),tw,1,fg) ;
			}
		} else DrawText(tx+12,GRID_TABS_Y+6,pageName_[i],dim,1) ;
		tx+=w+8 ;
	}
	if (pageCount_>1 && !drawEditing()) DrawText(640-10-16*3,GRID_TABS_Y+6,"L·R",dim,1) ;
	bool used[8]={false,false,false,false,false,false,false,false} ;
	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		UIField &f=it->CurrentItem() ;
		if (f.GetPosition()._y<20) continue ;   // (los pads del kit: los dibuja drawKit)
		int k=gridSlot(f) ;
		used[k]=true ;
		int cx,cy,cw,ch ;
		gridBox(k,cx,cy,cw,ch) ;
		char text[96] ;
		if (f.IsStatic()) snprintf(text,sizeof(text),"%s",((UIStaticField &)f).Text()) ;
		else ((UIIntVarField &)f).Text(text,sizeof(text)) ;
		std::string all(text), name, value ;
		size_t c=all.find(": ") ;
		if (c==std::string::npos) { name=all ; value="" ; }
		else { name=all.substr(0,c) ; value=all.substr(c+2) ; }
		// los que se dibujan a su manera
		if (!f.IsStatic()) {
			UIIntVarField &vf=(UIIntVarField &)f ;
			FourCC id=vf.GetVariableID() ;
			int iv=vf.GetVariable().GetInt() ;
			char b[24] ;
			if (id==SIP_S_FHZ) {
				name="FREQ" ;
				if (iv>=10000) snprintf(b,sizeof(b),"%dkHz",(iv+500)/1000) ;
				else if (iv>=1000) snprintf(b,sizeof(b),"%d.%dkHz",iv/1000,(iv%1000)/100) ;
				else snprintf(b,sizeof(b),"%dHz",iv) ;
				value=b ;
			} else if (id==SIP_S_SLICEBY) {
				name="SLICES" ;
				if (iv==SSB_MANUAL) value="manual" ;
				else { snprintf(b,sizeof(b),"%d",current_->FindVariable(SIP_S_SLICES)->GetInt()) ; value=b ; }
			} else if (id==SIP_S_MODE && iv==SM_SLICE) {   // cómo se rebana (A + ↑ ↓)
				if (UIModeField::fresh_==current_->FindVariable(SIP_S_MODE)) value="slice" ;   // recién llegado: aún no se elige
				else if (current_->FindVariable(SIP_S_SLICEBY)->GetInt()==SSB_MANUAL) value="manual" ;   // (slice, a mano: no cabe más)
				else { snprintf(b,sizeof(b),"slice %d",current_->FindVariable(SIP_S_SLICES)->GetInt()) ; value=b ; }
			} else if ((id==SIP_S_PAN || id==IV_PADPAN) && iv==0) value="C" ;
			else if (id==IV_PADDEC && iv==0) value="todo" ;
			else if (id==IV_PADCHK && iv==0) value="--" ;
			else if (id==SIP_S_GLIDE && current_->FindVariable(SIP_S_POLY)->GetInt()>1) value="--" ;   // el glide es del mono
			else if (id==SIP_S_GLIDE && iv==0) value="off" ;
			// un segundo o más: en segundos, así el número cabe grande
			if (value.size()>2 && value.compare(value.size()-2,2,"ms")==0) {
				int ms=atoi(value.c_str()) ;
				if (ms>=1000) {
					snprintf(b,sizeof(b),"%d.%ds",ms/1000,(ms%1000)/100) ;
					value=b ;
				}
			}
		}
		bool flagBox=flagID && !f.IsStatic() && ((UIIntVarField &)f).GetVariableID()==flagID ;
		if (fadeEdit_) flagBox=!f.IsStatic() && ((UIIntVarField &)f).GetVariableID()==(FourCC)(fadeSel_?SIP_S_FOUT:SIP_S_FIN) ;
		if (envEdit_) flagBox=!f.IsStatic() && ((UIIntVarField &)f).GetVariableID()==envID(envSel_) ;
		if (filtEdit_) flagBox=!f.IsStatic() && ((UIIntVarField &)f).GetVariableID()==filtBox_ ;
		bool here=drawEditing()?flagBox:(&f==focus) ;
		bool moving=here && aHeld && !f.IsStatic() ;
		GUIColor box=moving?fg:card, ink=moving?bg:fg, soft=Mix(box,ink,55) ;
		bool off=false ;
		if (drawEditing()) {   // en el dibujo: solo lo que ahí se mueve sigue vivo
			FourCC bid=f.IsStatic()?0:((UIIntVarField &)f).GetVariableID() ;
			if (waveEdit_) off=(bid!=SIP_S_START && bid!=SIP_S_END) ;
			else if (fadeEdit_) off=(bid!=SIP_S_FIN && bid!=SIP_S_FOUT) ;
			else if (filtEdit_) off=!(bid==SIP_S_FTYPE || bid==SIP_S_FHZ || bid==SIP_S_FQ) ;
			else off=!(bid==envID(0) || bid==envID(1) || bid==envID(2) || bid==envID(3)) ;
			if (off) { ink=Mix(bg,fg,45) ; soft=Mix(bg,fg,32) ; }   // se leen, pero en gris (como las del kit)
		}
		// kit, pestaña PAD sin desbloquear (Select): se leen, pero en gris
		if (kitMode_ && page_[1]==0 && !padEdit_ && !moving) { ink=Mix(bg,fg,45) ; soft=Mix(bg,fg,32) ; }
		if (moving) FillRect(cx,cy,cw,ch,box,7) ;
		if (here && !moving) FrameRect(cx,cy,cw,ch,fg,7,3) ;
		DrawText(cx+12,cy+10,name.c_str(),soft,1) ;
		if (value.empty()) continue ;
		// el número (con signo y punto) grande; lo que sigue, su unidad
		size_t n=0 ;
		while (n<value.size() && (isdigit((unsigned char)value[n]) || value[n]=='+' || value[n]=='-' || value[n]=='.')) n++ ;
		bool number=false ;   // al menos un dígito ('--' es palabra)
		for (size_t q=0;q<n;q++) if (isdigit((unsigned char)value[q])) number=true ;
		GUIColor vc=f.IsStatic()?soft:ink ;
		if (number) {
			std::string num=value.substr(0,n), unit=value.substr(n) ;
			if (24*int(num.size())+16*int(unit.size())+20>cw) {   // (la letra de 32 avanza 24)   // no cabe: todo a 16
				DrawText(cx+12,cy+42,value.c_str(),vc,1) ;
			} else {
				DrawText(cx+12,cy+32,num.c_str(),vc,2) ;
				if (!unit.empty()) DrawText(cx+16+24*int(num.size()),cy+48,unit.c_str(),vc,1) ;
			}
		} else if (24*int(value.size())+24<=cw) {   // palabra corta (sí, no, LP, C, --): grande
			DrawText(cx+12,cy+32,value.c_str(),vc,2) ;
		} else {
			DrawText(cx+12,cy+42,value.c_str(),vc,1) ;
		}
		// la rayita: cuánto va (solo los números con rango)
		if (!f.IsStatic() && number) {
			UIIntVarField &vf=(UIIntVarField &)f ;
			if (vf.GetVariable().GetType()==Variable::INT && vf.Max()>vf.Min()) {
				float fr=float(vf.GetVariable().GetInt()-vf.Min())/float(vf.Max()-vf.Min()) ;
				if (vf.GetVariableID()==SIP_S_FHZ)   // la frecuencia, en octavas
					fr=logf(float(vf.GetVariable().GetInt())/vf.Min())/logf(float(vf.Max())/vf.Min()) ;
				if (fr<0) fr=0 ;
				if (fr>1) fr=1 ;
				int bx=cx+12, bw=cw-24, by=cy+ch-12 ;
				FillRect(bx,by,bw,5,off?Mix(bg,fg,8):Mix(box,ink,20),2) ;
				if (fr>0) FillRect(bx,by,int(bw*fr)<5?5:int(bw*fr),5,ink,2) ;
			}
		}
	}
	(void)used ;
}

/************************************************************
 trk2als: forma de onda con los pinceles, a todo lo ancho (filas 1-9).
 Compases numerados (si hay bpm), la región del loop en naranja con su marco,
 rayitas de cada tiempo dentro de la región y una línea verde donde arranca.
 También dibuja las etiquetas de los bloques de campos.
 ************************************************************/

void InstrumentView::drawWaveform() {
	topInfo_.clear() ;
	helpText_=0 ;
	drawWaveformBody() ;
	// trk2als: las barras de los pinceles, encima de todo
	std::string ctx ;
	if (ownedByTrack()) {
		ctx=viewData_->project_->GetTrackName(viewData_->songX_) ;
	} else {
		char t[20] ;
		sprintf(t,simplerView_ ? "Sampler %2.2X" : (audioMode_ ? "Clip %2.2X" : "Instrument %2.2X"),
		        viewData_->currentInstrument_) ;
		ctx=t ;
	}
	if (gridMode_ && current_ && current_->GetType()==IT_SAMPLE) {
		// trk2als: en la rejilla, arriba solo el nombre del sonido (sin la extensión)
		SampleInstrument *si=(SampleInstrument *)current_ ;
		std::string nm ;
		if (kitMode_) nm=topInfo_ ;   // el sonido del pad donde está la cruz
		else if (sfMode_ && si->ZoneCount()) nm=si->ZoneSetName() ;
		else {
			int idx=si->GetSampleIndex() ;
			SamplePool *sp=SamplePool::GetInstance() ;
			if (idx>=0 && idx<sp->GetNameListSize()) nm=sp->GetNameList()[idx] ;
			size_t dot=nm.rfind('.') ;
			if (dot!=std::string::npos && dot+5>=nm.size()) nm=nm.substr(0,dot) ;
		}
		if (!nm.empty()) ctx=nm ;
	} else if (!topInfo_.empty()) ctx+="  "+topInfo_ ;
	drawTopBar(TAB_INST,ctx.c_str()) ;
	if (gridMode_) {
		// trk2als: la ayuda (L2 + R2) de la rejilla: lo que hacen los botones
		// donde se está ahora. Atajos de hasta 7 letras, para que quepa el texto
		const char *gh ;
		bool rec=Player::GetInstance()->AutoRecState()!=0 ;
		if (kitMode_) {
			UIField *kf=GetFocus() ;
			bool onPad=kf && !kf->IsStatic() && ((UIIntVarField *)kf)->GetVariableID()==IV_PADSEL ;
			if (onPad) gh=(page_[1]==0)
				? "flechas:de pad en pad;A:tocar el pad;A larga:cambiar su sonido;A+^v:volumen del pad;A+<>:largo del pad;B:copiar el pad;B+A:cortarlo;SEL+A:pegarlo;SEL:las cajas del pad;L/R:otra pestaña;SEL+<>:otra pantalla"
				: "flechas:de pad en pad;A:tocar el pad;A larga:cambiar su sonido;A+^v:volumen del pad;A+<>:largo del pad;B:copiar el pad;B+A:cortarlo;SEL+A:pegarlo;v:a las cajas;L/R:otra pestaña;SEL+<>:otra pantalla" ;
			else gh=(page_[1]==0)
				? "flechas:mover la cruz;A+<>:cambia el valor;A+^v:lo cambia de a mucho;Y+<>^v:otro pad, misma caja;SEL:de vuelta a los pads;L/R:otra pestaña;SEL+<>:otra pantalla"
				: "flechas:mover la cruz;A+<>:cambia el valor;A+^v:lo cambia de a mucho;Y+<>^v:otro pad;^:de vuelta a los pads;L/R:otra pestaña;SEL+<>:otra pantalla" ;
		} else if (waveEdit_ && canMark()) {
			gh="<>:mover el cursor;L/R:de rebanada en rebanada;A:pone o agarra la marca;A+<>:mueve la marca (suena);B+A:borra la marca;SEL,B:marcar varias;B+L/R:inicio / fin aquí;B+^v:zoom;START:tocar todo;L+START:rebanar al vuelo;^v:otro dibujo;SEL:a las cajas;SEL+L/R:deshacer / rehacer" ;
		} else if (waveEdit_) {
			gh="L/R:elige la bandera;<>:mueve la bandera;A+<>:fade o envolvente;A+^v:sustain;B+<>:salto grande;B+^v:zoom;B+A:bandera a su lugar;^v:otro dibujo;START:tocar;R2:capturar lo movido;SEL:a las cajas;SEL+L/R:deshacer / rehacer;SEL+<>:otra pantalla" ;
		} else if (filtEdit_) {
			gh=rec ? "<>:corte (se graba);A+<>:corte, fino;A+^v:resonancia;L/R:tipo: LP, BP, HP;^v:otro dibujo;Y+START:dejar de grabar;SEL:a las cajas"
			       : "<>:corte;A+<>:corte, fino;A+^v:resonancia;L/R:tipo: LP, BP, HP;^v:otro dibujo;START:tocar;R2:capturar lo movido;Y+START:grabar;SEL:a las cajas;SEL+<>:otra pantalla" ;
		} else if (envEdit_) {
			gh="L/R:elige la parte;<>:su tiempo;A+<>:su tiempo, fino;A+^v:sustain;^v:otro dibujo;START:tocar;R2:capturar lo movido;Y+START:grabar;SEL:a las cajas;SEL+<>:otra pantalla" ;
		} else {
			gh=rec ? "flechas:mover la cruz;A+<>:mantenida, se graba;L/R:otra pestaña;Y+START:dejar de grabar;SEL:al dibujo"
			       : "flechas:mover la cruz;A+<>:cambia el valor;A+^v:lo cambia de a mucho;L/R:otra pestaña;A+L:su curva en el clip;B+A:de fábrica / sin curva;R2:capturar lo movido;Y+START:grabar;SEL:al dibujo;SEL+<>:otra pantalla" ;
		}
		drawHelpBar(gh) ;
		drawAutoMarks() ;
		return ;
	}
	const char *help=helpText_?helpText_:"A+<>:valor;A+^v:valor x16;SEL+<>:pantalla" ;
	if (kitMode_ && GetFocus() && !GetFocus()->IsStatic() &&
	    isPadField(((UIIntVarField *)GetFocus())->GetVariableID()))
		help="A:oír;AA:sample;X:vol/decay;Y:tono/pan;L:inicio/choke" ;
	if (kitMode_ && GetFocus() && !GetFocus()->IsStatic() &&
	    ((UIIntVarField *)GetFocus())->GetVariableID()==SIP_SAMPLE)
		help="A:cambiar de kit;B+A:vaciar el instrumento" ;
	if (!helpText_ && GetFocus() && !GetFocus()->IsStatic() &&
	    automatable(((UIIntVarField *)GetFocus())->GetVariableID()))
		help=Player::GetInstance()->AutoRecState() ? "A sostenido+<>:grabar;Y+START:dejar de grabar"
		                                        : "A+<>:valor;R2:capturar;Y+START:grabar;B+A:borrarla" ;
	drawHelpBar(help) ;
	drawAutoMarks() ;   // trk2als
}

void InstrumentView::drawWaveformBody() {
	if (!SupportsShapes() || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;

	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi=ColorOf(CD_HILITE1) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	GUIColor dim=Mix(bg,fg,27) ;
	GUIColor dim2=Mix(bg,fg,55) ;

	// etiquetas de los bloques
	for (int i=0;i<labelCount_;i++) {
		DrawText(16,labelRow_[i]*16+4,labelText_[i],dim2,0) ;
	}

	const int X=16, Y=gridMode_?48:(kitMode_?56:34), W=WF_BARS*8,
	          H=gridMode_?186:(kitMode_?144:(simplerView_?80:112)) ;
	if (!(kitMode_ && gridMode_)) FillRect(X-8,Y-4,W+16,H+8,Mix(bg,fg,9),8) ;
	if (gridMode_ && drawEditing()) FrameRect(X-8,Y-4,W+16,H+8,fg,8,3) ;   // dibujando: el marco pasa aquí

	if (kitMode_) {   // trk2als: kit: la rejilla de pads
		if (gridMode_) drawKit(si) ;
		else drawPadGrid(si,X,Y,W,H) ;
		return ;
	}
	if (sfMode_ && si->IsZoned()) {   // trk2als: soundfont: el mapa de zonas
		if (!gridMode_) { drawZoneMap(si,X,Y,W,H) ; return ; }
		// en la rejilla: la onda de la zona que suena (la de la última nota)
		// con su envolvente, y abajo la tira de zonas
		const std::vector<ZoneSource::Zone> &zones=si->Zones() ;
		int last=si->LastNote() ;
		if (last<0) last=60 ;
		int cur=-1, best=1000 ;
		for (size_t i=0;i<zones.size();i++) {
			int d=(last<zones[i].lo)?zones[i].lo-last:(last>zones[i].hi)?last-zones[i].hi:0 ;
			if (d<best) { best=d ; cur=int(i) ; }
		}
		const int ZH=34 ;
		drawZoneStrip(si,X,Y+H-ZH,W,ZH,cur) ;
		if (cur<0 || !zones[cur].src) return ;
		SoundSource *zs=zones[cur].src ;
		int size=zs->GetSize(-1) ;
		short *data=(short *)zs->GetSampleBuffer(-1) ;
		int channels=zs->GetChannelCount(-1) ;
		if (size<=0 || !data || channels<1) return ;
		int key=-1000-cur ;
		if (key!=wfSample_ || size!=wfSize_ || wfFrom_!=0 || wfTo_!=size) {
			for (int b=0;b<WF_BARS;b++) {
				int from=int((long long)size*b/WF_BARS), to=int((long long)size*(b+1)/WF_BARS) ;
				int step=(to-from)/200+1, peak=0 ;
				for (int i=from;i<to;i+=step) { int v=data[i*channels] ; if (v<0) v=-v ; if (v>peak) peak=v ; }
				wfPeaks_[b]=(unsigned char)(peak>>7) ;
			}
			wfSample_=key ; wfSize_=size ; wfFrom_=0 ; wfTo_=size ;
		}
		drawSimplerWave(si,X,Y,W,H-ZH-8,size,zs) ;
		return ;
	}

	int index=si->GetSampleIndex() ;
	if (index<0) return ;
	SoundSource *src=SamplePool::GetInstance()->GetSource(index) ;
	if (!src) return ;
	int size=src->GetSize(-1) ;
	short *data=(short *)src->GetSampleBuffer(-1) ;
	int channels=src->GetChannelCount(-1) ;
	if (size<=0 || !data || channels<1) return ;

	// trk2als: en el Sampler la onda hace zoom a la región start..end (con
	// un margen a cada lado) para que se vea lo que suena
	int vFrom=0, vTo=size ;
	if (simplerView_) {
		int a=si->RegionPoint(true) ;
		int b=si->RegionPoint(false) ;
		if (b<=a) b=a+1 ;
		int margin=(b-a)/10 ;
		vFrom=a-margin ;
		vTo=b+margin ;
		int minLen=src->GetSampleRate(-1)/20 ;        // al menos 50 ms a la vista
		if (vTo-vFrom<minLen) { int c=(a+b)/2 ; vFrom=c-minLen/2 ; vTo=c+minLen/2 ; }
		if (vFrom<0) { vTo-=vFrom ; vFrom=0 ; }
		if (vTo>size) { vFrom-=vTo-size ; vTo=size ; if (vFrom<0) vFrom=0 ; }
		if (vTo-vFrom>size*8/10) { vFrom=0 ; vTo=size ; } // casi todo: la onda completa
		if (waveEdit_) { vFrom=0 ; vTo=size ; }   // dibujando: el sample entero, para elegir inicio y fin
		// editando marcas: zoom alrededor del cursor
		if (waveEdit_ && waveZoom_>1) {
			int len=(vTo-vFrom)/waveZoom_ ;
			if (len<64) len=64 ;
			int c=markVar_.GetInt() ;
			vFrom=c-len/2 ;
			if (vFrom<0) vFrom=0 ;
			vTo=vFrom+len ;
			if (vTo>size) { vTo=size ; vFrom=(size-len>0)?size-len:0 ; }
		}
	}
	if (index!=wfSample_ || size!=wfSize_ || vFrom!=wfFrom_ || vTo!=wfTo_) {
		for (int b=0;b<WF_BARS;b++) {
			int from=vFrom+int((long long)(vTo-vFrom)*b/WF_BARS) ;
			int to=vFrom+int((long long)(vTo-vFrom)*(b+1)/WF_BARS) ;
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
		wfFrom_=vFrom ;
		wfTo_=vTo ;
	}

	if (simplerView_) {
		drawSimplerWave(si,X,Y,W,H,size) ;
		return ;
	}

	int st=si->FindVariable(SIP_START)->GetInt() ;
	int ls=si->FindVariable(SIP_LOOPSTART)->GetInt() ;
	int le=si->FindVariable(SIP_END)->GetInt() ;
	if (le<=0 || le>size) le=size ;
	if (ls<0 || ls>=le) ls=0 ;
	int s0=X+int((long long)ls*W/size) ;
	int s1=X+int((long long)le*W/size) ;
	if (s1<s0+2) s1=s0+2 ;
	int sx=X+int((long long)st*W/size) ;
	// en una pista de tracker: one-shot = suena de start a end, sin región
	// de loop; los compases solo con looper sync
	int lm=si->FindVariable(SIP_LOOPMODE)->GetInt() ;
	bool oneShot=(!audioMode_ && lm==SILM_ONESHOT) ;
	bool showBars=(audioMode_ || lm==SILM_LOOPSYNC) ;
	int sound0=oneShot?sx:((!audioMode_ && sx<s0)?sx:s0) ;   // desde dónde suena

	// duración y compases arriba a la derecha
	float bar=showBars?si->GetBarSamples():0 ;
	int rate=src->GetSampleRate(-1) ;
	if (rate>0) {
		char info[32] ;
		int cs=int((long long)size*100/rate) ;
		if (bar>0) {
			sprintf(info,"%d.%02ds  %d bars",cs/100,cs%100,int(size/bar+0.5f)) ;
		} else {
			sprintf(info,"%d.%02ds",cs/100,cs%100) ;
		}
		topInfo_=info ;
	}

	// región del loop (fondo)
	if (!oneShot) FillRect(s0,Y-4,s1-s0,H+8,Mix(bg,hi,20),8) ;

	// barras de la onda
	int mid=Y+20+(H-20)/2 ;
	for (int b=0;b<WF_BARS;b++) {
		int c=X+b*8+4 ;
		bool inLoop=(c>=sound0 && c<s1) ;
		int h=(wfPeaks_[b]*(H-30))/255 ;
		if (h<4) h=4 ;
		FillRect(c-3,mid-h/2,5,h,inLoop?hi:dim,2) ;
	}

	// compases: líneas, números y tiempos dentro de la región
	if (bar>0) {
		float px=bar*W/size ;                     // ancho de un compás en pixeles
		int total=int(size/bar+0.999f) ;
		int every=1 ;                             // numerar sin amontonar
		while (px*every<(gridMode_?44:22)) every*=2 ;
		for (int i=0;i<total;i++) {
			int x=X+int(i*px) ;
			if (x>=X+W) break ;
			if (i>0) FillRect(x,Y+18,1,H-20,Mix(bg,fg,28)) ;
			if (i%every==0) {
				char num[8] ;
				sprintf(num,"%d",i+1) ;
				int pos=int(i*bar) ;
				bool sel=(pos>=ls && pos<le) ;
				DrawText(x+5,gridMode_?Y+H-22:Y+1,num,sel?hi2:dim2,gridMode_?1:0) ;
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

	if (!oneShot) FrameRect(s0,Y-4,s1-s0,H+8,hi,8,2) ;
	// dónde arranca el sample (en las pistas de audio siempre es loop start)
	if (!audioMode_ && (oneShot || st!=ls)) {
		FillRect(sx-1,Y-4,2,H+8,hi2) ;
		DrawText(sx+5,Y+1,"start",hi2,0) ;
	}
	// cursor de reproducción: la línea verde sigue a la voz que suena
	playheadPx_=-1 ;
	int pos=si->GetPlayPosition() ;
	if (pos>=0 && pos<=size) {
		playheadPx_=X+int((long long)pos*W/size) ;
		FillRect(playheadPx_-1,Y-4,3,H+8,ColorOf(CD_PLAY),1) ;
	}
}

// trk2als: onda del Sampler: start/end, región del loop (classic), rebanadas
// (slice), la envolvente junto a la etiqueta AMP y el cursor de reproducción
void InstrumentView::drawSimplerWave(SampleInstrument *si,int X,int Y,int W,int H,int size,SoundSource *zsrc) {
	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi=ColorOf(CD_HILITE1) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	GUIColor dim=Mix(bg,fg,22) ;
	GUIColor dim2=Mix(bg,fg,55) ;
	GUIColor ghost=Mix(bg,hi,28) ;
	GUIColor panel=Mix(bg,fg,9) ;
	int mode=si->GetSimpler(SIP_S_MODE) ;
	// cuadro del sample -> px, con el zoom de la onda
	int vLen=(wfTo_>wfFrom_)?wfTo_-wfFrom_:size ;
	#define FPX(f) (X+int((long long)((f)-wfFrom_)*W/vLen))
	int a=FPX((long long)(zsrc?0:si->RegionPoint(true))) ;
	int b=FPX((long long)(zsrc?size:si->RegionPoint(false))) ;
	if (b<=a) b=a+2 ;
	// con zoom: abajo, una barrita con la parte del sample que se ve
	if (vLen<size) {
		int sy=Y+H+1 ;
		FillRect(X,sy,W,2,Mix(bg,fg,18)) ;
		FillRect(X+int((long long)wfFrom_*W/size),sy,int((long long)vLen*W/size)+2,2,dim2) ;
	}

	SoundSource *src=zsrc?zsrc:SamplePool::GetInstance()->GetSource(si->GetSampleIndex()) ;
	int rate=src?src->GetSampleRate(-1):0 ;
	bool warped=!zsrc && (si->GetSimpler(SIP_S_WARP)!=SW_OFF) ;
	if (rate>0) {
		char info[48] ;
		int cs=int((long long)size*100/rate) ;
		sprintf(info,"%d.%02ds",cs/100,cs%100) ;
		// con warp: cuántos compases dura la región a su tempo
		int bpm=si->GetSimpler(SIP_S_BPM) ;
		if (warped && bpm>0) {
			float bars=float(size)*(si->GetSimpler(SIP_S_END)-si->GetSimpler(SIP_S_START))/100.0f/rate*bpm/240.0f ;
			int b10=int(bars*10+0.5f) ;
			sprintf(info+strlen(info)," %d.%d bar%s",b10/10,b10%10,b10==10?"":"s") ;
		}
		strcat(info," a C3") ;   // la envolvente dibujada es la de la nota original
		if (!waveEdit_) topInfo_=info ;
	}

	bool loop=(mode==SM_CLASSIC && si->GetSimpler(SIP_S_LOOP)) ;
	int l0=b-int((long long)size*si->GetSimpler(SIP_S_LLEN)/100*W/vLen) ;
	if (l0<a) l0=a ;
	if (loop) FillRect(l0,Y-4,b-l0,H+8,Mix(bg,hi,16),8) ;
	// slice: los bordes de cada rebanada en px (partes iguales o marcas)
	std::vector<int> slPx ;
	if (mode==SM_SLICE) {
		std::vector<int> bounds ;
		si->SliceBounds(bounds) ;
		for (size_t i=0;i<bounds.size();i++) slPx.push_back(FPX(bounds[i])) ;
		if (slPx.size()>=2) { slPx.front()=a ; slPx.back()=b ; }
	}
	int nSl=(slPx.size()>=2)?int(slPx.size())-1:1 ;
	if (slPx.size()<2) {             // sin región todavía: un solo borde a..b
		slPx.clear() ;
		slPx.push_back(a) ;
		slPx.push_back(b) ;
	}
	// slice: la rebanada que está sonando, iluminada
	if (mode==SM_SLICE && nSl>0 && slPx.size()>=2) {
		int pos=si->GetPlayPosition() ;
		if (pos>=0) {
			int px=FPX(pos) ;
			for (int k=0;k<nSl;k++) {
				if (px>=slPx[k] && px<slPx[k+1]) {
					FillRect(slPx[k],Y-4,slPx[k+1]-slPx[k],H+8,Mix(bg,hi,22),4) ;
					break ;
				}
			}
		}
	}

	// --- la envolvente de volumen sobre la onda, calculada a C3 ---
	// px -> milisegundos: cuadros del sample por ms de salida (con warp, al
	// tempo del proyecto)
	float framesPerMs=rate/1000.0f ;
	if (warped && rate>0) {
		int bpm=si->GetSimpler(SIP_S_BPM) ;
		float tick=SyncMaster::GetInstance()->GetTickSampleCount() ;
		if (bpm>0 && tick>0) framesPerMs=(rate*60.0f/bpm/(24.0f*tick))*44.1f ;
	}
	float msPerPx=(framesPerMs>0)?float(vLen)/W/framesPerMs:0 ;
	bool oneShot=!zsrc && (mode==SM_ONESHOT || mode==SM_SLICE) ;   // fades (como Simpler)
	float atk=float(oneShot?si->GetSimpler(SIP_S_FIN):si->GetSimpler(SIP_S_ATK)) ;
	float dec=oneShot?0:float(si->GetSimpler(SIP_S_DEC)) ;
	float rel=float(si->GetSimpler(oneShot?SIP_S_FOUT:SIP_S_REL)) ;
	float sus=1.0f ;
	if (!oneShot) {
		int db=si->GetSimpler(SIP_S_SUS) ;
		sus=(db<=-60)?0:powf(10.0f,db/20.0f) ;
	}
	// ganancia en el px x (dentro de la región; en slice, cada rebanada empieza
	// su propia envolvente)
	#define ENV_AT(xx,out) { \
		int s0=a, s1=b ; \
		if (nSl>1) { for (int kk=0;kk<nSl;kk++) if ((xx)>=slPx[kk] && (xx)<slPx[kk+1]) \
		             { s0=slPx[kk] ; s1=slPx[kk+1] ; break ; } } \
		float t=((xx)-s0)*msPerPx ; float gg ; \
		if (t<atk) gg=(atk<1)?1:t/atk ; \
		else if (t<atk+dec) gg=1.0f-(1.0f-sus)*(t-atk)/dec ; \
		else gg=sus ; \
		if (oneShot && rel>=1) { float left=(s1-(xx))*msPerPx ; if (left<rel) gg*=left/rel ; } \
		out=(gg<0)?0:(gg>1?1:gg) ; }

	int mid=Y+12+(H-12)/2 ;
	for (int k=0;k<WF_BARS;k++) {
		int c=X+k*8+4 ;
		int h=(wfPeaks_[k]*(H-24))/255 ;
		if (h<4) h=4 ;
		if (c>=a && c<b) {
			float g ;
			ENV_AT(c,g)
			FillRect(c-3,mid-h/2,5,h,ghost,2) ;          // la onda original, tenue
			int he=int(h*g) ;
			if (he>=2) FillRect(c-3,mid-he/2,5,he,hi,2) ; // lo que suena
		} else {
			FillRect(c-3,mid-h/2,5,h,dim,2) ;
		}
	}
	// pestaña filter: la curva es la envolvente del corte (en otro color) y
	// arriba, de dónde a dónde se mueve
	bool ftab=(si->GetSimpler(SIP_S_ETAB)==1) ;
	if (ftab) {
		atk=float(si->GetSimpler(SIP_S_FATK)) ;
		dec=float(si->GetSimpler(SIP_S_FDEC)) ;
		rel=float(si->GetSimpler(SIP_S_FREL)) ;
		int db=si->GetSimpler(SIP_S_FSUS) ;
		sus=(db<=-60)?0:powf(10.0f,db/20.0f) ;
		oneShot=false ;   // la del filtro siempre es ADSR
		hi2=fg ;
		int hz=si->GetSimpler(SIP_S_FHZ) ;
		int amt=si->GetSimpler(SIP_S_FST) ;
		int top=int(hz*powf(2.0f,amt/12.0f)) ;
		if (top>20000) top=20000 ;
		if (top<20) top=20 ;
		char txt[48], fa[12], fb[12] ;
		#define HZTXT(buf,v) { if ((v)>=1000) sprintf(buf,"%d.%dk",(v)/1000,((v)%1000)/100) ; else sprintf(buf,"%d",(v)) ; }
		HZTXT(fa,hz)
		HZTXT(fb,top)
		#undef HZTXT
		sprintf(txt,"corte %sHz > %sHz",fa,fb) ;
		if (!gridMode_) DrawText(a+16,Y+1,txt,fg,0) ;
	}
	// la curva encima de las barras
	if (gridMode_) {
		// trk2als: como las curvas de automatización: un trazo continuo sobre
		// el sonido, con sus puntos; el tramo que se edita, más grueso
		GUIColor cc=TrackColor(viewData_->songX_) ;
		int top=Y+8, hh=H-18 ;
		std::vector<int> ys ;
		for (int x=a;x<b;x++) {
			float g ;
			ENV_AT(x,g)
			ys.push_back(top+int((1.0f-g)*hh)) ;
		}
		// el release después del final (empieza al soltar la nota)
		bool tail=(mode==SM_CLASSIC || ftab || zsrc) && rel>=1 && msPerPx>0 && sus>0 ;
		int tl=0 ;
		if (tail) {
			tl=int(rel/msPerPx) ;
			if (tl>X+W-b) tl=X+W-b ;
			for (int x=0;x<tl;x++) {
				float g=sus*(1.0f-float(x)/(rel/msPerPx)) ;
				ys.push_back(top+int((1.0f-(g<0?0:g))*hh)) ;
			}
		}
		// los quiebres: fin del attack, fin del decay, el final (suelta)
		int xa=(msPerPx>0)?a+int(atk/msPerPx):a, xd=(msPerPx>0)?xa+int(dec/msPerPx):a ;
		if (xa>b) xa=b ;
		if (xd>b) xd=b ;
		FourCC fid=focusID(*this) ;
		if (fadeEdit_) fid=fadeSel_?SIP_S_FOUT:SIP_S_FIN ;
		if (waveEdit_ && !zsrc && !ftab) fid=canMark()?0:waveStopID() ;   // en la onda: el tramo de la parada
		if (envEdit_) fid=envID(envSel_) ;
		int h0=-1, h1=-1 ;
		if (fid==SIP_S_ATK || fid==SIP_S_FATK || fid==SIP_S_FIN) { h0=a ; h1=xa ; }
		else if (fid==SIP_S_DEC || fid==SIP_S_FDEC) { h0=xa ; h1=xd ; }
		else if (fid==SIP_S_SUS || fid==SIP_S_FSUS) { h0=xd ; h1=b ; }
		else if (fid==SIP_S_REL || fid==SIP_S_FREL || fid==SIP_S_FOUT) { h0=oneShot?b-int(msPerPx>0?rel/msPerPx:0):b ; h1=b+tl ; }
		if (filtEdit_) drawFilterCurve(si,X,Y,W,H,cc) ;
		else if (!(mode==SM_SLICE && !ftab && !zsrc)) drawCurveLine(ys,a,cc,h0,h1,X,X+W) ;
		int nodes[3]={xa,xd,b} ;
		int nn=(oneShot || filtEdit_)?0:3 ;
		for (int k=0;k<nn;k++) {
			int nx=nodes[k] ;
			if (nx<a || nx-a>=int(ys.size()) || nx<X || nx>X+W) continue ;
			int ny=ys[nx-a] ;
			FillRect(nx-10,ny-10,20,20,cc,10) ;
			FillRect(nx-5,ny-5,10,10,bg,5) ;
		}
	} else
	for (int x=a;x<b;x+=3) {
		float g ;
		ENV_AT(x,g)
		FillRect(x,Y+6+int((1.0f-g)*(H-14)),3,2,hi2) ;
	}
	// classic (o la del filtro): el release después del final (empieza
	// cuando sueltas la nota)
	if (!gridMode_ && (mode==SM_CLASSIC || ftab) && rel>=1 && msPerPx>0 && sus>0) {
		int len=int(rel/msPerPx) ;
		if (len>X+W-b) len=X+W-b ;
		for (int x=0;x<len;x+=4) {
			float g=sus*(1.0f-float(x)/(rel/msPerPx)) ;
			FillRect(b+x,Y+6+int((1.0f-g)*(H-14)),2,2,Mix(panel,hi2,int(90*(1.0f-float(x)/len)))) ;
		}
	}
	#undef ENV_AT

	hi2=ColorOf(CD_HILITE2) ;
	// banderas: start y end apuntan hacia la parte que suena
	// dibujando: la bandera donde está el cursor (L / R) se prende: llena, más
	// grande y con la línea gruesa; la otra queda hueca
	int selFlag=0 ;
	if (waveEdit_ && !zsrc) {
		int wc=markVar_.GetInt() ;
		if (!canMark()) selFlag=(waveStop_==0)?1:(waveStop_==3)?2:0 ;   // la parada elegida
		else if (waveFlag_==1 || wc==si->RegionPoint(true)) selFlag=1 ;
		else if (waveFlag_==2 || wc==si->RegionPoint(false)) selFlag=2 ;
	}
	GUIColor fpanel=Mix(bg,fg,9) ;
	#define FLAG(xx,dir) { bool fsel=(selFlag==((dir)>0?1:2)) ; bool hollow=waveEdit_ && !fsel ; \
		int fh=fsel?28:(gridMode_?20:10), lw=fsel?6:2 ; \
		FillRect((xx)-lw/2,Y-4,lw,H+8,hi2) ; \
		for (int q=0;q<fh;q++) { int w=(q<fh/2)?2*q+3:2*(fh-q)+1 ; \
			FillRect((dir)>0?(xx):(xx)-w,Y-4+q,w,1,hi2) ; \
			if (hollow && w>8 && q>2 && q<fh-3) FillRect((dir)>0?(xx)+2:(xx)-w+4,Y-4+q,w-6,1,fpanel) ; } }
	if (mode==SM_SLICE) {
		int lastName=-1000 ;
		for (int k=0;k<nSl;k++) {
			int x=slPx[k] ;
			FillRect(x-2,Y-4,4,H+8,hi2) ;
			if (x-lastName>=(gridMode_?56:30)) {   // nombres sin amontonar
				lastName=x ;
				char num[8] ;
				// la nota que dispara la rebanada (C1 = la primera, como en Simpler)
				static const char *names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"} ;
				int note=36+k ;
				sprintf(num,"%s%d",names[note%12],note/12-2) ;
				int cw=gridMode_?16:8 ;
				FillRect(x+2,Y,cw*int(strlen(num))+4,gridMode_?20:13,bg) ;   // legible sobre la envolvente
				DrawText(x+4,Y+1,num,hi2,gridMode_?1:0) ;
			}
		}
		FillRect(b-1,Y-4,2,H+8,hi2) ;
		if (selFlag==1) FillRect(a-3,Y-4,6,H+8,hi2) ;   // el inicio / el fin elegido (L / R)
		if (selFlag==2) FillRect(b-3,Y-4,6,H+8,hi2) ;
		// slice manual, editando: las marcas seleccionadas y el cursor
		if (waveEdit_ && si->GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL) {
			int ca,cb ;
			si->SimplerRegion(ca,cb) ;
			std::vector<int> m=si->GetMarks() ;
			int cpos=markVar_.GetInt() ;   // (el cursor puede estar fuera de la región)
			int near=nearMark(m) ;
			for (size_t i=0;i<m.size();i++) {
				int rlo=waveAnchor_<cpos?waveAnchor_:cpos, rhi=waveAnchor_<cpos?cpos:waveAnchor_ ;
				bool sel=(waveSel_==2) || (waveSel_==1 && int(i)==near) || (waveSel_==3 && m[i]>=rlo && m[i]<=rhi) ;
				if (sel) {
					int mx=FPX(m[i]) ;
					if (mx>=X && mx<=X+W) FillRect(mx-5,Y-4,10,H+8,ColorOf(CD_PLAY)) ;   // agarrada / marcada
				}
			}
			// el cursor: línea gruesa con un triángulo grande arriba y otro abajo
			int cx=FPX(cpos) ;
			GUIColor curC=(waveSel_==1)?ColorOf(CD_PLAY):fg ;
			FillRect(cx-2,Y-4,5,H+8,curC) ;
			for (int q=0;q<14;q++) {
				FillRect(cx-(13-q),Y-4+q,2*(13-q)+1,1,curC) ;
				FillRect(cx-q,Y+H+4-13+q,2*q+1,1,curC) ;
			}
			char tag[48] ;
			SoundSource *cs=si->currentSource() ;
			int size=cs?cs->GetSize(-1):0 ;
			int pm=size>0?int((long long)cpos*1000/size):0 ;
			sprintf(tag,"marcas %d  %d.%d%%  zoom x%d",int(m.size()),pm/10,pm%10,waveZoom_) ;
			if (!gridMode_) DrawText(X+4,Y+H-12,tag,fg,0) ;   // con cajas: va al aviso de abajo
			{
				const char *help=tapPlay_?"sonando: A marca  START para  L+START de nuevo" :
				                         "START toca  A escucha  AA marca  B+<> mueve  A+^v zoom" ;
				helpText_=help ;
			}
			// lo que suena al escuchar (A)
			if (previewing_) {
				int sp=Player::GetInstance()->GetStreamPosition() ;
				if (sp>=wfFrom_ && sp<=wfFrom_+vLen) FillRect(FPX(sp)-1,Y-4,3,H+8,ColorOf(CD_PLAY),1) ;
			}
		}
	} else {
		if (!zsrc) { FLAG(a,1) FLAG(b,-1) }   // multicapa: suena el sample entero
		if (loop) FrameRect(l0,Y-4,b-l0,H+8,hi,8,2) ;
	}
	#undef FLAG
	// dibujando (classic, one-shot o slice por región): el cursor
	if (waveEdit_ && !zsrc && !(mode==SM_SLICE && si->GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL)) {
		int cpos=markVar_.GetInt() ;
		int cx=FPX(cpos) ;
		if (cx>=X && cx<=X+W) {
			FillRect(cx-1,Y-4,2,H+8,fg) ;
			for (int q=0;q<6;q++) FillRect(cx-q,Y+H+4+q,2*q+1,1,fg) ;
		}
		char tag[96] ;
		SoundSource *cs=si->currentSource() ;
		int size=cs?cs->GetSize(-1):0 ;
		int pm=size>0?int((long long)cpos*1000/size):0 ;
		char fz[40]="" ;
		FourCC sid=waveStopID() ;   // el 'cómo' de esta parada (A + flechas)
		if (sid) {
			int sv=si->GetSimpler(sid) ;
			const char *sn=(sid==SIP_S_FIN)?"fade in":(sid==SIP_S_FOUT)?"fade out":(sid==SIP_S_ATK)?"attack":
			               (sid==SIP_S_DEC)?"decay":(sid==SIP_S_SUS)?"sustain":"release" ;
			if (sid==SIP_S_SUS) sprintf(fz,"  %s %ddB",sn,sv) ;
			else if (sv>=1000) sprintf(fz,"  %s %d.%ds",sn,sv/1000,(sv%1000)/100) ;
			else sprintf(fz,"  %s %dms",sn,sv) ;
		}
		if (waveZoom_>1) sprintf(tag,"%d.%d%%%s  zoom x%d",pm/10,pm%10,fz,waveZoom_) ;
		else sprintf(tag,"%d.%d%%%s",pm/10,pm%10,fz) ;
		if (!gridMode_) DrawText(X+4,Y+H-22,tag,fg,1) ;   // con cajas: va al aviso de abajo
	}

	// cursor de reproducción
	playheadPx_=-1 ;
	int pos=si->GetPlayPosition() ;
	if (pos>=wfFrom_ && pos<=wfFrom_+vLen) {
		playheadPx_=FPX(pos) ;
		FillRect(playheadPx_-1,Y-4,3,H+8,ColorOf(CD_PLAY),1) ;
	}
	#undef FPX
}

void InstrumentView::drawCurveLine(const std::vector<int> &ys,int x0,GUIColor c,int hiFrom,int hiTo,int xmin,int xmax) {
	int n=int(ys.size()) ;
	for (int i=0;i<n;i++) {
		int x=x0+i ;
		if (x<xmin || x>xmax) continue ;   // con zoom: solo lo que cae en la onda
		int th=(x>=hiFrom && x<hiTo)?11:6 ;   // gruesa: en la consola lo delgado se pierde
		int y=ys[i], prev=i>0?ys[i-1]:y ;
		int y0=y<prev?y:prev, y1=y<prev?prev:y ;
		FillRect(x,y0-th/2,1,y1-y0+th,c) ;
	}
}

// trk2als: multicapa: las zonas en una tira (solo el rango que usan), cada
// una con su nota; la que suena, en negativo
void InstrumentView::drawZoneStrip(SampleInstrument *si,int X,int Y,int W,int H,int cur) {
	GUIColor bg=ColorOf(CD_BACKGROUND), fg=ColorOf(CD_NORMAL) ;
	const std::vector<ZoneSource::Zone> &zones=si->Zones() ;
	if (zones.empty()) return ;
	int lo=127, hi=0 ;
	for (size_t i=0;i<zones.size();i++) { if (zones[i].lo<lo) lo=zones[i].lo ; if (zones[i].hi>hi) hi=zones[i].hi ; }
	int span=hi-lo+1 ;
	if (span<1) span=1 ;
	for (size_t i=0;i<zones.size();i++) {
		const ZoneSource::Zone &z=zones[i] ;
		int x0=X+int((long long)(z.lo-lo)*W/span), x1=X+int((long long)(z.hi-lo+1)*W/span) ;
		int w=x1-x0-4 ;
		if (w<2) w=2 ;
		bool on=(int(i)==cur) ;
		FillRect(x0+2,Y,w,H,on?fg:Mix(bg,fg,18),6) ;
		int r=(z.root>=0)?z.root:z.lo ;
		std::string nm=padNoteName(r) ;
		if (16*int(nm.size())+8<=w) DrawText(x0+2+(w-16*int(nm.size()))/2,Y+(H-16)/2,nm.c_str(),on?bg:fg,1) ;
	}
}

// trk2als: ~25 veces por segundo; si el cursor se movió, redibujar las figuras
bool InstrumentView::Tick() {
	if (capping_) capTick() ;
	if (!touching_ || HasModal()) return false ;
	recordTouch() ;
	return !touching_ ;   // se acabó: a redibujar
}

void InstrumentView::AnimationUpdate() {
	// la nota de la escucha de la envolvente: se suelta al segundo
	if (envOffAt_ && System::GetInstance()->GetClock()>=envOffAt_) envNoteOff() ;
	if (!current_ || current_->GetType()!=IT_SAMPLE || HasModal()) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (kitMode_ && gridMode_) {   // los pads que suenan, y el + de A sostenida
		unsigned long now=System::GetInstance()->GetClock() ;
		int lit=0, base=si->KitBase() ;
		for (int k=0;k<16;k++) { unsigned long h=si->PadHit(base+k) ; if (h && now>=h && now-h<160) lit|=1<<k ; }
		bool redraw=(lit!=kitLitShown_) ;
		if (padHoldAt_ && !padPlus_ && now-padHoldAt_>450) { padPlus_=true ; redraw=true ; }
		if (redraw) { kitLitShown_=lit ; DrawView() ; return ; }
	}
	// trk2als: redibujar si cambió algún valor automatizado (o la grabación).
	// Lo que se graba mientras se sostiene A va en Tick: aquí se está dibujando
	// (candado de dibujo) y grabar toma el del mezclador: en ese orden, con el
	// audio tomándolos al revés, se trababan
	{
		std::vector<int> now ;
		if (Player::GetInstance()->IsRunning()) {
			IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				UIField &f=it->CurrentItem() ;
				if (f.IsStatic()) continue ;
				FourCC id=((UIIntVarField &)f).GetVariableID() ;
				int av ;
				if (automatable(id) && si->GetAutomated(id,av) && !(touching_ && id==touchID_)) now.push_back(av) ;
			}
		}
		int rs=Player::GetInstance()->AutoRecState() ;
		if (tapPending_>=0) rs=0 ;
		if (rs==1) rs=(System::GetInstance()->GetClock()/250)%2 ? 1 : 3 ;   // parpadea
		if (now!=autoShown_ || recShown_!=rs) {
			DrawView() ;
			return ;
		}
	}
	if (tapPending_>=0) {
		Player *player=Player::GetInstance() ;
		if (!player->IsRunning()) { tapPending_=-1 ; songByTap_=false ; }
		else if (player->AutoRecState()==0) {   // ya pasó la precuenta: el uno
			int from=tapPending_ ;
			tapPending_=-1 ;
			int ra,rb ;
			si->SimplerRegion(ra,rb) ;
			markVar_.SetInt(from) ;
			startPreview(from,rb) ;
			slicePlay_=false ;
			tapPlay_=previewing_ ;
		}
	}
	int pos=si->GetPlayPosition() ;
	int px=-1 ;
	if (pos>=wfFrom_ && wfTo_>wfFrom_ && pos<=wfTo_) {
		px=16+int((long long)(pos-wfFrom_)*(WF_BARS*8)/(wfTo_-wfFrom_)) ;
	}
	// editando marcas: el cabezal de la escucha
	if (previewing_) {
		int sp=Player::GetInstance()->GetStreamPosition() ;
		int ra,rb ;
		if (tapPlay_ && (sp==-1 || (sp>=0 && si->SimplerRegion(ra,rb) && sp>=rb))) {
			stopTapPlay() ;
			if (gridMode_) DrawView() ;   // la rejilla también es de figuras
			else { ClearShapes() ; drawWaveform() ; }
			return ;
		}
		if (sp==-1 && !tapPlay_) {
			previewing_=false ;
			slicePlay_=false ;
			px=-2 ;
		}
		if (sp>=0 && sp!=previewPos_) {
			previewPos_=sp ;
			px=-2 ;
		}
	}
	if (px!=playheadPx_) {
		if (gridMode_) DrawView() ;   // la rejilla también es de figuras
		else { ClearShapes() ; drawWaveform() ; }
	}
}

// trk2als: suena la nota (C3) con sus fades, sin clip: el fade in desde el
// inicio; el fade out, desde un poco antes de donde empieza a bajar
void InstrumentView::fadeAudition() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	if (clipSounding()) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int hold=1000 ;
	if (fadeSel_==1) {
		int a,b ;
		SoundSource *src=si->currentSource() ;
		if (src && si->SimplerRegion(a,b)) {
			int rate=src->GetSampleRate(-1) ;
			int from=b-int((si->GetSimpler(SIP_S_FOUT)+600)*(rate/1000.0f)) ;
			if (from>a) si->AuditionFrom(from) ;
			hold=si->GetSimpler(SIP_S_FOUT)+800 ;   // que alcance a oírse entero
		}
	}
	envAudition(hold) ;
}

// la pista ya suena con las notas de su clip: ahí se oye el cambio, no hace
// falta (ni conviene) tocarle una nota encima
bool InstrumentView::clipSounding() {
	Player *player=Player::GetInstance() ;
	if (!player->IsRunning()) return false ;
	int ch=viewData_->songX_ ;
	if (ch<0 || ch>=SONG_CHANNEL_COUNT) return false ;
	if (player->ClipTickPos(ch)<0) return false ;
	unsigned char c=viewData_->currentPlayChain_[ch] ;
	if (c>=CLIP_COUNT) return false ;
	return !viewData_->song_->clips_->Get(c).notes.empty() ;
}

FourCC InstrumentView::envID(int part) {
	static const FourCC amp[4]={SIP_S_ATK,SIP_S_DEC,SIP_S_SUS,SIP_S_REL} ;
	static const FourCC flt[4]={SIP_S_FATK,SIP_S_FDEC,SIP_S_FSUS,SIP_S_FREL} ;
	if (part<0) part=0 ;
	if (part>3) part=3 ;
	return envFilter_?flt[part]:amp[part] ;
}

void InstrumentView::envNoteOff() {
	if (!envOffAt_) return ;
	envOffAt_=0 ;
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	Player::GetInstance()->LiveNoteOff(envCh_,envNote_) ;
	ms->Unlock() ;
}

// suena la nota (C3) un segundo y se suelta: se oyen el sustain y el release
void InstrumentView::envAudition(int holdMs) {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	if (clipSounding()) return ;
	if (previewing_) stopPreview() ;
	envNoteOff() ;
	int ch=viewData_->songX_ ;
	if (ch<0 || ch>=SONG_CHANNEL_COUNT) ch=0 ;
	envCh_=ch ;
	SampleInstrument *asi=(SampleInstrument *)current_ ;
	envNote_=(!sfMode_ && asi->GetSimpler(SIP_S_MODE)==SM_SLICE)?36:60 ;
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	Player::GetInstance()->LiveNoteOn(ch,current_,envNote_,100) ;
	ms->Unlock() ;
	envOffAt_=System::GetInstance()->GetClock()+holdMs ;
}

// true si la tecla se usó aquí
bool InstrumentView::processEnvEdit(unsigned short mask,bool pressed) {
	bool ok=current_ && current_->GetType()==IT_SAMPLE && gridMode_ && !kitMode_ && !audioMode_ ;
	if (ok && !envFilter_ && !sfMode_ && ((SampleInstrument *)current_)->GetSimpler(SIP_S_MODE)==SM_ONESHOT) ok=false ;
	if (!ok) { envNoteOff() ; envEdit_=false ; filtEdit_=false ; return false ; }
	const unsigned short arrows=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
	if ((mask&(EPBM_SELECT|EPBM_R)) && (mask&arrows) && !(mask&EPBM_B)) {   // cambiar de pantalla: sale
		envNoteOff() ;
		envEdit_=false ; filtEdit_=false ;
		return false ;
	}
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (pressed && (mask==EPBM_UP || mask==EPBM_DOWN)) {   // ↑ / ↓: el otro dibujo, sin salir
		hopDrawing(mask==EPBM_DOWN?1:-1) ;
		return true ;
	}
	if (!pressed) {
		// L o R solos (tocar y soltar): la parte anterior / la siguiente, y suena
		if (pageTap_ && mask==0) {
			envSel_+=pageTap_ ;
			if (envSel_<0) envSel_=0 ;
			if (envSel_>3) envSel_=3 ;
			pageTap_=0 ;
			envAudition() ;
		}
		isDirty_=true ;
		return true ;
	}
	pageTap_=(mask==EPBM_L)?-1:(mask==EPBM_R)?1:0 ;
	isDirty_=true ;
	if (mask==EPBM_START) { envAudition() ; return true ; }
	// (aquí no hay B + L / B + R: L y R ya eligen la parte y A la mueve)
	// como en la onda y el filtro: las flechas mueven lo que se ve (el tiempo
	// de la parte elegida); A + arriba / abajo le da forma (el sustain)
	if (mask==EPBM_LEFT || mask==EPBM_RIGHT || ((mask&EPBM_A) && !(mask&EPBM_B))) {
		bool changed=false ;
		if ((mask&(EPBM_LEFT|EPBM_RIGHT)) && envSel_!=2) {   // el tiempo de esa parte
			Variable *v=si->FindVariable(envID(envSel_)) ;
			int cur=v->GetInt() ;
			// las flechas solas: rápido; con A: fino
			int step=(mask&EPBM_A)?((cur>=2000)?100:(cur>=500)?50:(cur>=100)?10:5)
			                      :((cur>=2000)?500:(cur>=500)?200:(cur>=100)?50:20) ;
			int nv=cur+((mask&EPBM_RIGHT)?step:-step) ;
			int max=(envSel_==0)?5000:10000 ;
			if (nv<0) nv=0 ;
			if (nv>max) nv=max ;
			v->SetInt(nv) ;
			drawCapture(envID(envSel_),cur) ;
			changed=true ;
		} else if ((mask&(EPBM_UP|EPBM_DOWN)) && (mask&EPBM_A)) {
			// el nivel: solo el sustain tiene altura
			int d=((mask&EPBM_UP)?1:-1)*(drawRep_<4?1:3) ;   // fino, y al mantener acelera
			if (envSel_==2) {
				Variable *v=si->FindVariable(envID(2)) ;
				int was=v->GetInt() ;
				int nv=was+d ;
				if (nv<-60) nv=-60 ;
				if (nv>0) nv=0 ;
				v->SetInt(nv) ;
				drawCapture(envID(2),was) ;
				changed=true ;
			}   // (ENV, cuánto mueve la envolvente al filtro, se queda en su caja)
		}
		if (changed) envAudition() ;
		return true ;
	}
	return true ;   // lo demás no hace nada hasta salir con Select
}

// true si la tecla se usó aquí
bool InstrumentView::processFadeEdit(unsigned short mask,bool pressed) {
	if (!current_ || current_->GetType()!=IT_SAMPLE || !gridMode_ ||
	    ((SampleInstrument *)current_)->GetSimpler(SIP_S_MODE)!=SM_ONESHOT) {
		fadeEdit_=false ;
		return false ;
	}
	const unsigned short arrows=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
	if ((mask&(EPBM_SELECT|EPBM_R)) && (mask&arrows) && !(mask&EPBM_B)) {   // cambiar de pantalla: sale
		fadeEdit_=false ;
		return false ;
	}
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (!pressed) {
		// L o R solos (tocar y soltar): elegir el fade, y suena
		if (pageTap_ && mask==0) {
			fadeSel_=(pageTap_<0)?0:1 ;
			pageTap_=0 ;
			fadeAudition() ;
		}
		isDirty_=true ;
		return true ;
	}
	pageTap_=(mask==EPBM_L)?-1:(mask==EPBM_R)?1:0 ;
	isDirty_=true ;
	if (mask==EPBM_START) { fadeAudition() ; return true ; }
	// A + ← → mueve el fade elegido (L / R), y va sonando
	if ((mask&EPBM_A) && !(mask&EPBM_B)) {
		if (mask&(EPBM_LEFT|EPBM_RIGHT)) {
			Variable *v=si->FindVariable(fadeSel_?SIP_S_FOUT:SIP_S_FIN) ;
			int cur=v->GetInt() ;
			int step=(cur>=500)?50:(cur>=100)?10:5 ;   // más fino abajo
			// el fade out, al revés: ← lo alarga (su inicio se va a la izquierda,
			// como se ve en la curva) y → lo acorta
			bool more=fadeSel_?((mask&EPBM_LEFT)!=0):((mask&EPBM_RIGHT)!=0) ;
			int nv=cur+(more?step:-step) ;
			if (nv<0) nv=0 ;
			if (nv>2000) nv=2000 ;
			v->SetInt(nv) ;
			fadeAudition() ;
		}
		return true ;
	}
	return true ;   // lo demás no hace nada hasta salir con Select
}

void InstrumentView::OnFocus() {
	fadeEdit_=false ;
	envNoteOff() ;
	envEdit_=false ; filtEdit_=false ;
	bool fromCurve=(viewData_->curveFocus_!=0) ;
	if (viewData_->curveFocus_) {   // vuelve de su curva: el foco en ese parámetro
		lastFocusID_=viewData_->curveFocus_ ;
		viewData_->curveFocus_=0 ;
	}
	onInstrumentChange();
	// trk2als: al Sampler se llega parado en su dibujo (la onda, o la
	// envolvente de la pestaña): lo fuerte se edita ahí; Select abre las cajas.
	// (De regreso de una curva se queda en la caja de ese parámetro)
	boxMode_=fromCurve ;
	autoDraw() ;
}

void InstrumentView::autoDraw() {
	if (boxMode_ || drawEditing() || !gridMode_ || kitMode_ || audioMode_) return ;
	toggleDraw() ;
}

// trk2als: el instrumento de .sf2 del sample actual pasa a zonas
void InstrumentView::convertSf2(bool refresh) {
	sf2Pending_=false ;
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int idx=si->GetSampleIndex() ;
	SoundSource *src=(idx>=0)?SamplePool::GetInstance()->GetSource(idx):0 ;
	if (!src || !src->IsMulti()) return ;
	SoundFontPreset *sp=(SoundFontPreset *)src ;
	std::string msg ;
	bool ok=SF2ImportRaw(sp->GetPath(),sp->GetPresetID(),si,msg) ;
	char buf[64] ;
	if (ok) snprintf(buf,sizeof(buf),"%s: %d zonas",msg.c_str(),si->ZoneCount()) ;
	else snprintf(buf,sizeof(buf),"%s",msg.c_str()) ;
	View::SetNotification(buf) ;
	lastSampleIdx_=si->GetSampleIndex() ;
	if (refresh) onInstrumentChange() ;
	isDirty_=true ;
}

void InstrumentView::Update(Observable &o,I_ObservableData *d) {
	if (padBusy_) return ;   // trk2als: el kit se está reescribiendo
	onInstrumentChange() ;
}

/************************************************************
 trk2als: presets del Sampler
 ************************************************************/

void InstrumentView::openPresetLoad() {
	PresetDialog *pd=new PresetDialog(*this) ;
	// trk2als: como en la cabecera de la pista: A lanza y entra en el 1
	SoundLauncher *l=SoundLauncher::Current() ;
	if (l && current_ && current_->GetType()==IT_SAMPLE) {
		l->Open(viewData_->currentInstrument_,this) ;
		pd->SetLauncher(l) ;
	}
	DoModal(pd,PresetLoadCallback) ;
}

void InstrumentView::OnBrowserClosed(bool kit) {
	lastFocusID_=kit?IV_PADSEL:IV_PRESET_LOAD ;
	padShown_=-1 ;
	onInstrumentChange() ;
	isDirty_=true ;
}

void InstrumentView::LaunchBefore(int,int track,bool) {
	padBusy_=true ;   // el kit se reescribe: los campos no se reconstruyen a medias
	if (track>=0) Player::GetInstance()->HoldTrack(track,true) ;
}

void InstrumentView::LaunchAfter(int instrument,int track,const std::string &name,bool ok) {
	if (track>=0) Player::GetInstance()->HoldTrack(track,false) ;
	padBusy_=false ;
	if (instrument==viewData_->currentInstrument_) {
		padShown_=-1 ;
		onInstrumentChange() ;
	}
	if (ok && !HasModal()) {
		View::SetNotification((SoundLauncher::DisplayName(name)+" cargado").c_str()) ;
	}
	isDirty_=true ;
}

// se quitaron samples sin uso: lo que guardaba el deshacer del kit (con
// índices de samples) ya no sirve
void InstrumentView::LaunchPurged() {
	kitUndo_.clear() ;
	kitRedo_.clear() ;
	kitOwner_=0 ;
}

void InstrumentView::openPresetSave() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (si->GetSampleIndex()<0) {
		View::SetNotification("Primero elige un sample") ;
		return ;
	}
	// primero la carpeta (categoría), luego el nombre (un kit, de entrada en Kits)
	// trk2als: la categoría la pone lo que se guarda: kit, multicapa o sample
	int kind=si->IsKit()?InstrumentPresets::PK_KIT:(si->IsZoned()?InstrumentPresets::PK_MULTI:InstrumentPresets::PK_SAMPLE) ;
	PresetDialog *pd=new PresetDialog(*this,true,kind) ;
	DoModal(pd,PresetFolderCallback) ;
}

void InstrumentView::OnPresetFolder(const std::string &folder) {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	presetFolder_=folder ;
	std::string base="root:presets" ;
	if (!folder.empty()) base+="/"+folder ;
	NewProjectDialog *nd=new NewProjectDialog(*this,base.c_str()) ;
	nd->SetPrefix("") ;
	nd->SetTitle(folder.empty()?"Guardar preset":"Guardar en "+folder) ;
	nd->SetInitialName(InstrumentPresets::SuggestedName(si)) ;
	nd->SetAllowExisting(true) ;     // con el mismo nombre se actualiza
	DoModal(nd,PresetSaveCallback) ;
}

void InstrumentView::OnPresetLoad(const std::string &name) {
	if (name.empty() || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	int tr=ownerTrack(viewData_) ;   // la pista se calla mientras se reescribe
	InstrumentPresets::Prepare(name) ;   // sus samples, antes (es lo lento)
	if (tr>=0) Player::GetInstance()->HoldTrack(tr,true) ;
	bool loaded=InstrumentPresets::Load((SampleInstrument *)current_,name) ;
	if (tr>=0) Player::GetInstance()->HoldTrack(tr,false) ;
	if (loaded) {
		std::string msg="Preset "+name+" cargado" ;
		View::SetNotification(msg.c_str()) ;
	} else {
		View::SetNotification("No se pudo cargar el preset") ;
	}
	lastFocusID_=IV_PRESET_LOAD ;
	onInstrumentChange() ;
	isDirty_=true ;
}

void InstrumentView::OnPresetSave(const std::string &shortName) {
	if (shortName.empty() || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	std::string name=presetFolder_.empty()?shortName:presetFolder_+"/"+shortName ;
	bool existed=InstrumentPresets::Exists(name) ;
	if (InstrumentPresets::Save((SampleInstrument *)current_,name)) {
		std::string msg="Preset "+name+(existed?" actualizado":" guardado") ;
		View::SetNotification(msg.c_str()) ;
	} else {
		View::SetNotification("No se pudo guardar el preset") ;
	}
	isDirty_=true ;
}

/************************************************************
 trk2als: slice manual, editar marcas sobre la onda

   (al entrar suena la región, y la canción si no estaba sonando)
   A              mientras suena: marca donde va sonando
   START          tocar / parar la región desde el cursor
   L + START      borrar las marcas y rebanar de nuevo desde el inicio
   ← →            mover el cursor (o la marca agarrada)
   A              escuchar del cursor a la siguiente marca
   A + ← →        moverse escuchando
   A, A           poner una marca en el cursor (doble toque)
   A + ↑ ↓        acercar / alejar
   B + ← →        saltar a la marca anterior / siguiente (queda agarrada)
   B + A          borrar la marca del cursor (o las seleccionadas)
   Select + B     seleccionar: una vez la marca del cursor, otra vez todas
   ↓              soltar la marca agarrada / terminar (queda en "slice by")
 ************************************************************/

void InstrumentView::enterWaveEdit(bool play) {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int a,b ;
	if (!si->SimplerRegion(a,b)) return ;
	waveEdit_=true ;
	waveZoom_=1 ;
	waveSel_=0 ;
	waveAPut_=false ;
	lastAPress_=0 ;
	int c=markVar_.GetInt() ;
	if (c<a || c>b) markVar_.SetInt(a) ;
	waveStop_=0 ;
	if (!canMark()) markVar_.SetInt(si->RegionPoint(true)) ;   // sin cursor libre: en la bandera del inicio
	if (play) startTapPlay(a) ;
}

// trk2als: Select: cajas <-> dibujo. Por ahora el dibujo editable son las
// rebanadas a mano (página SONIDO, modo slice, slice by manual)
void InstrumentView::toggleDraw() {
	if (waveEdit_) { exitWaveEdit() ; return ; }
	if (fadeEdit_) { fadeEdit_=false ; return ; }
	if (envEdit_ || filtEdit_) { envNoteOff() ; envEdit_=false ; filtEdit_=false ; return ; }
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (kitMode_ && gridMode_) {   // kit, pestaña PAD: las cajas del pad <-> los pads
		if (page_[1]!=0) return ;
		padEdit_=!padEdit_ ;
		lastFocusID_=padEdit_?IV_PADST:IV_PADSEL ;
		onInstrumentChange() ;
		return ;
	}
	if (kitMode_ || audioMode_ || !simplerView_ || !gridMode_) return ;
	// AMP en one-shot: los fades (un multicapa siempre lleva su envolvente)
	(void)si ;
	// FILTRO: su envolvente; AMP (classic / slice): la de volumen
	bool fpage=!strcmp(pageName_[page_[1]],"FILTRO"), apage=!strcmp(pageName_[page_[1]],"AMP") ;
	if (fpage) {   // FILTRO: primero el filtro (su curva); abajo, su envolvente
		filtEdit_=true ;
		filtBox_=SIP_S_FHZ ;
		return ;
	}
	if (apage) {
		envEdit_=true ;
		envFilter_=false ;
		envSel_=0 ;
		return ;
	}
	if (sfMode_ || strcmp(pageName_[page_[1]],"SAMPLER")) return ;   // la onda: solo en SAMPLER (no en multicapa)
	enterWaveEdit(false) ;
}

// trk2als: dentro de un dibujo, ↑ / ↓ pasan al dibujo de la otra pestaña sin
// salir de la edición (de la onda / la envolvente de volumen a la del filtro
// y de regreso): se termina una y se sigue con la otra
void InstrumentView::hopDrawing(int dir) {
	// la columna: la onda (SAMPLER), el volumen (AMP, multicapa), el filtro y
	// la envolvente del filtro (los dos en FILTRO)
	int pg[8], kd[8], n=0, cur=-1 ;   // kd: 0 onda / envolvente de la página, 1 la curva del filtro
	for (int i=0;i<pageCount_ && n<7;i++) {
		const char *pn=pageName_[i] ;
		bool here=(i==page_[1]) ;
		if (!strcmp(pn,"FILTRO")) {
			if (here && filtEdit_) cur=n ;
			pg[n]=i ; kd[n++]=1 ;
			if (here && envEdit_) cur=n ;
			pg[n]=i ; kd[n++]=0 ;
		} else if (!strcmp(pn,"AMP") || (!sfMode_ && !strcmp(pn,"SAMPLER"))) {
			if (here) cur=n ;
			pg[n]=i ; kd[n++]=0 ;
		}
	}
	if (cur<0 || cur+dir<0 || cur+dir>=n) return ;
	int t=cur+dir ;
	if (waveEdit_) exitWaveEdit() ;
	envNoteOff() ;
	envEdit_=false ; filtEdit_=false ;
	fadeEdit_=false ;
	if (page_[1]!=pg[t]) {
		page_[1]=pg[t] ;
		lastFocusID_=0 ;
		onInstrumentChange() ;
	}
	if (!strcmp(pageName_[pg[t]],"FILTRO")) {
		if (kd[t]==1) { filtEdit_=true ; filtBox_=SIP_S_FHZ ; }
		else { envEdit_=true ; envFilter_=true ; envSel_=0 ; }
	} else toggleDraw() ;
	isDirty_=true ;
}

// trk2als: el dibujo del filtro: su curva de respuesta. Izq / der cambia el
// corte; A + arriba / abajo la resonancia; L / R el tipo
bool InstrumentView::processFiltEdit(unsigned short mask,bool pressed) {
	bool ok=current_ && current_->GetType()==IT_SAMPLE && gridMode_ && !kitMode_ && !audioMode_ ;
	if (!ok) { envNoteOff() ; filtEdit_=false ; return false ; }
	const unsigned short arrows=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
	if ((mask&(EPBM_SELECT|EPBM_R)) && (mask&arrows) && !(mask&EPBM_B)) {   // cambiar de pantalla: sale
		envNoteOff() ;
		filtEdit_=false ;
		return false ;
	}
	if (mask&EPBM_SELECT) return false ;   // Select solo (o deshacer): lo de siempre
	SampleInstrument *si=(SampleInstrument *)current_ ;
	char msg[48] ;
	if (!pressed) {
		// L o R solos (tocar y soltar): el tipo de filtro anterior / siguiente
		if (pageTap_ && mask==0) {
			Variable *v=si->FindVariable(SIP_S_FTYPE) ;
			int wasT=v->GetInt() ;
			int t=wasT+pageTap_ ;
			if (t<0) t=0 ;
			if (t>2) t=2 ;
			v->SetInt(t) ;
			drawCapture(SIP_S_FTYPE,wasT) ;
			if (capping_) capEnd() ;
			filtBox_=SIP_S_FTYPE ;
			View::SetNotification(v->GetString()) ;
			pageTap_=0 ;
			envAudition() ;
		}
		isDirty_=true ;
		return true ;
	}
	pageTap_=(mask==EPBM_L)?-1:(mask==EPBM_R)?1:0 ;
	isDirty_=true ;
	if (mask==EPBM_UP || mask==EPBM_DOWN) { hopDrawing(mask==EPBM_DOWN?1:-1) ; return true ; }
	if (mask==EPBM_START) { envAudition() ; return true ; }
	// como en la onda: las flechas mueven lo que se ve (el corte), A + flechas
	// le da forma (la resonancia) y L / R eligen cuál (el tipo)
	if (mask==EPBM_LEFT || mask==EPBM_RIGHT) {   // el corte, de a tercera mayor (lo fino, en su caja)
		Variable *v=si->FindVariable(SIP_S_FHZ) ;
		float hz=float(v->GetInt())*((mask==EPBM_RIGHT)?1.2599210f:1.0f/1.2599210f) ;
		int nv=int(hz+0.5f) ;
		if (nv==v->GetInt()) nv+=(mask==EPBM_RIGHT)?1:-1 ;
		if (nv<30) nv=30 ;
		if (nv>22000) nv=22000 ;
		int wasHz=v->GetInt() ;
		v->SetInt(nv) ;
		drawCapture(SIP_S_FHZ,wasHz) ;
		filtBox_=SIP_S_FHZ ;
		if (nv>=1000) snprintf(msg,sizeof(msg),"corte %d.%dkHz",nv/1000,(nv%1000)/100) ;
		else snprintf(msg,sizeof(msg),"corte %dHz",nv) ;
		View::SetNotification(msg) ;
		envAudition() ;
		return true ;
	}
	if ((mask&EPBM_A) && !(mask&EPBM_B) && (mask&(EPBM_LEFT|EPBM_RIGHT))) {   // el corte, fino (un semitono)
		Variable *v=si->FindVariable(SIP_S_FHZ) ;
		int wasHz=v->GetInt() ;
		float hz=float(wasHz)*((mask&EPBM_RIGHT)?1.0594631f:1.0f/1.0594631f) ;
		int nv=int(hz+0.5f) ;
		if (nv==wasHz) nv+=(mask&EPBM_RIGHT)?1:-1 ;
		if (nv<30) nv=30 ;
		if (nv>22000) nv=22000 ;
		v->SetInt(nv) ;
		drawCapture(SIP_S_FHZ,wasHz) ;
		filtBox_=SIP_S_FHZ ;
		if (nv>=1000) snprintf(msg,sizeof(msg),"corte %d.%dkHz",nv/1000,(nv%1000)/100) ;
		else snprintf(msg,sizeof(msg),"corte %dHz",nv) ;
		View::SetNotification(msg) ;
		envAudition() ;
		return true ;
	}
	if ((mask&EPBM_A) && !(mask&EPBM_B) && (mask&(EPBM_UP|EPBM_DOWN))) {   // la resonancia: fino, y al mantener acelera
		Variable *v=si->FindVariable(SIP_S_FQ) ;
		int wasQ=v->GetInt() ;
		int nv=wasQ+((mask&EPBM_UP)?1:-1)*(drawRep_<4?2:10) ;
		if (nv<0) nv=0 ;
		if (nv>100) nv=100 ;
		v->SetInt(nv) ;
		drawCapture(SIP_S_FQ,wasQ) ;
		filtBox_=SIP_S_FQ ;
		snprintf(msg,sizeof(msg),"resonancia %d%%",nv) ;
		View::SetNotification(msg) ;
		envAudition() ;
		return true ;
	}
	return true ;
}

// la curva de respuesta del filtro sobre el panel: 30 Hz a la izquierda,
// 22 kHz a la derecha; 0 dB a un tercio de la altura
void InstrumentView::drawFilterCurve(SampleInstrument *si,int X,int Y,int W,int H,GUIColor c) {
	int type=si->GetSimpler(SIP_S_FTYPE) ;
	float fc=float(si->GetSimpler(SIP_S_FHZ)) ;
	if (fc<30) fc=30 ;
	float q=0.707f+si->GetSimpler(SIP_S_FQ)*0.11f ;
	std::vector<int> ys ;
	int y0=Y+H/3, top=Y+8, bot=Y+H-8 ;
	float pxDb=(H*0.62f)/48.0f ;   // 48 dB hacia abajo caben en el panel
	for (int x=0;x<W;x++) {
		float f=30.0f*powf(22000.0f/30.0f,float(x)/float(W-1)) ;
		float r=f/fc ;
		float den=sqrtf((1-r*r)*(1-r*r)+(r/q)*(r/q)) ;
		if (den<1e-6f) den=1e-6f ;
		float mag=(type==0)?1.0f/den:(type==2)?(r*r)/den:(r/q)/den ;
		float db=20.0f*log10f(mag<1e-6f?1e-6f:mag) ;
		int y=y0-int(db*pxDb) ;
		if (y<top) y=top ;
		if (y>bot) y=bot ;
		ys.push_back(y) ;
	}
	drawCurveLine(ys,X,c,-1,-1,X,X+W) ;
	// el corte: una guía
	int cx=X+int(float(W-1)*logf(fc/30.0f)/logf(22000.0f/30.0f)) ;
	FillRect(cx,Y+4,1,H-8,Mix(ColorOf(CD_BACKGROUND),ColorOf(CD_NORMAL),45)) ;
}

bool InstrumentView::canMark() {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	return si->GetSimpler(SIP_S_MODE)==SM_SLICE && si->GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL ;
}

// trk2als: toda la región suena desde 'from' (y la canción, si no estaba
// sonando): con A se marca donde va sonando
void InstrumentView::startTapPlay(int from,bool countIn) {
	markVar_.SetInt(from) ;
	int ra,rb ;
	((SampleInstrument *)current_)->SimplerRegion(ra,rb) ;
	// rebanando: si no suena nada, una precuenta (como la captura) para
	// estar listo; la región empieza a sonar en el uno
	if (countIn && canMark() && !Player::GetInstance()->IsRunning()) {
		Player *player=Player::GetInstance() ;
		player->RequestCountIn(false) ;
		player->OnSongStartButton(0,7,false,false) ;
		songByTap_=true ;
		tapPending_=from ;
		return ;
	}
	startPreview(from,rb) ;
	slicePlay_=false ;
	tapPlay_=previewing_ ;
	Player *player=Player::GetInstance() ;
	if (tapPlay_ && !player->IsRunning()) {
		player->OnSongStartButton(0,7,false,false) ;
		songByTap_=true ;
	}
}

void InstrumentView::stopTapPlay() {
	tapPending_=-1 ;
	stopPreview() ;
	if (songByTap_) {
		Player *player=Player::GetInstance() ;
		if (player->IsRunning()) player->OnSongStartButton(0,7,false,false) ;
		songByTap_=false ;
	}
}

void InstrumentView::exitWaveEdit() {
	stopTapPlay() ;
	waveEdit_=false ;
	waveSel_=0 ;
	waveZoom_=1 ;
}

// suena desde 'from' (o el cursor) hasta 'end' (o el final del sample)
void InstrumentView::startPreview(int from,int end) {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	if (clipSounding()) return ;   // con el clip sonando se oye el clip, no la prueba
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int index=si->GetSampleIndex() ;
	SoundSource *src=si->currentSource() ;
	if (index<0 || !src || src->GetSize(-1)<=0) return ;
	if (from<0) from=markVar_.GetInt() ;
	std::string name=SamplePool::GetInstance()->GetNameList()[index] ;
	double frac=double(from)/src->GetSize(-1) ;
	Player *player=Player::GetInstance() ;
	player->StartStreaming(Path(SamplePool::GetInstance()->SourcePath(index)),1.0,0,frac,false) ;   // trk2als: también si es referencia
	player->SetStreamEnd(end) ;
	previewing_=true ;
	slicePlay_=(end>0) ;
}

// la rebanada donde está 'frame': de su marca a la siguiente
void InstrumentView::sliceAt(int frame,int &from,int &to) {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	std::vector<int> bounds ;
	si->SliceBounds(bounds) ;
	from=frame ; to=-1 ;
	for (size_t k=0;k+1<bounds.size();k++) {
		if (frame>=bounds[k] && frame<bounds[k+1]) { from=bounds[k] ; to=bounds[k+1] ; return ; }
	}
	if (bounds.size()>=2 && frame>=bounds.back()) { from=bounds[bounds.size()-2] ; to=bounds.back() ; }
}

void InstrumentView::stopPreview() {
	if (previewing_) Player::GetInstance()->StopStreaming() ;
	previewing_=false ;
	tapPlay_=false ;
	slicePlay_=false ;
}

// la marca más cercana al cursor (dentro de 1% de la región), o -1
int InstrumentView::nearMark(const std::vector<int> &m) {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int a,b ;
	si->SimplerRegion(a,b) ;
	int tol=(b-a)/100 ;
	if (tol<1) tol=1 ;
	int c=markVar_.GetInt(), best=-1, dist=tol+1 ;
	for (size_t i=0;i<m.size();i++) {
		int d=m[i]-c ;
		if (d<0) d=-d ;
		if (d<dist) { dist=d ; best=int(i) ; }
	}
	return best ;
}

// true si la tecla se usó aquí
bool InstrumentView::processWaveEdit(unsigned short mask,bool pressed) {
	if (!current_ || current_->GetType()!=IT_SAMPLE || sfMode_ || kitMode_ || audioMode_) {
		exitWaveEdit() ;
		return false ;
	}
	if (!pressed) {
		if (!(mask&EPBM_A) && (waveAPut_ || waveSel_==1 || waveFlag_)) waveARelease() ;
		if (mask==0) waveGesture_=false ;   // el siguiente gesto, otra foto
		isDirty_=true ;   // soltó: la caja deja de verse en negativo
		if (previewing_ && !tapPlay_ && !slicePlay_ && !(mask&EPBM_A)) stopPreview() ;
		return true ;
	}
	// cambiar de pantalla (Select + flechas, o R + flechas) termina la edición.
	// Ojo: Shift en la Mac también manda R, así que R solo no cuenta
	const unsigned short arrows=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
	if ((mask&EPBM_R) && (mask&arrows) && !(mask&EPBM_B)) {
		exitWaveEdit() ;
		return false ;
	}
	if (mask==EPBM_UP || mask==EPBM_DOWN) {   // ↑ / ↓: el otro dibujo, sin salir
		hopDrawing(mask==EPBM_DOWN?1:-1) ;
		return true ;
	}
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int a,b ;
	si->SimplerRegion(a,b) ;
	int step=(wfTo_>wfFrom_)?(wfTo_-wfFrom_)/128:(b-a)/128 ;
	if (step<1) step=1 ;
	int c=markVar_.GetInt() ;
	SoundSource *wsrc=si->currentSource() ;
	int size=wsrc?wsrc->GetSize(-1):b ;
	if (c<0) c=0 ;
	if (c>size) c=size ;
	std::vector<int> m=si->GetMarks() ;
	isDirty_=true ;

	// L + Start (o ← + Start): rebanar de nuevo, sin marcas, desde el inicio
	if ((mask&EPBM_START) && (mask&EPBM_L)) {
		wavePush() ;
		stopTapPlay() ;
		m.clear() ;
		si->SetMarks(m) ;
		waveSel_=0 ;
		startTapPlay(a) ;
		return true ;
	}
	// Start: tocar / parar la región desde el cursor (y la canción)
	if (mask==EPBM_START) {
		if (tapPlay_ || tapPending_>=0) { tapPending_=-1 ; stopTapPlay() ; }
		else startTapPlay((c<a || c>=b-step)?a:c) ;
		return true ;
	}
	// mientras suena la región: A marca donde va sonando (lo que se oye va
	// un poco atrás de lo que se lee: se compensa la latencia del audio)
	if (tapPlay_ && canMark()) {
		if (mask==EPBM_A) {
			int sp=Player::GetInstance()->GetStreamPosition() ;
			SoundSource *src=si->currentSource() ;
			if (sp>=0 && src) {
				waveGesture_=false ;   // cada marca al vuelo se deshace sola
				wavePush() ;
				// config.xml: <SLICETAPLATENCY value="40"/> en ms (si no, ~2 bloques de audio)
				static int latMs=-2 ;
				if (latMs==-2) {
					const char *v=Config::GetInstance()->GetValue("SLICETAPLATENCY") ;
					latMs=v?atoi(v):-1 ;
				}
				int lat=(latMs>=0)?int((long long)latMs*src->GetSampleRate(-1)/1000) :
				        int((long long)Audio::GetInstance()->GetAudioBufferSize()*2*
				            src->GetSampleRate(-1)/44100) ;
				int at=si->AddMark(sp-lat>a?sp-lat:a+1) ;
				if (at>=0) markVar_.SetInt(at) ;
				else View::SetNotification("muy cerca de otra marca") ;
			}
			return true ;
		}
		// cualquier otra cosa la detiene y sigue como siempre
		stopTapPlay() ;
	}

	// classic, one-shot y slice por región: no hay cursor libre. L / R eligen
	// la bandera (el cursor vive en ella); ← → dicen DÓNDE (la mueven, sonando;
	// B + ← → a saltos grandes) y, en one-shot, A + ← → dice CÓMO: el fade in
	// en el inicio, el fade out en el final. B + ↑ ↓ zoom; B + A: a su valor
	if (!canMark()) {
		int fs=si->RegionPoint(true), fe=si->RegionPoint(false) ;
		int smode=si->GetSimpler(SIP_S_MODE) ;
		bool oneShot=(smode==SM_ONESHOT), classic=(smode==SM_CLASSIC) ;
		if (!classic && waveStop_!=0 && waveStop_!=3) waveStop_=0 ;
		bool st=(waveStop_==0), en=(waveStop_==3) ;
		markVar_.SetInt(en?fe:fs) ;
		if ((mask&EPBM_B) && (mask&EPBM_A)) {
			if (st || en) {
				wavePush() ;
				si->SetRegionPoint(st,st?0:size) ;
				markVar_.SetInt(si->RegionPoint(st)) ;
			}
			return true ;
		}
		if ((mask&EPBM_B) && (mask&(EPBM_UP|EPBM_DOWN))) {
			if (mask&EPBM_UP) { if (waveZoom_<64) waveZoom_*=2 ; }
			else if (waveZoom_>1) waveZoom_/=2 ;
			waveNotice(0) ;
			return true ;
		}
		// A + flechas: CÓMO. One-shot: el fade de esa bandera. Classic: el
		// attack (inicio), el decay, el sustain (↑ ↓) y el release (final)
		if ((mask&EPBM_A) && (mask&(EPBM_LEFT|EPBM_RIGHT|EPBM_UP|EPBM_DOWN)) && !(mask&(EPBM_B|EPBM_SELECT|EPBM_L|EPBM_R))) {
			FourCC id=waveStopID() ;
			if (!id) return true ;
			Variable *v=si->FindVariable(id) ;
			int cur=v->GetInt(), nv=cur ;
			if (id==SIP_S_SUS) {
				if (!(mask&(EPBM_UP|EPBM_DOWN))) return true ;
				nv=cur+((mask&EPBM_UP)?1:-1)*(drawRep_<4?1:3) ;
				if (nv<-60) nv=-60 ;
				if (nv>0) nv=0 ;
			} else {
				if (!(mask&(EPBM_LEFT|EPBM_RIGHT))) return true ;
				// fino; al mantener, acelera (aquí las flechas solas son de la bandera)
				int fstep=(drawRep_<4)?((cur>=2000)?100:(cur>=500)?50:(cur>=100)?10:5)
				                      :((cur>=2000)?500:(cur>=500)?200:(cur>=100)?50:20) ;
				// el fade out crece hacia adentro del sample (con ←); lo demás, con →
				bool more=(id==SIP_S_FOUT)?((mask&EPBM_LEFT)!=0):((mask&EPBM_RIGHT)!=0) ;
				nv=cur+(more?fstep:-fstep) ;
				int max=oneShot?2000:(id==SIP_S_ATK?5000:10000) ;
				if (nv<0) nv=0 ;
				if (nv>max) nv=max ;
			}
			if (nv==cur) return true ;
			wavePush() ;
			v->SetInt(nv) ;
			waveNotice(id) ;
			if (oneShot) { fadeSel_=en?1:0 ; fadeAudition() ; }
			else envAudition() ;
			return true ;
		}
		// ← → (B: a saltos): DÓNDE, solo en las banderas
		if ((mask&(EPBM_LEFT|EPBM_RIGHT)) && !(mask&(EPBM_A|EPBM_SELECT|EPBM_L|EPBM_R)) && (st || en)) {
			wavePush() ;
			int jump=step*((mask&EPBM_B)?16:1) ;
			int p=(st?fs:fe)+((mask&EPBM_RIGHT)?jump:-jump) ;
			if (p<0) p=0 ;
			if (p>size) p=size ;
			int wasPct=si->GetSimpler(st?SIP_S_START:SIP_S_END) ;
			si->SetRegionPoint(st,p) ;
			drawCapture(st?SIP_S_START:(FourCC)SIP_S_END,wasPct) ;
			markVar_.SetInt(si->RegionPoint(st)) ;
			if (oneShot) { fadeSel_=st?0:1 ; fadeAudition() ; }   // la nota, con sus fades
			else { waveFlag_=st?1:2 ; waveARelease() ; }           // el sample: desde el inicio, o el último segundo
			return true ;
		}
		return true ;
	}
	// (slice a mano) B + L / B + R: el inicio / el final donde está el cursor
	if ((mask&EPBM_B) && (mask&(EPBM_L|EPBM_R)) && !(mask&(EPBM_SELECT|EPBM_A)) && size>0) {
		// sostenidos, ← → lo van moviendo (y va sonando)
		wavePush() ;
		bool st=(mask&EPBM_L)!=0 ;
		if (mask&EPBM_RIGHT) c+=step ;
		else if (mask&EPBM_LEFT) c-=step ;
		if (c<0) c=0 ;
		if (c>size) c=size ;
		si->SetRegionPoint(st,c) ;
		c=si->RegionPoint(st) ;        // donde quedó (no se cruzan)
		markVar_.SetInt(c) ;
		waveFlag_=st?1:2 ;
		waveARelease() ;   // suena: desde el inicio, o el último segundo
		return true ;
	}
	// Select, B: marcar desde aquí (las flechas agrandan); otra vez: todas
	if ((mask&(EPBM_SELECT|EPBM_R)) && (mask&EPBM_B) && !(mask&EPBM_A)) {
		if (!canMark()) return true ;
		if (waveSel_==3) { waveSel_=2 ; View::SetNotification("marcadas todas") ; }
		else { waveSel_=3 ; waveAnchor_=c ; View::SetNotification("marcando: flechas para agrandar") ; }
		return true ;
	}
	if ((mask&EPBM_B) && (mask&EPBM_A) && waveSel_<2 && (c<=a+step || c>=b-step)) {
		// B + A sobre una bandera: el inicio a 0 y el final a 100
		wavePush() ;
		bool st=(c<=a+step) ;
		si->SetRegionPoint(st,st?0:size) ;
		si->SimplerRegion(a,b) ;
		markVar_.SetInt(st?a:b) ;
		waveFlag_=0 ;
		return true ;
	}
	if ((mask&EPBM_B) && (mask&EPBM_A)) {     // B + A: borrar (la del cursor o las marcadas)
		if (!canMark()) return true ;
		wavePush() ;
		if (waveSel_==2) m.clear() ;
		else if (waveSel_==3) {
			int lo=waveAnchor_<c?waveAnchor_:c, hi=waveAnchor_<c?c:waveAnchor_ ;
			std::vector<int> keep ;
			for (size_t i=0;i<m.size();i++) if (m[i]<lo || m[i]>hi) keep.push_back(m[i]) ;
			m=keep ;
		} else {
			int k=nearMark(m) ;
			if (k>=0) m.erase(m.begin()+k) ;
		}
		si->SetMarks(m) ;
		waveSel_=0 ;
		waveAPut_=false ;
		waveNotice(1) ;
		return true ;
	}
	if (mask&EPBM_B) {
		if (mask&(EPBM_LEFT|EPBM_RIGHT)) {     // B + ← →: de a saltos grandes
			c+=((mask&EPBM_RIGHT)?step:-step)*16 ;
			if (c<0) c=0 ;
			if (c>size) c=size ;
			markVar_.SetInt(c) ;
		} else if (mask&EPBM_UP) {             // B + ↑ / ↓: zoom
			if (waveZoom_<64) waveZoom_*=2 ;
			waveNotice(0) ;
		} else if (mask&EPBM_DOWN) {
			if (waveZoom_>1) waveZoom_/=2 ;
			waveNotice(0) ;
		}
		return true ;
	}
	if (mask&EPBM_Y) {                         // Y + ← →: de marca en marca
		std::vector<int> pts ;
		a=si->RegionPoint(true) ;      // las banderas, sin acomodar
		b=si->RegionPoint(false) ;
		pts.push_back(a) ;
		if (canMark()) for (size_t i=0;i<m.size();i++) if (m[i]>a && m[i]<b) pts.push_back(m[i]) ;
		pts.push_back(b) ;
		if (mask&EPBM_RIGHT) {
			int next=-1 ;
			for (size_t i=0;i<pts.size();i++) if (pts[i]>c && (next<0 || pts[i]<next)) next=pts[i] ;
			if (next>=0) markVar_.SetInt(next) ;
		} else if (mask&EPBM_LEFT) {
			int prev=-1 ;
			for (size_t i=0;i<pts.size();i++) if (pts[i]<c && pts[i]>prev) prev=pts[i] ;
			if (prev>=0) markVar_.SetInt(prev) ;
		}
		return true ;
	}
	if (mask&EPBM_A) {
		if (mask==EPBM_A) {                    // A sobre una marca: la agarra; en vacío: pone una al soltar
			wavePush() ;
			waveFlag_=0 ;
			int fa=si->RegionPoint(true), fb=si->RegionPoint(false) ;
			if (c>=fa-step && c<=fa+step) { waveFlag_=1 ; waveSel_=0 ; waveAPut_=false ; markVar_.SetInt(fa) ; return true ; }
			if (c>=fb-step && c<=fb+step) { waveFlag_=2 ; waveSel_=0 ; waveAPut_=false ; markVar_.SetInt(fb) ; return true ; }
			if (!canMark() || c<=a || c>=b) { waveSel_=0 ; waveAPut_=false ; return true ; }
			// A: suena al apretarla. Sobre una marca la agarra; en vacío pone
			// una ahí mismo y queda agarrada (A + ← → la mueve y la va sonando)
			int k=nearMark(m) ;
			waveAPut_=false ;
			if (k<0) {
				int at=si->AddMark(c) ;
				if (at<0) { waveSel_=0 ; View::SetNotification("muy cerca de otra marca") ; return true ; }
				markVar_.SetInt(at) ;
				m=si->GetMarks() ;
				k=nearMark(m) ;
				waveNotice(1) ;
			}
			if (k>=0) {
				waveSel_=1 ;
				markVar_.SetInt(m[k]) ;
				startPreview(m[k],(k+1<(int)m.size())?m[k+1]:b) ;
			}
			return true ;
		}
		if ((mask&(EPBM_LEFT|EPBM_RIGHT)) && waveFlag_) {   // A + ← → sobre una bandera: de 1 en 1 %
			bool st=(waveFlag_==1) ;   // de a un paso (más fino con zoom)
			int p=si->RegionPoint(st)+((mask&EPBM_RIGHT)?step:-step) ;
			if (p<0) p=0 ;
			if (p>size) p=size ;
			si->SetRegionPoint(st,p) ;
			markVar_.SetInt(si->RegionPoint(st)) ;
			return true ;
		}
		if (mask&(EPBM_LEFT|EPBM_RIGHT)) {     // A + ← →: moverla
			if (waveAPut_) {                     // en vacío y moviendo: se pone y se mueve
				waveAPut_=false ;
				int at=si->AddMark(c) ;
				if (at>=0) { markVar_.SetInt(at) ; m=si->GetMarks() ; waveSel_=1 ; }
				else View::SetNotification("muy cerca de otra marca") ;
			}
			if (waveSel_!=1) return true ;
			int k=nearMark(m) ;
			if (k<0) return true ;
			int gap=si->MinMarkGap() ;           // no se pega a sus vecinas
			int lo=((k>0)?m[k-1]:a)+gap ;
			int hi=((k+1<(int)m.size())?m[k+1]:b)-gap ;
			if (hi<lo) hi=lo ;
			int p=m[k]+((mask&EPBM_RIGHT)?step:-step) ;
			if (p<lo) p=lo ;
			if (p>hi) p=hi ;
			m[k]=p ;
			si->SetMarks(m) ;
			markVar_.SetInt(p) ;
			// se oye la rebanada desde donde quedó (hasta donde empieza la
			// siguiente); cada movimiento la vuelve a empezar
			startPreview(p,(k+1<(int)m.size())?m[k+1]:b) ;
		}
		return true ;
	}
	if (mask==EPBM_LEFT || mask==EPBM_RIGHT) {   // ← →: el cursor (por todo el sample)
		c+=(mask==EPBM_RIGHT)?step:-step ;
		if (c<0) c=0 ;
		if (c>size) c=size ;
		markVar_.SetInt(c) ;
		return true ;
	}
	return true ;
}

// trk2als: lo que antes se escribía dentro de la onda va al aviso de abajo,
// y solo cuando cambia: el zoom (id 0), el fade / la envolvente que se mueve,
// o cuántas rebanadas hay (id 1)
void InstrumentView::waveNotice(FourCC id) {
	if (!gridMode_ || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	char t[48] ;
	if (id==0) sprintf(t,"zoom x%d",waveZoom_) ;
	else if (id==1) sprintf(t,"%d rebanadas",int(si->GetMarks().size())+1) ;
	else {
		int sv=si->GetSimpler(id) ;
		const char *sn=(id==SIP_S_FIN)?"fade in":(id==SIP_S_FOUT)?"fade out":(id==SIP_S_ATK)?"attack":
		               (id==SIP_S_DEC)?"decay":(id==SIP_S_SUS)?"sustain":"release" ;
		if (id==SIP_S_SUS) sprintf(t,"%s %ddB",sn,sv) ;
		else if (sv>=1000) sprintf(t,"%s %d.%ds",sn,sv/1000,(sv%1000)/100) ;
		else sprintf(t,"%s %dms",sn,sv) ;
	}
	View::SetNotification(t) ;
}

// el parámetro de 'cómo' de la parada elegida en la onda
FourCC InstrumentView::waveStopID() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return 0 ;
	int smode=((SampleInstrument *)current_)->GetSimpler(SIP_S_MODE) ;
	if (smode==SM_ONESHOT) return (waveStop_==3)?SIP_S_FOUT:(waveStop_==0?SIP_S_FIN:0) ;
	if (smode==SM_CLASSIC) {
		static const FourCC ids[4]={SIP_S_ATK,SIP_S_DEC,SIP_S_SUS,SIP_S_REL} ;
		return ids[(waveStop_<0 || waveStop_>3)?0:waveStop_] ;
	}
	return 0 ;
}

// trk2als: al soltar A en el dibujo de las rebanadas: la marca nueva (A en
// vacío) o la agarrada se acomoda en el cruce por cero, y suena su rebanada
void InstrumentView::waveARelease() {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int c=markVar_.GetInt(), at=-1 ;
	if (waveFlag_) {   // soltó una bandera: suena desde el inicio, o el último segundo hasta el final
		int ra,rb ;
		si->SimplerRegion(ra,rb) ;
		SoundSource *src=si->currentSource() ;
		int sec=src?src->GetSampleRate(-1):44100 ;
		if (waveFlag_==1) startPreview(ra,rb) ;
		else startPreview((rb-sec>ra)?rb-sec:ra,rb) ;
		waveFlag_=0 ;
		isDirty_=true ;
		return ;
	}
	if (waveAPut_) {
		waveAPut_=false ;
		at=si->AddMark(c) ;
		if (at<0) View::SetNotification("muy cerca de otra marca") ;
		else waveNotice(1) ;
	} else if (waveSel_==1) {
		std::vector<int> m=si->GetMarks() ;
		int k=nearMark(m) ;
		if (k>=0) {
			int old=m[k] ;
			m.erase(m.begin()+k) ;
			si->SetMarks(m) ;
			at=si->AddMark(c) ;
			if (at<0) {                          // no cupo en el cruce: se queda donde estaba
				m.push_back(old) ;
				std::sort(m.begin(),m.end()) ;
				si->SetMarks(m) ;
				at=old ;
			}
		}
		waveSel_=0 ;
	}
	if (at>=0) markVar_.SetInt(at) ;   // (ya sonó al apretar A y al moverla: al soltar, nada)
	isDirty_=true ;
}


// trk2als: modo soundfont: el teclado (0..127) abajo y una franja por zona
// encima, con su raíz marcada; la zona de la última nota tocada resaltada
void InstrumentView::drawZoneMap(SampleInstrument *si,int X,int Y,int W,int H) {
	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi=ColorOf(CD_HILITE1) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	GUIColor dim2=Mix(bg,fg,55) ;
	const std::vector<ZoneSource::Zone> &zones=si->Zones() ;
	const int keys=128 ;
	#define KX(k) (X+int((long long)(k)*W/keys))
	// teclado
	int ky=Y+H-18, kh=18 ;
	FillRect(X,ky,W,kh,Mix(bg,fg,80)) ;
	static const bool black[12]={false,true,false,true,false,false,true,false,true,false,true,false} ;
	for (int k=0;k<keys;k++) {
		if (black[k%12]) FillRect(KX(k),ky,KX(k+1)-KX(k),kh*6/10,Mix(bg,fg,15)) ;
		if (k%12==0) {   // las C con su octava (C3 = 60)
			FillRect(KX(k),ky,1,kh,Mix(bg,fg,40)) ;
		}
	}
	// zona de la última nota (o la del C3)
	int last=si->LastNote() ;
	if (last<0) last=60 ;
	int cur=-1, best=1000 ;
	for (size_t i=0;i<zones.size();i++) {
		int d=(last<zones[i].lo)?zones[i].lo-last:(last>zones[i].hi)?last-zones[i].hi:0 ;
		if (d<best) { best=d ; cur=int(i) ; }
	}
	// franjas
	int zy=Y+4, zh=ky-zy-6 ;
	for (size_t i=0;i<zones.size();i++) {
		const ZoneSource::Zone &z=zones[i] ;
		int x0=KX(z.lo), x1=KX(z.hi+1) ;
		bool on=(int(i)==cur) ;
		GUIColor c=on?Mix(bg,hi,70):Mix(bg,(i%2)?hi2:hi,30) ;
		FillRect(x0+1,zy,x1-x0-2>1?x1-x0-2:1,zh,c,4) ;
		// raíz
		if (z.root>=0 && z.root<keys) {
			int rx=KX(z.root)+(KX(z.root+1)-KX(z.root))/2 ;
			FillRect(rx,zy+zh-10,2,10,on?bg:fg) ;
			FillRect(rx,ky,2,kh,hi2) ;
		}
	}
	// nota tocada en el teclado
	FillRect(KX(last),ky+kh-5,KX(last+1)-KX(last)>2?KX(last+1)-KX(last):2,5,hi) ;
	// texto: zona actual
	if (cur>=0) {
		const ZoneSource::Zone &z=zones[cur] ;
		static const char *names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"} ;
		char info[96] ;
		snprintf(info,sizeof(info),"%s%d-%s%d  raiz %s%d  %s",names[z.lo%12],z.lo/12-2,names[z.hi%12],z.hi/12-2,
		         names[z.root%12],z.root/12-2,z.name.c_str()) ;
		topInfo_=info ;
	}
	#undef KX
}

/************************************************************
 trk2als: editor de kits
 ************************************************************/

int InstrumentView::padNote() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return 36 ;
	return ((SampleInstrument *)current_)->KitBase()+padSelVar_.GetInt() ;
}

bool InstrumentView::isPadField(FourCC id) {
	return id==IV_PADSEL || id==IV_PADSMP || id==IV_PADGAIN || id==IV_PADTUNE ||
	       id==IV_PADPAN || id==IV_PADDEC || id==IV_PADCHK || id==IV_PADST ;
}

// los campos toman los valores del pad elegido
void InstrumentView::loadPadVars() {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int sel=padSelVar_.GetInt() ;
	if (sel<0 || sel>15) { sel=0 ; padSelVar_.SetInt(0) ; }
	int note=padNote() ;
	PadParams p=si->GetPad(note) ;
	padGainVar_.SetInt(p.gain) ;
	padTuneVar_.SetInt(p.tune) ;
	padPanVar_.SetInt(p.pan) ;
	padDecayVar_.SetInt(p.decay) ;
	padChokeVar_.SetInt(p.choke) ;
	padStartVar_.SetInt(p.start) ;
	std::string wav=si->PadSample(note) ;
	padSmpVar_.SetInt(wav.empty()?-1:SamplePool::GetInstance()->IndexOf(wav.c_str())) ;
	padShown_=sel ;
}

// lo que cambió en los campos pasa al kit
void InstrumentView::kitSync() {
	if (!kitMode_ || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (padSelVar_.GetInt()!=padShown_) {   // otro pad: se oye, como en el Move
		loadPadVars() ;
		if (!gridMode_) previewPad() ;   // con cajas: suena con A, no al pasar
		isDirty_=true ;
		return ;
	}
	int note=padNote() ;
	PadParams p ;
	p.gain=padGainVar_.GetInt() ; p.tune=padTuneVar_.GetInt() ; p.pan=padPanVar_.GetInt() ;
	p.decay=padDecayVar_.GetInt() ; p.choke=padChokeVar_.GetInt() ; p.start=padStartVar_.GetInt() ;
	PadParams q=si->GetPad(note) ;
	if (p.gain!=q.gain || p.tune!=q.tune || p.pan!=q.pan || p.decay!=q.decay ||
	    p.choke!=q.choke || p.start!=q.start) {
		si->SetPad(note,p) ;
		// en las cajas del pad también se oye lo que se mueve (con la regla
		// del kit: si el clip ya toca ese pad, se oye el clip)
		if (gridMode_) previewPad() ;
		isDirty_=true ;
	}
	int idx=padSmpVar_.GetInt() ;
	SamplePool *sp=SamplePool::GetInstance() ;
	std::string want=(idx>=0 && idx<sp->GetNameListSize())?sp->GetNameList()[idx]:"" ;
	if (want!=si->PadSample(note)) {
		padBusy_=true ;
		si->SetPadSample(note,want) ;
		padBusy_=false ;
		refreshKit() ;
		previewPad() ;
	}
}

// el kit cambió: otra vez los campos (el nombre y los pads) con el foco donde estaba
void InstrumentView::refreshKit() {
	UIField *f=GetFocus() ;
	if (f && !f->IsStatic()) lastFocusID_=((UIIntVarField *)f)->GetVariableID() ;
	onInstrumentChange() ;
	isDirty_=true ;
}

// el golpe del pad tal como queda: se toca el kit de verdad en el canal de
// su pista (con el volumen, la afinación, el paneo, el inicio y el decay)
void InstrumentView::previewPad() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int note=padNote() ;
	if (si->PadSample(note).empty()) return ;
	int ch=viewData_->songX_ ;
	if (ch<0 || ch>=SONG_CHANNEL_COUNT) ch=0 ;
	Player *player=Player::GetInstance() ;
	if (player->IsRunning()) {
		// trk2als: sonando: el pad se oye solo si el clip de la pista no lo
		// toca (si ya trae notas de ese pad, se oye el clip)
		int tr=viewData_->project_->GetTranspose() ;
		if (player->ClipTickPos(ch)>=0) {
			unsigned char c=viewData_->currentPlayChain_[ch] ;
			if (c<CLIP_COUNT) {
				Clip &clip=viewData_->song_->clips_->Get(c) ;
				for (size_t i=0;i<clip.notes.size();i++) if (clip.notes[i].pitch+tr==note) return ;
			}
		}
		MixerService::GetInstance()->Lock() ;
		player->LiveNoteOn(ch,si,note-tr,100) ;
		MixerService::GetInstance()->Unlock() ;
		return ;
	}
	if (previewing_) stopPreview() ;
	player->PreviewNote(ch,si,note) ;
}

// el kit entero (todas sus variables), para deshacer
void InstrumentView::kitTake(std::vector<std::string> &snap) {
	snap.clear() ;
	IteratorPtr<Variable> it(current_->GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &v=it->CurrentItem() ;
		if (v.GetID()==SIP_SAMPLE) {
			char b[16] ; snprintf(b,sizeof(b),"%d",v.GetInt()) ; snap.push_back(b) ;
		} else snap.push_back(v.GetString()) ;
	}
	// trk2als: y el nombre de la pista (deshacer un cambio de kit lo regresa)
	int tr=ownerTrack(viewData_) ;
	if (tr>=0) snap.push_back(viewData_->project_->GetTrackName(tr)) ;
}

void InstrumentView::kitRestore(const std::vector<std::string> &snap) {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	padBusy_=true ;
	IteratorPtr<Variable> it(current_->GetIterator()) ;
	size_t k=0 ;
	for (it->Begin();!it->IsDone() && k<snap.size();it->Next(),k++) {
		Variable &v=it->CurrentItem() ;
		if (v.GetID()==SIP_SAMPLE) v.SetInt(atoi(snap[k].c_str())) ;
		else if (strcmp(v.GetString(),snap[k].c_str())) v.SetString(snap[k].c_str()) ;
	}
	si->SetZones(si->FindVariable(SIP_S_ZONES)->GetString()) ;
	int tr=ownerTrack(viewData_) ;
	if (tr>=0 && k<snap.size()) viewData_->project_->SetTrackName(tr,snap[k].c_str()) ;
	padBusy_=false ;
	refreshKit() ;
	kitTake(kitState_) ;
}

// después de cada botón: si el kit cambió, lo de antes va a deshacer
void InstrumentView::kitTrack() {
	if (!kitMode_ || !current_) { kitUndo_.clear() ; kitRedo_.clear() ; kitOwner_=0 ; return ; }
	std::vector<std::string> now ;
	kitTake(now) ;
	if (kitOwner_!=current_) {   // otro instrumento: historia nueva
		kitOwner_=current_ ;
		kitUndo_.clear() ; kitRedo_.clear() ;
		kitState_=now ;
		return ;
	}
	if (now!=kitState_) {
		kitUndo_.push_back(kitState_) ;
		if (kitUndo_.size()>64) kitUndo_.erase(kitUndo_.begin()) ;
		kitRedo_.clear() ;
		kitState_=now ;
	}
}

static const int decaySteps[]={0,10,20,30,40,50,75,100,150,200,250,300,400,500,750,1000,1500,2000,3000,5000} ;

// sobre un pad: X, Y y L con flechas cambian el pad sin bajar a los campos
bool InstrumentView::padShortcut(unsigned short mask) {
	const unsigned short arrows=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
	unsigned short mod=mask&~arrows ;
	unsigned short dir=mask&arrows ;
	if (!dir || (mod!=EPBM_X && mod!=EPBM_Y && mod!=EPBM_L)) return false ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int note=padNote() ;
	if (si->PadSample(note).empty()) { View::SetNotification("pad vacío") ; return true ; }
	PadParams p=si->GetPad(note) ;
	bool vert=(dir==EPBM_UP || dir==EPBM_DOWN) ;
	int d=(dir==EPBM_UP || dir==EPBM_RIGHT)?1:-1 ;
	char msg[48] ;
	if (mod==EPBM_X && vert) {
		p.gain=std::max(-36,std::min(12,p.gain+d)) ;
		snprintf(msg,sizeof(msg),"volumen %+ddB",p.gain) ;
	} else if (mod==EPBM_X) {
		int n=sizeof(decaySteps)/sizeof(int), k=0 ;
		while (k<n-1 && decaySteps[k]<p.decay) k++ ;
		if (decaySteps[k]!=p.decay && d<0) k-- ;
		k=std::max(0,std::min(n-1,k+(decaySteps[k]==p.decay?d:(d>0?1:0)))) ;
		p.decay=decaySteps[k] ;
		if (p.decay) snprintf(msg,sizeof(msg),"decay %dms",p.decay) ;
		else strcpy(msg,"decay: todo") ;
	} else if (mod==EPBM_Y && vert) {
		p.tune=std::max(-24,std::min(24,p.tune+d)) ;
		snprintf(msg,sizeof(msg),"afinación %+dst",p.tune) ;
	} else if (mod==EPBM_Y) {
		p.pan=std::max(-50,std::min(50,p.pan+d*5)) ;
		snprintf(msg,sizeof(msg),"paneo %+d",p.pan) ;
	} else if (vert) {
		p.choke=std::max(0,std::min(16,p.choke+d)) ;
		if (p.choke) snprintf(msg,sizeof(msg),"choke %d",p.choke) ;
		else strcpy(msg,"choke: no") ;
	} else {
		p.start=std::max(0,std::min(99,p.start+d)) ;
		snprintf(msg,sizeof(msg),"inicio %d%%",p.start) ;
	}
	si->SetPad(note,p) ;
	loadPadVars() ;
	View::SetNotification(msg) ;
	previewPad() ;
	isDirty_=true ;
	return true ;
}

void InstrumentView::openPadBrowser() {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	viewMode_=VM_NORMAL ;
	Path sampleLib(SamplePool::GetInstance()->GetSampleLib()) ;
	if (FileSystem::GetInstance()->GetFileType(sampleLib.GetPath().c_str())!=FT_DIR) {
		DoModal(new MessageBox(*this,"Can't access the samplelib",MBBF_OK)) ;
		return ;
	}
	padImport_=true ;
	padImportPrev_=si->FindVariable(SIP_SAMPLE)->GetInt() ;
	padBusy_=true ;   // el navegador cambia 'sample' del instrumento
	// trk2als: el navegador nuevo; se abre donde está el sample del pad
	PresetDialog *pd=new PresetDialog(*this) ;
	std::string cur=si->PadSample(padNote()) ;
	pd->SetPick(PresetDialog::PICK_WAV,viewData_->currentInstrument_,
	            cur.empty()?-1:SamplePool::GetInstance()->IndexOf(cur.c_str())) ;
	DoModal(pd,PadImportCallback) ;
}

// A suena el pad (en uno vacío abre el navegador; A,A en el sample también);
// B+A corta el pad; Select+A pega el pad copiado (B lo copia).
// Select+L deshace y Select+R rehace (en cualquier campo del kit)
// otro kit en este instrumento (de los presets que son kits)
void InstrumentView::OnKitLoad(const std::string &name) {
	if (name.empty() || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	padBusy_=true ;
	int tr=ownerTrack(viewData_) ;
	InstrumentPresets::Prepare(name) ;
	if (tr>=0) Player::GetInstance()->HoldTrack(tr,true) ;
	bool ok=InstrumentPresets::Load((SampleInstrument *)current_,name) ;
	if (tr>=0) Player::GetInstance()->HoldTrack(tr,false) ;
	padBusy_=false ;
	std::string shortName=name.substr(name.rfind('/')==std::string::npos?0:name.rfind('/')+1) ;
	if (ok && ownedByTrack()) viewData_->project_->SetTrackName(viewData_->songX_,shortName.c_str()) ;
	View::SetNotification(ok?(shortName+" cargado").c_str():"No se pudo cargar el kit") ;
	padShown_=-1 ;
	lastFocusID_=SIP_SAMPLE ;
	onInstrumentChange() ;
	isDirty_=true ;
}

// Select + L / R en un kit: los pads (lo llama Undo)
void InstrumentView::kitUndo(bool undo) {
	kitTrack() ;   // lo que haya cambiado sin botón (el navegador)
	std::vector<std::vector<std::string> > &from=undo?kitUndo_:kitRedo_ ;
	std::vector<std::vector<std::string> > &to=undo?kitRedo_:kitUndo_ ;
	if (from.empty()) { View::SetNotification(undo?"nada que deshacer":"nada que rehacer") ; return ; }
	to.push_back(kitState_) ;
	std::vector<std::string> snap=from.back() ;
	from.pop_back() ;
	kitRestore(snap) ;
	View::SetNotification(undo?"deshecho":"rehecho") ;
	isDirty_=true ;
}

bool InstrumentView::processKit(unsigned short mask) {
	UIField *f=GetFocus() ;
	if (!f || f->IsStatic()) return false ;
	FourCC id=((UIIntVarField *)f)->GetVariableID() ;
	if (id==SIP_SAMPLE) {   // el nombre del kit: A cambia de kit (solo kits)
		if (mask==EPBM_A) {
			PresetDialog *pd=new PresetDialog(*this,false,InstrumentPresets::PK_KIT) ;
			SoundLauncher *l=SoundLauncher::Current() ;   // trk2als: lanza y entra en el 1
			if (l) {
				l->Open(viewData_->currentInstrument_,this) ;
				pd->SetLauncher(l) ;
			}
			DoModal(pd,KitLoadCallback) ;
			return true ;
		}
		return (mask&EPBM_A)!=0 && !(mask&EPBM_B) ;   // A + flechas: nada (no es un sample)
	}
	if (gridMode_) {   // Y + flechas: otro pad, sin salir de la caja donde estás
		const unsigned short arr=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
		unsigned short d=mask&arr ;
		if ((mask&~arr)==EPBM_Y && d && id!=IV_PADSEL) {   // (sobre un pad, Y + flechas es su atajo)
			int sel=padSelVar_.GetInt(), r=sel/4, c=sel%4 ;
			if (d==EPBM_LEFT && c>0) c-- ;
			else if (d==EPBM_RIGHT && c<3) c++ ;
			else if (d==EPBM_UP && r<3) r++ ;
			else if (d==EPBM_DOWN && r>0) r-- ;
			padSelVar_.SetInt(r*4+c) ;
			kitSync() ;
			isDirty_=true ;
			return true ;
		}
	}
	// editando las cajas del pad: de la fila de arriba no se sube a los pads
	if (gridMode_ && padEdit_ && mask==EPBM_UP && f->GetPosition()._y<=20) return true ;
	if (!isPadField(id)) return false ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int note=padNote() ;
	if (!gridMode_ && padShortcut(mask)) return true ;
	// con cajas: sobre un pad, A + flechas es "la clavija" (lo que más se le
	// mueve a un golpe): A + arriba/abajo el volumen, A + izq/der el largo
	if (gridMode_ && id==IV_PADSEL) {
		const unsigned short arr=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
		unsigned short d=mask&arr ;
		if ((mask&~arr)==EPBM_A && d) {
			padHoldAt_=0 ; padPlus_=false ;
			if (si->PadSample(note).empty()) return true ;
			PadParams p=si->GetPad(note) ;
			char msg[48] ;
			if (d==EPBM_UP || d==EPBM_DOWN) {
				p.gain=std::max(-36,std::min(12,p.gain+((d==EPBM_UP)?1:-1))) ;
				snprintf(msg,sizeof(msg),"volumen %+ddB",p.gain) ;
			} else {
				// el largo del golpe: hacia la izquierda más corto (de todo a
				// 10 ms), hacia la derecha más largo
				p.decay=padLongStep(p.decay,(d==EPBM_LEFT)?1:-1) ;
				if (p.decay) snprintf(msg,sizeof(msg),"largo %dms",p.decay) ;
				else strcpy(msg,"largo: todo") ;
			}
			si->SetPad(note,p) ;
			loadPadVars() ;
			View::SetNotification(msg) ;
			previewPad() ;
			isDirty_=true ;
			return true ;
		}
	}
	if (id==IV_PADSEL) {   // en la rejilla: las flechas (con o sin A) van de pad en pad
		const unsigned short arrows=EPBM_UP|EPBM_DOWN|EPBM_LEFT|EPBM_RIGHT ;
		unsigned short dir=mask&arrows ;
		if (mask!=EPBM_A) viewMode_=VM_NORMAL ;
		if (dir && (mask&~(arrows|EPBM_A))==0) {
			int sel=padSelVar_.GetInt(), r=sel/4, c=sel%4 ;
			if (dir==EPBM_LEFT && c>0) c-- ;
			else if (dir==EPBM_RIGHT && c<3) c++ ;
			else if (dir==EPBM_UP && r<3) r++ ;        // el pad 1 está abajo
			else if (dir==EPBM_DOWN && r>0) r-- ;
			else if (mask==EPBM_DOWN) return gridMode_ && page_[1]==0 ;   // la orilla de abajo: a los campos (en PAD no: Select)
			else if (mask==EPBM_UP && !ownedByTrack()) return false ;   // arriba, el kit (si se puede cambiar aquí)
			else return true ;
			padSelVar_.SetInt(r*4+c) ;
			kitSync() ;   // el pad nuevo suena
			isDirty_=true ;
			return true ;
		}
	}
	bool whole=(id==IV_PADSEL || id==IV_PADSMP) ;
	if (mask==EPBM_B && whole) {   // copiar
		padClipWav_=si->PadSample(note) ;
		padClipP_=si->GetPad(note) ;
		padClip_=!padClipWav_.empty() ;
		padClipCut_=false ;
		if (padClip_) View::SetNotification("pad copiado") ;
		return false ;
	}
	if (mask==(EPBM_B|EPBM_A) && whole) {   // cortar: el pad queda vacío
		padBusy_=true ;
		si->SetPadSample(note,"") ;
		si->SetPad(note,PadParams()) ;
		padBusy_=false ;
		padClipCut_=padClip_ ;
		refreshKit() ;
		View::SetNotification(padClip_?"pad cortado":"pad vacío") ;
		return true ;
	}
	if (mask==(EPBM_SELECT|EPBM_A) && whole) {   // pegar (encima de otro: lo reemplaza)
		if (!padClip_) { View::SetNotification("nada que pegar") ; return true ; }
		std::string oldWav=si->PadSample(note) ;
		PadParams oldP=si->GetPad(note) ;
		padBusy_=true ;
		si->SetPadSample(note,padClipWav_) ;
		si->SetPad(note,padClipP_) ;
		padBusy_=false ;
		refreshKit() ;
		previewPad() ;
		// lo cortado cae encima de otro pad: el de antes queda en la mano,
		// para ponerlo en el hueco (así se reacomodan los sonidos)
		if (padClipCut_ && !oldWav.empty() && oldWav!=padClipWav_) {
			padClipWav_=oldWav ;
			padClipP_=oldP ;
			View::SetNotification("pegado; el de antes quedó cortado") ;
		} else {
			padClipCut_=false ;
			View::SetNotification("pad pegado") ;
		}
		return true ;
	}
	if (mask==EPBM_A && gridMode_ && id==IV_PADSEL) {
		if (si->PadSample(note).empty()) { padAPending_=true ; return false ; }   // vacío: al soltar, el navegador
		padHoldAt_=System::GetInstance()->GetClock() ;   // sostenida: el +
		padPlus_=false ;
		previewPad() ;
		return true ;
	}
	if (mask==EPBM_A) {
		if ((id==IV_PADSMP || id==IV_PADSEL) && viewMode_==VM_NEW) {   // A,A: otro sample de la samplelib
			openPadBrowser() ;
			return true ;
		}
		if (id==IV_PADSEL && si->PadSample(note).empty()) {   // pad vacío: al soltar A, el navegador
			padAPending_=true ;
			return false ;
		}
		viewMode_=(id==IV_PADSMP || id==IV_PADSEL)?VM_NEW:VM_NORMAL ;
		previewPad() ;
		return true ;
	}
	return false ;
}

// regresó el navegador: lo importado va al pad
void InstrumentView::OnPadImport(bool ok) {
	padBusy_=false ;
	padImport_=false ;
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	Variable *sv=si->FindVariable(SIP_SAMPLE) ;
	int idx=sv->GetInt() ;
	SamplePool *sp=SamplePool::GetInstance() ;
	padBusy_=true ;
	if (ok && idx>=0 && idx<sp->GetNameListSize() && idx!=padImportPrev_) {
		si->SetPadSample(padNote(),sp->GetNameList()[idx]) ;
	} else {
		sv->SetInt(padImportPrev_) ;
		si->SetZones(si->FindVariable(SIP_S_ZONES)->GetString()) ;
	}
	padBusy_=false ;
	lastFocusID_=IV_PADSEL ;
	onInstrumentChange() ;
	previewPad() ;
	isDirty_=true ;
}

// la rejilla: 4 filas de 4, el pad 1 abajo a la izquierda (como el Move);
// el elegido resaltado, los vacíos apagados
void InstrumentView::drawPadGrid(SampleInstrument *si,int X,int Y,int W,int H) {
	GUIColor bg=ColorOf(CD_BACKGROUND) ;
	GUIColor fg=ColorOf(CD_NORMAL) ;
	GUIColor hi=ColorOf(CD_HILITE1) ;
	GUIColor hi2=ColorOf(CD_HILITE2) ;
	int base=si->KitBase() ;
	int sel=padSelVar_.GetInt() ;
	bool onGrid=GetFocus() && !GetFocus()->IsStatic() &&
	            ((UIIntVarField *)GetFocus())->GetVariableID()==IV_PADSEL ;
	const int gap=4 ;
	int cw=(W-3*gap)/4, ch=(H-3*gap)/4 ;
	for (int r=0;r<4;r++) {
		for (int c=0;c<4;c++) {
			int idx=r*4+c ;
			int note=base+idx ;
			int x=X+c*(cw+gap), y=Y+(3-r)*(ch+gap) ;
			std::string name=si->PadName(note) ;
			bool on=(idx==sel) ;
			PadParams p=si->GetPad(note) ;
			GUIColor box=name.empty()?Mix(bg,fg,12):Mix(bg,hi,(p.choke?45:30)) ;
			if (on && onGrid) box=hi2 ;
			FillRect(x,y,cw,ch,box,4) ;
			if (on && !onGrid) FrameRect(x,y,cw,ch,hi2,4,2) ;   // elegido, pero el cursor está abajo
			GUIColor tc=(on && onGrid)?bg:(name.empty()?Mix(bg,fg,40):fg) ;
			std::string t=name.empty()?padNoteName(note):name ;
			if (t.size()>16) t=t.substr(0,16) ;
			DrawText(x+6,y+(ch-12)/2,t.c_str(),tc,0) ;
		}
	}
	// debajo de la rejilla: el pad elegido (su sample y sus ajustes)
	int note=base+sel ;
	std::string wav=si->PadSample(note) ;
	PadParams p=si->GetPad(note) ;
	char l1[96], l2[96], dec[16], chk[12] ;
	snprintf(l1,sizeof(l1),"pad %d  %s  %s",sel+1,padNoteName(note).c_str(),wav.empty()?"(vacío: A abre el navegador)":wav.c_str()) ;
	if (p.decay) snprintf(dec,sizeof(dec),"%dms",p.decay) ; else strcpy(dec,"todo") ;
	if (p.choke) snprintf(chk,sizeof(chk),"%d",p.choke) ; else strcpy(chk,"no") ;
	snprintf(l2,sizeof(l2),"vol %+ddB  tono %+d  pan %+d  decay %s  choke %s  inicio %d%%",
	         p.gain,p.tune,p.pan,dec,chk,p.start) ;
	int ty=Y+H+10 ;
	DrawText(X,ty,l1,fg,0) ;
	if (!wav.empty()) DrawText(X,ty+16,l2,Mix(bg,fg,70),0) ;
	char info[96] ;
	snprintf(info,sizeof(info),"pad %d %s",sel+1,padNoteName(note).c_str()) ;
	topInfo_=info ;
}

// las 3 letras de un pad: lo que es (KCK, SNA, CHH...) si el nombre lo dice;
// si no, las primeras 3 de su palabra más larga
static std::string padAbbrev(const std::string &name) {
	std::string low ;
	for (size_t i=0;i<name.size();i++) low+=(char)tolower((unsigned char)name[i]) ;
	static const char *keys[][2]={
		{"kick","KCK"},{"bombo","KCK"},{"bd","KCK"},{"snare","SNA"},{"tarola","SNA"},{"caja","SNA"},
		{"clap","CLP"},{"rim","RIM"},{"openhat","OHH"},{"open","OHH"},{"ohh","OHH"},{"closed","CHH"},
		{"chh","CHH"},{"hihat","CHH"},{"hat","CHH"},{"hh","CHH"},{"tom","TOM"},{"crash","CRA"},
		{"ride","RID"},{"shaker","SHK"},{"cowbell","COW"},{"cencerro","COW"},{"conga","CNG"},
		{"bongo","BNG"},{"guira","GUI"},{"guiro","GUI"},{"clave","CLV"},{"tamb","TMB"},{"perc","PER"},
		{"cymbal","CYM"},{"fx","FX"},{0,0}} ;
	for (int k=0;keys[k][0];k++) if (low.find(keys[k][0])!=std::string::npos) return keys[k][1] ;
	std::string best, cur ;
	for (size_t i=0;i<=low.size();i++) {
		if (i<low.size() && isalpha((unsigned char)low[i])) cur+=low[i] ;
		else { if (cur.size()>best.size()) best=cur ; cur.clear() ; }
	}
	if (best.empty()) best=low ;
	best=best.substr(0,3) ;
	for (size_t i=0;i<best.size();i++) best[i]=(char)toupper((unsigned char)best[i]) ;
	return best ;
}

// trk2als: el kit en la rejilla: a la izquierda los 16 pads (el 1 abajo,
// como en Live) con 3 letras; a la derecha la onda del pad elegido. Un pad
// que acaba de sonar (el clip, la consola o el controlador) se prende
void InstrumentView::drawKit(SampleInstrument *si) {
	GUIColor bg=ColorOf(CD_BACKGROUND), fg=ColorOf(CD_NORMAL), play=ColorOf(CD_PLAY) ;
	GUIColor panel=Mix(bg,fg,9) ;
	int base=si->KitBase() ;
	int sel=padSelVar_.GetInt() ;
	bool onGrid=GetFocus() && !GetFocus()->IsStatic() &&
	            ((UIIntVarField *)GetFocus())->GetVariableID()==IV_PADSEL ;
	unsigned long now=System::GetInstance()->GetClock() ;
	const int PW=52, PH=46, G=4, PX=8, PY=40 ;
	for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
		int idx=r*4+c, note=base+idx ;
		int x=PX+c*(PW+G), y=PY+(3-r)*(PH+G) ;
		std::string name=si->PadName(note) ;
		bool on=(idx==sel) ;
		unsigned long hit=si->PadHit(note) ;
		bool lit=hit && now>=hit && now-hit<160 ;
		// lleno = está sonando; marco = el elegido; doble marco = la cruz está aquí
		if (name.empty()) FrameRect(x,y,PW,PH,Mix(bg,fg,22),5,2) ;
		else {
			FillRect(x,y,PW,PH,lit?play:panel,5) ;
			DrawText(x+2,y+15,padAbbrev(name).c_str(),lit?bg:fg,1) ;
		}
		if (on) FrameRect(x,y,PW,PH,fg,5,3) ;
		if (on && onGrid) FrameRect(x-4,y-4,PW+8,PH+8,fg,8,2) ;
		// el +: en un pad vacío con la cruz encima, o con A sostenida (cambiar el sonido)
		if (on && onGrid && (name.empty() || padPlus_)) {
			if (!name.empty()) FillRect(x+3,y+3,PW-6,PH-6,bg,3) ;
			FillRect(x+PW/2-10,y+PH/2-2,20,4,fg) ;
			FillRect(x+PW/2-2,y+PH/2-10,4,20,fg) ;
		}
	}
	// en el hueco de la primera caja de PAD: de qué pad son los valores
	// (su número y sus 3 letras)
	if (page_[1]==0) {
		int cx,cy,cw,ch ;
		gridBox(0,cx,cy,cw,ch) ;
		(void)cw ; (void)ch ;
		std::string nm=si->PadName(base+sel) ;
		char num[8] ;
		snprintf(num,sizeof(num),"%d",sel+1) ;
		DrawText(cx+12,cy+10,"PAD",Mix(bg,fg,55),1) ;
		DrawText(cx+12,cy+32,num,fg,2) ;
		DrawText(cx+16+24*int(strlen(num)),cy+48,nm.empty()?"--":padAbbrev(nm).c_str(),fg,1) ;
	}
	// la onda del pad elegido
	const int WX=236, WY=40, WW=396, WH=198 ;
	FillRect(WX,WY,WW,WH,panel,8) ;
	int note=base+sel ;
	std::string wav=si->PadSample(note) ;
	char info[96] ;
	snprintf(info,sizeof(info),"%s",wav.empty()?padNoteName(note).c_str():si->PadName(note).c_str()) ;
	topInfo_=info ;
	if (wav.empty()) {
		if (onGrid) DrawText(WX+16,WY+WH/2-8,"A: elegir sonido",Mix(bg,fg,50),1) ;
		return ;
	}
	SamplePool *sp=SamplePool::GetInstance() ;
	int index=sp->IndexOf(wav.c_str()) ;
	SoundSource *src=(index>=0)?sp->GetSource(index):0 ;
	if (!src) return ;
	int size=src->GetSize(-1) ;
	short *data=(short *)src->GetSampleBuffer(-1) ;
	int channels=src->GetChannelCount(-1) ;
	if (size<=0 || !data || channels<1) return ;
	const int NB=76 ;   // barras de 3 px cada 5
	if (kitWfIndex_!=index || kitWfSize_!=size) {
		for (int b=0;b<NB;b++) {
			int from=int((long long)size*b/NB), to=int((long long)size*(b+1)/NB) ;
			int step=(to-from)/200+1, peak=0 ;
			for (int i=from;i<to;i+=step) { int v=data[i*channels] ; if (v<0) v=-v ; if (v>peak) peak=v ; }
			kitWf_[b]=(unsigned char)(peak>>7) ;
		}
		kitWfIndex_=index ; kitWfSize_=size ;
	}
	PadParams p=si->GetPad(note) ;
	int ix=WX+8, iw=WW-16, mid=WY+WH/2 ;
	int ax=ix+iw*p.start/100 ;   // el inicio
	for (int b=0;b<NB;b++) {
		int x=ix+b*5 ;
		int h=kitWf_[b]*(WH-24)/2/255+1 ;
		FillRect(x,mid-h,3,2*h,(x>=ax)?Mix(bg,fg,70):Mix(bg,fg,25)) ;
	}
	// la bandera del inicio y la del final
	FillRect(ax-1,WY+4,2,WH-8,fg) ;
	for (int q=0;q<20;q++) { int w=(q<10)?2*q+3:2*(20-q)+1 ; FillRect(ax,WY+4+q,w,1,fg) ; }
	int bx=ix+iw ;
	FillRect(bx-1,WY+4,2,WH-8,fg) ;
	for (int q=0;q<20;q++) { int w=(q<10)?2*q+3:2*(20-q)+1 ; FillRect(bx-w,WY+4+q,w,1,fg) ; }
	// el decay: la curva con la que se apaga el golpe
	if (p.decay>0) {
		float ms=size*1000.0f/src->GetSampleRate(-1) ;
		int dw=(ms>0)?int(iw*p.decay/ms):0 ;
		if (dw>bx-ax) dw=bx-ax ;
		std::vector<int> ys ;
		int top=WY+14, hh=WH-28 ;
		for (int x=0;x<dw;x++) { float t=float(x)/dw, g=(1-t)*(1-t) ; ys.push_back(top+int((1.0f-g)*hh)) ; }
		if (dw>2) drawCurveLine(ys,ax,TrackColor(viewData_->songX_),-1,-1,WX,WX+WW) ;
	}
}

/************************************************************
 trk2als: automatización desde la pantalla del instrumento
 ************************************************************/

// los parámetros del Sampler que suenan en vivo (los lee GetSimpler)
bool InstrumentView::automatable(FourCC id) {
	switch (id) {
		case SIP_S_START: case SIP_S_END: case SIP_S_LLEN:
		case SIP_S_TRANSP: case SIP_S_DETUNE: case SIP_S_GLIDE:
		case SIP_S_FTYPE: case SIP_S_FHZ: case SIP_S_FQ: case SIP_S_FST:
		case SIP_S_FATK: case SIP_S_FDEC: case SIP_S_FSUS: case SIP_S_FREL: case SIP_S_FKEY:
		case SIP_S_FIN: case SIP_S_FOUT:
		case SIP_S_ATK: case SIP_S_DEC: case SIP_S_SUS: case SIP_S_REL:
		case SIP_S_VOL: case SIP_S_PAN:
			return true ;
		default:
			return false ;
	}
}

int InstrumentView::autoChannel() {
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++)
		if (viewData_->project_->GetTrackInstrument(ch)==viewData_->currentInstrument_) return ch ;
	return -1 ;
}

int InstrumentView::autoClip(int ch) {
	if (ch<0) return -1 ;
	Player *player=Player::GetInstance() ;
	if (player->ClipTickPos(ch)>=0) return viewData_->currentPlayChain_[ch] ;
	// sin sonar: el de la celda de la Session donde está el cursor
	if (viewData_->songX_!=ch) return -1 ;
	unsigned char c=*viewData_->GetCurrentSongPointer() ;
	if (c>=CLIP_COUNT || viewData_->song_->clips_->Get(c).IsEmpty()) return -1 ;
	return c ;
}

static FourCC focusID(FieldView &fv) {
	UIField *f=fv.GetFocus() ;
	if (!f || f->IsStatic()) return 0 ;
	return ((UIIntVarField *)f)->GetVariableID() ;
}

void InstrumentView::toggleAutoRec() {
	Player *player=Player::GetInstance() ;
	if (player->AutoRecState()!=0) {
		if (touching_) endTouch() ;
		player->SetAutoRec(false) ;
		View::SetNotification("automatización: grabación apagada") ;
		isDirty_=true ;
		return ;
	}
	int ch=autoChannel() ;
	if (ch<0 || autoClip(ch)<0) {
		View::SetNotification("no hay clip en la pista para grabar") ;
		return ;
	}
	if (!player->IsRunning() || player->ClipTickPos(ch)<0) {
		// no suena: precuenta de 4 tiempos y arranca el clip de la celda
		// (desde su loop) ya grabando
		viewData_->songX_=ch ;
		viewData_->chainRow_=0 ;
		if (player->IsRunning()) player->Stop() ;
		player->RequestCountIn() ;
		player->OnStartButton(PM_CHAIN,ch,false,0) ;
		View::SetNotification("precuenta: 4 tiempos y graba") ;
	} else {
		// ya suena: empieza en el siguiente uno del compás
		player->ArmAutoRec() ;
		View::SetNotification("graba desde el siguiente compás") ;
	}
	isDirty_=true ;
}

// mover a mano un parámetro con automatización: arranca desde el valor que
// está sonando (sin saltos) y, si no se graba, manda la mano hasta que el clip
// vuelva a empezar; la lane no se toca
void InstrumentView::takeOver() {
	FourCC id=focusID(*this) ;
	if (!automatable(id) || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int av ;
	if (!si->GetAutomated(id,av)) return ;   // no está sonando automatizado
	Variable *v=si->FindVariable(id) ;
	if (v) v->SetInt(av) ;
	Player *player=Player::GetInstance() ;
	if (!player->IsAutoRec()) player->OverrideAutomation(autoChannel(),id) ;
}

// el rango, el formato y el nombre de un parámetro, para dibujar su curva
void InstrumentView::curveMeta(UIIntVarField *fld,CurveMeta &m) {
	m.instr=viewData_->currentInstrument_ ;
	m.min=fld->GetMin() ;
	m.max=fld->GetMax() ;
	m.step=fld->GetStep()>0?fld->GetStep():1 ;
	m.big=fld->GetBigStep()>0?fld->GetBigStep():m.step*10 ;
	m.disp=fld->GetDisplayOffset() ;
	m.log=fld->IsLog() ;
	// "volume: %+ddB" -> nombre "volume", formato "%+ddB"
	const char *fmt=fld->GetFormat() ;
	const char *colon=fmt?strstr(fmt,": "):0 ;
	if (colon) {
		int n=int(colon-fmt) ;
		if (n>23) n=23 ;
		memcpy(m.name,fmt,n) ;
		m.name[n]=0 ;
		snprintf(m.fmt,sizeof(m.fmt),"%s",colon+2) ;
	} else {
		snprintf(m.name,sizeof(m.name),"%s",fmt?fmt:"") ;
		snprintf(m.fmt,sizeof(m.fmt),"%%d") ;
	}
	if (m.log) snprintf(m.fmt,sizeof(m.fmt),"%%dHz") ;
	// los tiempos (attack, decay, glide...): como en Live, los valores chicos
	// ocupan más altura (si no, 40 a 120 ms de 5000 ni se ven)
	m.time=!m.log && m.min>=0 && strstr(m.fmt,"ms")!=0 ;
	// un parámetro de lista (el tipo de filtro): el formato lleva %s y se
	// imprime con el nombre, nunca con el número
	Variable &var=fld->GetVariable() ;
	m.list=0 ;
	m.listN=0 ;
	if (var.GetType()==Variable::CHAR_LIST && var.GetListPointer()) {
		m.list=var.GetListPointer() ;
		m.listN=var.GetListSize() ;
	} else if (strstr(m.fmt,"%s")) {
		snprintf(m.fmt,sizeof(m.fmt),"%%d") ;
	}
}

void InstrumentView::applyCurveMeta(const CurveMeta &m) {
	ViewData *vd=viewData_ ;
	vd->curveInstr_=m.instr ;
	vd->curveMin_=m.min ;
	vd->curveMax_=m.max ;
	vd->curveStep_=m.step ;
	vd->curveBig_=m.big ;
	vd->curveDisp_=m.disp ;
	vd->curveLog_=m.log ;
	vd->curveTime_=m.time ;
	memcpy(vd->curveName_,m.name,sizeof(vd->curveName_)) ;
	memcpy(vd->curveFmt_,m.fmt,sizeof(vd->curveFmt_)) ;
	vd->curveList_=m.list ;
	vd->curveListN_=m.listN ;
}

// A + L: al clip, a la curva del parámetro del foco
bool InstrumentView::openCurve() {
	UIField *f=GetFocus() ;
	if (!f || f->IsStatic() || !current_ || current_->GetType()!=IT_SAMPLE) return false ;
	UIIntVarField *fld=(UIIntVarField *)f ;
	FourCC id=fld->GetVariableID() ;
	if (!automatable(id)) return false ;
	int ch=autoChannel() ;
	int clip=autoClip(ch) ;
	if (clip<0) { View::SetNotification("sin clip") ; isDirty_=true ; return false ; }   // no hay dónde dibujarla
	if (touching_) cancelTouch() ;   // la A del combo no graba nada
	ViewData *vd=viewData_ ;
	CurveMeta m ;
	curveMeta(fld,m) ;
	applyCurveMeta(m) ;
	vd->curveID_=id ;
	vd->curveLastID_=id ;
	vd->currentChain_=clip ;
	vd->songX_=ch ;
	lastFocusID_=id ;
	ViewType vt=VT_CLIP ;
	ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
	SetChanged() ;
	NotifyObservers(&ve) ;
	return true ;
}

// el toque que empezó A (grabando) se deshace: como si no se hubiera tocado
void InstrumentView::cancelTouch() {
	if (!touching_) return ;
	touching_=false ;
	if (autoUndo_.empty()) return ;
	LaneSnap s=autoUndo_.back() ;
	autoUndo_.pop_back() ;
	MixerService::GetInstance()->Lock() ;
	viewData_->song_->clips_->Get(s.clip).lanes=s.lanes ;
	MixerService::GetInstance()->Unlock() ;
	Variable *v=(s.var && s.instr==current_) ? current_->FindVariable(s.var) : 0 ;
	if (v) v->SetInt(s.value) ;
}

void InstrumentView::beginTouch() { beginTouchID(focusID(*this)) ; }

void InstrumentView::beginTouchID(FourCC id) {
	if (!automatable(id) || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	int ch=autoChannel() ;
	int clip=autoClip(ch) ;
	if (clip<0 || Player::GetInstance()->ClipTickPos(ch)<0) return ;
	touching_=true ;
	touchID_=id ;
	touchCh_=ch ;
	touchClip_=clip ;
	touchLast_=-1 ;
	Clip &c=viewData_->song_->clips_->Get(clip) ;
	AutoLane *l=c.FindLane(id,-1) ;
	touchHadOld_=(l!=0) ;
	pushLaneSnap(clip) ;   // un toque grabado se deshace de una vez
	autoUndo_.back().var=id ;
	autoUndo_.back().value=preTouchVal_ ;
	if (l) touchOld_=*l ;
}

// escribe el valor del campo desde la última vez hasta donde va el clip: lo
// que había ahí se sobrescribe (y al dar la vuelta, desde el inicio del loop)
void InstrumentView::recordTouch() {
	if (!touching_) return ;
	Player *player=Player::GetInstance() ;
	int pos=player->ClipTickPos(touchCh_) ;
	if (pos<0 || viewData_->currentPlayChain_[touchCh_]!=touchClip_ || !player->IsAutoRec()) {
		endTouch() ;
		return ;
	}
	int value=current_->FindVariable(touchID_)->GetInt() ;
	MixerService::GetInstance()->Lock() ;   // el audio lee las lanes
	Clip &c=viewData_->song_->clips_->Get(touchClip_) ;
	AutoLane &lane=c.GetLane(touchID_,-1) ;
	std::vector<AutoPoint> &p=lane.points ;
	int from=touchLast_<0?pos:touchLast_+1 ;
	int loopFrom=(c.loopStart>=0?c.loopStart:0)*CLIP_STEPS_PER_PAGE*CLIP_TICKS ;
	bool wrapped=(touchLast_>=0 && pos<touchLast_) ;
	for (size_t k=0;k<p.size();) {
		bool gone=wrapped ? (p[k].pos>=from || (p[k].pos>=loopFrom && p[k].pos<=pos))
		                  : (p[k].pos>=from && p[k].pos<=pos) ;
		if (gone) p.erase(p.begin()+k) ;
		else k++ ;
	}
	if (wrapped) p.push_back(AutoPoint(loopFrom,value,false)) ;
	p.push_back(AutoPoint(pos,value,false)) ;
	lane.Sort() ;
	MixerService::GetInstance()->Unlock() ;
	touchLast_=pos ;
}

// al soltar: lo de después vuelve a lo que había (como touch en Live) y la
// lane se limpia (puntos que repiten el valor no cambian nada)
void InstrumentView::endTouch() {
	if (!touching_) return ;
	touching_=false ;
	MixerService::GetInstance()->Lock() ;
	Clip &c=viewData_->song_->clips_->Get(touchClip_) ;
	AutoLane *lane=c.FindLane(touchID_,-1) ;
	if (lane) {
		int v ;
		if (touchHadOld_ && touchLast_>=0 && touchOld_.ValueAt(touchLast_+1,v) &&
		    touchLast_+1<c.Steps()*CLIP_TICKS) {
			lane->points.push_back(AutoPoint(touchLast_+1,v,false)) ;
			lane->Sort() ;
		}
		std::vector<AutoPoint> out ;
		for (size_t k=0;k<lane->points.size();k++) {
			const AutoPoint &a=lane->points[k] ;
			if (!out.empty() && !out.back().ramp && !a.ramp && out.back().value==a.value) continue ;
			out.push_back(a) ;
		}
		lane->points=out ;
	}
	MixerService::GetInstance()->Unlock() ;
	isDirty_=true ;
}

/************************************************************
 trk2als: captura de automatización (R2)
 ************************************************************/

// trk2als: lo que se mueve desde un dibujo (la bandera, el corte, la
// resonancia, la envolvente) también se anota para la captura, igual que lo
// que se mueve en su caja. Hace falta que su caja esté en la página (de ahí
// sale su escala)
void InstrumentView::drawCapture(FourCC id,int before) {
	if (kitMode_ || !current_ || current_->GetType()!=IT_SAMPLE || !automatable(id)) return ;
	Player *player=Player::GetInstance() ;
	if (player->IsAutoRec()) {
		// grabando: se escribe en la curva, como A sostenida en su caja (hasta
		// soltar todo)
		if (touching_ && touchID_!=id) endTouch() ;
		if (!touching_) { preTouchVal_=before ; beginTouchID(id) ; }
		if (touching_) recordTouch() ;
		return ;
	}
	// si el parámetro ya traía curva sonando: lo último que se mueve se lo queda
	{
		int av ;
		if (((SampleInstrument *)current_)->GetAutomated(id,av)) player->OverrideAutomation(autoChannel(),id) ;
	}
	if (capping_ && capCur_.id!=id) capEnd() ;
	if (!capping_) {
		int ch=autoChannel() ;
		int clip=autoClip(ch) ;
		if (clip<0 || Player::GetInstance()->ClipTickPos(ch)<0) return ;
		UIIntVarField *fld=0 ;
		IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
		for (it->Begin();!it->IsDone();it->Next()) {
			UIField &f=it->CurrentItem() ;
			if (!f.IsStatic() && ((UIIntVarField &)f).GetVariableID()==id) { fld=(UIIntVarField *)&f ; break ; }
		}
		if (!fld) return ;
		capCur_.id=id ;
		capCur_.ch=ch ;
		capCur_.clip=clip ;
		capCur_.base=before ;
		curveMeta(fld,capCur_.meta) ;
		capCur_.pts.clear() ;
		capLast_=-1 ;
		capping_=true ;
	}
	capTick() ;
}

void InstrumentView::capBegin() {
	if (kitMode_ || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	FourCC id=focusID(*this) ;
	if (!automatable(id)) return ;
	int ch=autoChannel() ;
	int clip=autoClip(ch) ;
	if (clip<0 || Player::GetInstance()->ClipTickPos(ch)<0) return ;
	capCur_.id=id ;
	capCur_.ch=ch ;
	capCur_.clip=clip ;
	capCur_.base=preTouchVal_ ;   // lo de antes de tocar (si no había curva)
	curveMeta((UIIntVarField *)GetFocus(),capCur_.meta) ;   // focusID ya dijo que es un campo
	capCur_.pts.clear() ;
	capLast_=-1 ;
	capping_=true ;
}

void InstrumentView::capTick() {
	if (!capping_) return ;
	Player *player=Player::GetInstance() ;
	int pos=player->ClipTickPos(capCur_.ch) ;
	if (pos<0 || viewData_->currentPlayChain_[capCur_.ch]!=capCur_.clip || player->IsAutoRec() ||
	    !current_ || current_->GetType()!=IT_SAMPLE) {
		capEnd() ;
		return ;
	}
	Variable *v=current_->FindVariable(capCur_.id) ;
	if (!v) { capEnd() ; return ; }
	if (capLast_>=0 && pos<capLast_) {   // el clip dio la vuelta: otro tramo
		// el de antes cubre hasta el final del loop y el nuevo empieza en su
		// inicio (sin huecos que vuelvan a lo de antes)
		Clip &c=viewData_->song_->clips_->Get(capCur_.clip) ;
		int loopFrom=(c.loopStart>=0?c.loopStart:0)*CLIP_STEPS_PER_PAGE*CLIP_TICKS ;
		int loopTo=((c.loopStart>=0 && c.loopEnd>=c.loopStart)?(c.loopEnd+1)*CLIP_STEPS_PER_PAGE:c.Steps())*CLIP_TICKS-1 ;
		if (loopTo>capLast_) capLast_=loopTo ;
		CapSeg keep=capCur_ ;
		int lastV=capCur_.pts.empty()?v->GetInt():capCur_.pts.back().value ;
		capEnd() ;
		capCur_=keep ;
		capCur_.pts.clear() ;
		if (pos>loopFrom) capCur_.pts.push_back(AutoPoint(loopFrom,lastV,false)) ;
		capLast_=loopFrom ;
		capping_=true ;
	}
	int value=v->GetInt() ;
	std::vector<AutoPoint> &p=capCur_.pts ;
	if (!p.empty() && p.back().pos==pos) p.back().value=value ;
	else if (p.empty() || p.back().value!=value) p.push_back(AutoPoint(pos,value,false)) ;
	capLast_=pos ;
}

void InstrumentView::capEnd() {
	if (!capping_) return ;
	capping_=false ;
	if (capCur_.pts.empty()) return ;
	// el tramo llega hasta donde se soltó (aunque el valor no cambiara)
	if (capLast_>capCur_.pts.back().pos)
		capCur_.pts.push_back(AutoPoint(capLast_,capCur_.pts.back().value,false)) ;
	capCur_.tEnd=System::GetInstance()->GetClock() ;
	capSegs_.push_back(capCur_) ;
	if (capSegs_.size()>64) capSegs_.erase(capSegs_.begin()) ;
}

// un tramo en su lugar: antes y después la curva sigue como estaba (o, si no
// había, con el valor de antes de tocar)
void InstrumentView::capWrite(const CapSeg &s) {
	Clip &c=viewData_->song_->clips_->Get(s.clip) ;
	if (c.IsEmpty() || s.pts.empty()) return ;
	int total=c.Steps()*CLIP_TICKS ;
	int from=s.pts.front().pos, to=s.pts.back().pos ;
	if (from>=total) return ;
	if (to>=total) to=total-1 ;
	AutoLane *ol=c.FindLane(s.id,-1) ;
	bool had=ol && !ol->points.empty() ;
	AutoLane old ;
	if (had) old=*ol ;
	AutoLane &lane=c.GetLane(s.id,-1) ;
	std::vector<AutoPoint> &p=lane.points ;
	for (size_t k=0;k<p.size();) {
		if (p[k].pos>=from && p[k].pos<=to) p.erase(p.begin()+k) ;
		else k++ ;
	}
	bool atBefore=false, atAfter=false ;
	for (size_t k=0;k<p.size();k++) {
		if (p[k].pos==from-1) atBefore=true ;
		if (p[k].pos==to+1) atAfter=true ;
	}
	if (from>0 && !atBefore) {
		int v=s.base ;
		if (had) old.ValueAt(from-1,v) ;
		p.push_back(AutoPoint(from-1,v,false)) ;
	}
	for (size_t k=0;k<s.pts.size() && s.pts[k].pos<=to;k++) p.push_back(s.pts[k]) ;
	if (to+1<total && !atAfter) {
		int v=s.base ;
		bool ramp=false ;
		if (had) {
			old.ValueAt(to+1,v) ;
			// si ahí iba en rampa, sigue en rampa hacia el siguiente nodo
			for (size_t k=0;k<old.points.size() && old.points[k].pos<=to+1;k++)
				ramp=old.points[k].ramp && old.points[k].pos<to+1 ;
		}
		p.push_back(AutoPoint(to+1,v,ramp)) ;
	}
	lane.Sort() ;
	std::vector<AutoPoint> out ;
	for (size_t k=0;k<p.size();k++) {
		const AutoPoint &a=p[k] ;
		if (!out.empty() && !out.back().ramp && !out.back().inv && !a.ramp && !a.inv && out.back().value==a.value) continue ;
		out.push_back(a) ;
	}
	p=out ;
}

unsigned long InstrumentView::LastCaptureMove() {
	unsigned long newest=capping_?System::GetInstance()->GetClock():0 ;
	for (size_t i=0;i<capSegs_.size();i++) if (capSegs_[i].tEnd>newest) newest=capSegs_[i].tEnd ;
	return newest ;
}

const char *InstrumentView::CaptureAutomation(int track) {
	if (capping_) capEnd() ;
	// la pista donde moviste algo por última vez (no importa dónde estés
	// ahora); solo esa. Sin nada anotado, R2 no hace nada (ni avisa)
	// (... salvo que la pista no tenga clip: ahí no hay dónde capturar)
	if (capSegs_.empty()) return (autoClip(track>=0?track:viewData_->songX_)<0)?"sin clip":0 ;
	track=capSegs_[0].ch ;
	unsigned long newest=capSegs_[0].tEnd ;
	for (size_t i=1;i<capSegs_.size();i++)
		if (capSegs_[i].tEnd>=newest) { newest=capSegs_[i].tEnd ; track=capSegs_[i].ch ; }
	// solo la última vuelta de cada parámetro en cada clip (como Live): lo
	// que se movió antes de eso se olvida
	int tempo=viewData_->project_->GetTempo() ;
	if (tempo<20) tempo=120 ;
	std::vector<bool> keep(capSegs_.size(),true) ;
	for (size_t i=0;i<capSegs_.size();i++) {
		const CapSeg &s=capSegs_[i] ;
		Clip &c=viewData_->song_->clips_->Get(s.clip) ;
		int steps=(c.loopStart>=0 && c.loopEnd>=c.loopStart) ? (c.loopEnd-c.loopStart+1)*CLIP_STEPS_PER_PAGE : c.Steps() ;
		unsigned long loopMs=(unsigned long)(steps*15000L/tempo) ;
		unsigned long last=s.tEnd ;
		for (size_t j=i+1;j<capSegs_.size();j++)
			if (capSegs_[j].id==s.id && capSegs_[j].clip==s.clip && capSegs_[j].tEnd>last) last=capSegs_[j].tEnd ;
		if (last-s.tEnd>loopMs || s.ch!=track) keep[i]=false ;
	}
	std::vector<int> snapped ;
	for (size_t i=0;i<capSegs_.size();i++) {
		if (!keep[i]) continue ;
		int clip=capSegs_[i].clip ;
		bool done=false ;
		for (size_t k=0;k<snapped.size();k++) if (snapped[k]==clip) done=true ;
		if (!done) { pushLaneSnap(clip) ; snapped.push_back(clip) ; }   // Select + L lo quita
	}
	MixerService::GetInstance()->Lock() ;
	for (size_t i=0;i<capSegs_.size();i++) {
		if (!keep[i]) continue ;
		capWrite(capSegs_[i]) ;
		Player::GetInstance()->ReleaseOverride(capSegs_[i].ch,capSegs_[i].id) ;
	}
	MixerService::GetInstance()->Unlock() ;
	// lo último capturado es la curva que muestra Select en el clip
	{
		int last=-1 ;
		for (size_t i=0;i<capSegs_.size();i++)
			if (keep[i] && (last<0 || capSegs_[i].tEnd>=capSegs_[last].tEnd)) last=int(i) ;
		if (last>=0) {   // gana siempre lo último que se movió
			applyCurveMeta(capSegs_[last].meta) ;
			viewData_->curveLastID_=capSegs_[last].id ;
			if (viewData_->curveID_) viewData_->curveID_=capSegs_[last].id ;   // si el clip ya muestra una curva, pasa a esta
		}
	}
	for (size_t i=0;i<capSegs_.size();)   // lo de esta pista ya se usó (o era viejo)
		if (capSegs_[i].ch==track) capSegs_.erase(capSegs_.begin()+i) ;
		else i++ ;
	isDirty_=true ;
	return "automatización capturada" ;
}

bool InstrumentView::deleteLane() {
	FourCC id=focusID(*this) ;
	if (!automatable(id)) return false ;
	int clip=autoClip(autoChannel()) ;
	if (clip<0) return false ;
	Clip &c=viewData_->song_->clips_->Get(clip) ;
	AutoLane *l=c.FindLane(id,-1) ;
	if (!l) return false ;
	if (touching_) endTouch() ;
	pushLaneSnap(clip) ;
	laneUndoLane_=*l ;
	laneUndoClip_=clip ;
	laneUndo_=true ;
	laneUndoTime_=System::GetInstance()->GetClock() ;
	MixerService::GetInstance()->Lock() ;
	c.RemoveLane(id,-1) ;
	MixerService::GetInstance()->Unlock() ;
	View::SetNotification("automatización borrada") ;   // Select + L la regresa, como todo
	isDirty_=true ;
	return true ;
}

void InstrumentView::pushLaneSnap(int clip) {
	if (clip<0) return ;
	LaneSnap s ;
	s.clip=clip ;
	s.lanes=viewData_->song_->clips_->Get(clip).lanes ;
	s.var=0 ;
	s.value=0 ;
	s.instr=current_ ;
	autoUndo_.push_back(s) ;
	if (autoUndo_.size()>48) autoUndo_.erase(autoUndo_.begin()) ;
	autoRedo_.clear() ;
}

InstrumentView::WaveSnap InstrumentView::waveNow() {
	SampleInstrument *si=(SampleInstrument *)current_ ;
	WaveSnap w ;
	w.s=si->GetSimpler(SIP_S_START) ; w.sf=si->GetSimpler(SIP_S_STARTF) ;
	w.e=si->GetSimpler(SIP_S_END) ; w.ef=si->GetSimpler(SIP_S_ENDF) ;
	w.fi=si->GetSimpler(SIP_S_FIN) ; w.fo=si->GetSimpler(SIP_S_FOUT) ;
	w.ak=si->GetSimpler(SIP_S_ATK) ; w.dc=si->GetSimpler(SIP_S_DEC) ;
	w.su=si->GetSimpler(SIP_S_SUS) ; w.rl=si->GetSimpler(SIP_S_REL) ;
	w.marks=si->GetMarks() ;
	return w ;
}

// la foto de antes de un gesto en la onda (una por gesto)
void InstrumentView::wavePush() {
	if (waveGesture_ || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	waveGesture_=true ;
	WaveSnap w=waveNow() ;
	if (!waveUndo_.empty() && waveUndo_.back()==w) return ;
	waveUndo_.push_back(w) ;
	if (waveUndo_.size()>48) waveUndo_.erase(waveUndo_.begin()) ;
	waveRedo_.clear() ;
}

bool InstrumentView::Undo(bool redo) {
	selTap_=false ;   // Select + L / R no es un toque de Select: no cambia cajas <-> dibujo
	if (kitMode_) { kitUndo(!redo) ; return true ; }   // en un kit: los pads
	if (waveEdit_ && current_ && current_->GetType()==IT_SAMPLE) {   // en la onda: inicio, fin y marcas
		std::vector<WaveSnap> &from=redo?waveRedo_:waveUndo_ ;
		std::vector<WaveSnap> &to=redo?waveUndo_:waveRedo_ ;
		WaveSnap now=waveNow() ;
		while (!from.empty() && from.back()==now) from.pop_back() ;   // fotos sin cambio
		if (from.empty()) {
			View::SetNotification(redo?"nada que rehacer":"nada que deshacer") ;
			return true ;
		}
		WaveSnap w=from.back() ;
		from.pop_back() ;
		to.push_back(now) ;
		SampleInstrument *si=(SampleInstrument *)current_ ;
		si->FindVariable(SIP_S_START)->SetInt(w.s) ; si->FindVariable(SIP_S_STARTF)->SetInt(w.sf) ;
		si->FindVariable(SIP_S_END)->SetInt(w.e) ; si->FindVariable(SIP_S_ENDF)->SetInt(w.ef) ;
		si->FindVariable(SIP_S_FIN)->SetInt(w.fi) ; si->FindVariable(SIP_S_FOUT)->SetInt(w.fo) ;
		si->FindVariable(SIP_S_ATK)->SetInt(w.ak) ; si->FindVariable(SIP_S_DEC)->SetInt(w.dc) ;
		si->FindVariable(SIP_S_SUS)->SetInt(w.su) ; si->FindVariable(SIP_S_REL)->SetInt(w.rl) ;
		si->SetMarks(w.marks) ;
		waveSel_=0 ; waveFlag_=0 ; waveAPut_=false ;
		if (!canMark()) markVar_.SetInt(si->RegionPoint(waveStop_!=3)) ;   // sin cursor libre: en su parada
		View::SetNotification(redo?"rehecho":"deshecho") ;
		isDirty_=true ;
		return true ;
	}
	std::vector<LaneSnap> &from=redo?autoRedo_:autoUndo_ ;
	std::vector<LaneSnap> &to=redo?autoUndo_:autoRedo_ ;
	if (touching_) endTouch() ;
	if (from.empty()) {
		View::SetNotification(redo?"nada que rehacer":"nada que deshacer") ;
		return true ;
	}
	LaneSnap s=from.back() ;
	from.pop_back() ;
	Clip &c=viewData_->song_->clips_->Get(s.clip) ;
	LaneSnap now ;
	now.clip=s.clip ;
	now.lanes=c.lanes ;
	now.var=s.var ;
	now.value=0 ;
	now.instr=s.instr ;
	Variable *v=(s.var && s.instr && s.instr==current_) ? current_->FindVariable(s.var) : 0 ;
	if (v) now.value=v->GetInt() ;
	to.push_back(now) ;
	MixerService::GetInstance()->Lock() ;   // el audio lee las lanes
	c.lanes=s.lanes ;
	if (v) v->SetInt(s.value) ;
	MixerService::GetInstance()->Unlock() ;
	laneUndo_=false ;
	View::SetNotification(redo?"automatización: rehecho":"automatización: deshecho") ;
	isDirty_=true ;
	return true ;
}

bool InstrumentView::undoLane() {
	if (!laneUndo_) return false ;
	laneUndo_=false ;
	if (System::GetInstance()->GetClock()-laneUndoTime_>5000) return false ;
	Clip &c=viewData_->song_->clips_->Get(laneUndoClip_) ;
	if (c.IsEmpty()) return false ;
	MixerService::GetInstance()->Lock() ;
	c.RemoveLane(laneUndoLane_.target,laneUndoLane_.pad) ;
	c.lanes.push_back(laneUndoLane_) ;
	MixerService::GetInstance()->Unlock() ;
	View::SetNotification("automatización recuperada") ;
	isDirty_=true ;
	return true ;
}

// un puntito arriba, a la derecha del valor de cada parámetro automatizado
// en el clip; y "REC" mientras se graba
void InstrumentView::drawAutoMarks() {
	if (!SupportsShapes()) return ;
	GUIColor hi=ColorOf(CD_HILITE2) ;
	int rs=Player::GetInstance()->AutoRecState() ;
	if (tapPending_>=0) rs=0 ;   // precuenta para rebanar: no graba
	if (rs==2 || (rs==1 && recShown_==1)) {   // esperando: parpadea
		FillRect(640-64,2,58,16,GUIColor(0xD0,0x30,0x30),6) ;
		DrawText(640-56,4,"REC",GUIColor(0xFF,0xFF,0xFF),0) ;
	} else if (rs==1) {
		FrameRect(640-64,2,58,16,GUIColor(0xD0,0x30,0x30),6,2) ;
		DrawText(640-56,4,"REC",GUIColor(0xD0,0x30,0x30),0) ;
	}
	int clip=autoClip(autoChannel()) ;
	if (clip<0) return ;
	Clip &c=viewData_->song_->clips_->Get(clip) ;
	if (c.lanes.empty()) return ;
	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		UIField &f=it->CurrentItem() ;
		if (f.IsStatic()) continue ;
		UIIntVarField &vf=(UIIntVarField &)f ;
		if (!c.FindLane(vf.GetVariableID(),-1)) continue ;
		if (gridMode_) {
			int bx,by,bw,bh ;
			gridBox(gridSlot(f),bx,by,bw,bh) ;
			FillRect(bx+bw-18,by+10,8,8,hi,4) ;
			continue ;
		}
		// a la derecha del valor, arriba (como superíndice)
		GUIPoint p=f.GetPosition() ;
		FillRect((p._x+vf.GetDrawnLength())*16+3,p._y*16-1,6,6,hi,3) ;
	}
}

void InstrumentView::LaunchNotify(const char *msg) { View::SetNotification(msg) ; isDirty_=true ; }
