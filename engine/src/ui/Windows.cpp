// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
//
// The windows draw their original background picture and put live values into the boxes of that picture.
// Box positions were measured on the pictures; every label is part of the picture itself.
#include "ui/Windows.h"
#include "core/Lang.h"
#include "audio/Sound.h"

#include "assets/Megadata.h"
#include "game/Crops.h"
#include "game/Shop.h"
#include "game/Disasters.h"
#include "ui/MapView.h"

#include <algorithm>
#include <cctype>
#include "assets/Graphics.h"
#include <optional>
#include <set>
#include <random>
#include <map>
#include <filesystem>
#include "core/Paths.h"
#include <cmath>
#include <cstring>

namespace osf {

// the MAP window's caption: the original's x for English; a translation is centred in the caption area (x 8..151)
static int Caption(int x, const char* english)
{
    if (CurrentLanguage() == "en")
        return x;
    return std::max(8, 8 + (144 - Gfx::TextWidth(Tr(english))) / 2);
}

static const char* const kMonthShort[12] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"), N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec") };

static std::string Num(int64_t v) { return std::to_string(v); }

// ---------------------------------------------------------------- balance sheet
class BalanceWindow : public Window
{
public:
    BalanceWindow() : Window(kWinBalance, N_("BALANCE SHEET"), "cashflow.bmp", 400, 224) {}
    Rect CloseButton() const override { return { 329, 185, 46, 22 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        const GameState& s = *ctx.state;
        const Simulation& sim = *ctx.sim;
        auto v = [&](const char* k) { return sim.Scalar(k); };
        const int64_t land = sim.LandValue();
        // the herd is valued afresh whenever the window is shown, and the asset line keeps that figure
        int64_t herd = 0;
        for (const SaleItem& it : SaleList(*ctx.state, *ctx.sim, 2))
            herd += it.value;
        ctx.state->scalars["asset_livestock"] = herd;
        // every figure is printed into a box of 7 or 8 characters filled with its row's colour (light grey and white
        // stripes, grey for the totals), in black; the box over "TOTAL EXPENSES" hides its last letter
        // (width: the printed width, %7ld or %8ld; the estimated tax prints 7 characters into an 8-character box)
        auto field = [&](int x, int y, int len, int bg, int64_t value, int width = 0) {
            g.Fill(x, y, 8 * len, 8, bg);
            g.TextRight(x + 8 * (width ? width : len), y, Num(value), kBlack);
        };
        auto stripe = [](int row) { return row % 2 ? kWhite : kLightGrey; };
        const int64_t assets[5] = { s.cash, v("asset_machinery"), land, v("asset_futures"), herd };
        int64_t totalAssets = 0;
        for (int i = 0; i < 5; ++i)
        {
            field(144, 24 + 8 * i, 7, stripe(i), assets[i]);
            totalAssets += assets[i];
        }
        field(136, 88, 8, kGrey, totalAssets);

        static const char* const rev[5] = { "rev_crops", "rev_machinery", "rev_land", "rev_futures", "rev_animals" };
        int64_t totalRev = 0;
        for (int i = 0; i < 5; ++i)
        {
            field(144, 120 + 8 * i, 7, stripe(i), v(rev[i]));
            totalRev += v(rev[i]);
        }
        field(136, 168, 8, kGrey, totalRev);

        static const char* const exp[11] = { "exp_seed", "exp_machinery", "exp_land", "exp_futures", "exp_structures", "exp_livestock",
                                             "exp_chemicals", "exp_interest", "exp_leases", "exp_taxes", "exp_misc" };
        int64_t totalExp = 0;
        for (int i = 0; i < 11; ++i)
        {
            // the Taxes line shows last year's tax (the year's bucket is only used at the year end)
            const int64_t value = i == 9 ? sim.LastTax() + v(exp[i]) : v(exp[i]);
            field(320, 24 + 8 * i, 7, stripe(i), value);
            if (i != 7) // the loan-interest line is shown but not added
                totalExp += value;
        }
        field(312, 152, 8, kGrey, totalExp);
        field(312, 168, 8, kGrey, totalRev - totalExp);
        field(192, 184, 8, kLightGrey, sim.EstimatedTax(), 7);
        field(192, 192, 7, kWhite, sim.LoanPayment());
    }
};

// ---------------------------------------------------------------- market value
class MarketWindow : public Window
{
public:
    MarketWindow() : Window(kWinMarket, N_("MARKET VALUE"), "futures.bmp", 320, 208) {}
    Rect CloseButton() const override { return { 259, 174, 46, 22 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        const GameState& s = *ctx.state;
        if (_list.empty() || _loadedNames != LoadedNames(s))
            BuildList(ctx);
        const auto& spares = ctx.sim->SpareMarket();
        const Entry& chosenEntry = _list[static_cast<size_t>(std::clamp(_sel, 0, static_cast<int>(_list.size()) - 1))];
        const Simulation::SpareCropMarket* spare = chosenEntry.slot < 0 && chosenEntry.spare >= 0 && chosenEntry.spare < static_cast<int>(spares.size())
                                           ? &spares[static_cast<size_t>(chosenEntry.spare)] : nullptr;
        const auto& hist = spare ? spare->history : ctx.sim->PriceHistory()[_crop];
        // 30-week history, in window-picture coordinates (the original's window origin + 16):
        // a dark grid every 4 pixels over light grey, a yellow line at the crop's reference price, and per week a
        // 3-pixel red bar 4 * (16 + (price - reference) / step) high, step = reference / 8, kept inside the chart
        const int ref = std::max(1, spare ? spare->basePrice : s.crops[_crop].BasePrice());
        const int step = std::max(1, ref >> 3);
        g.Fill(24, 20, 124, 140, kLightGrey);
        for (int i = 0; i < 30; ++i)
        {
            const int p = hist[i];
            const int k = p <= ref ? 16 - (ref - p) / step : 16 + (p - ref) / step;
            int top = 0xAF - 16 - 4 * k;
            if (top < 0x24 - 16)
                top = 0x26 - 16;
            if (top > 0xAE - 16)
                top = 0xAC - 16;
            g.Fill(0x19 + 4 * i, top, 3, 0xAF - 16 - top + 1, kRed);
        }
        // the grid goes over the bars, then the reference line
        for (int x = 24; x < 0x94 - 16 + 16; x += 4)
            g.Fill(x, 20, 1, 0xB0 - 0x24, kDarkGrey);
        for (int y = 20; y < 0xB0 - 16; y += 4)
            g.Fill(24, y, 0x90 - 0x18, 1, kDarkGrey);
        g.Fill(24, 0x6C - 16, 0x90 - 0x18, 2, kYellow);

        // every crop in a scrolling list where the original has its 4 x 4 grid of the 16 loaded crops' signs (an
        // engine change, user finding Q27): the loaded crops with their PLANT icon, the crop files not loaded with
        // their own icon; alphabetical, eight rows, a scroll bar on the right
        const Rect area = ListArea();
        g.Fill(area.x, area.y, area.w, area.h, kLightGrey);
        const auto& pal = g.Pal();
        auto argb = [&](int v) {
            const auto& p = pal[static_cast<size_t>(v & 15)];
            return 0xFF000000u | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        };
        _top = std::clamp(_top, 0, std::max(0, static_cast<int>(_list.size()) - kRows));
        for (int r = 0; r < kRows && _top + r < static_cast<int>(_list.size()); ++r)
        {
            const Entry& e = _list[static_cast<size_t>(_top + r)];
            const int y = area.y + 16 * r;
            const bool chosen = _top + r == _sel;
            if (chosen)
                g.Fill(area.x, y, area.w - kBar, 16, kNavy);
            if (e.slot >= 0)
                g.DrawTile(832 + e.slot, area.x, y);
            else if (e.spare >= 0 && e.spare < static_cast<int>(spares.size()))
            {
                const auto& icon = spares[static_cast<size_t>(e.spare)].icon;
                std::vector<uint32_t> img(256, argb(kLightGrey));
                for (size_t q = 0; q < 256 && q < icon.size(); ++q)
                    img[q] = argb(icon[q]);
                g.DrawPixels(img, 16, 16, area.x, y, 16, 16);
            }
            g.Text(area.x + 18, y + 4, e.name.substr(0, 12), chosen ? kWhite : kBlack);
        }
        // the scroll bar: arrows at both ends, the thumb for the rows shown
        const Rect up = UpArrow(), down = DownArrow();
        g.Fill(up.x, up.y, kBar, area.h, kDarkGrey);
        for (const Rect& r : { up, down })
        {
            g.Fill(r.x, r.y, r.w, r.h, kLightGrey);
            g.Bevel(r.x, r.y, r.w, r.h, true);
        }
        for (int i = 0; i < 3; ++i)
        {
            g.Fill(up.x + 4 - i, up.y + 3 + i, 2 * i + 1, 1, kBlack);
            g.Fill(down.x + 4 - i, down.y + down.h - 4 - i, 2 * i + 1, 1, kBlack);
        }
        const int n = static_cast<int>(_list.size());
        const int track = area.h - 2 * kBar;
        if (n > kRows)
        {
            const int th = std::max(6, track * kRows / n), ty = up.y + kBar + (track - th) * _top / (n - kRows);
            g.Fill(up.x + 1, ty, kBar - 2, th, kLightGrey);
            g.Bevel(up.x + 1, ty, kBar - 2, th, true);
        }
        g.Frame(area.x - 1, area.y - 1, area.w + 2, area.h + 2, kBlack);

        // the three read-outs are printed left-aligned into light grey boxes of 13, 6 and 3 characters
        auto field = [&](int x, int y, int len, const std::string& text) {
            g.Fill(x, y, 8 * len, 8, kLightGrey);
            g.Text(x, y, text.substr(0, len), kBlack);
        };
        const Entry& e = _list[static_cast<size_t>(std::clamp(_sel, 0, n - 1))];
        field(200, 147, 13, e.name);
        if (e.slot >= 0)
        {
            // "Price" is what one field's harvest brings now: yield units x price / 10 (corn 289 x 15 = 4335, seen live)
            field(200, 160, 6, Num((s.crops[e.slot].Yield() * (s.MarketPrice(e.slot) / 10)) & 0xFFFF));
            field(120, 182, 3, Num(s.Contracts(e.slot)));
        }
        else if (spare)
        {
            // a crop not loaded: the engine's market of the other crops (no futures can be bought for it)
            field(200, 160, 6, Num((spare->yield * (spare->history[29] / 10)) & 0xFFFF));
            field(120, 182, 3, "0");
        }
    }

    void Click(int x, int y, UiContext&) override
    {
        const int n = static_cast<int>(_list.size());
        if (UpArrow().Contains(x, y))
            _top = std::max(0, _top - 1);
        else if (DownArrow().Contains(x, y))
            _top = std::min(std::max(0, n - kRows), _top + 1);
        else if (const Rect a = ListArea(); x >= a.x + a.w - kBar && x < a.x + a.w && y >= a.y && y < a.y + a.h)
            _top = std::clamp(_top + (y < a.y + a.h / 2 ? -kRows : kRows), 0, std::max(0, n - kRows)); // the track pages
        else if (a.Contains(x, y))
        {
            const int i = _top + (y - a.y) / 16;
            if (i < n)
                Choose(i);
        }
    }
    bool KeyDown(SDL_Keycode k, UiContext&) override
    {
        const int n = static_cast<int>(_list.size());
        if (k != SDLK_UP && k != SDLK_DOWN && k != SDLK_PAGEUP && k != SDLK_PAGEDOWN && k != SDLK_HOME && k != SDLK_END)
            return false;
        const int step = k == SDLK_UP ? -1 : k == SDLK_DOWN ? 1 : k == SDLK_PAGEUP ? -kRows : k == SDLK_PAGEDOWN ? kRows : k == SDLK_HOME ? -n : n;
        Choose(std::clamp(_sel + step, 0, std::max(0, n - 1)));
        if (_sel < _top)
            _top = _sel;
        else if (_sel >= _top + kRows)
            _top = _sel - kRows + 1;
        return true;
    }
    bool Wheel(int dy) override
    {
        _top = std::clamp(_top - dy, 0, std::max(0, static_cast<int>(_list.size()) - kRows));
        return true;
    }

private:
    static constexpr int kRows = 8, kBar = 10;
    struct Entry { std::string name; int slot; int spare; };
    std::vector<Entry> _list;
    std::string _loadedNames;
    int _sel = 0, _top = 0, _crop = 0;

    static Rect ListArea() { return { 0xB0, 0x18 - kTitleH, 128, 128 }; }
    static Rect UpArrow() { const Rect a = ListArea(); return { a.x + a.w - kBar, a.y, kBar, kBar }; }
    static Rect DownArrow() { const Rect a = ListArea(); return { a.x + a.w - kBar, a.y + a.h - kBar, kBar, kBar }; }
    void Choose(int i)
    {
        _sel = i;
        if (_list[static_cast<size_t>(i)].slot >= 0)
            _crop = _list[static_cast<size_t>(i)].slot;
    }
    static std::string LoadedNames(const GameState& s)
    {
        std::string all;
        for (int i = 0; i < 16 && i < static_cast<int>(s.crops.size()); ++i)
            all += s.crops[static_cast<size_t>(i)].fileName + "|";
        return all;
    }
    void BuildList(UiContext& ctx)
    {
        const GameState& s = *ctx.state;
        const std::string chosen = _list.empty() ? std::string() : _list[static_cast<size_t>(_sel)].name;
        _list.clear();
        _loadedNames = LoadedNames(s);
        for (int i = 0; i < 16 && i < static_cast<int>(s.crops.size()); ++i)
            _list.push_back({ CropName(s, i), i, -1 });
        const auto& spares = ctx.sim->SpareMarket();
        for (size_t k = 0; k < spares.size(); ++k)
            _list.push_back({ CropNameFor(spares[k].file, spares[k].name), -1, static_cast<int>(k) });
        std::stable_sort(_list.begin(), _list.end(), [&](const Entry& x, const Entry& y) { return SortKey(x.name) < SortKey(y.name); });
        _sel = 0;
        for (size_t i = 0; i < _list.size(); ++i)
            if ((chosen.empty() && _list[i].slot == _crop) || (!chosen.empty() && _list[i].name == chosen))
                _sel = static_cast<int>(i);
        _top = std::max(0, _sel - kRows / 2);
        if (_list[static_cast<size_t>(_sel)].slot >= 0)
            _crop = _list[static_cast<size_t>(_sel)].slot;
    }
};

// ---------------------------------------------------------------- weather
class WeatherWindow : public Window
{
public:
    WeatherWindow() : Window(kWinWeather, N_("WEATHER"), "weather.bmp", 336, 192) {}

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        const GameState& s = *ctx.state;
        const Simulation& sim = *ctx.sim;
        const int season = s.cal.Season();               // 0 winter, 1 spring, 2 summer, 3 autumn
        static const int seasonIcon[4] = { 200, 201, 202, 203 };
        static const int seasonStart[4] = { 45, 10, 22, 34 };
        const int next = (season + 1) % 4;
        const int weeksTill = ((seasonStart[next] - s.cal.AbsWeek()) % 48 + 48) % 48;

