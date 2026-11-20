
#include "NewProjectDialog.h"
#include "Application/Utils/ProjectName.h"
#include "Application/Utils/RandomNames.h"
#include <string.h>

// trk2als: solo lo que sirve en un nombre de carpeta
static const char *rowsUp[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_"};
static const char *rowsLow[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_"};
#define KROWS 5   // 4 de letras + la de abajo (mayus, espacio, borrar, listo)
static const char *buttonText[3] = {"AZAR", "OK", "CANCELAR"};
static const int buttonW[3] = {112, 112, 144};

NewProjectDialog::NewProjectDialog(View &view, Path currentPath)
    : ModalView(view), prefix_(PROJECT_PREFIX), allowExisting_(false),
      currentPath_(currentPath), cursor_(0), selected_(0), keyboard_(false),
      upper_(true), krow_(1), kcol_(0), warnTime_(0) {}

NewProjectDialog::~NewProjectDialog() {}

void NewProjectDialog::OnPlayerUpdate(PlayerEventType, unsigned int) {}

void NewProjectDialog::OnFocus() {
    name_ = initial_.substr(0, MAX_NAME_LENGTH);
    cursor_ = name_.size();
    keyboard_ = false;
    upper_ = true;
    krow_ = 1; kcol_ = 0;
    selected_ = name_.empty() ? 0 : 2;   // con nombre sugerido, ya en Ok
    warn_.clear();
}

char NewProjectDialog::keyAt(int row, int col) {
    if (row < 0 || row >= 4) return 0;
    const char *r = upper_ ? rowsUp[row] : rowsLow[row];
    if (col < 0 || col >= (int)strlen(r)) return 0;
    return r[col];
}

int NewProjectDialog::specialAt(int col) {
    if (col <= 1) return 0;
    if (col <= 5) return 1;
    if (col <= 7) return 2;
    return 3;
}

void NewProjectDialog::warn(const char *msg) {
    warn_ = msg;
    warnTime_ = SDL_GetTicks();
}

void NewProjectDialog::typeKey() {
    if (krow_ == 4) {
        switch (specialAt(kcol_)) {
        case 0: upper_ = !upper_; break;
        case 1:
            if ((int)name_.size() < MAX_NAME_LENGTH && cursor_ > 0) {
                name_.insert(cursor_, 1, ' ');
                cursor_++;
            }
            break;
        case 2: backspace(); break;
        case 3: keyboard_ = false; selected_ = name_.empty() ? 0 : 2; break;
        }
        return;
    }
    char ch = keyAt(krow_, kcol_);
    if (!ch) return;
    if ((int)name_.size() >= MAX_NAME_LENGTH) { warn("Maximo 16 letras"); return; }
    name_.insert(cursor_, 1, ch);
    cursor_++;
}

void NewProjectDialog::backspace() {
    if (cursor_ > 0) {
        name_.erase(cursor_ - 1, 1);
        cursor_--;
    }
}

void NewProjectDialog::randomName() {
    for (int tries = 0; tries < 200; tries++) {
        std::string n = getRandomName();
        if ((int)n.size() > MAX_NAME_LENGTH) n = n.substr(0, MAX_NAME_LENGTH);
        name_ = n;
        cursor_ = name_.size();
        if (allowExisting_ || !currentPath_.Descend(GetName()).Exists()) break;
    }
}

void NewProjectDialog::tryOk() {
    // sin espacios al principio ni al final
    while (!name_.empty() && name_[name_.size() - 1] == ' ') name_.erase(name_.size() - 1);
    while (!name_.empty() && name_[0] == ' ') name_.erase(0, 1);
    if (cursor_ > (int)name_.size()) cursor_ = name_.size();
    if (name_.empty()) {
        warn("Ponle nombre para poder guardar");
        selected_ = 0;
        return;
    }
    if (!allowExisting_ && currentPath_.Descend(GetName()).Exists()) {
        warn("Ya hay uno con ese nombre");
        return;
    }
    EndModal(1);
}

void NewProjectDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed) return;
    isDirty_ = true;

    if (keyboard_) {
        if (mask == EPBM_A) { typeKey(); return; }
        if (mask == EPBM_B) { backspace(); return; }
        if (mask == EPBM_START) { keyboard_ = false; selected_ = name_.empty() ? 0 : 2; return; }
        if (mask == EPBM_L) { if (cursor_ > 0) cursor_--; return; }
        if (mask == EPBM_R) { if (cursor_ < (int)name_.size()) cursor_++; return; }
        if (mask == EPBM_UP) { krow_ = (krow_ + KROWS - 1) % KROWS; }
        if (mask == EPBM_DOWN) { krow_ = (krow_ + 1) % KROWS; }
        if (mask == EPBM_LEFT) {
            if (krow_ == 4) {   // de grupo en grupo
                int s = specialAt(kcol_);
                s = (s + 3) % 4;
                kcol_ = s == 0 ? 0 : s == 1 ? 2 : s == 2 ? 6 : 8;
            } else {
                int n = strlen(rowsUp[krow_]);
                kcol_ = (kcol_ + n - 1) % n;
            }
        }
        if (mask == EPBM_RIGHT) {
            if (krow_ == 4) {
                int s = (specialAt(kcol_) + 1) % 4;
                kcol_ = s == 0 ? 0 : s == 1 ? 2 : s == 2 ? 6 : 8;
            } else {
                int n = strlen(rowsUp[krow_]);
                kcol_ = (kcol_ + 1) % n;
            }
        }
        // la fila corta (ZXCV..._): que no quede fuera
        if (krow_ < 4 && kcol_ >= (int)strlen(rowsUp[krow_])) kcol_ = strlen(rowsUp[krow_]) - 1;
        return;
    }

    if (mask == EPBM_A) {
        switch (selected_) {
        case 0: keyboard_ = true; break;
        case 1: randomName(); break;
        case 2: tryOk(); break;
        case 3: EndModal(0); break;
        }
        return;
    }
    if (mask == EPBM_START) { tryOk(); return; }
    if (mask == EPBM_B && selected_ == 0) { backspace(); return; }
    if (mask == EPBM_UP) { selected_ = 0; return; }
    if (mask == EPBM_DOWN) { if (selected_ == 0) selected_ = 2; return; }
    if (mask == EPBM_LEFT) {
        if (selected_ == 0) { if (cursor_ > 0) cursor_--; }
        else if (selected_ > 1) selected_--;
        return;
    }
    if (mask == EPBM_RIGHT) {
        if (selected_ == 0) { if (cursor_ < (int)name_.size()) cursor_++; }
        else if (selected_ < 3) selected_++;
        return;
    }
}

