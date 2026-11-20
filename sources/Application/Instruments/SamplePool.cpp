#include "SamplePool.h"
#include <string.h>
#include <stdlib.h>
#include "System/Console/Trace.h"
#include "Application/Persistency/PersistencyService.h" 
#include "System/io/Status.h"
#include <string>
#include "SoundFontSample.h"
#include "SoundFontPreset.h"
#include "SoundFontManager.h"
#include "Application/Model/Config.h"

#define SAMPLE_LIB "root:samplelib" 

SamplePool::SamplePool() {
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		names_[i]=NULL ;
		wav_[i]=NULL ;
	} ;
	count_=0 ;
} ;

SamplePool::~SamplePool() {
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		SAFE_DELETE(wav_[i]) ;
		SAFE_FREE(names_[i]) ;
	} ;
} ;

const char *SamplePool::GetSampleLib() {
	Config *config=Config::GetInstance() ;
	const char *lib=config->GetValue("SAMPLELIB") ;
	return lib?lib:SAMPLE_LIB ;
} 

void SamplePool::Reset() {
	count_=0 ;
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		SAFE_DELETE(wav_[i]) ;
		SAFE_FREE(names_[i]) ;
		refs_[i].clear() ;
	} ;
	missing_.clear() ;
	SoundFontManager::GetInstance()->Reset() ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_INVALID_DIR, SLOAD_ERR_MAX_SAMPLES,
   SLOAD_ERR_MAX_SOUNDFONTS, SLOAD_ERR_MAX_SAMPLES | SLOAD_ERR_MAX_SOUNDFONTS}.
*/
unsigned int SamplePool::Load() {

    Path sampleDir("samples:");

    I_Dir *dir = FileSystem::GetInstance()->Open(sampleDir.GetPath().c_str());
    if (!dir) {
        return SLOAD_ERR_INVALID_DIR;
    }

    unsigned int result = SLOAD_OK;

    // First, find all wav files

    dir->GetContent("*.wav");
    IteratorPtr<Path> it(dir->GetIterator()) ;
	count_=0 ;

	for(it->Begin();!it->IsDone();it->Next()) {
		Path &path=it->CurrentItem() ;
        Trace::Log("Load", "%s", path.GetCanonicalPath().c_str());
        loadSample(path.GetPath().c_str()) ;
		if (count_==MAX_PIG_SAMPLES) {
            result |= SLOAD_ERR_MAX_SAMPLES;
            Trace::Error("Warning maximum sample count reached");
            break;
		} ;

	} ;

	// now, let's look at soundfonts

	dir->GetContent("*.sf2") ;
	IteratorPtr<Path> it2(dir->GetIterator()) ;
    int sf_idx = 0;

    for (it2->Begin(); !it2->IsDone(); it2->Next(), sf_idx++) {
        Path &path=it2->CurrentItem() ;
        Trace::Log("Load", "%s", path.GetCanonicalPath().c_str());
        loadSoundFont(path.GetPath().c_str()) ;
		if (sf_idx == MAX_SOUNDFONTS) {
            result |= SLOAD_ERR_MAX_SOUNDFONTS;
            Trace::Error("Warning maximum soundfont count reached");
            break;
		} ;
    };

    delete dir;

    // now sort the samples
    Sort();

    return result;
};

void SamplePool::Sort() {
    int rest=count_;
	while(rest>0) {
        int index = 0;
        for (int i=1;i<rest;i++) {
			if (strcmp(names_[i],names_[index])>0) {
                index = i;
            }
        }
        SoundSource *tWav = wav_[index];
		char *tName = names_[index];
		std::string tRef = refs_[index];
		wav_[index] = wav_[rest-1];
		names_[index] = names_[rest-1];
		refs_[index] = refs_[rest-1];
		wav_[rest-1] = tWav;
        names_[rest - 1] = tName;
        refs_[rest - 1] = tRef;
        rest--;
	}
}

int SamplePool::getIndexOf(const char *name) {
    for (int i=0;i<count_;i++) {
		if (strcmp(names_[i], name)==0) {
			return i;
		}
	}
	return -1;
}

SoundSource *SamplePool::GetSource(int i) {
	return wav_[i] ;
} ;

