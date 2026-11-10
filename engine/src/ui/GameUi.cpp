// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/GameUi.h"
#include "game/Machines.h"

#include "assets/Graphics.h"
#include "assets/Megadata.h"
#include "audio/Music.h"
#include "audio/Sound.h"
#include "core/Lang.h"
#include "core/Paths.h"
#include "game/Crops.h"

#include "game/Shop.h"
#include "ui/MapView.h"

#include <algorithm>
#include <set>
#include <filesystem>
#include <map>
#include <cstdlib>

namespace osf {

static const char* const kMonthShort[12] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"), N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec") };
static constexpr int kItemH = 12;
static const char* const kSprayNames[4] = { N_("Fertilize"), N_("Pesticide"), N_("Herbicide"), N_("Fungicide") };
static constexpr int kToolbarRightPart = 240; // the date / funds part of the toolbar picture (x 400..639)

void GameUi::BuildMenus()
{
    _menus = {
        // the original's menus, item for item, and a Help menu (the engine's extras are on keys: F1 help, F5 / F9 quick save / load,
        // Ctrl+1/2/3 interface size; the prebuilt farms are on the region screen of New Game), each with the drop-down
        // box (x, width, height) measured on the original
        { N_("File"), 8, { { N_("About SimFarm"), "win:17" }, { N_("New Game"), "newgame" }, { N_("Save"), "savecurrent" }, { N_("Save As"), "win:12" },
                       { N_("Load Game"), "win:13" }, { N_("Quit"), "quit" } }, 0, 128, 75 }, // no Load Crop: the PLANT list loads crops
        { N_("Options"), 64, { { N_("Music"), "music" }, { N_("Sound Effects"), "sfx" }, { N_("AutoBuy"), "opt:opt_autobuy" }, { N_("AutoScroll"), "opt:opt_autoscroll" },
                           { N_("AutoDoze"), "opt:opt_autodoze" }, { N_("AutoGoto"), "opt:opt_autogoto" }, { N_("Messages"), "msgs" }, { N_("AutoLease"), "opt:menu_6" },
                           { N_("Smart Driving"), "smartdrive" }, { N_("Modern Sound"), "modernsound" }, { N_("Modern Render"), "modernrender" }, { N_("Sound Volume"), "win:20" }, { N_("Language"), "win:21" } }, 64, 136, 160 },
        { N_("Speed"), 144, { { N_("Ultra"), "speed:0" }, { N_("Fast"), "speed:1" }, { N_("Normal"), "speed:2" }, { N_("Slow"), "speed:3" }, { N_("Pause"), "speed:4" } }, 144, 64, 64 },
        { N_("Windows"), 208, { { N_("Buy"), "win:0" }, { N_("Sell"), "win:1" }, { N_("Evaluation"), "win:5" }, { N_("Map"), "win:3" }, { N_("Weather"), "win:4" },
                            { N_("Balance Sheet"), "win:9" }, { N_("Bank"), "win:10" }, { N_("Market Value"), "win:8" },
                            { N_("Farm Expert"), "win:6" } }, 208, 120, 116 }, // no Edit item: the farm view always shows its tools
        { N_("Disasters"), 288, { { N_("Tornado"), "dis:0" }, { N_("Locusts"), "dis:1" }, { N_("Drought"), "dis:2" }, { N_("Flood"), "dis:3" }, { N_("Frost"), "dis:4" },
                              { N_("Windstorm"), "dis:5" }, { N_("Disable"), "dis:6" } }, 288, 104, 88 },
        // engine addition: the legend of the tools (also F1)
        { N_("Help"), 392, { { N_("Tool Help"), "win:18" } }, 392, 104, 15 },
    };
    if (CurrentLanguage() == "en")
        return;
    // a translation: the words, and the titles placed one after the other with a gap (smaller when they would
    // reach the radio), each box as wide as its longest item
    for (auto& m : _menus)
    {
        m.title = Tr(m.title);
        for (auto& it : m.items)
            it.label = Tr(it.label);
    }
    const int radioLeft = 640 - kRadioWidth - 4;
    int gap = 24;
    for (; gap > 8; gap -= 4)
    {
        int x = 8;
        for (const auto& m : _menus)
            x += Gfx::TextWidth(m.title) + gap;
        if (x - gap + 8 <= radioLeft)
            break;
    }
    int x = 8;
    for (size_t i = 0; i < _menus.size(); ++i)
    {
        Menu& m = _menus[i];
        m.x = x;
        int widest = 0;
        for (const auto& it : m.items)
            widest = std::max(widest, Gfx::TextWidth(it.label));
        m.boxW = std::max(m.boxW, widest + 16);
        m.boxX = std::min(i == 0 ? 0 : x, 640 - m.boxW);
        x += Gfx::TextWidth(m.title) + gap;
    }
}

void GameUi::SetScreenSize(int pw, int ph)
{
    if (_menus.empty())
        BuildMenus();
    _w = std::max(640, pw / _g.Scale());
    _h = std::max(200, ph / _g.Scale());
    for (auto& w : _windows)
    {
        w->x = std::clamp(w->x, 0, std::max(0, _w - w->w));
        w->y = std::clamp(w->y, kTop, std::max(kTop, _h - 20));
    }
}

bool GameUi::IsOpen(int kind) const
{
    for (const auto& w : _windows)
        if (w->kind == kind)
            return true;
    return false;
}

void GameUi::OpenWindow(int kind)
{
    for (auto& w : _windows)
        if (w->kind == kind)
        {
            Raise(w.get());
            return;
        }
    auto w = MakeWindow(kind);
    if (!w)
    {
        ShowMessage(Tr("This window is not available yet."));
        return;
    }
    // the original opens each window at its own place (measured at 640x480: outer top-left of the title bar);
    // the others cascade
    static const std::map<int, std::pair<int, int>> kPlace = { { kWinBuy, { 128, 96 } }, { kWinSell, { 128, 80 } }, { kWinMap, { 80, 64 } },
                                                               { kWinWeather, { 160, 128 } }, { kWinBalance, { 128, 96 } },
                                                               { kWinBank, { 176, 96 } }, { kWinMarket, { 144, 112 } }, { kWinEvaluation, { 128, 128 } }, { kWinExpert, { 128, 96 } },
                                                               { kWinCrop, { 160, 96 } }, { kWinSave, { 160, 96 } }, { kWinLoad, { 160, 96 } }, { kWinAbout, { 96, 96 } }, { kWinHelp, { 40, 40 } },
                                                               { kWinSound, { 192, 112 } }, { kWinLanguage, { 224, 112 } } };
    if (const auto it = kPlace.find(kind); it != kPlace.end())
    {
        w->x = std::clamp(it->second.first, 0, std::max(0, _w - w->w));
        w->y = std::clamp(it->second.second, kTop, std::max(kTop, _h - w->h - Window::kTitleH));
    }
    else
    {
        w->x = std::clamp(80 + 24 * _cascade, 0, std::max(0, _w - w->w));
        w->y = std::clamp(kTop + 16 + 20 * _cascade, kTop, std::max(kTop, _h - w->h - Window::kTitleH));
        _cascade = (_cascade + 1) % 6;
    }
    if (kind == kWinSave || kind == kWinLoad)
    {
        // the Save Game and Load Game dialogs pause the game; closing them resumes it at Normal speed whatever the
        // speed was before (the original's pause counter), unless the player had paused it already
        _resumeNormal = _ctx.sim->GetSpeed() != Speed::Paused;
        _ctx.sim->SetSpeed(Speed::Paused);
    }
    _windows.push_back(std::move(w));
}

void GameUi::Raise(Window* w)
{
    auto it = std::find_if(_windows.begin(), _windows.end(), [&](const auto& p) { return p.get() == w; });
    if (it != _windows.end() && it + 1 != _windows.end())
        std::rotate(it, it + 1, _windows.end());
}

Window* GameUi::WindowAt(int x, int y)
{
    for (auto it = _windows.rbegin(); it != _windows.rend(); ++it)
        if ((*it)->Outer().Contains(x, y))
            return it->get();
    return nullptr;
}

bool GameUi::ItemChecked(const std::string& cmd) const
{
    if (!_ctx.sim || !_ctx.state)
        return false;
    if (cmd.rfind("speed:", 0) == 0)
        return static_cast<int>(_ctx.sim->GetSpeed()) == std::stoi(cmd.substr(6));
    if (cmd.rfind("opt:", 0) == 0)
    {
        auto it = _ctx.state->scalars.find(cmd.substr(4));
        return it != _ctx.state->scalars.end() && it->second != 0;
    }
    if (cmd.rfind("scale:", 0) == 0)
        return _g.Scale() == std::stoi(cmd.substr(6));
    if (cmd.rfind("win:", 0) == 0)
        return IsOpen(std::stoi(cmd.substr(4))) || (std::stoi(cmd.substr(4)) == kWinEdit && _paletteVisible);
    if (cmd == "sfx")
        return soundEnabled && soundEnabled();
    if (cmd == "msgs")
        return _messagesOn;
    if (cmd == "smartdrive")
        return Machines::smartDriving;
    if (cmd == "music")
        return musicEnabled && musicEnabled();
    if (cmd == "modernsound")
        return _ctx.audio && _ctx.audio->Modern();
    if (cmd == "modernrender")
        return _ctx.view && _ctx.view->Modern();
    if (cmd.rfind("dis:", 0) == 0 && _ctx.sim)
    {
        static const int ids[6] = { 1, 4, 2, 3, 5, 6 };
        const int k = std::stoi(cmd.substr(4));
        return k == 6 ? _ctx.sim->Hazards().Disabled() : _ctx.sim->Hazards().Active(ids[k]);
    }
    return false;
}

bool GameUi::ItemEnabled(const std::string& cmd) const
{
    if (_ctx.sim && _ctx.sim->Plane().Flying())
        return cmd.rfind("speed:", 0) == 0; // in the air only the game speed can be changed (F42)
    if (cmd.rfind("win:", 0) == 0)
    {
        const int k = std::stoi(cmd.substr(4));
        return k == kWinWeather || k == kWinMarket || k == kWinBalance || k == kWinBank || k == kWinMap || k == kWinEvaluation || k == kWinBuy || k == kWinSell || k == kWinSave || k == kWinLoad || k == kWinExpert || k == kWinCrop || k == kWinAbout || k == kWinHelp || k == kWinEdit || k == kWinSound || k == kWinLanguage;
    }
    return true;
}

void GameUi::Execute(const std::string& cmd)
{
    if (!ItemEnabled(cmd))
        return;
    Click();
    if (cmd.rfind("speed:", 0) == 0 && _ctx.sim)
        _ctx.sim->SetSpeed(static_cast<Speed>(std::stoi(cmd.substr(6))));
    else if (cmd.rfind("opt:", 0) == 0 && _ctx.state)
    {
        auto& v = _ctx.state->scalars[cmd.substr(4)];
        v = v ? 0 : 1;
    }
    else if (cmd == "win:" + std::to_string(kWinEdit))
        ; // no EDIT window: the farm view always shows its tools (an engine change)
    else if (cmd.rfind("win:", 0) == 0)
    {
        const int k = std::stoi(cmd.substr(4));
        if (IsOpen(k))
            for (auto& w : _windows)
                w->closed |= w->kind == k;
        else
            OpenWindow(k);
    }
    else if (cmd == "msgs")
        _messagesOn = !_messagesOn;
    else if (cmd == "smartdrive")
    {
        // the engine's route planner for machines (on) or the original's step-by-step steering (off)
        Machines::smartDriving = !Machines::smartDriving;
        ShowMessage(Machines::smartDriving ? Tr("Smart driving on: machines plan their way to the field and back.")
                                           : Tr("Smart driving off: machines steer as in the original."));
    }
    else if (cmd == "quit")
        AskQuit();
    else if (cmd == "newgame")
        AskNewGame();
    else if (cmd == "savecurrent")
    {
        // Save: the game's own file again; a farm that has none yet (a prebuilt farm, a new world) asks for a name
        std::string err;
        if (_ctx.currentSave.empty())
            OpenWindow(kWinSave);
        else if (_ctx.state && _ctx.state->Save(_ctx.currentSave, err))
            Saved(std::filesystem::path(_ctx.currentSave).filename().string());
        else
            ShowMessage(TrF("Save failed: {0}", err));
    }
    else if (cmd.rfind("dis:", 0) == 0 && _ctx.sim)
    {
        static const int ids[6] = { 1, 4, 2, 3, 5, 6 };
        const int k = std::stoi(cmd.substr(4));
        Disasters& d = _ctx.sim->Hazards();
        if (k == 6)
            d.SetDisabled(!d.Disabled());
        else
            d.Start(ids[k]); // a disaster that cannot start now (running, switched off, ...) is silently ignored
    }
    else if (onCommand)
        onCommand(cmd);
}

// ---------------------------------------------------------------- menus
Rect GameUi::MenuBox(int m) const
{
    return { _menus[m].boxX, kMenuH, _menus[m].boxW, _menus[m].boxH };
}

int GameUi::MenuAt(int x, int y) const
{
    if (y < 0 || y >= kMenuH)
        return -1;
    for (size_t m = 0; m < _menus.size(); ++m)
        if (x >= _menus[m].x - 4 && x < _menus[m].x + Gfx::TextWidth(_menus[m].title) + 4)
            return static_cast<int>(m);
    return -1;
}