std::string NewProjectDialog::GetName() {
    std::string n = name_;
    while (!n.empty() && n[n.size() - 1] == ' ') n.erase(n.size() - 1);
    return prefix_ + n;
}

std::string NewProjectDialog::RandomProjectName(const Path &dir) {
    std::string name;
    for (int tries = 0; tries < 200; tries++) {
        name = std::string(PROJECT_PREFIX) + getRandomName();
        if (!Path(dir).Descend(name).Exists()) break;
    }
    return name;
}

void NewProjectDialog::DrawView() {
    if (SupportsShapes()) drawShapes();
    else drawGrid();
}

// lo mínimo sin figuras (otras plataformas)
void NewProjectDialog::drawGrid() {
    SetWindow(20, keyboard_ ? 10 : 5);
    GUITextProperties props;
    SetColor(CD_NORMAL);
    std::string shown = name_ + std::string(MAX_NAME_LENGTH - name_.size(), '.');
    DrawString(2, 0, (title_.empty() ? "Guardar como" : title_.c_str()), props);
    char b[2] = {0, 0};
    for (int i = 0; i < MAX_NAME_LENGTH; i++) {
        props.invert_ = (i == cursor_ && selected_ == 0);
        b[0] = shown[i];
        DrawString(2 + i, 2, b, props);
    }
    props.invert_ = false;
    if (keyboard_) {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < (int)strlen(rowsUp[r]); c++) {
                props.invert_ = (r == krow_ && c == kcol_);
                b[0] = keyAt(r, c);
                DrawString(5 + c, 4 + r, b, props);
            }
        const char *sp[4] = {upper_ ? "abc" : "ABC", "_", "<-", "OK"};
        int xs[4] = {2, 6, 9, 13};
        for (int s = 0; s < 4; s++) {
            props.invert_ = (krow_ == 4 && specialAt(kcol_) == s);
            DrawString(xs[s], 9, sp[s], props);
        }
        return;
    }
    int xs[3] = {1, 7, 11};
    for (int i = 0; i < 3; i++) {
        props.invert_ = (selected_ == i + 1);
        DrawString(xs[i], 4, buttonText[i], props);
    }
}