        g.DrawTile(seasonIcon[season], 16, 4);
        g.Text(40, 8, TrF("Week {0} of {1}", s.cal.week + 1, Tr(kMonthShort[s.cal.month])), kBlack);
        g.Text(176, 8, weeksTill == 1 ? TrF("{0} Week Till", weeksTill) : TrF("{0} Weeks Till", weeksTill), kBlack);
        g.DrawTile(seasonIcon[next], 302, 4);

        // thermometer: today's temperature; the Celsius figure is the original's (F - 32) / 2
        const int tf = sim.DailyTemperature() > -1000 ? sim.DailyTemperature() : sim.Temperature();
        const int tc = (tf - 32) >> 1;
        g.TextRight(50, 47, Num(tf), kBlack);
        g.TextRight(50, 63, Num(tc), kBlack);
        const int mercury = std::clamp(tf, 0, 105) * 42 / 100;
        g.Fill(74, 79 - mercury, 5, mercury + 1, kRed);

        // today's scene: sky by the day's pattern, ground by season
        // today's scene: the sky picture of today's sky code (snow shows the rain picture), the ground of the season
        g.DrawBank("wethsky.btl", std::clamp(sim.TodaySky() == 4 ? 3 : sim.TodaySky(), 0, 3), 111, 48);
        g.DrawBank("wethgrnd.btl", season, 111, 80);

        // rain: Avg Rain = a quarter of the sum over the year's 48 weeks of the rain values, where the
        // original tests this year's value but adds the first year's (kept); Rain to Date = rain days / 4
        int sum = 0;
        const int row = sim.RainProfileRow();
        for (int wk = 0; wk < 48; ++wk)
            if (static_cast<int8_t>(s.WeatherRain(row + wk)) > 0)
                sum += static_cast<int8_t>(s.WeatherRain(wk));
        const int rainToDate = static_cast<int>(std::min<int64_t>(60, sim.Scalar("rain_days") >> 2));
        g.TextRight(320, 48, Num(sum >> 2), kBlack);
        g.TextRight(320, 80, Num(rainToDate), kBlack);
        g.Fill(250, 91 - rainToDate, 6, rainToDate, kBlue);

        // evaporation: the dial is fldgrph.til face 15 + temperature / 32
        g.DrawPicture("fldgrph.til$" + std::to_string(15 + std::clamp(tf >> 5, 0, 4)), 24, 104);
        static const char* const kEvaporation[5] = { N_("evaporation:Slow"), N_("evaporation:Slow"), N_("evaporation:Average"),
                                                     N_("evaporation:Fast"), N_("evaporation:Fast") }; // by temperature / 32
        const char* evaporation = kEvaporation[std::clamp(tf >> 5, 0, 4)];
        g.TextFit(12, 174, 76, TrId(evaporation, std::strchr(evaporation, ':') + 1), kBlack);

        // the wind rotor: its two frames of wethvane.til swap whenever more timer ticks have passed than
        // the delay of the wind speed (table by wind / 8), so it spins faster in a stronger wind and stops in a pause
        static const int kVaneDelay[12] = { 64, 64, 60, 8, 8, 4, 4, 4, 2, 2, 2, 2 };
        const int delay = kVaneDelay[std::clamp(sim.Wind() >> 3, 0, 11)];
        if (_vaneTick < 0)
            _vaneTick = sim.TimerTicks(); // opening the window stamps the time
        if (sim.TimerTicks() - _vaneTick > delay)
        {
            _vaneFrame ^= 1;
            _vaneTick = sim.TimerTicks();
        }
        g.DrawPicture("wethvane.til$" + std::to_string(_vaneFrame), 272, 104);
        g.Text(262, 174, TrF("{0} Mph", sim.Wind()), kBlack);

