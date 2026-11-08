// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/Shop.h"
#include "game/Simulation.h"
#include "ui/Gfx.h"

#include <memory>
#include <vector>
#include <string>

namespace osf {

class MapView;
class Megadata;

class SoundPlayer;

struct UiContext
{
    GameState* state = nullptr;
    Simulation* sim = nullptr;
    MapView* view = nullptr;
    const Megadata* mega = nullptr;
    std::string gameDir;
    // requests from a window to the interface, handled after the click
    int placeRecord = -1;          // BUY: carry this catalogue item to the map
    std::string command;           // for the application, e.g. "loadpath:<file>"
    std::string currentSave;       // the file the game was loaded from or last saved as (File > Save)
    std::string message, sound;
    SoundPlayer* audio = nullptr;  // volumes and Modern Sound (the SOUND window)
};

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// A floating window: title bar, then the background picture from the archive as its content.
// Positions are in the 640x480 interface coordinate system.
class Window
{
public:
    static constexpr int kTitleH = 16; // the original's title bar (window title at y, contents from y + 16)
    Window(int kind, std::string title, std::string background, int w, int h)
        : kind(kind), title(std::move(title)), background(std::move(background)), w(w), h(h) {}
    virtual ~Window() = default;

    // content-local coordinates
    virtual void DrawContent(Gfx& g, UiContext& ctx) = 0;
    virtual void Click(int /*x*/, int /*y*/, UiContext& /*ctx*/) {}
    virtual bool KeyDown(SDL_Keycode /*k*/, UiContext& /*ctx*/) { return false; }
    virtual bool Wheel(int /*dy*/) { return false; } // the mouse wheel over the window (dy > 0 = up)
    virtual Rect CloseButton() const { return {}; }

    Rect Outer() const { return { x, y, w, h + kTitleH }; }
    int ContentY() const { return y + kTitleH; }

    const int kind;         // toolbar button / menu id that opens it
    std::string title, background;
    int x = 0, y = 0, w, h;
    bool closed = false;
    bool closeBox = true;   // the round close box at the title's left (window flag bit 1; the Save/Load dialogs have none)
    uint32_t autoClose = 0; // SDL ticks when a window the game opened by itself closes again (0 = never)
};

enum WindowKind
{
    kWinBuy = 0, kWinSell, kWinExamine, kWinMap, kWinWeather, kWinEvaluation, kWinExpert, kWinEdit, kWinMarket,
    kWinBalance, kWinBank, kWinField, kWinSave, kWinLoad, kWinExamineCard, kWinCrop, kWinAirplane, kWinAbout, kWinHelp,
    kWinFunds, // the engine's money log (a click on the Funds box)
    kWinSound, // the engine's volume window (Options > Sound Volume)
    kWinLanguage, // the engine's language list (Options > Language)
};

std::unique_ptr<Window> MakeWindow(int kind);
std::unique_ptr<Window> MakeFieldWindow(int field);
std::unique_ptr<Window> MakeAirplaneWindow(int plane);
void DrawExamineCard(Gfx& g, const Megadata* mega, const ExamineCard& card, int x, int y); // the Examine card at (x, y)

} // namespace osf
