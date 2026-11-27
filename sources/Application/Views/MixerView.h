#ifndef _MIXER_VIEW_H_
#define _MIXER_VIEW_H_

#include "BaseClasses/View.h"
#include "ViewData.h"

class MixerView: public View {
public:
	MixerView(GUIWindow &w,ViewData *viewData) ;
	~MixerView() ;
	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType ,unsigned int tick=0) ;
    virtual void AnimationUpdate();
    virtual void OnFocus() ;
protected:
	void processNormalButtonMask(unsigned int mask) ;
    void processSelectionButtonMask(unsigned int mask) ;
	void onStart() ;
	void onStop() ;
	void updateCursor(int dx,int dy)  ;
    void toggleMute();
    void toggleSolo();
    void unMuteAll();

private:
	const char *song_ ;
    int soloChannel_;
    int mixerRow_; // 0=bus, 1=volume, 2=hpf, 3=lpf

    struct {                      // .Clipboard structure
        bool active_ ;            // .If currently making a selection
        unsigned char *data_ ;    // .Null if clipboard empty
        int x_ ;                  // .Current selection positions
        int y_ ;                  // .
        int offset_ ;             // .
        int width_ ;              // .Size of selection
        int height_ ;             // .
    } clipboard_;

    int saveX_;
    int saveY_ ;
	int saveOffset_ ;
	bool invertBatt_ ;
    ViewType previousViewType_; // Track which view we came from for easy
                                // navigation back
    void updateVu();
    void drawStrips();          // trk2als: la mixer con figuras: una tira por pista (solo volumen)
    float vuL_[9], vuR_[9];
    int vuShown_[18];           // lo que se pintó (para no repintar de más)
    unsigned long lastVu_;
    bool masterOn_;             // con todas las pistas ocupadas: el master encima de la última (R / L)
    int tapKey_;
    unsigned short gesture_;
    int fxRow_;                 // trk2als: 0 = envío al ECO, 1 = a la REVERB, 2 = el volumen
    int stripCols();            // cuántas columnas (las de la Session)
    bool masterShown();         // el master a la vista (última columna libre, o encima con R)
    void editSend(unsigned short mask);    // el gesto de mute / solo ya hecho (no se repite al sostener)
    void DrawVuBars();          // Draw VU bars - called from both DrawView and
                                // AnimationUpdate
    int vuBarHeightsL_[9];  // Left channel smoothed bar heights with slew rate decay (8 channels + 1 master)
    int vuBarHeightsR_[9];  // Right channel smoothed bar heights with slew rate decay (8 channels + 1 master)
};
#endif
