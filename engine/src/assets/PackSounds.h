// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/PackTables.h"
#include "assets/TilePack.h"

#include <string>

namespace osf {

// A pack's sounds: the sound effects and songs the game asks for by name ("click.voc", "uhoh.voc", "amain.xmi"...), as
// ordinary files, in an optional "sounds" block of tilepack.json:
//
//   "sounds": { "click.voc": "sounds/click.wav", "uhoh.voc": "sounds/uhoh.wav", "genfarm.xmi": "music/farm.mid" }
//
// The key is the name the game uses (an effect ends in .voc, a song in .xmi); the value is a file in the pack: a .wav
// (PCM, 8 or 16 bits, one or two channels, any rate) for an effect, a standard MIDI file (.mid) for a song. The engine
// already reads both under those names (as it does for the Windows edition). A game song the pack leaves out is
// played by an engine song of the same mood. Files are checked for what they claim to be (RIFF WAVE, MThd), at most
// 8 MB each.
//
// The names the game uses: effects bird1 bird2 boos bulldoze cheers click compbeep cow1 cow2 explode flood horse1 pig
// plane plopitem ploptool rooster sheep1 sheep2 smack swarm thunder tornado uhoh wind (.voc); songs alien amain carnival
// cowman dfarm ffarm genfarm rodeo (.xmi).

// Reads the "sounds" block of a manifest's text, the files through `read`, into `out` as the archive entries the game reads
// (the files' bytes under the keys). No block: nothing. False with `error` set when anything in it is wrong (`out` is
// unchanged then).
bool ParsePackSounds(const std::string& manifestJson, PackReader read, void* user, TableBlobs& out, std::string& error);

// Reads `dir`/tilepack.json's sounds (a folder without a manifest is an error).
bool LoadPackSounds(const std::string& dir, TableBlobs& out, std::string& error);

} // namespace osf
