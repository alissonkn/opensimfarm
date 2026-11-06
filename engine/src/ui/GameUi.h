// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/Editing.h"
#include "ui/Gfx.h"
#include "ui/Windows.h"

#include <SDL.h>

#include <array>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <deque>
#include <vector>

namespace osf {

// The main-screen interface: menu bar, toolbar band with date and
// funds, a status strip, and the floating windows. Works in 640x480-style units times the UI scale; the
// screen may be wider or taller than 640x480, the right part of the toolbar stays on the right edge.
class GameUi
{
public:
    // kTop: the desktop below the toolbar (windows stay below it). The farm is shown in the EDIT window, framed as
    // in the original (title bar at y 48, status row and palette from y 64, scroll bars along the right and bottom)
    // but filling the screen's width and height: the original's black margins (x 0..15, 624..639, y 464..479) are
    // left out, so at 640x480 the map is 48..623 x 78..463.
    static constexpr int kMenuH = 16, kToolbarY = 16, kToolbarH = 32, kTop = 48, kStatusY = 48, kStatusH = 14, kPalTop = 48;
    static constexpr int kMapTop = 62; // no EDIT title bar (an engine change): the status row sits right under the toolbar
    // the map starts right of the tool column: 48 wide as in the original, wider with the tool names shown
    int MapLeft() const { return kPaletteX + PaletteW(); }

    explicit GameUi(Gfx& g) : _g(g) {}
    ~GameUi();
    void SetGameDir(const std::string& d) { _ctx.gameDir = d; }
    void SetAudio(SoundPlayer* a) { _ctx.audio = a; }
    void SetContext(GameState* state, Simulation* sim, MapView* view) { _ctx.state = state; _ctx.sim = sim; _ctx.view = view; _ctx.mega = _g.Mega(); }
    // a new game starts with the Examine tool (checked live: chosen again after New Game; Load Game keeps the tool)
    void ResetTool() { _tool = Tool::Examine; _moveKind = 0; }
    // the plain arrow pointer, for the region screen (the original keeps the game's tool pointer there, e.g. the
    // magnifier after File > New Game; user finding)
    static float ThumbFraction(float pos, float shown); // a scroll thumb's place along its track, 0..1
    void ArrowCursor()
    {
        if (_shownCursor == -1)
            return;
        SDL_SetCursor(_arrow ? _arrow : SDL_GetDefaultCursor());
        _shownCursor = -1;
    }
    // the file File > Save writes: a loaded saved game (.SFM); none for a prebuilt farm or a new world
    void SetCurrentSave(const std::string& path)
    {
        std::string ext = path.size() > 4 ? path.substr(path.size() - 4) : "";
        for (auto& c : ext)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        _ctx.currentSave = ext == ".SFM" ? path : "";
    }
    void SetScreenSize(int pixelsW, int pixelsH);
    std::function<void(const std::string&)> onCommand;    // "save", "load", "farm:N", "quit", "scale:N"
    std::function<void(const std::string&)> onSound;      // effect file name, e.g. "uhoh.voc"
    std::function<bool()> soundEnabled, musicEnabled;

    bool HandleEvent(const SDL_Event& e);                  // true when the interface used the event
    void Draw();
    void ShowMessage(const std::string& m);
    void OpenWindow(int kind);
    bool loadFromStart = false; // the open LOAD GAME window came from the region screen: closing it goes back there
    bool IsOpen(int kind) const;
    // the map scroll bars are kScrollBar thick (an engine change: the original's are 16; user findings F18, Q13)
    static constexpr int kScrollBar = 11, kScrollArrow = 16;
    Rect MapRect() const { return { MapLeft(), kMapTop, _w - MapLeft() - kScrollBar, _h - kMapTop - kScrollBar }; } // interface units
    SDL_Rect MapPixels() const { const Rect r = MapRect(); const int s = _g.Scale(); return { r.x * s, r.y * s, r.w * s, r.h * s }; }
    bool OverMap(int x, int y) const { return MapRect().Contains(x, y); }
    void ShowEvent(const GameEvent& e);                    // modal box; the clock stops while it is open
    bool TextFocus() const { return !_windows.empty() && (_windows.back()->kind == kWinSave || _windows.back()->kind == kWinLoad || _windows.back()->kind == kWinBank); }
    // the arrow keys belong to an open menu, list or box instead of scrolling the map (user finding F36)
    bool ArrowKeysTaken() const
    {
        if (_openMenu >= 0 || _historyOpen || !_events.empty())
            return true;
        if (_windows.empty())
            return false;
        const int k = _windows.back()->kind;
        return k == kWinBuy || k == kWinSell || k == kWinSave || k == kWinLoad || k == kWinCrop || k == kWinFunds || k == kWinBank || k == kWinMarket;
    }
    // the left edge of the forecast box in the toolbar: the picture's place (304) in a 640 window, else as far right as
    // leaves room for the weather summary before the date / funds part
    int ForecastX() const { return 256; } // right after the last button at any width (user, Q16d)
    bool MessagesOn() const { return _messagesOn; }
    void SetMessagesOn(bool on) { _messagesOn = on; }
    bool ToolNamesOn() const { return _toolNames; }
    void SetToolNamesOn(bool on) { _toolNames = on; }
    bool Modal() const { return !_events.empty(); } // an event box stops the clock
    // File > Quit (and the engine's Esc): "save before leaving?" YES / CANCEL / NO; NO or no answer leaves the program,
    // YES opens Save and leaves after a save. File > New Game: events.dat 45, CANCEL / YES (no answer = CANCEL).
    void AskQuit();
    void AskNewGame();
    void Saved(const std::string& name); // every save: the thumbs-up box

private:
    bool _resumeNormal = false; // a Save / Load dialog paused the game; closing it resumes at Normal speed
    struct MenuItem { std::string label; std::string command; };
    struct Menu { std::string title; int x; std::vector<MenuItem> items; int boxX, boxW, boxH; };

