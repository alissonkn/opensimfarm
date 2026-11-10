// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"

#include <cstdint>
#include <vector>

namespace osf {

// Modern Render: the player's 16 x 16 tiles redrawn smoothly in memory (the files are never changed).
// A tile is enlarged 4 times with the pixel-art doubling rule twice (an edge-following rule of the EPX family:
// a corner of an enlarged pixel takes a neighbour's colour where two neighbours and the diagonal of that colour
// meet there, which rounds the edges of solid shapes, keeps straight edges straight and leaves dithers alone),
// then averaged down to the size it is shown at, in linear light, so edges come out anti-aliased and every tile
// lands pixel for pixel on the screen.

// A texture (the ground's, the grass's, a field's soil: scattered pixels of two or three colours) is not a shape
// and is left exactly as it is: rounding would turn its pattern into another one. A pixel counts as part of a
// texture when, of the neighbour pairs in the 5 x 5 square around it that touch its own colour, at least
// kBusyShare per cent differ (a shape's pixel stays calm whatever lies beside it).
constexpr int kBusyShare = 25;
// shapes are also blended softly: each output pixel takes a tent this many output pixels wider on each side
// (0 = the plain average of its own area); textures always get the plain average
constexpr float kSoftness = 0.5f;
std::vector<uint8_t> CalmMask(const uint8_t* tile16); // 16 x 16: 1 where the tile may be smoothed

// one doubling of a square block of palette indices (size s x s -> 2s x 2s); pixels past the edge repeat the edge.
// With `calm` (s x s), only the pixels marked there are rounded; the others are just repeated
std::vector<uint8_t> Double(const std::vector<uint8_t>& px, int s, const std::vector<uint8_t>* calm = nullptr);

// a 16 x 16 tile of indices as `size` x `size` ARGB pixels; with seeThrough, kSeeThrough pixels are see-through (the
// transparent pixels get the colour of the nearby opaque ones, so a scaled edge shows no fringe)
std::vector<uint32_t> SmoothTile(const uint8_t* tile16, const Palette& pal, bool seeThrough, int size);

// the same tile at `size` with plain pixel repetition (the classic look), for comparison and tests
std::vector<uint32_t> PlainTile(const uint8_t* tile16, const Palette& pal, bool seeThrough, int size);

} // namespace osf
