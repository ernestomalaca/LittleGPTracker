#include "SongView.h"
#include "Application/Utils/ProjectName.h"
#include <cstring>
#define SONG_COL_W 4   // trk2als: ancho de cada pista en la grilla
#include "Application/Commands/ApplicationCommandDispatcher.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Mixer/AudioWatch.h"
#include "Application/Model/ProjectDatas.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Model/Song.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "Application/Utils/Latin1.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/TrackTypeDialog.h"
#include "Application/Views/ModalDialogs/PresetDialog.h"
#include "Application/Views/ModalDialogs/ImportSampleDialog.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/InstrumentPresets.h"
#include "UIController.h"
#include "VuMeterUtil.h"
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>
#include "Services/Audio/AudioOutDriver.h"
#include "Application/Model/Config.h"
#include <stdlib.h>
#include <stdio.h>

// trk2als: carga del audio (pico del último segundo) y su estimado en la
// consola de referencia: CONSOLEFACTOR en config.xml (cuántas veces más lenta
// es, 9 para la RG353V contra una Mac con Apple Silicon)
static void loadText(char *out) {
    static int factor = -1;
    if (factor < 0) {
        const char *v = Config::GetInstance()->GetValue("CONSOLEFACTOR");
        factor = v ? atoi(v) : 9;
        if (factor < 1) factor = 1;
    }
    int now, peak;
    GetAudioLoad(now, peak);
    int est = peak * factor;
    if (est > 999) est = 999;
    (void)peak;   // solo cabe el estimado de la consola (5 caracteres)
    sprintf(out, "rg%3d", est > 999 ? 999 : est);
}

/****************
 Constructor
 ****************/

SongView::SongView(GUIWindow &w, ViewData *viewData, const char *song)
    : View(w, viewData) {
    followSeen_ = -1;
    aOpenPending_ = false;
    bTap_ = false;

    updatingChain_ = false;
    lastChain_ = 0;
    lastClip_ = 0xFF; // trk2als: sin loop todavía
    headerMode_ = false;
    rowMode_ = false;
    pendingTypeCh_ = -1;
    presetCh_ = -1;
    newTrack_ = false;
    launcher_ = new SoundLauncher(viewData_, this);
    pickClip_ = pickCell_ = -1;
    newSlotCell_ = -1;
    pendingType_ = 0;
    songname_ = song;
    View::projectName_ = ProjectNiceName(songname_);

    for (int i = 0; i < 8; i++) {
        this->lastPlayedPosition_[i] = 0;
        this->lastQueuedPosition_[i] = 0;
    }
    for (int i = 0; i < 8; i++) blockVu_[i] = 0;   // trk2als
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) soloed_[i] = false;
    inBlockRedraw_ = false;
    lastVuDraw_ = 0;
    sesNowValid_ = false;
    sesMarking_ = sesPasting_ = sesDupDone_ = false;
    scnMarking_ = scnBPending_ = false;
    scnMarkRow_ = 0;
    scnRows_ = 0;
    sesBPending_ = sesAPending_ = sesAMoved_ = false;
    NOTIFICATION_TIMEOUT = 1800;   // el aviso de abajo se lee con calma
    vuBarHeightsL_[0] = 0;
    vuBarHeightsR_[0] = 0;
    clipboard_.active_ = false;
    clipboard_.data_ = 0;
    invertBatt_ = false;
    canDeepClone_ = false;
    jumpLength_ = 0x10; // B-jump 16 rows like LSDJ
}

/****************
 Destructor
 ****************/

SongView::~SongView() {
    delete launcher_;
    if (clipboard_.data_ != 0)
        SYS_FREE((void *)clipboard_.data_);
};

/******************************************************
 updateChain:
        update current chain value by adding offset
        parameter
 ******************************************************/

/******************************************************
 trk2als: pistas de audio
 ******************************************************/

bool SongView::isAudioColumn(int x) {
    return viewData_->project_->IsAudioChannel(x);
}

// la grilla va pegada a la izquierda: 8 pistas de 4 caracteres ("A04 ")
GUIPoint SongView::gridAnchor() {
    GUIPoint a = GetAnchor();
    a._x = 3;
    return a;
}

// lo que se ve arriba: el clip o chain bajo el cursor, o el nombre de la pista
std::string SongView::cellName() {
    Project *project = viewData_->project_;
    unsigned char d = *viewData_->GetCurrentSongPointer();
    if (headerMode_ || d == 0xFF) return project->GetTrackName(viewData_->songX_);
    char buf[48];
    if (isAudioColumn(viewData_->songX_)) {
        I_Instrument *instr = project->GetInstrumentBank()->GetInstrument(d);
        std::string n = instr ? instr->GetName() : "";
        size_t dot = n.rfind('.');
        if (dot != std::string::npos) n = n.substr(0, dot);
        snprintf(buf, sizeof(buf), "%s %s", clipLabel(d).c_str(), n.c_str());
        return buf;
    }
    const std::string &cn = viewData_->song_->GetClipName(d);
    if (!cn.empty()) return cn;
    snprintf(buf, sizeof(buf), "chain %02X", d);
    return buf;
}

// trk2als: un chain nuevo en una pista MIDI se llama como la primera palabra
// del preset de la pista más un número: "woodblock 1", "woodblock 2"...
void SongView::nameNewChain(unsigned char chain, int col) {
    if (col < 0) col = viewData_->songX_;
    if (chain == 0xFF || isAudioColumn(col)) return;
    std::string base = viewData_->project_->GetTrackName(col);
    size_t sp = base.find(' ');
    if (sp != std::string::npos) base = base.substr(0, sp);
    if (base.size() > 12) base = base.substr(0, 12);
    int next = 1;
    Song *song = viewData_->song_;
    for (int c = 0; c < CLIP_COUNT; c++) {
        const std::string &n = song->GetClipName(c);
        if (n.size() > base.size() + 1 && n.compare(0, base.size() + 1, base + " ") == 0) {
            int k = atoi(n.c_str() + base.size() + 1);
            if (k >= next) next = k + 1;
        }
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d", base.c_str(), next);
    song->SetClipName(chain, buf);
}

void SongView::prepareClip(unsigned char clip) {
    if (clip >= MAX_SAMPLEINSTRUMENT_COUNT)
        return;
    I_Instrument *instr =
        viewData_->project_->GetInstrumentBank()->GetInstrument(clip);
    // trk2als: ya no se toca el loop mode (el instrumento puede estar en una
    // pista MIDI); el canal de audio lo toca en looper sync por su cuenta
    (void)instr;
}

unsigned char SongView::freeClip() {
    bool used[MAX_SAMPLEINSTRUMENT_COUNT] = {false};
    Song *song = viewData_->song_;
    for (int row = 0; row < 256; row++) {
        for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
            unsigned char c = song->data_[row * SONG_CHANNEL_COUNT + ch];
            if (isAudioColumn(ch) && c < MAX_SAMPLEINSTRUMENT_COUNT) used[c] = true;
        }
    }
    for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {   // los de las pistas MIDI
        int o = viewData_->project_->GetTrackInstrument(ch);
        if (o >= 0 && o < MAX_SAMPLEINSTRUMENT_COUNT) used[o] = true;
    }
    for (int i = AUDIO_CLIP_BASE; i < MAX_SAMPLEINSTRUMENT_COUNT; i++) {
        if (!used[i] && !isLoadedClip(i)) return (unsigned char)i;
    }
    return 0xFF;
}

// trk2als: cómo se ve un loop: A00 es el instrumento AUDIO_CLIP_BASE; los de
// proyectos de antes (más abajo) en minúscula
std::string SongView::clipLabel(unsigned char id) {
    char buf[8];
    if (id >= AUDIO_CLIP_BASE) snprintf(buf, sizeof(buf), "A%02X", id - AUDIO_CLIP_BASE);
    else snprintf(buf, sizeof(buf), "a%02X", id);
    return buf;
}

// trk2als: ¿el instrumento es un loop cargado desde la Song (aunque ya no
// esté en ninguna celda)?
bool SongView::isLoadedClip(int i) {
    I_Instrument *instr = viewData_->project_->GetInstrumentBank()->GetInstrument(i);
    if (!instr || instr->GetType() != IT_SAMPLE) return false;
    Variable *v = ((SampleInstrument *)instr)->FindVariable(SIP_CLIP);
    return v && v->GetInt() == 1;
}

// los loops que ya hay: los marcados y los que están en alguna pista de audio
// (proyectos de antes), en orden
std::vector<unsigned char> SongView::audioClips() {
    std::vector<unsigned char> out;
    bool inCell[MAX_SAMPLEINSTRUMENT_COUNT] = {false};
    unsigned char *data = viewData_->song_->data_;
    for (int i = 0; i < 256 * SONG_CHANNEL_COUNT; i++) {
        unsigned char c = data[i];
        if (c < MAX_SAMPLEINSTRUMENT_COUNT && isAudioColumn(i % SONG_CHANNEL_COUNT)) inCell[c] = true;
    }
    for (int i = 0; i < MAX_SAMPLEINSTRUMENT_COUNT; i++) {
        if (inCell[i] || isLoadedClip(i)) out.push_back((unsigned char)i);
    }
    return out;
}

// A sobre una celda vacía de audio: el último loop (o el primero), para
// cambiarlo con A+flechas; si todavía no hay ninguno, a cargar uno
void SongView::placeAudio() {
    std::vector<unsigned char> clips = audioClips();
    if (clips.empty()) {
        pickAudio();
        return;
    }
    unsigned char clip = clips[0];
    for (size_t i = 0; i < clips.size(); i++) {
        if (clips[i] == lastClip_) clip = lastClip_;
    }
    *viewData_->GetCurrentSongPointer() = clip;
    lastClip_ = clip;
    isDirty_ = true;
}

// A+flechas en una celda de audio: recorre los loops cargados; después del
// último viene '+' (un loop nuevo: se carga al soltar A)
void SongView::stepAudio(int dir) {
    unsigned char *c = viewData_->GetCurrentSongPointer();
    int cell = int(c - viewData_->song_->data_);
    std::vector<unsigned char> clips = audioClips();
    int n = int(clips.size());
    int idx;   // 0..n-1 un loop, n = '+'
    if (newSlotCell_ == cell) {
        idx = n;
    } else {
        idx = -1;
        for (int i = 0; i < n; i++) {
            if (clips[i] == *c) idx = i;
        }
        if (idx < 0) idx = n;   // no estaba en la lista: como '+'
    }
    idx += dir;
    if (idx < 0) idx = 0;
    if (idx > n) idx = n;
    if (idx == n) {
        if (freeClip() == 0xFF) {
            View::SetNotification("no quedan instrumentos libres");
            idx = n - 1;
            if (idx < 0) return;
        } else {
            *c = 0xFF;                // hasta que se cargue no suena nada
            newSlotCell_ = cell;
            isDirty_ = true;
            return;
        }
    }
    newSlotCell_ = -1;
    *c = clips[idx];
    lastClip_ = *c;
    isDirty_ = true;
}

// trk2als: A en una celda vacía de una pista de audio: elegir el archivo; el
// loop (un instrumento libre, limpio) queda en esa celda
static void AudioPickCallback(View &v, ModalView &dialog) {
    ((SongView &)v).OnAudioPicked(dialog.GetReturnCode() == 1);
}

void SongView::pickAudio(int cell) {
    unsigned char clip = freeClip();
    if (clip == 0xFF) {
        View::SetNotification("no quedan instrumentos libres");
        return;
    }
    Path sampleLib(SamplePool::GetInstance()->GetSampleLib());
    if (FileSystem::GetInstance()->GetFileType(sampleLib.GetPath().c_str()) != FT_DIR) {
        View::SetNotification("no encuentro la samplelib");
        return;
    }
    viewData_->project_->GetInstrumentBank()->GetInstrument(clip)->Purge();
    pickClip_ = clip;
    pickCell_ = (cell >= 0) ? cell : int(viewData_->GetCurrentSongPointer() - viewData_->song_->data_);
    viewData_->currentInstrument_ = clip;
    PresetDialog *pd = new PresetDialog(*this);   // trk2als: el navegador nuevo (loops a tempo)
    pd->SetPick(PresetDialog::PICK_LOOP, clip);
    DoModal(pd, AudioPickCallback);
}

void SongView::OnAudioPicked(bool imported) {
    if (!imported || pickClip_ < 0 || pickCell_ < 0) return;
    I_Instrument *instr = viewData_->project_->GetInstrumentBank()->GetInstrument(pickClip_);
    if (instr->GetType() == IT_SAMPLE) {
        SampleInstrument *si = (SampleInstrument *)instr;
        si->FindVariable(SIP_LOOPBPM)->SetInt(si->DetectLoopBpm());   // y con él los compases
        si->FindVariable(SIP_CLIP)->SetInt(1);                         // un loop cargado
        si->InitAudioClip();   // trk2als: ganancia y warp de un loop nuevo (LOOPWARP)
    }
    viewData_->song_->data_[pickCell_] = (unsigned char)pickClip_;
    lastClip_ = (unsigned char)pickClip_;
    pickClip_ = pickCell_ = -1;
    isDirty_ = true;
}

void SongView::updateChain(int offset) {

    if (isAudioColumn(viewData_->songX_)) {   // trk2als: por los loops cargados
        stepAudio(offset > 0 ? 1 : -1);
        return;
    }

    unsigned int chain = viewData_->UpdateSongChain(offset);
    updatingChain_ = true;
    lastChain_ = chain;
    updateX_ = viewData_->songX_;
    updateY_ = viewData_->songY_;
    isDirty_ = true;
    canDeepClone_ = false;
}

/******************************************************
 updateChain:
        set current chain value to value parameter
 ******************************************************/

void SongView::setChain(unsigned char value) {
    viewData_->SetSongChain(value);
    lastChain_ = value;
    isDirty_ = true;
}

/******************************************************
 updateSongOffset:
        Jump from the current position up or down
    by [offset] rows
 ******************************************************/

// trk2als: no hay que bajar a la fila mil: se llega hasta una abajo de la
// última fila con algo (o la última que se ve); al poner algo ahí, una más
void SongView::clampRows() {
    const int rows = View::songRowCount_;
    Song *song = viewData_->song_;
    Project *project = viewData_->project_;
    int last = -1;
    for (int r = SONG_ROW_COUNT - 1; r >= 0 && last < 0; r--)
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++)
            if (project->TrackExists(i) && song->data_[i + SONG_CHANNEL_COUNT * r] != 0xFF) { last = r; break; }
    int maxRow = last + 1;
    if (maxRow < rows - 1) maxRow = rows - 1;
    if (maxRow > SONG_ROW_COUNT - 1) maxRow = SONG_ROW_COUNT - 1;
    int abs = viewData_->songY_ + viewData_->songOffset_;
    if (abs > maxRow) abs = maxRow;
    int maxOff = maxRow - rows + 1;
    if (maxOff < 0) maxOff = 0;
    if (viewData_->songOffset_ > maxOff) viewData_->songOffset_ = maxOff;
    if (abs < viewData_->songOffset_) viewData_->songOffset_ = abs;
    if (abs > viewData_->songOffset_ + rows - 1) viewData_->songOffset_ = abs - rows + 1;
    viewData_->songY_ = abs - viewData_->songOffset_;
}

void SongView::updateSongOffset(int offset) {
    viewData_->UpdateSongOffset(offset);
    clampRows();
    isDirty_ = true;
    canDeepClone_ = false;
}

/******************************************************
 updateCursor:
        modify location of cursor in view by
        adding dx & dy parameters
 ******************************************************/

void SongView::updateCursor(int dx, int dy) {
    viewData_->UpdateSongCursor(dx, dy);
    clampTrackCursor();   // trk2als
    clampRows();
    isDirty_ = true;
    canDeepClone_ = false;
}

/******************************************************
 cutPosition:
        copy current position content to clipboard &
        erase current position value
 ******************************************************/

void SongView::cutPosition() {

    // prepare selection data
    clipboard_.x_ = viewData_->songX_;
    clipboard_.y_ = viewData_->songY_;
    clipboard_.offset_ = viewData_->songOffset_;

    saveX_ = viewData_->songX_;
    saveY_ = viewData_->songY_;
    saveOffset_ = viewData_->songOffset_;

    // cut selection
    cutSelection();
};

/******************************************************
 pastePosition:
        set current position to last chain value if
        current step is empty
 ******************************************************/

