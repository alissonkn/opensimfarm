// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/PicturePack.h"

#include "assets/Png.h"
#include "core/Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>

namespace osf {

namespace {

constexpr int kMaxPictureSide = 8192;
constexpr long long kMaxPackPixels = 50LL * 1000 * 1000; // all the pictures of a pack together (200 MB as ARGB)
constexpr int kMaxLogical = 4096;

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// a number member within [lo, hi], else `fallback`; sets `bad` when it is present but outside
double NumberIn(const Json& obj, const char* key, double fallback, double lo, double hi, bool& bad)
{
    const Json* v = obj.Get(key);
    if (!v)
        return fallback;
    if (!v->IsNumber() || !(v->number >= lo && v->number <= hi))
    {
        bad = true;
        return fallback;
    }
    return v->number;
}

// a [w, h] pair of whole numbers in 1..kMaxLogical
bool Pair(const Json* v, int& w, int& h)
{
    if (!v || !v->IsArray() || v->array.size() != 2)
        return false;
    for (const Json& n : v->array)
        if (!n.IsNumber() || n.number < 1 || n.number > kMaxLogical || n.number != std::floor(n.number))
            return false;
    w = static_cast<int>(v->array[0].number);
    h = static_cast<int>(v->array[1].number);
    return true;
}

int Logical(int pixels, double scale)
{
    return std::clamp(static_cast<int>(std::lround(pixels / scale)), 1, kMaxLogical);
}

struct Builder
{
    PackReader read;
    void* user;
    PicturePack pack;
    long long pixels = 0;
    std::string error;

    bool Fail(const std::string& what)
    {
        error = "tilepack.json pictures: " + what;
        return false;
    }

    bool Load(const std::string& file, const std::string& what, RgbaPicture& image)
    {
        if (!IsPackFileName(file))
            return Fail(what + " needs a \"file\" name inside the pack's folder");
        std::vector<uint8_t> bytes;
        if (!read(user, file, bytes) || bytes.empty())
            return Fail(file + " cannot be read");
        int w = 0, h = 0;
        if (!PngSize(bytes, w, h))
            return Fail(file + " is not a readable PNG");
        if (w > kMaxPictureSide || h > kMaxPictureSide)
            return Fail(file + " is larger than " + std::to_string(kMaxPictureSide) + " pixels");
        pixels += static_cast<long long>(w) * h;
        if (pixels > kMaxPackPixels)
            return Fail("the pictures hold too many pixels together");
        std::string why;
        if (!DecodePng(bytes, image, why))
            return Fail(file + ": " + why);
        return true;
    }

    bool Single(const std::string& key, const Json& entry, double defaultScale)
    {
        if (!entry.IsObject())
            return Fail("\"" + key + "\" must be an object");
        bool bad = false;
        const double scale = NumberIn(entry, "scale", defaultScale, 0.25, 16, bad);
        int lw = 0, lh = 0;
        const bool hasSize = entry.Get("size") != nullptr;
        if (bad || (hasSize && !Pair(entry.Get("size"), lw, lh)))
            return Fail("\"" + key + "\" has a bad \"scale\" or \"size\"");
        PackPicture pic;
        if (!Load(entry.Str("file"), "\"" + key + "\"", pic.image))
            return false;
        pic.logicalW = hasSize ? lw : Logical(pic.image.w, scale);
        pic.logicalH = hasSize ? lh : Logical(pic.image.h, scale);
        pack.Add(key, std::move(pic));
        return true;
    }

