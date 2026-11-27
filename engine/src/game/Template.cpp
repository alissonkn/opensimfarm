// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Template.h"

#include "assets/Megadata.h"
#include "core/Json.h"
#include "core/Paths.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace osf {

void MakeBlankFarm(GameState& s)
{
    s = GameState();
    s.ResizeMap(kOriginalMapSize);
    s.version.assign(1, 1);
    s.animals.assign(256 * 32, 0);
    s.objects20.assign(127 * 20, 0);
    s.machines.assign(127 * 56, 0);
    s.fields.assign(98 * 68, 0);
    s.structures.assign(127 * 70, 0);
    s.items.assign(512 * 12, 0);
    s.traffic.assign(16 * 8, 0);
    s.timeline.assign(98 * 240 * 2, 0);
    s.weather.assign(240 * 3, 0);
    s.catalog.assign(static_cast<size_t>(kCatalogueRecords) * kCatalogueRecordBytes, 0);
    s.irrigationFlag.assign(1, 0);
    s.irrigationLists.assign(64 * 3, 0);
    s.cropNames.assign(16 * 14, 0);
    s.eventGrounds.assign(8 * 2, 0);
    // the tail: the scalars the engine names, then whatever else the format keeps there; it makes the file the save
    // format's exact size
    const size_t others = 1 + 6 * 2 + 1 + s.animals.size() + s.objects20.size() + s.machines.size() + s.fields.size() + s.structures.size() + s.items.size() +
                          s.traffic.size() + s.timeline.size() + s.world.cells.size() * 5 + s.weather.size() + s.catalog.size() + s.irrigationFlag.size() +
                          s.irrigationLists.size() + s.cropNames.size() + s.zones.size() + s.eventGrounds.size();
    s.tail.assign(std::max(GameState::TailBytes(), SaveBodySize(s.world.size) - others), 0);
}

