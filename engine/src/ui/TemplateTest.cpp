// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/TemplateTest.h"

#include "assets/Megadata.h"
#include "assets/PackTables.h"
#include "assets/SaveGame.h"
#include "game/Editing.h"
#include "game/GameState.h"
#include "game/Shop.h"
#include "game/Simulation.h"
#include "game/Template.h"
#include "game/WorldGen.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace osf {

namespace {

// a fake archive of one entry, a folder to be a game folder
void MakeArchive(const std::string& dir)
{
    std::filesystem::create_directories(dir);
    const std::vector<uint8_t> one = { 1, 2, 3 };
    std::vector<uint8_t> idx;
    auto rec = [&](const char* name, uint32_t off) {
        char n[16] = {};
        std::snprintf(n, sizeof n, "%s", name);
        idx.insert(idx.end(), n, n + 16);
        for (int s = 0; s < 32; s += 8)
            idx.push_back(static_cast<uint8_t>(off >> s));
    };
    rec("DUMMY.DAT", 0);
    rec("", 3);
    std::ofstream(dir + "/MEGADATA.IDX", std::ios::binary).write(reinterpret_cast<const char*>(idx.data()), static_cast<long>(idx.size()));
    std::ofstream(dir + "/MEGADATA.EEA", std::ios::binary).write(reinterpret_cast<const char*>(one.data()), 3);
}

std::string TemplateJson(const std::string& block) { return "{ \"format\": 1, \"template\": " + block + " }"; }

} // namespace

