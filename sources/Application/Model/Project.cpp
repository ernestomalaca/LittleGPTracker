#include "Project.h"
#include <string.h>
#include <stdio.h>
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/SyncMaster.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Groove.h"
#include "Scale.h"
#include <string>
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/io/Status.h"
#include "Table.h"
#include "Application/Model/Config.h"
#include <stdlib.h>

#include "ProjectDatas.h"
#include <math.h>

Project::Project()
:Persistent("PROJECT")
,midiDeviceList_(0),
tempoNudge_(0)
{
    // trk2als: tempo de los proyectos nuevos, 120 o DEFAULTTEMPO en config.xml
    const char *defTempo = Config::GetInstance()->GetValue("DEFAULTTEMPO");
    int startTempo = defTempo ? atoi(defTempo) : 120;
    if (startTempo < 60 || startTempo > 400) startTempo = 120;
    WatchedVariable *tempo = new WatchedVariable("tempo", VAR_TEMPO, startTempo);
    this->Insert(tempo);
    Variable *masterVolume = new Variable("master", VAR_MASTERVOL, 100, 100);
    this->Insert(masterVolume) ;
    // trk2als: metrónomo (un click por tiempo, más agudo en el 1 del compás)
    static const char *metroStates[2] = {"off", "on"};
    this->Insert(new Variable("metronome", VAR_METRONOME, metroStates, 2, 0));
    Variable *pregain =
        new Variable("pregain", VAR_PREGAIN, 100, 200);
    this->Insert(pregain);
    Variable *softclip =
        new Variable("softclip", VAR_SOFTCLIP, softclipStates, 5, 0);
    this->Insert(softclip);
    Variable *softclipGain = new Variable("softclipGain", VAR_SOFTCLIP_GAIN,
                                          softclipGainStates, 2, 0);
    this->Insert(softclipGain);
	Variable *wrap=new Variable("wrap", VAR_WRAP, false);
	this->Insert(wrap);
	Variable *transpose=new Variable("transpose", VAR_TRANSPOSE, 0);
	this->Insert(transpose);
    Variable *scale =
        new Variable("scale", VAR_SCALE, scaleNames, scaleCount, 0);
    this->Insert(scale);
    scale->SetInt(1);   // trk2als: proyecto nuevo en C Major con fold (como pidió el usuario)
    Variable *scaleRoot =
        new Variable("scaleroot", VAR_SCALEROOT, scaleNotes, scaleNoteCount, 0);
    this->Insert(scaleRoot);
    Variable *fold = new Variable("fold", VAR_FOLD, true);
    this->Insert(fold);
    appliedScale_ = 1;
    appliedRoot_ = 0;
    // trk2als: tipo de cada pista (MIDI o audio, como en Live); se cambia en
    // la cabecera de la Song. Proyectos nuevos: todas MIDI
    // trk2als: un proyecto nuevo arranca con cuatro pistas MIDI, cada una
    // dueña de su instrumento (el 00 de la 1, el 01 de la 2...); las demás no
    // existen hasta que se agregan con '+'. Sin preset cargado, la pista se
    // llama "MIDI 1", "AUDIO 5"...
    static const char *trackTypes[4] = {"TRK", "AUD", "MID", "OFF"};
    for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
        char name[16];
        sprintf(name, "track%d", ch + 1);
        Variable *t = new Variable(name, VAR_TRACKTYPE(ch), trackTypes, 4, ch < 4 ? TT_MIDI : TT_OFF);
        this->Insert(t);
        sprintf(name, "trackname%d", ch + 1);
        this->Insert(new Variable(name, VAR_TRACKNAME(ch), ""));
        sprintf(name, "trackinstr%d", ch + 1);
        this->Insert(new Variable(name, VAR_TRACKINSTR(ch), ch < 4 ? ch : -1, 0));
    }
    // proyectos de antes de los tipos guardaban cuántas pistas de audio había
    Variable *audioTracks = new Variable("audiotracks", VAR_AUDIOTRACKS, 0, 8);
    this->Insert(audioTracks);

    Variable *renderMode =
        new Variable("renderMode", VAR_RENDER, renderModes, MAX_RENDER_MODE, 0);
    this->Insert(renderMode);

