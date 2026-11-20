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
	PresetLauncher *launcher_ ;
} ;
#endif