// trk2als: abre lo que hay en la celda: la primera phrase de la chain (si la
// chain está vacía, la chain) o, en una pista de audio, su clip
void SongView::openCell() {
    unsigned char *data = viewData_->GetCurrentSongPointer();
    if (*data == 0xFF) return;
    ViewType vt;
    if (isAudioColumn(viewData_->songX_)) {
        vt = VT_INSTRUMENT;
        viewData_->currentInstrument_ = *data;
        viewData_->instrumentFromSong_ = true;
    } else {
        viewData_->currentChain_ = *data;
        viewData_->chainRow_ = 0;
        vt = VT_CLIP;   // trk2als: el clip MIDI se abre en el piano roll
    }
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
}

// trk2als: celda vacía + A = un clip nuevo (Model/Clip.h), de un compás
// trk2als: Select + → en una celda MIDI vacía: un clip vacío que todavía no
// está en la Session; ahí se toca (con la silueta) y al capturar o editarlo
// se vuelve un clip de verdad en esa celda. Si te sales sin nada, no queda
void SongView::openVirtualClip() {
    Song *song = viewData_->song_;
    unsigned short id = song->clips_->GetNext(song->data_, SONG_CHANNEL_COUNT * SONG_ROW_COUNT);
    if (id == NO_MORE_CLIP) {
        View::SetNotification("No quedan clips libres");
        return;
    }
    song->clips_->Get(id).Clear();
    song->clips_->Get(id).pages = 1;
    song->SetClipName(id, "");
    viewData_->currentChain_ = id;
    viewData_->chainRow_ = 0;
    viewData_->virtualCell_ = viewData_->songX_ + SONG_CHANNEL_COUNT * (viewData_->songY_ + viewData_->songOffset_);
    ViewType vt = VT_CLIP;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
}

unsigned short SongView::newClipId() {
    Song *song = viewData_->song_;
    unsigned short id = song->clips_->GetNext(song->data_, SONG_CHANNEL_COUNT * SONG_ROW_COUNT);
    if (id == NO_MORE_CLIP) {
        View::SetNotification("No quedan clips libres");
        return id;
    }
    song->clips_->Get(id).Clear();
    song->ClearClipLoop(id);
    song->SetClipName(id, "");
    return id;
}

void SongView::createClip() {
    unsigned short id = newClipId();
    if (id == NO_MORE_CLIP) return;
    viewData_->song_->clips_->Get(id).pages = 1;
    setChain((unsigned char)id);
    nameNewChain((unsigned char)id);
    isDirty_ = true;
}

void SongView::pasteLast() {

    // If we're on an empty spot, we past the last chain
    // otherwise we take the current chain as last

    unsigned char *c = viewData_->GetCurrentSongPointer();
    if (isAudioColumn(viewData_->songX_)) { // trk2als: el último loop
        if (*c == 0xFF) {
            unsigned char clip = (lastClip_ == 0xFF) ? freeClip() : lastClip_;
            if (clip == 0xFF) return;
            *c = clip;
            lastClip_ = clip;
            isDirty_ = true;
        } else {
            lastClip_ = *c;
        }
        return;
    }
    if (*c == 0xFF) {
        *c = lastChain_;
        isDirty_ = true;
    } else {
        lastChain_ = *c;
    }
};

/******************************************************
 clonePosition:
        slim clone current position
 ******************************************************/

void SongView::clonePosition() {

    // trk2als: como Duplicar en la Session de Live: la copia aparece en la
    // primera celda libre debajo y el cursor se va a ella. En una pista de
    // audio se repite el mismo loop.
    unsigned char *pos = viewData_->GetCurrentSongPointer();
    unsigned char current = *pos;
    if (current == 255)
        return;
    int col = viewData_->songX_;
    int row = viewData_->songY_ + viewData_->songOffset_;
    int target = -1;
    for (int r = row + 1; r < SONG_ROW_COUNT && target < 0; r++)
        if (viewData_->song_->data_[col + SONG_CHANNEL_COUNT * r] == 0xFF) target = r;
    if (target < 0) {
        View::SetNotification("no hay lugar abajo");
        return;
    }
    unsigned char value = current;
    if (!isAudioColumn(col)) {
        unsigned short next = newClipId();
        if (next == NO_MORE_CLIP)
            return;
        // el clon es un clip independiente con sus notas, su largo y su loop
        viewData_->song_->clips_->Copy(current, next);
        int ls, le;
        if (viewData_->song_->GetClipLoop(current, ls, le))
            viewData_->song_->SetClipLoop(next, ls, le);
        value = (unsigned char)next;
    }
    viewData_->song_->data_[col + SONG_CHANNEL_COUNT * target] = value;
    updateCursor(0, target - row);
    if (!isAudioColumn(col)) nameNewChain(value);
    View::SetNotification("clip duplicado abajo");
    isDirty_ = true;
};

/******************************************************
 deepClonePosition:
        deep clone chain and all phrases within
        made by koisignal (https://github.com/koi-ikeno)
 ******************************************************/

void SongView::deepClonePosition() {
    // trk2als: con los clips propios clonar ya es independiente
}

void SongView::extendSelection() {
    GUIRect rect = getSelectionRect();
    if (rect.Left() > 0 || rect.Right() < 7) {
        if (viewData_->songX_ < clipboard_.x_) {
            viewData_->songX_ = 0;
            clipboard_.x_ = 7;
        } else {
            viewData_->songX_ = 7;
            clipboard_.x_ = 0;
        }
        isDirty_ = true;
    } else {
        if (viewData_->songY_ < clipboard_.y_) {
            viewData_->songY_ = 0;
            clipboard_.y_ = 0x17;
        } else {
            clipboard_.y_ = 0;
            viewData_->songY_ = 0x17;
        }
        isDirty_ = true;
    }
}

/******************************************************
 OnFocus:
        called when current view is becoming active
 ******************************************************/

void SongView::OnFocus() {
    clipboard_.active_ = false;
    Player::GetInstance()->SetSequencerMode(SM_LIVE);   // trk2als: siempre como la Session de Live
    // trk2als: lo que cambió en otras pantallas (notas, nombres) entra a la foto
    sesNowValid_ = false;
    checkScaleChange();   // trk2als
};

GUIRect SongView::getSelectionRect() {

    GUIRect selRect(clipboard_.x_, clipboard_.y_ + clipboard_.offset_,
                    viewData_->songX_,
                    viewData_->songY_ + viewData_->songOffset_);

    selRect.Normalize();
    return selRect;
}

/******************************************************
 fillClipboard:
        fill clipboard with current selection value
 ******************************************************/

void SongView::fillClipboardData() {

    // Clear current selection data

    if (!clipboard_.data_)
        SYS_FREE((void *)clipboard_.data_);

    // Prepare selection related information

    GUIRect selRect = getSelectionRect();

    // Set current selection  data

    clipboard_.width_ = selRect.Width() + 1;
    clipboard_.height_ = selRect.Height() + 1;
    clipboard_.srcLeft_ = selRect.Left();

    clipboard_.data_ =
        (unsigned char *)SYS_MALLOC(clipboard_.width_ * clipboard_.height_);

    unsigned char *src = viewData_->song_->data_ + selRect.Left() +
                         SONG_CHANNEL_COUNT * selRect.Top();
    unsigned char *dst = clipboard_.data_;

    for (int j = 0; j < clipboard_.height_; j++) {
        for (int i = 0; i < clipboard_.width_; i++) {
            *dst++ = *src++;
        }
        src += (SONG_CHANNEL_COUNT - clipboard_.width_);
    }
};

/******************************************************
 copySelection:
        copy current selection to clipboard
 ******************************************************/

void SongView::copySelection() {

    fillClipboardData();
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    viewData_->songX_ = saveX_;
    viewData_->songY_ = saveY_;
    viewData_->songOffset_ = saveOffset_;
    View::SetNotification("copied selection");
}

/******************************************************
 cutSelection:
        cut current selection to clipboard
 ******************************************************/

void SongView::cutSelection() {

    // first copy the data to clipboard

    fillClipboardData();
    GUIRect selRect = getSelectionRect();

    // trk2als: como en la Session de Live, cortar vacía las celdas y lo de
    // abajo se queda en su lugar (LGPT subía todas las filas)
    for (int y = selRect.Top(); y <= selRect.Bottom(); y++)
        for (int x = selRect.Left(); x <= selRect.Right(); x++)
            viewData_->song_->data_[x + SONG_CHANNEL_COUNT * y] = 0xFF;

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    viewData_->songX_ = saveX_;
    viewData_->songY_ = saveY_;
    viewData_->songOffset_ = saveOffset_;

    isDirty_ = true;
}

/******************************************************
 pasteSelection:
        paste clipboard content to song
 ******************************************************/

void SongView::pasteClipboard() {

    if (!clipboard_.data_)
        return;

    // trk2als: en una pista de audio las celdas son loops y en una MIDI son
    // chains (el mismo número es otra cosa): no se mezclan
    for (int i = 0; i < clipboard_.width_; i++) {
        int from = clipboard_.srcLeft_ + i, to = viewData_->songX_ + i;
        if (to >= SONG_CHANNEL_COUNT) break;
        bool fromAudio = isAudioColumn(from), toAudio = isAudioColumn(to);
        if (fromAudio == toAudio) continue;
        bool any = false;
        for (int j = 0; j < clipboard_.height_; j++) {
            if (clipboard_.data_[j * clipboard_.width_ + i] != 0xFF) any = true;
        }
        if (!any) continue;
        View::SetNotification(fromAudio ? "clips de audio no van en pistas MIDI"
                                        : "clips MIDI no van en pistas de audio");
        return;
    }

    // Check we're not out of scope

    int width = clipboard_.width_;
    int height = clipboard_.height_;

    if (viewData_->songX_ + width > SONG_CHANNEL_COUNT) {
        width = SONG_CHANNEL_COUNT - viewData_->songX_;
    }
    if (viewData_->songY_ + viewData_->songOffset_ + height > SONG_ROW_COUNT) {
        height = SONG_ROW_COUNT - viewData_->songY_ - viewData_->songOffset_;
    } else {

        // Move down from insert point

        unsigned char *dst = viewData_->song_->data_ + viewData_->songX_ +
                             (SONG_ROW_COUNT - 1) * SONG_CHANNEL_COUNT;
        unsigned char *src = dst - height * SONG_CHANNEL_COUNT;

        int rowCount =
            SONG_ROW_COUNT - (viewData_->songY_ + viewData_->songOffset_);

        for (int j = 0; j < rowCount; j++) {
            for (int i = 0; i < width; i++) {
                *dst++ = *src++;
            }
            dst -= (SONG_CHANNEL_COUNT + width);
            src -= (SONG_CHANNEL_COUNT + width);
        }
    }

    // Prepare copy pointer

    unsigned char *dst = viewData_->GetCurrentSongPointer();
    unsigned char *src = clipboard_.data_;

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            *dst++ = *src++;
        }
        dst += (SONG_CHANNEL_COUNT - width);
        src += (clipboard_.width_ - width);
    }

    updateCursor(0, height);
}

void SongView::unMuteAll() {

    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
};

// trk2als: mute y solo solo con el gesto de LSDJ (B + Select / B + Start), no
// con los atajos de LGPT (R + B / R + A / L ...)
bool SongView::lsdjGesture(bool solo) {
    unsigned short p = View::physMask_;
    return (p & EPBM_B) && (p & (solo ? EPBM_START : EPBM_SELECT));
}

void SongView::toggleMute() {

    UIController *controller = UIController::GetInstance();

    int from = viewData_->songX_;
    int to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    };
    controller->ToggleMute(from, to);
    viewMode_ = (viewMode_ != VM_MUTEON) ? VM_MUTEON : VM_NORMAL;
};

void SongView::switchSoloMode() {

    UIController *controller = UIController::GetInstance();
    int from = viewData_->songX_;
    int to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    };
    bool soloing = (viewMode_ != VM_SOLOON);
    controller->SwitchSoloMode(from, to, soloing);
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) soloed_[i] = soloing && i >= from && i <= to;   // la "S"
    viewMode_ = soloing ? VM_SOLOON : VM_NORMAL;
    isDirty_ = true;
};

void SongView::onStart() {
    // Always play with zero offset in chains when in SongView
    viewData_->chainRow_ = 0;
    Player *player = Player::GetInstance();
    unsigned char from = viewData_->songX_;
    unsigned char to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    }
    int renderMode = viewData_->renderMode_;
    if (renderMode > 0 && !player->IsRunning()) {
        viewData_->isRendering_ = true;
        View::SetNotification("Rendering started!");
    } else if (viewData_->isRendering_ && player->IsRunning()) {
        viewData_->isRendering_ = false;
        View::SetNotification("Rendering done!");
    }
    (void)from; (void)to;
    player->Transport();   // trk2als: Start = el transporte (como la barra espaciadora de Live)
};

void SongView::startCurrentRow() {
    Player *player = Player::GetInstance();
    player->SetSequencerMode(SM_LIVE);
    player->OnSongStartButton(0, 7, false, false);
}

void SongView::startImmediate() {
    Player *player = Player::GetInstance();

    unsigned char from = viewData_->songX_;
    unsigned char to = from;
    player->OnSongStartButton(from, to, false, true);
}

void SongView::onStop() {
    // Always play with zero offset in chains when in SongView
    viewData_->chainRow_ = 0;
    Player *player = Player::GetInstance();
    unsigned char from = viewData_->songX_;
    unsigned char to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    }

    player->OnSongStartButton(from, to, true, false);
};

void SongView::jumpToNextSection(int direction) {

    int current = viewData_->songY_ + viewData_->songOffset_;
    bool foundGap = false;
    for (int i = 0; i < SONG_ROW_COUNT; i++) {
        unsigned char *start = viewData_->song_->data_ + viewData_->songX_ +
                               SONG_CHANNEL_COUNT * current;
        if (foundGap && (*start != 0xFF)) {
            break;
        } else {
            if (*start == 0xFF) {
                foundGap = true;
            }
        }
        current += direction;
        if (current < 0) {
            current += SONG_ROW_COUNT;
        }
        if (current >= SONG_ROW_COUNT) {
            current -= SONG_ROW_COUNT;
        }
    }
    // If we go backwards, we stil have to go to the beginning of the block

    if (direction < 0) {
        while (current > 0) {
            unsigned char *start = viewData_->song_->data_ + viewData_->songX_ +
                                   SONG_CHANNEL_COUNT * current;
            if (*start == 0xFF) {
                current++;
                break;
            };
            current--;
        };
    }

    // Update viewdata position from current

    if ((current - viewData_->songOffset_ > 0x17) ||
        (current - viewData_->songOffset_ < 0)) {
        viewData_->songOffset_ = current - 4;
        if (viewData_->songOffset_ < 0) {
            viewData_->songOffset_ = 0;
        }
    }
    viewData_->songY_ = current - viewData_->songOffset_;
    isDirty_ = true;
}

/******************************************************
 ProcessButtonMask:
        process button mask even coming from the main
        application window
 ******************************************************/

void SongView::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!sesNowValid_) { takeSesSnap(sesNow_); sesNowValid_ = true; }
    handleButtonMask(mask, pressed);
    checkHistory();
}

/************************************************************
 trk2als: deshacer / rehacer en la Session
 ************************************************************/

void SongView::takeSesSnap(SesSnap &s) {
    Song *song = viewData_->song_;
    s.grid.assign(song->data_, song->data_ + SONG_CHANNEL_COUNT * SONG_ROW_COUNT);
    s.reps.assign(song->sceneRep_, song->sceneRep_ + SONG_ROW_COUNT);
    s.clips.clear();
    for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
        if (isAudioColumn(c)) continue;
        for (int r = 0; r < SONG_ROW_COUNT; r++) {
            unsigned char d = song->data_[c + SONG_CHANNEL_COUNT * r];
            if (d != 0xFF && !s.clips.count(d)) s.clips[d] = song->clips_->Get(d);
        }
    }
}

void SongView::restoreSesSnap(const SesSnap &s) {
    Song *song = viewData_->song_;
    memcpy(song->data_, &s.grid[0], s.grid.size());
    if (s.reps.size() == SONG_ROW_COUNT) memcpy(song->sceneRep_, &s.reps[0], SONG_ROW_COUNT);
    for (std::map<int, Clip>::const_iterator it = s.clips.begin(); it != s.clips.end(); ++it)
        song->clips_->Get(it->first) = it->second;
    // trk2als: el sonido de la pista, como estaba
    Project *project = viewData_->project_;
    if (s.tempo > 0) project->FindVariable(VAR_TEMPO)->SetInt(s.tempo);
    if (s.instrTrack >= 0 && project->TrackExists(s.instrTrack)) {
        int owner = project->GetTrackInstrument(s.instrTrack);
        if (owner >= 0 && owner < MAX_SAMPLEINSTRUMENT_COUNT) {
            SampleInstrument *si = (SampleInstrument *)project->GetInstrumentBank()->GetInstrument(owner);
            InstrumentPresets::Restore(si, s.instr);
            project->SetTrackName(s.instrTrack, s.trackName.c_str());
        }
    }
}

