// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/StartScreen.h"

#include "assets/Megadata.h"
#include "core/Lang.h"
#include "core/Paths.h"
#include "game/GameState.h"
#include "game/MapEdit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace osf {

namespace {
// positions in the 640x480 picture
constexpr SDL_Rect kMap{ 248, 39, 290, 290 };
struct Button { int x, y, w, h; };
constexpr Button kUseRegion{ 97, 233, 140, 22 }, kDesign{ 97, 258, 140, 22 }, kLoad{ 97, 282, 140, 22 }, kQuit{ 97, 306, 140, 22 };
bool In(const Button& b, int x, int y) { return x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h; }
// the map size of a designed world, under the design picture (an engine addition: 96 x 96 as the original, or
// 144 x 144 for more land). The region screen stays as the original: its worlds are 96 x 96.
constexpr Button kMapSizeButton{ 150, 342, 150, 22 };
// the map editor's button beside it (an engine addition)
constexpr Button kEditButton{ 340, 342, 150, 22 };
// the language button (an engine addition), centred under the panel in the black part of the picture, and its list
constexpr Button kLanguageButton{ 200, 350, 240, 20 };
constexpr int kLanguageRowH = 14;
// the eight prebuilt farms on the region map: 17x17 boxes at (248 + 16 gx, 40 + 16 gy)
constexpr int kFarmGx[8] = { 0, 1, 1, 3, 9, 11, 13, 15 }, kFarmGy[8] = { 8, 11, 3, 6, 7, 6, 11, 8 };
} // namespace

StartScreen::StartScreen(Gfx& g, const std::string& gameDir) : _g(g), _gameDir(gameDir)
{
    LoadClimateMap();
    // the original opens with the region cell (2, 8) picked (green frame) and its climate on the sliders, the same at
    // every program start (seen live twice, 2026-11-04)
    _cellX = 2;
    _cellY = 8;
    Climate(_cellX, _cellY);
    _regionPicked = _classes[0] >= 0;
}

void StartScreen::Climate(int gx, int gy)
{
    // the sliders show the climate classes (0..3) of a region map cell
    const char* names[3] = { "rain data", "temp data", "wind data" };
    for (int k = 0; k < 3; ++k)
    {
        const auto it = _grids.find(names[k]);
        _classes[k] = it != _grids.end() && gy < static_cast<int>(it->second.size()) && gx < static_cast<int>(it->second[gy].size())
                          ? it->second[gy][gx] - 'a'
                          : -1;
    }
}

bool StartScreen::Valid(int gx, int gy) const
{
    for (const auto& [name, rows] : _grids)
        if (name.rfind("hotpoint", 0) == 0)
            return gy < static_cast<int>(rows.size()) && gx < static_cast<int>(rows[gy].size()) && rows[gy][gx] > 'a';
    return true; // a file without the grid: every cell
}

