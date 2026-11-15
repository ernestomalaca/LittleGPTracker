#include "ClipView.h"
#include "Application/Player/Player.h"
#include "Application/Model/Project.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Model/Scale.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "System/System/System.h"
#include <algorithm>
#include <math.h>
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
    pasting_ = false;
    stolen_ = false;
    lastLoopL_ = 0;
    baPending_ = false;
    prevHas_ = false;
    prevS_ = prevE_ = 0;
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

// trk2als: polifonía. Cada pista tiene 4 voces: la 0 es la columna de notas
// de la phrase (con KILL y MVEL en sus comandos) y las 1..3 son las capas
// (nota, velocity y corte). Aquí se leen todas como una lista de notas.
void ClipView::readNotes(std::vector<Note> &out) {
    out.clear();
    Phrase *ph = viewData_->song_->phrase_;
    int t = total();
    for (int v = 0; v < VOICE_COUNT; v++) {
        int open = -1;   // índice en 'out' de la nota que suena en esta voz
        for (int g = 0; g < t; g++) {
            int p, pos;
            stepPtr(g, p, pos);
            int i = 16 * p + pos;
            unsigned char note;
            bool kill;
            int vel = 100, instr = 0xFF;
            if (v == 0) {
                note = ph->note_[i];
                kill = (ph->cmd1_[i] == I_CMD_KILL) || (ph->cmd2_[i] == I_CMD_KILL);
                instr = ph->instr_[i];
                if (ph->cmd1_[i] == I_CMD_MVEL) vel = (ph->param1_[i] & 0xFF) / 2;
                if (ph->cmd2_[i] == I_CMD_MVEL) vel = (ph->param2_[i] & 0xFF) / 2;
            } else {
                int vi = Phrase::VIdx(v, p, pos);
                note = ph->voiceNote_[vi];
                kill = (ph->voiceFlags_[vi] & VF_KILL) != 0;
                if (ph->voiceVel_[vi] != VVEL_DEFAULT) vel = ph->voiceVel_[vi];
            }
            if ((kill || note != NO_NOTE) && open >= 0) {
                out[open].len = g - out[open].start;
                open = -1;
            }
            if (note != NO_NOTE) {
                Note n;
                n.start = g;
                n.len = -1;
                n.pitch = note;
                n.instr = instr;
                n.vel = vel < 1 ? 1 : vel;
                n.tick = ph->Tick(v, p, pos);
                n.rat = ph->Ratchet(v, p, pos);
                n.ramp = ph->Ramp(v, p, pos);
                if (n.tick > 5) n.tick = 5;
                out.push_back(n);
                open = int(out.size()) - 1;
            }
        }
        if (open >= 0) out[open].len = t - out[open].start;
    }
    std::stable_sort(out.begin(), out.end(), byStart);
}

void ClipView::writeNotes(std::vector<Note> &notes) {
    int t = total();
    // la última que se agregó gana si hay dos iguales (mismo paso y altura)
    std::vector<Note> uniq;
    for (int i = int(notes.size()) - 1; i >= 0; i--) {
        const Note &n = notes[i];
        if (n.start < 0 || n.start >= t || n.pitch < 0 || n.pitch > MAX_PITCH) continue;
        bool dup = false;
        for (size_t k = 0; k < uniq.size(); k++)
            if (uniq[k].start == n.start && uniq[k].pitch == n.pitch) dup = true;
        if (!dup) uniq.push_back(n);
    }
    std::stable_sort(uniq.begin(), uniq.end(), byStart);
    std::vector<Note> ok = uniq;
    // una misma altura no se encima: la de antes termina donde empieza la otra
    for (size_t i = 0; i < ok.size(); i++) {
        if (ok[i].start + ok[i].len > t) ok[i].len = t - ok[i].start;
        for (size_t k = i + 1; k < ok.size(); k++) {
            if (ok[k].pitch == ok[i].pitch && ok[k].start < ok[i].start + ok[i].len) {
                ok[i].len = ok[k].start - ok[i].start;
                break;
            }
        }
        if (ok[i].len < 1) ok[i].len = 1;
    }
    // repartir en las 4 voces: la primera libre; si no hay, la que termina
    // antes se corta (máximo 4 notas a la vez)
    std::vector<int> voiceOf(ok.size(), 0);
    int voiceNote[VOICE_COUNT];
    for (int v = 0; v < VOICE_COUNT; v++) voiceNote[v] = -1;
    stolen_ = false;
    for (size_t i = 0; i < ok.size(); i++) {
        int pick = -1;
        for (int v = 0; v < VOICE_COUNT && pick < 0; v++) {
            int j = voiceNote[v];
            if (j < 0 || ok[j].start + ok[j].len <= ok[i].start) pick = v;
        }
        if (pick < 0) {
            int best = 0;
            for (int v = 1; v < VOICE_COUNT; v++)
                if (ok[voiceNote[v]].start + ok[voiceNote[v]].len <
                    ok[voiceNote[best]].start + ok[voiceNote[best]].len)
                    best = v;
            int j = voiceNote[best];
            ok[j].len = ok[i].start - ok[j].start;
            if (ok[j].len < 1) ok[j].len = 1;
            pick = best;
            stolen_ = true;
        }
        voiceOf[i] = pick;
        voiceNote[pick] = int(i);
    }
    Phrase *ph = viewData_->song_->phrase_;
    // borrar notas, KILL y MVEL del clip (los demás comandos se quedan) y las capas
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
        for (int v = 1; v < VOICE_COUNT; v++) {
            int vi = Phrase::VIdx(v, p, pos);
            ph->voiceNote_[vi] = NO_NOTE;
            ph->voiceVel_[vi] = VVEL_DEFAULT;
            ph->voiceFlags_[vi] = 0;
        }
        for (int v = 0; v < VOICE_COUNT; v++) {
            ph->timing_[Phrase::TIdx(v, p, pos)] = 0;
            ph->ramp_[Phrase::TIdx(v, p, pos)] = 0;
        }
    }
    for (size_t k = 0; k < ok.size(); k++) {
        const Note &n = ok[k];
        int v = voiceOf[k];
        int p, pos;
        stepPtr(n.start, p, pos);
        int i = 16 * p + pos;
        // dónde empieza la siguiente nota de esta voz
        int next = t;
        for (size_t m = k + 1; m < ok.size(); m++)
            if (voiceOf[m] == v) { next = ok[m].start; break; }
        int end = n.start + n.len;
        if (n.tick > 0 || n.rat > 1)
            ph->timing_[Phrase::TIdx(v, p, pos)] =
                (unsigned char)((n.tick & 0x0F) | (((n.rat - 1) & 7) << 4));
        if (n.rat > 1 && n.ramp) ph->ramp_[Phrase::TIdx(v, p, pos)] = (unsigned char)(signed char)n.ramp;
        if (v == 0) {
            ph->note_[i] = (unsigned char)n.pitch;
            ph->instr_[i] = (unsigned char)n.instr;
            if (n.vel != 100) {
                ushort vv = (ushort)(n.vel * 2);
                if (ph->cmd1_[i] == I_CMD_NONE) { ph->cmd1_[i] = I_CMD_MVEL; ph->param1_[i] = vv; }
                else if (ph->cmd2_[i] == I_CMD_NONE) { ph->cmd2_[i] = I_CMD_MVEL; ph->param2_[i] = vv; }
            }
            if (end < next) {
                stepPtr(end, p, pos);
                int j = 16 * p + pos;
                // con nudge el final también se recorre: KILL 0t = corta t sextos después
                if (ph->cmd1_[j] == I_CMD_NONE) { ph->cmd1_[j] = I_CMD_KILL; ph->param1_[j] = n.tick; }
                else if (ph->cmd2_[j] == I_CMD_NONE) { ph->cmd2_[j] = I_CMD_KILL; ph->param2_[j] = n.tick; }
            }
        } else {
            int vi = Phrase::VIdx(v, p, pos);
            ph->voiceNote_[vi] = (unsigned char)n.pitch;
            ph->voiceVel_[vi] = (n.vel == 100) ? VVEL_DEFAULT : (unsigned char)n.vel;
            if (end < next) {
                stepPtr(end, p, pos);
                ph->voiceFlags_[Phrase::VIdx(v, p, pos)] |= VF_KILL;
            }
        }
    }
    notes = ok;
}