char **SamplePool::GetNameList() {
	return names_ ;
} ;

int SamplePool::GetNameListSize() {
	return count_ ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_MAX_SAMPLES, SLOAD_ERR_INPUT_FILE}.
*/
int SamplePool::loadSample(const char *path) {

    if (count_==MAX_PIG_SAMPLES) return SLOAD_ERR_MAX_SAMPLES ;

Path sPath(path) ;
Status::Set("Loading %s", sPath.GetName().c_str());
Trace::Log("loadSample", "%s", path);

Path wavPath(path);
WavFile *wave = WavFile::Open(path);
if (wave) {
    wav_[count_] = wave;
    const std::string name = wavPath.GetName();
    names_[count_] = (char *)SYS_MALLOC(name.length() + 1);
    strcpy(names_[count_], name.c_str());
    count_++;
    wave->GetBuffer(0, wave->GetSize(-1));
    wave->Close();
    return SLOAD_OK;
} else {
    Trace::Error("Failed to load samples %s", wavPath.GetName().c_str());
    return SLOAD_ERR_INPUT_FILE;
}
}

#define IMPORT_CHUNK_SIZE 65536   // trk2als: antes 1000 bytes por escritura
#include <unistd.h>
// trk2als: mientras suena la música, la copia va con pausitas para no
// acaparar la tarjeta (InstrumentPresets::Prepare lo prende)
volatile bool g_importGentle = false ;

/*
  Returns a nonnegative int or an element of
  {-SLOAD_ERR_INVALID_DIR, -SLOAD_ERR_INPUT_FILE, -SLOAD_ERR_MAX_SAMPLES,
   -SLOAD_ERR_MAX_SOUNDFONTS}.
*/
int SamplePool::ImportSample(Path &path) {

    if (count_ == MAX_PIG_SAMPLES)
        return -SLOAD_ERR_MAX_SAMPLES;

    // trk2als: un .wav ya no se copia al proyecto: se usa desde donde está
    if (path.Matches("*.wav")) {
        int idx = Acquire(path);
        return idx >= 0 ? idx : -SLOAD_ERR_INPUT_FILE;
    }

    // construct target path

    std::string dpath = "samples:";
    dpath+=path.GetName() ;
	Path dstPath(dpath.c_str()) ;

    // Opens files

	I_File *fin=FileSystem::GetInstance()->Open(path.GetPath().c_str(),"r") ;
    if (!fin) {
        Trace::Error("Failed to open input file %s",
                     path.GetCanonicalPath().c_str());
        return -SLOAD_ERR_INPUT_FILE;
    };
    fin->Seek(0, SEEK_END);
    long size=fin->Tell() ;
	fin->Seek(0,SEEK_SET) ;

	I_File *fout=FileSystem::GetInstance()->Open(dstPath.GetPath().c_str(),"w") ;
	if (!fout) {
		fin->Close() ;
        delete (fin);
        return -SLOAD_ERR_OUTPUT_FILE ;
	} ;

    // copy file to current project

    static char buffer[IMPORT_CHUNK_SIZE];   // (en la pila no: 64 KB)
    while (size>0) {
		int count=(size>IMPORT_CHUNK_SIZE)?IMPORT_CHUNK_SIZE:size ;
		fin->Read(buffer,1,count) ;
		fout->Write(buffer,1,count) ;
		size-=count ;
		if (g_importGentle) usleep(2000) ;
	} ;

	fin->Close() ;
	fout->Close() ;
	delete(fin) ;
	delete(fout) ;

    // now load the sample
    int status = dstPath.Matches("*.wav")
                     ? loadSample(dstPath.GetPath().c_str())
                     : loadSoundFont(dstPath.GetPath().c_str());

    SetChanged();
    SamplePoolEvent ev ;
	ev.index_=count_-1 ;
	ev.type_=SPET_INSERT ;
    NotifyObservers(&ev);
    return !status ?(count_-1):(-status) ;
};

bool SamplePool::IsImported(std::string name) {
    std::string dpath="samples:";
    dpath += name;
    Path dstPath(dpath.c_str());
    Path checkPath(dstPath.GetPath());
    return checkPath.Exists();
}

