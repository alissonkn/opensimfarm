// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/GameState.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace osf {

int Calendar::Season() const
{
    const int w = AbsWeek();
    if (w >= 45 || w <= 9)
        return 0;
    if (w <= 22)
        return 1; // week 22 keeps spring
    if (w <= 33)
        return 2;
    return 3;
}

namespace {

constexpr size_t kSaveSize = 139072;

// Scalar tail, in the order the original writes it.
const std::vector<std::pair<const char*, int>>& TailLayout()
{
    static const std::vector<std::pair<const char*, int>> layout = [] {
        std::vector<std::pair<const char*, int>> t = {
            { "event_grounds", 1 }, { "ev_1ED6", 1 }, { "ev_start", 2 }, { "ev_bet", 1 }, { "ev_entry", 2 }, { "ev_kind", 1 }, { "ev_countdown", 1 },
            // ledger (u32): assets, revenue lines, expense lines, totals
            { "asset_machinery", 4 }, { "asset_land", 4 }, { "asset_futures", 4 }, { "asset_livestock", 4 }, { "asset_total", 4 },
            { "rev_crops", 4 }, { "rev_machinery", 4 }, { "rev_land", 4 }, { "rev_futures", 4 }, { "rev_animals", 4 },
            { "exp_seed", 4 }, { "exp_machinery", 4 }, { "exp_land", 4 }, { "exp_futures", 4 }, { "exp_land_dup", 4 }, { "exp_livestock", 4 },
            { "exp_chemicals", 4 }, { "exp_interest", 4 }, { "exp_leases", 4 }, { "exp_taxes", 4 }, { "exp_misc", 4 },
            { "total_expense", 4 }, { "total_revenue", 4 },
            { "far_FA18", 4 }, { "loan_payment", 4 }, { "farm_plot_x", 2 }, { "farm_plot_y", 2 }, { "cash", 4 }, { "rain_days", 2 },
            { "var_2410", 2 }, { "var_2424", 1 }, { "day", 2 }, { "season", 2 }, { "week", 2 }, { "year", 2 }, { "month", 2 },
            { "timer", 2 }, { "timer2", 2 }, { "loan_credit", 4 }, { "loan_entry", 4 }, { "loan_balance", 4 }, { "view_x", 2 }, { "view_y", 2 },
            { "homestead_x", 2 }, { "homestead_y", 2 }, { "town_work_x", 1 }, { "town_work_y", 1 }, { "airplanes", 1 }, { "airports", 1 },
            { "opt_A961", 1 }, { "opt_A9C7", 1 }, { "opt_autobuy", 1 }, { "opt_autoscroll", 1 }, { "opt_autodoze", 1 }, { "speed_id", 1 },
            { "paused", 1 }, { "speed_delay", 2 }, { "var_1F90", 2 }, { "var_2286", 2 }, { "irr_period", 2 },
            { "yearly_income", 4 }, { "tax_due", 4 }, { "tax_counter", 1 },
        };
        static const char* menuFlags[17] = { "menu_0", "menu_1", "menu_2", "menu_3", "menu_4", "menu_5", "menu_6", "menu_7", "menu_8",
                                              "menu_9", "menu_10", "menu_11", "menu_12", "menu_13", "menu_14", "menu_15", "menu_16" };
        for (auto* m : menuFlags)
            t.emplace_back(m, 1);
        t.emplace_back("town_pot", 4);
        return t;
    }();
    return layout;
}

} // namespace

