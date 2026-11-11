// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
//
// OpenSimFarm runs on the player's own copy of SimFarm (Maxis, 1993); no original file is part of it.
//
// Usage:
//   opensimfarm [path-to-SimFarm-folder] [farm.SSM|save.SFM]      play (the folder is remembered)
//   opensimfarm --reconfigure [farm.SSM|save.SFM]                  choose the SimFarm folder again (folder dialog), then play
//   opensimfarm --findgame [folder]                                where SimFarm is found (search of the usual places)
//   opensimfarm --selftest [folder]                                 load and re-save every prebuilt farm, compare bytes
//   opensimfarm --simulate DAYS [farm] [folder]                     run DAYS game days without a window and print a report
// Controls: menus and toolbar with the left button; right/middle drag, arrows or WASD = scroll; wheel = zoom;
//           1-4 = Slow / Normal / Fast / Ultra, 0 = pause; Space = pause; F5 = save; F9 = load; Esc = close window / quit.

#include "assets/Graphics.h"
#include "audio/Sound.h"
#include "assets/Megadata.h"
#include "assets/Picture.h"
#include "core/Lang.h"
#include "ui/LangTest.h"
#include "core/Paths.h"
#include "game/AutoPlayer.h"
#include "game/Editing.h"
#include "game/Flight.h"
#include "game/Irrigation.h"
#include "game/Machines.h"
#include "game/Shop.h"
#include "game/WorldGen.h"
#include "game/MapEdit.h"
#include "game/GameState.h"
#include "game/Simulation.h"
#include "ui/GameUi.h"
#include "ui/AppIcon.h"
#include "ui/Gfx.h"
#include "ui/MapView.h"
#include "ui/Touch.h"
#include "ui/StartScreen.h"

#include <SDL.h>

#include <algorithm>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The browser version (Emscripten, built with ASYNCIFY): the main loop stays as it is and gives the browser control
// once per frame, until its next screen refresh (also how the page stays responsive; a hidden tab gets no refreshes,
// so the game simply waits there).
EM_ASYNC_JS(void, WaitForNextFrame, (), { await new Promise(function (r) { requestAnimationFrame(r); }); });
// the chosen files cannot be used: the page (site/play) shows why and asks for the folder again
// the browser's text prompt (the phone layout's Save name field): 1 with the text in out, 0 when cancelled
EM_JS(int, AskTextPrompt, (const char* title, const char* current, char* out, int cap), {
    var r = window.prompt(UTF8ToString(title), UTF8ToString(current));
    if (r === null) return 0;
    stringToUTF8(r, out, cap);
    return 1;
});
// the phone layout (GameUi::SetPhone): on a touch screen, or chosen with ?layout=phone / ?layout=desktop in the address
// (the page gets the class "phone" for its own layout: the BETA tab moves off the game's top bar)
EM_JS(int, PhoneLayoutWanted, (), {
    var q = location.search, on;
    if (/[?&]layout=phone/.test(q)) on = 1;
    else if (/[?&]layout=desktop/.test(q)) on = 0;
    else on = (window.matchMedia && window.matchMedia('(pointer: coarse)').matches) ? 1 : 0;
    document.body.classList.toggle('phone', !!on);
    return on;
});
EM_JS(void, ReportGameFilesProblem, (const char* text), {
    if (Module.onGameFilesProblem) Module.onGameFilesProblem(UTF8ToString(text));
});
#endif

using namespace osf;

static void Fatal(const std::string& msg, bool gui = true)
{
    std::fprintf(stderr, "OpenSimFarm: %s\n", msg.c_str());
    if (gui)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenSimFarm", Latin1ToUtf8(msg).c_str(), nullptr);
}

static std::string ResolveFarm(const std::string& gameDir, const std::string& arg)
{
    if (std::filesystem::exists(arg))
        return arg;
    auto found = FindFileNoCase(gameDir, arg);
    if (found)
        return *found;
    auto inConfig = std::filesystem::path(ConfigDir()) / "saves" / arg;
    return std::filesystem::exists(inConfig) ? inConfig.string() : std::string();
}

static const char* kMonths[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static std::string StatusLine(const GameState& s, const Simulation& sim)
{
    static const char* speeds[] = { "Ultra", "Fast", "Normal", "Slow", "Paused" };
    char buf[256];
    std::snprintf(buf, sizeof buf, "Week %d of %s %d  |  Funds %lld  |  %s  |  wind %d  soil %d  %dF",
                  s.cal.week + 1, kMonths[s.cal.month], s.cal.year, static_cast<long long>(s.cash),
                  speeds[static_cast<int>(sim.GetSpeed())], sim.Wind(), sim.SoilWetness(), sim.Temperature());
    return buf;
}

// a scratch file of the test modes, named for this process so that runs side by side do not share it
static std::string TempPath(const std::string& name)
{
#ifdef _WIN32
    const long long pid = _getpid();
#else
    const long long pid = getpid();
#endif
    const std::string path = (std::filesystem::temp_directory_path() / ("osf" + std::to_string(pid) + "_" + name)).string();
    static std::vector<std::string> made; // deleted when the program ends
    if (made.empty())
        std::atexit([] {
            std::error_code ec;
            for (const std::string& p : made)
                std::filesystem::remove(p, ec);
        });
    if (std::find(made.begin(), made.end(), path) == made.end())
        made.push_back(path);
    return path;
}

// The archive's pictures as the interface draws them (both editions; for the Windows edition they are made from its
// program, WindowsEdition.h): each window picture at its size, the button and gauge faces, the font, the sprites (some
// of each frame see-through), the cursors and the picture banks (pictures, not noise)
static int ArchivePictureTest(const Megadata& mega)
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        fails += !ok;
    };
    static const struct { const char* name; int w, h; } kWindows[] = {
        { "about.bmp", 400, 224 },    { "airpwndo.bmp", 256, 176 }, { "bankterm.bmp", 288, 224 }, { "buywndo.bmp", 256, 224 },
        { "cashflow.bmp", 400, 224 }, { "choscrop.bmp", 320, 224 }, { "cntlbtnh.bmp", 640, 32 },  { "evaluate.bmp", 352, 192 },
        { "examine.bmp", 208, 208 },  { "farmburo.bmp", 400, 224 }, { "fdstatus.bmp", 336, 224 }, { "filegenr.bmp", 208, 208 },
        { "futures.bmp", 320, 208 },  { "mapwndo.bmp", 288, 240 },  { "popup.bmp", 256, 192 },    { "sellwndo.bmp", 192, 256 },
        { "twnchoic.bmp", 368, 224 }, { "weather.bmp", 336, 192 },
    };
    // a picture, not noise: most pixels have the colour of their right-hand neighbour
    auto calm = [](const Picture& p) {
        long same = 0;
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x + 1 < p.w; ++x)
                same += p.px[static_cast<size_t>(y) * p.w + x] == p.px[static_cast<size_t>(y) * p.w + x + 1];
        return same * 100 >= static_cast<long>(p.h) * (p.w - 1) * 40;
    };
    for (const auto& w : kWindows)
    {
        Picture p;
        const bool ok = DecodePlanarBitmap(mega.Read(w.name), p) && p.w == w.w && p.h == w.h && calm(p);
        check(ok, std::string(w.name) + " " + std::to_string(p.w) + " x " + std::to_string(p.h));
    }
    {
        Picture p;
        check(DecodePlanarBitmap(DecodeLzss(mega.Read("editeasy.lzs")), p) && p.w == 48 && p.h == 208, "editeasy.lzs 48 x 208");
    }
    for (const auto& [file, faces, wb, h] : { std::tuple<const char*, int, int, int>{ "buttons.til", 18, 6, 24 }, { "fldgrph.til", 20, 6, 24 }, { "wethvane.til", 2, 4, 32 } })
    {
        Picture p;
        // (the weather vane's rotor is drawn over a dotted sky: only its size is checked)
        check(DecodeButtonFace(mega.Read(file), faces - 1, p, wb, h) && (wb == 4 || calm(p)), std::string(file) + " " + std::to_string(faces) + " faces");
    }
    {
        Font8 f;
        const bool ok = DecodeFont(mega.Read("egafont.fnt"), f);
        int inkA = 0, inkSpace = 0;
        for (int r = 0; r < 8; ++r)
        {
            inkA += __builtin_popcount(f.glyphs['A'][static_cast<size_t>(r)]);
            inkSpace += __builtin_popcount(f.glyphs[' '][static_cast<size_t>(r)]);
        }
        check(ok && inkA >= 10 && inkSpace == 0, "the font: A has " + std::to_string(inkA) + " ink pixels, the space none");
    }
    for (const auto& [file, frames] : { std::pair<const char*, int>{ "airplane.spr", 8 }, { "apshadow.spr", 8 }, { "tornado.spr", 4 }, { "swarm.spr", 4 }, { "cowman1.spr", 5 } })
    {
        bool ok = true;
        std::string shares;
        for (int f = 0; f < frames; ++f)
        {
            Picture p;
            if (!DecodeSprite(mega.Read(file), f, p))
            {
                ok = false;
                break;
            }
            const long clear = std::count(p.px.begin(), p.px.end(), 255);
            const long share = clear * 100 / static_cast<long>(p.px.size());
            ok = ok && share >= 2 && share <= 98;
            shares += " " + std::to_string(share);
        }
        check(ok, std::string(file) + " " + std::to_string(frames) + " frames, % see-through:" + shares);
    }
    {
        const std::vector<uint8_t> d = mega.Read("cursors.til");
        bool ok = d.size() >= 1536;
        for (int c = 0; c < 6 && ok; ++c)
        {
            int shown = 0;
            for (int i = 0; i < 32; ++i)
                shown += __builtin_popcount(d[static_cast<size_t>(2 * c) * 128 + i]);
            ok = shown >= 20 && shown < 256;
        }
        check(ok, "cursors.til: six cursors with shapes");
    }
    for (const char* bank : { "alltiles.btl", "wethgrnd.btl", "wethsky.btl" })
    {
        Picture p;
        check(DecodeBankRecord(mega.Read(bank), 0, p) && p.w == 112 && calm(p), std::string(bank) + ": the first picture");
    }
    std::printf("archive pictures %s\n", fails ? "FAIL" : "ok");
    return fails;
}

static int SelfTest(const std::string& gameDir)
{
    int failures = 0;
    for (int i = 0; i < 8; ++i)
    {
        const std::string name = "SCEN" + std::to_string(i) + ".SSM";
        const std::string path = ResolveFarm(gameDir, name);
        GameState s;
        std::string err;
        if (path.empty() || !s.Load(gameDir, path, err, false))
        {
            std::printf("%s: load failed %s\n", name.c_str(), err.c_str());
            failures++;
            continue;
        }
        const std::string out = TempPath("osf_selftest.sfm");
        s.Save(out, err);
        std::ifstream a(path, std::ios::binary), b(out, std::ios::binary);
        std::string da((std::istreambuf_iterator<char>(a)), {}), db((std::istreambuf_iterator<char>(b)), {});
        int crops = 0;
        for (const auto& c : s.crops)
            crops += c.loaded;
        // the saved file is the original's bytes and then OpenSimFarm's 8-byte signature (map size 96)
        std::vector<uint8_t> dv(db.begin(), db.end());
        size_t body = 0;
        const bool same = db.size() == da.size() + kSaveSignatureSize && db.compare(0, da.size(), da) == 0 &&
                          SaveFileMapSize(dv, body) == kOriginalMapSize && body == da.size();
        if (std::getenv("OSF_PROBE"))
        {
            int owners[4] = { 0, 0, 0, 0 };
            for (int c = 0; c < s.Plots(); ++c)
                for (int r = 0; r < s.Plots(); ++r)
                    owners[s.PlotOwner(c, r) & 3]++;
            std::map<int, int> hist;
            for (int x = 0; x < s.world.size; ++x)
                for (int y = 0; y < s.world.size; ++y)
                    if (s.PlotOwner(x / 8, y / 8) == 1 && !s.world.At(x, y).HasObject())
                        hist[s.world.At(x, y).BaseTile()]++;
            int ground = 0, total = 0;
            for (auto [id, n] : hist)
            {
                total += n;
                ground += (id >= 15 && id <= 30) ? n : 0;
                if (n > 20)
                    std::printf(" %d:%d", id, n);
            }
            std::printf("  ground %d of %d", ground, total);
            std::printf("\n");
            Field f1 = s.FieldAt(1);
            std::printf("  plots free %d player %d town %d other %d; field1 at %d,%d owner(x/8,y/8)=%d owner(y/8,x/8)=%d\n", owners[0], owners[1],
                        owners[2], owners[3], f1.X(), f1.Y(), s.PlotOwner(f1.X() / 8, f1.Y() / 8), s.PlotOwner(f1.Y() / 8, f1.X() / 8));
        }
        failures += !same;
        std::printf("%s: %s  fields %d machines %d animals %d buildings %d items %d  crops %d/16  cash %lld  date %d/%d/%d\n",
                    name.c_str(), same ? "round-trip OK" : "ROUND-TRIP DIFFERS", s.fieldCount, s.machineCount, s.animalCount,
                    s.structureCount, s.itemCount, crops, static_cast<long long>(s.cash), s.cal.week + 1, s.cal.month + 1, s.cal.year);
    }
    return failures ? 1 : 0;
}

static int SimulateHeadless(const std::string& gameDir, const std::string& farm, int days)
{
    GameState s;
    std::string err;
    const std::string path = ResolveFarm(gameDir, farm);
    if (path.empty() || !s.Load(gameDir, path, err))
    {
        Fatal("cannot load " + farm + " " + err, false);
        return 1;
    }
    Simulation sim(s);
    std::printf("start: %s  land %lld  est.tax %lld\n", StatusLine(s, sim).c_str(), static_cast<long long>(sim.LandValue()),
                static_cast<long long>(sim.EstimatedTax()));
    for (int d = 0; d < days; ++d)
    {
        for (int t = 0; t <= s.speedDelay; ++t)
            sim.TimerTick();
        std::string m;
        while (sim.TakeMessage(m))
            std::printf("  [%d/%s/%d] %s\n", s.cal.week + 1, kMonths[s.cal.month], s.cal.year, m.c_str());
        GameEvent ev;
        while (sim.TakeEvent(ev))
            if (ev.question)
                std::printf("  [%d/%s/%d] (question, answered no) %s\n", s.cal.week + 1, kMonths[s.cal.month], s.cal.year, ev.text.c_str());
        while (sim.TakeSound(m))
        {
        }
        if (s.cal.day == 0 && s.cal.week == 0)
        {
            std::printf("%s\n", StatusLine(s, sim).c_str());
            for (int i = 1; i < 98; ++i)
            {
                Field f = s.FieldAt(i);
                if (f.InUse())
                    std::printf("    field %2d %-12s state %d age %3d stress %5d pests %3d weeds %3d disease %3d A %3d tox %3d heat %4d\n", i,
                                s.crops[f.Crop() & 15].name.c_str(), f.State(), f.Age(), f.Stress(), f.Pests(), f.Weeds(), f.Disease(),
                                f.NutrientA(), f.Toxicity(), f.HeatDeficit());
            }
        }
    }
    std::printf("end:   %s  prices corn-slot0 %d\n", StatusLine(s, sim).c_str(), s.MarketPrice(0));
    return 0;
}

