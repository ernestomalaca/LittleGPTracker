#include "SDLGUIWindowImp.h"
#include "Application/Model/Config.h"
#include "Application/Utils/char.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/System/System.h"
#include "UIFramework/BasicDatas/FontConfig.h"
#include "UIFramework/BasicDatas/GUIEvent.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "Resources/hdfont.h"
#include <math.h>
#include <assert.h>
#include <string.h>

SDLGUIWindowImp *instance_;

unsigned short appWidth = 320;
unsigned short appHeight = 240;

SDLGUIWindowImp::SDLGUIWindowImp(GUICreateWindowParams &p) {

    SDLCreateWindowParams &sdlP = (SDLCreateWindowParams &)p;
    cacheFonts_ = sdlP.cacheFonts_;
    framebuffer_ = sdlP.framebuffer_;

    // By default if we are not running a framebuffer device
    // we assumed it's windowed

    windowed_ = !framebuffer_;

    SDL_DisplayMode displayMode;

    // SDL Prioritises screens so just take the first for now.

    int displayModeRet = SDL_GetDisplayMode(0, 0, &displayMode);

    if (displayModeRet < 0) {
        Trace::Error("DISPLAY", "No display mode found.  Error Code: %d.",
                     displayModeRet);
    }

    NAssert(displayModeRet >= 0);

#if defined(PLATFORM_PSP)
    int screenWidth = 480;
    int screenHeight = 272;
    windowed_ = false;
#elif defined(RS97)
    int screenWidth = 320;
    int screenHeight = 240;
    windowed_ = false;
#else
    int screenWidth = displayMode.w;
    int screenHeight = displayMode.h;
#endif

#if defined(RS97)
    /* Pick the best bitdepth for the RS97 as it will select 32 as its default,
     * even though that's slow */
    bitDepth_ = 16;
#else
    bitDepth_ = SDL_BITSPERPIXEL(displayMode.format);
#endif

    const char *driverName = SDL_GetVideoDriver(0);

    Trace::Log("DISPLAY", "Using driver %s. Screen (%d,%d) Bpp:%d", driverName,
               screenWidth, screenHeight, bitDepth_);

    bool fullscreen = false;

    const char *fullscreenValue = Config::GetInstance()->GetValue("FULLSCREEN");
    if ((fullscreenValue) && (!strcmp(fullscreenValue, "YES"))) {
        fullscreen = true;
    }

    if (!strcmp(driverName, "fbcon")) {
        framebuffer_ = true;
        windowed_ = false;
    }

#ifdef PLATFORM_PSP
    mult_ = 1;
#else
    int multFromSize = MIN(screenHeight / appHeight, screenWidth / appWidth);
    const char *mult = Config::GetInstance()->GetValue("SCREENMULT");
    if (mult) {
        mult_ = atoi(mult);
    } else {
        if (framebuffer_) {
            mult_ = multFromSize;
        } else {
            mult_ = 1;
        }
    }
#endif
    // trk2als: a partir de 640x480 (SCREENMULT par) se usa la fuente nítida de
    // 16x16 en vez de la de 8x8 ampliada. HDFONT=NO en config.xml la apaga.
    {
        const char *hd = Config::GetInstance()->GetValue("HDFONT");
        bool wanted = !(hd && !strcmp(hd, "NO"));
        hdFont_ = wanted && (mult_ >= 2) && (mult_ % 2 == 0);
        hdScale_ = hdFont_ ? mult_ / 2 : 1;
        Trace::Log("DISPLAY", "HD font: %s", hdFont_ ? "on" : "off");
    }

    // Create a window that is the requested size

    screenRect_._topLeft._x = 0;
    screenRect_._topLeft._y = 0;
    screenRect_._bottomRight._x = windowed_ ? appWidth * mult_ : screenWidth;
    screenRect_._bottomRight._y = windowed_ ? appHeight * mult_ : screenHeight;

    Trace::Log("DISPLAY", "Creating SDL Window (%d,%d)", screenRect_.Width(),
               screenRect_.Height());
    window_ = SDL_CreateWindow(
        "LittleGPTracker", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screenRect_.Width(), screenRect_.Height(),
        fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN);
    NAssert(window_);

    // Compute the x & y offset to locate our app window

    appAnchorX_ = (screenRect_.Width() - appWidth * mult_) / 2;
    appAnchorY_ = (screenRect_.Height() - appHeight * mult_) / 2;

    SDL_SetWindowIcon(window_, SDL_LoadBMP("lgpt_icon.bmp"));
    surface_ = SDL_GetWindowSurface(window_);

    NAssert(surface_);

    Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
#else
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = 0xff000000;
#endif

    instance_ = this;
    currentColor_ = 0;
    backgroundColor_ = 0;
    foregroundColor_ = 0;
    SDL_ShowCursor(SDL_DISABLE);
    FontConfig();

    if (cacheFonts_) {
        Trace::Log("DISPLAY", "Preparing fonts");
        prepareFonts();
    }
    updateCount_ = 0;
};