int GameUi::ItemAt(int x, int y) const
{
    if (_openMenu < 0)
        return -1;
    const Rect b = MenuBox(_openMenu);
    if (!b.Contains(x, y) || y < b.y + 2)
        return -1;
    const int i = (y - b.y - 2) / kItemH;
    return i < static_cast<int>(_menus[_openMenu].items.size()) ? i : -1;
}

void GameUi::DrawMenus()
{
    // the original's bar: dark grey, white titles; a drop-down is a white box with a black outline, each item a line
    // of text starting with a tick ('^' in the game's font) or a space; the item under the pointer is a black bar
    // behind the label only
    _g.Fill(0, 0, _w, kMenuH, kDarkGrey);
    for (const auto& menu : _menus)
        _g.Text(menu.x, 4, menu.title, kWhite);
    DrawRadio();
    if (_openMenu < 0)
        return;
    const Rect b = MenuBox(_openMenu);
    _g.Fill(b.x, b.y, b.w, b.h, kWhite);
    _g.Frame(b.x, b.y, b.w, b.h, kBlack);
    const auto& items = _menus[_openMenu].items;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int y = b.y + 4 + static_cast<int>(i) * kItemH;
        const bool hover = static_cast<int>(i) == _hoverItem;
        const bool enabled = ItemEnabled(items[i].command);
        if (hover && enabled)
            _g.Fill(b.x + 8, y, b.w - 16, 8, kBlack);
        if (ItemChecked(items[i].command))
            _g.Text(b.x, y, "^", enabled ? kBlack : kGrey);
        _g.Text(b.x + 8, y, items[i].label, !enabled ? kGrey : hover ? kWhite : kBlack);
    }
}

// ---------------------------------------------------------------- the radio (an engine addition)
namespace {
struct RadioStation { std::string file, title; bool own; };
// the game's songs, with the engine's own descriptive names for when the game plays them, then the engine's songs
const std::vector<RadioStation>& Stations()
{
    static const std::vector<RadioStation> list = [] {
        std::vector<RadioStation> v = { { "amain.xmi", N_("TITLE THEME"), false }, { "genfarm.xmi", N_("FARM LIFE"), false },
                                        { "ffarm.xmi", N_("GOOD YEARS"), false },  { "dfarm.xmi", N_("HARD TIMES"), false },
                                        { "carnival.xmi", N_("COUNTY FAIR"), false }, { "rodeo.xmi", N_("RODEO"), false },
                                        { "cowman.xmi", N_("CELEBRATION"), false } };
        // the engine's songs in the order of the game songs whose mood they share
        const size_t game = v.size();
        for (size_t m = 0; m <= game; ++m)
            for (const BuiltinSongInfo& b : BuiltinSongs())
            {
                size_t mood = game;
                for (size_t k = 0; k < game; ++k)
                    if (b.mood == v[k].file)
                        mood = k;
                if (mood == m)
                    v.push_back({ b.file, b.title, true });
            }
        return v;
    }();
    return list;
}
std::string StationTitle(const RadioStation& st) { return st.own ? TrId("song:" + st.file, st.title) : std::string(Tr(st.title.c_str())); }
constexpr int kRadioW = GameUi::kRadioWidth, kRadioH = 14; // right after the Help menu at 640 wide; fits "PAUSED CELEBRATION"
}

int GameUi::RadioCount() { return static_cast<int>(Stations().size()); }

Rect GameUi::RadioBox() const { return { _w - kRadioW - 4, 1, kRadioW, kRadioH }; }

Rect GameUi::RadioButton(int k) const
{
    const Rect b = RadioBox();
    if (k < 3)
        return { b.x + 3 + 16 * k, b.y + 2, 14, 10 };
    if (k == 4)
        return { b.x + b.w - 33, b.y + 2, 30, 10 };      // the AUTO button
    return { b.x + 52, b.y + 2, b.w - 52 - 35, 10 };   // the display
}

int GameUi::RadioIndex(const std::string& song) const
{
    for (int i = 0; i < RadioCount(); ++i)
        if (song == Stations()[static_cast<size_t>(i)].file)
            return i;
    return -1;
}

std::string GameUi::RadioSong(const std::string& gameSong)
{
    if (!_radioManual)
    {
        // the game's choice; an engine song of the same mood takes turns with it, a whole song each
        std::vector<std::string> turn = { gameSong };
        for (const BuiltinSongInfo& b : BuiltinSongs())
            if (b.mood == gameSong)
                turn.push_back(b.file);
        _radioLoops = turn.size() == 1;
        int& k = _moodTurn[gameSong];
        if (_ctx.audio && _ctx.audio->MusicEnded() && _ctx.audio->CurrentSong() == turn[static_cast<size_t>(k) % turn.size()])
            k = (k + 1) % static_cast<int>(turn.size());
        return turn[static_cast<size_t>(k) % turn.size()];
    }
    _radioLoops = false;
    if (_ctx.audio && _ctx.audio->MusicEnded())
        _radioTrack = (_radioTrack + 1) % RadioCount(); // a picked song was played to its end: the next one
    return Stations()[static_cast<size_t>(_radioTrack)].file;
}

bool GameUi::RadioClick(int x, int y)
{
    for (int k = 0; k < 5; ++k)
    {
        if (k == 3 || !RadioButton(k).Contains(x, y))
            continue;
        Click();
        const bool musicOn = !musicEnabled || musicEnabled();
        if (k == 0 || k == 2)
        {
            int cur = _radioManual ? _radioTrack : RadioIndex(_ctx.audio ? _ctx.audio->CurrentSong() : "");
            if (cur < 0)
                cur = 0;
            _radioTrack = (cur + (k == 2 ? 1 : RadioCount() - 1)) % RadioCount();
            if (!_radioManual)
            {
                // a hint on the status line only (not kept in its message history)
                _message = Tr("Radio: your pick. Press AUTO to let the game choose again.");
                _messageTime = SDL_GetTicks();
            }
            _radioManual = true;
            if (!musicOn && onCommand)
                onCommand("music"); // skipping to a song plays it
        }
        else if (k == 1 && onCommand)
            onCommand("music");
        else if (k == 4 && _radioManual)
        {
            _radioManual = false;
            _message = Tr("Radio: the game chooses the music again.");
            _messageTime = SDL_GetTicks();
        }
        _radioPressed = k;
        _radioPressedAt = SDL_GetTicks();
        return true;
    }
    return RadioBox().Contains(x, y); // the rest of the radio takes the click too
}

void GameUi::DrawRadio()
{
    const Rect b = RadioBox();
    if (b.x < 428)
        return; // no room beside the menus
    _g.Fill(b.x, b.y, b.w, b.h, kBlack);
    _g.Fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kBrown);
    const bool musicOn = !musicEnabled || musicEnabled();
    if (_radioPressed >= 0 && SDL_GetTicks() - _radioPressedAt > 150)
        _radioPressed = -1;
    // a triangle 4 wide and 7 high pointing right (dir 1) or left (dir -1), its flat side at x
    auto tri = [&](int x, int cy, int dir) {
        for (int i = 0; i < 4; ++i)
            _g.Fill(x + dir * i, cy - 3 + i, 1, 7 - 2 * i, kBlack);
    };
    for (int k = 0; k < 3; ++k)
    {
        const Rect r = RadioButton(k);
        const bool down = _radioPressed == k;
        _g.Fill(r.x, r.y, r.w, r.h, kLightGrey);
        _g.Bevel(r.x, r.y, r.w, r.h, !down);
        const int cx = r.x + 7 + (down ? 1 : 0), cy = r.y + 4 + (down ? 1 : 0);
        if (k == 0)
        {
            _g.Fill(cx - 5, cy - 3, 1, 7, kBlack);
            tri(cx - 1, cy, -1);
            tri(cx + 3, cy, -1);
        }
        else if (k == 2)
        {
            tri(cx - 4, cy, 1);
            tri(cx, cy, 1);
            _g.Fill(cx + 4, cy - 3, 1, 7, kBlack);
        }
        else if (musicOn)
        {
            _g.Fill(cx - 3, cy - 3, 2, 7, kBlack); // playing: the pause sign
            _g.Fill(cx + 1, cy - 3, 2, 7, kBlack);
        }
        else
            tri(cx - 2, cy, 1); // paused: the play sign
    }
    // the display: the song's number and name in green; paused: PAUSED in yellow before the name
    const Rect d = RadioButton(3);
    _g.Fill(d.x, d.y, d.w, d.h, kBlack);
    _g.Frame(d.x, d.y, d.w, d.h, kDarkGrey);
    const int idx = _radioManual ? _radioTrack : RadioIndex(_ctx.audio ? _ctx.audio->CurrentSong() : "");
    const std::string name = idx >= 0 ? StationTitle(Stations()[static_cast<size_t>(idx)]) : std::string(Tr("NO MUSIC"));
    std::string prefix = !musicOn ? std::string(Tr("PAUSED")) + " " : idx >= 0 ? std::to_string(idx + 1) + "/" + std::to_string(RadioCount()) + " " : "";
    if (Gfx::SmallTextWidth(prefix + name) > d.w - 5 && musicOn)
        prefix.clear(); // a long name: the number gives way
    _g.SmallText(d.x + 3, d.y + 3, prefix, musicOn ? kGreen : kYellow);
    _g.SmallText(d.x + 3 + (prefix.empty() ? 0 : Gfx::SmallTextWidth(prefix) + 1), d.y + 3, name, kGreen);
    // AUTO: lit (yellow, pressed in) while the game chooses the songs; a plain button after the player picked one
    const Rect a = RadioButton(4);
    const bool lit = !_radioManual;
    _g.Fill(a.x, a.y, a.w, a.h, lit ? kYellow : kLightGrey);
    _g.Bevel(a.x, a.y, a.w, a.h, !lit && _radioPressed != 4);
    const int tw = Gfx::SmallTextWidth(Tr("AUTO"));
    _g.SmallText(a.x + (a.w - tw) / 2 + (lit ? 1 : 0), a.y + (lit ? 3 : 2), Tr("AUTO"), lit ? kBlack : kDarkGrey);
}

// ---------------------------------------------------------------- toolbar, status strip
void GameUi::DrawToolbar()
{
    // left part (buttons and forecast box) at the left edge, date/funds part at the right edge, grey between
    const int rightX = _w - kToolbarRightPart;
    // a window wider than 640 stretches the toolbar between the two picture parts: fill the gap with the picture's
    // plain columns (black top row over grey), so the black line under the menu bar runs across the whole width
    // the forecast box (picture x 304..399) moves with the right part, so it and the weather summary stay beside
    // the date in a wide window (an engine change, user finding Q16b); the buttons stay at the left
    const int fx = ForecastX();
    for (int x = 256; x < rightX; x += 20)
        _g.DrawPicturePart("cntlbtnh.bmp", 380, 0, std::min(20, rightX - x), kToolbarH, x, kToolbarY);
    _g.DrawPicturePart("cntlbtnh.bmp", 0, 0, 224, kToolbarH, 0, kToolbarY);
    _g.DrawPicturePart("cntlbtnh.bmp", 400, 0, kToolbarRightPart, kToolbarH, rightX, kToolbarY);
    // no EDIT button (an engine change): Market Value moves into its place, its own place shows the toolbar's grey
    _g.DrawPicturePart("cntlbtnh.bmp", 256, 0, 32, kToolbarH, 224, kToolbarY);
    _g.DrawPicturePart("cntlbtnh.bmp", 304, 0, 96, kToolbarH, fx, kToolbarY);
    for (int slot = 0; slot < 8; ++slot)
        if (IsOpen(ToolbarWindow(ToolbarButton(slot))))
        {
            // pressed: the two-pixel bevel inside the black border turns round (dark top / left, white bottom /
            // right), the face stays as it is (measured on the original)
            const int x = slot * 32, y = kToolbarY;
            _g.Fill(x + 1, y + 1, 29, 1, kGrey);
            _g.Fill(x + 1, y + 2, 28, 1, kGrey);
            _g.Fill(x + 30, y + 2, 1, 1, kWhite);
            _g.Fill(x + 1, y + 3, 2, 26, kGrey);
            _g.Fill(x + 29, y + 3, 2, 26, kWhite);
            _g.Fill(x + 1, y + 29, 2, 1, kGrey);
            _g.Fill(x + 3, y + 29, 28, 1, kWhite);
            _g.Fill(x + 1, y + 30, 1, 1, kGrey);
            _g.Fill(x + 2, y + 30, 29, 1, kWhite);
        }

    if (!_ctx.state || !_ctx.sim)
        return;
    const GameState& s = *_ctx.state;
    // forecast strip: the next three days
    static const int skyIcon[5] = { 204, 205, 206, 207, 208 };
    for (int i = 0; i < 3; ++i)
    {
        const int d = s.cal.day + 1 + i;
        const int p = d < 7 ? _ctx.sim->WeekPattern()[d] : _ctx.sim->NextWeekPattern()[d - 7];
        _g.DrawTile(skyIcon[std::clamp(p, 0, 4)], fx + 24 + 16 * i, kToolbarY + 7);
    }
    // a short weather summary right of the forecast (an engine addition, user finding Q16): today's sky, today's
    // temperature (the WEATHER window's figures) and the wind, in the small text
    {
        static const char* const kSky[5] = { N_("Clear"), N_("Fair"), N_("Cloudy"), N_("Rain"), N_("Snow") };
        const int tf = _ctx.sim->DailyTemperature() > -1000 ? _ctx.sim->DailyTemperature() : _ctx.sim->Temperature();
        _g.SmallText(fx + 77, kToolbarY + 5, Tr(kSky[std::clamp(_ctx.sim->TodaySky(), 0, 4)]), kBlack);
        _g.SmallText(fx + 77, kToolbarY + 13, std::to_string(tf) + "F " + std::to_string((tf - 32) >> 1) + "C", kBlack);
        _g.SmallText(fx + 77, kToolbarY + 21, TrF("{0} mph", _ctx.sim->Wind()), kBlack);
    }
    // the original shows its event boxes inside the day step, after the week has moved on but before the toolbar
    // is drawn again, so the old date stays up behind the month-change, fair, rodeo and year-end boxes
    const std::string date = TrF("Week {0} of {1} {2}", s.cal.week + 1, Tr(kMonthShort[s.cal.month]), s.cal.year);
    // the lost auction asks its question inside that step too, so the date and the funds from before it stay up
    // behind its boxes until the program ends (seen live: Week 4 of Oct and 0 behind all three boxes)
    const bool lost = _ctx.sim->Bankrupt();
    if ((_events.empty() && !lost) || _shownDate.empty())
    {
        _shownDate = date;
        _shownCash = s.cash;
    }
    if (!lost)
        _shownCash = s.cash;
    _g.Text(rightX + 64, kToolbarY + 5, _shownDate, kBlack);
    _g.Text(rightX + 64, kToolbarY + 19, "      " + std::to_string(_shownCash), kBlack); // the game's format: 6 spaces, then the number

}