// Reload the midi device list

	buildMidiDeviceList() ;

	WatchedVariable *midi=new WatchedVariable("midi",VAR_MIDIDEVICE,midiDeviceList_,midiDeviceListSize_) ;
	this->Insert(midi) ;
	midi->AddObserver(*this) ;


	song_=new Song() ;
	instrumentBank_=new InstrumentBank() ;

	// look if we can find a sav file

	// Makes sure the tables exists for restoring

	TableHolder::GetInstance() ;

	Groove::GetInstance()->Clear() ;

	tempoTapCount_=0 ;

	Status::Set("About to load project") ;

} ;

Project::~Project() {
	delete song_ ;
	delete instrumentBank_ ;
} ;

int Project::GetScale() {
    Variable *v = FindVariable(VAR_SCALE);
    NAssert(v);
    int s = v->GetInt();
    return (s < 0 || s >= scaleCount) ? 0 : s;   // escalas viejas (picoTracker): sin escala
}

void Project::AcceptScale() {
    appliedScale_ = GetScale();
    appliedRoot_ = GetScaleRoot();
}

bool Project::ScaleChanged() {
    return appliedScale_ != GetScale() || appliedRoot_ != GetScaleRoot();
}

// trk2als: los clips que tocan las pistas con kit de batería: sus notas son
// pads y no se ajustan a la escala
void Project::kitClips(bool *skip) {
    for (int c = 0; c < CLIP_COUNT; c++) skip[c] = false;
    for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
        if (IsAudioChannel(ch)) continue;
        int owner = GetTrackInstrument(ch);
        if (owner < 0 || owner >= MAX_SAMPLEINSTRUMENT_COUNT) continue;
        I_Instrument *in = instrumentBank_->GetInstrument(owner);
        if (!in || in->GetType() != IT_SAMPLE || !((SampleInstrument *)in)->IsKit()) continue;
        for (int row = 0; row < 256; row++) {
            unsigned char c = song_->data_[ch + SONG_CHANNEL_COUNT * row];
            if (c < CLIP_COUNT) skip[c] = true;
        }
    }
}

// notas de los clips MIDI fuera de la escala
int Project::OutOfScaleNotes() {
    int scale = GetScale(), root = GetScaleRoot(), n = 0;
    if (scale == 0) return 0;
    bool skip[CLIP_COUNT];
    kitClips(skip);
    for (int c = 0; c < CLIP_COUNT; c++) {
        if (skip[c]) continue;
        Clip &clip = song_->clips_->Get(c);
        for (size_t k = 0; k < clip.notes.size(); k++)
            if (!InScale(scale, root, clip.notes[k].pitch)) n++;
    }
    return n;
}

// cada nota fuera de la escala a la nota de la escala más cercana (en empate,
// la de abajo: Mi en Do menor queda Mib)
int Project::FitToScale() {
    int scale = GetScale(), root = GetScaleRoot(), n = 0;
    if (scale == 0) return 0;
    bool skip[CLIP_COUNT];
    kitClips(skip);   // las baterías no se tocan
    for (int c = 0; c < CLIP_COUNT; c++) {
        if (skip[c]) continue;
        Clip &clip = song_->clips_->Get(c);
        for (size_t k = 0; k < clip.notes.size(); k++) {
            int note = clip.notes[k].pitch;
            if (InScale(scale, root, note)) continue;
            for (int d = 1; d < 12; d++) {
                if (note - d >= 0 && InScale(scale, root, note - d)) { note -= d; break; }
                if (note + d < 0x78 && InScale(scale, root, note + d)) { note += d; break; }
            }
            clip.notes[k].pitch = note;
            n++;
        }
    }
    return n;
}

bool Project::GetFold() {
    Variable *v = FindVariable(VAR_FOLD);
    return v ? v->GetBool() : false;
}

int Project::GetScaleRoot() {
    Variable *v = FindVariable(VAR_SCALEROOT);
    int r = v ? v->GetInt() : 0;
    return (r < 0 || r > 11) ? 0 : r;
}