void SongView::snapInstr(SesSnap &s, int track) {
    Project *project = viewData_->project_;
    s.instrTrack = -1;
    if (!project->TrackExists(track)) return;
    int owner = project->GetTrackInstrument(track);
    if (owner < 0 || owner >= MAX_SAMPLEINSTRUMENT_COUNT) return;
    SampleInstrument *si = (SampleInstrument *)project->GetInstrumentBank()->GetInstrument(owner);
    InstrumentPresets::Capture(si, s.instr);
    s.trackName = project->GetTrackName(track);
    s.instrTrack = track;
}

// trk2als: antes de cambiar el sonido de una pista: va a la historia (Select+L
// lo regresa) y la pista se calla mientras su instrumento se reescribe
void SongView::beginSoundChange(int track) {
    SesSnap before;
    takeSesSnap(before);
    snapInstr(before, track);
    sesUndo_.push_back(before);
    if (sesUndo_.size() > 48) sesUndo_.erase(sesUndo_.begin());
    sesRedo_.clear();
    Player::GetInstance()->HoldTrack(track, true);
}

void SongView::endSoundChange(int track) {
    Player::GetInstance()->HoldTrack(track, false);
    takeSesSnap(sesNow_);
    sesNowValid_ = true;
}

void SongView::checkHistory() {
    Song *song = viewData_->song_;
    if (!sesNowValid_ || sesNow_.grid.size() != SONG_CHANNEL_COUNT * SONG_ROW_COUNT) return;
    if (memcmp(&sesNow_.grid[0], song->data_, sesNow_.grid.size()) == 0 &&
        (sesNow_.reps.size() != SONG_ROW_COUNT || memcmp(&sesNow_.reps[0], song->sceneRep_, SONG_ROW_COUNT) == 0))
        return;
    sesUndo_.push_back(sesNow_);
    if (sesUndo_.size() > 48) sesUndo_.erase(sesUndo_.begin());
    sesRedo_.clear();
    takeSesSnap(sesNow_);
}

void SongView::CaptureBegin() {
    takeSesSnap(capBefore_);
    capBefore_.tempo = viewData_->project_->FindVariable(VAR_TEMPO)->GetInt();
}

void SongView::CaptureCommit() {
    sesUndo_.push_back(capBefore_);
    if (sesUndo_.size() > 48) sesUndo_.erase(sesUndo_.begin());
    sesRedo_.clear();
    takeSesSnap(sesNow_);
    sesNowValid_ = true;
}

bool SongView::Undo(bool redo) {
    std::vector<SesSnap> &from = redo ? sesRedo_ : sesUndo_;
    std::vector<SesSnap> &to = redo ? sesUndo_ : sesRedo_;
    if (from.empty()) {
        View::SetNotification(redo ? "nada que rehacer" : "nada que deshacer");
        return true;
    }
    int it = from.back().instrTrack;
    if (it >= 0) Player::GetInstance()->HoldTrack(it, true);
    MixerService::GetInstance()->Lock();
    SesSnap now;
    takeSesSnap(now);
    if (it >= 0) snapInstr(now, it);
    if (from.back().tempo > 0) now.tempo = viewData_->project_->FindVariable(VAR_TEMPO)->GetInt();
    to.push_back(now);
    restoreSesSnap(from.back());
    from.pop_back();
    takeSesSnap(sesNow_);
    sesNowValid_ = true;
    MixerService::GetInstance()->Unlock();
    if (it >= 0) Player::GetInstance()->HoldTrack(it, false);
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    View::SetNotification(redo ? "rehecho" : "deshecho");
    isDirty_ = true;
    return true;
}

/************************************************************
 trk2als: edición en la Session con los gestos del clip
 ************************************************************/

GUIRect SongView::markRect() {
    GUIRect r(clipboard_.x_, clipboard_.y_ + clipboard_.offset_, viewData_->songX_,
              viewData_->songY_ + viewData_->songOffset_);
    r.Normalize();
    return r;
}

void SongView::startMark() {
    viewMode_ = VM_NORMAL;
    canDeepClone_ = false;
    clipboard_.active_ = true;
    clipboard_.x_ = viewData_->songX_;
    clipboard_.y_ = viewData_->songY_;
    clipboard_.offset_ = viewData_->songOffset_;
    sesMarking_ = true;
    sesDupDone_ = false;
    sesBPending_ = sesAPending_ = sesAMoved_ = false;
    View::SetNotification("marcando: flechas para agrandar");
    isDirty_ = true;
}

void SongView::endMark() {
    sesMarking_ = false;
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    isDirty_ = true;
}

void SongView::copyMark(std::vector<SesCell> &out) {
    out.clear();
    GUIRect r = markRect();
    Song *song = viewData_->song_;
    for (int c = r.Left(); c <= r.Right(); c++)
        for (int w = r.Top(); w <= r.Bottom(); w++) {
            unsigned char d = song->data_[c + SONG_CHANNEL_COUNT * w];
            if (d == 0xFF) continue;
            SesCell cell;
            cell.dx = c - r.Left();
            cell.dy = w - r.Top();
            cell.audio = isAudioColumn(c);
            cell.value = d;
            if (!cell.audio) cell.clip = song->clips_->Get(d);
            out.push_back(cell);
        }
}

// pega (los clips MIDI y de audio como clips nuevos e independientes); devuelve cuántos
int SongView::pasteCells(const std::vector<SesCell> &cells, int col, int row, int &skipped) {
    Song *song = viewData_->song_;
    Project *project = viewData_->project_;
    int n = 0;
    skipped = 0;
    for (size_t k = 0; k < cells.size(); k++) {
        const SesCell &cell = cells[k];
        int c = col + cell.dx, w = row + cell.dy;
        if (c < 0 || c >= SONG_CHANNEL_COUNT || w < 0 || w >= SONG_ROW_COUNT ||
            !project->TrackExists(c) || isAudioColumn(c) != cell.audio) {
            skipped++;
            continue;
        }
        unsigned char *dst = &song->data_[c + SONG_CHANNEL_COUNT * w];
        if (cell.audio) {
            // trk2als: un clip de audio pegado es independiente (como en Live):
            // si el original sigue en alguna celda, una copia en un lugar libre;
            // si ya no está (se cortó), el mismo
            unsigned char v = cell.value;
            bool inUse = false;
            for (int i = 0; i < SONG_ROW_COUNT * SONG_CHANNEL_COUNT && !inUse; i++)
                inUse = song->data_[i] == v && isAudioColumn(i % SONG_CHANNEL_COUNT) && &song->data_[i] != dst;
            if (inUse) {
                unsigned char nv = freeClip();
                if (nv == 0xFF || !project->GetInstrumentBank()->CopyInto(v, nv)) { skipped++; continue; }
                v = nv;
            }
            *dst = v;
        } else {
            unsigned short id = newClipId();
            if (id == NO_MORE_CLIP) { skipped++; continue; }
            song->clips_->Get(id) = cell.clip;
            *dst = (unsigned char)id;
            nameNewChain((unsigned char)id, c);
        }
        n++;
    }
    return n;
}

// A + flechas: lo marcado se mueve (sólo a celdas libres de su mismo tipo)
bool SongView::moveMark(int dx, int dy) {
    GUIRect r = markRect();
    Song *song = viewData_->song_;
    if (r.Left() + dx < 0 || r.Right() + dx >= viewData_->project_->TrackCount() ||
        r.Top() + dy < 0 || r.Bottom() + dy >= SONG_ROW_COUNT)
        return false;
    for (int c = r.Left(); c <= r.Right(); c++)
        for (int w = r.Top(); w <= r.Bottom(); w++) {
            if (song->data_[c + SONG_CHANNEL_COUNT * w] == 0xFF) continue;
            int nc = c + dx, nw = w + dy;
            if (isAudioColumn(nc) != isAudioColumn(c)) return false;
            bool inside = nc >= r.Left() && nc <= r.Right() && nw >= r.Top() && nw <= r.Bottom();
            if (!inside && song->data_[nc + SONG_CHANNEL_COUNT * nw] != 0xFF) return false;
        }
    std::vector<unsigned char> tmp;
    for (int c = r.Left(); c <= r.Right(); c++)
        for (int w = r.Top(); w <= r.Bottom(); w++) {
            tmp.push_back(song->data_[c + SONG_CHANNEL_COUNT * w]);
            song->data_[c + SONG_CHANNEL_COUNT * w] = 0xFF;
        }
    size_t k = 0;
    for (int c = r.Left(); c <= r.Right(); c++)
        for (int w = r.Top(); w <= r.Bottom(); w++, k++)
            if (tmp[k] != 0xFF) song->data_[(c + dx) + SONG_CHANNEL_COUNT * (w + dy)] = tmp[k];
    clipboard_.x_ += dx;
    clipboard_.y_ += dy;
    updateCursor(dx, dy);
    return true;
}

// Select + A con algo marcado: la copia va justo abajo (como Duplicar en Live)
void SongView::duplicateMark() {
    GUIRect r = markRect();
    std::vector<SesCell> cells;
    copyMark(cells);
    if (cells.empty()) { View::SetNotification("no hay clips marcados"); return; }
    Song *song = viewData_->song_;
    int h = r.Bottom() - r.Top() + 1;
    int at = -1;
    for (int t = r.Bottom() + 1; t + h <= SONG_ROW_COUNT && at < 0; t++) {
        bool free = true;
        for (size_t k = 0; k < cells.size() && free; k++)
            if (song->data_[(r.Left() + cells[k].dx) + SONG_CHANNEL_COUNT * (t + cells[k].dy)] != 0xFF)
                free = false;
        if (free) at = t;
    }
    if (at < 0) { View::SetNotification("no hay lugar abajo"); return; }
    int skipped;
    int n = pasteCells(cells, r.Left(), at, skipped);
    // la marca se va a la copia, para duplicar otra vez
    int dy = at - r.Top();
    clipboard_.y_ += dy;
    updateCursor(0, dy);
    char b[40];
    snprintf(b, sizeof(b), n == 1 ? "clip duplicado abajo" : "%d clips duplicados abajo", n);
    View::SetNotification(b);
}

/************************************************************
 trk2als: editar escenas (filas enteras, desde la columna de escenas)
 ************************************************************/

void SongView::scnRange(int &top, int &bot) {
    int cur = viewData_->songY_ + viewData_->songOffset_;
    top = bot = cur;
    if (scnMarking_) {
        top = scnMarkRow_ < cur ? scnMarkRow_ : cur;
        bot = scnMarkRow_ < cur ? cur : scnMarkRow_;
    }
}

void SongView::scnCopy(int top, int bot, std::vector<SesCell> &cells, std::vector<unsigned char> &reps) {
    cells.clear();
    reps.clear();
    Song *song = viewData_->song_;
    for (int w = top; w <= bot; w++) {
        reps.push_back(song->sceneRep_[w]);
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
            unsigned char d = song->data_[c + SONG_CHANNEL_COUNT * w];
            if (d == 0xFF) continue;
            SesCell cell;
            cell.dx = c;
            cell.dy = w - top;
            cell.audio = isAudioColumn(c);
            cell.value = d;
            if (!cell.audio) cell.clip = song->clips_->Get(d);
            cells.push_back(cell);
        }
    }
}

// n filas vacías en 'at' (lo de abajo baja); no se puede si se perdería algo
bool SongView::scnInsert(int at, int n) {
    Song *song = viewData_->song_;
    for (int w = SONG_ROW_COUNT - n; w < SONG_ROW_COUNT; w++)
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++)
            if (song->data_[c + SONG_CHANNEL_COUNT * w] != 0xFF) return false;
    memmove(song->data_ + SONG_CHANNEL_COUNT * (at + n), song->data_ + SONG_CHANNEL_COUNT * at,
            SONG_CHANNEL_COUNT * (SONG_ROW_COUNT - at - n));
    memset(song->data_ + SONG_CHANNEL_COUNT * at, 0xFF, SONG_CHANNEL_COUNT * n);
    memmove(song->sceneRep_ + at + n, song->sceneRep_ + at, SONG_ROW_COUNT - at - n);
    memset(song->sceneRep_ + at, 0, n);
    for (int c = 0; c < SONG_CHANNEL_COUNT; c++)   // lo que suena se va con su fila
        if (viewData_->songPlayPos_[c] >= at && viewData_->songPlayPos_[c] + n < SONG_ROW_COUNT)
            viewData_->songPlayPos_[c] += n;
    return true;
}

void SongView::scnDelete(int top, int bot) {
    Song *song = viewData_->song_;
    int n = bot - top + 1;
    memmove(song->data_ + SONG_CHANNEL_COUNT * top, song->data_ + SONG_CHANNEL_COUNT * (bot + 1),
            SONG_CHANNEL_COUNT * (SONG_ROW_COUNT - bot - 1));
    memset(song->data_ + SONG_CHANNEL_COUNT * (SONG_ROW_COUNT - n), 0xFF, SONG_CHANNEL_COUNT * n);
    memmove(song->sceneRep_ + top, song->sceneRep_ + bot + 1, SONG_ROW_COUNT - bot - 1);
    memset(song->sceneRep_ + SONG_ROW_COUNT - n, 0, n);
    for (int c = 0; c < SONG_CHANNEL_COUNT; c++)
        if (viewData_->songPlayPos_[c] > bot) viewData_->songPlayPos_[c] -= n;
}

bool SongView::scnPaste(int at, const std::vector<SesCell> &cells, const std::vector<unsigned char> &reps, int n) {
    if (n <= 0) return false;
    if (!scnInsert(at, n)) { View::SetNotification("no hay lugar abajo"); return false; }
    int skipped;
    pasteCells(cells, 0, at, skipped);
    for (int k = 0; k < n && k < (int)reps.size(); k++) viewData_->song_->sceneRep_[at + k] = reps[k];
    return true;
}

bool SongView::sceneButton(unsigned short phys, unsigned short v, bool pressed, bool repeat) {
    bool sel = (phys & EPBM_SELECT) != 0;
    int cur = viewData_->songY_ + viewData_->songOffset_;
    if (!pressed) {
        if (v == EPBM_B && scnBPending_ && scnMarking_) {   // B sola: copia lo marcado
            scnBPending_ = false;
            int top, bot;
            scnRange(top, bot);
            scnCopy(top, bot, scnClip_, scnReps_);
            scnRows_ = bot - top + 1;
            char b[40];
            snprintf(b, sizeof(b), scnRows_ == 1 ? "escena copiada" : "%d escenas copiadas", scnRows_);
            View::SetNotification(b);
            scnMarking_ = false;
            isDirty_ = true;
            return true;
        }
        return scnMarking_;
    }
    if (repeat && !(v & (EPBM_UP | EPBM_DOWN))) return scnMarking_;
    if (v == EPBM_SELECT) return scnMarking_;   // Select solo no suelta la marca (Select + A la duplica)
    if (v != EPBM_B) scnBPending_ = false;
    bool changed = false;
    if (!sesNowValid_) { takeSesSnap(sesNow_); sesNowValid_ = true; }
    if (!scnMarking_ && sel && v == EPBM_B && !(phys & EPBM_A)) {   // Select, B: marcar escenas
        scnMarking_ = true;
        scnMarkRow_ = cur;
        View::SetNotification("marcando escenas: ↑ ↓");
        isDirty_ = true;
        return true;
    }
    if (v == EPBM_A && (phys & EPBM_B)) {   // B + A: borra (lo de abajo sube)
        int top, bot;
        scnRange(top, bot);
        MixerService::GetInstance()->Lock();
        scnDelete(top, bot);
        MixerService::GetInstance()->Unlock();
        char b[40];
        snprintf(b, sizeof(b), bot == top ? "escena borrada" : "%d escenas borradas", bot - top + 1);
        View::SetNotification(b);
        scnBPending_ = false;
        if (scnMarking_) { scnMarking_ = false; updateCursor(0, top - cur); }
        changed = true;
    } else if (v == EPBM_A && sel) {
        MixerService::GetInstance()->Lock();
        if (scnMarking_) {   // Select + A con escenas marcadas: duplicarlas justo abajo
            int top, bot;
            scnRange(top, bot);
            std::vector<SesCell> cells;
            std::vector<unsigned char> reps;
            scnCopy(top, bot, cells, reps);
            if (scnPaste(bot + 1, cells, reps, bot - top + 1)) {
                View::SetNotification(bot == top ? "escena duplicada abajo" : "escenas duplicadas abajo");
                scnMarkRow_ = bot + 1;   // la marca se va a la copia
                updateCursor(0, (bot + 1 + (cur - top)) - cur);
            }
        } else if (scnRows_ > 0) {   // Select + A: pegar aquí (lo de abajo baja)
            if (scnPaste(cur, scnClip_, scnReps_, scnRows_))
                View::SetNotification(scnRows_ == 1 ? "escena pegada" : "escenas pegadas");
        } else {
            View::SetNotification("nada que pegar (Select, B, B copia)");
        }
        MixerService::GetInstance()->Unlock();
        changed = true;
    } else if (scnMarking_) {
        if (v == EPBM_B) scnBPending_ = true;
        else if (v == EPBM_UP) updateCursor(0, -1);
        else if (v == EPBM_DOWN) updateCursor(0, 1);
        else if (v == EPBM_A) { scnMarking_ = false; View::SetNotification("marca cancelada"); }
        else { scnMarking_ = false; isDirty_ = true; return false; }
        isDirty_ = true;
        return true;
    } else {
        return false;
    }
    checkHistory();
    isDirty_ = true;
    return true;
}

