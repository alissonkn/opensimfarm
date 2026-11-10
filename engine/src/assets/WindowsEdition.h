// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace osf {

// SimFarm for Windows (WSIMFARM.EXE) keeps only the tile banks and the tables in its MEGADATA archive. Its pictures
// are bitmaps inside WSIMFARM.EXE, its sounds .WAV files and its songs .MID files in the SOUND folder. The functions
// here read the player's Windows files and make from them the archive entries the engine reads: the same names and
// formats as the DOS edition's, in the Windows edition's own colours (the standard Windows colours).
//
// Besides the DOS names it adds:
//   uicolour.map   16 bytes: for each interface colour the engine draws with (Colour in Gfx.h) the Windows colour
//   mskdtile.msk   1 bit a pixel, 32 bytes a tile: the masked tiles' see-through pixels
// cursors.til ends with the six cursors' hot spots (x, y bytes). Of the archive's own entries the picture banks (.btl)
// are turned into the DOS layout and far_mapc.mbm (the overview colours) is replaced by colours taken from the tiles.

// true when gameDir holds the Windows edition's program
bool IsWindowsEdition(const std::string& gameDir);

// Adds the entries made from the Windows files to `out` (lower-case names), given the entries of the edition's own
// archive that are changed. False with `error` set when the program's pictures cannot be read.
bool MakeWindowsEntries(const std::string& gameDir, const std::map<std::string, std::vector<uint8_t>>& archive,
                        std::map<std::string, std::vector<uint8_t>>& out, std::string& error);

} // namespace osf