    void BuildMenus();
    bool ItemChecked(const std::string& cmd) const;
    bool ItemEnabled(const std::string& cmd) const;
    void Execute(const std::string& cmd);
    Rect MenuBox(int m) const;
    int MenuAt(int x, int y) const;
    int ItemAt(int x, int y) const;
    Window* WindowAt(int x, int y);
    void Raise(Window* w);
    void Click() { if (onSound) onSound("click.voc"); }
    void DrawToolbar();
    void DrawMenus();
    void DrawWindow(Window& w);
    // EDIT palette
    // One column of 24x24 icons in groups of two split by a line (the tools cut from the original's two-column
    // picture; PLANT and SPRAY drawn by the engine), the names beside them as an option. Cells 0..11 are the
    // tools, 12 PLANT, 13 SPRAY, 15 the >> / << button right below them that shows or hides the names.
    static constexpr int kPaletteX = 0;
    int PaletteW() const { return _toolNames ? 102 : 24; } // names in the small bold capitals (user findings Q15, Q22)
    Rect PaletteCell(int i) const;
    // the PLANT / SPRAY lists as in the original: a light grey box with a black frame right of the column, a title
    // row, then 17-pixel rows in columns of 112 (PLANT: 2 x 8, SPRAY: 1 x 4), the box top 96 / 40 above the button
    Rect PopupBox() const;
    // the PLANT list (an engine change, user findings Q11/F22): every crop, the 16 loaded ones and the crop files not
    // loaded in any slot (choosing one of those loads it into a free slot), in alphabetical order, down two columns
    int PopupRows() const { return _popup == 1 ? std::max(8, (static_cast<int>(_plantList.size()) + 1) / 2) : 4; }
    Rect PopupRow(int i) const
    {
        const Rect b = PopupBox();
        const int rows = PopupRows();
        return { b.x + 112 * (i / rows), b.y + 17 + 17 * (i % rows), 112, 17 };
    }
    struct SpareCrop { std::string file, name; std::vector<uint8_t> icon; };
    std::vector<SpareCrop> _spareCrops;
    struct PlantEntry { std::string name; int slot; int spare; }; // slot >= 0: a loaded crop, else _spareCrops[spare]
    std::vector<PlantEntry> _plantList;
    void ListSpareCrops();
    void BuildPlantList();
    void PlantSpareCrop(int k);
    // toolbar button -> window: BUY, SELL, (Examine), MAP, WEATHER, BALANCE SHEET, BANK, (EDIT), MARKET VALUE
    // the nine toolbar buttons: BUY, SELL, Evaluation, Map, Weather, Balance Sheet, Bank, EDIT, Market Value (live and
    // the original's toolbar routine, which presses the third button while the Evaluation window's slot is set)
    static int ToolbarWindow(int b) { return b == 2 ? kWinEvaluation : b == 5 ? kWinBalance : b == 6 ? kWinBank : b; }
    // the engine's toolbar has no EDIT button: slots 0-6 are the original's first seven buttons, slot 7 Market Value
    static int ToolbarButton(int slot) { return slot < 7 ? slot : slot == 7 ? kWinMarket : -1; }
    void DrawEditFrame();
    void DrawTitleBar(int x, int y, int w, const std::string& title, bool active, bool closeBox = true);
    void DrawStatus();
    bool FrameClick(int x, int y);       // the EDIT window's scroll arrows and bars
    int PopupCount() const { return _popup == 1 ? static_cast<int>(_plantList.size()) : _popup == 2 ? 4 : 0; }
    void DrawPalette();
    void IconBevel(int x, int y, bool pressed, int w = 24);
    void DrawCursor();
    void DrawHazards();
    void DrawPlane();
    void DrawFlightPanel();
    Rect FlightPanel() const;
    Rect FlightButton(int k) const;
    void DrawPuffs();
    void UpdateCursor();
    void AutoScroll();
    void ArrowRepeat();
    // zoom in (0) / out (1): an engine addition, in the map's bottom-right corner above the scroll bars
    Rect ZoomButton(int k) const
    {
        const Rect m = MapRect();
        // + and - and, below them, the edge-scrolling check box (k = 2, user finding Q9)
        return { m.x + m.w - 20, m.y + m.h - 53 + 17 * k, 16, 15 };
    }
    void DrawZoomButtons();
    int _arrowDX = 0, _arrowDY = 0;   // a scroll arrow held down
    Uint32 _arrowNext = 0;
    void DrawEventBox();
    void EventTimeout();
    void YearEvaluation();
    Rect EventBox() const { return { (_w - 256) / 2, (_h - 192) / 2 + 6, 256, 192 }; } // (192, 150) on 640x480
    // OK centred; YES / NO at a quarter and three quarters of the width (button faces 48x24)
    Rect VoteBox() const { return { (_w - 640) / 2 + 96, (_h - 480) / 2 + 96, 368, 224 }; } // Town Choice
    Rect EventButton(bool question, int k) const
    {
        const Rect b = EventBox();
        return { b.x + (question ? (k ? b.w - b.w / 4 : b.w / 4) : b.w / 2) - 24, b.y + kEventButtonY, 48, 24 };
    }
    static constexpr int kEventButtonY = 156;
    Rect FaceButton(int n, int k) const
    {
        // n buttons of an interface box: one centred, two at a quarter and three quarters, three at both and the middle
        const Rect b = EventBox();
        const int cx = n == 1 ? b.w / 2 : n == 2 ? (k ? b.w - b.w / 4 : b.w / 4) : b.w / 4 + k * b.w / 4;
        return { b.x + cx - 24, b.y + kEventButtonY, 48, 24 };
    }
    void UiAnswer(const GameEvent& e, int button);
    bool UiCommand(const std::string& cmd); // commands the interface handles itself ("saved:NAME")
    bool _quitAfterSave = false;
    bool PaletteClick(int x, int y);
    void UseTool(int px, int py, bool first);
    Tool _tool = Tool::Examine;
    Uint32 _autoNext = 0;  // the next AutoScroll step
    // the pointer left the window through an edge (-1 none; 0 left, 1 right, 2 top, 3 bottom bits): the original's
    // pointer is held at the screen edge, so the engine scrolls on as if it stayed there until it comes back
    int _parkedX = 0, _parkedY = 0;
    int _lastMx = -1, _lastMy = -1;
    int _popupWas = 0; // the list open when the last palette click came in
    int _toolParam = 0;
    int _popup = 0;
    bool _rightDown = false;             // a right press on the farm, handled at its release (click or scroll drag)
    int _rightX = 0, _rightY = 0;
    int _moveKind = 0, _moveIndex = 0, _moveFromX = 0, _moveFromY = 0;   // Move tool: the picked object               // 1 = crop list (PLANT), 2 = spray list
    bool _painting = false;
    // Examine: the card shown while the button is held
    int ExamineQuadrant(int x, int y) const;
    void OpenFieldSchedule(int field);
    bool MapPress(int cx, int cy); // the original's press-time cell handler: true when it took the click
    bool _exHold = false, _exShown = false;
    int _exQuad = -1, _exField = 0;
    ExamineCard _exCard;
    int _lastCellX = -1, _lastCellY = -1;
    bool _paletteVisible = true;
    bool _messagesOn = true;
    bool _toolNames = false;
    std::vector<GameEvent> _events;
    std::string _shownDate; // the toolbar's date, held while event boxes are up
    int64_t _shownCash = 0; // the toolbar's funds, held behind the lost auction's boxes
    int _votePressed = -1;
    bool _yearEvalDue = false;  // the year end asked for the Evaluation window      // the Town Choice button held down (-1 none)
    int _danceSet = 0;        // the dancing-cow box: its cowman%d.spr set (0 = none picked yet)
    uint32_t _eventShown = 0; // when the front box appeared (SDL ticks); boxes close by themselves after 1280 timer ticks
    struct PuffAnim { int x, y; Uint32 start; };
    std::vector<PuffAnim> _puffs;
    std::array<SDL_Cursor*, 6> _cursors{};   // from cursors.til: grab, open hand, magnifier, scissors, dollar, spray
    SDL_Cursor* _arrow = nullptr;
    int _cursorScale = 0, _shownCursor = -2;

    Gfx& _g;
    UiContext _ctx;
    int _w = 640, _h = 480;                     // screen in interface units
    std::vector<Menu> _menus;
    int _openMenu = -1, _hoverItem = -1;
    int _flightPressed = -1;
    Uint32 _flightPressedAt = 0;
    Uint32 _menuOpenedAt = 0;
    std::vector<std::unique_ptr<Window>> _windows; // back to front
    Window* _drag = nullptr;
    int _dragDX = 0, _dragDY = 0;
    std::string _message;
    Uint32 _messageTime = 0;
    // the engine's message history: the status row's down button opens a list of earlier messages, newest first
    std::deque<std::pair<std::string, std::string>> _history; // date, text
    bool _historyOpen = false;
    int _historyTop = 0;
    static constexpr int kHistoryRows = 16;
    Rect HistoryButton() const { return { MapLeft(), kStatusY, 16, kStatusH }; }
    Rect HistoryBox() const { const Rect m = MapRect(); return { m.x + 16, kStatusY + kStatusH, m.w - 16, 10 * kHistoryRows + 6 }; }
    void DrawHistory();
    int _cascade = 0;
};

} // namespace osf