bool SongView::InterceptButton(unsigned short phys, unsigned short v, bool pressed, bool repeat) {
    // trk2als: L / R tocados solos (sin otro botón en medio), como la octava en
    // el clip: L baja una página (las filas que se ven), R sube; R tres veces
    // rápido va a la cabecera de la pista (su sonido, su tipo). L + algo sigue
    // siendo su atajo
    static unsigned short pageTap = 0;
    if (pressed && !repeat) pageTap = ((v == EPBM_L || v == EPBM_R) && phys == v) ? v : 0;
    if (!pressed && v == pageTap && v) {
        pageTap = 0;
        static unsigned int rTaps[2] = {0, 0};
        unsigned int now = SDL_GetTicks();
        if (v == EPBM_R && !sesMarking_ && !sesPasting_ && !headerMode_ &&
            rTaps[0] && now - rTaps[0] < 900) {
            rTaps[0] = rTaps[1] = 0;
            rowMode_ = false;
            clampTrackCursor();
            headerMode_ = true;
            isDirty_ = true;
            return true;
        }
        if (v == EPBM_R) { rTaps[0] = rTaps[1]; rTaps[1] = now; }
        else rTaps[0] = rTaps[1] = 0;
        if (!sesMarking_ && !sesPasting_ && !headerMode_) {
            // la página y el cursor juntos; con pocas filas, el cursor se posa en la última que hay
            int page = v == EPBM_L ? View::songRowCount_ : -View::songRowCount_;
            int abs = viewData_->songY_ + viewData_->songOffset_ + page;
            if (abs < 0) abs = 0;
            if (abs > SONG_ROW_COUNT - 1) abs = SONG_ROW_COUNT - 1;
            int off = viewData_->songOffset_ + page;
            if (off < 0) off = 0;
            viewData_->songOffset_ = off;
            viewData_->songY_ = abs - off;
            if (viewData_->songY_ < 0) { viewData_->songOffset_ = abs; viewData_->songY_ = 0; }
            clampRows();
            isDirty_ = true;
            return true;
        }
    }
    if (rowMode_ && !headerMode_) return sceneButton(phys, v, pressed, repeat);
    if (rowMode_ || headerMode_) return false;
    bool sel = (phys & EPBM_SELECT) != 0;
    const unsigned short arrows = EPBM_UP | EPBM_DOWN | EPBM_LEFT | EPBM_RIGHT;
    int dx = (v == EPBM_LEFT) ? -1 : (v == EPBM_RIGHT) ? 1 : 0;
    int dy = (v == EPBM_UP) ? -1 : (v == EPBM_DOWN) ? 1 : 0;

    if (!sesMarking_ && !sesPasting_) {
        if (!pressed || repeat) return false;
        if (sel && v == EPBM_B && !(phys & EPBM_A)) {      // Select, B: marcar
            startMark();
            return true;
        }
        if (sel && v == EPBM_A && !(phys & EPBM_B) && viewMode_ != VM_CLONE) {   // Select+A: pegar
            if (sesClip_.empty()) View::SetNotification("nada que pegar");
            else {
                sesPasting_ = true;
                View::SetNotification("suelta A para pegar");
            }
            isDirty_ = true;
            return true;
        }
        return false;
    }

    if (v == EPBM_START) {   // tocar / parar: suelta la marca y sigue como siempre
        sesPasting_ = false;
        endMark();
        return false;
    }
    if (sel && (v == EPBM_L || v == EPBM_R)) {   // deshacer / rehacer
        sesPasting_ = false;
        endMark();
        return false;
    }
    if (!sesNowValid_) { takeSesSnap(sesNow_); sesNowValid_ = true; }
    MixerService::GetInstance()->Lock();

    if (sesPasting_) {
        if (pressed && v == EPBM_B) {
            sesPasting_ = false;
            View::SetNotification("pegado cancelado");
        } else if (pressed && (v & arrows)) {
            updateCursor(dx, dy);
        } else if (!pressed && v == EPBM_A) {        // al soltar A queda pegado ahí
            sesPasting_ = false;
            int skipped;
            int n = pasteCells(sesClip_, viewData_->songX_,
                               viewData_->songY_ + viewData_->songOffset_, skipped);
            char b[48];
            if (skipped) snprintf(b, sizeof(b), "%d pegados, %d no caben", n, skipped);
            else snprintf(b, sizeof(b), n == 1 ? "clip pegado" : "%d clips pegados", n);
            View::SetNotification(b);
        }
    } else {   // marcando
        if (pressed) {
            if (v & arrows) sesDupDone_ = false;
            if (v != EPBM_B) sesBPending_ = false;
            if (v != EPBM_A) sesAPending_ = false;
            if (v & arrows) {
                if (phys & EPBM_A) {
                    sesAMoved_ = true;
                    if (!moveMark(dx, dy)) View::SetNotification("ahi no cabe");
                } else {
                    updateCursor(dx, dy);
                }
            } else if (v == EPBM_B && !repeat) {
                if (sel && sesDupDone_) {                // después de duplicar: sigue marcada la copia
                    sesDupDone_ = false;
                    View::SetNotification("marcando la copia");
                } else if (sel) {                        // otra vez Select, B: filas enteras
                    clipboard_.x_ = 0;
                    int last = viewData_->project_->TrackCount() - 1;
                    updateCursor(last - viewData_->songX_, 0);
                    View::SetNotification("marcadas las escenas enteras");
                } else if (phys & EPBM_A) {
                } else {
                    sesBPending_ = true;
                }
            } else if (v == EPBM_A && !repeat) {
                if (sel) {
                    duplicateMark();
                    sesDupDone_ = true;
                } else if (phys & EPBM_B) {              // B + A: corta
                    copyMark(sesClip_);
                    GUIRect r = markRect();
                    for (int c = r.Left(); c <= r.Right(); c++)
                        for (int w = r.Top(); w <= r.Bottom(); w++)
                            viewData_->song_->data_[c + SONG_CHANNEL_COUNT * w] = 0xFF;
                    char b[40];
                    snprintf(b, sizeof(b), sesClip_.size() == 1 ? "clip cortado" : "%d clips cortados",
                             (int)sesClip_.size());
                    View::SetNotification(b);
                    sesBPending_ = false;
                    endMark();
                } else {
                    sesAPending_ = true;
                    sesAMoved_ = false;
                }
            }
        } else {
            if (v == EPBM_B && sesBPending_ && !sel) {   // B sola: copia
                sesBPending_ = false;
                copyMark(sesClip_);
                char b[40];
                snprintf(b, sizeof(b), sesClip_.size() == 1 ? "clip copiado" : "%d clips copiados",
                         (int)sesClip_.size());
                View::SetNotification(b);
                endMark();
            }
            if (v == EPBM_A && sesAPending_) {           // A sola: lanza (una fila) o cancela
                sesAPending_ = false;
                if (!sesAMoved_) {
                    GUIRect r = markRect();
                    if (r.Top() == r.Bottom()) {   // trk2als: los clips marcados de una fila, juntos en el 1
                        Player::GetInstance()->LaunchGroup(r.Top(), r.Left(), r.Right());
                        View::SetNotification(r.Left() == r.Right() ? "clip lanzado" : "grupo lanzado");
                    } else {
                        View::SetNotification("marca cancelada");
                    }
                    endMark();
                }
            }
        }
    }
    MixerService::GetInstance()->Unlock();
    checkHistory();
    isDirty_ = true;
    return true;
}

// lo que lleva el loop de la celda que suena, en milésimas (-1 si no suena)
int SongView::playFraction(int ch, int &total) {
    Player *player = Player::GetInstance();
    total = 0;
    unsigned char clip = viewData_->currentPlayChain_[ch];
    if (!player->IsRunning() || !player->IsChannelPlaying(ch) || clip == 0xFF) return -1;
    int start = 0;
    if (isAudioColumn(ch)) {
        total = player->AudioBars(clip);
    } else {
        int ls, le;
        if (viewData_->song_->GetClipLoop(clip, ls, le)) { start = ls; total = le - ls + 1; }
        else total = viewData_->song_->clips_->Get(clip).pages;
    }
    if (total <= 0) return -1;
    int bar = viewData_->chainPlayPos_[ch] - start;
    int step = viewData_->phrasePlayPos_[ch];
    if (bar < 0) bar = 0;
    if (bar >= total) bar = total - 1;
    return ((bar * 16 + step + 1) * 1000) / (total * 16);
}

void SongView::handleButtonMask(unsigned short mask, bool pressed) {

    // trk2als: Joy2Live: A tocada sobre un clip lo abre al soltarla; B tocada
    // sola quita los mute y solo fijos
    if (pressed) {
        if (mask != EPBM_A) aOpenPending_ = false;
        // LSDJ: lo que quedó muteado o en solo dura "hasta presionar B otra vez"
        if (mask == EPBM_B && !clipboard_.active_ && viewMode_ != VM_MUTEON && viewMode_ != VM_SOLOON) {
            bool any = false;
            Player *pl = Player::GetInstance();
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) if (pl->IsChannelMuted(i) || soloed_[i]) any = true;
            if (any) {
                unMuteAll();
                for (int i = 0; i < SONG_CHANNEL_COUNT; i++) soloed_[i] = false;
                isDirty_ = true;
            }
        }
        bTap_ = false;
    } else {
        if (newSlotCell_ >= 0 && !(mask & EPBM_A)) {   // trk2als: soltó A en '+'
            int cell = newSlotCell_;
            newSlotCell_ = -1;
            aOpenPending_ = false;
            isDirty_ = true;
            if (*viewData_->GetCurrentSongPointer() == 0xFF &&
                int(viewData_->GetCurrentSongPointer() - viewData_->song_->data_) == cell) {
                pickAudio(cell);
            }
            return;
        }
        if (aOpenPending_ && !(mask & EPBM_A)) {
            aOpenPending_ = false;
            // trk2als: como en Live: A lanza el clip (entra en el siguiente
            // compás); para editarlo, Select + →
            if (!headerMode_) {
                Player::GetInstance()->LaunchClip(viewData_->songX_, viewData_->songY_ + viewData_->songOffset_);
                isDirty_ = true;
            }
            return;
        }
        if (bTap_ && !(mask & EPBM_B)) {
            bTap_ = false;
            unMuteAll();   // trk2als: quita mutes y solos (sin restaurar nada)
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) soloed_[i] = false;
            if (viewMode_ == VM_SOLOON || viewMode_ == VM_MUTEON) viewMode_ = VM_NORMAL;
            isDirty_ = true;
            return;
        }
    }

    if (!pressed) {
        // trk2als: como el manual de LSDJ: B + Select / B + Start valen
        // mientras se sostienen; soltando B primero, se quedan (hasta
        // presionar B otra vez); soltando Select / Start primero, se quitan
        if (viewMode_ == VM_MUTEON) {
            if (mask & EPBM_R) {
                toggleMute();
            } else if (!(mask & (EPBM_B | EPBM_A))) {
                viewMode_ = VM_NORMAL;   // trk2als: quedó fijo
            }
        };
        if (viewMode_ == VM_SOLOON) {
            if (mask & EPBM_R) {
                switchSoloMode();
            } else if (!(mask & (EPBM_B | EPBM_A))) {
                viewMode_ = VM_NORMAL;   // trk2als: quedó fijo
            }
        };
        return;
    };

    if (headerMode_) { // trk2als: en la cabecera se cambia el tipo de pista
        processHeaderButtonMask(mask);
        return;
    }
    if (rowMode_) {   // trk2als: en la columna de filas se lanza / detiene la escena
        processRowButtonMask(mask);
        return;
    }

    if (viewMode_ == VM_NEW && isAudioColumn(viewData_->songX_)) {
        // trk2als: A otra vez = loop nuevo (un instrumento libre), como la chain nueva en MIDI
        if (mask == EPBM_A) {
            unsigned char clip = freeClip();
            if (clip != 0xFF) {
                *viewData_->GetCurrentSongPointer() = clip;
                lastClip_ = clip;
                isDirty_ = true;
            }
            mask &= (0xFFFF - EPBM_A);
        }
        viewMode_ = VM_NORMAL;
    }

    if (viewMode_ == VM_NEW) {
        if (mask == EPBM_A) {
            createClip();
            mask &= (0xFFFF - EPBM_A);
        }
    }

    if (viewMode_ == VM_CLONE) {
        if ((mask & EPBM_A) && (mask & EPBM_L)) {
            clonePosition();
            mask &= (0xFFFF - (EPBM_A | EPBM_L));
            canDeepClone_ = false;   // trk2als: ya es independiente
        } else {
            viewMode_ = VM_SELECTION;
        }
    };

    if (canDeepClone_ && (mask & EPBM_A) && (mask & EPBM_L)) {
        deepClonePosition();
        mask &= (0xFFFF - (EPBM_A | EPBM_L));
        canDeepClone_ = false;
    }
    if (clipboard_.active_) {
        viewMode_ = VM_SELECTION;
    };
    // Process selection related keys

    if (viewMode_ == VM_SELECTION) {
        if (clipboard_.active_ == false) {
            clipboard_.active_ = true;
            clipboard_.x_ = viewData_->songX_;
            clipboard_.y_ = viewData_->songY_;
            clipboard_.offset_ = viewData_->songOffset_;
            saveX_ = clipboard_.x_;
            saveY_ = clipboard_.y_;
            saveOffset_ = clipboard_.offset_;
        }
        processSelectionButtonMask(mask);
    } else {

        // Switch back to normal mode

        viewMode_ = VM_NORMAL;
        processNormalButtonMask(mask);
    }
}

/******************************************************
 processNormalButtonMask:
        process button mask in the case there is no
        selection active
 ******************************************************/

