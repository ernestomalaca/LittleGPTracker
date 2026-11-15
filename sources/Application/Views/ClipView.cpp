#include "ClipView.h"
#include "Application/Player/Player.h"
#include "Application/Model/Project.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Model/Scale.h"
#include "System/System/System.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

// trk2als: piano roll del clip MIDI. Ver ClipView.h

#define NO_NOTE 0xFF
#define MAX_PITCH 119
#define ROWS 24           // filas de notas a la vista (dos octavas)
#define ROW_H 14
#define STEP_W 36
#define GRID_X 56
#define GRID_Y 54
#define VEL_Y 398         // carril de velocity
#define VEL_H 58

static bool byStart(const ClipView::Note &a, const ClipView::Note &b) {
    return a.start < b.start;
}

static bool isBlack(int pitch) {
    int k = pitch % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

ClipView::ClipView(GUIWindow &w, ViewData *viewData) : View(w, viewData) {
    cursorStep_ = 0;
    cursorPitch_ = 60;
    topPitch_ = 71;
    lastLen_ = 1;
    lastVel_ = 100;
    aUsed_ = xUsed_ = bUsed_ = false;
    auditioning_ = false;
    playStep_ = -1;
    copyStep_ = copyPitch_ = 0;
    marking_ = false;
    markLevel_ = 1;
    anchorStep_ = anchorPitch_ = 0;
    aPending_ = bPending_ = false;
    msgTime_ = 0;
    chainShown_ = -1;
}

ClipView::~ClipView() {}

/************************************************************
 modelo
 ************************************************************/

int ClipView::pages() {
    unsigned char *row = viewData_->song_->chain_->data_ + 16 * viewData_->currentChain_;
    int n = 0;
    while (n < 16 && row[n] != 0xFF) n++;
    return n;
}

unsigned char *ClipView::stepPtr(int g, int &phrase, int &pos) {
    unsigned char *row = viewData_->song_->chain_->data_ + 16 * viewData_->currentChain_;
    phrase = row[g / 16];
    pos = g % 16;
    return viewData_->song_->phrase_->note_ + 16 * phrase + pos;
}

void ClipView::readNotes(std::vector<Note> &out) {
    out.clear();
    Phrase *ph = viewData_->song_->phrase_;
    int t = total();
    for (int g = 0; g < t; g++) {
        int p, pos;
        stepPtr(g, p, pos);
        int i = 16 * p + pos;
        bool kill = (ph->cmd1_[i] == I_CMD_KILL) || (ph->cmd2_[i] == I_CMD_KILL);
        if (ph->note_[i] != NO_NOTE) {
            if (!out.empty() && out.back().len < 0) out.back().len = g - out.back().start;
            Note n;
            n.start = g;
            n.len = -1;
            n.pitch = ph->note_[i];
            n.instr = ph->instr_[i];
            n.vel = 100;
            if (ph->cmd1_[i] == I_CMD_MVEL) n.vel = (ph->param1_[i] & 0xFF) / 2;
            if (ph->cmd2_[i] == I_CMD_MVEL) n.vel = (ph->param2_[i] & 0xFF) / 2;
            if (n.vel < 1) n.vel = 1;
            out.push_back(n);
        } else if (kill) {
            if (!out.empty() && out.back().len < 0) out.back().len = g - out.back().start;
        }
    }
    if (!out.empty() && out.back().len < 0) out.back().len = t - out.back().start;
}

void ClipView::writeNotes(std::vector<Note> &notes) {
    int t = total();
    std::stable_sort(notes.begin(), notes.end(), byStart);
    // monofónico: cada nota termina a más tardar donde empieza la siguiente
    std::vector<Note> ok;
    for (size_t i = 0; i < notes.size(); i++) {
        Note n = notes[i];
        if (n.start < 0 || n.start >= t) continue;
        if (!ok.empty() && ok.back().start == n.start) ok.pop_back();   // la nueva gana
        ok.push_back(n);
    }
    for (size_t i = 0; i < ok.size(); i++) {
        int next = (i + 1 < ok.size()) ? ok[i + 1].start : t;
        if (ok[i].start + ok[i].len > next) ok[i].len = next - ok[i].start;
        if (ok[i].len < 1) ok[i].len = 1;
    }
    Phrase *ph = viewData_->song_->phrase_;
    // borrar notas, KILL y MVEL del clip (los demás comandos se quedan)
    for (int g = 0; g < t; g++) {
        int p, pos;
        stepPtr(g, p, pos);
        int i = 16 * p + pos;
        ph->note_[i] = NO_NOTE;
        ph->instr_[i] = 0xFF;
        if (ph->cmd1_[i] == I_CMD_KILL || ph->cmd1_[i] == I_CMD_MVEL) {
            ph->cmd1_[i] = I_CMD_NONE;
            ph->param1_[i] = 0;
        }
        if (ph->cmd2_[i] == I_CMD_KILL || ph->cmd2_[i] == I_CMD_MVEL) {
            ph->cmd2_[i] = I_CMD_NONE;
            ph->param2_[i] = 0;
        }
    }
    for (size_t k = 0; k < ok.size(); k++) {
        const Note &n = ok[k];
        int p, pos;
        stepPtr(n.start, p, pos);
        int i = 16 * p + pos;
        ph->note_[i] = (unsigned char)n.pitch;
        ph->instr_[i] = (unsigned char)n.instr;
        if (n.vel != 100) {
            ushort v = (ushort)(n.vel * 2);
            if (ph->cmd1_[i] == I_CMD_NONE) { ph->cmd1_[i] = I_CMD_MVEL; ph->param1_[i] = v; }
            else if (ph->cmd2_[i] == I_CMD_NONE) { ph->cmd2_[i] = I_CMD_MVEL; ph->param2_[i] = v; }
        }
        int end = n.start + n.len;
        int next = (k + 1 < ok.size()) ? ok[k + 1].start : t;
        if (end < next) {
            stepPtr(end, p, pos);
            int j = 16 * p + pos;
            if (ph->cmd1_[j] == I_CMD_NONE) { ph->cmd1_[j] = I_CMD_KILL; ph->param1_[j] = 0; }
            else if (ph->cmd2_[j] == I_CMD_NONE) { ph->cmd2_[j] = I_CMD_KILL; ph->param2_[j] = 0; }
        }
    }
    notes = ok;
}

int ClipView::noteAt(std::vector<Note> &notes, int step) {
    for (size_t i = 0; i < notes.size(); i++) {
        if (step >= notes[i].start && step < notes[i].start + notes[i].len) return int(i);
    }
    return -1;
}

int ClipView::noteStarting(std::vector<Note> &notes, int step) {
    for (size_t i = 0; i < notes.size(); i++) {
        if (notes[i].start == step) return int(i);
    }
    return -1;
}

bool ClipView::addPage() {
    int n = pages();
    if (n >= 16) {
        View::SetNotification("El clip ya tiene 16 páginas");
        return false;
    }
    unsigned short phrase = viewData_->song_->phrase_->GetNext();
    if (phrase == NO_MORE_PHRASE) {
        View::SetNotification("No quedan phrases libres");
        return false;
    }
    Phrase *ph = viewData_->song_->phrase_;
    ph->SetUsed((unsigned char)phrase);
    for (int s = 0; s < 16; s++) {
        int i = 16 * phrase + s;
        ph->note_[i] = NO_NOTE;
        ph->instr_[i] = 0xFF;
        ph->cmd1_[i] = ph->cmd2_[i] = I_CMD_NONE;
        ph->param1_[i] = ph->param2_[i] = 0;
    }
    unsigned char *row = viewData_->song_->chain_->data_ + 16 * viewData_->currentChain_;
    row[n] = (unsigned char)phrase;
    viewData_->song_->chain_->transpose_[16 * viewData_->currentChain_ + n] = 0;
    viewData_->song_->chain_->SetUsed((unsigned char)viewData_->currentChain_);
    return true;
}

/************************************************************
 edición
 ************************************************************/

void ClipView::putNote() {
    if (pages() == 0 && !addPage()) return;
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i >= 0 && notes[i].pitch == cursorPitch_) {   // ya está: la elige
        lastLen_ = notes[i].len;
        lastVel_ = notes[i].vel;
        audition(notes[i].start);
        return;
    }
    if (i >= 0 && notes[i].start < cursorStep_) {     // la parte en dos
        notes[i].len = cursorStep_ - notes[i].start;
    }
    int instr = 0xFF;
    if (viewData_->project_->GetTrackInstrument(viewData_->songX_) < 0) {
        // pista sin dueño: el instrumento de la nota anterior
        for (size_t k = 0; k < notes.size(); k++)
            if (notes[k].start <= cursorStep_ && notes[k].instr != 0xFF) instr = notes[k].instr;
        if (instr == 0xFF) instr = viewData_->currentInstrument_;
    }
    Note n;
    n.start = cursorStep_;
    n.len = lastLen_;
    n.pitch = cursorPitch_;
    n.vel = lastVel_;
    n.instr = instr;
    notes.push_back(n);
    writeNotes(notes);
    audition(cursorStep_);
}

