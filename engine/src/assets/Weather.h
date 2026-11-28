// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <string>

namespace osf {

// How a rainy day and a frosty day look on the map in a pack. The original recolours one palette entry (colour 7, tan:
// blue in rain, white in frost) and so the ground and everything else drawn in it; a pack has no palette, so the map
// is laid under a translucent colour instead: a bluish veil in rain, a pale one in frost. Only the map is tinted;
// the interface keeps its colours (assets/Theme.h).
struct WeatherTint
{
    uint8_t r = 0, g = 0, b = 0;
    uint8_t alpha = 0; // 0 none .. 255 the colour alone
    bool None() const { return alpha == 0; }
};

struct WeatherLook
{
    WeatherTint rain, frost;
};

// the engine's own: a slate-blue veil of about 30% in rain, a pale-blue-white one of about 45% in frost
WeatherLook DefaultWeatherLook();

// The tint of a day: kind 0 (a plain day) none, 1 rain, 2 frost (a rain day at 32 F or below).
inline WeatherTint TintOfDay(const WeatherLook& look, int kind) { return kind == 1 ? look.rain : kind == 2 ? look.frost : WeatherTint{}; }

// A pack's tilepack.json names them in an optional "weather" block:
//
//   "weather": { "rain":  { "colour": "#30508c", "strength": 0.30 },
//                "frost": { "colour": "#e8f4ff", "strength": 0.45 } }
//
// "colour" is "#rrggbb" or "#rgb"; "strength" is the share of the veil, 0 (none) to 1 (the colour alone). Either may be
// left out of a day (it keeps the engine's own) and either day may be left out; "strength": 0 means no tint. Unknown
// names are errors.

// Reads the "weather" block of a manifest's text into `out` (starting from DefaultWeatherLook). `present` tells whether
// the manifest had a block. False with `error` set when the block is wrong.
bool ParseWeatherLook(const std::string& json, WeatherLook& out, bool& present, std::string& error);

// Reads `dir`/tilepack.json's weather (a folder without a manifest is an error).
bool LoadWeatherLook(const std::string& dir, WeatherLook& out, bool& present, std::string& error);

} // namespace osf