// the status row of the EDIT window: messages, the pause notice or the chosen tool
void GameUi::ShowMessage(const std::string& m)
{
    _message = m;
    _messageTime = SDL_GetTicks();
    if (m.empty())
        return;
    std::string date;
    if (_ctx.state)
        date = TrF("W{0} {1} {2}", _ctx.state->cal.week + 1, Tr(kMonthShort[_ctx.state->cal.month % 12]), _ctx.state->cal.year);
    if (!_history.empty() && _history.back().second == m)
        _history.back().first = date; // the same message again: one line, with its latest date
    else
        _history.emplace_back(date, m);
    if (_history.size() > 200)
        _history.pop_front();
}

void GameUi::DrawHistory()
{
    if (!_historyOpen)
        return;
    const Rect b = HistoryBox();
    _g.Fill(b.x, b.y, b.w, b.h, kLightGrey);
    _g.Frame(b.x, b.y, b.w, b.h, kBlack);
    const int n = static_cast<int>(_history.size());
    _historyTop = std::clamp(_historyTop, 0, std::max(0, n - kHistoryRows));
    if (n == 0)
        _g.Text(b.x + 6, b.y + 4, Tr("No messages yet."), kBlack);
    const int chars = std::max(0, (b.w - 12 - 96) / 8);
    for (int r = 0; r < kHistoryRows && _historyTop + r < n; ++r)
    {
        const auto& e = _history[static_cast<size_t>(n - 1 - _historyTop - r)];
        _g.Text(b.x + 6, b.y + 4 + 10 * r, e.first, kGrey);
        _g.Text(b.x + 102, b.y + 4 + 10 * r, e.second.substr(0, static_cast<size_t>(chars)), kBlack);
    }
}

void GameUi::DrawStatus()
{
    if (!_ctx.state || !_ctx.sim)
        return;
    std::string text;
    if (_ctx.sim->Plane().Flying())
    {
        const Flight& f = _ctx.sim->Plane();
        text = TrF("Flying  height {0}", f.Altitude()) + (f.Spraying() ? std::string("  ") + Tr("SPRAYING") : std::string()) + "   " +
               Tr("Arrows fly, F sprays, Enter lands");
    }
    else if (!_message.empty() && SDL_GetTicks() - _messageTime < 6000)
        text = _message;
    else if (_ctx.sim->GetSpeed() == Speed::Paused)
        text = Tr("Game Paused...");
    else if (_tool != Tool::None)
    {
        const ToolInfo& info = GetToolInfo(_tool);
        text = _tool == Tool::Plant ? TrF("Plant {0}  Seed: {1}", CropName(*_ctx.state, _toolParam & 15),
                                          std::max(5, _ctx.state->MarketPrice(_toolParam & 15) >> 2))
               : _tool == Tool::Spray ? TrF("{0}: click a field", Tr(kSprayNames[_toolParam - 6]))
               : _tool == Tool::Examine ? std::string(Tr("Examine: click a field or an object"))
               : _tool == Tool::Move ? std::string(_moveKind ? Tr("Move: click the new place") : Tr("Move: choose a machine, a barrel or a stored thing"))
               : _tool == Tool::Place ? TrF("Place {0}  Price: {1}", ShopName(*_ctx.state, _toolParam), ShopPrice(*_ctx.state, _toolParam))
               : info.cost ? TrF("{0}  Cost: {1}", Tr(info.name), info.cost)
                           : TrF("{0}: not available yet", Tr(info.name));
    }
    const int tx = MapLeft() + 20; // right of the status row's arrow button
    // the game speed at the right end of the status row, in the small text (an engine addition, user finding Q23)
    static const char* const kSpeedName[5] = { N_("Speed: Ultra"), N_("Speed: Fast"), N_("Speed: Normal"), N_("Speed: Slow"), N_("Paused") };
    const std::string speed = Tr(kSpeedName[std::clamp(static_cast<int>(_ctx.sim->GetSpeed()), 0, 4)]);
    const int sx = _w - kScrollBar - 4 - Gfx::SmallTextWidth(speed);
    _g.SmallText(sx, kStatusY + 4, speed, kNavy);
    // a text too long for the row is drawn with tighter letters, and cut only when even that does not fit
    const int room = sx - 8 - tx;
    if (Gfx::TextWidth(text) > room && Gfx::FitWidth(text, room) <= room)
        _g.TextFit(tx, kStatusY + 3, room, text, kBlack);
    else
        _g.Text(tx, kStatusY + 3, text.substr(0, static_cast<size_t>(std::max(0, room / 8))), kBlack);
}

// ---------------------------------------------------------------- EDIT window frame
// Measured on the original at 640x480: window x 16..623, y 48..463; title bar 48..63 with the close button at the
// left; palette column x 16..63; status row y 64..77 (a down-arrow button at x 64, the text from x 80, the
// scroll-up arrow at the right); the map; a vertical scroll bar x 608..623 and a horizontal one y 448..463 with
// arrow buttons and a box in the corner; outside the window the screen is black. The engine's window fills the
// screen (no black margins), the same parts shifted to the edges.
void GameUi::DrawEditFrame()
{
    const Rect m = MapRect();
    const int right = m.x + m.w, bottom = m.y + m.h;   // 624, 464 at 640x480
    // palette column and status row
    _g.Fill(kPaletteX, kPalTop, PaletteW(), bottom - kPalTop, kLightGrey);
    _g.Fill(MapLeft(), kStatusY, right - MapLeft(), kStatusH, kLightGrey);
    _g.Frame(MapLeft() + 16, kStatusY, right - MapLeft() - 16, kStatusH, kBlack);
    // a bevelled button with a black arrow (shaft and head), as on the original's scroll buttons; the head is as
    // wide as the button allows
    auto button = [&](int x, int y, int w, int h, int dir) { // 0 up, 1 right, 2 down, 3 left
        _g.Fill(x, y, w, h, kLightGrey);
        _g.Bevel(x, y, w, h, true);
        const int cx = x + w / 2, cy = y + h / 2;
        const bool vertical = dir == 0 || dir == 2;
        const int heads = std::min(4, ((vertical ? w : h) - 1) / 2 + 1);
        const int len = vertical ? h : w;
        const int shaft = std::max(0, std::min(5, len - heads - 5));
        const int total = heads + shaft;
        for (int k = 0; k < heads; ++k)
        {
            const int along = k - total / 2; // head rows from the tip, widest at the base
            if (dir == 0) _g.Fill(cx - k, cy + along, 2 * k + 1, 1, kBlack);
            if (dir == 2) _g.Fill(cx - k, cy - along - 1, 2 * k + 1, 1, kBlack);
            if (dir == 3) _g.Fill(cx + along, cy - k, 1, 2 * k + 1, kBlack);
            if (dir == 1) _g.Fill(cx - along - 1, cy - k, 1, 2 * k + 1, kBlack);
        }
        const int sw = heads >= 4 ? 3 : 1, s0 = heads - total / 2;
        if (shaft > 0)
        {
            if (dir == 0) _g.Fill(cx - sw / 2, cy + s0, sw, shaft, kBlack);
            if (dir == 2) _g.Fill(cx - sw / 2, cy - s0 - shaft + 1, sw, shaft, kBlack);
            if (dir == 3) _g.Fill(cx + s0, cy - sw / 2, shaft, sw, kBlack);
            if (dir == 1) _g.Fill(cx - s0 - shaft + 1, cy - sw / 2, shaft, sw, kBlack);
        }
    };
    button(MapLeft(), kStatusY, 16, 14, 2);
    if (_historyOpen)
    {
        _g.Bevel(MapLeft(), kStatusY, 16, 14, false); // pressed while the message list is open
    }
    // scroll bars: checkered tracks, a white thumb at the view position, arrow buttons, the corner box
    float vx = 0, vy = 0, vw = 1, vh = 1;
    if (_ctx.view)
        _ctx.view->VisibleCells(vx, vy, vw, vh);
    const int cells = _ctx.view ? _ctx.view->MapSize() : kOriginalMapSize;
    const int B = kScrollBar, A = kScrollArrow;
    // checkered tracks: light grey with every other pixel dark
    _g.Fill(right, m.y, B, bottom - A - m.y, kLightGrey);
    _g.Fill(A, bottom, right - 2 * A, B, kLightGrey);
    for (int y = m.y; y < bottom - A; ++y)
        for (int x = right + (y & 1); x < right + B; x += 2)
            _g.Fill(x, y, 1, 1, kDarkGrey);
    for (int y = bottom; y < bottom + B; ++y)
        for (int x = A + (y & 1); x < right - A; x += 2)
            _g.Fill(x, y, 1, 1, kDarkGrey);
    const int vTrack = bottom - A - m.y, hTrack = right - 2 * A;
    _g.Fill(right + 1, m.y + static_cast<int>(ThumbFraction(vy, vh, cells) * (vTrack - A)), B - 2, A, kWhite);
    _g.Fill(A + static_cast<int>(ThumbFraction(vx, vw, cells) * (hTrack - A)), bottom + 1, A, B - 2, kWhite);
    button(right, kStatusY, B, 14, 0);
    button(right, bottom - A, B, A, 2);
    button(0, bottom, A, B, 3);
    button(right - A, bottom, A, B, 1);
    _g.Fill(right, bottom, B, B, kLightGrey);
    _g.Bevel(right, bottom, B, B, true);
}

// where a scroll thumb sits along its track: 0 with the view at the top / left edge of the farm, 1 at the bottom /
// right edge, whatever the zoom (the view can only move the map's cells less what it shows; user finding: at some
// zoom levels the thumb stopped short of the end)
float GameUi::ThumbFraction(float pos, float shown, int mapCells)
{
    const float range = static_cast<float>(mapCells) - shown;
    return range > 0.01f ? std::clamp(pos / range, 0.0f, 1.0f) : 0.0f;
}

bool GameUi::FrameClick(int x, int y)
{
    if (!_ctx.view)
        return false;
    const Rect m = MapRect();
    const int right = m.x + m.w, bottom = m.y + m.h;
    float vx, vy, vw, vh;
    _ctx.view->VisibleCells(vx, vy, vw, vh);
    int dx = 0, dy = 0;
    const int B = kScrollBar, A = kScrollArrow;
    if (x >= right && x < right + B && y >= kStatusY && y < bottom)
        dy = y < m.y ? -1 : y >= bottom - A ? 1 : (y - m.y < ThumbFraction(vy, vh, _ctx.view->MapSize()) * (bottom - 2 * A - m.y) ? -static_cast<int>(vh) : static_cast<int>(vh));
    else if (y >= bottom && y < bottom + B && x >= 0 && x < right)
        dx = x < A ? -1 : x >= right - A ? 1 : (x - A < ThumbFraction(vx, vw, _ctx.view->MapSize()) * (right - 3 * A) ? -static_cast<int>(vw) : static_cast<int>(vw));
    else
        return false;
    _ctx.view->SetTopLeft(static_cast<int>(vx) + dx, static_cast<int>(vy) + dy);
    if (std::abs(dx) == 1 || std::abs(dy) == 1)
    {
        // an arrow: keeps scrolling while the button is held
        _arrowDX = dx;
        _arrowDY = dy;
        _arrowNext = SDL_GetTicks() + 300;
    }
    Click();
    return true;
}

void GameUi::ArrowRepeat()
{
    if ((_arrowDX == 0 && _arrowDY == 0) || !_ctx.view)
        return;
    if (!(MouseState(nullptr, nullptr) & SDL_BUTTON_LMASK))
    {
        _arrowDX = _arrowDY = 0;
        return;
    }
    const Uint32 now = SDL_GetTicks();
    if (static_cast<int32_t>(now - _arrowNext) < 0)
        return;
    _arrowNext = now + 40;
    float vx, vy, vw, vh;
    _ctx.view->VisibleCells(vx, vy, vw, vh);
    _ctx.view->SetTopLeft(static_cast<int>(vx) + _arrowDX, static_cast<int>(vy) + _arrowDY);
}