// Exercises the EDIT tools on a prebuilt farm: a road line, bulldozing it, and a new corn field grown for 20 weeks.
static int ToolsTest(const std::string& gameDir, const std::string& farm)
{
    GameState s;
    std::string err;
    if (!s.Load(gameDir, ResolveFarm(gameDir, farm), err))
        return 1;
    std::printf("%s\n", farm.c_str());
    Simulation sim(s);
    int failures = 0;
    auto expect = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        failures += !ok;
    };
    if (farm == "SCEN1.SSM" || farm == "SCEN3.SSM")
    {
        // smart driving (user findings F25 / F37): on prebuilt farms 2 and 4 no machine stays busy for 30 days in a
        // row over 200 days (a hitched implement driven on its own used to hold its lead in place for months; real work
        // runs up to about 16 days in a row on farm 2, depending on the clock's seed: 60 seeds measured 2026-11-10)
        const bool was = Machines::smartDriving;
        Machines::smartDriving = true;
        int longest = 0, badPictures = 0;
        {
            GameState b;
            if (!b.Load(gameDir, ResolveFarm(gameDir, farm), err))
                return 1;
            Simulation bsim(b);
            std::map<int, int> busy;
            for (int d = 0; d < 200; ++d)
            {
                for (int t = 0; t <= b.speedDelay; ++t)
                    bsim.TimerTick();
                std::string m;
                while (bsim.TakeMessage(m)) {}
                GameEvent e;
                while (bsim.TakeEvent(e)) {}
                std::vector<bool> covered(static_cast<size_t>(b.world.size) * b.world.size, false);
                for (int i = 1; i < 127; ++i)
                {
                    Machine m2(b.machines, i);
                    if (m2.Type() > 8 || !(m2.Flags() & 0x0B))
                        busy.erase(i);
                    else
                        longest = std::max(longest, ++busy[i]);
                    if (!m2.InUse() || !(m2.Flags() & 0x80) || m2.Type() > 7)
                        continue;
                    const bool v = (m2.Heading() & 7) == 0 || (m2.Heading() & 7) == 4;
                    for (int k = 0; k < Machines::Cells(m2.Type()); ++k)
                    {
                        const int x = m2.X() + (v ? 0 : k), y = m2.Y() + (v ? k : 0);
                        if (x >= b.world.size || y >= b.world.size)
                            continue;
                        covered[static_cast<size_t>(x) * b.world.size + y] = true;
                        const int o = b.world.At(x, y).overlay & 0x7FF;
                        if (!(m2.Flags() & 0x0C) && (!b.world.At(x, y).HasObject() || o < 848 || o > 945))
                            ++badPictures; // a drawn machine on the road missing a cell: half a truck (F38)
                    }
                }
                for (int x = 0; x < b.world.size; ++x)
                    for (int y = 0; y < b.world.size; ++y)
                    {
                        const int o = b.world.At(x, y).overlay & 0x7FF;
                        if (b.world.At(x, y).HasObject() && o >= 848 && o <= 911 && !covered[static_cast<size_t>(x) * b.world.size + y])
                            ++badPictures; // a machine picture where no machine is
                    }
            }
        }
        Machines::smartDriving = was;
        expect(longest < 30, "smart driving: no machine stays busy 30 days in a row (longest " + std::to_string(longest) + ")");
        expect(badPictures == 0, "smart driving: every machine on the way is drawn whole and no machine picture is left behind (" +
                                     std::to_string(badPictures) + " bad cell-days)");
    }
    if (farm == "SCEN1.SSM")
    {
        // the rodeo and the fair on this farm's event ground: offers at the month change, the ground's
        // picture bank, the rodeo piece and the crowd, the result after the weekly countdown, the ground restored
        {
            // F53: the event-ground table holds (x, y) pairs (this farm: 8, 40 for its ground on plot 1,5); a table
            // written the old engine way (x values, then the y values 8 bytes later) is put back into pairs on load
            GameState g = s;
            const bool pairs = g.eventGrounds.size() >= 16 && g.eventGrounds[0] == 8 && g.eventGrounds[1] == 40;
            std::fill(g.eventGrounds.begin(), g.eventGrounds.end(), 0);
            if (g.eventGrounds.size() >= 16)
            {
                g.eventGrounds[0] = 8;
                g.eventGrounds[8] = 40;
            }
            g.FixEventGroundTable();
            expect(pairs && g.eventGrounds[0] == 8 && g.eventGrounds[1] == 40 && g.eventGrounds[8] == 0,
                   "the event-ground table is (x, y) pairs; an old engine table is repaired");
        }
        GameState t = s;
        Simulation tsim(t);
        t.cal.month = 5;
        t.cal.week = 3;
        t.cal.day = 6;
        tsim.DayTick();
        GameEvent ev;
        bool offered = false;
        while (tsim.TakeEvent(ev))
            offered |= ev.id == kRodeoEvent && ev.question;
        const int64_t before = t.cash;
        tsim.Answer(kRodeoEvent, true);
        expect(offered && tsim.EventKind() == 1 && t.cash == before - 500 && t.scalars["ev_countdown"] == 2,
               "rodeo offered at the change into July; YES pays 500, countdown 2 weeks");
        for (int k = 0; k < 200; ++k)
            tsim.TimerTick();
        int pieces = 0;
        for (const auto& c : t.world.cells)
            pieces += c.HasObject() && (c.overlay & 0x7FF) >= 937 && (c.overlay & 0x7FF) <= 944;
        expect(pieces == 1, "rodeo: " + std::to_string(pieces) + " piece in the arena");
        {
            // F53: the rider shown changes at most once every 32 ticks at Normal (the original's view redraw), though
            // the pass itself runs every 8 ticks
            auto state = [&] {
                std::string st;
                for (size_t i = 0; i < t.world.cells.size(); ++i)
                    if (t.world.cells[i].HasObject() && (t.world.cells[i].overlay & 0x7FF) >= 937 && (t.world.cells[i].overlay & 0x7FF) <= 944)
                        st += std::to_string(i) + ":" + std::to_string(t.world.cells[i].overlay & 0x7FF) + " ";
                return st;
            };
            std::string last = state();
            int changes = 0, gap = 1000, since = 0;
            for (int k = 0; k < 640; ++k)
            {
                tsim.TimerTick();
                ++since;
                const std::string now = state();
                if (now != last)
                {
                    if (changes++ > 0)
                        gap = std::min(gap, since);
                    since = 0;
                    last = now;
                }
            }
            expect(changes >= 8 && changes <= 20 && gap >= 32,
                   "rodeo rider pace: " + std::to_string(changes) + " changes in 640 ticks, at least " + std::to_string(gap) + " apart");
        }
        bool result = false;
        for (int d = 0; d < 14 && !result; ++d)
        {
            tsim.DayTick();
            while (tsim.TakeEvent(ev))
                result |= ev.id == -63 || ev.id == -64;
        }
        expect(result && tsim.EventKind() == 0 && t.cal.month == 6 && t.cal.week == 2,
               "rodeo result two weeks later, ground back to normal");
        t.cal.month = 8;
        t.cal.week = 3;
        t.cal.day = 6;
        tsim.DayTick();
        bool notice = false;
        while (tsim.TakeEvent(ev))
            notice |= ev.id == -61;
        for (int k = 0; k < 64; ++k)
            tsim.TimerTick();
        expect(notice && tsim.EventKind() == 2 && t.scalars["ev_countdown"] == 3, "fair notice (no animals) and the fair scene");
        for (int d = 0; d < 21; ++d)
            tsim.DayTick();
        int plain = 0;
        for (const auto& c : t.world.cells)
            plain += c.BaseTile() >= 483 && c.BaseTile() <= 517;
        expect(tsim.EventKind() == 0 && plain == 35, "fair over after 3 weeks; the ground's 35 pictures restored");
    }
    // the prebuilt farms have no free owned land: take the first unowned, empty plot (test setup)
    for (int c = 0; c < s.Plots(); ++c)
        for (int r = 0; r < s.Plots(); ++r)
        {
            bool empty = s.PlotOwner(c, r) == 0;
            for (int x = c * 8; x < c * 8 + 8 && empty; ++x)
                for (int y = r * 8; y < r * 8 + 8 && empty; ++y)
                    empty = !s.world.At(x, y).HasObject() && s.world.At(x, y).BaseTile() >= 15 && s.world.At(x, y).BaseTile() <= 30;
            if (empty && c > 0 && r > 0 && c < 11 && r < 11)
            {
                s.SetPlotOwner(c, r, 1);
                c = r = 12;
            }
        }
    // find a valid field position
    int fx = -1, fy = -1;
    for (int x = 1; x < 86 && fx < 0; ++x)
        for (int y = 1; y < 86 && fx < 0; ++y)
        {
            const int64_t before = s.cash;
            GameState probe = s;
            Simulation psim(probe);
            if (ApplyTool(probe, psim, Tool::Plant, x, y, 0).ok)
            {
                fx = x;
                fy = y;
            }
            (void)before;
        }
    if (fx < 0)
    {
        std::printf("skip no empty plot to test on\n");
        return failures ? 1 : 0; // (the checks above still count)
    }
    expect(true, "found a valid field position at " + std::to_string(fx) + "," + std::to_string(fy));
    const int fieldsBefore = s.fieldCount;
    const EditResult r = ApplyTool(s, sim, Tool::Plant, fx, fy, 0);
    expect(r.ok && s.fieldCount == fieldsBefore + 1, "plant corn creates a field: " + r.message);
    const int fi = sim.FieldIndexAt(fx + 3, fy + 3);
    expect(fi > 0 && s.FieldAt(fi).State() == 3, "the new field is in the planting state");
    {
        const CropDef& cd = s.crops[s.FieldAt(fi).Crop() & 15];
        bool harvestMark = false;
        for (int r = s.cal.AbsWeek(); r < 240; ++r)
            harvestMark |= s.ScheduleCode(r, fi) == 0x1B8;
        expect(harvestMark, "the new field's plan has a harvest week (" + cd.name + ", " + std::to_string(cd.Weeks()) + " weeks)");
    }
    expect(!ApplyTool(s, sim, Tool::Plant, fx + 1, fy + 1, 1).ok, "a second field may not overlap");
    const int crop = s.FieldAt(fi).Crop();
    std::printf("     crop %s\n", s.crops[crop & 15].name.c_str());
    {
        // machines: plough and plant by driving, before any week deadline
        int ticks = 0;
        while (s.FieldAt(fi).State() == 3 && ticks < 40000)
        {
            sim.Fleet().Tick(true);
            ticks++;
            if (std::getenv("OSF_TRACE") && ticks < 200)
            {
                Machine l(s.machines, s.FieldAt(fi).Get16(0x28));
                std::printf("t%d sub %d lead %d,%d stuck %d\n", ticks, s.FieldAt(fi).Sub(), l.X(), l.Y(), l.Stuck());
            }
        }
        Field g = s.FieldAt(fi);
        if (g.State() != 4)
        {
            Machine l(s.machines, g.Get16(0x28));
            std::printf("     lead %d type %d at %d,%d target %d,%d stuck %d flags %x partner %d field %d,%d\n", l.index, l.Type(), l.X(), l.Y(), l.TargetX(),
                        l.TargetY(), l.Stuck(), l.Flags(), l.Partner(), g.X(), g.Y());
        }
        expect(g.State() == 4, "machines planted the field in " + std::to_string(ticks) + " steps (state " + std::to_string(g.State()) +
                                   " sub " + std::to_string(g.Sub()) + ")");
        expect(s.world.At(fx + 3, fy + 4).BaseTile() == 688 + 8 * (g.Crop() & 15), "the sweep painted the crop tiles");
        for (int t = 0; t < 4000; ++t)
            sim.Fleet().Tick(true);
        int busy = 0;
        for (int i = 1; i < 127; ++i)
        {
            Machine m(s.machines, i);
            busy += m.InUse() && (m.Flags() & 0x0B);
        }
        expect(busy == 0, "all machines are back and free (" + std::to_string(busy) + " busy)");
    }
    for (int d = 0; d < 7 * 6; ++d)
        for (int t = 0; t <= s.speedDelay; ++t)
            sim.TimerTick();
    Field f = s.FieldAt(fi);
    // (a perennial may already have reached its yearly harvest week in these six weeks)
    expect(f.State() == 4 && (f.Age() >= 3 || s.crops[f.Crop() & 15].Perennial()),
           "after 6 weeks the crop grows: state " + std::to_string(f.State()) + " age " + std::to_string(f.Age()));
    {
        Field g = s.FieldAt(fi);
        std::string why;
        s.scalars["opt_autobuy"] = 1;
        const bool sprayed = sim.StartSpray(g, 6, why);
        int ticks = 0;
        while (sprayed && s.FieldAt(fi).State() == 6 && ticks < 40000)
        {
            sim.Fleet().Tick(true);
            ticks++;
        }
        expect(sprayed && s.FieldAt(fi).State() == 4, "fertilizer job done by a tractor and sprayer in " + std::to_string(ticks) + " steps " + why);
        Field h = s.FieldAt(fi);
        const bool started = sim.StartHarvest(h);
        ticks = 0;
        const bool handPicked = s.crops[s.FieldAt(fi).Crop() & 15].HandPicked(); // pickers + truck, needs days to pass
        while (started && s.FieldAt(fi).State() == 5 && ticks < 40000)
        {
            if (handPicked)
                sim.TimerTick();
            else
                sim.Fleet().Tick(true);
            ticks++;
        }
        const int after = s.FieldAt(fi).State();
        expect(started && after != 5, "harvest done by harvester, trailer and truck in " + std::to_string(ticks) + " steps (state " + std::to_string(after) + ")");
        for (int t = 0; t < 4000; ++t)
            sim.Fleet().Tick(true);
    }
    for (int crop = 0; crop < 16; ++crop)
    {
        // a hand-picked crop (P37): the pickers are paid, a truck fetches the crop, stubble, then store or sell
        if (!s.crops[crop].loaded || !s.crops[crop].HandPicked())
            continue;
        GameState t = s;
        Simulation tsim(t);
        Field h = t.FieldAt(fi);
        h.SetState(4);
        h.SetSub(0);
        h.SetCrop(crop);
        h.Set16(0x38, 500);
        h.Set16(0x28, 0);
        const int64_t before = t.cash;
        const bool started = tsim.StartHarvest(h) && h.Sub() == 10;
        int ticks = 0;
        int64_t paid = 0;
        while (started && t.FieldAt(fi).State() == 5 && ticks < 20000)
        {
            const int64_t c0 = t.cash;
            const bool picking = t.FieldAt(fi).Sub() == 10;
            tsim.TimerTick();
            if (picking && t.FieldAt(fi).Sub() == 11)
                paid = c0 - t.cash;
            ticks++;
        }
        expect(started && t.FieldAt(fi).State() != 5 && paid >= 500 && before >= t.cash - 100000,
               "hand harvest of " + t.crops[crop].name + " (crew paid, truck, " + std::to_string(ticks) + " ticks)");
        break;
    }
    {
        // irrigation: open water, a pump, and a ditch along the field's left edge (tiles written directly)
        GameState t = s;
        Simulation tsim(t);
        auto set = [&](int x, int y, int id) { if (x >= 0 && y >= 0 && x < 96 && y < 96) t.world.At(x, y).base = static_cast<uint16_t>(id); };
        const int dx = fx - 1;
        set(dx - 2, fy, 55 | 0x2000); // open water (cell flag)
        set(dx - 1, fy, 120);
        // the pump building and its entry in the source list, as a purchase makes them
        for (int i = 1; i < 127; ++i)
            if (!(Rd8(t.structures, static_cast<size_t>(i) * 70 + 2) & 0x20))
            {
                const size_t b = static_cast<size_t>(i) * 70;
                Wr16(t.structures, b, 0x46);
                Wr8(t.structures, b + 2, 0x20);
                Wr8(t.structures, b + 4, dx - 1);
                Wr8(t.structures, b + 5, fy);
                t.structureCount = std::max(t.structureCount, i + 1);
                break;
            }
        tsim.Water().AddSource(dx - 1, fy, 0x40);
        for (int y = fy; y < fy + 8; ++y)
            set(dx, y, 87);
        ReshapeAround(t, dx, fy + 3);
        for (int y = fy; y < fy + 8; ++y)
            ReshapeAround(t, dx, y);
        for (int k = 0; k < 64 + 60; ++k) // first fill pass and its queue steps
            tsim.TimerTick();
        int full = 0;
        for (int y = fy; y < fy + 8; ++y)
            full += t.world.At(dx, y).BaseTile() >= 98 && t.world.At(dx, y).BaseTile() <= 108;
        Field tf = t.FieldAt(fi);
        expect(full == 8 || tf.FullDitches() > 0, "a pump beside water fills the ditch (" + std::to_string(full) + " full cells, field counts " +
                                                     std::to_string(tf.FullDitches()) + " full / " + std::to_string(tf.EmptyDitches()) + " empty)");
    }
    {
        // disasters: all six started, two weeks of ticks, the farm still saves
        GameState t = s;
        Simulation tsim(t);
        // start rules of the original: drought, flood, frost, windstorm, then locusts all start; a tornado
        // then ends the swarm, and neither a windstorm nor locusts can start while it lasts
        int started = 0;
        for (int id : { 2, 3, 5, 6, 4, 1 })
            started += tsim.Hazards().Start(id);
        const bool swarmEnded = !tsim.Hazards().Active(Disasters::kLocusts);
        const bool blocked = !tsim.Hazards().Start(Disasters::kLocusts);
        expect(swarmEnded && blocked, "a tornado ends the locust swarm and keeps a new one from starting");
        const int fields0 = t.fieldCount;
        for (int k = 0; k < 256 * 14; ++k)
            tsim.TimerTick();
        std::string e2;
        const std::string p2 = TempPath("osf_dis.sfm");
        expect(started == 6 && t.Save(p2, e2), "all disasters start and run for two weeks (fields " + std::to_string(fields0) + " -> " + std::to_string(t.fieldCount) +
                                                  ", flood/drought/frost/windstorm/tornado/locusts)");
        GameState off = s;
        Simulation osim(off);
        osim.Hazards().SetDisabled(true);
        expect(!osim.Hazards().Start(1), "Disable stops new disasters");
        // the price shock comes before the active test: five Flood requests in a row raise the last
        // history week five times (x1.5 each, capped at 4 x base), though only the first starts a flood; a disabled
        // game does nothing
        GameState fl = s;
        Simulation fsim(fl);
        int crop = -1;
        for (int c = 0; c < 16 && crop < 0; ++c)
            if (fl.crops[c].loaded && fl.crops[c].B(50) == 1 && fl.crops[c].BasePrice() > 0)
                crop = c;
        if (crop >= 0)
        {
            const int base = fl.crops[crop].BasePrice();
            int expectH = fsim.PriceHistory()[crop][29];
            int starts = 0;
            for (int k = 0; k < 5; ++k)
            {
                starts += fsim.Hazards().Start(Disasters::kFlood);
                expectH = std::min(4 * base, expectH + (expectH >> 1));
            }
            expect(starts == 1 && fsim.PriceHistory()[crop][29] == expectH,
                   "five Flood requests: one flood, five stacked price shocks (" + fl.crops[crop].name + " " + std::to_string(fsim.PriceHistory()[crop][29]) + ")");
            const int before = osim.PriceHistory()[crop][29];
            osim.Hazards().Start(Disasters::kFlood);
            expect(osim.PriceHistory()[crop][29] == before, "with disasters disabled a Flood request leaves the prices alone");
        }
        // a right click on a building: its inside (slot pictures, 0xD9 for free slots), then its outside
        // again with the same tile ids (closing writes whole words, so flag bits such as 0x800 are dropped, as in the
        // original)
        GameState in = s;
        Simulation isim(in);
        for (int i = 1; i < 127; ++i)
        {
            const size_t b = static_cast<size_t>(i) * 70;
            const int cat = Rd16(in.structures, b);
            if (!(Rd8(in.structures, b + 2) & 0x20) || cat < 0x40 || cat > 0x45)
                continue;
            const int bx = Rd8(in.structures, b + 4), by = Rd8(in.structures, b + 5);
            const World before = in.world;
            const bool opened = isim.ToggleInside(bx, by) == i && (Rd8(in.structures, b + 2) & 1);
            const int slot0 = Rd16(in.structures, b + 0x22) == 0xFFFF ? 0xD9 : Rd16(in.structures, b + 0x0A);
            const bool inside = (in.world.At(bx, by).base & 0x7FF) == (slot0 < 0x350 ? slot0 : 0xD9);
            const bool closed = isim.ToggleInside(bx, by) == i && !(Rd8(in.structures, b + 2) & 1);
            bool same = true;
            std::string diff;
            for (int k = 0; k < in.world.size * in.world.size; ++k)
                if (in.world.cells[static_cast<size_t>(k)].BaseTile() != before.cells[static_cast<size_t>(k)].BaseTile() && same)
                {
                    same = false;
                    diff = " cell " + std::to_string(k / in.world.size) + "," + std::to_string(k % in.world.size) + " " +
                           std::to_string(before.cells[static_cast<size_t>(k)].base) + " -> " + std::to_string(in.world.cells[static_cast<size_t>(k)].base);
                }
            expect(opened && inside && closed && same, "a right click shows a building's inside and its outside again (type " +
                                                         std::to_string(cat) + " at " + std::to_string(bx) + "," + std::to_string(by) + ")" + diff + (opened ? "" : " not opened") + (inside ? "" : " inside wrong") + (closed ? "" : " not closed"));
            break;
        }
    }
    if (farm == "SCEN0.SSM")
    {
        // the crop duster: bought at the airport, fuelled, loaded, flown with spraying, landed back
        GameState t = s;
        Simulation tsim(t);
        Flight& f = tsim.Plane();
        const std::string why = f.Buy();
        int plane = 0;
        for (int i = 1; i < 127 && !plane; ++i)
            if (Rd16(t.machines, static_cast<size_t>(i) * 56) == 8 && (Rd8(t.machines, static_cast<size_t>(i) * 56 + 2) & 0x20))
                plane = i;
        expect(why.empty() && plane, "a crop duster is bought at the airport " + why);
        if (plane)
        {
            // parked at (plot x + 3 or 5, plot y + 6), 71 72 over 111 112, the plot's hangar bit set
            Machine pm(t.machines, plane);
            const int hx = pm.HomeX(), hy = pm.HomeY(), hp = t.PlotType(hx / 8, hy / 8);
            expect((hx % 8 == 3 || hx % 8 == 5) && hy % 8 == 6 && (hp & 15) == 5 && (hp & (hx % 8 == 3 ? 0x20 : 0x40)) &&
                       t.world.At(hx, hy).BaseTile() == 71 && t.world.At(hx + 1, hy).BaseTile() == 72 &&
                       t.world.At(hx, hy + 1).BaseTile() == 111 && t.world.At(hx + 1, hy + 1).BaseTile() == 112,
                   "the plane is parked in its hangar slot");
            const int64_t c = t.cash;
            expect(f.Refuel(plane) && f.Reload(plane) && t.cash == c - 255 - 5100, "fuel 255 and a load 5100");
            expect(f.Start(plane) && f.Flying(), "it takes off");
            const int day = t.cal.day;
            f.Key('S');
            for (int k = 0; k < 200; ++k)
                tsim.TimerTick();
            expect(t.cal.day == day && Flight::Load(t, plane) < 255 && Flight::Fuel(t, plane) < 255, "the clock stands still, fuel and load are used");
            f.Key('E');
            for (int k = 0; k < 3; ++k)
                tsim.TimerTick();
            int wreck = 0;
            for (const auto& c : t.world.cells)
                wreck += (c.BaseTile() >= 73 && c.BaseTile() <= 74) || (c.BaseTile() >= 113 && c.BaseTile() <= 114);
            expect(!f.Flying() && wreck == 4 && t.world.At(hx, hy).BaseTile() == 23 && !(t.PlotType(hx / 8, hy / 8) & (hx % 8 == 3 ? 0x20 : 0x40)),
                   "ended away from the airport: a crash, wreck tiles, hangar emptied and freed");
        }
    }
    if (farm == "SCEN0.SSM")
    {
        // the flight loop pass by pass against known values (2 ticks a pass): keys by pass,
        // the state after 400 passes (scroll origin, offset from the centre, heading, fuel, load, wear)
        GameState t = s;
        Simulation tsim(t);
        Flight& f = tsim.Plane();
        f.Buy();
        int plane = 0;
        for (int i = 1; i < 127 && !plane; ++i)
            if (Rd16(t.machines, static_cast<size_t>(i) * 56) == 8 && (Rd8(t.machines, static_cast<size_t>(i) * 56 + 2) & 0x20))
                plane = i;
        f.Refuel(plane);
        f.Reload(plane);
        f.Start(plane);
        const std::map<int, int> keys = { { 3, 'R' }, { 4, 'R' }, { 20, 'S' }, { 60, 'R' }, { 61, 'R' }, { 90, 'L' },
                                          { 150, 'R' }, { 151, 'R' }, { 152, 'R' }, { 200, '-' }, { 220, '+' }, { 300, 'L' } };
        for (int p = 0; p < 400 && f.Flying(); ++p)
        {
            if (keys.count(p))
                f.Key(keys.at(p));
            for (int k = 0; k < 2; ++k)
                tsim.TimerTick();
        }
        const int sx = static_cast<int>(f.ViewX() - 17), sy = static_cast<int>(f.ViewY() - 11.5f);
        const int xo = static_cast<int>(f.X()) - 17 - sx, yo = static_cast<int>(f.Y()) - 11 - sy;
        // the expected values of this run (2 ticks a pass), worked out beforehand: hangar (5,30) centres the
        // view at (0,19); after 400 passes sx 51, sy 0, offset (0,-8), heading 2, fuel 232, load 183, wear 23
        expect(sx == 51 && sy == 0 && xo == 0 && yo == -8 && f.Heading() == 2 && Flight::Fuel(t, plane) == 232 &&
                   Flight::Load(t, plane) == 183 && Flight::Wear(t, plane) == 23,
               "the flight follows the original's loop pass by pass (400 passes, " + std::to_string(sx) + "," +
                   std::to_string(sy) + " " + std::to_string(xo) + "," + std::to_string(yo) + ")");
    }
    {
        // liquidation: stacks, animals, owned machines, then land, until the debt is covered
        GameState t = s;
        Simulation tsim(t);
        t.cash = 0;
        const bool paid = tsim.Liquidate(20000, true);
        expect(paid && !tsim.Bankrupt(), "a debt of 20000 is paid by forced sales (cash left " + std::to_string(t.cash) + ")");
        GameState u = s;
        Simulation usim(u);
        u.cash = 0;
        usim.Liquidate(99000000, true);
        expect(usim.Bankrupt(), "an impossible debt ends the game");
    }
    {
        // land: buy a free plot, then sell it again
        GameState t = s;
        Simulation tsim(t);
        int pc = -1, pr = -1;
        for (int c = 1; c < 11 && pc < 0; ++c)
            for (int r = 1; r < 11 && pc < 0; ++r)
                if (t.PlotOwner(c, r) == 0)
                {
                    pc = c;
                    pr = r;
                }
        std::string why;
        const int64_t c0 = t.cash, value = tsim.PlotLandValue(pc, pr);
        const bool bought = pc >= 0 && tsim.BuyPlot(pc, pr, why);
        expect(bought && t.PlotOwner(pc, pr) == 1 && t.cash == c0 - value, "a free plot is bought at its land value " + why);
        const bool sold = bought && tsim.SellPlot(pc, pr, why);
        expect(sold && t.PlotOwner(pc, pr) == 0 && t.cash > c0 - value, "and sold again " + why);
    }
    {
        GameState t = s;
        Simulation tsim(t);
        int town0 = 0, town1 = 0;
        for (int c = 0; c < t.Plots(); ++c)
            for (int r = 0; r < t.Plots(); ++r)
                town0 += t.PlotOwner(c, r) == 2;
        const bool built = tsim.TownBuildPlot(5);
        for (int c = 0; c < t.Plots(); ++c)
            for (int r = 0; r < t.Plots(); ++r)
                town1 += t.PlotOwner(c, r) == 2;
        expect(built && town1 == town0 + 1, "the town vote builds a plot beside the town (" + std::to_string(town0) + " -> " + std::to_string(town1) + ")");
    }
    {
        // a voted airport (byte 0xA5, dirt roads) is the worked plot: its first growth week takes it to level 11 (12 when the 8 days
        // span two weeks) and the original's redraw paves its roads (cmp of the whole byte with 5; user's new game 2026-11-11); with the hangar
        // bit 0x40 (0xE5 = level 14) it stays as it is
        auto roads = [](const GameState& g, int c, int r, int& dirt, int& paved) {
            dirt = paved = 0;
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j)
                {
                    const int b = g.world.At(c * 8 + i, r * 8 + j).BaseTile();
                    dirt += b >= 161 && b <= 171;
                    paved += b >= 150 && b <= 160;
                }
        };
        Megadata mega;
        std::string merr;
        const bool megaOk = mega.Open(gameDir, merr);
        expect(megaOk, "the game's archive opens for the airport check " + merr);
        for (const int hangar : { 0, 0x40 })
        {
            if (!megaOk)
                break;
            GameState t = s;
            t.scalars["town_pot"] = 1000;
            Simulation tsim(t);
            tsim.SetTownLayout(mega.Read("townly0.cmp"));
            const bool built = tsim.TownBuildPlot(5);
            const int c = static_cast<int>(t.scalars["town_work_x"]), r = static_cast<int>(t.scalars["town_work_y"]);
            const int placed = t.PlotType(c, r);
            t.SetPlotType(c, r, placed | hangar);
            int dirt0, paved0, dirt1, paved1;
            roads(t, c, r, dirt0, paved0);
            for (int d = 0; d < 8; ++d)
                tsim.DayTick();
            roads(t, c, r, dirt1, paved1);
            const std::string got = "byte " + std::to_string(placed) + " -> " + std::to_string(t.PlotType(c, r)) + ", dirt " +
                                    std::to_string(dirt0) + " -> " + std::to_string(dirt1) + ", paved " + std::to_string(paved0) +
                                    " -> " + std::to_string(paved1);
            if (!hangar)
                expect(built && placed == 0xA5 && dirt0 > 0 && (t.PlotType(c, r) == 0xB5 || t.PlotType(c, r) == 0xC5) && dirt1 == 0 && paved1 >= dirt0,
                       "a voted airport's first growth week paves its roads (" + got + ")");
            else
                expect(built && t.PlotType(c, r) == 0xE5 && dirt1 == dirt0,
                       "an airport with hangar bit 0x40 (level 14) keeps its dirt roads (" + got + ")");
        }
    }
    {
        // an unanswered vote: the town picks a type 1-14 itself; the plot it works on grows one level a week
        GameState t = s;
        t.scalars["town_pot"] = 1000;
        Simulation tsim(t);
        const bool offered = tsim.TownOffer();
        tsim.TownVoteTimeout();
        const int c = static_cast<int>(t.scalars["town_work_x"]), r = static_cast<int>(t.scalars["town_work_y"]);
        const int type = t.PlotType(c, r) & 15, level0 = t.PlotType(c, r) >> 4;
        expect(offered && t.PlotOwner(c, r) == 2 && (t.PlotType(c, r) == 0xFF || (type >= 1 && type <= 14)),
               "the vote's timeout builds a random town plot (type " + std::to_string(type) + ")");
        int ticks = 0;
        while (t.cal.day != 0 && ticks < 8)
            tsim.DayTick(), ++ticks;
        const int level1 = t.PlotType(c, r) >> 4;
        for (int d = 0; d < 7; ++d)
            tsim.DayTick();
        const int level2 = t.PlotType(c, r) >> 4;
        expect(type == 5 || type == 4 || (level2 == level1 + 1 && level1 <= level0 + 1),
               "town growth: one level a week (" + std::to_string(level0) + " " + std::to_string(level1) + " " + std::to_string(level2) + ")");
    }
    // a road next to the field
    const int64_t cash = s.cash;
    const bool road = ApplyTool(s, sim, Tool::PavedRoad, fx - 1, fy, 0).ok;
    expect(!road || s.cash == cash - 30, "paved road costs 30 per cell");
    if (road)
    {
        expect(ApplyTool(s, sim, Tool::Bulldoze, fx - 1, fy, 0).ok, "bulldozing the road");
        expect(s.world.At(fx - 1, fy).BaseTile() >= 15 && s.world.At(fx - 1, fy).BaseTile() <= 30, "bulldozed cell is ground again");
    }
    {
        // AutoDoze (seen live): a rock is cleared for 25 before the road goes down; a fence stays and the road
        // is refused
        GameState t = s;
        Simulation tsim(t);
        t.scalars["opt_autodoze"] = 1;
        int rx = -1, ry = -1;
        for (int x = 1; x < t.world.size - 1 && rx < 0; ++x)
            for (int y = 1; y < t.world.size - 1 && rx < 0; ++y)
                if (t.PlotOwner(x / 8, y / 8) == 1 && !t.world.At(x, y).HasObject() && !tsim.FieldIndexAt(x, y) &&
                    t.world.At(x, y).BaseTile() >= 15 && t.world.At(x, y).BaseTile() <= 30)
                {
                    rx = x;
                    ry = y;
                }
        if (rx >= 0)
        {
            MapCell& c = t.world.At(rx, ry);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 536);
            const int64_t before = t.cash;
            const bool ok = ApplyTool(t, tsim, Tool::DirtRoad, rx, ry, 0).ok;
            expect(ok && before - t.cash == 20 + 25, "AutoDoze clears a rock for 25 before a dirt road (paid " + std::to_string(before - t.cash) + ")");
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 178);
            expect(!ApplyTool(t, tsim, Tool::DirtRoad, rx, ry, 0).ok && c.BaseTile() == 178, "AutoDoze leaves a fence: the road is refused");
            // gates: only on a straight fence piece; vertical 177 -> 188, horizontal 178 -> 190
            const bool hGate = ApplyTool(t, tsim, Tool::Gate, rx, ry, 0).ok && c.BaseTile() == 190;
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 177);
            const bool vGate = ApplyTool(t, tsim, Tool::Gate, rx, ry, 0).ok && c.BaseTile() == 188;
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 179);
            const bool corner = !ApplyTool(t, tsim, Tool::Gate, rx, ry, 0).ok && c.BaseTile() == 179;
            expect(hGate && vGate && corner, "a gate in a horizontal fence is 190, in a vertical one 188, none on a fence corner");
            // every rock picture (535-538, family 145) is bulldozed for 25 (user finding F26: the small one, 538, was refused)
            // a broken fence (192, 193), the tiles 73 / 113 and a road bridge (172) also go for 25 (seen live, user finding F33)
            for (int tile : { 192, 193, 73, 113, 172 })
            {
                c.base = static_cast<uint16_t>((c.base & 0xC800) | tile);
                const int64_t cash0 = t.cash;
                expect(ApplyTool(t, tsim, Tool::Bulldoze, rx, ry, 0).ok && cash0 - t.cash == 25 && c.BaseTile() <= 30,
                       "bulldozing tile " + std::to_string(tile));
            }
            for (int rock = 535; rock <= 538; ++rock)
            {
                c.base = static_cast<uint16_t>((c.base & 0xF800) | rock);
                const int64_t cash0 = t.cash;
                expect(ApplyTool(t, tsim, Tool::Bulldoze, rx, ry, 0).ok && cash0 - t.cash == 25 && c.BaseTile() <= 30,
                       "bulldozing rock " + std::to_string(rock));
            }
        }
    }
    {
        // road bridges (seen live 2026-11-10, user report "bridges are not working"): a road laid on a ditch or on water
        // becomes the road bridge 172 for 40 (the same picture whichever way it runs, the water flags kept), a ditch dug
        // across a road too; the road and ditch pieces around it connect to it; a paved road on a dirt road is refused
        GameState t = s;
        Simulation tsim(t);
        int bx = -1, by = -1;
        auto freeGround = [&](int x, int y) {
            const MapCell& c = t.world.At(x, y);
            return t.PlotOwner(x / 8, y / 8) == 1 && !c.HasObject() && !tsim.FieldIndexAt(x, y) && c.BaseTile() >= 15 &&
                   c.BaseTile() <= 30 && !(c.base & 0x2000);
        };
        for (int x = 2; x < t.world.size - 2 && bx < 0; ++x)
            for (int y = 2; y < t.world.size - 2 && bx < 0; ++y)
            {
                bool ok = true;
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dy = -2; dy <= 2; ++dy)
                        ok = ok && freeGround(x + dx, y + dy);
                if (ok)
                {
                    bx = x;
                    by = y;
                }
            }
        if (bx >= 0) // (a farm with no free 5 x 5 block of ground is skipped)
        {
            auto tile = [&](int x, int y) { return t.world.At(x, y).BaseTile(); };
            for (int dx = -1; dx <= 1; ++dx)
                ApplyTool(t, tsim, Tool::Ditch, bx + dx, by, 0);
            const int64_t cash0 = t.cash;
            const bool bridged = ApplyTool(t, tsim, Tool::DirtRoad, bx, by, 0).ok;
            const int64_t paid = cash0 - t.cash;
            ApplyTool(t, tsim, Tool::DirtRoad, bx, by - 1, 0);
            ApplyTool(t, tsim, Tool::DirtRoad, bx, by + 1, 0);
            expect(bridged && paid == 40 && tile(bx, by) == 172, "a dirt road on a ditch is the bridge 172 for 40 (tile " +
                   std::to_string(tile(bx, by)) + ", paid " + std::to_string(paid) + ")");
            expect(tile(bx, by - 1) == 161 && tile(bx, by + 1) == 161 && tile(bx - 1, by) == 88 && tile(bx + 1, by) == 88,
                   "the road (161) and the ditch (88) connect to the bridge");
            const int64_t cash1 = t.cash;
            expect(!ApplyTool(t, tsim, Tool::PavedRoad, bx, by - 1, 0).ok && tile(bx, by - 1) == 161 && t.cash == cash1,
                   "a paved road on a dirt road is refused");
            const bool dug = ApplyTool(t, tsim, Tool::Ditch, bx, by + 1, 0).ok;
            expect(dug && tile(bx, by + 1) == 172 && cash1 - t.cash == 40, "a ditch across a road is a bridge for 40");
            MapCell& w = t.world.At(bx + 2, by + 2);
            w.base = static_cast<uint16_t>(0x3000 | 55); // a river cell
            expect(ApplyTool(t, tsim, Tool::PavedRoad, bx + 2, by + 2, 0).ok && w.base == (0x3000 | 172),
                   "a road on water is a bridge that keeps the water flags");
            // bulldozed, a bridge on water is water again (the original)
            t.cash = 100000;
            expect(ApplyTool(t, tsim, Tool::Bulldoze, bx + 2, by + 2, 0).ok && w.base == (0x3000 | 55),
                   "a bulldozed bridge on water is water again");
        }
        {
            // a square of bridges on the farm's river, all bulldozed again: every cell water again (user report
            // 2026-11-11 "try doing multiple bridges like a big square and deleting all")
            GameState t = s;
            Simulation tsim(t);
            t.cash = 1000000;
            auto river = [&](int x, int y) {
                if (!t.world.In(x, y))
                    return false;
                const MapCell& c = t.world.At(x, y);
                return (c.base & 0x3000) == 0x3000 && c.BaseTile() >= 39 && c.BaseTile() <= 69 && !c.HasObject();
            };
            int qx = -1, qy = -1;
            for (int x = 0; x + 2 < t.world.size && qx < 0; ++x)
                for (int y = 0; y + 2 < t.world.size && qx < 0; ++y)
                {
                    bool all = true;
                    for (int i = 0; i < 3 && all; ++i)
                        for (int j = 0; j < 3 && all; ++j)
                            all = river(x + i, y + j);
                    if (all)
                        qx = x, qy = y;
                }
            if (qx >= 0 && [&] { GameState u = t; Simulation us(u); for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (!ApplyTool(u, us, Tool::DirtRoad, qx + i, qy + j, 0).ok) return false; return true; }())
            {
                int built = 0, dozed = 0, water = 0;
                std::string left;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        built += ApplyTool(t, tsim, Tool::DirtRoad, qx + i, qy + j, 0).ok ? 1 : 0;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        dozed += ApplyTool(t, tsim, Tool::Bulldoze, qx + i, qy + j, 0).ok ? 1 : 0;
                for (int d = 0; d < 60; ++d) // and two months on
                    tsim.DayTick();
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                    {
                        const MapCell& c = t.world.At(qx + i, qy + j);
                        if ((c.base & 0x3000) == 0x3000 && c.BaseTile() >= 39 && c.BaseTile() <= 69)
                            ++water;
                        else
                            left += " " + std::to_string(c.BaseTile()) + "/" + std::to_string(c.base & 0xF800);
                    }
                expect(built == 9 && dozed == 9 && water == 9, "a 3 x 3 square of bridges on the river, bulldozed, is river again (built " +
                       std::to_string(built) + ", bulldozed " + std::to_string(dozed) + ", water " + std::to_string(water) + left + ") at " + std::to_string(qx) + "," + std::to_string(qy));
            }
        }
    }
    {
        // the Move tool on an animal (engine addition, user finding Q20): picked, dropped on a free neighbouring cell
        GameState t = s;
        Simulation tsim(t);
        bool tried = false;
        for (int x = 1; x < t.world.size - 1 && !tried; ++x)
            for (int y = 1; y < t.world.size - 1 && !tried; ++y)
            {
                const int a = tsim.Herds().AnimalAt(x, y);
                if (!a || t.PlotOwner(x / 8, y / 8) != 1)
                    continue;
                for (int d = 0; d < 4 && !tried; ++d)
                {
                    const int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                    const MapCell& n = t.world.At(nx, ny);
                    if (t.PlotOwner(nx / 8, ny / 8) != 1 || n.HasObject() || n.BaseTile() < 15 || n.BaseTile() > 30)
                        continue;
                    tried = true;
                    int kind = 0, index = 0;
                    std::string why;
                    const bool picked = PickObject(t, tsim, x, y, kind, index, why) && kind == kMoveAnimal && index == a;
                    const bool dropped = picked && DropObject(t, tsim, kind, index, x, y, nx, ny).ok;
                    expect(dropped && tsim.Herds().AnimalAt(nx, ny) == a && !t.world.At(x, y).HasObject() && t.world.At(nx, ny).HasObject(),
                           "the Move tool moves an animal one cell");
                }
            }
    }
    {
        // BUY and SELL: a tractor and a cow on another empty plot (test setup), then sold again
        int bx = -1, by = -1;
        for (int c = 1; c < 11 && bx < 0; ++c)
            for (int r = 1; r < 11 && bx < 0; ++r)
            {
                bool empty = s.PlotOwner(c, r) == 0;
                for (int x = c * 8; x < c * 8 + 8 && empty; ++x)
                    for (int y = r * 8; y < r * 8 + 8 && empty; ++y)
                        empty = !s.world.At(x, y).HasObject() && s.world.At(x, y).BaseTile() >= 15 && s.world.At(x, y).BaseTile() <= 30;
                if (empty)
                {
                    s.SetPlotOwner(c, r, 1);
                    bx = c * 8 + 2;
                    by = r * 8 + 2;
                }
            }
        if (bx >= 0)
        {
            const int64_t c0 = s.cash;
            const EditResult t = ShopBuy(s, sim, 0, bx, by);
            const EditResult c = ShopBuy(s, sim, 46, bx + 3, by);
            const EditResult b = ShopBuy(s, sim, 12, bx + 5, by + 3);
            expect(t.ok && c.ok && b.ok && s.cash == c0 - 400 - 480 - 800, "BUY places a tractor, a cow and a silo " + t.message + c.message + b.message);
            {
                // seed: the bag goes into a building's free slot (bare ground refused), planting uses it up and frees
                // the slot (seen live on MVB)
                GameState tsd = s;
                Simulation ssd(tsd);
                const int st = ssd.StructureAt(bx + 5, by + 3);
                const size_t sb = static_cast<size_t>(st) * 70;
                const int64_t cs = tsd.cash;
                const int own0 = tsd.CatalogOwn(66), contracts0 = tsd.Contracts(0);
                const EditResult g = ShopBuy(tsd, ssd, 21, bx + 1, by + 5);
                const EditResult in = ShopBuy(tsd, ssd, 21, bx + 5, by + 3);
                const int bag = st ? Rd8(tsd.structures, sb + 0x3A) : 0;
                const size_t ib = static_cast<size_t>(bag) * 12;
                expect(!g.ok && in.ok && bag && Rd16(tsd.items, ib) == 0xC0 && Rd16(tsd.items, ib + 2) == 10 && Rd16(tsd.structures, sb + 0x22) == 0xC0 &&
                           Rd16(tsd.structures, sb + 0x0A) == 0x340 && tsd.cash == cs - ShopPrice(tsd, 21),
                       "BUY puts a corn seed bag into the silo, not on bare ground " + g.message + in.message);
                expect(tsd.CatalogOwn(66) == own0 + 1 && tsd.Contracts(0) == contracts0 && ShopOwnRecord(21) == 66,
                       "a bought seed bag counts on the seed record's Own, not on the corn contracts");
                int fi = 0;
                for (int f = 1; f < 98 && !fi; ++f)
                    if (tsd.FieldAt(f).InUse())
                        fi = f;
                if (bag && fi && tsd.crops[0].loaded)
                {
                    tsd.scalars["opt_autobuy"] = 0;
                    Field f = tsd.FieldAt(fi);
                    std::string why;
                    const bool planted = ssd.StartPlanting(f, 0, why);
                    expect(planted && !(Rd8(tsd.items, ib + 10) & 0x20) && Rd16(tsd.structures, sb + 0x22) == 0xFFFF && Rd16(tsd.structures, sb + 0x0A) == 0xD9,
                           "planting corn uses the bag up and frees its slot " + why);
                    expect(tsd.CatalogOwn(66) == own0, "planting lowers the seed Own count again");
                    // with the Own count at 0 (an inconsistent save) the dispose routine keeps the record: the slot is
                    // freed, the bag stays in use (seen live on OWN1)
                    GameState tz = s;
                    Simulation sz(tz);
                    ShopBuy(tz, sz, 21, bx + 5, by + 3);
                    tz.SetCatalogOwn(66, 0);
                    tz.scalars["opt_autobuy"] = 0;
                    Field fz = tz.FieldAt(fi);
                    const bool pz = sz.StartPlanting(fz, 0, why);
                    expect(pz && (Rd8(tz.items, ib + 10) & 0x20) && Rd16(tz.structures, sb + 0x22) == 0xFFFF,
                           "with seed Own 0 planting frees the slot but keeps the bag record " + why);
                }
            }
            {
                // a barrel bought, then moved with the Move tool: it is there at once (seen live on MRKC)
                GameState t3 = s;
                Simulation s3(t3);
                const EditResult br = ShopBuy(t3, s3, 82, bx + 1, by + 5);
                int kind = 0, index = 0;
                std::string why;
                const bool picked = br.ok && PickObject(t3, s3, bx + 1, by + 5, kind, index, why) && kind == kMoveBarrel;
                const EditResult mv = picked ? DropObject(t3, s3, kind, index, bx + 1, by + 5, bx + 4, by + 4) : EditResult{};
                const int ox = Rd8(t3.objects20, static_cast<size_t>(index) * 20 + 5), oy = Rd8(t3.objects20, static_cast<size_t>(index) * 20 + 6);
                expect(mv.ok && ox == bx + 4 && oy == by + 4 && t3.world.At(ox, oy).HasObject() && (t3.world.At(ox, oy).overlay & 0x7FF) == 915 &&
                           !t3.world.At(bx + 1, by + 5).HasObject(),
                       "the Move tool moves a barrel at once to " + std::to_string(ox) + "," + std::to_string(oy) + " " + why + mv.message);
            }
            GameState t2 = s;
            Simulation s2(t2);
            const int64_t before = t2.cash;
            const EditResult dz = ApplyTool(t2, s2, Tool::Bulldoze, bx + 5, by + 3, 0);
            expect(dz.ok && t2.cash == before + 800 && s2.StructureAt(bx + 5, by + 3) == 0, "bulldozing the silo sells it " + dz.message);
            {
                // a content cow facing a barn walks into it (a slot of the barn, flag 4) and later out west of it;
                // facing the silo it is held back and turns to a random walk
                auto cowAt = [](GameState& g, int x, int y) {
                    for (int i = 1; i < 256; ++i)
                    {
                        const size_t b = static_cast<size_t>(i) * 32;
                        if ((Rd8(g.animals, b + 2) & 0x20) && Rd16(g.animals, b + 6) == 0xA1 && Rd8(g.animals, b) == x && Rd8(g.animals, b + 1) == y)
                            return i;
                    }
                    return 0;
                };
                auto aim = [](GameState& g, int i, int heading) {
                    // the only animal, so that the cows' centre stays next to it (horses' sums would skew it)
                    for (int k = 1; k < 256; ++k)
                        if (k != i)
                            Wr8(g.animals, static_cast<size_t>(k) * 32 + 2, 0);
                    const size_t b = static_cast<size_t>(i) * 32;
                    Wr8(g.animals, b + 4, heading);
                    Wr8(g.animals, b + 5, 0);
                    Wr8(g.animals, b + 0x0D, 0);
                    Wr8(g.animals, b + 0x0E, 0);
                };
                auto placeCow = [](GameState& g, Simulation& sm, int i, int x, int y) {
                    // set the cow down by hand (the Move tool does not take animals)
                    const size_t b = static_cast<size_t>(i) * 32;
                    sm.ClearAnimalCell(Rd8(g.animals, b), Rd8(g.animals, b + 1));
                    Wr8(g.animals, b, x);
                    Wr8(g.animals, b + 1, y);
                    MapCell& c = g.world.At(x, y);
                    c.base |= 0x800;
                    c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | Animals::TileFor((Rd16(g.animals, b + 6) - 0xA0) & 3, Rd8(g.animals, b + 4), 0));
                    return true;
                };
                GameState t4 = s;
                Simulation s4(t4);
                const EditResult barn = ShopBuy(t4, s4, 16, bx, by + 3);
                const int cow = cowAt(t4, bx + 3, by);
                int kind = 0, index = 0;
                std::string why;
                const bool moved = cow && placeCow(t4, s4, cow, bx + 3, by + 4);
                const size_t cb = static_cast<size_t>(cow) * 32;
                if (moved)
                    aim(t4, cow, 6);
                s4.Herds().Tick(1);
                const int st = s4.StructureAt(bx, by + 3);
                const size_t sb = static_cast<size_t>(st) * 70;
                const bool inside = (Rd8(t4.animals, cb + 2) & 4) && Rd16(t4.animals, cb + 0x10) == st && Rd16(t4.structures, sb + 0x22) == 0xA1 &&
                                    Rd8(t4.structures, sb + 0x3A) == cow && Rd8(t4.animals, cb) == bx && Rd8(t4.animals, cb + 1) == by + 3;
                expect(barn.ok && moved && inside, "a cow walks into a barn " + barn.message + why);
                int passes = 0;
                while (inside && (Rd8(t4.animals, cb + 2) & 4) && passes < 400)
                {
                    s4.Herds().Tick(1);
                    passes++;
                }
                expect(inside && !(Rd8(t4.animals, cb + 2) & 4) && Rd8(t4.animals, cb) == bx - 1 && Rd8(t4.animals, cb + 1) == by + 3 &&
                           Rd16(t4.structures, sb + 0x22) == 0xFFFF && Rd16(t4.structures, sb + 0x0A) == 0xD9,
                       "and walks out west of it after " + std::to_string(passes) + " passes");
                {
                    // herd centres as the original computes them (slots shifted by one species): a horse at
                    // x 21, one cow and three sheep -> the cows divide by 1 + 21, the sheep read the empty slot 3
                    GameState t6 = s;
                    Simulation s6(t6);
                    for (int k = 1; k < 256; ++k)
                        Wr8(t6.animals, static_cast<size_t>(k) * 32 + 2, 0);
                    auto put = [&](int k, int sp, int x, int y) {
                        const size_t b = static_cast<size_t>(k) * 32;
                        Wr8(t6.animals, b, x);
                        Wr8(t6.animals, b + 1, y);
                        Wr8(t6.animals, b + 2, 0x20);
                        Wr16(t6.animals, b + 6, 0xA0 + sp);
                    };
                    put(1, 0, 21, 36);
                    put(2, 1, 44, 36);
                    put(3, 3, 20, 30);
                    put(4, 3, 22, 32);
                    put(5, 3, 24, 34);
                    s6.Herds().Tick(1000);
                    auto& h = s6.Herds();
                    // sheep: slot 3, no sums, count byte = high byte of the horses' y sum = 0 -> (0, 0); cow: 44/22, 36/22 (+0..3);
                    // horse: slot 2 = the sheep's sums 66, 96 over 3 sheep + the horse's y 36 = 39 (+0..3)
                    expect(h.CentreX(3) == 0 && h.CentreY(3) == 0 && h.CentreX(1) >= 2 && h.CentreX(1) <= 5 && h.CentreY(1) >= 1 && h.CentreY(1) <= 4 &&
                               h.CentreX(0) >= 1 && h.CentreX(0) <= 4 && h.CentreY(0) >= 2 && h.CentreY(0) <= 5,
                           "herd centres keep the original's slot shift (sheep " + std::to_string(h.CentreX(3)) + "," + std::to_string(h.CentreY(3)) +
                               ", cow " + std::to_string(h.CentreX(1)) + "," + std::to_string(h.CentreY(1)) + ", horse " + std::to_string(h.CentreX(0)) + ")");
                }
                GameState t5 = s;
                Simulation s5(t5);
                const bool moved5 = cow && placeCow(t5, s5, cow, bx + 4, by + 3);
                if (moved5)
                    aim(t5, cow, 2);
                s5.Herds().Tick(1);
                expect(moved5 && Rd8(t5.animals, cb) == bx + 4 && Rd8(t5.animals, cb + 1) == by + 3 && Rd8(t5.animals, cb + 5) == 8,
                       "a silo holds a cow back and sends it on a random walk");
            }
        }
        {
            // traffic: along a lone paved road east, a cow on the road kicks, a car with no
            // road around leaves, and one starts every week at the town plot's corner
            GameState t6 = s;
            Simulation s6(t6);
            for (int x = 6; x <= 22; ++x)
                for (int y = 7; y <= 11; ++y)
                {
                    MapCell& c = t6.world.At(x, y);
                    c.base = static_cast<uint16_t>(y == 10 && x >= 10 && x <= 20 ? 150 : 15);
                    c.overlay = 0;
                }
            std::fill(t6.traffic.begin(), t6.traffic.end(), 0);
            t6.carCount = 0;
            s6.Cars().Add(10, 10, 2);
            bool east = true;
            for (int k = 1; k <= 4; ++k)
            {
                s6.Cars().Tick(1);
                east = east && Rd16(t6.traffic, 2) == 10 + k && Rd16(t6.traffic, 4) == 10 && t6.world.At(10 + k, 10).HasObject() &&
                       (t6.world.At(10 + k, 10).overlay & 0x7FF) == (0xE5 + Rd8(t6.traffic, 7)) * 4 + 1 && !t6.world.At(9 + k, 10).HasObject();
            }
            expect(east, "a car keeps to the road and is drawn on its cell");
            const bool placed = s6.Herds().Place(15, 10, 1);
            int cow = 0;
            for (int i = 1; i < 256 && placed; ++i)
                if ((Rd8(t6.animals, static_cast<size_t>(i) * 32 + 2) & 0x20) && Rd8(t6.animals, static_cast<size_t>(i) * 32) == 15 &&
                    Rd8(t6.animals, static_cast<size_t>(i) * 32 + 1) == 10)
                    cow = i;
            s6.Cars().Tick(1);
            const size_t cb = static_cast<size_t>(cow) * 32;
            expect(cow && (Rd8(t6.animals, cb + 2) & 8) && (t6.world.At(15, 10).overlay & 0x7FF) == Animals::kKickPicture + 1 &&
                       !(Rd8(t6.traffic, 0) & 2) && Rd16(t6.traffic, 2) == 15,
                   "a car running into a cow makes it kick and drives on unseen");
            for (int k = 0; k < 700; ++k)
                s6.Herds().Tick(1);
            expect(cow && !(Rd8(t6.animals, cb + 2) & 8), "the cow stops kicking after 640 ticks");
            std::fill(t6.traffic.begin(), t6.traffic.end(), 0);
            t6.carCount = 0;
            t6.world.At(8, 9).base = 150;
            s6.Cars().Add(8, 9, 0);
            s6.Cars().Tick(1);
            expect(t6.carCount == 0 && !(Rd8(t6.traffic, 0) & 1), "a car with no road to go on leaves");
            {
                // a machine does not wait for an animal in its way: it steps onto the cell, the occupancy
                // test kicks the animal (a female loses her mark) and the machine is not drawn there
                for (int k = 1; k < 256; ++k)
                    Wr8(t6.animals, static_cast<size_t>(k) * 32 + 2, 0);
                std::fill(t6.machines.begin(), t6.machines.end(), 0);
                // the row from the machine to the cow is plain ground (a random event earlier in the test could have
                // left something on it, so the cow sometimes could not be placed)
                for (int x = 10; x <= 14; ++x)
                {
                    t6.world.At(x, 10).base = 20;
                    t6.world.At(x, 10).overlay = 0;
                }
                const bool cowIn = s6.Herds().Place(13, 10, 1);
                int ewe = 0;
                for (int i = 1; i < 256 && cowIn; ++i)
                    if (Rd8(t6.animals, static_cast<size_t>(i) * 32 + 2) & 0x20)
                        ewe = i;
                const size_t eb = static_cast<size_t>(ewe) * 32;
                Wr8(t6.animals, eb + 2, Rd8(t6.animals, eb + 2) | 2);
                Machine m(t6.machines, 1);
                Wr16(t6.machines, 56, 0); // a tractor
                m.SetPos(10, 10);
                m.SetTarget(20, 10);
                m.SetHeading(2);
                m.SetFlags(0x22);
                for (int k = 0; k < 40 && m.X() < 13; ++k)
                    s6.Fleet().Tick(false);
                expect(ewe && m.X() == 13 && m.Y() == 10 && (Rd8(t6.animals, eb + 2) & 8) && !(Rd8(t6.animals, eb + 2) & 2) && !(m.Flags() & 0x80),
                       "a driving machine runs into a cow: it kicks, loses her female mark, the machine is not drawn");
            }
            const int days = 7;
            for (int d = 0; d < days; ++d)
                s6.DayTick();
            int fresh = 0;
            for (int i = 0; i < 16; ++i)
                fresh += Rd8(t6.traffic, static_cast<size_t>(i) * 8) & 1;
            expect(t6.carCount >= 0 && fresh == t6.carCount && fresh <= 1, "a week brings at most one car, counted (" + std::to_string(fresh) + ")");
        }
        auto machines = SaleList(s, sim, 1);
        const int64_t c1 = s.cash;
        expect(!machines.empty() && ShopSell(s, sim, machines.back()).ok && s.cash > c1, "SELL a machine pays its value");
        if (bx >= 0)
        {
            int kind = 0, index = 0;
            std::string why;
            ExamineCard card;
            const bool shown = ExamineCardAt(s, bx + 3, by, card);
            expect(shown && card.kind == ExamineCard::kAnimal && card.picture == 32,
                   "Examine shows the cow's card (picture " + std::to_string(card.picture) + ")");
            expect(TownBuildingAt(320) == 1 && TownBuildingAt(362) == 1 && TownBuildingAt(400) == 1 && TownBuildingAt(330) == 4 &&
                       TownBuildingAt(332) == 5 && TownBuildingAt(335) == 6 && TownBuildingAt(240) == 0 && TownBuildingAt(280) == 0 &&
                       TownBuildingAt(309) == 0 && TownBuildingAt(343) == 0,
                   "town building tiles: market 320/362/400, buy 330, expert 332, no window from 335, 240, 280, 309");
            expect(PickObject(s, sim, bx + 3, by, kind, index, why) && kind == kMoveAnimal, "the Move tool takes a cow (an engine addition; the original does not, seen live)");
        }
        {
            // the Move tool and a machine parked in a building: refused while the building is closed, taken
            // out when its inside is shown, and it drives to the chosen cell
            GameState tm = s;
            Simulation smv(tm);
            int stored = 0;
            for (int i = 1; i < 127 && !stored; ++i)
                if (Machine m(tm.machines, i); m.InUse() && (m.Flags() & 0x04))
                    stored = i;
            if (stored)
            {
                Machine m(tm.machines, stored);
                const int mx = m.X(), my = m.Y();
                int kind = 0, index = 0, tx = -1, ty = -1;
                std::string why;
                const bool closed = !PickObject(tm, smv, mx, my, kind, index, why);
                smv.ToggleInside(mx, my);
                const bool picked = PickObject(tm, smv, mx, my, kind, index, why) && kind == 1 && index == stored;
                for (int d = 3; d < 12 && tx < 0; ++d)
                    for (int k = -d; k <= d && tx < 0; ++k)
                    {
                        const int x = mx + k, y = my + d;
                        if (x < 0 || y < 0 || x >= tm.world.size || y >= tm.world.size || tm.PlotOwner(x / 8, y / 8) != 1)
                            continue;
                        const MapCell& c = tm.world.At(x, y);
                        if (!c.HasObject() && ((c.BaseTile() >= 15 && c.BaseTile() <= 30) || (c.BaseTile() >= 150 && c.BaseTile() <= 171)))
                            tx = x, ty = y;
                    }
                const bool dropped = picked && tx >= 0 && DropObject(tm, smv, kind, index, mx, my, tx, ty).ok;
                const bool driving = dropped && (m.Flags() & 0x02) && !(m.Flags() & 0x04);
                int ticks = 0;
                while (driving && (m.Flags() & 0x02) && ticks < 4000)
                {
                    smv.Fleet().Tick(false);
                    ticks++;
                }
                expect(closed && picked && driving && m.X() == tx && m.Y() == ty,
                       "the Move tool takes a machine out of an open building and it drives there (" + std::to_string(ticks) + " ticks) " + why);
            }
            else
                expect(true, "(no machine parked in a building on this farm: Move-from-building check skipped)");
        }
        {
            // the Move tool and a stored crop stack: picked only from an open silo, it goes into
            // another silo's free slot, and a drop on open ground is refused
            GameState tq = s;
            Simulation sq(tq);
            int from = 0, to = 0;
            for (int i = 1; i < 127; ++i)
            {
                const size_t b = static_cast<size_t>(i) * 70;
                const int cat = Rd16(tq.structures, b);
                if (!(Rd8(tq.structures, b + 2) & 0x20) || (cat != 0x40 && cat != 0x41))
                    continue;
                // two empty silos with no machine on their first cell (one standing there would be picked first)
                if (Rd16(tq.structures, b + 0x22) != 0xFFFF || sq.Fleet().MachineAt(Rd8(tq.structures, b + 4), Rd8(tq.structures, b + 5)))
                    continue;
                if (!from)
                    from = i;
                else if (!to)
                    to = i;
            }
            int item = 0;
            for (int i = 1; i < 512 && !item && from && to; ++i)
                if (!(Rd8(tq.items, static_cast<size_t>(i) * 12 + 10) & 0x20))
                    item = i;
            if (item)
            {
                // a corn stack stored in the first silo, as a harvest leaves it
                const size_t ib = static_cast<size_t>(item) * 12, sb0 = static_cast<size_t>(from) * 70;
                for (size_t o = 0; o < 12; ++o)
                    tq.items[ib + o] = 0;
                Wr16(tq.items, ib, 0xA8);
                Wr8(tq.items, ib + 8, Rd8(tq.structures, sb0 + 4));
                Wr8(tq.items, ib + 9, Rd8(tq.structures, sb0 + 5));
                Wr8(tq.items, ib + 10, 0x20);
                Wr16(tq.structures, sb0 + 0x22, 0xA8);
                Wr16(tq.structures, sb0 + 0x0A, 0x330);
                Wr8(tq.structures, sb0 + 0x3A, item);
            }
            if (item)
            {
                const size_t fb = static_cast<size_t>(from) * 70, tb = static_cast<size_t>(to) * 70;
                const int fx = Rd8(tq.structures, fb + 4), fy = Rd8(tq.structures, fb + 5);
                int kind = 0, index = 0;
                std::string why;
                const bool closed = !PickObject(tq, sq, fx, fy, kind, index, why);
                sq.ToggleInside(fx, fy);
                const bool picked = PickObject(tq, sq, fx, fy, kind, index, why) && kind == 4 && index == item;
                const int tx = Rd8(tq.structures, tb + 4), ty = Rd8(tq.structures, tb + 5);
                const bool moved = picked && DropObject(tq, sq, kind, index, fx, fy, tx, ty).ok;
                expect(closed && picked && moved && Rd16(tq.structures, fb + 0x22) == 0xFFFF && Rd16(tq.structures, fb + 0x0A) == 0xD9 &&
                           Rd8(tq.structures, tb + 0x3A) == item && Rd8(tq.items, static_cast<size_t>(item) * 12 + 8) == tx &&
                           tq.world.At(fx, fy).BaseTile() == 0xD9,
                       "the Move tool carries a crop stack from an open silo into another silo");
                // then out of that silo onto bare ground of the farm, and from there back into the first silo
                int gx = -1, gy = -1;
                for (int d = 2; d < 20 && gx < 0; ++d)
                    for (int k = -d; k <= d && gx < 0; ++k)
                        for (const auto& [x, y] : { std::pair{ tx + k, ty + d }, std::pair{ tx + k, ty - d }, std::pair{ tx + d, ty + k }, std::pair{ tx - d, ty + k } })
                            if (gx < 0 && x >= 0 && y >= 0 && x < tq.world.size && y < tq.world.size && tq.PlotOwner(x / 8, y / 8) == 1 &&
                                !tq.world.At(x, y).HasObject() && tq.world.At(x, y).BaseTile() >= 15 && tq.world.At(x, y).BaseTile() <= 30 &&
                                !sq.StructureAt(x, y))
                                gx = x, gy = y;
                if (gx >= 0)
                {
                    if (!(Rd8(tq.structures, tb + 2) & 1))
                        sq.ToggleInside(tx, ty);
                    const bool p2 = PickObject(tq, sq, tx, ty, kind, index, why) && kind == kMoveItem && index == item;
                    const bool out = p2 && DropObject(tq, sq, kind, index, tx, ty, gx, gy).ok;
                    expect(out && tq.world.At(gx, gy).BaseTile() == 0x330 && Rd8(tq.items, static_cast<size_t>(item) * 12 + 8) == gx &&
                               Rd8(tq.items, static_cast<size_t>(item) * 12 + 9) == gy && Rd16(tq.structures, tb + 0x22) == 0xFFFF &&
                               tq.world.At(tx, ty).BaseTile() == 0xD9,
                           "the Move tool puts the stack on bare ground (" + std::to_string(gx) + "," + std::to_string(gy) + ")");
                    const bool p3 = out && PickObject(tq, sq, gx, gy, kind, index, why) && kind == kMoveItem && index == item;
                    const bool back = p3 && DropObject(tq, sq, kind, index, gx, gy, fx, fy).ok;
                    expect(back && Rd16(tq.structures, fb + 0x22) == 0xA8 && Rd16(tq.structures, fb + 0x0A) == 0x330 &&
                               tq.world.At(gx, gy).BaseTile() >= 15 && tq.world.At(gx, gy).BaseTile() <= 30 && tq.world.At(fx, fy).BaseTile() == 0x330,
                           "and from the ground back into the open silo");
                }
            }
            else
                expect(true, "(no two free silos on this farm: stack move check skipped)");
        }
        {
            // the Move tool and a water trough / a feed pile on the ground (seen live on ACT1): both are there
            // at once; the trough keeps its water picture, the old cell becomes ground; the pile's picture follows its amount
            GameState tr = s;
            Simulation sr(tr);
            std::vector<std::pair<int, int>> bare;
            for (int x = 0; x < tr.world.size && bare.size() < 4; ++x)
                for (int y = 0; y < tr.world.size && bare.size() < 4; ++y)
                {
                    const MapCell& c = tr.world.At(x, y);
                    if (tr.PlotOwner(x / 8, y / 8) == 1 && !c.HasObject() && c.BaseTile() >= 15 && c.BaseTile() <= 30 && !sr.StructureAt(x, y) &&
                        !sr.Fleet().MachineAt(x, y) && !ItemAt(tr, x, y) && (bare.empty() || std::abs(bare.back().first - x) + std::abs(bare.back().second - y) > 2))
                        bare.push_back({ x, y });
                }
            if (bare.size() == 4)
            {
                tr.cash = 100000;
                const auto [ax, ay] = bare[0];
                const auto [bx2, by2] = bare[1];
                const auto [cx2, cy2] = bare[2];
                const auto [dx2, dy2] = bare[3];
                const bool placed = ApplyTool(tr, sr, Tool::Trough, ax, ay, 0).ok && ApplyTool(tr, sr, Tool::Hay, cx2, cy2, 0).ok;
                int kind = 0, index = 0;
                std::string why;
                const bool pt = placed && PickObject(tr, sr, ax, ay, kind, index, why) && kind == kMoveTrough && index;
                const bool dt = pt && DropObject(tr, sr, kind, index, ax, ay, bx2, by2).ok;
                expect(dt && tr.world.At(bx2, by2).BaseTile() == 176 && tr.world.At(ax, ay).BaseTile() >= 15 && tr.world.At(ax, ay).BaseTile() <= 30 &&
                           ItemAt(tr, bx2, by2) == index,
                       "the Move tool moves a full water trough " + why);
                const bool ph = placed && PickObject(tr, sr, cx2, cy2, kind, index, why) && kind == kMoveFeed && index;
                const bool dh = ph && DropObject(tr, sr, kind, index, cx2, cy2, dx2, dy2).ok;
                expect(dh && tr.world.At(dx2, dy2).HasObject() && (tr.world.At(dx2, dy2).overlay & 0x7FF) == 1020 && !tr.world.At(cx2, cy2).HasObject() &&
                           ItemAt(tr, dx2, dy2) == index,
                       "the Move tool moves a feed pile " + why);
                // onto a building or a busy cell the pile is refused
                bool refusedOnTrough = false;
                if (dh && PickObject(tr, sr, dx2, dy2, kind, index, why))
                    refusedOnTrough = !DropObject(tr, sr, kind, index, dx2, dy2, bx2, by2).ok;
                expect(refusedOnTrough, "a feed pile cannot go onto a trough");
            }
            else
                expect(true, "(no bare ground on this farm: trough/feed move check skipped)");
        }
        {
            GameState t = s;
            Simulation tsim(t);
            tsim.SetGameDir(gameDir);
            int freeSlot = -1;
            for (int c = 15; c >= 0 && freeSlot < 0; --c)
            {
                bool used = false;
                // the original's rule: any field in use with the crop, or a seed bag / stored harvest of it
                for (int f = 1; f < 98; ++f)
                    used |= t.FieldAt(f).InUse() && t.FieldAt(f).Crop() == c;
                for (size_t b = 12; b + 12 <= t.items.size(); b += 12)
                    used |= (Rd8(t.items, b + 10) & 0x20) && (Rd16(t.items, b) == 0xA8 + c || Rd16(t.items, b) == 0xC0 + c);
                if (!used)
                    freeSlot = c;
            }
            std::string why;
            int usedSlot = -1;
            for (int f = 1; f < 98 && usedSlot < 0; ++f)
                if (t.FieldAt(f).InUse())
                    usedSlot = t.FieldAt(f).Crop();
            std::string w1, w2;
            expect(usedSlot < 0 || !tsim.ReplaceCrop(usedSlot, "TOBACCO.CRP", w1), "Load Crop refuses a crop a field has");
            std::string slot0;
            for (int k = 0; k < 14 && t.cropNames[k]; ++k)
                slot0 += static_cast<char>(std::toupper(t.cropNames[k]));
            expect(freeSlot <= 0 || !tsim.ReplaceCrop(freeSlot, slot0, w2), "Load Crop refuses a file already loaded (any case): " + slot0);
            // a crop file no slot has loaded
            std::string fresh;
            for (const char* cand : { "TOBACCO.CRP", "SUNFLOWE.CRP", "GLADIOLU.CRP", "ORANGES.CRP", "APPLES.CRP" })
            {
                bool loaded = false;
                for (int c = 0; c < 16; ++c)
                {
                    std::string n;
                    for (int k = 0; k < 14 && t.cropNames[c * 14 + k]; ++k)
                        n += static_cast<char>(std::toupper(t.cropNames[c * 14 + k]));
                    loaded |= n == cand;
                }
                if (!loaded && fresh.empty())
                    fresh = cand;
            }
            const auto before = tsim.PriceHistory();
            const bool ok = freeSlot >= 0 && tsim.ReplaceCrop(freeSlot, fresh, why);
            expect(ok && tsim.PriceHistory()[freeSlot][29] == t.crops[freeSlot].BasePrice() &&
                       std::equal(before[freeSlot].begin(), before[freeSlot].begin() + 29, tsim.PriceHistory()[freeSlot].begin()),
                   "Load Crop sets only the newest week of the price history");
            std::string e3;
            GameState back2;
            const std::string p3 = TempPath("osf_crop.sfm");
            expect(ok && t.Save(p3, e3) && back2.Load(gameDir, p3, e3) && back2.crops[freeSlot].name == t.crops[freeSlot].name,
                   "Load Crop replaces slot " + std::to_string(freeSlot) + " and the save keeps it " + why);
        }
        {
            // planting another crop on a field repaints its sign (the original's plant step calls the
            // field painter for column 0; user save WRONGCRO, F41)
            GameState t = s;
            Simulation tsim(t);
            int field = 0;
            for (int i = 1; i < 98 && !field; ++i)
                if (t.FieldAt(i).InUse())
                    field = i;
            if (field)
            {
                Field f = t.FieldAt(field);
                const int other = ((f.Crop() & 15) + 1) % 16;
                f.SetState(0);
                t.scalars["opt_autobuy"] = 1;
                t.cash = std::max<int64_t>(t.cash, 100000);
                std::string why;
                const bool planted = tsim.StartPlanting(f, other, why);
                bool sign = true;
                for (int k = 0; k < 4; ++k)
                    sign = sign && t.world.At(f.X() + (k & 1), f.Y() + (k >> 1)).BaseTile() == 539 + 4 * other + k;
                expect(planted && sign, "a field planted with another crop shows the new crop's sign " + why);
            }
        }
        {
            // the year change carries every field's plan on to the end of the timeline (the original's year routine
            // fills the new year with fallow weeks, then repeats the plan from the last harvest mark of a perennial or
            // the last plant code of an annual); an earlier engine left the year empty, so old orchards had no plan
            for (const bool perennial : { true, false })
            {
                GameState t = s;
                Simulation tsim(t);
                int field = 0, crop = -1;
                for (int i = 1; i < 98 && !field; ++i)
                    if (t.FieldAt(i).InUse())
                        field = i;
                for (int c = 0; c < 16 && crop < 0; ++c)
                    if (t.crops[c].loaded && t.crops[c].Perennial() == perennial && t.crops[c].Weeks() > 0)
                        crop = c;
                if (!field || crop < 0)
                    continue;
                Field f = t.FieldAt(field);
                f.SetCrop(crop);
                tsim.StampPlant(field, crop, std::min(239, t.cal.AbsWeek() + 1));
                bool full = f.Crop() == crop;
                for (int y = 0; y < 6; ++y)
                {
                    tsim.SlideTimeline();
                    bool mark = false;
                    for (int r = 192; r < 240; ++r)
                    {
                        const int code = t.ScheduleCode(r, field);
                        full = full && code != 0;
                        mark = mark || code == 0x1B8;
                    }
                    full = full && mark;
                }
                expect(full, std::string("six year changes keep ") + (perennial ? "a perennial" : "an annual") + " field's plan to the end of the timeline (" +
                                 t.crops[crop].name + ")");
            }
        }
        {
            // perennials: after a harvest the age is the crop file's name byte 18 and the phase
            // 0; the timeline marks the harvest week of every year and shows the weeks since it / 6 in between
            GameState t = s;
            Simulation tsim(t);
            for (int pf = 1; pf < 98; ++pf)
            {
                // a field of a shipped farm: its saved schedule (built by the original) is what StampPlant writes from
                // its plant week on (perennials: the yearly plan; annuals: the repeating cycle)
                const Field f = t.FieldAt(pf);
                const int crop = f.Crop() & 15;
                if (!f.InUse())
                    continue;
                int row = 0;
                while (row < 240 && t.ScheduleCode(row, pf) != 0x340 + crop)
                    ++row;
                GameState u = t;
                Simulation usim(u);
                usim.StampPlant(pf, crop, row);
                int diff = 0;
                for (int r = row; r < 240; ++r)
                    diff += u.ScheduleCode(r, pf) != t.ScheduleCode(r, pf);
                expect(row < 240 && diff == 0, "the saved " + t.crops[crop].name + " timeline of field " + std::to_string(pf) + " is the stamped plan (" +
                                                    std::to_string(diff) + " weeks differ)");
            }
            CropDef apples, onions;
            int field = 0;
            for (int f = 1; f < 98 && !field; ++f)
                if (t.FieldAt(f).InUse())
                    field = f;
            if (field && LoadCropFile(gameDir, "APPLES.CRP", apples) && LoadCropFile(gameDir, "ONIONS.CRP", onions))
            {
                t.crops[0] = apples;
                t.crops[1] = onions;
                Field f = t.FieldAt(field);
                f.SetCrop(0);
                f.SetState(5);
                f.SetSub(6);
                f.SetAge(47);
                f.SetPhase(2);
                f.SetStress(9000);
                tsim.HarvestEnd(f);
                expect(f.State() == 4 && f.Sub() == 0 && f.Age() == 1 && f.Phase() == 0 && f.Stress() == 0, "an apple harvest leaves the trees at age 1");
                {
                    // the field executor runs every frame: in week 0 the reset field grows at the next timer tick
                    GameState u = t;
                    Simulation usim(u);
                    u.cal.week = 0;
                    Field g = u.FieldAt(field);
                    usim.TimerTick();
                    expect(g.Age() == 2 && g.Phase() == 1, "and grows again in the same week when that is week 0 (age " + std::to_string(g.Age()) + ")");
                }
                tsim.StampPlant(field, 0, 10);
                expect(t.ScheduleCode(10, field) == 0x340 && t.ScheduleCode(44, field) == 0x1B8 && t.ScheduleCode(92, field) == 0x1B8 &&
                           t.ScheduleCode(45, field) == 688 && t.ScheduleCode(43, field) == 695 && t.ScheduleCode(20, field) == 692,
                       "the apple timeline: harvest mark in week 44 of every year, stage = weeks since it / 6");
                tsim.StampPlant(field, 1, 10);
                expect(t.ScheduleCode(11, field) == 696 && t.ScheduleCode(13, field) == 696 && t.ScheduleCode(14, field) == 697 &&
                           t.ScheduleCode(32, field) == 703 && t.ScheduleCode(33, field) == 0x1B8 && t.ScheduleCode(34, field) == 0x341,
                       "the onion timeline: 22 stage weeks at 3 weeks per stage, harvest, replant");
            }
        }
        const EditResult seed = ShopBuy(s, sim, 21);
        expect(!seed.ok && ShopNeedsPlacing(21), "seed is not bought at once: its bag is placed into a building");
    }
    {
        // F21: a plot sold straight after buying it pays back what it cost (the original pays 100 more)
        GameState g;
        std::string e2;
        bool ok = g.Load(gameDir, ResolveFarm(gameDir, "SCEN0.SSM"), e2);
        Simulation gs(g);
        int fc = -1, fr = -1;
        for (int c = 0; ok && c < 12 && fc < 0; ++c)
            for (int r = 0; r < 12 && fc < 0; ++r)
                if (g.PlotOwner(c, r) == 0)
                    fc = c, fr = r;
        std::string why;
        g.cash = 100000;
        const int64_t before = g.cash;
        ok = ok && fc >= 0 && gs.BuyPlot(fc, fr, why);
        gs.ReconcileCash();
        ok = ok && gs.SellPlot(fc, fr, why);
        gs.ReconcileCash();
        expect(ok && g.cash == before, "a plot bought and sold back at once leaves the cash where it was");
    }
    {
        // the irrigation source list is stored with the original's stride-2 writes: every source
        // of the third prebuilt farm (four pumps) is read back as a pump on its own building
        GameState g;
        std::string e2;
        bool all = g.Load(gameDir, ResolveFarm(gameDir, "SCEN2.SSM"), e2) && !g.irrigationFlag.empty() && g.irrigationFlag[0] == 4;
        for (int i = 0; all && i < 4; ++i)
        {
            const int x = g.irrigationLists[static_cast<size_t>(3 * i)], y = g.irrigationLists[static_cast<size_t>(3 * i + 1)];
            all = g.irrigationLists[static_cast<size_t>(3 * i + 2)] == 64 && x < g.world.size && y < g.world.size && g.world.At(x, y).BaseTile() >= 120 &&
                  g.world.At(x, y).BaseTile() <= 121;
        }
        expect(all, "the saved irrigation sources are read with the original's overlapping layout (SCEN2: four pumps)");
    }
    {
        // worn-out machines: wear 255, not leased -> sold for price / 8; the weekly loop stops
        // after as many machines as the live count, so in a week with a sale the last one is skipped; a plane's record
        // freed clears its airport hangar bit and leaves ground 23 under it
        GameState t = s;
        Simulation tsim(t);
        std::fill(t.machines.begin(), t.machines.end(), 0);
        t.machineCount = 0;
        for (int i : { 1, 2 })
        {
            Machine m(t.machines, i);
            Wr16(t.machines, static_cast<size_t>(i) * 56, 0);
            m.SetFlags(0x20);
            m.SetPos(10 + i, 10);
            Wr8(t.machines, static_cast<size_t>(i) * 56 + 0x11, 255);
            t.machineCount++;
        }
        const int64_t cash0 = t.cash;
        tsim.Fleet().Week();
        const bool first = !Machine(t.machines, 1).InUse() && Machine(t.machines, 2).InUse() && t.cash == cash0 + 50 &&
                           Rd16(t.machines, 2 * 56 + 0x12) == 0;
        tsim.Fleet().Week();
        expect(first && !Machine(t.machines, 2).InUse() && t.cash == cash0 + 100 && t.machineCount == 0,
               "worn-out machines are sold for an eighth of the price, the last one a week later");
        // a plane: hangar slot 5 of an airport plot
        const int pc = 2, pr = 3, hx = pc * 8 + 5, hy = pr * 8 + 6;
        t.SetPlotType(pc, pr, 5 | 0x40);
        Machine p(t.machines, 3);
        Wr16(t.machines, 3 * 56, 8);
        p.SetFlags(0x20);
        p.SetPos(hx, hy);
        Wr8(t.machines, 3 * 56 + 0x32, hx);
        Wr8(t.machines, 3 * 56 + 0x33, hy);
        t.machineCount = 1;
        bool listed = false;
        for (const SaleItem& it : SaleList(t, tsim, 1))
            listed = listed || (it.index == 3 && it.value == 6000);
        tsim.Fleet().Remove(3);
        expect(listed && t.PlotType(pc, pr) == 5 && t.world.At(hx, hy).BaseTile() == 23 && t.world.At(hx + 1, hy + 1).BaseTile() == 23,
               "the crop duster is in the SELL list; freeing it clears the hangar bit and leaves ground under it");
    }
    {
        // game over: an auction that sells everything but the homestead and still cannot cover the debt
        GameState t = s;
        Simulation tsim(t);
        t.cash = 0;
        const bool paid = tsim.Liquidate(9000000, true);
        int plots = 0;
        for (int c = 0; c < t.Plots(); ++c)
            for (int r = 0; r < t.Plots(); ++r)
                plots += t.PlotOwner(c, r) == 1;
        GameEvent ev;
        bool lostBox = false;
        while (tsim.TakeEvent(ev))
            lostBox = lostBox || (ev.id == kGameOverEvent && ev.question && ev.picture == 74);
        expect(!paid && tsim.Bankrupt() && lostBox && plots == 1 && SaleList(t, tsim, 1).empty() && SaleList(t, tsim, 2).empty() &&
                   SaleList(t, tsim, 0).empty(),
               "an auction that cannot cover the debt sells all but the homestead, the farm is lost (" + std::to_string(plots) +
                   " plot left)");
        // the answers: YES starts a new game with no more boxes; NO shows the tax board's notice and then leaves
        tsim.Answer(kGameOverEvent, true);
        expect(tsim.GameOverChoice() == 2 && !tsim.TakeEvent(ev), "the lost farm's YES starts a new game, no notice");
        GameState u = s;
        Simulation usim2(u);
        u.cash = 0;
        usim2.Liquidate(9000000, true);
        while (usim2.TakeEvent(ev)) {}
        usim2.Answer(kGameOverEvent, false);
        expect(usim2.GameOverChoice() == 1 && usim2.TakeEvent(ev) && ev.id == 37,
               "the lost farm's NO (or no answer) shows the tax board's notice, then the program ends");
    }
    std::string path = TempPath("osf_tools.sfm");
    GameState back;
    expect(s.Save(path, err) && back.Load(gameDir, path, err) && back.fieldCount == s.fieldCount, "the edited farm saves and loads");
    return failures ? 1 : 0;
}