// la nota que suena en ese paso y esa altura
int ClipView::noteAt(std::vector<Note> &notes, int step, int pitch) {
    for (size_t i = 0; i < notes.size(); i++) {
        if (notes[i].pitch == pitch && step >= notes[i].start &&
            step < notes[i].start + notes[i].len)
            return int(i);
    }
    return -1;
}

int ClipView::noteStarting(std::vector<Note> &notes, int step, int pitch) {
    for (size_t i = 0; i < notes.size(); i++) {
        if (notes[i].start == step && notes[i].pitch == pitch) return int(i);
    }
    return -1;
}

bool ClipView::addPage() {
    int n = pages();
    if (n >= 16) {
        View::SetNotification("El clip ya tiene 16 paginas");
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
    ph->ClearVoices(phrase);
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
    int i = noteAt(notes, cursorStep_, cursorPitch_);
    if (i >= 0 && notes[i].start == cursorStep_) {   // ya está: la elige
        lastLen_ = notes[i].len;
        lastVel_ = notes[i].vel;
        audition(notes[i].start);
        return;
    }
    if (i >= 0) {     // la misma nota sonando desde antes: la parte en dos
        notes[i].len = cursorStep_ - notes[i].start;
    }
    // con otras alturas sonando: acorde (hasta 4 notas a la vez)
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
    if (stolen_) message("Maximo 4 notas a la vez");
    audition(cursorStep_);
}

void ClipView::cutNote() {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_, cursorPitch_);
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
        // solo la misma altura se reemplaza o se corta; las demás hacen acorde
        for (size_t i = 0; i < notes.size(); i++) {
            if (notes[i].pitch == n.pitch && notes[i].start < n.start &&
                notes[i].start + notes[i].len > n.start)
                notes[i].len = n.start - notes[i].start;
        }
        for (size_t i = 0; i < notes.size();) {
            if (notes[i].start == n.start && notes[i].pitch == n.pitch) notes.erase(notes.begin() + i);
            else i++;
        }
        notes.push_back(n);
    }
    writeNotes(notes);
    if (stolen_) message("Maximo 4 notas a la vez");
}

// dónde caería lo copiado: empieza justo en el cursor; lo que se sale de la
// página no se pega
void ClipView::pastePreview(std::vector<Note> &add, int &dropped) {
    add.clear();
    dropped = 0;
    int pageEnd = (cursorStep_ / 16 + 1) * 16;
    if (pageEnd > total()) pageEnd = total();
    for (size_t k = 0; k < clipboard_.size(); k++) {
        Note n = clipboard_[k];
        n.start = cursorStep_ + (n.start - copyStep_);
        n.pitch = shiftPitch(n.pitch, copyPitch_, cursorPitch_);
        bool offPads = kit() && (n.pitch < kitBase() || n.pitch > kitBase() + 15);
        if (n.start < 0 || n.start >= pageEnd || n.pitch < 0 || n.pitch > MAX_PITCH || offPads) {
            dropped++;
            continue;
        }
        add.push_back(n);
    }
}

void ClipView::pasteNote() {
    if (clipboard_.empty()) { message("Nada que pegar"); return; }
    if (pages() == 0 && !addPage()) return;
    std::vector<Note> add;
    int dropped;
    pastePreview(add, dropped);
    if (add.empty()) { message("No cabe aqui"); return; }
    std::vector<Note> notes;
    readNotes(notes);
    placeNotes(notes, add);
    if (dropped) message("%d pegadas (lo demas no cabia)", (int)add.size());
    else message(add.size() == 1 ? "Nota pegada" : "%d notas pegadas", (int)add.size());
}

/************************************************************
 loop del clip (como el Move): B + L inicio, B + R final,
 B + L, L rápido: solo este compás / sin loop
 ************************************************************/

void ClipView::loopButton(bool left) {
    Song *song = viewData_->song_;
    int chain = viewData_->currentChain_;
    int n = pages();
    if (n == 0) return;
    int page = cursorStep_ / 16;
    int s, e;
    bool has = song->GetChainLoop(chain, s, e);
    if (!has) { s = 0; e = n - 1; }
    unsigned long now = System::GetInstance()->GetClock();
    if (left && now - lastLoopL_ < 400) {   // L dos veces: este compás solo, o sin loop
        lastLoopL_ = 0;
        if (prevHas_ && prevS_ == page && prevE_ == page) {
            song->ClearChainLoop(chain);
            message("Loop apagado: suena todo el clip");
        } else {
            song->SetChainLoop(chain, page, page);
            message("Loop: solo el compas %d", page + 1);
        }
        return;
    }
    if (left) {
        lastLoopL_ = now;
        song->GetChainLoop(chain, prevS_, prevE_);
        prevHas_ = song->GetChainLoop(chain, s, e);
        if (!prevHas_) { s = 0; e = n - 1; }
        s = page;
        if (e < s) e = s;
    } else {
        e = page;
        if (s > e) s = e;
    }
    song->SetChainLoop(chain, s, e);
    char buf[48];
    if (s == e) sprintf(buf, "Loop: compas %d", s + 1);
    else sprintf(buf, "Loop: compases %d a %d", s + 1, e + 1);
    message(buf);
}

