#ifndef SDL_GUI_WINDOW_H_
#define SDL_GUI_WINDOW_H_

#include "UIFramework/Interfaces/I_GUIWindowImp.h"
#include <SDL2/SDL.h>

bool ProcessDEBEvent(SDL_Event &event) ;
void ProcessButtonChange(unsigned short,unsigned short) ;

#define MAX_OVERLAYS 250

struct SDLCreateWindowParams: public GUICreateWindowParams {
	SDLCreateWindowParams():cacheFonts_(true),framebuffer_(false) {} ;
	bool cacheFonts_ ;
	bool framebuffer_ ;
} ;

class SDLGUIWindowImp: public I_GUIWindowImp {

public:

	SDLGUIWindowImp(GUICreateWindowParams &p) ;
	virtual ~SDLGUIWindowImp() ;

public: // I_GUIWindowImp implementation

	virtual void SetColor(GUIColor &) ;
	virtual void DrawRect(GUIRect &) ;
	virtual void DrawChar(const char c,GUIPoint &pos,GUITextProperties &);
	virtual void DrawString(const char *string,GUIPoint &pos,GUITextProperties &,bool overlay=false);
	virtual GUIRect GetRect() ;
	virtual void Invalidate() ;
	virtual void Flush();
	virtual void Lock() ;
	virtual void Unlock() ;
	virtual void Clear(GUIColor &, bool overlay=false) ;
	virtual void ClearRect(GUIRect &) ;
	virtual void PushEvent(GUIEvent &event) ;

public: // Added functionality
	void ProcessExpose() ;
	void ProcessQuit() ;
	void ProcessUserEvent(SDL_Event &event) ;
protected:
	void prepareFonts() ;
	void prepareFullFonts() ;
	void prepareBPP1Fonts() ;
  void transform(const GUIRect &srcRect,SDL_Rect *dstRect);
  void transform(const GUIPoint &srcPoint, int *x, int *y);

private:
    SDL_Window *window_;
    SDL_Surface *surface_;
//	SDL_Surface *offscreen_ ;
    GUIRect screenRect_ ;
	unsigned int currentColor_ ;
	unsigned int backgroundColor_ ;
	unsigned int foregroundColor_ ;
	int bitDepth_ ;
	bool cacheFonts_ ;
	bool framebuffer_ ;
  bool windowed_;
	SDL_Rect updateRects_[MAX_OVERLAYS] ;
	int updateCount_ ;
	int appAnchorX_ ;
	int appAnchorY_ ;
	int mult_ ;
	bool hdFont_ ;   // trk2als: fuente nítida de 16x16 (pantallas de 640x480)
	int hdScale_ ;
	void drawHDChar(unsigned char c,int xx,int yy,GUITextProperties &p) ;
public: // trk2als: pinceles
	virtual bool SupportsShapes() { return true ; } ;
	virtual void FillRoundRect(GUIRect &,int radius,GUIColor &) ;
	virtual void FrameRoundRect(GUIRect &,int radius,int thick,GUIColor &) ;
	virtual void BlendText(const char *,GUIPoint &,GUIColor &,int scale) ;
private:
	int hdToWin(int v) { return (v*mult_)/2 ; } ;
	void markUpdate(int x,int y,int w,int h) ;
	Uint32 readPixel(int x,int y) ;
	void writePixel(int x,int y,Uint32 v) ;
	void fillRoundRows(int x,int y,int w,int h,int r,int thick,Uint32 color) ;
} ;
#endif
