#ifndef _SOUND_LAUNCHER_H_
#define _SOUND_LAUNCHER_H_

// trk2als: lanzar un sonido (preset) a un instrumento como si fuera un clip:
// sus samples se leen por detrás (la música y la interfaz siguen) y el cambio
// entra justo antes del 1 del compás siguiente (sin música: en cuanto está
// leído). Lo usan todos los navegadores que cargan presets: la cabecera de
// la pista, cargar preset del instrumento y cambiar de kit.

#include "Application/Views/ModalDialogs/PresetDialog.h"
#include "Application/Instruments/InstrumentPresets.h"
#include <map>
#include <vector>
#include <string>

class ViewData ;

// quien abrió el navegador: lo que pasa alrededor del cambio (deshacer,
// callar la pista, refrescar su pantalla)
class LaunchHost {
public:
	virtual ~LaunchHost() {}
	// antes de aplicar: firstOfVisit = lo primero que entra en esta visita
	// al navegador (ahí va el deshacer). Debe callar la pista si la hay.
	virtual void LaunchBefore(int instrument, int track, bool firstOfVisit) = 0 ;
	// después: devolver la pista, refrescar
	virtual void LaunchAfter(int instrument, int track, const std::string &name, bool ok) = 0 ;
	// se quitaron samples que nadie usaba: lo que se podía deshacer con
	// índices de samples ya no sirve
	virtual void LaunchPurged() {}
	// un aviso en su pantalla
	virtual void LaunchNotify(const char *msg) = 0 ;
} ;

class SoundLauncher : public PresetLauncher {
public:
	SoundLauncher(ViewData *vd, LaunchHost *owner) ;
	virtual ~SoundLauncher() ;
	static SoundLauncher *Current() ;   // el del proyecto abierto (o 0)
	// el nombre corto para la pista y los avisos ("KIT 1", "kick" de kick.wav)
	static std::string DisplayName(const std::string &name) ;

	// al abrir un navegador que lanza: a qué instrumento y quién lo abrió
	void Open(int instrument, LaunchHost *host) ;

	// PresetLauncher (lo que pregunta el navegador)
	virtual void Launch(const std::string &name) ;
	virtual int LaunchState(const std::string &name, int &permille) ;
	virtual bool LaunchPlaying() ;
	virtual std::string LaunchCurrent() ;
	virtual bool LaunchPreview(const std::string &name) ;
	virtual void LaunchPreviewStop() ;

	// lo llama la interfaz seguido (aunque se esté en otra pantalla);
	// true = hay que redibujar
	bool Tick() ;

	// lo que está en camino, para dibujar la barrita en la cabecera
	int Phase() const { return phase_ ; }    // 0 nada, 1 leyendo, 2 esperando el 1
	int PendingTrack() ;                     // -1 si no hay o no es de una pista
	int Permille() ;

	// se va a borrar esta pista (antes de quitarla)
	void TrackRemoved(int track) ;
	// lo que se cargó por otro camino (para la palomita)
	void SetPreset(int instrument, const std::string &name) ;

private:
	int trackOf(int instrument) ;
	std::string currentOf(int instrument) ;
	void apply() ;
	void cancel() ;
	void notify(LaunchHost *h, const char *msg) ;
	bool tickPreview() ;
	void previewOff() ;
	void previewRelease(bool purge) ;
	void spareOff(int track) ;
	bool spareQuiet() ;

	ViewData *vd_ ;
	LaunchHost *owner_ ;     // la Session: se entera de las limpiezas
	// la visita al navegador
	int target_ ;            // instrumento
	LaunchHost *host_ ;
	bool visitUndo_ ;
	// lo que está en camino
	InstrumentPresets::Preload *job_ ;
	std::string name_ ;
	int instr_ ;
	LaunchHost *jobHost_ ;
	bool jobFirst_ ;
	int phase_ ;
	int prevStep_ ;
	unsigned long step15At_ ;
	unsigned long lastDraw_ ;
	// el preset (con carpeta) que tiene cada instrumento
	std::map<int, std::string> preset_ ;
	std::map<int, std::string> shown_ ;   // cómo quedó llamándose su pista
	// la pre-escucha en el clip: el sonido se lee aparte y entra en un
	// instrumento de repuesto que toca el clip de la pista (el de la pista
	// no cambia). Los samples que se trajeron solo para eso se quitan al final
	InstrumentPresets::Preload *pjob_ ;
	std::string pname_ ;
	int ptrack_ ;            // la pista que lo oye (-1 nada)
	bool pkeep_ ;            // se lanzó: sigue hasta que entre
	bool prelease_ ;         // hay samples de la pre-escucha por quitar
	std::vector<std::string> padded_ ;
	unsigned long quietAt_ ;   // el repuesto se está apagando (cola): hasta cuándo
} ;

#endif