void SongView::processNormalButtonMask(unsigned int mask) {

    // B Modifier

    if (mask & EPBM_B) {

        if (mask & EPBM_DOWN)
            updateSongOffset(SongView::jumpLength_);
        if (mask & EPBM_UP)
            updateSongOffset(-SongView::jumpLength_);
        // trk2als: (B + ← → cambiaba entre song y live; ahora siempre es como Live)
        if ((mask & EPBM_A) && (!(mask & EPBM_R)))
            cutPosition();
        if (mask & EPBM_L) {

            viewMode_ = VM_CLONE;
        };
        if ((mask & EPBM_R) && lsdjGesture(false)) {   // trk2als: solo B + Select (como LSDJ)
            toggleMute();
        };
        if (mask & EPBM_START) {
            startImmediate();
        }
    } else {

        // A modifier

        if (mask & EPBM_A) {

            if (mask & EPBM_DOWN)
                updateChain(-0x10);
            if (mask & EPBM_UP)
                updateChain(0x10);
            if (mask & EPBM_LEFT)
                updateChain(-0x01);
            if (mask & EPBM_RIGHT)
                updateChain(0x01);
            if (mask & EPBM_L && !canDeepClone_) {
                pasteClipboard();
            }
            if (mask == EPBM_A) {
                unsigned char *cell = viewData_->GetCurrentSongPointer();
                if (*cell != 0xFF) {
                    aOpenPending_ = true;      // trk2als: se lanza al soltar A
                } else {
                    // trk2als: como el botoncito de stop de Live en los slots vacíos
                    Player::GetInstance()->StopTrack(viewData_->songX_, viewData_->songY_ + viewData_->songOffset_);
                    isDirty_ = true;
                }
            }
            if ((mask & EPBM_R) && lsdjGesture(true)) {   // trk2als: solo B + Start (como LSDJ)
                switchSoloMode();
            };
        } else {

            // R Modifier

            if (mask & EPBM_R) {

                // trk2als: (R + L quitaba los mutes como en LGPT; ahora es B, como LSDJ)

                if (mask & EPBM_RIGHT) {
                    unsigned char *data = viewData_->GetCurrentSongPointer();
                    if (*data != 0xFF && isAudioColumn(viewData_->songX_)) {
                        // trk2als: un loop se edita en su instrumento
                        ViewType vt = VT_INSTRUMENT;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        viewData_->currentInstrument_ = *data;
                        viewData_->instrumentFromSong_ = true;
                        SetChanged();
                        NotifyObservers(&ve);
                    } else if (*data != 0xFF) {
                        ViewType vt = VT_CLIP;   // trk2als: ya no hay chains
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        viewData_->currentChain_ = *data;
                        viewData_->chainRow_ = 0;
                        SetChanged();
                        NotifyObservers(&ve);
                    } else if (!isAudioColumn(viewData_->songX_) && viewData_->project_->TrackExists(viewData_->songX_)) {
                        openVirtualClip();   // trk2als: celda vacía: un clip vacío para tocar y capturar
                    } else if (viewData_->project_->TrackExists(viewData_->songX_)) {
                        // trk2als: celda vacía de audio: escoger un loop
                        pickAudio(int(data - viewData_->song_->data_));
                    }
                }

                if (mask & EPBM_UP) {
                    ViewType vt = VT_PROJECT;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {
                    ViewType vt = VT_MIXER;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_START) {   // trk2als: Stop All Clips, como en Live
                    Player::GetInstance()->StopAllClips();
                }

            } else {

                // L Modifier

                if (mask & EPBM_L) {
                    if (mask & EPBM_DOWN)
                        jumpToNextSection(1);
                    if (mask & EPBM_UP)
                        jumpToNextSection(-1);
                    if (mask & EPBM_START)
                        startCurrentRow();
                    if (mask & EPBM_LEFT)
                        nudgeTempo(-1);
                    if (mask & EPBM_RIGHT)
                        nudgeTempo(1);
                } else {

                    // No modifier

                    if (mask & EPBM_DOWN)
                        updateCursor(0, 1);
                    if (mask & EPBM_UP) {
                        // trk2als: desde la fila 00, arriba es la cabecera
                        if (viewData_->songY_ == 0 && viewData_->songOffset_ == 0) {
                            headerMode_ = true;
                            isDirty_ = true;
                        } else {
                            updateCursor(0, -1);
                        }
                    }
                    if (mask & EPBM_LEFT) {
                        // trk2als: desde la primera pista, ← va a las escenas (como en LSDJ)
                        if (viewData_->songX_ == 0) rowMode_ = true;
                        else updateCursor(-1, 0);
                        isDirty_ = true;
                    }
                    if (mask & EPBM_RIGHT)
                        updateCursor(1, 0);

                    if (mask & EPBM_START) {
                        onStart();
                    }
                }
            }
        }
    }

    if ((!(mask & EPBM_A)) && updatingChain_) {
        unsigned char *c = viewData_->song_->data_ + updateX_ +
                           8 * (viewData_->songOffset_ + updateY_);
        updatingChain_ = false;
    }
};

/******************************************************
 trk2als: cabecera de las columnas = tipo de pista
   A abre la ventana para elegir el tipo, izq/der
   cambia de columna, abajo regresa a las celdas
 ******************************************************/

static void TrackPresetCallback(View &v, ModalView &dialog) {
    SongView &sv = (SongView &)v;
    sv.OnTrackBrowser(dialog.GetReturnCode(), ((PresetDialog &)dialog).GetSelected());
}

// trk2als: A en la cabecera de una pista MIDI: el navegador de sonidos (con
// "Kit vacío", "Importar .sf2" y "Sample vacío"); lo que se elija va a su instrumento
void SongView::openTrackBrowser(int ch, bool isNew) {
    presetCh_ = ch;
    newTrack_ = isNew;
    PresetDialog *d = new PresetDialog(*this);
    d->SetEmptyOptions(true);
    // trk2als: A lanza el sonido a la pista (entra en el 1)
    launcher_->Open(viewData_->project_->GetTrackInstrument(ch), this);
    d->SetLauncher(launcher_);
    DoModal(d, TrackPresetCallback);
}

void SongView::goToInstrument(int ch) {
    int owner = viewData_->project_->GetTrackInstrument(ch);
    if (owner < 0) return;
    viewData_->currentInstrument_ = owner;
    viewData_->instrumentFromSong_ = true;
    headerMode_ = false;   // al volver, el cursor ya está en los clips
    viewData_->songX_ = ch;
    ViewType vt = VT_INSTRUMENT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
}

static void TrackSf2Callback(View &v, ModalView &dialog) {
    ((SongView &)v).OnSf2Done(dialog.GetReturnCode() == 1);
}

void SongView::OnSf2Done(bool ok) {
    Project *project = viewData_->project_;
    int ch = presetCh_;
    if (ok) {
        int owner = project->GetTrackInstrument(ch);
        if (owner >= 0) {
            SampleInstrument *si = (SampleInstrument *)project->GetInstrumentBank()->GetInstrument(owner);
            std::string n = si->ZoneSetName();
            if (n.empty()) n = "Multicapa";
            project->SetTrackName(ch, n.c_str());
            View::SetNotification((n + " cargado").c_str());
        }
        endSoundChange(ch);
    } else {
        Player::GetInstance()->HoldTrack(ch, false);
        if (!sesUndo_.empty()) sesUndo_.pop_back();   // no cambió nada
        if (newTrack_) {
            project->DeleteTrack(ch);
            for (int c = ch; c < SONG_CHANNEL_COUNT - 1; c++) soloed_[c] = soloed_[c + 1];   // la "S" va con su pista
            soloed_[SONG_CHANNEL_COUNT - 1] = false;   // la pista nueva se queda sin nada: fuera
            clampTrackCursor();
        }
    }
    newTrack_ = false;
    isDirty_ = true;
}

void SongView::OnTrackBrowser(int code, const std::string &name) {
    Project *project = viewData_->project_;
    int ch = presetCh_;
    if (code == PD_CLOSED) {   // trk2als: se cerró después de lanzar (o sin lanzar nada)
        if (newTrack_ && launcher_->Phase() && launcher_->PendingTrack() == ch)
            newTrack_ = false;  // ya va un sonido en camino: se queda
        if (newTrack_) {        // pista nueva y no se lanzó nada: fuera
            project->DeleteTrack(ch);
            for (int c = ch; c < SONG_CHANNEL_COUNT - 1; c++) soloed_[c] = soloed_[c + 1];   // la "S" va con su pista
            soloed_[SONG_CHANNEL_COUNT - 1] = false;
            newTrack_ = false;
            clampTrackCursor();
        } else if (project->TrackExists(ch)) {
            headerMode_ = false;   // de la cabecera a los clips de la pista
            viewData_->songX_ = ch;
            clampTrackCursor();
        }
        isDirty_ = true;
        return;
    }
    if (code == 1) {
        newTrack_ = false;
        LoadTrackPreset(name);
        return;
    }
    if (code == PD_EMPTY_KIT) {
        newTrack_ = false;
        ApplyTrackType(ch, TTD_NEWKIT, false);
        return;
    }
    if (code == PD_EMPTY_SAMPLE) {
        newTrack_ = false;
        int owner = project->GetTrackInstrument(ch);
        if (owner < 0) return;
        beginSoundChange(ch);
        project->GetInstrumentBank()->GetInstrument(owner)->Purge();
        project->SetTrackName(ch, "Sample");
        endSoundChange(ch);
        View::SetNotification("sample vacío: A en sample elige el archivo");
        goToInstrument(ch);
        return;
    }
    if (code == PD_IMPORT_SF2) {
        int owner = project->GetTrackInstrument(ch);
        if (owner < 0) return;
        viewData_->currentInstrument_ = owner;
        beginSoundChange(ch);
        PresetDialog *pd = new PresetDialog(*this);   // trk2als: el navegador nuevo, solo .sf2
        pd->SetPick(PresetDialog::PICK_SF2, owner);
        DoModal(pd, TrackSf2Callback);
        return;
    }
    // cancelar: si la pista era nueva, se quita
    if (newTrack_) {
        project->DeleteTrack(ch);
            for (int c = ch; c < SONG_CHANNEL_COUNT - 1; c++) soloed_[c] = soloed_[c + 1];   // la "S" va con su pista
            soloed_[SONG_CHANNEL_COUNT - 1] = false;
        newTrack_ = false;
        clampTrackCursor();
        isDirty_ = true;
    }
}

static void DeleteTrackCallback(View &v, ModalView &dialog) {
    SongView &sv = (SongView &)v;
    if (dialog.GetReturnCode() == MBL_YES && sv.pendingTypeCh_ >= 0)
        sv.ApplyTrackType(sv.pendingTypeCh_, TTD_DELETE, false);
    sv.pendingTypeCh_ = -1;
}

void SongView::askDeleteTrack(int ch) {
    Project *project = viewData_->project_;
    if (!project->TrackExists(ch)) return;
    pendingTypeCh_ = ch;
    char t[64];
    snprintf(t, sizeof(t), "¿Borrar la pista %d?\n%s", ch + 1, project->GetTrackName(ch));
    MessageBox *mb = new MessageBox(*this, "No se puede deshacer", MBBF_YES | MBBF_NO);
    mb->Title(t).Label(MBL_YES, "Ok").Label(MBL_NO, "Volver");
    DoModal(mb, DeleteTrackCallback);
}

// trk2als: el preset va al instrumento de la pista, y la pista toma su nombre
void SongView::LoadTrackPreset(const std::string &name) {
    Project *project = viewData_->project_;
    int ch = presetCh_;
    int owner = project->GetTrackInstrument(ch);
    if (name.empty() || owner < 0 || owner >= MAX_SAMPLEINSTRUMENT_COUNT) return;
    SampleInstrument *si =
        (SampleInstrument *)project->GetInstrumentBank()->GetInstrument(owner);
    // trk2als: primero sus samples, con la pista sonando (es lo lento); luego
    // la pista se calla solo lo que tarda en aplicarse
    bool cleaned = false;
    SamplePool *pool = SamplePool::GetInstance();
    unsigned long t0 = SDL_GetTicks();
    unsigned long m0 = MixerService::GetInstance()->BuffersMixed();
    unsigned w0, k0;
    MixerService::GetInstance()->TakeMax(w0, k0);
    AudioWatchReset();
    bool ready = InstrumentPresets::Prepare(name);
    if (!ready && pool->GetNameListSize() >= MAX_PIG_SAMPLES - 16) {
        // trk2als: el pool se llenó con los samples de los sonidos de antes:
        // se quitan los que ya nadie usa y se intenta otra vez. Sin callar
        // nada ni detener el audio: lo que se quita no está sonando. Lo que
        // se podía deshacer de sonidos se pierde (sus samples ya no están)
        project->PurgeInstruments(true);
        for (size_t i = 0; i < sesUndo_.size(); i++) sesUndo_[i].instrTrack = -1;
        for (size_t i = 0; i < sesRedo_.size(); i++) sesRedo_[i].instrTrack = -1;
        cleaned = true;
        ready = InstrumentPresets::Prepare(name);
    }
    unsigned long t1 = SDL_GetTicks();
    unsigned long m1 = MixerService::GetInstance()->BuffersMixed();
    unsigned w1, k1;
    MixerService::GetInstance()->TakeMax(w1, k1);
    unsigned gOut, gMix, gLock;
    AudioWatchTake(gOut, gMix, gLock);
    beginSoundChange(ch);   // ahora sí: la pista se calla lo que tarda en aplicarse
    bool ok = ready && InstrumentPresets::Load(si, name);
    endSoundChange(ch);
    extern int g_prepareHeld, g_prepareDepth;
    extern unsigned long g_prepareOwner, g_prepareSelf;
    Trace::Log("PRESET", "%s: samples %lu ms, pista callada %lu ms | durante la lectura: %lu bloques "
               "mezclados; huecos máx: salida %u ms, mezcla %u ms, candado %u ms | candado soltado %d "
               "(prof %d, dueño %s)",
               name.c_str(), t1 - t0, (unsigned long)SDL_GetTicks() - t1, m1 - m0, gOut, gMix, gLock,
               g_prepareHeld, g_prepareDepth,
               g_prepareOwner == 0 ? "nadie" : (g_prepareOwner == g_prepareSelf ? "la interfaz" : "otro hilo"));
    if (!ok) {
        View::SetNotification(pool->GetNameListSize() >= MAX_PIG_SAMPLES - 16
            ? "Ya no caben más samples en el proyecto"
            : "No se pudo cargar el preset");
        return;
    }
    std::string shortName = SoundLauncher::DisplayName(name);
    project->SetTrackName(ch, shortName.c_str());
    launcher_->SetPreset(owner, name);
    View::SetNotification((shortName + (cleaned ? " cargado\nse quitaron samples que nadie usaba" : " cargado")).c_str());
    headerMode_ = false;   // trk2als: de la cabecera al primer clip de la pista
    viewData_->songX_ = ch;
    clampTrackCursor();
    isDirty_ = true;
}

static void TrackTypeCallback(View &v, ModalView &dialog) {
    SongView &sv = (SongView &)v;
    int code = dialog.GetReturnCode();
    if (code >= 0 && sv.pendingTypeCh_ >= 0) {
        sv.ApplyTrackType(sv.pendingTypeCh_, code, false);
    }
    sv.pendingTypeCh_ = -1;
}

// trk2als: agregar (code = tipo) o borrar (TTD_DELETE) la pista
void SongView::ApplyTrackType(int channel, int code, bool) {
    Project *project = viewData_->project_;
    char msg[40];
    if (code == TTD_LOAD) {   // trk2als: el mismo navegador que la cabecera (lanza al 1)
        openTrackBrowser(channel, false);
        return;
    }
    if (code == TTD_EDIT || code == TTD_NEWKIT) {   // al instrumento de la pista; B regresa a la Song
        int owner = project->GetTrackInstrument(channel);
        if (owner < 0) return;
        if (code == TTD_NEWKIT) {   // trk2als: un kit vacío para armarlo pad por pad
            SampleInstrument *si =
                (SampleInstrument *)project->GetInstrumentBank()->GetInstrument(owner);
            if (si->GetType() != IT_SAMPLE) return;
            beginSoundChange(channel);
            si->MakeEmptyKit();
            project->SetTrackName(channel, "Kit");
            endSoundChange(channel);
            View::SetNotification("kit nuevo: A+flechas elige el pad");
        }
        viewData_->currentInstrument_ = owner;
        viewData_->instrumentFromSong_ = true;
        headerMode_ = false;   // al volver, el cursor ya está en los clips
        ViewType vt = VT_INSTRUMENT;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        SetChanged();
        NotifyObservers(&ve);
        return;
    }
    if (code == TTD_DELETE) {
        // trk2als: también con la música andando (Player::RemoveTrack)
        std::string name = project->GetTrackName(channel);
        launcher_->TrackRemoved(channel);
        Player::GetInstance()->RemoveTrack(channel);
        snprintf(msg, sizeof(msg), "pista %s borrada", name.c_str());
        View::SetNotification(msg);
    } else {
        int ch = project->AddTrack(code == TT_AUDIO ? TT_AUDIO : TT_MIDI);
        if (ch < 0) return;
        viewData_->songX_ = ch;
        // trk2als: la MIDI va directo al navegador; la de audio, a elegir un loop
        if (code != TT_AUDIO) {
            openTrackBrowser(ch, true);
            return;
        }
        headerMode_ = false;
        clampTrackCursor();
        isDirty_ = true;
        pickAudio(-1);
        return;
    }
    headerMode_ = false;   // trk2als: de la cabecera a los clips (si quedan pistas)
    clampTrackCursor();
    isDirty_ = true;
}

// el cursor solo anda por pistas que existen; en la cabecera, también por el '+'
void SongView::clampTrackCursor() {
    int count = viewData_->project_->TrackCount();
    if (count == 0) headerMode_ = true;   // sin pistas solo queda el '+'
    int maxX = headerMode_ ? (count < Project::MaxTracks() ? count : count - 1) : count - 1;   // el '+' solo si cabe otra
    if (viewData_->songX_ > maxX) viewData_->songX_ = maxX;
    if (viewData_->songX_ < 0) viewData_->songX_ = 0;
}

// trk2als: la columna de escenas (a la izquierda, como en LSDJ): A lanza la
// escena, Y + ↑ ↓ sus repeticiones, Select + Start detiene los clips al compás;
// → vuelve a las pistas
void SongView::processRowButtonMask(unsigned int mask) {
    Player *player = Player::GetInstance();
    if ((mask & EPBM_R) && (mask & EPBM_START)) {   // Select + Start: Stop All Clips
        player->StopAllClips();
        isDirty_ = true;
        return;
    }
    if (mask & EPBM_R) {   // Select + ↑ ↓: Project / Mixer, como siempre (Select + ← → no hace nada aquí)
        if (mask & (EPBM_UP | EPBM_DOWN)) {
            rowMode_ = false;
            processNormalButtonMask(mask);
        }
        return;
    }
    if ((mask & EPBM_B) && (mask & EPBM_Y)) {   // B + Y: las repeticiones (xN) apagadas / prendidas
        Song *song = viewData_->song_;
        song->followOff_ = !song->followOff_;
        View::SetNotification(song->followOff_ ? "repeticiones apagadas" : "repeticiones prendidas");
        isDirty_ = true;
        return;
    }
    if (mask & EPBM_B) {
        if (mask & EPBM_DOWN) updateSongOffset(SongView::jumpLength_);
        if (mask & EPBM_UP) updateSongOffset(-SongView::jumpLength_);
        isDirty_ = true;
        return;
    }
    if (mask & EPBM_Y) {   // trk2als: Y + ↑ ↓ (como dividir en el clip): cuántas veces suena la escena (∞, x1 … x16)
        int dir = (mask & EPBM_UP) ? 1 : (mask & EPBM_DOWN) ? -1 : 0;
        int srow = viewData_->songY_ + viewData_->songOffset_;
        bool has = false;
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++)
            if (viewData_->project_->TrackExists(i) && viewData_->song_->data_[i + SONG_CHANNEL_COUNT * srow] != 0xFF) has = true;
        if (dir && !has) View::SetNotification("escena vacía: sin repeticiones");
        if (dir && has) {
            unsigned char &r = viewData_->song_->sceneRep_[srow];
            int v = r + dir;
            if (v < 0) v = 0;
            if (v > 16) v = 16;
            r = (unsigned char)v;
            isDirty_ = true;
        }
        return;
    }
    if (mask == EPBM_A) {   // trk2als: A lanza la escena (la fila), como en Live
        viewData_->chainRow_ = 0;
        player->LaunchScene(viewData_->songY_ + viewData_->songOffset_);
    }
    if (mask & EPBM_START) player->Transport();
    if (mask & EPBM_RIGHT) {   // → vuelve a la primera pista
        rowMode_ = false;
        viewData_->songX_ = 0;
        clampTrackCursor();
    }
    if (mask & EPBM_DOWN) updateCursor(0, 1);
    if (mask & EPBM_UP) updateCursor(0, -1);
    isDirty_ = true;
}