int Project::GetTempo() {
	Variable *v=FindVariable(VAR_TEMPO) ;
	NAssert(v) ;
	int tempo = v->GetInt()+tempoNudge_ ;
	return tempo ;
} ;

int Project::GetMasterVolume() {
    Variable *v = FindVariable(VAR_MASTERVOL);
    NAssert(v);
	return v->GetInt();
} ;

int Project::GetSoftclip() {
    Variable *v = FindVariable(VAR_SOFTCLIP);
    NAssert(v);
	return v->GetInt();
}

int Project::GetSoftclipGain() {
    Variable *v = FindVariable(VAR_SOFTCLIP_GAIN);
    NAssert(v);
	return v->GetBool();
}

int Project::GetPregain() {
    Variable *v = FindVariable(VAR_PREGAIN);
    NAssert(v);
	return v->GetInt();
}

int Project::GetRenderMode() {
    Variable *v = FindVariable(VAR_RENDER);
    NAssert(v);
	return v->GetInt();
}

void Project::NudgeTempo(int value) {
	if((GetTempo() + tempoNudge_) > 0)
		tempoNudge_ += value;
} ;

void Project::Trigger() {
	if (tempoNudge_!=0) {
		if (tempoNudge_>0) {
			tempoNudge_-- ;
		} else {
			tempoNudge_++ ;
		};
	}
} ;

TrackType Project::GetTrackType(int channel) {
    Variable *v = FindVariable(VAR_TRACKTYPE(channel));
    int t = v ? v->GetInt() : TT_MIDI;
    if (t == TT_OFF) return TT_OFF;
    return (t == TT_AUDIO) ? TT_AUDIO : TT_MIDI;
}

bool Project::TrackExists(int channel) {
    return channel >= 0 && channel < SONG_CHANNEL_COUNT && GetTrackType(channel) != TT_OFF;
}

int Project::TrackCount() {
    int n = 0;
    while (n < SONG_CHANNEL_COUNT && TrackExists(n)) n++;
    return n;
}

const char *Project::GetTrackName(int channel) {
    Variable *v = FindVariable(VAR_TRACKNAME(channel));
    const char *n = v ? v->GetString() : 0;
    if (n && *n) return n;
    static char def[16];
    snprintf(def, sizeof(def), "%s %d", IsAudioChannel(channel) ? "AUDIO" : "MIDI", channel + 1);
    return def;
}

void Project::SetTrackName(int channel, const char *name) {
    Variable *v = FindVariable(VAR_TRACKNAME(channel));
    if (v) v->SetString(name ? name : "");
}

int Project::GetTrackInstrument(int channel) {
    if (!TrackExists(channel) || IsAudioChannel(channel)) return -1;
    Variable *v = FindVariable(VAR_TRACKINSTR(channel));
    int i = v ? v->GetInt() : -1;
    return (i >= 0 && i < MAX_INSTRUMENT_COUNT) ? i : -1;
}

int Project::AddTrack(TrackType type) {
    int ch = TrackCount();
    if (ch >= SONG_CHANNEL_COUNT) return -1;
    unsigned char *data = song_->data_;
    for (int r = 0; r < 256; r++) data[r * SONG_CHANNEL_COUNT + ch] = 0xFF;
    FindVariable(VAR_TRACKTYPE(ch))->SetInt(type == TT_AUDIO ? TT_AUDIO : TT_MIDI);
    FindVariable(VAR_TRACKNAME(ch))->SetString("");
    int owner = -1;
    if (type != TT_AUDIO) {
        // un instrumento de sample que nadie usa: ni otra pista ni un loop
        // de audio
        bool used[MAX_SAMPLEINSTRUMENT_COUNT] = {false};
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
            int o = GetTrackInstrument(c);
            if (c != ch && o >= 0 && o < MAX_SAMPLEINSTRUMENT_COUNT) used[o] = true;
        }
        for (int i = 0; i < 256 * SONG_CHANNEL_COUNT; i++) {
            unsigned char c = data[i];
            if (c < MAX_SAMPLEINSTRUMENT_COUNT && IsAudioChannel(i % SONG_CHANNEL_COUNT)) used[c] = true;
        }
        for (int i = 0; i < AUDIO_CLIP_BASE && owner < 0; i++) {   // los de arriba son loops
            if (!used[i]) owner = i;
        }
        if (owner >= 0) GetInstrumentBank()->GetInstrument(owner)->Purge();   // limpio
    }
    FindVariable(VAR_TRACKINSTR(ch))->SetInt(owner);
    return ch;
}