SDLGUIWindowImp::~SDLGUIWindowImp() {}

static SDL_Surface *fonts[FONT_COUNT];

void SDLGUIWindowImp::prepareFullFonts() {
    Trace::Log("DISPLAY", "Preparing full font cache");
    Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
#else
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = 0xff000000;
#endif

    for (int i = 0; i < FONT_COUNT; i++) {

        if (fonts[i] == NULL) {
            fonts[i] = SDL_CreateRGBSurface(
                SDL_SWSURFACE, 8 * mult_, 8 * mult_,
                surface_->format->BitsPerPixel, surface_->format->Rmask,
                surface_->format->Gmask, surface_->format->Bmask,
                surface_->format->Amask);
        }
        if (fonts[i] == NULL) {
            Trace::Error("[DISPLAY] Failed to create font surface %d", i);
        } else {
            SDL_LockSurface(fonts[i]);
            int pixelSize = fonts[i]->format->BytesPerPixel;
            unsigned char *bgPtr = (unsigned char *)&backgroundColor_;
            unsigned char *fgPtr = (unsigned char *)&foregroundColor_;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            bgPtr += (4 - pixelSize);
            fgPtr += (4 - pixelSize);
#endif
            const unsigned char *src = font + i * 8;
            unsigned char *dest = (unsigned char *)fonts[i]->pixels;
            for (int y = 0; y < 8; y++) {
                for (int n = 0; n < mult_; n++) {
                    for (int x = 0; x < 8; x++) {
                        unsigned char *pxlPtr = src[x] ? bgPtr : fgPtr;
                        for (int m = 0; m < mult_; m++) {
                            memcpy(dest, pxlPtr, pixelSize);
                            dest += pixelSize;
                        }
                    }
                    dest += fonts[i]->pitch - 8 * pixelSize * mult_;
                }
                if (y < 7)
                    src += FONT_WIDTH;
            }
            SDL_UnlockSurface(fonts[i]);
        };
    };
}

void SDLGUIWindowImp::prepareFonts() {

    Trace::Log("DISPLAY", "Preparing font cache");
    Config *config = Config::GetInstance();

    unsigned char r, g, b;
    const char *value = config->GetValue("BACKGROUND");
    if (value) {
        char2hex(value, &r);
        char2hex(value + 2, &g);
        char2hex(value + 4, &b);
    } else {
        r = 0xF1;
        g = 0xF1;
        b = 0x96;
    }
    backgroundColor_ = SDL_MapRGB(surface_->format, r, g, b);

    value = config->GetValue("FOREGROUND");
    if (value) {
        char2hex(value, &r);
        char2hex(value + 2, &g);
        char2hex(value + 4, &b);
    } else {
        r = 0x77;
        g = 0x6B;
        b = 0x56;
    }
    foregroundColor_ = SDL_MapRGB(surface_->format, r, g, b);

    prepareFullFonts();
}

