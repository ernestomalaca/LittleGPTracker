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
#define VAR_RENDER MAKE_FOURCC('R', 'N', 'D', 'R')
// trk2als: cuantos de los 8 canales (contando desde el ultimo) son pistas de audio
#define VAR_AUDIOTRACKS MAKE_FOURCC('A', 'U', 'D', 'T') // trk2als: solo para abrir proyectos viejos
// trk2als: tipo de cada pista (TYP0..TYP7)
#define VAR_TRACKTYPE(ch) MAKE_FOURCC('T', 'Y', 'P', '0' + (ch))
// TT_TRACKER solo existe para leer proyectos guardados con él: al abrirlos
// pasa a TT_MIDI (las pistas MIDI se escriben con chains y phrases)
enum TrackType { TT_TRACKER = 0, TT_AUDIO = 1, TT_MIDI = 2 };

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
  int GetTempo(); // Takes nudging into account
  int GetTranspose();
  int GetSoftclip();
  int GetSoftclipGain();
  int GetPregain();
  int GetRenderMode();
  TrackType GetTrackType(int channel); // TRK: tracker (sale como audio), AUD: loops, MID: notas
  bool IsAudioChannel(int channel); // canal de audio: sus celdas son loops, no chains
  bool IsAudioInstrument(int instrument); // ¿algún loop de una pista de audio usa este instrumento?
  void Trigger();

  static const unsigned int MAX_RENDER_MODE = 3;
  // I_Observer
  virtual void Update(Observable &o, I_ObservableData *d);

  InstrumentBank *GetInstrumentBank();
  virtual void SaveContent(TiXmlNode *node);
  virtual void RestoreContent(TiXmlElement *element);

  void LoadFirstGen(const char *root);

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
