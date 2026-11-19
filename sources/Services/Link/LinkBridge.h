#ifndef _LINK_BRIDGE_H_
#define _LINK_BRIDGE_H_

// trk2als: Ableton Link (https://github.com/Ableton/link, GPL v2+). La
// librería pide C++17; el resto de Chancho es C++03, así que todo pasa por
// estas funciones (LinkBridge.cpp se compila aparte).

#ifdef ABLETON_LINK
namespace LinkBridge {
    // prender / apagar (la primera vez crea la sesión y sus hilos)
    void Enable(bool on);
    bool Enabled();
    int Peers();
    // seguir el play/stop de los demás (y avisarles el nuestro)
    void EnableStartStop(bool on);

    // hilo de audio: el tiempo (beats), el tempo y si la sesión está tocando,
    // para dentro de 'aheadSec' segundos (lo que tarda en oírse lo que se
    // calcula ahora). false si Link está apagado
    bool AudioState(double aheadSec, double &beat, double &tempo, bool &playing);
    // hilo de audio: avisa que empezamos a tocar en el compás 'atBeat'
    void AudioSetPlaying(bool playing, double aheadSec, double atBeat);

    // hilo de la interfaz
    double Tempo();
    void SetTempo(double bpm);
    bool IsPlaying();
    void SetPlaying(bool playing);
}
#else
// sin Link (otras plataformas): todo apagado
namespace LinkBridge {
    inline void Enable(bool) {}
    inline bool Enabled() { return false; }
    inline int Peers() { return 0; }
    inline void EnableStartStop(bool) {}
    inline bool AudioState(double, double &, double &, bool &) { return false; }
    inline void AudioSetPlaying(bool, double, double) {}
    inline double Tempo() { return 0; }
    inline void SetTempo(double) {}
    inline bool IsPlaying() { return false; }
    inline void SetPlaying(bool) {}
}
#endif

#endif