int TemplateSelfTest(const char* packFolder)
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
        fails += ok ? 0 : 1;
    };

    // ---------------------------------------------------------------- the blank farm and the save format
    std::printf("-- the blank farm\n");
    const std::string dir = (std::filesystem::temp_directory_path() / "osf_templatetest").string();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    MakeArchive(dir);
    {
        GameState blank;
        MakeBlankFarm(blank);
        std::vector<uint8_t> bytes;
        std::string e;
        check(blank.Serialize(bytes, e) && bytes.size() == SaveBodySize(kOriginalMapSize) + kSaveSignatureSize, "a blank farm is written at the save format's size");
        GameState back;
        check(back.LoadBytes(dir, bytes, "blank", e) && back.world.size == 96 && back.catalog == blank.catalog && back.zones.size() == 12 * 12 * 7 && back.tail.size() == blank.tail.size() && back.tail.size() >= GameState::TailBytes() &&
                  back.version == blank.version && back.weather.size() == 720 && back.timeline.size() == 98 * 240 * 2,
              "  and read back the same, section for section");
        check(!back.LoadBytes(dir, { 1, 2, 3 }, "short", e) && e.find("short") != std::string::npos, "  a file that is no save is refused, naming it");
        // Save and Load still go through the same bytes
        const std::string path = dir + "/blank.sfm";
        GameState viaFile;
        check(blank.Save(path, e) && viaFile.Load(dir, path, e) && viaFile.catalog == blank.catalog, "  Save and Load of a file give the same");
        check(GameState::TailFieldSize("cash") == 4 && GameState::TailFieldSize("menu_6") == 1 && GameState::TailFieldSize("irr_period") == 2 && GameState::TailFieldSize("nothing") == 0 &&
                  GameState::TailBytes() > 100,
              "  the scalar tail's fields and sizes are known by name");
    }

    // ---------------------------------------------------------------- a template block
    std::printf("-- the template block\n");
    {
        const std::string block =
            "{ \"version\": 7, \"catalogue\": ["
            " { \"id\": 12, \"family\": 6, \"kind\": 64, \"own\": 3, \"price\": 600, \"size\": [1, 1] },"
            " { \"id\": 0, \"family\": 40, \"kind\": 0, \"price\": 12000, \"size\": [1, -2], \"turns\": 2, \"picture\": -5, \"extra\": [1, 2, 3, 4, 5, 255] },"
            " { \"id\": 102, \"price\": 65535 } ], \"scalars\": { \"menu_6\": 1, \"irr_period\": 3, \"loan_credit\": 123456 } }";
        std::vector<uint8_t> blob;
        bool present = false;
        std::string e;
        const bool ok = ParsePackTemplate(TemplateJson(block), blob, present, e);
        check(ok && present && !blob.empty(), "a block is made into a save" + (ok ? "" : ": " + e));
        GameState s;
        check(ok && s.LoadBytes(dir, blob, "pack", e), "  which loads as one");
        if (ok)
        {
            auto b = [&](int rec, int off) { return s.catalog[static_cast<size_t>(rec) * 18 + static_cast<size_t>(off)]; };
            check(s.version[0] == 7 && s.CatalogPrice(12) == 600 && s.CatalogOwn(12) == 3 && Rd16(s.catalog, 12 * 18) == 6 && Rd16(s.catalog, 12 * 18 + 2) == 64 && b(12, 8) == 1 && b(12, 9) == 1,
                  "  the version and a record's family, kind, own, price and size are where the engine reads them");
            check(s.CatalogPrice(0) == 12000 && static_cast<int8_t>(b(0, 9)) == -2 && b(0, 10) == 2 && static_cast<int8_t>(b(0, 11)) == -5 && b(0, 12) == 1 && b(0, 17) == 255 && Rd16(s.catalog, 0) == 40,
                  "  a negative size and picture keep their sign, turns and the six extra bytes are in place");
            check(s.CatalogPrice(102) == 65535 && s.CatalogPrice(50) == 0 && Rd16(s.catalog, 50 * 18) == 0, "  the last record is reachable; the records left out are zero");
            check(s.scalars["menu_6"] == 1 && s.scalars["irr_period"] == 3 && s.scalars["loan_credit"] == 123456, "  the scalars come out as the save's own (a four-byte one too)");
        }
        // no block
        std::vector<uint8_t> none = { 9 };
        bool p2 = true;
        check(ParsePackTemplate("{ \"format\": 1 }", none, p2, e) && !p2 && none == std::vector<uint8_t>({ 9 }), "no template block: nothing made, the bytes left alone");
        // a block of nothing is a blank farm
        std::vector<uint8_t> empty;
        check(ParsePackTemplate(TemplateJson("{}"), empty, present, e) && present && GameState().LoadBytes(dir, empty, "empty", e), "an empty block is a blank farm");

        auto bad = [&](const std::string& body, const std::string& why) {
            std::vector<uint8_t> keep = { 7 };
            bool p = true;
            std::string er;
            const bool r = !ParsePackTemplate(TemplateJson(body), keep, p, er) && !er.empty() && !p && keep == std::vector<uint8_t>({ 7 });
            check(r, "rejects " + why + (r ? " (" + er + ")" : ""));
        };
        bad("5", "a template that is not an object");
        bad("{ \"colour\": 1 }", "a name the template does not have");
        bad("{ \"version\": 256 }", "a version of 256");
        bad("{ \"catalogue\": 5 }", "a catalogue that is not an array");
        bad("{ \"catalogue\": [ 5 ] }", "a record that is not an object");
        bad("{ \"catalogue\": [ { \"price\": 5 } ] }", "a record without an id");
        bad("{ \"catalogue\": [ { \"id\": 103 } ] }", "an id of 103");
        bad("{ \"catalogue\": [ { \"id\": -1 } ] }", "an id of -1");
        bad("{ \"catalogue\": [ { \"id\": 1.5 } ] }", "an id that is not whole");
        bad("{ \"catalogue\": [ { \"id\": 1 }, { \"id\": 1 } ] }", "an id twice");
        bad("{ \"catalogue\": [ { \"id\": 1, \"cost\": 5 } ] }", "a field a record does not have");
        bad("{ \"catalogue\": [ { \"id\": 1, \"price\": 65536 } ] }", "a price of 65536");
        bad("{ \"catalogue\": [ { \"id\": 1, \"price\": -1 } ] }", "a negative price");
        bad("{ \"catalogue\": [ { \"id\": 1, \"family\": \"six\" } ] }", "a family that is text");
        bad("{ \"catalogue\": [ { \"id\": 1, \"size\": [1] } ] }", "a size of one number");
        bad("{ \"catalogue\": [ { \"id\": 1, \"size\": [200, 1] } ] }", "a size of 200");
        bad("{ \"catalogue\": [ { \"id\": 1, \"turns\": 256 } ] }", "turns of 256");
        bad("{ \"catalogue\": [ { \"id\": 1, \"picture\": 128 } ] }", "a picture of 128");
        bad("{ \"catalogue\": [ { \"id\": 1, \"extra\": [1, 2, 3] } ] }", "3 extra bytes");
        bad("{ \"catalogue\": [ { \"id\": 1, \"extra\": [1, 2, 3, 4, 5, 256] } ] }", "an extra byte of 256");
        bad("{ \"scalars\": 5 }", "scalars that is not an object");
        bad("{ \"scalars\": { \"nothing\": 1 } }", "a scalar the save does not have");
        bad("{ \"scalars\": { \"cash\": 5 } }", "the cash (a world sets it itself)");
        bad("{ \"scalars\": { \"month\": 5 } }", "the calendar (a world sets it itself)");
        bad("{ \"scalars\": { \"menu_6\": 256 } }", "a one-byte scalar of 256");
        bad("{ \"scalars\": { \"irr_period\": 65536 } }", "a two-byte scalar of 65536");
        bad("{ \"scalars\": { \"menu_6\": -1 } }", "a negative scalar");
        bad("{ \"scalars\": { \"menu_6\": 1.5 } }", "a scalar that is not whole");
    }

    // ---------------------------------------------------------------- where a new world's template comes from
    std::printf("-- the source of the template\n");
    {
        Megadata mega;
        std::string e;
        check(mega.Open(dir, e) && !HasTemplate(mega, dir), "no pack template and no SCEN0.SSM: there is no template");
        GameState t;
        check(!LoadTemplateState(mega, dir, t, e) && e == "SCEN0.SSM is missing.", "  and loading says so");
        // SCEN0.SSM in the folder, price 111
        GameState file;
        MakeBlankFarm(file);
        file.SetCatalogPrice(0, 111);
        check(file.Save(dir + "/Scen0.ssm", e), "a SCEN0.SSM is written");
        check(HasTemplate(mega, dir) && LoadTemplateState(mega, dir, t, e) && t.CatalogPrice(0) == 111, "SCEN0.SSM (any case) is the template when there is no pack's");
        // a pack's template over it, price 222
        std::vector<uint8_t> blob;
        bool present = false;
        ParsePackTemplate(TemplateJson("{ \"catalogue\": [ { \"id\": 0, \"price\": 222 } ] }"), blob, present, e);
        mega.Override(kTemplateEntry, blob);
        GameState t2;
        check(HasTemplate(mega, dir) && LoadTemplateState(mega, dir, t2, e) && t2.CatalogPrice(0) == 222, "a pack's template wins over SCEN0.SSM");
        std::filesystem::remove(dir + "/Scen0.ssm", ec);
        GameState t3;
        check(HasTemplate(mega, dir) && LoadTemplateState(mega, dir, t3, e) && t3.CatalogPrice(0) == 222, "and stands alone without it");
        GameState t4;
        Megadata broken;
        broken.Open(dir, e);
        broken.Override(kTemplateEntry, { 1, 2, 3 });
        check(!LoadTemplateState(broken, dir, t4, e) && !e.empty(), "a template entry that is no save is an error");
    }

    // ---------------------------------------------------------------- the game on a pack's template
    if (packFolder)
    {
        std::printf("-- the game on the template of %s\n", packFolder);
        std::vector<uint8_t> blob;
        TableBlobs tables;
        bool present = false;
        std::string e;
        const bool loaded = LoadPackTemplate(packFolder, blob, present, e) && present && LoadPackTables(packFolder, tables, e);
        check(loaded, "its template and tables are made" + (loaded ? "" : ": " + e));
        if (loaded)
        {
            Megadata mega;
            mega.Open(dir, e);
            mega.Override(kTemplateEntry, blob);
            for (const auto& [name, bytes] : tables)
                mega.Override(name, bytes);
            GameState tmpl;
            check(LoadTemplateState(mega, dir, tmpl, e), "the template loads");

            // the catalogue by the engine's own roles
            bool machines = true, buildings = true, animals = true, supplies = true;
            for (const int rec : ShopItems(kShopMachinery))
                machines = machines && tmpl.CatalogPrice(rec) > 0 && tmpl.CatalogByteAt(rec, 8) >= 1 && tmpl.CatalogByteAt(rec, 9) >= 1;
            for (const int rec : ShopItems(kShopBuildings))
                buildings = buildings && tmpl.CatalogPrice(rec) > 0 && tmpl.CatalogByteAt(rec, 8) >= 1 && tmpl.CatalogByteAt(rec, 9) >= 1 && Rd16(tmpl.catalog, static_cast<size_t>(rec) * 18 + 2) == 0x40 + rec - 12;
            for (const int rec : ShopItems(kShopLivestock))
                animals = animals && tmpl.CatalogPrice(rec) > 0 && Rd16(tmpl.catalog, static_cast<size_t>(rec) * 18 + 2) == 0xA0 + rec - 45;
            for (const int rec : ShopItems(kShopSupplies))
                supplies = supplies && tmpl.CatalogPrice(rec) > 0 && Rd16(tmpl.catalog, static_cast<size_t>(rec) * 18 + 2) == 0xDC + rec - 82;
            check(machines && buildings && animals && supplies, "every machine, building, animal and supply the shop offers is priced, with a size and the engine's ledger code");

            // a new world from it
            WorldOptions opt;
            opt.seed = 11;
            GameState world;
            std::string gerr;
            const bool gen = GenerateWorld(dir, mega, tmpl, opt, world, gerr);
            check(gen && world.world.size == 96 && world.cash == 40000, "a world is generated from the template, the pack's climate and town layout" + (gen ? "" : ": " + gerr));
            if (gen)
            {
                int owned = 0;
                for (int c = 0; c < world.Plots(); ++c)
                    for (int r = 0; r < world.Plots(); ++r)
                        owned += world.PlotOwner(c, r) == 1;
                check(owned == 9 && world.CatalogPrice(12) == tmpl.CatalogPrice(12), "  the farm is 9 plots, and the catalogue is the template's");

                Simulation sim(world);
                sim.SetGameDir(dir);
                sim.SetTownLayout(mega.Read("townly0.cmp"));
                world.cash = 9000000; // enough to buy everything once

                // a free spot of w x h cells on the player's own land
                auto spot = [&](int w, int h, int& sx, int& sy) {
                    for (int x = 0; x + w <= world.world.size; ++x)
                        for (int y = 0; y + h <= world.world.size; ++y)
                        {
                            bool free = true;
                            for (int i = x; i < x + w && free; ++i)
                                for (int j = y; j < y + h && free; ++j)
                                {
                                    const MapCell& c = world.world.At(i, j);
                                    free = world.PlotOwner(i / 8, j / 8) == 1 && c.BaseTile() >= 15 && c.BaseTile() <= 30 && !c.HasObject() && !sim.FieldIndexAt(i, j) && !(c.overlay & 0x4000);
                                }
                            if (free)
                            {
                                sx = x;
                                sy = y;
                                return true;
                            }
                        }
                    return false;
                };
                auto buy = [&](int rec, const std::string& name, int w, int h, int expectCountDelta) {
                    int x = 0, y = 0;
                    if (!spot(w + 1, h + 1, x, y)) // (a ring of free ground round it)
                    {
                        check(false, "  " + name + ": no free spot on the farm");
                        return;
                    }
                    const int64_t before = world.cash;
                    const int ownBefore = world.CatalogOwn(ShopOwnRecord(rec));
                    const int count = world.structureCount + world.machineCount + world.animalCount;
                    const EditResult r = ShopBuy(world, sim, rec, x, y);
                    check(r.ok && before - world.cash == ShopPrice(world, rec) && world.CatalogOwn(ShopOwnRecord(rec)) == ownBefore + 1 &&
                              world.structureCount + world.machineCount + world.animalCount == count + expectCountDelta,
                          "  buying a " + name + " costs " + std::to_string(ShopPrice(world, rec)) + " and adds it" + (r.ok ? "" : " (refused: " + r.message + ")"));
                };
                // the buildings first (a barn takes the machines, seed and spray)
                for (const int rec : ShopItems(kShopBuildings))
                    buy(rec, ShopName(world, rec), std::max(1, world.CatalogByteAt(rec, 8)), std::max(1, world.CatalogByteAt(rec, 9)), 1);
                for (const int rec : ShopItems(kShopMachinery))
                    if (rec != 8)
                        buy(rec, ShopName(world, rec), 1, std::max(1, Machines::Cells(rec)), 1);
                for (const int rec : ShopItems(kShopLivestock))
                    buy(rec, ShopName(world, rec), 3, 3, 1);
                check(world.CatalogOwn(0) == 1 && world.CatalogOwn(45) == 1 && world.CatalogOwn(12) == 1, "  the Own counts of the first of each kind are 1");

                // the large barn (kind 0x45) takes supplies and seed bags into its storage slots
                int bx = -1, by = -1;
                for (int i = 1; i < 127 && bx < 0; ++i)
                {
                    const size_t b = static_cast<size_t>(i) * 70;
                    if ((Rd8(world.structures, b + 2) & 0x20) && Rd16(world.structures, b) == 0x45)
                    {
                        bx = Rd8(world.structures, b + 4);
                        by = Rd8(world.structures, b + 5);
                    }
                }
                check(bx >= 0, "  the large barn is on the map");
                if (bx >= 0)
                {
                    for (const int rec : ShopItems(kShopSupplies))
                    {
                        const int64_t before = world.cash;
                        const EditResult r = ShopBuy(world, sim, rec, bx, by);
                        check(r.ok && before - world.cash == ShopPrice(world, rec), "  buying " + ShopName(world, rec) + " (" + std::to_string(ShopPrice(world, rec)) + ") puts it in the barn" + (r.ok ? "" : " (refused: " + r.message + ")"));
                    }
                    const EditResult seed = ShopBuy(world, sim, 21, bx, by);
                    check(seed.ok, std::string("  a seed bag goes into the barn") + (seed.ok ? "" : " (refused: " + seed.message + ")"));
                }

                // half a year, a day at a time as the clock runs
                const int startYear = world.cal.year, startMonth = world.cal.month;
                std::string m;
                GameEvent ev;
                int events = 0;
                for (int day = 0; day < 26 * 7; ++day)
                {
                    for (int t = 0; t <= world.speedDelay; ++t)
                        sim.TimerTick();
                    while (sim.TakeMessage(m)) {}
                    while (sim.TakeEvent(ev)) ++events;
                    while (sim.TakeSound(m)) {}
                }
                const int months = (world.cal.year - startYear) * 12 + world.cal.month - startMonth;
                check(months >= 5 && months <= 7, "  half a year runs: the calendar moved " + std::to_string(months) + " months");
                check(world.cash > -1000000 && world.cash < 9999999, "  and the money is still a sane number (" + std::to_string(static_cast<long long>(world.cash)) + ")");
            }
        }
    }

    std::filesystem::remove_all(dir, ec);
    std::printf("template farm %s\n", fails ? "FAIL" : "ok");
    return fails;
}

} // namespace osf