// A new game from start to first harvest: generated world, a corn field in the farm block, leased machines.
// a new game on a generated world, on the original's 96 x 96 map and on the engine's larger 144 x 144 one
static int NewGameTestSize(const std::string& gameDir, int mapSize);
static int NewGameTest(const std::string& gameDir)
{
    int failures = 0;
    for (int n : { kOriginalMapSize, kLargeMapSize })
    {
        std::printf("-- a new world of %d x %d cells\n", n, n);
        failures += NewGameTestSize(gameDir, n);
    }
    return failures ? 1 : 0;
}

static int NewGameTestSize(const std::string& gameDir, int mapSize)
{
    Megadata mega;
    std::string err;
    GameState tmpl, s;
    if (!mega.Open(gameDir, err) || !tmpl.Load(gameDir, ResolveFarm(gameDir, "SCEN0.SSM"), err))
        return 1;
    int failures = 0;
    auto expect = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        failures += !ok;
    };
    WorldOptions opt;
    opt.seed = 4242;
    opt.temperature = 3; // warm: corn planted in March needs the heat
    opt.rain = 0;
    opt.mapSize = mapSize;
    expect(GenerateWorld(gameDir, mega, tmpl, opt, s, err), "world generated " + err);
    expect(s.world.size == mapSize && s.Plots() == mapSize / 8 && s.zones.size() == static_cast<size_t>(s.Plots() * s.Plots() * 7),
           "the map is " + std::to_string(s.world.size) + " cells a side, " + std::to_string(s.Plots()) + " plots a side");
    Simulation sim(s);
    sim.SetTownLayout(mega.Read("townly0.cmp")); // as the game does: the town plots are redrawn when they change level
    const int fc = static_cast<int>(s.scalars["farm_plot_x"]), fr = static_cast<int>(s.scalars["farm_plot_y"]);
    // the new farm: 3 x 3 plots, fenced, the homestead picture in its corner
    int owned = 0;
    for (int c = 0; c < s.Plots(); ++c)
        for (int r = 0; r < s.Plots(); ++r)
            owned += s.PlotOwner(c, r) == 1;
    expect(owned == 9 && s.world.At(fc * 8, fr * 8).BaseTile() == 180 && s.world.At(fc * 8 + 1, fr * 8 + 2).BaseTile() == 252 &&
               (s.world.At(fc * 8 + 12, fr * 8 + 12).overlay & 0x800) && s.cal.month == 3,
           "a 3 x 3 farm, fenced, with the homestead, marked as yours, in April");
    // the town's home plot (level 10, type 0): the layout's dirt roads on ground, every cell flagged as town
    {
        int tc = -1, tr = -1, roads = 0, town = 0;
        for (int c = 0; c < s.Plots(); ++c)
            for (int r = 0; r < s.Plots(); ++r)
                if (s.PlotOwner(c, r) == 2)
                {
                    tc = c;
                    tr = r;
                }
        if (tc >= 0)
            for (int x = tc * 8; x < tc * 8 + 8; ++x)
                for (int y = tr * 8; y < tr * 8 + 8; ++y)
                {
                    roads += s.world.At(x, y).BaseTile() >= 161 && s.world.At(x, y).BaseTile() <= 171;
                    town += (s.world.At(x, y).overlay & 0x4000) != 0;
                }
        expect(tc >= 0 && s.PlotType(tc, tr) == 0xA0 && roads >= 20 && town == 64,
               "the town's home plot: roads and the town flag (" + std::to_string(roads) + " road cells)");
        // no water sources are left over from the template farm (a new world saved by the original has none)
        expect(!s.irrigationFlag.empty() && s.irrigationFlag[0] == 0, "a new world has no water sources (" +
               std::to_string(s.irrigationFlag.empty() ? -1 : s.irrigationFlag[0]) + ")");
    }
    // the field goes in the middle of the farm, cleared of trees and rocks first
    for (int x = fc * 8 + 8; x < fc * 8 + 17; ++x)
        for (int y = fr * 8 + 8; y < fr * 8 + 18; ++y)
            s.world.At(x, y).base = static_cast<uint16_t>(17);
    const EditResult r = ApplyTool(s, sim, Tool::Plant, fc * 8 + 9, fr * 8 + 9, 0);
    expect(r.ok, "a corn field in the farm block " + r.message);
    // the soil saturates in most climates: an empty ditch along the field drains it
    int dug = 0;
    for (int y = fr * 8 + 9; y < fr * 8 + 17; ++y)
        dug += ApplyTool(s, sim, Tool::Ditch, fc * 8 + 8, y, 0).ok;
    expect(dug == 8, "a drainage ditch along the field (" + std::to_string(dug) + " cells)");
    const int fi = sim.FieldIndexAt(fc * 8 + 10, fr * 8 + 10);
    const int64_t cash0 = s.cash;
    int harvested = 0;
    std::string m;
    for (int day = 0; day < 7 * 20 && !harvested; ++day)
    {
        for (int t = 0; t <= s.speedDelay; ++t)
            sim.TimerTick();
        if (s.cal.day == 0 && std::getenv("OSF_TRACE_FIELD"))
        {
            Field f = s.FieldAt(fi);
            int mat = 0, q = 0;
            const int64_t v = sim.HarvestValue(f, &mat, &q);
            std::printf("     wk %d/%d state %d age %d heatdef %d stress %d moist %d nutA %d value %lld maturity %d q %d temp %d\n", s.cal.week + 1, s.cal.month + 1,
                        f.State(), f.Age(), f.HeatDeficit(), f.Stress(), f.Moisture(), f.NutrientA(), static_cast<long long>(v), mat, q, sim.Temperature());
        }
        while (sim.TakeMessage(m))
            if (m.rfind("Harvested", 0) == 0)
            {
                harvested++;
                std::printf("     %s (week %d of month %d)\n", m.c_str(), s.cal.week + 1, s.cal.month + 1);
            }
        GameEvent ev;
        while (sim.TakeEvent(ev)) {}
    }
    expect(harvested > 0, "the crop was harvested within 20 weeks (field state " + std::to_string(s.FieldAt(fi).State()) + ")");
    {
        // the town's home plot after its first level change: its roads are paved (the original redraws the home
        // plot's block with dirt roads, then paves them; seen in a save of the original at level 13)
        const int tc = static_cast<int>(s.scalars["homestead_x"]), tr = static_cast<int>(s.scalars["homestead_y"]);
        int paved = 0, dirt = 0;
        for (int x = tc * 8; x < tc * 8 + 8; ++x)
            for (int y = tr * 8; y < tr * 8 + 8; ++y)
            {
                const int t = s.world.At(x, y).BaseTile();
                paved += t >= 150 && t <= 160;
                dirt += t >= 161 && t <= 171;
            }
        expect((s.PlotType(tc, tr) >> 4) != 10 && paved >= 20 && dirt == 0,
               "the town's home plot changed level and its roads are paved (" + std::to_string(paved) + " paved, " + std::to_string(dirt) + " dirt)");
    }
    std::printf("     cash %lld -> %lld\n", static_cast<long long>(cash0), static_cast<long long>(s.cash));
    const std::string path = TempPath("osf_newgame.sfm");
    GameState back;
    expect(s.Save(path, err) && back.Load(gameDir, path, err), "the new farm saves and loads");
    auto readFile = [](const std::string& p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
    };
    const std::vector<uint8_t> file = readFile(path);
    size_t body = 0;
    expect(file.size() == SaveBodySize(mapSize) + kSaveSignatureSize && SaveFileMapSize(file, body) == mapSize &&
               (mapSize != kOriginalMapSize || body == kOriginalSaveSize),
           "the save carries the signature with its map size (" + std::to_string(file.size()) + " bytes)");
    // (loaded without the new-game price reset, as the round-trip test does)
    expect(back.Load(gameDir, path, err, false), "loaded again as saved");
    bool cellsSame = back.world.size == s.world.size && back.zones == s.zones;
    for (size_t i = 0; cellsSame && i < s.world.cells.size(); ++i)
        cellsSame = back.world.cells[i].base == s.world.cells[i].base && back.world.cells[i].overlay == s.world.cells[i].overlay &&
                    back.world.cells[i].terrain == s.world.cells[i].terrain;
    const std::string path2 = TempPath("osf_newgame2.sfm");
    expect(cellsSame && back.Save(path2, err) && readFile(path2) == file, "loaded again: the same map and plots, saved again byte for byte");
    {
        // the signature must match the file: one that claims another map size is refused
        std::vector<uint8_t> bad = file;
        bad[bad.size() - 3] = static_cast<uint8_t>(mapSize == kOriginalMapSize ? kLargeMapSize : kOriginalMapSize);
        std::ofstream(path2, std::ios::binary).write(reinterpret_cast<const char*>(bad.data()), static_cast<std::streamsize>(bad.size()));
        GameState refused;
        expect(!refused.Load(gameDir, path2, err), "a save whose signature does not fit its size is refused");
    }
    if (mapSize > 128)
    {
        // the far corner of a larger world (its last 2 x 2 plots): cells past 127 hold a field (the records keep
        // coordinates in bytes)
        const int pc = s.Plots() - 2, x0 = pc * 8;
        s.cash = 1000000;
        int bought = 0;
        std::string why;
        for (int c = pc; c < pc + 2; ++c)
            for (int r = pc; r < pc + 2; ++r)
                bought += sim.BuyPlot(c, r, why);
        expect(bought == 4 && (s.world.At(x0 + 15, x0 + 15).overlay & 0x800), "the last plots of the map can be bought " + why);
        const bool sold = sim.SellPlot(pc + 1, pc + 1, why) && s.PlotOwner(pc + 1, pc + 1) == 0;
        expect(sold && sim.BuyPlot(pc + 1, pc + 1, why), "the last plot sold and bought again " + why);
        for (int x = x0; x < x0 + 16; ++x)
            for (int y = x0; y < x0 + 16; ++y)
                if (!s.world.At(x, y).HasObject())
                    s.world.At(x, y).base = 17;
        const EditResult far = ApplyTool(s, sim, Tool::Plant, x0 + 1, x0 + 1, 0);
        const int ff = sim.FieldIndexAt(x0 + 3, x0 + 3);
        expect(far.ok && ff > 0 && s.FieldAt(ff).X() > 127 && s.FieldAt(ff).Y() > 127,
               "a field in the far corner, at cell " + std::to_string(ff > 0 ? s.FieldAt(ff).X() : -1) + " " + far.message);
        GameState again;
        expect(s.Save(path2, err) && again.Load(gameDir, path2, err) && again.FieldAt(ff).X() == s.FieldAt(ff).X() &&
                   again.PlotOwner(pc, pc) == 1,
               "the far field and plot survive a save and load");
        std::filesystem::remove(path2);
        // a town plot past the original's 12 plots a side grows like any other (it once stayed at level 0)
        GameState t = s;
        int tc = -1, tr = -1;
        for (int c = 12; c < t.Plots() && tc < 0; ++c)
            for (int r = 12; r < t.Plots() && tc < 0; ++r)
                if (t.PlotOwner(c, r) == 0)
                    tc = c, tr = r;
        if (tc >= 0)
        {
            t.SetPlotOwner(tc, tr, 2);
            t.SetPlotType(tc, tr, 1);
            t.scalars["town_work_x"] = tc;
            t.scalars["town_work_y"] = tr;
            t.scalars["town_pot"] = 1000;
            Simulation tsim(t);
            tsim.SetTownLayout(mega.Read("townly0.cmp"));
            for (int d = 0; d < 15; ++d)
                tsim.DayTick();
        }
        expect(tc >= 0 && (t.PlotType(tc, tr) >> 4) >= 1,
               "a town plot at " + std::to_string(tc) + "," + std::to_string(tr) + " grows (level " + std::to_string(tc >= 0 ? t.PlotType(tc, tr) >> 4 : -1) + ")");
    }
    return failures ? 1 : 0;
}