void SongView::processHeaderButtonMask(unsigned int mask) {
    // trk2als: B+A borra la pista (B solo espera a la A)
    if (mask == (EPBM_A | EPBM_B)) {
        askDeleteTrack(viewData_->songX_);
        return;
    }
    if (mask == EPBM_B) return;
    if (mask & (EPBM_R | EPBM_L | EPBM_B)) { // atajos de siempre: salir de la cabecera
        headerMode_ = false;
        isDirty_ = true;
        processNormalButtonMask(mask);
        return;
    }
    if (mask & EPBM_START) {
        onStart();
        return;
    }
    if (mask == EPBM_A) { // A: en el '+' agregar una pista; en una pista, su sonido
        int ch = viewData_->songX_;
        Project *project = viewData_->project_;
        bool adding = !project->TrackExists(ch);
        if (!adding) {
            if (project->GetTrackInstrument(ch) >= 0) openTrackBrowser(ch, false);
            else View::SetNotification("pista de audio: A en una celda carga un loop");
            return;
        }
        pendingTypeCh_ = ch;
        TrackTypeDialog *d = new TrackTypeDialog(*this, ch, adding, project->GetTrackName(ch),
                                                 project->GetTrackInstrument(ch) >= 0);
        DoModal(d, TrackTypeCallback);
        return;
    }
    if (mask & EPBM_A) return;
    if (mask & EPBM_LEFT) updateCursor(-1, 0);
    if (mask & EPBM_RIGHT) updateCursor(1, 0);
    if ((mask & EPBM_DOWN) && viewData_->project_->TrackCount() > 0) {
        headerMode_ = false;
        clampTrackCursor();
        isDirty_ = true;
    }
}

/******************************************************
 processSelectionButtonMask:
        process button mask in the case there is a
        selection active
 ******************************************************/

void SongView::processSelectionButtonMask(unsigned int mask) {

    // trk2als: como en Joy2Live: B + A corta lo marcado y A sola cancela la marca
    if (mask == (EPBM_A | EPBM_B)) {
        cutSelection();
        return;
    }
    if (mask == EPBM_A) {
        clipboard_.active_ = false;
        viewMode_ = VM_NORMAL;
        isDirty_ = true;
        return;
    }

    // B Modifier

    if (mask & EPBM_B) {
        if ((mask & EPBM_R) && lsdjGesture(false)) {   // trk2als: solo B + Select (como LSDJ)
            toggleMute();
        };
        if (mask & EPBM_L) {
            extendSelection();
        };
        if (mask == EPBM_B) {
            copySelection();
        }

    } else {

        // A modifier

        if (mask & EPBM_A) {
            if (mask & EPBM_L) {
                cutSelection();
            }
            if ((mask & EPBM_R) && lsdjGesture(true)) {   // trk2als: solo B + Start (como LSDJ)
                switchSoloMode();
            };
        } else {

            // R Modifier

            if (mask & EPBM_R) {

                // trk2als: (R + L quitaba los mutes como en LGPT; ahora es B, como LSDJ)

                if (mask & EPBM_RIGHT) {
                    unsigned char *data = viewData_->GetCurrentSongPointer();
                    if (*data != 0xFF && isAudioColumn(viewData_->songX_)) {
                        // trk2als: un loop se edita en su instrumento
                        ViewType vt = VT_INSTRUMENT;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        viewData_->currentInstrument_ = *data;
                        viewData_->instrumentFromSong_ = true;
                        SetChanged();
                        NotifyObservers(&ve);
                    } else if (*data != 0xFF) {
                        ViewType vt = VT_CLIP;   // trk2als: ya no hay chains
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        viewData_->currentChain_ = *data;
                        viewData_->chainRow_ = 0;
                        SetChanged();
                        NotifyObservers(&ve);
                    } else if (!isAudioColumn(viewData_->songX_) && viewData_->project_->TrackExists(viewData_->songX_)) {
                        openVirtualClip();   // trk2als: celda vacía: un clip vacío para tocar y capturar
                    } else if (viewData_->project_->TrackExists(viewData_->songX_)) {
                        // trk2als: celda vacía de audio: escoger un loop
                        pickAudio(int(data - viewData_->song_->data_));
                    }
                }

                if (mask & EPBM_UP) {
                    ViewType vt = VT_PROJECT;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {
                    ViewType vt = VT_MIXER;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_START) {   // trk2als: Stop All Clips, como en Live
                    Player::GetInstance()->StopAllClips();
                }

            } else {

                // No modifier

                if (mask & EPBM_DOWN)
                    updateCursor(0, 1);
                if (mask & EPBM_UP)
                    updateCursor(0, -1);
                if (mask & EPBM_LEFT)
                    updateCursor(-1, 0);
                if (mask & EPBM_RIGHT)
                    updateCursor(1, 0);
                if (mask & EPBM_START) {
                    onStart();
                }
            }
        }
    }
}

/******************************************************
 DrawVuBars:
        Draw stereo L/R VU meters to the right of tracks
        Called from both AnimationUpdate and DrawView to ensure
        bars are always visible and never flicker
 ******************************************************/

void SongView::DrawVuBars() {
    Player *player = Player::GetInstance();

    GUIPoint anchor = GetAnchor();
    GUIPoint vuPos = anchor;
    vuPos._x += 25;
    vuPos._y += View::songRowCount_ - 1;
    
    GUITextProperties vuProps;
    vuProps.invert_ = true;

    float peakLevelsL[1];
    float peakLevelsR[1];

    // Read master peak level (post-volume) to match MixerView's Master meter
    if (!player->IsRunning()) {
        peakLevelsL[0] = peakLevelsR[0] = 0.0f;
    } else {
        MixerService *ms = MixerService::GetInstance();
        uint32_t masterLevel = ms->GetMasterPeakLevel();
        peakLevelsL[0] = (float)((masterLevel >> 16) & 0xFFFF) / 32767.0f;
        peakLevelsR[0] = (float)(masterLevel & 0xFFFF) / 32767.0f;
    }

    // Update bar height for master using utility
    int displayHeightsL[1];
    int displayHeightsR[1];
    UpdateVuBarHeights(vuBarHeightsL_, displayHeightsL, peakLevelsL, 1);
    UpdateVuBarHeights(vuBarHeightsR_, displayHeightsR, peakLevelsR, 1);

    // Draw vertical VU bars for L and R channels (two columns side by side)
    for (int row = 0; row < VU_METER_HEIGHT; row++) {
        SetColor(GetVuBarColor(row));

        // Draw left channel at x
        GUIPoint posL = vuPos;
        posL._y -= row;
        DrawVuBarRow(this, posL, row, displayHeightsL[0], vuProps,
                     GetVuBarColor(row));

        // Draw right channel at x+1 (one character to the right)
        GUIPoint posR = vuPos;
        posR._x += 1;
        posR._y -= row;
        DrawVuBarRow(this, posR, row, displayHeightsR[0], vuProps,
                     GetVuBarColor(row));
    }

    SetColor(CD_NORMAL);
}

/******************************************************
 AnimationUpdate:
        Update animation and draw VU meters
 ******************************************************/

// trk2als: con xN andando, la Session sigue a la escena: si la que viene
// queda fuera de la pantalla, baja la vista (la que suena arriba). Solo
// cuando cambia la que viene: si luego te mueves tú, no te jala de vuelta
bool SongView::followScroll() {
    int fn, fl;
    if (!Player::GetInstance()->SceneFollow(fn, fl)) { followSeen_ = -1; return false; }
    if (fn == followSeen_) return false;
    followSeen_ = fn;
    const int rows = View::songRowCount_;
    int off = viewData_->songOffset_;
    if (fn >= off && fn <= off + rows - 1 && fn - 1 >= off) return false;   // ya se ven las dos
    int abs = viewData_->songY_ + off;
    off = fn - 1;
    if (off > SONG_ROW_COUNT - rows) off = SONG_ROW_COUNT - rows;
    if (off < 0) off = 0;
    if (abs < off) abs = off;
    if (abs > off + rows - 1) abs = off + rows - 1;
    viewData_->songOffset_ = off;
    viewData_->songY_ = abs - off;
    clampRows();
    isDirty_ = true;
    return true;
}

void SongView::AnimationUpdate() {
    bool scrolled = followScroll();
    if (blockGrid()) {   // trk2als: los medidores por pista, ~12 veces por segundo
        if (scrolled) blockRedraw();
        unsigned long now = System::GetInstance()->GetClock();
        if (now - lastVuDraw_ < 80) return;
        lastVuDraw_ = now;
        float l[8], r[8];
        ReadMixBusPeakLevels(Player::GetInstance()->IsRunning(), l, r);
        bool changed = false;
        for (int i = 0; i < 8; i++) {
            float pk = l[i] > r[i] ? l[i] : r[i];
            int h = int(pk * 10 + 0.5f);
            if (h > 10) h = 10;
            int v = h >= blockVu_[i] ? h : blockVu_[i] - 1;   // cae despacio
            if (v != blockVu_[i]) { blockVu_[i] = v; changed = true; }
        }
        if (!changed && blockState() != lastBlockState_) changed = true;   // p.ej. venció el aviso
        if (changed) blockRedraw();
        return;
    }
    DrawVuBars();
}

/******************************************************
 Redraw:
        redraw completely the song view
 ******************************************************/

void SongView::DrawView() {

    Clear();
    if (!blockGrid()) View::EnableNotification();   // trk2als: con bloques va abajo

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    // Prepare selection related information

    GUIRect selRect;
    if (clipboard_.active_) {
        selRect = GUIRect(clipboard_.x_, clipboard_.y_ + clipboard_.offset_,
                          viewData_->songX_,
                          viewData_->songY_ + viewData_->songOffset_);

        selRect.Normalize();
    }

    // Draw title

    SetColor(CD_NORMAL);

    Player *player = Player::GetInstance();

    std::ostringstream os;

    os << "Session";
    os << ": ";

    if (IsProjectFolder(songname_)) {
        os << ProjectNiceName(songname_);
    }

    std::string buffer(os.str());

    // trk2als: el nombre de la pista donde está el cursor
    clampTrackCursor();
    {
        Project *project = viewData_->project_;
        std::string tname = project->TrackExists(viewData_->songX_)
                                ? cellName() : std::string("agregar pista");
        if (rowMode_) {   // trk2als: en la columna de filas
            char fb[16];
            snprintf(fb, sizeof(fb), "FILA %02X", viewData_->songY_ + viewData_->songOffset_);
            tname = fb;
        }
        std::string ctx = tname;   // trk2als: siempre como la Session de Live
        if (!drawTopBar(TAB_SES, ctx.c_str())) {
            DrawString(pos._x, pos._y, buffer.c_str(), props);
            if (tname.size() > 17) tname = tname.substr(0, 17);
            SetColor(CD_HILITE2);
            DrawString(pos._x + 23, pos._y, tname.c_str(), props);
            SetColor(CD_NORMAL);
        }
    }

    // trk2als: rejilla de bloques grandes
    if (blockGrid()) {
        drawBlockGrid();
        if (rowMode_)
            drawHelpBar("START:lanzar la fila;SEL+START:detener todo;>:a las pistas");
        else if (sesMarking_)
            drawHelpBar("flechas:marcar;B:copiar;B+A:cortar;A+flechas:mover;SEL+A:duplicar;SEL,B:escenas;A:salir");
        else if (sesPasting_)
            drawHelpBar("suelta A:pegar;flechas:acomodar;B:cancelar");
        else
            drawHelpBar(headerMode_ ? "A:sonido / agregar;B+A:borrar pista;<>:pista;v:a los clips"
                                    : "A:poner/abrir;A+<>:elegir;B+A:borrar;SEL,B:marcar;SEL+A:pegar;SEL+<>:pantalla;SEL+L:deshacer;SEL+R:rehacer");
        return;
    }

    // Compute song grid location

    GUIPoint anchor = gridAnchor();   // trk2als: más a la izquierda, celdas de 4

    // Display row numbers

    char row[3];
    pos = anchor;
    pos._x -= 3;
    for (int j = 0; j < View::songRowCount_; j++) {
        char p = j + viewData_->songOffset_;
        ((p / altRowNumber_) % 2) ? SetColor(CD_ROW) : SetColor(CD_ROW2);
        hex2char(p, row);
        bool here = rowMode_ && j == viewData_->songY_;   // trk2als: cursor en la fila
        if (here) SetColor(CD_CURSOR);
        props.invert_ = here;
        DrawString(pos._x, pos._y, row, props);
        props.invert_ = false;
        pos._y += 1;
    }

    // Draw column titles
    pos = anchor;
    pos._y -= 1;
    SetColor(CD_COL_TITLE);
    char chbuf[4];
    short dx = SONG_COL_W;
    int trackCount = viewData_->project_->TrackCount();
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        // trk2als: tipo de pista + número: M2 midi, A6 audio; después de la
        // última, '+' para agregar una
        if (i > trackCount) break;
        TrackType type = viewData_->project_->GetTrackType(i);
        if (i == trackCount) {
            snprintf(chbuf, sizeof(chbuf), "+ ");
        } else {
            snprintf(chbuf, sizeof(chbuf), "%c%d", type == TT_AUDIO ? 'A' : 'M', i + 1);
        }
        SetColor(type == TT_AUDIO ? CD_HILITE1 : CD_COL_TITLE);
        bool here = headerMode_ && i == viewData_->songX_;
        if (here) SetColor(CD_CURSOR);
        props.invert_ = here;
        DrawString(pos._x + i * dx, pos._y, chbuf, props);
        props.invert_ = false;
    }

    SetColor(CD_NORMAL);

    pos = anchor;
    unsigned char *data =
        viewData_->song_->data_ + (SONG_CHANNEL_COUNT * viewData_->songOffset_);
    dx = SONG_COL_W;
    short dy = 1;
    for (int j = 0; j < View::songRowCount_; j++) {

        pos._x = anchor._x;

        for (int i = 0; i < 8; i++) {

            bool invert = false;

            // see if we need to invert current step
            // if there's a selection or we are at cursor position

            if (clipboard_.active_) {
                if ((i >= selRect.Left()) && (i <= selRect.Right()) &&
                    (j + viewData_->songOffset_ >= selRect.Top()) &&
                    (j + viewData_->songOffset_ <= selRect.Bottom())) {
                    invert = true;
                }
            } else {
                if (!rowMode_ && i == viewData_->songX_ && j == viewData_->songY_) {
                    invert = true;
                }
            }

            // draw current step

            unsigned char d = *data++;

            if (!viewData_->project_->TrackExists(i)) { // trk2als: pista que no existe
                pos._x += dx;
                continue;
            }

            if (d != 0xFF && isAudioColumn(i)) {
                SetColor(CD_HILITE1); // trk2als: loop de audio
            } else if (d == 0xFE) {
                SetColor(CD_SONGVIEWFE);
            } else if (d == 0x00) {
                SetColor(CD_SONGVIEW00);
            } else {
                SetColor(CD_NORMAL);
            }

            if (invert) {
                SetColor(CD_HILITE2);
                props.invert_ = true;
            }

            if (isAudioColumn(i)) {   // trk2als: los clips de audio son A00, A01...
                char clip[4];
                int cellIdx = int(data - 1 - viewData_->song_->data_);
                if (cellIdx == newSlotCell_) strcpy(clip, "A+ ");   // loop nuevo, al soltar A
                else if (d == 0xFF) strcpy(clip, "---");
                else snprintf(clip, sizeof(clip), "%s", clipLabel(d).c_str());
                DrawString(pos._x, pos._y, clip, props);
            } else if (d == 0xFF) {
                DrawString(pos._x, pos._y, "--", props);
            } else {
                hex2char(d, row);
                DrawString(pos._x, pos._y, row, props);
            }

            // Put back drawing state

            if (invert) {
                SetColor(CD_NORMAL);
                props.invert_ = false;
            }

            // Next step

            pos._x += dx;
        }
        pos._y += dy;
    }
    SetColor(CD_NORMAL);

    drawMap();
    drawNotes(gridAnchor()._x, SONG_COL_W);

    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    };

    // Draw VU bars at the end so they appear on top of everything
    // They will always show the empty dashes and smoothly decay when paused
    DrawVuBars();
    if (rowMode_)
        drawHelpBar("START:lanzar la fila;SEL+START:detener todo;>:a las pistas");
    else
    drawHelpBar(headerMode_ ? "A:sonido / agregar;B+A:borrar pista;<>:pista;v:a los clips"
                            : "A:poner/abrir;A+<>:elegir;B+A:borrar;SEL+<>:pantalla;SEL+L:deshacer;SEL+R:rehacer");
};

