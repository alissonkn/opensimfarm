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

// A sheet of square tiles in one of two forms.
//
// Indexed (the original art): 16x16 tiles of 4-bit colour indices, one byte per pixel, coloured by a Palette.
// Source format: 128 bytes per tile = 4 planes x 16 rows x 2 bytes, plane-major.
// In the masked tiles a see-through pixel holds kSeeThrough (the DOS edition's tiles have colour 7 there, the
// Windows edition's have a mask of their own).
//
// Truecolour (a tile pack, assets/TilePack.h): `size` x `size` tiles of straight-alpha ARGB (0xAARRGGBB), `size`
// any value from 8 to 128. The palette plays no part in them. A tile is still one map cell: the engine's geometry
// (cameras, cells, zoom) stays in units of 16 per cell whatever `size` is, and only the pictures are sharper.
constexpr uint8_t kSeeThrough = 16;
constexpr int kLogicalTile = 16; // a tile's edge in the engine's own units (the original tile size)
struct TileSheet
{
    int count = 0;
    int size = kLogicalTile;     // pixels along a tile's edge in the art
    std::vector<uint8_t> pixels; // indexed: count * 256 indices
    std::vector<uint32_t> argb;  // truecolour: count * size * size pixels (then `pixels` is empty)
    bool TrueColour() const { return !argb.empty(); }
    const uint8_t* Tile(int i) const { return &pixels[static_cast<size_t>(i) * 256]; }
    const uint32_t* TileArgb(int i) const { return &argb[static_cast<size_t>(i) * size * size]; }
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
    // true when the tiles are truecolour (a tile pack): the weather palettes and the crop pictures that are patched
    // into the indexed sheets do not apply to them yet
    bool TrueColour() const { return plain.TrueColour(); }
};
bool LoadMapGraphics(const Megadata& mega, MapGraphics& out);
// Put a crop's own pictures (CropDef::art: 8 growth tiles, 4 sign tiles, 2 icons) over crop slot `slot`'s tiles:
// growth 688 + 8 * slot .. +7, sign 539 + 4 * slot .. +3, icons 816 + slot and 832 + slot.
// Returns false if art is too short.
bool ApplyCropArt(TileSheet& sheet, int slot, const std::vector<uint8_t>& art);
// The same for a truecolour sheet: the crop's 14 tiles as `artSize` x `artSize` ARGB pixels each (CropDef::artArgb) over
// the slot's tile ids, as above. False unless the sheet is truecolour with that tile size and the art holds 14 tiles.
bool ApplyCropArtArgb(TileSheet& sheet, int slot, const std::vector<uint32_t>& art, int artSize);

} // namespace osf