void ClipView::cutNote() {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i < 0) { message("Nada que cortar"); return; }
    clipboard_.clear();
    clipboard_.push_back(notes[i]);
    copyStep_ = notes[i].start;
    copyPitch_ = notes[i].pitch;
    // la nota de antes no se alarga: termina donde empezaba esta
    notes.erase(notes.begin() + i);
    writeNotes(notes);
    message("Nota cortada");
}

// mete notas nuevas: las que empiezan donde ya había una la reemplazan y la
// que sonaba encima se corta donde empieza la nueva
void ClipView::placeNotes(std::vector<Note> &notes, std::vector<Note> &add) {
    for (size_t k = 0; k < add.size(); k++) {
        const Note &n = add[k];
        for (size_t i = 0; i < notes.size(); i++) {
            if (notes[i].start < n.start && notes[i].start + notes[i].len > n.start)
                notes[i].len = n.start - notes[i].start;
        }
        for (size_t i = 0; i < notes.size();) {
            if (notes[i].start == n.start) notes.erase(notes.begin() + i);
            else i++;
        }
        notes.push_back(n);
    }
    writeNotes(notes);
}

void ClipView::pasteNote() {
    if (clipboard_.empty()) { message("Nada que pegar"); return; }
    std::vector<Note> add;
    for (size_t k = 0; k < clipboard_.size(); k++) {
        Note n = clipboard_[k];
        n.start = cursorStep_ + (n.start - copyStep_);
        n.pitch = shiftPitch(n.pitch, copyPitch_, cursorPitch_);
        if (n.start < 0 || n.pitch < 0 || n.pitch > MAX_PITCH) continue;
        add.push_back(n);
    }
    int last = 0;
    for (size_t k = 0; k < add.size(); k++) last = std::max(last, add[k].start + 1);
    while (total() < last && addPage()) {}
    std::vector<Note> notes;
    readNotes(notes);
    placeNotes(notes, add);
    message(add.size() == 1 ? "Nota pegada" : "%d notas pegadas", (int)add.size());
}

