// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/MapEditor.h"

#include "core/Lang.h"

#include <algorithm>
#include <cstdlib>

namespace osf {

namespace {
// the screen: the map in a 448 x 448 square on the left, the tool panel on the right
constexpr int kMapX = 8, kMapY = 16, kMapSide = 448;
constexpr int kPanelX = 464, kPanelY = 16, kPanelW = 168, kPanelH = 448;
constexpr int kButtonX = kPanelX + 4, kButtonW = kPanelW - 8, kButtonH = 22;
constexpr int kToolY = 40, kToolStep = 26;
constexpr int kBrushY = 214, kBrushW = 37;
constexpr int kBrushSizes[4] = { 1, 3, 5, 9 };
constexpr int kUndoY = 384, kDoneY = 412, kCancelY = 438;
constexpr int kHelpY = 244;
constexpr size_t kUndoSteps = 40;

struct ToolInfo
{
    const char* name;
    const char* help;
    int tile; // the picture on its button
};
const ToolInfo kToolInfo[6] = {
    { N_("GROUND"), N_("Paints plain ground over water, trees and rocks."), 18 },
    { N_("WATER"), N_("Paints lakes and rivers, also on your farm. The farmhouse and the town stay dry."), 55 },
    { N_("TREES"), N_("Plants trees."), 145 },
    { N_("ROCKS"), N_("Places rocks."), 536 },
    { N_("FARM"), N_("Click where your farm starts: 3 x 3 plots. Water under the farmhouse dries."), 180 },
    { N_("TOWN"), N_("Click where the town starts: one plot. Water under it dries."), 166 },
};

bool In(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && y >= by && x < bx + bw && y < by + bh; }
} // namespace

void MapEditor::Open(const GameState& land, const WorldSites& sites, int wetness)
{
    _land = land;
    _sites = sites;
    _wetness = wetness;
    _random = static_cast<uint32_t>(SDL_GetTicks()) | 1u;
    _undo.clear();
    _note.clear();
    _zoom = 0;
    _camX = _camY = 0;
    _painting = _panning = false;
    _hoverX = _hoverY = -1;
}

static int CellPixels(int zoom, int size)
{
    return zoom == 0 ? kMapSide / size : zoom == 1 ? 8 : 16;
}

bool MapEditor::CellAt(int x, int y, int& cx, int& cy) const
{
    const int px = CellPixels(_zoom, _land.world.size), shown = std::min(_land.world.size, kMapSide / px);
    if (x < kMapX || y < kMapY || x >= kMapX + shown * px || y >= kMapY + shown * px)
        return false;
    cx = _camX + (x - kMapX) / px;
    cy = _camY + (y - kMapY) / px;
    return _land.world.In(cx, cy);
}

void MapEditor::ClampCamera()
{
    const int shown = kMapSide / CellPixels(_zoom, _land.world.size);
    const int most = std::max(0, _land.world.size - shown);
    _camX = std::clamp(_camX, 0, most);
    _camY = std::clamp(_camY, 0, most);
}

void MapEditor::SetZoom(int level, int ax, int ay)
{
    level = std::clamp(level, 0, 2);
    if (level == _zoom)
        return;
    int cx = _land.world.size / 2, cy = cx;
    const bool anchored = CellAt(ax, ay, cx, cy);
    const int offX = anchored ? (ax - kMapX) : kMapSide / 2, offY = anchored ? (ay - kMapY) : kMapSide / 2;
    _zoom = level;
    const int px = CellPixels(_zoom, _land.world.size);
    _camX = cx - offX / px;
    _camY = cy - offY / px;
    ClampCamera();
}

void MapEditor::Pan(int dx, int dy)
{
    _camX += dx;
    _camY += dy;
    ClampCamera();
}

void MapEditor::PushUndo()
{
    if (_undo.size() >= kUndoSteps)
        _undo.erase(_undo.begin());
    _undo.push_back(Step{ _land.world.cells, _sites });
}

void MapEditor::Undo()
{
    if (_undo.empty())
        return;
    _land.world.cells = std::move(_undo.back().cells);
    _sites = _undo.back().sites;
    _undo.pop_back();
}

void MapEditor::FarmAt(int cx, int cy, int& col, int& row) const
{
    // the clicked plot becomes the farm's middle one, kept on the map
    const int P = _land.Plots();
    col = std::clamp(cx / kPlotCells - 1, 0, P - 3);
    row = std::clamp(cy / kPlotCells - 1, 0, P - 3);
}

void MapEditor::Apply(int cx, int cy)
{
    std::string why;
    switch (_tool)
    {
    case kGround: PaintLand(_land, LandBrush::Ground, cx, cy, _brush, _wetness, _random, _sites); break;
    case kWater: PaintLand(_land, LandBrush::Water, cx, cy, _brush, _wetness, _random, _sites); break;
    case kTrees: PaintLand(_land, LandBrush::Trees, cx, cy, _brush, _wetness, _random, _sites); break;
    case kRocks: PaintLand(_land, LandBrush::Rocks, cx, cy, _brush, _wetness, _random, _sites); break;
    case kFarm:
    {
        int col, row;
        FarmAt(cx, cy, col, row);
        if (!PlaceFarm(_land, _sites, col, row, _wetness, why))
            _note = Tr("The farm and the town cannot overlap.");
        break;
    }
    case kTown:
        if (!PlaceTown(_land, _sites, cx / kPlotCells, cy / kPlotCells, _wetness, why))
            _note = Tr("The farm and the town cannot overlap.");
        break;
    default: break;
    }
}

void MapEditor::Stroke(int cx, int cy)
{
    // every cell on the line from the last one, so a fast drag leaves no gaps
    const int dx = cx - _lastCx, dy = cy - _lastCy, n = std::max(std::abs(dx), std::abs(dy));
    for (int i = 1; i <= n; ++i)
        Apply(_lastCx + dx * i / n, _lastCy + dy * i / n);
    _lastCx = cx;
    _lastCy = cy;
}

std::string MapEditor::HandleEvent(const SDL_Event& e, int x, int y)
{
    int cx = -1, cy = -1;
    const bool onMap = CellAt(x, y, cx, cy);
    if (e.type == SDL_KEYDOWN)
    {
        const SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_ESCAPE)
            return "cancel";
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
            return "done";
        if (k == SDLK_z && (e.key.keysym.mod & KMOD_CTRL))
            Undo();
        else if (k >= SDLK_1 && k <= SDLK_6)
            _tool = static_cast<Tool>(k - SDLK_1);
        else if (k == SDLK_LEFT) Pan(-4, 0);
        else if (k == SDLK_RIGHT) Pan(4, 0);
        else if (k == SDLK_UP) Pan(0, -4);
        else if (k == SDLK_DOWN) Pan(0, 4);
        else if (k == SDLK_PLUS || k == SDLK_EQUALS || k == SDLK_KP_PLUS) SetZoom(_zoom + 1, x, y);
        else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) SetZoom(_zoom - 1, x, y);
        return {};
    }
    if (e.type == SDL_MOUSEWHEEL)
    {
        if (e.wheel.y)
            SetZoom(_zoom + (e.wheel.y > 0 ? 1 : -1), x, y);
        return {};
    }
    if (e.type == SDL_MOUSEMOTION)
    {
        _hoverX = onMap ? cx : -1;
        _hoverY = onMap ? cy : -1;
        if (_panning)
        {
            const int px = CellPixels(_zoom, _land.world.size);
            const int mx = (_panX - x) / px, my = (_panY - y) / px;
            if (mx || my)
            {
                Pan(mx, my);
                _panX -= mx * px;
                _panY -= my * px;
            }
        }
        else if (_painting)
        {
            // a drag that leaves the map goes on along its edge
            if (!onMap)
            {
                const int px = CellPixels(_zoom, _land.world.size), shown = std::min(_land.world.size, kMapSide / px);
                CellAt(std::clamp(x, kMapX, kMapX + shown * px - 1), std::clamp(y, kMapY, kMapY + shown * px - 1), cx, cy);
            }
            if (cx == _lastCx && cy == _lastCy)
                return {};
            if (_tool == kFarm || _tool == kTown)
            {
                Apply(cx, cy); // the farm or the town follows the drag
                _lastCx = cx;
                _lastCy = cy;
            }
            else
                Stroke(cx, cy);
        }
        return {};
    }
    if (e.type == SDL_MOUSEBUTTONUP)
    {
        if (e.button.button == SDL_BUTTON_LEFT)
            _painting = false;
        else
            _panning = false;
        return {};
    }
    if (e.type != SDL_MOUSEBUTTONDOWN)
        return {};
    if (e.button.button != SDL_BUTTON_LEFT)
    {
        // the right (or middle) button drags the map
        _panning = true;
        _panX = x;
        _panY = y;
        return {};
    }
    _note.clear();
    if (onMap)
    {
        PushUndo();
        _painting = true;
        _lastCx = cx;
        _lastCy = cy;
        Apply(cx, cy);
        return {};
    }
    for (int t = 0; t < kTools; ++t)
        if (In(x, y, kButtonX, kToolY + t * kToolStep, kButtonW, kButtonH))
            _tool = static_cast<Tool>(t);
    for (int b = 0; b < 4; ++b)
        if (In(x, y, kButtonX + b * (kBrushW + 4), kBrushY, kBrushW, kButtonH))
            _brush = kBrushSizes[b];
    if (In(x, y, kButtonX, kUndoY, kButtonW, kButtonH))
        Undo();
    if (In(x, y, kButtonX, kDoneY, kButtonW, kButtonH))
        return "done";
    if (In(x, y, kButtonX, kCancelY, kButtonW, kButtonH))
        return "cancel";
    return {};
}