void SDLGUIWindowImp::DrawChar(const char c, GUIPoint &pos,
                               GUITextProperties &p) {
    int xx, yy;
    transform(pos, &xx, &yy);

    if ((xx < 0) || (yy < 0))
        return;
    if ((xx >= screenRect_._bottomRight._x) ||
        (yy >= screenRect_._bottomRight._y))
        return;
    if ((!framebuffer_) && (updateCount_ < MAX_OVERLAYS)) {
        SDL_Rect *area = updateRects_ + updateCount_++;
        area->x = xx;
        area->y = yy;
        area->h = 8 * mult_;
        area->w = 8 * mult_;
    }

    if (hdFont_) { // trk2als
        drawHDChar((unsigned char)c, xx, yy, p);
        return;
    }

    bool useCache = false;
    if (cacheFonts_ && (currentColor_ == foregroundColor_) && (!p.invert_)) {
        useCache = true;
    }

    if (useCache) {

        SDL_Rect srcRect;
        srcRect.x = 0;
        srcRect.y = 0;
        srcRect.w = 8 * mult_;
        srcRect.h = 8 * mult_;

        SDL_Rect dstRect;
        dstRect.x = xx;
        dstRect.y = yy;
        dstRect.w = 8 * mult_;
        dstRect.h = 8 * mult_;

        unsigned int fontID = c;
        if (fontID < FONT_COUNT) {
            SDL_BlitSurface(fonts[fontID], &srcRect, surface_, &dstRect);
        }

    } else {
        // prepare bg & fg pixel ptr
        int pixelSize = surface_->format->BytesPerPixel;
        unsigned char *bgPtr = (unsigned char *)&backgroundColor_;
        unsigned char *fgPtr = (unsigned char *)&currentColor_;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        bgPtr += (4 - pixelSize);
        fgPtr += (4 - pixelSize);
#endif
        const unsigned char *src = font + c * 8;
        unsigned char *dest = ((unsigned char *)surface_->pixels) +
                              (yy * surface_->pitch) + xx * pixelSize;

        for (int y = 0; y < 8; y++) {
            for (int n = 0; n < mult_; n++) {
                for (int x = 0; x < 8; x++) {
                    unsigned char *pxlPtr =
                        ((src[x]) ^ (unsigned char)p.invert_) ? bgPtr : fgPtr;
                    ;
                    for (int m = 0; m < mult_; m++) {
                        memcpy(dest, pxlPtr, pixelSize);
                        dest += pixelSize;
                    }
                }
                dest += surface_->pitch - 8 * pixelSize * mult_;
            }
            if (y < 7)
                src += FONT_WIDTH;
        }
    }
}

// trk2als: dibuja un caracter de la fuente nítida mezclando el color del texto
// con el fondo según la opacidad de cada pixel (16 tonos por caracter)
void SDLGUIWindowImp::drawHDChar(unsigned char c, int xx, int yy,
                                 GUITextProperties &p) {
    c &= 127;
    Uint32 fg = p.invert_ ? backgroundColor_ : currentColor_;
    Uint32 bg = p.invert_ ? currentColor_ : backgroundColor_;
    Uint8 fr, fgc, fb, br, bgc, bb;
    SDL_GetRGB(fg, surface_->format, &fr, &fgc, &fb);
    SDL_GetRGB(bg, surface_->format, &br, &bgc, &bb);
    Uint32 shades[16];
    for (int i = 0; i < 16; i++) {
        int a = i * 17; // 0..255
        shades[i] = SDL_MapRGB(surface_->format, (br * (255 - a) + fr * a) / 255,
                               (bgc * (255 - a) + fgc * a) / 255,
                               (bb * (255 - a) + fb * a) / 255);
    }
    int pixelSize = surface_->format->BytesPerPixel;
    int size = HDFONT_SIZE * hdScale_;
    if (xx + size > surface_->w || yy + size > surface_->h)
        return;
    const unsigned char *src = hdfont[c];
    for (int y = 0; y < HDFONT_SIZE; y++) {
        for (int sy = 0; sy < hdScale_; sy++) {
            unsigned char *dest = ((unsigned char *)surface_->pixels) +
                                  (yy + y * hdScale_ + sy) * surface_->pitch +
                                  xx * pixelSize;
            for (int x = 0; x < HDFONT_SIZE; x++) {
                Uint32 v = shades[src[y * HDFONT_SIZE + x] >> 4];
                unsigned char *vp = (unsigned char *)&v;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                vp += (4 - pixelSize);
#endif
                for (int sx = 0; sx < hdScale_; sx++) {
                    memcpy(dest, vp, pixelSize);
                    dest += pixelSize;
                }
            }
        }
    }
}

/************************************************************
 trk2als: pinceles (rectángulos redondeados, marcos y texto libre)
 Coordenadas lógicas de 640x480; hdToWin() las lleva a la ventana real.
 ************************************************************/

