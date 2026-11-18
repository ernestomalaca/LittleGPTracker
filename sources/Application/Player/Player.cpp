#include "Player.h"
#include "Application/Views/BaseClasses/ViewEvent.h"
#include "System/io/Status.h"
#include "System/System/System.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Utils/char.h"
#include "System/Console/n_assert.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Model/Groove.h"
#include <math.h>
#include <string.h>
#include "Services/Midi/MidiService.h"
#include "Application/Instruments/SampleInstrument.h"

// Private constructor - Singleton

Player::Player() {

    isRunning_ = false;
    metroStep_ = 0;
    sliceCount_ = 0;
    viewData_=0;
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {   // trk2als
        autoInstr_[i] = 0;
        autoStep_[i] = 0;
        autoStepSlice_[i] = 0;
    }
	mixer_=new PlayerMixer();

    lastSongPos_ = 0;
    mode_=PM_SONG;
	sequencerMode_=SM_SONG;   // trk2als: arranca en Song; el modo Live (escenas que dan vueltas, como el Move) con B + izq/der
	lastPercentage_=0;
	retrigAllImmediate_=false;
	startTime_=0;
	currentTime_=0;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		instrumentOnChannel_[i][0] = ' ';
		instrumentOnChannel_[i][1] = ' ';
		instrumentOnChannel_[i][2] = '\0';
    }
}

Player *Player::GetInstance() {
	if (instance_==0) {
        instance_ = new Player();
    }
    return instance_;
}

bool Player::Init(Project *project,ViewData *viewData) {

    viewData_ = viewData;
    project_ = project;

    if (!mixer_->Init(project)) {
        return false;
    }

    mixer_->AddObserver((*this));
    SyncMaster *sync = SyncMaster::GetInstance();
    sync->SetTempo(project_->GetTempo());
	return mixer_->Start();
}

void Player::Reset() {
    viewData_ = 0;
    project_ = 0;
    mixer_->RemoveObserver(*this);
	Close();
}

void Player::Close() {
    mixer_->Stop();
    mixer_->Close();
}

void Player::SetChannelMute(int channel,bool mute) {
    mixer_->SetChannelMute(channel, mute);
}

bool Player::IsChannelMuted(int channel) {
    return mixer_->IsChannelMuted(channel);
}

void Player::Start(PlayMode mode, bool forceSongMode) {

    mixer_->Lock();

    lastBeatCount_ = 0;

    // Get start time for clock

    System *system = System::GetInstance();
    now_ = startClock_ = system->GetClock();

    // Sets play mode.
    // DO I need playMode_ in view data ?
    // Seems like duplicate with mode_

    viewData_->playMode_ = (forceSongMode ? PM_SONG : mode);

    // Always set position, allows for playing song with chain offset
    unsigned playPos = viewData_->songY_ + viewData_->songOffset_;
    lastSongPos_ = playPos;

    // Clear all channel based data

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        mixer_->StopChannel(i);
        timeToLive_[i] = 0;
        timeToStart_[i]=0;
        timed_[i].clear();
        for (int v = 0; v < 4; v++) voiceEnd_[i][v] = 0;
        autoStep_[i] = 0;
        autoStepSlice_[i] = sliceCount_;
        audioStarted_[i] = false;
        SampleInstrument::SetChannelSync(i, isAudio(i));
		TablePlayback &tpb=TablePlayback::GetTablePlayback(i);
		tpb.Stop();
    }

    // Tell the instruments we're starting

    project_->GetInstrumentBank()->OnStart();

    Groove::GetInstance()->Reset();

    // Let's get started !

    SyncMaster::GetInstance()->Start();

    firstPlayCycle_ = true;
    mode_ = viewData_->playMode_;

    mixer_->OnPlayerStart();

    MidiService *ms = MidiService::GetInstance();
    ms->OnPlayerStart();

    switch (viewData_->playMode_) {
    case PM_SONG: {
        for (int i = 0; i < 8; i++) {
            mixer_->StartChannel(i);
            updateSongPos(playPos, i, viewData_->chainRow_);
        }
    } break;

    case PM_LIVE: {
        for (int i = 0; i < 8; i++) {
            if ((liveQueueingMode_[i] == QM_CHAINSTART) ||
                (liveQueueingMode_[i] == QM_PHRASESTART) ||
                (liveQueueingMode_[i] == QM_TICKSTART)) {
                mixer_->StartChannel(i);
                updateSongPos(liveQueuePosition_[i], i,
                              liveQueueChainPosition_[i]);
                liveQueueingMode_[i] = QM_NONE;
            }
        }
    } break;

    case PM_CHAIN:
    case PM_PHRASE: {
        int currentChannel = viewData_->songX_;
        mixer_->StartChannel(currentChannel);
        ;
        int currentChainPos = viewData_->chainRow_;
        if (isAudio(currentChannel)) currentChainPos = 0; // trk2als: el loop desde el principio
        updateSongPos(playPos, currentChannel, currentChainPos);
    } break;
    case PM_AUDITION: {
	    int currentChannel = viewData_->songX_;
	    mixer_->StartChannel(currentChannel);
	    int currentChainPos = viewData_->chainRow_;
	    int currentPhrasePos = viewData_->phraseCurPos_;
	    // uses hop for PhrasePos
	    updateSongPos(playPos, currentChannel, currentChainPos, currentPhrasePos);
	} break;
		default:
            NInvalid;
            break;
        }

        startTime_ = mixer_->GetAudioOut()->GetStreamTime();

        SetChanged();
        PlayerEvent pe(PET_START);
        NotifyObservers(&pe);

        isRunning_ = true; // keep last !!!!
        mixer_->Unlock();
}

