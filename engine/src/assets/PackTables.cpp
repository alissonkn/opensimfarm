// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/PackTables.h"

#include "assets/Theme.h"
#include "core/Json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>

namespace osf {

namespace {

struct Fail
{
    std::string what;
};

[[noreturn]] void Bad(const std::string& what)
{
    throw Fail{ "tilepack.json tables: " + what };
}

std::vector<uint8_t> Bytes(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

// a whole number within [lo, hi]
int Whole(const Json& v, int lo, int hi, const std::string& what)
{
    if (!v.IsNumber() || v.number != std::floor(v.number) || v.number < lo || v.number > hi)
        Bad(what + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
    return static_cast<int>(v.number);
}

const Json& Member(const Json& obj, const char* key, const char* where)
{
    const Json* v = obj.Get(key);
    if (!v)
        Bad(std::string(where) + " needs \"" + key + "\"");
    return *v;
}

// text allowed in the boxes: blank to 'z' without the file formats' own marks
void CheckText(const std::string& text, const std::string& what)
{
    for (unsigned char c : text)
        if (c < ' ' || c > 'z' || c == '@' || c == '~' || c == '`')
            Bad(what + " may hold only letters, digits and punctuation from blank to 'z', not '@', '~' or '`'");
}

std::string Pad(std::string line, size_t width)
{
    line.resize(width, '@');
    return line;
}

// ---------------------------------------------------------------- climate

void Climate(const Json& block, TableBlobs& out)
{
    if (!block.IsObject())
        Bad("\"climate\" must be an object");
    struct Kind { const char* key; const char* file; int hi; };
    for (const Kind& k : { Kind{ "rain", "rain", 7 }, Kind{ "temperature", "temp", 255 }, Kind{ "wind", "wind", 255 } })
    {
        const Json* tables = block.Get(k.key);
        if (!tables)
            continue;
        if (!tables->IsArray() || tables->array.size() != 4)
            Bad(std::string("climate \"") + k.key + "\" must be 4 tables, one for each climate class");
        for (size_t cls = 0; cls < 4; ++cls)
        {
            const Json& t = tables->array[cls];
            const std::string where = std::string("climate ") + k.key + " class " + std::to_string(cls);
            if (!t.IsArray() || (t.array.size() != 48 && t.array.size() != static_cast<size_t>(kClimateWeeks)))
                Bad(where + " must be 48 or 240 numbers");
            std::vector<int> weeks;
            for (const Json& v : t.array)
                weeks.push_back(Whole(v, 0, k.hi, where + "'s numbers"));
            if (weeks.size() == 48) // one year: the same year 5 times
            {
                const std::vector<int> year = weeks;
                for (int again = 1; again < kClimateWeeks / 48; ++again)
                    weeks.insert(weeks.end(), year.begin(), year.end());
            }
            std::string text; // 60 lines of 4 values
            for (int line = 0; line < kClimateWeeks / 4; ++line)
            {
                for (int c = 0; c < 4; ++c)
                    text += std::to_string(weeks[static_cast<size_t>(line * 4 + c)]) + (c < 3 ? "," : "");
                text += "\r\n";
            }
            out[std::string(k.file) + std::to_string(cls) + ".dat"] = Bytes(text);
        }
    }
}

// ---------------------------------------------------------------- regions

std::vector<std::string> Grid(const Json& rows, char lo, char hi, const std::string& what)
{
    if (!rows.IsArray() || rows.array.size() != static_cast<size_t>(kRegionSize))
        Bad(what + " must be " + std::to_string(kRegionSize) + " rows");
    std::vector<std::string> out;
    for (const Json& r : rows.array)
    {
        if (!r.IsString() || r.string.size() != static_cast<size_t>(kRegionSize))
            Bad(what + "'s rows must be strings of " + std::to_string(kRegionSize) + " letters");
        for (char c : r.string)
            if (c < lo || c > hi)
                Bad(what + " may hold only the letters '" + lo + "' to '" + hi + "'");
        out.push_back(r.string);
    }
    return out;
}

void Regions(const Json& block, TableBlobs& out)
{
    if (!block.IsObject())
        Bad("\"regions\" must be an object");
    std::string text;
    auto section = [&](const char* name, const std::vector<std::string>& rows) {
        text += std::string("//") + name + "\r\n";
        for (const std::string& r : rows)
            text += r + " \r\n"; // (a row is its 18 letters and a blank)
    };
    section("rain data", Grid(Member(block, "rain", "regions"), 'a', 'd', "regions rain"));
    section("temp data", Grid(Member(block, "temperature", "regions"), 'a', 'd', "regions temperature"));
    section("wind data", Grid(Member(block, "wind", "regions"), 'a', 'd', "regions wind"));
    if (const Json* h = block.Get("hotpoints"))
        section("hotpoint data", Grid(*h, 'a', 'z', "regions hotpoints"));
    out["scenario.odt"] = Bytes(text);
}

// ---------------------------------------------------------------- town layout

void TownLayout(const Json& block, TableBlobs& out)
{
    if (!block.IsObject())
        Bad("\"townLayout\" must be an object");
    const Json& blocks = Member(block, "blocks", "townLayout");
    const int count = kTownBlocksAcross * kTownBlocksDown;
    if (!blocks.IsArray() || blocks.array.size() != static_cast<size_t>(count))
        Bad("townLayout \"blocks\" must be " + std::to_string(count) + " blocks");
    std::vector<uint16_t> words(static_cast<size_t>(kTownLayoutWords), 0);
    const int width = kTownBlocksAcross * kTownBlock;
    for (int t = 0; t < count; ++t)
    {
        const Json& b = blocks.array[static_cast<size_t>(t)];
        const std::string where = "townLayout block " + std::to_string(t);
        if (!b.IsArray() || b.array.size() != static_cast<size_t>(kTownBlock))
            Bad(where + " must be " + std::to_string(kTownBlock) + " rows");
        for (int j = 0; j < kTownBlock; ++j)
        {
            const Json& row = b.array[static_cast<size_t>(j)];
            if (!row.IsArray() || row.array.size() != static_cast<size_t>(kTownBlock))
                Bad(where + "'s rows must be " + std::to_string(kTownBlock) + " tile ids");
            for (int i = 0; i < kTownBlock; ++i)
                words[static_cast<size_t>((t / kTownBlocksAcross * kTownBlock + j) * width + t % kTownBlocksAcross * kTownBlock + i)] =
                    static_cast<uint16_t>(Whole(row.array[static_cast<size_t>(i)], 0, 2047, where + "'s tile ids"));
        }
    }
    std::vector<uint8_t> blob;
    for (uint16_t w : words)
    {
        blob.push_back(static_cast<uint8_t>(w & 0xFF));
        blob.push_back(static_cast<uint8_t>(w >> 8));
    }
    out["townly0.cmp"] = std::move(blob);
}

// ---------------------------------------------------------------- events and items

// The records of a list of { "id": n, ... } members, by id (0..maxId), each id once
std::map<int, const Json*> ById(const Json& list, const char* what, int maxId)
{
    if (!list.IsArray())
        Bad(std::string("\"") + what + "\" must be an array");
    std::map<int, const Json*> out;
    for (const Json& e : list.array)
    {
        if (!e.IsObject())
            Bad(std::string("each of \"") + what + "\" must be an object");
        const int id = Whole(Member(e, "id", what), 0, maxId, std::string(what) + " \"id\"");
        if (!out.emplace(id, &e).second)
            Bad(std::string(what) + " id " + std::to_string(id) + " is given twice");
    }
    return out;
}

void Events(const Json& list, TableBlobs& out)
{
    const auto byId = ById(list, "events", 255);
    std::string text;
    const int last = byId.empty() ? -1 : byId.rbegin()->first;
    for (int id = 0; id <= last; ++id)
    {
        std::vector<std::string> lines;
        const auto it = byId.find(id);
        if (it != byId.end())
        {
            const std::string where = "event " + std::to_string(id);
            std::string title = it->second->Str("title");
            CheckText(title, where + "'s title");
            if (title.size() > static_cast<size_t>(kEventWidth))
                Bad(where + "'s title is longer than " + std::to_string(kEventWidth) + " characters");
            // a title is centred by the blanks ('`') in front of it, as the box does not centre
            lines.push_back(std::string((kEventWidth - title.size()) / 2, '`') + title);
            const Json* ls = it->second->Get("lines");
            if (ls && (!ls->IsArray() || ls->array.size() > 6))
                Bad(where + " \"lines\" must be at most 6 lines");
            for (size_t k = 0; ls && k < ls->array.size(); ++k)
            {
                if (!ls->array[k].IsString())
                    Bad(where + "'s lines must be strings");
                CheckText(ls->array[k].string, where + "'s lines");
                if (ls->array[k].string.size() > static_cast<size_t>(kEventWidth))
                    Bad(where + "'s line " + std::to_string(k + 1) + " is longer than " + std::to_string(kEventWidth) + " characters");
                lines.push_back(ls->array[k].string);
            }
        }
        lines.resize(kEventLines);
        for (const std::string& l : lines)
            text += Pad(l, 30) + "\r\n";
    }
    out["events.dat"] = Bytes(text);
}

// a description wrapped at words into lines of at most `width`; false when it needs more than `maxLines` or a word is
// longer than a line
bool Wrap(const std::string& text, size_t width, size_t maxLines, std::vector<std::string>& lines)
{
    std::string cur;
    size_t i = 0;
    while (i < text.size())
    {
        const size_t end = std::min(text.find(' ', i), text.size());
        const std::string word = text.substr(i, end - i);
        i = end + 1;
        if (word.empty())
            continue;
        if (word.size() > width)
            return false;
        if (!cur.empty() && cur.size() + 1 + word.size() > width)
        {
            lines.push_back(cur);
            cur.clear();
        }
        cur += (cur.empty() ? "" : " ") + word;
    }
    if (!cur.empty())
        lines.push_back(cur);
    return lines.size() <= maxLines;
}

void Items(const Json& list, TableBlobs& out)
{
    const auto byId = ById(list, "items", 255);
    std::string text;
    const int last = byId.empty() ? -1 : byId.rbegin()->first;
    const size_t usable = static_cast<size_t>(kItemLineWidth) - 1; // (one blank at the end of a line keeps words apart)
    for (int id = 0; id <= last; ++id)
    {
        std::vector<std::string> lines;
        const auto it = byId.find(id);
        if (it != byId.end())
        {
            const std::string where = "item " + std::to_string(id);
            const std::string name = it->second->Str("name"), description = it->second->Str("description");
            CheckText(name, where + "'s name");
            CheckText(description, where + "'s description");
            if (name.empty() || name.size() > usable)
                Bad(where + "'s name must be 1 to " + std::to_string(usable) + " characters");
            lines.push_back(name);
            std::vector<std::string> wrapped;
            if (!Wrap(description, usable, 6, wrapped))
                Bad(where + "'s description does not fit 6 lines of " + std::to_string(usable) + " characters");
            lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        }
        lines.resize(kEventLines);
        for (const std::string& l : lines)
        {
            std::string line = l.empty() ? std::string() : l + " "; // (the blank keeps the next line's first word apart)
            line.resize(kItemLineWidth, '@');
            text += line + "\r\n";
        }
    }
    out["itemtext.dat"] = Bytes(text);
}

// ---------------------------------------------------------------- overview colours

void Overview(const Json& block, TableBlobs& out)
{
    if (!block.IsObject())
        Bad("\"overview\" must be an object");
    using Block = std::array<std::array<uint8_t, 3>, 4>; // a tile's 2 x 2 pixels, row by row
    std::vector<Block> colours(static_cast<size_t>(kOverviewTiles), Block{});
    // a colour: "#rrggbb" for all four pixels, or 4 of them
    auto four = [&](const Json& v, const std::string& what) {
        Block b{};
        auto one = [&](const Json& c) {
            std::array<uint8_t, 3> rgb{};
            if (!c.IsString() || !ParseColourText(c.string, rgb))
                Bad(what + " must be colours like \"#3f9f3f\" (the overview is RGB: palette indices are not used)");
            return rgb;
        };
        if (v.IsString())
            b.fill(one(v));
        else
        {
            if (!v.IsArray() || v.array.size() != 4)
                Bad(what + " must be a colour, or 4 colours (the tile's 2 x 2 block, row by row)");
            for (size_t k = 0; k < 4; ++k)
                b[k] = one(v.array[k]);
        }
        return b;
    };
    if (const Json* d = block.Get("default"))
        std::fill(colours.begin(), colours.end(), four(*d, "overview \"default\""));
    if (const Json* ranges = block.Get("ranges"))
    {
        if (!ranges->IsArray())
            Bad("overview \"ranges\" must be an array");
        for (const Json& r : ranges->array)
        {
            if (!r.IsObject())
                Bad("each overview range must be an object");
            const int from = Whole(Member(r, "from", "an overview range"), 0, kOverviewTiles - 1, "an overview range's \"from\"");
            const int to = Whole(Member(r, "to", "an overview range"), from, kOverviewTiles - 1, "an overview range's \"to\"");
            const Block c = four(Member(r, "colours", "an overview range"), "an overview range's \"colours\"");
            for (int t = from; t <= to; ++t)
                colours[static_cast<size_t>(t)] = c;
        }
    }
    if (const Json* tiles = block.Get("tiles"))
    {
        if (!tiles->IsObject())
            Bad("overview \"tiles\" must be an object");
        for (const auto& [key, v] : tiles->object)
        {
            char* end = nullptr;
            const long t = std::strtol(key.c_str(), &end, 10);
            if (key.empty() || *end || t < 0 || t >= kOverviewTiles)
                Bad("overview tile \"" + key + "\" must be a tile id from 0 to " + std::to_string(kOverviewTiles - 1));
            colours[static_cast<size_t>(t)] = four(v, "overview tile " + key);
        }
    }
    std::vector<uint8_t> blob;
    blob.reserve(static_cast<size_t>(kOverviewRgbBytes));
    for (const Block& b : colours)
        for (const auto& px : b)
            blob.insert(blob.end(), { px[0], px[1], px[2], 255 });
    out["overview.rgb"] = std::move(blob);
}

} // namespace

bool ParsePackTables(const std::string& text, TableBlobs& out, std::string& error)
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
    const Json* tables = root.Get("tables");
    if (!tables)
        return true;
    TableBlobs made;
    try
    {
        if (!tables->IsObject())
            Bad("\"tables\" must be an object");
        if (const Json* v = tables->Get("climate")) Climate(*v, made);
        if (const Json* v = tables->Get("regions")) Regions(*v, made);
        if (const Json* v = tables->Get("townLayout")) TownLayout(*v, made);
        if (const Json* v = tables->Get("events")) Events(*v, made);
        if (const Json* v = tables->Get("items")) Items(*v, made);
        if (const Json* v = tables->Get("overview")) Overview(*v, made);
    }
    catch (const Fail& f)
    {
        error = f.what;
        return false;
    }
    for (auto& [name, bytes] : made)
        out[name] = std::move(bytes);
    return true;
}

bool LoadPackTables(const std::string& dir, TableBlobs& out, std::string& error)
{
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParsePackTables(text, out, error);
}

} // namespace osf