void StartScreen::LoadClimateMap()
{
    const Megadata* mega = _g.Mega();
    if (!mega)
        return;
    std::string section, line;
    for (uint8_t c : mega->Read("scenario.odt"))
    {
        if (c == '\r')
            continue;
        if (c != '\n')
        {
            line += static_cast<char>(c);
            continue;
        }
        if (line.rfind("//", 0) == 0)
        {
            section.clear();
            for (char ch : line.substr(2))
                section += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        else if (!section.empty() && line.size() >= 18 && line[18] == ' ')
            _grids[section].push_back(line.substr(0, 18));
        line.clear();
    }
}

bool StartScreen::ToPicture(int px, int py, int& x, int& y) const
{
    x = (px - _ox) / _scale;
    y = (py - _oy) / _scale;
    return x >= 0 && y >= 0 && x < 640 && y < 480;
}

std::string StartScreen::HandleEvent(const SDL_Event& e)
{
    if (_intro < 2)
    {
        // a mouse button (on its release, as the original waits for it) or a key moves on to the next picture
        if (e.type == SDL_MOUSEBUTTONUP || e.type == SDL_KEYDOWN)
        {
            _intro++;
            _introShown = 0;
        }
        return {};
    }
    if (_editing)
    {
        int px = 0, py = 0;
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
            px = e.button.x, py = e.button.y;
        else if (e.type == SDL_MOUSEMOTION)
            px = e.motion.x, py = e.motion.y;
        else
            SDL_GetMouseState(&px, &py);
        int x = -1, y = -1;
        if (_phone)
        {
            // the editor magnified to the phone's screen (DrawPhone's _editF and offset)
            x = static_cast<int>(std::floor((px - _editX) / _editF));
            y = static_cast<int>(std::floor((py - _editY) / _editF));
            if (x < 0 || y < 0 || x >= 640 || y >= 480)
                x = y = -1;
        }
        else
            ToPicture(px, py, x, y);
        const std::string r = _editor.HandleEvent(e, x, y);
        if (r == "done")
        {
            _land = _editor.Land();
            _sites = _editor.Sites();
            FinishEdited();
            _editing = false;
        }
        else if (r == "cancel")
            _editing = false;
        return {};
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
    {
        if (_design)
        {
            _design = false;
            return {};
        }
        return "quit";
    }
    if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT)
        return {};
    if (_phone)
        return PhoneEvent(e.button.x, e.button.y);
    int x, y;
    if (!ToPicture(e.button.x, e.button.y, x, y))
        return {};
    if (!_design && LanguageClick(x, y))
    {
        std::string c;
        if (!_changedLanguage.empty())
            c = "language:" + _changedLanguage;
        _changedLanguage.clear();
        return c;
    }
    if (_design)
        return DesignEvent(x, y);
    return PictureClick(x, y);
}

std::string StartScreen::PictureClick(int x, int y)
{
    // the farm USE THIS REGION starts: a place on the map gets a world generated with its climate and the design
    // screen's river and lake choices (both off unless turned on there), a farm box its prebuilt farm
    const auto use = [&]() -> std::string {
        if (!_regionPicked)
            return "farm:" + std::to_string(_selected);
        _opt.rain = std::max(0, _classes[0]);
        _opt.temperature = std::max(0, _classes[1]);
        _opt.wind = std::max(0, _classes[2]);
        Generate();
        return _haveWorld ? "generated" : std::string();
    };
    if (In(kUseRegion, x, y))
        return use();
    // LOAD A SAVED GAME (seen live): the farm screen opens with the LOAD GAME window over the farm of the current
    // choice; its CLOSE comes back to this screen
    if (In(kLoad, x, y))
    {
        const std::string c = use();
        return c.empty() ? c : "load+" + c;
    }
    if (In(kQuit, x, y))
        return "quit";
    if (_design)
        return DesignEvent(x, y);
    if (In(kDesign, x, y))
    {
        _design = true;
        for (int k = 0; k < 3; ++k)
            if (_classes[k] >= 0)
                (k == 0 ? _opt.rain : k == 1 ? _opt.temperature : _opt.wind) = _classes[k];
        Generate();
        return {};
    }
    for (int k = 0; k < 8; ++k)
        if (x >= 248 + 16 * kFarmGx[k] && x < 265 + 16 * kFarmGx[k] && y >= 40 + 16 * kFarmGy[k] && y < 57 + 16 * kFarmGy[k])
        {
            _selected = k;
            _regionPicked = false;
            Climate(kFarmGx[k], kFarmGy[k]);
            return {};
        }
    if (x >= kMap.x && y >= kMap.y && x < kMap.x + kMap.w && y < kMap.y + kMap.h)
    {
        const int gx = std::min(17, (x - 248) / 16), gy = std::min(17, std::max(0, y - 40) / 16);
        // only a cell the file's "Hotpoint box" grid marks valid (b) can be picked; a click on the sea or outside the
        // country does nothing (seen live: the earlier pick stays) and the original plays its refusal click
        if (!Valid(gx, gy))
            return {};
        _cellX = gx;
        _cellY = gy;
        Climate(gx, gy);
        _regionPicked = _classes[0] >= 0;
        return {};
    }
    return {};
}

// ---------------------------------------------------------------- Design Your Terrain (defworld.int)
namespace {
constexpr Button kRainDown{ 139, 102, 17, 16 }, kRainUp{ 243, 102, 17, 16 }, kTempDown{ 139, 160, 17, 16 }, kTempUp{ 243, 160, 17, 16 },
    kWindDown{ 139, 219, 17, 16 }, kWindUp{ 243, 219, 17, 16 }, kRiver{ 176, 258, 49, 24 }, kLake{ 176, 285, 49, 24 },
    kGenerate{ 266, 286, 123, 24 }, kPlay{ 401, 286, 48, 24 };
constexpr SDL_Rect kPreview{ 264, 72, 192, 192 };
}

void StartScreen::Generate(bool byButton)
{
    const Megadata* mega = _g.Mega();
    if (!mega)
        return;
    if (!_template.Load(_gameDir, FindFileNoCase(_gameDir, "SCEN0.SSM").value_or(""), _note))
        return;
    _opt.seed = static_cast<unsigned>(SDL_GetTicks()) * 2654435761u + 7u;
    std::string err;
    // the ground shade term: the design screen's GENERATE WORLD sets it to rainfall x 4 first (live:
    // rainfall 0 gave the driest ground 15-17); entering the screen and Use This Region keep the live soil
    // wetness, a new game's 2 here
    _byButton = byButton;
    _opt.mapSize = _design ? _designMapSize : kOriginalMapSize;
    WorldOptions opt = _opt;
    opt.wetness = byButton ? 4 * _opt.rain : 2;
    _haveWorld = GenerateTerrain(*mega, _template, opt, _land, _sites, err);
    if (!_haveWorld)
    {
        _note = err;
        return;
    }
    _wetness = opt.wetness;
    _world = _land;
    FinishWorld(_gameDir, *mega, _world, _sites);
    MakePreview();
}

void StartScreen::FinishEdited()
{
    if (!_g.Mega())
        return;
    _world = _land;
    FinishWorld(_gameDir, *_g.Mega(), _world, _sites);
    MakePreview();
}

void StartScreen::MakePreview()
{
    const Megadata* mega = _g.Mega();
    if (!mega)
        return;
    // the preview as the original draws it: each cell's ground tile (the low 11 bits of its word) as a
    // 2 x 2 block of its overview colours (far_mapc.mbm, the Map window's table) in the design picture's palette
    const std::vector<uint8_t> mapc = mega->Read("far_mapc.mbm");
    const Palette pal = LoadPalette(mega->Read("scenario.pal"));
    // (a larger world is drawn the same way at two pixels a cell and scaled into the preview box)
    const int N = _land.world.size, D = 2 * N;
    _previewSide = D;
    _preview.assign(static_cast<size_t>(D) * D, 0xFF000000u);
    for (int x = 0; x < N; ++x)
        for (int y = 0; y < N; ++y)
        {
            const int t = _land.world.At(x, y).BaseTile();
            for (int r = 0; r < 2; ++r)
            {
                const int b = 2 * t + r < static_cast<int>(mapc.size()) ? mapc[static_cast<size_t>(2 * t + r)] : 0;
                for (int c = 0; c < 2; ++c)
                {
                    int idx = 0;
                    for (int plane = 0; plane < 4; ++plane)
                        idx |= ((b >> (6 - 2 * plane + (1 - c))) & 1) << plane;
                    _preview[static_cast<size_t>(2 * y + r) * D + 2 * x + c] =
                        0xFF000000u | (pal[idx][0] << 16) | (pal[idx][1] << 8) | pal[idx][2];
                }
            }
        }
}

void StartScreen::ToggleMapSize()
{
    _designMapSize = _designMapSize == kLargeMapSize ? kOriginalMapSize : kLargeMapSize;
    Generate(_byButton); // the preview shows a world of the new size at once
}

void StartScreen::DrawEditButton()
{
    const Button& b = kEditButton;
    _g.Fill(b.x, b.y, b.w, b.h, kBlack);
    _g.Fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kLightGrey);
    _g.Bevel(b.x + 1, b.y + 1, b.w - 2, b.h - 2, true);
    _g.TextFit(b.x + 2, b.y + 7, b.w - 4, Tr("EDIT MAP"), kBlack, 1);
}