/************************************************************
 bloques
 ************************************************************/

void ClipView::selRect(int &s0, int &s1, int &p0, int &p1) {
    s0 = std::min(anchorStep_, cursorStep_);
    s1 = std::max(anchorStep_, cursorStep_);
    p0 = std::min(anchorPitch_, cursorPitch_);
    p1 = std::max(anchorPitch_, cursorPitch_);
    if (markLevel_ >= 2) { p0 = 0; p1 = MAX_PITCH; }
    if (markLevel_ >= 3) { s0 = 0; s1 = std::max(0, total() - 1); }
}

void ClipView::selectedNotes(std::vector<Note> &notes, std::vector<int> &idx) {
    readNotes(notes);
    idx.clear();
    int s0, s1, p0, p1;
    selRect(s0, s1, p0, p1);
    for (size_t i = 0; i < notes.size(); i++) {
        if (notes[i].start >= s0 && notes[i].start <= s1 && notes[i].pitch >= p0 &&
            notes[i].pitch <= p1)
            idx.push_back(int(i));
    }
}

void ClipView::copySelection(bool cut) {
    std::vector<Note> notes;
    std::vector<int> idx;
    selectedNotes(notes, idx);
    marking_ = false;
    if (idx.empty()) { message("Nada marcado"); return; }
    clipboard_.clear();
    for (size_t k = 0; k < idx.size(); k++) clipboard_.push_back(notes[idx[k]]);
    copyStep_ = cursorStep_;
    copyPitch_ = cursorPitch_;
    if (cut) {
        for (int k = int(idx.size()) - 1; k >= 0; k--) notes.erase(notes.begin() + idx[k]);
        writeNotes(notes);
    }
    int n = (int)idx.size();
    if (cut) message(n == 1 ? "Nota cortada" : "%d notas cortadas", n);
    else message(n == 1 ? "Nota copiada" : "%d notas copiadas", n);
}

// A + flechas con un bloque marcado: lo mueve (dir = semitono o grado de la escala)
void ClipView::moveSelection(int dt, int dir, int octave) {
    std::vector<Note> notes;
    std::vector<int> idx;
    selectedNotes(notes, idx);
    if (idx.empty()) { message("Nada marcado"); return; }
    std::vector<Note> moved;
    for (size_t k = 0; k < idx.size(); k++) {
        Note n = notes[idx[k]];
        n.start += dt;
        if (dir) n.pitch = stepPitch(n.pitch, dir);
        n.pitch += 12 * octave;
        if (n.start < 0 || n.start >= total() || n.pitch < 0 || n.pitch > MAX_PITCH) return;
        moved.push_back(n);
    }
    for (int k = int(idx.size()) - 1; k >= 0; k--) notes.erase(notes.begin() + idx[k]);
    placeNotes(notes, moved);
    // la marca y el cursor se van con el bloque
    anchorStep_ += dt;
    cursorStep_ += dt;
    if (markLevel_ < 2) {
        int a = anchorPitch_, c = cursorPitch_;
        if (dir) { a = stepPitch(a, dir); c = stepPitch(c, dir); }
        a += 12 * octave;
        c += 12 * octave;
        if (a >= 0 && a <= MAX_PITCH && c >= 0 && c <= MAX_PITCH) {
            anchorPitch_ = a;
            cursorPitch_ = c;
        }
    }
    keepVisible();
}

