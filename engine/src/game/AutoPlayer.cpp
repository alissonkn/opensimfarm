// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/AutoPlayer.h"

#include "game/Editing.h"
#include "game/Machines.h"
#include "game/Shop.h"

#include <algorithm>
#include <climits>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <vector>

namespace osf {

namespace {
constexpr int kYard = 10;    // the farmhouse yard (cells from the farm's corner): no field, the machines park there
constexpr int kPitch = 9;    // the field grid: 8 cells of field and the one free cell the game asks between fields
constexpr int kFieldCells = 8;
constexpr int kMaxFields = 97;
constexpr int kAnyFreePlot = -2; // SpotOk: every free plot counts as yours
// machine types and their catalogue records (the same numbers)
constexpr int kSet[] = { 0, 1, 2, 3, 4, 5, 7 }; // tractor, plow, planter, sprayer, harvester, trailer, truck
bool IsGround(int id) { return id >= 15 && id <= 30; }
bool IsTreeOrRock(int id) { return (id >= 131 && id <= 146) || (id >= 535 && id <= 538); }
bool IsFence(int id) { return id >= 177 && id <= 191; }
bool IsRoad(int id) { return (id >= 150 && id <= 171); }
} // namespace

AutoPlayer::AutoPlayer(GameState& s, Simulation& sim, std::string checkpointDir) : _s(s), _sim(sim), _dir(std::move(checkpointDir))
{
    // the farm's first plot: from the new-world scalars, else the first plot owned
    _ox = static_cast<int>(_s.scalars["farm_plot_x"]) * 8;
    _oy = static_cast<int>(_s.scalars["farm_plot_y"]) * 8;
    if (_s.PlotOwner(_ox / 8, _oy / 8) != 1)
        for (int c = 0; c < _s.Plots(); ++c)
            for (int r = 0; r < _s.Plots(); ++r)
                if (_s.PlotOwner(c, r) == 1 && (_s.PlotOwner(_ox / 8, _oy / 8) != 1))
                {
                    _ox = c * 8;
                    _oy = r * 8;
                }
    // a player would turn these on: seed bought when a field is planted, a machine leased while its own is busy
    _s.scalars["opt_autobuy"] = 1;
    _s.scalars["opt_autodoze"] = 1;
    _s.scalars["menu_6"] = 1;
    _lastYear = _s.cal.year;
    // the grid starts just past the yard
    _gx = (_ox + kYard + 1) % kPitch;
    _gy = (_oy + 1) % kPitch;
    if (_gx == 0) _gx = kPitch;
    if (_gy == 0) _gy = kPitch;
}

void AutoPlayer::Note(const std::string& m)
{
    if (_notes.size() < 64)
        _notes.push_back("Autoplayer: " + m);
}

bool AutoPlayer::TakeNote(std::string& out)
{
    if (_notes.empty())
        return false;
    out = _notes.front();
    _notes.pop_front();
    return true;
}

int AutoPlayer::Fields() const
{
    int n = 0;
    for (int i = 1; i < 98; ++i)
        if (const_cast<GameState&>(_s).FieldAt(i).InUse())
            ++n;
    return n;
}

bool AutoPlayer::Owned(int x, int y) const { return _s.world.In(x, y) && _s.PlotOwner(x / 8, y / 8) == 1; }

bool AutoPlayer::FarmhouseArea(int x, int y) const { return x >= _ox && x < _ox + kYard && y >= _oy && y < _oy + kYard; }

int64_t AutoPlayer::Reserve() const { return _sim.EstimatedTax() + 3000; }

void AutoPlayer::Tick()
{
    const Calendar& c = _s.cal;
    if (c.day == _lastDay && c.week == _lastWeek)
        return;
    _lastDay = c.day;
    if (c.year != _lastYear)
    {
        _lastYear = c.year;
        Checkpoint();
    }
    if (c.month != _lastMonth)
    {
        _lastMonth = c.month;
        _boughtLandThisMonth = false;
    }
    if (c.week != _lastWeek)
    {
        _lastWeek = c.week;
        Week();
    }
}

void AutoPlayer::Week()
{
    if (_sim.Bankrupt())
        return;
    if (std::getenv("OSF_APMONEY") && _s.cal.month == 11 && _s.cal.week == 3)
    {
        std::printf("money %d:", _s.cal.year);
        for (const char* line : { "rev_crops", "rev_machinery", "rev_land", "exp_seed", "exp_machinery", "exp_land", "exp_chemicals", "exp_interest",
                                  "exp_leases", "exp_misc", "exp_taxes" })
            std::printf(" %s %lld", line, static_cast<long long>(_s.scalars[line]));
        std::printf("\n  machines (type owned worn leased):");
        for (int type : kSet)
        {
            int own = 0, worn = 0, leased = 0;
            for (int i = 1; i < 127; ++i)
            {
                Machine m(_s.machines, i);
                if (!m.InUse() || m.Type() != type)
                    continue;
                if (m.Leased()) ++leased; else ++own;
                if ((m.Wear() & 0xC0) == 0xC0) ++worn;
            }
            std::printf(" %d:%d/%d/%d", type, own, worn, leased);
        }
        std::printf("\n");
    }
    BuyMachines();
    OpenInnerFences();
    Replant();
    Spray();
    if (!NewField() && !_boughtLandThisMonth && !_freeSpot && BuyLand())
        _boughtLandThisMonth = true;
}

std::vector<std::pair<int, int>> AutoPlayer::ParkingCells() const
{
    // free parking cells: in the farmhouse yard, then in the parking lots (grid places kept free of fields), every
    // other column and with a free cell under each, so every machine can drive out
    std::vector<std::pair<int, int>> cells, out;
    for (int x = _ox + 4; x < _ox + kYard - 1; x += 2)
        for (int y = _oy + 1; y < _oy + kYard - 2; y += 3)
            cells.emplace_back(x, y);
    for (const auto& [lx, ly] : _lots)
        for (int x = lx; x < lx + kFieldCells; x += 2)
            for (int y = ly; y < ly + kFieldCells - 1; y += 3)
                cells.emplace_back(x, y);
    for (const auto& [x, y] : cells)
        if (Owned(x, y) && Owned(x, y + 1) && IsGround(_s.world.At(x, y).BaseTile()) && !_s.world.At(x, y).HasObject() &&
            IsGround(_s.world.At(x, y + 1).BaseTile()) && !_s.world.At(x, y + 1).HasObject())
            out.emplace_back(x, y);
    return out;
}

bool AutoPlayer::MakeLot()
{
    // a new parking lot on the nearest free grid place
    const std::vector<char> fields = FieldMask();
    const int n = _s.world.size;
    int bx = -1, by = -1, bd = INT_MAX;
    for (int x = _gx; x + kFieldCells < n; x += kPitch)
        for (int y = _gy; y + kFieldCells < n; y += kPitch)
        {
            int obstacles = 0;
            const int d = (x - _ox) * (x - _ox) + (y - _oy) * (y - _oy);
            if (d < bd && SpotOk(x, y, fields, obstacles))
            {
                bd = d;
                bx = x;
                by = y;
            }
        }
    if (bx < 0)
        return false;
    for (int cx = bx; cx < bx + kFieldCells; ++cx)
        for (int cy = by; cy < by + kFieldCells; ++cy)
            if (IsTreeOrRock(_s.world.At(cx, cy).BaseTile()) || IsFence(_s.world.At(cx, cy).BaseTile()))
                ApplyTool(_s, _sim, Tool::Bulldoze, cx, cy, 0);
    _lots.emplace_back(bx, by);
    Note("set aside a parking place for machines.");
    RoadTo(bx, by);
    return true;
}

int AutoPlayer::MachinesWanted(int fields) const { return static_cast<int>(std::size(kSet)) * std::max(1, (fields + 2) / 3); }

int AutoPlayer::MachinesOwned() const
{
    int n = 0;
    for (int i = 1; i < 127; ++i)
    {
        Machine m(const_cast<GameState&>(_s).machines, i);
        if (m.InUse() && !m.Leased())
            ++n;
    }
    return n;
}

bool AutoPlayer::Park(int type)
{
    for (const auto& [x, y] : ParkingCells())
    {
        const EditResult r = ShopBuy(_s, _sim, type, x, y);
        if (r.ok)
            return true;
        if (std::getenv("OSF_APDEBUG"))
            std::printf("park %d at %d,%d refused: %s\n", type, x, y, r.message.c_str());
    }
    return MakeLot() && Park(type);
}

void AutoPlayer::BuyMachines()
{
    // a worn-out machine no job can use is sold (SELL window) and frees its parking cell
    for (const SaleItem& it : SaleList(_s, _sim, 1))
    {
        Machine m(_s.machines, it.index);
        if (m.InUse() && !m.Leased() && (m.Wear() & 0xC0) == 0xC0 && !(m.Flags() & (0x01 | 0x08)) && ShopSell(_s, _sim, it).ok)
            Note("sold a worn-out " + ShopName(_s, m.Type()) + ".");
    }
    // one set of machines for every three fields: leasing for every job costs half a machine each time
    const int want = std::max(1, (Fields() + 2) / 3);
    for (int type : kSet)
    {
        int have = 0;
        for (int i = 1; i < 127; ++i)
        {
            Machine m(_s.machines, i);
            if (m.InUse() && !m.Leased() && m.Type() == type && (m.Wear() & 0xC0) != 0xC0)
                ++have;
        }
        for (; have < want && _s.cash >= Reserve() + _s.CatalogPrice(type); ++have)
        {
            if (!Park(type))
                break;
            Note("bought a " + ShopName(_s, type) + ".");
        }
    }
}

void AutoPlayer::OpenInnerFences()
{
    // a fence with your own land on every side is taken away, so the machines can cross between your plots
    for (int x = 1; x < _s.world.size - 1; ++x)
        for (int y = 1; y < _s.world.size - 1; ++y)
        {
            if (!IsFence(_s.world.At(x, y).BaseTile()) || _s.world.At(x, y).HasObject())
                continue;
            bool inner = true;
            for (int dx = -1; dx <= 1 && inner; ++dx)
                for (int dy = -1; dy <= 1 && inner; ++dy)
                    inner = Owned(x + dx, y + dy);
            if (inner && _s.cash > Reserve())
                ApplyTool(_s, _sim, Tool::Bulldoze, x, y, 0);
        }
}

void AutoPlayer::OneHarvest(int field)
{
    // the plan the PLANT tool writes repeats the crop to the end of the timeline; like a player editing the plan,
    // the weeks after the first harvest are emptied, so the field comes free and gets a crop that suits that season
    Field f = _s.FieldAt(field);
    const CropDef& d = _s.crops[static_cast<size_t>(f.Crop() & 15)];
    if (d.Perennial())
        return;
    for (int r = _s.cal.AbsWeek() + d.Weeks() + 2; r < 240; ++r)
        _s.SetScheduleCode(r, field, 0);
}

double AutoPlayer::Outlook(int crop) const
{
    // what share of a full harvest the crop can make if planted this week, from the weather table: the heat the
    // crop misses over its weeks delays it (the growth step's rule), a crop not ripe by its harvest week is worth
    // less, and nothing below an eighth; too little rain over the season rules it out
    const CropDef& d = _s.crops[static_cast<size_t>(crop)];
    const int weeks = std::max(1, d.Weeks());
    const int first = _sim.RainProfileRow() + _s.cal.AbsWeek();
    int deficit = 0, rain = 0;
    for (int k = 0; k < weeks; ++k)
    {
        const int idx = (first + k) % 240;
        rain += _s.WeatherRain(idx);
        if (d.HeatNeed() == 0)
            continue;
        int heat = 7 * std::max(0, _s.WeatherTemp(idx) - d.BaseTemp());
        if (deficit >= heat) { deficit -= heat; heat = 0; }
        else { heat -= deficit; deficit = 0; }
        if (d.HeatNeed() > heat)
            deficit += d.HeatNeed() - heat;
    }
    if (d.WaterMin() > rain * 8 / weeks)
        return 0; // the 16-week rain rule of the game's own crop advice, over this crop's weeks
    const int delay = d.HeatNeed() ? deficit / (d.HeatNeed() + 2) : 0;
    const double ripe = std::min(1.0, static_cast<double>(weeks - delay) / weeks);
    if (ripe < 0.125)
        return 0;
    return std::max(0.0, ripe - 0.25); // a usual season's stress takes about a quarter
}

int AutoPlayer::BestCrop(int avoid) const
{
    // the best value per week: a harvest at today's price times its outlook, less the seed and, for a hand-picked
    // crop, the pickers; perennials and orchards are left out (years to the first harvest)
    int best = -1;
    double bestScore = 0;
    for (int c = 0; c < 16; ++c)
    {
        const CropDef& d = _s.crops[static_cast<size_t>(c)];
        if (!d.loaded || d.Perennial() || d.Orchard() || d.ChillNeed() || c == avoid)
            continue;
        const int64_t value = (static_cast<int64_t>(d.Yield()) * (_s.MarketPrice(c) / 10)) & 0xFFFF;
        const int64_t cost = _s.CatalogPrice(66 + c) + 20 + (d.HandPicked() ? 500 : 0);
        const double score = (value * Outlook(c) - cost) / (d.Weeks() + 3);
        if (score > bestScore)
        {
            bestScore = score;
            best = c;
        }
    }
    return best;
}

void AutoPlayer::Replant()
{
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse() || f.State() != 0)
            continue;
        int crop = BestCrop(f.Crop() & 15); // another crop than the last one: the pests, disease and weeds are cleared
        if (crop < 0)
            crop = BestCrop(-1);
        if (crop < 0 || _s.cash < Reserve() + 500)
            return;
        const EditResult r = ApplyTool(_s, _sim, Tool::Plant, f.X(), f.Y(), crop);
        if (r.ok)
            OneHarvest(i);
        if (r.ok)
            Note("planting " + CropName(_s.crops[static_cast<size_t>(crop)]) + " again.");
    }
}

