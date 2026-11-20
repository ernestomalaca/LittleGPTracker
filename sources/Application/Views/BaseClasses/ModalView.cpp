#include "ModalView.h"

ModalView::ModalView(View &v)
    : View(v.w_, v.viewData_), finished_(false), returnCode_(0){};

ModalView::~ModalView(){};

int ModalView::GetReturnCode() { return returnCode_; };

bool ModalView::IsFinished() { return finished_; };

void ModalView::EndModal(int returnCode) {
    returnCode_ = returnCode;
    finished_ = true;
};

void ModalView::ClearRect(int x, int y, int w, int h) {
    View::ClearRect(x + left_, y + top_, w, h);
}
void ModalView::DrawString(int x, int y, const char *txt,
                           GUITextProperties &props) {
    View::DrawString(x + left_, y + top_, txt, props);
};

void ModalView::SetWindow(int width, int height) {

    if (width > 36) {
        width = 36;
    };
    if (height > 26) {
        height = 26;
    };

    left_ = 20 - width / 2;
    top_ = 15 - (height + 1) / 2; // trk2als: centrado en la pantalla (30 filas)
    if (top_ < 2) {
        top_ = 2;
    }
    if (SupportsShapes()) {
        // trk2als: borde claro redondeado, como la ventana de ayuda
        ClearRect(-2, -2, width + 4, height + 4);
        int x = (left_ - 2) * 16 + 4, y = (top_ - 2) * 16 + 4;
        FrameRect(x, y, (width + 4) * 16 - 8, (height + 4) * 16 - 8,
                  ColorOf(CD_NORMAL), 6, 3);
        return;
    }
    ClearRect(-1, -1, width + 2, height + 2);

    SetColor(CD_BORDER);
    GUITextProperties props;
    props.invert_ = true;
    char line[41];
    memset(line, ' ', 40);
    line[width + 4] = 0;
    DrawString(-2, -2, line, props);
    DrawString(-2, height + 1, line, props);
    line[1] = 0;
    for (int i = 0; i < height + 2; i++) {
        DrawString(-2, i - 1, line, props);
        DrawString(width + 1, i - 1, line, props);
    }
};

void ModalView::DrawPanel(int x, int y, int w, int h) {
    left_ = top_ = 0;
    int cx0 = x / 16, cy0 = y / 16;
    int cx1 = (x + w - 1) / 16, cy1 = (y + h - 1) / 16;
    View::ClearRect(cx0, cy0, cx1 - cx0 + 1, cy1 - cy0 + 1);
    FillRect(x, y, w, h, ColorOf(CD_BACKGROUND), 6);
    FrameRect(x, y, w, h, ColorOf(CD_NORMAL), 6, 3);
}