// Select, B, A: lo marcado otra vez, justo después (y la marca se va a la copia)
void ClipView::duplicateSelection() {
    std::vector<Note> notes;
    std::vector<int> idx;
    selectedNotes(notes, idx);
    if (idx.empty()) { message("Nada marcado"); return; }
    int s0, s1, p0, p1;
    selRect(s0, s1, p0, p1);
    int end = s1 + 1;
    for (size_t k = 0; k < idx.size(); k++)
        end = std::max(end, notes[idx[k]].start + notes[idx[k]].len);
    int width = end - s0;
    std::vector<Note> add;
    for (size_t k = 0; k < idx.size(); k++) {
        Note n = notes[idx[k]];
        n.start += width;
        add.push_back(n);
    }
    int last = 0;
    for (size_t k = 0; k < add.size(); k++) last = std::max(last, add[k].start + 1);
    while (total() < last && addPage()) {}
    readNotes(notes);
    for (size_t k = 0; k < add.size();) {
        if (add[k].start >= total()) add.erase(add.begin() + k);
        else k++;
    }
    placeNotes(notes, add);
    if (markLevel_ >= 3) markLevel_ = 2;   // la copia, no todo el clip otra vez
    anchorStep_ = s0 + width;
    cursorStep_ = s1 + width;
    if (cursorStep_ >= total()) cursorStep_ = total() - 1;
    int n = (int)add.size();
    message(n == 1 ? "Nota duplicada" : "%d notas duplicadas", n);
}

/************************************************************
 escala
 ************************************************************/

int ClipView::stepPitch(int pitch, int dir) {
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    int p = pitch + dir;
    while (p >= 0 && p <= MAX_PITCH && !InScale(scale, root, p)) p += dir;
    if (p < 0 || p > MAX_PITCH) return pitch;
    return p;
}

int ClipView::degree(int pitch) {   // cuántas notas de la escala hay debajo
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    int d = 0;
    for (int p = 0; p < pitch; p++)
        if (InScale(scale, root, p)) d++;
    return d;
}

int ClipView::fromDegree(int d) {
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    for (int p = 0; p <= MAX_PITCH; p++) {
        if (InScale(scale, root, p)) {
            if (d == 0) return p;
            d--;
        }
    }
    return -1;
}

// pegar en otra altura: dentro de la escala se mueve por grados (Do-Mi-Sol
// pegado en Re queda Re-Fa-La)
int ClipView::shiftPitch(int pitch, int fromRef, int toRef) {
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    if (scale == 0 || !InScale(scale, root, pitch) || !InScale(scale, root, fromRef) ||
        !InScale(scale, root, toRef))
        return pitch + (toRef - fromRef);
    return fromDegree(degree(pitch) + degree(toRef) - degree(fromRef));
}

void ClipView::message(const char *fmt, int n) {
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, n);
    msg_ = buf;
    msgTime_ = System::GetInstance()->GetClock();
    isDirty_ = true;
}

void ClipView::AnimationUpdate() {
    if (!msg_.empty() && System::GetInstance()->GetClock() - msgTime_ > 1500) {
        msg_.clear();
        DrawView();
    }
}

void ClipView::moveNote(int dt, int dp) {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i < 0) return;
    Note &n = notes[i];
    if (dp) {   // un semitono, o un grado si hay escala (dp = ±12: octava)
        int p = (dp == 1 || dp == -1) ? stepPitch(n.pitch, dp) : n.pitch + dp;
        if (p < 0 || p > MAX_PITCH) return;
        n.pitch = p;
        cursorPitch_ = p;
    }
    if (dt) {
        int s = n.start + dt;
        if (s < 0 || s >= total()) return;
        int j = noteStarting(notes, s);
        if (j >= 0 && j != i) return;   // ahí empieza otra
        // la de antes no se alarga al irse esta
        if (dt > 0 && i > 0) {
            Note &prev = notes[i - 1];
            if (prev.start + prev.len > n.start) prev.len = n.start - prev.start;
        }
        if (dt < 0 && i > 0) {
            Note &prev = notes[i - 1];
            if (prev.start + prev.len > s) prev.len = s - prev.start;
        }
        n.start = s;
        cursorStep_ += dt;
    }
    writeNotes(notes);
    keepVisible();
    if (dp) audition(cursorStep_);
}