void AutoPlayer::Spray()
{
    const int now = _s.cal.month * 4 + _s.cal.week + 48 * (_s.cal.year - 1900);
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse() || f.State() != 4 || _s.cash < Reserve() + 1000)
            continue;
        auto it = _sprayed.find(i);
        if (it != _sprayed.end() && now - it->second < 12)
            continue; // once in a crop's season
        int maturity = 0;
        _sim.HarvestValue(f, &maturity);
        const CropDef& d = _s.crops[static_cast<size_t>(f.Crop() & 15)];
        const int64_t full = (static_cast<int64_t>(d.Yield()) * (_s.MarketPrice(f.Crop() & 15) / 10)) & 0xFFFF;
        if (maturity >= 60 || full < 1500)
            continue; // too close to the harvest, or a crop not worth a barrel
        const int p = _sim.FieldProblems(f);
        const int job = (p & 1) ? 7 : (p & 32) ? 9 : (p & 16) ? 8 : 0; // pests, disease, weeds
        if (!job)
            continue;
        if (ApplyTool(_s, _sim, Tool::Spray, f.X(), f.Y(), job).ok)
        {
            static const char* const kJob[4] = { "fertilizing", "spraying pesticide on", "spraying herbicide on", "spraying fungicide on" };
            Note(std::string(kJob[job - 6]) + " a field of " + CropName(_s.crops[static_cast<size_t>(f.Crop() & 15)]) + ".");
            _sprayed[i] = now;
        }
    }
}