namespace {

struct Fail
{
    std::string what;
};

[[noreturn]] void Bad(const std::string& what)
{
    throw Fail{ "tilepack.json template: " + what };
}

int Whole(const Json& v, int lo, int hi, const std::string& what)
{
    if (!v.IsNumber() || v.number != std::floor(v.number) || v.number < lo || v.number > hi)
        Bad(what + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
    return static_cast<int>(v.number);
}

void Record(GameState& s, const Json& r)
{
    if (!r.IsObject())
        Bad("each catalogue record must be an object");
    for (const auto& [key, value] : r.object)
        if (key != "id" && key != "family" && key != "kind" && key != "price" && key != "own" && key != "size" && key != "turns" && key != "picture" && key != "extra")
            Bad("a catalogue record has \"" + key + "\"; the fields are id, family, kind, price, own, size, turns, picture, extra");
    const Json* idv = r.Get("id");
    if (!idv)
        Bad("each catalogue record needs an \"id\"");
    const int id = Whole(*idv, 0, kCatalogueRecords - 1, "a catalogue \"id\"");
    const std::string where = "catalogue record " + std::to_string(id);
    const size_t at = static_cast<size_t>(id) * kCatalogueRecordBytes;
    auto put16 = [&](size_t off, int v) {
        s.catalog[at + off] = static_cast<uint8_t>(v & 0xFF);
        s.catalog[at + off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    if (const Json* v = r.Get("family")) put16(0, Whole(*v, 0, 65535, where + " \"family\""));
    if (const Json* v = r.Get("kind")) put16(2, Whole(*v, 0, 65535, where + " \"kind\""));
    if (const Json* v = r.Get("own")) put16(4, Whole(*v, 0, 65535, where + " \"own\""));
    if (const Json* v = r.Get("price")) put16(6, Whole(*v, 0, 65535, where + " \"price\""));
    if (const Json* v = r.Get("size"))
    {
        if (!v->IsArray() || v->array.size() != 2)
            Bad(where + " \"size\" must be [across, down]");
        s.catalog[at + 8] = static_cast<uint8_t>(static_cast<int8_t>(Whole(v->array[0], -128, 127, where + "'s size")));
        s.catalog[at + 9] = static_cast<uint8_t>(static_cast<int8_t>(Whole(v->array[1], -128, 127, where + "'s size")));
    }
    if (const Json* v = r.Get("turns")) s.catalog[at + 10] = static_cast<uint8_t>(Whole(*v, 0, 255, where + " \"turns\""));
    if (const Json* v = r.Get("picture")) s.catalog[at + 11] = static_cast<uint8_t>(static_cast<int8_t>(Whole(*v, -128, 127, where + " \"picture\"")));
    if (const Json* v = r.Get("extra"))
    {
        if (!v->IsArray() || v->array.size() != 6)
            Bad(where + " \"extra\" must be 6 bytes");
        for (size_t k = 0; k < 6; ++k)
            s.catalog[at + 12 + k] = static_cast<uint8_t>(Whole(v->array[k], 0, 255, where + "'s extra bytes"));
    }
}

} // namespace

bool ParsePackTemplate(const std::string& text, std::vector<uint8_t>& blob, bool& present, std::string& error)
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
    const Json* block = root.Get("template");
    if (!block)
        return true;
    try
    {
        if (!block->IsObject())
            Bad("\"template\" must be an object");
        for (const auto& [key, value] : block->object)
            if (key != "version" && key != "catalogue" && key != "scalars")
                Bad("\"" + key + "\" is not known; the template has version, catalogue and scalars");
        GameState s;
        MakeBlankFarm(s);
        if (const Json* v = block->Get("version"))
            s.version[0] = static_cast<uint8_t>(Whole(*v, 0, 255, "\"version\""));
        if (const Json* cat = block->Get("catalogue"))
        {
            if (!cat->IsArray())
                Bad("\"catalogue\" must be an array");
            std::set<int> seen;
            for (const Json& r : cat->array)
            {
                Record(s, r);
                if (!seen.insert(r.Int("id", -1)).second)
                    Bad("catalogue id " + std::to_string(r.Int("id", -1)) + " is given twice");
            }
        }
        if (const Json* sc = block->Get("scalars"))
        {
            if (!sc->IsObject())
                Bad("\"scalars\" must be an object");
            for (const auto& [name, value] : sc->object)
            {
                const int bytes = GameState::TailFieldSize(name);
                if (!bytes)
                    Bad("\"" + name + "\" is not a scalar of the save");
                // (these are written from the farm's own state when a save is made: the cash, the calendar, the speed)
                for (const char* own : { "cash", "day", "week", "month", "year", "season", "speed_delay", "exp_land_dup" })
                    if (name == own)
                        Bad("scalar \"" + name + "\" is set from the game's own state (cash, calendar, speed), so it is no template default");
                const int64_t hi = bytes >= 4 ? 2147483647 : (1LL << (8 * bytes)) - 1;
                if (!value.IsNumber() || value.number != std::floor(value.number) || value.number < 0 || value.number > static_cast<double>(hi))
                    Bad("scalar \"" + name + "\" must be a whole number from 0 to " + std::to_string(hi));
                s.scalars[name] = static_cast<int64_t>(value.number);
            }
        }
        std::vector<uint8_t> out;
        std::string why;
        if (!s.Serialize(out, why))
            Bad("the farm cannot be written: " + why);
        blob = std::move(out);
        present = true;
        return true;
    }
    catch (const Fail& f)
    {
        error = f.what;
        return false;
    }
}

bool LoadPackTemplate(const std::string& dir, std::vector<uint8_t>& blob, bool& present, std::string& error)
{
    present = false;
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParsePackTemplate(text, blob, present, error);
}

bool HasTemplate(const Megadata& mega, const std::string& gameDir)
{
    return mega.Has(kTemplateEntry) || FindFileNoCase(gameDir, "SCEN0.SSM").has_value();
}

bool LoadTemplateState(const Megadata& mega, const std::string& gameDir, GameState& out, std::string& error)
{
    if (mega.Has(kTemplateEntry))
        return out.LoadBytes(gameDir, mega.Read(kTemplateEntry), kTemplateEntry, error);
    const auto path = FindFileNoCase(gameDir, "SCEN0.SSM");
    if (!path)
    {
        error = "SCEN0.SSM is missing.";
        return false;
    }
    return out.Load(gameDir, *path, error);
}

} // namespace osf
