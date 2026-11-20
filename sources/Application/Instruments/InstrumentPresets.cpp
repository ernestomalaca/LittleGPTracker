#include "Application/Mixer/MixerService.h"
#include "InstrumentPresets.h"
#include "SampleInstrument.h"
#include "SamplePool.h"
#include "Externals/TinyXML/tinyxml.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Console/Trace.h"
#include <algorithm>
#include <string.h>
#include <stdlib.h>

#define PRESET_DIR "root:presets"
#define PRESET_FILE "preset.xml"

// parámetros que dependen del proyecto: no viajan en el preset
static bool skipParam(const char *name) {
	return !strcmp(name, "table") || !strcmp(name, "table automation") ;
}

static bool copyFile(const std::string &from, const std::string &to) {
	FileSystem *fs = FileSystem::GetInstance() ;
	I_File *fin = fs->Open(from.c_str(), "r") ;
	if (!fin) return false ;
	I_File *fout = fs->Open(to.c_str(), "w") ;
	if (!fout) {
		fin->Close() ;
		delete fin ;
		return false ;
	}
	fin->Seek(0, SEEK_END) ;
	long size = fin->Tell() ;
	fin->Seek(0, SEEK_SET) ;
	char buffer[4096] ;
	while (size > 0) {
		int n = size > (long)sizeof(buffer) ? (int)sizeof(buffer) : (int)size ;
		fin->Read(buffer, 1, n) ;
		fout->Write(buffer, 1, n) ;
		size -= n ;
	}
	fin->Close() ;
	fout->Close() ;
	delete fin ;
	delete fout ;
	return true ;
}

// Path con alias ("root:...") no sabe si existe: hay que resolverlo antes
static Path resolved(const Path &p) {
	return Path(p.GetPath()) ;
}

static Path presetPath(const std::string &name) {
	// "Drums/KICK1": una carpeta por cada parte
	Path p(PRESET_DIR) ;
	size_t start=0 ;
	while (start<name.size()) {
		size_t slash=name.find('/',start) ;
		std::string part=name.substr(start,slash==std::string::npos?std::string::npos:slash-start) ;
		if (!part.empty()) p=p.Descend(part) ;
		if (slash==std::string::npos) break ;
		start=slash+1 ;
	}
	return resolved(p) ;
}

std::string &InstrumentPresets::LastFolder() {
	static std::string last ;
	return last ;
}

void InstrumentPresets::ListFolder(const std::string &folder, std::vector<std::string> &folders,
                                   std::vector<std::string> &presets) {
	folders.clear() ;
	presets.clear() ;
	Path root=presetPath(folder) ;
	if (!root.Exists()) return ;
	I_Dir *dir=FileSystem::GetInstance()->Open(root.GetPath().c_str()) ;
	if (!dir) return ;
	dir->GetContent((char *)"*") ;
	IteratorPtr<Path> it(dir->GetIterator()) ;
	for (it->Begin(); !it->IsDone(); it->Next()) {
		Path p=it->CurrentItem() ;
		std::string n=p.GetName() ;
		if (n.empty() || n[0]=='.' || !p.IsDirectory()) continue ;
		if (Path(p).Descend(PRESET_FILE).Exists()) presets.push_back(n) ;
		else folders.push_back(n) ;
	}
	delete dir ;
	std::sort(folders.begin(),folders.end()) ;
	std::sort(presets.begin(),presets.end()) ;
}

std::vector<std::string> InstrumentPresets::List() {
	std::vector<std::string> names ;
	Path root = resolved(Path(PRESET_DIR)) ;
	if (!root.Exists()) return names ;
	I_Dir *dir = FileSystem::GetInstance()->Open(root.GetPath().c_str()) ;
	if (!dir) return names ;
	dir->GetContent((char *)"*") ;
	IteratorPtr<Path> it(dir->GetIterator()) ;
	for (it->Begin(); !it->IsDone(); it->Next()) {
		Path p = it->CurrentItem() ;
		std::string n = p.GetName() ;
		if (n.empty() || n[0] == '.') continue ;
		if (p.IsDirectory() && Path(p).Descend(PRESET_FILE).Exists()) {
			names.push_back(n) ;
		}
	}
	delete dir ;
	std::sort(names.begin(), names.end()) ;
	return names ;
}

bool InstrumentPresets::Exists(const std::string &name) {
	return presetPath(name).Descend(PRESET_FILE).Exists() ;
}

