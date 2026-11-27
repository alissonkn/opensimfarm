// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/TablesTest.h"

#include "assets/Megadata.h"
#include "assets/PackCards.h"
#include "assets/PackSounds.h"
#include "assets/PackTables.h"
#include "audio/Sound.h"
#include "assets/TableFormats.h"
#include "assets/Theme.h"
#include "assets/Weather.h"
#include "core/Json.h"
#include "game/Editing.h"
#include "game/GameState.h"
#include "game/WorldGen.h"
#include "ui/OverviewImage.h"

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

    // ---------------------------------------------------------------- the Map window's picture
    std::printf("-- the overview picture\n");
    {
        // the Map window's pixel stage as it was written inline: index buffer, nearest scaling into 192, XOR with 15
        auto oldPicture = [&](const std::vector<uint8_t>& mapc, const std::vector<int>& tile, int N, const std::vector<std::pair<int, int>>& xors) {
            const int D = 2 * N;
            std::vector<uint8_t> ix(static_cast<size_t>(D) * D);
            for (int x = 0; x < N; ++x)
                for (int y = 0; y < N; ++y)
                {
                    const int t = tile[static_cast<size_t>(x) * N + y];
                    for (int r = 0; r < 2; ++r)
                        for (int c = 0; c < 2; ++c)
                            ix[static_cast<size_t>(2 * y + r) * D + 2 * x + c] = static_cast<uint8_t>(OldOverview(mapc, t, r, c));
                }
            if (D != 192)
            {
                std::vector<uint8_t> box(192 * 192);
                for (int y = 0; y < 192; ++y)
                    for (int x = 0; x < 192; ++x)
                        box[static_cast<size_t>(y) * 192 + x] = ix[static_cast<size_t>(y * D / 192) * D + x * D / 192];
                ix.swap(box);
            }
            for (const auto& [x, y] : xors)
                if (x >= 0 && y >= 0 && x < 192 && y < 192)
                    ix[static_cast<size_t>(y) * 192 + x] ^= 15;
            return ix;
        };
        std::vector<uint8_t> mapc(1920);
        for (auto& b : mapc)
            b = static_cast<uint8_t>(rng());
        const auto palette = [](int i) { return 0xFF000000u | static_cast<uint32_t>(i * 0x101010 + 0x050607); };
        bool same = true;
        for (const int N : { 96, 144, 40 })
        {
            std::vector<int> tile(static_cast<size_t>(N) * N);
            for (auto& t : tile)
                t = static_cast<int>(rng() % 1000); // (some past the table: its 960 tiles)
            std::vector<std::pair<int, int>> xors = { { 0, 0 }, { 191, 191 }, { 5, 7 }, { 5, 7 }, { -1, 3 }, { 3, 192 }, { 100, 50 } };
            const OverviewTable table = MakeOverview({}, mapc);
            OverviewImage image(table, tile, N);
            image.ScaleTo(192);
            for (const auto& [x, y] : xors)
                image.Invert(x, y);
            const auto expected = oldPicture(mapc, tile, N, xors);
            const auto got = image.ToArgb(palette);
            bool eq = got.size() == expected.size() && image.Side() == 192 && !image.Rgb();
            for (size_t i = 0; i < expected.size() && eq; ++i)
                eq = got[i] == palette(expected[i]);
            same = same && eq;
        }
        check(same, "indexed table: the same picture as the window's old inline code for 96, 144 and 40 cells, scaled to 192, with XOR (also twice on a pixel and outside)");
        // an RGB table
        std::vector<uint8_t> rgb(static_cast<size_t>(kOverviewRgbBytes));
        for (size_t i = 0; i < rgb.size(); ++i)
            rgb[i] = static_cast<uint8_t>(i % 4 == 3 ? 255 : (i * 7 + i / 4) & 255);
        const OverviewTable rgbTable = MakeOverview(rgb, mapc);
        check(rgbTable.rgb && rgbTable.blob == rgb, "an RGB entry wins over the indexed one");
        std::vector<int> tile(static_cast<size_t>(96) * 96);
        for (size_t i = 0; i < tile.size(); ++i)
            tile[i] = static_cast<int>(i % 960);
        OverviewImage img(rgbTable, tile, 96);
        bool callback = false;
        auto before = img.ToArgb([&](int) { callback = true; return 0u; });
        bool placed = img.Rgb() && img.Side() == 192 && !callback;
        for (int x = 0; x < 96 && placed; x += 11)
            for (int y = 0; y < 96 && placed; y += 7)
                for (int r = 0; r < 2; ++r)
                    for (int c = 0; c < 2; ++c)
                        placed = placed && before[static_cast<size_t>(2 * y + r) * 192 + 2 * x + c] == rgbTable.Argb(tile[static_cast<size_t>(x) * 96 + y], r, c);
        check(placed, "RGB table: each cell's 2 x 2 block in place, and the palette is not asked");
        img.Invert(10, 20);
        auto after = img.ToArgb([](int) { return 0u; });
        const size_t at = 20 * 192 + 10;
        check(after[at] == (before[at] ^ 0x00FFFFFFu) && (after[at] >> 24) == 0xFF && after[at + 1] == before[at + 1], "RGB table: the XOR highlight inverts the colour, opaque, and only that pixel");
        img.Invert(10, 20);
        img.Invert(-1, 0);
        img.Invert(192, 0);
        img.Invert(0, 192);
        check(img.ToArgb([](int) { return 0u; }) == before, "  twice is no change, and outside the picture nothing happens");
        img.ScaleTo(96);
        const auto small = img.ToArgb([](int) { return 0u; });
        check(img.Side() == 96 && small.size() == 96 * 96 && small[5 * 96 + 7] == before[static_cast<size_t>(5 * 192 / 96) * 192 + 7 * 192 / 96], "RGB table: scaling picks the nearest pixel, as the indexed picture does");
        const OverviewTable none = MakeOverview({ 1, 2, 3 }, {});
        check(!none.rgb && none.Index(5, 0, 0) == 0 && MakeOverview({}, {}).blob.empty(), "a too short RGB entry is ignored; with neither entry the table is empty and gives index 0");
    }

    // ---------------------------------------------------------------- the Farm Expert's cards
    std::printf("-- Farm Expert cards\n");
    {
        // the card reader as the Expert window had it, word for word
        auto oldCard = [](const std::vector<uint8_t>& d) {
            static const std::map<int, std::pair<int, bool>> spec = { { 0x83, { 5, false } }, { 0x84, { 4, false } }, { 0x86, { 3, false } }, { 0x87, { 3, true } },
                                                                       { 0x89, { 3, false } }, { 0x8a, { 4, true } }, { 0x8b, { 2, false } }, { 0x8c, { 5, false } } };
            std::vector<CardOp> ops;
            size_t i = 0;
            while (i < d.size() && d[i] != 0xFF)
            {
                const auto it = spec.find(d[i]);
                if (it == spec.end())
                    break;
                CardOp o{ d[i++], {}, {} };
                for (int k = 0; k < it->second.first && i + 1 < d.size(); ++k, i += 2)
                    o.a.push_back(d[i] | (d[i + 1] << 8));
                if (it->second.second)
                    while (i < d.size() && d[i])
                        o.text += static_cast<char>(d[i++]);
                i += it->second.second ? 1 : 0;
                ops.push_back(std::move(o));
            }
            return ops;
        };
        bool same = true;
        static const int kOps[] = { 0x83, 0x84, 0x86, 0x87, 0x89, 0x8a, 0x8b, 0x8c, 0x85, 0x00, 0xFF };
        for (int round = 0; round < 300; ++round)
        {
            std::vector<uint8_t> blob;
            const int n = static_cast<int>(rng() % 60);
            for (int k = 0; k < n; ++k)
                blob.push_back(rng() % 4 == 0 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(kOps[rng() % 11]));
            const auto a = oldCard(blob), b = DecodeCard(blob);
            bool eq = a.size() == b.size();
            for (size_t k = 0; k < a.size() && eq; ++k)
                eq = a[k].op == b[k].op && a[k].a == b[k].a && a[k].text == b[k].text;
            same = same && eq;
        }
        check(same, "the shared card decoder reads 300 random blobs (known and unknown codes, truncated, 0xFF) as the Expert window's own reader did");

        const std::string good = "{ \"id\": 1, \"items\": [ { \"picture\": 66, \"x\": 24, \"y\": 40 }, { \"text\": \"Hello there\", \"x\": 120, \"y\": 44, \"width\": 200 },"
                                 " { \"button\": 4, \"x\": 280, \"y\": 90, \"goto\": 2 }, { \"hotspot\": [10, 20, 110, 60], \"goto\": 2 }, { \"tiles\": { \"first\": 539, \"count\": 4 }, \"x\": 20, \"y\": 150 },"
                                 " { \"crop\": 3, \"x\": 30, \"y\": 90 }, { \"sound\": [1, 2, 30, 40], \"play\": \"cow1.voc\" } ] },"
                                 " { \"id\": 2, \"items\": [ { \"button\": 3, \"x\": 336, \"y\": 200, \"goto\": 1 } ] }";
        TableBlobs out;
        std::string e;
        const bool ok = ParsePackCards("{ \"format\": 1, \"cards\": [ " + good + " ] }", out, e);
        check(ok && out.size() == 2 && out.count("card1.hc") && out.count("card2.hc"), "cards are made into card<id>.hc entries" + (ok ? "" : ": " + e));
        if (ok)
        {
            const auto ops = DecodeCard(out["card1.hc"]);
            check(ops.size() == 7 && ops[0].op == 0x86 && ops[0].a == std::vector<int>({ 66, 24, 40 }) && ops[1].op == 0x87 && ops[1].a == std::vector<int>({ 120, 44, 200 }) && ops[1].text == "Hello there" &&
                      ops[2].op == 0x84 && ops[2].a == std::vector<int>({ 280, 90, 4, 2 }) && ops[3].op == 0x83 && ops[3].a == std::vector<int>({ 10, 20, 110, 60, 2 }) &&
                      ops[4].op == 0x8c && ops[4].a == std::vector<int>({ 20, 150, 4, 0, 539 }) && ops[5].op == 0x89 && ops[5].a == std::vector<int>({ 3, 30, 90 }) &&
                      ops[6].op == 0x8a && ops[6].a == std::vector<int>({ 1, 2, 30, 40 }) && ops[6].text == "cow1.voc",
                  "  each kind of item is read back by the Expert's decoder, arguments in the order it uses them");
            check(out["card1.hc"].back() == 0xFF && DecodeCard(out["card2.hc"]).size() == 1, "  a card ends with 0xFF");
        }
        TableBlobs none;
        check(ParsePackCards("{ \"format\": 1 }", none, e) && none.empty(), "no cards block: nothing");
        auto bad = [&](const std::string& cards, const std::string& why) {
            TableBlobs b;
            std::string er;
            const bool r = !ParsePackCards("{ \"format\": 1, \"cards\": " + cards + " }", b, er) && !er.empty() && b.empty();
            check(r, "rejects " + why + (r ? " (" + er + ")" : ""));
        };
        const std::string home = "{ \"id\": 1, \"items\": [] }";
        bad("5", "cards that is not a list");
        bad("[]", "no cards");
        bad("[ 5 ]", "a card that is not an object");
        bad("[ { \"items\": [] } ]", "a card without an id");
        bad("[ { \"id\": 0, \"items\": [] } ]", "an id of 0");
        bad("[ { \"id\": 10000, \"items\": [] } ]", "an id of 10000");
        bad("[ " + home + ", " + home + " ]", "an id twice");
        bad("[ { \"id\": 2, \"items\": [] } ]", "no card 1");
        bad("[ { \"id\": 1 } ]", "a card without items");
        bad("[ { \"id\": 1, \"items\": [], \"title\": \"x\" } ]", "a field a card does not have");
        bad("[ { \"id\": 1, \"items\": [ 5 ] } ]", "an item that is not an object");
        bad("[ { \"id\": 1, \"items\": [ { \"x\": 1, \"y\": 2 } ] } ]", "an item of no kind");
        bad("[ { \"id\": 1, \"items\": [ { \"picture\": 66 } ] } ]", "a picture without a place");
        bad("[ { \"id\": 1, \"items\": [ { \"picture\": 256, \"x\": 1, \"y\": 2 } ] } ]", "a picture of 256");
        bad("[ { \"id\": 1, \"items\": [ { \"text\": \"a\", \"x\": 1, \"y\": 2 } ] } ]", "a text without a width");
        bad("[ { \"id\": 1, \"items\": [ { \"text\": \"a\", \"x\": 1, \"y\": 2, \"width\": 4 } ] } ]", "a width of 4");
        bad("[ { \"id\": 1, \"items\": [ { \"text\": \"caf\\u00e9\", \"x\": 1, \"y\": 2, \"width\": 100 } ] } ]", "a letter beyond ASCII");
        bad("[ { \"id\": 1, \"items\": [ { \"text\": \"" + std::string(801, 'x') + "\", \"x\": 1, \"y\": 2, \"width\": 100 } ] } ]", "a text of 801 characters");
        bad("[ { \"id\": 1, \"items\": [ { \"button\": 18, \"x\": 1, \"y\": 2, \"goto\": 1 } ] } ]", "a button face of 18");
        bad("[ { \"id\": 1, \"items\": [ { \"button\": 4, \"x\": 1, \"y\": 2 } ] } ]", "a button that goes nowhere");
        bad("[ { \"id\": 1, \"items\": [ { \"button\": 4, \"x\": 1, \"y\": 2, \"goto\": 7 } ] } ]", "a button to a card the pack does not have");
        bad("[ { \"id\": 1, \"items\": [ { \"hotspot\": [10, 10, 5, 20], \"goto\": 1 } ] } ]", "a hotspot with its right left of its left");
        bad("[ { \"id\": 1, \"items\": [ { \"hotspot\": [1, 2, 3], \"goto\": 1 } ] } ]", "a hotspot of 3 numbers");
        bad("[ { \"id\": 1, \"items\": [ { \"tiles\": { \"first\": 539 }, \"x\": 1, \"y\": 2 } ] } ]", "tiles without a count");
        bad("[ { \"id\": 1, \"items\": [ { \"tiles\": { \"first\": 2048, \"count\": 1 }, \"x\": 1, \"y\": 2 } ] } ]", "a first tile of 2048");
        bad("[ { \"id\": 1, \"items\": [ { \"crop\": 16, \"x\": 1, \"y\": 2 } ] } ]", "a crop slot of 16");
        bad("[ { \"id\": 1, \"items\": [ { \"sound\": [1, 2, 30, 40] } ] } ]", "a sound spot that plays nothing");
        std::string many = "[ { \"id\": 1, \"items\": [";
        for (int i = 0; i < 61; ++i)
            many += std::string(i ? "," : "") + "{ \"picture\": 1, \"x\": 1, \"y\": 1 }";
        bad(many + "] } ]", "61 items on a card");
    }

    // ---------------------------------------------------------------- sounds
    std::printf("-- sounds\n");
    {
        auto wav = [](int samples, int rate, int channels = 1, int bits = 16) {
            std::vector<uint8_t> d = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, static_cast<uint8_t>(channels), 0 };
            auto le32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) d.push_back(static_cast<uint8_t>(v >> (8 * k))); };
            le32(static_cast<uint32_t>(rate));
            le32(static_cast<uint32_t>(rate * channels * bits / 8));
            d.push_back(static_cast<uint8_t>(channels * bits / 8)); d.push_back(0);
            d.push_back(static_cast<uint8_t>(bits)); d.push_back(0);
            d.insert(d.end(), { 'd', 'a', 't', 'a' });
            le32(static_cast<uint32_t>(samples * channels * bits / 8));
            for (int i = 0; i < samples * channels; ++i)
                if (bits == 16) { const int v = (i % 50 < 25 ? 8000 : -8000); d.push_back(static_cast<uint8_t>(v & 255)); d.push_back(static_cast<uint8_t>((v >> 8) & 255)); }
                else d.push_back(static_cast<uint8_t>(i % 50 < 25 ? 200 : 56));
            const uint32_t riff = static_cast<uint32_t>(d.size() - 8);
            for (int k = 0; k < 4; ++k) d[4 + k] = static_cast<uint8_t>(riff >> (8 * k));
            return d;
        };
        std::map<std::string, std::vector<uint8_t>> files;
        files["sounds/ding.wav"] = wav(800, 11025);
        files["sounds/stereo8.WAV"] = wav(400, 22050, 2, 8);
        files["music/tune.mid"] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xFF, 0x2F, 0 };
        files["sounds/notwav.wav"] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
        files["music/notmidi.mid"] = wav(100, 8000);
        auto reader = [](void* user, const std::string& file, std::vector<uint8_t>& bytes) {
            auto& f = *static_cast<std::map<std::string, std::vector<uint8_t>>*>(user);
            const auto it = f.find(file);
            if (it == f.end())
                return false;
            bytes = it->second;
            return true;
        };
        TableBlobs out;
        std::string e;
        const bool ok = ParsePackSounds("{ \"format\": 1, \"sounds\": { \"Click.VOC\": \"sounds/ding.wav\", \"uhoh.voc\": \"sounds/stereo8.WAV\", \"genfarm.xmi\": \"music/tune.mid\" } }", reader, &files, out, e);
        check(ok && out.size() == 3 && out.count("click.voc") && out.count("uhoh.voc") && out.count("genfarm.xmi"), "a sounds block gives the files' bytes under the names the game asks for (keys in any case)" + (ok ? "" : ": " + e));
        std::vector<int16_t> pcm;
        int rate = 0;
        check(ok && DecodeVoc(out["click.voc"], pcm, rate) && rate == 11025 && pcm.size() == 800 && pcm[0] == 8000, "  a WAV effect decodes through the engine's own DecodeVoc (rate, length, samples)");
        check(ok && DecodeVoc(out["uhoh.voc"], pcm, rate) && rate == 22050 && pcm.size() == 400, "  so does an 8-bit stereo one (mixed to mono)");
        TableBlobs none;
        check(ParsePackSounds("{ \"format\": 1 }", reader, &files, none, e) && none.empty(), "no sounds block: nothing");
        auto bad = [&](const std::string& body, const std::string& why) {
            TableBlobs b;
            std::string er;
            const bool r = !ParsePackSounds("{ \"format\": 1, \"sounds\": " + body + " }", reader, &files, b, er) && !er.empty() && b.empty();
            check(r, "rejects " + why + (r ? " (" + er + ")" : ""));
        };
        bad("5", "sounds that is not an object");
        bad("{ \"click.wav\": \"sounds/ding.wav\" }", "a name that is neither .voc nor .xmi");
        bad("{ \"a/b.voc\": \"sounds/ding.wav\" }", "a name with a folder");
        bad("{ \"averyveryverylongname.voc\": \"sounds/ding.wav\" }", "a name over 16 characters");
        bad("{ \"click.voc\": 5 }", "a file that is not text");
        bad("{ \"click.voc\": \"../ding.wav\" }", "a file that leaves the folder");
        bad("{ \"click.voc\": \"sounds/ding.mid\" }", "an effect that is not a .wav");
        bad("{ \"genfarm.xmi\": \"sounds/ding.wav\" }", "a song that is not a .mid");
        bad("{ \"click.voc\": \"sounds/missing.wav\" }", "a file that is not there");
        bad("{ \"click.voc\": \"sounds/notwav.wav\" }", "a .wav that is no WAVE file");
        bad("{ \"genfarm.xmi\": \"music/notmidi.mid\" }", "a .mid that is no MIDI file");
        bad("{ \"click.voc\": \"sounds/ding.wav\", \"uhoh.voc\": \"sounds/missing.wav\" }", "a manifest with one bad file (nothing of it is kept)");
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
        " \"overview\": { \"default\": \"#010101\", \"ranges\": [ { \"from\": 15, \"to\": 30, \"colours\": [\"#0a0b0c\", \"#0b0c0d\", \"#0c0d0e\", \"#0d0e0f\"] } ],"
        "   \"tiles\": { \"17\": [\"#000000\", \"#ffffff\", \"#050505\", \"#0a0a0a\"], \"959\": \"#f0f0f0\" } } } }";
    TableBlobs blobs;
    std::string err;
    const bool ok = ParsePackTables(wholeJson, blobs, err);
    check(ok, "a manifest with every table is made into blobs" + (ok ? "" : ": " + err));
    if (ok)
    {
        check(blobs.size() == 12 + 1 + 1 + 1 + 1 + 1, "  12 climate tables, scenario.odt, townly0.cmp, events.dat, itemtext.dat, overview.rgb");
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
        const std::vector<uint8_t>& ovBlob = blobs["overview.rgb"];
        const OverviewTable ov = MakeOverview(ovBlob, {});
        check(ovBlob.size() == static_cast<size_t>(kOverviewRgbBytes) && ov.rgb && ov.Argb(0, 0, 0) == 0xFF010101u && ov.Argb(14, 1, 1) == 0xFF010101u && ov.Argb(15, 0, 0) == 0xFF0A0B0Cu &&
                  ov.Argb(15, 0, 1) == 0xFF0B0C0Du && ov.Argb(15, 1, 0) == 0xFF0C0D0Eu && ov.Argb(30, 1, 1) == 0xFF0D0E0Fu && ov.Argb(31, 0, 1) == 0xFF010101u &&
                  ov.Argb(17, 0, 0) == 0xFF000000u && ov.Argb(17, 0, 1) == 0xFFFFFFFFu && ov.Argb(17, 1, 0) == 0xFF050505u && ov.Argb(17, 1, 1) == 0xFF0A0A0Au &&
                  ov.Argb(959, 0, 0) == 0xFFF0F0F0u && ov.Argb(959, 1, 1) == 0xFFF0F0F0u,
              "  overview: RGB, the default, a range, a 4-colour tile in order and a single-colour tile");
        check(ov.Argb(960, 0, 0) == 0xFF000000u && ov.Argb(-1, 0, 0) == 0xFF000000u && ov.Argb(0, 2, 0) == 0xFF000000u && ov.Index(5, 0, 0) == 0, "  overview: outside the table is opaque black, and an RGB table has no indices");
        bool every = true;
        for (int i = 0; i < 256; ++i)
        {
            // every value of each channel survives, in each of the four positions
            TableBlobs one;
            std::string e;
            char hex[4][8];
            for (int k = 0; k < 4; ++k)
                std::snprintf(hex[k], sizeof hex[k], "#%02x%02x%02x", (i + k * 64) & 255, (255 - i + k * 3) & 255, (i * 7 + k) & 255);
            const std::string js = std::string("{ \"format\": 1, \"tables\": { \"overview\": { \"tiles\": { \"5\": [\"") + hex[0] + "\",\"" + hex[1] + "\",\"" + hex[2] + "\",\"" + hex[3] + "\"] } } } }";
            const bool made = ParsePackTables(js, one, e);
            const OverviewTable t = MakeOverview(one["overview.rgb"], {});
            every = every && made;
            for (int k = 0; k < 4 && made; ++k)
                every = every && t.Argb(5, k / 2, k % 2) == (0xFF000000u | (static_cast<uint32_t>((i + k * 64) & 255) << 16) | (static_cast<uint32_t>((255 - i + k * 3) & 255) << 8) | static_cast<uint32_t>((i * 7 + k) & 255));
        }
        check(every, "  overview: every value of each colour channel comes back from each of the 4 positions");
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
        check(mega.Read("Events.Dat") == blobs["events.dat"] && mega.Has("RAIN0.DAT") && mega.Read("overview.rgb").size() == static_cast<size_t>(kOverviewRgbBytes), "Override replaces an entry and adds new ones, names in any case");

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
        bad("{ \"overview\": { \"default\": [\"#000\", \"#000\", \"#000\"] } }", "an overview colour of 3 pixels");
        bad("{ \"overview\": { \"default\": [0, 0, 0, 0] } }", "palette indices (the overview is RGB)");
        bad("{ \"overview\": { \"default\": \"#12\" } }", "a colour that is not #rgb or #rrggbb");
        bad("{ \"overview\": { \"default\": \"green\" } }", "a colour name");
        bad("{ \"overview\": { \"tiles\": { \"960\": \"#000\" } } }", "tile 960");
        bad("{ \"overview\": { \"tiles\": { \"x\": \"#000\" } } }", "a tile key that is not a number");
        bad("{ \"overview\": { \"ranges\": [ { \"from\": 10, \"to\": 5, \"colours\": \"#000\" } ] } }", "a range that ends before it starts");
        bad("{ \"overview\": { \"ranges\": [ { \"from\": 1, \"colours\": \"#000\" } ] } }", "a range without \"to\"");
        bad("{ \"overview\": 5 }", "an overview that is not an object");
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
            const auto itemLines = DecodeTextLines(pack["itemtext.dat"], true);
            check(pack["itemtext.dat"].size() % 128 == 0 && pack["itemtext.dat"].size() >= 104 * 128 && ItemLines(itemLines, 6)[0].find("Small Silo") == 0 &&
                      ItemLines(itemLines, 40)[0].find("Tractor") == 0 && ItemLines(itemLines, 18)[0].find("Corn") == 0 && ItemLines(itemLines, 103)[0].find("Water Tower") == 0,
                  "  the item texts, by tile family (the id the catalogue gives them): buildings, machines, crops");
            TableBlobs cards;
            bool cardsOk = LoadPackCards(packFolder, cards, e) && cards.size() == 6 && cards.count("card1.hc");
            for (const auto& [name, bytes] : cards)
                cardsOk = cardsOk && !DecodeCard(bytes).empty() && bytes.back() == 0xFF;
            check(cardsOk, "  the Farm Expert's 6 cards: a welcome card with 5 topics, each readable");
            TableBlobs sounds;
            check(LoadPackSounds(packFolder, sounds, e) && sounds.size() == 25, "  25 sound effects");
            bool decodes = true;
            std::string names;
            for (const char* name : { "bird1", "bird2", "boos", "bulldoze", "cheers", "click", "compbeep", "cow1", "cow2", "explode", "flood", "horse1", "pig", "plane", "plopitem",
                                      "ploptool", "rooster", "sheep1", "sheep2", "smack", "swarm", "thunder", "tornado", "uhoh", "wind" })
            {
                std::vector<int16_t> pcm;
                int rate = 0;
                const std::string key = std::string(name) + ".voc";
                int peak = 0;
                const bool good = sounds.count(key) && DecodeVoc(sounds[key], pcm, rate) && rate == 8000 && pcm.size() > 200 && pcm.size() < 3 * 8000;
                for (int16_t v : pcm)
                    peak = std::max(peak, std::abs(static_cast<int>(v)));
                if (!good || peak < 3000)
                    names += std::string(" ") + name;
                decodes = decodes && good && peak >= 3000;
            }
            check(decodes, "  every effect the game asks for is there and decodes: 8 kHz, under 3 s, audible" + (decodes ? "" : " (not:" + names + ")"));
            Theme theme;
            bool hasTheme = false;
            check(LoadTheme(packFolder, theme, hasTheme, e) && hasTheme && theme[12] == std::array<uint8_t, 3>({ 0xC4, 0xC4, 0xC4 }) && theme[15] == std::array<uint8_t, 3>({ 0xFF, 0xFF, 0xFF }) &&
                      theme[0] == std::array<uint8_t, 3>({ 0, 0, 0 }),
                  "  the interface theme: 16 colours, light grey panels, black and white");
            WeatherLook look;
            bool hasWeather = false;
            check(LoadWeatherLook(packFolder, look, hasWeather, e) && hasWeather && look.rain.alpha == 77 && look.rain.b == 0x8C && look.frost.alpha == 115 && look.frost.r == 0xE8,
                  "  the weather look: a slate-blue veil of 30% in rain, a pale one of 45% in frost");
            const OverviewTable shipped = MakeOverview(pack["overview.rgb"], {});
            check(shipped.rgb && shipped.Argb(40, 0, 0) == 0xFF3A5FD0u && shipped.Argb(20, 1, 1) == 0xFF80502Au,
                  "  the overview colours by family, in RGB: water blue, ground brown");
        }
    }

    std::printf("data tables %s\n", fails ? "FAIL" : "ok");
    return fails;
}

} // namespace osf