        // five-day forecast from the rest of this week and the next one
        static const int skyIcon[5] = { 204, 205, 206, 207, 208 };
        for (int i = 0; i < 5; ++i)
        {
            const int d = s.cal.day + 1 + i;
            const int p = d < 7 ? sim.WeekPattern()[d] : sim.NextWeekPattern()[d - 7];
            g.DrawTile(skyIcon[std::clamp(p, 0, 4)], 113 + 24 * i, 136);
        }
    }

private:
    static inline int _vaneFrame = 0; // kept between openings, as the original's frame byte
    int _vaneTick = -1;
};

// ---------------------------------------------------------------- bank
class BankWindow : public Window
{
public:
    BankWindow() : Window(kWinBank, N_("BANK"), "bankterm.bmp", 288, 224) {}
    Rect CloseButton() const override { return { 185, 177, 46, 21 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        const Simulation& sim = *ctx.sim;
        // each value is cleared with a dark grey box and printed left-aligned in white
        auto value = [&](int x, int y, int w, const std::string& text) {
            g.Fill(x, y, w, 8, kDarkGrey);
            g.Text(x, y, text, kWhite);
        };
        value(192, 24, 64, Num(ctx.state->cash));
        value(192, 40, 56, Num(sim.LoanBalance()));
        value(192, 56, 56, Num(sim.CreditLimit()));
        value(192, 72, 56, Num(sim.InterestRate()));
        value(192, 88, 56, Num(sim.LoanPayment()));
        value(176, 128, 72, _entry.empty() ? "0" : _entry);
        if (!_note.empty())
        {
            // on the terminal screen, above LOAN AMOUNT
            g.Fill(38, 110, 212, 10, kDarkGrey);
            g.TextCentre(144, 111, _note.substr(0, 26), kYellow);
        }
        if (_pressed >= 0 && _pressedTimer > 0)
        {
            const Rect r = Key(_pressed);
            g.DimRect(r.x, r.y, r.w, r.h, 110);
            _pressedTimer--;
        }
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        static const char keys[12] = { '7', '8', '9', 'C', '4', '5', '6', '?', '1', '2', '3', '0' };
        for (int k = 0; k < 12; ++k)
            if (Key(k).Contains(x, y))
            {
                Press(k);
                if (keys[k] == 'C')
                    _entry.clear();
                else if (keys[k] == '?')
                    _note = Tr("Borrow up to your limit.");
                else if (_entry.size() < 7 && !(_entry.empty() && keys[k] == '0'))
                    _entry += keys[k];
                return;
            }
        if (Rect{ 145, 153, 86, 21 }.Contains(x, y))
        {
            // nothing typed: the whole debt, as the original; an amount: that much (engine extension)
            const int64_t balance = ctx.sim->LoanBalance();
            const int64_t amount = _entry.empty() ? balance : std::min<int64_t>(balance, std::stoll(_entry));
            _note = balance == 0                 ? std::string(Tr("There is no loan to repay."))
                    : !ctx.sim->Repay(amount)    ? std::string(Tr("Not enough cash to repay."))
                    : amount == balance          ? std::string(Tr("Loan repaid in full."))
                                                 : TrF("Repaid {0}.", amount);
            _entry.clear();
        }
        else if (Rect{ 145, 177, 38, 21 }.Contains(x, y) && !_entry.empty())
        {
            const int64_t amount = std::stoll(_entry);
            _note = ctx.sim->Borrow(amount) ? Tr("Loan granted.") : Tr("Over your credit limit.");
            _entry.clear();
        }
    }

    bool KeyDown(SDL_Keycode k, UiContext&) override
    {
        if (k >= SDLK_0 && k <= SDLK_9 && _entry.size() < 7 && !(_entry.empty() && k == SDLK_0))
            _entry += static_cast<char>('0' + (k - SDLK_0));
        else if (k == SDLK_BACKSPACE && !_entry.empty())
            _entry.pop_back();
        else
            return false;
        return true;
    }

private:
    static Rect Key(int k) { return { 49 + (k % 4) * 16, 153 + (k / 4) * 16, 14, 14 }; }
    void Press(int k) { _pressed = k; _pressedTimer = 6; }
    std::string _entry, _note;
    int _pressed = -1, _pressedTimer = 0;
};

// ---------------------------------------------------------------- map
class MapWindow : public Window
{
public:
    MapWindow() : Window(kWinMap, N_("MAP"), "mapwndo.bmp", 288, 240) {}
    Rect CloseButton() const override { return { 225, 203, 46, 22 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        GameState& s = *ctx.state;
        // every cell is drawn as a 2x2 block of the overview colours of a tile (far_mapc.mbm: 2 bytes per tile id,
        // one per pixel row, four 2-bit plane fields, left pixel in the high bit). The mode picks
        // the tile: the terrain, the plot-ownership tile, or a step 0x1D0 + value / 8 of the 32-step ramp
        if (_mapc.empty() && ctx.mega)
            _mapc = ctx.mega->Read("far_mapc.mbm");
        // a larger world (an engine addition) is drawn the same way at two pixels a cell, then scaled into the box
        const int N = s.world.size, D = 2 * N, P = s.Plots();
        std::vector<int> tile(static_cast<size_t>(N) * N);
        for (int x = 0; x < N; ++x)
            for (int y = 0; y < N; ++y)
            {
                const MapCell& c = s.world.At(x, y);
                const int terrain = (c.HasObject() && (c.overlay & 0x7FF) < 960) ? (c.overlay & 0x7FF) : c.BaseTile();
                int t = terrain;
                if (_mode == kProperty)
                {
                    static const int zoneTile[4] = { -1, 0x1EF, 0x1CF, 0x1D2 };
                    const int z = zoneTile[s.PlotOwner(x / 8, y / 8) & 3];
                    t = z < 0 ? terrain : z;
                }
                else if (_mode == kWater)
                    t = (c.base & 0x2000) ? 0x1EF : std::min(0x1EF, 0x1D3 + ctx.sim->SoilWetness() + c.terrain);
                else if (_mode != kOverhead && _mode != kProfit)
                    t = _mode == kNutrients ? 0x1EF : 0x1D0;
                tile[static_cast<size_t>(x) * N + y] = t;
            }
        if (_mode != kOverhead && _mode != kProperty && _mode != kWater)
            for (int fi = 1; fi < 98; ++fi)
            {
                Field f = s.FieldAt(fi);
                if (!f.InUse())
                    continue;
                const int t = _mode == kProfit ? 0x1EF - ((f.Stress() >> 8) >> 3) : 0x1D0 + (FieldValue(f) >> 3);
                for (int x = f.X(); x < std::min(N, f.X() + f.W()); ++x)
                    for (int y = f.Y(); y < std::min(N, f.Y() + f.H()); ++y)
                        tile[static_cast<size_t>(x) * N + y] = t;
            }
        std::vector<uint8_t> ix(static_cast<size_t>(D) * D);
        for (int x = 0; x < N; ++x)
            for (int y = 0; y < N; ++y)
            {
                const int t = tile[static_cast<size_t>(x) * N + y];
                for (int r = 0; r < 2; ++r)
                {
                    const int b = 2 * t + r < static_cast<int>(_mapc.size()) ? _mapc[static_cast<size_t>(2 * t + r)] : 0;
                    for (int c = 0; c < 2; ++c)
                    {
                        int idx = 0;
                        for (int plane = 0; plane < 4; ++plane)
                            idx |= ((b >> (6 - 2 * plane + (1 - c))) & 1) << plane;
                        ix[static_cast<size_t>(2 * y + r) * D + 2 * x + c] = static_cast<uint8_t>(idx);
                    }
                }
            }
        // a larger world's picture is scaled into the 192 x 192 box here, before the lines below, so that they stay whole
        if (D != 192)
        {
            std::vector<uint8_t> box(192 * 192);
            for (int y = 0; y < 192; ++y)
                for (int x = 0; x < 192; ++x)
                    box[static_cast<size_t>(y) * 192 + x] = ix[static_cast<size_t>(y * D / 192) * D + x * D / 192];
            ix.swap(box);
        }
        auto sc = [&](int v) { return v * 192 / D; }; // map pixels (two a cell) to box pixels
        // the original draws the chosen plot and the view frame in XOR mode with colour 15 (palette index ^ 15):
        // horizontal lines stop before their end x, vertical lines include their end y
        auto xorPixel = [&](int x, int y) {
            if (x >= 0 && y >= 0 && x < 192 && y < 192)
                ix[static_cast<size_t>(y) * 192 + x] ^= 15;
        };
        auto xorH = [&](int x1, int x2, int y) { for (int x = x1; x < x2; ++x) xorPixel(x, y); };
        auto xorV = [&](int x, int y1, int y2) { for (int y = y1; y <= y2; ++y) xorPixel(x, y); };
        if (_mode == kProperty && _plotC >= 0)
            for (int y = sc(_plotR * 16); y < sc(_plotR * 16 + 16); ++y)
                xorH(sc(_plotC * 16), sc(_plotC * 16 + 16), y);
        // the view: two nested outlines from its top-left cell, two map pixels per whole cell it shows
        int viewCol = 0, viewRow = 0;
        if (ctx.view)
        {
            float vx, vy, vw, vh;
            ctx.view->VisibleCells(vx, vy, vw, vh);
            const int x1 = sc(2 * static_cast<int>(vx)), y1 = sc(2 * static_cast<int>(vy));
            const int x2 = sc(2 * static_cast<int>(vx) + 2 * static_cast<int>(vw)) - 1, y2 = sc(2 * static_cast<int>(vy) + 2 * static_cast<int>(vh)) - 1;
            xorV(x1, y1, y2);
            xorV(x2, y1, y2);
            xorH(x1, x2, y1);
            xorH(x1, x2, y2);
            xorV(x1 + 1, y1 + 1, y2 - 2);
            xorV(x2 - 1, y1 + 1, y2 - 2);
            xorH(x1 + 1, x2 - 2, y1 + 1);
            xorH(x1 + 1, x2 - 2, y2 - 1);
            viewCol = std::clamp(static_cast<int>(vx + vw / 2) / 8, 0, P - 1);
            viewRow = std::clamp(static_cast<int>(vy + vh / 2) / 8, 0, P - 1);
        }
        const Palette& pal = g.Pal();
        std::vector<uint32_t> px(192 * 192);
        for (size_t i = 0; i < px.size(); ++i)
            px[i] = 0xFF000000u | (pal[ix[i]][0] << 16) | (pal[ix[i]][1] << 8) | pal[ix[i]][2];
        g.DrawPixels(px, 192, 192, 72, 0, 192, 192);

        // property mode: the plot grid in yellow, twelve lines each way from the map's top-left corner, over the
        // XOR drawing
        if (_mode == kProperty)
        {
            for (int k = 0; k < P; ++k)
            {
                g.Fill(72 + 192 * k / P, 0, 1, 192, kYellow);
                g.Fill(72, 192 * k / P, 192, 1, kYellow);
            }
        }
        // letters over the map, in the game's font: T the town plot, H the farmhouse (the saved position words),
        // D a tornado, L the locusts
        auto letter = [&](const char* c, int mx, int my) {
            mx = sc(mx);
            my = sc(my);
            if (mx >= 0 && my >= 0 && mx < 192 && my < 192)
                g.Text(72 + mx, my, c, kBlack);
        };
        letter("T", static_cast<int>(ctx.sim->Scalar("homestead_x")) * 16 + 8, static_cast<int>(ctx.sim->Scalar("homestead_y")) * 16 + 8);
        letter("H", static_cast<int>(ctx.sim->Scalar("view_x")) * 2, static_cast<int>(ctx.sim->Scalar("view_y")) * 2);
        const Disasters& hz = ctx.sim->Hazards();
        if (hz.Active(Disasters::kTornado))
            letter("D", hz.TornadoX() * 2, hz.TornadoY() * 2);
        if (hz.Active(Disasters::kLocusts))
            letter("L", hz.SwarmX() * 2, hz.SwarmY() * 2);
        // the Land Value box shows the chosen plot, else the plot in the middle of the view
        const int vc = _plotC >= 0 ? _plotC : viewCol, vr = _plotC >= 0 ? _plotR : viewRow;
        // (an owned plot chosen for SELL shows what selling pays)
        const bool sale = _plotC >= 0 && ctx.state->PlotOwner(vc, vr) == 1;
        g.TextRight(64, 184, std::to_string(sale ? ctx.sim->PlotSalePrice(vc, vr) : ctx.sim->PlotLandValue(vc, vr)), kBlack);
        static const char* const names[9] = { N_("Overhead"), N_("Weeds"), N_("Soil Toxicity"), N_("Groundwater"), N_("Soil Nutrients"), N_("Field Profit"), N_("Disease"),
                                              N_("Pests"), N_("Property") };
        // the caption's left edge per mode (measured in DOSBox; the original keeps a small x table per mode)
        static const int captionX[9] = { 32, 40, 16, 16, 8, 16, 32, 40, 32 };
        g.Fill(8, 196, 144, 36, kLightGrey);
        if (_mode == kOverhead || _mode == kProperty)
            g.Text(Caption(captionX[_mode], names[_mode]), 216, Tr(names[_mode]), kBlack);
        else
        {
            // the legend: Low and High over the 32-step ramp of map tiles 0x1D0.., each drawn as its 2x2 overview
            // block four times down, in a sunken bevel; the mode's name under it
            g.Text(24, 199, Tr("Low"), kBlack);
            g.Text(80, 199, Tr("High"), kBlack);
            for (int k = 0; k < 32; ++k)
                for (int r = 0; r < 8; ++r)
                {
                    const size_t at = static_cast<size_t>(2 * (0x1D0 + k) + (r & 1));
                    const int b = at < _mapc.size() ? _mapc[at] : 0;
                    for (int c = 0; c < 2; ++c)
                    {
                        int idx = 0;
                        for (int plane = 0; plane < 4; ++plane)
                            idx |= ((b >> (6 - 2 * plane + (1 - c))) & 1) << plane;
                        g.Fill(32 + 2 * k + c, 210 + r, 1, 1, idx);
                    }
                }
            g.Fill(30, 208, 66, 1, kGrey);
            g.Fill(30, 209, 65, 1, kGrey);
            g.Fill(30, 208, 1, 11, kGrey);
            g.Fill(31, 209, 1, 9, kGrey);
            g.Fill(32, 218, 65, 1, kWhite);
            g.Fill(31, 219, 66, 1, kWhite);
            g.Fill(97, 209, 1, 11, kWhite);
            g.Fill(96, 210, 1, 9, kWhite);
            g.Text(Caption(captionX[_mode], names[_mode]), 222, Tr(names[_mode]), kBlack);
        }
        // BUY / SELL in property mode, the same kind of button as CLOSE
        if (_mode == kProperty)
        {
            const bool owned = _plotC >= 0 && s.PlotOwner(_plotC, _plotR) == 1;
            g.Fill(160, 203, 48, 22, kLightGrey);
            g.Bevel(160, 203, 48, 22, true);
            g.TextCentre(184, 210, owned ? Tr("SELL") : Tr("BUY"), kBlack);
        }
        // a button once pressed and released keeps two grey pixels in its bottom-left corner, as the original's
        // toggle buttons do
        for (int m = 0; m < 8; ++m)
            if (_toggled[m])
            {
                const Rect b = Button(m);
                g.Fill(b.x + 2, b.y + 26, 1, 1, kGrey);
                g.Fill(b.x + 1, b.y + 27, 1, 1, kGrey);
            }
        // the chosen mode's button is a toggle drawn pressed (grey top/left, white bottom/right); Overhead has none
        if (_mode != kOverhead)
        {
            const Rect b = Button(_mode == kProperty ? 0 : _mode);
            g.Fill(b.x + 1, b.y + 1, 25, 1, kGrey);
            g.Fill(b.x + 1, b.y + 2, 24, 1, kGrey);
            g.Fill(b.x + 1, b.y + 3, 2, 24, kGrey);
            g.Fill(b.x + 1, b.y + 27, 1, 1, kGrey);
            g.Fill(b.x + 26, b.y + 2, 1, 1, kWhite);
            g.Fill(b.x + 25, b.y + 3, 2, 23, kWhite);
            g.Fill(b.x + 3, b.y + 26, 24, 1, kWhite);
            g.Fill(b.x + 2, b.y + 27, 25, 1, kWhite);
        }
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        for (int m = 0; m < 8; ++m)
            if (Button(m).Contains(x, y))
            {
                // each button is a toggle: pressing it picks its mode (the first one Property), pressing the
                // pressed one again releases it and the map goes back to Overhead
                const int target = m == 0 ? kProperty : m;
                _toggled[m] = true;
                _mode = _mode == target ? kOverhead : target;
                return;
            }
        if (_mode == kProperty && Rect{ 160, 203, 48, 22 }.Contains(x, y))
        {
            if (_plotC < 0)
            {
                ctx.message = Tr("Choose a plot on the map first.");
                ctx.sound = "uhoh.voc";
                return;
            }
            std::string why;
            const bool buy = ctx.state->PlotOwner(_plotC, _plotR) != 1;
            const bool ok = buy ? ctx.sim->BuyPlot(_plotC, _plotR, why) : ctx.sim->SellPlot(_plotC, _plotR, why);
            ctx.message = ok ? std::string(buy ? Tr("The land is yours.") : Tr("The land is sold.")) : why;
            ctx.sound = ok ? "plopitem.voc" : "uhoh.voc";
            return;
        }
        if (Rect{ 72, 0, 192, 192 }.Contains(x, y))
        {
            // to the map's two pixels a cell (the same on the original's map; a larger world is scaled into the box)
            const int D = 2 * ctx.state->world.size, mx = (x - 72) * D / 192, my = y * D / 192;
            if (_mode == kProperty)
            {
                _plotC = mx / 16;
                _plotR = my / 16;
            }
            else if (ctx.view)
                // the clicked cell becomes the view's top-left. Measured on the original: the view then sits 2 pixels
                // above that row's top edge
                ctx.view->SetCamera(mx / 2 * 16, my / 2 * 16 - 2);
        }
    }

private:
    // button order of the original (left, right per row): Overhead/Property, Weeds; Soil Toxicity, Groundwater;
    // Soil Nutrients, Field Profit; Disease, Pests
    enum { kOverhead, kWeeds, kToxic, kWater, kNutrients, kProfit, kDisease, kPests, kProperty };
    static Rect Button(int m) { return { 10 + (m % 2) * 30, 3 + (m / 2) * 30, 28, 28 }; }
    int FieldValue(Field& f) const
    {
        switch (_mode)
        {
        case kPests: return f.Pests();
        case kToxic: return f.Toxicity();
        case kNutrients: return f.NutrientA();
        case kWeeds: return f.Weeds();
        case kDisease: return f.Disease();
        default: return 0;
        }
    }
    // the mode and the chosen plot are session globals in the original: the window reopens as it was left
    static inline int _mode = kOverhead;
    static inline int _plotC = -1, _plotR = -1;
    bool _toggled[8] = {};        // a mode button once pressed
    std::vector<uint8_t> _mapc;   // far_mapc.mbm
};

// The game's plain rectangle fill writes whole video bytes: both edges fall back to the 8-pixel column
// they lie in, counted on the screen. originX is the screen x of the content's left edge.
static void ColumnFill(Gfx& g, int originX, int x, int y, int w, int h, int colour)
{
    const int left = (originX + x) & ~7, right = (originX + x + w) & ~7;
    if (right > left)
        g.Fill(left - originX, y, right - left, h, colour);
}

// ---------------------------------------------------------------- evaluation
class EvaluationWindow : public Window
{
public:
    EvaluationWindow() : Window(kWinEvaluation, N_("EVALUATION"), "evaluate.bmp", 352, 192) {}
    Rect CloseButton() const override { return { 289, 153, 46, 23 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        GameState& s = *ctx.state;
        const Simulation::EvalBars b = ctx.sim->Evaluation();
        static const char* const labels[5] = { N_("Farm Growth"), N_("Town Growth"), N_("Productivity"), N_("Soil Quality"), N_("Environment Quality") };
        const int bars[5] = { b.farm, b.town, b.productivity, b.soil, b.environment };
        for (int i = 0; i < 5; ++i)
        {
            g.TextFit(16, 8 + 16 * i, 176, Tr(labels[i]), kBlack); // up to the bars at 192
            int colour = kGreen;
            if (i == 3) colour = b.soilOk ? kGreen : kRed;
            if (i == 4) colour = b.environmentOk ? kGreen : kRed;
            ColumnFill(g, x, 192, 8 + 16 * i, bars[i], 8, colour);
            ColumnFill(g, x, 192 + bars[i], 8 + 16 * i, 80 - bars[i], 8, kDarkGrey);
        }
        // the season's name in the box at 288..335 (a translation has keys of their own: six letters fit)
        static const char* const seasons[4] = { N_("Winter"), N_("Spring"), N_("Summer"), N_("Fall") };
        static const char* const seasonBox[4] = { N_("season box:Winter"), N_("season box:Spring"), N_("season box:Summer"),
                                                  N_("season box:Fall") };
        const int season = s.cal.Season();
        g.DrawTile(200 + season, 304, 16);
        if (CurrentLanguage() == "en")
            g.Text(288, 40, seasons[season], kBlack);
        else
            g.TextFit(288, 40, 48, TrId(seasonBox[season], Tr(seasons[season])), kBlack);

        // problems found on any field
        int bits = 0;
        for (int i = 1; i < 98; ++i)
        {
            Field f = s.FieldAt(i);
            if (f.InUse())
                bits |= f.Status();
        }
        // problem icons: picture 16 + bit (the field status markers), each bit in its own cell of the 4x2 grid
        auto picture = [&](int n, int x0, int y0) {
            for (int k = 0; k < 4; ++k)
                g.DrawTile(539 + 4 * n + k, x0 + 16 * (k & 1), y0 + 16 * (k >> 1));
        };
        for (int bit = 0; bit < 8; ++bit)
            if (bits & (1 << bit))
                picture(16 + bit, 16 + 32 * (bit >> 1), 112 + 32 * (bit & 1));

        // suggested crops: the crop signs, filled column by column
        const std::vector<int> crops = ctx.sim->SuggestedCrops();
        for (size_t i = 0; i < crops.size() && i < 8; ++i)
            picture(crops[i], 152 + 32 * static_cast<int>(i / 2), 112 + 32 * static_cast<int>(i % 2));
    }
};

// ---------------------------------------------------------------- field schedule
class FieldWindow : public Window
{
public:
    explicit FieldWindow(int field) : Window(kWinField, N_("SCHEDULE"), "fdstatus.bmp", 336, 224), _field(field) {}
    Rect CloseButton() const override { return { 272, 101, 56, 15 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        GameState& s = *ctx.state;
        Simulation& sim = *ctx.sim;
        Field f = s.FieldAt(_field);
        if (!f.InUse())
        {
            closed = true;
            return;
        }
        if (_start < 0)
            _start = s.cal.AbsWeek() - s.cal.AbsWeek() % 4;
        const int crop = f.Crop() & 15;
        // a sign picture: tiles 539 + 4n as 2x2
        auto picture = [&](int n, int x0, int y0) {
            for (int k = 0; k < 4; ++k)
                g.DrawTile(539 + 4 * n + k, x0 + 16 * (k & 1), y0 + 16 * (k >> 1));
        };

        // crop grid, tool cells, scroll bar, chosen tool
        for (int c = 0; c < 16; ++c)
        {
            g.DrawTile(832 + c, CropCell(c).x, CropCell(c).y); // the green-framed crop icons
            // with the water drop pressed each crop shows its water need (P20 / 2), with the thermometer its warmth
            // (P10 / 7 + 50 degrees F), in white, odd columns 6 pixels lower
            const int col = c % 8, rowy = CropCell(c).y + 2 + 6 * (col & 1);
            if (_waterView)
                g.Text(CropCell(c).x, rowy, std::to_string(s.crops[c].WaterMin() / 2), kWhite);
            else if (_heatView)
                g.Text(CropCell(c).x, rowy, std::to_string(s.crops[c].HeatNeed() / 7 + 50), kWhite);
        }
        // a toggle that is on is drawn pressed: grey top/left and white bottom/right edges inside its 18 x 23 frame
        // (the control's bit 0, flipped by a click)
        // one redrawn raised after being on keeps grey bottom-left corner pixels, like the category buttons
        for (int b = 0; b < 2; ++b)
        {
            const int bx = b ? 236 : 82, by = 13;
            if (b ? _heatView : _waterView)
            {
                g.Fill(bx + 1, by + 1, 15, 1, kGrey);
                g.Fill(bx + 1, by + 2, 14, 1, kGrey);
                g.Fill(bx + 16, by + 2, 1, 1, kWhite);
                g.Fill(bx + 1, by + 3, 2, 18, kGrey);
                g.Fill(bx + 1, by + 21, 1, 1, kGrey);
                g.Fill(bx + 15, by + 3, 2, 17, kWhite);
                g.Fill(bx + 3, by + 20, 14, 1, kWhite);
                g.Fill(bx + 2, by + 21, 15, 1, kWhite);
            }
            else if (_toggled[b])
            {
                g.Fill(bx + 2, by + 20, 1, 1, kGrey);
                g.Fill(bx + 1, by + 21, 1, 1, kGrey);
            }
        }
        for (int i = 0; i < 6; ++i)
        {
            const Rect r = ToolCell(i);
            if (kToolCode[i] >= 848)
                ColumnFill(g, x, r.x, r.y, 16, 16, kLightGrey);
            g.DrawAnyTile(kToolCode[i], r.x, r.y);
        }
        for (int k = 0; k < 10; ++k)
            g.DrawTile(9, 88 + 16 * k, 88);
        g.DrawTile(0xC2, 88 + (_start / 4) / 6 * 16, 88);
        Rect sel{};
        if (_tool >= 0x340 && _tool <= 0x34F)
        {
            sel = CropCell(_tool - 0x340);
            picture(_tool - 0x340, 272, 8);
        }
        for (int i = 0; i < 6; ++i)
            if (_tool == kToolCode[i])
            {
                sel = ToolCell(i);
                picture(kToolPicture[i], 272, 8);
            }
        if (!_cut && sel.w)
            g.Frame(sel.x, sel.y, 16, 16, kYellow);

        // TODAY, CUT and CLOSE get a one-pixel deeper raised edge than the picture has
        for (const int by : { 61, 77, 101 })
        {
            g.Fill(318, by + 2, 1, 1, kGrey);
            g.Fill(317, by + 3, 1, 10, kGrey);
            g.Fill(275, by + 13, 43, 1, kGrey);
            g.Fill(274, by + 14, 1, 1, kGrey);
        }

        // season of today, three month labels, 12 week cells from _start
        static const char* const seasons[4] = { N_("Winter"), N_("Spring"), N_("Summer"), N_("Fall") };
        // (a translation may use the box's whole width, 16 characters: "Primavera 2026")
        LabelText(g, 104, 108, TrF("{0} {1}", Tr(seasons[s.cal.Season()]), s.cal.year), CurrentLanguage() == "en" ? 12 : 16);
        static const char* const months[12] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"), N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec") };
        for (int m = 0; m < 3; ++m)
            LabelText(g, 96 + 64 * m, 50, Tr(months[((_start / 4) + m) % 12]), 3);
        const int now = s.cal.AbsWeek();
        for (int i = 0; i < 12; ++i)
        {
            const int row = _start + i;
            const int code = row < 240 ? s.ScheduleCode(row, _field) : 0;
            if (code > 0x350)
                ColumnFill(g, x, 72 + 16 * i, 72, 16, 16, kLightGrey);
            if (code > 0 && code < 848 + 200)
                g.DrawAnyTile(code, 72 + 16 * i, 72);
            // the week's rain or temperature value; the original reads the weather profile's first year at
            // (first month shown) * 4 + i, whatever year the timeline shows
            const int wk = ((_start / 4) % 12) * 4 + i;
            if (_waterView)
                g.Text(72 + 16 * i, 74 + 6 * (i & 1), std::to_string(static_cast<int8_t>(s.WeatherRain(wk))), kWhite);
            else if (_heatView)
                g.Text(72 + 16 * i, 74 + 6 * (i & 1), std::to_string(s.WeatherTemp(wk)), kWhite);
        }
        // today: a 3-pixel yellow bar 6 pixels into the current week's cell
        if (now >= _start && now < _start + 12)
            g.Fill(72 + 16 * (now - _start) + 6, 72, 3, 16, kYellow);