/************************************************************
 compases: duplicar a la derecha / izquierda, borrar
 ************************************************************/

bool ClipView::duplicatePage(bool right) {
    int n = pages();
    if (n == 0) return false;
    if (n >= 16) { message("El clip ya tiene 16 compases"); return false; }
    int page = cursorStep_ / 16;
    Song *song = viewData_->song_;
    int chain = viewData_->currentChain_;
    unsigned char *row = song->chain_->data_ + 16 * chain;
    unsigned char *tr = song->chain_->transpose_ + 16 * chain;
    unsigned short np = song->phrase_->GetNext();
    if (np == NO_MORE_PHRASE) { message("No quedan phrases libres"); return false; }
    Phrase *ph = song->phrase_;
    ph->SetUsed((unsigned char)np);
    int src = 16 * row[page], dst = 16 * np;
    memcpy(ph->note_ + dst, ph->note_ + src, 16);
    memcpy(ph->instr_ + dst, ph->instr_ + src, 16);
    memcpy(ph->cmd1_ + dst, ph->cmd1_ + src, 16 * sizeof(FourCC));
    memcpy(ph->cmd2_ + dst, ph->cmd2_ + src, 16 * sizeof(FourCC));
    memcpy(ph->param1_ + dst, ph->param1_ + src, 16 * sizeof(ushort));
    memcpy(ph->param2_ + dst, ph->param2_ + src, 16 * sizeof(ushort));
    ph->CopyVoices(row[page], np);
    // la copia entra después (derecha) o en su lugar, empujando el original (izquierda)
    int at = right ? page + 1 : page;
    for (int r = n; r > at; r--) {
        row[r] = row[r - 1];
        tr[r] = tr[r - 1];
    }
    row[at] = (unsigned char)np;
    tr[at] = tr[right ? page : page + 1];
    // el loop se recorre con lo que se movió
    int s, e;
    if (song->GetChainLoop(chain, s, e)) {
        if (at <= s) { s++; e++; }
        else if (at <= e) e++;
        if (e > 15) e = 15;
        if (s > 15) song->ClearChainLoop(chain);
        else song->SetChainLoop(chain, s, e);
    }
    if (right) cursorStep_ += 16;   // a la copia
    message(right ? "Compas %d duplicado a la derecha" : "Compas %d duplicado a la izquierda", page + 1);
    return true;
}

static void DeletePageCallback(View &v, ModalView &dialog) {
    ((ClipView &)v).deletePageAnswer(dialog.GetReturnCode() == MBL_YES);
}

void ClipView::askDeletePage() {
    if (pages() <= 1) { message("El clip necesita al menos un compas"); return; }
    char buf[64];
    snprintf(buf, sizeof(buf), "Borrar el compas %d?\nLo de despues se recorre", cursorStep_ / 16 + 1);
    MessageBox *mb = new MessageBox(*this, buf, MBBF_YES | MBBF_NO);
    DoModal(mb, DeletePageCallback);
}

void ClipView::deletePageAnswer(bool yes) {
    isDirty_ = true;
    if (!yes) return;
    Snap before;
    takeSnap(before);
    int n = pages();
    int page = cursorStep_ / 16;
    Song *song = viewData_->song_;
    int chain = viewData_->currentChain_;
    unsigned char *row = song->chain_->data_ + 16 * chain;
    unsigned char *tr = song->chain_->transpose_ + 16 * chain;
    for (int r = page; r < n - 1; r++) {
        row[r] = row[r + 1];
        tr[r] = tr[r + 1];
    }
    row[n - 1] = 0xFF;
    tr[n - 1] = 0;
    int s, e;
    if (song->GetChainLoop(chain, s, e)) {
        if (page < s) { s--; e--; }
        else if (page <= e) e--;
        if (e < s || e < 0) song->ClearChainLoop(chain);
        else song->SetChainLoop(chain, s, e);
    }
    if (cursorStep_ >= total()) cursorStep_ = total() - 1;
    undo_.push_back(before);
    redo_.clear();
    message("Compas %d borrado", page + 1);
}

/************************************************************
 deshacer
 ************************************************************/

void ClipView::takeSnap(Snap &s) {
    s.chain = viewData_->currentChain_;
    s.data.clear();
    Chain *ch = viewData_->song_->chain_;
    Phrase *ph = viewData_->song_->phrase_;
    unsigned char *row = ch->data_ + 16 * s.chain;
    unsigned char *tr = ch->transpose_ + 16 * s.chain;
    s.data.insert(s.data.end(), row, row + 16);
    s.data.insert(s.data.end(), tr, tr + 16);
    for (int r = 0; r < 16 && row[r] != 0xFF; r++) {
        int i = 16 * row[r];
        s.data.insert(s.data.end(), ph->note_ + i, ph->note_ + i + 16);
        s.data.insert(s.data.end(), ph->instr_ + i, ph->instr_ + i + 16);
        const unsigned char *c1 = (const unsigned char *)(ph->cmd1_ + i);
        const unsigned char *c2 = (const unsigned char *)(ph->cmd2_ + i);
        const unsigned char *p1 = (const unsigned char *)(ph->param1_ + i);
        const unsigned char *p2 = (const unsigned char *)(ph->param2_ + i);
        s.data.insert(s.data.end(), c1, c1 + 16 * sizeof(FourCC));
        s.data.insert(s.data.end(), c2, c2 + 16 * sizeof(FourCC));
        s.data.insert(s.data.end(), p1, p1 + 16 * sizeof(ushort));
        s.data.insert(s.data.end(), p2, p2 + 16 * sizeof(ushort));
        for (int v = 1; v < VOICE_COUNT; v++) {   // las capas de voces
            int vi = Phrase::VIdx(v, row[r], 0);
            s.data.insert(s.data.end(), ph->voiceNote_ + vi, ph->voiceNote_ + vi + 16);
            s.data.insert(s.data.end(), ph->voiceVel_ + vi, ph->voiceVel_ + vi + 16);
            s.data.insert(s.data.end(), ph->voiceFlags_ + vi, ph->voiceFlags_ + vi + 16);
        }
        for (int v = 0; v < VOICE_COUNT; v++) {   // nudge y subdivisiones
            int ti = Phrase::TIdx(v, row[r], 0);
            s.data.insert(s.data.end(), ph->timing_ + ti, ph->timing_ + ti + 16);
            s.data.insert(s.data.end(), ph->ramp_ + ti, ph->ramp_ + ti + 16);
        }
    }
}