    bool Set(const Json& set, double defaultScale)
    {
        if (!set.IsObject())
            return Fail("each set must be an object");
        const std::string key = set.Str("key");
        const size_t at = key.find("{n}");
        if (at == std::string::npos)
            return Fail("a set's \"key\" needs {n}: " + key);
        bool bad = false;
        const int count = static_cast<int>(NumberIn(set, "count", 0, 1, 1024, bad));
        const int columns = static_cast<int>(NumberIn(set, "columns", count, 1, 1024, bad));
        const int first = static_cast<int>(NumberIn(set, "first", 0, 0, 100000, bad));
        const double scale = NumberIn(set, "scale", defaultScale, 0.25, 16, bad);
        int cw = 0, ch = 0;
        const bool hasCell = set.Get("cell") != nullptr;
        if (bad || count < 1 || (hasCell && !Pair(set.Get("cell"), cw, ch)))
            return Fail("set \"" + key + "\" needs a \"count\" from 1 to 1024, and sensible \"columns\", \"first\", \"scale\" and \"cell\"");
        RgbaPicture sheet;
        if (!Load(set.Str("file"), "set \"" + key + "\"", sheet))
            return false;
        const int rows = (count + columns - 1) / columns;
        const int pw = sheet.w / columns, ph = sheet.h / rows; // one cell, in pixels
        if (pw < 1 || ph < 1)
            return Fail(set.Str("file") + " is too small for " + std::to_string(count) + " pictures in " + std::to_string(columns) + " columns");
        for (int i = 0; i < count; ++i)
        {
            PackPicture pic;
            pic.image.w = pw;
            pic.image.h = ph;
            pic.image.argb.resize(static_cast<size_t>(pw) * ph);
            const int ox = (i % columns) * pw, oy = (i / columns) * ph;
            for (int y = 0; y < ph; ++y)
                std::copy_n(&sheet.argb[static_cast<size_t>(oy + y) * sheet.w + ox], pw, &pic.image.argb[static_cast<size_t>(y) * pw]);
            pic.logicalW = hasCell ? cw : Logical(pw, scale);
            pic.logicalH = hasCell ? ch : Logical(ph, scale);
            std::string k = key;
            k.replace(at, 3, std::to_string(first + i));
            pack.Add(k, std::move(pic));
        }
        return true;
    }
};

} // namespace

const PackPicture* PicturePack::Find(const std::string& key) const
{
    const auto it = _pictures.find(Lower(key));
    return it == _pictures.end() ? nullptr : &it->second;
}

void PicturePack::Add(const std::string& key, PackPicture picture)
{
    _pictures[Lower(key)] = std::move(picture);
}

bool ParsePicturePack(const std::string& text, PackReader read, void* user, PicturePack& out, std::string& error)
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
    const Json* block = root.Get("pictures");
    if (!block)
        return true; // (no pictures: nothing to add)
    Builder b{ read, user, {}, 0, {} };
    if (!block->IsObject())
    {
        error = "tilepack.json: \"pictures\" must be an object";
        return false;
    }
    bool bad = false;
    const double scale = NumberIn(*block, "scale", 1, 0.25, 16, bad);
    if (bad)
    {
        error = "tilepack.json pictures: \"scale\" must be from 0.25 to 16";
        return false;
    }
    if (const Json* single = block->Get("single"))
    {
        if (!single->IsObject())
        {
            error = "tilepack.json pictures: \"single\" must be an object";
            return false;
        }
        for (const auto& [key, entry] : single->object)
            if (!b.Single(key, entry, scale))
            {
                error = b.error;
                return false;
            }
    }
    if (const Json* sets = block->Get("sets"))
    {
        if (!sets->IsArray())
        {
            error = "tilepack.json pictures: \"sets\" must be an array";
            return false;
        }
        for (const Json& set : sets->array)
            if (!b.Set(set, scale))
            {
                error = b.error;
                return false;
            }
    }
    for (const auto& [key, pic] : b.pack.All())
        out.Add(key, pic);
    return true;
}

namespace {
struct DirReader { const std::string& dir; };
bool ReadFromDir(void* user, const std::string& file, std::vector<uint8_t>& bytes)
{
    return ReadPackFile(static_cast<const DirReader*>(user)->dir, file, bytes);
}
}

bool LoadPicturePack(const std::string& dir, PicturePack& out, std::string& error)
{
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    DirReader reader{ dir };
    return ParsePicturePack(text, ReadFromDir, &reader, out, error);
}

} // namespace osf