void NewProjectDialog::drawShapes() {
    const int PX = 8, PY = 8, PW = 624, PH = 464;
    DrawPanel(PX, PY, PW, PH);
    GUIColor bg = ColorOf(CD_BACKGROUND), fg = ColorOf(CD_NORMAL);
    GUIColor acc = ColorOf(CD_HILITE2), dim = Mix(bg, fg, 50), card = Mix(bg, fg, 10);
    GUIColor off = Mix(bg, fg, 22), warnc = Mix(fg, ColorOf(CD_MUTE), 45);

    // el título
    std::string t = title_.empty() ? "Guardar como" : title_;
    for (size_t i = 0; i < t.size(); i++) t[i] = toupper(t[i]);
    DrawText(32, 24, t.c_str(), acc, 1);
    if (keyboard_) DrawText(PX + PW - 24 - 8 * 22, 28, "Start cierra el teclado", dim, 0);

    // el nombre
    const int FY = 52, FH = 72;
    FillRect(24, FY, 592, FH, card, 8);
    if (selected_ == 0 || keyboard_) FrameRect(24, FY, 592, FH, acc, 8, 3);
    int nx = 44, ny = FY + 14;
    if (!name_.empty()) DrawText(nx, ny, name_.c_str(), fg, 2);
    // el cursor: una rayita debajo de donde va la próxima letra
    int cx = nx + cursor_ * 24;
    if (selected_ == 0 || keyboard_) FillRect(cx + 1, ny + 34, 20, 3, acc);
    if (name_.empty()) DrawText(nx + 36, ny + 10, "sin nombre", dim, 1);
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%d/%d", (int)name_.size(), MAX_NAME_LENGTH);
    DrawText(PX + PW - 32 - 8 * (int)strlen(cnt), FY + 8, cnt, dim, 0);
    if (!prefix_.empty()) {
        std::string folder = prefix_ + (name_.empty() ? std::string("...") : name_);
        DrawText(nx, FY + FH - 14, folder.c_str(), dim, 0);
    }

    bool warning = !warn_.empty() && SDL_GetTicks() - warnTime_ < 2500;

    if (keyboard_) {
        const int W = 54, G = 6, H = 40, X0 = 24, Y0 = 140;
        for (int r = 0; r < 4; r++) {
            int n = strlen(rowsUp[r]);
            for (int c = 0; c < n; c++) {
                bool on = (r == krow_ && c == kcol_);
                int x = X0 + c * (W + G), y = Y0 + r * (H + G);
                FillRect(x, y, W, H, on ? acc : card, 6);
                char b[2] = {keyAt(r, c), 0};
                DrawText(x + (W - 16) / 2, y + (H - 16) / 2, b, on ? bg : fg, 1);
            }
        }
        const char *sp[4] = {upper_ ? "abc" : "ABC", "ESPACIO", "BORRAR", "LISTO"};
        const int span[4][2] = {{0, 2}, {2, 4}, {6, 2}, {8, 2}};
        int y = Y0 + 4 * (H + G);
        for (int s = 0; s < 4; s++) {
            bool on = (krow_ == 4 && specialAt(kcol_) == s);
            int x = X0 + span[s][0] * (W + G), w = span[s][1] * (W + G) - G;
            FillRect(x, y, w, H, on ? acc : card, 6);
            int tw = 8 * strlen(sp[s]);
            DrawText(x + (w - tw) / 2, y + (H - 12) / 2, sp[s], on ? bg : fg, 0);
        }
        if (warning) DrawText(32, 398, warn_.c_str(), warnc, 0);
        else DrawText(32, 398, "A escribe   B borra   L / R mueven el cursor", dim, 0);
        return;
    }

    // sin teclado: la ayuda y los botones
    DrawText(44, 146, "A en el nombre abre el teclado", dim, 0);
    if (warning) DrawText(44, 166, warn_.c_str(), warnc, 0);
    else if (name_.empty()) DrawText(44, 166, "Ponle nombre para poder guardar", warnc, 0);

    const int BH = 36, BY = PY + PH - 24 - BH;
    int bx = 616 - (buttonW[0] + buttonW[1] + buttonW[2] + 32);
    for (int i = 0; i < 3; i++) {
        const char *tx = buttonText[i];
        int w = buttonW[i];
        int tx0 = bx + (w - 16 * (int)strlen(tx)) / 2;
        bool on = (selected_ == i + 1);
        bool dead = (i == 1 && name_.empty());
        if (on) {
            FillRect(bx, BY, w, BH, dead ? off : acc, 6);
            DrawText(tx0, BY + 10, tx, dead ? dim : bg, 1);
        } else {
            FrameRect(bx, BY, w, BH, dead ? off : Mix(bg, fg, 30), 6, 2);
            DrawText(tx0, BY + 10, tx, dead ? off : fg, 1);
        }
        bx += w + 16;
    }
    DrawText(32, BY + 5, "Start guarda", dim, 0);
}