void ClipView::restoreSnap(const Snap &s) {
    Chain *ch = viewData_->song_->chain_;
    Phrase *ph = viewData_->song_->phrase_;
    unsigned char *row = ch->data_ + 16 * s.chain;
    unsigned char *tr = ch->transpose_ + 16 * s.chain;
    const unsigned char *d = &s.data[0];
    memcpy(row, d, 16);
    memcpy(tr, d + 16, 16);
    d += 32;
    for (int r = 0; r < 16 && row[r] != 0xFF; r++) {
        int i = 16 * row[r];
        memcpy(ph->note_ + i, d, 16); d += 16;
        memcpy(ph->instr_ + i, d, 16); d += 16;
        memcpy(ph->cmd1_ + i, d, 16 * sizeof(FourCC)); d += 16 * sizeof(FourCC);
        memcpy(ph->cmd2_ + i, d, 16 * sizeof(FourCC)); d += 16 * sizeof(FourCC);
        memcpy(ph->param1_ + i, d, 16 * sizeof(ushort)); d += 16 * sizeof(ushort);
        memcpy(ph->param2_ + i, d, 16 * sizeof(ushort)); d += 16 * sizeof(ushort);
        for (int v = 1; v < VOICE_COUNT; v++) {
            int vi = Phrase::VIdx(v, row[r], 0);
            memcpy(ph->voiceNote_ + vi, d, 16); d += 16;
            memcpy(ph->voiceVel_ + vi, d, 16); d += 16;
            memcpy(ph->voiceFlags_ + vi, d, 16); d += 16;
        }
        for (int v = 0; v < VOICE_COUNT; v++) {
            memcpy(ph->timing_ + Phrase::TIdx(v, row[r], 0), d, 16); d += 16;
            memcpy(ph->ramp_ + Phrase::TIdx(v, row[r], 0), d, 16); d += 16;
        }
    }
    if (cursorStep_ >= total() && total() > 0) cursorStep_ = total() - 1;
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
    int s0, s1, q0, q1;
    selRect(s0, s1, q0, q1);
    copyStep_ = s0;          // al pegar, el bloque empieza en el cursor
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
        if (dt) {   // en la rejilla: lo que tenía nudge cae a la raya
            if (!(n.tick > 0 && dt < 0)) n.start += dt;
            n.tick = 0;
        }
        if (dir) n.pitch = stepPitch(n.pitch, dir);
        n.pitch += 12 * octave;
        if (n.start < 0 || n.start >= total() || n.pitch < 0 || n.pitch > MAX_PITCH) {
            message("No cabe en el clip");
            return;
        }
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
    // solo dentro de este clip: lo que se sale no se escribe (no crece ni toca a otro)
    int dropped = 0;
    for (size_t k = 0; k < add.size();) {
        if (add[k].start >= total()) { add.erase(add.begin() + k); dropped++; }
        else k++;
    }
    if (add.empty()) { message("No cabe en el clip"); return; }
    placeNotes(notes, add);
    if (markLevel_ >= 3) markLevel_ = 2;   // la copia, no todo el clip otra vez
    anchorStep_ = s0 + width;
    cursorStep_ = s1 + width;
    if (cursorStep_ >= total()) cursorStep_ = total() - 1;
    int n = (int)add.size();
    if (dropped) message("%d duplicadas (lo demas no cabia en el clip)", n);
    else message(n == 1 ? "Nota duplicada" : "%d notas duplicadas", n);
}

/************************************************************
 escala
 ************************************************************/

SampleInstrument *ClipView::kit() {
    int owner = viewData_->project_->GetTrackInstrument(viewData_->songX_);
    if (owner < 0 || owner >= MAX_SAMPLEINSTRUMENT_COUNT) return 0;
    I_Instrument *in = viewData_->project_->GetInstrumentBank()->GetInstrument(owner);
    if (!in || in->GetType() != IT_SAMPLE) return 0;
    SampleInstrument *si = (SampleInstrument *)in;
    return si->IsKit() ? si : 0;
}

// los 16 pads: C1..D#2 como el Move, salvo que el kit use otras teclas
int ClipView::kitBase() {
    SampleInstrument *si = kit();
    return si ? si->KitBase() : 36;
}

int ClipView::stepPitch(int pitch, int dir) {
    if (kit()) {   // kit: de pad en pad, dentro de los 16
        int b = kitBase(), p = pitch + dir;
        return (p < b || p > b + 15) ? pitch : p;
    }
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    // las notas que quedaron fuera de la escala también se pueden alcanzar
    bool used[MAX_PITCH + 1];
    memset(used, 0, sizeof(used));
    if (scale > 0) {
        std::vector<Note> notes;
        readNotes(notes);
        for (size_t i = 0; i < notes.size(); i++)
            if (notes[i].pitch >= 0 && notes[i].pitch <= MAX_PITCH) used[notes[i].pitch] = true;
    }
    int p = pitch + dir;
    while (p >= 0 && p <= MAX_PITCH && !InScale(scale, root, p) && !used[p]) p += dir;
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
    if (kit()) return pitch + (toRef - fromRef);   // kit: de pad en pad, sin escala
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
    if (HasModal()) return;   // no borrar la ventanita (se dibuja encima)
    if (!msg_.empty() && System::GetInstance()->GetClock() - msgTime_ > 1500) {
        msg_.clear();
        DrawView();
    }
}

void ClipView::moveNote(int dt, int dp) {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_, cursorPitch_);
    if (i < 0) return;
    Note &n = notes[i];
    if (dp) {   // un semitono, o un grado si hay escala (dp = ±12: octava)
        int p = (dp == 1 || dp == -1) ? stepPitch(n.pitch, dp) : n.pitch + dp;
        if (p < 0 || p > MAX_PITCH) return;
        int j = noteStarting(notes, n.start, p);
        if (j >= 0 && j != i) return;   // ahí ya hay esa nota
        n.pitch = p;
        cursorPitch_ = p;
    }
    if (dt) {
        // sin el atajo del nudge se mueve en la rejilla: una nota con nudge
        // cae primero a la raya de la rejilla en esa dirección
        int s = (n.tick > 0 && dt < 0) ? n.start : n.start + dt;
        if (s < 0 || s >= total()) return;
        int j = noteStarting(notes, s, n.pitch);
        if (j >= 0 && j != i) return;   // ahí empieza otra igual
        cursorStep_ += s - n.start;
        n.start = s;
        n.tick = 0;
    }
    writeNotes(notes);
    keepVisible();
    if (dp) audition(cursorStep_);
}

