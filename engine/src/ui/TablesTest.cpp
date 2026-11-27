// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/TablesTest.h"

#include "assets/Megadata.h"
#include "assets/PackTables.h"
#include "assets/TableFormats.h"
#include "assets/Theme.h"
#include "core/Json.h"
#include "game/Editing.h"
#include "game/GameState.h"
#include "game/WorldGen.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>

namespace osf {

namespace {

std::vector<uint8_t> B(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// ---- the readers this code replaced, kept here word for word: the decoders must give what they gave

bool OldReadTable(const std::vector<uint8_t>& blob, std::vector<int>& out)
{
    out.clear();
    std::string text;
    for (uint8_t c : blob)
        text += (c == ',' || c == '\r' || c == '\n') ? ' ' : static_cast<char>(c);
    std::istringstream in(text);
    int v;
    while (in >> v && out.size() < 240)
        out.push_back(v);
    return out.size() == 240;
}

std::vector<uint16_t> OldWorldGenTown(const std::vector<uint8_t>& raw)
{
    std::vector<uint16_t> layout;
    for (size_t i = 0; i + 1 < raw.size() && layout.size() < 960; i += 2)
        layout.push_back(static_cast<uint16_t>(raw[i] | (raw[i + 1] << 8)));
    return layout;
}

std::vector<uint16_t> OldSimulationTown(const std::vector<uint8_t>& cmp)
{
    std::vector<uint16_t> out;
    if (cmp.size() < 1920)
        return out;
    for (size_t i = 0; i < 960; ++i)
        out.push_back(static_cast<uint16_t>(cmp[2 * i] | (cmp[2 * i + 1] << 8)));
    return out;
}

std::vector<std::string> OldEventLines(const std::vector<uint8_t>& blob, int id)
{
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t c : blob)
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
    std::vector<std::string> out;
    for (int k = 0; k < 7; ++k)
    {
        const size_t i = static_cast<size_t>(id) * 8 + k;
        out.push_back(i < lines.size() ? lines[i] : std::string());
    }
    return out;
}

std::vector<std::string> OldItemLines(const std::vector<uint8_t>& d, int id)
{
    std::vector<std::string> lines, out;
    std::string cur;
    for (uint8_t c : d)
    {
        if (c == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else if (c != '\r')
            cur += static_cast<char>(c == '@' || c == '~' ? ' ' : c);
    }
    lines.push_back(cur);
    for (int k = 0; k < 7; ++k)
    {
        const size_t i = static_cast<size_t>(id) * 8 + k;
        out.push_back(i < lines.size() ? lines[i] : std::string());
    }
    return out;
}

std::map<std::string, std::vector<std::string>> OldScenarioGrids(const std::vector<uint8_t>& blob)
{
    std::map<std::string, std::vector<std::string>> grids;
    std::string section, line;
    for (uint8_t c : blob)
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
            grids[section].push_back(line.substr(0, 18));
        line.clear();
    }
    return grids;
}

int OldOverview(const std::vector<uint8_t>& mapc, int t, int r, int c)
{
    const int b = 2 * t + r < static_cast<int>(mapc.size()) ? mapc[static_cast<size_t>(2 * t + r)] : 0;
    int idx = 0;
    for (int plane = 0; plane < 4; ++plane)
        idx |= ((b >> (6 - 2 * plane + (1 - c))) & 1) << plane;
    return idx;
}

std::string Years(int a, int b, int c, int d) // 48 numbers: a, b, c, d repeating
{
    std::string s = "[";
    for (int i = 0; i < 48; ++i)
        s += std::string(i ? "," : "") + std::to_string(std::array<int, 4>{ a, b, c, d }[static_cast<size_t>(i) % 4]);
    return s + "]";
}

std::string Rows18(char fill, const char* special = "")
{
    std::string s = "[";
    for (int r = 0; r < 18; ++r)
    {
        std::string row(18, fill);
        if (r == 3 && *special)
            row[5] = special[0];
        s += std::string(r ? "," : "") + "\"" + row + "\"";
    }
    return s + "]";
}

std::string Block(int base) // 8 rows of 8 ids: base + 8 * row + column
{
    std::string s = "[";
    for (int j = 0; j < 8; ++j)
    {
        s += std::string(j ? "," : "") + "[";
        for (int i = 0; i < 8; ++i)
            s += std::string(i ? "," : "") + std::to_string(base + 8 * j + i);
        s += "]";
    }
    return s + "]";
}

} // namespace

int TablesSelfTest(const char* packFolder)
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
        fails += ok ? 0 : 1;
    };
    std::mt19937 rng(12345);

    // ---------------------------------------------------------------- the decoders match the readers they replaced
    std::printf("-- decoders against the old readers\n");
    {
        auto numbers = [&](int n, const char* sep) {
            std::string s;
            for (int i = 0; i < n; ++i)
                s += std::to_string(static_cast<int>(rng() % 200) - 5) + (i + 1 < n ? sep : "");
            return B(s);
        };
        bool same = true;
        for (const int n : { 0, 1, 239, 240, 241, 300 })
            for (const char* sep : { ",", " ", "\r\n", ",\r\n  " })
            {
                std::vector<int> a, b;
                const auto blob = numbers(n, sep);
                same = same && OldReadTable(blob, a) == DecodeClimateTable(blob, b) && a == b;
            }
        std::vector<int> a, b;
        const auto junk = B("12, x 14, 15");
        same = same && OldReadTable(junk, a) == DecodeClimateTable(junk, b) && a == b;
        check(same, "climate tables: the same numbers and the same yes or no on 0, 239, 240, 241, 300 numbers and on junk");
    }
    {
        bool same = true;
        for (const size_t n : { size_t(0), size_t(1), size_t(1919), size_t(1920), size_t(1921), size_t(2400) })
        {
            std::vector<uint8_t> blob(n);
            for (auto& x : blob)
                x = static_cast<uint8_t>(rng());
            const auto now = DecodeTownLayout(blob);
            same = same && now == OldSimulationTown(blob);
            // WorldGen's old reader took what it found; the plot code draws nothing unless it finds all 960 words
            const auto old = OldWorldGenTown(blob);
            same = same && (now.size() == 960) == (old.size() == 960) && (now.size() != 960 || now == old);
        }
        check(same, "town layout: the same 960 words from a blob of 0, 1, 1919, 1920, 1921, 2400 bytes");
    }
    {
        bool same = true;
        for (int round = 0; round < 40; ++round)
        {
            std::string s;
            const int n = static_cast<int>(rng() % 400);
            for (int i = 0; i < n; ++i)
            {
                const int r = static_cast<int>(rng() % 12);
                s += r == 0 ? '\n' : r == 1 ? '\r' : r == 2 ? '@' : r == 3 ? '~' : r == 4 ? '`' : static_cast<char>('A' + rng() % 26);
            }
            const auto blob = B(s);
            for (int id = 0; id < 12; ++id)
                same = same && OldEventLines(blob, id) == EventLines(DecodeTextLines(blob), id) && OldItemLines(blob, id) == ItemLines(DecodeTextLines(blob, true), id);
        }
        check(same, "event and item texts: the same 7 lines for ids 0..11 on 40 random blobs (CR, LF, '@', '~', '`')");
    }
    {
        const auto sample = B("//Rain Data\r\naaaaaaaaaabbbbbbbb \r\nccccccccccdddddddd \r\nshort \r\n//Hotpoint\r\nbbbbbbbbbbbbbbbbbb\r\nbbbbbbbbbbbbbbbbbb  x\r\n//temp data\naaaaaaaaaaaaaaaaaa \nabcdabcdabcdabcdab \n// \nignored 123456789012 \n");
        check(OldScenarioGrids(sample) == DecodeScenarioGrids(sample) && DecodeScenarioGrids(sample).size() >= 2, "scenario grids: the same sections and rows on a sample with short, unterminated and odd lines");
    }
    {
        bool same = true;
        std::vector<uint8_t> blob(300);
        for (auto& x : blob)
            x = static_cast<uint8_t>(rng());
        for (int t = -2; t < 170 && same; ++t)
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                    same = same && (t < 0 || OldOverview(blob, t, r, c) == OverviewIndex(blob, t, r, c));
        for (int v = 0; v < 256; ++v)
            for (int c = 0; c < 2; ++c)
                same = same && OldOverview({ static_cast<uint8_t>(v), static_cast<uint8_t>(v) }, 0, 0, c) == OverviewIndex({ static_cast<uint8_t>(v), static_cast<uint8_t>(v) }, 0, 0, c);
        check(same, "overview colours: the same palette index for every byte value and position, and 0 past the table");
    }

    // ---------------------------------------------------------------- a full manifest
    std::printf("-- tables from JSON\n");
    std::string wholeJson = "{ \"format\": 1, \"tables\": {"
        " \"climate\": { \"rain\": [" + Years(0, 1, 2, 3) + "," + Years(1, 2, 3, 4) + "," + Years(2, 3, 4, 5) + "," + Years(3, 4, 5, 7) + "],"
        "   \"temperature\": [" + Years(30, 50, 70, 90) + "," + Years(31, 51, 71, 91) + "," + Years(32, 52, 72, 92) + "," + Years(33, 53, 73, 93) + "],"
        "   \"wind\": [" + Years(1, 2, 3, 4) + "," + Years(5, 6, 7, 8) + "," + Years(9, 10, 11, 12) + "," + Years(13, 14, 15, 16) + "] },"
        " \"regions\": { \"rain\": " + Rows18('a', "d") + ", \"temperature\": " + Rows18('b') + ", \"wind\": " + Rows18('c') + ", \"hotpoints\": " + Rows18('a', "z") + " },"
        " \"townLayout\": { \"blocks\": [";
    for (int t = 0; t < 15; ++t)
        wholeJson += std::string(t ? "," : "") + Block(t * 64);
    wholeJson += "] },"
        " \"events\": [ { \"id\": 3, \"title\": \"Big News\", \"lines\": [ \"Line one\", \"Line two is here\" ] }, { \"id\": 1, \"title\": \"X\" } ],"
        " \"items\": [ { \"id\": 2, \"name\": \"Tractor\", \"description\": \"It pulls the plow and other tools\" }, { \"id\": 0, \"name\": \"Seed\", \"description\": \"\" } ],"
        " \"overview\": { \"default\": [1, 1, 1, 1], \"ranges\": [ { \"from\": 15, \"to\": 30, \"colours\": [10, 11, 12, 13] } ], \"tiles\": { \"17\": [0, 15, 5, 10], \"959\": [15, 15, 15, 15] } } } }";
    TableBlobs blobs;
    std::string err;
    const bool ok = ParsePackTables(wholeJson, blobs, err);
    check(ok, "a manifest with every table is made into blobs" + (ok ? "" : ": " + err));
    if (ok)
    {
        check(blobs.size() == 12 + 1 + 1 + 1 + 1 + 1, "  12 climate tables, scenario.odt, townly0.cmp, events.dat, itemtext.dat, far_mapc.mbm");
        std::vector<int> t;
        check(DecodeClimateTable(blobs["rain0.dat"], t) && t[0] == 0 && t[1] == 1 && t[4] == 0 && t[48] == 0 && t[49] == 1 && t[239] == 3,
              "  climate: 48 numbers repeat as 5 years, in place");
        check(DecodeClimateTable(blobs["temp3.dat"], t) && t[0] == 33 && t[3] == 93 && DecodeClimateTable(blobs["wind2.dat"], t) && t[2] == 11, "  climate: classes and kinds go to their file names");
        const std::string text(blobs["rain1.dat"].begin(), blobs["rain1.dat"].end());
        check(std::count(text.begin(), text.end(), '\n') == 60 && text.substr(0, text.find("\r\n")) == "1,2,3,4", "  climate: 60 lines of 4 values, as the original's files");

        const auto g = DecodeScenarioGrids(blobs["scenario.odt"]);
        check(g.count("rain data") && g.at("rain data").size() == 18 && g.at("rain data")[3][5] == 'd' && g.at("rain data")[0] == "aaaaaaaaaaaaaaaaaa" &&
                  g.at("temp data")[7] == std::string(18, 'b') && g.at("wind data")[17] == std::string(18, 'c') && g.at("hotpoint data")[3][5] == 'z',
              "  regions: four sections of 18 rows of 18 letters");
        const auto town = DecodeTownLayout(blobs["townly0.cmp"]);
        check(town.size() == 960 && town[0] == 0 && town[7] == 7 && town[40] == 8 && town[8] == 64 && town[8 * 40] == 5 * 64 + 0 && town[(16 + 7) * 40 + 39] == 14 * 64 + 63 &&
                  town[(8 + 2) * 40 + 3 * 8 + 5] == (5 + 3) * 64 + 2 * 8 + 5,
              "  town layout: block t at column t % 5, row t / 5 of the 40 x 24 grid, tile ids in place");
        const auto ev = DecodeTextLines(blobs["events.dat"]);
        check(ev.size() == 4 * 8 + 1 && ev[3 * 8].size() == 30 && ev[3 * 8].find('`') == 0, "  events: 8 lines of 30 for each id up to the last");
        const auto e3 = EventLines(ev, 3);
        check(e3[0] == std::string((27 - 8) / 2, '`') + "Big News" + std::string(30 - (27 - 8) / 2 - 8, '@') && e3[1] == "Line one" + std::string(22, '@') && e3[2] == "Line two is here" + std::string(14, '@') &&
                  e3[3] == std::string(30, '@') && EventLines(ev, 2)[0] == std::string(30, '@'),
              "  events: the title centred with blanks ('`'), lines padded with '@', unused ids and lines blank");
        const auto it = ItemLines(DecodeTextLines(blobs["itemtext.dat"], true), 2);
        check(blobs["itemtext.dat"].size() == 3 * 128 && it[0].substr(0, 8) == "Tractor " && it[1].substr(0, 11) == "It pulls th" && it[1].size() == 14,
              "  items: 128 bytes an id, name first (itemtext.dat)");
        std::string rec(blobs["itemtext.dat"].begin() + 2 * 128, blobs["itemtext.dat"].begin() + 3 * 128), name;
        for (char c : rec)
        {
            if (c == '~' || c == '@')
                break;
            name += c;
        }
        check(name == "Tractor " && rec.find("It pulls the") != std::string::npos && rec.find("plow and") != std::string::npos, "  items: the Examine card's reading of the record finds the name and the words");
        const std::vector<uint8_t>& ov = blobs["far_mapc.mbm"];
        check(ov.size() == 1920 && OverviewIndex(ov, 0, 0, 0) == 1 && OverviewIndex(ov, 15, 0, 0) == 10 && OverviewIndex(ov, 30, 1, 1) == 13 && OverviewIndex(ov, 31, 0, 1) == 1 &&
                  OverviewIndex(ov, 17, 0, 1) == 15 && OverviewIndex(ov, 17, 1, 0) == 5 && OverviewIndex(ov, 17, 1, 1) == 10 && OverviewIndex(ov, 17, 0, 0) == 0 &&
                  OverviewIndex(ov, 959, 1, 1) == 15,
              "  overview: default, range and single tiles, each 2 x 2 block in order");
        bool every = true;
        for (int i = 0; i < 16; ++i)
        {
            // every palette index survives the packing in each of the four positions
            TableBlobs one;
            std::string e;
            const std::string js = "{ \"format\": 1, \"tables\": { \"overview\": { \"tiles\": { \"5\": [" + std::to_string(i) + "," + std::to_string(15 - i) + "," + std::to_string((i * 7) & 15) + "," + std::to_string((i * 3 + 1) & 15) + "] } } } }";
            every = every && ParsePackTables(js, one, e) && OverviewIndex(one["far_mapc.mbm"], 5, 0, 0) == i && OverviewIndex(one["far_mapc.mbm"], 5, 0, 1) == 15 - i &&
                    OverviewIndex(one["far_mapc.mbm"], 5, 1, 0) == ((i * 7) & 15) && OverviewIndex(one["far_mapc.mbm"], 5, 1, 1) == ((i * 3 + 1) & 15);
        }
        check(every, "  overview: all 16 palette indices come back from each of the 4 positions");
    }

    // ---------------------------------------------------------------- the real consumers
    std::printf("-- the engine's own consumers\n");
    if (ok)
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "osf_tablestest").string();
        std::filesystem::create_directories(dir);
        {
            // a fake archive holding one entry; the pack's tables are laid over it
            const std::vector<uint8_t> one = { 1, 2, 3 };
            std::vector<uint8_t> idx;
            auto rec = [&](const char* name, uint32_t off) {
                char n[16] = {};
                std::snprintf(n, sizeof n, "%s", name);
                idx.insert(idx.end(), n, n + 16);
                for (int s = 0; s < 32; s += 8)
                    idx.push_back(static_cast<uint8_t>(off >> s));
            };
            rec("EVENTS.DAT", 0);
            rec("", 3);
            std::ofstream(dir + "/MEGADATA.IDX", std::ios::binary).write(reinterpret_cast<const char*>(idx.data()), static_cast<long>(idx.size()));
            std::ofstream(dir + "/MEGADATA.EEA", std::ios::binary).write(reinterpret_cast<const char*>(one.data()), 3);
        }
        Megadata mega;
        std::string e;
        check(mega.Open(dir, e) && mega.Has("events.dat") && mega.Read("EVENTS.DAT") == std::vector<uint8_t>({ 1, 2, 3 }), "the fake archive opens with its one entry");
        for (const auto& [name, bytes] : blobs)
            mega.Override(name, bytes);
        check(mega.Read("Events.Dat") == blobs["events.dat"] && mega.Has("RAIN0.DAT") && mega.Read("far_mapc.mbm").size() == 1920, "Override replaces an entry and adds new ones, names in any case");

        // world generation reads the pack's climate tables through the archive's names
        GameState tmpl;
        tmpl.weather.assign(240 * 3, 0);
        WorldOptions opt;
        opt.rain = 2;
        opt.temperature = 3;
        opt.wind = 0;
        GameState made;
        WorldSites sites;
        std::string gerr;
        const bool gen = GenerateTerrain(mega, tmpl, opt, made, sites, gerr);
        check(gen && made.weather.size() == 720, "GenerateTerrain runs on the pack's climate tables" + (gen ? "" : ": " + gerr));
        if (gen)
            check(made.WeatherRain(0) == 2 && made.WeatherRain(1) == 3 && made.WeatherRain(2) == 4 && made.WeatherRain(3) == 5 && made.WeatherTemp(0) == 33 &&
                      made.WeatherTemp(1) == 53 && made.WeatherTemp(239) == 93 && made.WeatherWind(0) == 1 && made.WeatherWind(1) == 2 && made.WeatherWind(100) == 1,
                  "  class 2 rain, class 3 temperature and class 0 wind come out in the save's week table");
        Megadata bare;
        check(bare.Open(dir, e) && !GenerateTerrain(bare, tmpl, opt, made, sites, gerr) && !gerr.empty(), "without the tables world generation says so");

        // the town plots draw from the pack's layout: block 0 holds ids 0..63; make dirt (161..171) cells and ground
        {
            const std::string js = "{ \"format\": 1, \"tables\": { \"townLayout\": { \"blocks\": [";
            std::string blocks;
            for (int t = 0; t < 15; ++t)
            {
                std::string b = "[";
                for (int j = 0; j < 8; ++j)
                {
                    b += std::string(j ? "," : "") + "[";
                    for (int i = 0; i < 8; ++i)
                        b += std::string(i ? "," : "") + std::to_string(i == 3 ? 161 : j == 5 ? 162 : 15);
                    b += "]";
                }
                blocks += std::string(t ? "," : "") + b + "]";
            }
            TableBlobs town;
            std::string te;
            const bool made2 = ParsePackTables(js + blocks + "] } } }", town, te);
            check(made2, "a town layout of dirt roads and ground" + (made2 ? "" : ": " + te));
            if (made2)
            {
                const std::vector<uint16_t> layout = DecodeTownLayout(town["townly0.cmp"]);
                GameState s;
                s.world.Resize(96);
                PlaceTownPlotCells(s, layout, 2, 2, 0);
                const int road = s.world.At(2 * 8 + 3, 2 * 8 + 0).BaseTile(), cross = s.world.At(2 * 8 + 3, 2 * 8 + 5).BaseTile(), plain = s.world.At(2 * 8 + 0, 2 * 8 + 0).BaseTile();
                check(road >= 161 && road <= 172 && cross >= 161 && cross <= 172 && plain >= 15 && plain < 31 && (s.world.At(2 * 8 + 3, 2 * 8).overlay & 0x4000),
                      "PlaceTownPlotCells lays the pack's roads (dirt pieces) and ground, every cell flagged as town");
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // ---------------------------------------------------------------- errors
    std::printf("-- bad tables\n");
    {
        auto bad = [&](const std::string& tables, const std::string& why) {
            TableBlobs b;
            std::string e;
            const bool rejected = !ParsePackTables("{ \"format\": 1, \"tables\": " + tables + " }", b, e) && !e.empty() && b.empty();
            check(rejected, "rejects " + why + (rejected ? " (" + e + ")" : ""));
        };
        const std::string four48 = Years(1, 1, 1, 1) + "," + Years(1, 1, 1, 1) + "," + Years(1, 1, 1, 1) + "," + Years(1, 1, 1, 1);
        std::string short47 = "[";
        for (int i = 0; i < 47; ++i)
            short47 += std::string(i ? "," : "") + "1";
        short47 += "]";
        bad("5", "tables that is not an object");
        bad("{ \"climate\": 5 }", "climate that is not an object");
        bad("{ \"climate\": { \"rain\": [" + Years(0, 0, 0, 0) + "] } }", "rain with 1 table, not 4");
        bad("{ \"climate\": { \"rain\": [" + short47 + "," + short47 + "," + short47 + "," + short47 + "] } }", "47 weeks");
        bad("{ \"climate\": { \"rain\": [" + Years(0, 8, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "] } }", "a rain class of 8");
        bad("{ \"climate\": { \"wind\": [" + Years(0, 256, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "] } }", "wind of 256");
        bad("{ \"climate\": { \"temperature\": [" + Years(-1, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "] } }", "a negative temperature");
        bad("{ \"climate\": { \"temperature\": [" + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + "," + Years(0, 0, 0, 0) + ",[1.5," + Years(0, 0, 0, 0).substr(3) + "] } }", "a fractional week");
        bad("{ \"regions\": { \"rain\": " + Rows18('a') + ", \"temperature\": " + Rows18('a') + " } }", "regions without wind");
        bad("{ \"regions\": { \"rain\": " + Rows18('e') + ", \"temperature\": " + Rows18('a') + ", \"wind\": " + Rows18('a') + " } }", "a climate class letter above 'd'");
        bad("{ \"regions\": { \"rain\": " + Rows18('a').substr(0, 40) + "], \"temperature\": " + Rows18('a') + ", \"wind\": " + Rows18('a') + " } }", "17 or fewer rows");
        bad("{ \"townLayout\": { \"blocks\": [] } }", "a town layout with no blocks");
        {
            std::string b = Block(0), many;
            for (int t = 0; t < 15; ++t)
                many += std::string(t ? "," : "") + (t == 7 ? "[[1,2,3]]" : b);
            bad("{ \"townLayout\": { \"blocks\": [" + many + "] } }", "a block that is not 8 x 8");
            std::string big;
            for (int t = 0; t < 15; ++t)
                big += std::string(t ? "," : "") + (t == 14 ? Block(2040) : b); // ids up to 2040 + 63 = 2103
            bad("{ \"townLayout\": { \"blocks\": [" + big + "] } }", "a tile id above 2047");
        }
        bad("{ \"events\": [ { \"id\": 1 }, { \"id\": 1 } ] }", "an event id twice");
        bad("{ \"events\": [ { \"id\": 256 } ] }", "an event id of 256");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"" + std::string(28, 'x') + "\" } ] }", "a 28-character title");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"a\", \"lines\": [\"" + std::string(28, 'x') + "\"] } ] }", "a 28-character line");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"a\", \"lines\": [\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\"] } ] }", "7 lines");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"a@b\" } ] }", "an '@' in a title");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"a~b\" } ] }", "a '~' in a title");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"a`b\" } ] }", "a '`' in a title");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"caf\\u00e9\" } ] }", "a letter beyond ASCII");
        bad("{ \"events\": [ { \"id\": 1, \"title\": \"a{b\" } ] }", "a '{' (above 'z': the box skips it)");
        bad("{ \"events\": [ { \"id\": 1, \"lines\": [5] } ] }", "a line that is not a string");
        bad("{ \"items\": [ { \"id\": 1, \"name\": \"\" } ] }", "an empty item name");
        bad("{ \"items\": [ { \"id\": 1, \"name\": \"Fourteen chars\" } ] }", "a 14-character item name");
        bad("{ \"items\": [ { \"id\": 1, \"name\": \"Ok\", \"description\": \"Extraordinarily\" } ] }", "a word longer than a line");
        bad("{ \"items\": [ { \"id\": 1, \"name\": \"Ok\", \"description\": \"a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z a b\" } ] }", "a description that needs 7 lines");
        bad("{ \"items\": [ { \"id\": 1, \"name\": \"Ok~\" } ] }", "a '~' in an item name");
        bad("{ \"overview\": { \"default\": [0, 0, 0] } }", "an overview colour of 3 indices");
        bad("{ \"overview\": { \"default\": [0, 0, 0, 16] } }", "a palette index of 16");
        bad("{ \"overview\": { \"tiles\": { \"960\": [0, 0, 0, 0] } } }", "tile 960");
        bad("{ \"overview\": { \"tiles\": { \"x\": [0, 0, 0, 0] } } }", "a tile key that is not a number");
        bad("{ \"overview\": { \"ranges\": [ { \"from\": 10, \"to\": 5, \"colours\": [0, 0, 0, 0] } ] } }", "a range that ends before it starts");
        bad("{ \"overview\": { \"ranges\": [ { \"from\": 1, \"colours\": [0, 0, 0, 0] } ] } }", "a range without \"to\"");
        // all or nothing: a good table before a bad one is not kept
        TableBlobs b;
        std::string e;
        check(!ParsePackTables("{ \"format\": 1, \"tables\": { \"overview\": {}, \"events\": [ { \"id\": 1, \"title\": \"a@\" } ] } }", b, e) && b.empty(), "a failing manifest adds nothing");
        check(ParsePackTables("{ \"format\": 1 }", b, e) && b.empty() && ParsePackTables("{ \"format\": 1, \"tables\": {} }", b, e) && b.empty(), "no tables, or an empty block, gives no blobs");
        check(ParsePackTables("{ \"format\": 1, \"tables\": { \"events\": [] } }", b, e) && b["events.dat"].empty(), "an empty event list is an empty events.dat");
    }

    // ---------------------------------------------------------------- a pack's own tables
    if (packFolder)
    {
        std::printf("-- the tables of %s\n", packFolder);
        TableBlobs pack;
        std::string e;
        const bool loaded = LoadPackTables(packFolder, pack, e);
        check(loaded && !pack.empty(), "its tables are made into blobs" + (loaded ? "" : ": " + e));
        if (loaded && !pack.empty())
        {
            int tables = 0;
            bool climate = true;
            for (const char* kind : { "rain", "temp", "wind" })
                for (int c = 0; c < 4; ++c)
                {
                    std::vector<int> weeks;
                    climate = climate && DecodeClimateTable(pack[std::string(kind) + std::to_string(c) + ".dat"], weeks);
                    ++tables;
                }
            check(climate && pack.count("rain0.dat") + pack.count("temp3.dat") + pack.count("wind1.dat") == 3, "  12 climate tables, each of 240 weeks");
            const auto grids = DecodeScenarioGrids(pack["scenario.odt"]);
            check(grids.count("rain data") && grids.at("rain data").size() == 18 && grids.count("hotpoint data"), "  the region map: rain, temperature, wind and hotpoints");
            check(DecodeTownLayout(pack["townly0.cmp"]).size() == 960, "  the town layout: 960 words");
            const auto ev = DecodeTextLines(pack["events.dat"]);
            const auto e26 = EventLines(ev, 26);
            check(ev.size() > 79 * 8 && e26[1].find("There is no airport") != std::string::npos, "  80 event texts, the engine's own words for the ids it raises");
            const auto it = ItemLines(DecodeTextLines(pack["itemtext.dat"], true), 102);
            check(pack["itemtext.dat"].size() == 103 * 128 && it[0].find("Item 102") == 0, "  103 item texts");
            Theme theme;
            bool hasTheme = false;
            check(LoadTheme(packFolder, theme, hasTheme, e) && hasTheme && theme[12] == std::array<uint8_t, 3>({ 0xC4, 0xC4, 0xC4 }) && theme[15] == std::array<uint8_t, 3>({ 0xFF, 0xFF, 0xFF }) &&
                      theme[0] == std::array<uint8_t, 3>({ 0, 0, 0 }),
                  "  the interface theme: 16 colours, light grey panels, black and white");
            check(pack["far_mapc.mbm"].size() == 1920 && OverviewIndex(pack["far_mapc.mbm"], 40, 0, 0) == 6 && OverviewIndex(pack["far_mapc.mbm"], 20, 1, 1) == 10,
                  "  the overview colours by family: water blue, ground brown");
        }
    }

    std::printf("data tables %s\n", fails ? "FAIL" : "ok");
    return fails;
}

} // namespace osf
