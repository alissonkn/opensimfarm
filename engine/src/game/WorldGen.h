// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <string>
#include <vector>

namespace osf {

struct WorldOptions
{
    int rain = 1, temperature = 1, wind = 1;  // climate classes 0..3
    bool river = false, lake = false; // both off until the design screen turns them on
    unsigned seed = 1;
    int mapSize = 96; // 96 (the original) or 144 (an engine addition: a larger world, more land)
    // the ground shade term of the scatter: 2 for Use This Region (the original's first maps), rainfall x 4 when the
    // design screen generates (it sets the term before its GENERATE)
    int wetness = 2;
    // when set, receives the map's tile ids (low 11 bits, x * size + y) as they stand before the town and the farm are
    // placed: the design screen's preview
    std::vector<uint16_t>* terrain = nullptr;
};

// A new game on a generated world. The template supplies
// the record layouts and the town graphics (any original farm); everything game-related is reset.
class Megadata;
bool GenerateWorld(const std::string& gameDir, const Megadata& mega, const GameState& tmpl, const WorldOptions& opt, GameState& out, std::string& error);

} // namespace osf
