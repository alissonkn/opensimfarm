// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "audio/Sound.h"

#include <cstdio>
#include <cstdlib>

#include "assets/Megadata.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace osf {

bool DecodeVoc(const std::vector<uint8_t>& d, std::vector<int16_t>& out, int& rate)
{
    static const char kMagic[] = "Creative Voice File";
    if (d.size() < 26 || std::memcmp(d.data(), kMagic, 19) != 0)
        return false;
    size_t o = d[20] | (d[21] << 8);
    rate = 11025;
    out.clear();
    while (o < d.size())
    {
        const int type = d[o];
        if (type == 0 || o + 4 > d.size())
            break;
        const size_t len = d[o + 1] | (d[o + 2] << 8) | (d[o + 3] << 16);
        const size_t body = o + 4;
        if (body + len > d.size())
            break;
        if (type == 1 && len >= 2)
        {
            // sound data: rate divisor, codec (0 = 8-bit unsigned PCM), samples
            rate = 1000000 / (256 - d[body]);
            if (d[body + 1] == 0)
                for (size_t i = body + 2; i < body + len; ++i)
                    out.push_back(static_cast<int16_t>((d[i] - 128) * 256));
        }
        else if (type == 2)
        {
            for (size_t i = body; i < body + len; ++i)
                out.push_back(static_cast<int16_t>((d[i] - 128) * 256));
        }
        else if (type == 3 && len >= 3)
        {
            const size_t n = (d[body] | (d[body + 1] << 8)) + 1;
            out.insert(out.end(), n, 0);
        }
        o = body + len;
    }
    return !out.empty();
}

bool SoundPlayer::Init(const Megadata& mega)
{
    _mega = &mega;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &SoundPlayer::Callback;
    want.userdata = this;
    _dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (!_dev)
        return false;
    _rate = have.freq;
    _channels = std::clamp<int>(have.channels, 1, 8);
    _room.Init(_rate);
    SDL_PauseAudioDevice(_dev, 0);
    return true;
}

SoundPlayer::~SoundPlayer()
{
    if (_dev)
        SDL_CloseAudioDevice(_dev);
}

// Modern Sound's version of an effect: the player's recording, cleaned and brightened (AudioFx). The engine's own
// synthesized effects were tried and removed again on the user's word (2026-11-04).
std::vector<float> ModernEffect(const std::string& name, const std::vector<int16_t>& pcm, int srcRate, int dstRate)
{
    std::vector<float> y = EnhanceEffect(pcm, srcRate, dstRate);
    if (name == "uhoh.voc") // the refusal sound is heard often: well under the others (user: too loud)
        for (float& v : y)
            v *= 0.4f;
    return y;
}