void ClipView::changeLength(int d) {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i < 0) return;
    int next = (i + 1 < (int)notes.size()) ? notes[i + 1].start : total();
    int len = notes[i].len + d;
    if (len < 1) len = 1;
    if (notes[i].start + len > next) len = next - notes[i].start;
    notes[i].len = len;
    lastLen_ = len;
    cursorStep_ = notes[i].start;   // al editarla, el cursor salta al inicio de la nota
    writeNotes(notes);
}

void ClipView::changeVelocity(int d) {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i < 0) return;
    int v = notes[i].vel + d;
    if (v < 1) v = 1;
    if (v > 127) v = 127;
    notes[i].vel = v;
    lastVel_ = v;
    cursorStep_ = notes[i].start;
    writeNotes(notes);
}

void ClipView::moveCursor(int dt, int dp) {
    int t = total();
    if (t < 16) t = 16;
    cursorStep_ += dt;
    if (cursorStep_ < 0) cursorStep_ = 0;
    if (cursorStep_ >= t) cursorStep_ = t - 1;
    if (dp == 1 || dp == -1) cursorPitch_ = stepPitch(cursorPitch_, dp);   // con escala: solo sus notas
    else cursorPitch_ += dp;
    if (cursorPitch_ < 0) cursorPitch_ = 0;
    if (cursorPitch_ > MAX_PITCH) cursorPitch_ = MAX_PITCH;
    keepVisible();
}

void ClipView::keepVisible() {
    if (cursorPitch_ > topPitch_) topPitch_ = cursorPitch_;
    if (cursorPitch_ < topPitch_ - ROWS + 1) topPitch_ = cursorPitch_ + ROWS - 1;
    if (topPitch_ > MAX_PITCH) topPitch_ = MAX_PITCH;
    if (topPitch_ < ROWS - 1) topPitch_ = ROWS - 1;
}

void ClipView::audition(int step) {
    Player *player = Player::GetInstance();
    if (player->IsRunning() && viewData_->playMode_ != PM_AUDITION) return;
    if (step < 0 || step >= total()) return;
    int p, pos;
    stepPtr(step, p, pos);
    viewData_->currentPhrase_ = p;
    viewData_->chainRow_ = step / 16;
    viewData_->phraseCurPos_ = pos;
    if (player->IsRunning()) player->Stop();
    player->OnStartButton(PM_AUDITION, viewData_->songX_, false, viewData_->chainRow_);
    auditioning_ = true;
}

void ClipView::stopAudition() {
    if (!auditioning_) return;
    auditioning_ = false;
    Player *player = Player::GetInstance();
    if (viewData_->playMode_ == PM_AUDITION && player->IsRunning()) player->Stop();
}

void ClipView::switchView(ViewType vt) {
    stopAudition();
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
}

std::string ClipView::noteName(int pitch) {
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char buf[8];
    sprintf(buf, "%s%d", names[pitch % 12], pitch / 12 - 2);
    return buf;
}

void ClipView::OnFocus() {
    if (chainShown_ != viewData_->currentChain_) {
        chainShown_ = viewData_->currentChain_;
        cursorStep_ = 0;
        cursorPitch_ = 60;
        std::vector<Note> notes;
        readNotes(notes);
        if (!notes.empty()) cursorPitch_ = notes[0].pitch;
        topPitch_ = cursorPitch_ + ROWS / 2;
        keepVisible();
    }
    if (cursorStep_ >= total() && total() > 0) cursorStep_ = total() - 1;
    playStep_ = -1;
}

/************************************************************
 botones (tal cual, sin traducir: ver AppWindow::joyButton)
 ************************************************************/

