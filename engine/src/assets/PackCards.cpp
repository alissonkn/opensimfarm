// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/PackCards.h"

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
    throw Fail{ "tilepack.json cards: " + what };
}

int Whole(const Json& v, int lo, int hi, const std::string& what)
{
    if (!v.IsNumber() || v.number != std::floor(v.number) || v.number < lo || v.number > hi)
        Bad(what + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
    return static_cast<int>(v.number);
}

void Word(std::vector<uint8_t>& out, int v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void Text(std::vector<uint8_t>& out, const std::string& t, const std::string& what, size_t maxLen)
{
    if (t.size() > maxLen)
        Bad(what + " is longer than " + std::to_string(maxLen) + " characters");
    for (unsigned char c : t)
        if (c < ' ' || c > '~')
            Bad(what + " may hold only printable ASCII");
    out.insert(out.end(), t.begin(), t.end());
    out.push_back(0);
}

// x, y of an item, and a rectangle [x0, y0, x1, y1] of 4 numbers
void Position(const Json& it, int& x, int& y, const std::string& where)
{
    const Json* jx = it.Get("x");
    const Json* jy = it.Get("y");
    if (!jx || !jy)
        Bad(where + " needs \"x\" and \"y\"");
    x = Whole(*jx, 0, 639, where + " \"x\"");
    y = Whole(*jy, 0, 479, where + " \"y\"");
}

void Rect(const Json& v, int r[4], const std::string& where)
{
    if (!v.IsArray() || v.array.size() != 4)
        Bad(where + " must be a rectangle [x0, y0, x1, y1]");
    for (int k = 0; k < 4; ++k)
        r[k] = Whole(v.array[static_cast<size_t>(k)], 0, 639, where);
    if (r[2] <= r[0] || r[3] <= r[1])
        Bad(where + " must have its right below and right of its left and top");
}

} // namespace

bool ParsePackCards(const std::string& text, TableBlobs& out, std::string& error)
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
    const Json* block = root.Get("cards");
    if (!block)
        return true;
    try
    {
        if (!block->IsArray() || block->array.empty() || block->array.size() > 64)
            Bad("\"cards\" must be a list of 1 to 64 cards");
        std::set<int> ids, targets;
        TableBlobs made;
        for (const Json& card : block->array)
        {
            if (!card.IsObject())
                Bad("each card must be an object");
            for (const auto& [key, value] : card.object)
                if (key != "id" && key != "items")
                    Bad("a card has \"" + key + "\"; the fields are id and items");
            const Json* idv = card.Get("id");
            if (!idv)
                Bad("each card needs an \"id\"");
            const int id = Whole(*idv, 1, 9999, "a card's \"id\"");
            if (!ids.insert(id).second)
                Bad("card " + std::to_string(id) + " is given twice");
            const std::string where = "card " + std::to_string(id);
            const Json* items = card.Get("items");
            if (!items || !items->IsArray() || items->array.size() > 60)
                Bad(where + " needs \"items\", a list of at most 60");
            std::vector<uint8_t> blob;
            for (const Json& it : items->array)
            {
                if (!it.IsObject())
                    Bad(where + "'s items must be objects");
                const std::string w = where + " item";
                int x = 0, y = 0, r[4];
                if (const Json* v = it.Get("picture"))
                {
                    Position(it, x, y, w);
                    blob.push_back(0x86);
                    Word(blob, Whole(*v, 0, 255, w + " \"picture\""));
                    Word(blob, x);
                    Word(blob, y);
                }
                else if (const Json* v = it.Get("text"))
                {
                    Position(it, x, y, w);
                    if (!v->IsString())
                        Bad(w + " \"text\" must be text");
                    const int width = Whole(it.Get("width") ? *it.Get("width") : Json(), 8, 400, w + " \"width\"");
                    blob.push_back(0x87);
                    Word(blob, x);
                    Word(blob, y);
                    Word(blob, width);
                    Text(blob, v->string, w + " text", 800);
                }
                else if (const Json* v = it.Get("button"))
                {
                    Position(it, x, y, w);
                    const int face = Whole(*v, 0, 17, w + " \"button\" (a face of buttons.til)");
                    const int to = Whole(it.Get("goto") ? *it.Get("goto") : Json(), 1, 9999, w + " \"goto\"");
                    targets.insert(to);
                    blob.push_back(0x84);
                    Word(blob, x);
                    Word(blob, y);
                    Word(blob, face);
                    Word(blob, to);
                }
                else if (const Json* v = it.Get("hotspot"))
                {
                    Rect(*v, r, w + " \"hotspot\"");
                    const int to = Whole(it.Get("goto") ? *it.Get("goto") : Json(), 1, 9999, w + " \"goto\"");
                    targets.insert(to);
                    blob.push_back(0x83);
                    for (int k = 0; k < 4; ++k)
                        Word(blob, r[k]);
                    Word(blob, to);
                }
                else if (const Json* v = it.Get("tiles"))
                {
                    Position(it, x, y, w);
                    if (!v->IsObject())
                        Bad(w + " \"tiles\" must be { \"first\": n, \"count\": n }");
                    const int first = Whole(v->Get("first") ? *v->Get("first") : Json(), 0, 2047, w + " tiles \"first\"");
                    const int count = Whole(v->Get("count") ? *v->Get("count") : Json(), 1, 24, w + " tiles \"count\"");
                    blob.push_back(0x8c);
                    Word(blob, x);
                    Word(blob, y);
                    Word(blob, count);
                    Word(blob, 0);
                    Word(blob, first);
                }
                else if (const Json* v = it.Get("crop"))
                {
                    Position(it, x, y, w);
                    blob.push_back(0x89);
                    Word(blob, Whole(*v, 0, 15, w + " \"crop\" (a slot)"));
                    Word(blob, x);
                    Word(blob, y);
                }
                else if (const Json* v = it.Get("sound"))
                {
                    Rect(*v, r, w + " \"sound\"");
                    const Json* play = it.Get("play");
                    if (!play || !play->IsString() || play->string.empty())
                        Bad(w + " \"sound\" needs \"play\", the name of a sound");
                    blob.push_back(0x8a);
                    for (int k = 0; k < 4; ++k)
                        Word(blob, r[k]);
                    Text(blob, play->string, w + " sound name", 16);
                }
                else
                    Bad(w + " is none of picture, text, button, hotspot, tiles, crop, sound");
            }
            blob.push_back(0xFF);
            made["card" + std::to_string(id) + ".hc"] = std::move(blob);
        }
        if (!ids.count(1))
            Bad("there must be a card 1: the Expert opens on it");
        for (int t : targets)
            if (!ids.count(t))
                Bad("a \"goto\" names card " + std::to_string(t) + ", which the pack does not have");
        for (auto& [name, bytes] : made)
            out[name] = std::move(bytes);
        return true;
    }
    catch (const Fail& f)
    {
        error = f.what;
        return false;
    }
}

bool LoadPackCards(const std::string& dir, TableBlobs& out, std::string& error)
{
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParsePackCards(text, out, error);
}

} // namespace osf
