// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Picture.h"

#include <cstdint>
#include <vector>

namespace osf {

// The fonts on offer in Options > Advanced: the game's own 8 x 8 font, or one of the modern fonts built into the program
// (src/third_party/fonts, SIL Open Font License), drawn at the resolution in use. A modern letter keeps the
// original's place: an 8 unit wide cell whose capitals are 7 units tall on the same baseline. Its cell is taller than
// the original's, by kModernFontAbove units over it (accents on capitals) and kModernFontBelow under it (descenders).
constexpr int kModernFontAbove = 4;
constexpr int kModernFontBelow = 2;
constexpr int kModernFontCellH = 8 + kModernFontAbove + kModernFontBelow;

// the font used when the player has not chosen one
constexpr int kDefaultFont = 1;

// how many fonts are on offer (font 0 is the game's own), and each one's name
int FontCount();
const char* FontName(int font);

// modern font `font` (1 .. FontCount() - 1) as an ARGB atlas of 16 x 16 cells (codes 0-255, Latin-1), each
// 8 x kModernFontCellH units at `detail` pixels per unit: white, the alpha the letter's cover. Codes the font lacks,
// and the game's own symbols (the tick on '^'), are the game's glyph (font8) with each pixel repeated. Empty if the
// font is not a modern one or cannot be read
std::vector<uint32_t> ModernFontAtlas(const Font8& font8, int detail, int font);

} // namespace osf