// Compares two saved games field by field (used to check the engine against the original game).
static int CompareSaves(const std::string& gameDir, const std::string& a, const std::string& b)
{
    GameState x, y;
    std::string err;
    if (!x.Load(gameDir, a, err, false) || !y.Load(gameDir, b, err, false))
    {
        std::printf("cannot load: %s\n", err.c_str());
        return 2;
    }
    int diffs = 0;
    auto line = [&](const std::string& what, long long u, long long v) {
        if (u != v)
        {
            std::printf("  %-28s %12lld %12lld  (%+lld)\n", what.c_str(), u, v, v - u);
            diffs++;
        }
    };
    std::printf("A = %s\nB = %s\n", a.c_str(), b.c_str());
    std::printf("date A week %d of %d/%d day %d | B week %d of %d/%d day %d\n", x.cal.week + 1, x.cal.month + 1, x.cal.year, x.cal.day, y.cal.week + 1,
                y.cal.month + 1, y.cal.year, y.cal.day);
    std::printf("-- scalars (A, B)\n");
    for (const auto& [k, v] : x.scalars)
        if (k.rfind("menu_", 0) != 0 && k != "timer" && k != "timer2")
            line(k, v, y.scalars.count(k) ? y.scalars.at(k) : 0);
    std::printf("-- fields\n");
    static const std::pair<const char*, int> fieldBytes[] = { { "state", 0x00 }, { "sub", 0x01 }, { "crop", 0x0C }, { "pests", 0x12 }, { "weeds", 0x13 },
                                                              { "disease", 0x14 }, { "fertN", 0x1A }, { "A", 0x1B }, { "B", 0x1C }, { "C", 0x1D },
                                                              { "tox", 0x1E }, { "phase", 0x1F }, { "age", 0x20 }, { "fullDitch", 0x31 }, { "emptyDitch", 0x32 },
                                                              { "pressP", 0x3A }, { "pressW", 0x3B }, { "pressD", 0x3C } };
    static const std::pair<const char*, int> fieldWords[] = { { "tile", 0x0E }, { "stress", 0x10 }, { "moisture", 0x16 }, { "heatReq", 0x22 },
                                                              { "heatDef", 0x24 }, { "chill", 0x26 }, { "contract", 0x2E } };
    for (int i = 1; i < 98; ++i)
    {
        Field f = x.FieldAt(i), g = y.FieldAt(i);
        if (!f.InUse() && !g.InUse())
            continue;
        const int before = diffs;
        line("field " + std::to_string(i) + " in use", f.InUse(), g.InUse());
        for (const auto& [n, o] : fieldBytes)
            line("field " + std::to_string(i) + " " + n, f.Get8(o), g.Get8(o));
        for (const auto& [n, o] : fieldWords)
            line("field " + std::to_string(i) + " " + n, f.Get16(o), g.Get16(o));
        (void)before;
    }
    std::printf("-- market prices\n");
    for (int c = 0; c < 16; ++c)
        line("price " + x.crops[c].name, x.MarketPrice(c), y.MarketPrice(c));
    std::printf("-- tables\n");
    auto count = [](const std::vector<uint8_t>& t, size_t rec, size_t flag) {
        int n = 0;
        for (size_t i = 1; (i + 1) * rec <= t.size(); ++i)
            n += (t[i * rec + flag] & 0x20) != 0;
        return n;
    };
    line("animals in use", count(x.animals, 32, 2), count(y.animals, 32, 2));
    line("machines in use", count(x.machines, 56, 2), count(y.machines, 56, 2));
    line("buildings in use", count(x.structures, 70, 2), count(y.structures, 70, 2));
    line("barrels in use", count(x.objects20, 20, 2), count(y.objects20, 20, 2));
    line("items in use", count(x.items, 12, 10), count(y.items, 12, 10));
    line("weather table differs", 0, x.weather != y.weather);
    int rows = 0;
    for (size_t i = 0; i < x.timeline.size() && i < y.timeline.size(); i += 2)
        rows += x.timeline[i] != y.timeline[i] || x.timeline[i + 1] != y.timeline[i + 1];
    line("timeline cells differing", 0, rows);
    std::printf("-- map (base tiles; animals and machines ignored)\n");
    std::map<std::string, int> kinds;
    for (int cx = 0; cx < x.world.size; ++cx)
        for (int cy = 0; cy < x.world.size; ++cy)
        {
            const int u = x.world.At(cx, cy).BaseTile(), v = y.world.At(cx, cy).BaseTile();
            if (u == v)
                continue;
            auto kind = [](int id) {
                return id >= 15 && id <= 37 ? "ground" : id >= 39 && id <= 68 ? "water" : id >= 87 && id <= 108 ? "ditch" : id >= 539 && id <= 634 ? "sign/marker"
                       : id >= 688 && id <= 815 ? "crop" : id == 0x24 || id == 0x0C ? "soil" : "other";
            };
            if (u >= 39 && u <= 68 && v >= 39 && v <= 68 && (((u - 39) ^ 1) + 39) == v)
                continue; // water shimmer frame (the ambient animation), not a state difference
            kinds[std::string(kind(u)) + " -> " + kind(v)]++;
            if (std::getenv("OSF_COMPARE_CELLS"))
                std::printf("  cell %d,%d: %d -> %d\n", cx, cy, u, v);
        }
    for (const auto& [k, n] : kinds)
    {
        std::printf("  %-28s %d cells\n", k.c_str(), n);
        diffs++;
    }
    std::printf("%d differences\n", diffs);
    return 0;
}

