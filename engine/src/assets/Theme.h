// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"

#include <array>
#include <string>

namespace osf {

// The interface's 16 named colours as plain RGB, in place of a palette. Everything the interface draws with a colour
// (fills, frames, bevels, lines, text, the plot grid, the overview map's legend) asks for one of these by number (the
// `Colour` enum of ui/Gfx.h); the original gives each a palette entry, a pack gives each an RGB value.
//
// A pack's tilepack.json names them in an optional "theme" block, as "#rrggbb" or "#rgb":
//
//   "theme": { "black": "#000000", "yellow": "#ffe14d", "sky": "#55aaff", "red": "#d03030", "pink": "#f080c0",
//              "navy": "#1c2a7a", "blue": "#3a5fd0", "tan": "#d8c08a", "green": "#3f9f3f", "darkGreen": "#1f6a2e",
//              "brown": "#80502a", "colour11": "#70d8d8", "lightGrey": "#c4c4c4", "grey": "#868686",
//              "darkGrey": "#484848", "white": "#ffffff" }
//
// A colour left out takes the engine's own (DefaultTheme). Unknown names are errors, so that a typo is not a silent
// default. The theme is constant: unlike the original's palette it does not change with the weather.
using Theme = Palette; // 16 x RGB

// the 16 names, by colour number (0 black .. 15 white; 11 has no name in the engine: "colour11")
extern const char* const kThemeColourNames[16];

// the engine's own colours: grey panels, black text, white and grey bevel edges
Theme DefaultTheme();

// "#rgb" or "#rrggbb" (any case) to RGB. False for anything else.
bool ParseColourText(const std::string& text, std::array<uint8_t, 3>& out);

// Reads the "theme" block of a manifest's text into `out` (starting from DefaultTheme). `present` tells whether the
// manifest had a block; with none, `out` is the default and the result is true. False with `error` set when the block
// is wrong.
bool ParseTheme(const std::string& json, Theme& out, bool& present, std::string& error);

// Reads `dir`/tilepack.json's theme (a folder without a manifest is an error).
bool LoadTheme(const std::string& dir, Theme& out, bool& present, std::string& error);

} // namespace osf