/*
    Unsorted reassign for now
    Returns the index of the sample in the pool
    count_-1 position if new
    previous position if already imported
*/
int SamplePool::Reassign(std::string name, bool imported) {
    if (count_ == MAX_PIG_SAMPLES)
        return -1;
    int insertedIndex = getIndexOf(name.c_str());
    if (imported)
        unload(insertedIndex);

    std::string aliasPath = "samples:";
    aliasPath += name;
    Path dstPath(aliasPath.c_str());

    if (loadSample(dstPath.GetCanonicalPath().c_str())) {
        SetChanged();
        SamplePoolEvent ev;
        ev.index_ = getIndexOf(name.c_str());;
        ev.type_=SPET_INSERT;
        NotifyObservers(&ev);
        return ev.index_;
    }
    return -1;
}

void SamplePool::PurgeSample(int i) {

	// construct the path of the sample to delete

	std::string wavPath="samples:" ;
	wavPath+=names_[i] ;
	Path path(wavPath.c_str()) ;
	//delete wav
	SAFE_DELETE(wav_[i]) ;
	// delete name entry
	SAFE_FREE(names_[i]) ;   // trk2als: se pidió con SYS_MALLOC

	// delete file (trk2als: una referencia no es del proyecto: no se borra)
	if (refs_[i].empty()) FileSystem::GetInstance()->Delete(path.GetPath().c_str()) ;

	// shift all entries from deleted to end
	for (int j=i;j<count_-1;j++) {
		wav_[j]=wav_[j+1] ;
		names_[j]=names_[j+1] ;
		refs_[j]=refs_[j+1] ;
	} ;
	// decrease sample count
	count_-- ;
	wav_[count_]=0 ;
	names_[count_]=0 ;
	refs_[count_].clear() ;

	// now notify observers
	SetChanged() ;
	SamplePoolEvent ev ;
	ev.index_=i ;
	ev.type_=SPET_DELETE ;
	NotifyObservers(&ev) ;
} ;

void SamplePool::unload(int i) {

    // construct the path of the sample to delete

	std::string wavPath="samples:" ;
	wavPath+=names_[i] ;
	Path path(wavPath.c_str()) ;

	// shift all entries from deleted to end
	for (int j=i;j<count_-1;j++) {
		wav_[j]=wav_[j+1] ;
		names_[j]=names_[j+1] ;
		refs_[j]=refs_[j+1] ;
	} ;
	// decrease sample count
	count_-- ;
	wav_[count_]=0 ;
	names_[count_]=0 ;
	refs_[count_].clear() ;

	// now notify observers
	SetChanged() ;
	SamplePoolEvent ev ;
	ev.index_=i ;
	ev.type_=SPET_DELETE ;
	NotifyObservers(&ev) ;
}

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_MAX_SOUNDFONTS, SLOAD_ERR_INPUT_FILE}.
*/
int SamplePool::loadSoundFont(const char *path) {

    Path sPath(path);
    Status::Set("Loading %s", sPath.GetName().c_str());
    Trace::Log("loadSoundFont", "%s", path);

    sfBankID id = SoundFontManager::GetInstance()->LoadBank(path);
    if (id==-SF_BANK_TABLE_FULL) {
		return SLOAD_ERR_MAX_SOUNDFONTS ;
    } else if (id < 0) {
        return SLOAD_ERR_INPUT_FILE;
    }

    // Grab the sample offset

    long offset = sfGetSMPLOffset(id);

    // Add all presets of the sf

    WORD presetCount = 0;
    SFPRESETHDRPTR pHeaders=sfGetPresetHdrs(id,&presetCount); 

	for (int i=0;i<presetCount;i++) {
		if (count_<MAX_PIG_SAMPLES) {
			sfPresetHdr current=pHeaders[i] ;
			SoundFontPreset *sp=new SoundFontPreset(id,i) ;
			sp->SetPath(path) ;   // trk2als
			wav_[count_]=sp ;
			const char *name=pHeaders[i].achPresetName ;
            Trace::Log("loadSoundFont", "%s", name);
            names_[count_] = (char *)SYS_MALLOC(strlen(name) + 1);
            strcpy(names_[count_], name);
            count_++;
		}
	}
    /*
        // Get Sample information

        WORD headerCount=0 ;
        SFSAMPLEHDRPTR  &headers=sfGetSampHdrs(id,&headerCount );

        // Loop on every sample, add them

        for (int i=0;i<headerCount;i++) {
            if (count_<MAX_PIG_SAMPLES) {
                sfSampleHdr &current=headers[i] ;
                wav_[count_]=new SoundFontSample(current) ;
                const char *name=headers[i].achSampleName ;
                names_[count_]=(char*)SYS_MALLOC(strlen(name)+1) ;
                strcpy(names_[count_],name) ;
                count_++ ;
            }
        }
    */
    return SLOAD_OK;
} ;