void StartScreen::DrawMapSizeButton()
{
    const Button& b = kMapSizeButton;
    _g.Fill(b.x, b.y, b.w, b.h, kBlack);
    _g.Fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kLightGrey);
    _g.Bevel(b.x + 1, b.y + 1, b.w - 2, b.h - 2, true);
    _g.TextCentre(b.x + b.w / 2, b.y + 7, _designMapSize == kLargeMapSize ? Tr("MAP 144 x 144") : Tr("MAP 96 x 96"), kBlack);
}

std::string StartScreen::DesignEvent(int x, int y)
{
    auto step = [&](int& v, int d) { v = std::clamp(v + d, 0, 3); };
    if (In(kRainDown, x, y)) step(_opt.rain, -1);
    if (In(kRainUp, x, y)) step(_opt.rain, 1);
    if (In(kTempDown, x, y)) step(_opt.temperature, -1);
    if (In(kTempUp, x, y)) step(_opt.temperature, 1);
    if (In(kWindDown, x, y)) step(_opt.wind, -1);
    if (In(kWindUp, x, y)) step(_opt.wind, 1);
    // RIVER and LAKE only switch (drawn pressed while on); GENERATE WORLD builds the world with them, as the original
    if (In(kRiver, x, y))
        _opt.river = !_opt.river;
    if (In(kLake, x, y))
        _opt.lake = !_opt.lake;
    if (In(kGenerate, x, y))
        Generate(true);
    if (In(kMapSizeButton, x, y))
        ToggleMapSize();
    if (In(kEditButton, x, y) && _haveWorld)
    {
        _editor.Open(_land, _sites, _wetness);
        _editing = true;
    }
    if (In(kPlay, x, y) && _haveWorld)
    {
        // the climate classes count at PLAY!, so the tables follow the sliders even without a new generation
        std::string err;
        GameState again;
        const unsigned seed = _opt.seed;
        if (_g.Mega() && GenerateWorld(_gameDir, *_g.Mega(), _template, _opt, again, err))
        {
            _opt.seed = seed;
            _world.weather = again.weather;
        }
        _design = false;
        return "generated";
    }
    return {};
}

void StartScreen::DrawDesign()
{
    // the sliders: 80 x 10 bars at x 160, filled from the left with value x 27 pixels (at most 80) in the
    // design palette's colours 3 (rain), 1 (temperature), 8 (wind), the rest black (the main palette's red, yellow
    // and green; for the Windows edition the Windows colours that stand for them); the original's planar fill ends
    // on whole bytes, so the steps show 0, 24, 48 and 80 pixels
    if (_g.Mega())
    {
        const int sy[3] = { 105, 163, 222 }, colour[3] = { kRed, kYellow, kGreen };
        const int cls[3] = { _opt.rain, _opt.temperature, _opt.wind };
        for (int k = 0; k < 3; ++k)
        {
            const int w = (160 + std::min(80, 27 * cls[k])) / 8 * 8 - 160;
            const auto& c = _g.Ui(colour[k]);
            std::vector<uint32_t> bar(80, 0xFF000000u);
            for (int i = 0; i < w; ++i)
                bar[static_cast<size_t>(i)] = 0xFF000000u | (c[0] << 16) | (c[1] << 8) | c[2];
            _g.DrawPixels(bar, 80, 1, 160, sy[k], 80, 10);
        }
    }
    // a switched-on button is drawn pressed (read from the original): its 2-pixel bevel inside the black outline
    // turns grey (13) at the top / left and white at the bottom / right, the text stays in place
    const auto pressed = [&](const Button& b) {
        const int x = b.x, y = b.y;
        _g.Fill(x + 1, y + 1, 45, 1, kGrey);
        _g.Fill(x + 46, y + 1, 1, 1, kWhite);
        _g.Fill(x + 47, y + 1, 1, 1, kLightGrey);
        _g.Fill(x + 1, y + 2, 44, 1, kGrey);
        _g.Fill(x + 45, y + 2, 1, 1, kWhite);
        _g.Fill(x + 46, y + 2, 1, 1, kLightGrey);
        _g.Fill(x + 47, y + 2, 1, 1, kWhite);
        _g.Fill(x + 1, y + 3, 2, 19, kGrey);
        _g.Fill(x + 46, y + 3, 2, 19, kWhite);
        _g.Fill(x + 1, y + 22, 1, 1, kGrey);
        _g.Fill(x + 2, y + 22, 1, 1, kLightGrey);
        _g.Fill(x + 3, y + 22, 45, 1, kWhite);
        _g.Fill(x + 1, y + 23, 1, 1, kLightGrey);
        _g.Fill(x + 2, y + 23, 46, 1, kWhite);
    };
    if (_opt.river)
        pressed(kRiver);
    if (_opt.lake)
        pressed(kLake);
    if (_haveWorld)
    {
        _g.DrawPixels(_preview, _previewSide, _previewSide, kPreview.x, kPreview.y, kPreview.w, kPreview.h);
        // the town's home plot marked T at its corner, the farm H at its origin cell (two pixels per cell; a larger
        // world's preview is scaled into the box, and the marks with it)
        auto mark = [&](int px, int py, const char* c) { _g.Text(kPreview.x + px * 192 / _previewSide, kPreview.y + py * 192 / _previewSide, c, kBlack); };
        mark(16 * static_cast<int>(_world.scalars["homestead_x"]), 16 * static_cast<int>(_world.scalars["homestead_y"]), "T");
        mark(2 * static_cast<int>(_world.scalars["view_x"]), 2 * static_cast<int>(_world.scalars["view_y"]), "H");
    }
    DrawMapSizeButton();
    DrawEditButton();
    if (!_note.empty())
        _g.Text(140, 376, _note.substr(0, 44), kYellow);
}

