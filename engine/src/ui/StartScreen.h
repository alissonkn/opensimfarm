// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/WorldGen.h"
#include "ui/Gfx.h"

#include <SDL.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace osf {

// The Select-a-Region screen: the original picture with its buttons; a click on
// the map shows that place's rainfall / temperature / wind classes (from the player's scenario.odt); the eight
// prebuilt farms are the yellow boxes on the map.
class StartScreen
{
public:
    StartScreen(Gfx& g, const std::string& gameDir);
    void SetGameDir(const std::string& d) { _gameDir = d; } // the SimFarm folder was moved (main.cpp)
    // returns "farm:N", "generated" (take the world with World()), either with "load+" in front for LOAD A SAVED GAME,
    // "quit" or an empty string
    std::string HandleEvent(const SDL_Event& e);
    void Draw(int pixelsW, int pixelsH);
    GameState& World() { return _world; }
    void Reset() { _design = false; }
    void SkipIntro() { _intro = 2; }

private:
    void LoadClimateMap();
    bool ToPicture(int px, int py, int& x, int& y) const;
    std::string DesignEvent(int x, int y);
    void DrawDesign();
    void Climate(int gx, int gy);
    bool Valid(int gx, int gy) const; // the region grid's valid cells
    void Generate(bool byButton = false);

    Gfx& _g;
    std::string _gameDir;
    std::map<std::string, std::vector<std::string>> _grids; // section name -> 18 rows
    std::array<int, 3> _classes{ -1, -1, -1 };               // rain, temperature, wind
    int _selected = 0;   // the chosen prebuilt farm box (when no region cell is picked)
    int _scale = 1, _ox = 0, _oy = 0;
    std::string _note;
    bool _design = false;
    // the intro at program start: the Maxis presents picture, then the title picture, each with its
    // own palette, up to 959 timer ticks or until a click or key; shown once per run
    int _intro = 0;            // 0 presents, 1 title, 2 done
    uint32_t _introShown = 0;  // SDL ticks when the current picture appeared
    bool _regionPicked = false;
    int _cellX = 0, _cellY = 0; // picked region cell
    WorldOptions _opt;
    GameState _template, _world;
    bool _haveWorld = false;
    std::vector<uint32_t> _preview;
};

} // namespace osf
