// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/WorldGen.h"

#include <cstdint>
#include <string>

namespace osf {

// The map editor's changes to a new world's land (an engine addition), between GenerateTerrain and FinishWorld:
// ground, water, trees and rocks painted with a square brush, and the places of the farm and of the town.
enum class LandBrush { Ground, Water, Trees, Rocks };

// the plain ground picture for a cell of this height (terrain byte) under the world's ground shade term
int GroundTile(int terrain, int wetness);

// paints the square of side `size` (1, 3, 5 ...) centred on cell (cx, cy); the farmhouse corner and the town's home plot
// never take water (the rest of the farm can). Shorelines around the change are redrawn. Returns the cells changed.
int PaintLand(GameState& s, LandBrush brush, int cx, int cy, int size, int wetness, uint32_t& random, const WorldSites& sites);

bool InFarm(const WorldSites& sites, int x, int y); // cell (x, y) is on the farm (3 x 3 plots)
// the farm's corner where the farmhouse and its yard go (FinishWorld): always dry land
constexpr int kFarmhouseCells = 5;
bool InFarmhouse(const WorldSites& sites, int x, int y);
bool InTown(const WorldSites& sites, int x, int y); // cell (x, y) is on the town's home plot

// moves the farm (top-left plot col, row) or the town's home plot; water under the farmhouse corner or the town's plot
// becomes ground. False, with the reason, when it would overlap the other one (kept on the map)
bool PlaceFarm(GameState& s, WorldSites& sites, int col, int row, int wetness, std::string& why);
bool PlaceTown(GameState& s, WorldSites& sites, int col, int row, int wetness, std::string& why);

// headless checks of the above (--mapedittest); 0 when all is well
int MapEditSelfTest(const std::string& gameDir);

} // namespace osf