void SDLGUIWindowImp::markUpdate(int x, int y, int w, int h) {
    if ((!framebuffer_) && (updateCount_ < MAX_OVERLAYS)) {
        SDL_Rect *area = updateRects_ + updateCount_++;
        area->x = x;
        area->y = y;
        area->w = w;
        area->h = h;
    } else {
        updateCount_ = MAX_OVERLAYS; // Flush() actualiza toda la pantalla
    }
}

Uint32 SDLGUIWindowImp::readPixel(int x, int y) {
    int bpp = surface_->format->BytesPerPixel;
    Uint32 v = 0;
    unsigned char *src =
        (unsigned char *)surface_->pixels + y * surface_->pitch + x * bpp;
    unsigned char *vp = (unsigned char *)&v;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    vp += (4 - bpp);
#endif
    memcpy(vp, src, bpp);
    return v;
}

void SDLGUIWindowImp::writePixel(int x, int y, Uint32 v) {
    int bpp = surface_->format->BytesPerPixel;
    unsigned char *dst =
        (unsigned char *)surface_->pixels + y * surface_->pitch + x * bpp;
    unsigned char *vp = (unsigned char *)&v;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    vp += (4 - bpp);
#endif
    memcpy(dst, vp, bpp);
}

// Pinta un rectángulo de esquinas redondeadas fila por fila. thick<=0 lo
// rellena; thick>0 pinta solo un marco de ese grosor (en pixeles de ventana)
void SDLGUIWindowImp::fillRoundRows(int x, int y, int w, int h, int r,
                                    int thick, Uint32 color) {
    if (w <= 0 || h <= 0)
        return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int inset = 0;
        float dy = -1;
        if (j < r) dy = r - j - 0.5f;
        else if (j >= h - r) dy = j - (h - r) + 0.5f;
        if (dy >= 0) {
            float dx = sqrtf(float(r * r) - dy * dy);
            inset = r - int(dx + 0.5f);
        }
        SDL_Rect row;
        row.y = y + j;
        row.h = 1;
        bool full = (thick <= 0) || (j < thick) || (j >= h - thick);
        if (full) {
            row.x = x + inset;
            row.w = w - 2 * inset;
            if (row.w > 0) SDL_FillRect(surface_, &row, color);
        } else {
            int t = thick + (inset > 0 ? 1 : 0);
            row.x = x + inset;
            row.w = t;
            SDL_FillRect(surface_, &row, color);
            row.x = x + w - inset - t;
            SDL_FillRect(surface_, &row, color);
        }
    }
}

void SDLGUIWindowImp::FillRoundRect(GUIRect &r, int radius, GUIColor &c) {
    int x = appAnchorX_ + hdToWin(r.Left());
    int y = appAnchorY_ + hdToWin(r.Top());
    int w = hdToWin(r.Right()) - hdToWin(r.Left());
    int h = hdToWin(r.Bottom()) - hdToWin(r.Top());
    Uint32 color = SDL_MapRGB(surface_->format, c._r & 0xFF, c._g & 0xFF, c._b & 0xFF);
    fillRoundRows(x, y, w, h, hdToWin(radius), 0, color);
    markUpdate(x, y, w, h);
}

void SDLGUIWindowImp::FrameRoundRect(GUIRect &r, int radius, int thick,
                                     GUIColor &c) {
    int x = appAnchorX_ + hdToWin(r.Left());
    int y = appAnchorY_ + hdToWin(r.Top());
    int w = hdToWin(r.Right()) - hdToWin(r.Left());
    int h = hdToWin(r.Bottom()) - hdToWin(r.Top());
    int t = hdToWin(thick);
    if (t < 1) t = 1;
    Uint32 color = SDL_MapRGB(surface_->format, c._r & 0xFF, c._g & 0xFF, c._b & 0xFF);
    fillRoundRows(x, y, w, h, hdToWin(radius), t, color);
    markUpdate(x, y, w, h);
}

