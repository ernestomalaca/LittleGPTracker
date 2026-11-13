#include "InstrumentPresets.h"
#include "SampleInstrument.h"
#include "SamplePool.h"
#include "Externals/TinyXML/tinyxml.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Console/Trace.h"
#include <algorithm>
#include <string.h>

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
	return resolved(Path(PRESET_DIR).Descend(name)) ;
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
	Path folder = presetPath(name) ;
	if (!folder.Exists() && fs->MakeDir(folder.GetPath().c_str()).Failed()) {
		Trace::Error("No se pudo crear %s", folder.GetPath().c_str()) ;
		return false ;
	}

	// la copia del sample va junto al preset
	int index = instr->GetSampleIndex() ;
	if (index >= 0) {
		std::string sample = SamplePool::GetInstance()->GetNameList()[index] ;
		Path src(std::string("samples:") + sample) ;
		Path dst = Path(folder).Descend(sample) ;
		if (!copyFile(src.GetPath(), dst.GetPath())) {
			Trace::Error("No se pudo copiar %s", src.GetPath().c_str()) ;
			return false ;
		}
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
	std::string sample ;
	for (TiXmlElement *p = data->FirstChildElement("PARAM"); p; p = p->NextSiblingElement("PARAM")) {
		const char *n = p->Attribute("NAME") ;
		const char *v = p->Attribute("VALUE") ;
		if (n && v && !strcmp(n, "sample")) sample = v ;
	}
	SamplePool *pool = SamplePool::GetInstance() ;
	int index = -1 ;
	if (!sample.empty() && sample != "(null)") {
		for (int i = 0; i < pool->GetNameListSize(); i++) {
			if (sample == pool->GetNameList()[i]) {
				index = i ;
				break ;
			}
		}
		if (index < 0) {
			Path src = Path(folder).Descend(sample) ;
			if (!src.Exists()) {
				Trace::Error("Falta el sample %s del preset", sample.c_str()) ;
				return false ;
			}
			index = pool->ImportSample(src) ;
			if (index < 0) return false ;
		}
	}

	// el sample primero: cambiarlo reinicia start/end/loop del instrumento
	if (index >= 0) {
		instr->AssignSample(index) ;
		instr->RefreshSample() ;
	}

	// luego todos los parámetros (los que este instrumento conozca)
	for (TiXmlElement *p = data->FirstChildElement("PARAM"); p; p = p->NextSiblingElement("PARAM")) {
		const char *n = p->Attribute("NAME") ;
		const char *v = p->Attribute("VALUE") ;
		if (!n || !v || !strcmp(n, "sample") || skipParam(n)) continue ;
		IteratorPtr<Variable> it(instr->GetIterator()) ;
		for (it->Begin(); !it->IsDone(); it->Next()) {
			Variable &var = it->CurrentItem() ;
			if (!strcmp(var.GetName(), n)) {
				var.SetString(v) ;
				break ;
			}
		}
	}
	return true ;
}
