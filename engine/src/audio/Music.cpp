// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "audio/Music.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace osf {

static uint32_t Vlq(const std::vector<uint8_t>& d, size_t& i)
{
    uint32_t v = 0;
    while (i < d.size())
    {
        const uint8_t b = d[i++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return v;
}

bool ParseXmi(const std::vector<uint8_t>& d, Song& out)
{
    out = Song{};
    size_t at = 0;
    for (; at + 8 <= d.size(); ++at)
        if (std::memcmp(&d[at], "EVNT", 4) == 0)
            break;
    if (at + 8 > d.size())
        return false;
    const size_t len = (static_cast<size_t>(d[at + 4]) << 24) | (d[at + 5] << 16) | (d[at + 6] << 8) | d[at + 7];
    std::vector<uint8_t> e(d.begin() + static_cast<long>(at + 8), d.begin() + static_cast<long>(std::min(d.size(), at + 8 + len)));
    size_t i = 0;
    uint32_t tick = 0;
    while (i < e.size())
    {
        const uint8_t b = e[i];
        if (b < 0x80)
        {
            tick += b; // interval bytes add up
            ++i;
            continue;
        }
        ++i;
        const int kind = b & 0xF0;
        if (b == 0xFF)
        {
            const uint8_t type = i < e.size() ? e[i++] : 0;
            const uint32_t n = Vlq(e, i);
            i += n;
            if (type == 0x2F)
                break;
            continue;
        }
        if (b == 0xF0 || b == 0xF7)
        {
            i += Vlq(e, i);
            continue;
        }
        Song::Event ev{ tick, b, 0, 0, 0 };
        if (i < e.size())
            ev.a = e[i++];
        if (kind != 0xC0 && kind != 0xD0 && i < e.size())
            ev.b = e[i++];
        if (kind == 0x90)
            ev.duration = Vlq(e, i);
        out.events.push_back(ev);
        out.length = std::max(out.length, tick + ev.duration);
    }
    return !out.events.empty();
}

void Synth::Start(const Song* song, bool loop)
{
    _song = song;
    _loop = loop;
    _tick = 0;
    _next = 0;
    _program.fill(0);
    _volume.fill(100);
    _expression.fill(127);
    for (auto& v : _voices)
        v.on = false;
}

void Synth::NoteOn(int ch, int note, int vel, double seconds)
{
    Voice* slot = &_voices[0];
    for (auto& v : _voices)
        if (!v.on || v.age > slot->age)
        {
            slot = &v;
            if (!v.on)
                break;
        }
    Voice& v = *slot;
    v = Voice{};
    v.on = true;
    v.channel = ch;
    v.note = note;
    v.freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    v.amp = vel / 127.0 * _volume[ch] / 127.0 * _expression[ch] / 127.0;
    v.length = std::max(0.05, seconds);
    v.noise = 0x1234u + static_cast<uint32_t>(note) * 77u;
}

float Synth::Sample(Voice& v, int rate)
{
    const double t = v.age;
    const double release = 0.12;
    double env;
    const int prog = _program[v.channel];
    const bool drums = v.channel == 9;
    if (drums)
        env = std::exp(-t * (v.note < 40 ? 18.0 : 35.0));
    else if (prog < 8 || (prog >= 24 && prog < 32) || (prog >= 104 && prog < 112))
        env = std::exp(-t * 2.5) * (t < 0.005 ? t / 0.005 : 1.0);              // struck / plucked
    else
        env = std::min(1.0, t / 0.03) * (prog >= 40 && prog < 56 ? 0.8 : 0.9); // sustained
    if (t > v.length)
        env *= std::max(0.0, 1.0 - (t - v.length) / release);
    if (t > v.length + release || (t > 0.05 && env < 0.0005))
    {
        v.on = false;
        return 0;
    }
    double s;
    if (drums)
    {
        v.noise = v.noise * 1103515245u + 12345u;
        s = ((v.noise >> 16) & 0x7FFF) / 16384.0 - 1.0;
        if (v.note < 40)
            s = 0.5 * s + std::sin(2 * M_PI * v.phase) * 0.8; // kick: thump
    }
    else
    {
        const double p = v.phase;
        if (prog >= 32 && prog < 40)
            s = 2.0 * std::fabs(2.0 * (p - std::floor(p + 0.5))) - 1.0;           // bass: triangle
        else if (prog >= 40 && prog < 56)
            s = 0.6 * (2.0 * (p - std::floor(p + 0.5))) + 0.4 * std::sin(2 * M_PI * p); // strings: soft saw
        else if (prog >= 56 && prog < 80)
            s = 0.55 * std::sin(2 * M_PI * p) + 0.3 * std::sin(6 * M_PI * p) + 0.15 * std::sin(10 * M_PI * p); // winds
        else
            s = 0.7 * std::sin(2 * M_PI * p) + 0.3 * std::sin(4 * M_PI * p);      // piano / default
    }
    v.phase += (drums ? (v.note < 40 ? 60.0 : 0.0) : v.freq) / rate;
    v.phase -= std::floor(v.phase);
    v.age += 1.0 / rate;
    return static_cast<float>(s * env * v.amp);
}

void Synth::Render(int32_t* mix, int samples, int rate)
{
    if (!_song)
        return;
    const double ticksPerSample = 120.0 / rate;
    for (int n = 0; n < samples; ++n)
    {
        while (_song && _next < _song->events.size() && _song->events[_next].tick <= _tick)
        {
            const Song::Event& e = _song->events[_next++];
            const int ch = e.status & 15;
            switch (e.status & 0xF0)
            {
            case 0x90:
                if (e.b)
                    NoteOn(ch, e.a, e.b, e.duration / 120.0);
                break;
            case 0xB0:
                if (e.a == 7) _volume[ch] = e.b;
                if (e.a == 11) _expression[ch] = e.b;
                break;
            case 0xC0: _program[ch] = e.a; break;
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
        float acc = 0;
        for (auto& v : _voices)
            if (v.on)
                acc += Sample(v, rate);
        mix[n] += static_cast<int32_t>(acc * 5000.0f);
    }
}

} // namespace osf
