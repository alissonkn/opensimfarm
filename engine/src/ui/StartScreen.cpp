// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/StartScreen.h"

#include "assets/Megadata.h"
#include "core/Paths.h"
#include "game/GameState.h"

#include <algorithm>
#include <cctype>
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
constexpr Button kMapSizeButton{ 229, 342, 150, 22 };
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
    int x, y;
    if (!ToPicture(e.button.x, e.button.y, x, y))
        return {};
    if (_design)
        return DesignEvent(x, y);
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
    std::vector<uint16_t> terrain;
    opt.terrain = &terrain;
    _haveWorld = GenerateWorld(_gameDir, *mega, _template, opt, _world, err);
    if (!_haveWorld)
    {
        _note = err;
        return;
    }
    // the preview as the original draws it: each cell's ground tile (the low 11 bits of its word) as a
    // 2 x 2 block of its overview colours (far_mapc.mbm, the Map window's table) in the design picture's palette
    const std::vector<uint8_t> mapc = mega->Read("far_mapc.mbm");
    const Palette pal = LoadPalette(mega->Read("scenario.pal"));
    // (a larger world is drawn the same way at two pixels a cell and scaled into the preview box)
    const int N = _world.world.size, D = 2 * N;
    _previewSide = D;
    _preview.assign(static_cast<size_t>(D) * D, 0xFF000000u);
    for (int x = 0; x < N; ++x)
        for (int y = 0; y < N; ++y)
        {
            const int t = terrain[static_cast<size_t>(x * N + y)];
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

void StartScreen::DrawMapSizeButton()
{
    const Button& b = kMapSizeButton;
    _g.Fill(b.x, b.y, b.w, b.h, kBlack);
    _g.Fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kLightGrey);
    _g.Bevel(b.x + 1, b.y + 1, b.w - 2, b.h - 2, true);
    _g.TextCentre(b.x + b.w / 2, b.y + 7, _designMapSize == kLargeMapSize ? "MAP 144 x 144" : "MAP 96 x 96", kBlack);
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
    // design palette's colours 3 (rain), 1 (temperature), 8 (wind), the rest black; the original's planar fill ends
    // on whole bytes, so the steps show 0, 24, 48 and 80 pixels
    if (_g.Mega())
    {
        const Palette pal = LoadPalette(_g.Mega()->Read("scenario.pal"));
        const int sy[3] = { 105, 163, 222 }, colour[3] = { 3, 1, 8 };
        const int cls[3] = { _opt.rain, _opt.temperature, _opt.wind };
        for (int k = 0; k < 3; ++k)
        {
            const int w = (160 + std::min(80, 27 * cls[k])) / 8 * 8 - 160;
            const auto& c = pal[static_cast<size_t>(colour[k])];
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
        SDL_Renderer* r = _g.Renderer();
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        _g.DrawFullScreen(_intro == 0 ? "prsnt480.int" : "sftitle.int", _intro == 0 ? "prsnt480.pal" : "sftitle.pal",
                          SDL_Rect{ _ox, _oy, 640 * _scale, 480 * _scale });
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
    _g.SetScale(saved);
    SDL_RenderSetViewport(r, nullptr);
}

} // namespace osf