// trk2als
int SamplePool::IndexOf(const char *name) {
    for (int i = 0; i < count_; i++) {
        if (names_[i] && !strcmp(names_[i], name)) return i;
    }
    return -1;
}

int SamplePool::LoadProjectSample(const char *name) {
    int idx = IndexOf(name);
    if (idx >= 0) return idx;
    std::string path = "samples:";
    path += name;
    Path p(path.c_str());
    if (loadSample(p.GetPath().c_str()) != SLOAD_OK) return -1;
    SetChanged();
    SamplePoolEvent ev;
    ev.index_ = count_ - 1;
    ev.type_ = SPET_INSERT;
    NotifyObservers(&ev);
    return count_ - 1;
}

/************************************************************
 trk2als: samples por referencia
 ************************************************************/

#include <fstream>
#include <unistd.h>

// la ruta como se guarda: "root:..." si está dentro de la carpeta de Chancho
// (así el proyecto sirve en la consola y en la compu), si no, completa
static std::string refKey(const std::string &full) {
	std::string root = Path("root:").GetPath() ;
	while (!root.empty() && root[root.size() - 1] == '/') root.erase(root.size() - 1) ;
	if (!root.empty() && full.compare(0, root.size() + 1, root + "/") == 0)
		return "root:" + full.substr(root.size() + 1) ;
	return full ;
}

static std::string resolveKey(const std::string &key) {
	return Path(key.c_str()).GetPath() ;
}

int SamplePool::loadAs(const char *path, const std::string &name) {
	if (count_ == MAX_PIG_SAMPLES) return -1 ;
	Trace::Log("loadSample", "%s (ref)", path) ;
	WavFile *wave = WavFile::Open(path) ;
	if (!wave) {
		Trace::Error("Failed to load samples %s", path) ;
		return -1 ;
	}
	wav_[count_] = wave ;
	names_[count_] = (char *)SYS_MALLOC(name.length() + 1) ;
	strcpy(names_[count_], name.c_str()) ;
	count_++ ;
	wave->GetBuffer(0, wave->GetSize(-1)) ;
	wave->Close() ;
	return count_ - 1 ;
}

int SamplePool::Acquire(const Path &srcIn) {
	Path src(srcIn) ;
	std::string full = src.GetPath() ;
	std::string key = refKey(full) ;
	std::string name = src.GetName() ;
	// el mismo archivo ya está
	for (int i = 0; i < count_; i++)
		if (refs_[i] == key) return i ;
	// ya está en la carpeta del proyecto (o es una copia de antes, del mismo nombre)
	std::string projSamples = Path("samples:").GetPath() ;
	while (!projSamples.empty() && projSamples[projSamples.size() - 1] == '/') projSamples.erase(projSamples.size() - 1) ;
	bool inProject = full.compare(0, projSamples.size() + 1, projSamples + "/") == 0 ;
	int idx = IndexOf(name.c_str()) ;
	if (idx >= 0 && refs_[idx].empty()) return idx ;
	if (inProject) return LoadProjectSample(name.c_str()) ;
	// otro sample se llama igual: este entra con otro nombre
	if (idx >= 0) {
		std::string base = name, ext ;
		size_t dot = name.find_last_of('.') ;
		if (dot != std::string::npos) { base = name.substr(0, dot) ; ext = name.substr(dot) ; }
		for (int k = 2; k < 100; k++) {
			char b[16] ;
			snprintf(b, sizeof(b), " (%d)", k) ;
			std::string alt = base + b + ext ;
			if (IndexOf(alt.c_str()) < 0) { name = alt ; break ; }
		}
	}
	int at = loadAs(full.c_str(), name) ;
	if (at < 0) return -1 ;
	refs_[at] = key ;
	SetChanged() ;
	SamplePoolEvent ev ;
	ev.index_ = at ;
	ev.type_ = SPET_INSERT ;
	NotifyObservers(&ev) ;
	return at ;
}