std::string InstrumentPresets::SuggestedName(SampleInstrument *instr) {
	int index = instr->GetSampleIndex() ;
	if (index < 0) return "preset" ;
	std::string n = SamplePool::GetInstance()->GetNameList()[index] ;
	if (instr->IsZoned()) n = instr->ZoneSetName() ;   // soundfont: "marimba" 
	if (instr->IsKit() && *instr->FindVariable(SIP_S_KITNAME)->GetString())
		n = instr->FindVariable(SIP_S_KITNAME)->GetString() ;   // kit: su nombre
	size_t dot = n.find_last_of('.') ;
	if (dot != std::string::npos) n = n.substr(0, dot) ;
	// solo lo que se puede escribir con el teclado de la consola
	std::string out ;
	for (size_t i = 0; i < n.size(); i++) {
		char c = n[i] ;
		if (c >= 'a' && c <= 'z') c = c - 'a' + 'A' ;
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c ;
		else if (c == '_' || c == '-' || c == ' ') out += '_' ;
	}
	if (out.empty()) out = "PRESET" ;
	return out ;
}

bool InstrumentPresets::Save(SampleInstrument *instr, const std::string &name) {
	if (name.empty()) return false ;
	FileSystem *fs = FileSystem::GetInstance() ;
	Path root = resolved(Path(PRESET_DIR)) ;
	if (!root.Exists() && fs->MakeDir(root.GetPath().c_str()).Failed()) {
		Trace::Error("No se pudo crear %s", root.GetPath().c_str()) ;
		return false ;
	}
	// las carpetas de la categoría ("Drums/Kicks/...") si faltan
	for (size_t slash=name.find('/'); slash!=std::string::npos; slash=name.find('/',slash+1)) {
		Path cat=presetPath(name.substr(0,slash)) ;
		if (!cat.Exists()) fs->MakeDir(cat.GetPath().c_str()) ;
	}
	Path folder = presetPath(name) ;
	if (!folder.Exists() && fs->MakeDir(folder.GetPath().c_str()).Failed()) {
		Trace::Error("No se pudo crear %s", folder.GetPath().c_str()) ;
		return false ;
	}

	// la copia del sample va junto al preset (en un soundfont, la de cada zona)
	std::vector<std::string> samples ;
	int index = instr->GetSampleIndex() ;
	if (index >= 0) samples.push_back(SamplePool::GetInstance()->GetNameList()[index]) ;
	if (instr->IsZoned()) {
		const std::vector<ZoneSource::Zone> &zones = instr->Zones() ;
		for (size_t i = 0; i < zones.size(); i++) {
			if (std::find(samples.begin(), samples.end(), zones[i].name) == samples.end())
				samples.push_back(zones[i].name) ;
		}
	}
	for (size_t i = 0; i < samples.size(); i++) {
		Path src(SamplePool::GetInstance()->SourcePath(SamplePool::GetInstance()->IndexOf(samples[i].c_str()))) ;
		Path dst = Path(folder).Descend(samples[i]) ;
		if (src.GetPath() == dst.GetPath()) continue ;   // ya vive ahí (se actualiza el preset)
		if (!copyFile(src.GetPath(), dst.GetPath())) {
			Trace::Error("No se pudo copiar %s", src.GetPath().c_str()) ;
			return false ;
		}
	}

	if (instr->IsKit()) {   // el kit se llama como su preset
		size_t sl = name.find_last_of('/') ;
		instr->FindVariable(SIP_S_KITNAME)->SetString((sl == std::string::npos ? name : name.substr(sl + 1)).c_str()) ;
	}
	Path file = Path(folder).Descend(PRESET_FILE) ;
	TiXmlDocument doc(file.GetPath()) ;
	TiXmlElement top("LGPTPRESET") ;
	top.SetAttribute("VERSION", "1") ;
	TiXmlElement data("INSTRUMENT") ;
	data.SetAttribute("TYPE", "SAMPLE") ;
	IteratorPtr<Variable> it(instr->GetIterator()) ;
	for (it->Begin(); !it->IsDone(); it->Next()) {
		Variable &v = it->CurrentItem() ;
		if (skipParam(v.GetName())) continue ;
		TiXmlElement param("PARAM") ;
		param.SetAttribute("NAME", v.GetName()) ;
		param.SetAttribute("VALUE", v.GetString()) ;
		data.InsertEndChild(param) ;
	}
	top.InsertEndChild(data) ;
	doc.InsertEndChild(top) ;
	return doc.SaveFile() ;
}