void ClipView::ProcessButtonMask(unsigned short mask, bool pressed) {
    unsigned short v = View::pressedButton_;
    if (!pressed) {
        if (!(mask & EPBM_A)) {
            stopAudition();
            if (aPending_ && marking_) {   // A sola con algo marcado: cancela
                marking_ = false;
                message("Marca cancelada");
            }
            aPending_ = false;
        }
        if (!(mask & EPBM_B)) {
            if (bPending_ && marking_ && !(mask & EPBM_SELECT)) copySelection(false);   // B sola: copia
            bPending_ = false;
        }
        isDirty_ = true;
        return;
    }
    int dt = (v == EPBM_LEFT) ? -1 : (v == EPBM_RIGHT) ? 1 : 0;
    int dp = (v == EPBM_UP) ? 1 : (v == EPBM_DOWN) ? -1 : 0;
    Player *player = Player::GetInstance();
    if (v != EPBM_A) aPending_ = false;
    if (v != EPBM_B) bPending_ = false;

    if (mask & EPBM_SELECT) {
        if (v == EPBM_B) {   // Select, B: marcar; otra B: pasos completos; otra: todo el clip
            if (!marking_) {
                marking_ = true;
                markLevel_ = 1;
                anchorStep_ = cursorStep_;
                anchorPitch_ = cursorPitch_;
                message("Marcando: flechas para agrandar");
            } else if (markLevel_ < 3) {
                markLevel_++;
                message(markLevel_ == 2 ? "Marcando pasos completos" : "Marcado todo el clip");
            }
        } else if (v == EPBM_A) {
            if (marking_) duplicateSelection();   // Select, B, A
            else pasteNote();
        } else if (v == EPBM_START) {   // todo: la Song desde la fila de este clip
            stopAudition();
            if (player->IsRunning()) player->Stop();
            else player->OnSongStartButton(0, SONG_CHANNEL_COUNT - 1, false, false);
        } else if (v == EPBM_LEFT) {
            marking_ = false;
            switchView(VT_SONG);
        } else if (v == EPBM_RIGHT) {
            marking_ = false;
            int owner = viewData_->project_->GetTrackInstrument(viewData_->songX_);
            if (owner >= 0) viewData_->currentInstrument_ = owner;
            viewData_->instrumentFromSong_ = false;
            switchView(VT_INSTRUMENT);
        } else if (v == EPBM_DOWN && pages() > 0) {   // la phrase de esta página, a la antigua
            marking_ = false;
            int p, pos;
            stepPtr(cursorStep_, p, pos);
            viewData_->currentPhrase_ = p;
            viewData_->chainRow_ = cursorStep_ / 16;
            switchView(VT_PHRASE);
        }
        isDirty_ = true;
        return;
    }
    if (v == EPBM_START) {
        stopAudition();
        if (player->IsRunning()) {
            player->Stop();
        } else if (pages() > 0) {
            viewData_->chainRow_ = 0;
            player->OnStartButton(PM_CHAIN, viewData_->songX_, false, 0);
        }
        isDirty_ = true;
        return;
    }

    if (marking_) {
        if (mask & EPBM_B) {
            if (v == EPBM_B) bPending_ = true;
            else if (v == EPBM_A) { bPending_ = false; copySelection(true); }   // B + A: corta
        } else if (mask & EPBM_A) {
            if (v == EPBM_A) aPending_ = true;
            else {
                aPending_ = false;
                if (dt) moveSelection(dt, 0, 0);
                else if (dp) moveSelection(0, dp, 0);
                else if (v == EPBM_L) moveSelection(0, 0, -1);
                else if (v == EPBM_R) moveSelection(0, 0, 1);
            }
        } else {   // la cruceta agranda la marca
            if (dt || dp) moveCursor(dt, dp);
            if (v == EPBM_L) moveCursor(0, -12);
            if (v == EPBM_R) moveCursor(0, 12);
        }
        isDirty_ = true;
        return;
    }

    if (mask & EPBM_X) {               // propiedades de la nota
        if (dt) changeLength(dt);
        if (dp) changeVelocity(dp * 8);
        isDirty_ = true;
        return;
    }
    if (mask & EPBM_B) {
        if (v == EPBM_A) cutNote();
        if (dt) {                      // de página en página
            if (dt > 0 && cursorStep_ / 16 + 1 >= pages()) addPage();
            moveCursor(dt * 16, 0);
        }
        isDirty_ = true;
        return;
    }
    if (mask & EPBM_A) {
        if (v == EPBM_A) putNote();
        else if (dt || dp) moveNote(dt, dp);
        else if (v == EPBM_L) moveNote(0, -12);
        else if (v == EPBM_R) moveNote(0, 12);
        isDirty_ = true;
        return;
    }
    if (dt && cursorStep_ < total()) {   // sobre una nota: brinca al terminar (o a su inicio)
        std::vector<Note> notes;
        readNotes(notes);
        int i = noteAt(notes, cursorStep_);
        if (i >= 0 && notes[i].pitch == cursorPitch_) {
            if (dt > 0) dt = notes[i].start + notes[i].len - cursorStep_;
            else if (cursorStep_ > notes[i].start) dt = notes[i].start - cursorStep_;
        }
    }
    if (dt || dp) moveCursor(dt, dp);
    if (v == EPBM_L) moveCursor(0, -12);
    if (v == EPBM_R) moveCursor(0, 12);
    isDirty_ = true;
}

/************************************************************
 dibujo
 ************************************************************/