bool GameState::Load(const std::string& gameDir, const std::string& path, std::string& error, bool resetPrices)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() != kSaveSize)
    {
        error = "not an original SimFarm save or prebuilt farm: " + path;
        return false;
    }
    size_t o = 0;
    auto take = [&](std::vector<uint8_t>& dst, size_t n) {
        dst.assign(d.begin() + o, d.begin() + o + n);
        o += n;
    };
    auto count16 = [&](int& dst) {
        dst = d[o] | (d[o + 1] << 8);
        o += 2;
    };
    take(version, 1);
    count16(animalCount);    take(animals, 256 * 32);
    count16(object20Count);  take(objects20, 127 * 20);
    count16(machineCount);   take(machines, 127 * 56);
    count16(fieldCount);     take(fields, 98 * 68);
    count16(structureCount); take(structures, 127 * 70);
    count16(itemCount);      take(items, 512 * 12);
    carCount = d[o++];
    take(traffic, 16 * 8);
    take(timeline, 98 * 240 * 2);
    for (auto& c : world.cells)
    {
        c.base = static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
        c.overlay = static_cast<uint16_t>(d[o + 2] | (d[o + 3] << 8));
        c.terrain = d[o + 4];
        o += 5;
    }
    take(weather, 240 * 3);
    take(catalog, 103 * 18);
    take(irrigationFlag, 1);
    take(irrigationLists, 64 * 3);
    {
        // the original writes its source list (3-byte entries x, y, kind) with a stride of 2 (for i < 64
        // the bytes at 2i, 2i + 1, 2i + 2) and reads it back the same way, so the file holds overlapping
        // triples; read them back in order to get the entries (seen live: SCEN2's four pumps all registered)
        std::vector<uint8_t> m(192, 0);
        for (size_t i = 0; i < 64; ++i)
            for (size_t k = 0; k < 3; ++k)
                m[2 * i + k] = irrigationLists[3 * i + k];
        irrigationLists = m;
    }
    take(cropNames, 16 * 14);
    take(zones, 12 * 12 * 7);
    take(eventGrounds, 8 * 2);
    take(tail, d.size() - o);

    size_t t = 0;
    for (const auto& [name, size] : TailLayout())
    {
        int64_t v = 0;
        for (int b = 0; b < size; ++b)
            v |= static_cast<int64_t>(Rd8(tail, t + b)) << (8 * b);
        scalars[name] = v;
        t += size;
    }
    cash = scalars["cash"];
    cal.day = static_cast<int>(scalars["day"]) % 7;
    cal.week = static_cast<int>(scalars["week"]) % 4;
    cal.month = static_cast<int>(scalars["month"]) % 12;
    cal.year = static_cast<int>(scalars["year"]);
    speedDelay = scalars["speed_delay"] ? static_cast<int>(scalars["speed_delay"]) : 256;

    for (int i = 0; i < 16; ++i)
    {
        std::string n(reinterpret_cast<const char*>(&cropNames[static_cast<size_t>(i) * 14]), 14);
        n = n.substr(0, n.find('\0'));
        crops[i] = CropDef{};
        if (!n.empty())
            LoadCropFile(gameDir, n, crops[i]);
        // loading a game re-reads the crop files and puts every market price back to the crop's starting price
        // (seen in the original: a save loaded and saved again at once has the prices of a new game)
        if (crops[i].loaded && resetPrices)
        {
            SetMarketPrice(i, std::max(10, crops[i].BasePrice()));
            SetCatalogPrice(66 + i, crops[i].SeedPrice());
        }
    }
    return true;
}

void GameState::StoreScalarsToTail()
{
    scalars["cash"] = cash;
    // the original's save writes the land-purchase line twice and never the structures line (nor the structures
    // asset), so both are lost on a load
    scalars["exp_land_dup"] = scalars["exp_land"];
    scalars["day"] = cal.day;
    scalars["week"] = cal.week;
    scalars["month"] = cal.month;
    scalars["year"] = cal.year;
    scalars["season"] = cal.Season();
    scalars["speed_delay"] = speedDelay;
    size_t t = 0;
    for (const auto& [name, size] : TailLayout())
    {
        const int64_t v = scalars[name];
        for (int b = 0; b < size; ++b)
            Wr8(tail, t + b, static_cast<int>((v >> (8 * b)) & 0xFF));
        t += size;
    }
}

bool GameState::Save(const std::string& path, std::string& error) const
{
    GameState copy = *this;
    copy.StoreScalarsToTail();
    std::vector<uint8_t> d;
    d.reserve(kSaveSize);
    auto put = [&](const std::vector<uint8_t>& v) { d.insert(d.end(), v.begin(), v.end()); };
    auto put16 = [&](int v) { d.push_back(static_cast<uint8_t>(v & 0xFF)); d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); };
    put(copy.version);
    put16(animalCount);    put(copy.animals);
    put16(object20Count);  put(copy.objects20);
    put16(machineCount);   put(copy.machines);
    put16(fieldCount);     put(copy.fields);
    put16(structureCount); put(copy.structures);
    put16(itemCount);      put(copy.items);
    d.push_back(static_cast<uint8_t>(carCount));
    put(copy.traffic);
    put(copy.timeline);
    for (const auto& c : world.cells)
    {
        put16(c.base);
        put16(c.overlay);
        d.push_back(c.terrain);
    }
    {
        // back to the file's stride-2 layout (see the loader)
        std::vector<uint8_t> f(192, 0);
        for (size_t i = 0; i < 64; ++i)
            for (size_t k = 0; k < 3; ++k)
                f[3 * i + k] = copy.irrigationLists.size() >= 192 ? copy.irrigationLists[2 * i + k] : 0;
        copy.irrigationLists = f;
    }
    put(copy.weather); put(copy.catalog); put(copy.irrigationFlag); put(copy.irrigationLists);
    put(copy.cropNames); put(copy.zones); put(copy.eventGrounds); put(copy.tail);
    if (d.size() != kSaveSize)
    {
        error = "internal error: save size mismatch";
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return static_cast<bool>(out);
}

} // namespace osf