void Project::DeleteTrack(int channel) {
    if (!TrackExists(channel)) return;
    unsigned char *song = song_->data_;
    // qué es de esta pista y qué usa alguien más
    static bool clipMine[CLIP_COUNT], clipOther[CLIP_COUNT];
    static bool instrMine[MAX_INSTRUMENT_COUNT], instrOther[MAX_INSTRUMENT_COUNT];
    memset(clipMine, 0, sizeof(clipMine)); memset(clipOther, 0, sizeof(clipOther));
    memset(instrMine, 0, sizeof(instrMine)); memset(instrOther, 0, sizeof(instrOther));
    for (int r = 0; r < 256; r++) {
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
            unsigned char v = song[r * SONG_CHANNEL_COUNT + c];
            if (v == 0xFF) continue;
            if (IsAudioChannel(c)) {
                if (v < MAX_INSTRUMENT_COUNT) (c == channel ? instrMine : instrOther)[v] = true;
            } else if (v < CLIP_COUNT) {
                (c == channel ? clipMine : clipOther)[v] = true;
            }
        }
    }
    int owner = GetTrackInstrument(channel);
    if (owner >= 0) {
        instrMine[owner] = true;
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {   // ¿otra pista con el mismo? (no debería)
            if (c != channel && GetTrackInstrument(c) == owner) instrOther[owner] = true;
        }
    }
    // borrar lo que era solo de esta pista
    for (int i = 0; i < CLIP_COUNT; i++) {
        if (clipMine[i] && !clipOther[i]) song_->clips_->Get(i).Clear();
    }
    InstrumentBank *bank = GetInstrumentBank();
    for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
        if (instrMine[i] && !instrOther[i]) bank->GetInstrument(i)->Purge();
    }
    // las pistas de la derecha se recorren una a la izquierda
    for (int r = 0; r < 256; r++) {
        unsigned char *row = song + r * SONG_CHANNEL_COUNT;
        for (int c = channel; c < SONG_CHANNEL_COUNT - 1; c++) row[c] = row[c + 1];
        row[SONG_CHANNEL_COUNT - 1] = 0xFF;
    }
    for (int c = channel; c < SONG_CHANNEL_COUNT - 1; c++) {
        FindVariable(VAR_TRACKTYPE(c))->SetInt(FindVariable(VAR_TRACKTYPE(c + 1))->GetInt());
        std::string n = FindVariable(VAR_TRACKNAME(c + 1))->GetString();
        FindVariable(VAR_TRACKNAME(c))->SetString(n.c_str());
        FindVariable(VAR_TRACKINSTR(c))->SetInt(FindVariable(VAR_TRACKINSTR(c + 1))->GetInt());
    }
    FindVariable(VAR_TRACKTYPE(SONG_CHANNEL_COUNT - 1))->SetInt(TT_OFF);
    FindVariable(VAR_TRACKNAME(SONG_CHANNEL_COUNT - 1))->SetString("");
    FindVariable(VAR_TRACKINSTR(SONG_CHANNEL_COUNT - 1))->SetInt(-1);
}

bool Project::IsAudioChannel(int channel) {
    return GetTrackType(channel) == TT_AUDIO;
}

int Project::GetTranspose() {
	Variable *v=FindVariable(VAR_TRANSPOSE) ;
	NAssert(v) ;
	int result=v->GetInt() ;
	if (result>0x80) {
		result-=128 ;
	}
	return result ;
} ;

bool Project::IsAudioInstrument(int instrument) {
	for (int row=0;row<256;row++) {
		for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
			if (IsAudioChannel(ch) && song_->data_[row*SONG_CHANNEL_COUNT+ch]==instrument) {
				return true ;
			}
		}
	}
	return false ;
}

