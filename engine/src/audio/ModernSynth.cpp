// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "audio/ModernSynth.h"

#include <algorithm>
#include <cmath>

namespace osf {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2 * kPi;

// PolyBLEP: removes the step discontinuity of a naive saw / square, so the oscillators do not alias
inline double Blep(double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}
inline double Saw(double p, double dt) { return 2.0 * p - 1.0 - Blep(p, dt); }
inline double Square(double p, double dt)
{
    double q = p + 0.5;
    q -= std::floor(q);
    return (p < 0.5 ? 1.0 : -1.0) + Blep(p, dt) - Blep(q, dt);
}
inline void Advance(double& p, double inc)
{
    p += inc;
    p -= std::floor(p);
}

// one step of a TPT state-variable low-pass (resonance k = 1 / Q)
inline float SvfLow(float in, float g, float k, float& s1, float& s2)
{
    const float hp = (in - (k + g) * s1 - s2) / (1 + g * (k + g));
    const float bp = g * hp + s1;
    s1 = g * hp + bp;
    const float lp = g * bp + s2;
    s2 = g * bp + lp;
    return lp;
}

} // namespace

ModernSynth::ModernSynth()
{
    for (auto& v : _voices)
        v.ks.reserve(4096);
}

void ModernSynth::Start(const Song* song, bool loop)
{
    _song = song;
    _loop = loop;
    _tick = 0;
    _next = 0;
    _program.fill(0);
    _volume.fill(100);
    _expression.fill(127);
    _pan.fill(64);
    _mod.fill(0);
    _sustain.fill(false);
    _bend.fill(1.0);
    for (auto& v : _voices)
        v.on = false;
}

void ModernSynth::Stop()
{
    _song = nullptr;
    for (auto& v : _voices)
        v.on = false;
}