std::vector<char> AutoPlayer::FieldMask() const
{
    // 1 on every cell of a field in use
    const int n = _s.world.size;
    std::vector<char> m(static_cast<size_t>(n) * n, 0);
    for (int i = 1; i < 98; ++i)
    {
        Field f = const_cast<GameState&>(_s).FieldAt(i);
        if (!f.InUse())
            continue;
        for (int x = f.X(); x < f.X() + f.W() && x < n; ++x)
            for (int y = f.Y(); y < f.Y() + f.H() && y < n; ++y)
                m[static_cast<size_t>(x) * n + y] = 1;
    }
    return m;
}

bool AutoPlayer::SpotOk(int x, int y, const std::vector<char>& fields, int& obstacles, int plotC, int plotR) const
{
    // an 8 x 8 field at (x, y): on your land (or on plot plotC, plotR, counted as yours), clear ground once trees, rocks
    // and your own fences are bulldozed, out of the farmhouse yard, and one free cell from every other field
    const int n = _s.world.size;
    obstacles = 0;
    if (x < 1 || y < 1 || x + kFieldCells >= n || y + kFieldCells >= n)
        return false;
    for (const auto& [lx, ly] : _lots)
        if (x < lx + kFieldCells + 1 && lx < x + kFieldCells + 1 && y < ly + kFieldCells + 1 && ly < y + kFieldCells + 1)
            return false;
    for (int cx = x - 1; cx <= x + kFieldCells; ++cx)
        for (int cy = y - 1; cy <= y + kFieldCells; ++cy)
            if (fields[static_cast<size_t>(cx) * n + cy])
                return false;
    for (int cx = x; cx < x + kFieldCells; ++cx)
        for (int cy = y; cy < y + kFieldCells; ++cy)
        {
            const bool mine = Owned(cx, cy) || (cx / 8 == plotC && cy / 8 == plotR) ||
                              (plotC == kAnyFreePlot && _s.PlotOwner(cx / 8, cy / 8) == 0);
            const MapCell& c = _s.world.At(cx, cy);
            const int id = c.BaseTile();
            if (!mine || FarmhouseArea(cx, cy) || (c.base & 0x2000) || c.HasObject())
                return false;
            if (IsTreeOrRock(id) || IsFence(id))
                ++obstacles;
            else if (!IsGround(id) && !IsRoad(id))
                return false;
        }
    return true;
}