bool InstrumentPresets::Load(SampleInstrument *instr, const std::string &name) {
	Path folder = presetPath(name) ;
	Path file = Path(folder).Descend(PRESET_FILE) ;
	TiXmlDocument doc(file.GetPath()) ;
	if (!doc.LoadFile()) {
		Trace::Error("No se pudo leer %s", file.GetPath().c_str()) ;
		return false ;
	}
	TiXmlElement *top = doc.FirstChildElement("LGPTPRESET") ;
	TiXmlElement *data = top ? top->FirstChildElement("INSTRUMENT") : 0 ;
	if (!data) return false ;

	// primero el sample: si el proyecto no lo tiene, se copia desde el preset
	std::string sample, zones ;
	for (TiXmlElement *p = data->FirstChildElement("PARAM"); p; p = p->NextSiblingElement("PARAM")) {
		const char *n = p->Attribute("NAME") ;
		const char *v = p->Attribute("VALUE") ;
		if (n && v && !strcmp(n, "sample")) sample = v ;
		if (n && v && !strcmp(n, "s zones")) zones = v ;
	}
	SamplePool *pool = SamplePool::GetInstance() ;
	// trk2als: cada .wav del preset se usa desde su carpeta (sin copiarlo al
	// proyecto); si en el proyecto otro sample se llama igual, entra con
	// otro nombre y las zonas se reescriben con ese nombre
	std::string newZones ;
	if (!zones.empty()) {
		size_t pos = 0 ;
		while (pos < zones.size()) {
			size_t end = zones.find(';', pos) ;
			if (end == std::string::npos) end = zones.size() ;
			std::string item = zones.substr(pos, end - pos) ;
			pos = end + 1 ;
			size_t comma = std::string::npos ;
			int commas = 0 ;
			for (size_t c = 0; c < item.size(); c++) {
				if (item[c] == ',' && ++commas == 6) { comma = c ; break ; }
			}
			if (comma == std::string::npos) continue ;
			std::string wav = item.substr(comma + 1) ;
			Path src = Path(folder).Descend(wav) ;
			int idx = src.Exists() ? pool->Acquire(src) : pool->IndexOf(wav.c_str()) ;
			if (idx < 0) {
				Trace::Error("Falta el sample %s del preset", wav.c_str()) ;
				return false ;
			}
			if (!newZones.empty()) newZones += ";" ;
			newZones += item.substr(0, comma + 1) + pool->GetNameList()[idx] ;
		}
		zones = newZones ;
	}
	int index = -1 ;
	if (!sample.empty() && sample != "(null)") {
		Path src = Path(folder).Descend(sample) ;
		index = src.Exists() ? pool->Acquire(src) : pool->IndexOf(sample.c_str()) ;
		if (index < 0) {
			Trace::Error("Falta el sample %s del preset", sample.c_str()) ;
			return false ;
		}
	}

	// el sample primero: cambiarlo reinicia start/end/loop del instrumento
	if (index >= 0) {
		instr->AssignSample(index) ;
		instr->RefreshSample() ;
	}

	// lo del kit anterior no se queda si el preset no lo trae
	instr->FindVariable(SIP_S_PADS)->SetString("") ;
	instr->FindVariable(SIP_S_KITNAME)->SetString("") ;
	instr->FindVariable(SIP_S_KIT)->SetInt(0) ;
	// luego todos los parámetros (los que este instrumento conozca)
	bool newFilter = false ;
	for (TiXmlElement *p = data->FirstChildElement("PARAM"); p; p = p->NextSiblingElement("PARAM")) {
		const char *n = p->Attribute("NAME") ;
		const char *v = p->Attribute("VALUE") ;
		if (!n || !v || !strcmp(n, "sample") || skipParam(n)) continue ;
		if (!strcmp(n, "s filter v")) newFilter = true ;
		IteratorPtr<Variable> it(instr->GetIterator()) ;
		for (it->Begin(); !it->IsDone(); it->Next()) {
			Variable &var = it->CurrentItem() ;
			if (!strcmp(var.GetName(), n)) {
				var.SetString(v) ;
				break ;
			}
		}
	}
	// las zonas, ya con sus samples en el pool (vacío = un solo sample)
	instr->SetZones(zones) ;
	if (instr->GetSimpler(SIP_S_KIT) && !*instr->FindVariable(SIP_S_KITNAME)->GetString()) {
		size_t sl = name.find_last_of('/') ;   // kit sin nombre: el de su carpeta
		instr->FindVariable(SIP_S_KITNAME)->SetString((sl == std::string::npos ? name : name.substr(sl + 1)).c_str()) ;
	}
	// presets de antes del filtro nuevo: se convierte como al abrir un proyecto
	if (!newFilter) {
		instr->FindVariable(SIP_S_FVER)->SetInt(0) ;
		instr->InitSimpler() ;
	}
	return true ;
}

