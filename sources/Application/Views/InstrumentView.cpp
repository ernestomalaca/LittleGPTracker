#include "InstrumentView.h"
#include "Application/Instruments/SF2Import.h"
#include "Application/Instruments/SoundFontPreset.h"
#include "Application/AppWindow.h"
#include <math.h>


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
	UISoundfontField(GUIPoint &p,Variable &v,const std::string &label,int idx)
		:UIIntVarField(p,v,"sample: %s",idx,idx,1,1),label_(label) {}
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

class UIFreqVarField : public UIIntVarField {
public:
	UIFreqVarField(GUIPoint &p, Variable &v) : UIIntVarField(p,v,"freq: %d",30,22000,1,12) {}
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
				snprintf(buf,sizeof(buf),"pad %2d: %s %s",v+1,padNoteName(note).c_str(),n.empty()?"(vacio)":n.c_str()) ;
				break ;
			}
			case SAMPLE: {
				SamplePool *sp=SamplePool::GetInstance() ;
				const char *n=(v>=0 && v<sp->GetNameListSize())?sp->GetNameList()[v]:0 ;
				snprintf(buf,sizeof(buf),"sample: %.30s",n?n:"(vacio)") ;
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
	markTap_=false ;
	waveEdit_=false ;
	waveZoom_=1 ;
	waveSel_=0 ;
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
		f1=new UISoundfontField(AT(L),*v,label,v->GetInt()) ;
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
		v=instrument->FindVariable(SIP_S_VOICES) ;
		ADD(new UIIntVarField(AT(R),*v,"voices: %d",1,2,1,1)) ;
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
	isDirty_=true ;
} ;

