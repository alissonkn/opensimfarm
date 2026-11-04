// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/Simulation.h"

#include <functional>
#include <string>
#include <vector>

namespace osf {

// The EDIT palette tools.
enum class Tool
{
    None, Move, Examine, PavedRoad, DirtRoad, Fence, Gate, Ditch, Valve, Hay, Trough, Trees, Bulldoze, Plant, Spray, Place,
};

struct ToolInfo
{
    const char* name;
    int cost;      // per cell; 0 = free / priced elsewhere
    int paletteTile; // icon for the palette
};
const ToolInfo& GetToolInfo(Tool t);

struct EditResult
{
    bool ok = false;
    std::string message;   // shown in the status strip (may be empty)
    std::string sound;     // effect to play, e.g. "uhoh.voc"
};

// Applies a tool to map cell (cx, cy). param = crop slot for Plant, spray job (6..9) for Spray.
EditResult ApplyTool(GameState& s, Simulation& sim, Tool tool, int cx, int cy, int param = 0);

// Tile helpers shared with the simulation and the map view.
int GroundTile(const GameState& s, int wetness, int cx, int cy);
void ReshapeAround(GameState& s, int cx, int cy);
// Options > AutoDoze: only rocks (535-538) and trees (131-146) are bulldozed, at 25 each
bool AutoDozable(int id);
// whether a new field can go with its top-left corner at (x, y); why = the reason when not
bool FieldPositionOk(GameState& s, Simulation& sim, int x, int y, std::string& why);
// The fence of a newly owned plot: along each side whose neighbouring plot is not yours, the edge cells
// that are ground, rock or tree become fence, then the shapes are redone.
void FencePlot(GameState& s, int col, int row);
// The homestead picture for a wealth tier 0-5, drawn from the farm's corner cell (x, y): yard tiles,
// from tier 3 the yard pieces, and the 2 x 2 house.
void DrawHomestead(GameState& s, int x, int y, int tier);
// Town plots, from the town layout file (40 x 24 tile ids, 5 x 3 blocks of 8 x 8): a newly placed plot
// shows the block's roads on bare ground (an airport its whole block) and gets the town flag on every cell; at every
// level change the plot is drawn again by its type and level. rand() is the G2 shift register.
void PlaceTownPlotCells(GameState& s, const std::vector<uint16_t>& layout, int col, int row, int type);
void RedrawTownPlot(GameState& s, const std::vector<uint16_t>& layout, int col, int row, const std::function<int()>& rand);

} // namespace osf
