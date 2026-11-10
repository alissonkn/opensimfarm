// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace osf {

class Megadata;

// 16-colour palette, expanded to 8-bit RGB (6-bit DAC values, c*4 + (c>>4)).
using Palette = std::array<std::array<uint8_t, 3>, 16>;
Palette LoadPalette(const std::vector<uint8_t>& pal);

// A sheet of 16x16 tiles, decoded to 4-bit colour indices (one byte per pixel).
// Source format: 128 bytes per tile = 4 planes x 16 rows x 2 bytes, plane-major.
// In the masked tiles a see-through pixel holds kSeeThrough (the DOS edition's tiles have colour 7 there, the
// Windows edition's have a mask of their own).
constexpr uint8_t kSeeThrough = 16;
struct TileSheet
{
    int count = 0;
    std::vector<uint8_t> pixels; // count * 256 indices
    const uint8_t* Tile(int i) const { return &pixels[static_cast<size_t>(i) * 256]; }
};
TileSheet DecodeTiles16(const std::vector<uint8_t>& data, int maxTiles);

// Everything the map view needs from the original resources.
struct MapGraphics
{
    Palette palette{};
    TileSheet plain;  // 848 plain tiles (base layer ids 0..847)
    TileSheet masked; // 200 masked tiles (overlay ids 0x350..0x417, kSeeThrough = transparent)
    TileSheet events; // twnevent.til: 35-tile banks of the event ground (0 plain, 1 rodeo, 2 fair) over tiles 483-517
    bool windowsEdition = false; // the tiles are the Windows edition's (WindowsEdition.h)
};
bool LoadMapGraphics(const Megadata& mega, MapGraphics& out);
// Put a crop's own pictures (CropDef::art: 8 growth tiles, 4 sign tiles, 2 icons) over crop slot `slot`'s tiles:
// growth 688 + 8 * slot .. +7, sign 539 + 4 * slot .. +3, icons 816 + slot and 832 + slot.
// Returns false if art is too short.
bool ApplyCropArt(TileSheet& sheet, int slot, const std::vector<uint8_t>& art);

} // namespace osf