void StartScreen::Draw(int w, int h)
{
    if (_intro < 2)
    {
        const uint32_t now = SDL_GetTicks();
        if (!_introShown)
            _introShown = now;
        if (now - _introShown >= 959u * 20u) // 959 ticks of the 50 Hz clock
        {
            _intro++;
            _introShown = now;
        }
    }
    if (_intro < 2)
    {
        _scale = std::max(1, std::min(w / 640, h / 480));
        _ox = (w - 640 * _scale) / 2;
        _oy = (h - 480 * _scale) / 2;
        SDL_Rect dst{ _ox, _oy, 640 * _scale, 480 * _scale };
        if (_phone)
        {
            // the pictures as wide as the phone allows (any size, not whole multiples)
            const float f = std::min(w / 640.0f, h / 480.0f);
            dst = { static_cast<int>((w - 640 * f) / 2), static_cast<int>((h - 480 * f) / 2), static_cast<int>(640 * f), static_cast<int>(480 * f) };
        }
        SDL_Renderer* r = _g.Renderer();
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        _g.DrawFullScreen(_intro == 0 ? "prsnt480.int" : "sftitle.int", _intro == 0 ? "prsnt480.pal" : "sftitle.pal", dst);
        return;
    }
    if (_editing && _phone)
    {
        // the map editor keeps its 640 x 480 layout, magnified to fill the phone (best turned sideways)
        SDL_Renderer* r = _g.Renderer();
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        _editF = std::min(w / 640.0f, h / 480.0f);
        _editX = static_cast<int>((w - 640 * _editF) / 2);
        _editY = static_cast<int>((h - 480 * _editF) / 2);
        SDL_RenderSetScale(r, _editF, _editF);
        const SDL_Rect vp{ static_cast<int>(_editX / _editF), static_cast<int>(_editY / _editF), 640, 480 };
        SDL_RenderSetViewport(r, &vp);
        const int saved = _g.Scale();
        _g.SetScale(1);
        _editor.Draw();
        _g.SetScale(saved);
        SDL_RenderSetViewport(r, nullptr);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        return;
    }
    if (_phone)
    {
        PhoneLayout(w, h);
        DrawPhone();
        return;
    }
    if (_editing)
    {
        _scale = std::max(1, std::min(w / 640, h / 480));
        _ox = (w - 640 * _scale) / 2;
        _oy = (h - 480 * _scale) / 2;
        SDL_Renderer* r = _g.Renderer();
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        const SDL_Rect vp{ _ox, _oy, 640 * _scale, 480 * _scale };
        SDL_RenderSetViewport(r, &vp);
        const int saved = _g.Scale();
        _g.SetScale(_scale);
        _editor.Draw();
        _g.SetScale(saved);
        SDL_RenderSetViewport(r, nullptr);
        return;
    }
    if (_design)
    {
        _scale = std::max(1, std::min(w / 640, h / 480));
        _ox = (w - 640 * _scale) / 2;
        _oy = (h - 480 * _scale) / 2;
        SDL_Renderer* r = _g.Renderer();
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        _g.DrawFullScreen("defworld.int", "scenario.pal", SDL_Rect{ _ox, _oy, 640 * _scale, 480 * _scale });
        const SDL_Rect vp{ _ox, _oy, 640 * _scale, 480 * _scale };
        SDL_RenderSetViewport(r, &vp);
        const int saved = _g.Scale();
        _g.SetScale(_scale);
        DrawTranslatedDesign();
        DrawDesign();
        _g.SetScale(saved);
        SDL_RenderSetViewport(r, nullptr);
        return;
    }
    _scale = std::max(1, std::min(w / 640, h / 480));
    _ox = (w - 640 * _scale) / 2;
    _oy = (h - 480 * _scale) / 2;
    SDL_Renderer* r = _g.Renderer();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    _g.DrawFullScreen("scenario.int", "scenario.pal", SDL_Rect{ _ox, _oy, 640 * _scale, 480 * _scale });
    // interface drawing in picture coordinates
    const SDL_Rect vp{ _ox, _oy, 640 * _scale, 480 * _scale };
    SDL_RenderSetViewport(r, &vp);
    const int saved = _g.Scale();
    _g.SetScale(_scale);
    DrawTranslatedRegion();
    // the three sliders: rainfall, temperature, wind
    const int sliderY[3] = { 101, 149, 197 };
    for (int k = 0; k < 3; ++k)
        if (_classes[k] > 0)
                _g.Fill(120, sliderY[k], 32 * _classes[k], 10, kBlue);
    // farm boxes (the selected one dark grey) and a picked region cell in green
    for (int k = 0; k < 8; ++k)
    {
        const int bx = 248 + 16 * kFarmGx[k], by = 40 + 16 * kFarmGy[k];
        _g.Frame(bx, by, 17, 17, _selected == k && !_regionPicked ? kDarkGrey : kYellow);
        if (_selected == k && !_regionPicked)
        {
            // the dark frame leaves the two left corners yellow
            _g.Fill(bx, by, 1, 1, kYellow);
            _g.Fill(bx, by + 16, 1, 1, kYellow);
        }
    }
    if (_regionPicked)
        _g.Frame(248 + 16 * _cellX, 40 + 16 * _cellY, 17, 17, kGreen);
    if (!_note.empty())
        _g.Text(96, 460, _note, kYellow);
    DrawLanguage();
    _g.SetScale(saved);
    SDL_RenderSetViewport(r, nullptr);
}