bool AutoPlayer::NewField()
{
    if (Fields() >= kMaxFields)
        return false;
    const int crop = BestCrop(-1);
    // the free grid places on your land, the nearest to the farmhouse first
    struct Spot { int x, y, cost, dist; };
    std::vector<Spot> spots;
    const int n = _s.world.size;
    const std::vector<char> fields = FieldMask();
    for (int x = _gx; x + kFieldCells < n; x += kPitch)
        for (int y = _gy; y + kFieldCells < n; y += kPitch)
        {
            int obstacles = 0;
            if (SpotOk(x, y, fields, obstacles))
                spots.push_back({ x, y, 25 * obstacles, (x - _ox) * (x - _ox) + (y - _oy) * (y - _oy) });
        }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.dist < b.dist; });
    _freeSpot = !spots.empty();
    if (std::getenv("OSF_APDEBUG") && !spots.empty())
        std::printf("%zu spots, first %d,%d cost %d, cash %lld reserve %lld\n", spots.size(), spots[0].x, spots[0].y, spots[0].cost,
                    static_cast<long long>(_s.cash), static_cast<long long>(Reserve()));
    if (crop < 0)
        return false; // nothing to plant in this season
    if (static_cast<int>(ParkingCells().size()) < MachinesWanted(Fields() + 1) - MachinesOwned())
        return MakeLot(); // room for the machines one more field will need comes first
    for (const Spot& sp : spots)
    {
        if (_s.cash < Reserve() + sp.cost + 1500)
            continue;
        for (int cx = sp.x; cx < sp.x + kFieldCells; ++cx)
            for (int cy = sp.y; cy < sp.y + kFieldCells; ++cy)
                if (IsTreeOrRock(_s.world.At(cx, cy).BaseTile()) || IsFence(_s.world.At(cx, cy).BaseTile()))
                    ApplyTool(_s, _sim, Tool::Bulldoze, cx, cy, 0);
        const EditResult r = ApplyTool(_s, _sim, Tool::Plant, sp.x, sp.y, crop);
        if (!r.ok)
        {
            if (std::getenv("OSF_APDEBUG"))
                std::printf("plant refused at %d,%d: %s\n", sp.x, sp.y, r.message.c_str());
            continue;
        }
        OneHarvest(_sim.FieldIndexAt(sp.x, sp.y));
        Note("new field of " + CropName(_s.crops[static_cast<size_t>(crop)]) + ".");
        RoadTo(sp.x, sp.y);
        return true;
    }
    return false;
}