bool Project::Wrap() {
	Variable *v=FindVariable(VAR_WRAP) ;
	NAssert(v) ;
	return v->GetBool() ;
} ;

InstrumentBank* Project::GetInstrumentBank() {
	return instrumentBank_ ;
} ;

//bool Project::MidiEnabled() {
//	Variable *v=FindVariable(VAR_MIDIENABLE) ;
//	NAssert(v) ;
//	return v->GetBool() ;
//}

void Project::Update(Observable &o,I_ObservableData *d) {
	WatchedVariable &v=(WatchedVariable &)o ;
	switch(v.GetID()) {
		case VAR_MIDIDEVICE:
			MidiService::GetInstance()->SelectDevice(std::string(v.GetString())) ;
 /*           bool enabled=v.GetBool() ;
            Midi *midi=Midi::GetInstance() ;
            if (enabled) {
                midi->Init() ;
            } else {
                midi->Stop() ;
                midi->Close() ;
            }
 */           break ;
    }
}

void Project::Purge() {

	song_->clips_->Purge(song_->data_,256*SONG_CHANNEL_COUNT) ;   // trk2als

} ;

void Project::PurgeInstruments(bool removeFromDisk) {

    bool used[MAX_INSTRUMENT_COUNT] = {false};

	// trk2als: el instrumento de cada pista MIDI
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
		int o=GetTrackInstrument(ch) ;
		if (o>=0) used[o]=true ;
	}

	// trk2als: los loops de las pistas de audio también son instrumentos usados
	for (int row=0;row<256;row++) {
		for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
			unsigned char clip=song_->data_[row*SONG_CHANNEL_COUNT+ch] ;
			if (IsAudioChannel(ch) && clip<MAX_INSTRUMENT_COUNT) {
				used[clip]=true ;
			}
		}
	}

	InstrumentBank *bank=GetInstrumentBank() ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		if (!used[i]) {
			I_Instrument *instrument=bank->GetInstrument(i) ;
			instrument->Purge() ;
		}
	}

  // now see if any samples isn't used and get rid if them if needed

	if (removeFromDisk) {

		// clear used flag

        bool used[MAX_PIG_SAMPLES] = {false};

        // flag all samples actually used

        for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
            I_Instrument *instrument=bank->GetInstrument(i) ;
			if (instrument->GetType()==IT_SAMPLE) {
				SampleInstrument *si=(SampleInstrument *)instrument ;
				int index=si->GetSampleIndex() ;
                if (index >= 0)
                    used[index] = true;
            };
        }

        // Now effectively purge all unused sample from disk

        int purged = 0;
        SamplePool *sp = SamplePool::GetInstance();
        for (int i = 0; i < MAX_PIG_SAMPLES; i++) {
            if ((!used[i])&&(sp->GetSource(i-purged))) {
                sp->PurgeSample(i - purged);
                Trace::Debug("Purged sample [%d]", i - purged);
        		purged++;
				}
        }
    }
}

