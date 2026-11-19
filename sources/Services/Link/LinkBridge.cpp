// trk2als: Ableton Link (ver LinkBridge.h). Se compila con -std=c++17.

#include "LinkBridge.h"
#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace {
std::unique_ptr<ableton::Link> g_link;
std::atomic<bool> g_on(false);
const double QUANTUM = 4.0;   // un compás de 4/4

// la frecuencia real del audio: cuántas muestras se calculan por segundo del
// reloj de Link, en una ventana de ~30 s (si la consola no va a la que
// creemos, el tempo se iría corriendo)
struct RatePoint { double samples; double micros; };
RatePoint g_rate[32];
int g_rateCount = 0, g_rateIdx = 0;
double g_rateLastMicros = 0;
std::atomic<double> g_sampleRate(0.0);
std::atomic<double> g_aheadMs(0.0);

void measureRate(double samples, double micros) {
    if (micros - g_rateLastMicros < 1e6) return;   // un punto por segundo
    g_rateLastMicros = micros;
    g_rate[g_rateIdx] = RatePoint{samples, micros};
    g_rateIdx = (g_rateIdx + 1) % 32;
    if (g_rateCount < 32) g_rateCount++;
    if (g_rateCount < 4) return;
    const RatePoint &first = g_rate[g_rateCount < 32 ? 0 : g_rateIdx];
    const RatePoint &last = g_rate[(g_rateIdx + 31) % 32];
    double dt = (last.micros - first.micros) / 1e6;
    if (dt > 2.5) g_sampleRate = (last.samples - first.samples) / dt;
}

ableton::Link &link() {
    if (!g_link) g_link.reset(new ableton::Link(120.0));
    return *g_link;
}
}

namespace LinkBridge {

void Enable(bool on) {
    if (on == g_on.load()) return;
    link().enable(on);
    g_on = on;
}

bool Enabled() { return g_on.load(); }

int Peers() { return g_on.load() ? (int)link().numPeers() : 0; }

void EnableStartStop(bool on) { link().enableStartStopSync(on); }

bool AudioState(double sampleTime, double aheadSec, double &beat, double &tempo, bool &playing) {
    if (!g_on.load()) return false;
    ableton::Link &l = link();
    auto state = l.captureAudioSessionState();
    // cuándo se oye esta rebanada: ahora + lo que ya está en la fila del
    // audio (aheadSec lo mide el reproductor). Sin promedios largos: la fila
    // cambia con el tempo y un filtro lento se queda atrás
    auto now = l.clock().micros();
    measureRate(sampleTime, (double)now.count());
    g_aheadMs = aheadSec * 1000.0;
    auto t = now + std::chrono::microseconds((long long)(aheadSec * 1e6));
    beat = state.beatAtTime(t, QUANTUM);
    tempo = state.tempo();
    playing = state.isPlaying();
    return true;
}

void AudioSetPlaying(bool playing, double aheadSec, double atBeat) {
    if (!g_on.load()) return;
    ableton::Link &l = link();
    auto state = l.captureAudioSessionState();
    auto t = l.clock().micros() + std::chrono::microseconds((long long)(aheadSec * 1e6));
    if (playing) state.setIsPlayingAndRequestBeatAtTime(true, t, atBeat, QUANTUM);
    else state.setIsPlaying(false, t);
    l.commitAudioSessionState(state);
}

double Tempo() {
    if (!g_on.load()) return 0;
    return link().captureAppSessionState().tempo();
}

void SetTempo(double bpm) {
    if (!g_on.load()) return;
    ableton::Link &l = link();
    auto state = l.captureAppSessionState();
    state.setTempo(bpm, l.clock().micros());
    l.commitAppSessionState(state);
}

bool IsPlaying() {
    if (!g_on.load()) return false;
    return link().captureAppSessionState().isPlaying();
}

void SetPlaying(bool playing) {
    if (!g_on.load()) return;
    ableton::Link &l = link();
    auto state = l.captureAppSessionState();
    state.setIsPlaying(playing, l.clock().micros());
    l.commitAppSessionState(state);
}

double MeasuredSampleRate() { return g_sampleRate.load(); }
double FilterAheadMs() { return g_aheadMs.load(); }

}
