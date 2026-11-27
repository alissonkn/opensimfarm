// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"

#include <cstdint>
#include <vector>

namespace osf {

// Modern Render: the player's pictures upscaled in memory for the size they are shown at (the files are never
// changed), with MMPX, a pixel-art scaler that only ever uses the picture's own colours: each pixel becomes 2 x 2,
// and a quarter takes a neighbour's colour where the pattern around it shows a diagonal or 2:1 slope, a corner or a
// thin line crossing. Diagonals and curves get finer steps while flat areas, straight edges, text and the hand-placed
// pixels keep their look (no blending, no rounding into blobs). For larger sizes it is applied twice (4 times).
// The map's tiles, the windows, the buttons and icons, the sprites, the full-screen pictures, the font and the
// cursors all go through it.

// one MMPX doubling of an ARGB picture (w x h -> 2w x 2h); pixels past the edge repeat the edge
std::vector<uint32_t> Double(const std::vector<uint32_t>& argb, int w, int h);

// an ARGB picture enlarged `factor` times (1, 2 or 4: doubled that many times; 3 is doubled twice and resampled)
std::vector<uint32_t> Upscale(const std::vector<uint32_t>& argb, int w, int h, int factor);

// an ARGB picture resized to outW x outH by area averaging (premultiplied alpha)
std::vector<uint32_t> Resample(const std::vector<uint32_t>& argb, int w, int h, int outW, int outH);

// a truecolour picture resized to outW x outH for display: area averaging when it shrinks (or on one axis), bilinear
// interpolation (premultiplied alpha, edges clamped) when it grows on both. Hi-res art is not pixel art: it is
// shown smooth, never put through the MMPX scaler above
std::vector<uint32_t> ResampleSmooth(const std::vector<uint32_t>& argb, int w, int h, int outW, int outH);

// a w x h picture of palette indices as outW x outH ARGB pixels, upscaled (index seeThrough, if >= 0, is
// see-through): doubled once or twice, each pixel then repeated for larger sizes, and resampled to the exact size
// when that is not a whole multiple
std::vector<uint32_t> UpscalePicture(const uint8_t* px, int w, int h, const Palette& pal, int seeThrough, int outW, int outH);

// the same picture with plain pixel repetition (the classic look)
std::vector<uint32_t> PlainPicture(const uint8_t* px, int w, int h, const Palette& pal, int seeThrough, int outW, int outH);

// a 16 x 16 tile of indices as `size` x `size` ARGB pixels; with seeThrough, kSeeThrough pixels are see-through
inline std::vector<uint32_t> SmoothTile(const uint8_t* tile16, const Palette& pal, bool seeThrough, int size)
{
    return UpscalePicture(tile16, 16, 16, pal, seeThrough ? kSeeThrough : -1, size, size);
}
inline std::vector<uint32_t> PlainTile(const uint8_t* tile16, const Palette& pal, bool seeThrough, int size)
{
    return PlainPicture(tile16, 16, 16, pal, seeThrough ? kSeeThrough : -1, size, size);
}

// Tile `t` of a sheet as outSize x outSize ARGB pixels: an indexed tile through the palette and the pixel-art scaler
// (smooth) or by pixel repetition; a truecolour tile resampled (ResampleSmooth), whatever `smooth` says.
// seeThrough: an indexed sheet's kSeeThrough pixels are see-through (a truecolour tile keeps its own alpha when
// true; when false it is made opaque, as the plain ground is drawn without blending)
std::vector<uint32_t> SheetTileArgb(const TileSheet& sheet, int t, const Palette& pal, bool seeThrough, int outSize, bool smooth);

} // namespace osf