void ModernSynth::NoteOn(int ch, int note, int vel, double seconds, int rate)
{
    // a free voice, else the quietest released one, else the oldest
    Voice* slot = nullptr;
    for (auto& v : _voices)
        if (!v.on)
        {
            slot = &v;
            break;
        }
    if (!slot)
        for (auto& v : _voices)
            if (v.released && (!slot || v.relLevel < slot->relLevel))
                slot = &v;
    if (!slot)
    {
        slot = &_voices[0];
        for (auto& v : _voices)
            if (v.age > slot->age)
                slot = &v;
    }
    Voice& v = *slot;
    std::vector<float> keep = std::move(v.ks);
    v = Voice{};
    v.ks = std::move(keep);
    v.on = true;
    v.channel = ch;
    v.note = note;
    v.freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const float velocity = vel / 127.0f;
    v.vel = velocity * velocity * 0.6f + velocity * 0.4f; // a softer touch curve than linear
    v.vel *= static_cast<float>(_volume[ch] / 127.0 * _expression[ch] / 127.0);
    v.length = std::max(0.04, seconds);
    v.noise = 0x9E3779B9u ^ (static_cast<uint32_t>(note) * 2654435761u) ^ static_cast<uint32_t>(ch * 40503);
    float pan = (_pan[ch] - 64) / 64.0f;
    const int prog = _program[ch];
    if (ch == 9)
    {
        v.model = kDrum;
        switch (note)
        {
        case 35: case 36: v.drum = kKick; break;
        case 38: case 40: v.drum = kSnare; break;
        case 37: v.drum = kStick; break;
        case 39: v.drum = kClap; break;
        case 42: case 44: v.drum = kHatClosed; pan += 0.25f; break;
        case 46: v.drum = kHatOpen; pan += 0.25f; break;
        case 49: case 52: case 55: case 57: v.drum = kCrash; pan -= 0.3f; break;
        case 51: case 53: case 59: v.drum = kRide; pan += 0.35f; break;
        case 56: v.drum = kCowbell; break;
        case 41: case 43: case 45: case 47: case 48: case 50:
            v.drum = kTom;
            v.freq = 70.0 + (note - 41) * 14.0;
            pan += (note - 45) * 0.06f;
            break;
        default: v.drum = note >= 60 ? kShaker : kTom; v.freq = 90.0 + (note % 12) * 12.0; break;
        }
        v.length = 10; // drums ring out on their own envelope
    }
    else
    {
        const int family = prog / 8;
        switch (family)
        {
        case 0: v.model = (prog == 4 || prog == 5) ? kEPiano : kPiano; break;
        case 1: v.model = prog >= 11 && prog <= 13 ? kMallet : kBell; break; // vibraphone, marimba, xylophone: mallets
        case 2: v.model = kOrgan; break;
        case 3: v.model = kPluck; break;
        case 4: v.model = kBass; break;
        case 5: v.model = prog >= 44 && prog <= 47 ? (prog == 45 ? kPluck : kStrings) : kStrings; break; // 45 pizzicato
        case 6: v.model = prog >= 52 && prog <= 55 ? kPad : (prog >= 56 ? kBrass : kStrings); break;
        case 7: v.model = kBrass; break;
        case 8: v.model = kReed; break;
        case 9: v.model = kFlute; break;
        case 10: v.model = kLead; break;
        case 11: v.model = kPad; break;
        case 12: v.model = kPad; break;
        case 13: v.model = kPluck; break;   // ethnic: sitar, banjo, shamisen, koto, kalimba ...
        case 14: v.model = prog <= 114 ? kBell : kDrum; break; // tinkle bell, agogo, steel drums; wood block, taiko, toms
        default: v.model = kPiano; break;
        }
        if (v.model == kDrum)
        {
            // pitched percussion on a melodic channel: wood block as a click, taiko / melodic / synth tom as a tom
            v.drum = prog == 115 ? kStick : kTom;
            v.freq = std::clamp(v.freq, 40.0, 600.0);
            v.length = 10;
        }
        if (v.model == kPluck || v.model == kBass)
        {
            // Karplus-Strong: a delay line one period long, filled with a burst of filtered noise; the period is
            // N + 1/2 (the averaging filter) + the fractional all-pass delay d
            const double period = rate / v.freq;
            size_t n = static_cast<size_t>(std::floor(period - 0.5 - 0.1));
            n = std::clamp<size_t>(n, 2, 4095);
            const double d = period - 0.5 - n;
            v.ksFrac = static_cast<float>((1 - d) / (1 + d));
            v.ksLen = n;
            v.ks.assign(n, 0.0f);
            float lp = 0;
            const float bright = v.model == kBass ? 0.35f : 0.35f + 0.5f * velocity;
            for (size_t i = 0; i < n; ++i)
            {
                lp += bright * (Noise(v) - lp);
                v.ks[i] = lp;
            }
            // remove the burst's DC so the string does not thump
            float mean = 0;
            for (float s : v.ks)
                mean += s;
            mean /= static_cast<float>(n);
            for (float& s : v.ks)
                s -= mean;
            // decay per period from the wanted ring time (about 1.5 s high, 4 s low for guitars; 2.5 s for basses)
            const double ring = v.model == kBass ? 2.5 : std::clamp(5.0 - v.note * 0.045, 0.8, 4.0);
            v.ksDamp = static_cast<float>(std::pow(0.001, 1.0 / (ring * v.freq)));
        }
    }
    pan = std::clamp(pan, -1.0f, 1.0f);
    const double a = (pan + 1) * kPi / 4;
    v.gainL = static_cast<float>(std::cos(a));
    v.gainR = static_cast<float>(std::sin(a));
}

// attack / decay / sustain / release per instrument; the note's own length ends the sustain
float ModernSynth::Envelope(Voice& v)
{
    double A = 0.005, D = 1.0, S = 0.8, R = 0.2;
    switch (v.model)
    {
    case kPiano: A = 0.002; D = std::clamp(3.5 - v.note * 0.03, 0.6, 3.0); S = 0.0; R = 0.25; break;
    case kEPiano: A = 0.002; D = 1.6; S = 0.15; R = 0.3; break;
    case kBell: A = 0.001; D = 1.2; S = 0.0; R = 0.6; break;
    case kMallet: A = 0.001; D = std::clamp(0.9 - v.note * 0.006, 0.25, 0.6); S = 0.0; R = 0.2; break;
    case kOrgan: A = 0.006; D = 1; S = 1.0; R = 0.06; break;
    case kPluck: case kBass: A = 0.001; D = 100; S = 1.0; R = 0.12; break; // the string decays by itself
    case kStrings: A = 0.09; D = 1; S = 0.9; R = 0.35; break;
    case kPad: A = 0.35; D = 2; S = 0.85; R = 0.9; break;
    case kBrass: A = 0.03; D = 0.5; S = 0.75; R = 0.15; break;
    case kReed: A = 0.03; D = 0.6; S = 0.8; R = 0.12; break;
    case kFlute: A = 0.06; D = 0.8; S = 0.85; R = 0.15; break;
    case kLead: A = 0.01; D = 0.4; S = 0.7; R = 0.15; break;
    case kDrum: return 1.0f;
    }
    const double t = v.age;
    double e = t < A ? t / A : S + (1 - S) * std::exp(-(t - A) / D);
    if (!v.released && t >= v.length && !_sustain[static_cast<size_t>(v.channel)])
    {
        v.released = true;
        v.relAge = t;
        v.relLevel = static_cast<float>(e);
    }
    if (v.released)
        e = v.relLevel * std::exp(-(t - v.relAge) / (R / 4.6)); // R = time to -40 dB
    if (v.released && e < 1e-4)
        v.on = false;
    else if (!v.released && S == 0.0 && t > A && e < 1e-4)
        v.on = false;
    return static_cast<float>(e);
}