const std::vector<float>* SoundPlayer::Effect(const std::string& name)
{
    auto& cache = _cache[_modern ? 1 : 0];
    auto it = cache.find(name);
    if (it == cache.end())
    {
        std::vector<int16_t> pcm;
        int rate = 0;
        std::vector<float> ready;
        if (DecodeVoc(_mega->Read(name), pcm, rate) && rate > 0)
            ready = _modern ? ModernEffect(name, pcm, rate, _rate) : PlainEffect(pcm, rate, _rate);
        it = cache.emplace(name, std::move(ready)).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}

void SoundPlayer::Place(int cx, int cy, float& gl, float& gr) const
{
    const float vol = _sfxVol / 10.0f;
    if (!_modern || cx < 0 || cy < 0)
    {
        gl = gr = vol * (_modern ? 0.80f : 1.0f);
        return;
    }
    // left / right by the cell's place across the view (equal-power pan, never fully to one side); quieter the
    // farther it is from the middle of the view, but still heard from off screen
    const float half = std::max(4.0f, _lhalf);
    const float dx = (cx + 0.5f - _lx) / half, dy = (cy + 0.5f - _ly) / half;
    const float pan = std::clamp(dx, -1.0f, 1.0f) * 0.8f;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float att = 1.0f / (1.0f + 0.7f * std::max(0.0f, dist - 0.6f));
    const float a = (pan + 1) * 0.78539816f;
    gl = vol * att * std::cos(a) * 1.13f;
    gr = vol * att * std::sin(a) * 1.13f;
}

void SoundPlayer::Play(const std::string& name, int cellX, int cellY)
{
    if (std::getenv("OSF_TRACE_SOUND"))
        std::fprintf(stderr, "sound %.2f %s\n", SDL_GetTicks() / 1000.0, name.c_str()); // comparison runs
    if (!_dev || !_enabled || !_mega || _sfxVol <= 0)
        return;
    const std::vector<float>* pcm = Effect(name);
    if (!pcm)
        return;
    float gl, gr;
    Place(cellX, cellY, gl, gr);
    std::lock_guard<std::mutex> g(_lock);
    if (_voices.size() < 8)
        _voices.push_back({ pcm, 0, gl, gr });
}

void SoundPlayer::PlayMusic(const std::string& name, bool loop)
{
    if (std::getenv("OSF_TRACE_SOUND") && name != _songName)
        std::fprintf(stderr, "music %.2f %s\n", SDL_GetTicks() / 1000.0, name.c_str());
    if (!_dev || !_mega || name == _songName)
        return;
    auto it = _songs.find(name);
    if (it == _songs.end())
    {
        Song song;
        ParseXmi(_mega->Read(name), song);
        it = _songs.emplace(name, std::move(song)).first;
    }
    std::lock_guard<std::mutex> g(_lock);
    _songName = name;
    _songPtr = it->second.events.empty() ? nullptr : &it->second;
    _synth.Stop();
    _modernSynth.Stop();
    if (_songPtr)
    {
        if (_modern)
            _modernSynth.Start(_songPtr, loop);
        else
            _synth.Start(_songPtr, loop);
    }
}

void SoundPlayer::StopMusic()
{
    std::lock_guard<std::mutex> g(_lock);
    _synth.Stop();
    _modernSynth.Stop();
    _songName.clear();
    _songPtr = nullptr;
}

void SoundPlayer::SetMusicEnabled(bool on)
{
    std::lock_guard<std::mutex> g(_lock);
    _musicOn = on;
}

void SoundPlayer::SetModern(bool on)
{
    std::lock_guard<std::mutex> g(_lock);
    if (on == _modern)
        return;
    _modern = on;
    _voices.clear();
    // the song carries on from its start in the other synthesizer
    _synth.Stop();
    _modernSynth.Stop();
    if (_songPtr)
    {
        if (_modern)
            _modernSynth.Start(_songPtr, true);
        else
            _synth.Start(_songPtr, true);
    }
}

void SoundPlayer::SetMusicVolume(int v)
{
    std::lock_guard<std::mutex> g(_lock);
    _musicVol = std::clamp(v, 0, 10);
}

void SoundPlayer::SetEffectsVolume(int v)
{
    std::lock_guard<std::mutex> g(_lock);
    _sfxVol = std::clamp(v, 0, 10);
}

void SoundPlayer::SetListener(float x, float y, float half)
{
    _lx = x;
    _ly = y;
    _lhalf = half;
}

void SoundPlayer::Callback(void* user, Uint8* stream, int len)
{
    auto* self = static_cast<SoundPlayer*>(user);
    auto* out = reinterpret_cast<int16_t*>(stream);
    const int ch = self->_channels;
    const int n = len / 2 / ch;
    std::lock_guard<std::mutex> g(self->_lock);
    const float mv = self->_musicVol / 10.0f;
    if (!self->_modern)
    {
        // the original path: one mono mix, as before, copied to every channel
        self->_mixOld.assign(static_cast<size_t>(n), 0);
        if (self->_musicOn)
            self->_synth.Render(self->_mixOld.data(), n, self->_rate);
        for (int i = 0; i < n; ++i)
        {
            float mix = self->_mixOld[static_cast<size_t>(i)] / 32768.0f * mv;
            for (auto& v : self->_voices)
                if (v.pos < v.pcm->size())
                    mix += (*v.pcm)[v.pos++] * v.gl;
            const int16_t s = static_cast<int16_t>(std::clamp(mix, -1.0f, 1.0f) * 32767.0f);
            for (int c = 0; c < ch; ++c)
                out[i * ch + c] = s;
        }
    }
    else
    {
        self->_l.assign(static_cast<size_t>(n), 0.0f);
        self->_r.assign(static_cast<size_t>(n), 0.0f);
        self->_send.assign(static_cast<size_t>(n), 0.0f);
        if (self->_musicOn)
            self->_modernSynth.Render(self->_l.data(), self->_r.data(), self->_send.data(), n, self->_rate);
        for (int i = 0; i < n; ++i)
        {
            const size_t k = static_cast<size_t>(i);
            float l = self->_l[k] * mv, r = self->_r[k] * mv, send = self->_send[k] * mv * 0.20f;
            for (auto& v : self->_voices)
                if (v.pos < v.pcm->size())
                {
                    const float s = (*v.pcm)[v.pos++];
                    l += s * v.gl;
                    r += s * v.gr;
                    send += s * (v.gl + v.gr) * 0.03f; // effects stay close and dry (user: muffled)
                }
            self->_room.Process(send, l, r);
            l = SoftLimit(l);
            r = SoftLimit(r);
            if (ch == 1)
                out[i] = static_cast<int16_t>((l + r) * 0.5f * 32767.0f);
            else
                for (int c = 0; c < ch; ++c)
                    out[i * ch + c] = static_cast<int16_t>((c % 2 == 0 ? l : r) * 32767.0f);
        }
    }
    self->_voices.erase(std::remove_if(self->_voices.begin(), self->_voices.end(), [](const Voice& v) { return v.pos >= v.pcm->size(); }),
                        self->_voices.end());
}

int SoundPlayer::SelfTest(const Megadata& mega)
{
    // everything in memory: decode, enhance and render, then look at the numbers (no file is written)
    int fails = 0;
    const int rate = 48000;
    auto report = [&](const std::string& what, const std::vector<float>& l, const std::vector<float>& r, double seconds, double cpu) {
        float peak = 0;
        double sum = 0;
        bool finite = true;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
            peak = std::max({ peak, std::fabs(l[i]), std::fabs(r[i]) });
            sum += 0.5 * (static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i]);
        }
        const double rms = l.empty() ? 0 : std::sqrt(sum / l.size());
        const bool ok = finite && peak > 0.01f && peak <= 1.0f;
        fails += !ok;
        std::printf("%-14s %5.1f s  peak %.3f  rms %.3f  cpu %.0f%%  %s\n", what.c_str(), seconds, peak, rms, 100.0 * cpu / std::max(1e-9, seconds),
                    ok ? "ok" : "FAIL");
    };
    for (const char* song : { "amain.xmi", "dfarm.xmi", "ffarm.xmi", "genfarm.xmi", "carnival.xmi", "rodeo.xmi", "cowman.xmi" })
    {
        Song s;
        if (!ParseXmi(mega.Read(song), s))
            continue;
        {
            // which instruments the song uses: channel / program and its note count
            std::map<int, int> notes;
            std::array<int, 16> prog{};
            for (const Song::Event& e : s.events)
            {
                if ((e.status & 0xF0) == 0xC0) prog[e.status & 15] = e.a;
                if ((e.status & 0xF0) == 0x90 && e.b) notes[(e.status & 15) * 1000 + ((e.status & 15) == 9 ? 0 : prog[e.status & 15])]++;
            }
            std::printf("%-14s", song);
            for (auto [k, c] : notes)
                std::printf(" ch%d:%s%d x%d", k / 1000, k / 1000 == 9 ? "drums" : "p", k % 1000, c);
            std::printf("\n");
        }
        ModernSynth syn;
        syn.Start(&s, false);
        Room room;
        room.Init(rate);
        const double seconds = std::min(60.0, s.length / 120.0 + 1.0);
        const int n = static_cast<int>(seconds * rate);
        std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n)), send(static_cast<size_t>(n));
        const auto t0 = std::chrono::steady_clock::now();
        for (int at = 0; at < n; at += 1024)
            syn.Render(&l[static_cast<size_t>(at)], &r[static_cast<size_t>(at)], &send[static_cast<size_t>(at)], std::min(1024, n - at), rate);
        for (int i = 0; i < n; ++i)
        {
            const size_t k = static_cast<size_t>(i);
            float a = l[k] * 0.8f, b = r[k] * 0.8f;
            room.Process(send[k] * 0.16f, a, b);
            l[k] = SoftLimit(a);
            r[k] = SoftLimit(b);
        }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        report(song, l, r, seconds, cpu);
        // the original path's level, for comparison
        Synth old;
        old.Start(&s, false);
        std::vector<int32_t> m(static_cast<size_t>(n), 0);
        for (int at = 0; at < n; at += 1024)
            old.Render(&m[static_cast<size_t>(at)], std::min(1024, n - at), rate);
        std::vector<float> o(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            o[static_cast<size_t>(i)] = std::clamp(m[static_cast<size_t>(i)] / 32768.0f, -1.0f, 1.0f);
        std::printf("  (original)   ");
        report(song, o, o, seconds, 0);
    }
    // every effect: its length and the share of energy above 5 kHz (recording as it
    // is played without Modern Sound, then with it) - the brightener's measure
    auto hfShare = [&](const std::vector<float>& y) {
        double all = 0, hi = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        const double w = 2 * 3.14159265358979 * 5000 / rate, c = std::cos(w), al = std::sin(w) / 1.4142, a0 = 1 + al;
        const double b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
        for (float v : y)
        {
            const double o = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = o;
            all += v * v;
            hi += o * o;
        }
        return all > 0 ? 100.0 * hi / all : 0.0;
    };
    for (const char* fx : { "bird1.voc", "bird2.voc", "boos.voc", "bulldoze.voc", "cheers.voc", "click.voc", "compbeep.voc", "cow1.voc",
                            "cow2.voc", "explode.voc", "flood.voc", "horse1.voc", "pig.voc", "plane.voc", "plopitem.voc", "ploptool.voc",
                            "sheep1.voc", "sheep2.voc", "smack.voc", "swarm.voc", "thunder.voc", "tornado.voc", "uhoh.voc", "wind.voc" })
    {
        std::vector<int16_t> pcm;
        int src = 0;
        if (!DecodeVoc(mega.Read(fx), pcm, src))
            continue;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<float> y = ModernEffect(fx, pcm, src, rate);
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  recording %.2f s  share above 5 kHz: as recorded %4.1f%%, modern %4.1f%%\n", pcm.size() / static_cast<double>(src),
                    hfShare(PlainEffect(pcm, src, rate)), hfShare(y));
        report(fx, y, y, y.size() / static_cast<double>(rate), cpu);
    }
    std::printf("%s\n", fails ? "audiotest FAIL" : "audiotest ok");
    return fails ? 1 : 0;
}

} // namespace osf