/************************************************************
 trk2als: tipos de preset
 ************************************************************/

int InstrumentPresets::KindOf(const std::string &name) {
	Path file=presetPath(name).Descend(PRESET_FILE) ;
	TiXmlDocument doc(file.GetPath()) ;
	if (!doc.LoadFile()) return PK_SAMPLE ;
	TiXmlElement *top=doc.FirstChildElement("LGPTPRESET") ;
	TiXmlElement *data=top?top->FirstChildElement("INSTRUMENT"):0 ;
	if (!data) return PK_SAMPLE ;
	bool kit=false, zones=false ;
	for (TiXmlElement *p=data->FirstChildElement("PARAM");p;p=p->NextSiblingElement("PARAM")) {
		const char *n=p->Attribute("NAME") ;
		const char *v=p->Attribute("VALUE") ;
		if (!n || !v) continue ;
		if (!strcmp(n,"s kit") && strcmp(v,"0") && *v) kit=true ;
		if (!strcmp(n,"s zones") && *v) zones=true ;
	}
	return kit?PK_KIT:(zones?PK_MULTI:PK_SAMPLE) ;
}

bool InstrumentPresets::FolderHas(const std::string &folder,int kind) {
	std::vector<std::string> folders, presets ;
	ListFolder(folder,folders,presets) ;
	for (size_t i=0;i<presets.size();i++)
		if (KindOf(folder.empty()?presets[i]:folder+"/"+presets[i])==kind) return true ;
	for (size_t i=0;i<folders.size();i++)
		if (FolderHas(folder.empty()?folders[i]:folder+"/"+folders[i],kind)) return true ;
	return false ;
}

const char *InstrumentPresets::KindFolder(int kind) {
	switch (kind) {
		case PK_KIT: return "Kits" ;
		case PK_MULTI: return "Multicapa" ;
		default: return "Samples" ;
	}
}

bool InstrumentPresets::Describe(const std::string &name, Info &out) {
	out = Info() ;
	Path folder = presetPath(name) ;
	out.dir = folder.GetPath() ;
	TiXmlDocument doc(Path(folder).Descend(PRESET_FILE).GetPath()) ;
	if (!doc.LoadFile()) return false ;
	TiXmlElement *top = doc.FirstChildElement("LGPTPRESET") ;
	TiXmlElement *data = top ? top->FirstChildElement("INSTRUMENT") : 0 ;
	if (!data) return false ;
	bool kit = false ;
	std::string zones ;
	for (TiXmlElement *p = data->FirstChildElement("PARAM"); p; p = p->NextSiblingElement("PARAM")) {
		const char *n = p->Attribute("NAME") ;
		const char *v = p->Attribute("VALUE") ;
		if (!n || !v) continue ;
		if (!strcmp(n, "sample")) out.sample = v ;
		if (!strcmp(n, "s kit") && strcmp(v, "0") && *v) kit = true ;
		if (!strcmp(n, "s zones")) zones = v ;
	}
	size_t pos = 0 ;
	while (pos < zones.size()) {
		size_t end = zones.find(';', pos) ;
		if (end == std::string::npos) end = zones.size() ;
		std::string item = zones.substr(pos, end - pos) ;
		pos = end + 1 ;
		int commas = 0 ;
		for (size_t c = 0; c < item.size(); c++) {
			if (item[c] == ',' && ++commas == 6) {
				out.zoneFiles.push_back(item.substr(c + 1)) ;
				break ;
			}
		}
	}
	out.zones = (int)out.zoneFiles.size() ;
	out.kind = kit ? PK_KIT : (out.zones ? PK_MULTI : PK_SAMPLE) ;
	return true ;
}