void Player::Stop() {

    mixer_->Lock();

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        mixer_->ChokeChannel(i);   // trk2als: que no siga sonando el one-shot
        mixer_->StopChannel(i);
        timed_[i].clear();
        clearAutomation(i);   // trk2als: el instrumento vuelve a su valor
    }
    MidiService::GetInstance()->OnPlayerStop();
    mixer_->OnPlayerStop();

    SyncMaster::GetInstance()->Stop();
    isRunning_ = false;
    SetChanged();
	PlayerEvent pe(PET_STOP);
	NotifyObservers(&pe);

	mixer_->Unlock();
}

char *Player::GetPlayedNote(int channel) {
    return mixer_->GetPlayedNote(channel);
}

char *Player::GetPlayedOctive(int channel) {
    return mixer_->GetPlayedOctive(channel);
}

char *Player::GetPlayedInstrument(int channel) {
	if( (mixer_->GetPlayedOctive(channel))[1] == ' ' ){
		return mixer_->GetPlayedOctive(channel);
	} else {
		if (!IsChannelMuted(channel)) {
			return (char *)(&(instrumentOnChannel_[channel][0]));
		} else {
            return "--";
        }
    }
}

char *Player::GetLiveIndicator(int channel) {

    bool blink = true;

    switch (liveQueueingMode_[channel]) {
    case QM_CHAINSTART:
    case QM_CHAINSTOP:
        blink = (now_ - startClock_) % 500 < 250;
        break;
    case QM_PHRASESTART:
    case QM_PHRASESTOP:
        blink = (now_ - startClock_) % 125 < 72;
        break;
    case QM_TICKSTART:
        blink = (now_ - startClock_) % 75 < 37;
        break;
    case QM_NONE:
        break;
    }
    if (blink) {
		switch (liveQueueingMode_[channel]) {
        case QM_CHAINSTART:
        case QM_PHRASESTART:
        case QM_TICKSTART:
            if (!IsChannelMuted(channel)) {
                return (">");
            } else {
                return ("-");
            }
            break;
        case QM_CHAINSTOP:
        case QM_PHRASESTOP:
            return "_";
            break;
        case QM_NONE:
            break;
        }
    }
    return " ";
}

void Player::SetSequencerMode(SequencerMode mode) {
    if (isRunning_) {
        switch (mode) {
        case SM_LIVE:
            mode_ = PM_LIVE;
            break;
        case SM_SONG:
            mode_ = PM_SONG;
            break;
        }
    }
    sequencerMode_ = mode;
}

SequencerMode Player::GetSequencerMode() { return sequencerMode_; }

bool Player::IsChannelPlaying(int channel) {
    return mixer_->IsChannelPlaying(channel);
}

// Handles start button on any screen BUT the song screen

void Player::OnStartButton(PlayMode origin,unsigned int from,bool startFromPrevious,unsigned char chainPos) {

	switch(GetSequencerMode()) {

        case SM_SONG:

			// If sequencer not running, start otherwise stop

			if (isRunning_ && viewData_->playMode_ != PM_AUDITION) {
                Stop();
            } else {
				for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
                    liveQueueingMode_[i] = QM_NONE;
                }
				Start(origin,startFromPrevious);
            }
            break;
		case SM_LIVE: // doesn't make much sense here
            break;
        }
}

// Handles start on song screen
void Player::OnSongStartButton(unsigned int from,unsigned int to,bool requestStop,bool forceImmediate) {

    switch(GetSequencerMode()) {

        case SM_SONG:

			// If sequencer not running, start otherwise stop

			if (isRunning_ && viewData_->playMode_ != PM_AUDITION) {
				if (!forceImmediate) {
                    Stop();
                } else {
					// Get current song row and queue for immediate retrigger
                    retrigPos_ = viewData_->songY_ + viewData_->songOffset_;
                    retrigAllImmediate_=true;
				}
            } else {
                for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        			liveQueueingMode_[i]=QM_NONE;
                }
                Start(PM_SONG,false);
            }
            break;

		case SM_LIVE:

            // Get current song row
            unsigned char songPos=viewData_->songY_+viewData_->songOffset_;

            if (!IsRunning()) {

                // not playing; we queue the chains in the selection
                // that contain something then start the player

                for (unsigned int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                    if ((i<from)||(i>to)) {
						QueueChannel(i,QM_NONE,0);
                    } else {
                        if (isPlayable(songPos,i,0)) {
                            QueueChannel(i, QM_CHAINSTART, songPos, 0);
                        }
                    }
                }

                Start(PM_LIVE, false);

            } else { // Player already running

                // Queue all chain in the given selection

                for (unsigned int i = from; i < to + 1; i++) {

                    QueueingMode mode = QM_NONE;

                    uchar row = songPos;

                    if (!requestStop) {
                        if (findPlayable(&row, i, 0)) {
                            if (!forceImmediate) {
                                mode = QM_PHRASESTART;   // trk2als: al compás, como el Move
                            } else {
                                mode = QM_TICKSTART;
                            }
                        }
                    } else { // modifier = onStop from song screen
                        mode = QM_PHRASESTOP;   // trk2als: se detiene al compás
                    }
                    if (mode != QM_NONE) {
                        QueueChannel(i, mode, row, 0);
                    }
                }
            }
            break;
        }
}

bool Player::IsRunning() { return isRunning_; }

bool Player::Clipped() { return mixer_->Clipped(); }

/************************************************************
 trk2als: pistas de audio
 ************************************************************/

