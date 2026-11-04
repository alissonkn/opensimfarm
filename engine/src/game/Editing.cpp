// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Editing.h"

#include "game/Machines.h"

#include <algorithm>

namespace osf {

namespace {

struct Family
{
    int base, count;
    bool Has(int id) const { return id >= base && id < base + count; }
};
// tile families
constexpr Family kGround{ 15, 16 }, kDitchEmpty{ 87, 11 }, kDitchFull{ 98, 11 }, kPaved{ 150, 11 }, kDirt{ 161, 11 },
    kFence{ 177, 11 }, kGate{ 188, 4 }, kValve{ 122, 4 }, kTrees{ 128, 19 }, kTrough{ 175, 2 }, kRocks{ 535, 4 }; // rocks: family 145 (the bulldozer tests it)
constexpr int kTreeBase = 131;   // family k62: base + (random & 15)
constexpr int kHayTile = 1020;   // masked object tile
constexpr int kSignBase = 539;

// connected-piece shape from the neighbour mask N=1 E=2 S=4 W=8
constexpr int kShape[16] = { 0, 0, 1, 2, 0, 0, 3, 4, 1, 5, 1, 6, 7, 8, 9, 10 };

int Id(const GameState& s, int x, int y) { return s.world.At(x, y).BaseTile(); }
bool InMap(int x, int y) { return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize; }
bool HasObject(const GameState& s, int x, int y) { return s.world.At(x, y).HasObject(); }
bool Owned(const GameState& s, int x, int y) { return s.PlotOwner(x / 8, y / 8) == 1; }
// buildable land: ground or a road piece (ground family or road families)
bool Buildable(int id) { return kGround.Has(id) || kDirt.Has(id) || kPaved.Has(id); }

// which connection set a tile belongs to: 1 road, 2 fence, 3 ditch, 0 none
int ConnectionSet(int id)
{
    if (kPaved.Has(id) || kDirt.Has(id))
        return 1;
    if (kFence.Has(id))
        return 2;
    if (kDitchEmpty.Has(id) || kDitchFull.Has(id))
        return 3;
    return 0;
}
bool Connects(int set, int id)
{
    switch (set)
    {
    case 1: return kPaved.Has(id) || kDirt.Has(id) || kGate.Has(id);
    case 2: return kFence.Has(id) || kGate.Has(id);
    case 3: return kDitchEmpty.Has(id) || kDitchFull.Has(id) || kValve.Has(id);
    default: return false;
    }
}

void SetBase(GameState& s, int x, int y, int id)
{
    MapCell& c = s.world.At(x, y);
    c.base = static_cast<uint16_t>((c.base & 0xF800) | (id & 0x7FF));
}

int Mask(const GameState& s, int x, int y, int set)
{
    static const int dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };
    int m = 0;
    for (int k = 0; k < 4; ++k)
        if (InMap(x + dx[k], y + dy[k]) && Connects(set, Id(s, x + dx[k], y + dy[k])))
            m |= 1 << k;
    return m;
}

void Reshape(GameState& s, int x, int y)
{
    if (!InMap(x, y))
        return;
    const int id = Id(s, x, y);
    const int set = ConnectionSet(id);
    if (set == 0)
    {
        // a gate keeps the picture it was placed with (188/189 in a vertical fence, 190/191 in a horizontal one,
        // closed/open). Valves: 122/123 horizontal open/closed, 124/125 vertical open/closed (the closed state is kept).
        if (kValve.Has(id))
        {
            const int m = Mask(s, x, y, 3);
            const bool vertical = (m & 0x5) != 0 && (m & 0xA) == 0;
            SetBase(s, x, y, kValve.base + (vertical ? 2 : 0) + ((id - kValve.base) & 1));
        }
        return;
    }
    const Family fam = kPaved.Has(id) ? kPaved : kDirt.Has(id) ? kDirt : kFence.Has(id) ? kFence : kDitchEmpty.Has(id) ? kDitchEmpty : kDitchFull;
    SetBase(s, x, y, fam.base + kShape[Mask(s, x, y, set)]);
}

EditResult Refuse(const std::string& m = std::string()) { return { false, m, "uhoh.voc" }; }

bool FieldClear(GameState& s, Simulation& sim, int x0, int y0, std::string& why)
{
    // 8x8 free, owned, buildable block with a one-cell margin that touches no other field.
    // The engine says which rule failed (the original has one message for all, user finding F29)
    why.clear();
    for (int x = x0 - 1; x <= x0 + 8 && why.empty(); ++x)
        for (int y = y0 - 1; y <= y0 + 8 && why.empty(); ++y)
        {
            const bool inside = x >= x0 && x < x0 + 8 && y >= y0 && y < y0 + 8;
            if (!InMap(x, y))
                why = "A field does not fit at the edge of the map.";
            else if (sim.FieldIndexAt(x, y) != 0)
                why = inside ? "There is already a field there." : "Fields need one free cell between them.";
            else if (inside && !Owned(s, x, y))
                why = "A field must be on your own land.";
            else if (inside && (HasObject(s, x, y) || !Buildable(Id(s, x, y))))
                why = "A field needs 8 x 8 cells of clear ground.";
        }
    return why.empty();
}

} // namespace

