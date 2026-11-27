
#include "Mixer.h"
#include "Application/Utils/HexBuffers.h"

Mixer::Mixer():Persistent("MIXER")  {
	Clear() ;
} ;

Mixer::~Mixer() {
} ;

void Mixer::Clear() {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channelBus_[i]=i ;
        channelVolume_[i] = 0xFF;
        channelHPF_[i] = 0; // 0=OFF, 1=20Hz, 2=90Hz
        channelLPF_[i] = 0; // 0=OFF, else frequency in Hz (20-20000)
        channelSend_[SEND_ECO][i] = 0;
        channelSend_[SEND_REV][i] = 0;
    }
    // trk2als: los efectos de envío, de fábrica: eco de corchea con puntillo,
    // repitiendo a la mitad, medio opaco; reverb mediana, medio brillante
    fx_[FX_ECO_TIME] = 2;
    fx_[FX_ECO_BACK] = 50;
    fx_[FX_ECO_TONE] = 50;
    fx_[FX_REV_SIZE] = 50;
    fx_[FX_REV_TONE] = 60;
} ;

void Mixer::SaveContent(TiXmlNode *node) {
    saveHexBuffer(node, "BUS", (unsigned char *)channelBus_,
                  SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "VOL", channelVolume_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "HPF", channelHPF_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "LPF", channelLPF_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "SENDECO", channelSend_[SEND_ECO], SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "SENDREV", channelSend_[SEND_REV], SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "FX", fx_, FX_COUNT);
} ;

void Mixer::RestoreContent(TiXmlElement *element) {
    TiXmlElement *current = element->FirstChildElement();
    while (current) {
        const char *value = current->Value();
        if (!strcmp("BUS", value)) {
            restoreHexBuffer(current, (unsigned char *)channelBus_);
        } else if (!strcmp("VOL", value)) {
            restoreHexBuffer(current, channelVolume_);
        } else if (!strcmp("HPF", value)) {
            restoreHexBuffer(current, channelHPF_);
        } else if (!strcmp("LPF", value)) {
            restoreHexBuffer(current, (unsigned char *)channelLPF_);
        } else if (!strcmp("SENDECO", value)) {
            restoreHexBuffer(current, channelSend_[SEND_ECO]);
        } else if (!strcmp("SENDREV", value)) {
            restoreHexBuffer(current, channelSend_[SEND_REV]);
        } else if (!strcmp("FX", value)) {
            restoreHexBuffer(current, fx_);
        }
        current = current->NextSiblingElement();
    }
    // trk2als: cada pista suena en su bus (los medidores lo dan por hecho);
    // proyectos guardados tras borrar pistas traían el ruteo recorrido
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) channelBus_[i] = i;
}