void ClipView::DrawView() {
    Clear();
    ClearShapes();

    GUIColor bg = ColorOf(CD_BACKGROUND);
    GUIColor fg = ColorOf(CD_NORMAL);
    GUIColor hi = ColorOf(CD_HILITE1);
    GUIColor hi2 = ColorOf(CD_HILITE2);
    GUIColor dim = Mix(bg, fg, 55);
    GUIColor play = ColorOf(CD_PLAY);

    std::vector<Note> notes;
    readNotes(notes);
    int npages = pages();
    int page = cursorStep_ / 16;
    int p0 = page * 16;
    int cur = noteAt(notes, cursorStep_);
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();

    // barra de arriba
    const std::string &cn = viewData_->song_->GetChainName(viewData_->currentChain_);
    char cname[16];
    sprintf(cname, "Clip %2.2X", viewData_->currentChain_);
    std::string ctx = std::string(viewData_->project_->GetTrackName(viewData_->songX_)) + " > " +
                      (cn.empty() ? std::string(cname) : cn);
    drawTopBar(TAB_CLIP, ctx.c_str());

    // encabezado: página, nota del cursor y lo de la nota elegida
    {
        char info[96];
        std::string nn = noteName(cursorPitch_);
        if (cur >= 0) {
            sprintf(info, "pag %d/%d   paso %d   %s   vel %d   largo %d", page + 1,
                    npages > 0 ? npages : 1, cursorStep_ % 16 + 1, noteName(notes[cur].pitch).c_str(),
                    notes[cur].vel, notes[cur].len);
        } else {
            sprintf(info, "pag %d/%d   paso %d   %s", page + 1, npages > 0 ? npages : 1,
                    cursorStep_ % 16 + 1, nn.c_str());
        }
        if (!msg_.empty()) {   // el aviso de lo que acaba de pasar
            int w = 8 * int(msg_.size()) + 12;
            FillRect(6, 31, w, 18, Mix(bg, hi2, 25), 4);
            DrawText(12, 34, msg_.c_str(), hi2, 0);
        } else {
            DrawText(10, 34, info, fg, 0);
            if (scale > 0) {   // la escala, como en Live
                std::string sc = std::string(scaleNotes[root]) + " " + scaleNames[scale];
                if (sc.size() > 16) sc = sc.substr(0, 16);
                DrawText(466 - 8 * int(sc.size()), 34, sc.c_str(), hi, 0);
            }
        }
        // las páginas, a la derecha
        int bx = 632 - 16 * 10;
        for (int i = 0; i < 16; i++) {
            int x = bx + i * 10;
            if (i < npages) {
                bool playing = playStep_ >= 0 && playStep_ / 16 == i;
                GUIColor c = (i == page) ? hi2 : Mix(bg, fg, 35);
                FillRect(x, 36, 7, 8, c, 2);
                if (playing) FillRect(x, 46, 7, 2, play);
            } else {
                FillRect(x + 3, 39, 2, 2, Mix(bg, fg, 20));
            }
        }
    }

    // la rejilla
    const int gw = 16 * STEP_W;
    for (int r = 0; r < ROWS; r++) {
        int pitch = topPitch_ - r;
        int y = GRID_Y + r * ROW_H;
        bool black = isBlack(pitch);
        bool in = InScale(scale, root, pitch);
        bool isRoot = scale > 0 && (pitch - root) % 12 == 0;
        // con escala: sus filas claras, las demás casi negras (como Live)
        int shade = scale > 0 ? (in ? 9 : 1) : (black ? 3 : 8);
        FillRect(GRID_X, y, gw, ROW_H, Mix(bg, fg, shade));
        if ((scale > 0 ? isRoot : pitch % 12 == 0))
            FillRect(GRID_X, y + ROW_H - 1, gw, 1, Mix(bg, fg, 22));
        // teclado
        GUIColor key = black ? Mix(bg, fg, 22) : Mix(bg, fg, 70);
        if (scale > 0 && !in) key = Mix(bg, fg, black ? 10 : 25);
        if (isRoot) key = Mix(bg, hi, 75);
        if (pitch == cursorPitch_) key = hi2;
        FillRect(8, y + 1, 44, ROW_H - 2, key, 2);
        if (scale > 0 ? isRoot : pitch % 12 == 0)
            DrawText(12, y + 1, noteName(pitch).c_str(), bg, 0);
    }
    for (int s = 0; s <= 16; s++) {
        GUIColor c = (s % 4 == 0) ? Mix(bg, fg, 28) : Mix(bg, fg, 13);
        FillRect(GRID_X + s * STEP_W - (s == 16 ? 1 : 0), GRID_Y, 1, ROWS * ROW_H, c);
    }
    // más allá del final del clip
    int t = total();
    if (t < p0 + 16) {
        int from = t > p0 ? t - p0 : 0;
        FillRect(GRID_X + from * STEP_W, GRID_Y, (16 - from) * STEP_W, ROWS * ROW_H, bg);
    }

    // lo marcado
    if (marking_) {
        int s0, s1, q0, q1;
        selRect(s0, s1, q0, q1);
        int a = std::max(s0, p0), b = std::min(s1, p0 + 15);
        int r0 = std::max(0, topPitch_ - q1), r1 = std::min(ROWS - 1, topPitch_ - q0);
        if (a <= b && r0 <= r1) {
            int x = GRID_X + (a - p0) * STEP_W, y = GRID_Y + r0 * ROW_H;
            int w = (b - a + 1) * STEP_W, h = (r1 - r0 + 1) * ROW_H;
            FillRect(x, y, w, h, Mix(bg, hi, 22));
            FrameRect(x, y, w, h, hi, 2, 2);
        }
    }

    // las notas
    for (size_t i = 0; i < notes.size(); i++) {
        const Note &n = notes[i];
        int s0 = n.start - p0, s1 = n.start + n.len - p0;
        if (s1 <= 0 || s0 >= 16) continue;
        if (s0 < 0) s0 = 0;
        if (s1 > 16) s1 = 16;
        GUIColor c = Mix(bg, hi2, 35 + 65 * n.vel / 127);
        int r = topPitch_ - n.pitch;
        if (r < 0 || r >= ROWS) {        // fuera de la vista: una rayita en el borde
            int y = r < 0 ? GRID_Y - 3 : GRID_Y + ROWS * ROW_H + 1;
            FillRect(GRID_X + s0 * STEP_W + 2, y, (s1 - s0) * STEP_W - 4, 2, c);
            continue;
        }
        int x = GRID_X + s0 * STEP_W + 1, y = GRID_Y + r * ROW_H + 1;
        int w = (s1 - s0) * STEP_W - 2;
        FillRect(x, y, w, ROW_H - 2, c, 3);
        if (n.start >= p0) FillRect(x, y, 3, ROW_H - 2, Mix(c, fg, 40), 1);   // dónde empieza
        if ((int)i == cur) FrameRect(x - 1, y - 1, w + 2, ROW_H, fg, 3, 2);
    }

    // el cursor: una nota fantasma
    if (cur < 0 || notes[cur].pitch != cursorPitch_) {
        int r = topPitch_ - cursorPitch_;
        int x = GRID_X + (cursorStep_ - p0) * STEP_W + 1, y = GRID_Y + r * ROW_H + 1;
        FillRect(x, y, STEP_W - 2, ROW_H - 2, Mix(bg, fg, 30), 3);
        FrameRect(x - 1, y - 1, STEP_W, ROW_H, fg, 3, 1);
    }

    // el cabezal
    if (playStep_ >= p0 && playStep_ < p0 + 16) {
        FillRect(GRID_X + (playStep_ - p0) * STEP_W, GRID_Y - 2, 2, ROWS * ROW_H + 4, play);
    }

    // carril de velocity
    FillRect(GRID_X, VEL_Y, gw, VEL_H, Mix(bg, fg, 5), 4);
    DrawText(12, VEL_Y + VEL_H / 2 - 6, "vel", dim, 0);
    for (size_t i = 0; i < notes.size(); i++) {
        const Note &n = notes[i];
        int s = n.start - p0;
        if (s < 0 || s >= 16) continue;
        int h = (VEL_H - 8) * n.vel / 127;
        if (h < 2) h = 2;
        GUIColor c = ((int)i == cur) ? hi2 : Mix(bg, hi, 40 + 60 * n.vel / 127);
        FillRect(GRID_X + s * STEP_W + 10, VEL_Y + VEL_H - 4 - h, STEP_W - 20, h, c, 2);
    }

    if (marking_)
        drawHelpBar("flechas:marcar;B:copiar;B+A:cortar;A+<>:mover;SEL+A:duplicar;A:salir");
    else
        drawHelpBar("A:nota;X+flechas:largo/vel;B+A:cortar;SEL+A:pegar;SEL,B:marcar");
}

void ClipView::OnPlayerUpdate(PlayerEventType, unsigned int) {
    Player *player = Player::GetInstance();
    int ch = viewData_->songX_;
    int step = -1;
    if (player->IsRunning() && viewData_->playMode_ != PM_AUDITION &&
        viewData_->currentPlayChain_[ch] == viewData_->currentChain_) {
        step = viewData_->chainPlayPos_[ch] * 16 + viewData_->phrasePlayPos_[ch];
    }
    if (step != playStep_) {
        playStep_ = step;
        DrawView();
    } else {
        refreshTopBar();
    }
}