void InstrumentPresets::Capture(SampleInstrument *instr, Snapshot &out) {
	out.params.clear() ;
	int idx = instr->GetSampleIndex() ;
	out.sample = idx >= 0 ? SamplePool::GetInstance()->GetNameList()[idx] : "" ;
	IteratorPtr<Variable> it(instr->GetIterator()) ;
	for (it->Begin(); !it->IsDone(); it->Next()) {
		Variable &v = it->CurrentItem() ;
		if (!strcmp(v.GetName(), "sample") || skipParam(v.GetName())) continue ;
		out.params.push_back(std::make_pair(std::string(v.GetName()), std::string(v.GetString()))) ;
	}
}

void InstrumentPresets::Restore(SampleInstrument *instr, const Snapshot &s) {
	int idx = s.sample.empty() ? -1 : SamplePool::GetInstance()->IndexOf(s.sample.c_str()) ;
	if (idx >= 0) {
		instr->AssignSample(idx) ;
		instr->RefreshSample() ;
	} else {
		instr->FindVariable(SIP_SAMPLE)->SetInt(-1) ;
	}
	std::string zones ;
	for (size_t i = 0; i < s.params.size(); i++) {
		const std::string &n = s.params[i].first ;
		if (n == "s zones") { zones = s.params[i].second ; continue ; }
		IteratorPtr<Variable> it(instr->GetIterator()) ;
		for (it->Begin(); !it->IsDone(); it->Next()) {
			Variable &var = it->CurrentItem() ;
			if (n == var.GetName()) { var.SetString(s.params[i].second.c_str()) ; break ; }
		}
	}
	instr->SetZones(zones) ;
}

// trk2als: Prepare corre en un hilo de baja prioridad (de CPU y de disco),
// que el sistema atiende después del audio: en la consola, copiar a la
// tarjeta con prioridad normal dejaba al audio sin turno (huecos). La
// interfaz espera a que termine.
extern volatile bool g_importGentle ;
int g_prepareHeld = -1 ;
int g_prepareDepth = 0 ; unsigned long g_prepareOwner = 0, g_prepareSelf = 0 ;   // para el log: cuántas veces se soltó el candado
struct PrepareJob { std::string name ; bool ok ; } ;

static bool prepareNow(const std::string &name) {
	InstrumentPresets::Info info ;
	if (!InstrumentPresets::Describe(name, info)) return false ;
	SamplePool *pool = SamplePool::GetInstance() ;
	std::vector<std::string> files(info.zoneFiles) ;
	if (!info.sample.empty()) files.push_back(info.sample) ;
	for (size_t i = 0; i < files.size(); i++) {
		Path src(info.dir + "/" + files[i]) ;
		if (!src.Exists()) continue ;   // Load avisa lo que falte
		if (pool->Acquire(src) < 0) return false ;   // solo se lee (sin copiar)
	}
	return true ;
}

#if defined(__linux__)
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
static void *prepareThread(void *arg) {
	PrepareJob *job = (PrepareJob *)arg ;
	pid_t tid = (pid_t)syscall(SYS_gettid) ;
	setpriority(PRIO_PROCESS, tid, 19) ;                       // CPU: lo último
#ifdef SYS_ioprio_set
	syscall(SYS_ioprio_set, 1 /*IOPRIO_WHO_PROCESS*/, tid, 3 << 13 /*IDLE*/) ;   // disco: lo último
#endif
	g_importGentle = true ;
	if (getenv("TRK_SLOWPREP")) usleep(atoi(getenv("TRK_SLOWPREP")) * 1000) ;   // pruebas
	job->ok = prepareNow(job->name) ;
	g_importGentle = false ;
	return 0 ;
}
#endif

bool InstrumentPresets::Prepare(const std::string &name) {
#if defined(__linux__)
	PrepareJob job ;
	job.name = name ;
	job.ok = false ;
	pthread_t th ;
	if (pthread_create(&th, 0, prepareThread, &job) == 0) {
		MixerService *ms = MixerService::GetInstance() ;
		ms->Debug(g_prepareDepth, g_prepareOwner) ;
		g_prepareSelf = (unsigned long)SDL_ThreadID() ;
		int held = ms->ReleaseAll() ;   // el audio sigue mientras se lee
		pthread_join(th, 0) ;
		ms->Reacquire(held) ;
		g_prepareHeld = held ;
		return job.ok ;
	}
#endif
	return prepareNow(name) ;
}
