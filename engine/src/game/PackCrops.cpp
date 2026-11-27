// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/PackCrops.h"

#include "assets/Png.h"
#include "core/Json.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace osf {

namespace {

struct Fail
{
    std::string what;
};

[[noreturn]] void Bad(const std::string& what)
{
    throw Fail{ "tilepack.json crops: " + what };
}

int Whole(const Json& v, int lo, int hi, const std::string& what)
{
    if (!v.IsNumber() || v.number != std::floor(v.number) || v.number < lo || v.number > hi)
        Bad(what + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
    return static_cast<int>(v.number);
}

void CheckText(const std::string& text, size_t maxLen, const std::string& what, bool allowTilde)
{
    if (text.size() > maxLen)
        Bad(what + " is longer than " + std::to_string(maxLen) + " characters");
    for (unsigned char c : text)
        if (c < ' ' || c > '~' || c == '@' || (c == '~' && !allowTilde))
            Bad(what + " may hold only printable ASCII, not '@'" + (allowTilde ? "" : " or '~'"));
}

struct Param
{
    const char* name;
    int offset;
    bool word; // 16 bits (little-endian) at offset, else a byte
    int hi;
};

const Param kParams[] = {
    { "firstStage", 6, false, 255 }, { "chillHours", 8, true, 65535 }, { "heat", 10, true, 65535 }, { "yield", 12, true, 65535 },
    { "price", 14, true, 65535 }, { "seedBase", 18, true, 65535 }, { "waterMin", 20, true, 255 }, { "waterMax", 22, true, 255 },
    { "drainA", 30, false, 255 }, { "drainB", 31, false, 255 }, { "drainC", 32, false, 255 }, { "weeks", 36, false, 255 },
    { "resistPests", 38, false, 255 }, { "resistWeeds", 39, false, 255 }, { "resistDisease", 40, false, 255 }, { "stages", 46, false, 8 },
    { "baseTemp", 47, false, 255 }, { "wetPenalty", 50, false, 255 }, { "dryPenalty", 51, false, 255 }, { "frostShift", 52, false, 255 },
    { "harvestWeek", 56, false, 255 },
};

CropDef Crop(const Json& c, int tileSize, PackReader read, void* user)
{
    if (!c.IsObject())
        Bad("each crop must be an object");
    for (const auto& [key, value] : c.object)
        if (key != "file" && key != "name" && key != "params" && key != "art" && key != "help" && key != "info")
            Bad("a crop has \"" + key + "\"; the fields are file, name, params, art, help, info");
    CropDef d;
    d.fileName = c.Str("file");
    const std::string where = "crop \"" + d.fileName + "\"";
    if (d.fileName.size() < 5 || d.fileName.size() > 14 || d.fileName.compare(d.fileName.size() - 4, 4, ".crp") != 0)
        Bad("a crop's \"file\" must be 5 to 14 characters ending in \".crp\" (a save names its crops by it): \"" + d.fileName + "\"");
    for (char ch : d.fileName)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.'))
            Bad(where + ": the file name may hold only lower case letters, digits, '_' and '.'");
    d.name = c.Str("name");
    if (d.name.empty())
        Bad(where + " needs a \"name\"");
    CheckText(d.name, 17, where + "'s name", false);

    if (const Json* params = c.Get("params"))
    {
        if (!params->IsObject())
            Bad(where + " \"params\" must be an object");
        for (const auto& [key, value] : params->object)
        {
            bool known = false;
            for (const Param& p : kParams)
                if (key == p.name)
                {
                    known = true;
                    const int v = Whole(value, 0, p.hi, where + " param \"" + key + "\"");
                    d.p[static_cast<size_t>(p.offset)] = static_cast<uint8_t>(v & 0xFF);
                    if (p.word)
                        d.p[static_cast<size_t>(p.offset) + 1] = static_cast<uint8_t>(v >> 8);
                }
            if (known)
                continue;
            if (key == "handPicked" || key == "perennial" || key == "orchard")
            {
                if (value.type != Json::Type::Bool)
                    Bad(where + " param \"" + key + "\" must be true or false");
                if (key == "handPicked")
                    d.p[37] = value.boolean ? 1 : 0;
                else
                    d.p[42] = static_cast<uint8_t>(d.p[42] | (value.boolean ? (key == "perennial" ? 1 : 4) : 0));
            }
            else if (key == "regrowAge")
                d.regrowAge = Whole(value, 0, 255, where + " param \"regrowAge\"");
            else
                Bad(where + " has the param \"" + key + "\", which is not known");
        }
    }
    if (d.Weeks() < 1 || d.Stages() < 1)
        Bad(where + " needs \"weeks\" (1..255) and \"stages\" (1..8) in its params");
    if (d.WaterMin() > d.WaterMax())
        Bad(where + ": \"waterMin\" is above \"waterMax\"");
    if (d.Perennial() && d.HarvestWeek() == 0)
        Bad(where + " is a perennial and needs a \"harvestWeek\"");

    if (const Json* help = c.Get("help"))
    {
        if (!help->IsArray() || help->array.size() > 7)
            Bad(where + " \"help\" must be at most 7 lines");
        for (const Json& l : help->array)
        {
            if (!l.IsString())
                Bad(where + "'s help lines must be text");
            CheckText(l.string, 26, where + "'s help lines", false);
            d.help.push_back(l.string);
        }
    }
    d.info = c.Str("info");
    CheckText(d.info, 250, where + "'s info", true);

    if (const Json* art = c.Get("art"))
    {
        if (!art->IsString() || !IsPackFileName(art->string))
            Bad(where + " \"art\" must be the name of a PNG file in the pack");
        if (tileSize < 8)
            Bad(where + " has \"art\", which needs the pack's \"tileSize\"");
        std::vector<uint8_t> bytes;
        if (!read(user, art->string, bytes) || bytes.empty())
            Bad(art->string + " cannot be read");
        RgbaPicture image;
        std::string why;
        if (!DecodePng(bytes, image, why))
            Bad(art->string + ": " + why);
        if (image.w != 14 * tileSize || image.h != tileSize)
            Bad(art->string + " is " + std::to_string(image.w) + " x " + std::to_string(image.h) + "; the 14 pictures of " + where + " need " +
                std::to_string(14 * tileSize) + " x " + std::to_string(tileSize));
        d.artSize = tileSize;
        d.artArgb.resize(static_cast<size_t>(14) * tileSize * tileSize);
        for (int k = 0; k < 14; ++k)
            for (int y = 0; y < tileSize; ++y)
                std::copy_n(&image.argb[static_cast<size_t>(y) * image.w + static_cast<size_t>(k) * tileSize], tileSize,
                            &d.artArgb[static_cast<size_t>(k) * tileSize * tileSize + static_cast<size_t>(y) * tileSize]);
    }
    d.loaded = true;
    return d;
}

} // namespace

bool ParsePackCrops(const std::string& text, PackReader read, void* user, std::vector<CropDef>& out, bool& present, std::string& error)
{
    present = false;
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
    const Json* block = root.Get("crops");
    if (!block)
        return true;
    try
    {
        if (!block->IsArray())
            Bad("\"crops\" must be an array");
        if (block->array.size() < 16 || block->array.size() > 64)
            Bad("a pack needs from 16 to 64 crops (a new world starts with 16)");
        const int tileSize = root.Int("tileSize", 0);
        std::vector<CropDef> made;
        std::set<std::string> seen;
        for (const Json& c : block->array)
        {
            made.push_back(Crop(c, tileSize, read, user));
            if (!seen.insert(made.back().fileName).second)
                Bad("the file \"" + made.back().fileName + "\" is given twice");
        }
        out = std::move(made);
        present = true;
        return true;
    }
    catch (const Fail& f)
    {
        error = f.what;
        return false;
    }
}

namespace {
struct DirReader { const std::string& dir; };
bool ReadFromDir(void* user, const std::string& file, std::vector<uint8_t>& bytes)
{
    return ReadPackFile(static_cast<const DirReader*>(user)->dir, file, bytes);
}
} // namespace

bool LoadPackCrops(const std::string& dir, std::vector<CropDef>& out, bool& present, std::string& error)
{
    present = false;
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    DirReader reader{ dir };
    return ParsePackCrops(text, ReadFromDir, &reader, out, present, error);
}

} // namespace osf
