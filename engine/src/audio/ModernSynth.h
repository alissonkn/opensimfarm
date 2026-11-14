// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "audio/Music.h"

#include <array>
#include <cstdint>
#include <vector>

namespace osf {

// The engine's "modern sound" synthesizer (an engine addition, Options > Advanced): plays the notes of the
// player's own song files with instruments made here in code (no recorded instruments, no sound banks):
// FM piano / bells / electric piano, drawbar organ, plucked strings (Karplus-Strong), detuned, filtered strings, pads
// and brass, reeds and flutes with breath and vibrato, and a synthesized General-MIDI drum kit. Stereo, with the
// songs' own pan, volume, expression, sustain-pedal and pitch-bend controls.
class ModernSynth
{
public:
    ModernSynth();
    void Start(const Song* song, bool loop);
    void Stop();
    bool Playing() const { return _song != nullptr; }
    // adds the dry stereo signal to l / r and the reverb send to send (all float, full scale 1)
    void Render(float* l, float* r, float* send, int samples, int rate);

private:
    enum Model : uint8_t { kPiano, kEPiano, kBell, kMallet, kOrgan, kPluck, kBass, kStrings, kPad, kBrass, kReed, kFlute, kLead, kDrum };
    enum Drum : uint8_t { kKick, kSnare, kHatClosed, kHatOpen, kCrash, kRide, kTom, kClap, kStick, kCowbell, kShaker };
    struct Voice
    {
        bool on = false, released = false;
        Model model = kPiano;
        Drum drum = kKick;
        int channel = 0, note = 0;
        double freq = 0, age = 0, length = 0, relAge = 0;
        float vel = 0, relLevel = 0, gainL = 0, gainR = 0;
        double ph[3] = { 0, 0, 0 }, mph = 0, vib = 0;
        float s1 = 0, s2 = 0, hp = 0, last = 0;            // filter states
        float g = 0;                                         // filter coefficient (refreshed every 16 samples)
        int gCount = 0;
        uint32_t noise = 1;
        std::vector<float> ks;                               // plucked-string delay line
        size_t ksPos = 0, ksLen = 0;
        float ksFrac = 0, apX = 0, apY = 0, ksDamp = 0.5f;
    };
    void NoteOn(int ch, int note, int vel, double seconds, int rate);
    float Sample(Voice& v, int rate);
    float Envelope(Voice& v);
    float Noise(Voice& v)
    {
        v.noise = v.noise * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(v.noise)) / 2147483648.0f;
    }

    const Song* _song = nullptr;
    bool _loop = false;
    double _tick = 0;
    size_t _next = 0;
    std::array<int, 16> _program{}, _volume{}, _expression{}, _pan{}, _mod{};
    std::array<bool, 16> _sustain{};
    std::array<double, 16> _bend{};                          // frequency factor
    std::array<Voice, 40> _voices{};
};

} // namespace osf