bool AutoPlayer::RoadTo(int fx, int fy)
{
    // the cheapest way along the lanes from the farmhouse yard to the new field (cells already road cost nothing),
    // then dirt road on it
    const int n = _s.world.size;
    auto passable = [&](int x, int y) {
        if (!Owned(x, y) || _sim.FieldIndexAt(x, y))
            return false;
        const MapCell& c = _s.world.At(x, y);
        const int id = c.BaseTile();
        if ((c.base & 0x2000) || c.HasObject() || IsFence(id) || !(IsGround(id) || IsRoad(id) || IsTreeOrRock(id)))
            return false;
        return true;
    };
    std::vector<int> dist(static_cast<size_t>(n) * n, INT_MAX), from(static_cast<size_t>(n) * n, -1);
    std::deque<int> q;
    for (int x = _ox + 4; x <= _ox + 5; ++x)
        for (int y = _oy + 1; y <= _oy + 4; ++y)
            if (passable(x, y))
            {
                dist[static_cast<size_t>(x) * n + y] = 0;
                q.push_back(x * n + y);
            }
    auto target = [&](int x, int y) { return x >= fx - 1 && x <= fx + kFieldCells && y >= fy - 1 && y <= fy + kFieldCells; };
    int end = -1;
    static const int kDx[4] = { 1, -1, 0, 0 }, kDy[4] = { 0, 0, 1, -1 };
    while (!q.empty())
    {
        const int cur = q.front();
        q.pop_front();
        const int x = cur / n, y = cur % n;
        if (target(x, y))
        {
            end = cur;
            break;
        }
        for (int k = 0; k < 4; ++k)
        {
            const int nx = x + kDx[k], ny = y + kDy[k];
            if (!_s.world.In(nx, ny) || !passable(nx, ny))
                continue;
            const int w = IsRoad(_s.world.At(nx, ny).BaseTile()) ? 0 : 1;
            const size_t ni = static_cast<size_t>(nx) * n + ny;
            if (dist[static_cast<size_t>(cur)] + w < dist[ni])
            {
                dist[ni] = dist[static_cast<size_t>(cur)] + w;
                from[ni] = cur;
                if (w == 0)
                    q.push_front(static_cast<int>(ni));
                else
                    q.push_back(static_cast<int>(ni));
            }
        }
    }
    if (end < 0)
        return false;
    int laid = 0;
    for (int c = end; c >= 0; c = from[static_cast<size_t>(c)])
    {
        const int x = c / n, y = c % n;
        if (!IsRoad(_s.world.At(x, y).BaseTile()) && _s.cash > Reserve() + 100 && ApplyTool(_s, _sim, Tool::DirtRoad, x, y, 0).ok)
            ++laid;
    }
    if (laid)
        Note("laid " + std::to_string(laid) + " cells of dirt road.");
    return true;
}

