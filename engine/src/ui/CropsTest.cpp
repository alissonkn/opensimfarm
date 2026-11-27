// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/CropsTest.h"

#include "assets/Megadata.h"
#include "assets/PackTables.h"
#include "game/AutoPlayer.h"
#include "game/Crops.h"
#include "game/Machines.h"
#include "game/PackCrops.h"
#include "game/Simulation.h"
#include "game/Template.h"
#include "game/WorldGen.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

namespace osf {

namespace {

// 16 crops without art, `extra` replacing the first crop's text
std::string Crops(const std::string& first, int count = 16)
{
    std::string s = "{ \"format\": 1, \"crops\": [ " + first;
    for (int i = 1; i < count; ++i)
        s += ", { \"file\": \"c" + std::to_string(i) + ".crp\", \"name\": \"Crop " + std::to_string(i) + "\", \"params\": { \"weeks\": 10, \"stages\": 8 } }";
    return s + " ] }";
}

bool NoFiles(void*, const std::string&, std::vector<uint8_t>&) { return false; }

const char* kGood =
    "{ \"file\": \"corn.crp\", \"name\": \"Corn\", \"params\": { \"weeks\": 18, \"stages\": 8, \"firstStage\": 1, \"baseTemp\": 50, \"heat\": 300, \"chillHours\": 140, \"yield\": 400, \"price\": 3000, \"seedBase\": 90,"
    " \"waterMin\": 5, \"waterMax\": 19, \"drainA\": 3, \"drainB\": 4, \"drainC\": 5, \"wetPenalty\": 6, \"dryPenalty\": 7, \"frostShift\": 2,"
    " \"resistPests\": 1, \"resistWeeds\": 2, \"resistDisease\": 3, \"handPicked\": true, \"perennial\": true, \"orchard\": true, \"harvestWeek\": 36, \"regrowAge\": 1 },"
    " \"help\": [\"Line one\", \"Line two\"], \"info\": \"Corn~Temperature: Warm\" }";

} // namespace

int CropsSelfTest(const char* packFolder)
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
        fails += ok ? 0 : 1;
    };
    SetPackCrops({});

    // ---------------------------------------------------------------- the block
    std::printf("-- the crops block\n");
    {
        std::vector<CropDef> crops;
        bool present = false;
        std::string e;
        const bool ok = ParsePackCrops(Crops(kGood), NoFiles, nullptr, crops, present, e);
        check(ok && present && crops.size() == 16, "16 crops are read" + (ok ? "" : ": " + e));
        if (ok)
        {
            const CropDef& c = crops[0];
            check(c.name == "Corn" && c.fileName == "corn.crp" && c.loaded && !c.HasArgbArt() && c.regrowAge == 1, "  name, file name, loaded, no art");
            check(c.Weeks() == 18 && c.Stages() == 8 && c.P6() == 1 && c.BaseTemp() == 50 && c.HeatNeed() == 300 && c.ChillNeed() == 140 && c.Yield() == 400 && c.BasePrice() == 3000 &&
                      c.SeedBase() == 90 && c.SeedPrice() == 22 && c.WaterMin() == 5 && c.WaterMax() == 19,
                  "  the engine's accessors read the parameters (a word's two bytes included)");
            check(c.DrainA() == 3 && c.DrainB() == 4 && c.DrainC() == 5 && c.WetPenalty() == 6 && c.DryPenalty() == 7 && c.FrostShift() == 2 && c.ResistPests() == 1 && c.ResistWeeds() == 2 &&
                      c.ResistDisease() == 3 && c.HandPicked() && c.Perennial() && c.Orchard() && c.HarvestWeek() == 36,
                  "  drains, penalties, resistances, hand picking, perennial, orchard and harvest week");
            check(c.help.size() == 2 && c.help[1] == "Line two" && c.info == "Corn~Temperature: Warm", "  the help lines and the info card");
            check(crops[1].fileName == "c1.crp" && crops[1].Weeks() == 10 && !crops[1].HandPicked() && !crops[1].Perennial() && crops[1].p[40] == 0, "  what is left out is zero or false");
        }
        std::vector<CropDef> kept = { CropDef{} };
        bool p2 = true;
        check(ParsePackCrops("{ \"format\": 1 }", NoFiles, nullptr, kept, p2, e) && !p2 && kept.size() == 1, "no crops block: nothing read");

        auto bad = [&](const std::string& json, const std::string& why) {
            std::vector<CropDef> keep = { CropDef{} };
            bool p = true;
            std::string er;
            const bool r = !ParsePackCrops(json, NoFiles, nullptr, keep, p, er) && !er.empty() && !p && keep.size() == 1;
            check(r, "rejects " + why + (r ? " (" + er + ")" : ""));
        };
        auto first = [&](const std::string& crop, int count = 16) { return Crops(crop, count); };
        const std::string base = "\"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 }";
        bad("{ \"format\": 1, \"crops\": 5 }", "crops that is not an array");
        bad(first("{ " + base + " }", 15), "15 crops");
        bad(first("{ " + base + " }", 65), "65 crops");
        bad(first("5"), "a crop that is not an object");
        bad(first("{ " + base + ", \"colour\": 1 }"), "a field a crop does not have");
        bad(first("{ \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a crop without a file");
        bad(first("{ \"file\": \"a.txt\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a file name that is not .crp");
        bad(first("{ \"file\": \"verylongname1.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a file name of 17 characters");
        bad(first("{ \"file\": \"A.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "an upper case file name");
        bad(first("{ \"file\": \"a b.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a file name with a blank");
        bad(first("{ \"file\": \"c1.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a file given twice");
        bad(first("{ \"file\": \"a.crp\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a crop without a name");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A name that is far too long\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "a name of 28 characters");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A@\", \"params\": { \"weeks\": 5, \"stages\": 4 } }"), "an '@' in a name");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\" }"), "a crop without params (no weeks)");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 0, \"stages\": 4 } }"), "weeks of 0");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 256, \"stages\": 4 } }"), "weeks of 256");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 9 } }"), "9 stages");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 0 } }"), "0 stages");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"yield\": 65536 } }"), "a yield of 65536");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"yield\": -1 } }"), "a negative yield");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"yield\": 1.5 } }"), "a yield that is not whole");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"colour\": 1 } }"), "a param that does not exist");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"waterMin\": 20, \"waterMax\": 10 } }"), "waterMin above waterMax");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"perennial\": 1 } }"), "perennial that is not true or false");
        bad(first("{ \"file\": \"a.crp\", \"name\": \"A\", \"params\": { \"weeks\": 5, \"stages\": 4, \"perennial\": true } }"), "a perennial without a harvest week");
        bad(first("{ " + base + ", \"help\": [\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\"] }"), "8 help lines");
        bad(first("{ " + base + ", \"help\": [\"" + std::string(27, 'x') + "\"] }"), "a help line of 27 characters");
        bad(first("{ " + base + ", \"help\": [5] }"), "a help line that is not text");
        bad(first("{ " + base + ", \"info\": \"" + std::string(251, 'x') + "\" }"), "an info of 251 characters");
        bad(first("{ " + base + ", \"art\": \"crops/missing.png\" }"), "art without the pack's tile size");
        bad("{ \"format\": 1, \"tileSize\": 32, \"crops\": [ { " + base + ", \"art\": \"crops/missing.png\" } ] }", "art that is not there (and too few crops)");
    }

    // ---------------------------------------------------------------- the registry
    std::printf("-- the registry\n");
    {
        std::vector<CropDef> crops;
        bool present = false;
        std::string e;
        ParsePackCrops(Crops(kGood), NoFiles, nullptr, crops, present, e);
        check(!HavePackCrops() && DefaultCropFiles().size() == 16 && DefaultCropFiles()[0] == "corn.crp", "with no pack crops: the original's 16 default files");
        const std::string dir = (std::filesystem::temp_directory_path() / "osf_cropstest").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir + "/Crops");
        std::ofstream(dir + "/Crops/Zebra.crp").put('x');
        std::ofstream(dir + "/Crops/alpha.CRP").put('x');
        std::ofstream(dir + "/Crops/notes.txt").put('x');
        check(CropFiles(dir) == std::vector<std::string>({ "ALPHA.CRP", "ZEBRA.CRP" }) && CropFiles("").empty(), "  and the CROPS folder's *.CRP files, upper case and sorted");
        CropDef none;
        check(!LoadCropFile(dir, "corn.crp", none), "  a crop that is in no folder is not loaded");
        SetPackCrops(crops);
        check(HavePackCrops() && DefaultCropFiles().size() == 16 && DefaultCropFiles()[0] == "corn.crp" && DefaultCropFiles()[15] == "c15.crp", "with pack crops: its first 16 are the defaults, in order");
        CropDef got;
        check(LoadCropFile(dir, "CORN.CRP", got) && got.name == "Corn" && got.loaded && got.fileName == "CORN.CRP" && got.HeatNeed() == 300, "  a crop is found by its file name in any case (and keeps the name it was asked by)");
        check(!LoadCropFile(dir, "alpha.crp", got), "  the folder's crops are not there any more: a pack's crops are all there are");
        const auto files = CropFiles(dir);
        check(files.size() == 16 && files[0] == "C1.CRP" && files.back() == "CORN.CRP", "  the list is the pack's, upper case and sorted");
        SetPackCrops({});
        check(!HavePackCrops() && CropFiles(dir).size() == 2 && !LoadCropFile(dir, "corn.crp", got), "clearing them puts the folder back");
        std::filesystem::remove_all(dir, ec);
    }

    // ---------------------------------------------------------------- art
    std::printf("-- art\n");
    {
        CropDef c;
        check(c.IconArgb16().empty(), "a crop without truecolour art has no RGB icon");
        c.artSize = 32;
        c.artArgb.assign(14 * 32 * 32, 0xFF102030u);
        for (int i = 0; i < 32 * 32; ++i)
            c.artArgb[static_cast<size_t>(13) * 1024 + i] = i % 32 < 16 ? 0xFFFF0000u : 0xFF0000FFu; // icon: left red, right blue
        const std::vector<uint32_t> icon = c.IconArgb16();
        check(icon.size() == 256 && icon[0] == 0xFFFF0000u && icon[7] == 0xFFFF0000u && icon[8] == 0xFF0000FFu && icon[15] == 0xFF0000FFu && icon[255] == 0xFF0000FFu,
              "the icon is tile 13 shrunk to 16 x 16, left and right in place");
        c.artSize = 16; // a tile of 16 pixels is taken as it is
        c.artArgb.assign(14 * 256, 0xFF010203u);
        c.artArgb[static_cast<size_t>(13) * 256 + 5 * 16 + 7] = 0xFFAABBCCu;
        check(c.IconArgb16()[5 * 16 + 7] == 0xFFAABBCCu && c.IconArgb16()[0] == 0xFF010203u, "  a 16-pixel tile is the icon itself");

        TileSheet sheet;
        sheet.count = 848;
        sheet.size = 8;
        sheet.argb.assign(static_cast<size_t>(848) * 64, 0xFF000000u);
        std::vector<uint32_t> art(14 * 64);
        for (int k = 0; k < 14; ++k)
            for (int i = 0; i < 64; ++i)
                art[static_cast<size_t>(k) * 64 + i] = 0xFF000000u | static_cast<uint32_t>(k + 1);
        check(ApplyCropArtArgb(sheet, 3, art, 8), "art goes into a truecolour sheet of the same tile size");
        auto tile = [&](int id) { return sheet.argb[static_cast<size_t>(id) * 64 + 5]; };
        check(tile(688 + 24) == 0xFF000001u && tile(688 + 31) == 0xFF000008u && tile(539 + 12) == 0xFF000009u && tile(539 + 15) == 0xFF00000Cu && tile(816 + 3) == 0xFF00000Du && tile(832 + 3) == 0xFF00000Eu,
              "  growth over 688 + 8 slot, the sign over 539 + 4 slot, the icons over 816 + slot and 832 + slot");
        check(tile(688 + 23) == 0xFF000000u && tile(688 + 32) == 0xFF000000u && tile(816 + 4) == 0xFF000000u, "  and no other tile is touched");
        check(!ApplyCropArtArgb(sheet, 3, art, 16) && !ApplyCropArtArgb(sheet, 16, art, 8) && !ApplyCropArtArgb(sheet, -1, art, 8) && !ApplyCropArtArgb(sheet, 0, std::vector<uint32_t>(10), 8),
              "  not for another tile size, a slot out of 0..15, or too little art");
        TileSheet indexed;
        indexed.count = 848;
        indexed.pixels.assign(848 * 256, 0);
        check(!ApplyCropArtArgb(indexed, 0, art, 8), "  nor for an indexed sheet");
    }

    // ---------------------------------------------------------------- the game on a pack's crops
    if (packFolder)
    {
        std::printf("-- the game on the crops of %s\n", packFolder);
        std::vector<CropDef> crops;
        bool present = false;
        std::string e;
        const bool loaded = LoadPackCrops(packFolder, crops, present, e) && present;
        check(loaded && crops.size() == 24, "its 24 crops are read, with art" + (loaded ? "" : ": " + e));
        if (loaded)
        {
            bool art = true;
            for (const CropDef& c : crops)
                art = art && c.HasArgbArt() && c.artSize == 32 && c.IconArgb16().size() == 256 && c.artArgb[static_cast<size_t>(12) * 1024] == 0xFFC8B078u;
            check(art, "  every crop has its 14 pictures (tile 12's frame is tan, as drawn)");
            int perennial = 0, hand = 0;
            for (const CropDef& c : crops)
            {
                perennial += c.Perennial();
                hand += c.HandPicked();
            }
            check(perennial == 4 && hand == 7 && crops[16].fileName == "gladiolu.crp", "  4 orchards, 7 hand-picked crops, the spares after the 16 defaults");
            SetPackCrops(crops);

            const std::string dir = (std::filesystem::temp_directory_path() / "osf_cropstest2").string();
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir);
            {
                // a fake archive: one dummy entry
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
            Megadata mega;
            std::vector<uint8_t> blob;
            TableBlobs tables;
            bool hasTemplate = false;
            check(mega.Open(dir, e) && LoadPackTemplate(packFolder, blob, hasTemplate, e) && hasTemplate && LoadPackTables(packFolder, tables, e), "the template and tables of the pack are made");
            mega.Override(kTemplateEntry, blob);
            for (const auto& [name, bytes] : tables)
                mega.Override(name, bytes);
            GameState tmpl, world;
            WorldOptions opt;
            opt.seed = 5;
            std::string gerr;
            const bool gen = LoadTemplateState(mega, dir, tmpl, e) && GenerateWorld(dir, mega, tmpl, opt, world, gerr);
            check(gen, "a world is generated" + (gen ? "" : ": " + gerr + e));
            if (gen)
            {
                bool all = true, prices = true;
                for (int c = 0; c < 16; ++c)
                {
                    all = all && world.crops[static_cast<size_t>(c)].loaded && world.crops[static_cast<size_t>(c)].fileName == crops[static_cast<size_t>(c)].fileName;
                    prices = prices && world.MarketPrice(c) == std::max(10, crops[static_cast<size_t>(c)].BasePrice()) && world.CatalogPrice(66 + c) == crops[static_cast<size_t>(c)].SeedPrice() &&
                             world.CatalogPrice(50 + c) == world.MarketPrice(c);
                }
                check(all && prices, "  the 16 default crops are loaded in their slots, with market, stored-stack and seed prices from their parameters");
                bool names = true;
                for (int c = 0; c < 16; ++c)
                {
                    std::string n(reinterpret_cast<const char*>(&world.cropNames[static_cast<size_t>(c) * 14]), 14);
                    names = names && n.substr(0, n.find('\0')) == crops[static_cast<size_t>(c)].fileName;
                }
                check(names, "  the save's crop name table holds their file names");

                // save, load: the crops come back by name from the registry
                const std::string path = dir + "/crops.sfm";
                GameState back;
                check(world.Save(path, e) && back.Load(dir, path, e, false), "the world is saved and read back");
                bool same = true;
                for (int c = 0; c < 16; ++c)
                    same = same && back.crops[static_cast<size_t>(c)].loaded && back.crops[static_cast<size_t>(c)].name == world.crops[static_cast<size_t>(c)].name &&
                           back.crops[static_cast<size_t>(c)].HasArgbArt() && back.crops[static_cast<size_t>(c)].Weeks() == world.crops[static_cast<size_t>(c)].Weeks();
                check(same, "  its crops are found again by the names in the save, art and all");
                GameState reset;
                check(reset.Load(dir, path, e, true) && reset.MarketPrice(3) == std::max(10, crops[3].BasePrice()), "  a load puts the market prices back to the crops' own, as the original does");

                // the spare crops: the 8 not loaded
                Simulation sim(world);
                sim.SetGameDir(dir);
                sim.SetTownLayout(mega.Read("townly0.cmp"));
                const auto& spare = sim.SpareMarket();
                bool icons = spare.size() == 8;
                for (const auto& m : spare)
                    icons = icons && !m.iconArgb.empty() && m.icon.empty() && m.basePrice > 0 && m.history[29] == m.basePrice;
                check(icons, "the spare crops (the 8 after the first 16) are listed with RGB icons and a price history");

                // the autoplayer farms for three years, on five different worlds and games of chance
                for (const unsigned seed : { 3u, 5u, 8u, 13u, 21u })
                {
                    const std::string seedText = std::to_string(seed);
#ifdef _WIN32
                    _putenv_s("OSF_SEED", seedText.c_str());
#else
                    setenv("OSF_SEED", seedText.c_str(), 1);
#endif
                    WorldOptions o2;
                    o2.seed = seed;
                    GameState play;
                    std::string perr;
                    if (!GenerateWorld(dir, mega, tmpl, o2, play, perr))
                    {
                        check(false, "seed " + seedText + ": a world is generated: " + perr);
                        continue;
                    }
                    Simulation psim(play);
                    psim.SetGameDir(dir);
                    psim.SetTownLayout(mega.Read("townly0.cmp"));
                    Machines::smartDriving = true;
                    const std::string ckpt = dir + "/ap" + seedText;
                    std::filesystem::create_directories(ckpt);
                    AutoPlayer ap(play, psim, ckpt);
                    const int endYear = play.cal.year + 3;
                    int sales = 0, wasted = 0, lastWeek = -1;
                    long long sold = 0;
                    int wet[13] = {};
                    std::string m;
                    for (long guard = 0; play.cal.year < endYear && !psim.Bankrupt() && psim.GameOverChoice() == 0 && guard < 400000000; ++guard)
                    {
                        psim.TimerTick();
                        ap.Tick();
                        while (psim.TakeMessage(m))
                        {
                            if (const size_t at = m.find(": sold for "); at != std::string::npos && m.rfind("Harvested", 0) == 0)
                            {
                                ++sales;
                                sold += std::atoll(m.c_str() + at + 11);
                            }
                            else if (m.rfind("Harvested", 0) == 0 && m.find("worth nothing") != std::string::npos)
                                ++wasted;
                        }
                        GameEvent ev;
                        while (psim.TakeEvent(ev))
                            ap.Answer(ev);
                        while (ap.TakeNote(m)) {}
                        while (psim.TakeSound(m)) {}
                        psim.TakeYearEvaluation();
                        if (play.cal.day == 0 && lastWeek != play.cal.week)
                        {
                            lastWeek = play.cal.week;
                            ++wet[std::clamp(psim.SoilWetness(), 0, 12)];
                        }
                    }
                    int soaked = wet[12], parched = wet[0] + wet[1];
                    int weeks = 0;
                    for (int w : wet) weeks += w;
                    check(play.cal.year >= endYear && !psim.Bankrupt() && sales >= 4 && sold >= 8000 && wasted <= 2 * sales + 2 && soaked * 4 < weeks && parched * 4 < weeks,
                          "seed " + seedText + ": 3 years farmed: " + std::to_string(sales) + " harvests sold for " + std::to_string(sold) + " (" + std::to_string(wasted) +
                              " worth nothing), " + std::to_string(ap.Fields()) + " fields, cash " + std::to_string(static_cast<long long>(play.cash)) + ", soil soaked " +
                              std::to_string(soaked) + " and parched " + std::to_string(parched) + " of " + std::to_string(weeks) + " weeks");
                }
            }
            std::filesystem::remove_all(dir, ec);
        }
    }

    SetPackCrops({});
    std::printf("crops %s\n", fails ? "FAIL" : "ok");
    return fails;
}

} // namespace osf
