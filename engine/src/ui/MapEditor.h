// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/MapEdit.h"
#include "game/WorldGen.h"
#include "ui/Gfx.h"

#include <SDL.h>

#include <string>
#include <vector>

namespace osf {

// The map editor (an engine addition), opened from the design screen's EDIT MAP button: the new world's land drawn
// with the game's tiles, where the player paints ground, water, trees and rocks and chooses where the farm and the
// town start. Its own 640 x 480 screen: the map on the left, the tools on the right.
class MapEditor
{
public:
    explicit MapEditor(Gfx& g) : _g(g) {}
    void Open(const GameState& land, const WorldSites& sites, int wetness);
    // x, y: the mouse in picture coordinates. Returns "done" (take Land() and Sites()), "cancel" or an empty string
    std::string HandleEvent(const SDL_Event& e, int x, int y);
    void Draw(); // in picture coordinates (the caller sets the scale and the viewport)
    const GameState& Land() const { return _land; }
    const WorldSites& Sites() const { return _sites; }

private:
    enum Tool { kGround, kWater, kTrees, kRocks, kFarm, kTown, kTools };
    bool CellAt(int x, int y, int& cx, int& cy) const;
    void Apply(int cx, int cy);           // the tool at a cell (a brush stroke's step, or a placement)
    void Stroke(int cx, int cy);          // the brush from the last cell to this one, without gaps
    void PushUndo();
    void Undo();
    void SetZoom(int level, int ax, int ay); // keeps the cell under (ax, ay) in place
    void Pan(int dx, int dy);
    void ClampCamera();
    void FarmAt(int cx, int cy, int& col, int& row) const; // the farm's top-left plot for a click at cell (cx, cy)
    void DrawButton(int x, int y, int w, int h, const std::string& label, bool selected, int tile = -1);

    Gfx& _g;
    GameState _land;
    WorldSites _sites;
    int _wetness = 2;
    uint32_t _random = 1;
    Tool _tool = kWater;
    int _brush = 3;           // brush side in cells
    int _zoom = 0;            // 0 = the whole map, 1 = 8 pixels a cell, 2 = 16
    int _camX = 0, _camY = 0; // the top-left cell shown
    bool _painting = false, _panning = false;
    int _lastCx = -1, _lastCy = -1;
    int _panX = 0, _panY = 0;
    int _hoverX = -1, _hoverY = -1; // the cell under the mouse, -1 off the map
    std::string _note;             // a refusal, shown until the next action
    struct Step
    {
        std::vector<MapCell> cells;
        WorldSites sites;
    };
    std::vector<Step> _undo;
    std::vector<uint32_t> _pixels; // the whole-map picture
};

} // namespace osf
