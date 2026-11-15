#include "ClipView.h"
#include "Application/Player/Player.h"
#include "Application/Model/Project.h"
#include "Application/Instruments/CommandList.h"
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
    hasClip_ = false;
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
    if (i < 0) return;
    clip_ = notes[i];
    hasClip_ = true;
    // la nota de antes no se alarga: termina donde empezaba esta
    notes.erase(notes.begin() + i);
    writeNotes(notes);
}

void ClipView::pasteNote() {
    if (!hasClip_) return;
    if (pages() == 0 && !addPage()) return;
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i >= 0 && notes[i].start < cursorStep_) notes[i].len = cursorStep_ - notes[i].start;
    Note n = clip_;
    n.start = cursorStep_;
    n.pitch = cursorPitch_;
    notes.push_back(n);
    writeNotes(notes);
}

void ClipView::moveNote(int dt, int dp) {
    std::vector<Note> notes;
    readNotes(notes);
    int i = noteAt(notes, cursorStep_);
    if (i < 0) return;
    Note &n = notes[i];
    if (dp) {
        int p = n.pitch + dp;
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
    cursorPitch_ += dp;
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
    const unsigned short arrows = EPBM_UP | EPBM_DOWN | EPBM_LEFT | EPBM_RIGHT;
    if (!pressed) {
        if (!(mask & EPBM_A)) stopAudition();
        isDirty_ = true;
        return;
    }
    unsigned short v = View::pressedButton_;
    int dt = (v == EPBM_LEFT) ? -1 : (v == EPBM_RIGHT) ? 1 : 0;
    int dp = (v == EPBM_UP) ? 1 : (v == EPBM_DOWN) ? -1 : 0;
    Player *player = Player::GetInstance();

    if (mask & EPBM_SELECT) {
        if (v == EPBM_LEFT) switchView(VT_SONG);
        else if (v == EPBM_RIGHT) {
            int owner = viewData_->project_->GetTrackInstrument(viewData_->songX_);
            if (owner >= 0) viewData_->currentInstrument_ = owner;
            viewData_->instrumentFromSong_ = false;
            switchView(VT_INSTRUMENT);
        } else if (v == EPBM_DOWN && pages() > 0) {   // la phrase de esta página, a la antigua
            int p, pos;
            stepPtr(cursorStep_, p, pos);
            viewData_->currentPhrase_ = p;
            viewData_->chainRow_ = cursorStep_ / 16;
            switchView(VT_PHRASE);
        } else if (v == EPBM_A) {
            pasteNote();
        } else if (v == EPBM_START) {
            if (player->IsRunning()) player->Stop();
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
    if (dt || dp) moveCursor(dt, dp);
    if (v == EPBM_L) moveCursor(0, -12);
    if (v == EPBM_R) moveCursor(0, 12);
    (void)arrows;
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
        DrawText(10, 34, info, fg, 0);
        // las páginas, a la derecha
        int bx = 632 - 16 * 14;
        for (int i = 0; i < 16; i++) {
            int x = bx + i * 14;
            if (i < npages) {
                bool playing = playStep_ >= 0 && playStep_ / 16 == i;
                GUIColor c = (i == page) ? hi2 : Mix(bg, fg, 35);
                FillRect(x, 36, 10, 8, c, 2);
                if (playing) FillRect(x, 46, 10, 2, play);
            } else {
                FillRect(x + 4, 39, 2, 2, Mix(bg, fg, 20));
            }
        }
    }

    // la rejilla
    const int gw = 16 * STEP_W;
    for (int r = 0; r < ROWS; r++) {
        int pitch = topPitch_ - r;
        int y = GRID_Y + r * ROW_H;
        bool black = isBlack(pitch);
        FillRect(GRID_X, y, gw, ROW_H, Mix(bg, fg, black ? 3 : 8));
        if (pitch % 12 == 0) FillRect(GRID_X, y + ROW_H - 1, gw, 1, Mix(bg, fg, 22));
        // teclado
        GUIColor key = black ? Mix(bg, fg, 22) : Mix(bg, fg, 70);
        if (pitch == cursorPitch_) key = hi2;
        FillRect(8, y + 1, 44, ROW_H - 2, key, 2);
        if (pitch % 12 == 0) DrawText(12, y + 1, noteName(pitch).c_str(), bg, 0);
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

    drawHelpBar("A:nota;A+flechas:mover;X+flechas:largo/vel;B+A:borrar;L/R:octava");
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