void Project::RestoreContent(TiXmlElement *element) {

	// Get version attribute

	PersistencyDocument *doc=(PersistencyDocument *)element->GetDocument() ;
	doc->version_=32 ;

	const char *aVersion=element->Attribute("VERSION") ;
	if (aVersion) {
		doc->version_=int(atof(aVersion)*100) ;
	};

	// Get table ratio
	
	int tableRatio ;
	if (!element->Attribute("TABLERATIO",&tableRatio)) {
		tableRatio=(doc->version_<=32)?2:1 ;
	}
	SyncMaster::GetInstance()->SetTableRatio(tableRatio) ;

	// trk2als: un proyecto guardado sin tipos de pista es de antes de ellos:
	// todas de tracker, salvo las de audio que diga 'audiotracks' (las últimas)
	FindVariable(VAR_AUDIOTRACKS)->SetInt(0) ;
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
		FindVariable(VAR_TRACKTYPE(ch))->SetInt(TT_MIDI) ;
		FindVariable(VAR_TRACKINSTR(ch))->SetInt(-1) ;   // trk2als: proyecto de antes: manda la phrase
		FindVariable(VAR_TRACKNAME(ch))->SetString("") ;
	}

	// Now loop on all variables

	TiXmlElement *current=element->FirstChildElement() ;
	while (current) {
		const char *name=current->Attribute("NAME") ;
		const char *value=current->Attribute("VALUE") ;
		Variable *v=FindVariable(name) ;
		if (v) {
			v->SetString(value) ;
		} ;
		current=current->NextSiblingElement() ;
	} ;
	// trk2als: pasar el 'audiotracks' viejo a tipos de pista
	int audio=FindVariable(VAR_AUDIOTRACKS)->GetInt() ;
	for (int ch=SONG_CHANNEL_COUNT-audio;ch<SONG_CHANNEL_COUNT;ch++) {
		if (ch>=0) FindVariable(VAR_TRACKTYPE(ch))->SetInt(TT_AUDIO) ;
	}
	FindVariable(VAR_AUDIOTRACKS)->SetInt(0) ;
	AcceptScale() ;   // trk2als: lo guardado ya está en su escala
	// las pistas TRK de antes ahora son MIDI
	for (int ch=0;ch<SONG_CHANNEL_COUNT;ch++) {
		Variable *t=FindVariable(VAR_TRACKTYPE(ch)) ;
		if (t->GetInt()==TT_TRACKER) t->SetInt(TT_MIDI) ;
	}
};


void Project::SaveContent(TiXmlNode *node) {

	// store project version

	TiXmlElement *element=(TiXmlElement *)node ;
	element->SetAttribute("VERSION",PROJECT_NUMBER) ;

	// store table ratio if not one

	int tableRatio=SyncMaster::GetInstance()->GetTableRatio() ;
	if (tableRatio!=1) {
		element->SetAttribute("TABLERATIO",tableRatio) ;
	}
	// save all of the project's parameters
	
	IteratorPtr<Variable> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		TiXmlElement param("PARAMETER") ;
		Variable v=it->CurrentItem() ;
		param.SetAttribute("NAME",v.GetName()) ;
		param.SetAttribute("VALUE",v.GetString()) ;
		node->InsertEndChild(param) ;
	}
} ;

void Project::buildMidiDeviceList() {
	if (midiDeviceList_) {
		for (int i=0;i<midiDeviceListSize_;i++) {
			SAFE_FREE(midiDeviceList_[i]) ;
		}
		SAFE_FREE(midiDeviceList_) ;
	} 
	midiDeviceListSize_=MidiService::GetInstance()->Size() ;
	midiDeviceList_=(char **)SYS_MALLOC(midiDeviceListSize_*sizeof(char *)) ;
	IteratorPtr<MidiOutDevice> it(MidiService::GetInstance()->GetIterator()) ;
	it->Begin() ;
	for (int i=0;i<midiDeviceListSize_;i++) {
		std::string deviceName=it->CurrentItem().GetName() ;
		midiDeviceList_[i]=(char *)malloc(sizeof(char *)*deviceName.size()+1);
		strcpy(midiDeviceList_[i],deviceName.c_str()) ;
		it->Next() ;
	} ;
} ;

void Project::OnTempoTap() {

	unsigned long now=System::GetInstance()->GetClock() ;

  if (tempoTapCount_!=0) {
		// count last tick tempo and see if in range
		unsigned millisec=now-lastTap_[tempoTapCount_-1] ;
		int t=int(60000/(float)millisec) ;
		if (t>30) {
			if (tempoTapCount_==MAX_TAP) {
				for (int i=0;i<int(tempoTapCount_)-1;i++)  {
					lastTap_[i]=lastTap_[i+1] ;
				}				
			} else {
				tempoTapCount_++ ;
			}
			int tempo=int(60000*(tempoTapCount_-1)/(float)(now-lastTap_[0])) ;
			Variable *v=FindVariable(VAR_TEMPO) ;
			v->SetInt(tempo) ;
		} else {
			tempoTapCount_=1 ;
		}
	} else {
		tempoTapCount_=1 ;
	}
	lastTap_[tempoTapCount_-1]=now ;
}