static const char* const kNoDialogText = N_(
    "No folder dialog is available on this system (install zenity or kdialog),\nso tell OpenSimFarm the "
    "folder once by starting it as\n  opensimfarm /path/to/SIMFARM\n\nThe folder must contain "
    "MEGADATA.IDX and MEGADATA.EEA.\nOpenSimFarm does not include any of the original game's files.");

// Asks the player for the SimFarm folder: a box with `problem` (or the plain "not found" text) and the buttons
// Choose folder... / `cancelLabel`, then the system's folder dialog, until a folder with the game files (it, or one up
// to two levels below it, not in `skip`) is chosen. Nothing when the player cancels; `noDialog` when the system has
// no folder dialog.
static std::optional<std::string> AskPlayerForGameFolder(std::string problem, const char* cancelLabel,
                                                         const std::vector<std::string>& skip, bool& noDialog)
{
    noDialog = false;
#ifdef __EMSCRIPTEN__
    // in the browser the page chooses the files; no folder dialog here
    (void)cancelLabel;
    (void)skip;
    ReportGameFilesProblem(Latin1ToUtf8(problem.empty() ? Tr("The SimFarm files could not be found.") : problem).c_str());
    return std::nullopt;
#endif
    for (;;)
    {
        const std::string choose = Latin1ToUtf8(Tr("Choose folder...")), cancel = Latin1ToUtf8(Tr(cancelLabel));
        const SDL_MessageBoxButtonData buttons[] = {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, choose.c_str() },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, cancel.c_str() },
        };
        const std::string text = Latin1ToUtf8((problem.empty() ? std::string(Tr("OpenSimFarm plays with the files of your own copy of SimFarm,\nbut it could not find them."))
                                                               : problem) + "\n\n" +
            Tr("Choose the SimFarm folder (the one that holds MEGADATA.IDX\nand MEGADATA.EEA). OpenSimFarm only reads the files there\nand remembers the folder."));
        const SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, nullptr, "OpenSimFarm", text.c_str(), 2, buttons, nullptr };
        int pressed = 0;
        if (SDL_ShowMessageBox(&box, &pressed) != 0 || pressed != 1)
            return std::nullopt;
        bool available = true;
        const auto chosen = AskGameDirectory(available);
        if (!available)
        {
            noDialog = true;
            return std::nullopt;
        }
        if (!chosen)
            continue; // the dialog was cancelled: back to the box
        if (const auto dir = GameDirectoryIn(*chosen, skip))
            return dir;
        problem = (GameDirectoryIn(*chosen) ? TrF("That folder holds only SimFarm files that cannot be used:\n{0}", *chosen)
                                            : TrF("That folder does not hold SimFarm's files\n(MEGADATA.IDX and MEGADATA.EEA):\n{0}", *chosen));
    }
}