#define AUDIO_PHRASE 0xFE // phrase virtual de un canal de audio
#define CLIP_PHRASE 0xFD  // trk2als: compás de un clip MIDI propio (Model/Clip.h)

bool Player::isAudio(int channel) {
    return project_ && project_->IsAudioChannel(channel);
}

int Player::audioBars(uchar clip) {
    if (clip >= MAX_SAMPLEINSTRUMENT_COUNT) return 0;
    I_Instrument *instr = project_->GetInstrumentBank()->GetInstrument(clip);
    if (!instr || instr->GetType() != IT_SAMPLE) return 0;
    // la celda dura 'length' compases (el loop se repite adentro); si no
    // está puesto, lo mismo que el loop
    Variable *v = instr->FindVariable(SIP_CLIPBARS);
    int bars = v ? v->GetInt() : 0;
    if (bars <= 0) {
        v = instr->FindVariable(SIP_LOOPBARS);
        bars = v ? v->GetInt() : 1;
    }
    return (bars < 1) ? 1 : ((bars > 16) ? 16 : bars);
}

// Contenido de la chain en una posición; en canales de audio, una chain virtual
uchar Player::chainStep(int channel, uchar chain, int pos) {
    if (chain == 0xFF || pos < 0 || pos > 15) return 0xFF;
    if (isAudio(channel)) {
        return (pos < audioBars(chain)) ? AUDIO_PHRASE : 0xFF;
    }
    // trk2als: las pistas MIDI tocan sus clips propios; cada fila de la
    // "chain" es un compás del clip
    return (pos < viewData_->song_->clips_->Get(chain).pages) ? CLIP_PHRASE : 0xFF;
}

// Arranca el loop al entrar a la celda (o al empezar a tocar a media celda,
// con un offset para que quede en fase con el compás)
void Player::playAudioCell(int channel) {
    uchar clip = viewData_->currentPlayChain_[channel];
    if (clip == 0xFF || viewData_->currentPlayPhrase_[channel] == 0xFF) return;
    int cpos = viewData_->chainPlayPos_[channel];
    int pos = viewData_->phrasePlayPos_[channel];
    bool atStart = (cpos == 0 && pos == 0);
    if (audioStarted_[channel] && !atStart) return;

    int bars = audioBars(clip);
    if (bars == 0) return;
    I_Instrument *instrument =
        project_->GetInstrumentBank()->GetInstrument(clip);
    Variable *root = instrument->FindVariable(SIP_ROOTNOTE);
    int note = root ? root->GetInt() : 60;

    mixer_->StopInstrument(channel);
    SampleInstrument::SetChannelSync(channel, true);
    mixer_->StartInstrument(channel, instrument, note, true);
    if (!atStart) {
        // posición dentro del loop (la celda puede durar varias vueltas)
        Variable *lb = instrument->FindVariable(SIP_LOOPBARS);
        int loopBars = lb ? lb->GetInt() : bars;
        if (loopBars < 1) loopBars = 1;
        int step = (cpos * 16 + pos) % (loopBars * 16);
        int offset = (step * 256) / (loopBars * 16);
        if (offset > 0xFF) offset = 0xFF;
        instrument->ProcessCommand(channel, I_CMD_PLOF, (ushort)(offset << 8));
    }
    instrumentOnChannel_[channel][0] = '0' + (clip / 16 > 9 ? 7 : 0) + clip / 16;
    instrumentOnChannel_[channel][1] = '0' + (clip % 16 > 9 ? 7 : 0) + clip % 16;
    instrumentOnChannel_[channel][2] = '\0';
    audioStarted_[channel] = true;
}

bool Player::isPlayable(int row, int col, int chainPos) {

    uchar *chain = viewData_->song_->data_ + 8 * row + col;
    if (*chain != 0xFF) {
        uchar data = chainStep(col, *chain, chainPos);
    	return (data!=0xFF);
    }
    return false;
}

bool Player::findPlayable(uchar *row, int col, uchar chainPos) {

    // first look if current is fine

    uchar *chain = viewData_->song_->data_ + 8 * (*row) + col;
    if (*chain != 0xFF) {
        uchar data = chainStep(col, *chain, chainPos);
        return (data != 0xFF);
    }

    // Find upwards the first non blank

    while (*row != 255) {
        chain = viewData_->song_->data_ + 8 * (*row) + col;
        if (*chain!=0xFF) {
			break;
		}
        *row -= 1;
    }

    if (*row == 255)
        return false;

    // Find upwards the first blank

    while (*row != 255) {
        chain=viewData_->song_->data_+8*(*row)+col;
        if (*chain == 0xFF) {
            break;
        }
        *row -= 1;
    }

    if (*row == 255) {
        *row = 0;
    } else {
        *row += 1;
    }
    chain = viewData_->song_->data_ + 8 * (*row) + col;
    uchar data=0xFF;
    if (*chain != 0xFF) {
        data = chainStep(col, *chain, chainPos);
    }
    return (data!=0xFF);
}

QueueingMode Player::GetQueueingMode(int i) { return liveQueueingMode_[i]; }

unsigned char Player::GetQueuePosition(int i) { return liveQueuePosition_[i]; }

unsigned char Player::GetQueueChainPosition(int i) {
    return liveQueueChainPosition_[i];
}

void Player::QueueChannel(int i,QueueingMode mode,unsigned char position,unsigned char chainpos) {
    liveQueueingMode_[i] = mode;
    liveQueuePosition_[i] = position;
    liveQueueChainPosition_[i] = chainpos;
}

/************************************************************
 Update:
    this one gets called when the audio driver has just sent
    a block of audio and we get room to prepare the next one
 ************************************************************/

