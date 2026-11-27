// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/TilePack.h"

#include "core/Json.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "third_party/stb_image.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace osf {

namespace {
constexpr int kMinTile = 8, kMaxTile = 128;
constexpr int kPlainTiles = 848, kMaskedTiles = 200, kEventTiles = 105; // the original game's sheets
}

bool DecodeTileSheetPng(const std::vector<uint8_t>& png, int tileSize, int columns, int count, TileSheet& out, std::string& error)
{
    if (tileSize < kMinTile || tileSize > kMaxTile || columns < 1 || columns > 1024 || count < 1 || count > 4096)
    {
        error = "a sheet's tile size, columns or count is out of range";
        return false;
    }
    int w = 0, h = 0, comp = 0;
    if (png.empty() || !stbi_info_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &comp))
    {
        error = "not a readable PNG";
        return false;
    }
    const int rows = (count + columns - 1) / columns;
    if (static_cast<long long>(w) < static_cast<long long>(columns) * tileSize || static_cast<long long>(h) < static_cast<long long>(rows) * tileSize)
    {
        error = "the image is " + std::to_string(w) + " x " + std::to_string(h) + ", too small for " + std::to_string(count) + " tiles of " +
                std::to_string(tileSize) + " in " + std::to_string(columns) + " columns (" + std::to_string(columns * tileSize) + " x " +
                std::to_string(rows * tileSize) + " needed)";
        return false;
    }
    stbi_uc* rgba = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &comp, 4);
    if (!rgba)
    {
        error = std::string("the PNG cannot be decoded: ") + stbi_failure_reason();
        return false;
    }
    TileSheet sheet;
    sheet.count = count;
    sheet.size = tileSize;
    sheet.argb.resize(static_cast<size_t>(count) * tileSize * tileSize);
    for (int t = 0; t < count; ++t)
    {
        const int ox = (t % columns) * tileSize, oy = (t / columns) * tileSize;
        uint32_t* dst = &sheet.argb[static_cast<size_t>(t) * tileSize * tileSize];
        for (int y = 0; y < tileSize; ++y)
            for (int x = 0; x < tileSize; ++x)
            {
                const stbi_uc* p = rgba + (static_cast<size_t>(oy + y) * w + ox + x) * 4;
                dst[y * tileSize + x] = (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
            }
    }
    stbi_image_free(rgba);
    out = std::move(sheet);
    return true;
}

bool ParseTilePack(const std::string& text, bool (*read)(void*, const std::string&, std::vector<uint8_t>&), void* user, TilePackSheets& out,
                   std::string& error)
{
    Json root;
    if (!ParseJson(text, root, error))
    {
        error = "tilepack.json: " + error;
        return false;
    }
    if (!root.IsObject() || root.Int("format", 0) != 1)
    {
        error = "tilepack.json: \"format\" must be 1";
        return false;
    }
    const int tileSize = root.Int("tileSize", 0);
    if (tileSize < kMinTile || tileSize > kMaxTile)
    {
        error = "tilepack.json: \"tileSize\" must be from " + std::to_string(kMinTile) + " to " + std::to_string(kMaxTile);
        return false;
    }
    const Json* sheets = root.Get("sheets");
    if (!sheets || !sheets->IsObject())
    {
        error = "tilepack.json: \"sheets\" is missing";
        return false;
    }
    struct Want { const char* key; int minCount; bool required; TileSheet TilePackSheets::*dst; };
    const Want wants[] = { { "plain", kPlainTiles, true, &TilePackSheets::plain },
                           { "masked", kMaskedTiles, true, &TilePackSheets::masked },
                           { "events", kEventTiles, false, &TilePackSheets::events } };
    TilePackSheets result;
    result.name = root.Str("name");
    for (const Want& w : wants)
    {
        const Json* sh = sheets->Get(w.key);
        if (!sh)
        {
            if (w.required)
            {
                error = std::string("tilepack.json: sheet \"") + w.key + "\" is missing";
                return false;
            }
            continue;
        }
        const std::string file = sh->Str("file");
        // a plain file name in the pack's folder: no folders, no way out of it
        if (file.empty() || file.find_first_of("/\\:") != std::string::npos || file == "." || file == ".." || file.find("..") != std::string::npos)
        {
            error = std::string("tilepack.json: sheet \"") + w.key + "\" needs a plain \"file\" name";
            return false;
        }
        const int count = sh->Int("count", w.minCount), columns = sh->Int("columns", 0);
        if (count < w.minCount || columns < 1)
        {
            error = std::string("tilepack.json: sheet \"") + w.key + "\" needs \"columns\" and a \"count\" of at least " + std::to_string(w.minCount);
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!read(user, file, bytes) || bytes.empty())
        {
            error = file + " cannot be read";
            return false;
        }
        std::string why;
        if (!DecodeTileSheetPng(bytes, tileSize, columns, count, result.*w.dst, why))
        {
            error = file + ": " + why;
            return false;
        }
        result.hasEvents = result.hasEvents || std::string(w.key) == "events";
    }
    out = std::move(result);
    return true;
}

namespace {
struct DirReader { const std::string& dir; };
bool ReadFromDir(void* user, const std::string& file, std::vector<uint8_t>& bytes)
{
    const auto& d = *static_cast<const DirReader*>(user);
    std::ifstream f(d.dir + "/" + file, std::ios::binary);
    if (!f)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
}

bool LoadTilePack(const std::string& dir, MapGraphics& out, std::string& error)
{
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    DirReader reader{ dir };
    TilePackSheets sheets;
    if (!ParseTilePack(text, ReadFromDir, &reader, sheets, error))
        return false;
    out.plain = std::move(sheets.plain);
    out.masked = std::move(sheets.masked);
    if (sheets.hasEvents)
        out.events = std::move(sheets.events);
    else
        out.events = TileSheet(); // (the pack's tiles have no event ground: none is drawn)
    out.windowsEdition = false;
    return true;
}

} // namespace osf