// a qué notas les toca: lo marcado, o la nota del cursor
bool ClipView::targets(std::vector<Note> &notes, std::vector<int> &idx) {
    if (marking_) {
        selectedNotes(notes, idx);
    } else {
        readNotes(notes);
        idx.clear();
        int i = noteAt(notes, cursorStep_, cursorPitch_);
        if (i >= 0) idx.push_back(i);
    }
    return !idx.empty();
}

void ClipView::changeLength(int d) {
    std::vector<Note> notes;
    std::vector<int> idx;
    if (!targets(notes, idx)) return;
    for (size_t k = 0; k < idx.size(); k++) {
        Note &n = notes[idx[k]];
        int next = total();   // hasta la siguiente nota igual o el final del clip
        for (size_t m = 0; m < notes.size(); m++)
            if (notes[m].pitch == n.pitch && notes[m].start > n.start && notes[m].start < next)
                next = notes[m].start;
        int len = n.len + d;
        if (len < 1) len = 1;
        if (n.start + len > next) len = next - n.start;
        n.len = len;
        lastLen_ = len;
    }
    if (!marking_) cursorStep_ = notes[idx[0]].start;   // al editarla, el cursor salta al inicio
    writeNotes(notes);
    if (stolen_) message("Maximo 4 notas a la vez");
}

// velocity: con varias notas, todas a la vez y en proporción (la más fuerte
// se mueve d, las demás según lo que tengan); en el tope se van juntando
void ClipView::changeVelocity(int d) {
    std::vector<Note> notes;
    std::vector<int> idx;
    if (!targets(notes, idx)) return;
    int vmax = 1;
    for (size_t k = 0; k < idx.size(); k++) vmax = std::max(vmax, notes[idx[k]].vel);
    for (size_t k = 0; k < idx.size(); k++) {
        Note &n = notes[idx[k]];
        int step = (idx.size() == 1) ? d : int(floor(d * double(n.vel) / vmax + (d > 0 ? 0.5 : -0.5)));
        if (step == 0) step = d > 0 ? 1 : -1;
        n.vel = std::max(1, std::min(127, n.vel + step));
        lastVel_ = n.vel;
    }
    if (!marking_) cursorStep_ = notes[idx[0]].start;
    writeNotes(notes);
    char m[32];
    if (idx.size() == 1) snprintf(m, sizeof(m), "Velocity %d", notes[idx[0]].vel);
    else snprintf(m, sizeof(m), "Velocity %d notas", int(idx.size()));
    message(m);
}

// nudge: de sexto en sexto de paso; la nota se recorre entera
void ClipView::nudge(int d) {
    std::vector<Note> notes;
    std::vector<int> idx;
    if (!targets(notes, idx)) return;
    int t = total();
    for (size_t k = 0; k < idx.size(); k++) {
        Note &n = notes[idx[k]];   // la nota entera se recorre (inicio y final)
        int pos = n.start * 6 + n.tick + d;
        if (pos < 0) pos = 0;
        if (pos > (t - n.len) * 6 + 5) pos = (t - n.len) * 6 + 5;
        if (pos < 0) pos = 0;
        n.start = pos / 6;
        n.tick = pos % 6;
    }
    int tk = notes[idx[0]].tick;
    if (!marking_) cursorStep_ = notes[idx[0]].start;
    writeNotes(notes);
    char m[32];
    if (tk == 0) snprintf(m, sizeof(m), "Nudge: en la rejilla");
    else snprintf(m, sizeof(m), "Nudge +%d/6 de paso", tk);
    message(m);
    audition(cursorStep_);
}

// Y + ← →: rampa de velocity en los golpes (→ crece, ← se apaga)
void ClipView::rampVel(int d) {
    std::vector<Note> notes;
    std::vector<int> idx;
    if (!targets(notes, idx)) return;
    int r = 0, sub = 0;
    for (size_t k = 0; k < idx.size(); k++) {
        Note &n = notes[idx[k]];
        if (n.rat < 2) continue;
        n.ramp = std::max(-4, std::min(4, n.ramp + d));
        r = n.ramp;
        sub++;
    }
    if (!sub) { message("Rampa: primero subdivide con Y+arriba"); return; }
    if (!marking_) cursorStep_ = notes[idx[0]].start;
    writeNotes(notes);
    std::string m = "Rampa: ";
    if (r == 0) m += "plana";
    else {
        m += r > 0 ? "crece " : "se apaga ";
        for (int k = 0; k < abs(r); k++) m += "*";
    }
    message(m.c_str());
}

// subdividir: la nota se reparte en 2..8 golpes iguales (con 1, la nota entera)
void ClipView::ratchet(int d) {
    std::vector<Note> notes;
    std::vector<int> idx;
    if (!targets(notes, idx)) return;
    int r = 1;
    for (size_t k = 0; k < idx.size(); k++) {
        Note &n = notes[idx[k]];
        n.rat = std::max(1, std::min(8, n.rat + d));
        if (n.rat == 1) n.ramp = 0;
        r = n.rat;
    }
    if (!marking_) cursorStep_ = notes[idx[0]].start;
    writeNotes(notes);
    char m[32];
    if (r == 1) snprintf(m, sizeof(m), "Subdivision: nota entera");
    else snprintf(m, sizeof(m), "Subdivision: %d golpes", r);
    message(m);
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
    if (kit()) {
        int b = kitBase();
        if (cursorPitch_ < b) cursorPitch_ = b;
        if (cursorPitch_ > b + 15) cursorPitch_ = b + 15;
        topPitch_ = b + 15;
        buildRows();
        return;
    }
    if (cursorPitch_ > topPitch_) topPitch_ = cursorPitch_;
    buildRows();
    if (rowOf(cursorPitch_) < 0) {   // quedó abajo: el cursor pasa a la última fila
        topPitch_ = cursorPitch_;
        int n = 1;
        for (int q = cursorPitch_ + 1; q <= MAX_PITCH && n < ROWS; q++) {
            if (allowed_[q]) {
                topPitch_ = q;
                n++;
            }
        }
        buildRows();
    }
    // sin hueco abajo: si las filas se acaban antes del fondo, sube la vista
    while ((int)rows_.size() < ROWS && topPitch_ < MAX_PITCH) {
        topPitch_++;
        buildRows();
    }
}