void MapEditor::DrawButton(int x, int y, int w, int h, const std::string& label, bool selected, int tile)
{
    _g.Fill(x, y, w, h, kBlack);
    _g.Fill(x + 1, y + 1, w - 2, h - 2, selected ? kYellow : kLightGrey);
    _g.Bevel(x + 1, y + 1, w - 2, h - 2, !selected);
    int tx = x + 4;
    if (tile >= 0)
    {
        _g.DrawTile(tile, x + 4, y + 3, 16);
        tx = x + 24;
    }
    const int room = x + w - 3 - tx;
    if (tile < 0)
        _g.TextFit(x + 2, y + (h - 8) / 2, w - 4, label, kBlack, 1);
    else
        _g.TextFit(tx, y + (h - 8) / 2, room, label, kBlack, 0);
}

void MapEditor::Draw()
{
    const int N = _land.world.size, px = CellPixels(_zoom, N), shown = std::min(N, kMapSide / px);
    // the map
    _g.Fill(kMapX - 1, kMapY - 1, shown * px + 2, shown * px + 2, kWhite);
    if (px >= 8)
        for (int x = 0; x < shown; ++x)
            for (int y = 0; y < shown; ++y)
                _g.DrawTile(_land.world.At(_camX + x, _camY + y).BaseTile(), kMapX + x * px, kMapY + y * px, px);
    else
    {
        // the whole map: a tile shrunk to 3 or 4 pixels shows only stripes of it, so each cell gets its tile's average
        // colour, as the Map window draws
        _pixels.resize(static_cast<size_t>(shown) * shown);
        for (int x = 0; x < shown; ++x)
            for (int y = 0; y < shown; ++y)
                _pixels[static_cast<size_t>(y) * shown + x] = _g.TileColour(_land.world.At(_camX + x, _camY + y).BaseTile());
        _g.DrawPixels(_pixels, shown, shown, kMapX, kMapY, shown * px, shown * px);
    }
    // a rectangle of cells in window terms, cut to the shown part; false when none of it shows
    const auto box = [&](int cx, int cy, int w, int h, int& bx, int& by, int& bw, int& bh) {
        const int x0 = std::max(cx, _camX), y0 = std::max(cy, _camY), x1 = std::min(cx + w, _camX + shown), y1 = std::min(cy + h, _camY + shown);
        if (x0 >= x1 || y0 >= y1)
            return false;
        bx = kMapX + (x0 - _camX) * px;
        by = kMapY + (y0 - _camY) * px;
        bw = (x1 - x0) * px;
        bh = (y1 - y0) * px;
        return true;
    };
    const auto outline = [&](int cx, int cy, int w, int h, int colour, const std::string& label) {
        int bx, by, bw, bh;
        if (!box(cx, cy, w, h, bx, by, bw, bh))
            return;
        _g.Frame(bx, by, bw, bh, kBlack);
        _g.Frame(bx + 1, by + 1, bw - 2, bh - 2, colour);
        _g.Frame(bx + 2, by + 2, bw - 4, bh - 4, colour);
        if (!label.empty() && bh > 10)
        {
            // the name in its corner (it may reach past a small box: the town's plot at the whole-map zoom)
            const int lw = Gfx::SmallTextWidth(label) + 3, lx = std::max(kMapX, std::min(bx + 3, kMapX + shown * px - lw));
            _g.Fill(lx, by + 3, lw, 8, kBlack);
            _g.SmallText(lx + 2, by + 4, label, colour);
        }
    };
    outline(_sites.farmCol * kPlotCells, _sites.farmRow * kPlotCells, 3 * kPlotCells, 3 * kPlotCells, kYellow, Tr("FARM"));
    outline(_sites.townCol * kPlotCells, _sites.townRow * kPlotCells, kPlotCells, kPlotCells, kRed, Tr("TOWN"));
    // where the tool would act
    if (_hoverX >= 0 && !_panning)
    {
        int bx, by, bw, bh;
        if (_tool == kFarm || _tool == kTown)
        {
            int col = _hoverX / kPlotCells, row = _hoverY / kPlotCells, side = 1;
            if (_tool == kFarm)
            {
                FarmAt(_hoverX, _hoverY, col, row);
                side = 3;
            }
            if (box(col * kPlotCells, row * kPlotCells, side * kPlotCells, side * kPlotCells, bx, by, bw, bh))
                _g.Frame(bx, by, bw, bh, kWhite);
        }
        else
        {
            const int half = (_brush - 1) / 2;
            if (box(_hoverX - half, _hoverY - half, _brush, _brush, bx, by, bw, bh))
                _g.Frame(bx, by, bw, bh, kWhite);
        }
    }

    // the panel
    _g.Fill(kPanelX, kPanelY, kPanelW, kPanelH, kBlack);
    _g.Fill(kPanelX + 1, kPanelY + 1, kPanelW - 2, kPanelH - 2, kLightGrey);
    _g.Bevel(kPanelX + 1, kPanelY + 1, kPanelW - 2, kPanelH - 2, true);
    _g.TextFit(kPanelX + 4, kPanelY + 8, kPanelW - 8, Tr("MAP EDITOR"), kNavy, 1);
    for (int t = 0; t < kTools; ++t)
        DrawButton(kButtonX, kToolY + t * kToolStep, kButtonW, kButtonH, Tr(kToolInfo[t].name), _tool == t, kToolInfo[t].tile);
    _g.TextFit(kButtonX, kBrushY - 12, kButtonW, Tr("BRUSH"), kBlack, 1);
    const bool brushTool = _tool <= kRocks;
    for (int b = 0; b < 4; ++b)
        DrawButton(kButtonX + b * (kBrushW + 4), kBrushY, kBrushW, kButtonH, std::to_string(kBrushSizes[b]), brushTool && _brush == kBrushSizes[b]);
    // what the tool does, a refusal, and the mouse help
    _g.Wrap(kPanelX, kHelpY, kPanelW - 8, _note.empty() ? Tr(kToolInfo[_tool].help) : _note, _note.empty() ? kBlack : kRed);
    _g.Wrap(kPanelX, kHelpY + 72, kPanelW - 8, Tr("Wheel: zoom. Right drag: move the map."), kDarkGrey);
    DrawButton(kButtonX, kUndoY, kButtonW, kButtonH, Tr("UNDO"), false);
    DrawButton(kButtonX, kDoneY, kButtonW, kButtonH, Tr("DONE"), false);
    DrawButton(kButtonX, kCancelY, kButtonW, kButtonH, Tr("CANCEL"), false);
}

} // namespace osf