/******************************************************
 OnPlayerUpdate:
        Called when positions in player change. Should
        provide visual feedback of currently played
        position
 ******************************************************/

void SongView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {

    Player *player = Player::GetInstance();

    // trk2als: con la rejilla de bloques se redibuja todo solo si algo cambió
    if (blockGrid()) {
        if (blockState() != lastBlockState_ || eventType == PET_STOP) blockRedraw();
        return;
    }

    GUIPoint anchor = gridAnchor();   // trk2als
    GUIPoint pos = anchor;
    pos._x -= 1;

    GUITextProperties props;
    SetColor(CD_CURSOR);

    // Loop on all channels

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {

        // Clear all current positions

        int y = lastPlayedPosition_[i] - viewData_->songOffset_;
        if (y >= 0 && y < View::songRowCount_ &&
            viewData_->playMode_ != PM_AUDITION) {
            pos._y = anchor._y + y;
            DrawString(pos._x, pos._y, " ", props);
        }

        // Clear all last queued positions

        y = lastQueuedPosition_[i] - viewData_->songOffset_;
        if (y >= 0 && y < View::songRowCount_) {
            pos._y = anchor._y + y;
            DrawString(pos._x, pos._y, " ", props);
        }

        // For each playing position, draw current location

        if (player->IsChannelPlaying(i)) {
            if (eventType != PET_STOP) {
                if (viewData_->currentPlayChain_[i] != 0xFF) {
                    int y = viewData_->songPlayPos_[i] - viewData_->songOffset_;
                    if (y >= 0 && y < View::songRowCount_) {
                        pos._y = anchor._y + y;
                        if (!player->IsChannelMuted(i)) {
                            SetColor(CD_PLAY);
                            DrawString(pos._x, pos._y, ">", props);
                        } else {
                            SetColor(CD_MUTE);
                            DrawString(pos._x, pos._y, "-", props);
                        }
                        SetColor(CD_CURSOR);
                        lastPlayedPosition_[i] = viewData_->songPlayPos_[i];
                    }
                }
            }
        }

        // If in live mode, update queued position

        if (player->GetSequencerMode() == SM_LIVE) {
            if (player->GetQueueingMode(i) != QM_NONE) {

                if (eventType != PET_STOP) {
                    int y =
                        player->GetQueuePosition(i) - viewData_->songOffset_;
                    if (y >= 0 && y < View::songRowCount_) {
                        pos._y = anchor._y + y;
                        char *indicator = player->GetLiveIndicator(i);
                        DrawString(pos._x, pos._y, indicator, props);
                        lastQueuedPosition_[i] = player->GetQueuePosition(i);
                    }
                }
            };
        }
        pos._x += SONG_COL_W;
    }

    SetColor(CD_NORMAL);

    // Draw clipping indicator & CPU usage

    if (View::miniLayout_) {
        pos._y = 0;
        pos._x = 25;
    } else {
        pos = GetAnchor();   // el panel de la derecha no se mueve
        pos._x += 25;
    }

    if (player->Clipped()) {
        DrawString(pos._x, pos._y, "clip", props);
    } else {
        DrawString(pos._x, pos._y, "----", props);
    }

    char strbuffer[24];
    pos._y += 1;
    loadText(strbuffer);   // trk2als: carga en la Mac y estimado en la consola
    DrawString(pos._x, pos._y, strbuffer, props);

    System *sys = System::GetInstance();
    int batt = sys->GetBatteryLevel();
    if (batt >= 0) {
        if (batt < 90) {
            SetColor(CD_HILITE2);
            invertBatt_ = !invertBatt_;
        } else {
            invertBatt_ = false;
        };
        props.invert_ = invertBatt_;

        pos._y += 1;
        sprintf(strbuffer, "%3.3d", batt);
        DrawString(pos._x, pos._y, strbuffer, props);
    }

    if (eventType != PET_STOP) {
        SetColor(CD_NORMAL);
        props.invert_ = false;
        int time = int(player->GetPlayTime());
        int mi = time / 60;
        int se = time - mi * 60;
        sprintf(strbuffer, "%2.2d:%2.2d", mi, se);
        pos._y += 1;
        DrawString(pos._x, pos._y, strbuffer, props);
    }
    drawNotes(gridAnchor()._x, SONG_COL_W);
};

void SongView::nudgeTempo(int direction) {
    ApplicationCommandDispatcher *dispatcher =
        ApplicationCommandDispatcher::GetInstance();
    switch (direction) {
    case -1:
        dispatcher->OnNudgeDown();
        break;
    case 1:
        dispatcher->OnNudgeUp();
        break;
    }
}

/******************************************************
 trk2als: la Session como rejilla de bloques grandes (8 pistas x 8 escenas,
 o 12 con SESSIONROWS). Todo va con figuras: celdas de color por pista con
 el número del clip y su nombre, lo que suena con marco claro, lo que está
 en cola con marco que parpadea, las pistas en mute apagadas, y abajo un
 medidor por pista. La navegación y la edición no cambian.
 ******************************************************/

// trk2als: el parpadeo de lo que espera el 1: encendido / apagado parejo, 250 ms cada uno
bool SongView::queueBlink() {
    return (System::GetInstance()->GetClock() / 250) % 2 == 0;
}

// trk2als: la escena que sigue sola (xN) parpadea mientras espera; en su
// último compás, más rápido (para marcar que ya viene)
static bool followBlink(int barsLeft) {
    unsigned long t = System::GetInstance()->GetClock();
    return barsLeft <= 1 ? (t / 110) % 2 == 0 : (t / 250) % 2 == 0;
}

bool SongView::blockGrid() {
    return !View::miniLayout_ && SupportsShapes();
}

// el aviso de lo que acaba de pasar (dura NOTIFICATION_TIMEOUT)
bool SongView::noticeOn() {
    return !displayNotification_.empty() &&
           (SDL_GetTicks() - notificationTime_) <= NOTIFICATION_TIMEOUT;
}

// la etiqueta de la pista: KIT, SMP (sampler), INS (multicapa), AUD (audio)
static const char *trackKind(Project *project, int i) {
    if (project->GetTrackType(i) == TT_AUDIO) return "AUD";
    int owner = project->GetTrackInstrument(i);
    I_Instrument *in = owner >= 0 ? project->GetInstrumentBank()->GetInstrument(owner) : 0;
    if (!in) return "MID";
    if (in->GetType() != IT_SAMPLE) return "MID";
    SampleInstrument *si = (SampleInstrument *)in;
    if (si->IsKit()) return "KIT";
    if (si->IsZoned() && si->ZoneCount() > 1) return "INS";
    return "SMP";
}

void SongView::blockRedraw() {
    if (inBlockRedraw_ || HasModal()) return;
    inBlockRedraw_ = true;
    ClearShapes();
    DrawView();
    inBlockRedraw_ = false;
}

// lo que se ve y cambia al sonar: si es igual, no hace falta redibujar
std::string SongView::blockState() {
    Player *player = Player::GetInstance();
    std::string st;
    char b[48];
    bool live = player->GetSequencerMode() == SM_LIVE;
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        int pos = (player->IsRunning() && player->IsChannelPlaying(i) &&
                   viewData_->currentPlayChain_[i] != 0xFF) ? viewData_->songPlayPos_[i] : -1;
        int q = -1, blink = 0;
        if (live && player->IsRunning() && player->GetQueueingMode(i) != QM_NONE) {
            q = player->GetQueuePosition(i);
            blink = queueBlink();   // trk2als: lo que espera el 1 parpadea parejo (4 veces por segundo)
        }
        int tot;
        int fr = pos >= 0 ? playFraction(i, tot) : -1;
        snprintf(b, sizeof(b), "%d,%d,%d,%d,%d,%d,%d;", pos, q, blink, player->IsChannelMuted(i) ? 1 : 0, fr,
                 int(player->GetQueueingMode(i)), player->StopCell(i));
        st += b;
    }
    char lt[24];
    loadText(lt);
    int t = player->IsRunning() ? int(player->GetPlayTime()) : -1;
    snprintf(b, sizeof(b), "%s|%d|%d|%d", lt, t, player->Clipped() ? 1 : 0, noticeOn() ? 1 : 0);
    st += b;
    int fn, fl;   // la escena que sigue sola: su parpadeo
    if (player->SceneFollow(fn, fl)) {
        snprintf(b, sizeof(b), "|f%d,%d,%d", fn, fl, followBlink(fl) ? 1 : 0);
        st += b;
    }
    return st;
}

static unsigned char trackColors[8][3] = {   // tema Lavanda
    {0xE6, 0xC0, 0x7A}, {0xE8, 0xA0, 0x7E}, {0x8F, 0xA0, 0xF0}, {0xB7, 0xA4, 0xF0}, {0xE2, 0x8D, 0xB8}, {0x7F, 0xC9, 0x8E}, {0x74, 0xC2, 0xC8}, {0xB2, 0xD5, 0x8A}};

// trk2als: los colores de las pistas se pueden cambiar en config.xml:
// <TRACKCOLORS value="E0B24A,E8955A,..."/> (hasta 8, en hex)
static void loadTrackColors() {
    static int gen = -1;   // se vuelven a leer si cambia el tema
    static unsigned char def[8][3];
    if (gen == Config::Generation()) return;
    if (gen < 0) memcpy(def, trackColors, sizeof(def));
    gen = Config::Generation();
    memcpy(trackColors, def, sizeof(def));
    const char *v = Config::GetInstance()->GetValue("TRACKCOLORS");
    if (!v) return;
    for (int i = 0; i < 8 && *v; i++) {
        unsigned int c;
        if (sscanf(v, "%6x", &c) != 1) break;
        trackColors[i][0] = (c >> 16) & 0xFF;
        trackColors[i][1] = (c >> 8) & 0xFF;
        trackColors[i][2] = c & 0xFF;
        const char *comma = strchr(v, ',');
        if (!comma) break;
        v = comma + 1;
    }
}

// trk2als: en fondos claros los grises suaves de la rejilla se pierden: se marcan más
static int gridPct(bool light, int p) { return light ? (p * 2 < p + 14 ? p * 2 : p + 14) : p; }