// ---------------------------------------------------------------- language (an engine addition)
void StartScreen::DrawLanguage()
{
    const Button& b = kLanguageButton;
    const auto langs = AvailableLanguages();
    std::string current = "English";
    for (const LanguageInfo& l : langs)
        if (l.code == CurrentLanguage())
            current = l.name;
    _g.Fill(b.x, b.y, b.w, b.h, kBlack);
    _g.Fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kLightGrey);
    _g.Bevel(b.x + 1, b.y + 1, b.w - 2, b.h - 2, !_languageOpen);
    // "Language" in the chosen language, and the language's own name
    const std::string label = std::string(Tr("Language")) + ": " + current;
    _g.TextCentre(b.x + b.w / 2 - 4, b.y + 6, label, kBlack);
    // a small arrow: the button opens a list
    for (int k = 0; k < 4; ++k)
        _g.Fill(b.x + b.w - 16 + k, b.y + 8 + k, 7 - 2 * k, 1, kBlack);
    if (!_languageOpen)
        return;
    const int n = static_cast<int>(langs.size());
    const int ly = b.y + b.h + 1;
    _g.Fill(b.x, ly, b.w, n * kLanguageRowH + 4, kBlack);
    _g.Fill(b.x + 1, ly + 1, b.w - 2, n * kLanguageRowH + 2, kWhite);
    for (int i = 0; i < n; ++i)
    {
        const bool chosen = langs[static_cast<size_t>(i)].code == CurrentLanguage();
        const int ry = ly + 2 + i * kLanguageRowH;
        if (chosen)
            _g.Fill(b.x + 2, ry, b.w - 4, kLanguageRowH, kNavy);
        _g.TextCentre(b.x + b.w / 2, ry + 3, langs[static_cast<size_t>(i)].name, chosen ? kWhite : kBlack);
    }
}

bool StartScreen::LanguageClick(int x, int y)
{
    const Button& b = kLanguageButton;
    if (In(b, x, y))
    {
        _languageOpen = !_languageOpen;
        return true;
    }
    if (!_languageOpen)
        return false;
    _languageOpen = false;
    const auto langs = AvailableLanguages();
    const int ly = b.y + b.h + 1 + 2;
    if (x >= b.x && x < b.x + b.w && y >= ly && y < ly + static_cast<int>(langs.size()) * kLanguageRowH)
    {
        const std::string& code = langs[static_cast<size_t>((y - ly) / kLanguageRowH)].code;
        if (code != CurrentLanguage())
        {
            SetLanguage(code);
            _changedLanguage = code;
        }
    }
    return true; // a click elsewhere only closes the list
}

namespace {
// a word of the picture covered with the panel's grey and written again in the chosen language
struct Cover
{
    int x, y, w, h;   // the area covered
    const char* text; // English (translated when drawn); '\n' starts a second line
    int cx, ty;       // the text's centre and top (a title is drawn twice the size)
    bool big;
};
} // namespace

static void DrawCovers(Gfx& g, int scale, const Cover* covers, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const Cover& c = covers[i];
        g.Fill(c.x, c.y, c.w, c.h, kLightGrey);
        const std::string t = Tr(c.text);
        if (!c.big)
        {
            if (Gfx::TextWidth(t) <= c.w - 2)
                g.TextCentre(c.cx, c.ty, t, kBlack);
            else
                g.TextFit(c.x + 1, c.ty, c.w - 2, t, kBlack, 1); // tighter letters when the place is too narrow
            continue;
        }
        // a title: the font at twice the size (drawn at half the coordinates with the scale doubled), blue over a
        // dark shadow, one or two lines
        const size_t nl = t.find('\n');
        const std::string lines[2] = { t.substr(0, nl), nl == std::string::npos ? std::string() : t.substr(nl + 1) };
        g.SetScale(scale * 2);
        for (int l = 0; l < 2; ++l)
        {
            const int x = (c.cx - Gfx::TextWidth(lines[l])) / 2, y = (c.ty + 22 * l) / 2;
            g.Text(x + 1, y + 1, lines[l], kNavy);
            g.Text(x, y, lines[l], kBlue);
        }
        g.SetScale(scale);
    }
}