const ToolInfo& GetToolInfo(Tool t)
{
    static const ToolInfo info[] = {
        { "", 0, -1 },
        { "Move", 0, -1 },
        { "Examine", 0, 198 },
        { "Paved Road", 30, 151 },
        { "Dirt Road", 20, 162 },
        { "Fence", 50, 178 },
        { "Fence Gate", 75, 188 },
        { "Irrigation Ditch", 80, 99 },
        { "Ditch Valve", 35, 122 },
        { "Livestock Feed", 100, 172 },
        { "Water Trough", 15, 176 },
        { "Trees", 10, 131 },
        { "Bulldoze", 25, 199 },
        { "Plant", 0, 816 },
        { "Spray", 0, -1 },
        { "Place", 0, -1 },
    };
    return info[static_cast<int>(t)];
}

int GroundTile(const GameState& s, int wetness, int cx, int cy)
{
    return kGround.base + std::min(15, wetness + (s.world.At(cx, cy).terrain >> 2));
}

void FencePlot(GameState& s, int col, int row)
{
    auto fenceable = [](int id) { return (id >= 15 && id <= 30) || (id >= 535 && id <= 538) || (id >= 131 && id <= 146); };
    auto mine = [&](int c, int r) { return c >= 0 && r >= 0 && c < 12 && r < 12 && s.PlotOwner(c, r) == 1; };
    const int x0 = col * 8, y0 = row * 8;
    auto put = [&](int x, int y) {
        MapCell& c = s.world.At(x, y);
        if (!c.HasObject() && fenceable(c.BaseTile()))
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 177);
    };
    for (int i = 0; i < 8; ++i)
    {
        if (!mine(col, row - 1)) put(x0 + i, y0);
        if (!mine(col, row + 1)) put(x0 + i, y0 + 7);
        if (!mine(col - 1, row)) put(x0, y0 + i);
        if (!mine(col + 1, row)) put(x0 + 7, y0 + i);
    }
    for (int i = 0; i < 8; ++i)
    {
        ReshapeAround(s, x0 + i, y0);
        ReshapeAround(s, x0 + i, y0 + 7);
        ReshapeAround(s, x0, y0 + i);
        ReshapeAround(s, x0 + 7, y0 + i);
    }
}

void DrawHomestead(GameState& s, int x, int y, int tier)
{
    if (x < 0 || y < 0 || x + 3 >= kMapSize || y + 3 >= kMapSize || tier < 0 || tier > 5)
        return;
    auto set = [&](int dx, int dy, int id) { MapCell& c = s.world.At(x + dx, y + dy); c.base = static_cast<uint16_t>((c.base & 0xF800) | id); };
    // the yard first, then per tier the house (base picture b at (1,2), b+1 at (2,2), b+40 and b+41 below) and the
    // yard pieces 0x208-0x20C of the richer tiers
    set(1, 1, 79); set(2, 1, 79); set(3, 1, 79); set(3, 2, 79); set(3, 3, 79);
    static const int kHouse[6] = { 252, 272, 274, 274, 274, 274 };
    if (tier >= 3) set(1, 1, 0x208);
    if (tier >= 4) { set(2, 1, 0x209); set(3, 1, 0x20A); }
    if (tier >= 5) { set(3, 2, 0x20B); set(3, 3, 0x20C); }
    const int b = kHouse[tier];
    set(1, 2, b); set(2, 2, b + 1); set(1, 3, b + 40); set(2, 3, b + 41);
}