void Player::Update(Observable &o,I_ObservableData *d) {

    // Make sure sync's ok

    MidiService::GetInstance()->Trigger();
    project_->Trigger();

	if (isRunning_) {
        SyncMaster *sync = SyncMaster::GetInstance();
        sync->SetTempo(project_->GetTempo());

        if (!firstPlayCycle_) {
            Groove::GetInstance()->Trigger();
            sync->NextSlice();
            if (sync->MajorSlice()) metroStep_++;   // trk2als: otro paso (1/16)
            triggerLiveChains_ = false;
            if (retrigAllImmediate_) {
				for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
                    QueueChannel(i, QM_TICKSTART, retrigPos_, 0);
                }
                retrigAllImmediate_=false;
			}
			// Don't advance in audition mode
            if (viewData_->playMode_ != PM_AUDITION)
                moveToNextStep();
			if (triggerLiveChains_) {
                triggerLiveChains();
            }
        }

        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (timeToStart_[i] > 0) {
                if (--timeToStart_[i] == 0) {
                    playCursorPosition(i);
                }
            }
        }
        fireTimed();   // trk2als: los golpes con nudge o subdivisiones
        updateAutomation();   // trk2als: las lanes de los clips
        sliceCount_++;

        // trk2als: metrónomo, un click en cada tiempo (4 pasos)
        if (firstPlayCycle_) metroStep_ = 0;
        if ((firstPlayCycle_ || sync->MajorSlice()) && metroStep_ % 4 == 0 &&
            project_->FindVariable(VAR_METRONOME)->GetInt()) {
            mixer_->MetronomeClick(metroStep_ % 16 == 0);
        }

        // Initialise retrigger table
        int instrRetrigger[SONG_CHANNEL_COUNT];
        memset(instrRetrigger, -1, SONG_CHANNEL_COUNT * sizeof(int));

        // Process any table commands now

        for (int channel = 0; channel < SONG_CHANNEL_COUNT; channel++) {
            TablePlayback &tpb=TablePlayback::GetTablePlayback(channel);
			if (!tpb.GetAutomation()) {
                TablePlayerChange tpc;
                tpc.timeToLive_=timeToLive_[channel];
                tpc.instrRetrigger_ = -1;
                tpb.ProcessStep(tpc);
				timeToLive_[channel]=tpc.timeToLive_;
                instrRetrigger[channel] = tpc.instrRetrigger_;
            }
        }

        // Do we need to kill a voice ?

        if (sync->TableSlice()) {
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                bool stopped = false;
                if (timeToLive_[i] > 0) {
                    if (--timeToLive_[i] == 0) {
                        mixer_->StopInstrument(i);
                        stopped = true;
                    }
                }
                if (!stopped) {
                    if (instrRetrigger[i] >= 0) {
                        int note = mixer_->GetChannelNote(i);
                        I_Instrument *instr = mixer_->GetInstrument(i);
                        if ((note!=0xFF)&&(instr!=0)) {
                            note += (instrRetrigger[i] > 80)
                                        ? instrRetrigger[i] - 256
                                        : instrRetrigger[i];
                            while (note > 127) {
                                note-=12;
                            }
                            mixer_->StopInstrument(i);
                            mixer_->StartInstrument(i, instr, note, false);
                        }
                    }
                }
            }
        }

        firstPlayCycle_ = false;
        System *system = System::GetInstance();
        now_ = system->GetClock();
    }
    // Always notify refresh to enable VU meter dropoff after pause
    PlayerEvent pe(PET_UPDATE, 0);
    SetChanged();
    NotifyObservers(&pe);
}

bool Player::ProcessChannelCommand(int channel, FourCC cmd, ushort param) {

    I_Instrument *instr = mixer_->GetInstrument(channel);

    switch (cmd) {
    case I_CMD_KILL:
        if (instr) {
            int timeToLive = (param & 0xFF);
            timeToLive_[channel] = timeToLive + 1;
        }
        return true;
    case I_CMD_TMPO:
        if ((param < 400) && (param > 40)) {
            Variable *v = project_->FindVariable(VAR_TEMPO);
            v->SetInt(param);
            SyncMaster *sync = SyncMaster::GetInstance();
            sync->SetTempo(project_->GetTempo());
        }
        return true;
        break;
    case I_CMD_TABL: {
        TableHolder *th = TableHolder::GetInstance();
        TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
        param = param & 0x7F;
        Table &table = th->GetTable(param);
        tpb.Start(instr, table, false);
        return true;
        break;
    }
    case I_CMD_GROV: {
        Groove *gr = Groove::GetInstance();
        bool all = (param & 0xFF00) != 0;
        param = param & 0xFF;
        if (all) {
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                gr->SetGroove(i, param);
            }
        } else {
            gr->SetGroove(channel, param);
        }
    } break;
    case I_CMD_HOP: {
        bool channel_stop = (param & 0xFF) == 0xFF;
        if (channel_stop) {
            // Stop current channel
            mixer_->StopChannel(channel);

            // If all channels are stopped, stop the song if in song mode.
            if (GetSequencerMode() == SM_SONG) {
                bool all_channels_stopped = true;
                for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
                    all_channels_stopped &= !mixer_->IsChannelPlaying(c);
                }
                if (all_channels_stopped) {
                    Stop();
                }
            } else {
                liveQueueingMode_[channel] = QM_NONE;
            }
      }
    } break;
        case I_CMD_STOP: {
            switch (GetSequencerMode()) {
            case SM_SONG:
                Stop();
                break;
            case SM_LIVE:
                //            QueueChannel(channel,QM_CHAINSTOP,0);

                mixer_->StopChannel(channel);
                liveQueueingMode_[channel] = QM_NONE;
                break;
            }
        } break;
        default:
			break;
        }
        return false;
}