// Texto sin fondo: cada pixel de la letra se mezcla con lo que ya hay debajo
void SDLGUIWindowImp::BlendText(const char *s, GUIPoint &pos, GUIColor &c,
                                int scale) {
    if (scale < 1) scale = 1;
    int size = hdToWin(HDFONT_SIZE * scale);
    if (size < 1) return;
    int x = appAnchorX_ + hdToWin(pos._x);
    int y = appAnchorY_ + hdToWin(pos._y);
    int x0 = x;
    int maxX = appAnchorX_ + hdToWin(640), maxY = appAnchorY_ + hdToWin(480);
    for (; *s; s++, x += size) {
        unsigned char ch = ((unsigned char)*s) & 127;
        if (ch == ' ') continue;
        if (x + size > maxX || y + size > maxY || x < 0 || y < 0) continue;
        const unsigned char *g = hdfont[ch];
        for (int dy = 0; dy < size; dy++) {
            int sy = dy * HDFONT_SIZE / size;
            for (int dx = 0; dx < size; dx++) {
                int a = g[sy * HDFONT_SIZE + dx * HDFONT_SIZE / size];
                if (a == 0) continue;
                Uint8 br, bgc, bb;
                if (a < 255) {
                    SDL_GetRGB(readPixel(x + dx, y + dy), surface_->format, &br, &bgc, &bb);
                } else {
                    br = bgc = bb = 0;
                }
                Uint8 r = (br * (255 - a) + (c._r & 0xFF) * a) / 255;
                Uint8 gg = (bgc * (255 - a) + (c._g & 0xFF) * a) / 255;
                Uint8 b = (bb * (255 - a) + (c._b & 0xFF) * a) / 255;
                writePixel(x + dx, y + dy, SDL_MapRGB(surface_->format, r, gg, b));
            }
        }
    }
    markUpdate(x0, y, x - x0, size);
}

void SDLGUIWindowImp::transform(const GUIRect &srcRect, SDL_Rect *dstRect) {
    dstRect->x = srcRect.Left() * mult_ + appAnchorX_;
    dstRect->y = srcRect.Top() * mult_ + appAnchorY_;
    dstRect->w = srcRect.Width() * mult_;
    dstRect->h = srcRect.Height() * mult_;
}

void SDLGUIWindowImp::transform(const GUIPoint &srcPoint, int *x, int *y) {
    *x = appAnchorX_ + srcPoint._x * mult_;
    *y = appAnchorY_ + srcPoint._y * mult_;
}

void SDLGUIWindowImp::DrawString(const char *string, GUIPoint &pos,
                                 GUITextProperties &p, bool overlay) {

    int len = int(strlen(string));
    int xx, yy;
    transform(pos, &xx, &yy);

    if ((!framebuffer_) && (updateCount_ < MAX_OVERLAYS)) {
        SDL_Rect *area = updateRects_ + updateCount_++;
        area->x = xx;
        area->y = yy;
        area->h = 8 * mult_;
        area->w = len * 8 * mult_;
    }

    for (int l = 0; l < len; l++) {
        bool useCache = false;
        if (cacheFonts_ && (currentColor_ == foregroundColor_) &&
            (!p.invert_)) {
            useCache = true;
        }

        if (useCache) {
            SDL_Rect srcRect;
            srcRect.x = 0;
            srcRect.y = 0;
            srcRect.w = 8 * mult_;
            srcRect.h = 8 * mult_;

            SDL_Rect dstRect;
            dstRect.x = xx;
            dstRect.y = yy;
            dstRect.w = 8 * mult_;
            dstRect.h = 8 * mult_;

            unsigned int fontID = string[l];
            if (fontID < FONT_COUNT) {
                SDL_BlitSurface(fonts[fontID], &srcRect, surface_, &dstRect);
            }
        } else {
            // prepare bg & fg pixel ptr
            int pixelSize = surface_->format->BytesPerPixel;
            unsigned char *bgPtr = (unsigned char *)&backgroundColor_;
            unsigned char *fgPtr = (unsigned char *)&currentColor_;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            bgPtr += (4 - pixelSize);
            fgPtr += (4 - pixelSize);
#endif
            const unsigned char *src = font + (string[l] * 8);
            unsigned char *dest = ((unsigned char *)surface_->pixels) +
                                  (yy * surface_->pitch) + xx * pixelSize;

            for (int y = 0; y < 8; y++) {
                for (int n = 0; n < mult_; n++) {
                    for (int x = 0; x < 8; x++) {
                        unsigned char *pxlPtr =
                            ((src[x]) ^ (unsigned char)p.invert_) ? bgPtr
                                                                  : fgPtr;
                        ;
                        for (int m = 0; m < mult_; m++) {
                            memcpy(dest, pxlPtr, pixelSize);
                            dest += pixelSize;
                        }
                    }
                    dest += surface_->pitch - 8 * pixelSize * mult_;
                }
                if (y < 7)
                    src += FONT_WIDTH;
            }
        }
        xx += 8 * mult_;
    }
}