        picture(f.Crop(), 32, 8);

        // gauges: 48x24 dials of fldgrph.til; faces 0-4 disease/pests/weeds, 5-9 water band, 10-14 soil
        auto gauge = [&](int face, int gx) { g.DrawPicture("fldgrph.til$" + std::to_string(face), gx, 176); };
        gauge(f.Disease() / 52, 24);
        gauge(f.Pests() / 52, 80);
        gauge(f.Weeds() / 52, 136);
        gauge(10 + f.NutrientA() / 52, 272);
        const CropDef& cd = s.crops[crop];
        const int lo = std::max(0, static_cast<int>(static_cast<int8_t>(cd.WaterMin() - 2 * f.FullDitches())));
        const int hi = static_cast<int8_t>(2 * f.EmptyDitches() + cd.WaterMax());
        const int mid = (hi - lo) / 2 + lo;
        const unsigned m = static_cast<unsigned>(f.Moisture());
        const int band = m < static_cast<unsigned>(lo) ? 0 : m < static_cast<unsigned>(mid) ? 1 : m < static_cast<unsigned>(hi) ? 2 : 4;
        gauge(5 + band, 216);

        // maturity bar, value, quality sign
        int maturity = 0, quality = 65535;
        const int64_t value = sim.HarvestValue(f, &maturity, &quality);
        ColumnFill(g, x, 104, 147, maturity, 8, kBlue);
        ColumnFill(g, x, 104 + maturity, 147, 80 - maturity, 8, kLightGrey);
        LabelText(g, 120, 132, "$" + std::to_string(value), 7);
        picture(24 + (quality >> 14), 272, 128);
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        GameState& s = *ctx.state;
        Simulation& sim = *ctx.sim;
        Field f = s.FieldAt(_field);
        for (int c = 0; c < 16; ++c)
            if (CropCell(c).Contains(x, y))
            {
                _tool = 0x340 + c;
                _cut = false;
            }
        for (int i = 0; i < 6; ++i)
            if (ToolCell(i).Contains(x, y))
            {
                _tool = kToolCode[i];
                _cut = false;
            }
        if (Rect{ 272, 61, 56, 15 }.Contains(x, y))
            _start = s.cal.AbsWeek() - s.cal.AbsWeek() % 4;
        if (Rect{ 272, 77, 56, 15 }.Contains(x, y))
            _cut = !_cut;
        if (Rect{ 73, 89, 15, 14 }.Contains(x, y))
            _start = std::max(0, _start - 4);
        if (Rect{ 249, 89, 15, 14 }.Contains(x, y))
            _start = std::min(228, _start + 4);
        // water drop / thermometer: two toggle buttons, at most one on, that show numbers over the crop icons and the
        // timeline
        if (Rect{ 82, 13, 17, 23 }.Contains(x, y))
        {
            _waterView = !_waterView;
            _heatView = false;
            _toggled[0] = true;
        }
        if (Rect{ 236, 13, 17, 23 }.Contains(x, y))
        {
            _heatView = !_heatView;
            _waterView = false;
            _toggled[1] = true;
        }
        if (!Rect{ 72, 72, 192, 16 }.Contains(x, y))
            return;
        const int row = _start + (x - 72) / 16;
        const int now = s.cal.AbsWeek();
        // the click router: the contract tool takes only the current week, every other tool (CUT
        // included) only a later week; the weekly step then runs the cell (seen live: a plant click on the current
        // week does nothing, on the next week it is stamped)
        if (!_cut && _tool == 0xC6)
        {
            if (row == now)
                sim.LockContract(f);
            return;
        }
        if (row <= now)
            return;
        if (_cut)
        {
            sim.CutPlan(_field, row);
            return;
        }
        if (_tool >= 0x340 && _tool <= 0x34F)
            sim.StampPlant(_field, _tool - 0x340, row);
        else if (_tool == 0x1B8)
        {
            s.SetScheduleCode(row, _field, 0x1B8);
            for (int r = row + 1; r < 240; ++r)
            {
                const int c = s.ScheduleCode(r, _field);
                if (c >= 688 && c < 816)
                    s.SetScheduleCode(r, _field, 0);
                else
                    break;
            }
        }
        else if (_tool >= 0x390 && _tool <= 0x393)
            sim.StampOperation(_field, row, _tool);
    }

private:
    static constexpr int kToolCode[6] = { 0x1B8, 0xC6, 0x393, 0x392, 0x391, 0x390 };
    static constexpr int kToolPicture[6] = { 30, 29, 23, 16, 20, 21 }; // the Tool box sign of each tool cell
    static Rect CropCell(int c) { return { 104 + (c % 8) * 16, 8 + (c / 8) * 16, 16, 16 }; }
    static Rect ToolCell(int i) { return { 24 + (i % 2) * 16, 64 + (i / 2) * 16, 16, 16 }; }
    // text on a light grey strip n characters wide
    void LabelText(Gfx& g, int tx, int ty, const std::string& s, int n) const
    {
        ColumnFill(g, x, tx, ty, 8 * n, 8, kLightGrey);
        g.Text(tx, ty, s.substr(0, static_cast<size_t>(n)), kBlack);
    }
    int _field;
    int _start = -1;
    int _tool = 0x340; // corn is chosen when the window opens
    bool _cut = false;
    bool _waterView = false, _heatView = false; // the water drop / thermometer toggles
    bool _toggled[2] = {}; // a toggle once pressed
    std::string _note;
};

// ---------------------------------------------------------------- BUY
// The item description comes from the player's itemtext.dat (14-character lines, '@' padding, 8 lines per item).
static std::vector<std::string> ItemText(const Megadata* mega, int id)
{
    std::vector<std::string> out;
    if (!mega)
        return out;
    static std::vector<std::string> lines;
    if (lines.empty())
    {
        const std::vector<uint8_t> d = mega->Read("itemtext.dat");
        std::string cur;
        for (uint8_t c : d)
        {
            if (c == '\n')
            {
                lines.push_back(cur);
                cur.clear();
            }
            else if (c != '\r')
                cur += static_cast<char>(c == '@' || c == '~' ? ' ' : c);
        }
        lines.push_back(cur);
    }
    // a translation ("item:<id>"): the name, '~', the description
    if (const std::string tr = TrId("item:" + std::to_string(id), ""); !tr.empty())
    {
        const size_t cut = tr.find('~');
        std::string rest = cut == std::string::npos ? std::string() : tr.substr(cut + 1);
        std::replace(rest.begin(), rest.end(), '~', ' ');
        return { tr.substr(0, cut), rest };
    }
    for (int k = 0; k < 7; ++k)
    {
        const size_t i = static_cast<size_t>(id) * 8 + k;
        out.push_back(i < lines.size() ? lines[i] : std::string());
    }
    return out;
}

// the original's raised box (the scroll thumb): black frame, two-pixel white top/left and grey bottom/right edges
// meeting in light grey corner pixels
static void RaisedBox(Gfx& g, int x, int y, int w, int h)
{
    g.Fill(x, y, w, h, kLightGrey);
    g.Frame(x, y, w, h, kBlack);
    g.Fill(x + 1, y + 1, w - 3, 1, kWhite);
    g.Fill(x + 1, y + 2, w - 4, 1, kWhite);
    g.Fill(x + w - 3, y + 2, 1, 1, kLightGrey);
    g.Fill(x + w - 2, y + 2, 1, h - 4, kGrey);
    g.Fill(x + 1, y + 3, 2, h - 6, kWhite);
    g.Fill(x + w - 3, y + 3, 1, h - 6, kGrey);
    g.Fill(x + 1, y + h - 3, 1, 1, kWhite);
    g.Fill(x + 3, y + h - 3, w - 4, 1, kGrey);
    g.Fill(x + 2, y + h - 2, w - 3, 1, kGrey);
}
// a 28 x 28 category button shown pressed (BUY and SELL): the edges swap (grey top/left, white bottom/right) and a grey shadow
// runs along the right and bottom just outside the frame; the picture inside stays
static void SunkenButton(Gfx& g, int x, int y)
{
    const int w = 28, h = 28;
    g.Frame(x, y, w, h, kBlack);
    g.Fill(x + 1, y + 1, w - 3, 1, kGrey);
    g.Fill(x + w - 2, y + 1, 1, 1, kLightGrey);
    g.Fill(x + 1, y + 2, w - 4, 1, kGrey);
    g.Fill(x + w - 3, y + 2, 1, 1, kLightGrey);
    g.Fill(x + w - 2, y + 2, 1, 1, kWhite);
    g.Fill(x + 1, y + 3, 2, h - 6, kGrey);
    g.Fill(x + w - 3, y + 3, 2, h - 4, kWhite);
    g.Fill(x + 1, y + h - 3, 2, 1, kGrey);
    g.Fill(x + 3, y + h - 3, w - 6, 1, kWhite);
    g.Fill(x + 1, y + h - 2, 1, 1, kGrey);
    g.Fill(x + 2, y + h - 2, w - 4, 1, kWhite);
    g.Fill(x + w, y + 3, 1, h - 2, kGrey);
    g.Fill(x + 2, y + h, w - 1, 1, kGrey);
}

// a category button released again is redrawn raised: its bottom-left corner turns grey
static void ReleasedButton(Gfx& g, int x, int y)
{
    g.Fill(x + 2, y + 25, 1, 1, kGrey);
    g.Fill(x + 1, y + 26, 1, 1, kGrey);
}

class BuyWindow : public Window
{
public:
    BuyWindow() : Window(kWinBuy, N_("BUY"), "buywndo.bmp", 256, 224) {}
    Rect CloseButton() const override { return { 201, 185, 48, 22 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        GameState& s = *ctx.state;
        const std::vector<int> items = ShopItems(_cat);
        const int rec = items[_item % items.size()];
        // the chosen category sunken, released ones raised (as in SELL)
        SunkenButton(g, 10, 6 + 30 * _cat);
        _pressed |= 1 << _cat;
        for (int c = 0; c < 5; ++c)
            if (c != _cat && (_pressed >> c & 1))
                ReleasedButton(g, 10, 6 + 30 * c);
        for (int k = 0; k < 2; ++k) // the up / down arrows, likewise once pressed
            if (_pressed >> (5 + k) & 1)
            {
                g.Fill(178 + 31 * k, 97, 1, 1, kGrey);
                g.Fill(177 + 31 * k, 98, 1, 1, kGrey);
            }
        g.DrawBank("alltiles.btl", ShopPicture(s, rec), 48, 8);
        // preview (checked on the original): 4 x 3 cells of bare ground, each a random one of tiles 19..22 drawn anew
        // for every item shown, the item's map picture at map size in the middle
        if (rec != _shownRec)
        {
            _shownRec = rec;
            for (int& t : _ground)
                t = 19 + static_cast<int>(_uiRandom() % 4);
        }
        for (int k = 0; k < 12; ++k)
            g.DrawTile(_ground[k], 176 + 16 * (k % 4), 16 + 16 * (k / 4));
        if (rec >= 21 && rec <= 36)
            g.DrawTile(832 + rec - 21, 176 + 24, 16 + 16); // a seed: the crop's green-framed icon (from its crop file)
        else
        {
            const size_t r18 = static_cast<size_t>(rec) * 18;
            const int base = FamilyStart(s.catalog[r18]);
            const int w = std::clamp<int>(s.catalog[r18 + 8], 1, 4), h = std::clamp<int>(s.catalog[r18 + 9], 1, 3);
            int k = 0;
            if (base >= 0)
                for (int ty = 0; ty < h; ++ty)
                    for (int tx = 0; tx < w; ++tx)
                        g.DrawAnyTile(base + k++, 176 + (64 - 16 * w) / 2 + 16 * tx, 16 + (48 - 16 * h) / 2 + 16 * ty);
        }
        // the name, then the description re-wrapped by words to 13 characters, 8 px per line
        // (a seed shows its crop's name and the help card of its crop file)
        const bool seed = rec >= 21 && rec <= 36;
        std::vector<std::string> text = ItemText(ctx.mega, ShopTextId(s, rec));
        if (seed)
        {
            text = CropHelp(s.crops[rec - 21]);
            text.insert(text.begin(), CropName(s, rec - 21));
        }
        // (a translation shows the engine's translated name: the item file's names are English)
        if (CurrentLanguage() == "en" || text.empty() || seed)
            g.Text(48, 97, (text.empty() ? ShopName(s, rec) : text[0]).substr(0, 13), kBlack);
        else
        {
            const std::string name = ShopName(s, rec);
            g.TextFit(48, 97, 104, name.empty() ? text[0].substr(0, 13) : name, kBlack);
        }
        std::vector<std::string> lines;
        std::string line;
        for (size_t i = 1; i < text.size(); ++i)
        {
            std::string w;
            for (size_t c = 0; c <= text[i].size(); ++c)
            {
                if (c < text[i].size() && text[i][c] != ' ')
                {
                    w += text[i][c];
                    continue;
                }
                if (w.empty())
                    continue;
                if (!line.empty() && line.size() + 1 + w.size() > 13)
                {
                    lines.push_back(line);
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + w;
                w.clear();
            }
        }
        if (!line.empty())
            lines.push_back(line);
        for (size_t i = 0; i < lines.size() && i < 10; ++i)
            g.Text(48, 113 + 8 * static_cast<int>(i), lines[i], kBlack);
        g.Text(176, 128, std::to_string(ShopPrice(s, rec)), kBlack);
        g.Text(176, 160, std::to_string(s.CatalogOwn(ShopOwnRecord(rec))), kBlack);
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        GameState& s = *ctx.state;
        for (int c = 0; c < 5; ++c)
            if (Rect{ 11, 7 + 30 * c, 27, 27 }.Contains(x, y))
            {
                _cat = c;
                _item = 0;
            }
        const int n = static_cast<int>(ShopItems(_cat).size());
        // up steps forward through the records, down back (wrapping), as in the original
        if (Rect{ 177, 73, 26, 26 }.Contains(x, y))
        {
            _item = (_item + 1) % n;
            _pressed |= 1 << 5;
        }
        if (Rect{ 208, 73, 26, 26 }.Contains(x, y))
        {
            _item = (_item + n - 1) % n;
            _pressed |= 1 << 6;
        }
        if (Rect{ 169, 185, 31, 22 }.Contains(x, y))
        {
            const int rec = ShopItems(_cat)[_item % n];
            if (ShopNeedsPlacing(rec))
            {
                // the original closes the BUY window and leaves the placement tool on (each click on the map buys
                // one) until another tool is chosen (live: windmill, fertilizer, cow)
                ctx.placeRecord = rec;
                ctx.message = rec >= 21 && rec <= 36 ? TrF("Click on a building to store a bag of {0}. Each click buys one.", ShopName(s, rec))
                                                     : TrF("Click on your land to place: {0}. Each click buys one.", ShopName(s, rec));
                closed = true;
            }
            else
            {
                const EditResult r = ShopBuy(s, *ctx.sim, rec);
                ctx.message = r.message;
                ctx.sound = r.sound;
            }
        }
    }

    // the engine's keys (the original's windows take no arrow keys): Up / Down step through the items as the arrow
    // buttons do, Left / Right change the category
    bool KeyDown(SDL_Keycode k, UiContext&) override
    {
        const int n = static_cast<int>(ShopItems(_cat).size());
        if (k == SDLK_UP)
            _item = (_item + 1) % n;
        else if (k == SDLK_DOWN)
            _item = (_item + n - 1) % n;
        else if (k == SDLK_LEFT || k == SDLK_RIGHT)
        {
            _cat = (_cat + (k == SDLK_RIGHT ? 1 : 4)) % 5;
            _item = 0;
        }
        else
            return false;
        return true;
    }

private:
    static inline int _cat = 0, _pressed = 0; // the chosen category is kept while the program runs, as in SELL
    int _item = 0;
    int _shownRec = -1;
    int _ground[12] = {};
    std::minstd_rand _uiRandom{ std::random_device{}() }; // interface-only randomness, never the game's generators
};

// ---------------------------------------------------------------- SELL
class SellWindow : public Window
{
public:
    SellWindow() : Window(kWinSell, N_("SELL"), "sellwndo.bmp", 192, 256) {}
    Rect CloseButton() const override { return { 129, 217, 46, 22 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        static const int kinds[3] = { 0, 1, 2 }; // crops, machinery, livestock (icon order)
        _list = SaleList(*ctx.state, *ctx.sim, kinds[_cat]);
        SunkenButton(g, 16, 2 + 30 * _cat); // the chosen category shows pressed
        _pressed |= 1 << _cat;
        for (int c = 0; c < 3; ++c)
            if (c != _cat && (_pressed >> c & 1))
                ReleasedButton(g, 16, 2 + 30 * c);
        for (int k = 0; k < 2; ++k) // the up / down arrows, likewise once pressed (seen live)
            if (_pressed >> (3 + k) & 1)
            {
                g.Fill(18, 96 + 80 * k + 13, 1, 1, kGrey);
                g.Fill(17, 96 + 80 * k + 14, 1, 1, kGrey);
            }
        _sel = std::clamp(_sel, 0, std::max(0, static_cast<int>(_list.size()) - 1));
        _top = std::clamp(_top, 0, std::max(0, static_cast<int>(_list.size()) - kRows));
        {
            // the list's scroll thumb on the track between the arrows (also shown, at the top, when the list is empty)
            const int n = static_cast<int>(_list.size()), span = 176 - 112 - 14;
            const int ty = 112 + (n > kRows ? span * _top / (n - kRows) : 0);
            RaisedBox(g, 16, ty, 16, 16);
        }
        if (_list.empty())
        {
            // the original: the market-scale picture and two capital lines at the list's top-left corner
            g.DrawBank("alltiles.btl", 48, 56, 8);
            g.Text(40, 96, Tr("NOTHING"), kBlack);
            g.Text(40, 104, Tr("FOR SALE"), kBlack);
            return;
        }
        g.DrawBank("alltiles.btl", _list[_sel].picture, 56, 8);
        g.Text(48, 80, _list[_sel].label, kBlack);
        // three rows of 32 pixels: the thing's own map picture, its rating badge (tiles
        // 539 + 4 * (27 - quality), four of them: HIGH, FAIR, POOR, BAD), then "$ value"; the row frames go over
        // the badges, the chosen row's frame in yellow
        for (int r = 0; r < kRows; ++r)
        {
            const int y = 96 + 32 * r;
            g.Fill(40, y, 136, 32, kLightGrey);
            if (_top + r >= static_cast<int>(_list.size()))
                continue;
            const SaleItem& it = _list[_top + r];
            if (it.record >= 0)
            {
                const size_t rec = static_cast<size_t>(it.record) * 18;
                const int base = FamilyStart(ctx.state->catalog[rec]);
                const int w = std::clamp<int>(ctx.state->catalog[rec + 8], 1, 2), h = std::clamp<int>(ctx.state->catalog[rec + 9], 1, 2);
                int k = 0;
                for (int ty = 0; ty < h; ++ty)
                    for (int tx = 0; tx < w; ++tx)
                        g.DrawAnyTile(base + k++, 48 + 16 * tx, y + 16 * ty + (h == 1 ? 8 : 0));
            }
            const int badge = 539 + 4 * (27 - std::clamp(it.quality, 0, 3));
            for (int k = 0; k < 4; ++k)
                g.DrawTile(badge + k, 80 + 16 * (k & 1), y + 16 * (k >> 1));
            g.Text(112, y + 12, "$ " + std::to_string(it.value), kBlack);
        }
        for (int r = 0; r < kRows; ++r)
            g.Frame(40, 96 + 32 * r, 137, 32, kBlack); // one column wider than the yellow frame
        if (_sel >= _top && _sel < _top + kRows)
        {
            const int y = 96 + 32 * (_sel - _top);
            g.Frame(40, y, 136, 32, kYellow);
            g.Frame(41, y + 1, 134, 30, kYellow);
        }
        g.Text(40, 200, TrF("Item: {0} of {1}", _sel + 1, _list.size()), kBlack);
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        for (int c = 0; c < 3; ++c)
            if (Rect{ 17, 3 + 30 * c, 27, 27 }.Contains(x, y))
            {
                _cat = c;
                _sel = _top = 0;
            }
        // the arrows move the chosen row by one; the list scrolls only to keep it in view (seen live: down twice from
        // item 1 frames the third row, a third time scrolls by one with item 4 at the bottom, up then frames item 3)
        const int n = static_cast<int>(_list.size());
        if (n > 0 && (Rect{ 16, 96, 16, 16 }.Contains(x, y) || Rect{ 16, 176, 16, 16 }.Contains(x, y)))
        {
            _sel = std::clamp(_sel + (y >= 176 ? 1 : -1), 0, n - 1);
            _top = std::clamp(_top, _sel - kRows + 1, _sel);
        }
        if (Rect{ 16, 96, 16, 16 }.Contains(x, y))
            _pressed |= 1 << 3;
        if (Rect{ 16, 176, 16, 16 }.Contains(x, y))
            _pressed |= 1 << 4;
        if (Rect{ 40, 96, 136, 96 }.Contains(x, y))
            _sel = std::min(_top + (y - 96) / 32, std::max(0, static_cast<int>(_list.size()) - 1));
        if (Rect{ 89, 217, 38, 22 }.Contains(x, y) && _sel < static_cast<int>(_list.size()))
        {
            const EditResult r = ShopSell(*ctx.state, *ctx.sim, _list[_sel]);
            ctx.message = r.message;
            ctx.sound = r.sound;
        }
    }

    // the engine's keys: Up / Down move the chosen row (the list scrolls with it), Left / Right change the category
    bool KeyDown(SDL_Keycode k, UiContext&) override
    {
        const int n = static_cast<int>(_list.size());
        if ((k == SDLK_UP || k == SDLK_DOWN) && n > 0)
        {
            _sel = std::clamp(_sel + (k == SDLK_DOWN ? 1 : -1), 0, n - 1);
            _top = std::clamp(_top, _sel - kRows + 1, _sel);
        }
        else if (k == SDLK_LEFT || k == SDLK_RIGHT)
        {
            _cat = (_cat + (k == SDLK_RIGHT ? 1 : 2)) % 3;
            _sel = _top = 0;
        }
        else if (k != SDLK_UP && k != SDLK_DOWN)
            return false;
        return true;
    }

private:
    static constexpr int kRows = 3;
    static inline int _cat = 0; // the chosen category is kept while the program runs (the original reopens on it)
    int _pressed = 0; // categories and arrows pressed in this opening (bits; a new opening starts clean, seen live)
    int _sel = 0, _top = 0;
    std::vector<SaleItem> _list;
};

// ---------------------------------------------------------------- SAVE / LOAD GAME
// Saves go to the settings folder (never into the SimFarm folder); both folders' saves are listed.
// As in the original: 16 rows, the selection drives the scroll thumb and the view, the selected file's name
// fills the name field, and typing overwrites the name from the cursor cell.
class FileWindow : public Window
{
public:
    explicit FileWindow(bool save) : Window(save ? kWinSave : kWinLoad, save ? N_("SAVE GAME") : N_("LOAD GAME"), "filegenr.bmp", 208, 208), _save(save) { closeBox = false; }
    Rect CloseButton() const override { return { 144, 56, 48, 24 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        if (!_scanned)
            Scan(ctx);
        // the name field: the cursor is a red cell over the character it will overwrite
        if (_cursor < 8)
            g.Fill(24 + 8 * _cursor, 24, 8, 8, kRed);
        for (int k = 0; k < static_cast<int>(_name.size()); ++k)
            g.Text(24 + 8 * k, 24, _name.substr(k, 1), k == _cursor ? kWhite : kBlack);
        const int n = static_cast<int>(_files.size());
        for (int r = 0; r < kRows && _top + r < n; ++r)
        {
            const int y = 56 + 8 * r;
            const bool sel = _top + r == _sel;
            if (sel)
                g.Fill(24, y, 96, 8, kRed);
            g.Text(24, y, _files[_top + r].first, sel ? kWhite : kBlack);
        }
        // the thumb shows the selection's place in the whole list
        RaisedBox(g, 128, 72 + (n > 1 && _sel > 0 ? 80 * _sel / (n - 1) : 0), 16, 16);
        // an arrow pressed once keeps a grey bottom-left corner (as the released category buttons)
        for (int k = 0; k < 2; ++k)
            if (_pressed[k])
            {
                g.Fill(130, 56 + 112 * k + 13, 1, 1, kGrey);
                g.Fill(129, 56 + 112 * k + 14, 1, 1, kGrey);
            }
        g.DrawButton(_save ? 12 : 7, 144, 24);
        if (!_note.empty())
            g.Text(8, 192, _note.substr(0, 24), kRed);
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        const int n = static_cast<int>(_files.size());
        if (Rect{ 128, 56, 16, 16 }.Contains(x, y))
        {
            _pressed[0] = true;
            if (_sel > 0)
                Select(_sel - 1);
        }
        else if (Rect{ 128, 168, 16, 16 }.Contains(x, y))
        {
            _pressed[1] = true;
            if (_sel + 1 < n)
                Select(_sel + 1);
        }
        else if (Rect{ 128, 72, 16, 96 }.Contains(x, y) && n > 0)
            Select(std::min(80, y - 72) * (n - 1) / 80); // the thumb jumps to the click
        else if (Rect{ 24, 56, 96, 8 * kRows }.Contains(x, y) && _top + (y - 56) / 8 < n)
            Select(_top + (y - 56) / 8);
        else if (Rect{ 144, 24, 48, 24 }.Contains(x, y))
            Commit(ctx);
        else if (Rect{ 16, 16, 120, 24 }.Contains(x, y) && ctx.askText)
        {
            // the name field on a phone: the browser asks for the name (letters, digits, '_' and '-', at most 8)
            std::string typed = _name;
            if (ctx.askText(Tr(_save ? N_("Save the farm as (up to 8 letters and digits):") : N_("Load the save named:")), typed))
            {
                std::string name;
                for (char c : typed)
                    if (name.size() < 8 && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' '))
                        name += c == ' ' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                _name = name;
                _cursor = static_cast<int>(_name.size());
            }
        }
    }

    bool KeyDown(SDL_Keycode k, UiContext& ctx) override
    {
        // letters, digits, '_' and '-'; a space becomes '_'; at most 8 characters
        if (k == SDLK_BACKSPACE)
        {
            if (_cursor > 0)
                _name.resize(static_cast<size_t>(--_cursor));
        }
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
            Commit(ctx);
        else if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN || k == SDLK_HOME || k == SDLK_END)
        {
            // the engine's keys: move through the file list
            const int n = static_cast<int>(_files.size());
            if (n > 0)
            {
                const int step = k == SDLK_UP ? -1 : k == SDLK_DOWN ? 1 : k == SDLK_PAGEUP ? -kRows : k == SDLK_PAGEDOWN ? kRows : k == SDLK_HOME ? -n : n;
                Select(std::clamp((_sel < 0 ? 0 : _sel) + step, 0, n - 1));
            }
        }
        else if ((k >= SDLK_a && k <= SDLK_z) || (k >= SDLK_0 && k <= SDLK_9) || k == SDLK_MINUS || k == SDLK_SPACE)
        {
            if (_cursor < 8)
            {
                _name.resize(static_cast<size_t>(_cursor));
                _name += k == SDLK_SPACE ? '_' : static_cast<char>(std::toupper(static_cast<int>(k)));
                ++_cursor;
            }
        }
        else
            return false;
        return true;
    }

private:
    static constexpr int kRows = 16;
    void Select(int i)
    {
        _sel = i;
        _top = std::clamp(_top, _sel - kRows + 1, _sel);
        _name = _files[i].first.substr(0, _files[i].first.find('.'));
    }
    void Scan(UiContext& ctx)
    {
        _scanned = true;
        _files.clear();
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path saves = fs::path(ConfigDir()) / "saves";
        fs::create_directories(saves, ec);
        auto add = [&](const fs::path& dir) {
            for (const auto& e : fs::directory_iterator(dir, ec))
            {
                std::string up = e.path().filename().string();
                for (char& c : up)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                const bool known = std::any_of(_files.begin(), _files.end(), [&](const auto& f) { return f.first == up; });
                if (up.size() > 4 && up.substr(up.size() - 4) == ".SFM" && !known)
                    _files.push_back({ up, e.path().string() });
            }
        };
        add(saves); // a save of the settings folder hides the SimFarm folder's file of the same name
        if (!ctx.gameDir.empty())
            add(ctx.gameDir);
        std::sort(_files.begin(), _files.end());
        if (!_files.empty())
            Select(0);
    }
    void Commit(UiContext& ctx)
    {
        if (_save)
        {
            if (_name.empty())
                return;
            const std::string path = (std::filesystem::path(ConfigDir()) / "saves" / (_name + ".SFM")).string();
            std::string err;
            if (ctx.state->Save(path, err))
            {
                ctx.currentSave = path;
                ctx.command = "saved:" + _name + ".SFM"; // the interface's "saved" box
                ctx.sound = "plopitem.voc";
                closed = true;
            }
            else
                _note = Tr("Save failed");
            return;
        }
        for (const auto& f : _files)
            if (f.first == _name + ".SFM")
            {
                ctx.command = "loadpath:" + f.second;
                closed = true;
                return;
            }
        _note = Tr("No such saved game");
    }
    bool _save;
    bool _scanned = false;
    std::string _name, _note;
    std::vector<std::pair<std::string, std::string>> _files; // display name, path
    int _sel = -1, _top = 0, _cursor = 0;
    bool _pressed[2] = { false, false }; // up, down arrow
};

// ---------------------------------------------------------------- FARM EXPERT (help cards)
// Each card is a small script from the player's cardN.hc: pictures, text boxes, hotspots to other cards,
// navigation buttons, icon tiles and a crop-information block that is generated from the crop's parameters.
class ExpertWindow : public Window
{
public:
    ExpertWindow() : Window(kWinExpert, N_("FARM EXPERT"), "farmburo.bmp", 400, 224) {}
    // CLOSE sits where the cards put their CANCEL (back) button, so it only acts on a card without one
    Rect CloseButton() const override { return HasButtonAt(336, 200) ? Rect{ 0, 0, 0, 0 } : Rect{ 336, 200 - kTop, 48, 24 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        if (_loaded != _card)
            Load(ctx);
        // each card starts on a cleared panel; card coordinates count from the window's top-left corner,
        // the title bar included
        g.Fill(8, 0, 384, 216, kLightGrey);
        g.DrawButton(15, 336, 200 - kTop);
        for (const Op& o : _ops)
            switch (o.op)
            {
            case 0x86: g.DrawBank("alltiles.btl", o.a[0], o.a[1], o.a[2] - kTop); break;
            case 0x87: g.Wrap(o.a[0], o.a[1] - kTop, o.a[2], o.text, kBlack); break;
            case 0x8c:
                for (int k = 0; k < o.a[2]; ++k)
                    g.DrawTile(o.a[4] + k, o.a[0] + 16 * k, o.a[1] - kTop);
                break;
            case 0x84: g.DrawButton(o.a[2], o.a[0], o.a[1] - kTop); break;
            case 0x89: CropInfo(g, ctx, o.a[0], o.a[1], o.a[2] - kTop); break;
            default: break;
            }
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        y += kTop;
        for (const Op& o : _ops)
        {
            if (o.op == 0x83 && x >= o.a[0] && y >= o.a[1] && x < o.a[2] && y < o.a[3])
            {
                _card = o.a[4];
                ctx.sound = "click.voc";
                return;
            }
            if (o.op == 0x84 && Rect{ o.a[0], o.a[1], 48, 24 }.Contains(x, y))
            {
                _card = o.a[3];
                ctx.sound = "click.voc";
                return;
            }
            if (o.op == 0x8a && x >= o.a[0] && y >= o.a[1] && x < o.a[2] && y < o.a[3])
                ctx.sound = o.text;
        }
    }

private:
    static constexpr int kTop = 16; // title bar height
    struct Op
    {
        int op;
        std::vector<int> a;
        std::string text;
    };
    bool HasButtonAt(int x, int y) const
    {
        for (const Op& o : _ops)
            if (o.op == 0x84 && o.a[0] == x && o.a[1] == y)
                return true;
        return false;
    }
    void Load(UiContext& ctx)
    {
        static const std::map<int, std::pair<int, bool>> spec = { { 0x83, { 5, false } }, { 0x84, { 4, false } }, { 0x86, { 3, false } }, { 0x87, { 3, true } },
                                                                   { 0x89, { 3, false } }, { 0x8a, { 4, true } }, { 0x8b, { 2, false } }, { 0x8c, { 5, false } } };
        _ops.clear();
        _loaded = _card;
        const std::vector<uint8_t> d = ctx.mega ? ctx.mega->Read("card" + std::to_string(_card) + ".hc") : std::vector<uint8_t>();
        size_t i = 0;
        int texts = 0;
        while (i < d.size() && d[i] != 0xFF)
        {
            const auto it = spec.find(d[i]);
            if (it == spec.end())
                break;
            Op o{ d[i++], {}, {} };
            for (int k = 0; k < it->second.first && i + 1 < d.size(); ++k, i += 2)
                o.a.push_back(d[i] | (d[i + 1] << 8));
            if (it->second.second)
                while (i < d.size() && d[i])
                    o.text += static_cast<char>(d[i++]);
            i += it->second.second ? 1 : 0;
            if (o.op == 0x87) // a text box: its translation by "card:<card>:<n>", n counting the card's text boxes
                o.text = TrId("card:" + std::to_string(_card) + ":" + std::to_string(texts++), o.text);
            _ops.push_back(std::move(o));
        }
    }
    // the crop's information card from its crop file, in a box 30 characters wide (the original's lines break
    // somewhere between 29 and 31 characters; no crop file tells them apart)
    static void CropInfo(Gfx& g, UiContext& ctx, int n, int x, int y)
    {
        const CropDef& c = ctx.state->crops[n & 15];
        if (c.loaded)
            g.Wrap(x, y, 248, osf::CropInfo(c), kBlack);
    }
    int _card = 1, _loaded = 0;
    std::vector<Op> _ops;
};

// ---------------------------------------------------------------- EXAMINE card
// A 208x208 card drawn while the button is held: the picture at (48, 24), then the texts.
// itemtext.dat is read as 128-byte records at 128 x n, the file's own layout (some records begin with a line feed).
static std::string ItemTextRecord(const Megadata* mega, int id, bool nameOnly)
{
    if (const std::string tr = id >= 0 ? TrId("item:" + std::to_string(id), "") : std::string(); !tr.empty())
        return nameOnly ? tr.substr(0, tr.find('~')) : tr; // a translation: the name, '~', the description
    static std::vector<uint8_t> d;
    if (d.empty() && mega)
        d = mega->Read("itemtext.dat");
    std::string out;
    for (size_t i = static_cast<size_t>(id) * 128; id >= 0 && i < d.size() && i < static_cast<size_t>(id + 1) * 128 && d[i]; ++i)
    {
        if (nameOnly && (d[i] == '~' || d[i] == '@'))
            break;
        out += static_cast<char>(d[i]);
    }
    return out;
}

void DrawExamineCard(Gfx& g, const Megadata* mega, const ExamineCard& c, int x, int y)
{
    g.DrawPicture("examine.bmp", x, y);
    g.DrawBank("alltiles.btl", c.picture, x + 48, y + 24);
    // one line of text: a control character takes a blank cell
    auto line = [&](int tx, int ty, std::string t) {
        for (auto& ch : t)
            if (static_cast<unsigned char>(ch) < 0x20)
                ch = ' ';
        g.Text(tx, ty, t, kBlack);
    };
    if (c.kind == ExamineCard::kText)
    {
        g.Wrap(x + 16, y + 96, 160, c.textId >= 0 ? ItemTextRecord(mega, c.textId, false) : c.text, kBlack);
        return;
    }
    if (c.kind == ExamineCard::kLine)
    {
        line(x + 24, y + 96, c.text);
        return;
    }
    line(x + 24, y + 96, ItemTextRecord(mega, c.textId, true));
    if (c.kind == ExamineCard::kStructure)
    {
        line(x + 24, y + 106, Tr("Damage:"));
        // the original's bar: two plain fills, so both parts end on 8-pixel screen columns
        const int red = std::min(64, c.damage >> 2);
        ColumnFill(g, 0, x + 96, y + 106, red, 8, kRed);
        ColumnFill(g, 0, x + 96 + red, y + 106, 64 - red, 8, kGrey);
        if (c.slots)
            for (int cc = 0; cc < c.cols; ++cc)
                for (int r = 0; r < c.rows; ++r)
                    g.DrawAnyTile(c.tiles[static_cast<size_t>(cc * c.rows + r)], x + 24 + 16 * cc, y + 116 + 16 * r);
        return;
    }
    // a machine, animal or item: its map tiles (masked ones on light grey), the rating badge and the value
    for (int r = 0, k = 0; r < c.rows; ++r)
        for (int cc = 0; cc < c.cols; ++cc, ++k)
        {
            if (c.firstTile >= 848)
                g.Fill(x + 32 + 16 * cc, y + 112 + 16 * r, 16, 16, kLightGrey);
            g.DrawAnyTile(c.firstTile + k, x + 32 + 16 * cc, y + 112 + 16 * r);
        }
    const int badge = 539 + 4 * (27 - std::clamp(c.quality, 0, 3));
    for (int k = 0; k < 4; ++k)
        g.DrawTile(badge + k, x + 56 + 16 * (k & 1), y + 104 + 16 * (k >> 1));
    line(x + 96, y + 116, "$" + std::to_string(c.value));
}

// ---------------------------------------------------------------- LOAD CROP
class CropWindow : public Window
{
public:
    CropWindow() : Window(kWinCrop, N_("LOAD CROP"), "choscrop.bmp", 320, 224) {}
    Rect CloseButton() const override { return { 249, 185, 46, 22 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        if (_files.empty())
            Scan(ctx);
        const GameState& s = *ctx.state;
        g.Text(16, 24, CropName(s, _slot).substr(0, 16), kBlack);
        if (_file >= 0)
            g.Text(160, 24, _files[_file].first.substr(0, 16), kBlack);
        // the 16 slots: each crop's 32x32 sign (tiles 539 + 4c ..), the chosen one framed in yellow
        for (int c = 0; c < 16; ++c)
        {
            const Rect r = Slot(c);
            for (int k = 0; k < 4; ++k)
                g.DrawTile(539 + 4 * c + k, r.x + 16 * (k & 1), r.y + 16 * (k >> 1));
            if (c == _slot)
                g.Frame(r.x, r.y, 32, 32, kYellow);
        }
        // the crop files by name, 16 rows
        _top = std::clamp(_top, 0, std::max(0, static_cast<int>(_files.size()) - kRows));
        for (int i = 0; i < kRows && _top + i < static_cast<int>(_files.size()); ++i)
        {
            const int y = 48 + 8 * i;
            const bool sel = _top + i == _file;
            if (sel)
                g.Fill(160, y, 96, 8, kRed);
            g.Text(160, y, _files[_top + i].second, sel ? kWhite : kBlack);
        }
        // the scroll thumb on the track between the arrows
        const int n = static_cast<int>(_files.size());
        RaisedBox(g, 288, 64 + (n > kRows ? 80 * _top / (n - kRows) : 0), 16, 16);
        if (!_note.empty())
            g.Text(16, 190, _note.substr(0, 20), kRed);
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        for (int c = 0; c < 16; ++c)
            if (Slot(c).Contains(x, y))
                _slot = c;
        if (Rect{ 160, 48, 128, 8 * kRows }.Contains(x, y))
        {
            const int i = _top + (y - 48) / 8;
            if (i >= 0 && i < static_cast<int>(_files.size()))
                _file = i;
        }
        if (Rect{ 288, 48, 16, 16 }.Contains(x, y))
            _top = std::max(0, _top - 1);
        if (Rect{ 288, 160, 16, 16 }.Contains(x, y))
            _top++;
        if (Rect{ 185, 185, 62, 22 }.Contains(x, y) && _file >= 0)
        {
            std::string why;
            // the original says nothing on success; a refusal is an event box (in use / already loaded)
            if (!ctx.sim->ReplaceCrop(_slot, _files[_file].first, why) && why != "in use")
                _note = why;
        }
    }

    // the engine's keys (the original takes no arrow keys here): Up / Down move through the crop files, Left / Right
    // through the 16 slots
    bool KeyDown(SDL_Keycode k, UiContext&) override
    {
        const int n = static_cast<int>(_files.size());
        if ((k == SDLK_UP || k == SDLK_DOWN) && n > 0)
        {
            _file = std::clamp((_file < 0 ? 0 : _file) + (k == SDLK_DOWN ? 1 : -1), 0, n - 1);
            _top = std::clamp(_top, _file - kRows + 1, _file);
        }
        else if (k == SDLK_LEFT || k == SDLK_RIGHT)
            _slot = (_slot + (k == SDLK_RIGHT ? 1 : 15)) % 16;
        else if (k != SDLK_UP && k != SDLK_DOWN)
            return false;
        return true;
    }

private:
    static constexpr int kRows = 16;
    static Rect Slot(int c) { return { 16 + (c % 4) * 32, 48 + (c / 4) * 32, 32, 32 }; }
    void Scan(UiContext& ctx)
    {
        // every .CRP file of the crops folder, in file-name order, shown by the crop's own name; the first is chosen
        std::error_code ec;
        auto dir = FindFileNoCase(ctx.gameDir, "CROPS");
        if (!dir)
            return;
        for (const auto& e : std::filesystem::directory_iterator(*dir, ec))
        {
            std::string n = e.path().filename().string();
            for (char& c : n)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (n.size() <= 4 || n.substr(n.size() - 4) != ".CRP")
                continue;
            CropDef def;
            _files.emplace_back(n, LoadCropFile(ctx.gameDir, n, def) ? CropName(def) : n);
        }
        std::sort(_files.begin(), _files.end());
        _file = _files.empty() ? -1 : 0;
    }
    int _slot = 0, _file = -1, _top = 0;
    std::vector<std::pair<std::string, std::string>> _files; // file, crop name
    std::string _note;
};

// ---------------------------------------------------------------- AIRPLANE
class AirplaneWindow : public Window
{
public:
    explicit AirplaneWindow(int plane) : Window(kWinAirplane, N_("AIRPLANE"), "airpwndo.bmp", 256, 176), _plane(plane) {}
    Rect CloseButton() const override { return { 197, 146, 44, 20 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        // the dials are fldgrph.til faces 48 x 24 (gas and spray value / 52 + 10, damage wear / 52); the
        // three numbers are what the buttons cost: the tank's missing fuel at 1, the missing load at a tenth of the
        // chemical's price per unit, and 10 per point of wear
        const GameState& s = *ctx.state;
        const int fuel = Flight::Fuel(s, _plane), load = Flight::Load(s, _plane), wear = Flight::Wear(s, _plane);
        g.DrawPicture("fldgrph.til$" + std::to_string(std::clamp(fuel, 0, 255) / 52 + 10), 104, 16);
        g.DrawPicture("fldgrph.til$" + std::to_string(std::clamp(load, 0, 255) / 52 + 10), 104, 64);
        g.DrawPicture("fldgrph.til$" + std::to_string(std::clamp(wear, 0, 255) / 52), 104, 112);
        g.Text(24, 44, std::to_string(255 - fuel), kBlack);
        g.Text(24, 99, std::to_string((255 - load) * 20), kBlack);
        g.Text(24, 154, std::to_string(wear * 10), kBlack);
        // a left button clicked once is redrawn released: its bottom-left corner turns grey
        for (int k = 0; k < 3; ++k)
            if (_released & (1 << k))
            {
                g.Fill(23, 3 + 55 * k + 35, 1, 1, kGrey);
                g.Fill(22, 3 + 55 * k + 36, 1, 1, kGrey);
            }
        // the chosen chemical's 38 x 38 button is drawn pressed: the bevel's white and grey swap (checked on the original)
        static const int items[3] = { 0xDD, 0xDE, 0xDF };
        for (int k = 0; k < 3; ++k)
            if (Flight::Chemical(s, _plane) == items[k])
            {
                const int bx = 197, by = 5 + 40 * k, n = 38;
                g.Fill(bx + 1, by + 1, n - 3, 1, kGrey);
                g.Fill(bx + 1, by + 2, n - 4, 1, kGrey);
                g.Fill(bx + n - 3, by + 2, 1, 1, kLightGrey);
                g.Fill(bx + n - 2, by + 2, 1, 1, kWhite);
                g.Fill(bx + 1, by + 3, 2, n - 6, kGrey);
                g.Fill(bx + n - 3, by + 3, 2, n - 6, kWhite);
                g.Fill(bx + 1, by + n - 3, 2, 1, kGrey);
                g.Fill(bx + 3, by + n - 3, n - 4, 1, kWhite);
                g.Fill(bx + 1, by + n - 2, 1, 1, kGrey);
                g.Fill(bx + 2, by + n - 2, n - 3, 1, kWhite);
            }
    }

    void Click(int x, int y, UiContext& ctx) override
    {
        Flight& f = ctx.sim->Plane();
        auto pay = [&](bool ok, const char* what) {
            ctx.message = ok ? Tr(what) : Tr("Not enough money, or nothing to do.");
            ctx.sound = ok ? "plopitem.voc" : "uhoh.voc";
        };
        for (int k = 0; k < 3; ++k)
            if (Rect{ 21, 3 + 55 * k, 38, 38 }.Contains(x, y))
                _released |= 1 << k;
        if (Rect{ 21, 3, 38, 38 }.Contains(x, y))
            pay(f.Refuel(_plane), N_("The tank is full."));
        if (Rect{ 21, 58, 38, 38 }.Contains(x, y))
            pay(f.Reload(_plane), N_("The spray tank is loaded."));
        if (Rect{ 21, 113, 38, 38 }.Contains(x, y))
            pay(f.Repair(_plane), N_("The plane is repaired."));
        static const int items[3] = { 0xDD, 0xDE, 0xDF };
        for (int k = 0; k < 3; ++k)
            if (Rect{ 197, 5 + 40 * k, 38, 38 }.Contains(x, y))
                f.SetChemical(_plane, items[k]);
        if (Rect{ 197, 125, 44, 20 }.Contains(x, y))
        {
            if (f.Start(_plane))
            {
                closed = true;
                ctx.message = Tr("Arrows fly, F sprays, Enter lands (or the buttons below)");
                // the original flies also from a paused game; in the engine Paused holds the plane (F42), so a take-off
                // from a paused game sets Normal
                if (ctx.sim->GetSpeed() == Speed::Paused)
                    ctx.sim->SetSpeed(Speed::Normal);
            }
        }
    }

private:
    int _plane;
    int _released = 0;
};

std::unique_ptr<Window> MakeAirplaneWindow(int plane) { return std::make_unique<AirplaneWindow>(plane); }

// ---------------------------------------------------------------- ABOUT (credits re-authored)
class AboutWindow : public Window
{
public:
    AboutWindow() : Window(kWinAbout, N_("ABOUT"), "about.bmp", 400, 224) {}
    Rect CloseButton() const override { return { 0, 0, 400, 224 }; } // any click closes it

    void DrawContent(Gfx& g, UiContext&) override
    {
        static const char* const lines[] = { "OpenSimFarm " OSF_VERSION, "", N_("An open re-implementation"), N_("that runs on your own copy"), N_("of SimFarm (Maxis, 1993)."),
                                             "", N_("Free software under the"), N_("GNU GPL version 3."), N_("No original files included.") };
        int y = 70;
        for (const char* l : lines)
        {
            g.TextFit(150, y, 216, *l ? Tr(l) : l, kBlack);
            y += 11;
        }
        // the picture's registration line ("this copy is registered to") is covered with the panel's grey, and the
        // line credits OpenSimFarm's author (user finding Q18)
        g.Fill(96, 187, 208, 11, kLightGrey);
        g.TextCentre(200, 189, Tr("OpenSimFarm by Kalinke"), kBlack);
    }
};

// ---------------------------------------------------------------- HELP (F1): OpenSimFarm's own controls summary
class HelpWindow : public Window
{
public:
    // a plain panel with one entry per control, the description wrapped beside its name and a gap between entries
    // (user finding F32: the text ran over the picture's edges and was hard to read); a longer translation makes the
    // window taller, up to the screen
    HelpWindow() : Window(kWinHelp, N_("HOW TO PLAY"), "", 560, 328) { h = std::clamp(Layout(nullptr), 328, 416); }
    Rect CloseButton() const override { return { 0, 0, 560, h }; }

    void DrawContent(Gfx& g, UiContext&) override
    {
        g.Fill(0, 0, 560, h, kLightGrey);
        g.Bevel(0, 0, 560, h, true);
        Layout(&g);
    }

private:
    static constexpr std::pair<const char*, const char*> kEntries[] = {
        { N_("Mouse"), N_("Menus, toolbar and windows. The tools on the left work on the map; drag to paint roads, fences and ditches.") },
        { N_("Right button"), N_("On a building: show what is stored inside. Elsewhere: drop the chosen tool.") },
        { N_("Scroll"), N_("Right or middle drag, arrow keys or WASD, the scroll bars, or the map edges (check box under -).") },
        { N_("Zoom"), N_("Mouse wheel or the + and - buttons.") },
        { N_("Speed"), N_("1 Slow, 2 Normal, 3 Fast, 4 Ultra, 0 Pause. Space pauses and resumes.") },
        { N_("Save, load"), N_("F5 quick save, F9 quick load.") },
        { N_("Ctrl+1/2/3"), N_("Interface size.") },
        { N_("Esc"), N_("Drop the tool or close the window.") },
        { N_("Fields"), N_("PLANT a crop on 8 x 8 cells of clear land you own, one free cell away from other fields. Machines do the work.") },
        { N_("Water"), N_("Wet soil? Dig empty ditches along the field. Dry? Fill ditches from a pump.") },
        { N_("Examine"), N_("Click a field for its schedule.") },
        { N_("Land"), N_("Map, Property mode: buy more land.") },
        { N_("Start"), N_("Click a yellow box for a ready-made farm, or a place on land for a new one, then USE THIS REGION.") },
        { N_("Crop duster"), N_("Left / Right (or A / D) turn, Up / Down (or W / S) climb and dive, F sprays, Enter lands; or the SPRAY and LAND buttons. Space and 0-4 still pause and set the speed.") },
    };

    // draws the entries (g set) and returns the height they take
    static int Layout(Gfx* g)
    {
        constexpr int kKeyX = 12, kTextX = 116, kChars = (560 - kTextX - 12) / 8;
        int y = 10;
        for (const auto& [key, text] : kEntries)
        {
            if (g)
                g->TextFit(kKeyX, y, kTextX - kKeyX - 4, Tr(key), kBlue);
            // words onto lines of kChars characters
            std::string line, word;
            const std::string t = std::string(Tr(text)) + " ";
            for (char ch : t)
            {
                if (ch != ' ')
                {
                    word += ch;
                    continue;
                }
                if (!line.empty() && line.size() + 1 + word.size() > static_cast<size_t>(kChars))
                {
                    if (g)
                        g->Text(kTextX, y, line, kBlack);
                    y += 10;
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + word;
                word.clear();
            }
            if (!line.empty() && g)
                g->Text(kTextX, y, line, kBlack);
            y += 15; // a gap between entries (all fit in the window, user finding: the Help text ran over)
        }
        return y - 5;
    }
};

// ---------------------------------------------------------------- FUNDS (an engine addition: the money log)
class FundsWindow : public Window
{
public:
    FundsWindow() : Window(kWinFunds, N_("FUNDS"), "", 368, 224) {}
    static constexpr int kRows = 17;
    Rect Up() const { return { 300, 202, 28, 16 }; }
    Rect Down() const { return { 332, 202, 28, 16 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        g.Fill(0, 0, w, h, kLightGrey);
        g.Text(8, 4, Tr("Date"), kBlack);
        g.Text(104, 4, Tr("Money in and out"), kBlack);
        g.TextRight(352, 4, Tr("Amount"), kBlack);
        g.Fill(4, 15, w - 8, 1, kGrey);
        if (!ctx.sim)
            return;
        ctx.sim->ReconcileCash();
        const auto& log = ctx.sim->CashLog();
        const int n = static_cast<int>(log.size());
        _top = std::clamp(_top, 0, std::max(0, n - kRows));
        if (n == 0)
            g.Text(8, 22, Tr("No money has changed hands yet."), kBlack);
        for (int r = 0; r < kRows && _top + r < n; ++r)
        {
            const Simulation::CashEntry& e = log[static_cast<size_t>(n - 1 - _top - r)];
            const int y = 20 + 10 * r;
            g.Text(8, y, TrF("W{0} {1} {2}", e.week + 1, Tr(kMonthShort[e.month % 12]), e.year), kBlack);
            std::string what = e.what + (e.count > 1 ? " x" + std::to_string(e.count) : "");
            g.Text(104, y, what.substr(0, 20), kBlack);
            g.TextRight(352, y, (e.amount > 0 ? "+" : "-") + std::to_string(e.amount > 0 ? e.amount : -e.amount), e.amount > 0 ? kDarkGreen : kRed);
        }
        g.Fill(4, 197, w - 8, 1, kGrey);
        g.Text(8, 206, TrF("Funds  {0}", ctx.state ? ctx.state->cash : 0), kBlack);
        for (int k = 0; k < 2; ++k)
        {
            const Rect b = k == 0 ? Up() : Down();
            g.Fill(b.x, b.y, b.w, b.h, kLightGrey);
            g.Bevel(b.x, b.y, b.w, b.h, true);
            for (int i = 0; i < 5; ++i) // a small triangle, up (newer) or down (older)
                g.Fill(b.x + 14 - i, b.y + (k == 0 ? 5 + i : 10 - i), 2 * i + 1, 1, kBlack);
        }
    }
    void Click(int x, int y, UiContext&) override
    {
        if (Up().Contains(x, y))
            _top = std::max(0, _top - kRows);
        else if (Down().Contains(x, y))
            _top += kRows;
    }
    bool KeyDown(SDL_Keycode k, UiContext&) override
    {
        if (k == SDLK_UP) _top = std::max(0, _top - 1);
        else if (k == SDLK_DOWN) _top++;
        else if (k == SDLK_PAGEUP) _top = std::max(0, _top - kRows);
        else if (k == SDLK_PAGEDOWN) _top += kRows;
        else if (k == SDLK_HOME) _top = 0;
        else return false;
        return true;
    }

private:
    int _top = 0;
};

// ---------------------------------------------------------------- SOUND (an engine addition: volumes, Modern Sound)
class SoundWindow : public Window
{
public:
    SoundWindow() : Window(kWinSound, N_("SOUND"), "", 256, 112) {}
    static constexpr int kTrackX = 76, kStep = 12; // 0 .. 10 in steps of 12 pixels
    Rect Track(int row) const { return { kTrackX - 2, 8 + 22 * row, 10 * kStep + 4, 12 }; }
    Rect Check() const { return { 8, 54, 12, 12 }; }

    void DrawContent(Gfx& g, UiContext& ctx) override
    {
        g.Fill(0, 0, w, h, kLightGrey);
        if (!ctx.audio)
            return;
        const int vol[2] = { ctx.audio->MusicVolume(), ctx.audio->EffectsVolume() };
        static const char* const kName[2] = { N_("Music"), N_("Effects") };
        for (int k = 0; k < 2; ++k)
        {
            const Rect t = Track(k);
            g.Text(8, t.y + 2, Tr(kName[k]), kBlack);
            g.Fill(t.x, t.y, t.w, t.h, kWhite);
            g.Bevel(t.x, t.y, t.w, t.h, false);
            if (vol[k] > 0)
                g.Fill(t.x + 2, t.y + 2, vol[k] * kStep, t.h - 4, kBlue);
            g.TextRight(w - 4, t.y + 2, std::to_string(vol[k] * 10) + "%", kBlack);
        }
        const Rect c = Check();
        g.Fill(c.x, c.y, c.w, c.h, kWhite);
        g.Bevel(c.x, c.y, c.w, c.h, false);
        if (ctx.audio->Modern())
            for (int i = 0; i < 6; ++i) // a tick
            {
                g.Fill(c.x + 2 + i, c.y + 5 + (i < 2 ? i : 2 - (i - 2)), 2, 2, kBlack);
            }
        g.Text(28, c.y + 2, Tr("Modern sound"), kBlack);
        g.Text(8, 74, ctx.audio->Modern() ? Tr("Cleaner effects in stereo,") : Tr("The sound as the original"), kDarkGrey);
        g.Text(8, 86, ctx.audio->Modern() ? Tr("new instruments for songs.") : Tr("plays it."), kDarkGrey);
    }
    void Click(int x, int y, UiContext& ctx) override
    {
        if (!ctx.audio)
            return;
        for (int k = 0; k < 2; ++k)
            if (Rect t = Track(k); x >= t.x - 6 && x < t.x + t.w + 6 && y >= t.y - 3 && y < t.y + t.h + 3)
            {
                const int v = std::clamp((x - kTrackX + kStep / 2) / kStep, 0, 10);
                if (k == 0)
                    ctx.audio->SetMusicVolume(v);
                else
                {
                    ctx.audio->SetEffectsVolume(v);
                    ctx.sound = "cow1.voc"; // hear the new level
                }
            }
        if (Rect c = Check(); x >= c.x && x < 140 && y >= c.y - 2 && y < c.y + c.h + 2)
            ctx.command = "modernsound";
    }
    bool KeyDown(SDL_Keycode k, UiContext& ctx) override
    {
        if (!ctx.audio || (k != SDLK_LEFT && k != SDLK_RIGHT))
            return false;
        ctx.audio->SetEffectsVolume(ctx.audio->EffectsVolume() + (k == SDLK_RIGHT ? 1 : -1));
        return true;
    }
};

// ---------------------------------------------------------------- LANGUAGE (an engine addition: change the language while playing)
class LanguageWindow : public Window
{
public:
    static constexpr int kRowH = 14;
    LanguageWindow() : Window(kWinLanguage, N_("LANGUAGE"), "", 184, 8), _langs(AvailableLanguages())
    {
        h = 8 + kRowH * static_cast<int>(_langs.size()) + 4;
    }
    Rect Row(size_t i) const { return { 8, 6 + kRowH * static_cast<int>(i), w - 16, kRowH - 2 }; }

    void DrawContent(Gfx& g, UiContext&) override
    {
        g.Fill(0, 0, w, h, kLightGrey);
        for (size_t i = 0; i < _langs.size(); ++i)
        {
            const Rect r = Row(i);
            const bool current = _langs[i].code == CurrentLanguage();
            if (current)
                g.Fill(r.x, r.y, r.w, r.h, kBlue);
            g.TextFit(r.x + 4, r.y + 2, r.w - 8, _langs[i].name, current ? kWhite : kBlack);
        }
    }
    void Click(int x, int y, UiContext& ctx) override
    {
        for (size_t i = 0; i < _langs.size(); ++i)
            if (Row(i).Contains(x, y) && _langs[i].code != CurrentLanguage())
                ctx.command = "language:" + _langs[i].code;
    }
    bool KeyDown(SDL_Keycode k, UiContext& ctx) override
    {
        if (k != SDLK_UP && k != SDLK_DOWN)
            return false;
        size_t cur = 0;
        for (size_t i = 0; i < _langs.size(); ++i)
            if (_langs[i].code == CurrentLanguage())
                cur = i;
        const size_t next = k == SDLK_UP ? (cur + _langs.size() - 1) % _langs.size() : (cur + 1) % _langs.size();
        ctx.command = "language:" + _langs[next].code;
        return true;
    }

private:
    std::vector<LanguageInfo> _langs;
};

std::unique_ptr<Window> MakeFieldWindow(int field) { return std::make_unique<FieldWindow>(field); }

std::unique_ptr<Window> MakeWindow(int kind)
{
    switch (kind)
    {
    case kWinBalance: return std::make_unique<BalanceWindow>();
    case kWinBuy: return std::make_unique<BuyWindow>();
    case kWinHelp: return std::make_unique<HelpWindow>();
    case kWinAbout: return std::make_unique<AboutWindow>();
    case kWinCrop: return std::make_unique<CropWindow>();
    case kWinExpert: return std::make_unique<ExpertWindow>();
    case kWinSave: return std::make_unique<FileWindow>(true);
    case kWinLoad: return std::make_unique<FileWindow>(false);
    case kWinSell: return std::make_unique<SellWindow>();
    case kWinMarket: return std::make_unique<MarketWindow>();
    case kWinWeather: return std::make_unique<WeatherWindow>();
    case kWinBank: return std::make_unique<BankWindow>();
    case kWinMap: return std::make_unique<MapWindow>();
    case kWinEvaluation: return std::make_unique<EvaluationWindow>();
    case kWinFunds: return std::make_unique<FundsWindow>();
    case kWinSound: return std::make_unique<SoundWindow>();
    case kWinLanguage: return std::make_unique<LanguageWindow>();
    default: return nullptr;
    }
}

} // namespace osf
