// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/TruecolourTest.h"

#include "assets/Megadata.h"
#include "assets/PicturePack.h"
#include "assets/Png.h"
#include "assets/SaveGame.h"
#include "assets/TilePack.h"
#include "core/Json.h"
#include "ui/Gfx.h"
#include "ui/MapView.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

namespace osf {

namespace {

// ---- a PNG writer (stored deflate blocks) for the test's sheets

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0)
{
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
    {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int s = 24; s >= 0; s -= 8)
        v.push_back(static_cast<uint8_t>(x >> s));
}

void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data)
{
    Put32(png, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    png.insert(png.end(), td.begin(), td.end());
    Put32(png, Crc32(td.data(), td.size()));
}

// straight-alpha ARGB pixels -> a PNG file's bytes
std::vector<uint8_t> MakePng(const std::vector<uint32_t>& argb, int w, int h)
{
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter: none
        for (int x = 0; x < w; ++x)
        {
            const uint32_t p = argb[static_cast<size_t>(y) * w + x];
            raw.insert(raw.end(), { static_cast<uint8_t>(p >> 16), static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p), static_cast<uint8_t>(p >> 24) });
        }
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    for (size_t pos = 0; pos < raw.size() || pos == 0;)
    {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.insert(z.end(), { static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(~n), static_cast<uint8_t>((~n) >> 8) });
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
        pos += n;
        if (last)
            break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw)
    {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    Put32(z, (b << 16) | a);
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> ihdr;
    Put32(ihdr, static_cast<uint32_t>(w));
    Put32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    return png;
}

// ---- synthetic art: plain tile t is a flat colour; tile 5 is red left / blue right; masked tile m is see-through
// except a white square in its middle; event tile e is a flat colour

uint32_t PlainColour(int t) { return 0xFF000000u | (static_cast<uint32_t>((t * 37) & 0xFF) << 16) | (static_cast<uint32_t>((t * 91) & 0xFF) << 8) | static_cast<uint32_t>((t * 151) & 0xFF); }
constexpr uint32_t kTestRed = 0xFFFF0000u, kTestBlue = 0xFF0000FFu, kTestWhite = 0xFFFFFFFFu;

struct Sheets { std::vector<uint8_t> plain, masked, events; int size = 0; };

Sheets MakeSheets(int size)
{
    Sheets s;
    s.size = size;
    auto sheet = [&](int count, int columns, const std::function<uint32_t(int, int, int)>& px) {
        const int rows = (count + columns - 1) / columns, w = columns * size, h = rows * size;
        std::vector<uint32_t> a(static_cast<size_t>(w) * h, 0);
        for (int t = 0; t < count; ++t)
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    a[static_cast<size_t>((t / columns) * size + y) * w + (t % columns) * size + x] = px(t, x, y);
        return MakePng(a, w, h);
    };
    s.plain = sheet(848, 32, [&](int t, int x, int) { return t == 5 ? (x < size / 2 ? kTestRed : kTestBlue) : PlainColour(t); });
    s.masked = sheet(200, 20, [&](int, int x, int y) { return x >= size / 4 && x < size * 3 / 4 && y >= size / 4 && y < size * 3 / 4 ? kTestWhite : 0u; });
    s.events = sheet(105, 15, [&](int t, int, int) { return PlainColour(300 + t); });
    return s;
}

std::string PackJson(int size, bool events = true)
{
    return "{ \"format\": 1, \"name\": \"test \\u00e9\", \"tileSize\": " + std::to_string(size) +
           ", \"sheets\": { \"plain\": { \"file\": \"plain.png\", \"count\": 848, \"columns\": 32 },"
           " \"masked\": { \"file\": \"masked.png\", \"count\": 200, \"columns\": 20 }" +
           (events ? ", \"events\": { \"file\": \"events.png\", \"count\": 105, \"columns\": 15 }" : "") + " } }";
}

bool ReadSheet(void* user, const std::string& file, std::vector<uint8_t>& bytes)
{
    const Sheets& s = *static_cast<const Sheets*>(user);
    bytes = file == "plain.png" ? s.plain : file == "masked.png" ? s.masked : file == "events.png" ? s.events : std::vector<uint8_t>();
    return !bytes.empty();
}

bool Near(uint32_t a, uint32_t b, int tol = 2)
{
    for (int s = 0; s <= 16; s += 8)
        if (std::abs(static_cast<int>((a >> s) & 0xFF) - static_cast<int>((b >> s) & 0xFF)) > tol)
            return false;
    return true;
}

struct Target
{
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    explicit Target(int w, int h)
    {
        surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    }
    ~Target()
    {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (surface) SDL_FreeSurface(surface);
    }
    void Clear(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0)
    {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderClear(renderer);
    }
    uint32_t At(int x, int y) const
    {
        return static_cast<const uint32_t*>(surface->pixels)[static_cast<size_t>(y) * (surface->pitch / 4) + x] | 0xFF000000u;
    }
};

// a fake archive holding only the interface font, enough for Gfx::Init
// a 16 x 4 planar window picture (.bmp) of palette colour 1
std::vector<uint8_t> PlanarBitmapOfColour1()
{
    std::vector<uint8_t> d = { 2, 0, 4, 0, 4, 0, 0, 0 }; // width in bytes, height, planes, unused
    for (int plane = 0; plane < 4; ++plane)
        d.insert(d.end(), 2 * 4, plane == 0 ? 0xFF : 0x00);
    return d;
}

bool MakeFakeArchive(const std::string& dir, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& extra = {})
{
    std::filesystem::create_directories(dir);
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries = { { "EGAFONT.FNT", std::vector<uint8_t>(120 * 32, 0) } };
    entries.insert(entries.end(), extra.begin(), extra.end());
    std::vector<uint8_t> idx, data;
    auto rec = [&](const char* name, uint32_t off) {
        char n[16] = {};
        std::snprintf(n, sizeof n, "%s", name);
        idx.insert(idx.end(), n, n + 16);
        for (int s = 0; s < 32; s += 8)
            idx.push_back(static_cast<uint8_t>(off >> s));
    };
    for (const auto& [name, bytes] : entries)
    {
        rec(name.c_str(), static_cast<uint32_t>(data.size()));
        data.insert(data.end(), bytes.begin(), bytes.end());
    }
    rec("", static_cast<uint32_t>(data.size())); // the sentinel
    std::ofstream(dir + "/MEGADATA.IDX", std::ios::binary).write(reinterpret_cast<const char*>(idx.data()), static_cast<long>(idx.size()));
    std::ofstream(dir + "/MEGADATA.EEA", std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<long>(data.size()));
    return true;
}

} // namespace

