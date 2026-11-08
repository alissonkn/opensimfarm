// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "audio/Music.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace osf {

// the songs in engine/music/, built into the program (music_builtin.cpp, made by tools/EmbedMusic.cmake)
struct BuiltinMusic
{
    const char* file;
    const unsigned char* data;
    unsigned long size;
};
extern const BuiltinMusic kBuiltinMusic[];
extern const int kBuiltinMusicCount;

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

bool ParseMidi(const std::vector<uint8_t>& d, Song& out)
{
    out = Song{};
    auto be32 = [&](size_t i) { return (static_cast<uint32_t>(d[i]) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3]; };
    if (d.size() < 14 || std::memcmp(d.data(), "MThd", 4) != 0 || be32(4) < 6)
        return false;
    const int format = (d[8] << 8) | d[9], tracks = (d[10] << 8) | d[11], division = (d[12] << 8) | d[13];
    if (format > 1 || division <= 0 || (division & 0x8000))
        return false; // format 2 and SMPTE time are not used by songs
    // every event of every track in file ticks; the tempo map turns them into seconds afterwards
    struct Raw { uint64_t tick; int order; uint8_t status, a, b; };
    std::vector<Raw> raw;
    std::vector<std::pair<uint64_t, uint32_t>> tempos; // (tick, microseconds per quarter)
    size_t at = 8 + be32(4);
    for (int t = 0; t < tracks && at + 8 <= d.size(); ++t)
    {
        const size_t len = be32(at + 4);
        if (std::memcmp(&d[at], "MTrk", 4) != 0)
        {
            at += 8 + len; // an unknown chunk
            --t;
            continue;
        }
        size_t i = at + 8;
        const size_t end = std::min(d.size(), i + len);
        uint64_t tick = 0;
        uint8_t running = 0;
        int order = 0;
        while (i < end)
        {
            uint32_t delta = 0;
            for (int k = 0; k < 4 && i < end; ++k)
            {
                const uint8_t b = d[i++];
                delta = (delta << 7) | (b & 0x7F);
                if (!(b & 0x80))
                    break;
            }
            tick += delta;
            if (i >= end)
                break;
            uint8_t status = d[i];
            if (status == 0xFF)
            {
                const uint8_t type = i + 1 < end ? d[i + 1] : 0;
                i += 2;
                uint32_t n = 0;
                for (int k = 0; k < 4 && i < end; ++k)
                {
                    const uint8_t b = d[i++];
                    n = (n << 7) | (b & 0x7F);
                    if (!(b & 0x80))
                        break;
                }
                const size_t m = std::min<size_t>(n, end - i);
                const std::string text(reinterpret_cast<const char*>(d.data() + i), m);
                if (type == 0x51 && m == 3)
                    tempos.push_back({ tick, (static_cast<uint32_t>(d[i]) << 16) | (d[i + 1] << 8) | d[i + 2] });
                else if (type == 0x03 && out.title.empty())
                    out.title = text;
                else if (type == 0x01 && text.rfind("mood:", 0) == 0)
                    out.mood = text.substr(5);
                i += m;
                if (type == 0x2F)
                    break;
                continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
                ++i;
                uint32_t n = 0;
                for (int k = 0; k < 4 && i < end; ++k)
                {
                    const uint8_t b = d[i++];
                    n = (n << 7) | (b & 0x7F);
                    if (!(b & 0x80))
                        break;
                }
                i += n;
                continue;
            }
            if (status & 0x80)
                ++i, running = status;
            else
                status = running; // running status: the data byte is already at i
            if (!(status & 0x80))
                return false;
            const int kind = status & 0xF0;
            Raw r{ tick, order++, status, 0, 0 };
            if (i < end)
                r.a = d[i++] & 0x7F;
            if (kind != 0xC0 && kind != 0xD0 && i < end)
                r.b = d[i++] & 0x7F;
            raw.push_back(r);
        }
        at += 8 + len;
    }
    if (raw.empty())
        return false;
    std::stable_sort(raw.begin(), raw.end(), [](const Raw& x, const Raw& y) { return x.tick < y.tick; });
    std::stable_sort(tempos.begin(), tempos.end());
    // file ticks -> 120 ticks a second (default tempo 120 beats a minute)
    auto when = [&](uint64_t tick) {
        double sec = 0;
        uint64_t from = 0;
        uint32_t us = 500000;
        for (const auto& [tt, tempo] : tempos)
        {
            if (tt >= tick)
                break;
            sec += static_cast<double>(tt - from) * us / 1e6 / division;
            from = tt;
            us = tempo;
        }
        sec += static_cast<double>(tick - from) * us / 1e6 / division;
        return static_cast<uint32_t>(std::lround(sec * 120.0));
    };
    // note-ons get their duration from the matching note-off (or note-on at velocity 0), first in first out
    std::map<int, std::vector<size_t>> open; // channel * 128 + note -> indices into out.events
    for (const Raw& r : raw)
    {
        const int kind = r.status & 0xF0, key = (r.status & 15) * 128 + r.a;
        const uint32_t t = when(r.tick);
        if (kind == 0x90 && r.b > 0)
        {
            open[key].push_back(out.events.size());
            out.events.push_back({ t, r.status, r.a, r.b, 0 });
        }
        else if (kind == 0x80 || kind == 0x90)
        {
            auto it = open.find(key);
            if (it != open.end() && !it->second.empty())
            {
                Song::Event& e = out.events[it->second.front()];
                e.duration = std::max<uint32_t>(1, t - e.tick);
                it->second.erase(it->second.begin());
            }
        }
        else if (kind == 0xB0 || kind == 0xC0)
            out.events.push_back({ t, r.status, r.a, r.b, 0 });
        out.length = std::max(out.length, t);
    }
    for (const auto& [key, list] : open)
        for (size_t k : list)
            out.events[k].duration = std::max<uint32_t>(1, out.length - out.events[k].tick); // never released: to the end
    for (const Song::Event& e : out.events)
        out.length = std::max(out.length, e.tick + e.duration);
    return !out.events.empty();
}

const std::vector<BuiltinSongInfo>& BuiltinSongs()
{
    static const std::vector<BuiltinSongInfo> list = [] {
        std::vector<BuiltinSongInfo> v;
        for (int i = 0; i < kBuiltinMusicCount; ++i)
        {
            Song s;
            if (ParseMidi(std::vector<uint8_t>(kBuiltinMusic[i].data, kBuiltinMusic[i].data + kBuiltinMusic[i].size), s))
                v.push_back({ kBuiltinMusic[i].file, s.title.empty() ? kBuiltinMusic[i].file : s.title, s.mood });
        }
        return v;
    }();
    return list;
}

bool LoadBuiltinSong(const std::string& file, Song& out)
{
    for (int i = 0; i < kBuiltinMusicCount; ++i)
        if (file == kBuiltinMusic[i].file)
            return ParseMidi(std::vector<uint8_t>(kBuiltinMusic[i].data, kBuiltinMusic[i].data + kBuiltinMusic[i].size), out);
    return false;
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