bool AutoPlayer::BuyLand()
{
    // the nearest grid place next to your land that the free plots under it would make usable: all of them are
    // bought together when the money allows
    const std::vector<char> fields = FieldMask();
    const int n = _s.world.size;
    int64_t bestCost = 0;
    double bestScore = 0;
    std::vector<std::pair<int, int>> bestPlots;
    for (int x = _gx; x + kFieldCells < n; x += kPitch)
        for (int y = _gy; y + kFieldCells < n; y += kPitch)
        {
            std::vector<std::pair<int, int>> plots;
            bool ok = true, touches = false;
            for (int c = x / 8; c <= (x + kFieldCells - 1) / 8 && ok; ++c)
                for (int r = y / 8; r <= (y + kFieldCells - 1) / 8 && ok; ++r)
                {
                    const int owner = _s.PlotOwner(c, r);
                    if (owner == 1)
                        touches = true;
                    else if (owner != 0)
                        ok = false; // the town's
                    else
                    {
                        plots.emplace_back(c, r);
                        for (const auto& [nc, nr] : { std::pair{ c - 1, r }, std::pair{ c + 1, r }, std::pair{ c, r - 1 }, std::pair{ c, r + 1 } })
                            if (nc >= 0 && nr >= 0 && nc < _s.Plots() && nr < _s.Plots() && _s.PlotOwner(nc, nr) == 1)
                                touches = true;
                    }
                }
            if (!ok || plots.empty() || !touches)
                continue;
            int obstacles = 0;
            if (!SpotOk(x, y, fields, obstacles, kAnyFreePlot, kAnyFreePlot))
                continue;
            int64_t cost = 25 * obstacles;
            for (const auto& [c, r] : plots)
                cost += _sim.PlotLandValue(c, r);
            const int dx = x - _ox, dy = y - _oy;
            const double score = 1e9 / static_cast<double>(cost) / (1 + 0.002 * (dx * dx + dy * dy));
            if (score > bestScore)
            {
                bestScore = score;
                bestCost = cost;
                bestPlots = plots;
            }
        }
    if (bestPlots.empty() || _s.cash - bestCost < Reserve() + 3000)
        return false;
    for (const auto& [c, r] : bestPlots)
    {
        const int64_t price = _sim.PlotLandValue(c, r);
        std::string why;
        if (_sim.BuyPlot(c, r, why))
            Note("bought the plot at " + std::to_string(c) + "," + std::to_string(r) + " for " + std::to_string(price) + ".");
    }
    return true;
}