// ---------------------------------------------------------------- EDIT palette
// The original's palette picture (editeasy.lzs, 48 x 208): six rows of two 24-pixel tool icons, then the PLANT,
// SPRAY and ? buttons. The engine shows one item per line over the full height of the column, and the
// >> / << button at the foot shows or hides the tool names beside the icons.
static const Tool kPaletteGrid[6][2] = { { Tool::Move, Tool::Examine }, { Tool::PavedRoad, Tool::DirtRoad }, { Tool::Fence, Tool::Gate },
                                         { Tool::Ditch, Tool::Valve }, { Tool::Hay, Tool::Trough }, { Tool::Trees, Tool::Bulldoze } };
static const char* const kToolNames[12] = { N_("Move"), N_("Examine"), N_("Paved Road"), N_("Dirt Road"), N_("Fence"), N_("Gate"),
                                            N_("Ditch"), N_("Valve"), N_("Feed"), N_("Trough"), N_("Trees"), N_("Bulldoze") };
static const char* const kButtonNames[2] = { N_("Plant"), N_("Spray") };
// the palette in groups (separated by a line): move/examine, roads, fences, water, livestock, trees/bulldoze, crops
static bool GroupStart(int i) { return i > 0 && i % 2 == 0; }
static constexpr int kToggleH = 16;
static constexpr int kToggleGap = 6; // space between SPRAY and the >> / << button (user finding Q19)

Rect GameUi::PaletteCell(int i) const
{
    // one 24-pixel line per item: the 12 tools, PLANT, SPRAY; then the >> / << button
    if (i == 15)
        return { kPaletteX, kPalTop + 24 * 14 + kToggleGap, PaletteW(), kToggleH };
    return { kPaletteX, kPalTop + 24 * i, PaletteW(), 24 };
}

void GameUi::DrawPalette()
{
    if (!_ctx.state)
        return;
    const auto chosen = [&](int i) {
        return i < 12 ? kPaletteGrid[i / 2][i % 2] == _tool : (i == 12 && _tool == Tool::Plant) || (i == 13 && _tool == Tool::Spray);
    };
    if (_paletteVisible)
        for (int i = 0; i < 14; ++i)
        {
            const Rect r = PaletteCell(i);
            if (i < 12) // a 24 x 24 icon of the original picture (two columns of six)
                _g.DrawPicturePart("editeasy.lzs", 24 * (i % 2), 24 * (i / 2), 24, 24, r.x, r.y);
            else
            {
                // PLANT and SPRAY show the BUY window's seed and chemical category pictures (user findings Q6/Q7):
                // the framed corn (16 x 16 at 16, 42 of its background) and barrel (18 x 18 at 15, 131), on a light
                // grey icon with the original's frame
                _g.Fill(r.x, r.y, 24, 24, kLightGrey);
                _g.Fill(r.x, r.y, 24, 1, kBlack);
                _g.Fill(r.x, r.y, 1, 24, kBlack);
                if (i == 12)
                    _g.DrawPicturePart("buywndo.bmp", 16, 42, 16, 16, r.x + 4, r.y + 4);
                else
                    _g.DrawPicturePart("buywndo.bmp", 15, 131, 18, 18, r.x + 3, r.y + 3);
            }
            // the chosen tool is drawn pressed, as the original: its 2-pixel bevel lit from the other side. PLANT and
            // SPRAY (never pressed in the original) also show pressed while their list is open or their tool is chosen
            const bool pressed = chosen(i) || (i == 12 && _popup == 1) || (i == 13 && _popup == 2);
            if (_toolNames)
            {
                // with the names shown the icon and its name are one button across the row (an engine change, user
                // finding Q30): the icon's own right edge is covered by the button face, the bevel goes round both
                _g.Fill(r.x + 22, r.y + 1, r.w - 22, 23, kLightGrey);
                _g.Fill(r.x, r.y, r.w, 1, kBlack);
                _g.SmallText(r.x + 28 + (pressed ? 1 : 0), r.y + 10 + (pressed ? 1 : 0), Tr(i < 12 ? kToolNames[i] : kButtonNames[i - 12]), kBlack);
                IconBevel(r.x, r.y, pressed, r.w);
            }
            else if (i >= 12 || pressed)
                IconBevel(r.x, r.y, pressed);
        }
    // group separators: a dark line on the first row of each group, across the column (no extra space)
    if (_paletteVisible)
        for (int i = 0; i < 14; ++i)
            if (GroupStart(i))
                _g.Fill(kPaletteX, PaletteCell(i).y, PaletteW(), 1, kDarkGrey);
    // the >> / << button
    const Rect t = PaletteCell(15);
    _g.Fill(t.x, t.y, t.w, t.h, kLightGrey);
    _g.Bevel(t.x, t.y, t.w, t.h, true);
    _g.TextCentre(t.x + t.w / 2, t.y + 4, _toolNames ? "<<" : ">>", kBlack);
    // pull-down lists for PLANT and SPRAY: each crop's green-framed icon (tile 832 + slot, from its crop file), the
    // spray items' pictures (masked tiles 67 .. 64), the names 16 pixels right of the pictures
    if (_popup)
    {
        const Rect b = PopupBox();
        _g.Fill(b.x, b.y, b.w, b.h, kLightGrey);
        _g.Frame(b.x, b.y, b.w, b.h, kBlack);
        _g.Text(b.x + 16, b.y + 4, _popup == 1 ? Tr("Plant Field") : Tr("Spray Field"), kBlack);
        const auto& pal = _g.Pal();
        auto argb = [&](int v) {
            const auto& p = pal[static_cast<size_t>(v & 15)];
            return 0xFF000000u | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        };
        for (int i = 0; i < PopupCount(); ++i)
        {
            const Rect r = PopupRow(i);
            // the row under the pointer is marked like a menu line (an engine addition, user finding Q26)
            const bool hover = r.Contains(_lastMx, _lastMy);
            const int ink = hover ? kWhite : kBlack;
            if (hover)
                _g.Fill(r.x + 1, r.y, r.w - 2, r.h, kNavy);
            if (_popup == 1)
            {
                const PlantEntry& pe = _plantList[static_cast<size_t>(i)];
                if (pe.slot >= 0)
                    _g.DrawTile(832 + pe.slot, r.x, r.y);
                else
                {
                    // a crop file not loaded: its icon from the file
                    const auto& icon = _spareCrops[static_cast<size_t>(pe.spare)].icon;
                    std::vector<uint32_t> img(256, argb(kLightGrey));
                    for (size_t p = 0; p < 256 && p < icon.size(); ++p)
                        img[p] = argb(icon[p]);
                    _g.DrawPixels(img, 16, 16, r.x, r.y, 16, 16);
                }
                _g.Text(r.x + 16, r.y + 4, pe.name.substr(0, 12), ink);
            }
            else
            {
                _g.DrawAnyTile(848 + 67 - i, r.x, r.y);
                _g.Text(r.x + 16, r.y + 4, Tr(kSprayNames[i]), ink);
            }
        }
        _g.Frame(b.x, b.y, b.w, b.h, kBlack); // the frame stays over the pictures of the first column
    }
}

void GameUi::ListSpareCrops()
{
    _spareCrops.clear();
    _plantList.clear();
    if (!_ctx.state || _ctx.gameDir.empty())
    {
        BuildPlantList();
        return;
    }
    std::error_code ec;
    const auto dir = FindFileNoCase(_ctx.gameDir, "CROPS");
    if (!dir)
    {
        BuildPlantList();
        return;
    }
    std::set<std::string> loaded;
    for (const CropDef& c : _ctx.state->crops)
    {
        std::string n = c.fileName;
        for (char& ch : n)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        loaded.insert(n);
    }
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(*dir, ec))
    {
        std::string n = e.path().filename().string();
        for (char& ch : n)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (n.size() > 4 && n.substr(n.size() - 4) == ".CRP" && !loaded.count(n))
            files.push_back(n);
    }
    std::sort(files.begin(), files.end());
    for (const std::string& n : files)
    {
        CropDef def;
        if (_spareCrops.size() >= 64 || !LoadCropFile(_ctx.gameDir, n, def) || def.art.size() < 14 * 128)
            continue;
        const TileSheet t = DecodeTiles16(def.art, 14);
        _spareCrops.push_back({ n, def.name, std::vector<uint8_t>(t.Tile(13), t.Tile(13) + 256) });
    }
    BuildPlantList();
}

void GameUi::BuildPlantList()
{
    _plantList.clear();
    if (!_ctx.state)
        return;
    for (int i = 0; i < 16 && i < static_cast<int>(_ctx.state->crops.size()); ++i)
        _plantList.push_back({ CropName(*_ctx.state, i), i, -1 });
    for (size_t k = 0; k < _spareCrops.size(); ++k)
        _plantList.push_back({ CropNameFor(_spareCrops[k].file, _spareCrops[k].name), -1, static_cast<int>(k) });
    std::stable_sort(_plantList.begin(), _plantList.end(), [&](const PlantEntry& a, const PlantEntry& b) { return SortKey(a.name) < SortKey(b.name); });
}

void GameUi::PlantSpareCrop(int k)
{
    if (!_ctx.sim || k < 0 || k >= static_cast<int>(_spareCrops.size()))
        return;
    const int slot = _ctx.sim->FreeCropSlot();
    std::string why;
    if (slot < 0)
    {
        ShowMessage(TrF("Every crop slot is in use: sell or plant over a crop to make room for {0}.", CropNameFor(_spareCrops[static_cast<size_t>(k)].file, _spareCrops[static_cast<size_t>(k)].name)));
        return;
    }
    const std::string replaced = CropName(*_ctx.state, slot);
    if (!_ctx.sim->ReplaceCrop(slot, _spareCrops[static_cast<size_t>(k)].file, why))
    {
        ShowMessage(why);
        return;
    }
    ShowMessage(TrF("{0} loaded in place of {1}: click a field", CropNameFor(_spareCrops[static_cast<size_t>(k)].file, _spareCrops[static_cast<size_t>(k)].name), replaced));
    _tool = Tool::Plant;
    _toolParam = slot;
}

// the 2-pixel bevel of a 24 x 24 palette icon (inside its black top / left line): raised = white top / left, grey
// (13) bottom / right, cut diagonally at the corners; pressed (read from the original's chosen tool) = the colours
// swapped, the bottom-left corner grey
void GameUi::IconBevel(int x, int y, bool pressed, int w)
{
    // the original's 24 x 24 icon bevel; w > 24 stretches it across a palette row with the tool name (Q30)
    const uint8_t lit = pressed ? kGrey : kWhite, shade = pressed ? kWhite : kGrey;
    _g.Fill(x + 1, y + 1, w - 2, 1, lit);
    _g.Fill(x + 1, y + 2, w - 3, 1, lit);
    _g.Fill(x + 1, y + 3, 2, 19, lit);
    _g.Fill(x + w - 1, y + 1, 1, 1, kLightGrey);
    _g.Fill(x + w - 2, y + 2, 1, 1, kLightGrey);
    _g.Fill(x + w - 1, y + 2, 1, 20, shade);
    _g.Fill(x + w - 2, y + 3, 1, 19, shade);
    _g.Fill(x + 3, y + 22, w - 3, 1, shade);
    _g.Fill(x + 2, y + 23, w - 2, 1, shade);
    _g.Fill(x + 1, y + 22, 1, 1, pressed ? kGrey : kWhite);
    _g.Fill(x + 2, y + 22, 1, 1, pressed ? kGrey : kLightGrey);
    _g.Fill(x + 1, y + 23, 1, 1, pressed ? kGrey : kLightGrey);
}

Rect GameUi::PopupBox() const
{
    const int rows = PopupRows(), cols = _popup == 1 ? 2 : 1;
    const Rect button = PaletteCell(_popup == 1 ? 12 : 13);
    const int bottom = MapRect().y + MapRect().h;
    const int h = 17 * (rows + 1) + rows;
    const int y = std::clamp(button.y - (_popup == 1 ? 96 : 40), kPalTop, std::max(kPalTop, bottom - h));
    return { kPaletteX + PaletteW(), y, 1 + 112 * cols, h };
}

bool GameUi::PaletteClick(int x, int y)
{
    _popupWas = _popup;
    if (_popup)
    {
        for (int i = 0; i < PopupCount(); ++i)
            if (PopupRow(i).Contains(x, y))
            {
                if (_popup == 1 && _plantList[static_cast<size_t>(i)].slot < 0)
                    PlantSpareCrop(_plantList[static_cast<size_t>(i)].spare);
                else
                {
                    _tool = _popup == 1 ? Tool::Plant : Tool::Spray;
                    _toolParam = _popup == 1 ? _plantList[static_cast<size_t>(i)].slot : 6 + i;
                }
                _popup = 0;
                return true;
            }
        const bool inPopup = PopupBox().Contains(x, y);
        _popup = 0;
        if (inPopup)
            return true;
    }
    if (PaletteCell(15).Contains(x, y))
    {
        _toolNames = !_toolNames;
        Click();
        return true;
    }
    if (!_paletteVisible)
        return false;
    int cell = -1;
    for (int i = 0; i < 14 && cell < 0; ++i)
        if (PaletteCell(i).Contains(x, y))
            cell = i;
    if (cell < 0)
        return x >= kPaletteX && x < kPaletteX + PaletteW() && y >= kPalTop && y < MapRect().y + MapRect().h; // the column's gaps
    if (cell < 12)
    {
        const Tool t = kPaletteGrid[cell / 2][cell % 2];
        _tool = _tool == t ? Tool::None : t;
    }
    else
    {
        // PLANT / SPRAY open their list; a click on the button while its list is open closes it again and lets go
        // of its tool (the engine's own toggle, like the other tools)
        const int list = cell == 12 ? 1 : 2;
        const Tool t = cell == 12 ? Tool::Plant : Tool::Spray;
        if (_popupWas == list)
        {
            if (_tool == t)
                _tool = Tool::None;
        }
        else
        {
            _popup = list;
            if (list == 1)
                ListSpareCrops();
        }
    }
    _moveKind = 0;
    return true;
}

