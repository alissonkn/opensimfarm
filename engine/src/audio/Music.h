// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace osf {

// A song from an .xmi file (Miles extended MIDI): the EVNT chunk holds MIDI events whose
// note-ons carry their own duration and whose delays are runs of interval bytes; playback runs at 120 ticks/s.
struct Song
{
    struct Event { uint32_t tick; uint8_t status, a, b; uint32_t duration; };
    std::vector<Event> events;
    uint32_t length = 0;
    std::string title, mood; // a standard MIDI file's track name and its "mood:<game song>" text (the engine's songs)
};
bool ParseXmi(const std::vector<uint8_t>& data, Song& out);
// A standard MIDI file (format 0 or 1), turned into the same events: times at 120 ticks/s through the file's tempo
// changes, each note-on with the duration up to its note-off
bool ParseMidi(const std::vector<uint8_t>& data, Song& out);

// The engine's own songs (engine/music/*.mid, composed for OpenSimFarm), built into the program
struct BuiltinSongInfo
{
    std::string file, title, mood;
};
const std::vector<BuiltinSongInfo>& BuiltinSongs();
bool LoadBuiltinSong(const std::string& file, Song& out);

// A small General-MIDI-style software synthesizer (the original used FM instrument banks of DOS sound drivers).
class Synth
{
public:
    void Start(const Song* song, bool loop);
    void Stop() { _song = nullptr; for (auto& v : _voices) v.on = false; }
    bool Playing() const { return _song != nullptr; }
    void Render(int32_t* mix, int samples, int rate);   // adds to mix

private:
    struct Voice { bool on = false; int channel = 0, note = 0; double phase = 0, freq = 0, amp = 0, age = 0, length = 0; uint32_t noise = 1; };
    void NoteOn(int ch, int note, int vel, double seconds);
    float Sample(Voice& v, int rate);

    const Song* _song = nullptr;
    bool _loop = false;
    double _tick = 0;
    size_t _next = 0;
    std::array<int, 16> _program{}, _volume{}, _expression{};
    std::array<Voice, 32> _voices{};
};

} // namespace osf