void SongView::drawBlockGrid() {
    loadTrackColors();
    Player *player = Player::GetInstance();
    Project *project = viewData_->project_;
    lastBlockState_ = blockState();
    {   // trk2als: una escena sin clips vuelve a ∞ (sus repeticiones no aplican)
        Song *song = viewData_->song_;
        for (int r = 0; r < SONG_ROW_COUNT; r++) {
            if (!song->sceneRep_[r]) continue;
            bool any = false;
            for (int i = 0; i < SONG_CHANNEL_COUNT && !any; i++)
                any = project->TrackExists(i) && song->data_[i + SONG_CHANNEL_COUNT * r] != 0xFF;
            if (!any) song->sceneRep_[r] = 0;
        }
    }

    GUIColor bg = ColorOf(CD_BACKGROUND);
    GUIColor fg = ColorOf(CD_NORMAL);
    GUIColor hi1 = ColorOf(CD_HILITE1);
    GUIColor hi2 = ColorOf(CD_HILITE2);
    GUIColor cur = ColorOf(CD_CURSOR);
    GUIColor play = ColorOf(CD_PLAY);
    GUIColor dim = Mix(bg, fg, 50);
    // en fondos claros los grises suaves se pierden: la rejilla se marca más
    bool lightBg = bg._r * 77 + bg._g * 151 + bg._b * 28 > 150 * 256;
    GUIColor cellBg = Mix(bg, fg, gridPct(lightBg, 6));
    GUIColor panel = Mix(bg, fg, gridPct(lightBg, 10));

    const int rows = View::songRowCount_;
    // trk2als: las escenas a la izquierda (como en LSDJ), en una columna ancha
    // para sus repeticiones; las pistas a la derecha
    const int SX = 6, GAP = 4, SW = 60;             // la columna de escenas
    const int X0 = SX + SW + GAP + 2;               // la primera pista
    int trackCount = project->TrackCount();
    const int NCOL = trackCount > Project::MaxTracks() ? trackCount : Project::MaxTracks();
    const int COLW = (640 - 6 - X0 + GAP) / NCOL;
    const int XEND = X0 + NCOL * COLW - GAP;        // donde acaban las pistas
    const int HY = 36, HH = 24;                     // cabeceras
    const int GY = 64, GBOT = 436;                  // rejilla
    const int ROWH = (GBOT - GY) / rows;
    const int VY = 442, VH = 32;                    // medidores
    int offset = viewData_->songOffset_;

    // filas que suenan (para la columna de escenas)
    bool rowPlaying[16] = {false};
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        if (player->IsRunning() && player->IsChannelPlaying(i) &&
            viewData_->currentPlayChain_[i] != 0xFF) {
            int y = viewData_->songPlayPos_[i] - offset;
            if (y >= 0 && y < rows) rowPlaying[y] = true;
        }
    }

    GUIRect selRect;
    if (clipboard_.active_) {
        selRect = GUIRect(clipboard_.x_, clipboard_.y_ + clipboard_.offset_,
                          viewData_->songX_, viewData_->songY_ + offset);
        selRect.Normalize();
    }

    // la pista donde está el cursor, un pelo más clara (de arriba a abajo):
    // para saber siempre dónde estás
    int curCol = rowMode_ ? -1 : viewData_->songX_;
    if (curCol >= 0 && curCol < trackCount)
        FillRect(X0 + curCol * COLW - 2, HY - 2, COLW - GAP + 4, GBOT - HY + 2, Mix(bg, fg, gridPct(lightBg, 5)), 4);

    // cabeceras
    for (int i = 0; i < SONG_CHANNEL_COUNT && i <= trackCount && (i < trackCount || i < Project::MaxTracks()); i++) {
        int x = X0 + i * COLW;
        bool here = headerMode_ && i == viewData_->songX_;
        if (i == trackCount) {   // agregar pista
            FillRect(x, HY, COLW - GAP, HH, here ? cur : panel, 3);
            DrawText(x + (COLW - GAP) / 2 - 8, HY + 4, "+", here ? bg : dim, 1);
            break;
        }
        int tci = project->GetTrackColor(i);
        GUIColor tc(trackColors[tci][0], trackColors[tci][1], trackColors[tci][2]);
        FillRect(x, HY, COLW - GAP, HH, here ? cur : (i == curCol ? Mix(bg, fg, gridPct(lightBg, 16)) : panel), 3);
        FillRect(x, HY, COLW - GAP, 3, tc);
        GUIColor ncol = here ? bg : (player->IsChannelMuted(i) ? dim : fg);
        DrawText(x + 5, HY + 5, trackKind(project, i), ncol, 1);
        if (player->IsChannelMuted(i)) DrawText(x + COLW - GAP - 12, HY + 7, "M", ColorOf(CD_MUTE), 0);
        else if (soloed_[i]) DrawText(x + COLW - GAP - 12, HY + 7, "S", hi2, 0);   // trk2als: en solo
        if (launcher_->Phase() && i == launcher_->PendingTrack()) {   // trk2als: un sonido lanzado: cargando / esperando el 1
            int pm = launcher_->Permille();
            int bw = COLW - GAP - 8;
            FillRect(x + 4, HY + HH - 2, bw, 2, Mix(bg, fg, gridPct(lightBg, 25)));
            if (bw * pm / 1000 > 0) FillRect(x + 4, HY + HH - 2, bw * pm / 1000, 2, hi2);
        }
    }

    // (las escenas no llevan cabecera: se entiende solo)

    // escenas y celdas
    for (int j = 0; j < rows; j++) {
        int y = GY + j * ROWH;
        int h = ROWH - GAP;
        int row = j + offset;
        bool hereRow = rowMode_ && j == viewData_->songY_;
        // en la columna de escenas: la fila que se va a lanzar, un pelo más
        // clara de lado a lado (como la pista en vertical)
        if (hereRow && trackCount > 0)
            FillRect(SX - 4, y - 2, XEND + 4 - (SX - 4), h + 4, Mix(bg, fg, gridPct(lightBg, 5)), 4);
        {   // la escena: ▶ número y sus repeticiones (x2…; ∞ = se queda ahí)
            bool has = false, queued = false, stopAll = rowPlaying[j];
            int fNext = -1, fLeft = 0;
            bool following = player->SceneFollow(fNext, fLeft) && fNext == row;
            if (following) queued = true;   // la que sigue (xN) también espera
            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                if (!project->TrackExists(i)) continue;
                if (viewData_->song_->data_[i + SONG_CHANNEL_COUNT * row] != 0xFF) has = true;
                QueueingMode q = player->GetQueueingMode(i);
                if (player->IsRunning() && player->GetQueuePosition(i) == row &&
                    (q == QM_PHRASESTART || q == QM_TICKSTART || q == QM_CHAINSTART)) queued = true;
                // la escena lanzada que detiene pistas (también la vacía): espera el 1
                if (player->IsRunning() && (q == QM_PHRASESTOP || q == QM_CHAINSTOP) && player->StopCell(i) == row)
                    queued = true;
                if (player->IsRunning() && player->IsChannelPlaying(i) && viewData_->currentPlayChain_[i] != 0xFF &&
                    viewData_->songPlayPos_[i] == row &&
                    ((q != QM_PHRASESTOP && q != QM_CHAINSTOP) || player->StopCell(i) >= 0))
                    stopAll = false;   // solo con Select + Start (sin celda que lo detenga) parpadea la fila que suena
            }
            FillRect(SX, y, SW, h, has ? Mix(bg, fg, gridPct(lightBg, 9)) : cellBg, 3);
            if (scnMarking_ && rowMode_) {   // escenas marcadas: toda la fila
                int top, bot;
                scnRange(top, bot);
                if (row >= top && row <= bot) FrameRect(SX - 3, y - 3, XEND + 3 - (SX - 3), h + 6, hi2, 4, 3);
            }
            if (hereRow) FrameRect(SX - 3, y - 3, SW + 6, h + 6, cur, 5, 3);   // el cursor por fuera, como en los clips
            GUIColor green = LinkGreen();
            // compacta, como un clip: el número arriba a la izquierda (a la
            // altura del de los clips), el ▶ en la esquina y las repeticiones abajo
            bool qb = following ? followBlink(fLeft) : queueBlink();
            if (queued && qb) FrameRect(SX, y, SW, h, hi1, 3, 3);   // el marco, como el de los clips
            if (stopAll && !queued && qb) FrameRect(SX, y, SW, h, fg, 3, 3);   // toda la fila se detiene
            int ts = 10, tx = SX + SW - 14, ty = y + 6;
            if (queued && !qb) {   // en espera del 1: parpadea entre lleno y hueco
                for (int k = 0; k < ts; k++) {
                    int hh = ts - (k / 2) * 2;
                    if (k < 2 || k >= ts - 2) FillRect(tx + k, ty + k / 2, 1, hh, green);
                    else { FillRect(tx + k, ty + k / 2, 1, 2, green); FillRect(tx + k, ty + k / 2 + hh - 2, 1, 2, green); }
                }
            } else {
                GUIColor tcol = (rowPlaying[j] || queued) ? green : (has ? fg : Mix(bg, fg, gridPct(lightBg, 25)));
                if (stopAll && !queued) FillRect(tx, ty, ts, ts, qb ? fg : Mix(bg, fg, 30), 1);   // ■ parpadeando
                else for (int k = 0; k < ts; k++) FillRect(tx + k, ty + k / 2, 1, ts - (k / 2) * 2, tcol);
            }
            char rl[4];
            snprintf(rl, sizeof(rl), "%02X", row);
            DrawText(SX + 5, y + 4, rl, has ? fg : dim, 1);
            int rep = viewData_->song_->sceneRep_[row];
            int cy = y + h - 19, ch = 15;
            if (rep > 0) {
                char rb[6];   // trk2als: grande y sin recuadro
                if (viewData_->song_->followOff_) strcpy(rb, "--");   // apagadas: guardadas, sin número
                else snprintf(rb, sizeof(rb), "x%d", rep);
                DrawText(SX + 5, cy, rb, dim, 1);   // siempre en gris: no se confunde con el número
            } else if (hereRow && has) {   // ∞ (se queda ahí): solo al editarla
                FrameRect(SX + 6, cy + 3, 10, 9, fg, 4, 2);
                FrameRect(SX + 15, cy + 3, 10, 9, fg, 4, 2);
            }
        }

        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (!project->TrackExists(i)) continue;
            int x = X0 + i * COLW;
            int w = COLW - GAP;
            int cell = i + SONG_CHANNEL_COUNT * row;
            unsigned char d = viewData_->song_->data_[cell];
            bool audio = isAudioColumn(i);
            bool empty = (d == 0xFF) && cell != newSlotCell_;
            bool muted = player->IsChannelMuted(i);
            int tci = project->GetTrackColor(i);
        GUIColor tc(trackColors[tci][0], trackColors[tci][1], trackColors[tci][2]);
            if (muted) tc = Mix(tc, bg, 55);
            // trk2als: se borró el clip que suena: sigue a la vista, apagado,
            // hasta que de verdad termina su vuelta (si no, el marco de
            // "sonando" quedaba sobre una celda vacía)
            if (empty && player->IsRunning() && player->IsChannelPlaying(i) &&
                viewData_->currentPlayChain_[i] != 0xFF && viewData_->songPlayPos_[i] == row) {
                d = viewData_->currentPlayChain_[i];
                empty = false;
                tc = Mix(tc, bg, 60);
            }
            bool lit = (i == curCol) || hereRow;
            if (lit) tc = Mix(tc, fg, 12);
            bool stopHere = empty && player->IsRunning() && player->GetQueueingMode(i) == QM_PHRASESTOP &&
                            player->StopCell(i) == row;
            if (stopHere) {   // trk2als: esta celda vacía va a detener la pista en el 1 (parpadea)
                FillRect(x, y, w, h, queueBlink() ? Mix(bg, fg, 88) : Mix(bg, fg, 30), 3);
                FillRect(x + w / 2 - 4, y + h / 2 - 4, 8, 8, bg, 1);   // el cuadrito de stop
            } else if (empty) {
                FillRect(x, y, w, h, lit ? Mix(bg, fg, gridPct(lightBg, 11)) : cellBg, 3);
                FillRect(x + w / 2 - 2, y + h / 2 - 2, 4, 4, Mix(bg, fg, gridPct(lightBg, 18)));
            } else {
                FillRect(x, y, w, h, tc, 3);
                // número grande y nombre chico
                char id[8];
                std::string name;
                if (cell == newSlotCell_) {
                    strcpy(id, "A+");
                } else if (audio) {
                    snprintf(id, sizeof(id), "%s", clipLabel(d).c_str());
                    I_Instrument *instr = project->GetInstrumentBank()->GetInstrument(d);
                    name = instr ? instr->GetName() : "";
                    size_t dot = name.rfind('.');
                    if (dot != std::string::npos) name = name.substr(0, dot);
                } else {
                    snprintf(id, sizeof(id), "%02X", d);
                    name = viewData_->song_->GetClipName(d);
                }
                DrawText(x + 5, y + 4, id, bg, 1);
                if (!name.empty() && h >= 34) {   // trk2als: el nombre también grande (completo arriba, en la barra)
                    int room = (w - 8) / 16;
                    if (TextLen(name) > room) name = name.substr(0, room);
                    DrawText(x + 5, y + h - 19, name.c_str(), bg, 1);
                }
            }
            // lo que suena: marco blanco; lo que está en cola (Live): marco que parpadea
            bool playingHere = player->IsRunning() && player->IsChannelPlaying(i) &&
                               viewData_->currentPlayChain_[i] != 0xFF &&
                               viewData_->songPlayPos_[i] == row;
            bool queuedHere = player->GetSequencerMode() == SM_LIVE && player->IsRunning() &&
                              player->GetQueueingMode(i) != QM_NONE &&
                              player->GetQueuePosition(i) == row &&
                              player->GetQueueingMode(i) != QM_PHRASESTOP &&
                              player->GetQueueingMode(i) != QM_CHAINSTOP;
            // marco claro pegado al borde (fino, no toca el texto del clip)
            bool stopping = playingHere && (player->GetQueueingMode(i) == QM_PHRASESTOP ||
                                            player->GetQueueingMode(i) == QM_CHAINSTOP);
            if (playingHere) {
                // sigue sonando hasta el 1 aunque se vaya a detener: se ve fijo (lo
                // que parpadea es lo que lo detiene: la celda vacía o la escena)
                (void)stopping;
                FrameRect(x, y, w, h, muted ? dim : fg, 3, 3);
                GUIColor pc = empty ? fg : bg;
                int tx = x + w - 15, ty = y + 6;
                for (int k = 0; k < 9; k++) FillRect(tx + k, ty + k / 2, 1, 10 - (k / 2) * 2, pc);   // el play
            }
            else if (queuedHere && queueBlink()) FrameRect(x, y, w, h, hi1, 3, 3);
            // la línea de abajo: cuánto lleva el loop (lo blanco es lo que falta)
            if (playingHere && !empty) {
                int total;
                int fr = playFraction(i, total);
                if (fr >= 0) {
                    int lw = w - 10;
                    int done = lw * fr / 1000;
                    // la barrita oscura es lo que falta: se va acortando
                    if (lw - done > 0) FillRect(x + 5 + done, y + h - 7, lw - done, 3, Mix(tc, bg, 45));
                }
            }
            // pegando: dónde quedaría
            if (sesPasting_) {
                int cx = viewData_->songX_, cy = viewData_->songY_ + offset;
                for (size_t k = 0; k < sesClip_.size(); k++)
                    if (cx + sesClip_[k].dx == i && cy + sesClip_[k].dy == row)
                        FrameRect(x - 2, y - 2, w + 4, h + 4, hi1, 4, 3);
            }
            // selección y cursor
            bool sel = clipboard_.active_ && i >= selRect.Left() && i <= selRect.Right() &&
                       row >= selRect.Top() && row <= selRect.Bottom();
            bool here = !clipboard_.active_ && !rowMode_ && !headerMode_ &&
                        i == viewData_->songX_ && j == viewData_->songY_;
            if (sel) FrameRect(x - 2, y - 2, w + 4, h + 4, hi2, 4, 3);
            if (here) FrameRect(x - 3, y - 3, w + 6, h + 6, cur, 5, 3);
        }
    }

    // abajo: el aviso de lo que acaba de pasar, en lugar de los medidores
    if (noticeOn()) {
        std::string m = displayNotification_;
        size_t nl = m.find('\n');
        if (nl != std::string::npos) m = m.substr(0, nl) + " " + m.substr(nl + 1);
        size_t room = (XEND - X0 - 8) / 16;
        if (m.size() > room) m = m.substr(0, room);
        DrawText(X0 + 4, VY + 8, m.c_str(), fg, 1);
    }

    // abajo: carga y tiempo, y un medidor por pista
    char lt[24];
    loadText(lt);
    char ld[8];   // la carga (lo que era "rg"), en %
    snprintf(ld, sizeof(ld), "%d%%", atoi(lt + 2));
    FillRect(SX, VY, SW, VH, panel, 3);
    // trk2als: solo la carga, en grande (el tiempo no hacía falta)
    {
        int load = atoi(lt + 2);
        if (load >= 100) snprintf(ld, sizeof(ld), "MAX");
        bool clip = player->Clipped();
        const char *txt = clip ? "CLP" : ld;
        int tw = 16 * int(strlen(txt));
        DrawText(SX + (SW - tw) / 2, VY + (VH - 16) / 2, txt,
                 clip ? ColorOf(CD_MUTE) : (load >= 80 ? ColorOf(CD_MUTE) : dim), 1);
    }
    for (int i = 0; i < SONG_CHANNEL_COUNT && i < trackCount && !noticeOn(); i++) {
        int x = X0 + i * COLW;
        int w = COLW - GAP;
        FillRect(x, VY, w, VH, panel, 3);
        int v = blockVu_[i];
        if (v > 0) {
            int tci = project->GetTrackColor(i);
        GUIColor tc(trackColors[tci][0], trackColors[tci][1], trackColors[tci][2]);
            int hh = (VH - 6) * v / 10;
            FillRect(x + 3, VY + VH - 3 - hh, w - 6, hh, v >= 10 ? ColorOf(CD_MUTE) : tc, 2);
        }
    }
}

/************************************************************
 trk2als: lanzar un sonido a la pista desde el navegador, como un clip:
 sus samples se leen por detrás (todo sigue andando) y el cambio entra en
 el 1 del compás siguiente (sin música: en cuanto está leído)
 ************************************************************/

bool SongView::Tick() { return launcher_->Tick(); }

void SongView::LaunchBefore(int, int track, bool firstOfVisit) {
    newTrack_ = false;   // ya tiene sonido: cerrar el navegador no la quita
    if (track < 0) return;
    if (firstOfVisit) beginSoundChange(track);   // un solo deshacer por visita al navegador
    else Player::GetInstance()->HoldTrack(track, true);
}

void SongView::LaunchAfter(int, int track, const std::string &name, bool ok) {
    if (track >= 0) endSoundChange(track);
    if (ok && !HasModal()) {
        View::SetNotification((SoundLauncher::DisplayName(name) + " cargado").c_str());
    }
    isDirty_ = true;
}

void SongView::LaunchPurged() {
    for (size_t i = 0; i < sesUndo_.size(); i++) sesUndo_[i].instrTrack = -1;
    for (size_t i = 0; i < sesRedo_.size(); i++) sesRedo_[i].instrTrack = -1;
}

void SongView::LaunchNotify(const char *msg) { View::SetNotification(msg); isDirty_ = true; }