std::string SamplePool::SourcePath(int i) {
	if (i < 0 || i >= count_ || !names_[i]) return "" ;
	if (!refs_[i].empty()) return resolveKey(refs_[i]) ;
	return Path((std::string("samples:") + names_[i]).c_str()).GetPath() ;
}

int SamplePool::ReferenceCount() {
	int n = 0 ;
	for (int i = 0; i < count_; i++) if (!refs_[i].empty()) n++ ;
	return n ;
}

// project:samples.ref: una línea por referencia, "nombre<TAB>ruta"
void SamplePool::SaveRefs() {
	std::string file = Path("project:samples.ref").GetPath() ;
	if (ReferenceCount() == 0) {   // nada: que no quede un archivo viejo
		unlink(file.c_str()) ;
		return ;
	}
	std::ofstream o(file.c_str()) ;
	for (int i = 0; i < count_; i++)
		if (!refs_[i].empty() && names_[i]) o << names_[i] << "\t" << refs_[i] << "\n" ;
}

int SamplePool::LoadRefs() {
	missing_.clear() ;
	std::ifstream in(Path("project:samples.ref").GetPath().c_str()) ;
	std::string line ;
	while (std::getline(in, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1) ;
		size_t tab = line.find('\t') ;
		if (tab == std::string::npos) continue ;
		std::string name = line.substr(0, tab), key = line.substr(tab + 1) ;
		if (IndexOf(name.c_str()) >= 0) continue ;   // ya está (una copia en el proyecto)
		std::string full = resolveKey(key) ;
		if (access(full.c_str(), R_OK) != 0) {
			Trace::Error("Falta el sample %s (%s)", name.c_str(), full.c_str()) ;
			missing_.push_back(name) ;
			continue ;
		}
		int at = loadAs(full.c_str(), name) ;
		if (at < 0) { missing_.push_back(name) ; continue ; }
		refs_[at] = key ;
	}
	if (count_ > 0) {
		SetChanged() ;
		SamplePoolEvent ev ;
		ev.index_ = count_ - 1 ;
		ev.type_ = SPET_INSERT ;
		NotifyObservers(&ev) ;
	}
	return (int)missing_.size() ;
}

// Recopilar: las referencias se copian a la carpeta del proyecto
int SamplePool::Collect() {
	int n = 0 ;
	for (int i = 0; i < count_; i++) {
		if (refs_[i].empty() || !names_[i]) continue ;
		std::string from = resolveKey(refs_[i]) ;
		std::string to = Path((std::string("samples:") + names_[i]).c_str()).GetPath() ;
		FileSystem *fs = FileSystem::GetInstance() ;
		I_File *fi = fs->Open(from.c_str(), "r") ;
		if (!fi) { Trace::Error("No se pudo leer %s", from.c_str()) ; continue ; }
		I_File *fo = fs->Open(to.c_str(), "w") ;
		if (!fo) { fi->Close() ; delete fi ; Trace::Error("No se pudo escribir %s", to.c_str()) ; continue ; }
		static char buf[65536] ;
		fi->Seek(0, SEEK_END) ;
		long size = fi->Tell() ;
		fi->Seek(0, SEEK_SET) ;
		bool ok = true ;
		while (size > 0) {
			int k = size > (long)sizeof(buf) ? (int)sizeof(buf) : (int)size ;
			if (fi->Read(buf, 1, k) != k) { ok = false ; break ; }
			if (fo->Write(buf, 1, k) != k) { ok = false ; break ; }
			size -= k ;
		}
		fi->Close() ; delete fi ;
		fo->Close() ; delete fo ;
		if (!ok) { fs->Delete(to.c_str()) ; continue ; }
		refs_[i].clear() ;
		n++ ;
	}
	return n ;
}