void SDLGUIWindowImp::DrawRect(GUIRect &r) {
    SDL_Rect rect;
    transform(r, &rect);
    SDL_FillRect(surface_, &rect, currentColor_);
};

void SDLGUIWindowImp::Clear(GUIColor &c, bool overlay) {
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = screenRect_.Width();
    rect.h = screenRect_.Height();

    Uint32 newBg =
        SDL_MapRGB(surface_->format, c._r & 0xFF, c._g & 0xFF, c._b & 0xFF);
    if (newBg != backgroundColor_) {
        backgroundColor_ = newBg;
        if (cacheFonts_) {
            prepareFullFonts();
        }
    }
    SDL_FillRect(surface_, &rect, backgroundColor_);

    if (!framebuffer_) {
        SDL_Rect *area = updateRects_;
        area->x = rect.x;
        area->y = rect.y;
        area->w = rect.w;
        area->h = rect.h;
        updateCount_ = 1;
    }
}

void SDLGUIWindowImp::ClearRect(GUIRect &r) {
    SDL_Rect rect;
    transform(r, &rect);
    SDL_FillRect(surface_, &rect, backgroundColor_);
}

// To the app we might have a smaller window
// than the effective one (PSP)

GUIRect SDLGUIWindowImp::GetRect() {
    return GUIRect(0, 0, appWidth, appHeight);
}

// Pushback a SDL event to specify screen has to be redrawn.

void SDLGUIWindowImp::Invalidate() {
    // Todo: SL: Haven't found a good replacement here yet
    SDL_Event event;
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_EXPOSED;
    SDL_PushEvent(&event);
}

void SDLGUIWindowImp::SetColor(GUIColor &c) {
    currentColor_ =
        SDL_MapRGB(surface_->format, c._r & 0xFF, c._g & 0xFF, c._b & 0xFF);
}

void SDLGUIWindowImp::Lock() {
    if (framebuffer_) {
        return;
    }

    if (SDL_MUSTLOCK(surface_)) {
        SDL_LockSurface(surface_);
    }
}

void SDLGUIWindowImp::Unlock() {
    if (framebuffer_) {
        return;
    }

    if (SDL_MUSTLOCK(surface_)) {
        SDL_UnlockSurface(surface_);
    }
}

void SDLGUIWindowImp::Flush() {
    // blit partial updates on resource constrained platforms
    if ((!framebuffer_) && (updateCount_ != 0)) {
        if (updateCount_ < MAX_OVERLAYS) {
            SDL_UpdateWindowSurfaceRects(window_, updateRects_, updateCount_);
        } else {
            SDL_Rect updateRect;
            updateRect.x = screenRect_.Left();
            updateRect.y = screenRect_.Top();
            updateRect.w = screenRect_.Width();
            updateRect.h = screenRect_.Height();
            SDL_UpdateWindowSurfaceRects(window_, &updateRect, 1);
        }
    }
    updateCount_ = 0;
}

void SDLGUIWindowImp::ProcessExpose() {
    // Expose and resize events will cause a new surface to be needed.
    surface_ = SDL_GetWindowSurface(window_);
    _window->Update();
}

void SDLGUIWindowImp::ProcessQuit() {
    GUIPoint p;
    GUIEvent e(p, ET_SYSQUIT);
    _window->DispatchEvent(e);
};

void SDLGUIWindowImp::PushEvent(GUIEvent &event) {
    SDL_Event sdlevent;
    sdlevent.type = SDL_USEREVENT;
    sdlevent.user.data1 = &event;
    SDL_PushEvent(&sdlevent);
};

void SDLGUIWindowImp::ProcessUserEvent(SDL_Event &event) {
    GUIEvent *guiEvent = (GUIEvent *)event.user.data1;
    _window->DispatchEvent(*guiEvent);
    delete (guiEvent);
}