void StartScreen::DrawTranslatedRegion()
{
    if (CurrentLanguage() == "en")
        return; // the picture's own words
    static const Cover kCovers[] = {
        { 100, 30, 136, 44, N_("CHOOSE\nA REGION"), 166, 32, true },
        { 93, 86, 147, 10, N_("RAINFALL"), 166, 87, false },
        { 93, 134, 147, 10, N_("TEMPERATURE"), 166, 135, false },
        { 93, 182, 147, 10, N_("WIND SPEED"), 166, 183, false },
        { 92, 113, 36, 11, N_("Low"), 108, 114, false }, { 200, 113, 41, 11, N_("High"), 224, 114, false },
        { 92, 160, 36, 11, N_("Low"), 108, 161, false }, { 200, 160, 41, 11, N_("High"), 224, 161, false },
        { 92, 209, 36, 11, N_("Low"), 108, 210, false }, { 200, 209, 41, 11, N_("High"), 224, 210, false },
        { 100, 239, 134, 10, N_("PLAY THIS REGION"), 167, 240, false },
        { 100, 263, 134, 10, N_("MAKE YOUR OWN"), 167, 264, false },
        { 100, 288, 134, 10, N_("OPEN A SAVED GAME"), 167, 289, false },
        { 100, 313, 134, 10, N_("QUIT"), 167, 314, false },
    };
    DrawCovers(_g, _scale, kCovers, sizeof(kCovers) / sizeof(kCovers[0]));
}

void StartScreen::DrawTranslatedDesign()
{
    if (CurrentLanguage() == "en")
        return;
    static const Cover kCovers[] = {
        { 140, 42, 326, 24, N_("MAKE YOUR LAND"), 307, 46, true },
        { 150, 87, 100, 10, N_("RAINFALL"), 200, 89, false },
        { 140, 146, 120, 10, N_("TEMPERATURE"), 200, 148, false },
        { 140, 205, 120, 10, N_("WIND SPEED"), 200, 207, false },
        { 150, 120, 36, 11, N_("Low"), 168, 122, false }, { 206, 120, 44, 11, N_("High"), 230, 122, false },
        { 150, 178, 36, 11, N_("Low"), 169, 180, false }, { 206, 178, 44, 11, N_("High"), 231, 180, false },
        { 150, 237, 36, 11, N_("Low"), 170, 239, false }, { 206, 237, 44, 11, N_("High"), 232, 239, false },
        { 178, 265, 44, 10, N_("RIVER"), 200, 267, false },
        { 178, 292, 44, 10, N_("LAKE"), 201, 294, false },
        { 268, 292, 118, 10, N_("NEW RANDOM WORLD"), 327, 294, false },
        { 403, 292, 44, 10, N_("PLAY!"), 426, 294, false },
    };
    DrawCovers(_g, _scale, kCovers, sizeof(kCovers) / sizeof(kCovers[0]));
}