int TruecolourSelfTest()
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
        fails += ok ? 0 : 1;
    };

    // ---------------------------------------------------------------- JSON
    std::printf("-- JSON\n");
    {
        Json j;
        std::string err;
        check(ParseJson("\xEF\xBB\xBF{\"a\": [1, 2.5e1, -3], \"b\": {\"c\": \"x\\n\\u00e9\\ud83d\\ude00\"}, \"d\": true, \"e\": null}", j, err), "a valid document parses");
        check(j.IsObject() && j.Get("a") && j.Get("a")->array.size() == 3 && j.Get("a")->array[1].number == 25 && j.Get("a")->array[2].number == -3, "arrays and numbers");
        check(j.Get("b") && j.Get("b")->Str("c") == "x\n\xC3\xA9\xF0\x9F\x98\x80", "string escapes and surrogate pairs become UTF-8");
        check(j.Int("zz", 7) == 7 && j.Int("d", 9) == 9 && j.Str("a", "f") == "f", "typed reads fall back for missing or mistyped members");
        for (const char* bad : { "", "{", "{\"a\":}", "[1,]", "{\"a\":1} x", "\"abc", "01", "1.", "{\"a\" 1}", "\"\\ud800\"", "tru", "\"\\q\"" })
        {
            Json k;
            std::string e;
            check(!ParseJson(bad, k, e) && !e.empty(), std::string("rejects: ") + bad);
        }
        std::string deep(100, '[');
        Json k;
        std::string e;
        check(!ParseJson(deep, k, e), "rejects nesting 100 deep");
    }

    // ---------------------------------------------------------------- the pack loader
    std::printf("-- tile pack loader\n");
    for (const int size : { 8, 16, 32, 64 })
    {
        Sheets s = MakeSheets(size);
        TilePackSheets out;
        std::string err;
        const bool ok = ParseTilePack(PackJson(size), ReadSheet, &s, out, err);
        check(ok, "a " + std::to_string(size) + " px pack loads" + (ok ? "" : ": " + err));
        if (!ok)
            continue;
        check(out.plain.TrueColour() && out.plain.count == 848 && out.plain.size == size && out.masked.count == 200 && out.events.count == 105 && out.hasEvents,
              "  sheet shapes");
        check(out.plain.TileArgb(0)[0] == PlainColour(0) && out.plain.TileArgb(847)[size * size - 1] == PlainColour(847) &&
                  out.plain.TileArgb(33)[5] == PlainColour(33),
              "  tiles read from their grid cell (first, last, second row)");
        const uint32_t* t5 = out.plain.TileArgb(5);
        check(t5[0] == kTestRed && t5[size - 1] == kTestBlue, "  left and right are not swapped");
        check(out.masked.TileArgb(7)[0] == 0 && out.masked.TileArgb(7)[(size / 2) * size + size / 2] == kTestWhite, "  the PNG's alpha is kept");
        check(out.name == "test \xC3\xA9", "  the name");
    }
    {
        Sheets s = MakeSheets(32);
        TilePackSheets out;
        std::string err;
        auto fails_with = [&](const std::string& json, const std::string& why) {
            err.clear();
            check(!ParseTilePack(json, ReadSheet, &s, out, err) && !err.empty(), "rejects " + why + " (" + err + ")");
        };
        std::string good = PackJson(32);
        auto sub = [&](const std::string& from, const std::string& to) { std::string r = good; r.replace(r.find(from), from.size(), to); return r; };
        fails_with(sub("\"format\": 1", "\"format\": 2"), "another format");
        fails_with(sub("\"tileSize\": 32", "\"tileSize\": 200"), "a tile size over 128");
        fails_with(sub("\"tileSize\": 32", "\"tileSize\": 4"), "a tile size under 8");
        fails_with(sub("\"tileSize\": 32", "\"tileSize\": 64"), "a tile size larger than the images allow");
        fails_with(sub("plain.png", "../plain.png"), "a file name that leaves the folder");
        fails_with(sub("masked.png", "sub/masked.png"), "a file in a folder that is not there");
        fails_with(sub("masked.png", "/masked.png"), "an absolute file name");
        fails_with(sub("masked.png", "a\\\\masked.png"), "a backslash in a file name");
        fails_with(sub("masked.png", "C:masked.png"), "a drive letter in a file name");
        fails_with(sub("\"count\": 848", "\"count\": 100"), "too few plain tiles");
        fails_with(sub("\"columns\": 20", "\"columns\": 0"), "no columns");
        fails_with(sub("events.png", "missing.png"), "an unreadable sheet");
        fails_with(sub("\"masked\"", "\"maskd\""), "a missing required sheet");
        s.masked = { 1, 2, 3 };
        fails_with(good, "a file that is not a PNG");
    }
    {
        Sheets s = MakeSheets(32);
        TilePackSheets out;
        std::string err;
        const bool ok = ParseTilePack(PackJson(32, false), ReadSheet, &s, out, err);
        check(ok && !out.hasEvents, "the events sheet is optional" + (ok ? "" : ": " + err));
    }

    // ---------------------------------------------------------------- the map view
    std::printf("-- map view\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0 && SDL_Init(0) != 0)
    {
        std::printf("  FAIL SDL cannot start\n");
        return fails + 1;
    }
    for (const int size : { 16, 32, 64 })
        for (const bool modern : { false, true })
        {
            Sheets s = MakeSheets(size);
            TilePackSheets pack;
            std::string err;
            if (!ParseTilePack(PackJson(size), ReadSheet, &s, pack, err))
            {
                check(false, "pack for the map view: " + err);
                continue;
            }
            MapGraphics gfx;
            gfx.plain = pack.plain;
            gfx.masked = pack.masked;
            gfx.events = pack.events;
            const std::string label = std::to_string(size) + " px tiles, " + (modern ? "Modern Render" : "classic") + ": ";
            World world;
            world.Resize(8);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 8; ++y)
                    world.At(x, y).base = static_cast<uint16_t>(x * 8 + y);
            world.At(2, 3).base |= 0x0800; // an object over cell (2, 3): masked tile 0
            world.At(2, 3).overlay = 0x350;
            world.At(4, 4).base = 5;      // the half-red half-blue tile
            Target t(8 * 32, 8 * 32);
            MapView v;
            check(v.Init(t.renderer, gfx), label + "the view starts on a truecolour pack");
            v.SetWorld(&world);
            v.SetModern(modern);
            v.SetCamera(0, 0);
            const int cell = static_cast<int>(std::lround(16 * v.Zoom()));
            t.Clear(0, 255, 0);
            v.Draw(t.renderer, 8 * cell, 8 * cell);
            SDL_RenderPresent(t.renderer);
            const auto centre = [&](int cx, int cy) { return t.At(cx * cell + cell / 2, cy * cell + cell / 2); };
            check(Near(centre(0, 0), PlainColour(0)) && Near(centre(7, 7), PlainColour(63)) && Near(centre(1, 2), PlainColour(10)), label + "cells show their tiles");
            check(Near(t.At(4 * cell + cell / 4, 4 * cell + cell / 2), kTestRed) && Near(t.At(4 * cell + cell * 3 / 4, 4 * cell + cell / 2), kTestBlue),
                  label + "a tile with two halves keeps both halves in place");
            check(Near(centre(2, 3), kTestWhite) && Near(t.At(2 * cell + 1, 3 * cell + 1), PlainColour(2 * 8 + 3)), label + "an object's see-through corners show the ground");
            // a tile that changes (a new picture for tile 20) shows at once
            MapGraphics changed = gfx;
            for (int i = 0; i < size * size; ++i)
                changed.plain.argb[static_cast<size_t>(20) * size * size + i] = 0xFF123456u;
            v.SetPlainTiles(t.renderer, changed);
            t.Clear(0, 255, 0);
            v.Draw(t.renderer, 8 * cell, 8 * cell);
            SDL_RenderPresent(t.renderer);
            check(Near(centre(2, 4), 0xFF123456u) && Near(centre(2, 5), PlainColour(21)), label + "a changed tile is redrawn, its neighbours are not touched");
        }

    // zoomed in and out: the smooth atlas for each zoom level keeps the colours (no bleeding from neighbours)
    {
        Sheets s = MakeSheets(64);
        TilePackSheets pack;
        std::string err;
        ParseTilePack(PackJson(64), ReadSheet, &s, pack, err);
        MapGraphics gfx;
        gfx.plain = pack.plain;
        gfx.masked = pack.masked;
        World world;
        world.Resize(8);
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
                world.At(x, y).base = static_cast<uint16_t>(x * 8 + y);
        Target t(512, 512);
        MapView v;
        v.Init(t.renderer, gfx);
        v.SetWorld(&world);
        // every zoom level (0.75 .. 3), the view exactly the size of the 8 x 8 world
        for (const int dir : { -1, -1, -1, -1, 1, 1, 1, 1, 1 })
        {
            const float before = v.Zoom();
            v.ZoomStep(dir, 0, 0);
            if (v.Zoom() == before)
                continue;
            const int cell = static_cast<int>(std::lround(16 * v.Zoom()));
            t.Clear(0, 255, 0);
            v.Draw(t.renderer, 8 * cell, 8 * cell);
            SDL_RenderPresent(t.renderer);
            bool all = true;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 8; ++y)
                {
                    if (x * 8 + y == 5)
                        continue; // (the two-coloured tile)
                    const bool cellOk = Near(t.At(x * cell + cell / 2, y * cell + cell / 2), PlainColour(x * 8 + y)) &&
                                        Near(t.At(x * cell, y * cell), PlainColour(x * 8 + y)) &&
                                        Near(t.At(x * cell + cell - 1, y * cell + cell - 1), PlainColour(x * 8 + y));
                    if (!cellOk)
                        std::printf("       cell (%d, %d): got %08X, wanted %08X\n", x, y, t.At(x * cell + cell / 2, y * cell + cell / 2), PlainColour(x * 8 + y));
                    all = all && cellOk;
                }
            char zoomText[16];
            std::snprintf(zoomText, sizeof zoomText, "%.2f", static_cast<double>(v.Zoom()));
            check(all, std::string("zoom ") + zoomText + " (" + std::to_string(cell) + " px cells): centres and edges have the tile's colour, no bleed");
        }
    }

    // ---------------------------------------------------------------- the interface's tiles
    std::printf("-- interface tiles\n");
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "osf_tilepacktest").string();
        MakeFakeArchive(dir);
        Megadata mega;
        std::string err;
        if (!mega.Open(dir, err))
            check(false, "the fake archive opens: " + err);
        else
        {
            Sheets s = MakeSheets(64);
            TilePackSheets pack;
            ParseTilePack(PackJson(64), ReadSheet, &s, pack, err);
            MapGraphics gfx;
            gfx.plain = pack.plain;
            gfx.masked = pack.masked;
            Target t(512, 128);
            Gfx g;
            check(g.Init(t.renderer, mega, gfx), "the interface starts on a truecolour pack");
            for (const int scale : { 1, 2, 3 })
            {
                g.SetScale(scale);
                t.Clear(0, 255, 0);
                g.DrawTile(7, 2, 2, 16);
                g.DrawAnyTile(848, 40, 2, 16);
                g.DrawTile(5, 70, 2, 16);
                SDL_RenderPresent(t.renderer);
                const std::string label = "scale " + std::to_string(scale) + ": ";
                check(Near(t.At((2 + 8) * scale, (2 + 8) * scale), PlainColour(7)), label + "DrawTile shows the tile");
                check(Near(t.At((40 + 8) * scale, (2 + 8) * scale), kTestWhite) && Near(t.At((40 + 1) * scale, (2 + 1) * scale), 0xFF00FF00u),
                      label + "DrawAnyTile shows the masked tile over what is behind it");
                check(Near(t.At((70 + 3) * scale, (2 + 8) * scale), kTestRed) && Near(t.At((70 + 13) * scale, (2 + 8) * scale), kTestBlue), label + "halves in place");
            }
            g.SetPlayPalette(1); // (no archive palette to read here: it must simply do nothing harmful)
            t.Clear(0, 255, 0);
            g.DrawTile(7, 2, 2, 16);
            SDL_RenderPresent(t.renderer);
            check(Near(t.At((2 + 8) * 3, (2 + 8) * 3), PlainColour(7)), "a palette change leaves the truecolour tiles as they were");
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // ---------------------------------------------------------------- pack pictures
    std::printf("-- pack pictures: the manifest\n");
    // pictures the test's reader serves
    std::map<std::string, std::vector<uint8_t>> files;
    auto flat = [](int w, int h, const std::function<uint32_t(int, int)>& px) {
        std::vector<uint32_t> a(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                a[static_cast<size_t>(y) * w + x] = px(x, y);
        return MakePng(a, w, h);
    };
    // a window at 2 pixels per unit: red left, blue right, the bottom row see-through
    files["ui/win.png"] = flat(64, 48, [](int x, int y) { return y >= 46 ? 0u : x < 32 ? kTestRed : kTestBlue; });
    // four 16 x 16 cells (2 x 2): flat colours
    files["ui/sprite.png"] = flat(32, 32, [](int x, int y) { return PlainColour((y / 16) * 2 + x / 16); });
    // a "full screen" of 160 x 120 pixels, a colour a quadrant
    files["ui/title.png"] = flat(160, 120, [](int x, int y) { return PlainColour(100 + (y / 60) * 2 + x / 80); });
    files["ui/notpng.png"] = { 1, 2, 3, 4 };
    auto reader = [](void* user, const std::string& file, std::vector<uint8_t>& bytes) {
        auto& f = *static_cast<std::map<std::string, std::vector<uint8_t>>*>(user);
        const auto it = f.find(file);
        if (it == f.end())
            return false;
        bytes = it->second;
        return true;
    };
    const std::string pictures =
        "{ \"format\": 1, \"pictures\": { \"scale\": 2, \"single\": {"
        " \"BuyWndo.bmp\": { \"file\": \"ui/win.png\" },"
        " \"popup.bmp\": { \"file\": \"ui/win.png\", \"size\": [100, 50] },"
        " \"about.bmp\": { \"file\": \"ui/win.png\", \"scale\": 4 },"
        " \"sftitle.int\": { \"file\": \"ui/title.png\", \"scale\": 0.25 } },"
        " \"sets\": [ { \"key\": \"airplane.spr@{n}\", \"file\": \"ui/sprite.png\", \"count\": 4, \"columns\": 2, \"first\": 10 },"
        " { \"key\": \"buttons.til${n}\", \"file\": \"ui/sprite.png\", \"count\": 4, \"columns\": 2, \"cell\": [48, 24] } ] } }";
    {
        PicturePack pack;
        std::string err;
        const bool ok = ParsePicturePack(pictures, reader, &files, pack, err);
        check(ok, "a manifest with singles and sets loads" + (ok ? "" : ": " + err));
        const PackPicture* w = pack.Find("buywndo.bmp");
        check(w && w == pack.Find("BUYWNDO.BMP") && pack.Size() == 4 + 4 + 4, "  keys are matched without regard to case; 12 pictures");
        check(w && w->image.w == 64 && w->image.h == 48 && w->logicalW == 32 && w->logicalH == 24, "  the default scale halves the size: 32 x 24 units");
        check(pack.Find("popup.bmp") && pack.Find("popup.bmp")->logicalW == 100 && pack.Find("popup.bmp")->logicalH == 50, "  an explicit size wins");
        check(pack.Find("about.bmp") && pack.Find("about.bmp")->logicalW == 16 && pack.Find("about.bmp")->logicalH == 12, "  a picture's own scale wins over the default");
        check(pack.Find("sftitle.int") && pack.Find("sftitle.int")->logicalW == 640 && pack.Find("sftitle.int")->logicalH == 480, "  a scale of 0.25 makes 160 x 120 pixels a 640 x 480 screen");
        const PackPicture* f = pack.Find("airplane.spr@12");
        check(!pack.Find("airplane.spr@0") && pack.Find("airplane.spr@10") && f && f->image.w == 16 && f->image.argb[0] == PlainColour(2) && f->logicalW == 8,
              "  a set: {n} counts from \"first\", each cell cut from its grid place, 8 x 8 units at the scale of 2");
        check(pack.Find("buttons.til$3") && pack.Find("buttons.til$3")->logicalW == 48 && pack.Find("buttons.til$3")->image.argb[0] == PlainColour(3), "  a set with a \"cell\" size");
        const PackPicture* t = pack.Find("sftitle.int");
        check(t && t->image.argb[0] == PlainColour(100) && t->image.argb[159] == PlainColour(101) && t->image.argb[119 * 160] == PlainColour(102) &&
                  t->image.argb[119 * 160 + 159] == PlainColour(103),
              "  pixels are in place");
    }
    {
        PicturePack pack;
        std::string err;
        check(ParsePicturePack("{ \"format\": 1 }", reader, &files, pack, err) && pack.Size() == 0, "a manifest without pictures gives an empty pack");
        TilePackSheets sheets;
        check(ParseTilePack("{ \"format\": 1, \"pictures\": {} }", reader, &files, sheets, err) && !sheets.hasSheets, "a pictures-only manifest is a good tile pack with no tiles");
        auto bad = [&](const std::string& json, const std::string& why) {
            PicturePack p;
            std::string e;
            check(!ParsePicturePack(json, reader, &files, p, e) && !e.empty() && p.Size() == 0, "rejects " + why + " (" + e + ")");
        };
        const std::string one = "{ \"format\": 1, \"pictures\": { \"single\": { \"a.bmp\": ";
        bad(one + "{ \"file\": \"ui/missing.png\" } } } }", "a missing file");
        bad(one + "{ \"file\": \"ui/notpng.png\" } } } }", "a file that is not a PNG");
        bad(one + "{ \"file\": \"../win.png\" } } } }", "a file name that leaves the folder");
        bad(one + "{ \"file\": \"ui/win.png\", \"scale\": 0 } } } }", "a scale of 0");
        bad(one + "{ \"file\": \"ui/win.png\", \"scale\": 100 } } } }", "a scale of 100");
        bad(one + "{ \"file\": \"ui/win.png\", \"size\": [0, 5] } } } }", "a size of 0");
        bad(one + "{ \"file\": \"ui/win.png\", \"size\": [5] } } } }", "a size that is not a pair");
        bad(one + "{ \"file\": \"ui/win.png\", \"size\": [1.5, 2] } } } }", "a size that is not whole");
        bad(one + "7 } } }", "an entry that is not an object");
        bad("{ \"format\": 1, \"pictures\": { \"scale\": 99 } }", "a default scale of 99");
        bad("{ \"format\": 1, \"pictures\": 5 }", "pictures that is not an object");
        bad("{ \"format\": 1, \"pictures\": { \"sets\": [ { \"key\": \"x\", \"file\": \"ui/sprite.png\", \"count\": 4 } ] } }", "a set key without {n}");
        bad("{ \"format\": 1, \"pictures\": { \"sets\": [ { \"key\": \"x{n}\", \"file\": \"ui/sprite.png\", \"count\": 0 } ] } }", "a set of no pictures");
        bad("{ \"format\": 1, \"pictures\": { \"sets\": [ { \"key\": \"x{n}\", \"file\": \"ui/sprite.png\", \"count\": 5000 } ] } }", "a set of 5000");
        bad("{ \"format\": 1, \"pictures\": { \"sets\": [ { \"key\": \"x{n}\", \"file\": \"ui/sprite.png\", \"count\": 64, \"columns\": 64 } ] } }", "a set with more cells than pixels");
        // all or nothing: a good picture before a bad one is not kept
        PicturePack p;
        std::string e;
        check(!ParsePicturePack("{ \"format\": 1, \"pictures\": { \"single\": { \"a.bmp\": { \"file\": \"ui/win.png\" }, \"b.bmp\": { \"file\": \"nope.png\" } } } }",
                                reader, &files, p, e) && p.Size() == 0,
              "a failing manifest adds nothing");
    }
    check(IsPackFileName("plain.png") && IsPackFileName("ui/x/y.png") && !IsPackFileName("") && !IsPackFileName("../x.png") && !IsPackFileName("a/../b.png") &&
              !IsPackFileName("/x.png") && !IsPackFileName("x/") && !IsPackFileName("a//b.png") && !IsPackFileName("a\\b.png") && !IsPackFileName("c:x.png") &&
              !IsPackFileName("./x.png"),
          "pack file names: relative, forward slashes, no ..");

    std::printf("-- pack pictures: the interface draws them\n");
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "osf_tilepacktest3").string();
        MakeFakeArchive(dir, { { "examine.bmp", PlanarBitmapOfColour1() } });
        Megadata mega;
        std::string err;
        PicturePack pack;
        if (!mega.Open(dir, err) || !ParsePicturePack(pictures, reader, &files, pack, err))
            check(false, "the archive and the pack load: " + err);
        else
        {
            MapGraphics gfx;
            for (int i = 0; i < 16; ++i)
                gfx.palette[static_cast<size_t>(i)] = { static_cast<uint8_t>(i * 16), static_cast<uint8_t>(255 - i * 16), static_cast<uint8_t>(i * 7 + 40) };
            gfx.plain.count = 848;
            gfx.plain.pixels.assign(848 * 256, 0);
            gfx.masked.count = 200;
            gfx.masked.pixels.assign(200 * 256, kSeeThrough);
            Target t(1920, 1440);
            Gfx g;
            check(g.Init(t.renderer, mega, gfx), "the interface starts");
            g.SetPicturePack(&pack);
            check(g.PicturePackInUse() == &pack, "the pack is in use");
            const uint32_t green = 0xFF00FF00u;
            for (const int scale : { 1, 2, 3 })
            {
                g.SetScale(scale);
                const std::string label = "scale " + std::to_string(scale) + ": ";
                int pw = 0, ph = 0;
                check(g.PictureSize("buywndo.bmp", pw, ph) && pw == 32 && ph == 24, label + "PictureSize gives the interface size, not the pixels'");
                t.Clear(0, 255, 0);
                g.DrawPicture("buywndo.bmp", 10, 20);
                SDL_RenderPresent(t.renderer);
                check(Near(t.At((10 + 8) * scale, (20 + 10) * scale), kTestRed) && Near(t.At((10 + 24) * scale, (20 + 10) * scale), kTestBlue) &&
                          Near(t.At((10 + 31) * scale, (20 + 10) * scale), kTestBlue) && Near(t.At((10 + 32) * scale + 1, (20 + 10) * scale), green),
                      label + "a picture fills exactly its interface rectangle, red left and blue right");
                check(Near(t.At((10 + 8) * scale, (20 + 23) * scale + scale / 2), green), label + "a see-through row shows what is behind");
                // a part: the right half (x 16..31, y 0..11) drawn at (100, 100)
                t.Clear(0, 255, 0);
                g.DrawPicturePart("buywndo.bmp", 16, 0, 16, 12, 100, 100);
                SDL_RenderPresent(t.renderer);
                check(Near(t.At((100 + 2) * scale, (100 + 2) * scale), kTestBlue) && Near(t.At((100 + 13) * scale, (100 + 9) * scale), kTestBlue) &&
                          Near(t.At((100 + 17) * scale, (100 + 2) * scale), green) && Near(t.At((100 - 1) * scale, (100 + 2) * scale), green),
                      label + "a part of a picture, cut at the right place");
                // a sprite frame to a window rectangle, a full screen to a rectangle
                t.Clear(0, 255, 0);
                g.DrawSprite("airplane.spr", 13, SDL_Rect{ 300, 300, 40, 40 });
                g.DrawFullScreen("sftitle.int", "sftitle.pal", SDL_Rect{ 0, 0, 640 * scale, 480 * scale });
                SDL_RenderPresent(t.renderer);
                check(Near(t.At(10 * scale, 10 * scale), PlainColour(100)) && Near(t.At(630 * scale, 10 * scale), PlainColour(101)) &&
                          Near(t.At(10 * scale, 470 * scale), PlainColour(102)) && Near(t.At(630 * scale, 470 * scale), PlainColour(103)),
                      label + "a full-screen picture drawn from the pack, the pack's colours");
                t.Clear(0, 255, 0);
                g.DrawSprite("airplane.spr", 13, SDL_Rect{ 300, 300, 40, 40 });
                SDL_RenderPresent(t.renderer);
                check(Near(t.At(320, 320), PlainColour(3)) && Near(t.At(301, 301), PlainColour(3)), label + "a sprite frame from a set");
                // the archive's own picture, which the pack does not have, still draws (and survives scale changes)
                t.Clear(0, 255, 0);
                g.DrawPicture("examine.bmp", 200, 200);
                SDL_RenderPresent(t.renderer);
                const auto& c = gfx.palette[1];
                check(Near(t.At(204 * scale, 201 * scale), 0xFF000000u | (static_cast<uint32_t>(c[0]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | c[2]),
                      label + "a picture the pack lacks still comes from the archive");
                // nothing anywhere: no crash, nothing drawn
                t.Clear(0, 255, 0);
                g.DrawPicture("nowhere.bmp", 5, 5);
                SDL_RenderPresent(t.renderer);
                check(Near(t.At(8 * scale, 8 * scale), green), label + "a picture neither has draws nothing");
            }
            // a detail change (Modern Render) remakes the pictures at the new size
            g.SetScale(1);
            g.SetDetail(2);
            t.Clear(0, 255, 0);
            g.DrawPicture("buywndo.bmp", 10, 20);
            SDL_RenderPresent(t.renderer);
            check(Near(t.At(18, 30), kTestRed) && Near(t.At(34, 30), kTestBlue), "after SetDetail the pack's picture draws the same");
            // with no pack the archive's picture of that key is used again (this archive has none: nothing)
            g.SetPicturePack(nullptr);
            t.Clear(0, 255, 0);
            g.DrawPicture("buywndo.bmp", 10, 20);
            SDL_RenderPresent(t.renderer);
            check(Near(t.At(18, 30), green), "without the pack its pictures are gone");
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // ---------------------------------------------------------------- the original (indexed) tiles still draw as before
    std::printf("-- indexed tiles (the original art's path, on synthetic tiles)\n");
    {
        MapGraphics gfx;
        for (int i = 0; i < 16; ++i)
            gfx.palette[static_cast<size_t>(i)] = { static_cast<uint8_t>(i * 16), static_cast<uint8_t>(255 - i * 16), static_cast<uint8_t>(i * 7 + 40) };
        auto colour = [&](int idx) {
            const auto& c = gfx.palette[static_cast<size_t>(idx)];
            return 0xFF000000u | (static_cast<uint32_t>(c[0]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | c[2];
        };
        gfx.plain.count = 848;
        gfx.plain.pixels.assign(848 * 256, 0);
        for (int t = 0; t < 848; ++t)
            for (int i = 0; i < 256; ++i)
                gfx.plain.pixels[static_cast<size_t>(t) * 256 + i] = static_cast<uint8_t>(t % 15 + 1);
        gfx.masked.count = 200;
        gfx.masked.pixels.assign(200 * 256, kSeeThrough);
        for (int t = 0; t < 200; ++t)
            for (int y = 4; y < 12; ++y)
                for (int x = 4; x < 12; ++x)
                    gfx.masked.pixels[static_cast<size_t>(t) * 256 + y * 16 + x] = 3;
        gfx.events.count = 105;
        gfx.events.pixels.assign(105 * 256, 2);
        World world;
        world.Resize(8);
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
                world.At(x, y).base = static_cast<uint16_t>(x * 8 + y);
        world.At(2, 3).base |= 0x0800;
        world.At(2, 3).overlay = 0x350;
        for (const bool modern : { false, true })
        {
            Target t(256, 256);
            MapView v;
            v.Init(t.renderer, gfx);
            v.SetWorld(&world);
            v.SetModern(modern);
            v.SetCamera(0, 0);
            t.Clear(0, 255, 0);
            v.Draw(t.renderer, 256, 256);
            SDL_RenderPresent(t.renderer);
            const std::string label = modern ? "Modern Render: " : "classic: ";
            check(Near(t.At(16, 16), colour(0 % 15 + 1)) && Near(t.At(7 * 32 + 16, 7 * 32 + 16), colour(63 % 15 + 1)),
                  label + "indexed map cells take their palette colours");
            check(Near(t.At(2 * 32 + 16, 3 * 32 + 16), colour(3)) && Near(t.At(2 * 32 + 1, 3 * 32 + 1), colour(19 % 15 + 1)),
                  label + "an indexed object's see-through pixels show the ground");
            MapGraphics changed = gfx;
            changed.palette[7] = { 0, 0, 255 }; // a rain day: colour 7 changes
            for (int i = 0; i < 256; ++i)
                changed.plain.pixels[static_cast<size_t>(20) * 256 + i] = 7;
            v.SetPlainTiles(t.renderer, changed);
            t.Clear(0, 255, 0);
            v.Draw(t.renderer, 256, 256);
            SDL_RenderPresent(t.renderer);
            check(Near(t.At(2 * 32 + 16, 4 * 32 + 16), 0xFF0000FFu), label + "a changed indexed tile and palette are redrawn");
        }
        const std::string dir = (std::filesystem::temp_directory_path() / "osf_tilepacktest2").string();
        MakeFakeArchive(dir);
        Megadata mega;
        std::string err;
        if (mega.Open(dir, err))
        {
            Target t(512, 128);
            Gfx g;
            check(g.Init(t.renderer, mega, gfx), "the interface starts on indexed tiles");
            for (const int detail : { 1, 2, 4 })
                for (const int scale : { 1, 2 })
                {
                    g.SetDetail(detail);
                    g.SetScale(scale);
                    t.Clear(0, 255, 0);
                    g.DrawTile(7, 2, 2, 16);
                    g.DrawTile(863 - 848 + 0, 40, 2, 16);
                    g.DrawAnyTile(848 + 1, 70, 2, 16);
                    SDL_RenderPresent(t.renderer);
                    check(Near(t.At((2 + 8) * scale, (2 + 8) * scale), colour(7 % 15 + 1)) && Near(t.At((70 + 8) * scale, (2 + 8) * scale), colour(3)) &&
                              Near(t.At((70 + 1) * scale, (2 + 1) * scale), 0xFF00FF00u),
                          "detail " + std::to_string(detail) + ", scale " + std::to_string(scale) + ": plain and masked tiles draw in their palette colours");
                }
        }
        else
            check(false, "the fake archive opens: " + err);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    SDL_Quit();
    std::printf("truecolour tiles %s\n", fails ? "FAIL" : "ok");
    return fails;
}

int PackPreview(const char* folder, const char* prefix)
{
    MapGraphics gfx;
    std::string err;
    if (!LoadTilePack(folder, gfx, err))
    {
        std::printf("%s\n", err.c_str());
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0 && SDL_Init(0) != 0)
    {
        std::printf("SDL cannot start\n");
        return 1;
    }
    std::printf("%s: %d px tiles, %d plain, %d masked, %d event\n", folder, gfx.plain.size, gfx.plain.count, gfx.masked.count, gfx.events.count);
    int failed = 0;
    for (const bool masked : { false, true })
    {
        const int n = masked ? 20 : 32;
        World world;
        world.Resize(n);
        for (int x = 0; x < n; ++x)
            for (int y = 0; y < n; ++y)
            {
                MapCell& c = world.At(x, y);
                const int i = y * n + x;
                if (masked)
                {
                    c.base = static_cast<uint16_t>(20 | (i < gfx.masked.count ? 0x0800 : 0));
                    c.overlay = static_cast<uint16_t>(0x350 + i);
                }
                else
                    c.base = static_cast<uint16_t>(i < gfx.plain.count ? i : 20);
            }
        MapView v;
        MapGraphics sheetOnly = gfx;
        const int cell = 32; // the default zoom (2): 32 px a cell
        Target t(n * cell, n * cell);
        if (!t.renderer || !v.Init(t.renderer, sheetOnly))
        {
            std::printf("the map view cannot start\n");
            failed = 1;
            continue;
        }
        v.SetWorld(&world);
        v.SetCamera(0, 0);
        t.Clear(255, 0, 255);
        v.Draw(t.renderer, n * cell, n * cell);
        SDL_RenderPresent(t.renderer);
        const std::string out = std::string(prefix) + (masked ? "-masked.bmp" : "-plain.bmp");
        if (SDL_SaveBMP(t.surface, out.c_str()) != 0)
        {
            std::printf("cannot write %s\n", out.c_str());
            failed = 1;
        }
        else
            std::printf("wrote %s\n", out.c_str());
    }
    // the pictures: the pack's windows, faces and sprites drawn by the interface, laid out in rows
    PicturePack pictures;
    if (!LoadPicturePack(folder, pictures, err))
    {
        std::printf("%s\n", err.c_str());
        failed = 1;
    }
    else if (pictures.Size())
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "osf_packpreview").string();
        MakeFakeArchive(dir);
        Megadata mega;
        Target t(1300, 1500);
        Gfx g;
        if (!mega.Open(dir, err) || !g.Init(t.renderer, mega, gfx))
        {
            std::printf("the interface cannot start: %s\n", err.c_str());
            failed = 1;
        }
        else
        {
            g.SetPicturePack(&pictures);
            t.Clear(0x30, 0x40, 0x30);
            int x = 4, y = 4, rowH = 0;
            auto place = [&](int w, int h) {
                if (x + w > 1296)
                {
                    x = 4;
                    y += rowH + 4;
                    rowH = 0;
                }
                const SDL_Point at{ x, y };
                x += w + 4;
                rowH = std::max(rowH, h);
                return at;
            };
            int shown = 0;
            for (const auto& [key, pic] : pictures.All())
            {
                const bool full = key.size() > 4 && key.compare(key.size() - 4, 4, ".int") == 0;
                const bool sprite = key.find('@') != std::string::npos;
                if (full)
                {
                    const SDL_Point at = place(160, 120);
                    g.DrawFullScreen(key, "none.pal", SDL_Rect{ at.x, at.y, 160, 120 });
                }
                else if (sprite)
                {
                    const SDL_Point at = place(pic.logicalW, pic.logicalH);
                    const size_t cut = key.find('@');
                    g.DrawSprite(key.substr(0, cut), std::atoi(key.c_str() + cut + 1), SDL_Rect{ at.x, at.y, pic.logicalW, pic.logicalH });
                }
                else
                {
                    const SDL_Point at = place(pic.logicalW, pic.logicalH);
                    g.DrawPicture(key, at.x, at.y);
                }
                ++shown;
            }
            const std::string out = std::string(prefix) + "-pictures.bmp";
            if (SDL_SaveBMP(t.surface, out.c_str()) != 0)
                failed = 1;
            else
                std::printf("wrote %s (%d pictures)\n", out.c_str(), shown);
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    SDL_Quit();
    return failed;
}

} // namespace osf
