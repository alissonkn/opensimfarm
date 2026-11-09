// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/MapEdit.h"

#include "assets/Megadata.h"
#include "core/Paths.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace osf {

namespace {
uint32_t Next(uint32_t& r)
{
    r = r * 1664525u + 1013904223u;
    return r >> 16;
}

// a cell that was water becomes land: its height is the mean of its dry neighbours' (4 when there are none)
void Dry(World& w, int x, int y, int wetness)
{
    MapCell& c = w.At(x, y);
    if (!IsWater(c))
        return;
    int sum = 0, n = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if ((dx || dy) && w.In(x + dx, y + dy) && !IsWater(w.At(x + dx, y + dy)))
            {
                sum += w.At(x + dx, y + dy).terrain;
                n++;
            }
    c.terrain = static_cast<uint8_t>(n ? sum / n : 4);
    c.base = static_cast<uint16_t>(GroundTile(c.terrain, wetness));
}

bool Overlap(int c0, int r0, int w0, int c1, int r1, int w1)
{
    return c0 < c1 + w1 && c1 < c0 + w0 && r0 < r1 + w1 && r1 < r0 + w0;
}

// water in a block of cells becomes ground, and the shores around it are redrawn
void DryCells(GameState& s, int x0, int y0, int w, int h, int wetness, uint32_t& random)
{
    for (int x = x0; x < x0 + w; ++x)
        for (int y = y0; y < y0 + h; ++y)
            if (s.world.In(x, y))
                Dry(s.world, x, y, wetness);
    Reshore(s.world, x0 - 1, y0 - 1, x0 + w, y0 + h, [&] { return static_cast<int>(Next(random) & 1); });
}
} // namespace

int GroundTile(int terrain, int wetness)
{
    // as the generator's scatter: base 15, the shade term, a quarter of the height
    return 15 + std::clamp(wetness, 0, 12) + (std::clamp(terrain, 0, 15) >> 2);
}

bool InFarmhouse(const WorldSites& s, int x, int y)
{
    return x >= s.farmCol * kPlotCells && x < s.farmCol * kPlotCells + kFarmhouseCells && y >= s.farmRow * kPlotCells &&
           y < s.farmRow * kPlotCells + kFarmhouseCells;
}

bool InFarm(const WorldSites& s, int x, int y)
{
    return x >= s.farmCol * kPlotCells && x < (s.farmCol + 3) * kPlotCells && y >= s.farmRow * kPlotCells && y < (s.farmRow + 3) * kPlotCells;
}

bool InTown(const WorldSites& s, int x, int y)
{
    return x >= s.townCol * kPlotCells && x < (s.townCol + 1) * kPlotCells && y >= s.townRow * kPlotCells && y < (s.townRow + 1) * kPlotCells;
}

int PaintLand(GameState& s, LandBrush brush, int cx, int cy, int size, int wetness, uint32_t& random, const WorldSites& sites)
{
    World& w = s.world;
    const int half = std::max(0, size - 1) / 2;
    int changed = 0;
    for (int x = cx - half; x <= cx + half; ++x)
        for (int y = cy - half; y <= cy + half; ++y)
        {
            if (!w.In(x, y))
                continue;
            MapCell& c = w.At(x, y);
            const MapCell before = c;
            switch (brush)
            {
            case LandBrush::Water:
                if (!InFarmhouse(sites, x, y) && !InTown(sites, x, y) && !IsWater(c))
                    SetWater(c);
                break;
            case LandBrush::Ground:
                Dry(w, x, y, wetness);
                c.base = static_cast<uint16_t>(GroundTile(c.terrain, wetness));
                break;
            case LandBrush::Trees:
                Dry(w, x, y, wetness);
                // the generator's trees: tiles 141..146 (131 + a height of 10..15)
                if (c.BaseTile() < 141 || c.BaseTile() > 146)
                    c.base = static_cast<uint16_t>(141 + Next(random) % 6);
                break;
            case LandBrush::Rocks:
                Dry(w, x, y, wetness);
                if (c.BaseTile() < 535 || c.BaseTile() > 538)
                    c.base = static_cast<uint16_t>(535 + (Next(random) & 3));
                break;
            }
            changed += c.base != before.base || c.terrain != before.terrain;
        }
    if (changed)
        Reshore(w, cx - half - 1, cy - half - 1, cx + half + 1, cy + half + 1, [&] { return static_cast<int>(Next(random) & 1); });
    return changed;
}