int main(int argc, char** argv)
{
    // --reconfigure (anywhere on the line): the player chooses the SimFarm folder again before the game starts; it is
    // taken out of the arguments, so the rest reads as usual (a farm or save may follow)
    bool reconfigure = false;
    std::vector<char*> args;
    for (int i = 0; i < argc; ++i)
    {
        if (i > 0 && std::strcmp(argv[i], "--reconfigure") == 0)
            reconfigure = true;
        else
            args.push_back(argv[i]);
    }
    args.push_back(nullptr);
    argc = static_cast<int>(args.size()) - 1;
    argv = args.data();
    // command-line modes without a window
    if (argc > 1 && std::strcmp(argv[1], "--audiotest") == 0)
    {
        // the modern sound path in memory: songs and effects rendered and measured, nothing written
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        Megadata mega;
        std::string err;
        if (!mega.Open(*dir, err)) { std::printf("%s\n", err.c_str()); return 1; }
        return SoundPlayer::SelfTest(mega);
    }
    if (argc > 1 && std::strcmp(argv[1], "--langtest") == 0)
    {
        // the built-in translations: characters, placeholders and the words that must fit the pictures
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        Megadata mega;
        std::string err;
        if (!mega.Open(*dir, err)) { std::printf("%s\n", err.c_str()); return 1; }
        return LangSelfTest(mega);
    }
    if (argc > 1 && std::strcmp(argv[1], "--touchtest") == 0)
        return TouchSelfTest() ? 1 : 0; // the touch gestures (no game folder needed)
    if (argc > 1 && std::strcmp(argv[1], "--graphicstest") == 0)
    {
        // Modern Render in memory: the tile smoothing on the player's tiles and the glide tracking, nothing written
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        Megadata mega;
        std::string err;
        if (!mega.Open(*dir, err)) { std::printf("%s\n", err.c_str()); return 1; }
        MapGraphics gfx;
        if (!LoadMapGraphics(mega, gfx)) { std::printf("tiles could not be read\n"); return 1; }
        const int pictures = ArchivePictureTest(mega);
        return MapView::SelfTest(gfx) || pictures ? 1 : 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0)
    {
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        return SelfTest(*dir);
    }
    if (argc > 1 && std::strcmp(argv[1], "--findgame") == 0)
    {
        // --findgame [FOLDER]: where the game would find SimFarm: in FOLDER (or up to two levels below it), else
        // by the search of the usual places; prints the folder, changes nothing
        const auto dir = argc > 2 ? GameDirectoryIn(argv[2]) : SearchGameDirectory();
        std::printf("%s\n", dir ? dir->c_str() : "not found");
        return dir ? 0 : 1;
    }
    if (argc > 4 && std::strcmp(argv[1], "--run") == 0)
    {
        // --run FROM DAYS TO [folder]: load a save, run DAYS game days at timer level, write TO
        char* fake[2] = { argv[0], argc > 5 ? argv[5] : nullptr };
        auto dir = ResolveGameDirectory(argc > 5 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err, true)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        sim.SetGameDir(*dir);
        Machines::smartDriving = std::getenv("OSF_SMART") != nullptr; // the engine's route planner (default: the original's steering)
        Megadata mega;
        if (mega.Open(*dir, err))
            sim.SetTownLayout(mega.Read("townly0.cmp"));
        // DAYS = "M/W/D" runs until that date
        if (const char* dis = std::getenv("OSF_START_DISASTER"))
            std::printf("  disaster %s started: %d\n", dis, sim.Hazards().Start(std::atoi(dis)) ? 1 : 0); // 1 tornado .. 6 windstorm
        int days = std::atoi(argv[3]);
        int tm = -1, tw = -1, td = -1, ty = -1;
        if (std::sscanf(argv[3], "%d/%d/%d/%d", &ty, &tm, &tw, &td) == 4)
            days = 100000;
        else if (ty = -1, std::sscanf(argv[3], "%d/%d/%d", &tm, &tw, &td) == 3)
            days = 100000;
        auto drain = [&] {
            std::string m;
            while (sim.TakeMessage(m))
                std::printf("  [%d/%d/%d] %s\n", s.cal.week + 1, s.cal.month + 1, s.cal.year, m.c_str());
            GameEvent ev;
            while (sim.TakeEvent(ev))
            {
                std::printf("  [%d/%d/%d] box %d: %s\n", s.cal.week + 1, s.cal.month + 1, s.cal.year, ev.id, ev.text.c_str());
                if (ev.id == kTownVoteEvent)
                {
                    // OSF_VOTE=0..3 answers the Town Choice box (residential, commercial, airport, recreational)
                    const int b = std::getenv("OSF_VOTE") ? std::atoi(std::getenv("OSF_VOTE")) : 0;
                    if (!sim.TownVote(b))
                        sim.TownVote(0);
                    std::printf("  town vote %d\n", b);
                }
            }
        };
        if (argv[3][0] == 't')
        {
            // "tN": exactly N timer ticks
            for (long t = std::atol(argv[3] + 1); t > 0; --t)
            {
                sim.TimerTick();
                drain();
            }
            days = 0;
        }
        for (int d = 0; d < days; ++d)
        {
            if (tm >= 0 && s.cal.month == tm && s.cal.week == tw && s.cal.day == td && (ty < 0 || s.cal.year == ty))
                break;
            for (int t = 0; t <= s.speedDelay; ++t)
            {
                sim.TimerTick();
                if (std::getenv("OSF_SIGNLOG"))
                {
                    // OSF_SIGNLOG: a field in use whose top-left 2 x 2 cells are not its crop's sign
                    static std::map<int, bool> lost;
                    for (int i = 1; i < 98; ++i)
                    {
                        Field f = s.FieldAt(i);
                        if (!f.InUse())
                            continue;
                        const int want = 539 + 4 * (f.Crop() & 15);
                        bool ok = true;
                        for (int k = 0; k < 4; ++k)
                            ok = ok && s.world.At(f.X() + (k & 1), f.Y() + (k >> 1)).BaseTile() == want + k;
                        if (!(f.State() == 5 && f.Sub() >= 12))
                        {
                            int st = 0;
                            for (int x = f.X(); x < f.X() + f.W(); ++x)
                                for (int y = f.Y(); y < f.Y() + f.H(); ++y)
                                    st += s.world.At(x, y).HasObject() && ((s.world.At(x, y).overlay & 0x7FF) == 0x40C || (s.world.At(x, y).overlay & 0x7FF) == 0x40D);
                            if (st)
                                std::printf("stubble left: field %d (%s) state %d sub %d: %d cells at %d/%d/%d\n", i, s.crops[f.Crop() & 15].name.c_str(), f.State(), f.Sub(), st, s.cal.month, s.cal.week, s.cal.day);
                        }
                        if (!ok && !lost[i])
                            std::printf("sign lost: field %d (%s) state %d sub %d at %d/%d/%d tick %d: tile %d\n", i, s.crops[f.Crop() & 15].name.c_str(), f.State(), f.Sub(),
                                        s.cal.month, s.cal.week, s.cal.day, t, s.world.At(f.X(), f.Y()).BaseTile());
                        lost[i] = !ok;
                    }
                }
            }
            drain();
            // OSF_PICLOG: machine pictures that do not match the machines (a drawn machine missing a cell, or a machine
            // picture where no machine stands)
            if (std::getenv("OSF_PICLOG"))
            {
                std::set<int> covered;
                int broken = 0, ghosts = 0;
                for (int i = 1; i < 127; ++i)
                {
                    Machine m(s.machines, i);
                    if (!m.InUse() || !(m.Flags() & 0x80) || m.Type() > 7)
                        continue;
                    const bool check = !(m.Flags() & 0x0C); // not stored, not on a field job (the sweep draws one cell, as the original)
                    const bool v = (m.Heading() & 7) == 0 || (m.Heading() & 7) == 4;
                    for (int k = 0; k < Machines::Cells(m.Type()); ++k)
                    {
                        const int x = m.X() + (v ? 0 : k), y = m.Y() + (v ? k : 0);
                        if (x >= s.world.size || y >= s.world.size)
                            continue;
                        covered.insert(x * s.world.size + y);
                        const int o = s.world.At(x, y).overlay & 0x7FF;
                        if (check && (!s.world.At(x, y).HasObject() || o < 848 || o > 945))
                        {
                            if (++broken <= 3)
                                std::printf("  missing: machine %d type %d flags %02X heading %d at %d,%d cell %d,%d tile %d\n", i, m.Type(), m.Flags(), m.Heading(), m.X(), m.Y(), x, y, s.world.At(x, y).BaseTile());
                        }
                    }
                }
                for (int x = 0; x < s.world.size; ++x)
                    for (int y = 0; y < s.world.size; ++y)
                    {
                        const int o = s.world.At(x, y).overlay & 0x7FF;
                        if (s.world.At(x, y).HasObject() && o >= 848 && o <= 911 && !covered.count(x * s.world.size + y))
                        {
                            if (++ghosts <= 3)
                                std::printf("  ghost: cell %d,%d picture %d\n", x, y, o);
                        }
                    }
                if (broken || ghosts)
                    std::printf("pictures day %d/%d/%d: %d missing cells, %d ghost cells\n", s.cal.month, s.cal.week, s.cal.day, broken, ghosts);
            }
            // OSF_BUSYLOG: a machine that has been busy (flags 0x0B, what SELL refuses) for 14 days in a row
            if (std::getenv("OSF_BUSYLOG"))
            {
                static std::map<int, int> busy;
                for (int i = 1; i < 127; ++i)
                {
                    Machine m(s.machines, i);
                    if (m.Type() > 8 || !(m.Flags() & 0x0B))
                    {
                        busy.erase(i);
                        continue;
                    }
                    if (++busy[i] % 14 == 0)
                        std::printf("busy %d type %d for %d days at %d,%d target %d,%d flags %02X day %d/%d/%d\n", i, m.Type(), busy[i], m.X(), m.Y(),
                                    m.TargetX(), m.TargetY(), m.Flags(), s.cal.month, s.cal.week, s.cal.day);
                }
            }
            // OSF_STUCKLOG: a machine away from its target that has not moved for two days
            if (std::getenv("OSF_STUCKLOG"))
            {
                static std::map<int, std::pair<int, int>> still; // index -> (position, days)
                for (int i = 1; i < 127; ++i)
                {
                    Machine m(s.machines, i);
                    const int pos = m.X() * 256 + m.Y();
                    if (m.Type() > 8 || (m.Flags() & 0x04) || (m.X() == m.TargetX() && m.Y() == m.TargetY()))
                    {
                        still.erase(i);
                        continue;
                    }
                    auto& e = still[i];
                    e.second = e.first == pos ? e.second + 1 : 0;
                    e.first = pos;
                    if (e.second == 2)
                        std::printf("still %d type %d at %d,%d target %d,%d flags %02X day %d/%d/%d\n", i, m.Type(), m.X(), m.Y(), m.TargetX(),
                                    m.TargetY(), m.Flags(), s.cal.month, s.cal.week, s.cal.day);
                }
            }
        }
        if (const char* fs = std::getenv("OSF_FIELDSET"))
        {
            // OSF_FIELDSET=FIELD:OFFSET=VALUE[,OFFSET=VALUE...] (hex offsets): set field record bytes for live checks
            int fi = 0;
            const char* p = fs;
            fi = static_cast<int>(std::strtol(p, const_cast<char**>(&p), 10));
            Field f = s.FieldAt(fi);
            while (*p == ':' || *p == ',')
            {
                ++p;
                const int off = static_cast<int>(std::strtol(p, const_cast<char**>(&p), 16));
                if (*p != '=')
                    break;
                ++p;
                const int val = static_cast<int>(std::strtol(p, const_cast<char**>(&p), 10));
                f.Set8(off, val);
                std::printf("field %d +%02X = %d (at %d,%d state %d)\n", fi, off, val, f.X(), f.Y(), f.State());
            }
        }
        if (std::getenv("OSF_MACHDUMP"))
        {
            // every machine record in use: type, flags, cell, target, home, partner, wear
            for (int i = 1; i < 127; ++i)
            {
                Machine m(s.machines, i);
                if (!m.InUse())
                    continue;
                std::printf("machine %d type %d flags %02X at %d,%d target %d,%d home %d,%d partner %d wear %02X%s\n", i, m.Type(), m.Flags(), m.X(), m.Y(),
                            m.TargetX(), m.TargetY(), m.HomeX(), m.HomeY(), m.Partner(), m.Wear(), m.Leased() ? " leased" : "");
            }
            std::printf("cash %lld autolease %lld loan %lld\n", static_cast<long long>(s.cash), static_cast<long long>(s.scalars["menu_6"]), static_cast<long long>(s.scalars["loan"]));
            for (int i = 1; i < 98; ++i)
            {
                Field f = s.FieldAt(i);
                if (f.InUse())
                    std::printf("field %d crop %s state %d sub %d lead %d impl %d at %d,%d\n", i, s.crops[f.Crop() & 15].name.c_str(), f.State(), f.Sub(), f.Get16(0x28), f.Get16(0x2A), f.X(), f.Y());
            }
        }
        if (const char* md = std::getenv("OSF_MAPDUMP"))
        {
            // OSF_MAPDUMP=X,Y,R: the base tile ids (and * for an object) of the square of radius R around (X, Y)
            int cx = 0, cy = 0, rr = 3;
            std::sscanf(md, "%d,%d,%d", &cx, &cy, &rr);
            for (int y = cy - rr; y <= cy + rr; ++y)
            {
                std::printf("%3d:", y);
                for (int x = cx - rr; x <= cx + rr; ++x)
                    if (x >= 0 && y >= 0 && x < s.world.size && y < s.world.size)
                    {
                        std::printf(" %4d%c", s.world.At(x, y).BaseTile(), s.world.At(x, y).HasObject() ? '*' : ' ');
                        if (std::getenv("OSF_MAPOV")) // OSF_MAPOV: the overlay picture too
                            std::printf("/%d", s.world.At(x, y).overlay & 0x7FF);
                    }
                std::printf("\n");
            }
        }
        if (const char* ss = std::getenv("OSF_SCALARSET"))
        {
            // OSF_SCALARSET=NAME=VALUE[,NAME=VALUE...]: set saved scalars (cash, tax_due, ...) for live checks
            std::string all = ss;
            size_t at = 0;
            while (at < all.size())
            {
                size_t end = all.find(',', at);
                if (end == std::string::npos)
                    end = all.size();
                const std::string kv = all.substr(at, end - at);
                const size_t eq = kv.find('=');
                if (eq != std::string::npos)
                {
                    s.scalars[kv.substr(0, eq)] = std::stoll(kv.substr(eq + 1));
                    std::printf("scalar %s = %s\n", kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str());
                }
                at = end + 1;
            }
        }
        if (const char* st = std::getenv("OSF_SETTILE"))
        {
            // OSF_SETTILE=ID[,ID...]: put these base tiles on the first owned plain-ground cells after (farm x, farm y)
            // + (2, 2), one per row, for live checks in the original; prints the cells
            std::vector<int> ids;
            for (const char* p = st; *p;)
            {
                ids.push_back(std::atoi(p));
                while (*p && *p != ',')
                    ++p;
                if (*p == ',')
                    ++p;
            }
            const int fx = static_cast<int>(s.scalars["farm_plot_x"]) * 8 + 2, fy = static_cast<int>(s.scalars["farm_plot_y"]) * 8 + 2;
            size_t k = 0;
            for (int y = fy; y < s.world.size && k < ids.size(); y += 2)
                for (int x = fx; x < s.world.size && k < ids.size(); ++x)
                {
                    MapCell& c = s.world.At(x, y);
                    if (s.PlotOwner(x / 8, y / 8) == 1 && !c.HasObject() && c.BaseTile() >= 15 && c.BaseTile() <= 30 && !sim.FieldIndexAt(x, y))
                    {
                        c.base = static_cast<uint16_t>((c.base & 0xF800) | ids[k]);
                        std::printf("tile %d at %d,%d\n", ids[k], x, y);
                        ++k;
                        break;
                    }
                }
        }
        if (std::getenv("OSF_STOREDUMP"))
        {
            // every storage building with what its slots hold (content words: 0xA8 + slot = a crop stack)
            for (int i = 1; i < 127; ++i)
            {
                const size_t b = static_cast<size_t>(i) * 70;
                if (!(Rd8(s.structures, b + 2) & 0x20))
                    continue;
                std::printf("structure %d cat %02X at %d,%d:", i, Rd16(s.structures, b), Rd8(s.structures, b + 4), Rd8(s.structures, b + 5));
                for (int k = 0; k < 12; ++k)
                {
                    const int t = Rd16(s.structures, b + 0x22 + 2 * k);
                    if (t != 0xFFFF)
                        std::printf(" %02X%s", t, t >= 0xA8 && t < 0xB8 ? ("(" + s.crops[(t - 0xA8) & 15].name + ")").c_str() : "");
                }
                std::printf("\n");
            }
        }
        if (std::getenv("OSF_CELLSCAN"))
        {
            // owned cells that show plain ground but carry the object flag or the field-cell flag without a field
            for (int x = 0; x < s.world.size; ++x)
                for (int y = 0; y < s.world.size; ++y)
                {
                    const MapCell& c = s.world.At(x, y);
                    if (s.PlotOwner(x / 8, y / 8) != 1)
                        continue;
                    const int id = c.BaseTile();
                    const bool ground = (id >= 15 && id <= 30) || id == 36;
                    const bool stale = c.HasObject() || ((c.overlay & 0x1000) && !sim.FieldIndexAt(x, y)) || (id == 36 && !sim.FieldIndexAt(x, y));
                    if (ground && stale)
                        std::printf("cell %d,%d base %04X overlay %04X field %d\n", x, y, c.base, c.overlay, sim.FieldIndexAt(x, y));
                }
        }
        if (std::getenv("OSF_TOWNDUMP"))
        {
            // the town's plots, its home plot and the plot it works on
            std::printf("home %lld,%lld work %lld,%lld pot %lld\n", static_cast<long long>(s.scalars["homestead_x"]), static_cast<long long>(s.scalars["homestead_y"]),
                        static_cast<long long>(s.scalars["town_work_x"]), static_cast<long long>(s.scalars["town_work_y"]), static_cast<long long>(s.scalars["town_pot"]));
            for (int c = 0; c < s.Plots(); ++c)
                for (int r = 0; r < s.Plots(); ++r)
                    if (s.PlotOwner(c, r) == 2)
                        std::printf("town plot %d,%d type %02X\n", c, r, s.PlotType(c, r));
        }
        // OSF_VIEW=x,y: the saved view's top-left cell (to look at a place of the map after loading)
        if (int vx = 0, vy = 0; std::getenv("OSF_VIEW") && std::sscanf(std::getenv("OSF_VIEW"), "%d,%d", &vx, &vy) == 2)
        {
            s.scalars["view_x"] = vx;
            s.scalars["view_y"] = vy;
            s.StoreScalarsToTail();
        }
        return s.Save(argv[4], err) ? 0 : 1;
    }
    if (argc > 7 && std::strcmp(argv[1], "--apply") == 0)
    {
        // --apply FROM TOOL X Y PARAM TO: one EDIT tool click on map cell (X, Y), as the player would (comparison runs);
        // TOOL is the Tool enum value (Plant 13 with PARAM = crop slot, Spray 14 with PARAM = job 6..9, ...)
        char* fake[2] = { argv[0], argc > 8 ? argv[8] : nullptr };
        auto dir = ResolveGameDirectory(argc > 8 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        sim.SetGameDir(*dir);
        const EditResult r = ApplyTool(s, sim, static_cast<Tool>(std::atoi(argv[3])), std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6]));
        std::printf("%s %s\n", r.ok ? "ok" : "refused", r.message.c_str());
        return s.Save(argv[7], err) ? 0 : 1;
    }
    if (argc > 4 && std::strcmp(argv[1], "--autoplay") == 0)
    {
        // --autoplay FROM YEARS OUTDIR [folder]: the autoplayer runs the farm for YEARS game years (headless, with the
        // engine's route planner); a checkpoint save OUTDIR/APyyyy.SFM at every year change, its notes on the console
        char* fake[2] = { argv[0], argc > 5 ? argv[5] : nullptr };
        auto dir = ResolveGameDirectory(argc > 5 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err, true)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        sim.SetGameDir(*dir);
        Machines::smartDriving = true;
        Megadata mega;
        if (mega.Open(*dir, err))
            sim.SetTownLayout(mega.Read("townly0.cmp"));
        AutoPlayer ap(s, sim, argv[4]);
        const int endYear = s.cal.year + std::atoi(argv[3]);
        while (s.cal.year < endYear && !sim.Bankrupt() && sim.GameOverChoice() == 0)
        {
            sim.TimerTick();
            ap.Tick();
            std::string m;
            while (sim.TakeMessage(m))
                if (std::getenv("OSF_APVERBOSE"))
                    std::printf("  [%d/%d/%d] %s\n", s.cal.week + 1, s.cal.month + 1, s.cal.year, m.c_str());
            GameEvent ev;
            while (sim.TakeEvent(ev))
                if (!ap.Answer(ev))
                    std::printf("  [%d/%d/%d] box %d: %s\n", s.cal.week + 1, s.cal.month + 1, s.cal.year, ev.id, ev.text.c_str());
            while (ap.TakeNote(m))
                std::printf("  [%d/%d/%d] %s\n", s.cal.week + 1, s.cal.month + 1, s.cal.year, m.c_str());
            sim.TakeYearEvaluation();
        }
        std::printf("end %d/%d/%d: %d fields, town levels %d, cash %lld%s\n", s.cal.week + 1, s.cal.month + 1, s.cal.year, ap.Fields(),
                    sim.TownLevels(), static_cast<long long>(s.cash), sim.Bankrupt() ? " (bankrupt)" : "");
        return s.Save(std::string(argv[4]) + "/APEND.SFM", err) ? 0 : 1;
    }
    if (argc > 4 && std::strcmp(argv[1], "--newworld") == 0)
    {
        // --newworld SIZE SEED OUT [folder]: a generated world (SIZE 96 or 144) saved to OUT, for test farms
        char* fake[2] = { argv[0], argc > 5 ? argv[5] : nullptr };
        auto dir = ResolveGameDirectory(argc > 5 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        Megadata mega;
        GameState tmpl, s;
        std::string err;
        if (!mega.Open(*dir, err) || !tmpl.Load(*dir, ResolveFarm(*dir, "SCEN0.SSM"), err)) { std::printf("%s\n", err.c_str()); return 1; }
        WorldOptions opt;
        opt.mapSize = std::atoi(argv[2]) == kLargeMapSize ? kLargeMapSize : kOriginalMapSize;
        opt.seed = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
        opt.river = opt.lake = true;
        if (!GenerateWorld(*dir, mega, tmpl, opt, s, err)) { std::printf("%s\n", err.c_str()); return 1; }
        std::printf("world %d x %d, farm at plot %lld,%lld, town at %lld,%lld\n", s.world.size, s.world.size,
                    static_cast<long long>(s.scalars["farm_plot_x"]), static_cast<long long>(s.scalars["farm_plot_y"]),
                    static_cast<long long>(s.scalars["homestead_x"]), static_cast<long long>(s.scalars["homestead_y"]));
        return s.Save(argv[4], err) ? 0 : 1;
    }
    if (argc > 5 && std::strcmp(argv[1], "--editworld") == 0)
    {
        // --editworld SIZE SEED LIST OUT [folder]: a generated world changed as in the map editor, saved to OUT. LIST
        // lines: "farm COL ROW", "town COL ROW", "ground|water|trees|rocks X Y SIZE" (a brush of SIZE cells) or
        // "line ground|water|trees|rocks X0 Y0 X1 Y1 SIZE" (a brush dragged from one cell to the other)
        char* fake[2] = { argv[0], argc > 6 ? argv[6] : nullptr };
        auto dir = ResolveGameDirectory(argc > 6 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        Megadata mega;
        GameState tmpl, s;
        std::string err;
        if (!mega.Open(*dir, err) || !tmpl.Load(*dir, ResolveFarm(*dir, "SCEN0.SSM"), err)) { std::printf("%s\n", err.c_str()); return 1; }
        WorldOptions opt;
        opt.mapSize = std::atoi(argv[2]) == kLargeMapSize ? kLargeMapSize : kOriginalMapSize;
        opt.seed = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
        WorldSites sites;
        if (!GenerateTerrain(mega, tmpl, opt, s, sites, err)) { std::printf("%s\n", err.c_str()); return 1; }
        const std::map<std::string, LandBrush> brushes{ { "ground", LandBrush::Ground }, { "water", LandBrush::Water }, { "trees", LandBrush::Trees }, { "rocks", LandBrush::Rocks } };
        std::ifstream in(argv[4]);
        std::string line;
        uint32_t rnd = opt.seed | 1;
        int changed = 0, refused = 0;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            std::string first;
            if (!(ls >> first) || first[0] == '#')
                continue;
            std::string why;
            if (first == "farm" || first == "town")
            {
                int c = 0, r = 0;
                ls >> c >> r;
                if (!(first == "farm" ? PlaceFarm(s, sites, c, r, opt.wetness, why) : PlaceTown(s, sites, c, r, opt.wetness, why)))
                {
                    ++refused;
                    std::printf("refused: %s (%s)\n", line.c_str(), why.c_str());
                }
            }
            else if (first == "line")
            {
                std::string kind;
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0, size = 1;
                ls >> kind >> x0 >> y0 >> x1 >> y1 >> size;
                const int n = std::max({ std::abs(x1 - x0), std::abs(y1 - y0), 1 });
                if (brushes.count(kind))
                    for (int i = 0; i <= n; ++i)
                        changed += PaintLand(s, brushes.at(kind), x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, size, opt.wetness, rnd, sites);
            }
            else if (brushes.count(first))
            {
                int x = 0, y = 0, size = 1;
                ls >> x >> y >> size;
                changed += PaintLand(s, brushes.at(first), x, y, size, opt.wetness, rnd, sites);
            }
            else
                std::printf("unknown line: %s\n", line.c_str());
        }
        FinishWorld(*dir, mega, s, sites);
        std::printf("world %d x %d, %d cells changed, %d refused, farm at plot %d,%d, town at %d,%d\n", s.world.size, s.world.size, changed, refused,
                    sites.farmCol, sites.farmRow, sites.townCol, sites.townRow);
        return s.Save(argv[5], err) ? 0 : 1;
    }
    if (argc > 6 && std::strcmp(argv[1], "--cells") == 0)
    {
        // --cells SAVE X0 Y0 X1 Y1 [folder]: the map area as text, one row per y: ~ water, T tree, R rock, f field,
        // # fence or gate, U trough, = empty ditch, W full ditch, + your land (plain), . other plain ground, o anything else
        char* fake[2] = { argv[0], argc > 7 ? argv[7] : nullptr };
        auto dir = ResolveGameDirectory(argc > 7 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        const int x0 = std::atoi(argv[3]), y0 = std::atoi(argv[4]), x1 = std::atoi(argv[5]), y1 = std::atoi(argv[6]);
        for (int y = y0; y <= y1; ++y)
        {
            std::printf("%3d ", y);
            for (int x = x0; x <= x1; ++x)
            {
                if (!s.world.In(x, y)) { std::putchar(' '); continue; }
                const MapCell& c = s.world.At(x, y);
                const int t = c.BaseTile();
                char ch = 'o';
                if (c.base & 0x2000) ch = '~';
                else if (c.overlay & 0x1000) ch = 'f';
                else if (t >= 128 && t <= 147) ch = 'T';
                else if (t >= 535 && t <= 538) ch = 'R';
                else if (t >= 177 && t <= 191) ch = '#';
                else if (t == 175 || t == 176) ch = 'U';
                else if (t >= 15 && t <= 30) ch = (c.overlay & 0x800) ? '+' : '.';
                else if (t >= 87 && t <= 109) ch = (t >= 98 ? 'W' : '=');
                std::putchar(ch);
            }
            std::putchar('\n');
        }
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "--shoplist") == 0)
    {
        // --shoplist SAVE [folder]: the BUY window's items: catalogue record, name, price, cells covered
        char* fake[2] = { argv[0], argc > 3 ? argv[3] : nullptr };
        auto dir = ResolveGameDirectory(argc > 3 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        for (int cat = kShopMachinery; cat <= kShopSupplies; ++cat)
            for (int rec : ShopItems(cat))
            {
                int w = 1, h = 1;
                ShopFootprint(s, rec, w, h);
                std::printf("%3d  %-24s %7d  %dx%d%s\n", rec, ShopName(s, rec).c_str(), ShopPrice(s, rec), w, h, ShopNeedsPlacing(rec) ? "" : "  (not placed)");
            }
        return 0;
    }
    if (argc > 4 && std::strcmp(argv[1], "--applylist") == 0)
    {
        // --applylist FROM LIST TO: many tool clicks in one run; each LIST line is "TOOL X Y PARAM" (see --apply),
        // "buy RECORD X Y" for a BUY placement (see --buy), "plot COL ROW" to buy land or "cash N"; a refused line is
        // reported with its message
        char* fake[2] = { argv[0], argc > 5 ? argv[5] : nullptr };
        auto dir = ResolveGameDirectory(argc > 5 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        sim.SetGameDir(*dir);
        std::ifstream in(argv[3]);
        std::string line;
        int ok = 0, refused = 0;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            std::string first;
            if (!(ls >> first))
                continue;
            EditResult r;
            if (first == "buy")
            {
                int rec = 0, x = 0, y = 0;
                ls >> rec >> x >> y;
                r = ShopBuy(s, sim, rec, x, y);
            }
            else if (first == "plot")
            {
                // "plot COL ROW": buy a plot of land as the MAP window's BUY does
                int c = 0, rw = 0;
                ls >> c >> rw;
                std::string why;
                r.ok = sim.BuyPlot(c, rw, why);
                r.message = why;
            }
            else if (first == "cash")
            {
                // "cash N": set the funds (for test farms)
                long long v = 0;
                ls >> v;
                s.cash = v;
                r.ok = true;
            }
            else
            {
                int x = 0, y = 0, param = 0;
                ls >> x >> y >> param;
                r = ApplyTool(s, sim, static_cast<Tool>(std::atoi(first.c_str())), x, y, param);
            }
            if (r.ok)
                ++ok;
            else
            {
                ++refused;
                std::printf("refused: %s (%s)\n", line.c_str(), r.message.c_str());
            }
        }
        std::printf("%d done, %d refused, cash %lld\n", ok, refused, static_cast<long long>(s.cash));
        return s.Save(argv[4], err) ? 0 : 1;
    }
    if (argc > 6 && std::strcmp(argv[1], "--buy") == 0)
    {
        // --buy FROM RECORD X Y TO: the BUY window's item (catalogue record) placed at map cell (X, Y)
        char* fake[2] = { argv[0], argc > 7 ? argv[7] : nullptr };
        auto dir = ResolveGameDirectory(argc > 7 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        const EditResult r = ShopBuy(s, sim, std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));
        std::printf("%s %s\n", r.ok ? "bought" : "refused", r.message.c_str());
        return s.Save(argv[6], err) ? 0 : 1;
    }
    if (argc > 6 && std::strcmp(argv[1], "--plot") == 0)
    {
        // --plot FROM COL ROW buy|sell TO: buy or sell a land plot (MAP window, property mode)
        char* fake[2] = { argv[0], argc > 7 ? argv[7] : nullptr };
        auto dir = ResolveGameDirectory(argc > 7 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        std::string why;
        const int c = std::atoi(argv[3]), r = std::atoi(argv[4]);
        std::printf("land value %lld\n", static_cast<long long>(sim.PlotLandValue(c, r)));
        const bool ok = std::strcmp(argv[5], "sell") == 0 ? sim.SellPlot(c, r, why) : sim.BuyPlot(c, r, why);
        std::printf("%s %s\n", ok ? "done" : "refused", why.c_str());
        return s.Save(argv[6], err) ? 0 : 1;
    }
    if (argc > 4 && std::strcmp(argv[1], "--bank") == 0)
    {
        // --bank FROM AMOUNT|repay TO: the Bank window's loan (keypad amount, OK) or REPAY
        char* fake[2] = { argv[0], argc > 5 ? argv[5] : nullptr };
        auto dir = ResolveGameDirectory(argc > 5 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        std::printf("credit %lld interest %d debt %lld\n", static_cast<long long>(sim.CreditLimit()), sim.InterestRate(), static_cast<long long>(sim.LoanBalance()));
        const bool ok = std::strcmp(argv[3], "repay") == 0 ? sim.RepayAll() : sim.Borrow(std::atoll(argv[3]));
        std::printf("%s credit %lld payment %lld\n", ok ? "done" : "refused", static_cast<long long>(sim.CreditLimit()), static_cast<long long>(sim.LoanPayment()));
        return s.Save(argv[4], err) ? 0 : 1;
    }
    if (argc > 5 && std::strcmp(argv[1], "--sell") == 0)
    {
        // --sell FROM KIND INDEX TO: the SELL window's KIND list (0 crops, 1 machines, 2 animals), entry INDEX
        char* fake[2] = { argv[0], argc > 6 ? argv[6] : nullptr };
        auto dir = ResolveGameDirectory(argc > 6 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        const auto list = SaleList(s, sim, std::atoi(argv[3]));
        for (const auto& it : list)
            std::printf("  %s %lld\n", it.label.c_str(), static_cast<long long>(it.value));
        const size_t k = static_cast<size_t>(std::atoi(argv[4]));
        if (k < list.size())
        {
            const EditResult r = ShopSell(s, sim, list[k]);
            std::printf("%s %s\n", r.ok ? "sold" : "refused", r.message.c_str());
        }
        return s.Save(argv[5], err) ? 0 : 1;
    }
    if (argc > 4 && std::strcmp(argv[1], "--contract") == 0)
    {
        // --contract FROM FIELD TO: the schedule window's chart tool on today's week of FIELD
        char* fake[2] = { argv[0], argc > 5 ? argv[5] : nullptr };
        auto dir = ResolveGameDirectory(argc > 5 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        Simulation sim(s);
        Field f = s.FieldAt(std::atoi(argv[3]));
        std::printf("%s\n", sim.LockContract(f) ? "locked" : "refused");
        return s.Save(argv[4], err) ? 0 : 1;
    }
    if (argc > 3 && std::strcmp(argv[1], "--compare") == 0)
    {
        char* fake[2] = { argv[0], argc > 4 ? argv[4] : nullptr };
        auto dir = ResolveGameDirectory(argc > 4 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        return CompareSaves(*dir, argv[2], argv[3]);
    }
    if (argc > 1 && std::strcmp(argv[1], "--newgametest") == 0)
    {
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        return NewGameTest(*dir);
    }
    if (argc > 1 && std::strcmp(argv[1], "--mapedittest") == 0)
    {
        // the map editor's land changes and placing of the farm and the town (game/MapEdit.h)
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        return MapEditSelfTest(*dir);
    }
    if (argc > 1 && std::strcmp(argv[1], "--toolstest") == 0)
    {
        char* fake[2] = { argv[0], argc > 2 ? argv[2] : nullptr };
        auto dir = ResolveGameDirectory(argc > 2 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        int result = 0;
        for (int i = 0; i < 8; ++i)
            result |= ToolsTest(*dir, "SCEN" + std::to_string(i) + ".SSM");
        return result;
    }
    if (argc > 2 && std::strcmp(argv[1], "--animals") == 0)
    {
        // --animals SAVE [folder]: the date and every animal (index, cell, species, condition, thirst, hunger, age)
        char* fake[2] = { argv[0], argc > 3 ? argv[3] : nullptr };
        auto dir = ResolveGameDirectory(argc > 3 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        std::printf("month %d week %d year %d count %d\n", s.cal.month, s.cal.week, s.cal.year, s.animalCount);
        for (int i = 1; i < 256; ++i)
        {
            const size_t b = static_cast<size_t>(i) * 32;
            if (Rd8(s.animals, b + 2) & 0x20)
            {
                const int x = Rd8(s.animals, b), y = Rd8(s.animals, b + 1);
                std::printf("%d %d,%d sp %X cond %d thirst %d hunger %d age %d flags %X walk %d heading %d\n", i, x, y, Rd16(s.animals, b + 6),
                            Rd8(s.animals, b + 8), Rd8(s.animals, b + 0x0D), Rd8(s.animals, b + 0x0E), Rd16(s.animals, b + 0x0A),
                            Rd8(s.animals, b + 2), Rd8(s.animals, b + 5), Rd8(s.animals, b + 4));
                if (std::getenv("OSF_AROUND"))
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (x + dx >= 0 && y + dy >= 0 && x + dx < s.world.size && y + dy < s.world.size)
                                std::printf("   (%d,%d) base %d obj %d ov %d\n", x + dx, y + dy, s.world.At(x + dx, y + dy).BaseTile(),
                                            s.world.At(x + dx, y + dy).HasObject(), s.world.At(x + dx, y + dy).overlay & 0x7FF);
            }
        }
        // the water towers: cell and level
        for (int i = 1; i < 127; ++i)
        {
            const size_t sb = static_cast<size_t>(i) * 70;
            if ((Rd8(s.structures, sb + 2) & 0x20) && Rd16(s.structures, sb) == 0x48)
                std::printf("tower %d at %d,%d level %d\n", i, Rd8(s.structures, sb + 4), Rd8(s.structures, sb + 5), Rd16(s.structures, sb + 0x0A));
        }
        // the troughs (item type 299) and hay piles (300): cell and amount
        for (int i = 1; i < 512; ++i)
        {
            const size_t ib = static_cast<size_t>(i) * 12;
            const int type = Rd16(s.items, ib);
            if ((Rd8(s.items, ib + 10) & 0x20) && (type == 299 || type == 300))
            {
                const int ix = Rd8(s.items, ib + 8), iy = Rd8(s.items, ib + 9);
                const bool in = ix < s.world.size && iy < s.world.size;
                std::printf("%s %d at %d,%d amount %d (cell base %d overlay %d)\n", type == 299 ? "trough" : "hay", i, ix, iy, Rd16(s.items, ib + 2),
                            in ? s.world.At(ix, iy).base : -1, in ? s.world.At(ix, iy).overlay : -1);
            }
        }
        // and the traffic: index, cell, heading, kind, flags, base word and overlay picture of the cell
        std::printf("traffic %d\n", s.carCount);
        for (int i = 0; i < 16; ++i)
        {
            const size_t b = static_cast<size_t>(i) * 8;
            if (!(Rd8(s.traffic, b) & 1))
                continue;
            const int x = Rd16(s.traffic, b + 2), y = Rd16(s.traffic, b + 4);
            const MapCell c = x < s.world.size && y < s.world.size ? s.world.At(x, y) : MapCell{};
            std::printf("car %d %d,%d heading %d kind %d flags %d base %d ov %d\n", i, x, y, Rd8(s.traffic, b + 6), Rd8(s.traffic, b + 7),
                        Rd8(s.traffic, b), c.base, c.overlay & 0x7FF);
        }
        return 0;
    }

    if (argc > 2 && std::strcmp(argv[1], "--town") == 0)
    {
        // --town SAVE [folder]: the town's plots (owner 2: column, row, type, level) and its home plot
        char* fake[2] = { argv[0], argc > 3 ? argv[3] : nullptr };
        auto dir = ResolveGameDirectory(argc > 3 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        std::printf("map %d, plots %d, town home %d,%d, farm home %d,%d, date %d/%d\n", s.world.size, s.Plots(), static_cast<int>(s.scalars["homestead_x"]),
                    static_cast<int>(s.scalars["homestead_y"]), static_cast<int>(s.scalars["farm_plot_x"]), static_cast<int>(s.scalars["farm_plot_y"]), s.cal.month + 1, s.cal.year);
        int n = 0;
        for (int r = 0; r < s.Plots(); ++r)
            for (int c = 0; c < s.Plots(); ++c)
                if (s.PlotOwner(c, r) == 2)
                {
                    std::printf("town plot %d,%d type %d level %d\n", c, r, s.PlotType(c, r) & 15, s.PlotType(c, r) >> 4);
                    ++n;
                }
        std::printf("%d town plots\n", n);
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "--fields") == 0)
    {
        // --fields SAVE [folder]: list the fields in use (state, crop slot and name, position, and the growth values:
        // sub-state, age, phase, stress, stage tile)
        char* fake[2] = { argv[0], argc > 3 ? argv[3] : nullptr };
        auto dir = ResolveGameDirectory(argc > 3 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        GameState s;
        std::string err;
        if (!s.Load(*dir, argv[2], err)) { std::printf("%s\n", err.c_str()); return 1; }
        for (int i = 1; i < 98; ++i)
        {
            const Field f = s.FieldAt(i);
            if (f.InUse())
                std::printf("field %d state %d crop %d %s at %d,%d sub %d age %d phase %d stress %d tile %d status 0x%x\n", i, f.State(), f.Crop() & 15,
                            s.crops[f.Crop() & 15].name.c_str(), f.X(), f.Y(), f.Sub(), f.Age(), f.Phase(), f.Stress(), f.Get16(0x0E), f.Status());
        }
        if (const char* tl = std::getenv("OSF_TIMELINE"))
        {
            // OSF_TIMELINE=n: field n's 240 schedule codes, one row of 48 weeks per line
            const int n = std::atoi(tl);
            for (int r = 0; r < 240; ++r)
                std::printf("%s%d", r % 48 == 0 ? (r ? "\n" : "") : " ", s.ScheduleCode(r, n));
            std::printf("\n");
        }
        return 0;
    }

    if (argc > 2 && std::strcmp(argv[1], "--simulate") == 0)
    {
        char* fake[2] = { argv[0], argc > 4 ? argv[4] : nullptr };
        auto dir = ResolveGameDirectory(argc > 4 ? 2 : 1, fake);
        if (!dir) { Fatal("game folder unknown", false); return 1; }
        return SimulateHeadless(*dir, argc > 3 ? argv[3] : "SCEN0.SSM", std::atoi(argv[2]));
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // the language: the one chosen before (settings.txt), else the system's when the program has it
    {
        std::ifstream in(std::filesystem::path(ConfigDir()) / "settings.txt");
        std::string line, lang;
        while (std::getline(in, line))
            if (line.rfind("language=", 0) == 0)
                lang = line.substr(9);
        while (!lang.empty() && (lang.back() == '\r' || lang.back() == ' '))
            lang.pop_back();
        SetLanguage(lang.empty() ? SystemLanguage() : lang);
    }
    std::optional<std::string> gameDir;
#ifndef __EMSCRIPTEN__
    if (reconfigure)
    {
        // the folder box at once; a farm or save given without a folder stays the farm (after the chosen folder)
        if (argc > 1 && !IsGameDirectory(argv[1]))
        {
            args.insert(args.begin() + 1, argv[0]); // the folder's place, filled below
            argc = static_cast<int>(args.size()) - 1;
            argv = args.data();
        }
        const std::string now = RememberedGameDirectory();
        const std::string ask = now.empty() ? std::string(Tr("Choose the SimFarm files to play with."))
                                            : TrF("OpenSimFarm now plays with the SimFarm files in\n{0}", now);
        bool noDialog = false;
        gameDir = AskPlayerForGameFolder(ask, now.empty() ? N_("Quit") : N_("Keep this folder"), {}, noDialog);
        if (noDialog)
            std::fprintf(stderr, "%s\n", Tr(kNoDialogText));
        if (gameDir && argc > 1)
            argv[1] = gameDir->data(); // the chosen folder in the folder's place
        else if (!gameDir && argc > 1 && argv[1] == argv[0])
            argv[1] = const_cast<char*>(""); // kept folder: no folder given, the farm stays argv[2]
        if (!gameDir && now.empty())
        {
            SDL_Quit();
            return 1;
        }
    }
#endif
    if (!gameDir)
        gameDir = ResolveGameDirectory(argc, argv);
    if (!gameDir)
    {
        // not given and not remembered: try the program's folder, the current folder and a SIMFARM folder beside either
        // (so a double-click works when the program is put into, or next to, the SimFarm folder); a hit is remembered
        std::vector<std::filesystem::path> bases;
        if (char* b = SDL_GetBasePath()) { bases.emplace_back(b); SDL_free(b); }
        std::error_code ec;
        bases.push_back(std::filesystem::current_path(ec));
        for (const auto& b : bases)
            for (const auto& c : { b, b / "SIMFARM", b.parent_path() / "SIMFARM" })
            {
                if (gameDir || !IsGameDirectory(c.string())) continue;
                std::string s = c.string();
                char* fake[2] = { argv[0], s.data() };
                gameDir = ResolveGameDirectory(2, fake);
            }
    }
    // The folder is checked in full before it is used (the archive opens, the tile sheets and palette load, the
    // starting farm loads); a folder that fails, was moved or was deleted leads to the search and then to the player,
    // with the reason, until a usable one is chosen or the player quits.
    Megadata mega;
    std::string err;
    MapGraphics gfx;
    std::string problem; // why no folder could be used yet (shown in the box)
    {
        const std::string remembered = RememberedGameDirectory();
        if (!gameDir && !remembered.empty())
            problem = TrF("SimFarm was in\n{0}\nbut it is no longer there (moved or deleted?).", remembered);
    }
    bool searched = false;
    std::vector<std::string> failed; // folders that did not pass the full check
    for (;;)
    {
        if (!gameDir && !searched)
        {
            searched = true; // once (it reads many folders)
            if (const auto found = SearchGameDirectory(failed))
                gameDir = found;
        }
        if (!gameDir)
        {
            bool noDialog = false;
            gameDir = AskPlayerForGameFolder(problem, N_("Quit"), failed, noDialog);
            if (!gameDir)
            {
                if (noDialog)
                    Fatal(Tr(kNoDialogText));
                SDL_Quit();
                return 1;
            }
        }
        // the full check
        mega = Megadata();
        gfx = MapGraphics();
        err.clear();
        bool ok = mega.Open(*gameDir, err) && LoadMapGraphics(mega, gfx);
        if (ok)
        {
            GameState probe;
            const auto scen = FindFileNoCase(*gameDir, "SCEN0.SSM");
            ok = scen && probe.Load(*gameDir, *scen, err);
            if (!ok && !scen)
                err = Tr("SCEN0.SSM is missing.");
        }
        else if (err.empty())
            err = Tr("The tile sheets or palette are missing from MEGADATA.EEA.");
        if (ok)
            break;
        problem = TrF("The SimFarm folder\n{0}\ncannot be used: {1}", *gameDir, err);
        failed.push_back(*gameDir);
        gameDir.reset();
    }
    if (*gameDir != RememberedGameDirectory())
        RememberGameDirectory(*gameDir); // a folder found, chosen or given is remembered once it works

    auto state = std::make_unique<GameState>();
    std::string cropArtSig; // the crop files whose pictures are in the tile textures
    std::string farmName = argc > 2 ? argv[2] : "SCEN0.SSM";
    if (!state->Load(*gameDir, ResolveFarm(*gameDir, farmName), err))
    {
        Fatal("Could not load the farm " + farmName + "\n" + err);
        SDL_Quit();
        return 1;
    }
    auto sim = std::make_unique<Simulation>(*state);
    sim->SetGameDir(*gameDir);
    sim->SetScaledClock(true); // speeds change how fast everything moves (README)
    sim->SetSpeed(Speed::Normal); // a loaded game goes on at Normal, as after the original's Load dialog
                    sim->SetTownLayout(mega.Read("townly0.cmp"));

    // start at the largest whole multiple of 640x480 that fits the desktop
    int uiScale = 1;
    SDL_Texture* frameTex = nullptr; // the offscreen frame for a window too small for the chosen scale
    int frameW = 0, frameH = 0;
    SDL_DisplayMode desktop;
    if (SDL_GetDesktopDisplayMode(0, &desktop) == 0)
        while (640 * (uiScale + 1) <= desktop.w * 9 / 10 && 480 * (uiScale + 1) <= desktop.h * 9 / 10)
            uiScale++;
    // the window holds the 640 x 480 screen at the scale plus the frame (2 window pixels left, right and below, Q24)
    SDL_Window* window = SDL_CreateWindow("OpenSimFarm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640 * uiScale + 4, 480 * uiScale + 2,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window)
    {
        // the program's own icon (title bar, task bar, window switcher)
        if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(kAppIcon), kAppIconSize, kAppIconSize,
                                                                   32, kAppIconSize * 4, SDL_PIXELFORMAT_RGBA32))
        {
            SDL_SetWindowIcon(window, icon);
            SDL_FreeSurface(icon);
        }
        // the window may not shrink below 1280 x 720 (user finding F19: menus and windows got too small to use; 720 rows
        // hold the 400 interface rows at 1.8x), or below what the screen holds when it is smaller than that
        int minW = 1280, minH = 720;
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        {
            minW = std::max(640, std::min(minW, usable.w));
            minH = std::max(400, std::min(minH, usable.h - 32)); // room for the title bar
        }
#ifndef __EMSCRIPTEN__
        SDL_SetWindowMinimumSize(window, minW, minH); // in the browser the page sizes the game (it scales to fit)
#endif
        // the frame at the sides and bottom is a resize handle (a few pixels more at the bottom corners)
        SDL_SetWindowHitTest(
            window,
            [](SDL_Window* win, const SDL_Point* p, void*) -> SDL_HitTestResult {
                int w, h;
                SDL_GetWindowSize(win, &w, &h);
                constexpr int edge = 2, corner = 12;
                const bool l = p->x < edge, r = p->x >= w - edge, t = p->y < edge, b = p->y >= h - edge;
                const bool nl = p->x < corner, nr = p->x >= w - corner, nt = p->y < corner, nb = p->y >= h - corner;
                (void)t;
                (void)nt;
                if ((b && nl) || (l && nb)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
                if ((b && nr) || (r && nb)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
                if (b) return SDL_HITTEST_RESIZE_BOTTOM; // no frame and no resizing along the top (user)
                if (l) return SDL_HITTEST_RESIZE_LEFT;
                if (r) return SDL_HITTEST_RESIZE_RIGHT;
                return SDL_HITTEST_NORMAL;
            },
            nullptr);
    }
    // the game reads keys, never composed text: with text input on, an input method (IBus) can take the letter
    // keys, and WASD would not scroll
    SDL_StopTextInput();
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer && window)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    // destroyed after everything declared below, so textures are released before their renderer
    struct SdlCleanup
    {
        SDL_Window* w;
        SDL_Renderer* r;
        ~SdlCleanup()
        {
            if (r) SDL_DestroyRenderer(r);
            if (w) SDL_DestroyWindow(w);
            SDL_Quit();
        }
    } cleanup{ window, renderer };
    MapView view;
    if (!renderer || !view.Init(renderer, gfx))
    {
        Fatal(TrF("Cannot create the window: {0}", SDL_GetError()));
        return 1;
    }
    view.SetWorld(&state->world);
    view.SetTopLeft(static_cast<int>(state->scalars["view_x"]), static_cast<int>(state->scalars["view_y"]));

    Gfx gfxUi;
    if (!gfxUi.Init(renderer, mega, gfx))
    {
        Fatal(Tr("The interface pictures or the font are missing from MEGADATA.EEA."));
        return 1;
    }
    // interface settings of the last session (settings.txt in the settings folder: key=value lines)
    std::map<std::string, int> settings;
    {
        std::ifstream in(std::filesystem::path(ConfigDir()) / "settings.txt");
        std::string line;
        while (std::getline(in, line))
            if (const auto eq = line.find('='); eq != std::string::npos)
                settings[line.substr(0, eq)] = std::atoi(line.c_str() + eq + 1);
    }
    if (settings.count("scale"))
        uiScale = std::clamp(settings["scale"], 1, 4);
    gfxUi.SetScale(uiScale);
    SoundPlayer sound;
    if (!sound.Init(mega))
        std::fprintf(stderr, "OpenSimFarm: no audio device, sound effects are off (%s)\n", SDL_GetError());
    GameUi ui(gfxUi);
    // the phone layout: the browser on a touch screen, or OPENSIMFARM_PHONE=1 (to try it on a desktop)
#ifdef __EMSCRIPTEN__
    const bool phone = PhoneLayoutWanted() != 0;
#else
    const bool phone = std::getenv("OPENSIMFARM_PHONE") && std::string(std::getenv("OPENSIMFARM_PHONE")) == "1";
#endif
    ui.SetPhone(phone);
#ifdef __EMSCRIPTEN__
    if (phone)
        ui.SetAskText([](const std::string& title, std::string& text) {
            char buf[64] = {};
            if (!AskTextPrompt(title.c_str(), text.c_str(), buf, sizeof buf))
                return false;
            text = buf;
            return true;
        });
#endif
    ui.SetGameDir(*gameDir);
    ui.onSound = [&](const std::string& name) { sound.Play(name); };
    ui.soundEnabled = [&] { return sound.Enabled(); };
    ui.musicEnabled = [&] { return sound.MusicEnabled(); };
    if (settings.count("sfx"))
        sound.SetEnabled(settings["sfx"] != 0);
    if (settings.count("music"))
        sound.SetMusicEnabled(settings["music"] != 0);
    sound.SetModern(!settings.count("modernsound") || settings["modernsound"] != 0); // on unless turned off
    // on unless turned off; "moderngraphics" is the setting's earlier name (the option was renamed Modern Render)
    {
        const char* key = settings.count("modernrender") ? "modernrender" : "moderngraphics";
        view.SetModern(!settings.count(key) || settings[key] != 0);
    }
    if (settings.count("musicvolume"))
        sound.SetMusicVolume(settings["musicvolume"]);
    if (settings.count("sfxvolume"))
        sound.SetEffectsVolume(settings["sfxvolume"]);
    ui.SetAudio(&sound);
    if (settings.count("messages"))
        ui.SetMessagesOn(settings["messages"] != 0);
    if (settings.count("toolnames"))
        ui.SetToolNamesOn(settings["toolnames"] != 0);
    Machines::smartDriving = !settings.count("smartdriving") || settings["smartdriving"] != 0; // on unless turned off
    ui.SetContext(state.get(), sim.get(), &view);
    ui.SetCurrentSave(ResolveFarm(*gameDir, farmName));
    // test aid (OPENSIMFARM_FLY=1): a crop duster bought, fuelled, loaded and in the air at once, to look at the flight
    // panel (the farm must have an airport, e.g. SCEN0.SSM)
    if (const char* fly = std::getenv("OPENSIMFARM_FLY"); fly && std::string(fly) == "1" && sim)
    {
        Flight& f = sim->Plane();
        const std::string why = f.Buy();
        int plane = 0;
        for (int i = 1; i < 127 && !plane; ++i)
            if (Rd16(state->machines, static_cast<size_t>(i) * 56) == 8 && (Rd8(state->machines, static_cast<size_t>(i) * 56 + 2) & 0x20))
                plane = i;
        if (!plane || !f.Refuel(plane) || !f.Reload(plane) || !f.Start(plane))
            std::fprintf(stderr, "OPENSIMFARM_FLY: no flight (%s)\n", why.c_str());
    }

    bool running = true;
    // without a farm on the command line the game starts at the Select-a-Region screen
    bool atStart = argc <= 2;
    StartScreen start(gfxUi, *gameDir);
    start.SetPhone(phone);
    // the intro pictures come before the first region screen of a run; a farm opened from the command line has
    // skipped them, and File > New Game later goes straight to the region screen, as in the original
    if (!atStart)
        start.SkipIntro();
    Uint64 last = SDL_GetPerformanceCounter();
    std::string lastTitle;
    auto loadFarm = [&](const std::string& name, bool announce = false) {
        auto next = std::make_unique<GameState>();
        const std::string path = ResolveFarm(*gameDir, name);
        if (path.empty() || !next->Load(*gameDir, path, err))
        {
            ui.ShowMessage(TrF("Could not load {0}", name));
            return;
        }
        farmName = name;
        // the session's unsaved values carry over to the loaded game, as in the original
        auto nextSim = std::make_unique<Simulation>(*next);
        if (sim)
            nextSim->InheritSession(*sim);
        state = std::move(next);
        sim = std::move(nextSim);
        sim->SetGameDir(*gameDir);
        sim->SetScaledClock(true); // speeds change how fast everything moves (README)
        sim->SetSpeed(Speed::Normal); // the original's Load dialog resumes at Normal, whatever the save holds
        sim->SetTownLayout(mega.Read("townly0.cmp"));
        view.SetWorld(&state->world);
        // engine choice: show the loaded game's own saved view (the original keeps the view where it was, which
        // makes saves of one farm look alike)
        view.SetTopLeft(static_cast<int>(state->scalars["view_x"]), static_cast<int>(state->scalars["view_y"]));
        ui.SetContext(state.get(), sim.get(), &view);
        ui.SetCurrentSave(path);
        // the original shows nothing after a load or a start from the region screen; the engine-only quick load says so
        if (announce)
            ui.ShowMessage(TrF("Loaded {0}", std::filesystem::path(name).filename().string()));
    };
    ui.onCommand = [&](const std::string& cmd) {
        if (cmd == "quit")
            running = false;
        else if (cmd == "save")
        {
            const auto dir = std::filesystem::path(ConfigDir()) / "saves";
            std::filesystem::create_directories(dir);
            const auto path = (dir / "QUICKSAVE.SFM").string();
            ui.ShowMessage(state->Save(path, err) ? TrF("Saved {0}", path) : TrF("Save failed: {0}", err));
        }
        else if (cmd == "load")
            loadFarm("QUICKSAVE.SFM", true);
        else if (cmd.rfind("language:", 0) == 0)
        {
            // Options > Language: the menus are rebuilt, everything else is translated as it is drawn; settings.txt keeps it
            SetLanguage(cmd.substr(9));
            ui.LanguageChanged();
        }
        else if (cmd.rfind("loadpath:", 0) == 0)
        {
            ui.loadFromStart = false;
            loadFarm(cmd.substr(9));
        }
        else if (cmd.rfind("farm:", 0) == 0)
        {
            loadFarm("SCEN" + cmd.substr(5) + ".SSM");
            ui.OpenWindow(kWinMap); // every start from the region screen opens the MAP window
        }
        else if (cmd.rfind("scale:", 0) == 0)
        {
            uiScale = std::clamp(std::stoi(cmd.substr(6)), 1, 4);
            gfxUi.SetScale(uiScale);
        }
        else if (cmd == "sfx")
            sound.SetEnabled(!sound.Enabled());
        else if (cmd == "music")
            sound.SetMusicEnabled(!sound.MusicEnabled());
        else if (cmd == "modernsound")
        {
            sound.SetModern(!sound.Modern());
            ui.ShowMessage(sound.Modern() ? Tr("Modern sound on: cleaner effects placed on the map, new instruments for the music.")
                                          : Tr("Modern sound off: the sound as the original plays it."));
        }
        else if (cmd == "modernrender")
        {
            view.SetModern(!view.Modern());
            ui.ShowMessage(view.Modern() ? Tr("Modern render on: smoothed pictures, and animals, machines and cars glide from cell to cell.")
                                         : Tr("Modern render off: the pictures and the movement as the original draws them."));
        }
        else if (cmd == "newgame")
            atStart = true;
    };

    // A window too small for a screen at its whole-number scale gets a fractional one: the frame is drawn at the
    // next whole scale into an offscreen picture of at least minW x minH screen pixels, which is shrunk smoothly to
    // the window; mouse positions are converted to match (MouseRatio, also for HiDPI windows).
    // beginFrame returns the scale to draw at and the frame's size; endFrame shows it.
    // The interface sits inside a frame of kBorder window pixels round the whole window (an engine addition, user
    // finding Q24); it is always drawn into an offscreen picture of the inner size, which endFrame shows inset.
    constexpr int kBorder = 2; // window pixels (user: 6 too much, then 2)
    SDL_Rect inner{ 0, 0, 0, 0 };
    auto beginFrame = [&](float maxScale, int minW, int minH, int& w, int& h, bool& offscreen) -> int {
        int ow, oh;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        int ww = ow, wh = oh;
        SDL_GetWindowSize(window, &ww, &wh);
        const float hd = static_cast<float>(ow) / static_cast<float>(std::max(1, ww)); // output pixels per window pixel
        const int bo = static_cast<int>(std::lround(kBorder * hd));
        inner = { bo, 0, std::max(1, ow - 2 * bo), std::max(1, oh - bo) }; // no frame along the top (user)
        const float fit = std::max(1.0f, std::min({ maxScale, inner.w / static_cast<float>(minW), inner.h / static_cast<float>(minH) }));
        const int whole = static_cast<int>(std::ceil(fit - 0.001f));
        const bool fractional = fit < static_cast<float>(whole) - 0.001f;
        const int fw = fractional ? static_cast<int>(std::ceil(inner.w * whole / fit)) : inner.w;
        const int fh = fractional ? static_cast<int>(std::ceil(inner.h * whole / fit)) : inner.h;
        if (!frameTex || frameW != fw || frameH != fh)
        {
            if (frameTex)
                SDL_DestroyTexture(frameTex);
            frameTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, fw, fh);
            if (frameTex)
                SDL_SetTextureScaleMode(frameTex, fractional ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
            frameW = fw;
            frameH = fh;
        }
        offscreen = frameTex && SDL_SetRenderTarget(renderer, frameTex) == 0;
        w = offscreen ? fw : ow, h = offscreen ? fh : oh;
        SetMouseOffset(offscreen ? kBorder : 0);
        SetMouseRatio(offscreen ? static_cast<float>(fw) / (static_cast<float>(inner.w) / hd) : hd);
        return fractional && offscreen ? whole : std::max(1, static_cast<int>(fit));
    };
    auto endFrame = [&](bool offscreen) {
        if (offscreen)
        {
            SDL_SetRenderTarget(renderer, nullptr);
            int ow, oh;
            SDL_GetRendererOutputSize(renderer, &ow, &oh);
            // the frame: light grey with a raised bevel outside and a sunken line next to the picture
            const int bo = inner.x;
            SDL_SetRenderDrawColor(renderer, 195, 195, 195, 255);
            SDL_RenderClear(renderer);
            const int line = std::max(1, bo / 2);
            auto band = [&](int x, int y, int bw, int bh, Uint8 c) {
                SDL_SetRenderDrawColor(renderer, c, c, c, 255);
                const SDL_Rect r{ x, y, bw, bh };
                SDL_RenderFillRect(renderer, &r);
            };
            band(0, 0, line, oh, 255);                    // outer bevel: light left, dark bottom and right
            band(0, oh - line, ow, line, 130);
            band(ow - line, 0, line, oh, 130);
            band(bo - line, 0, line, inner.h + line, 130); // inner sunken edge: dark left, light bottom and right
            band(bo - line, inner.h, inner.w + 2 * line, line, 255);
            band(bo + inner.w, 0, line, inner.h + line, 255);
            SDL_RenderCopy(renderer, frameTex, nullptr, &inner);
        }
        SDL_RenderPresent(renderer);
#ifdef __EMSCRIPTEN__
        WaitForNextFrame();
#endif
    };
    // window positions to positions in the drawn frame
    auto convertMouse = [](SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            // a window manager that ignores the minimum size (or none at all): grow the window back to it
            if (SDL_Window* win = SDL_GetWindowFromID(e.window.windowID))
            {
                int mw, mh;
                SDL_GetWindowMinimumSize(win, &mw, &mh);
                if (e.window.data1 < mw || e.window.data2 < mh)
                    SDL_SetWindowSize(win, std::max(mw, static_cast<int>(e.window.data1)), std::max(mh, static_cast<int>(e.window.data2)));
            }
        }
        const float k = MouseRatio();
        const int off = MouseOffset();
        if (k == 1.0f && off == 0)
            return;
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
        {
            e.button.x = static_cast<Sint32>(std::floor((e.button.x - off) * k));
            e.button.y = static_cast<Sint32>(std::floor(e.button.y * k));
        }
        else if (e.type == SDL_MOUSEMOTION)
        {
            e.motion.x = static_cast<Sint32>(std::floor((e.motion.x - off) * k));
            e.motion.y = static_cast<Sint32>(std::floor(e.motion.y * k));
            e.motion.xrel = static_cast<Sint32>(std::lround(e.motion.xrel * k));
            e.motion.yrel = static_cast<Sint32>(std::lround(e.motion.yrel * k));
        }
    };
    // Touch screens (the browser version on a phone or tablet). SDL turns the first finger into mouse events (marked
    // SDL_TOUCH_MOUSEID), which is what the windows, menus, buttons and palette get. A touch that starts on the free
    // farm map is read as gestures instead (ui/Touch.h): one finger pans, a tap clicks, a held press right-clicks,
    // two fingers pan and pinch-zoom. The clicks are put back into the queue as mouse events of their own (kTapMouse).
    // A browser also sends its own mouse events after a touch; those are dropped for a moment after each touch.
    constexpr Uint32 kTapMouse = 0x7A7A;
    TouchGestures touch;
    touch.smooth = true; // a pinch zooms with the fingers (user 2026-11-11: the zoom steps were "FAR from smooth")
    bool touchMap = false;
    Uint32 lastTouch = 0, touchWindow = 0;
    auto isMouse = [](const SDL_Event& e) {
        return e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEWHEEL;
    };
    auto mouseWhich = [](const SDL_Event& e) {
        return e.type == SDL_MOUSEMOTION ? e.motion.which : e.type == SDL_MOUSEWHEEL ? e.wheel.which : e.button.which;
    };
    auto isFinger = [](const SDL_Event& e) {
        return e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP;
    };
    // true when the event is a browser's duplicate of a touch (to be dropped)
    auto touchEcho = [&](const SDL_Event& e) {
        if (isFinger(e))
            lastTouch = SDL_GetTicks();
        return isMouse(e) && mouseWhich(e) != SDL_TOUCH_MOUSEID && mouseWhich(e) != kTapMouse && lastTouch &&
               SDL_GetTicks() - lastTouch < 700;
    };
    auto pushClick = [&](Uint8 button, float wx, float wy) {
        const Uint32 id = touchWindow;
        SDL_Event m{};
        m.type = SDL_MOUSEMOTION;
        m.motion.windowID = id;
        m.motion.which = kTapMouse;
        m.motion.x = static_cast<Sint32>(wx);
        m.motion.y = static_cast<Sint32>(wy);
        SDL_PushEvent(&m);
        for (Uint32 type : { static_cast<Uint32>(SDL_MOUSEBUTTONDOWN), static_cast<Uint32>(SDL_MOUSEBUTTONUP) })
        {
            SDL_Event b{};
            b.type = type;
            b.button.windowID = id;
            b.button.which = kTapMouse;
            b.button.button = button;
            b.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
            b.button.clicks = 1;
            b.button.x = static_cast<Sint32>(wx);
            b.button.y = static_cast<Sint32>(wy);
            SDL_PushEvent(&b);
        }
    };
    // a held press that moves the tool: the mouse's press, motion with the button held, and release
    auto pushMouse = [&](Uint32 type, float wx, float wy) {
        SDL_Event m{};
        m.type = type;
        if (type == SDL_MOUSEMOTION)
        {
            m.motion.windowID = touchWindow;
            m.motion.which = kTapMouse;
            m.motion.state = SDL_BUTTON_LMASK;
            m.motion.x = static_cast<Sint32>(wx);
            m.motion.y = static_cast<Sint32>(wy);
        }
        else
        {
            m.button.windowID = touchWindow;
            m.button.which = kTapMouse;
            m.button.button = SDL_BUTTON_LEFT;
            m.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
            m.button.clicks = 1;
            m.button.x = static_cast<Sint32>(wx);
            m.button.y = static_cast<Sint32>(wy);
        }
        SDL_PushEvent(&m);
    };
    // The SimFarm folder may be moved or deleted while the game runs (the pictures and sounds are in memory, but farms,
    // crop files and new worlds are read from it): it is looked at every 2 seconds, and when it is gone the game
    // stops and asks for its new place. The farm in memory is kept; "Go on without" plays on until a file is needed.
    Uint64 lastFolderCheck = SDL_GetTicks64();
    bool folderLostAsked = false;
    auto checkGameFolder = [&] {
        if (SDL_GetTicks64() - lastFolderCheck < 2000)
            return;
        lastFolderCheck = SDL_GetTicks64();
        if (IsGameDirectory(*gameDir))
        {
            folderLostAsked = false;
            return;
        }
        if (folderLostAsked)
            return;
        folderLostAsked = true;
        bool noDialog = false;
        const auto dir = AskPlayerForGameFolder(TrF("The SimFarm folder\n{0}\nhas been moved or deleted. Your farm is kept.", *gameDir), N_("Go on without"), {}, noDialog);
        if (noDialog)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "OpenSimFarm", Latin1ToUtf8(Tr(kNoDialogText)).c_str(), window);
        if (dir)
        {
            gameDir = dir;
            RememberGameDirectory(*dir);
            sim->SetGameDir(*dir);
            ui.SetGameDir(*dir);
            start.SetGameDir(*dir);
            folderLostAsked = false;
        }
        last = SDL_GetPerformanceCounter(); // the time spent in the box does not count as game time
        lastFolderCheck = SDL_GetTicks64();
    };
    // settings.txt in the config folder: written when the game ends (and in the browser also when a setting changes,
    // as a tab is often just closed)
    auto settingsText = [&]() {
        std::ostringstream out;
        out << "scale=" << uiScale << "\nsfx=" << sound.Enabled() << "\nmusic=" << sound.MusicEnabled() << "\nmessages=" << ui.MessagesOn()
            << "\ntoolnames=" << ui.ToolNamesOn() << "\nsmartdriving=" << Machines::smartDriving
            << "\nmodernsound=" << sound.Modern() << "\nmodernrender=" << view.Modern() << "\nmusicvolume=" << sound.MusicVolume() << "\nsfxvolume=" << sound.EffectsVolume() << "\nlanguage=" << CurrentLanguage() << "\n";
        return out.str();
    };
    auto writeSettings = [&]() {
        std::error_code ec;
        std::filesystem::create_directories(ConfigDir(), ec);
        std::ofstream(std::filesystem::path(ConfigDir()) / "settings.txt") << settingsText();
    };
#ifdef __EMSCRIPTEN__
    std::string writtenSettings = settingsText();
    int settingsCheck = 0;
#endif
    while (running)
    {
#ifdef __EMSCRIPTEN__
        if (++settingsCheck >= 60)
        {
            settingsCheck = 0;
            if (settingsText() != writtenSettings)
            {
                writtenSettings = settingsText();
                writeSettings();
            }
        }
#endif
        checkGameFolder();
        if (atStart)
        {
            const std::string song = ui.RadioSong("amain.xmi");
            sound.PlayMusic(song, ui.RadioLoops());
            SDL_Event e;
            while (SDL_PollEvent(&e))
            {
                if (e.type == SDL_QUIT)
                    running = false;
                if (touchEcho(e))
                    continue;
                convertMouse(e);
                std::string cmd = start.HandleEvent(e);
                // LOAD A SAVED GAME: the chosen farm comes up with the LOAD GAME window over it (as in the original)
                const bool openLoad = cmd.rfind("load+", 0) == 0;
                if (openLoad)
                    cmd = cmd.substr(5);
                if (cmd == "quit")
                    running = false;
                else if (cmd.rfind("language:", 0) == 0)
                {
                    sound.Play("click.voc");
                    ui.LanguageChanged(); // (the start screen has set the language; settings.txt keeps it)
                }
                else if (cmd == "generated")
                {
                    sound.Play("click.voc");
                    state = std::make_unique<GameState>(start.World());
                    sim = std::make_unique<Simulation>(*state);
                    sim->SetGameDir(*gameDir);
                    sim->SetScaledClock(true); // speeds change how fast everything moves (README)
                    sim->SetTownLayout(mega.Read("townly0.cmp"));
                    view.SetWorld(&state->world);
                    view.SetTopLeft(static_cast<int>(state->scalars["view_x"]), static_cast<int>(state->scalars["view_y"]));
                    ui.SetContext(state.get(), sim.get(), &view);
                    farmName = "NEW FARM";
                    ui.SetCurrentSave("");
                    ui.OpenWindow(kWinMap); // as the original's new game
                    ui.ResetTool();
                    atStart = false;
                }
                else if (!cmd.empty())
                {
                    sound.Play("click.voc");
                    ui.onCommand(cmd);
                    ui.ResetTool(); // every way out of the start screen follows the new-game set-up
                    atStart = false;
                }
                if (openLoad && !atStart && running)
                {
                    ui.OpenWindow(kWinLoad);
                    ui.loadFromStart = true; // its CLOSE goes back to the region screen
                }
            }
            int w, h;
            bool offscreen;
            ui.ArrowCursor();
            // the region screen: as large as the window allows (the phone layout: one unit per window pixel, it
            // sizes its own parts)
            if (phone)
                beginFrame(1.0f, 1, 1, w, h, offscreen);
            else
                beginFrame(64.0f, 640, 480, w, h, offscreen);
            start.Draw(w, h);
            endFrame(offscreen);
            last = SDL_GetPerformanceCounter();
            continue;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
            {
                running = false;
                continue;
            }
            if (touchEcho(e))
                continue;
            if (isFinger(e))
            {
                ui.SetTouchMode(true);
                touchWindow = e.tfinger.windowID;
                if (touchMap)
                {
                    int ww = 0, wh = 0;
                    if (SDL_Window* win = SDL_GetWindowFromID(e.tfinger.windowID))
                        SDL_GetWindowSize(win, &ww, &wh);
                    const float fx = e.tfinger.x * ww, fy = e.tfinger.y * wh;
                    const Uint32 t = SDL_GetTicks();
                    if (e.type == SDL_FINGERDOWN)
                    {
                        // with a building tool, a press held still moves the tool (GameUi::ToolDragWanted)
                        if (!touch.Active())
                            touch.holdDragMs = ui.ToolDragWanted() ? 250 : 0;
                        touch.Down(e.tfinger.fingerId, fx, fy, t);
                    }
                    else if (e.type == SDL_FINGERMOTION)
                        touch.Move(e.tfinger.fingerId, fx, fy, t);
                    else
                        touch.Up(e.tfinger.fingerId, fx, fy, t);
                    if (!touch.Active())
                        touchMap = false;
                }
                continue;
            }
            if (isMouse(e) && mouseWhich(e) == SDL_TOUCH_MOUSEID)
            {
                // the first finger as a mouse: its press decides whether this touch is a map gesture
                if (e.type == SDL_MOUSEBUTTONDOWN && !touch.Active())
                {
                    SDL_Event c = e;
                    convertMouse(c);
                    touchMap = ui.TouchOnMap(c.button.x, c.button.y);
                    if (touchMap)
                        touch.Cancel();
                }
                if (touchMap)
                    continue;
            }
            convertMouse(e);
            if (ui.HandleEvent(e))
                continue;
            if (e.type == SDL_KEYDOWN)
            {
                const SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE)
                    ui.AskQuit(); // as File > Quit (the engine's key; nothing was open to close)
                else if (k >= SDLK_0 && k <= SDLK_4 && !(e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)))
                {
                    // the engine's speed keys (user finding Q17): 1 Slow, 2 Normal, 3 Fast, 4 Ultra, 0 Pause
                    static const Speed kKey[5] = { Speed::Paused, Speed::Slow, Speed::Normal, Speed::Fast, Speed::Ultra };
                    sim->SetSpeed(kKey[k - SDLK_0]);
                }
                else if (k == SDLK_SPACE)
                {
                    // the engine's pause key: Space again resumes at the speed the game had (Normal if it was paused
                    // from the menu)
                    static Speed resume = Speed::Normal;
                    if (sim->GetSpeed() == Speed::Paused)
                        sim->SetSpeed(resume);
                    else
                    {
                        resume = sim->GetSpeed();
                        sim->SetSpeed(Speed::Paused);
                    }
                }
                else if (k == SDLK_F5)
                    ui.onCommand("save");
                else if (k == SDLK_F9)
                    ui.onCommand("load");
            }
            view.HandleEvent(e);
        }
        touch.Tick(SDL_GetTicks());
        for (const TouchAction& a : touch.Take())
        {
            // window pixels -> the converted pixels the map view works in (as convertMouse)
            const float k = MouseRatio();
            const float off = static_cast<float>(MouseOffset());
            switch (a.kind)
            {
            case TouchAction::Pan: view.PanBy(a.dx * k, a.dy * k); break;
            case TouchAction::Zoom:
                if (touch.smooth)
                    view.ZoomBy(a.factor, static_cast<int>((a.x - off) * k), static_cast<int>(a.y * k));
                else
                    view.ZoomAt(a.dir, static_cast<int>((a.x - off) * k), static_cast<int>(a.y * k));
                break;
            case TouchAction::Tap: pushClick(SDL_BUTTON_LEFT, a.x, a.y); break;
            case TouchAction::LongPress: pushClick(SDL_BUTTON_RIGHT, a.x, a.y); break;
            case TouchAction::DragStart:
                pushMouse(SDL_MOUSEMOTION, a.x, a.y);
                pushMouse(SDL_MOUSEBUTTONDOWN, a.x, a.y);
                break;
            case TouchAction::DragMove: pushMouse(SDL_MOUSEMOTION, a.x, a.y); break;
            case TouchAction::DragEnd: pushMouse(SDL_MOUSEBUTTONUP, a.x, a.y); break;
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;
#ifdef __EMSCRIPTEN__
        dt = std::min(dt, 0.1); // a tab that was hidden pauses the game instead of catching up all the time at once
#endif
        // typing a file name or an amount, or the arrow keys of a list, menu or box, must not scroll the map
        if (!ui.TextFocus() && !ui.ArrowKeysTaken())
            view.Update(static_cast<float>(dt));
        if (!ui.Modal())
            sim->Advance(dt);
        // the autoplayer (OSF_AUTOPLAY=folder for its yearly checkpoint saves): a test player that runs the farm
        // by itself at Ultra; a game loaded or started later gets a new one
        static const char* const autoplayDir = std::getenv("OSF_AUTOPLAY");
        static std::unique_ptr<AutoPlayer> autoplayer;
        static const Simulation* autoplaySim = nullptr;
        if (autoplayDir && autoplaySim != sim.get())
        {
            autoplayer = std::make_unique<AutoPlayer>(*state, *sim, autoplayDir);
            autoplaySim = sim.get();
            sim->SetSpeed(Speed::Ultra);
        }
        if (autoplayer)
        {
            autoplayer->Tick();
            std::string note;
            while (autoplayer->TakeNote(note))
                ui.ShowMessage(note);
        }
        {
            const std::string song = ui.RadioSong(sim->Song());
            sound.PlayMusic(song, ui.RadioLoops());
        }
        std::string m;
        while (sim->TakeMessage(m))
            ui.ShowMessage(m);
        {
            float vx, vy, vw, vh;
            view.VisibleCells(vx, vy, vw, vh);
            sound.SetListener(vx + vw / 2, vy + vh / 2, vw / 2);
        }
        int sx, sy;
        while (sim->TakeSound(m, sx, sy))
            sound.Play(m, sx, sy);
        int gx, gy;
        if (sim->TakeGoto(gx, gy))
            view.CenterOn(static_cast<float>(gx), static_cast<float>(gy)); // Options > AutoGoto
        GameEvent ev;
        while (sim->TakeEvent(ev))
        {
            if (autoplayer && autoplayer->Answer(ev))
                continue;
            ui.ShowEvent(ev);
            sound.Play("compbeep.voc");
        }
        // game over (after the events are handed over, so the notice after NO is shown first): the lost farm's question box answered YES starts a new game (the region screen); NO or no answer
        // ends the program once the notice after it is closed, as the original does
        if (sim->GameOverChoice() == 2 && !ui.Modal())
            atStart = true;
        else if (sim->GameOverChoice() == 1 && !ui.Modal())
            running = false;

        // each crop slot shows its own crop file's pictures (growth stages and sign), as the original copies them
        // into the tile sheet when a crop is loaded; rebuild the tile textures when the loaded crops change
        // the day's palette (run by the daily weather step): a rain or snow day switches the whole
        // screen to rainpal.pal, or to frostpal.pal when the temperature is below 32 F; any other day back to
        // simfarm.pal. The three differ only in colour 7 (tan, blue, white), so the ground's colour-7 pixels show the
        // rain or the frost
        int playPalette = 0;
        if (sim)
        {
            const int today = sim->WeekPattern()[static_cast<size_t>(std::clamp(state->cal.day, 0, 6))];
            if (today >= 3)
                playPalette = sim->Temperature() < 32 ? 2 : 1;
        }
        {
            std::string sig;
            for (const auto& c : state->crops)
                sig += c.fileName + '|';
            sig += std::to_string(playPalette);
            if (sig != cropArtSig)
            {
                cropArtSig = sig;
                MapGraphics patched = gfx;
                static const char* const palNames[3] = { "simfarm.pal", "rainpal.pal", "frostpal.pal" };
                const std::vector<uint8_t> pal = mega.Read(palNames[playPalette]);
                if (pal.size() >= 48)
                    patched.palette = LoadPalette(pal);
                for (int i = 0; i < static_cast<int>(state->crops.size()) && i < 16; ++i)
                    ApplyCropArt(patched.plain, i, state->crops[i].art);
                view.SetPlainTiles(renderer, patched);
                view.SetEventTiles(renderer, patched);
                gfxUi.SetPlainTiles(patched.plain);
                gfxUi.SetPlayPalette(playPalette);
            }
        }
        // debug aid (OPENSIMFARM_CHECK=1): report when a crop slot's loaded file stops matching the saved crop-name table
        static const bool checkCrops = std::getenv("OPENSIMFARM_CHECK") != nullptr;
        if (checkCrops && state)
            for (int i = 0; i < 16; ++i)
            {
                std::string n;
                for (int k = 0; k < 14 && state->cropNames[static_cast<size_t>(i) * 14 + k]; ++k)
                    n += static_cast<char>(std::tolower(state->cropNames[static_cast<size_t>(i) * 14 + k]));
                std::string f;
                for (char c : state->crops[i].fileName)
                    f += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                static std::string reported[16];
                if (n != f && reported[i] != n + "|" + f)
                {
                    reported[i] = n + "|" + f;
                    std::fprintf(stderr, "crop check: slot %d table '%s' loaded '%s' (%s)\n", i, n.c_str(), f.c_str(),
                                 state->crops[i].name.c_str());
                }
            }
        // the chosen interface scale while the window holds 640 x 400 interface pixels at it, else a fractional one
        int w, h;
        bool offscreen;
        // (the phone layout: one interface unit per window pixel; it sizes its own bars, lists and windows)
        gfxUi.SetScale(phone ? beginFrame(1.0f, 1, 1, w, h, offscreen) : beginFrame(static_cast<float>(uiScale), 640, 400, w, h, offscreen));
        SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255);
        SDL_RenderClear(renderer);
        ui.SetScreenSize(w, h);
        view.SetPhoneZoom(phone);
        if (phone) // the map's zoom levels in the phone's small pixels: a level-2 tile about a finger wide
            view.SetZoomBase(static_cast<float>(std::max(1L, std::lround(ui.PhoneScale() / 2.0))));
        const SDL_Rect mapArea = ui.MapPixels();
        SDL_RenderSetViewport(renderer, &mapArea);
        view.SetOrigin(mapArea.x, mapArea.y);
        view.SetEventBank(sim ? sim->EventKind() : 0);
        view.Draw(renderer, mapArea.w, mapArea.h);
        SDL_RenderSetViewport(renderer, nullptr);
        ui.Draw();
        endFrame(offscreen);

        const std::string title = "OpenSimFarm - " + farmName;
        if (title != lastTitle)
        {
            SDL_SetWindowTitle(window, title.c_str());
            lastTitle = title;
        }
    }
    writeSettings();
    return 0;
}
