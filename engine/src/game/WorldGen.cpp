// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/WorldGen.h"

#include "assets/Megadata.h"
#include "core/Paths.h"
#include "game/Editing.h"
#include "game/Crops.h"
#include "game/Rng.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

namespace osf {

namespace {
constexpr int kWater = 39 + 0x10;
bool InMap(int x, int y) { return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize; }
bool IsWater(const MapCell& c) { return (c.base & 0x2000) != 0; }

// one of the 12 climate tables: 60 lines x 4 comma-separated values = 240 weeks
bool ReadTable(const Megadata& mega, const std::string& name, std::vector<int>& out)
{
    out.clear();
    std::string text;
    for (uint8_t c : mega.Read(name))
        text += (c == ',' || c == '\r' || c == '\n') ? ' ' : static_cast<char>(c);
    std::istringstream in(text);
    int v;
    while (in >> v && out.size() < 240)
        out.push_back(v);
    return out.size() == 240;
}

// The two generators of world creation: G2's shift register and the additive
// generator built on it (values 0..n); G3 is game/Rng.h's Rng3.
struct WorldRandom
{
    uint16_t w[6] = { 0x001B, 0x03FA, 0x11A9, 0x00CA, 0x01A3, 0x0003 };
    Rng3 g3;
    explicit WorldRandom(unsigned seed)
    {
        // the original's state depends on everything drawn since start-up; a new world here starts from the seed
        w[0] = static_cast<uint16_t>(seed | 1);
        for (int k = 1; k <= 3; ++k)
            w[k] = static_cast<uint16_t>(w[k] + (seed >> 16 & 7) + k * (seed >> 8 & 0xFF));
        g3.SetState(static_cast<uint8_t>((seed >> 24) | 1));
    }
    int Shift()
    {
        for (int i = 0; i < 8; ++i)
        {
            const int bit = ((w[0] >> 1) ^ (w[0] >> 2)) & 1;
            w[0] = static_cast<uint16_t>((w[0] >> 1) | (bit << 15));
        }
        return w[0];
    }
    int Scaled(int n)
    {
        const int q = 0x7FFF / (n + 1);
        const uint16_t sum = static_cast<uint16_t>(w[1] + w[2] + w[3] + w[4]);
        w[5] = w[4]; w[4] = w[3]; w[3] = w[2]; w[2] = w[1];
        w[1] = sum;
        Shift();
        w[1] = static_cast<uint16_t>(w[1] + (w[0] & 0x1F));
        const int r = (sum & 0x7FFF) / q;
        return r > n ? n : r;
    }
};

void SetWater(MapCell& c)
{
    c.base = static_cast<uint16_t>(0x3000 | kWater);
    c.terrain = 1;
}
} // namespace

bool GenerateWorld(const std::string& gameDir, const Megadata& mega, const GameState& tmpl, const WorldOptions& opt, GameState& s, std::string& error)
{
    s = tmpl;

    // climate tables of the chosen classes; the save keeps them as 240 x (wind, rain class, temperature)
    std::vector<int> rain, temp, wind;
    if (!ReadTable(mega, "rain" + std::to_string(opt.rain & 3) + ".dat", rain) || !ReadTable(mega, "temp" + std::to_string(opt.temperature & 3) + ".dat", temp) ||
        !ReadTable(mega, "wind" + std::to_string(opt.wind & 3) + ".dat", wind))
    {
        error = "The climate tables are missing from MEGADATA.EEA.";
        return false;
    }
    for (int w = 0; w < 240; ++w)
    {
        Wr8(s.weather, static_cast<size_t>(w) * 3 + 0, wind[w]);
        Wr8(s.weather, static_cast<size_t>(w) * 3 + 1, rain[w]);
        Wr8(s.weather, static_cast<size_t>(w) * 3 + 2, temp[w]);
    }

    // ---------------------------------------------------------------- the map
    WorldRandom g(opt.seed);
    // 1. a height map by midpoint displacement in the tile layer: every cell starts at word 0,
    // each pass fills the cells off the coarser lattice with the mean of its aligned neighbours plus a random step,
    // clamped to 1..15, as ground base + value. Reads past the last column fall into the cleared overlay layer.
    std::vector<uint16_t> words(static_cast<size_t>(kMapSize) * kMapSize * 2, 0);
    auto H = [&](int a, int b) {
        if (a < 0) a = -a; else if (a > 96) a -= 96;
        if (b < 0) b = -b; else if (b > 96) b -= 96;
        return ((words[static_cast<size_t>(a * 96 + b)] & 0xFF) - 15) & 15;
    };
    for (const auto& [step, mask] : { std::pair{ 16, 31 }, { 8, 15 }, { 4, 7 }, { 2, 3 }, { 1, 1 } })
        for (int x = 0; x < 96; x += step)
            for (int y = 0; y < 96; y += step)
            {
                const bool xOn = (mask & x) == 0, yOn = (mask & y) == 0;
                if (xOn && yOn)
                    continue;
                const int r = g.Scaled(mask) - step;
                int v;
                if (yOn)
                    v = ((H(x + step, y) + H(x - step, y)) >> 1) + r;
                else if (xOn)
                    v = ((H(x, y - step) + H(x, y + step)) >> 1) + r;
                else
                    v = ((H(x + step, y - step) + H(x - step, y - step) + H(x + step, y + step) + H(x - step, y + step)) >> 2) + r;
                words[static_cast<size_t>(x * 96 + y)] = static_cast<uint16_t>(15 + (std::clamp(v, 1, 15) & 15));
            }
    // 2. the scatter: the height becomes the terrain byte and the ground shade; high cells
    // grow a tree (7 in 16), middling ones a rock (2 in 16)
    const int kWet = std::clamp(opt.wetness, 0, 12); // the ground shade term
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x)
        {
            MapCell& c = s.world.At(x, y);
            const uint16_t w = words[static_cast<size_t>(x * 96 + y)];
            const int t = ((w & 0xFF) - 15) & 15;
            c.terrain = static_cast<uint8_t>(t);
            c.overlay = 0;
            c.base = static_cast<uint16_t>((15 + kWet + (t >> 2)) | ((w >> 8 & 0xF8) << 8));
            if (t >= 10 && (g.Shift() & 15) < 7)
                c.base = static_cast<uint16_t>(131 + t);
            if (t > 3 && t < 6 && (g.Shift() & 15) < 2)
                c.base = static_cast<uint16_t>(535 + (g.Shift() & 3));
        }
    // then the terrain byte is smoothed in place: the mean of the 8 neighbours (edges clamped)
    auto T = [&](int x, int y) { return static_cast<int>(s.world.At(std::clamp(x, 0, 95), std::clamp(y, 0, 95)).terrain); };
    for (int y = 1; y < 96; ++y)
        for (int x = 1; x < 96; ++x)
            s.world.At(x, y).terrain = static_cast<uint8_t>(
                ((T(x + 1, y - 1) + T(x, y - 1) + T(x - 1, y - 1) + T(x + 1, y + 1) + T(x, y + 1) + T(x - 1, y + 1) + T(x + 1, y) + T(x - 1, y)) & 0xFF) >> 3);
    // and every plot gets its attributes: three random levels (weeds, disease, pests: bytes 4, 3, 2) and, from
    // the terrain byte of cell (column, row) - not of the plot - bytes 6 (land value, N/A) and 5
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
        {
            const size_t z = static_cast<size_t>(c * 12 + r) * 7;
            s.zones[z + 4] = static_cast<uint8_t>((g.Shift() & 15) << 2);
            s.zones[z + 3] = static_cast<uint8_t>((g.Shift() & 15) << 2);
            s.zones[z + 2] = static_cast<uint8_t>((g.Shift() & 15) << 2);
            const int t = s.world.At(c, r).terrain;
            s.zones[z + 6] = static_cast<uint8_t>(t << 4);
            s.zones[z + 5] = static_cast<uint8_t>(t << 2);
        }
    if (opt.river)
    {
        // a 3-cell-wide random walk across the map: the axis by a coin, the start by G3 0..93, and per
        // step a coin for the direction and a coin for the move
        const bool alongY = (g.Shift() & 1) != 0;
        int off = g.g3.Bounded(0x5E);
        for (int step = 0; step < 96; ++step)
        {
            const int x = alongY ? off : step, y = alongY ? step : off;
            if (InMap(x, y))
                for (int k = 0; k < 3; ++k)
                    if (InMap(alongY ? x + k : x, alongY ? y : y + k))
                        SetWater(s.world.At(alongY ? x + k : x, alongY ? y : y + k));
            const int dir = g.Shift() & 1;
            off += dir == 0 ? -(g.Shift() & 1) : (g.Shift() & 1);
        }
    }
    if (opt.lake)
    {
        // 2h rows from the centre row down, widths 2i + 1 (+0..3) for i = 0..h-1 then h..1; the original tests the
        // centre row for the map edge, so rows below the map are written only when the engine can
        const int cx = g.g3.Bounded(0x18) + g.g3.Bounded(0x30), cy = g.g3.Bounded(0x18) + g.g3.Bounded(0x30), h = (g.Shift() & 7) + 4;
        for (int n = 0; n < 2 * h; ++n)
        {
            const int i = n < h ? n : 2 * h - n;
            const int t = g.Shift() & 3;
            const int w = t + 2 * i + 1;
            const int y = cy + n;
            const int x0 = cx + (t >> 1) - (w >> 1);
            for (int x = x0; x < x0 + w; ++x)
                if (InMap(x, cy) && InMap(x, y))
                    SetWater(s.world.At(x, y));
        }
    }
    // shorelines
    static const int shore[16] = { 55, 51, 47, 49, 43, 57, 45, 65, 39, 53, 59, 67, 41, 61, 63, -1 };
    // the table's N/E/S/W bits follow the original's transposed row/column naming: bit 1 = the cell to the left,
    // 2 = below, 4 = to the right, 8 = above (checked against the tile pictures: 39 has its land above)
    static const int dx[4] = { -1, 0, 1, 0 }, dy[4] = { 0, 1, 0, -1 };
    for (int x = 0; x < kMapSize; ++x)
        for (int y = 0; y < kMapSize; ++y)
        {
            MapCell& c = s.world.At(x, y);
            if (!IsWater(c))
                continue;
            int mask = 0;
            for (int k = 0; k < 4; ++k)
                if (InMap(x + dx[k], y + dy[k]) && !IsWater(s.world.At(x + dx[k], y + dy[k])))
                    mask |= 1 << k;
            const int id = shore[mask] < 0 ? kWater : shore[mask] + (g.Shift() & 1);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | id);
        }

    if (opt.terrain)
    {
        opt.terrain->assign(96 * 96, 0);
        for (int x = 0; x < 96; ++x)
            for (int y = 0; y < 96; ++y)
                (*opt.terrain)[static_cast<size_t>(x * 96 + y)] = static_cast<uint16_t>(s.world.At(x, y).base & 0x7FF);
    }
    // ---------------------------------------------------------------- town plot and farm block
    auto plotDry = [&](int col, int row) {
        for (int x = col * 8; x < col * 8 + 8; ++x)
            for (int y = row * 8; y < row * 8 + 8; ++y)
                if (IsWater(s.world.At(x, y)))
                    return false;
        return true;
    };
    // the town's home plot and the farm: one of four layouts by G2, repeated until the home plot and the
    // farm's first 2 x 2 plots hold no water
    int tc = 0, tr = 0, fc = 0, fr = 0;
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        switch (g.Shift() & 3)
        {
        case 0: tc = (g.Shift() & 7) + 2; tr = 0; fc = (g.Shift() & 7) + 2; fr = (g.Shift() & 3) + 6; break;
        case 1: tc = 11 - (g.Shift() & 3); tr = (g.Shift() & 7) + 2; fc = (g.Shift() & 3) + 1; fr = (g.Shift() & 7) + 2; break;
        case 2: tc = (g.Shift() & 7) + 2; tr = 11 - (g.Shift() & 3); fc = (g.Shift() & 7) + 2; fr = g.Shift() & 3; break;
        default: tc = 0; tr = (g.Shift() & 7) + 2; fc = (g.Shift() & 3) + 6; fr = (g.Shift() & 7) + 2; break;
        }
        if (plotDry(fc, fr) && plotDry(tc, tr) && plotDry(fc + 1, fr) && plotDry(fc, fr + 1) && plotDry(fc + 1, fr + 1))
            break;
    }
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
        {
            s.SetPlotOwner(c, r, 0);
            s.SetPlotType(c, r, 0xFF);
        }
    // the town's home plot (type 0 at level 10): the roads of block 0 of the town layout file (960
    // little-endian words) on bare ground, every cell flagged as town. (The engine used to read the file as bytes,
    // found the size wrong and left the plot as plain ground: no town at all on generated worlds.)
    const std::vector<uint8_t> raw = mega.Read("townly0.cmp");
    std::vector<uint16_t> layout;
    for (size_t i = 0; i + 1 < raw.size() && layout.size() < 960; i += 2)
        layout.push_back(static_cast<uint16_t>(raw[i] | (raw[i + 1] << 8)));
    PlaceTownPlotCells(s, layout, tc, tr, 0);
    s.SetPlotOwner(tc, tr, 2);
    s.SetPlotType(tc, tr, 0xA0);
    // the farm is 3 x 3 plots: owned, every cell marked as your land (overlay flag 0x800), then each
    // plot fenced on its open sides (column by column), then the homestead picture of the poorest tier
    for (int c = fc; c < fc + 3; ++c)
        for (int r = fr; r < fr + 3; ++r)
            s.SetPlotOwner(c, r, 1);
    for (int x = fc * 8; x < fc * 8 + 24; ++x)
        for (int y = fr * 8; y < fr * 8 + 24; ++y)
            s.world.At(x, y).overlay |= 0x800;
    for (int c = fc; c < fc + 3; ++c)
        for (int r = fr; r < fr + 3; ++r)
            FencePlot(s, c, r);
    DrawHomestead(s, fc * 8, fr * 8, 0);

    // ---------------------------------------------------------------- reset the game tables
    std::fill(s.animals.begin(), s.animals.end(), 0);
    std::fill(s.objects20.begin(), s.objects20.end(), 0);
    std::fill(s.machines.begin(), s.machines.end(), 0);
    std::fill(s.fields.begin(), s.fields.end(), 0);
    std::fill(s.structures.begin(), s.structures.end(), 0);
    std::fill(s.items.begin(), s.items.end(), 0);
    std::fill(s.traffic.begin(), s.traffic.end(), 0);
    std::fill(s.timeline.begin(), s.timeline.end(), 0);
    std::fill(s.eventGrounds.begin(), s.eventGrounds.end(), 0);
    s.animalCount = s.object20Count = s.machineCount = s.fieldCount = s.structureCount = s.itemCount = s.carCount = 0;

    // default crops and market prices at twice the base
    static const char* const files[16] = { "corn.crp", "wheat.crp", "barley.crp", "potatoes.crp", "oats.crp", "rice.crp", "sorghum.crp", "soybeans.crp",
                                           "cotton.crp", "onions.crp", "tomatoes.crp", "lettuce.crp", "carrots.crp", "strawber.crp", "sugar.crp", "peanuts.crp" };
    for (int c = 0; c < 16; ++c)
    {
        for (int k = 0; k < 14; ++k)
            Wr8(s.cropNames, static_cast<size_t>(c) * 14 + k, files[c][k] ? files[c][k] : 0);
        for (int k = static_cast<int>(std::string(files[c]).size()); k < 14; ++k)
            Wr8(s.cropNames, static_cast<size_t>(c) * 14 + k, 0);
        s.crops[c] = CropDef{};
        LoadCropFile(gameDir, files[c], s.crops[c]);
        const int price = std::max(10, s.crops[c].BasePrice()); // a new game starts at the crop file's price (CMP0)
        s.SetMarketPrice(c, price);
        s.SetCatalogPrice(50 + c, price);
        s.SetCatalogPrice(66 + c, s.crops[c].SeedPrice());
    }
    for (int rec = 0; rec < 103; ++rec)
        s.SetCatalogOwn(rec, 0);

    // scalars of a new game
    for (auto& [k, v] : s.scalars)
        if (k.rfind("asset_", 0) == 0 || k.rfind("rev_", 0) == 0 || k.rfind("exp_", 0) == 0 || k.rfind("loan", 0) == 0 || k.rfind("total_", 0) == 0 ||
            k.rfind("ev_", 0) == 0 || k == "tax_due" || k == "tax_counter" || k == "yearly_income" || k == "rain_days" || k == "airplanes" ||
            k == "airports" || k == "event_grounds")
            v = 0;
    s.cash = 40000;
    s.scalars["cash"] = 40000;
    s.scalars["town_pot"] = 4800;
    s.scalars["homestead_x"] = tc;
    s.scalars["homestead_y"] = tr;
    // the town works on its home plot first (FEDB/FEDC; seen in a new world saved by the original: the home plot grew
    // 10 -> 13 in its first weeks, getting its buildings at the first level change). Without it the town never grew.
    s.scalars["town_work_x"] = tc;
    s.scalars["town_work_y"] = tr;
    s.scalars["farm_plot_x"] = fc;
    s.scalars["farm_plot_y"] = fr;
    s.scalars["view_x"] = fc * 8;
    s.scalars["view_y"] = fr * 8;
    s.scalars["opt_autobuy"] = 1;
    s.scalars["opt_autoscroll"] = 1;
    s.scalars["opt_autodoze"] = 0;
    // a new world starts in week 1 of April of the computer's year, on today's weekday
    const std::time_t now = std::time(nullptr);
    const std::tm* lt = std::localtime(&now);
    s.cal = Calendar{};
    s.cal.month = 3;
    s.cal.year = lt ? lt->tm_year + 1900 : 1993;
    s.cal.day = lt ? lt->tm_wday : 0;
    s.speedDelay = 256;
    s.StoreScalarsToTail();
    error.clear();
    return true;
}

} // namespace osf
