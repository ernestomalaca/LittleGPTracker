#ifndef _PLAYER_H_
#define _PLAYER_H_
#include "Foundation/T_Singleton.h"
#include "Foundation/Observable.h"
#include "System/Timer/Timer.h"
#include "Application/Views/ViewData.h"
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "PlayerMixer.h"
#include "SyncMaster.h"
#include <vector>

enum PlayerEventType {
	PET_START,
	PET_UPDATE,
	PET_STOP
} ;

enum SequencerMode {
     SM_SONG,
     SM_LIVE
} ;

enum QueueingMode {
     QM_NONE,
     QM_CHAINSTART,
     QM_PHRASESTART,
     QM_CHAINSTOP,
	 QM_PHRASESTOP,
	 QM_TICKSTART
} ;

class PlayerEvent: public ViewEvent {
public:

	PlayerEvent(PlayerEventType type,unsigned int tickCount=0) ;
	PlayerEventType GetType() ;
	unsigned int GetTickCount() ;
private:
	PlayerEventType type_ ;
	unsigned int tickCount_ ;
} ;

class Player: public I_Observer,public Observable,public T_Singleton<Player> {
private: // Singleton
	Player() ;
public:


	static Player *GetInstance() ;
	bool Init(Project*,ViewData *) ;
	// trk2als: automatización. Dónde va el clip de una pista en 1/24 de paso
	// desde su inicio (-1 si no suena un clip) y si se está grabando
	int ClipTickPos(int channel) ;
	// trk2als: Ableton Link (lo llama la interfaz seguido)
	void LinkPoll() ;
	int LinkPeers() ;
	bool LinkOn() ;
	int AudioBars(unsigned char clip) { return audioBars(clip) ; }   // trk2als
	void SetAutoRec(bool on) { autoRec_ = on ; autoRecPending_ = false ; }
	bool IsAutoRec() { return autoRec_ ; }
	// armar sonando: empieza a grabar en el siguiente uno del compás
	void ArmAutoRec() { autoRecPending_ = true ; }
	// la siguiente vez que arranque: 4 tiempos de precuenta y graba
	void RequestCountIn(bool rec=true) { countInRequest_ = true ; countInRec_ = rec ; }
	// trk2als: la Session como en Live. Lanzar / detener entra en el siguiente
	// compás (sin nada sonando, arranca ya); Transport = la barra espaciadora:
	// arranca (vuelven los clips que estaban lanzados) o detiene todo
	void LaunchClip(int channel, int row) ;
	void StopTrack(int channel, int cell = -1) ;   // cell: la fila de la celda vacía que la detiene
	// la fila de la celda vacía que va a detener la pista (se ve en blanco), o -1
	int StopCell(int channel) { return stopCell_[channel] ; }
	void StopAllClips() ;   // como "Stop All Clips" de Live: al compás, el reloj sigue
	void LaunchScene(int row) ;
	// varios clips de una fila juntos (pistas from..to); las celdas vacías detienen su pista
	void LaunchGroup(int row, int from, int to) ;
	void Transport() ;
	// 0 apagado, 1 esperando (precuenta o el siguiente compás), 2 grabando
	int AutoRecState() { return autoRec_ ? 2 : ((autoRecPending_ || countIn_) ? 1 : 0) ; }

	void Reset() ;
	void Close() ;

    virtual void Update(Observable &o, I_ObservableData *d);

    // basic interface

    void Start(PlayMode mode, bool forceSongMode);
    void Stop() ;

    //	void Toggle(PlayMode mode,bool forceSongMode=false) ;
    //	void ChangePlayMode(PlayMode mode) ;
    //	PlayMode GetPlayMode() ;

    void SetSequencerMode(SequencerMode mode);
    SequencerMode GetSequencerMode() ;

    void OnStartButton(PlayMode origin, unsigned int from,
                       bool startFromLastPos, unsigned char chainPos);
    void OnSongStartButton(unsigned int from,unsigned int to,bool requestStop,bool forceImmediate) ;

    bool IsRunning();
    bool Clipped() ;

    bool ProcessChannelCommand(int channel,FourCC cmd,ushort param) ;
    // trk2als: el canal de audio de una pista (cambia al borrar pistas en vivo)
    int Phys(int track) ;
    // trk2als: borra la pista (también con la música andando)
    void RemoveTrack(int track) ;
    // trk2als: callar la pista y no tocar notas nuevas (on) mientras se
    // cambia su instrumento; off: vuelve a tocar
    void HoldTrack(int track, bool on) ;

	// trk2als: la pre-escucha en el clip: mientras in != 0, el clip MIDI que
	// suena en esa pista toca con ese instrumento (el de la pista no se toca).
	// Al cambiar se calla lo que sonaba en la pista
	void SetPreviewInstrument(int track, I_Instrument *in) ;
	I_Instrument *GetPreviewInstrument(int track) ;
	bool ClipPlaying(int track) ;   // ¿suena un clip MIDI en esa pista?
	// se movió a mano un parámetro automatizado: manda la mano hasta que el
	// clip vuelva a empezar su loop (no se borra nada)
	void OverrideAutomation(int track, unsigned int id) ;
	void ReleaseOverride(int track, unsigned int id) ;   // la curva vuelve a mandar ya
	// ya nadie lo mezcla ni lo va a tocar (antes de cambiarle o quitarle samples)
	void KillInstrument(I_Instrument *in) ;