/********************************************************
 triggerLiveChains:
        look if there's any chain needed to be started
        i.e. they're queued but on a channel that is not
        yet active
 ********************************************************/

void Player::triggerLiveChains() {

    if (mode_ == PM_LIVE) {
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (!(mixer_->IsChannelPlaying(i)) &&
                ((liveQueueingMode_[i] == QM_CHAINSTART) ||
                 (liveQueueingMode_[i] == QM_TICKSTART) ||
                 (liveQueueingMode_[i] == QM_PHRASESTART))) {
                if (findPlayable(&(liveQueuePosition_[i]), i,
                                 liveQueueChainPosition_[i])) {
                    mixer_->StartChannel(i);
                    updateSongPos(liveQueuePosition_[i], i,
                                  liveQueueChainPosition_[i]);
                }
                liveQueueingMode_[i] = QM_NONE;
            }
        }
    }
}

/********************************************************
 updateSongPos:
    sets current song position for a give channel. the
    last parameter allow to start the song position at a
    give chain position directly (for PM_CHAIN/PM_PHRASE)
 ********************************************************/

// trk2als: el loop del clip (filas de la chain); sin loop, entero
int Player::loopStart(int channel, int chain) {
    int s, e;
    if (chain == 0xFF || isAudio(channel) || !viewData_->song_->GetClipLoop(chain, s, e)) return 0;
    return s;
}

int Player::loopEnd(int channel, int chain) {
    int s, e;
    if (chain == 0xFF || isAudio(channel) || !viewData_->song_->GetClipLoop(chain, s, e)) return 15;
    return e;
}

void Player::updateSongPos(int pos, int channel, int chainPos, int hop) {
    unsigned char *data = viewData_->song_->data_ + channel + 8 * pos;
    viewData_->songPlayPos_[channel]=pos;
	viewData_->currentPlayChain_[channel]=*data;
    if (chainPos == 0) chainPos = loopStart(channel, *data);   // entra al clip por su loop
    audioStarted_[channel] = false;
    updateChainPos(chainPos, channel, hop);
}

/********************************************************
 updateChainPos:
	sets current chain position for a give channel
 ********************************************************/

void Player::updateChainPos(int pos, int channel, int hop) {
    unsigned char chain=viewData_->currentPlayChain_[channel];
    if (chain != 0xFF) {
        viewData_->chainPlayPos_[channel]=pos;
		unsigned char step = chainStep(channel, chain, pos);
		unsigned char *data = &step;
		viewData_->currentPlayPhrase_[channel]=*data;
        if (*data == 0xFF) { // This could happen if starting in song mode on a
                             // row where a chain contains no phrase
            mixer_->StopChannel(channel);
        }
    } else {
        viewData_->currentPlayPhrase_[channel] = 0xFF;
        mixer_->StopChannel(channel);
    }
    updatePhrasePos((hop >= 0) ? hop : 0, channel);
}

/********************************************************
 updatePhrasePos:
    sets current phrase position for a give channel.
 ********************************************************/

void Player::updatePhrasePos(int pos, int channel) {

    viewData_->phrasePlayPos_[channel] = pos;

    // See if we need to delay the trigger
    timeToStart_[channel] = 1;

}

void Player::playCursorPosition(int channel) {

    if (isAudio(channel)) {
        playAudioCell(channel);
        return;
    }

    if (viewData_->currentPlayPhrase_[channel] == CLIP_PHRASE) playClipStep(channel);
}

/********************************************************
 moveToNextStep:
    Atomic routine that triggers the next step for all
    playing channels.
 ********************************************************/

void Player::moveToNextStep() {
    // we'll need to know if any channel is playing

    bool playingChannel = false;

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        bool liveTriggered = false;

        switch (liveQueueingMode_[i]) {
        case QM_TICKSTART:
            liveQueueingMode_[i] = QM_NONE;
            if (findPlayable(&(liveQueuePosition_[i]), i,
                             liveQueueChainPosition_[i])) {
                liveTriggered = true;
                updateSongPos(liveQueuePosition_[i], i,
                              liveQueueChainPosition_[i]);
            }
            return;
            break;
        case QM_PHRASESTART:
        case QM_PHRASESTOP:
        case QM_CHAINSTART:
        case QM_CHAINSTOP:
        case QM_NONE:
            break;
        }

        Groove *gs = Groove::GetInstance();

        if (mixer_->IsChannelPlaying(i) && !liveTriggered) {
            playingChannel = true;

            if (gs->TriggerChannel(i)) { // If groove says it is time to play
                if (viewData_->currentPlayPhrase_[i] != 0xFF) {
                    int pos = (viewData_->phrasePlayPos_[i]) + 1;
                    if (pos != 16) {
                        updatePhrasePos(pos, i);
                    } else {   // el siguiente compás del clip
                        if (mode_ != PM_PHRASE || isAudio(i)) {
                            moveToNextPhrase(i);
                        } else {
                            updatePhrasePos(0, i);
                        }
                    }
                }
            }
        }
    }
    // if no channel is playing we allow straight
    // queueing of chains
    if (!playingChannel) {
        triggerLiveChains_ = true;
    }
}

/********************************************************
 moveToNextPhrase:
    Compute what is the next phrase to be triggered on
    the selected channel. Called when the current phrase
    has reached the end
 ********************************************************/