void GameUi::UseTool(int px, int py, bool first)
{
    if (!_ctx.view || !_ctx.state || !_ctx.sim || _tool == Tool::None)
        return;
    int cx, cy;
    if (!_ctx.view->CellAt(px, py, cx, cy))
        return;
    if (first && _tool != Tool::Bulldoze && MapPress(cx, cy)) // the original skips the check for Bulldoze
    {
        // the click was taken (a valve, a parked plane, a town building's window): the tool does nothing this press
        _lastCellX = cx;
        _lastCellY = cy;
        _exHold = _exShown = false;
        return;
    }
    if (!first && cx == _lastCellX && cy == _lastCellY && _tool != Tool::Examine)
        return;
    // dragging paints only the line and area tools
    if (!first && (_tool == Tool::Plant || _tool == Tool::Spray || _tool == Tool::Move || _tool == Tool::Gate ||
                   _tool == Tool::Valve))
        return;
    if (_tool == Tool::Move)
    {
        if (!first)
            return;
        if (_moveKind == 0)
        {
            std::string why;
            if (PickObject(*_ctx.state, *_ctx.sim, cx, cy, _moveKind, _moveIndex, why))
            {
                _moveFromX = cx;
                _moveFromY = cy;
                ShowMessage(Tr("Now click where it should go."));
            }
            else
            {
                if (!why.empty())
                    ShowMessage(why);
                if (onSound)
                    onSound("uhoh.voc");
            }
            return;
        }
        const EditResult r = DropObject(*_ctx.state, *_ctx.sim, _moveKind, _moveIndex, _moveFromX, _moveFromY, cx, cy);
        if (r.ok)
            _moveKind = 0;
        if (!r.message.empty())
            ShowMessage(r.message);
        if (!r.sound.empty() && onSound)
            onSound(r.sound);
        return;
    }
    if (_tool == Tool::Place)
    {
        if (!first)
            return;
        const EditResult r = ShopBuy(*_ctx.state, *_ctx.sim, _toolParam, cx, cy);
        if (!r.message.empty())
            ShowMessage(r.message);
        if (!r.sound.empty() && onSound)
            onSound(r.sound);
        return;
    }
    if (_tool == Tool::Examine)
    {
        // in the original: a press on a field opens its schedule, elsewhere a card shows while the button is
        // held, in the screen quarter away from the pointer; moving onto a field while holding opens that field's
        // schedule when the button is let go
        const int q = ExamineQuadrant(px / _g.Scale(), py / _g.Scale());
        if (first)
        {
            _lastCellX = cx;
            _lastCellY = cy;
            _exField = 0;
            if (const int fi = _ctx.sim->FieldIndexAt(cx, cy))
            {
                if (q >= 0) // on the middle lines the original does nothing at all
                    OpenFieldSchedule(fi);
            }
            else
            {
                _exQuad = q;
                _exHold = _exShown = q >= 0 && ExamineCardAt(*_ctx.state, cx, cy, _exCard);
            }
            return;
        }
        if (!_exHold)
            return;
        const bool moved = cx != _lastCellX || cy != _lastCellY;
        if (q != _exQuad)
        {
            _exQuad = q;
            _exShown = false;
        }
        else if (!moved)
            return;
        _lastCellX = cx;
        _lastCellY = cy;
        if (q < 0)
            return;
        if (const int fi = _ctx.sim->FieldIndexAt(cx, cy))
            _exField = fi;
        else if (ExamineCardAt(*_ctx.state, cx, cy, _exCard))
            _exShown = true;
        return;
    }
    // walk cell by cell from the previous cell so fast drags leave no gaps
    int x = first ? cx : _lastCellX, y = first ? cy : _lastCellY;
    std::string message, sound;
    while (true)
    {
        if (first || x != _lastCellX || y != _lastCellY)
        {
            const EditResult r = ApplyTool(*_ctx.state, *_ctx.sim, _tool, x, y, _toolParam);
            if (!r.message.empty())
                message = r.message;
            if (!r.sound.empty())
                sound = r.sound;
        }
        if (x == cx && y == cy)
            break;
        if (std::abs(cx - x) >= std::abs(cy - y))
            x += cx > x ? 1 : -1;
        else
            y += cy > y ? 1 : -1;
    }
    _lastCellX = cx;
    _lastCellY = cy;
    if (!message.empty())
        ShowMessage(message);
    if (!sound.empty() && onSound)
        onSound(sound);
}

// Every press on the farm map, whatever the tool except Bulldoze (tool 15, skipped at the handler's start; live: a
// bulldozed valve is removed for 25), first goes through the original's press-time cell handler:
// * a ditch valve (122-125) switches (122<->123, 124<->125) and the press ends there; the field whose area holds the
//   cell one down and right of the valve gets bit 0 of its flag byte (code offset 4) cleared when 122 opens, set
//   otherwise (the bit is never read again);
// * a gate (188-191) switches (188<->189, 190<->191) and the press goes on;
// * a parked plane's cells (71, 72, 111, 112) open the AIRPLANE window of the machine there, and the press ends;
// * a town building tile (the 3 x 3 blocks of the 40-wide tile sheet starting at 320, 323, 326, 329, 332,
//   335, 338, 352, 355) of the first five blocks opens Market Value, Sell, Bank, Buy or Farm Expert and the press ends;
//   the other blocks and everything else go on to the tool. All checked live in the original (test saves with these
//   tiles placed on the map).
bool GameUi::MapPress(int cx, int cy)
{
    GameState& s = *_ctx.state;
    MapCell& c = s.world.At(cx, cy);
    const int id = c.BaseTile();
    if (id >= 122 && id <= 125)
    {
        c.base = static_cast<uint16_t>((c.base & 0xF800) | (122 + ((id - 122) ^ 1)));
        if (const int fi = _ctx.sim->FieldIndexAt(cx + 1, cy + 1))
        {
            Field f = s.FieldAt(fi);
            f.Set8(4, id == 122 ? (f.Get8(4) & ~1) : (f.Get8(4) | 1));
        }
        return true;
    }
    if (id >= 188 && id <= 191)
        c.base = static_cast<uint16_t>((c.base & 0xF800) | (188 + ((id - 188) ^ 1)));
    else if (id == 71 || id == 72 || id == 111 || id == 112)
    {
        if (const int plane = _ctx.sim->Plane().PlaneAt(cx, cy))
        {
            for (auto& w : _windows)
                w->closed |= w->kind == kWinAirplane;
            auto w = MakeAirplaneWindow(plane);
            // centred across the screen and 16 px below the middle (192, 160 on the original's 640 x 480)
            w->x = std::max(0, (_w - w->w) / 2);
            w->y = std::clamp((_h - w->h - Window::kTitleH) / 2 + 16, kTop, std::max(kTop, _h - w->h - Window::kTitleH));
            _windows.push_back(std::move(w));
            Click();
        }
        return true;
    }
    static const int kOpens[5] = { kWinMarket, kWinSell, kWinBank, kWinBuy, kWinExpert };
    const int k = TownBuildingAt(id);
    if (k < 1 || k > 5)
        return false;
    OpenWindow(kOpens[k - 1]);
    return true;
}

