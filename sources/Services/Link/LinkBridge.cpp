// trk2als: Ableton Link (ver LinkBridge.h). Se compila con -std=c++17.

#include "LinkBridge.h"
#include <ableton/Link.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace {
std::unique_ptr<ableton::Link> g_link;
std::atomic<bool> g_on(false);
const double QUANTUM = 4.0;   // un compás de 4/4

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

bool AudioState(double aheadSec, double &beat, double &tempo, bool &playing) {
    if (!g_on.load()) return false;
    ableton::Link &l = link();
    auto state = l.captureAudioSessionState();
    auto t = l.clock().micros() + std::chrono::microseconds((long long)(aheadSec * 1e6));
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

}