void Player::moveToNextPhrase(int channel, int hop) {

    triggerLiveChains_ = true;   // trk2als: lo lanzado entra al compás (como el Move)

    // First check if we're in live mode and the channel
    // has been trigged in immediate mode. In which case we
    // do the action straight away (START/STOP)

    if (mode_==PM_LIVE) {

        switch (liveQueueingMode_[channel]) {
        case QM_TICKSTART:
        case QM_PHRASESTART:
            if (findPlayable(&(liveQueuePosition_[channel]), channel,
                             liveQueueChainPosition_[channel])) {
                updateSongPos(liveQueuePosition_[channel], channel,
                              liveQueueChainPosition_[channel], hop);
            }
            liveQueueingMode_[channel] = QM_NONE;
            return;
            break;
        case QM_PHRASESTOP:
            mixer_->StopChannel(channel);
            liveQueueingMode_[channel] = QM_NONE;
            return;
        case QM_CHAINSTART:
        case QM_CHAINSTOP:
        case QM_NONE:
            break;
        }
    }

    // If nothing has been triggered, we need to find what is
    // The next phrase to play

    int chain = viewData_->currentPlayChain_[channel];
    int pos = (viewData_->chainPlayPos_[channel]) + 1;

    // Look if there' any data at current position
    // which means we continue in the current chain

    bool canContinue = (pos < 16) && (pos <= loopEnd(channel, chain));
    if (canContinue) {
        canContinue = (chainStep(channel, chain, pos) != 0xFF);
    }

    // If so, we trigger it. Otherwise, we go to the next phrase

    if (canContinue) {
        updateChainPos(pos, channel, hop);
    } else { // Should move to next chain
        // trk2als: como en Live, el clip termina sus notas al acabar (antes la
        // última sonaba hasta la siguiente nota, aunque fuera en otro clip)
        if (!isAudio(channel)) {
            for (int v = 1; v < VOICE_COUNT; v++) mixer_->StopVoice(channel, v);
            mixer_->StopInstrument(channel);
        }
        if ((mode_==PM_SONG)||(mode_==PM_LIVE)) {
            moveToNextChain(channel, hop); // HOP. here
        } else {
            updateChainPos(loopStart(channel, chain), channel, hop);
        }
    }
}

/********************************************************
 moveToNextChain:
    Compute what is the next chain to be triggered on
    the selected channel. Called when the current chain
    has reached the end.
 ********************************************************/

void Player::moveToNextChain(int channel, int hop) {

    // if there's unplaying channels queue they should be started
    // once all position have been updated

    triggerLiveChains_ = true;

    bool searchNext = true;
    int nextPos=0;
    int chainPosition=0;

    // Hop here ?

    // See if current channel has been queued to play something
    // in normal mode

    if (mode_ == PM_LIVE) {
        switch (liveQueueingMode_[channel]) {

        case QM_CHAINSTART:
        case QM_PHRASESTART:

            if (findPlayable(&(liveQueuePosition_[channel]), channel,
                             liveQueueChainPosition_[channel])) {
                nextPos = liveQueuePosition_[channel];
                searchNext = false;
                liveQueueingMode_[channel] = QM_NONE;
                chainPosition = liveQueueChainPosition_[channel];
            } else {
                liveQueueingMode_[channel] = QM_NONE;
            }
            break;

        case QM_CHAINSTOP:
        case QM_PHRASESTOP:
            mixer_->StopChannel(channel);
            liveQueueingMode_[channel] = QM_NONE;
            return;

        case QM_NONE:
            break;
        }
    }

    // if live mode didn't queue anything, we find the next to play

    if (searchNext && mode_ == PM_LIVE) {
        // trk2als: como en el Move / la Session de Live, el clip se repite
        // hasta que lances otra cosa
        nextPos = viewData_->songPlayPos_[channel];
        searchNext = false;
    }
    if (searchNext) {
        int pos = (viewData_->songPlayPos_[channel]) + 1;
        unsigned char *data=viewData_->song_->data_+channel+8*pos;
    	bool loopBack=(*data==0xFF);
        // Check if first step of chain contains somethin, if not we loop back
        if (!loopBack) {
            unsigned char step = chainStep(channel, *data, 0);
            loopBack = (step == 0xFF);
        }
        if (loopBack) {
            data -= 8;
            pos--;
            while (pos >= 0) {
                if (*data == 0xFF) { // we stop searching if there's a blank
                    break;
                } else { // Or if first phrase of chain is empty
                    if (chainStep(channel, *data, 0) == 0xFF) {
                        break;
                    }
                }
                if (pos != 0)
                    data -= 8;
                pos--;
            }
                    pos++;
        }
        nextPos = pos;
    }
    // Do a last check in case we had only one chain and it go destroyed

    if (isPlayable(nextPos, channel, chainPosition)) {
        updateSongPos(nextPos, channel, chainPosition, hop);
    } else {
        mixer_->StopChannel(channel);
    }
}

double Player::GetPlayTime() {
    AudioOut *out = mixer_->GetAudioOut();
    double currentTime = out->GetStreamTime();
    if (isRunning_) {
        currentTime_ = currentTime - startTime_;
    }
    return currentTime_;
}

int Player::GetPlayedBufferPercentage() {
    unsigned int beatCount = SyncMaster::GetInstance()->GetBeatCount();
    if (beatCount != lastBeatCount_) {
        lastBeatCount_ = beatCount;
        lastPercentage_ = mixer_->GetPlayedBufferPercentage();
    }
    return lastPercentage_;
}

PlayerEvent::PlayerEvent(PlayerEventType type, unsigned int tickCount)
    : ViewEvent(VET_PLAYER_POSITION_UPDATE) {
    type_ = type;
    tickCount_ = tickCount;
}