	void StartStreaming(const Path &path) ;
	void PreviewNote(int channel, I_Instrument *instr, int note) ;   // trk2als: tocar una nota con la canción parada
	// trk2als: un teclado MIDI: hasta 4 voces por pista (acordes), con
	// velocity; suena parado o con la canción andando
	void LiveNoteOn(int channel, I_Instrument *instr, int note, int vel) ;
	void LiveNoteOff(int channel, int note) ;
	// trk2als: para capturar lo tocado (R2): dónde va la canción (en pasos,
	// con fracción) y, si la pista toca un clip MIDI, cuál y en qué paso
	bool CapturePos(int channel, double &songStep, int &clip, double &clipStep) ;
	void StartStreaming(const Path &path, double speed, double delaySecs, double startFrac, bool loop) ;
	void StopStreaming() ;
	int GetStreamPosition() ; // trk2als: cuadro del archivo que suena (-1 = nada)
	void SetStreamEnd(int frame) ; // trk2als: el preview para en ese cuadro

	// Channel data

    bool IsChannelPlaying(int channel);
    void SetChannelMute(int channel,bool mute) ;
    bool IsChannelMuted(int channel);

    // Live queuing

    QueueingMode GetQueueingMode(int i);
    unsigned char GetQueuePosition(int i) ;
	unsigned char GetQueueChainPosition(int i) ;
	void QueueChannel(int i,QueueingMode mode,unsigned char position,unsigned char chainpos=0) ;

	char *GetLiveIndicator(int channel) ;
	double GetPlayTime() ;

	char *GetPlayedNote(int channel) ;
	char *GetPlayedOctive(int channel) ;
	char *GetPlayedInstrument(int channel) ;

	// info
	int GetPlayedBufferPercentage() ;

	std::string GetAudioAPI() ;
	std::string GetAudioDevice() ;
	int GetAudioBufferSize() ;
	int GetAudioRequestedBufferSize() ;
	int GetAudioPreBufferCount() ;

protected:
	void updateSongPos(int position,int channel,int chainPos=0,int hop=-1) ;
	void updateChainPos(int position,int channel,int hop=0) ;
	void updatePhrasePos(int pos,int channel) ;
	void playCursorPosition(int channel) ;
	void moveToNextStep() ;
	void moveToNextPhrase(int channel,int hop=-1) ;
	void moveToNextChain(int channel,int hop) ;

    void triggerLiveChains() ;

    bool isPlayable(int row,int col,int chainPos=0) ;
    bool findPlayable(uchar *row, int col, uchar chainPos = 0);

    // trk2als: pistas de audio. En un canal de audio la celda de la song es un
    // instrumento (el loop) y se comporta como una chain virtual de 'loop bars'
    // compases, sin phrases ni notas.
    bool isAudio(int channel) ;
    int loopStart(int channel, int chain) ;   // trk2als
    int loopEnd(int channel, int chain) ;
    int audioBars(uchar clip) ;
    uchar chainStep(int channel, uchar chain, int pos) ;
    void playAudioCell(int channel) ;

  private:
    PlayerMixer *mixer_ ;
	ViewData *viewData_ ;
	Project *project_ ;

	SequencerMode sequencerMode_ ;
	PlayMode mode_ ;
	bool isRunning_ ;

	unsigned long startClock_ ;    // .Used to time display live queued chains
								   //  for blinking effect
	unsigned long now_ ;
	int lastPercentage_ ;
	int lastBeatCount_ ;
	unsigned char lastSongPos_ ;
	bool firstPlayCycle_ ;
	bool triggerLiveChains_ ;

	double startTime_ ;
    double currentTime_;

    char instrumentOnChannel_[SONG_CHANNEL_COUNT][3];

    // Live queuing system

    unsigned char liveQueuePosition_[SONG_CHANNEL_COUNT] ;
    QueueingMode liveQueueingMode_[SONG_CHANNEL_COUNT] ;
	unsigned char liveQueueChainPosition_[SONG_CHANNEL_COUNT] ;
	unsigned int timeToLive_[SONG_CHANNEL_COUNT] ;
	unsigned int timeToStart_[SONG_CHANNEL_COUNT] ;
	bool audioStarted_[SONG_CHANNEL_COUNT] ; // el loop de la celda actual ya arranco

	bool retrigAllImmediate_ ;
	unsigned char retrigPos_ ;
	int metroStep_ ; // trk2als: pasos desde que arrancó (metrónomo)
public:
	int BarStep() const { return metroStep_ % 16 ; }   // trk2als: el 1/16 del compás que suena (0 = el 1)
private:

	// trk2als: notas con nudge o subdivididas: golpes programados a destiempo
	struct TimedHit {
		unsigned long at ;     // en qué rebanada (sliceCount_) suena
		int delay ;            // y cuántas muestras dentro de ella
		int voice ;            // 0 = la voz principal del canal
		unsigned char note ;
		int vel ;              // -1 = sin velocity propia
		I_Instrument *instr ;
} ;
	std::vector<TimedHit> timed_[SONG_CHANNEL_COUNT] ;
	I_Instrument *previewInstr_[SONG_CHANNEL_COUNT] ;   // trk2als
	unsigned long sliceCount_ ;
	void fireTimed() ;
	// trk2als: el clip MIDI propio
	void playClipStep(int channel) ;
	void startClipNote(int channel, int voice, I_Instrument *instr, unsigned char note,
	                   int vel, bool newInstrument) ;
	unsigned long voiceEnd_[SONG_CHANNEL_COUNT][4] ;   // rebanada en que se libera cada voz
	int liveNote_[SONG_CHANNEL_COUNT][VOICE_COUNT] ;    // trk2als: la nota del teclado en cada voz (-1 libre)
	unsigned long liveAge_[SONG_CHANNEL_COUNT][VOICE_COUNT], liveClock_ ;
	// trk2als: automatización (las lanes del clip)
	bool autoRec_ ;   // grabando automatización (suena el metrónomo)
	bool autoRecPending_ ;   // empieza en el siguiente uno
	bool countInRequest_ ;   // el próximo arranque lleva precuenta
	bool countInRec_ ;       // trk2als: ...y al terminar graba automatización (si no, solo arranca)
	int stopCell_[SONG_CHANNEL_COUNT] ;   // trk2als: ver StopCell
	// trk2als: la escena que suena y sus repeticiones (song->sceneRep_)
	int sceneRow_, scenePending_, sceneStart_ ;
	// trk2als: lo mismo por pista, para el clip que lanzaste solo (la escena
	// manda: al lanzar una escena esto se apaga)
	int clipFolRow_[SONG_CHANNEL_COUNT], clipFolPend_[SONG_CHANNEL_COUNT], clipFolStart_[SONG_CHANNEL_COUNT] ;
	int cellBars(int ch, int row) ;
	bool clipFollowDue(int ch, int ahead) ;
	void sceneTick() ;
	void preFadeBar(SyncMaster *sync) ;   // trk2als
	int sceneBars(int row) ;
public:
	// trk2als: la escena que sigue sola (xN): cuál entra y cuántos compases faltan
	bool SceneFollow(int &next, int &barsLeft) ;
	// trk2als: vueltas y stop del clip de una celda (MIDI: en el clip; audio: en su instrumento)
	int CellFollow(int ch, int row) ;
	void SetCellFollow(int ch, int row, int f) ;
	// trk2als: editando un clip o un instrumento las escenas no avanzan
	void SetFollowHold(bool h) { followHold_ = h ; }
	bool followActive() ;
	bool followHold_ ;
	// trk2als: Start desde el clip o el instrumento: como en la Session (para y
	// se acuerda de lo lanzado; otra vez Start, sigue). Si no había nada
	// lanzado, arranca este clip
	void EditorStart(int channel, int row) ;
private:
	int launched_[SONG_CHANNEL_COUNT] ;   // trk2als: la fila que estaba sonando al parar (-1 nada)
	bool countIn_ ;          // en la precuenta (no suena nada más)
	int countStep_ ;
	bool countFirst_ ;
	void updateAutomation() ;
	void clearAutomation(int channel) ;
	class SampleInstrument *trackSampler(int channel) ;
	int autoStep_[SONG_CHANNEL_COUNT] ;                 // el paso que suena
	unsigned long autoStepSlice_[SONG_CHANNEL_COUNT] ;  // la rebanada en que empezó
	class SampleInstrument *autoInstr_[SONG_CHANNEL_COUNT] ;
	std::vector<unsigned int> autoApplied_[SONG_CHANNEL_COUNT] ;
	// trk2als: parámetros que se movieron a mano (sin grabar): la lane no se
	// aplica hasta que el clip da la vuelta (como en Live)
	std::vector<unsigned int> autoOverride_[SONG_CHANNEL_COUNT] ;
	int autoLastPos_[SONG_CHANNEL_COUNT] ;
	// trk2als: Ableton Link
	double linkLatency() ;
	void linkStart() ;
	bool linkSlice(class SyncMaster *sync) ;
	bool linkWait_ ;
	double linkOrigin_ ;
	double linkInteg_ ;
	double linkQueue_ ;   // la fila del driver, suavizada (cuadros)
	unsigned long linkSlices_ ;
	int linkLastTempo_ ;
	bool linkStartStop_, linkSeenPlaying_, linkRemoteStart_, linkRemoteStop_ ;
	// trk2als: reenganchar la fase (Link prendido con la música andando, o la
	// sesión saltó): se espera el tiempo de la sesión que cae donde vamos en el compás
	volatile bool linkResync_ ;
	unsigned long linkResyncAt_ ;   // (interfaz) cuándo pedirlo: dar tiempo a encontrar la sesión
	int linkPeersSeen_ ;
	int barSlice_ ;                 // rebanada dentro del compás (96 = 4 tiempos x 24)
	unsigned long sliceAtMs_ ;      // trk2als: cuándo se hizo la última rebanada (para CapturePos)
};

#endif
