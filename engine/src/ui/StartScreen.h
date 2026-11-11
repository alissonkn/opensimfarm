// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/WorldGen.h"
#include "ui/MapEditor.h"
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
    // "language:<code>" after a language was chosen (already set), "quit" or an empty string
    std::string HandleEvent(const SDL_Event& e);
    void Draw(int pixelsW, int pixelsH);
    GameState& World() { return _world; }
    void Reset() { _design = false; _editing = false; }
    void SkipIntro() { _intro = 2; }
    // the phone layout (an engine addition, user 2026-11-11: "redesign the start screens for phones"): the region and
    // design screens rebuilt from their parts in big buttons for a narrow screen, portrait or landscape; Draw then gets
    // window pixels at one interface unit each
    void SetPhone(bool p) { _phone = p; }

private:
    void LoadClimateMap();
    bool ToPicture(int px, int py, int& x, int& y) const;
    std::string DesignEvent(int x, int y);
    void DrawDesign();
    void ToggleMapSize();
    void DrawMapSizeButton();
    void DrawLanguage();               // the language button under the panel, and its list when open
    bool LanguageClick(int x, int y);  // true when the click was the button's or the list's
    void DrawTranslatedRegion();       // the region picture's words in the chosen language
    void DrawTranslatedDesign();       // the same for the design picture
    void Climate(int gx, int gy);
    bool Valid(int gx, int gy) const; // the region grid's valid cells
    void Generate(bool byButton = false);
    void MakePreview();      // the design screen's picture of the land (_land)
    void FinishEdited();     // the world from the edited land: the town and the farm put in their places
    void DrawEditButton();
    // the phone layout (StartPhone section of StartScreen.cpp)
    void PhoneLayout(int w, int h);
    void DrawPhone();
    void DrawPhoneButton(const SDL_Rect& r, const std::string& label, bool pressed, bool enabled = true);
    void DrawPhoneClimate(int x, int y, int w, int k, int value, int colour, bool arrows);
    std::string PhoneEvent(int px, int py); // window pixels
    std::string PictureClick(int x, int y); // a click at a place of the 640 x 480 picture (the region screen)
    std::string NextLanguage();

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
    bool _byButton = false;
    int _designMapSize = 96; // the design screen's MAP button (96 or 144)
    GameState _template, _world;
    // the map editor (an engine addition): the land before the town and the farm are placed, where they go, and the
    // ground shade term the generator used
    GameState _land;
    WorldSites _sites;
    int _wetness = 2;
    bool _editing = false;
    MapEditor _editor{ _g };
    bool _haveWorld = false;
    std::vector<uint32_t> _preview;
    int _previewSide = 192;
    bool _languageOpen = false;  // the language list is open
    std::string _changedLanguage; // set by a choice in the list, handed to main.cpp as "language:<code>"
    bool _phone = false;
    int _u = 1;                 // window pixels per interface unit in the phone layout
    int _uw = 0, _uh = 0;       // the screen in units
    bool _wide = false;         // landscape
    SDL_Rect _mapPx{};          // the region map (or the design preview) in window pixels
    std::vector<std::pair<int, SDL_Rect>> _hits; // the phone buttons (PhoneButton ids) in units
    float _editF = 1.0f;        // the map editor's magnification on a phone, and its offset in window pixels
    int _editX = 0, _editY = 0;
};

} // namespace osf