PlayerEventType PlayerEvent::GetType() { return type_; }

unsigned int PlayerEvent::GetTickCount() { return tickCount_; }

void Player::StartStreaming(const Path &path) { mixer_->StartStreaming(path); }

void Player::StartStreaming(const Path &path, double speed, double delaySecs,
                            double startFrac, bool loop) {
    mixer_->StartStreaming(path, speed, delaySecs, startFrac, loop);
}
void Player::StopStreaming() { mixer_->StopStreaming(); }

void Player::PreviewNote(int channel, I_Instrument *instr, int note) {
    if (isRunning_ || !instr || channel < 0 || channel >= SONG_CHANNEL_COUNT) return;
    mixer_->Lock();
    mixer_->StartInstrument(channel, instr, (unsigned char)note, true);
    mixer_->Unlock();
}

int Player::GetStreamPosition() { return mixer_->GetStreamPosition(); }

void Player::SetStreamEnd(int frame) { mixer_->SetStreamEnd(frame); }

std::string Player::GetAudioAPI() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out)?out->GetAudioAPI():"";
}

std::string Player::GetAudioDevice() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out) ? out->GetAudioDevice() : "";
}

int Player::GetAudioBufferSize() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out) ? out->GetAudioBufferSize() : 0;
}

int Player::GetAudioRequestedBufferSize() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out)?out->GetAudioRequestedBufferSize():0;
}

int Player::GetAudioPreBufferCount() {
    AudioOut *out = mixer_->GetAudioOut();
    return (out) ? out->GetAudioPreBufferCount() : 0;
}

/********************************************************
 trk2als: nudge y subdivisiones
 ********************************************************/

// los golpes programados (nudge y subdivisiones) que ya tocan
void Player::fireTimed() {
    for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
        std::vector<TimedHit> &q = timed_[ch];
        for (size_t k = 0; k < q.size();) {
            if (q[k].at > sliceCount_) { k++; continue; }
            TimedHit h = q[k];
            q.erase(q.begin() + k);
            if (h.note == 0xFF) {   // el final recorrido de una nota con nudge
                if (h.voice == 0) mixer_->StopInstrument(ch);
                else mixer_->StopVoice(ch, h.voice);
                continue;
            }
            if (!h.instr) continue;
            if (h.voice == 0) {
                mixer_->StopInstrument(ch);
                SampleInstrument::SetChannelSync(ch, false);
                mixer_->StartInstrument(ch, h.instr, h.note, false);
                if (h.vel >= 0) h.instr->ProcessCommand(ch, I_CMD_MVEL, (ushort)(h.vel * 2));
            } else {
                mixer_->StartVoice(ch, h.instr, h.voice, h.note, h.vel);
            }
            if (h.delay > 0) h.instr->SetStartDelay(ch, h.voice, h.delay);
        }
    }
}

/********************************************************
 trk2als: el clip MIDI propio. En cada paso se programan las notas que
 empiezan ahí: el primer golpe después de su nudge (1/24 de paso) y, si está
 subdividida, los demás repartidos en lo que dura; al final, su corte. Cada
 nota toma una de las 4 voces del canal (la libre o la que acaba antes).
 ********************************************************/

void Player::playClipStep(int channel) {
    int chain = viewData_->currentPlayChain_[channel];
    if (chain == 0xFF) return;
    Clip &clip = viewData_->song_->clips_->Get(chain);
    int step = viewData_->chainPlayPos_[channel] * 16 + viewData_->phrasePlayPos_[channel];
    autoStep_[channel] = step;   // para la automatización (updateAutomation)
    autoStepSlice_[channel] = sliceCount_;
    if (step == 0 || step == loopStart(channel, chain) * 16) {
        for (int v = 0; v < VOICE_COUNT; v++) voiceEnd_[channel][v] = 0;
    }

    InstrumentBank *bank = project_->GetInstrumentBank();
    int owner = project_->GetTrackInstrument(channel);
    I_Instrument *instrument = (owner >= 0) ? bank->GetInstrument(owner)
                                            : mixer_->GetLastInstrument(channel);
    if (!instrument) instrument = bank->GetInstrument(0);
    if (!instrument) return;
    if (owner >= 0) {
        instrumentOnChannel_[channel][0] = (owner / 16) > 9 ? 'A' - 10 + owner / 16 : '0' + owner / 16;
        instrumentOnChannel_[channel][1] = (owner % 16) > 9 ? 'A' - 10 + owner % 16 : '0' + owner % 16;
        instrumentOnChannel_[channel][2] = '\0';
    }

    float sps = SyncMaster::GetInstance()->GetPlaySampleCount();
    std::vector<TimedHit> &q = timed_[channel];
    bool any = false;
    for (size_t k = 0; k < clip.notes.size(); k++) {
        const ClipNote &n = clip.notes[k];
        if (n.start != step) continue;
        int note = n.pitch + project_->GetTranspose();
        if (note < 0 || note > 127) continue;
        double start = n.tick / 4.0;      // en sextos de paso (rebanadas)
        double span = n.len * 6.0;
        unsigned long startSlice = sliceCount_ + (unsigned long)start;
        // la voz: la que ya terminó o, si no hay, la que acaba antes
        int v = -1;
        for (int i = 0; i < VOICE_COUNT && v < 0; i++)
            if (voiceEnd_[channel][i] <= startSlice) v = i;
        if (v < 0) {
            v = 0;
            for (int i = 1; i < VOICE_COUNT; i++)
                if (voiceEnd_[channel][i] < voiceEnd_[channel][v]) v = i;
        }
        // lo pendiente de esa voz desde aquí ya no va
        for (size_t j = 0; j < q.size();) {
            if (q[j].voice == v && q[j].at >= startSlice) q.erase(q.begin() + j);
            else j++;
        }
        bool newInstrument = (v == 0) && (mixer_->GetLastInstrument(channel) != instrument);
        for (int h = 0; h < n.rat; h++) {
            double at = start + span * h / n.rat;
            int vel = n.vel;
            if (n.rat > 1 && n.ramp) {   // rampa: los golpes crecen o se apagan
                float amt = 0.2f * (n.ramp > 0 ? n.ramp : -n.ramp);
                float f = n.ramp > 0 ? 1 - amt * (n.rat - 1 - h) / (n.rat - 1)
                                     : 1 - amt * h / (n.rat - 1);
                vel = int(n.vel * f + 0.5f);
                if (vel < 1) vel = 1;
            }
            if (at == 0) {   // en la rejilla: ya
                startClipNote(channel, v, instrument, (unsigned char)note, vel, newInstrument);
                continue;
            }
            TimedHit t;
            int whole = int(at);
            t.at = sliceCount_ + whole;
            t.delay = int((at - whole) * sps);
            t.voice = v;
            t.note = (unsigned char)note;
            t.vel = vel;
            t.instr = instrument;
            q.push_back(t);
        }
        // el corte, recorrido con el nudge
        TimedHit off;
        off.at = sliceCount_ + (unsigned long)(start + span + 0.5);
        off.delay = 0;
        off.voice = v;
        off.note = 0xFF;
        off.vel = -1;
        off.instr = 0;
        q.push_back(off);
        voiceEnd_[channel][v] = off.at;
        any = true;
    }
    if (any && instrument->GetTableAutomation()) {
        TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
        TablePlayerChange tpc;
        tpc.timeToLive_ = timeToLive_[channel];
        tpb.ProcessStep(tpc);
        timeToLive_[channel] = tpc.timeToLive_;
    }
}

