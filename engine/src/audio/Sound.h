// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "audio/AudioFx.h"
#include "audio/ModernSynth.h"
#include "audio/Music.h"

#include <SDL.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace osf {

class Megadata;

// Decodes a Creative Voice File (8-bit unsigned mono PCM blocks), or a PCM .WAV file (the Windows edition), to signed
// 16-bit samples.
bool DecodeVoc(const std::vector<uint8_t>& data, std::vector<int16_t>& samples, int& rate);
// Modern Sound's version of one effect: the cleaned, brightened recording (AudioFx)
std::vector<float> ModernEffect(const std::string& name, const std::vector<int16_t>& pcm, int srcRate, int dstRate);

// Sound effects from the player's archive and the music, mixed by SDL. Silent when there is
// no audio device. Two ways to play them (Options > Modern Sound, settings "modernsound"):
// * original: the effects straight-line resampled, mono, the simple built-in synthesizer for the songs (as before);
// * modern (an engine addition): the effects cleaned up and resampled in memory (AudioFx.h), placed left / right and
//   nearer / farther by where they happen on the map, a small room around everything, the songs played by
//   ModernSynth. The player's files are only read; nothing is written or kept outside the running program.
class SoundPlayer
{
public:
    bool Init(const Megadata& mega);
    ~SoundPlayer();
    // cellX / cellY: the map cell the sound comes from (-1 = not on the map: menus, windows, warnings)
    void Play(const std::string& name, int cellX = -1, int cellY = -1);
    void SetEnabled(bool on) { _enabled = on; }
    bool Enabled() const { return _enabled; }
    // music: name = an .xmi from the archive or one of the engine's songs (.mid, BuiltinSongs); the same song is not
    // restarted while it plays
    // (one played to its end without looping starts over when asked for again)
    void PlayMusic(const std::string& name, bool loop = true);
    bool MusicEnded() const; // the song was played to its end without looping (the radio's next song)
    void StopMusic();
    void SetMusicEnabled(bool on);
    bool MusicEnabled() const { return _musicOn; }
    const std::string& CurrentSong() const { return _songName; }

    void SetModern(bool on);
    bool Modern() const { return _modern; }
    // 0 .. 10 (settings "musicvolume", "sfxvolume")
    void SetMusicVolume(int v);
    void SetEffectsVolume(int v);
    int MusicVolume() const { return _musicVol; }
    int EffectsVolume() const { return _sfxVol; }
    // the middle of the farm view and half its width, in cells (for placing the map sounds)
    void SetListener(float cellX, float cellY, float halfWidth);

    // headless check of the modern path: renders the songs and effects in memory and prints levels (--audiotest)
    static int SelfTest(const Megadata& mega);

private:
    static void Callback(void* self, Uint8* stream, int len);
    struct Voice { const std::vector<float>* pcm; size_t pos; float gl, gr; };
    const std::vector<float>* Effect(const std::string& name);
    void Place(int cellX, int cellY, float& gl, float& gr) const;

    const Megadata* _mega = nullptr;
    SDL_AudioDeviceID _dev = 0;
    int _rate = 48000, _channels = 2;
    bool _enabled = true, _modern = true, _musicOn = true;
    int _musicVol = 8, _sfxVol = 8;
    float _lx = 48, _ly = 48, _lhalf = 20;
    std::map<std::string, std::vector<float>> _cache[2]; // [modern] effects at the device rate, float
    std::vector<Voice> _voices;
    mutable std::mutex _lock;
    std::map<std::string, Song> _songs;
    const Song* _songPtr = nullptr;
    Synth _synth;
    ModernSynth _modernSynth;
    Room _room;
    std::string _songName;
    bool _songLoop = true;
    std::vector<int32_t> _mixOld;
    std::vector<float> _l, _r, _send;
};

} // namespace osf