void ClipView::buildRows() {
    if (kit()) {
        int b = kitBase();
        topPitch_ = b + 15;
        rows_.clear();
        for (int p = 0; p <= MAX_PITCH; p++) allowed_[p] = (p >= b && p <= b + 15);
        for (int p = b + 15; p >= b; p--) rows_.push_back(p);
        return;
    }
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    bool folding = viewData_->project_->GetFold() && scale > 0;
    std::vector<Note> notes;
    if (folding) readNotes(notes);
    for (int p = 0; p <= MAX_PITCH; p++) allowed_[p] = !folding || InScale(scale, root, p);
    for (size_t i = 0; i < notes.size(); i++)
        if (notes[i].pitch >= 0 && notes[i].pitch <= MAX_PITCH) allowed_[notes[i].pitch] = true;
    rows_.clear();
    for (int p = topPitch_; p >= 0 && (int)rows_.size() < ROWS; p--)
        if (allowed_[p]) rows_.push_back(p);
}

int ClipView::rowOf(int pitch) {
    for (size_t r = 0; r < rows_.size(); r++)
        if (rows_[r] == pitch) return int(r);
    return -1;
}

void ClipView::audition(int step) {
    Player *player = Player::GetInstance();
    if (player->IsRunning() && viewData_->playMode_ != PM_AUDITION) return;
    if (step < 0 || step >= total()) return;
    if (SampleInstrument *k = kit()) {   // kit: solo el pad de la fila, tal como suena
        player->PreviewNote(viewData_->songX_, k, cursorPitch_);
        return;
    }
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

// nombre de la nota escrito como en la escala (Do menor: C D Eb F G Ab Bb):
// en las escalas de 7 notas cada grado usa su propia letra; en las demás,
// bemoles o sostenidos según la tonalidad
static const int natPc[7] = {0, 2, 4, 5, 7, 9, 11};   // C D E F G A B
static const char letters[8] = "CDEFGAB";

static int spell7(int scale, int root, int rootLetter, int *letterOf, int *accOf) {
    int deg = 0, cost = 0;
    for (int i = 0; i < 12; i++) {
        if (!scaleSteps[scale][i]) continue;
        int pc = (root + i) % 12;
        int l = (rootLetter + deg) % 7;
        int acc = ((pc - natPc[l]) % 12 + 12) % 12;
        if (acc > 6) acc -= 12;
        if (acc < -2 || acc > 2) return 1000;
        letterOf[pc] = l;
        accOf[pc] = acc;
        cost += acc < 0 ? -acc : acc;
        deg++;
    }
    return cost;
}

std::string ClipView::noteName(int pitch) {
    if (SampleInstrument *si = kit()) {   // kit: el nombre del sample del pad
        std::string n = si->PadName(pitch);
        if (!n.empty()) return n;
        char b[12];
        sprintf(b, "pad%d", pitch - kitBase() + 1);
        return b;
    }
    static const char *sharps[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *flats[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    int scale = viewData_->project_->GetScale();
    int root = viewData_->project_->GetScaleRoot();
    int pc = pitch % 12, oct = pitch / 12 - 2;
    char buf[12];
    if (scale <= 0) {
        sprintf(buf, "%s%d", sharps[pc], oct);
        return buf;
    }
    int count = 0;
    for (int i = 0; i < 12; i++) count += scaleSteps[scale][i] ? 1 : 0;
    // la letra de la raíz: natural, o la de sostenido / bemol que dé menos alteraciones
    int sharpL = -1, flatL = -1;
    for (int l = 0; l < 7; l++) {
        if (natPc[l] == root) sharpL = flatL = l;
        if ((natPc[l] + 1) % 12 == root && sharpL < 0) sharpL = l;
        if ((natPc[l] + 11) % 12 == root && flatL < 0) flatL = l;
    }
    bool useFlats = false;
    if (count == 7) {
        int lA[12], aA[12], lB[12], aB[12];
        int cA = spell7(scale, root, sharpL, lA, aA);
        int cB = spell7(scale, root, flatL, lB, aB);
        int *letterOf = (cB <= cA) ? lB : lA, *accOf = (cB <= cA) ? aB : aA;
        int best = (cB <= cA) ? cB : cA;
        if (best < 1000) {
            int negative = 0;
            for (int i = 0; i < 12; i++)
                if (scaleSteps[scale][i] && accOf[(root + i) % 12] < 0) negative++;
            useFlats = negative > 0;
            if (scaleSteps[scale][(pc - root + 12) % 12]) {
                int l = letterOf[pc], acc = accOf[pc];
                if (l == 6 && pc == 0) oct--;    // B# es del Do de arriba
                if (l == 0 && pc == 11) oct++;   // Cb es del Si de abajo
                std::string n(1, letters[l]);
                if (acc > 0) n += std::string(acc, '#');
                if (acc < 0) n += std::string(-acc, 'b');
                sprintf(buf, "%s%d", n.c_str(), oct);
                return buf;
            }
        }
    } else {
        // tonalidades menores (tercera menor) y raíces en tecla negra: bemoles
        useFlats = (scaleSteps[scale][3] && !scaleSteps[scale][4]) ||
                   (sharpL != flatL && root != 6);
    }
    sprintf(buf, "%s%d", useFlats ? flats[pc] : sharps[pc], oct);
    return buf;
}

void ClipView::OnFocus() {
    checkScaleChange();   // trk2als
    if (chainShown_ != viewData_->currentChain_) {
        chainShown_ = viewData_->currentChain_;
        undo_.clear();   // el historial es de cada clip
        redo_.clear();
        marking_ = false;
        pasting_ = false;
        cursorStep_ = 0;
        cursorPitch_ = 60;
        std::vector<Note> notes;
        readNotes(notes);
        if (!notes.empty()) cursorPitch_ = notes[0].pitch;
        else if (kit()) cursorPitch_ = kitBase();   // kit vacío: en el primer pad (el kick)
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
    // Select + L deshace, Select + R rehace
    if (pressed && (mask & EPBM_SELECT) && (v == EPBM_L || v == EPBM_R)) {
        std::vector<Snap> &from = (v == EPBM_L) ? undo_ : redo_;
        std::vector<Snap> &to = (v == EPBM_L) ? redo_ : undo_;
        if (from.empty()) {
            message(v == EPBM_L ? "Nada que deshacer" : "Nada que rehacer");
        } else {
            Snap now;
            takeSnap(now);
            to.push_back(now);
            restoreSnap(from.back());
            from.pop_back();
            message(v == EPBM_L ? "Deshecho" : "Rehecho");
        }
        marking_ = false;
        pasting_ = false;
        isDirty_ = true;
        return;
    }
    Snap before;
    takeSnap(before);
    handleButton(mask, pressed);
    Snap after;
    takeSnap(after);
    if (before.chain == after.chain && before.data != after.data) {
        undo_.push_back(before);
        if (undo_.size() > 64) undo_.erase(undo_.begin());
        redo_.clear();
    }
}

void ClipView::handleButton(unsigned short mask, bool pressed) {
    unsigned short v = View::pressedButton_;
    if (!pressed) {
        if (baPending_ && !(mask & EPBM_A)) {   // B + A sola: corta la nota
            baPending_ = false;
            cutNote();
        }
        if (pasting_ && !(mask & EPBM_A)) {   // al soltar A queda pegado ahí
            pasting_ = false;
            pasteNote();
        }
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

    if (pasting_) {
        if (v == EPBM_B) {
            pasting_ = false;
            message("Pegado cancelado");
        } else if (dt || dp) {
            moveCursor(dt, dp);
        } else if (v == EPBM_L) {
            moveCursor(0, -12);
        } else if (v == EPBM_R) {
            moveCursor(0, 12);
        }
        isDirty_ = true;
        return;
    }

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
            else if (clipboard_.empty()) message("Nada que pegar");
            else {                                // mantén A: se ve y se acomoda; al soltar, se pega
                pasting_ = true;
                message("Suelta A para pegar (flechas: acomodar, B: cancelar)");
            }
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
            int ls, le;
            int from = viewData_->song_->GetChainLoop(viewData_->currentChain_, ls, le) ? ls : 0;
            viewData_->chainRow_ = from;
            player->OnStartButton(PM_CHAIN, viewData_->songX_, false, from);
        }
        isDirty_ = true;
        return;
    }

    if (mask & EPBM_X) {               // propiedades de la nota (o de lo marcado)
        if (mask & EPBM_A) {           // X + A: ajuste fino (nudge, velocity ±1)
            if (dt) nudge(dt);
            if (dp) changeVelocity(dp);
        } else {
            if (dt) changeLength(dt);
            if (dp) changeVelocity(dp * 8);
        }
        isDirty_ = true;
        return;
    }
    if (mask & EPBM_Y) {               // Y + ↑ ↓: subdividir; Y + ← →: rampa de velocity
        if (dp) ratchet(dp);
        if (dt) rampVel(dt);
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

    if (mask & EPBM_B) {
        if (v == EPBM_L) loopButton(true);
        if (v == EPBM_R) loopButton(false);
        if (mask & EPBM_A) {           // B + A + flecha: el compás entero
            if (v == EPBM_A) baPending_ = true;
            else if (dt) { baPending_ = false; duplicatePage(dt > 0); }
            else if (v == EPBM_UP) { baPending_ = false; askDeletePage(); }
            isDirty_ = true;
            return;
        }
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
        int i = noteAt(notes, cursorStep_, cursorPitch_);
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
    int cur = noteAt(notes, cursorStep_, cursorPitch_);
    if (cur >= 0 && notes[cur].pitch != cursorPitch_) cur = -1;   // solo si el cursor está sobre ella
    SampleInstrument *kitInstr = kit();
    int scale = kitInstr ? 0 : viewData_->project_->GetScale();   // un kit no tiene escala
    int root = viewData_->project_->GetScaleRoot();
    const int RH = kitInstr ? (ROWS * ROW_H) / 16 : ROW_H;   // 16 pads llenan la rejilla

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
            if (notes[cur].tick) sprintf(info + strlen(info), "  +%d/6", notes[cur].tick);
            if (notes[cur].rat > 1) sprintf(info + strlen(info), "  x%d", notes[cur].rat);
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
                std::string sc = std::string(viewData_->project_->GetFold() ? "FOLD " : "") + scaleNotes[root] + " " + scaleNames[scale];
                if (sc.size() > 18) sc = sc.substr(0, 18);
                DrawText(466 - 8 * int(sc.size()), 34, sc.c_str(), hi, 0);
            }
        }
        // las páginas, a la derecha (y el loop debajo)
        int loopS = 0, loopE = 0;
        bool hasLoop = viewData_->song_->GetChainLoop(viewData_->currentChain_, loopS, loopE);
        int bx = 632 - 16 * 10;
        for (int i = 0; i < 16; i++) {
            int x = bx + i * 10;
            if (i < npages) {
                bool playing = playStep_ >= 0 && playStep_ / 16 == i;
                bool inLoop = !hasLoop || (i >= loopS && i <= loopE);
                GUIColor c = (i == page) ? hi2 : Mix(bg, fg, inLoop ? 45 : 12);
                if (hasLoop && inLoop) FillRect(x - 1, 50, 9, 2, hi);   // la llave del loop
                FillRect(x, 36, 7, 8, c, 2);
                if (playing) FillRect(x, 46, 7, 2, play);
            } else {
                FillRect(x + 3, 39, 2, 2, Mix(bg, fg, 20));
            }
        }
    }

    // la rejilla
    const int gw = 16 * STEP_W;
    buildRows();
    if ((int)rows_.size() < ROWS)
        FillRect(GRID_X, GRID_Y, gw, ROWS * ROW_H, bg);
    for (int r = 0; r < (int)rows_.size(); r++) {
        int pitch = rows_[r];
        int y = GRID_Y + r * RH;
        bool black = isBlack(pitch);
        bool in = InScale(scale, root, pitch);
        bool isRoot = scale > 0 && (pitch - root) % 12 == 0;
        // con escala: sus filas claras, las demás casi negras (como Live)
        int shade = scale > 0 ? (in ? 9 : 1) : (black ? 3 : 8);
        FillRect(GRID_X, y, gw, RH, Mix(bg, fg, shade));
        if ((scale > 0 ? isRoot : pitch % 12 == 0))
            FillRect(GRID_X, y + RH - 1, gw, 1, Mix(bg, fg, 22));
        // teclado
        GUIColor key = black ? Mix(bg, fg, 22) : Mix(bg, fg, 70);
        if (scale > 0 && !in) key = Mix(bg, fg, black ? 10 : 25);
        if (isRoot) key = Mix(bg, hi, 75);
        if (pitch == cursorPitch_) key = fg;   // la del cursor, en blanco
        FillRect(8, y + 1, 44, RH - 2, key, 2);
        if (kitInstr) {   // cada pad con su sample (los vacíos, apagados)
            std::string pn = kitInstr->PadName(pitch);
            if (pn.empty() && pitch != cursorPitch_) key = Mix(bg, fg, 12);
            FillRect(8, y + 1, 44, RH - 2, key, 2);
            if (!pn.empty()) DrawText(10, y + (RH - 12) / 2, pn.substr(0, 5).c_str(), bg, 0);
        } else if (scale > 0 ? isRoot : pitch % 12 == 0)
            DrawText(12, y + 1, noteName(pitch).c_str(), bg, 0);
        else if (pitch == cursorPitch_)   // la tecla del cursor dice qué nota es
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
        int r0 = -1, r1 = -2;
        for (int r = 0; r < (int)rows_.size(); r++) {
            if (rows_[r] <= q1 && rows_[r] >= q0) {
                if (r0 < 0) r0 = r;
                r1 = r;
            }
        }
        if (a <= b && r0 <= r1) {
            int x = GRID_X + (a - p0) * STEP_W, y = GRID_Y + r0 * RH;
            int w = (b - a + 1) * STEP_W, h = (r1 - r0 + 1) * RH;
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
        int r = rowOf(n.pitch);
        if (r < 0) {        // fuera de la vista: una rayita en el borde
            int y = n.pitch > topPitch_ ? GRID_Y - 3 : GRID_Y + ROWS * ROW_H + 1;
            FillRect(GRID_X + s0 * STEP_W + 2, y, (s1 - s0) * STEP_W - 4, 2, c);
            continue;
        }
        int x = GRID_X + s0 * STEP_W + 1, y = GRID_Y + r * RH + 1;
        int w = (s1 - s0) * STEP_W - 2;
        if (n.tick > 0) {   // nudge: la nota entera un poco después
            int off = n.tick * STEP_W / 6;
            if (n.start >= p0) x += off;
            else w += off;
            if (x + w > GRID_X + 16 * STEP_W) w = GRID_X + 16 * STEP_W - x;
            if (w < 3) w = 3;
        }
        if (n.rat > 1) {   // subdividida: n golpecitos (con rampa, de distinta altura)
            int span = w + 2;
            for (int k = 0; k < n.rat; k++) {
                int a = x + span * k / n.rat, b = x + span * (k + 1) / n.rat - 2;
                if (b - a < 2) b = a + 2;
                float f = 1.0f, amt = 0.2f * abs(n.ramp);
                if (n.ramp > 0) f = 1 - amt * (n.rat - 1 - k) / (n.rat - 1);
                if (n.ramp < 0) f = 1 - amt * k / (n.rat - 1);
                int h = int((RH - 2) * f);
                if (h < 3) h = 3;
                FillRect(a, y + (RH - 2) - h, b - a, h, c, 2);
            }
        } else {
            FillRect(x, y, w, RH - 2, c, 3);
        }
        if (n.start >= p0) FillRect(x, y, 3, RH - 2, Mix(c, fg, 40), 1);   // dónde empieza
        if ((int)i == cur) FrameRect(x - 1, y - 1, w + 2, RH, fg, 3, 2);
    }

    // lo que se va a pegar (Select + A sostenido)
    if (pasting_) {
        std::vector<Note> add;
        int dropped;
        pastePreview(add, dropped);
        for (size_t k = 0; k < add.size(); k++) {
            const Note &n = add[k];
            int r = rowOf(n.pitch);
            int s0 = n.start - p0, s1 = std::min(16, n.start + n.len - p0);
            if (r < 0 || s0 < 0 || s0 >= 16) continue;
            int x = GRID_X + s0 * STEP_W + 1, y = GRID_Y + r * RH + 1;
            int w = (s1 - s0) * STEP_W - 2;
            FillRect(x, y, w, RH - 2, Mix(bg, hi, 35), 3);
            FrameRect(x - 1, y - 1, w + 2, RH, hi, 3, 1);
        }
    }

    // el cursor: una nota fantasma
    if (!pasting_ && (cur < 0 || notes[cur].pitch != cursorPitch_) && rowOf(cursorPitch_) >= 0) {
        int r = rowOf(cursorPitch_);
        int x = GRID_X + (cursorStep_ - p0) * STEP_W + 1, y = GRID_Y + r * RH + 1;
        FillRect(x, y, STEP_W - 2, RH - 2, Mix(bg, fg, 30), 3);
        FrameRect(x - 1, y - 1, STEP_W, RH, fg, 3, 1);
    }

    // el cabezal
    if (playStep_ >= p0 && playStep_ < p0 + 16) {
        FillRect(GRID_X + (playStep_ - p0) * STEP_W, GRID_Y - 2, 2, ROWS * ROW_H + 4, play);
    }

    // carril de velocity
    FillRect(GRID_X, VEL_Y, gw, VEL_H, Mix(bg, fg, 5), 4);
    DrawText(12, VEL_Y + VEL_H / 2 - 6, "vel", dim, 0);
    // en un acorde las barras se apilan: la más alta atrás, la del cursor encima
    for (int pass = 0; pass < 2; pass++) {
        for (int lvl = 127; lvl >= 0; lvl--) {
            for (size_t i = 0; i < notes.size(); i++) {
                const Note &n = notes[i];
                if (n.vel != lvl && pass == 0) continue;
                if (pass == 1 && ((int)i != cur || lvl != 0)) continue;
                if (pass == 0 && (int)i == cur) continue;
                int s = n.start - p0;
                if (s < 0 || s >= 16) continue;
                int h = (VEL_H - 8) * n.vel / 127;
                if (h < 2) h = 2;
                GUIColor c = ((int)i == cur) ? hi2 : Mix(bg, hi, 40 + 60 * n.vel / 127);
                FillRect(GRID_X + s * STEP_W + 10, VEL_Y + VEL_H - 4 - h, STEP_W - 20, h, c, 2);
            }
        }
    }

    if (marking_)
        drawHelpBar("flechas:marcar;B:copiar;B+A:cortar;A+<>:mover;SEL+A:duplicar;A:salir");
    else
        drawHelpBar(pasting_ ? "suelta A:pegar;flechas:acomodar;B:cancelar"
                             : "A:nota;X:largo/vel;B+A:cortar;SEL+A:pegar;SEL,B:marcar;SEL+L:deshacer");
}

void ClipView::OnPlayerUpdate(PlayerEventType, unsigned int) {
    if (HasModal()) return;
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
