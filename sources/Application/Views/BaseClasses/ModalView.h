
#ifndef _MODAL_VIEW_H_
#define _MODAL_VIEW_H_

#include "View.h"

class ModalView : public View {
  public:
    ModalView(View &);
    virtual ~ModalView();

    bool IsFinished();
    int GetReturnCode();
    // trk2als: la ventana se dibuja toda con figuras (no con la rejilla)
    virtual bool UsesShapes() { return false; }

  protected:
    void SetWindow(int width, int height);
    virtual void ClearRect(int x, int y, int w, int h);
    virtual void DrawString(int x, int y, const char *txt,
                            GUITextProperties &props);
    void EndModal(int returnCode);
    // trk2als: el recuadro como el de la ayuda (fondo oscuro, borde claro),
    // en pixeles; borra la rejilla de abajo
    void DrawPanel(int x, int y, int w, int h);

  private:
    bool finished_;
    int returnCode_;
    int left_;
    int top_;
};
#endif