// ---------------------------------------------------------------- the phone layout (an engine addition)
// User 2026-11-11: "redesign the start screens for phones" (the phone version's bars and menus may differ from the
// original's, the mechanics stay). The region and design screens are rebuilt from their parts: the map (or the
// preview) as large as the screen allows, the climate bars, and big buttons. Each button stands for one of the
// picture's buttons and its tap goes through the same code as a click there.
namespace {
enum PhoneButtonId
{
    kPUse, kPDesign, kPLoad, kPQuit, kPLang, kPRainD, kPRainU, kPTempD, kPTempU, kPWindD, kPWindU,
    kPRiver, kPLake, kPGenerate, kPPlay, kPMapSize, kPEdit, kPBack
};
const Button* PictureButton(int id)
{
    static const Button* const k[] = { &kUseRegion, &kDesign, &kLoad, &kQuit, nullptr, &kRainDown, &kRainUp, &kTempDown,
                                       &kTempUp, &kWindDown, &kWindUp, &kRiver, &kLake, &kGenerate, &kPlay,
                                       &kMapSizeButton, &kEditButton, nullptr };
    return id >= 0 && id < static_cast<int>(sizeof(k) / sizeof(k[0])) ? k[id] : nullptr;
}
bool InRect(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
// the picture's own English words, or the translation the picture's covers use
std::string Words(const char* english, const char* key)
{
    if (CurrentLanguage() == "en")
        return english;
    std::string t = Tr(key);
    std::replace(t.begin(), t.end(), '\n', ' ');
    return t;
}
const char* const kClimateNames[3] = { N_("RAINFALL"), N_("TEMPERATURE"), N_("WIND SPEED") };
} // namespace

void StartScreen::PhoneLayout(int w, int h)
{
    _u = std::max(1, static_cast<int>(std::lround(std::min(w, h) / 200.0)));
    _uw = w / _u;
    _uh = h / _u;
    _wide = w > h;
}

void StartScreen::DrawPhoneButton(const SDL_Rect& r, const std::string& label, bool pressed, bool enabled)
{
    _g.Frame(r.x - 1, r.y - 1, r.w + 2, r.h + 2, kBlack);
    _g.Fill(r.x, r.y, r.w, r.h, pressed ? kGrey : kLightGrey);
    _g.Bevel(r.x, r.y, r.w, r.h, !pressed);
    _g.TextFit(r.x + 3, r.y + (r.h - 8) / 2, r.w - 6, label, enabled ? kBlack : kGrey, 1);
}

void StartScreen::DrawPhoneClimate(int x, int y, int w, int k, int value, int colour, bool arrows)
{
    int lw = 0;
    for (const char* n : kClimateNames)
        lw = std::max(lw, Gfx::SmallTextWidth(Tr(n)));
    lw += 6;
    const int h = arrows ? 14 : 8;
    _g.SmallText(x, y + (h - 6) / 2, Tr(kClimateNames[k]), kBlack);
    int bx = x + lw, bw = w - lw;
    if (arrows)
    {
        // lower and higher on either side of the bar, as the design picture's arrow buttons
        const SDL_Rect down{ bx, y, 16, h }, up{ x + w - 16, y, 16, h };
        DrawPhoneButton(down, "-", false);
        DrawPhoneButton(up, "+", false);
        _hits.push_back({ kPRainD + 2 * k, down });
        _hits.push_back({ kPRainU + 2 * k, up });
        bx += 20;
        bw -= 40;
    }
    const int by = y + (h - 8) / 2;
    _g.Frame(bx, by, bw, 8, kBlack);
    _g.Fill(bx + 1, by + 1, bw - 2, 6, arrows ? kBlack : kWhite);
    if (value > 0)
        _g.Fill(bx + 1, by + 1, (bw - 2) * std::min(3, value) / 3, 6, colour);
}

void StartScreen::DrawPhone()
{
    SDL_Renderer* rd = _g.Renderer();
    SDL_SetRenderDrawColor(rd, 0, 0, 0, 255);
    SDL_RenderClear(rd);
    const int saved = _g.Scale();
    _g.SetScale(_u);
    _hits.clear();
    // the map (or preview) square and the panel beside it (landscape) or under it (portrait)
    int side, mx, my;
    SDL_Rect panel;
    if (_wide)
    {
        side = std::min(_uh - 8, _uw * 45 / 100);
        mx = 4, my = (_uh - side) / 2;
        panel = { side + 12, 4, _uw - side - 16, _uh - 8 };
    }
    else
    {
        side = std::min(_uw - 8, _uh * 45 / 100);
        mx = (_uw - side) / 2, my = 4;
        panel = { 4, side + 12, _uw - 8, _uh - side - 16 };
    }
    _mapPx = { mx * _u, my * _u, side * _u, side * _u };
    _g.Frame(panel.x - 1, panel.y - 1, panel.w + 2, panel.h + 2, kBlack);
    _g.Fill(panel.x, panel.y, panel.w, panel.h, kLightGrey);
    _g.Bevel(panel.x, panel.y, panel.w, panel.h, true);
    _g.Frame(mx - 1, my - 1, side + 2, side + 2, kLightGrey);

    const int x0 = panel.x + 6, pw = panel.w - 12, bottom = panel.y + panel.h - 6;
    int y = panel.y + 6;
    const std::string title = _design ? Words("DESIGN YOUR TERRAIN", "MAKE YOUR LAND") : Words("SELECT A REGION", "CHOOSE\nA REGION");
    _g.TextFit(x0 + 1, y + 1, pw, title, kNavy, 1);
    _g.TextFit(x0, y, pw, title, kBlue, 1);
    y += 14;

    // the buttons fill what is left in rows of one or two
    const auto rows = [&](const std::vector<std::vector<std::pair<int, std::string>>>& list, int top) {
        const int n = static_cast<int>(list.size()), gap = 5;
        const int bh = std::clamp((bottom - top - (n - 1) * gap) / std::max(1, n), 14, 30);
        int ry = std::max(top, bottom - n * bh - (n - 1) * gap);
        for (const auto& row : list)
        {
            int rx = x0;
            int total = 0;
            for (const auto& b : row)
                total += b.first == kPLang ? 2 : 1;
            const int cw = pw - (static_cast<int>(row.size()) - 1) * gap;
            for (const auto& b : row)
            {
                const int bw = cw * (b.first == kPLang ? 2 : 1) / total;
                const SDL_Rect r{ rx, ry, bw, bh };
                const bool pressed = (b.first == kPRiver && _opt.river) || (b.first == kPLake && _opt.lake);
                const bool enabled = (b.first != kPEdit && b.first != kPPlay) || _haveWorld;
                DrawPhoneButton(r, b.second, pressed, enabled);
                _hits.push_back({ b.first, r });
                rx += bw + gap;
            }
            ry += bh + gap;
        }
    };

    if (!_design)
    {
        // the region map: the picture's map part, scaled to the square
        const float k = _mapPx.w / 290.0f;
        SDL_RenderSetClipRect(rd, &_mapPx);
        _g.DrawFullScreen("scenario.int", "scenario.pal",
                          SDL_Rect{ _mapPx.x - static_cast<int>(248 * k), _mapPx.y - static_cast<int>(39 * k), static_cast<int>(640 * k), static_cast<int>(480 * k) });
        _g.SetScale(1);
        const int t = std::max(2, static_cast<int>(std::lround(k)));
        const auto box = [&](int bx, int by, int colour) {
            const int px = _mapPx.x + static_cast<int>((bx - 248) * k), py = _mapPx.y + static_cast<int>((by - 39) * k), ps = static_cast<int>(17 * k);
            for (int i = 0; i < t; ++i)
                _g.Frame(px + i, py + i, ps - 2 * i, ps - 2 * i, colour);
        };
        for (int f = 0; f < 8; ++f)
            box(248 + 16 * kFarmGx[f], 40 + 16 * kFarmGy[f], _selected == f && !_regionPicked ? kDarkGrey : kYellow);
        if (_regionPicked)
            box(248 + 16 * _cellX, 40 + 16 * _cellY, kGreen);
        _g.SetScale(_u);
        SDL_RenderSetClipRect(rd, nullptr);

        for (int c = 0; c < 3; ++c, y += 11)
            DrawPhoneClimate(x0, y, pw, c, _classes[static_cast<size_t>(c)], kBlue, false);
        y += 2;
        std::string current = "English";
        for (const LanguageInfo& l : AvailableLanguages())
            if (l.code == CurrentLanguage())
                current = l.name;
        std::vector<std::vector<std::pair<int, std::string>>> list = {
            { { kPUse, Words("USE THIS REGION", "PLAY THIS REGION") } },
            { { kPDesign, Words("DESIGN YOUR OWN", "MAKE YOUR OWN") } },
            { { kPLoad, Words("LOAD A SAVED GAME", "OPEN A SAVED GAME") } },
            { { kPLang, std::string(Tr("Language")) + ": " + current }, { kPQuit, Tr("QUIT") } },
        };
        // (only the language's name when "Language: name" would be squeezed: the button is two thirds of the panel)
        if (Gfx::TextWidth(list[3][0].second) > (pw - 5) * 2 / 3 - 6)
            list[3][0].second = current;
        // the hint above the buttons when there is room for it
        const std::string hint = Tr("Tap a yellow box for a ready-made farm, or a place on land for a new one.");
        std::vector<std::string> lines;
        std::string line, word;
        for (size_t i = 0; i <= hint.size(); ++i)
        {
            if (i < hint.size() && hint[i] != ' ')
            {
                word += hint[i];
                continue;
            }
            const std::string tryLine = line.empty() ? word : line + " " + word;
            if (!line.empty() && Gfx::SmallTextWidth(tryLine) > pw)
            {
                lines.push_back(line);
                line = word;
            }
            else
                line = tryLine;
            word.clear();
        }
        if (!line.empty())
            lines.push_back(line);
        const int hintH = static_cast<int>(lines.size()) * 9 + 4;
        if (bottom - y - hintH >= 4 * 20 + 15)
        {
            for (const std::string& l : lines)
            {
                _g.SmallText(x0, y, l, kBlack);
                y += 9;
            }
            y += 4;
        }
        rows(list, y);
    }
    else
    {
        if (_haveWorld)
        {
            _g.DrawPixels(_preview, _previewSide, _previewSide, mx, my, side, side);
            const auto mark = [&](int px, int py, const char* c) { _g.Text(mx + px * side / _previewSide, my + py * side / _previewSide, c, kBlack); };
            mark(16 * static_cast<int>(_world.scalars["homestead_x"]), 16 * static_cast<int>(_world.scalars["homestead_y"]), "T");
            mark(2 * static_cast<int>(_world.scalars["view_x"]), 2 * static_cast<int>(_world.scalars["view_y"]), "H");
        }
        const int colour[3] = { kRed, kYellow, kGreen }, value[3] = { _opt.rain, _opt.temperature, _opt.wind };
        for (int c = 0; c < 3; ++c, y += 17)
            DrawPhoneClimate(x0, y, pw, c, value[c], colour[c], true);
        const std::vector<std::vector<std::pair<int, std::string>>> list = {
            { { kPRiver, Tr("RIVER") }, { kPLake, Tr("LAKE") } },
            { { kPMapSize, _designMapSize == kLargeMapSize ? Tr("MAP 144 x 144") : Tr("MAP 96 x 96") }, { kPEdit, Tr("EDIT MAP") } },
            { { kPGenerate, Words("GENERATE WORLD", "NEW RANDOM WORLD") } },
            { { kPBack, Tr("BACK") }, { kPPlay, Tr("PLAY!") } },
        };
        rows(list, y + 2);
    }
    if (!_note.empty())
        _g.SmallText(x0, bottom - 6, _note.substr(0, static_cast<size_t>(pw / 5)), kRed);
    _g.SetScale(saved);
}

std::string StartScreen::NextLanguage()
{
    const auto langs = AvailableLanguages();
    if (langs.empty())
        return {};
    size_t i = 0;
    while (i < langs.size() && langs[i].code != CurrentLanguage())
        ++i;
    const std::string& code = langs[(i + 1) % langs.size()].code;
    if (code == CurrentLanguage())
        return {};
    SetLanguage(code);
    return "language:" + code;
}

std::string StartScreen::PhoneEvent(int px, int py)
{
    if (!_design && InRect(_mapPx, px, py))
    {
        // a tap on the map: the place of the picture's map it shows
        const float k = _mapPx.w / 290.0f;
        return PictureClick(248 + static_cast<int>((px - _mapPx.x) / k), 39 + static_cast<int>((py - _mapPx.y) / k));
    }
    const int x = px / _u, y = py / _u;
    for (const auto& [id, r] : _hits)
    {
        if (!InRect(r, x, y))
            continue;
        if (id == kPLang)
            return NextLanguage();
        if (id == kPBack)
        {
            _design = false;
            return {};
        }
        const Button* b = PictureButton(id);
        if (!b)
            return {};
        return _design ? DesignEvent(b->x + 1, b->y + 1) : PictureClick(b->x + 1, b->y + 1);
    }
    return {};
}

} // namespace osf