bool PlaceFarm(GameState& s, WorldSites& sites, int col, int row, int wetness, std::string& why)
{
    const int P = s.Plots();
    col = std::clamp(col, 0, P - 3);
    row = std::clamp(row, 0, P - 3);
    if (Overlap(col, row, 3, sites.townCol, sites.townRow, 1))
    {
        why = "overlap";
        return false;
    }
    sites.farmCol = col;
    sites.farmRow = row;
    uint32_t r = static_cast<uint32_t>(col * 131 + row * 7 + 1);
    DryCells(s, col * kPlotCells, row * kPlotCells, kFarmhouseCells, kFarmhouseCells, wetness, r);
    why.clear();
    return true;
}

bool PlaceTown(GameState& s, WorldSites& sites, int col, int row, int wetness, std::string& why)
{
    const int P = s.Plots();
    col = std::clamp(col, 0, P - 1);
    row = std::clamp(row, 0, P - 1);
    if (Overlap(col, row, 1, sites.farmCol, sites.farmRow, 3))
    {
        why = "overlap";
        return false;
    }
    sites.townCol = col;
    sites.townRow = row;
    uint32_t r = static_cast<uint32_t>(col * 131 + row * 7 + 3);
    DryCells(s, col * kPlotCells, row * kPlotCells, kPlotCells, kPlotCells, wetness, r);
    why.clear();
    return true;
}