// ---------------------------------------------------------------- town plots
namespace {
int TownBlock(int type)
{
    if (type < 5) return type * 8;
    if (type < 10) return (type + 35) * 8; // the second row of blocks: 8 rows of 40 further on
    if (type < 15) return (type + 70) * 8;
    return -1;
}
bool InRange(int id, Family f) { return id >= f.base && id <= f.base + f.count; } // the original's test includes the end
void TownPiece(GameState& s, int x, int y, Family f)
{
    MapCell& c = s.world.At(x, y);
    c.base = static_cast<uint16_t>(c.base & ~0x3000);
    SetBase(s, x, y, f.base);
    ReshapeAround(s, x, y);
}
bool PlaneCell(const GameState& s, int x, int y)
{
    const int t = Id(s, x, y);
    return t == 0x47 || t == 0x48 || t == 0x6F || t == 0x70;
}
} // namespace

void PlaceTownPlotCells(GameState& s, const std::vector<uint16_t>& layout, int col, int row, int type)
{
    // the placement: cells under a parked plane are left alone; an airport copies its block, any other plot gets the
    // block's dirt roads (connected pieces) and bare ground (15 + the terrain's low nibble) on the rest; then every
    // cell gets the town flag 0x4000 on its overlay word
    const int off = TownBlock(type);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
        {
            const int x = col * 8 + i, y = row * 8 + j;
            MapCell& c = s.world.At(x, y);
            if (!PlaneCell(s, x, y) && off >= 0 && layout.size() == 960)
            {
                const int id = layout[static_cast<size_t>(j * 40 + off + i)];
                c.base = static_cast<uint16_t>(c.base & ~0x3000);
                if (type == 5)
                    c.base = static_cast<uint16_t>((c.base & 0xF800) | (id & 0x7FF));
                else if (InRange(id, kDirt))
                    TownPiece(s, x, y, kDirt);
                else
                    c.base = static_cast<uint16_t>(kGround.base + (c.terrain & 15));
            }
            c.overlay = static_cast<uint16_t>(c.overlay | 0x4000);
        }
}

void RedrawTownPlot(GameState& s, const std::vector<uint16_t>& layout, int col, int row, const std::function<int()>& rand)
{
    const int b = s.PlotType(col, row), type = b & 15, level = b >> 4, half = level >> 1;
    const int off = TownBlock(type);
    if (off < 0 || layout.size() != 960)
        return;
    auto lay = [&](int i, int j) { return static_cast<int>(layout[static_cast<size_t>(j * 40 + off + i)]); };
    auto put = [&](int i, int j, int id) {
        if (InMap(col * 8 + i, row * 8 + j))
            s.world.At(col * 8 + i, row * 8 + j).base = static_cast<uint16_t>(id);
    };
    // dirt roads of the block become paved road pieces in the cells i < iEnd, j0 <= j < j1
    auto pave = [&](int iEnd, int j0, int j1) {
        for (int i = 0; i < iEnd; ++i)
            for (int j = j0; j < j1; ++j)
                if (InRange(lay(i, j), kDirt))
                    TownPiece(s, col * 8 + i, row * 8 + j, kPaved);
    };
    if (type == 0 || type == 4 || type == 5)
    {
        // the home plot: its whole block with dirt road pieces; event ground and airport: the whole block with the
        // roads paved, cells under a parked plane left alone
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
            {
                if (type != 0 && PlaneCell(s, col * 8 + i, row * 8 + j))
                    continue;
                const int id = lay(i, j);
                if (InRange(id, kDirt))
                    TownPiece(s, col * 8 + i, row * 8 + j, type == 0 ? kDirt : kPaved);
                else
                    put(i, j, id);
            }
        return;
    }
    switch (level)
    {
    case 0: // bare ground
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
                put(i, j, kGround.base + (s.world.At(col * 8 + i, row * 8 + j).terrain & 15));
        return;
    case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        // every building spot of the block (an even id 252-280, its top-left cell) becomes a 2 x 2 house: one of four
        // (G2 & 3) in the style of level / 2; the spot's other cells are left to it; roads as dirt pieces
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
            {
                const int id = lay(i, j);
                if (id >= 0xFC && id <= 0x118 && !(id & 1))
                {
                    const int v = (rand() & 3) * 2 + 2 * half;
                    put(i, j, 0xFC + v); // the original writes all four cells, also past the block's edge
                    put(i + 1, j, 0xFD + v);
                    put(i, j + 1, 0x124 + v);
                    put(i + 1, j + 1, 0x125 + v);
                }
                else if ((id >= 0xFC && id <= 0x118) || (id >= 0x124 && id <= 0x140))
                    continue;
                else if (InRange(id, kDirt))
                    TownPiece(s, col * 8 + i, row * 8 + j, kDirt);
                else
                    put(i, j, id);
            }
        return;
    case 8: // the block's own buildings
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
            {
                const int id = lay(i, j);
                if (InRange(id, kDirt))
                    TownPiece(s, col * 8 + i, row * 8 + j, kDirt);
                else
                    put(i, j, id);
            }
        return;
    case 9: pave(4, 0, 4); return;            // the roads get paved quarter by quarter
    case 10: pave(8, 0, 4); return;
    case 11: pave(8, 0, 4); pave(4, 4, 8); return;
    case 12: pave(8, 0, 8); return;
    default: return;                          // level 13 draws nothing new
    }
}