void GameUi::DrawCursor()
{
    if (!_ctx.view || _tool == Tool::None || _tool == Tool::Move || _tool == Tool::Examine)
        return;
    int mx, my;
    MouseState(&mx, &my);
    const int s = _g.Scale();
    if (!OverMap(mx / s, my / s) || WindowAt(mx / s, my / s) || (_popup && PopupBox().Contains(mx / s, my / s)))
        return;
    int cx, cy;
    if (!_ctx.view->CellAt(mx, my, cx, cy))
        return;
    const bool field = _tool == Tool::Plant && _ctx.sim->FieldIndexAt(cx, cy) == 0;
    int fw = field ? 8 : 1, fh = field ? 8 : 1;
    if (_tool == Tool::Place)
        ShopFootprint(*_ctx.state, _toolParam, fw, fh); // a building's whole footprint from the pointer's cell, as the original
    const SDL_Rect r = _ctx.view->CellRect(cx, cy, fw, fh);
    SDL_Renderer* rd = _g.Renderer();
    std::string why;
    // a new field that cannot go there gets a red frame (an engine addition, user finding F29)
    const bool bad = field && !FieldPositionOk(*_ctx.state, *_ctx.sim, cx, cy, why);
    SDL_SetRenderDrawColor(rd, 255, bad ? 64 : 255, bad ? 64 : 255, 255);
    SDL_RenderDrawRect(rd, &r);
    const SDL_Rect inner{ r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
    SDL_SetRenderDrawColor(rd, 0, 0, 0, 255);
    SDL_RenderDrawRect(rd, &inner);
}

// ---------------------------------------------------------------- windows
// A title bar as in the original: tiles from the tile sheet, the round close box (tile 0), the wood grain repeated
// and the notched right end (tiles 1 and 2 on the active window, the dimmed 118 and 119 on the others); the title
// in white straight over the wood.
void GameUi::DrawTitleBar(int x, int y, int w, const std::string& title, bool active, bool closeBox)
{
    for (int tx = x + 16; tx < x + w - 16; tx += 16)
        _g.DrawTile(active ? 1 : 118, tx, y);
    _g.DrawTile(active ? 2 : 119, x + w - 16, y);
    _g.DrawTile(closeBox ? 0 : active ? 75 : 76, x, y); // without a close box: the plain left end
    // centred in whole characters, rounding to the right: x + ceil((w / 8 - length) / 2) * 8
    const std::string shown = Tr(title);
    _g.Text(x + (w / 8 - static_cast<int>(shown.size()) + 1) / 2 * 8, y + 4, shown, kWhite);
}

void GameUi::DrawWindow(Window& w)
{
    const Rect o = w.Outer();
    _g.Fill(o.x + 4, o.y + 4, o.w, o.h, kDarkGrey); // drop shadow
    DrawTitleBar(o.x, o.y, o.w, w.title, !_windows.empty() && _windows.back().get() == &w, w.closeBox);
    _g.DrawPicture(w.background, w.x, w.ContentY());

    // the content draws in picture coordinates: move the viewport (which also clips) onto the picture
    const SDL_Rect vp{ w.x * _g.Scale(), w.ContentY() * _g.Scale(), w.w * _g.Scale(), w.h * _g.Scale() };
    SDL_RenderSetViewport(_g.Renderer(), &vp);
    w.DrawContent(_g, _ctx);
    SDL_RenderSetViewport(_g.Renderer(), nullptr);
}

void GameUi::ShowEvent(const GameEvent& e)
{
    if (e.status)
        ShowMessage(e.text);
    // Options > Messages off: plain messages are dropped, but the ids posted as their picture variant
    // (0x80 + id) still show: 0, 26, 29, 31, 32, 33, 38, 42, 43, 60 (the 15 poster call sites) and the monthly loan and
    // tax boxes 34, 35, 37, 40, 41 (posted with the picture flag, 0xA2, 0xA3, 0xA5, 0xA8, 0xA9); the auction's opening box (36 / 39,
    // posted as its plain kind byte by the auction) does not
    static const std::set<int> kPictureVariant = { 0, 26, 29, 31, 32, 33, 34, 35, 37, 38, 40, 41, 42, 43, 60 };
    if (_messagesOn || e.always || kPictureVariant.count(e.id) || e.id == kTownVoteEvent || (e.id <= -63 && e.id >= -66)) // contest results come regardless
    {
        _events.push_back(e);
        _openMenu = -1; // a box closes an open pull-down (it would stay drawn under the box, and the box takes the clicks)
        _hoverItem = -1;
    }
}

// events.dat: 8 lines of 30 characters per event (title, six text lines, blank); '@' pads, '`' is an apostrophe
static std::vector<std::string> EventText(const Megadata* mega, int id)
{
    static std::vector<std::string> lines;
    if (lines.empty() && mega)
    {
        std::string cur;
        for (uint8_t c : mega->Read("events.dat"))
        {
            if (c == '\n')
            {
                lines.push_back(cur);
                cur.clear();
            }
            else if (c != '\r')
                cur += static_cast<char>(c);
        }
        lines.push_back(cur);
    }
    std::vector<std::string> out;
    for (int k = 0; k < 7; ++k)
    {
        const size_t i = static_cast<size_t>(id) * 8 + k;
        out.push_back(i < lines.size() ? lines[i] : std::string());
    }
    return out;
}

// the game's box waits 1280 timer ticks (50 a second, about 25.6 s) for an answer; then it closes by itself and a
// question counts as answered NO
void GameUi::EventTimeout()
{
    // Save closed without saving after the quit box's YES: the program stays
    if (_quitAfterSave && !IsOpen(kWinSave) && std::none_of(_events.begin(), _events.end(), [](const GameEvent& g) { return g.uiAction == 3; }))
        _quitAfterSave = false;
    if (_events.empty())
    {
        _eventShown = 0;
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if (_eventShown == 0)
        _eventShown = now;
    else if (_events.front().id == kTownVoteEvent)
    {
        // the Town Choice dialog waits 3520 timer ticks, then the town picks a random plot type itself
        if (now - _eventShown > 3520u * 1000u / kTimerHz)
        {
            _events.erase(_events.begin());
            _eventShown = 0;
            _votePressed = -1;
            if (_ctx.sim)
                _ctx.sim->TownVoteTimeout();
        }
    }
    else if (now - _eventShown > 1280u * 1000u / kTimerHz)
    {
        const GameEvent ev = _events.front();
        _events.erase(_events.begin());
        _eventShown = 0;
        if (!ev.faces.empty())
            UiAnswer(ev, ev.timeoutButton); // the quit box left alone leaves the program, as the original does
        else if (ev.question && _ctx.sim)
            _ctx.sim->Answer(ev.id, false);
    }
}

// the year end opens the Evaluation window unless it is open already; a window opened this way closes by itself
// after 1025 timer ticks. It waits for the year end's boxes (the Town Choice vote comes first).
void GameUi::YearEvaluation()
{
    const uint32_t now = SDL_GetTicks();
    if (_ctx.sim && !_yearEvalDue)
        _yearEvalDue = _ctx.sim->TakeYearEvaluation();
    if (_yearEvalDue && _events.empty())
    {
        _yearEvalDue = false;
        if (std::none_of(_windows.begin(), _windows.end(), [](const auto& w) { return w->kind == kWinEvaluation; }))
        {
            OpenWindow(kWinEvaluation);
            if (!_windows.empty() && _windows.back()->kind == kWinEvaluation)
                _windows.back()->autoClose = std::max<uint32_t>(1, now + static_cast<uint32_t>(1025 * 1000 / kTimerHz));
        }
    }
    for (auto& w : _windows)
        if (w->autoClose && static_cast<int32_t>(now - w->autoClose) >= 0)
            w->closed = true;
}

// the interface's own boxes, checked on the original: the farmer picture (alltiles 74) for quit and new game, the
// thumbs-up farmer (75) after every save; the button faces are buttons.til 10 YES, 3 CANCEL, 11 NO, 0 OK
void GameUi::AskQuit()
{
    GameEvent e{ -80, 74, Tr("Do you want to save your farm before you leave SimFarm?") };
    e.faces = { 10, 3, 11 };
    e.uiAction = 1;
    e.timeoutButton = 2; // no answer: the program ends without saving
    e.escButton = 1;
    _events.push_back(e);
}

void GameUi::AskNewGame()
{
    GameEvent e{ -81, 74, "" };
    e.textId = 45; // the player's events.dat: the quit question (asks whether to leave the game)
    e.faces = { 3, 10 };
    e.uiAction = 2;
    e.timeoutButton = 0;
    e.escButton = 0;
    _events.push_back(e);
}

void GameUi::Saved(const std::string& name)
{
    GameEvent e{ -82, 75, TrF("Your game has been saved as {0}.", name) };
    e.faces = { 0 };
    e.uiAction = 3;
    e.escButton = 0;
    _events.push_back(e);
}

void GameUi::UiAnswer(const GameEvent& e, int button)
{
    switch (e.uiAction)
    {
    case 1: // quit: YES saves first (Save dialog), CANCEL stays, NO leaves
        if (button == 0)
        {
            _quitAfterSave = true;
            if (!IsOpen(kWinSave))
                OpenWindow(kWinSave);
        }
        else if (button == 2 && onCommand)
            onCommand("quit");
        break;
    case 2: // new game: YES goes to the region screen
        if (button == 1 && onCommand)
            onCommand("newgame");
        break;
    case 3: // saved: after the quit box's YES the program ends now
        if (_quitAfterSave && onCommand)
        {
            _quitAfterSave = false;
            onCommand("quit");
        }
        break;
    default:
        break;
    }
}

bool GameUi::UiCommand(const std::string& cmd)
{
    if (cmd.rfind("saved:", 0) == 0)
    {
        Saved(cmd.substr(6));
        return true;
    }
    return false;
}

void GameUi::DrawEventBox()
{
    if (_events.empty())
        return;
    const GameEvent& e = _events.front();
    if (e.id == kTownVoteEvent)
    {
        // Town Choice: the dialog picture at (96, 96) of the 640x480 screen, nothing drawn over it; a held button
        // shows a sunken frame (two grey lines top and left, two white lines bottom and right)
        const Rect v = VoteBox();
        _g.DrawPicture("twnchoic.bmp", v.x, v.y);
        if (_votePressed >= 0)
        {
            const int x0 = v.x + 16 + 88 * _votePressed, y0 = v.y + 32, x1 = x0 + 72, y1 = y0 + 72;
            for (int i = 0; i < 2; ++i)
            {
                _g.Line(x0 + 1, y0 + 1 + i, x1 - 3 - i, y0 + 1 + i, kGrey);
                _g.Line(x0 + 1 + i, y0 + 1, x0 + 1 + i, y1 - 2, kGrey);
                _g.Line(x0 + 2 + i, y1 - 1 - i, x1 - 1, y1 - 1 - i, kWhite);
                _g.Line(x1 - 1 - i, y0 + 2 + i, x1 - 1 - i, y1 - 1, kWhite);
            }
            // the top right corner as the original leaves it
            _g.Fill(x1 - 2, y0 + 1, 1, 1, kWhite);
            _g.Fill(x1 - 3, y0 + 2, 1, 1, kWhite);
            _g.Fill(x1 - 2, y0 + 2, 1, 1, kGrey);
        }
        return;
    }
    // the message box: popup.bmp, the picture at (72, 24), the text box (16, 96, 224 wide) holding the event's
    // seven lines as one text (title first, centred by its own leading blanks), button faces with their shadow
    const Rect b = EventBox();
    _g.DrawPicture("popup.bmp", b.x, b.y);
    // picture -1 (the richer-home box): picture 0x4B, the text 8 px further left and 184 wide, and a dancing cow
    // beside the picture: one of the three cowman%d.spr sets, picked by G2's shift register (& 3, 0 -> 1)
    // when the box opens, steps one frame of its dance every 9 timer ticks
    const bool dance = e.picture == -1;
    _g.DrawBank("alltiles.btl", dance ? 0x4B : e.picture, b.x + 72, b.y + 24);
    const int textId = e.textId >= 0 ? e.textId : e.id;
    std::string text = textId < 0 ? e.text : std::string(); // the engine's own boxes carry their text
    if (const std::string tr = textId >= 0 ? TrId("event:" + std::to_string(textId), "") : std::string(); !tr.empty())
    {
        // a translation ("event:<id>"): '~' starts a new line; a first line ended by '~' is a title, centred like
        // the original's (the box holds lines of (width - 8) / 8 characters, '`' prints as a blank)
        text = tr;
        const size_t cut = tr.find('~');
        const int chars = ((dance ? 184 : 224) - 8) / 8;
        if (cut != std::string::npos && static_cast<int>(cut) < chars)
            text = std::string(static_cast<size_t>((chars - static_cast<int>(cut)) / 2), '`') + tr;
    }
    else if (textId >= 0)
        for (const std::string& l : EventText(_ctx.mega, textId))
            text += l + "\n";
    _g.Wrap(b.x + (dance ? 8 : 16), b.y + 96, dance ? 184 : 224, text, kBlack);
    if (dance)
    {
        static const uint8_t kDance[4][12] = { {}, { 0, 1, 2, 2, 1, 0, 3, 4, 4, 3, 1, 0 }, { 0, 1, 1, 2, 3, 3, 0 },
                                               { 0, 1, 2, 3, 3, 3, 2, 1, 0, 0 } };
        static const int kSteps[4] = { 1, 12, 7, 10 };
        if (_danceSet == 0)
        {
            _danceSet = static_cast<int>(SDL_GetPerformanceCounter() >> 4) & 3; // a free-running pick (G2 is not shared with the interface)
            if (_danceSet == 0)
                _danceSet = 1;
        }
        const uint32_t ticks = _eventShown ? static_cast<uint32_t>((SDL_GetTicks() - _eventShown) * kTimerHz / 1000.0) : 0;
        const int step = static_cast<int>(ticks / 9 % static_cast<uint32_t>(kSteps[_danceSet]));
        const int s = _g.Scale();
        _g.DrawSprite("cowman" + std::to_string(_danceSet) + ".spr", kDance[_danceSet][step],
                      SDL_Rect{ (b.x + 184) * s, (b.y + 32) * s, 64 * s, 88 * s });
    }
    else
        _danceSet = 0;
    if (!e.faces.empty())
    {
        const int n = static_cast<int>(e.faces.size());
        for (int k = 0; k < n; ++k)
        {
            const Rect r = FaceButton(n, k);
            _g.DrawButton(e.faces[k], r.x, r.y);
        }
        return;
    }
    for (int k = 0; k < (e.question ? 2 : 1); ++k)
    {
        const Rect r = EventButton(e.question, k);
        _g.DrawButton(!e.question ? e.okFace : k ? e.noFace : 10, r.x, r.y); // OK / RATS, YES, NO / CANCEL
    }
}

void GameUi::DrawHazards()
{
    if (!_ctx.sim || !_ctx.view)
        return;
    Disasters& d = _ctx.sim->Hazards();
    if (d.Active(Disasters::kTornado))
    {
        const SDL_Rect c = _ctx.view->SmoothCellRect(0, d.TornadoX() - 1, d.TornadoY() - 2, 3, 3);
        _g.DrawSprite("tornado.spr", d.Frame(), c);
    }
    if (d.Active(Disasters::kLocusts))
    {
        const SDL_Rect c = _ctx.view->SmoothCellRect(1, d.SwarmX(), d.SwarmY(), 4, 2);
        _g.DrawSprite("swarm.spr", d.Frame(), SDL_Rect{ c.x, c.y, c.w, c.h * 5 / 4 });
    }
}

GameUi::~GameUi()
{
    for (SDL_Cursor* c : _cursors)
        if (c)
            SDL_FreeCursor(c);
    if (_arrow)
        SDL_FreeCursor(_arrow);
}

void GameUi::UpdateCursor()
{
    const int scale = _g.Scale();
    if (_cursorScale != scale && _g.Mega())
    {
        // cursors.til: 6 cursors, each a group of four mask bitmaps then four image bitmaps (16x16, 1 bpp);
        // a pixel shows where the mask bit is set, white where the image bit is set, else black. The Windows
        // edition's cursors bring their own hot spots (after the bitmaps)
        const std::vector<uint8_t> d = _g.Mega()->Read("cursors.til");
        int hot[6][2] = { { 8, 8 }, { 8, 8 }, { 5, 5 }, { 2, 7 }, { 8, 8 }, { 4, 0 } };
        if (d.size() >= 1536 + 12)
            for (int c = 0; c < 6; ++c)
            {
                hot[c][0] = d[1536 + 2 * c];
                hot[c][1] = d[1536 + 2 * c + 1];
            }
        for (int c = 0; c < 6 && d.size() >= 1536; ++c)
        {
            if (_cursors[c])
                SDL_FreeCursor(_cursors[c]);
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 16 * scale, 16 * scale, 32, SDL_PIXELFORMAT_ARGB8888);
            if (!surf)
                continue;
            auto* px = static_cast<uint32_t*>(surf->pixels);
            const uint8_t* m = &d[static_cast<size_t>(2 * c) * 128];
            const uint8_t* im = &d[static_cast<size_t>(2 * c + 1) * 128];
            for (int y = 0; y < 16 * scale; ++y)
                for (int x = 0; x < 16 * scale; ++x)
                {
                    const int sx = x / scale, sy = y / scale;
                    const int mw = (m[sy * 2] << 8) | m[sy * 2 + 1], iw = (im[sy * 2] << 8) | im[sy * 2 + 1];
                    const int bit = 0x8000 >> sx;
                    px[y * (surf->pitch / 4) + x] = !(mw & bit) ? 0 : (iw & bit) ? 0xFFFFFFFFu : 0xFF000000u;
                }
            _cursors[c] = SDL_CreateColorCursor(surf, hot[c][0] * scale, hot[c][1] * scale);
            SDL_FreeSurface(surf);
        }
        if (!_arrow)
            _arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        _cursorScale = scale;
        _shownCursor = -2;
    }
    int mx, my;
    MouseState(&mx, &my);
    const bool overMap = _events.empty() && OverMap(mx / scale, my / scale) && !WindowAt(mx / scale, my / scale) && !(_popup && PopupBox().Contains(mx / scale, my / scale));
    int want = -1;
    if (overMap)
        switch (_tool)
        {
        case Tool::Move: want = _moveKind ? 1 : 0; break; // empty hand: cursor 0, holding: 1 (seen live, user finding F31)
        case Tool::Examine: want = 2; break;
        case Tool::Place: want = 4; break;
        case Tool::Spray: want = 5; break;
        default: break;
        }
    if (want != _shownCursor)
    {
        SDL_SetCursor(want >= 0 && _cursors[want] ? _cursors[want] : _arrow);
        _shownCursor = want;
    }
}

void GameUi::AutoScroll()
{
    // AutoScroll option: the farm view moves 2 cells at a time while the pointer is within 4 pixels of
    // the left or top edge (the original: the outermost pixel) or within the last 16 pixels of the right or bottom
    // edge (the original's zones), in interface pixels. Under DOS the pointer cannot leave the screen, so pushing it against
    // an edge keeps it there; a window lets it leave, so a pointer that left the focused window through an edge
    // counts as held on that edge until it comes back. The original steps once per pass of its main loop, so its
    // pace follows the machine; the engine takes a step every 50 ms (40 cells a second) whatever the frame rate
    if (!_ctx.state || !_ctx.view || !_ctx.state->scalars["opt_autoscroll"] || _openMenu >= 0 || _drag || !_events.empty())
    {
        _autoNext = 0;
        return;
    }
    int dx = 0, dy = 0;
    if (SDL_GetMouseFocus())
    {
        int mx, my;
        MouseState(&mx, &my);
        const int s = _g.Scale();
        const int ix = mx / s, iy = my / s;
        // the left and top zones are 4 pixels wide here, not the original's one (user finding F47: in a window the
        // outermost pixel is hard to hit, so the left side hardly scrolled): they cover only the palette's border
        // (its icons start at x 4) and the empty rows of the menu bar above its names (text from y 4)
        if (ix < 4)
            dx = -2;
        if (ix > _w - 17)
            dx = 2;
        if (iy < 4)
            dy = -2;
        if (iy > _h - 17)
            dy = 2;
    }
    else
    {
        SDL_Window* win = SDL_GetKeyboardFocus();
        if (!win || !(SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS))
            _parkedX = _parkedY = 0;
        dx = 2 * _parkedX;
        dy = 2 * _parkedY;
    }
    if (dx == 0 && dy == 0)
    {
        _autoNext = 0;
        return;
    }
    const Uint32 now = SDL_GetTicks();
    if (_autoNext != 0 && static_cast<int32_t>(now - _autoNext) < 0)
        return;
    _autoNext = now + 50;
    float x0, y0, w, h;
    _ctx.view->VisibleCells(x0, y0, w, h);
    _ctx.view->SetTopLeft(static_cast<int>(x0) + dx, static_cast<int>(y0) + dy);
}

void GameUi::DrawPuffs()
{
    // 7 frames, one every 16 timer ticks (0.32 s), drawn over the cell
    if (!_ctx.sim || !_ctx.view)
        return;
    int x, y;
    while (_ctx.sim->TakePuff(x, y))
        if (_puffs.size() < 64)
            _puffs.push_back({ x, y, SDL_GetTicks() });
    const Uint32 now = SDL_GetTicks();
    _puffs.erase(std::remove_if(_puffs.begin(), _puffs.end(), [&](const PuffAnim& p) { return now - p.start >= 7 * 320; }), _puffs.end());
    for (const PuffAnim& p : _puffs)
    {
        const int frame = static_cast<int>((now - p.start) / 320);
        const SDL_Rect r = _ctx.view->CellRect(p.x, p.y);
        _g.DrawTileAt(0x298 + frame, r);
    }
}

void GameUi::DrawPlane()
{
    if (!_ctx.sim || !_ctx.view || !_ctx.sim->Plane().Flying())
        return;
    Flight& f = _ctx.sim->Plane();
    _ctx.view->CenterOn(f.ViewX(), f.ViewY()); // the view follows the plane (off-centre at the map edges)
    const SDL_Rect cell = _ctx.view->CellRect(static_cast<int>(f.X()) - 1, static_cast<int>(f.Y()) - 1, 2, 2);
    const int frame = f.Heading() & 7; // frame n faces heading n (0 north, clockwise), as the original draws it (F42)
    const float px = cell.w / 32.0f;
    _g.DrawSprite("apshadow.spr", frame, cell);
    const SDL_Rect up{ cell.x, cell.y - static_cast<int>(2 * f.Altitude() * px), cell.w, cell.h };
    _g.DrawSprite("airplane.spr", frame, up);
    if (f.Spraying())
    {
        SDL_SetRenderDrawBlendMode(_g.Renderer(), SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(_g.Renderer(), 230, 230, 230, 110);
        const SDL_Rect cloud{ cell.x + cell.w / 4, cell.y + cell.h / 4, cell.w / 2, cell.h / 2 };
        SDL_RenderFillRect(_g.Renderer(), &cloud);
        SDL_SetRenderDrawBlendMode(_g.Renderer(), SDL_BLENDMODE_NONE);
    }
    DrawFlightPanel();
}

// the flight panel (an engine addition, user finding F42): the plane's gauges and a button for every flight key,
// in the map's bottom-left corner
Rect GameUi::FlightPanel() const
{
    const Rect m = MapRect();
    return { m.x + 6, m.y + m.h - 74, 210, 68 };
}

Rect GameUi::FlightButton(int k) const
{
    const Rect p = FlightPanel();
    return { p.x + 4 + 104 * k, p.y + 34, 98, 30 };
}

// only SPRAY and LAND (user: the turns and the height stay on the keys)
static const char* const kFlightButtons[2] = { N_("SPRAY"), N_("LAND") };
static const int kFlightKeys[2] = { 'S', 'E' };

void GameUi::DrawFlightPanel()
{
    const Flight& f = _ctx.sim->Plane();
    const Rect p = FlightPanel();
    _g.Frame(p.x - 1, p.y - 1, p.w + 2, p.h + 2, kBlack);
    _g.Fill(p.x, p.y, p.w, p.h, kLightGrey);
    _g.Bevel(p.x, p.y, p.w, p.h, true);
    const int plane = f.PlaneIndex();
    const int values[3] = { Flight::Fuel(*_ctx.state, plane), Flight::Load(*_ctx.state, plane), Flight::Wear(*_ctx.state, plane) };
    static const char* const kGauge[3] = { N_("FUEL"), N_("SPRAY"), N_("DAMAGE") };
    for (int i = 0; i < 3; ++i)
    {
        const int y = p.y + 5 + 9 * i;
        _g.SmallText(p.x + 6, y, Tr(kGauge[i]), kBlack);
        const int bx = p.x + 50, bw = 128;
        _g.Frame(bx, y - 1, bw + 2, 7, kBlack);
        _g.Fill(bx + 1, y, bw, 5, kWhite);
        const int v = std::clamp(values[i], 0, 255);
        // fuel and spray run low in red; damage turns red near the 250 at which the plane comes down
        const bool warn = i < 2 ? v < 32 : v > 200;
        _g.Fill(bx + 1, y, v * bw / 255, 5, warn ? kRed : (i == 2 ? kDarkGrey : kNavy));
        _g.SmallText(bx + bw + 6, y, std::to_string(v * 100 / 255) + "%", kBlack);
    }
    for (int k = 0; k < 2; ++k)
    {
        const Rect b = FlightButton(k);
        const bool down = (k == 0 && f.Spraying()) || (_flightPressed == k && SDL_GetTicks() - _flightPressedAt < 150);
        _g.Frame(b.x - 1, b.y - 1, b.w + 2, b.h + 2, kBlack);
        _g.Fill(b.x, b.y, b.w, b.h, down ? kGrey : kLightGrey);
        _g.Bevel(b.x, b.y, b.w, b.h, !down);
        const std::string t = Tr(kFlightButtons[k]);
        _g.SmallText(b.x + (b.w - Gfx::SmallTextWidth(t)) / 2, b.y + 13, t, kBlack);
    }
}

void GameUi::DrawZoomButtons()
{
    for (int k = 0; k < 3; ++k)
    {
        const Rect z = ZoomButton(k);
        _g.Frame(z.x - 1, z.y - 1, z.w + 2, z.h + 2, kBlack);
        _g.Fill(z.x, z.y, z.w, z.h, kLightGrey);
        _g.Bevel(z.x, z.y, z.w, z.h, true);
        if (k == 2)
        {
            // edge scrolling (Options > AutoScroll): a white check box, ticked while it is on
            _g.Fill(z.x + 3, z.y + 3, 10, 9, kWhite);
            _g.Frame(z.x + 3, z.y + 3, 10, 9, kBlack);
            if (_ctx.state && _ctx.state->scalars["opt_autoscroll"])
            {
                static const int tick[7][2] = { { 5, 7 }, { 6, 8 }, { 7, 9 }, { 8, 8 }, { 9, 7 }, { 10, 6 }, { 11, 5 } };
                for (const auto& t : tick)
                    _g.Fill(z.x + t[0], z.y + t[1] - 1, 1, 2, kBlack);
            }
            continue;
        }
        _g.Fill(z.x + 4, z.y + 6, 8, 2, kBlack); // '-'
        if (k == 0)
            _g.Fill(z.x + 7, z.y + 3, 2, 8, kBlack); // '+'
    }
}

void GameUi::Draw()
{
    if (_ctx.sim)
        _ctx.sim->SetMessagesOn(_messagesOn);
    EventTimeout();
    YearEvaluation();
    UpdateCursor();
    AutoScroll();
    ArrowRepeat();
    DrawHazards();
    DrawPuffs();
    DrawPlane();
    for (const auto& w : _windows)
        if (w->closed && (w->kind == kWinSave || w->kind == kWinLoad) && _resumeNormal)
        {
            _ctx.sim->SetSpeed(Speed::Normal);
            _resumeNormal = false;
        }
    for (const auto& w : _windows)
        if (w->closed && w->kind == kWinLoad && loadFromStart)
        {
            // the LOAD GAME window opened by the region screen's LOAD A SAVED GAME, closed without loading: back to
            // the region screen (seen live in the original)
            loadFromStart = false;
            if (onCommand)
                onCommand("newgame");
        }
    _windows.erase(std::remove_if(_windows.begin(), _windows.end(), [](const auto& w) { return w->closed; }), _windows.end());
    DrawCursor();
    DrawEditFrame();
    DrawZoomButtons();
    DrawStatus();
    DrawPalette();
    for (auto& w : _windows)
        DrawWindow(*w);
    DrawHistory();
    DrawToolbar();
    DrawMenus();
    if (_exHold && _exShown)
        DrawExamineCard(_g, _ctx.mega, _exCard, (_exQuad & 1) ? _w / 2 : 32, (_exQuad & 2) ? _h / 2 : 32);
    DrawEventBox();
}

// the screen quarter for the Examine card: 0 pointer bottom right, 1 bottom left, 2 top right, 3 top left;
// -1 on the middle lines, where the original shows no card
int GameUi::ExamineQuadrant(int x, int y) const
{
    const int hx = _w / 2, hy = _h / 2;
    if (x == hx || y == hy)
        return -1;
    return (x < hx ? 1 : 0) | (y < hy ? 2 : 0);
}

void GameUi::OpenFieldSchedule(int field)
{
    for (auto& w : _windows)
        w->closed |= w->kind == kWinField;
    auto w = MakeFieldWindow(field);
    w->x = std::clamp(160, 0, std::max(0, _w - w->w));
    w->y = std::clamp(80, kTop, std::max(kTop, _h - w->h - Window::kTitleH));
    _windows.push_back(std::move(w));
    Click();
}

// ---------------------------------------------------------------- input
bool GameUi::HandleEvent(const SDL_Event& e)
{
    const int s = _g.Scale();
    // the radio works at any time, also over an event box or in the air
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && e.button.y / s < kMenuH &&
        RadioClick(e.button.x / s, e.button.y / s))
    {
        _openMenu = -1;
        return true;
    }
    // where the pointer leaves the window (AutoScroll's held edge, see AutoScroll)
    if (e.type == SDL_MOUSEMOTION)
    {
        _lastMx = e.motion.x / s;
        _lastMy = e.motion.y / s;
    }
    else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_LEAVE)
    {
        // the edge it crossed: the nearest one to its last position, and a corner when both are close
        _parkedX = _parkedY = 0;
        if (_lastMx >= 0)
        {
            const int dl = _lastMx, dr = _w - 1 - _lastMx, dt = _lastMy, db = _h - 1 - _lastMy;
            const int nearest = std::min(std::min(dl, dr), std::min(dt, db));
            const int near = std::max(nearest, 24);
            if (dl <= near && dl <= dr) _parkedX = -1;
            else if (dr <= near) _parkedX = 1;
            if (dt <= near && dt <= db) _parkedY = -1;
            else if (db <= near) _parkedY = 1;
        }
    }
    else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_ENTER || e.window.event == SDL_WINDOWEVENT_FOCUS_LOST))
        _parkedX = _parkedY = 0;
    if (_ctx.sim && _ctx.sim->Plane().Flying() && _events.empty())
    {
        // flight keys: the keypad keys of the original and their ordinary equivalents
        if (e.type == SDL_KEYDOWN)
        {
            Flight& f = _ctx.sim->Plane();
            switch (e.key.keysym.sym)
            {
            // the original's keypad keys, and (engine additions, F42) the keys every keyboard has: arrows, Enter,
            // and the letters A / D, W / S, F (spray). Space and the number keys keep their usual meaning (pause,
            // game speed) and go on below
            case SDLK_SPACE: case SDLK_0: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: break;
            case SDLK_LEFT: case SDLK_KP_4: case SDLK_a: f.Key('L'); break;
            case SDLK_RIGHT: case SDLK_KP_6: case SDLK_d: f.Key('R'); break;
            case SDLK_HOME: case SDLK_KP_7: case SDLK_f: f.Key('S'); break;
            case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS: f.Key('+'); break;
            case SDLK_MINUS: case SDLK_KP_MINUS: f.Key('-'); break;
            case SDLK_PAGEUP: case SDLK_KP_9: case SDLK_UP: case SDLK_w: f.Key('U'); break;
            case SDLK_PAGEDOWN: case SDLK_KP_3: case SDLK_DOWN: case SDLK_s: f.Key('D'); break;
            case SDLK_END: case SDLK_KP_1: case SDLK_KP_5: case SDLK_ESCAPE: case SDLK_RETURN: case SDLK_KP_ENTER: f.Key('E'); break;
            default: break;
            }
            const SDL_Keycode k = e.key.keysym.sym;
            if (k != SDLK_SPACE && !(k >= SDLK_0 && k <= SDLK_4))
                return true;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            const int x = e.button.x / s, y = e.button.y / s;
            for (int k = 0; k < 2; ++k)
                if (FlightButton(k).Contains(x, y))
                {
                    Click();
                    _ctx.sim->Plane().Key(kFlightKeys[k]);
                    _flightPressed = k;
                    _flightPressedAt = SDL_GetTicks();
                    return true;
                }
        }
        // the menu bar stays usable (only the Speed menu's entries work in the air, see ItemEnabled)
        const bool menu = _openMenu >= 0 || (e.type == SDL_MOUSEBUTTONDOWN && MenuAt(e.button.x / s, e.button.y / s) >= 0);
        if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL) && !menu)
            return true;
    }
    if (!_events.empty())
    {
        if (_events.front().id == kTownVoteEvent)
        {
            // the four picture buttons: residential, commercial, airport, recreational; the vote is cast when the
            // button is let go
            const Rect v = VoteBox();
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                for (int k = 0; k < 4; ++k)
                    if (Rect{ v.x + 16 + 88 * k, v.y + 32, 73, 73 }.Contains(e.button.x / s, e.button.y / s))
                    {
                        Click();
                        _votePressed = k;
                    }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && _votePressed >= 0)
            {
                const int k = _votePressed;
                _votePressed = -1;
                // the box stays open when the choice is refused (another event ground is not possible)
                if (!_ctx.sim || _ctx.sim->TownVote(k))
                {
                    _events.erase(_events.begin());
                    _eventShown = 0;
                }
            }
            return e.type != SDL_QUIT && e.type != SDL_WINDOWEVENT;
        }
        // modal: only the OK button (or Enter / Space / Esc) does anything
        const GameEvent ev = _events.front();
        if (!ev.faces.empty())
        {
            const int n = static_cast<int>(ev.faces.size());
            int pressed = -1;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                for (int k = 0; k < n; ++k)
                    if (FaceButton(n, k).Contains(e.button.x / s, e.button.y / s))
                        pressed = k;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                pressed = ev.escButton;
            if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                pressed = 0; // Enter presses the first button (checked: New Game's CANCEL, Quit's YES)
            if (pressed >= 0)
            {
                _events.erase(_events.begin());
                _eventShown = 0;
                Click();
                UiAnswer(ev, pressed);
            }
            return e.type != SDL_QUIT && e.type != SDL_WINDOWEVENT;
        }
        const bool click = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
        const bool yes = (click && EventButton(ev.question, 0).Contains(e.button.x / s, e.button.y / s)) ||
                         (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER ||
                                                    (!ev.question && (e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_ESCAPE))));
        const bool no = ev.question && ((click && EventButton(true, 1).Contains(e.button.x / s, e.button.y / s)) ||
                                        (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE));
        if (yes || no)
        {
            _events.erase(_events.begin());
            _eventShown = 0;
            Click();
            if (ev.question && _ctx.sim)
                _ctx.sim->Answer(ev.id, yes);
        }
        return e.type != SDL_QUIT && e.type != SDL_WINDOWEVENT;
    }
    switch (e.type)
    {
    case SDL_MOUSEBUTTONDOWN:
    {
        if (e.button.button == SDL_BUTTON_RIGHT && !_popup && _openMenu < 0 && OverMap(e.button.x / s, e.button.y / s) &&
            !WindowAt(e.button.x / s, e.button.y / s))
        {
            // a right press on the farm: a click (released without dragging) is handled at the release; a drag scrolls
            _rightX = e.button.x;
            _rightY = e.button.y;
            _rightDown = true;
            return false;
        }
        if (e.button.button == SDL_BUTTON_RIGHT && (_tool != Tool::None || _popup || _openMenu >= 0))
        {
            // right click: drop the chosen tool (and any open list or menu), back to the plain pointer
            _tool = Tool::None;
            _popup = 0;
            _openMenu = -1;
            _moveKind = 0;
            _painting = false;
            return true;
        }
        if (e.button.button != SDL_BUTTON_LEFT)
            return _openMenu >= 0 || WindowAt(e.button.x / s, e.button.y / s) != nullptr || !OverMap(e.button.x / s, e.button.y / s);
        const int x = e.button.x / s, y = e.button.y / s;
        if (_openMenu >= 0)
        {
            const int item = ItemAt(x, y);
            const int m = MenuAt(x, y);
            if (item >= 0)
                Execute(_menus[_openMenu].items[item].command);
            _openMenu = m >= 0 && m != _openMenu ? m : -1;
            _hoverItem = -1;
            _menuOpenedAt = SDL_GetTicks();
            return true;
        }
        if (const int m = MenuAt(x, y); m >= 0)
        {
            _openMenu = m;
            _hoverItem = -1;
            _menuOpenedAt = SDL_GetTicks();
            return true;
        }
        if (_historyOpen || HistoryButton().Contains(x, y))
        {
            // the message list: the button opens / closes it; any other click closes it (a click inside is used up)
            const bool inside = HistoryButton().Contains(x, y) || HistoryBox().Contains(x, y);
            if (!_historyOpen)
            {
                _historyOpen = true;
                _historyTop = 0;
                Click();
                return true;
            }
            _historyOpen = false;
            if (inside)
                return true;
        }
        if (Window* w = WindowAt(x, y))
        {
            Raise(w);
            const int lx = x - w->x, ly = y - w->ContentY();
            if (ly < 0)
            {
                if (w->closeBox && lx >= 2 && lx < 13 && ly >= -12 && ly < -3)
                    w->closed = true;
                else
                {
                    _drag = w;
                    _dragDX = x - w->x;
                    _dragDY = y - w->y;
                }
            }
            else if (w->CloseButton().Contains(lx, ly))
            {
                w->closed = true;
                Click();
            }
            else
            {
                w->Click(lx, ly, _ctx);
                if (_ctx.placeRecord >= 0)
                {
                    _tool = Tool::Place;
                    _toolParam = _ctx.placeRecord;
                    _ctx.placeRecord = -1;
                }
                if (!_ctx.message.empty())
                    ShowMessage(_ctx.message);
                if (!_ctx.command.empty() && onCommand)
                {
                    const std::string c = _ctx.command;
                    _ctx.command.clear();
                    if (!UiCommand(c))
                        onCommand(c);
                    return true;
                }
                if (!_ctx.sound.empty() && onSound)
                    onSound(_ctx.sound);
                _ctx.message.clear();
                _ctx.sound.clear();
            }
            return true;
        }
        if (PaletteClick(x, y))
        {
            Click();
            return true;
        }
        if (ZoomButton(2).Contains(x, y) && _ctx.state)
        {
            auto& v = _ctx.state->scalars["opt_autoscroll"];
            v = v ? 0 : 1;
            ShowMessage(v ? Tr("Edge scrolling on: the map moves when the pointer is at a side.") : Tr("Edge scrolling off."));
            Click();
            return true;
        }
        for (int k = 0; k < 2; ++k)
            if (ZoomButton(k).Contains(x, y) && _ctx.view)
            {
                const SDL_Rect m = MapPixels();
                _ctx.view->ZoomStep(k == 0 ? 1 : -1, m.w / 2, m.h / 2);
                Click();
                return true;
            }
        if (FrameClick(x, y))
            return true;
        if (OverMap(x, y) && _tool != Tool::None)
        {
            _painting = true;
            UseTool(e.button.x, e.button.y, true);
            return true;
        }
        if (OverMap(x, y) && _ctx.view && _ctx.state && _ctx.sim)
        {
            // no tool: the press still reaches the original's cell handler (valves, gates, planes, town buildings)
            int cx, cy;
            if (_ctx.view->CellAt(e.button.x, e.button.y, cx, cy))
                MapPress(cx, cy);
            return true;
        }
        if (y >= kToolbarY && y < kToolbarY + kToolbarH)
        {
            // the Funds box opens the engine's money log
            if (x >= _w - kToolbarRightPart + 16 && y >= kToolbarY + 16)
            {
                Click();
                OpenWindow(kWinFunds);
                return true;
            }
            if (x < 8 * 32)
            {
                const int b = ToolbarButton(x / 32);
                if (b < 0)
                    return true;
                const int kind = ToolbarWindow(b);
                Execute("win:" + std::to_string(kind));
                if (!ItemEnabled("win:" + std::to_string(kind)))
                    ShowMessage(Tr("This window is not available yet."));
            }
            return true;
        }
        return !OverMap(x, y);
    }
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_RIGHT && _rightDown)
        {
            _rightDown = false;
            if (std::abs(e.button.x - _rightX) > 3 * s || std::abs(e.button.y - _rightY) > 3 * s)
                return false; // that was a scroll drag
            // the original's right press (farm window, event 7): the press-time cell handler runs (valves, gates, the
            // plane, town buildings), then a building there switches between its outside and its inside view.
            // Elsewhere the engine's right click drops the chosen tool.
            int cx, cy;
            bool used = false;
            if (_ctx.view && _ctx.state && _ctx.sim && _ctx.view->CellAt(_rightX, _rightY, cx, cy))
            {
                const int before = _ctx.state->world.At(cx, cy).base;
                used = MapPress(cx, cy) || _ctx.state->world.At(cx, cy).base != before;
                used = _ctx.sim->ToggleInside(cx, cy) != 0 || used;
            }
            if (!used)
            {
                _tool = Tool::None;
                _moveKind = 0;
                _painting = false;
            }
            return false; // the map view also sees the release (ends its drag)
        }
        if (e.button.button != SDL_BUTTON_LEFT)
            return false;
        if (_drag)
        {
            _drag = nullptr;
            return true;
        }
        if (_painting)
        {
            _painting = false;
            if (_exHold)
            {
                _exHold = _exShown = false;
                if (_exField)
                    OpenFieldSchedule(_exField);
                _exField = 0;
            }
            return true;
        }
        if (_openMenu >= 0 && SDL_GetTicks() - _menuOpenedAt > 250)
        {
            // press, hold and release on an item (the original's menu style)
            const int item = ItemAt(e.button.x / s, e.button.y / s);
            if (item >= 0)
            {
                Execute(_menus[_openMenu].items[item].command);
                _openMenu = -1;
            }
            return true;
        }
        return _openMenu >= 0;
    case SDL_MOUSEMOTION:
    {
        const int x = e.motion.x / s, y = e.motion.y / s;
        if (_ctx.sim && _ctx.view)
        {
            // animals shy away from the map cell under the pointer (as the original's animal step)
            int cx = -1, cy = -1;
            if (!_ctx.view->CellAt(e.motion.x, e.motion.y, cx, cy))
                cx = cy = -1;
            _ctx.sim->Herds().SetPointer(cx, cy);
        }
        if (_painting)
        {
            UseTool(e.motion.x, e.motion.y, false);
            return true;
        }
        if (_drag)
        {
            _drag->x = std::clamp(x - _dragDX, -_drag->w + 40, _w - 40);
            _drag->y = std::clamp(y - _dragDY, kTop, _h - Window::kTitleH);
            return true;
        }
        if (_openMenu >= 0)
        {
            if (const int m = MenuAt(x, y); m >= 0)
                _openMenu = m;
            _hoverItem = ItemAt(x, y);
            return true;
        }
        return false;
    }
    case SDL_MOUSEWHEEL:
    {
        int mx, my;
        MouseState(&mx, &my);
        if (_historyOpen && HistoryBox().Contains(mx / s, my / s))
        {
            _historyTop = std::max(0, _historyTop - 3 * e.wheel.y);
            return true;
        }
        if (_openMenu < 0)
            if (Window* w = WindowAt(mx / s, my / s))
                w->Wheel(e.wheel.y);
        return _openMenu >= 0 || WindowAt(mx / s, my / s) != nullptr || !OverMap(mx / s, my / s);
    }
    case SDL_KEYDOWN:
        if (_historyOpen)
        {
            const SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_ESCAPE) { _historyOpen = false; return true; }
            if (k == SDLK_UP) { _historyTop = std::max(0, _historyTop - 1); return true; }
            if (k == SDLK_DOWN) { _historyTop++; return true; }
            if (k == SDLK_PAGEUP) { _historyTop = std::max(0, _historyTop - kHistoryRows); return true; }
            if (k == SDLK_PAGEDOWN) { _historyTop += kHistoryRows; return true; }
        }
        if (e.key.keysym.sym == SDLK_ESCAPE)
        {
            if (_openMenu >= 0 || _popup)
            {
                _openMenu = -1;
                _popup = 0;
                return true;
            }
            if (_tool != Tool::None)
            {
                _tool = Tool::None;
                return true;
            }
            if (!_windows.empty())
            {
                _windows.back()->closed = true;
                return true;
            }
            return false;
        }
        if (e.key.keysym.sym == SDLK_F1)
        {
            OpenWindow(kWinHelp);
            return true;
        }
        if ((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_3 && onCommand)
        {
            onCommand("scale:" + std::to_string(e.key.keysym.sym - SDLK_0)); // interface size (not in the original menus)
            return true;
        }
        if (!_windows.empty() && _windows.back()->KeyDown(e.key.keysym.sym, _ctx))
        {
            if (!_ctx.message.empty())
                ShowMessage(_ctx.message);
            _ctx.message.clear();
            if (!_ctx.command.empty() && onCommand)
            {
                const std::string c = _ctx.command;
                _ctx.command.clear();
                if (!UiCommand(c))
                    onCommand(c);
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

} // namespace osf
