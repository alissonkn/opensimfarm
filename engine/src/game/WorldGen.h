// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <functional>
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

// The same in two steps, with room for the map editor between them (an engine addition): GenerateTerrain makes the
// climate and the land (ground, trees, rocks, river, lake) and chooses where the town and the farm go; FinishWorld
// puts them there and sets up a new game on that land.
struct WorldSites
{
    int townCol = 0, townRow = 0; // the town's home plot
    int farmCol = 0, farmRow = 0; // the farm's top-left plot (the farm is 3 x 3 plots)
};
bool GenerateTerrain(const Megadata& mega, const GameState& tmpl, const WorldOptions& opt, GameState& out, WorldSites& sites, std::string& error);
void FinishWorld(const std::string& gameDir, const Megadata& mega, GameState& s, const WorldSites& sites);

// water: the flags and the open-water tile; Reshore gives the water cells in x0..x1, y0..y1 the shoreline tile that
// fits their land neighbours (coin() picks one of each shore's two pictures)
constexpr int kWater = 39 + 0x10;
inline bool IsWater(const MapCell& c) { return (c.base & 0x2000) != 0; }
void SetWater(MapCell& c);
void Reshore(World& w, int x0, int y0, int x1, int y1, const std::function<int()>& coin);

} // namespace osf
