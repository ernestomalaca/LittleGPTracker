#ifndef _PROJECT_H_
#define _PROJECT_H_

#include "Song.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Persistency/Persistent.h"
#include "Foundation/Variables/VariableContainer.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Observable.h"

#define VAR_TEMPO MAKE_FOURCC('T', 'M', 'P', 'O')
#define VAR_MASTERVOL   	MAKE_FOURCC('M', 'S', 'T', 'R')
#define VAR_WRAP        	MAKE_FOURCC('W', 'R', 'A', 'P')
#define VAR_MIDIDEVICE  	MAKE_FOURCC('M', 'I', 'D', 'I')
#define VAR_TRANSPOSE   	MAKE_FOURCC('T', 'R', 'S', 'P')
#define VAR_SOFTCLIP 		MAKE_FOURCC('S', 'F', 'T', 'C')
#define VAR_SOFTCLIP_GAIN 	MAKE_FOURCC('S', 'F', 'G', 'N')
#define VAR_PREGAIN   		MAKE_FOURCC('P', 'R', 'G', 'N')
#define VAR_SCALE 			MAKE_FOURCC('S', 'C', 'A', 'L')
#define VAR_SCALEROOT 		MAKE_FOURCC('S', 'C', 'R', 'T')   // trk2als: raíz de la escala
#define VAR_FOLD 			MAKE_FOURCC('F', 'O', 'L', 'D')   // trk2als: piano roll solo con la escala
#define VAR_QUANTIZE		MAKE_FOURCC('Q', 'N', 'T', 'Z')   // trk2als: cuánto acerca X a la rejilla (%)
#define VAR_SWING			MAKE_FOURCC('S', 'W', 'N', 'G')   // trk2als: swing de las semicorcheas (50 = parejo)
#define VAR_FOLLOW			MAKE_FOURCC('F', 'O', 'L', 'W')   // trk2als: el clip sigue a lo que suena
#define VAR_RENDER MAKE_FOURCC('R', 'N', 'D', 'R')
#define VAR_METRONOME MAKE_FOURCC('M', 'E', 'T', 'R') // trk2als: metrónomo off/on
#define VAR_LINKOFS MAKE_FOURCC('L', 'K', 'O', 'F') // trk2als: desfase de Link en ms
#define VAR_LINK MAKE_FOURCC('L', 'I', 'N', 'K') // trk2als: Ableton Link off/on/on+play
// trk2als: cuantos de los 8 canales (contando desde el ultimo) son pistas de audio
#define VAR_AUDIOTRACKS MAKE_FOURCC('A', 'U', 'D', 'T') // trk2als: solo para abrir proyectos viejos
// trk2als: tipo de cada pista (TYP0..TYP7)
#define VAR_TRACKTYPE(ch) MAKE_FOURCC('T', 'Y', 'P', '0' + (ch))
// TT_TRACKER solo existe para leer proyectos guardados con él: al abrirlos
// pasa a TT_MIDI (las pistas MIDI se escriben con chains y phrases)
enum TrackType { TT_TRACKER = 0, TT_AUDIO = 1, TT_MIDI = 2, TT_OFF = 3 }; // OFF: la pista no existe
#define VAR_TRACKNAME(ch) MAKE_FOURCC('T', 'N', 'M', '0' + (ch))
#define VAR_TRACKINSTR(ch) MAKE_FOURCC('T', 'I', 'N', '0' + (ch))
#define VAR_TRACKCOLOR(ch) MAKE_FOURCC('T', 'C', 'L', '0' + (ch))   // trk2als: el color viaja con la pista

#define PROJECT_NUMBER "1"
#define PROJECT_RELEASE "6"
#define BUILD_COUNT "0-bacon20"

#define MAX_TAP 3

class Project: public Persistent,public VariableContainer,I_Observer  {
public:
  Project();
  ~Project();
  void Purge();
  void PurgeInstruments(bool removeFromDisk);

  Song *song_;

  int GetMasterVolume();
  bool Wrap();
  void OnTempoTap();
  void NudgeTempo(int value);
  int GetScale();
  int GetScaleRoot();   // trk2als
  bool GetFold();
  // trk2als: la escala con la que se escribió el contenido; si cambia, la
  // Song / el piano roll ofrecen pasar las notas a la nueva (Fit to Scale)
  bool ScaleChanged();
  int OutOfScaleNotes();
  int FitToScale();
  void kitClips(bool *skip);   // trk2als: clips de pistas de batería (no se ajustan)
  void AcceptScale();
  int appliedScale_, appliedRoot_;
  int GetTempo(); // Takes nudging into account
  int GetTranspose();
  int GetSoftclip();
  int GetSoftclipGain();
  int GetPregain();
  int GetRenderMode();
  TrackType GetTrackType(int channel); // TRK: tracker (sale como audio), AUD: loops, MID: notas
  bool IsAudioChannel(int channel); // canal de audio: sus celdas son loops, no chains
  bool IsAudioInstrument(int instrument); // ¿algún loop de una pista de audio usa este instrumento?
  // trk2als: las pistas existen de la 0 a TrackCount()-1 (como en Live: se
  // agregan y se borran, no cambian de tipo)
  int TrackCount();
  // trk2als: cuántas pistas se pueden agregar (MAXTRACKS en config.xml; 6)
  static int MaxTracks();
  bool TrackExists(int channel);
  const char *GetTrackName(int channel);   // el del preset, o "MIDI 3" / "AUDIO 5"
  void SetTrackName(int channel, const char *name);
  // instrumento de la pista MIDI (-1: proyecto de antes, manda la columna
  // de instrumento de la phrase)
  int GetTrackInstrument(int channel);
  // trk2als: cuál de los 8 colores tiene la pista (va con ella al borrar otras)
  int GetTrackColor(int channel);
  int AddTrack(TrackType type);      // canal nuevo, o -1 si ya hay 8
  void DeleteTrack(int channel);     // con sus chains, phrases e instrumentos que nadie más usa
  void Trigger();

  static const unsigned int MAX_RENDER_MODE = 3;
  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);

  InstrumentBank *GetInstrumentBank();
  virtual void SaveContent(TiXmlNode *node);
  virtual void RestoreContent(TiXmlElement *element);


protected:
  void buildMidiDeviceList();

private:
  InstrumentBank *instrumentBank_;
  char **midiDeviceList_;
  int midiDeviceListSize_;
  int tempoNudge_;
  unsigned long lastTap_[MAX_TAP];
  unsigned int tempoTapCount_;
};
#endif