float ModernSynth::Sample(Voice& v, int rate)
{
    const double t = v.age;
    const double fs = rate;
    const double bend = _bend[static_cast<size_t>(v.channel)];
    double out = 0;
    if (v.model == kDrum)
    {
        const float n = Noise(v);
        double e = 0;
        switch (v.drum)
        {
        case kKick:
        {
            const double f = 48.0 + 110.0 * std::exp(-t * 28.0);
            Advance(v.ph[0], f / fs);
            e = std::exp(-t * 7.0);
            out = std::sin(kTwoPi * v.ph[0]) * e + n * 0.25 * std::exp(-t * 300.0);
            break;
        }
        case kSnare:
        {
            Advance(v.ph[0], 185.0 / fs);
            v.hp += 0.35f * (n - v.hp);                     // noise band: high-passed by subtracting a low-pass
            const double body = std::sin(kTwoPi * v.ph[0]) * std::exp(-t * 30.0);
            out = 0.55 * body + 0.8 * (n - v.hp) * std::exp(-t * 16.0);
            e = std::exp(-t * 16.0);
            break;
        }
        case kHatClosed: case kHatOpen: case kCrash: case kRide: case kShaker:
        {
            // metallic noise: noise plus square partials at inharmonic ratios, high-passed
            static const double kMetal[3] = { 3.17, 4.52, 6.31 };
            double metal = 0;
            for (int i = 0; i < 3; ++i)
            {
                Advance(v.ph[i], 205.0 * kMetal[i] / fs);
                metal += v.ph[i] < 0.5 ? 1 : -1;
            }
            const float mix = static_cast<float>(0.6 * n + 0.15 * metal);
            v.hp += 0.55f * (mix - v.hp);
            const float hi = mix - v.hp;
            const double decay = v.drum == kHatClosed ? 45.0 : v.drum == kHatOpen ? 7.0 : v.drum == kCrash ? 1.6 : v.drum == kRide ? 2.8 : 25.0;
            e = std::exp(-t * decay);
            out = hi * e * (v.drum == kShaker ? 0.5 : v.drum == kHatClosed ? 0.7 : 0.55);
            break;
        }
        case kTom:
        {
            const double f = v.freq * (1.0 + 0.6 * std::exp(-t * 20.0));
            Advance(v.ph[0], f / fs);
            e = std::exp(-t * 6.5);
            out = std::sin(kTwoPi * v.ph[0]) * e + n * 0.12 * std::exp(-t * 60.0);
            break;
        }
        case kClap:
        {
            // three quick bursts, then a short tail
            const double b = std::fmod(t, 0.011);
            const double bursts = t < 0.033 ? std::exp(-b * 300.0) : 0.0;
            v.hp += 0.3f * (n - v.hp);
            e = std::exp(-t * 12.0);
            out = (n - v.hp) * (0.8 * bursts + 0.6 * e);
            break;
        }
        case kStick:
            Advance(v.ph[0], 1600.0 / fs);
            e = std::exp(-t * 70.0);
            out = (std::sin(kTwoPi * v.ph[0]) * 0.7 + n * 0.3) * e;
            break;
        case kCowbell:
            Advance(v.ph[0], 540.0 / fs);
            Advance(v.ph[1], 800.0 / fs);
            e = std::exp(-t * 9.0);
            out = (Square(v.ph[0], 540.0 / fs) + Square(v.ph[1], 800.0 / fs)) * 0.25 * e;
            break;
        }
        if (t > 0.01 && e < 1e-4)
            v.on = false;
        v.age += 1.0 / fs;
        return static_cast<float>(out * v.vel * 0.9);
    }

    const float env = Envelope(v);
    if (!v.on)
        return 0;
    // vibrato after a moment for the sustained, singing instruments (plus the mod wheel)
    double vib = 1.0;
    if (v.model == kStrings || v.model == kFlute || v.model == kReed || v.model == kLead || v.model == kPad)
    {
        Advance(v.vib, 5.4 / fs);
        const double depth = std::min(1.0, std::max(0.0, (t - 0.25) / 0.4)) * 0.0035 + _mod[static_cast<size_t>(v.channel)] / 127.0 * 0.01;
        vib = 1.0 + depth * std::sin(kTwoPi * v.vib);
    }
    const double f = v.freq * bend * vib;
    const double dt = std::min(0.45, f / fs);
    auto cutoff = [&](double hz) {
        if (v.gCount-- <= 0)
        {
            v.gCount = 16;
            v.g = static_cast<float>(std::tan(kPi * std::clamp(hz, 30.0, fs * 0.45) / fs));
        }
        return v.g;
    };
    switch (v.model)
    {
    case kPiano:
    {
        // two-operator FM, the brightness falling as the note decays, a slightly detuned second string
        const double index = 1.2 * std::exp(-t * 3.0) + 0.25;
        Advance(v.mph, f / fs);
        Advance(v.ph[0], f / fs);
        Advance(v.ph[1], f * 1.0012 / fs);
        const double m = index * std::sin(kTwoPi * v.mph);
        out = 0.6 * std::sin(kTwoPi * v.ph[0] + m) + 0.4 * std::sin(kTwoPi * v.ph[1] + 0.7 * m);
        out += 0.15 * std::sin(kTwoPi * v.ph[0] * 2) * std::exp(-t * 6.0);
        break;
    }
    case kEPiano:
    {
        const double index = 2.2 * std::exp(-t * 6.0) + 0.3;
        Advance(v.mph, f / fs);
        Advance(v.ph[0], f / fs);
        Advance(v.ph[1], f * 14.0 / fs);
        out = std::sin(kTwoPi * v.ph[0] + index * std::sin(kTwoPi * v.mph)) + 0.12 * std::sin(kTwoPi * v.ph[1]) * std::exp(-t * 25.0);
        out *= 0.8;
        break;
    }
    case kBell:
    {
        // inharmonic FM (ratio 3.5): glockenspiel, celesta, vibraphone, steel drums
        const double index = 3.0 * std::exp(-t * 4.0) + 0.4;
        Advance(v.mph, f * 3.5 / fs);
        Advance(v.ph[0], f / fs);
        out = 0.7 * std::sin(kTwoPi * v.ph[0] + index * std::sin(kTwoPi * v.mph));
        break;
    }
    case kMallet:
    {
        // a wooden bar: fundamental plus the tuned fourth harmonic, struck brighter than it rings
        Advance(v.ph[0], f / fs);
        Advance(v.mph, f * 4.0 / fs);
        out = 0.75 * std::sin(kTwoPi * v.ph[0] + (1.2 * std::exp(-t * 14.0)) * std::sin(kTwoPi * v.mph))
            + 0.2 * std::sin(kTwoPi * v.mph) * std::exp(-t * 9.0);
        break;
    }
    case kOrgan:
    {
        // drawbars 16', 8', 4', 2 2/3', 2' with a slow rotary-style tremolo
        static const double kBar[5] = { 0.5, 1, 2, 3, 4 }, kLvl[5] = { 0.35, 0.5, 0.3, 0.15, 0.12 };
        Advance(v.ph[0], f / fs);
        for (int i = 0; i < 5; ++i)
            out += kLvl[i] * std::sin(kTwoPi * std::fmod(v.ph[0] * kBar[i], 1.0));
        Advance(v.vib, 6.2 / fs);
        out *= 0.75 * (1.0 + 0.08 * std::sin(kTwoPi * v.vib));
        break;
    }
    case kPluck: case kBass:
    {
        if (v.ksLen < 2)
        {
            v.on = false;
            return 0;
        }
        const size_t i = v.ksPos, j = (v.ksPos + 1) % v.ksLen;
        const float avg = 0.5f * (v.ks[i] + v.ks[j]) * v.ksDamp;
        // fractional delay all-pass for exact tuning
        const float y = v.ksFrac * avg + v.apX - v.ksFrac * v.apY;
        v.apX = avg;
        v.apY = y;
        out = v.ks[i];
        v.ks[i] = y;
        v.ksPos = j;
        if (v.model == kBass)
        {
            Advance(v.ph[0], f / fs);
            out = 0.8 * out + 0.45 * std::sin(kTwoPi * v.ph[0]) * std::exp(-t * 1.2); // a round sub tone
        }
        else
            out *= 1.1;
        if (t > 0.05 && std::fabs(out) < 1e-5 && std::fabs(v.ks[j]) < 1e-5)
            v.on = false;
        break;
    }
    case kStrings: case kPad:
    {
        // three detuned saws (an ensemble), low-passed; the pad is darker and slower
        const double det[3] = { 1.0, 1.0045, 0.9957 };
        for (int k = 0; k < 3; ++k)
        {
            Advance(v.ph[k], f * det[k] / fs);
            out += Saw(v.ph[k], dt * det[k]) * (k == 0 ? 0.45 : 0.3);
        }
        const double base = v.model == kPad ? 900.0 : 2200.0;
        out = SvfLow(static_cast<float>(out), cutoff(base + f * 2.0), 1.2f, v.s1, v.s2) * 0.75;
        break;
    }
    case kBrass:
    {
        // a saw whose filter opens with the attack and closes a little after ("blare")
        Advance(v.ph[0], f / fs);
        Advance(v.ph[1], f * 1.003 / fs);
        out = 0.6 * Saw(v.ph[0], dt) + 0.4 * Saw(v.ph[1], dt);
        const double open = std::min(1.0, t / 0.05) * (0.7 + 0.3 * std::exp(-t * 4.0));
        out = SvfLow(static_cast<float>(out), cutoff(300.0 + (f * 5.0 + 1500.0) * open * (0.5 + v.vel)), 1.0f, v.s1, v.s2) * 0.8;
        break;
    }
    case kReed:
    {
        Advance(v.ph[0], f / fs);
        out = Square(v.ph[0], dt) * 0.6 + Saw(v.ph[0], dt) * 0.2;
        out = SvfLow(static_cast<float>(out), cutoff(f * 4.0 + 600.0), 1.6f, v.s1, v.s2) * 0.7;
        break;
    }
    case kFlute:
    {
        Advance(v.ph[0], f / fs);
        const float breath = Noise(v) * 0.08f * static_cast<float>(0.4 + std::exp(-t * 8.0));
        v.hp += 0.2f * (breath - v.hp);
        out = 0.8 * std::sin(kTwoPi * v.ph[0]) + 0.12 * std::sin(2 * kTwoPi * v.ph[0]) + v.hp;
        break;
    }
    case kLead:
    {
        Advance(v.ph[0], f / fs);
        Advance(v.ph[1], f * 1.006 / fs);
        out = 0.5 * Saw(v.ph[0], dt) + 0.4 * Square(v.ph[1], dt);
        out = SvfLow(static_cast<float>(out), cutoff(f * 6.0 + 1200.0), 1.4f, v.s1, v.s2) * 0.6;
        break;
    }
    case kDrum: break;
    }
    v.age += 1.0 / fs;
    return static_cast<float>(out * env * v.vel);
}