void Player::startClipNote(int channel, int voice, I_Instrument *instrument,
                           unsigned char note, int vel, bool newInstrument) {
    SampleInstrument::SetChannelSync(channel, false);
    if (voice != 0) {
        mixer_->StartVoice(channel, instrument, voice, note, vel);
        return;
    }
    mixer_->StopInstrument(channel);
    mixer_->StartInstrument(channel, instrument, note, newInstrument);
    instrument->ProcessCommand(channel, I_CMD_MVEL, (ushort)(vel * 2));
    TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
    int instrTable = instrument->GetTable();
    if (instrTable != VAR_OFF && newInstrument) {
        Table &table = TableHolder::GetInstance()->GetTable(instrTable);
        tpb.Start(instrument, table, instrument->GetTableAutomation());
    } else if (newInstrument) {
        tpb.Stop();
    }
}

/********************************************************
 trk2als: automatización. En cada rebanada (1/6 de paso) cada lane del clip
 que suena le dice al instrumento de la pista cuánto vale su parámetro. El
 instrumento no cambia su valor guardado: al parar o al pasar a un clip sin
 esa lane, vuelve a sonar como estaba ("no ha pasado nada").
 ********************************************************/

SampleInstrument *Player::trackSampler(int channel) {
    int owner = project_->GetTrackInstrument(channel);
    if (owner < 0) return 0;
    I_Instrument *in = project_->GetInstrumentBank()->GetInstrument(owner);
    if (!in || in->GetType() != IT_SAMPLE) return 0;
    return (SampleInstrument *)in;
}

void Player::clearAutomation(int channel) {
    if (autoInstr_[channel]) {
        for (size_t k = 0; k < autoApplied_[channel].size(); k++)
            autoInstr_[channel]->ClearAutomated(autoApplied_[channel][k]);
    }
    autoApplied_[channel].clear();
    autoInstr_[channel] = 0;
}

void Player::updateAutomation() {
    for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
        int chain = viewData_->currentPlayChain_[ch];
        bool playing = mixer_->IsChannelPlaying(ch) && !isAudio(ch) && chain != 0xFF &&
                       viewData_->currentPlayPhrase_[ch] == CLIP_PHRASE;
        SampleInstrument *si = playing ? trackSampler(ch) : 0;
        if (si != autoInstr_[ch]) clearAutomation(ch);
        if (!si) continue;
        autoInstr_[ch] = si;
        Clip &clip = viewData_->song_->clips_->Get(chain);
        int sub = int(sliceCount_ - autoStepSlice_[ch]);
        if (sub < 0) sub = 0;
        if (sub > 5) sub = 5;
        int pos = autoStep_[ch] * CLIP_TICKS + sub * (CLIP_TICKS / 6);
        std::vector<unsigned int> now;
        for (size_t l = 0; l < clip.lanes.size(); l++) {
            const AutoLane &lane = clip.lanes[l];
            if (lane.pad >= 0) continue;   // los pads del kit: más adelante
            int v;
            if (!lane.ValueAt(pos, v)) continue;
            si->SetAutomated(lane.target, v);
            now.push_back(lane.target);
        }
        // lo que se aplicaba y este clip ya no automatiza vuelve a su valor
        for (size_t k = 0; k < autoApplied_[ch].size(); k++) {
            bool still = false;
            for (size_t j = 0; j < now.size() && !still; j++) still = (now[j] == autoApplied_[ch][k]);
            if (!still) si->ClearAutomated(autoApplied_[ch][k]);
        }
        autoApplied_[ch] = now;
    }
}
