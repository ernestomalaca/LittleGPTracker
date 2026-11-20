#ifndef _PRESET_DIALOG_H_
#define _PRESET_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>
#include <vector>

// trk2als: el navegador de sonidos (presets por categorías: carpetas dentro
// de presets/). A pantalla completa, en dos columnas: a la izquierda las
// carpetas, a la derecha lo que hay adentro. Al detenerse en un preset suena
// su muestra (preview.wav en su carpeta; si no hay, el sample, o un
// patroncito con los pads del kit).
// Para cargar: GetSelected() da el nombre con su carpeta ("Kits/Cumbia/KIT 1").
// Para guardar (forSave): "[guardar aquí]" elige la carpeta; GetFolder().
// GetReturnCode(): 1 = listo; 0 = cancelar; con SetEmptyOptions(): también
// PD_EMPTY_KIT, PD_EMPTY_SAMPLE, PD_IMPORT_SF2 (la primera fila de cada categoría)
#define PD_EMPTY_KIT 2
#define PD_EMPTY_SAMPLE 3
#define PD_IMPORT_SF2 4
#define PD_CLOSED 5      // trk2als: se cerró después de lanzar (no hay nada más que hacer)

// trk2als: lanzar presets como clips: A lo carga en la pista por detrás y
// entra en el 1 siguiente; el navegador sigue abierto (salvo "cerrar al cargar")
class PresetLauncher {
public:
	enum State { LS_NONE = 0, LS_LOADING, LS_WAITING, LS_SOUNDING } ;
	virtual ~PresetLauncher() {}
	virtual void Launch(const std::string &name) = 0 ;
	// cómo va ese preset; permille: lo leído (LOADING) o lo que va del compás (WAITING)
	virtual int LaunchState(const std::string &name, int &permille) = 0 ;
	virtual bool LaunchPlaying() = 0 ;   // ¿la música está andando? (entonces no hay pre-escucha)
	// el sonido que tiene la pista ("Kits/Cumbia/KIT 1", o solo "KIT 1" si no se sabe de dónde)
	virtual std::string LaunchCurrent() = 0 ;
	// trk2als: la pre-escucha en el clip: si en la pista suena un clip MIDI,
	// ese clip toca con este sonido (true) sin cambiar el de la pista
	virtual bool LaunchPreview(const std::string &name) { return false ; }
	virtual void LaunchPreviewStop() {}
} ;

class PresetDialog : public ModalView {
public:
	// kind: -1 = todos; si no, solo presets de ese tipo
	// (InstrumentPresets::PK_KIT...) y las carpetas que tienen alguno
	PresetDialog(View &view, bool forSave = false, int kind = -1) ;
	virtual ~PresetDialog() ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) ;
	virtual void OnFocus() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
	virtual bool UsesShapes() { return SupportsShapes() ; }
	virtual bool Tick() ;
	std::string GetSelected() ;
	std::string GetFolder() { return folder_ ; }
	// trk2als: "Kit vacío" / "Importar .sf2" / "Sample vacío" en cada categoría
	void SetEmptyOptions(bool on) { emptyOptions_ = on ; load() ; }
	void SetLauncher(PresetLauncher *l) { launcher_ = l ; }
	// trk2als: elegir un archivo de la samplelib para el instrumento toInstr
	// (el sample de un pad, el del instrumento, un loop o un .sf2). A lo
	// importa y cierra con 1. current: el índice del pool que tiene ahora
	enum { PICK_NONE = 0, PICK_WAV, PICK_LOOP, PICK_SF2, PICK_ANY } ;
	void SetPick(int mode, int toInstr, int currentPoolIndex = -1) ;
private:
	enum Kind { K_UP, K_HERE, K_FOLDER, K_PRESET, K_EMPTY, K_NEWDIR } ;
public:
	void OnNewFolder(const std::string &name) ;   // "Nueva subcategoría"
private:
	struct Entry {
		Kind kind ;
		std::string name ;
		int ptype ;     // tipo de preset (kit, multicapa, sample)
		int zones ;
	} ;
	bool isCurrent(const Entry &e) ;
	bool holdsCurrent(const std::string &folder) ;
	void checkBadge(int x, int y, GUIColor fg, GUIColor bg) ;
	struct Node {       // la columna de carpetas
		std::string path ;
		std::string label ;
		int depth ;
	} ;
	void load() ;
	void buildTree() ;
	void goBack() ;
	int countSounds(const std::string &f) ;
	bool folderOk(const std::string &f) ;
	std::string full(const std::string &n) ;
	void drawGrid() ;
	void drawShapes() ;
	void schedulePreview() ;
	void startPreview() ;
	void stopPreview() ;
	bool forSave_ ;
	int kind_ ;
	std::string folder_ ;
	std::vector<Entry> entries_ ;
	std::vector<Node> tree_ ;
	std::string chosen_ ;
	int selected_ ;
	int top_ ;
	bool emptyOptions_ ;
	int col_ ;        // 0 carpetas, 1 lista
	int treeSel_ ;
	int treeTop_ ;
	unsigned long previewAt_ ;   // cuándo arranca la muestra (0 = nada)
	std::string playing_ ;       // lo que está sonando
	bool inClip_ ;
	unsigned long bAt_ ;         // cuándo se apretó B (0 = suelto): dejarlo apretado cierra
	bool bHint_ ;
	std::string bFolder_ ;       // dónde estaba al apretar B (ahí se vuelve a abrir)               // y suena en el clip de la pista (no la muestra)
	PresetLauncher *launcher_ ;
	int pick_ ;
	int pickInstr_ ;
	std::string pickCurrent_ ;   // el archivo que tiene (nombre del navegador)
	int pickWhat() ;
} ;
#endif