void ModernSynth::Render(float* l, float* r, float* send, int samples, int rate)
{
    if (!_song)
        return;
    const double ticksPerSample = 120.0 / rate; // the songs' 120 ticks a second (as the original synth)
    for (int n = 0; n < samples; ++n)
    {
        while (_song && _next < _song->events.size() && _song->events[_next].tick <= _tick)
        {
            const Song::Event& e = _song->events[_next++];
            const size_t ch = e.status & 15;
            switch (e.status & 0xF0)
            {
            case 0x90:
                if (e.b)
                    NoteOn(static_cast<int>(ch), e.a, e.b, e.duration / 120.0, rate);
                break;
            case 0xB0:
                if (e.a == 1) _mod[ch] = e.b;
                if (e.a == 7) _volume[ch] = e.b;
                if (e.a == 10) _pan[ch] = e.b;
                if (e.a == 11) _expression[ch] = e.b;
                if (e.a == 64) _sustain[ch] = e.b >= 64;
                if (e.a == 121) { _sustain[ch] = false; _bend[ch] = 1.0; _mod[ch] = 0; }
                if (e.a == 120 || e.a == 123)
                    for (auto& v : _voices)
                        if (v.on && static_cast<size_t>(v.channel) == ch && !v.released)
                            v.length = 0;
                break;
            case 0xC0: _program[ch] = e.a; break;
            case 0xE0:
            {
                const int value = ((e.b << 7) | e.a) - 8192;
                _bend[ch] = std::pow(2.0, value / 8192.0 * 2.0 / 12.0); // +-2 semitones
                break;
            }
            default: break;
            }
        }
        _tick += ticksPerSample;
        if (_song && _next >= _song->events.size() && _tick > _song->length + 60)
        {
            if (_loop)
            {
                _tick = 0;
                _next = 0;
            }
            else
                _song = nullptr;
        }
        float al = 0, ar = 0;
        for (auto& v : _voices)
            if (v.on)
            {
                const float s = Sample(v, rate);
                al += s * v.gainL;
                ar += s * v.gainR;
            }
        al *= 0.5f;
        ar *= 0.5f;
        l[n] += al;
        r[n] += ar;
        send[n] += (al + ar) * 0.5f;
    }
}

} // namespace osf
