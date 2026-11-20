
#ifndef _SAMPLE_POOL_H_
#define _SAMPLE_POOL_H_

#include "Foundation/T_Singleton.h"
#include "WavFile.h"
#include "Application/Model/Song.h"
#include "Foundation/Observable.h"
#include <string>
#include <vector>

// trk2als: antes 128 (como los instrumentos); los kits traen 12-16 samples
// cada uno y con varios kits cargados el pool se llenaba
#define MAX_PIG_SAMPLES 255

enum SamplePoolEventType {
	SPET_INSERT,
	SPET_DELETE
} ;

struct SamplePoolEvent: public I_ObservableData {
	SamplePoolEventType type_ ;
	int index_ ;
} ;

enum SampleLoadResult {
    SLOAD_OK = 0,
    SLOAD_ERR_MAX_SAMPLES = 1,
    SLOAD_ERR_MAX_SOUNDFONTS = 2,
    SLOAD_ERR_INVALID_DIR = 4,
    SLOAD_ERR_INPUT_FILE = 5,
    SLOAD_ERR_OUTPUT_FILE = 6,
};

class SamplePool: public T_Singleton<SamplePool>,public Observable {
public:
  unsigned int Load();
  void Sort();
  SamplePool();
  void Reset();
  ~SamplePool();
  SoundSource *GetSource(int i);
  char **GetNameList();
  int GetNameListSize();
  int ImportSample(Path &path);
  // trk2als: un .wav ya escrito en la carpeta samples del proyecto (lo cargado
  // se reusa por nombre). Índice, o <0
  int LoadProjectSample(const char *name);
  int IndexOf(const char *name);
  bool IsImported(std::string name);
  // int InsertSample(const std::string& sampleName, bool imported, std::string fi);
  int Reassign(std::string name, bool imported);
  void PurgeSample(int i);
  const char *GetSampleLib();

  // trk2als: samples por referencia (como Live). Un .wav que se trae de la
  // librería o de un preset ya no se copia al proyecto: se lee de donde está
  // y el proyecto guarda su ruta (samples.ref). "Recopilar" los copia.
  // Acquire: el sample de esa ruta (si ya está, el mismo; si otro sample se
  // llama igual, entra con otro nombre: "Kick (2).wav"). Índice, o <0
  int Acquire(const Path &src);
  bool IsReference(int i) { return i >= 0 && i < count_ && !refs_[i].empty(); }
  std::string SourcePath(int i);   // el archivo de verdad (proyecto o referencia)
  void SaveRefs();                 // project:samples.ref
  int LoadRefs();                  // al abrir; cuántas faltaron
  int Collect();                   // copia al proyecto las referencias; cuántas
  int ReferenceCount();
  const std::vector<std::string> &Missing() { return missing_; }
protected:
  void unload(int i);
  int loadSample(const char *path);
  int loadSoundFont(const char *path);
  int getIndexOf(const char *path);
  int count_;
  char *names_[MAX_PIG_SAMPLES];
  SoundSource *wav_[MAX_PIG_SAMPLES];
  std::string refs_[MAX_PIG_SAMPLES];   // trk2als: "" = en el proyecto; si no, su ruta
  std::vector<std::string> missing_;
  int loadAs(const char *path, const std::string &name);
};

#endif