void ReshapeAround(GameState& s, int cx, int cy)
{
    Reshape(s, cx, cy);
    Reshape(s, cx, cy - 1);
    Reshape(s, cx + 1, cy);
    Reshape(s, cx, cy + 1);
    Reshape(s, cx - 1, cy);
}

EditResult ApplyTool(GameState& s, Simulation& sim, Tool tool, int cx, int cy, int param)
{
    if (!InMap(cx, cy))
        return Refuse();
    const ToolInfo& info = GetToolInfo(tool);
    const int id = Id(s, cx, cy);

    if (tool == Tool::Spray)
    {
        const int fi = sim.FieldIndexAt(cx, cy);
        if (fi == 0)
            return Refuse("Click on a field to spray it.");
        Field f = s.FieldAt(fi);
        std::string why;
        if (!sim.StartSpray(f, param, why))
            return Refuse(why);
        // the tool has taken the chemical already (the job starts at its dispatch step) and the operation is written
        // into this week's cell of the field's timeline (confirmed against the original's save, 2026-10-31)
        f.SetSub(1);
        s.SetScheduleCode(s.cal.AbsWeek(), fi, 0x393 - (param - 6));
        return { true, "The job is scheduled.", "ploptool.voc" };
    }
    if (tool == Tool::Plant)
    {
        if (const int fi = sim.FieldIndexAt(cx, cy))
        {
            // planting on an existing, idle field
            Field f = s.FieldAt(fi);
            if (f.State() != 0)
                return Refuse("The field is in use.");
            std::string why;
            if (!sim.StartPlanting(f, param, why))
                return Refuse(why);
            sim.StampPlant(fi, param & 15, s.cal.AbsWeek()); // the crop's plan, with its harvest week
            return { true, "Planting " + s.crops[param & 15].name + ".", "ploptool.voc" };
        }
        if (std::string why; !FieldClear(s, sim, cx, cy, why))
            return Refuse(why);
        int index = 0;
        for (int i = 1; i < 98 && !index; ++i)
            if (!s.FieldAt(i).InUse())
                index = i;
        if (!index)
            return Refuse("There is no room for another field.");
        // the original's field creation: the listed bytes are written, the rest of a reused record (moisture, tile
        // word, ...) keeps its old values; the soil counters come from the zone bytes of the plot under the field's
        // centre (N = A = byte 6, pests = byte 2, disease = byte 3, weeds = byte 4), B = 65, C = 80
        Field f = s.FieldAt(index);
        const size_t zb = static_cast<size_t>(((cx + 4) >> 3) * 12 + ((cy + 4) >> 3)) * 7;
        auto zone = [&](int k) { return zb + k < s.zones.size() ? s.zones[zb + k] : 0; };
        f.Set16(0x28, 0);
        f.Set8(0x08, cx); f.Set8(0x09, cy); f.Set8(0x0A, 8); f.Set8(0x0B, 8);
        f.Set8(0x0C, 0);
        f.Set16(0x04, 0);
        f.Set16(0x06, 0x180);
        f.Set16(0x10, 0);
        f.Set16(0x22, 0);
        f.Set8(0x00, 0); f.Set8(0x01, 0); f.Set8(0x02, 0); f.Set8(0x03, 0);
        f.Set8(0x1F, 0); f.Set8(0x20, 0);
        f.Set8(0x05, f.Get8(0x05) | 1);
        f.Set16(0x24, 0);
        f.Set8(0x1A, zone(6)); f.Set8(0x1B, zone(6));
        f.Set8(0x1C, 0x41); f.Set8(0x1D, 0x50); f.Set8(0x1E, 0);
        f.Set8(0x12, zone(2)); f.Set8(0x13, zone(4)); f.Set8(0x14, zone(3));
        f.Set16(0x2E, 0);
        f.Set8(0x31, 0); f.Set8(0x32, 0);
        f.Set16(0x34, 0);
        f.Set8(0x36, 0); f.Set8(0x37, 0);
        f.Set16(0x38, 500); // picking-crew cost of a hand harvest
        f.Set8(0x3A, 0); f.Set8(0x3B, 0); f.Set8(0x3C, 0);
        f.Set16(0x3E, 0);
        f.Set8(0x40, 0xFF); f.Set8(0x41, 0xFF);
        std::string why;
        if (!sim.StartPlanting(f, param, why))
        {
            f.Set8(0x05, 0);
            return Refuse(why);
        }
        s.fieldCount++;
        // the cells become bare soil (flags kept) and carry the field-cell flag; the sign shows the crop. The original
        // writes no schedule plan for a field made with the PLANT tool.
        for (int x = cx; x < cx + 8; ++x)
            for (int y = cy; y < cy + 8; ++y)
            {
                MapCell& m = s.world.At(x, y);
                m.base = static_cast<uint16_t>((m.base & 0xF824) | 0x24);
                m.overlay |= 0x1000;
            }
        for (int k = 0; k < 4; ++k)
            SetBase(s, cx + (k & 1), cy + (k >> 1), kSignBase + 4 * (param & 15) + k);
        sim.StampPlant(index, param & 15, s.cal.AbsWeek()); // the crop's plan from this week on
        return { true, "New field: planting " + s.crops[param & 15].name + ".", "ploptool.voc" };
    }
    if (tool == Tool::Move || tool == Tool::Examine || tool == Tool::None)
        return {};

    if (!Owned(s, cx, cy))
        return Refuse("You do not own this land.");

    if (tool == Tool::Bulldoze)
    {
        MapCell& c = s.world.At(cx, cy);
        if (const int fi = sim.FieldIndexAt(cx, cy))
        {
            // the original asks "destroy this field?"; YES costs 25 and deletes it (param 1 = already answered)
            if (param == 1)
            {
                if (!sim.Spend(-1, 25))
                    return Refuse("Not enough money.");
                sim.DeleteField(fi);
                return { true, "", "bulldoze.voc" };
            }
            sim.AskBulldozeField(fi);
            return { true, "", "" };
        }
        if (const int st = sim.StructureAt(cx, cy))
        {
            // a building is sold at its catalogue price and its ground restored
            const int64_t value = sim.RemoveStructure(st);
            return { true, "The building was sold for " + std::to_string(value) + ".", "bulldoze.voc" };
        }
        if (c.HasObject())
        {
            const int o = c.overlay & 0x7FF;
            if (o >= 848 && o <= 945)
            {
                // a parked machine is sold at its value
                for (int i = 1; i < 127; ++i)
                {
                    Machine m(s.machines, i);
                    if (m.InUse() && m.Placed() && m.X() == cx && m.Y() == cy && !(m.Flags() & 0x0B) && m.Type() < 8)
                    {
                        const int64_t value = s.CatalogPrice(m.Type()) >> (m.Wear() >> 6);
                        const int type = m.Type();
                        sim.Fleet().Remove(i);
                        s.scalars["asset_machinery"] = std::max<int64_t>(0, s.scalars["asset_machinery"] - s.CatalogPrice(type));
                        s.SetCatalogOwn(type, std::max(0, s.CatalogOwn(type) - 1));
                        sim.Credit(type, value);
                        return { true, "The machine was sold for " + std::to_string(value) + ".", "bulldoze.voc" };
                    }
                }
                return Refuse("That machine is busy.");
            }
            if (o < kHayTile || o > kHayTile + 4)
                return Refuse("That cannot be bulldozed.");
        }
        else if (!(kPaved.Has(id) || kDirt.Has(id) || kFence.Has(id) || kGate.Has(id) || kDitchEmpty.Has(id) || kDitchFull.Has(id) ||
                   kValve.Has(id) || kTrees.Has(id) || kTrough.Has(id) || kRocks.Has(id) ||
                   // also the road bridges (family 128), the broken fence pieces 192 / 193 and the tiles 73 / 74, 113 / 114
                   // (seen live: 192, 193, 73, 113 and 172 each went for 25; user finding F33)
                   id == 172 || id == 173 || id == 192 || id == 193 || id == 73 || id == 74 || id == 113 || id == 114))
            return Refuse();
        if (!sim.Spend(-1, info.cost))
            return Refuse("Not enough money.");
        if (const int it = sim.ItemAt(cx, cy, 300))
            sim.RemoveItem(it);
        if (const int it = sim.ItemAt(cx, cy, 299))
            sim.RemoveItem(it);
        sim.Puff(cx, cy);
        if (c.HasObject())
        {
            c.base &= static_cast<uint16_t>(~0x800);
            c.overlay &= 0xF800;
        }
        else if ((c.base & 0x1000) && (c.base & 0x2000))
        {
            // a cell over water (a bridge on a river) gets the water back (water tile 39 + 16, flags 0x3000)
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 0x3000 | 55);
            ReshapeAround(s, cx, cy);
        }
        else
        {
            SetBase(s, cx, cy, GroundTile(s, sim.SoilWetness(), cx, cy));
            ReshapeAround(s, cx, cy);
        }
        return { true, "", "bulldoze.voc" };
    }

    // placement: what the target cell must hold
    bool fits = false;
    switch (tool)
    {
    case Tool::Gate: fits = id == kFence.base || id == kFence.base + 1; break; // only a straight fence piece
    case Tool::Valve: fits = kDitchEmpty.Has(id) || kDitchFull.Has(id); break;
    case Tool::PavedRoad: fits = kGround.Has(id) || kDirt.Has(id); break;   // paving a dirt road is allowed
    case Tool::DirtRoad: fits = kGround.Has(id); break;
    default: fits = Buildable(id); break;
    }
    if (!fits && !HasObject(s, cx, cy) && s.scalars["opt_autodoze"] && tool != Tool::Gate && tool != Tool::Valve && AutoDozable(id))
    {
        // AutoDoze: clear a rock or tree first at the bulldoze price; roads, fences and ditches stay
        if (!sim.Spend(-1, GetToolInfo(Tool::Bulldoze).cost))
            return Refuse("Not enough money.");
        SetBase(s, cx, cy, GroundTile(s, sim.SoilWetness(), cx, cy));
        ReshapeAround(s, cx, cy);
        fits = true;
    }
    if (!fits || HasObject(s, cx, cy))
        return Refuse();
    if (!sim.Spend(-1, info.cost))
        return Refuse("Not enough money.");

    switch (tool)
    {
    case Tool::PavedRoad: SetBase(s, cx, cy, kPaved.base); break;
    case Tool::DirtRoad: SetBase(s, cx, cy, kDirt.base); break;
    case Tool::Fence: SetBase(s, cx, cy, kFence.base); break;
    case Tool::Gate: SetBase(s, cx, cy, kGate.base + (id == kFence.base + 1 ? 2 : 0)); break; // 177 -> 188, 178 -> 190
    case Tool::Ditch: SetBase(s, cx, cy, kDitchEmpty.base); break;
    case Tool::Valve: SetBase(s, cx, cy, kValve.base); break;
    case Tool::Trough:
        SetBase(s, cx, cy, kTrough.base + 1);
        sim.NewItem(299, 256, cx, cy); // a full trough, refilled every week
        break;
    case Tool::Trees: SetBase(s, cx, cy, kTreeBase + static_cast<int>(sim.Random()() & 15)); break;
    case Tool::Hay:
    {
        MapCell& c = s.world.At(cx, cy);
        c.base |= 0x800;
        c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | kHayTile);
        sim.NewItem(300, 512, cx, cy);
        break;
    }
    default: break;
    }
    ReshapeAround(s, cx, cy);
    return { true, "", tool == Tool::Trees || tool == Tool::Hay || tool == Tool::Trough ? "plopitem.voc" : "ploptool.voc" };
}

bool FieldPositionOk(GameState& s, Simulation& sim, int x, int y, std::string& why) { return FieldClear(s, sim, x, y, why); }

bool AutoDozable(int id) { return (id >= 535 && id <= 538) || (id >= 131 && id <= 146); }

} // namespace osf