void InstrumentView::ProcessButtonMask(unsigned short mask,bool pressed) {
	if (kitMode_ && padAPending_) {   // A tocada sola en un pad vacío (sin flechas)
		if (pressed && mask!=EPBM_A) padAPending_=false ;
		else if (!pressed && !(mask&EPBM_A)) {
			padAPending_=false ;
			openPadBrowser() ;
			return ;
		}
	}
	if (pressed && kitMode_ && processKit(mask)) { kitTrack() ; return ; }
	processButtonMask(mask,pressed) ;
	if (pressed) { kitSync() ; kitTrack() ; }
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
			enterWaveEdit() ;
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

						 ImportSampleDialog *isd=new ImportSampleDialog(*this,false,sfMode_) ;
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
	bool toManual=(editedID==SIP_S_SLICEBY &&
	     current_->FindVariable(editedID)->GetInt()==SSB_MANUAL && editedBefore!=SSB_MANUAL) ;
	if ((editedID==SIP_WARP || editedID==SIP_LOOPMODE || editedID==SIP_S_MODE ||
	     editedID==SIP_S_LOOP || editedID==SIP_S_WARP || editedID==SIP_S_ETAB ||
	     editedID==SIP_S_SLICEBY) &&
	    current_->FindVariable(editedID)->GetInt()!=editedBefore) {
		lastFocusID_=editedID ;
		onInstrumentChange() ;
		isDirty_=true ;
	}
	if (toManual) {
		markTap_=false ;
		enterWaveEdit() ;
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
		   if (GetFocus()!=T_SimpleList<UIField>::GetFirst() && !onTable &&
		       !GetFocus()->IsStatic()) {
				resetFocusedField() ;
		   }
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

    FieldView::Redraw();
    drawMap() ;

    // trk2als: cuánto se acelera o frena el loop para ir al tempo del proyecto
    if (current_ && current_->GetType()==IT_SAMPLE) {
        SampleInstrument *si=(SampleInstrument *)current_ ;
        int bpm=si->FindVariable(SIP_LOOPBPM)->GetInt() ;
        int bars=si->FindVariable(SIP_LOOPBARS)->GetInt() ;
        float secs=si->GetLoopSeconds() ;
        bool sync=bpmInfoPos_._x>=0 && (audioMode_ ||
                  si->FindVariable(SIP_LOOPMODE)->GetInt()==SILM_LOOPSYNC) ;
        if (sync && bpm>0 && secs>0 && bars>0) {
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
	if (!topInfo_.empty()) ctx+="  "+topInfo_ ;
	drawTopBar(TAB_INST,ctx.c_str()) ;
	const char *help=helpText_?helpText_:"A+<>:valor;A+^v:valor x16;SEL+<>:pantalla" ;
	if (kitMode_ && GetFocus() && !GetFocus()->IsStatic() &&
	    isPadField(((UIIntVarField *)GetFocus())->GetVariableID()))
		help="A:oir;AA:sample;X:vol/decay;Y:tono/pan;L:inicio/choke" ;
	if (kitMode_ && GetFocus() && !GetFocus()->IsStatic() &&
	    ((UIIntVarField *)GetFocus())->GetVariableID()==SIP_SAMPLE)
		help="A:cambiar de kit;B+A:vaciar el instrumento" ;
	drawHelpBar(help) ;
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

	const int X=16, Y=kitMode_?56:34, W=WF_BARS*8, H=kitMode_?144:(simplerView_?80:112) ;
	FillRect(X-8,Y-4,W+16,H+8,Mix(bg,fg,9),8) ;

	if (kitMode_) {   // trk2als: kit: la rejilla de pads
		drawPadGrid(si,X,Y,W,H) ;
		return ;
	}
	if (sfMode_ && si->IsZoned()) {   // trk2als: soundfont: el mapa de zonas
		drawZoneMap(si,X,Y,W,H) ;
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
		int a=int((long long)size*si->GetSimpler(SIP_S_START)/100) ;
		int b=int((long long)size*si->GetSimpler(SIP_S_END)/100) ;
		if (b<=a) b=a+1 ;
		int margin=(b-a)/10 ;
		vFrom=a-margin ;
		vTo=b+margin ;
		int minLen=src->GetSampleRate(-1)/20 ;        // al menos 50 ms a la vista
		if (vTo-vFrom<minLen) { int c=(a+b)/2 ; vFrom=c-minLen/2 ; vTo=c+minLen/2 ; }
		if (vFrom<0) { vTo-=vFrom ; vFrom=0 ; }
		if (vTo>size) { vFrom-=vTo-size ; vTo=size ; if (vFrom<0) vFrom=0 ; }
		if (vTo-vFrom>size*8/10) { vFrom=0 ; vTo=size ; } // casi todo: la onda completa
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
void InstrumentView::drawSimplerWave(SampleInstrument *si,int X,int Y,int W,int H,int size) {
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
	int a=FPX((long long)size*si->GetSimpler(SIP_S_START)/100) ;
	int b=FPX((long long)size*si->GetSimpler(SIP_S_END)/100) ;
	if (b<=a) b=a+2 ;
	// con zoom: abajo, una barrita con la parte del sample que se ve
	if (vLen<size) {
		int sy=Y+H+1 ;
		FillRect(X,sy,W,2,Mix(bg,fg,18)) ;
		FillRect(X+int((long long)wfFrom_*W/size),sy,int((long long)vLen*W/size)+2,2,dim2) ;
	}

	SoundSource *src=SamplePool::GetInstance()->GetSource(si->GetSampleIndex()) ;
	int rate=src?src->GetSampleRate(-1):0 ;
	bool warped=(si->GetSimpler(SIP_S_WARP)!=SW_OFF) ;
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
	bool oneShot=(mode==SM_ONESHOT) ;
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
		DrawText(a+16,Y+1,txt,fg,0) ;
	}
	// la curva encima de las barras
	for (int x=a;x<b;x+=3) {
		float g ;
		ENV_AT(x,g)
		FillRect(x,Y+6+int((1.0f-g)*(H-14)),3,2,hi2) ;
	}
	// classic (o la del filtro): el release después del final (empieza
	// cuando sueltas la nota)
	if ((mode==SM_CLASSIC || ftab) && rel>=1 && msPerPx>0 && sus>0) {
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
	#define FLAG(xx,dir) { FillRect((xx)-1,Y-4,2,H+8,hi2) ; \
		for (int q=0;q<10;q++) { int w=(q<5)?2*q+3:21-2*q ; \
			FillRect((dir)>0?(xx):(xx)-w,Y-4+q,w,1,hi2) ; } }
	if (mode==SM_SLICE) {
		int lastName=-1000 ;
		for (int k=0;k<nSl;k++) {
			int x=slPx[k] ;
			FillRect(x-1,Y-4,2,H+8,hi2) ;
			if (x-lastName>=30) {                 // nombres sin amontonar
				lastName=x ;
				char num[8] ;
				// la nota que dispara la rebanada (C1 = la primera, como en Simpler)
				static const char *names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"} ;
				int note=36+k ;
				sprintf(num,"%s%d",names[note%12],note/12-2) ;
				FillRect(x+2,Y,8*int(strlen(num))+4,13,bg) ;   // legible sobre la envolvente
				DrawText(x+4,Y+1,num,hi2,0) ;
			}
		}
		FillRect(b-1,Y-4,2,H+8,hi2) ;
		// slice manual, editando: las marcas seleccionadas y el cursor
		if (waveEdit_ && si->GetSimpler(SIP_S_SLICEBY)==SSB_MANUAL) {
			int ca,cb ;
			si->SimplerRegion(ca,cb) ;
			std::vector<int> m=si->GetMarks() ;
			int cpos=markVar_.GetInt() ;
			if (cpos<ca) cpos=ca ;
			if (cpos>cb) cpos=cb ;
			int near=nearMark(m) ;
			for (size_t i=0;i<m.size();i++) {
				bool sel=(waveSel_>=2) || (waveSel_==1 && int(i)==near) ;
				if (sel) {
					int mx=FPX(m[i]) ;
					if (mx>=X && mx<=X+W) FillRect(mx-2,Y-4,4,H+8,hi) ;
				}
			}
			int cx=FPX(cpos) ;
			FillRect(cx-1,Y-4,2,H+8,fg) ;
			for (int q=0;q<6;q++) FillRect(cx-q,Y+H+4+q,2*q+1,1,fg) ;
			char tag[48] ;
			SoundSource *cs=si->currentSource() ;
			int size=cs?cs->GetSize(-1):0 ;
			int pm=size>0?int((long long)cpos*1000/size):0 ;
			sprintf(tag,"marcas %d  %d.%d%%  zoom x%d",int(m.size()),pm/10,pm%10,waveZoom_) ;
			DrawText(X+4,Y+H-12,tag,fg,0) ;
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
		FLAG(a,1)
		FLAG(b,-1)
		if (loop) FrameRect(l0,Y-4,b-l0,H+8,hi,8,2) ;
	}
	#undef FLAG

	// cursor de reproducción
	playheadPx_=-1 ;
	int pos=si->GetPlayPosition() ;
	if (pos>=wfFrom_ && pos<=wfFrom_+vLen) {
		playheadPx_=FPX(pos) ;
		FillRect(playheadPx_-1,Y-4,3,H+8,ColorOf(CD_PLAY),1) ;
	}
	#undef FPX
}

// trk2als: ~25 veces por segundo; si el cursor se movió, redibujar las figuras
void InstrumentView::AnimationUpdate() {
	if (!current_ || current_->GetType()!=IT_SAMPLE || HasModal()) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
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
			ClearShapes() ;
			drawWaveform() ;
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
		ClearShapes() ;
		drawWaveform() ;
	}
}

void InstrumentView::OnFocus() { onInstrumentChange(); }

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
	DoModal(pd,PresetLoadCallback) ;
}

void InstrumentView::openPresetSave() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	if (si->GetSampleIndex()<0) {
		View::SetNotification("Primero elige un sample") ;
		return ;
	}
	// primero la carpeta (categoría), luego el nombre (un kit, de entrada en Kits)
	PresetDialog *pd=new PresetDialog(*this,true,si->IsKit()?InstrumentPresets::PK_KIT:-1) ;
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
	if (InstrumentPresets::Load((SampleInstrument *)current_,name)) {
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

void InstrumentView::enterWaveEdit() {
	if (!current_ || current_->GetType()!=IT_SAMPLE) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int a,b ;
	if (!si->SimplerRegion(a,b)) return ;
	waveEdit_=true ;
	waveZoom_=1 ;
	waveSel_=0 ;
	lastAPress_=0 ;
	int c=markVar_.GetInt() ;
	if (c<a || c>b) markVar_.SetInt(a) ;
	startTapPlay(a) ;
}

// trk2als: toda la región suena desde 'from' (y la canción, si no estaba
// sonando): con A se marca donde va sonando
void InstrumentView::startTapPlay(int from) {
	markVar_.SetInt(from) ;
	int ra,rb ;
	((SampleInstrument *)current_)->SimplerRegion(ra,rb) ;
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
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int index=si->GetSampleIndex() ;
	SoundSource *src=si->currentSource() ;
	if (index<0 || !src || src->GetSize(-1)<=0) return ;
	if (from<0) from=markVar_.GetInt() ;
	std::string name=SamplePool::GetInstance()->GetNameList()[index] ;
	double frac=double(from)/src->GetSize(-1) ;
	Player *player=Player::GetInstance() ;
	player->StartStreaming(Path(std::string("samples:")+name),1.0,0,frac,false) ;
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
	if (!current_ || current_->GetType()!=IT_SAMPLE ||
	    ((SampleInstrument *)current_)->GetSimpler(SIP_S_SLICEBY)!=SSB_MANUAL ||
	    ((SampleInstrument *)current_)->GetSimpler(SIP_S_MODE)!=SM_SLICE) {
		exitWaveEdit() ;
		return false ;
	}
	if (!pressed) {
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
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int a,b ;
	si->SimplerRegion(a,b) ;
	int step=(wfTo_>wfFrom_)?(wfTo_-wfFrom_)/128:(b-a)/128 ;
	if (step<1) step=1 ;
	int c=markVar_.GetInt() ;
	if (c<a) c=a ;
	if (c>b) c=b ;
	std::vector<int> m=si->GetMarks() ;
	isDirty_=true ;

	// L + Start (o ← + Start): rebanar de nuevo, sin marcas, desde el inicio
	if ((mask&EPBM_START) && (mask&EPBM_L)) {
		stopTapPlay() ;
		m.clear() ;
		si->SetMarks(m) ;
		waveSel_=0 ;
		startTapPlay(a) ;
		return true ;
	}
	// Start: tocar / parar la región desde el cursor (y la canción)
	if (mask==EPBM_START) {
		if (tapPlay_) stopTapPlay() ;
		else startTapPlay((c>=b-step)?a:c) ;
		return true ;
	}
	// mientras suena la región: A marca donde va sonando (lo que se oye va
	// un poco atrás de lo que se lee: se compensa la latencia del audio)
	if (tapPlay_) {
		if (mask==EPBM_A) {
			int sp=Player::GetInstance()->GetStreamPosition() ;
			SoundSource *src=si->currentSource() ;
			if (sp>=0 && src) {
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

	if ((mask&(EPBM_L|EPBM_R)) && (mask&EPBM_B) && !(mask&EPBM_A)) {   // Select + B: seleccionar
		waveSel_=(waveSel_>=2)?2:waveSel_+1 ;
		return true ;
	}
	if ((mask&EPBM_B) && (mask&EPBM_A)) {     // B + A: borrar
		if (waveSel_>=2) {
			m.clear() ;
			si->SetMarks(m) ;
		} else {
			int k=nearMark(m) ;
			if (k>=0) {
				m.erase(m.begin()+k) ;
				si->SetMarks(m) ;
			}
		}
		waveSel_=0 ;
		return true ;
	}
	if (mask&EPBM_B) {
		if (mask&EPBM_RIGHT) {                  // B + →: la marca siguiente
			int next=b ;
			for (size_t i=0;i<m.size();i++) if (m[i]>c && m[i]<next) next=m[i] ;
			markVar_.SetInt(next) ;
			waveSel_=(next<b)?1:0 ;             // la marca queda agarrada: ← → la mueven
		} else if (mask&EPBM_LEFT) {            // B + ←: la anterior
			int prev=a ;
			for (size_t i=0;i<m.size();i++) if (m[i]<c && m[i]>prev) prev=m[i] ;
			markVar_.SetInt(prev) ;
			waveSel_=(prev>a)?1:0 ;
		}
		return true ;
	}
	if (mask&EPBM_A) {
		if (mask==EPBM_A) {
			unsigned long now=System::GetInstance()->GetClock() ;
			if (lastAPress_ && now-lastAPress_<400) {   // doble toque: marca
				int at=si->AddMark(c) ;
				if (at>=0) markVar_.SetInt(at) ;
				else View::SetNotification("muy cerca de otra marca") ;
				lastAPress_=0 ;
			} else {
				lastAPress_=now ;
			}
			waveSel_=0 ;
			int f,t ;                    // del cursor (o su marca) a la siguiente marca
			sliceAt(c,f,t) ;
			startPreview(c,t) ;
			return true ;
		}
		if (mask&(EPBM_LEFT|EPBM_RIGHT)) {        // A + ← →: moverse escuchando
			c+=(mask&EPBM_RIGHT)?step:-step ;
			if (c<a) c=a ;
			if (c>b) c=b ;
			markVar_.SetInt(c) ;
			lastAPress_=0 ;
			int f,t ;                    // del cursor hasta la siguiente marca
			sliceAt(c,f,t) ;
			startPreview(c,t) ;
		} else if (mask&EPBM_UP) {                // A + ↑ / ↓: zoom
			if (waveZoom_<64) waveZoom_*=2 ;
		} else if (mask&EPBM_DOWN) {
			if (waveZoom_>1) waveZoom_/=2 ;
		}
		return true ;
	}
	if ((mask==EPBM_LEFT || mask==EPBM_RIGHT) && waveSel_==1) {
		int k=nearMark(m) ;
		if (k>=0) {
			int gap=si->MinMarkGap() ;           // no se pega a sus vecinas
			int lo=((k>0)?m[k-1]:a)+gap ;
			int hi=((k+1<(int)m.size())?m[k+1]:b)-gap ;
			if (hi<lo) hi=lo ;
			int p=m[k]+((mask==EPBM_RIGHT)?step:-step) ;
			if (p<lo) p=lo ;
			if (p>hi) p=hi ;
			m[k]=p ;
			si->SetMarks(m) ;
			markVar_.SetInt(p) ;
			return true ;
		}
		waveSel_=0 ;
	}
	if (mask==EPBM_LEFT || mask==EPBM_RIGHT) {
		c+=(mask==EPBM_RIGHT)?step:-step ;
		if (c<a) c=a ;
		if (c>b) c=b ;
		markVar_.SetInt(c) ;
		return true ;
	}
	if (mask==EPBM_DOWN) {                       // terminar: queda en "slice by"
		if (waveSel_==1) {                         // soltar la marca, en el cruce por cero
			int k=nearMark(m) ;
			if (k>=0) {
				int old=m[k] ;
				m.erase(m.begin()+k) ;
				si->SetMarks(m) ;
				int at=si->AddMark(c) ;
				if (at<0) {                          // no cupo en el cruce: se queda donde estaba
					m.push_back(old) ;
					std::sort(m.begin(),m.end()) ;
					si->SetMarks(m) ;
					at=old ;
				}
				markVar_.SetInt(at) ;
			}
			waveSel_=0 ;
		} else if (waveSel_) waveSel_=0 ;
		else exitWaveEdit() ;
		return true ;
	}
	return true ;
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
		previewPad() ;
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
	if (Player::GetInstance()->IsRunning()) return ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int note=padNote() ;
	if (si->PadSample(note).empty()) return ;
	if (previewing_) stopPreview() ;
	int ch=viewData_->songX_ ;
	if (ch<0 || ch>=SONG_CHANNEL_COUNT) ch=0 ;
	Player::GetInstance()->PreviewNote(ch,si,note) ;
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
	if (si->PadSample(note).empty()) { View::SetNotification("pad vacio") ; return true ; }
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
		snprintf(msg,sizeof(msg),"afinacion %+dst",p.tune) ;
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
	DoModal(new ImportSampleDialog(*this,false,false,true),PadImportCallback) ;
}

// A suena el pad (en uno vacío abre el navegador; A,A en el sample también);
// B+A corta el pad; Select+A pega el pad copiado (B lo copia).
// Select+L deshace y Select+R rehace (en cualquier campo del kit)
// otro kit en este instrumento (de los presets que son kits)
void InstrumentView::OnKitLoad(const std::string &name) {
	if (name.empty() || !current_ || current_->GetType()!=IT_SAMPLE) return ;
	padBusy_=true ;
	bool ok=InstrumentPresets::Load((SampleInstrument *)current_,name) ;
	padBusy_=false ;
	std::string shortName=name.substr(name.rfind('/')==std::string::npos?0:name.rfind('/')+1) ;
	if (ok && ownedByTrack()) viewData_->project_->SetTrackName(viewData_->songX_,shortName.c_str()) ;
	View::SetNotification(ok?(shortName+" cargado").c_str():"No se pudo cargar el kit") ;
	padShown_=-1 ;
	lastFocusID_=SIP_SAMPLE ;
	onInstrumentChange() ;
	isDirty_=true ;
}

bool InstrumentView::processKit(unsigned short mask) {
	if (View::physMask_==(EPBM_SELECT|EPBM_L) || View::physMask_==(EPBM_SELECT|EPBM_R)) {
		kitTrack() ;   // lo que haya cambiado sin botón (el navegador)
		bool undo=(View::physMask_&EPBM_L)!=0 ;
		std::vector<std::vector<std::string> > &from=undo?kitUndo_:kitRedo_ ;
		std::vector<std::vector<std::string> > &to=undo?kitRedo_:kitUndo_ ;
		if (from.empty()) { View::SetNotification(undo?"nada que deshacer":"nada que rehacer") ; return true ; }
		to.push_back(kitState_) ;
		std::vector<std::string> snap=from.back() ;
		from.pop_back() ;
		kitRestore(snap) ;
		View::SetNotification(undo?"deshecho":"rehecho") ;
		return true ;
	}
	UIField *f=GetFocus() ;
	if (!f || f->IsStatic()) return false ;
	FourCC id=((UIIntVarField *)f)->GetVariableID() ;
	if (id==SIP_SAMPLE) {   // el nombre del kit: A cambia de kit (solo kits)
		if (mask==EPBM_A) {
			DoModal(new PresetDialog(*this,false,InstrumentPresets::PK_KIT),KitLoadCallback) ;
			return true ;
		}
		return (mask&EPBM_A)!=0 && !(mask&EPBM_B) ;   // A + flechas: nada (no es un sample)
	}
	if (!isPadField(id)) return false ;
	SampleInstrument *si=(SampleInstrument *)current_ ;
	int note=padNote() ;
	if (padShortcut(mask)) return true ;
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
			else if (mask==EPBM_UP || mask==EPBM_DOWN) return false ;   // la orilla: al campo de arriba / abajo
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
		View::SetNotification(padClip_?"pad cortado":"pad vacio") ;
		return true ;
	}
	if (mask==(EPBM_L|EPBM_A) && whole) {   // pegar (encima de otro: lo reemplaza)
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
			View::SetNotification("pegado; el de antes quedo cortado") ;
		} else {
			padClipCut_=false ;
			View::SetNotification("pad pegado") ;
		}
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
	snprintf(l1,sizeof(l1),"pad %d  %s  %s",sel+1,padNoteName(note).c_str(),wav.empty()?"(vacio: A abre el navegador)":wav.c_str()) ;
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