int MapEditSelfTest(const std::string& gameDir)
{
    Megadata mega;
    std::string err;
    GameState tmpl;
    if (!mega.Open(gameDir, err) || !tmpl.Load(gameDir, FindFileNoCase(gameDir, "SCEN0.SSM").value_or(""), err))
    {
        std::printf("FAIL the game files: %s\n", err.c_str());
        return 1;
    }
    int failures = 0;
    auto expect = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        failures += !ok;
    };
    for (int n : { kOriginalMapSize, kLargeMapSize })
    {
        std::printf("-- %d x %d\n", n, n);
        WorldOptions opt;
        opt.seed = 99;
        opt.mapSize = n;
        GameState land, whole;
        WorldSites sites;
        expect(GenerateTerrain(mega, tmpl, opt, land, sites, err), "the land of a new world " + err);
        {
            // the two steps make the same world as the one-step generator
            GameState two = land, one;
            FinishWorld(gameDir, mega, two, sites);
            GenerateWorld(gameDir, mega, tmpl, opt, one, err);
            bool same = one.world.size == two.world.size && one.zones == two.zones;
            for (size_t i = 0; same && i < one.world.cells.size(); ++i)
                same = one.world.cells[i].base == two.world.cells[i].base && one.world.cells[i].overlay == two.world.cells[i].overlay;
            expect(same, "terrain + finish = the generator's world");
        }
        const int P = land.Plots(), wet = 2;
        uint32_t rnd = 5;
        // the farm into the far corner, the town beside it
        std::string why;
        expect(!PlaceFarm(land, sites, sites.townCol, sites.townRow, wet, why), "the farm cannot cover the town");
        expect(PlaceTown(land, sites, 0, 0, wet, why) && PlaceFarm(land, sites, P - 3, P - 3, wet, why) && sites.farmCol == P - 3,
               "the town to the first plot, the farm to the last corner");
        expect(PlaceFarm(land, sites, P + 5, -2, wet, why) && sites.farmCol == P - 3 && sites.farmRow == 0, "a farm past the edge stays on the map");
        expect(PlaceFarm(land, sites, P - 3, P - 3, wet, why), "and back");
        // a lake in the middle: water, with shores
        const int mx = n / 2, my = n / 2;
        expect(PaintLand(land, LandBrush::Water, mx, my, 5, wet, rnd, sites) == 25, "a 5 x 5 lake painted");
        // (open water is one of two pictures, 55 or 56; a shore has its own)
        const int mid = land.world.At(mx, my).BaseTile(), edge = land.world.At(mx - 2, my).BaseTile();
        expect((mid == 55 || mid == 56) && edge != 55 && edge != 56 && IsWater(land.world.At(mx - 2, my)),
               "open water inside, a shore picture at its edge (" + std::to_string(edge) + ")");
        expect(PaintLand(land, LandBrush::Water, mx, my, 5, wet, rnd, sites) == 0, "painting it again changes nothing");
        // water on the farm, but not at the farmhouse corner nor on the town's plot
        const int fx = sites.farmCol * 8 + 12, fy = sites.farmRow * 8 + 12, hx = sites.farmCol * 8 + 2, hy = sites.farmRow * 8 + 2;
        PaintLand(land, LandBrush::Water, fx, fy, 3, wet, rnd, sites);
        PaintLand(land, LandBrush::Water, hx, hy, 5, wet, rnd, sites);
        PaintLand(land, LandBrush::Water, 3, 3, 3, wet, rnd, sites);
        expect(IsWater(land.world.At(fx, fy)) && !IsWater(land.world.At(hx, hy)) &&
                   !IsWater(land.world.At(hx + 2, hy + 2)) && !IsWater(land.world.At(3, 3)),
               "a pond on the farm; the farmhouse corner and the town stay dry");
        // trees and rocks, on land and on water
        PaintLand(land, LandBrush::Trees, mx, my, 1, wet, rnd, sites);
        const int tree = land.world.At(mx, my).BaseTile();
        PaintLand(land, LandBrush::Rocks, mx + 4, my, 1, wet, rnd, sites);
        const int rock = land.world.At(mx + 4, my).BaseTile();
        expect(tree >= 141 && tree <= 146 && !IsWater(land.world.At(mx, my)) && rock >= 535 && rock <= 538,
               "a tree in the lake (" + std::to_string(tree) + "), a rock beside it (" + std::to_string(rock) + ")");
        expect(IsWater(land.world.At(mx - 1, my)) && land.world.At(mx - 1, my).BaseTile() != 55 && land.world.At(mx - 1, my).BaseTile() != 56, "the water next to the tree has a shore");
        PaintLand(land, LandBrush::Ground, mx, my, 5, wet, rnd, sites);
        bool dry = true;
        for (int x = mx - 2; x <= mx + 2; ++x)
            for (int y = my - 2; y <= my + 2; ++y)
                dry = dry && !IsWater(land.world.At(x, y)) && land.world.At(x, y).BaseTile() >= 15 && land.world.At(x, y).BaseTile() <= 30;
        expect(dry, "ground painted over the lake: plain ground again");
        // the farm moved onto water: only the farmhouse corner dries
        PaintLand(land, LandBrush::Water, 12, n - 12, 5, wet, rnd, sites);
        PaintLand(land, LandBrush::Water, 2, (P - 3) * 8 + 2, 3, wet, rnd, sites);
        expect(PlaceFarm(land, sites, 0, P - 3, wet, why) && IsWater(land.world.At(12, n - 12)) && !IsWater(land.world.At(2, (P - 3) * 8 + 2)),
               "the farm moved onto water: the pond stays, the farmhouse corner dries");
        // the edited land becomes a game
        whole = land;
        FinishWorld(gameDir, mega, whole, sites);
        int owned = 0, townPlots = 0;
        for (int c = 0; c < P; ++c)
            for (int r = 0; r < P; ++r)
            {
                owned += whole.PlotOwner(c, r) == 1;
                townPlots += whole.PlotOwner(c, r) == 2;
            }
        expect(owned == 9 && whole.PlotOwner(0, P - 3) == 1 && whole.PlotOwner(0, 0) == 2 && townPlots == 1 && whole.scalars["farm_plot_x"] == 0 &&
                   whole.scalars["homestead_x"] == 0 && whole.world.At(0, (P - 3) * 8).BaseTile() == 180,
               "the game: the farm and the homestead where placed, the town on its plot");
        {
            // the pond on the farm: still water, your land, and no fence on it
            const MapCell& w = whole.world.At(12, n - 12);
            expect(IsWater(w) && (w.overlay & 0x800) && w.BaseTile() != 177 && whole.world.At(1, (P - 3) * 8 + 2).BaseTile() == 252 && !IsWater(whole.world.At(1, (P - 3) * 8 + 2)),
                   "the pond is part of the farm; the house stands on dry land");
        }
        const std::string path = (std::filesystem::temp_directory_path() / "osf_mapedit.sfm").string();
        GameState back;
        expect(whole.Save(path, err) && back.Load(gameDir, path, err) && back.world.At(mx + 4, my).BaseTile() == rock, "the edited world saves and loads");
        std::filesystem::remove(path);
    }
    return failures ? 1 : 0;
}

} // namespace osf