bool AutoPlayer::Answer(const GameEvent& ev)
{
    if (ev.id == kGameOverEvent || !ev.faces.empty())
        return false; // the lost farm and the interface's own boxes stay with the player
    if (ev.id == kTownVoteEvent)
    {
        // the town's choice in turn: homes, shops, homes, parks
        static const int kOrder[4] = { 0, 1, 0, 3 };
        const int vote = kOrder[_s.cal.year % 4];
        if (!_sim.TownVote(vote))
            _sim.TownVote(0);
        Note(std::string("voted for ") + (vote == 0 ? "homes" : vote == 1 ? "shops" : "parks") + " in the town.");
        return true;
    }
    if (ev.question)
    {
        _sim.Answer(ev.id, false); // no bets, no contests
        return true;
    }
    if (!ev.text.empty())
        _notes.push_back(ev.text);
    return true;
}

void AutoPlayer::Checkpoint()
{
    if (_dir.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(_dir, ec);
    const std::string path = _dir + "/AP" + std::to_string(_s.cal.year) + ".SFM";
    std::string err;
    if (_s.Save(path, err))
    {
        int plots = 0;
        for (int c = 0; c < _s.Plots(); ++c)
            for (int r = 0; r < _s.Plots(); ++r)
                plots += _s.PlotOwner(c, r) == 1;
        char buf[160];
        std::snprintf(buf, sizeof buf, "year %d: %d fields, %d plots, cash %lld, saved %s", _s.cal.year, Fields(), plots,
                      static_cast<long long>(_s.cash), path.c_str());
        Note(buf);
    }
    else
        Note("checkpoint not saved: " + err);
}

} // namespace osf
