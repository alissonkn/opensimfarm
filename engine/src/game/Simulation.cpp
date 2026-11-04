// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Simulation.h"
#include <optional>
#include <filesystem>
#include "core/Paths.h"
#include "assets/Graphics.h"

#include "game/Editing.h"
#include "game/Shop.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace osf {

namespace {

constexpr double kTicksPerSecond = kTimerHz; // 50 Hz
constexpr int kCropTileBase = 688;                      // 688 + 8*crop + stage
constexpr int kBareSoil = 0x24;
constexpr int kFallow = 0xD1, kHarvestMark = 0x1B8;  // timeline codes

int SignedByte(int v)
{
    v &= 0xFF;
    return v >= 128 ? v - 256 : v;
}

// Rain-day patterns by rain class: 1 = precipitation day.
constexpr int kRainDays[8][7] = {
    { 0, 0, 0, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0, 0, 0 }, { 0, 0, 1, 0, 0, 1, 0 }, { 1, 0, 0, 1, 1, 0, 0 },
    { 1, 1, 0, 0, 0, 1, 1 }, { 0, 1, 1, 1, 1, 1, 0 }, { 1, 1, 1, 1, 1, 1, 0 }, { 1, 1, 1, 1, 1, 1, 1 },
};

const char* kAnimalNames[4] = { "horse", "cow", "pig", "sheep" };
constexpr int kHungerStep[4] = { 2, 2, 1, 1 };
constexpr int kAnimalCatalog[4] = { 45, 46, 47, 48 }; // catalogue records of horse, cow, pig, sheep
constexpr int kThirstStep[4] = { 8, 8, 4, 2 };
constexpr int kAdultAge[4] = { 192, 80, 32, 12 };
constexpr int kLifespan[4] = { 960, 960, 720, 480 };
constexpr int kGestation[4] = { 48, 40, 44, 24 };

// half-price leases

} // namespace

Simulation::Simulation(GameState& state) : _s(state), _machines(std::make_unique<Machines>(state, *this)), _animals(std::make_unique<Animals>(state, *this)), _traffic(std::make_unique<Traffic>(state, *this)), _irrigation(std::make_unique<Irrigation>(state, *this)), _disasters(std::make_unique<Disasters>(state, *this)), _flight(std::make_unique<Flight>(state, *this))
{
    const auto seed = static_cast<uint16_t>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x3FFF);
    _rng.SetState(seed ? seed : 1);
    _g2 = static_cast<uint16_t>(seed * 40503u + 1u);
    if (_g2 == 0)
        _g2 = 1; // an all-zero shift register would stay zero
    _speed = state.speedDelay <= 64 ? Speed::Ultra : state.speedDelay <= 128 ? Speed::Fast : state.speedDelay <= 256 ? Speed::Normal : Speed::Slow;
    _profileBaseYear = 1993;

    // the soil wetness is not saved: the original keeps the value in memory, which is 8 after starting the program
    // (seen after every fresh load and new game), so a loaded farm starts at 8 and dries or wets from there
    _wetness = 8;
    _machines->RepairStorage(); // shown inside views are drawn again by Advance
    // a field that is not in a hand harvest's last days keeps no picker pictures (F40, see ClearStubble)
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (f.InUse() && !(f.State() == 5 && f.Sub() >= 12))
            ClearStubble(f);
    }
    // an earlier engine version's year change left the new last year of every timeline empty, so an old field's plan
    // ran out (user finding: an orchard's schedule showed no weeks). The original fills that year with fallow weeks
    // and carries the plan on; a field whose last year is empty gets the same, and a growing perennial with no plan
    // left gets its yearly plan from this week on (the plant stamp's perennial rule).
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse())
            continue;
        bool empty = true;
        for (int r = 192; r < 240 && empty; ++r)
            empty = _s.ScheduleCode(r, i) == 0;
        if (!empty || ExtendPlan(i))
            continue;
        const int crop = f.Crop() & 15;
        const CropDef& c = _s.crops[crop];
        const int hw = c.HarvestWeek();
        if (!c.Perennial() || hw == 0 || f.State() < 3)
            continue;
        for (int r = _s.cal.AbsWeek(); r < 240; ++r)
        {
            const int dx = r % 48;
            _s.SetScheduleCode(r, i, dx == hw ? kHarvestMark : kCropTileBase + 8 * crop + ((dx - hw + 48) % 48) / 6);
        }
    }
    // a field whose sign cells hold bare soil or crop pictures lost its sign to an earlier engine version's hand
    // harvest (user finding F39): it is put back. The original's farms have every sign.
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse() || f.W() < 2 || f.H() < 2)
            continue;
        bool lost = true;
        for (int k = 0; k < 4 && lost; ++k)
        {
            const int id = _s.world.At(f.X() + (k & 1), f.Y() + (k >> 1)).BaseTile();
            lost = id == kBareSoil || (id >= kCropTileBase && id < kCropTileBase + 128);
        }
        // a planted field showing another crop's sign (F41, see StartPlanting) gets its own
        const int sign = _s.world.At(f.X(), f.Y()).BaseTile();
        if (f.State() != 0 && sign >= 539 && sign < 539 + 64 && (sign - 539) % 4 == 0 && sign != 539 + 4 * (f.Crop() & 15))
            lost = true;
        if (lost)
            for (int k = 0; k < 4; ++k)
            {
                MapCell& c = _s.world.At(f.X() + (k & 1), f.Y() + (k >> 1));
                c.base = static_cast<uint16_t>((c.base & 0xF800) | (539 + 4 * (f.Crop() & 15) + k));
            }
    }

    _townPot = _s.scalars["town_pot"];
    // the tax board's grace counter is saved (one byte of the tail) and a running countdown goes on after loading;
    // the bank's counter is not saved (a fresh start of the original has none)
    _taxGrace = static_cast<int>(_s.scalars["tax_counter"]);
    _townCarry = 40000; // the town's reserve: not saved; a new game (and the title screen) sets 40,000
    _dailyTemp = Temperature(); // until the first day: the week's value (the day's figure is not saved)
    // price histories: generated (as the original does at game start) from the current price
    for (int c = 0; c < 16; ++c)
        GenerateHistory(c);
    ComputePattern(WeatherIndex(0), _weekPattern);
    ComputePattern(WeatherIndex(1), _nextPattern);

    // evaluation snapshots start from the loaded farm, so the growth gauges begin empty
    EvaluationSnapshot();
    _evOwnedPrev = _evOwned;
    _evTownPrev = _evTown;
    _evOwnedDelta = _evTownDelta = _evTierDelta = 0;
}

void Simulation::SetSpeed(Speed s)
{
    _speed = s;
    static const int delays[] = { 64, 128, 256, 640, 0 };
    if (s != Speed::Paused)
    {
        _s.speedDelay = delays[static_cast<int>(s)];
        _s.scalars["irr_period"] = 2; // every speed choice sets the irrigation pass period to 2 (the next continuation makes it 3)
    }
}

void Simulation::Advance(double seconds)
{
    _rawFrac += seconds * kTicksPerSecond;
    const auto whole = static_cast<int64_t>(_rawFrac);
    _rawTicks += whole;
    _rawFrac -= static_cast<double>(whole);
    struct InAdvance
    {
        bool& f;
        explicit InAdvance(bool& b) : f(b) { f = true; }
        ~InAdvance() { f = false; }
    } inAdvance(_inAdvance);
    // the original draws a shown inside view again whenever its slots change;
    // the engine draws every shown inside view from its slots once per frame
    for (int st = 1; st < 127; ++st)
    {
        const size_t b = static_cast<size_t>(st) * 70;
        const int cat = Rd16(_s.structures, b);
        if ((Rd8(_s.structures, b + 2) & 0x21) == 0x21 && cat >= 0x40 && cat <= 0x48 && cat != 0x46 && cat != 0x47)
            DrawBuilding(st);
    }
    if (_flight->Flying())
    {
        // the original's flight loop runs on the raw timer, whatever the speed and also while the game is paused
        // (seen live). The engine lets the Speed menu act on it (user finding F42): Paused holds the plane in the
        // air, Slow flies at half the pace; Normal, Fast and Ultra keep the original's pace
        if (_speed == Speed::Paused)
            return;
        _ticks += seconds * kTicksPerSecond * (_speed == Speed::Slow ? 0.5 : 1.0);
        for (int guard = 0; _ticks >= 1.0 && guard < 2048 && _flight->Flying(); ++guard)
        {
            _ticks -= 1.0;
            _flight->Tick();
        }
        _ticks = std::min(_ticks, 16.0);
        return;
    }
    if (_speed == Speed::Paused)
        return;
    const double rate = _scaledClock ? 256.0 / std::max(1, _s.speedDelay) : 1.0;
    _ticks += seconds * kTicksPerSecond * rate;
    int guard = 0;
    while (_ticks >= 1.0 && guard++ < 2048)
    {
        _ticks -= 1.0;
        TimerTick();
    }
    _ticks = std::min(_ticks, 16.0);
}

void Simulation::TimerTick()
{
    if (!_inAdvance)
        ++_rawTicks; // headless runs: one timer tick is one tick of the raw timer
    ReconcileCash();
    if (_flight->Flying())
    {
        // the simulation clock stands still while the crop duster flies
        _flight->Tick();
        return;
    }
    _machines->Tick(_speed == Speed::Ultra && !_scaledClock); // the original's Ultra step catches up with its short days
    {
        // the animal step period the speed sets (Ultra 1, Fast 8, Normal 16, Slow 32 timer ticks); with
        // the scaled clock (256-tick days) it is stretched so that the steps per day stay the original's
        const int delay = std::max(1, _s.speedDelay);
        const int period = delay <= 64 ? 1 : delay <= 128 ? 8 : delay <= 256 ? 16 : 32;
        _animals->Tick(_scaledClock ? std::max(1, period * 256 / delay) : period);
        _traffic->Tick(_scaledClock ? std::max(1, period * 256 / delay) : period); // the traffic runs on the same period
    }
    _irrigation->Tick();
    _disasters->Tick();
    // the field executor runs every frame: a growing field ticks whenever its phase equals the week of the
    // month, so one reset to phase 0 by a harvest in week 0 grows again that same week
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (f.InUse() && f.State() == 4 && f.Phase() == _s.cal.week)
            WeeklyGrowth(f);
    }
    if (++_ambient % 32 == 0)
        AmbientAnimation();
    if (_ambient % 8 == 0)
        AnimateStalls();
    if (++_dayTicks > (_scaledClock ? 256 : _s.speedDelay)) // "more than the delay": 257 ticks at Normal (measured 257-258 live)
    {
        _dayTicks = 0;
        DayTick();
    }
}

bool Simulation::TakeMessage(std::string& out)
{
    if (_messages.empty())
        return false;
    out = _messages.front();
    _messages.pop_front();
    return true;
}

bool Simulation::TakeSound(std::string& out)
{
    int x, y;
    return TakeSound(out, x, y);
}

bool Simulation::TakeSound(std::string& out, int& cellX, int& cellY)
{
    if (_sounds.empty())
        return false;
    out = _sounds.front();
    _sounds.pop_front();
    cellX = _soundAt.empty() ? -1 : _soundAt.front().first;
    cellY = _soundAt.empty() ? -1 : _soundAt.front().second;
    if (!_soundAt.empty())
        _soundAt.pop_front();
    return true;
}

void Simulation::Sound(const char* name, int cellX, int cellY)
{
    if (_sounds.size() < 8)
    {
        _sounds.push_back(name);
        _soundAt.emplace_back(cellX, cellY);
    }
}

void Simulation::Event(int id, int picture, const std::string& text, int x, int y)
{
    // the original's event poster only shows the box; the status line is
    // left alone
    GameEvent e{ id, picture, text, false, x, y };
    // ... except (an engine addition, user finding Q8) the bank and tax notices 31 .. 42, which are also written on
    // the status line, so they stay in the message history
    e.status = id >= 31 && id <= 42;
    if (_events.size() < 8)
        _events.push_back(e);
}

// Field status markers beside the crop sign (layout checked on the
// original's saves): columns 2-3 water (too dry / too wet), columns 4-5 low nutrients, columns 6-7 toxicity or one of
// pests / weeds / disease in rotation (field byte 0x36). A slot without a problem shows the field's own tile.
void Simulation::PaintMarkers(Field& f, const int (&slots)[3])
{
    // three 2x2 marker slots in the field's top rows after the sign (columns 2-3, 4-5, 6-7); -1 = show the field
    if (f.W() < 8 || f.H() < 2)
        return;
    const int rest = f.State() == 0 ? kBareSoil : (f.Get16(0x0E) ? f.Get16(0x0E) : kCropTileBase + 8 * (f.Crop() & 15));
    for (int sl = 0; sl < 3; ++sl)
        for (int k = 0; k < 4; ++k)
        {
            MapCell& c = _s.world.At(f.X() + 2 + 2 * sl + (k & 1), f.Y() + (k >> 1));
            const int id = c.BaseTile();
            if (slots[sl] < 0 && !(id >= 603 && id <= 634))
                continue;
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (slots[sl] >= 0 ? slots[sl] + k : rest));
        }
}

int Simulation::GroundTileFor(int wetness, const MapCell& c)
{
    return 15 + std::min(15, wetness + (c.terrain >> 2));
}

void Simulation::InheritSession(const Simulation& o)
{
    _rng.SetState(o._rng.State());
    _rng3.SetState(o._rng3.State());
    _g2 = o._g2;
    _dailyTemp = o._dailyTemp;
    _todaySky = o._todaySky;
    _wetness = o._wetness;
    _acc = o._acc;
    _wind = o._wind;
    _gust = o._gust;
    _interest = o._interest;
    _trend = o._trend;
    _townCarry = o._townCarry;
    _weekPattern = o._weekPattern;
    _nextPattern = o._nextPattern;
    _history = o._history;
}

bool Simulation::TakePuff(int& x, int& y)
{
    if (_puffs.empty())
        return false;
    x = _puffs.front()[0];
    y = _puffs.front()[1];
    _puffs.pop_front();
    return true;
}

bool Simulation::TakeEvent(GameEvent& out)
{
    if (_events.empty())
        return false;
    out = _events.front();
    _events.pop_front();
    return true;
}

void Simulation::Message(const std::string& m)
{
    _messages.push_back(m);
    if (_messages.size() > 50)
        _messages.pop_front();
}

int Simulation::WeatherIndex(int weekOffset) const
{
    const int profile = ((_s.cal.year - _profileBaseYear) % 5 + 5) % 5;
    return (profile * 48 + _s.cal.AbsWeek() + weekOffset) % 240;
}

// ---------------------------------------------------------------- money
static const char* CashLabel(int cat, bool income)
{
    if (cat == -1) return income ? "Miscellaneous" : "Building and clearing";
    if (cat >= 0 && cat <= 63) return income ? "Machinery sold" : "Machinery";
    if (cat <= 127) return income ? "Structure sold" : "Structures";
    if (cat <= 159) return income ? "Futures" : "Futures contract";
    if (cat <= 167) return income ? "Livestock sold" : "Livestock";
    if (cat >= 168 && cat <= 183) return "Crop sale";
    if (cat >= 192 && cat <= 215) return "Seed and supplies";
    if (cat >= 220 && cat <= 223) return "Chemicals";
    if (cat == 1000) return "Bank payment";
    if (cat == 1500) return "Machine lease";
    if (cat == 2000) return "Land bought";
    if (cat == 2500) return "Taxes";
    return income ? "Income" : "Expense";
}

void Simulation::ReconcileCash()
{
    if (_cashSeen == INT64_MIN)
        _cashSeen = _s.cash;
    else if (_s.cash != _cashSeen)
        PostCash(_s.cash - _cashSeen, "Other");
}

void Simulation::PostCash(int64_t amount, const std::string& what)
{
    _cashSeen = _s.cash;
    if (amount == 0)
        return;
    const Calendar& c = _s.cal;
    if (!_cashLog.empty())
    {
        CashEntry& e = _cashLog.back();
        // repeated small items of one day (a fence drawn cell by cell, a row of barrels) make one line
        if (e.what == what && e.day == c.day && e.week == c.week && e.month == c.month && e.year == c.year && (e.amount < 0) == (amount < 0))
        {
            e.amount += amount;
            e.count++;
            return;
        }
    }
    _cashLog.push_back({ c.day, c.week, c.month, c.year, amount, what, 1 });
    if (_cashLog.size() > 500)
        _cashLog.pop_front();
}

bool Simulation::Spend(int cat, int64_t amount, const char* label)
{
    if (_s.cash < amount)
        return false;
    ReconcileCash();
    _s.cash -= amount;
    PostCash(-amount, label ? label : CashLabel(cat, false));
    if (std::getenv("OSF_TRACE_MONEY"))
        std::fprintf(stderr, "  spend cat %d amount %lld (week %d month %d year %d)\n", cat, static_cast<long long>(amount), _s.cal.week, _s.cal.month, _s.cal.year);
    const char* line = nullptr;
    if (cat == -1) line = "exp_misc";
    else if (cat >= 0 && cat <= 63) line = "exp_machinery";
    else if (cat <= 127)
    {
        line = "exp_structures";
        _s.scalars["structures_value"] += amount; // the structures asset (not saved), part of the credit limit
    }
    else if (cat <= 159) line = "exp_futures";
    else if (cat <= 167) line = "exp_livestock";
    else if (cat >= 192 && cat <= 215) line = "exp_seed";
    else if (cat >= 220 && cat <= 223) line = "exp_chemicals";
    else if (cat == 1000) line = "exp_interest";
    else if (cat == 1500) line = "exp_leases";
    else if (cat == 2000) line = "exp_land";
    else if (cat == 2500) line = "exp_taxes";
    if (line)
        Ledger(line) += amount;
    return true;
}

void Simulation::Credit(int cat, int64_t amount, const char* label)
{
    ReconcileCash();
    const int64_t before = _s.cash;
    _s.cash = std::min<int64_t>(9999999, _s.cash + amount);
    PostCash(_s.cash - before, label ? label : CashLabel(cat, true));
    if (cat >= 168 && cat <= 183)
    {
        Ledger("rev_crops") += amount;
        Ledger("yearly_income") += amount;
    }
    else if (cat >= 128 && cat <= 159)
    {
        Ledger("exp_futures") -= amount;
        Ledger("rev_futures") += amount;
    }
    else if (cat >= 0 && cat <= 63)
        Ledger("rev_machinery") += amount;
    else if (cat >= 160 && cat <= 167)
        Ledger("rev_animals") += amount;
}

// ---------------------------------------------------------------- land, tax, credit
int64_t Simulation::PlotLandValue(int c, int r, bool asFree) const
{
    int freePlots = 0;
    for (int cc = 0; cc < 12; ++cc)
        for (int rr = 0; rr < 12; ++rr)
            if (_s.PlotOwner(cc, rr) == 0 || (asFree && cc == c && rr == r))
                freePlots++;
    // the "level" term is the high nibble of the plot's last zone attribute byte (zone record byte 6)
    const size_t zb = static_cast<size_t>(c * 12 + r) * 7 + 6;
    const int level = zb < _s.zones.size() ? (_s.zones[zb] >> 4) & 15 : 0;
    int water = 0;
    for (int x = c * 8; x < c * 8 + 8; ++x)
        for (int y = r * 8; y < r * 8 + 8; ++y)
            if (_s.world.At(x, y).base & 0x1000) // the water-reach flag of the tile word (not the overlay word)
                water++;
    return (100 * (144 - freePlots) + 16 * level + 16 * _wetness + 15000 + 500 * water) & 0xFFFF; // a 16-bit result
}

int64_t Simulation::LandValue() const
{
    int64_t total = 0;
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
            if (_s.PlotOwner(c, r) == 1)
                total += PlotLandValue(c, r);
    return total;
}

int Simulation::WealthTier() const
{
    const int64_t worth = std::max<int64_t>(0, (LandValue() + Scalar("asset_machinery") + Scalar("asset_futures") + Scalar("asset_livestock")) / 3 -
                                                   LoanBalance());
    static const int64_t bounds[7] = { 4000, 15000, 50000, 100000, 200000, 300000, 500000 };
    int tier = 0;
    while (tier < 7 && worth >= bounds[tier])
        tier++;
    return tier;
}

// ---------------------------------------------------------------- evaluation
int Simulation::FieldProblems(Field& f) const
{
    // the status test of the original's field refresh: the field's stored moisture against the crop's water range
    // widened by the ditch counts, both limits worked out in signed bytes (dry below the minimum, wet above the maximum)
    const CropDef& c = _s.crops[f.Crop() & 15];
    const unsigned water = static_cast<unsigned>(f.Moisture());
    int low = SignedByte((c.WaterMin() - 2 * f.FullDitches()) & 0xFF);
    if (low < 0)
        low = 0;
    const int high = SignedByte((2 * f.EmptyDitches() + c.WaterMax()) & 0xFF);
    int bits = 0;
    if (water < static_cast<unsigned>(low))
        bits |= 0x08;
    else if (static_cast<unsigned>(high) < water)
        bits |= 0x02;
    if (f.NutrientA() < 128)
        bits |= 0x80;
    if (f.Toxicity() > kToxicThreshold)
        bits |= 0x04;
    if (f.Pests() > 52) bits |= 0x01;
    if (f.Weeds() > 52) bits |= 0x10;
    if (f.Disease() > 52) bits |= 0x20;
    return bits;
}

void Simulation::EvaluationSnapshot()
{
    int owned = 0, town = 0;
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
        {
            owned += _s.PlotOwner(c, r) == 1;
            town += _s.PlotOwner(c, r) == 2;
        }
    // the deltas subtract the previous stored delta, literally as the original does
    _evOwnedPrev = _evOwned;
    _evOwned = owned;
    _evOwnedDelta = owned - _evOwnedDelta;
    _evTownPrev = _evTown;
    _evTown = town;
    _evTownDelta = town - _evTownDelta;
    _evTierDelta = WealthTier() - _evTierDelta;
}

Simulation::EvalBars Simulation::Evaluation()
{
    EvalBars b{};
    // the game clamps the 16-bit result as an unsigned number, so a shrinking (negative) value draws a full bar
    auto gauge = [](int v) { return static_cast<uint16_t>(v) > 80 ? 80 : v; };
    b.farm = gauge(_evOwnedDelta * 80 / std::max(1, _evOwnedPrev));
    b.town = gauge(_evTownDelta * 80 / std::max(1, _evTownPrev));
    b.productivity = gauge(_evTierDelta * 12);
    int fields = 0, lowNutrient = 0, toxic = 0;
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse())
            continue;
        fields++;
        const int p = f.Status();
        lowNutrient += (p & 0x80) != 0;
        toxic += (p & 0x04) != 0;
    }
    b.soil = std::min(80, lowNutrient * 80 / std::max(1, fields));
    b.environment = std::min(80, toxic * 80 / std::max(1, fields));
    b.soilOk = b.soil == 0;
    b.environmentOk = b.environment == 0;
    if (b.soilOk) b.soil = 80;
    if (b.environmentOk) b.environment = 80;
    return b;
}

std::vector<int> Simulation::SuggestedCrops() const
{
    int rain = 0, heat = 0, cold = 0;
    for (int k = 0; k < 16; ++k)
    {
        const int idx = (WeatherIndex() + k) % 240;
        rain += _s.WeatherRain(idx);
        const int t = _s.WeatherTemp(idx);
        if (t > 50) heat += 7 * t - 350;
        if (t < 45) cold += 45 - t;
    }
    std::vector<int> out;
    for (int c = 0; c < 16; ++c)
    {
        const CropDef& d = _s.crops[c];
        if (d.loaded && d.HeatNeed() <= heat && d.ChillNeed() / 7 <= cold && d.WaterMin() <= rain / 2)
            out.push_back(c);
    }
    return out;
}

int64_t Simulation::EstimatedTax() const
{
    const int64_t cash = _s.cash;
    const int r = cash < 150000 ? 10 : cash < 500000 ? 5 : 3;
    auto it = _s.scalars.find("yearly_income");
    const int64_t income = it == _s.scalars.end() ? 0 : it->second;
    auto due = _s.scalars.find("tax_due");
    return income / r + (due == _s.scalars.end() ? 0 : due->second) + LandValue() / 99;
}

int64_t Simulation::CreditLimit() const
{
    auto it = _s.scalars.find("loan_balance");
    const int64_t loan = it == _s.scalars.end() ? 0 : it->second;
    // the structures asset: the prices of buildings bought since the game was loaded (the original does not save it)
    auto sv = _s.scalars.find("structures_value");
    const int64_t structures = sv == _s.scalars.end() ? 0 : sv->second;
    return std::max<int64_t>(0, (LandValue() + structures) / 3 - loan);
}

bool Simulation::Borrow(int64_t amount)
{
    if (amount <= 0 || amount > CreditLimit())
        return false;
    ReconcileCash();
    const int64_t before = _s.cash;
    _s.cash = std::min<int64_t>(9999999, _s.cash + amount);
    PostCash(_s.cash - before, "Bank loan");
    Ledger("loan_balance") += amount;
    Ledger("loan_payment") += std::max<int64_t>(1, amount * (100 + _interest) / 100 / 20);
    _s.scalars["loan_credit"] = CreditLimit(); // the stored "credit available" (saved)
    return true;
}

bool Simulation::RepayAll()
{
    const int64_t balance = LoanBalance();
    if (balance <= 0 || !Spend(1000, balance)) // posted to Loan Interest
        return false;
    _townCarry += balance >> 1;
    _townPot += balance >> 2;
    _s.scalars["town_pot"] = _townPot;
    Ledger("loan_balance") = 0;
    Ledger("loan_payment") = 0;
    _loanGrace = 0;
    _s.scalars["loan_credit"] = CreditLimit();
    return true;
}

bool Simulation::Repay(int64_t amount)
{
    const int64_t balance = LoanBalance();
    if (amount >= balance)
        return RepayAll();
    if (amount <= 0 || !Spend(1000, amount))
        return false;
    _townCarry += amount >> 1;
    _townPot += amount >> 2;
    _s.scalars["town_pot"] = _townPot;
    // the quarterly payment shrinks with the debt
    Ledger("loan_payment") = std::max<int64_t>(1, Ledger("loan_payment") * (balance - amount) / balance);
    Ledger("loan_balance") = balance - amount;
    _s.scalars["loan_credit"] = CreditLimit();
    return true;
}

// ---------------------------------------------------------------- weather
// Sky of a dry day (0 clear, 1 mostly clear, 2 cloudy) from the week's temperature and a 3-bit draw of the
// shift register of the additive generator G2 (16 bits, taps at bits 1 and 2, stepped 8 times).
int Simulation::G2Step()
{
    for (int k = 0; k < 8; ++k)
    {
        const int bit = ((_g2 >> 1) ^ (_g2 >> 2)) & 1;
        _g2 = static_cast<uint16_t>((bit << 15) | (_g2 >> 1));
    }
    return _g2;
}

int Simulation::DrySky(int temp)
{
    const int r = G2Step() & 7;
    if (temp > 85)
        return r == 2 ? 1 : r == 5 ? 2 : 0;
    if (temp > 70)
        return (r == 1 || r == 7) ? 1 : (r == 2 || r == 4) ? 2 : 0;
    return (r == 0 || r == 4) ? 0 : (r == 1 || r == 3) ? 2 : 1;
}

void Simulation::ComputePattern(int idx, std::array<int, 7>& out)
{
    const int r = std::clamp(_s.WeatherRain(idx), 0, 7);
    const int temp = _s.WeatherTemp(idx);
    for (int d = 0; d < 7; ++d)
    {
        if (kRainDays[r][d])
            out[d] = temp <= 32 ? 4 : 3;
        else
            out[d] = DrySky(temp);
    }
}

void Simulation::DayTick()
{
    // The original's day order (read from its day routine): the day counter goes up and the daily field pass runs
    // (counters +1 on odd days, day 7 included); on day 7 the weekly chain runs (the wind of the ending week,
    // then a new week); then the new day's sky moves the wetness accumulator. The saved day is the last day done.
    const int newDay = _s.cal.day + 1;
    _dayCounter++;
    _markerTurn = newDay == 3 || newDay == 7; // the days the shown pest / weed / disease marker may change (F34)
    DailyFields((newDay & 1) != 0);
    _markerTurn = false;
    if (newDay & 1)
        DailyLivestock(); // the needs grow only on odd days (days 1, 3, 5, 7), inside the original's daily field pass
    FillWaterTowers(); // the end of the daily pass, before the weekly chain
    if (newDay >= 7)
    {
        // weekly wind: table value of the ending week plus the gust, which decays by one
        _wind = SignedByte(_s.WeatherWind(WeatherIndex()) + _gust);
        if (_gust > 0)
            _gust--;
        RefreshFields(true); // end of the weekly chain, still with the old week's wetness
        _s.cal.day = 0;
        WeekTick();
    }
    else
        _s.cal.day = newDay;
    // the day's temperature (the week's table value + 0..4, G3) and sky, every day after the weekly chain
    _dailyTemp = _rng3.Bounded(5) + _s.WeatherTemp(WeatherIndex());
    _todaySky = _weekPattern[std::clamp(_s.cal.day, 0, 6)];
    _disasters->Day(_wind, Temperature(), _wetness);
    // soil wetness accumulator
    const int code = _weekPattern[_s.cal.day % 7];
    const bool windy = _wind >= 15;
    int delta = 0;
    switch (code)
    {
    case 0: delta = windy ? -6 : -2; break;
    case 1: delta = windy ? -3 : -1; break;
    case 2: delta = windy ? -2 : -1; break;
    default: delta = 8; Ledger("rain_days") += 1; break;
    }
    _acc += delta;
    if (std::getenv("OSF_TRACE_WEATHER"))
        std::fprintf(stderr, "day %d/%d/%d sky %d wind %d temp %d rainclass %d acc %d wet %d\n", _s.cal.day, _s.cal.week + 1, _s.cal.month + 1, code, _wind,
                     Temperature(), _s.WeatherRain(WeatherIndex()), _acc, _wetness);
}

// Water towers (catalogue 0x48): level word +0x0A. A full ditch beside a tower adds 50 a day (at most 255); a refill
// takes the water of every tower it reaches, each emptied completely.
void Simulation::FillWaterTowers()
{
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        const int flags = Rd8(_s.structures, b + 2);
        if (!(flags & 0x20) || Rd16(_s.structures, b) != 0x48 || Rd8(_s.structures, b + 6) == 0xFF || (flags & 0x40))
            continue;
        const int x = Rd8(_s.structures, b + 4), y = Rd8(_s.structures, b + 5);
        bool ditch = false;
        for (const auto& [dx, dy] : { std::pair{ 0, -1 }, std::pair{ 1, 0 }, std::pair{ 0, 1 }, std::pair{ -1, 0 } })
            if (x + dx >= 0 && y + dy >= 0 && x + dx < kMapSize && y + dy < kMapSize && Irrigation::IsFullDitch(_s.world.At(x + dx, y + dy).BaseTile()))
                ditch = true;
        if (!ditch)
            continue;
        const int level = std::min(255, Rd16(_s.structures, b + 0x0A) + 50);
        Wr16(_s.structures, b + 0x0A, level);
        if ((flags & 1) && x < kMapSize && y < kMapSize)
            _s.world.At(x, y).base = static_cast<uint16_t>((level >> 6) + 83); // the level picture, flags cleared
    }
}

int Simulation::DrawWater(int limit)
{
    int total = 0;
    for (int i = 1; i < 127 && total < limit; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        const int flags = Rd8(_s.structures, b + 2);
        const int level = Rd16(_s.structures, b + 0x0A);
        if (!(flags & 0x20) || Rd16(_s.structures, b) != 0x48 || level == 0)
            continue;
        total = std::min(limit, total + level);
        Wr16(_s.structures, b + 0x0A, 0);
        const int x = Rd8(_s.structures, b + 4), y = Rd8(_s.structures, b + 5);
        if ((flags & 1) && x < kMapSize && y < kMapSize)
            _s.world.At(x, y).base = 83;
    }
    return total;
}

// ---------------------------------------------------------------- fields
void Simulation::Adjust(Field& f, int type, int amount)
{
    const CropDef& c = _s.crops[f.Crop() & 15];
    auto add = [&](int o, int a) { f.Set8(o, std::min(255, f.Get8(o) + a)); };
    auto sub = [&](int o, int a) { f.Set8(o, std::max(0, f.Get8(o) - a)); };
    switch (type)
    {
    case 0: Adjust(f, 13, amount); Adjust(f, 17, amount); Adjust(f, 15, amount); break;
    case 1: Adjust(f, 12, amount); Adjust(f, 16, amount); Adjust(f, 14, amount); break;
    case 2: sub(0x1E, amount); break;
    case 3: add(0x1E, amount); break;
    case 6: sub(0x14, amount); break;
    case 7: add(0x14, c.ResistDisease() ? amount >> 2 : amount); break;
    case 8: sub(0x12, amount); break;
    case 9: add(0x12, c.ResistPests() ? amount >> 2 : amount); break;
    case 10: sub(0x13, amount); break;
    case 11: add(0x13, c.ResistWeeds() ? amount >> 2 : amount); break;
    case 12: add(0x1B, amount); break;
    case 13: sub(0x1B, amount); break;
    case 14: add(0x1D, amount); break;
    case 15: if (f.Get8(0x1D) >= amount) f.Set8(0x1D, f.Get8(0x1D) - amount); break;
    case 16: add(0x1C, amount); break;
    case 17: if (f.Get8(0x1C) >= amount) f.Set8(0x1C, f.Get8(0x1C) - amount); break;
    default: break;
    }
}

void Simulation::DailyFields(bool alternateDay)
{
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse())
            continue;
        if (alternateDay)
        {
            Adjust(f, 1, 1);
            Adjust(f, 2, 2);
            StatusRefresh(f);
        }
    }
}

void Simulation::StatusRefresh(Field& f)
{
    // the original's field status refresh copies counter A into the "N" byte that the growth tick sums (only for a
    // planted field with no machine job); the status flags themselves are worked out by FieldProblems
    if (f.Get16(0x28) != 0)
        return; // a machine job is under way: nothing is refreshed
    int slots[3] = { -1, -1, -1 };
    int bits = 0;
    if (f.State() != 0)
    {
        f.Set8(0x1A, f.Get8(0x1B));
        const int p = FieldProblems(f);
        bits = p & 0x8E;
        slots[0] = (p & 0x08) ? 615 : (p & 0x02) ? 607 : -1;
        slots[1] = (p & 0x80) ? 631 : -1;
        slots[2] = (p & 0x04) ? 611 : -1;
        // the infestations in a fixed order (disease, pests, weeds); one of them is shown in slot 3 (over the toxic
        // marker) and the field's rotation byte moves on, so several problems take turns
        const int order[3][2] = { { 0x20, 623 }, { 0x01, 603 }, { 0x10, 619 } };
        int list[3] = { 0, 0, 0 }, n = 0;
        for (const auto& o : order)
            if (p & o[0])
            {
                list[n++] = o[1];
                bits |= o[0];
            }
        int idx = SignedByte(f.Get8(0x36));
        if (idx < 0 || idx > 2)
            idx = 0;
        if (n)
        {
            while (list[idx] == 0)
                idx = idx >= 2 ? 0 : idx + 1;
            slots[2] = list[idx];
            // the original writes the new marker into the map only; its screen shows it when the map is next redrawn,
            // about twice a week (seen live 2026-11-04), while the engine draws every frame. So between the two turns
            // of a week a marker that is still valid stays on screen (user finding F34); the rotation byte moves on
            // as in the original
            // (on a turn the next present marker after the shown one comes up)
            if (f.W() >= 8)
            {
                const int shown = _s.world.At(f.X() + 6, f.Y()).BaseTile();
                for (int k = 0; k < n; ++k)
                    if (list[k] == shown)
                        slots[2] = _markerTurn ? list[(k + 1) % n] : shown;
            }
        }
        f.Set8(0x36, idx >= 2 ? 0 : idx + 1);
    }
    f.Set8(0x34, bits);
    if (std::getenv("OSF_MARKLOG") && f.Index() == std::atoi(std::getenv("OSF_MARKLOG")))
    {
        static int last = -2;
        if (slots[2] != last)
            std::printf("marker field %d slot3 %d day %d week %d\n", f.Index(), slots[2], _dayCounter, _s.cal.week);
        last = slots[2];
    }
    PaintMarkers(f, slots);
}

void Simulation::RefreshFields(bool withStatus)
{
    // moisture from the wetness, the ditch cells on the field's edge recounted (full ones lower the crop's water
    // minimum, empty ones raise its maximum), optionally the status refresh. The original does this in the weekly
    // chain and at two points of the irrigation cycle only, so the counts keep the ditch state of that moment.
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse())
            continue;
        f.SetMoisture(2 * _wetness + (_disasters->Flooding() ? 5 : 0));
        int full = 0, empty = 0;
        auto count = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= kMapSize || y >= kMapSize)
                return;
            const int id = _s.world.At(x, y).BaseTile();
            full += Irrigation::IsFullDitch(id);
            empty += Irrigation::IsEmptyDitch(id);
        };
        // the ring: the rows above and below from the field's west edge to one past its east edge, and the columns
        // west and east of it (the two western corner cells are not looked at)
        for (int x = f.X(); x <= f.X() + f.W(); ++x)
        {
            count(x, f.Y() + f.H());
            count(x, f.Y() - 1);
        }
        for (int y = f.Y(); y < f.Y() + f.H(); ++y)
        {
            count(f.X() + f.W(), y);
            count(f.X() - 1, y);
        }
        f.Set8(0x31, full);
        f.Set8(0x32, empty);
        if (withStatus)
            StatusRefresh(f);
    }
}

void Simulation::PaintField(Field& f, int tile, bool onlyCropTiles)
{
    for (int x = f.X(); x < f.X() + f.W() && x < kMapSize; ++x)
        for (int y = f.Y(); y < f.Y() + f.H() && y < kMapSize; ++y)
        {
            MapCell& cell = _s.world.At(x, y);
            const int id = cell.base & 0x7FF;
            if (id >= 539 && id <= 634)
                continue; // field sign and status markers
            if (onlyCropTiles && !(id >= kCropTileBase && id <= 0x32F))
                continue;
            cell.base = static_cast<uint16_t>((cell.base & 0xF800) | (tile & 0x7FF));
        }
}

int Simulation::GrowthRoutine(Field& f, const CropDef& c)
{
    const int ageBefore = SignedByte(f.Age());
    f.SetAge((f.Age() + 1) & 0xFF);
    Adjust(f, 13, c.DrainA());
    Adjust(f, 17, c.DrainB());
    Adjust(f, 15, c.DrainC());
    Adjust(f, 9, f.Get8(0x3A));
    Adjust(f, 7, f.Get8(0x3C));
    Adjust(f, 11, f.Get8(0x3B));
    // water penalty
    const int m = f.Moisture();
    int low = c.WaterMin() - 2 * f.FullDitches();
    if (low < 0)
        low = 0;
    const int high = c.WaterMax() + 2 * f.EmptyDitches();
    int penalty = 0;
    if (low > m)
        penalty = (((_rng.Next() & 0x1F) * c.DryPenalty()) + 0x80) & 0xFF;
    else if (m >= high)
        penalty = (((_rng.Next() & 0x1F) * c.WetPenalty()) + 0x80) & 0xFF;
    // stress
    const int p36 = std::max(1, c.Weeks());
    const int s = (f.Disease() - f.Fertility() + f.Pests() + f.Weeds() + f.Toxicity() + 0xFF + penalty) & 0xFFFF;
    const int add = (((s / 6) * (2 * (65535 / p36))) >> 8) & 0xFFFF;
    if (std::getenv("OSF_TRACE_GROWTH"))
        std::printf("growth field@%d,%d m=%d low=%d high=%d pen=%d dis=%d fert=%d pest=%d weed=%d tox=%d p36=%d add=%d\n", f.X(), f.Y(), m, low, high,
                    penalty, f.Disease(), f.Fertility(), f.Pests(), f.Weeds(), f.Toxicity(), p36, add);
    f.SetStress(std::min(65535, f.Stress() + add));
    if (p36 > ageBefore)
    {
        const int n = (p36 + std::max(1, c.Stages()) - 1) / std::max(1, c.Stages());
        const int delay = c.HeatNeed() ? SignedByte((f.HeatDeficit() / (c.HeatNeed() + 2)) & 0xFF) : 0;
        const int v = SignedByte(f.Age()) - delay + (n >> 1);
        const int stage = v >= 0 ? v / n : -((-v) / n);
        const int tile = kCropTileBase + (f.Crop() & 15) * 8 + std::clamp(stage, 0, 7);
        f.Set16(0x0E, tile);
        PaintField(f, tile, true);
        return 1;
    }
    return c.HarvestWeek() ? 1 : 0;
}

void Simulation::WeeklyGrowth(Field& f)
{
    const CropDef& c = _s.crops[f.Crop() & 15];
    if (!c.loaded || f.Phase() != _s.cal.week)
        return;
    StatusRefresh(f); // before the drains: the stress sum uses the N byte of this moment
    const int T = _s.WeatherTemp(WeatherIndex());
    if (c.HeatNeed() != 0)
    {
        int heat = 7 * std::max(0, T - c.BaseTemp());
        int deficit = f.HeatDeficit();
        if (deficit != 0)
        {
            if (deficit >= heat) { deficit -= heat; heat = 0; }
            else { heat -= deficit; deficit = 0; }
        }
        if (c.HeatNeed() > heat)
            deficit += c.HeatNeed() - heat;
        f.SetHeatDeficit(std::min(65535, deficit));
    }
    if (c.ChillNeed() != 0 && T < 45)
        f.SetChill(f.Chill() + 1);
    f.SetPhase((f.Phase() + 1) & 3);
    if (GrowthRoutine(f, c) == 0)
    {
        Event(2, 76, "A crop of " + c.name + " died: it was not harvested in time.");
        ClearField(f);
    }
}

void Simulation::DisasterEvent(int id, int picture, const std::string& text, bool always)
{
    GameEvent e{ id, picture, text };
    e.status = false;
    e.always = always;
    if (_events.size() < 8)
        _events.push_back(e);
}

void Simulation::AutoGoto(int x, int y)
{
    if (_s.scalars["opt_autogoto"])
    {
        _gotoX = x;
        _gotoY = y;
    }
}

bool Simulation::TakeGoto(int& x, int& y)
{
    if (_gotoX < 0)
        return false;
    x = _gotoX;
    y = _gotoY;
    _gotoX = _gotoY = -1;
    return true;
}

// every field clear (crop died, busted, flood ...) also moves the view onto the field with AutoGoto
void Simulation::ClearField(Field& f)
{
    ClearStubble(f);
    AutoGoto(f.X(), f.Y());
    f.SetState(0);
    f.SetSub(0);
    f.SetAge(0);
    f.SetStress(0);
    f.SetHeatDeficit(0);
    f.SetChill(0);
    PaintField(f, kBareSoil, false);
}

bool Simulation::OwnsMachine(int type) const
{
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 56;
        const int kind = Rd16(_s.machines, b);
        const int flags = Rd8(_s.machines, b + 2);
        if ((flags & 0x20) && !(flags & 0x40) && kind == type)
            return true;
    }
    return false;
}

bool Simulation::StartPlanting(Field& f, int crop, std::string& why)
{
    const CropDef& c = _s.crops[crop & 15];
    const int seedPrice = _s.CatalogPrice(66 + (crop & 15)); // the seed record's price
    if (!c.loaded)
    {
        why = "That crop is not loaded.";
        return false;
    }
    // seed: a bag of this crop's seed (item 0xC0 + crop) from storage is used up; otherwise AutoBuy buys the seed
    // for the seed price + 20 (read from the original's seed routine; seen live as 38 = 18 + 20)
    int bag = 0;
    for (int i = 1; i < 512 && !bag; ++i)
        if ((Rd8(_s.items, static_cast<size_t>(i) * 12 + 10) & 0x20) && Rd16(_s.items, static_cast<size_t>(i) * 12) == 0xC0 + (crop & 15))
            bag = i;
    if (bag)
    {
        // the dispose routine frees the bag's slot or ground first; the record is let go, and the
        // seed record's Own count lowered, only while that count is above 0 (otherwise the record stays in use)
        const int rec = 66 + (crop & 15);
        if (_s.CatalogOwn(rec) > 0)
        {
            _s.SetCatalogOwn(rec, _s.CatalogOwn(rec) - 1);
            TakeItem(bag);
        }
        else
            TakeItem(bag, false);
    }
    else if (!_s.scalars["opt_autobuy"] || !Spend(0xC0, seedPrice + 20))
    {
        why = "There is no " + c.name + " seed.";
        return false;
    }
    f.SetHeatDeficit(0); f.SetChill(0); f.SetStress(0); f.SetPhase(0); f.SetAge(0); f.SetContract(0);
    f.SetState(3); f.SetSub(0); f.SetCrop(crop);
    // the sign shows the crop planted (the original's plant step and field creation both repaint it; a field
    // replanted with another crop kept the old crop's sign: user save WRONGCRO, finding F41)
    for (int k = 0; k < 4; ++k)
    {
        const int x = f.X() + (k & 1), y = f.Y() + (k >> 1);
        if (x < kMapSize && y < kMapSize)
        {
            MapCell& c = _s.world.At(x, y);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (539 + 4 * (crop & 15) + k));
        }
    }
    return true;
}

void Simulation::RollPressures(Field& f)
{
    // only the schedule's plant code re-rolls the infestation pressures (a PLANT-tool field keeps 0)
    const CropDef& c = _s.crops[f.Crop() & 15];
    auto roll = [&](int resist) {
        int r = _rng.Next() & 0xFF;
        if (r == 0) r = 1;
        return r > resist ? std::min(0x40, r - resist) : 0;
    };
    f.Set8(0x3B, roll(c.ResistWeeds()));
    f.Set8(0x3A, roll(c.ResistPests()));
    f.Set8(0x3C, roll(c.ResistDisease()));
}

bool Simulation::StartSpray(Field& f, int job, std::string& why, bool checkIdle)
{
    const int state = f.State();
    if (checkIdle && state != 4 && state != 0)
    {
        why = "The field is busy.";
        return false;
    }
    const int item = 0xDC + (job - 6);
    // consume a barrel of the chemical, or buy one (AutoBuy)
    bool have = false;
    for (int k = 1; k < 127 && !have; ++k)
    {
        const size_t b = static_cast<size_t>(k) * 20;
        if (Rd16(_s.objects20, b) == item && (Rd8(_s.objects20, b + 2) & 0x20) && Rd8(_s.objects20, b + 4) == 0)
        {
            Wr8(_s.objects20, b + 2, Rd8(_s.objects20, b + 2) & ~0x20);
            _s.object20Count = std::max(0, _s.object20Count - 1);
            // the barrel leaves the map
            const int x = Rd8(_s.objects20, b + 5), y = Rd8(_s.objects20, b + 6);
            // one kept in a building (the Move tool can put it there) leaves its slot
            if (const int st = x < kMapSize && y < kMapSize ? StructureAt(x, y) : 0)
            {
                const size_t sb = static_cast<size_t>(st) * 70;
                const int sk = (x - Rd8(_s.structures, sb + 4)) * 3 + (y - Rd8(_s.structures, sb + 5));
                if (sk >= 0 && sk < 12 && Rd16(_s.structures, sb + 0x22 + 2 * sk) == item && Rd8(_s.structures, sb + 0x3A + sk) == k)
                {
                    Wr16(_s.structures, sb + 0x0A + 2 * sk, 0xD9);
                    Wr16(_s.structures, sb + 0x22 + 2 * sk, 0xFFFF);
                    Wr8(_s.structures, sb + 0x3A + sk, 0);
                    ToggleInside(Rd8(_s.structures, sb + 4), Rd8(_s.structures, sb + 5));
                    ToggleInside(Rd8(_s.structures, sb + 4), Rd8(_s.structures, sb + 5));
                }
            }
            else if (x < kMapSize && y < kMapSize && (_s.world.At(x, y).overlay & 0x7FF) >= 912 && (_s.world.At(x, y).overlay & 0x7FF) <= 915)
            {
                _s.world.At(x, y).base &= static_cast<uint16_t>(~0x800);
                _s.world.At(x, y).overlay &= 0xF800;
            }
            have = true;
        }
    }
    if (!have && !(_s.scalars["opt_autobuy"] && Spend(item, 275)))
    {
        why = "Not enough chemicals for this job.";
        return false;
    }
    f.Set8(0x02, state);
    f.Set8(0x03, f.Sub());
    f.SetState(job);
    f.SetSub(0);
    return true;
}

int Simulation::FieldIndexAt(int cx, int cy)
{
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (f.InUse() && cx >= f.X() && cx < f.X() + f.W() && cy >= f.Y() && cy < f.Y() + f.H())
            return i;
    }
    return 0;
}

bool Simulation::StartHarvest(Field& f)
{
    if (f.State() != 4)
        return false;
    // the schedule's harvest start: state 5, flag bit 1; a hand-picked crop (P37) starts at its picking step and
    // needs only a truck (weekly schedule pass of the original)
    const bool hand = _s.crops[f.Crop() & 15].HandPicked();
    f.SetState(5);
    f.SetSub(hand ? 10 : 0);
    f.Set8(0x05, f.Get8(0x05) | 2);

    return true;
}

bool Simulation::LockContract(Field& f)
{
    if (f.State() != 4 || f.Contract() != 0)
        return false;
    const int crop = f.Crop() & 15;
    f.SetContract(_s.MarketPrice(crop));
    _s.SetCatalogOwn(GameState::kCropRecord0 + crop, _s.Contracts(crop) + 1);
    _s.SetScheduleCode(_s.cal.AbsWeek(), f.Index(), 0xC6); // the chart icon under today's marker
    return true;
}

// ---------------------------------------------------------------- timeline stamping
void Simulation::StampPlant(int field, int crop, int row)
{
    const CropDef& c = _s.crops[crop & 15];
    const int weeks = std::max(1, c.Weeks());
    const int stages = std::clamp(c.Stages(), 1, 8);
    const int now = _s.cal.AbsWeek();
    for (int r = 0; r < now && r < 240; ++r)
        _s.SetScheduleCode(r, field, kFallow);
    _s.SetScheduleCode(row, field, 0x340 + (crop & 15));
    // the plant mode of the timeline builder: an annual repeats [plant, `weeks` stage pictures, harvest]
    // to the end, stage k showing P6 + k / n with n = (P36 + P46 - 2) / P46 (one week less than the growth tick's
    // rounding); a perennial shows the harvest mark in its harvest week of every year and otherwise the stage
    // ((week - harvest week) mod 48) / 6, the weeks since the last harvest week
    const int n = std::max(1, (weeks + stages - 2) / stages);
    const int hw = c.HarvestWeek();
    for (int r = row + 1, k = 1; r < 240; ++r, ++k)
    {
        int code = 0;
        if (hw != 0)
        {
            const int dx = r % 48;
            code = dx == hw ? kHarvestMark : kCropTileBase + 8 * (crop & 15) + ((dx - hw + 48) % 48) / 6;
        }
        else
        {
            const int kk = k % (weeks + 2);
            if (kk == 0)
                code = 0x340 + (crop & 15);
            else if (kk <= weeks)
                code = kCropTileBase + 8 * (crop & 15) + c.P6() + (kk - 1) / n;
            else
                code = kHarvestMark;
        }
        _s.SetScheduleCode(r, field, code);
    }
}

// The year routine's plan extension: from the end of the timeline back, find the last harvest mark (the field's
// crop is a perennial) or the last plant code (otherwise), and repeat that crop's cycle from there to the end: the
// mark or plant code, P36 stage pictures P6 + k / n (n as in StampPlant), the harvest mark, and again from the
// start (a perennial from its regrowth age + 1). Returns false when no such week is left; the original then reads
// on past the first week, the engine stops there.
bool Simulation::ExtendPlan(int field)
{
    const Field f = _s.FieldAt(field);
    const bool perennial = _s.crops[f.Crop() & 15].Perennial();
    int row = 239, crop = -1;
    for (; row >= 0; --row)
    {
        const int code = _s.ScheduleCode(row, field);
        if (perennial ? code == kHarvestMark : (code >= 0x340 && code < 0x350))
        {
            crop = perennial ? (f.Crop() & 15) : code - 0x340;
            break;
        }
    }
    if (crop < 0)
        return false;
    const CropDef& c = _s.crops[crop];
    const int weeks = c.Weeks();
    const int n = std::max(1, (weeks + std::max(1, c.Stages()) - 2) / std::max(1, c.Stages()));
    std::vector<int> plan;
    plan.push_back(perennial ? kHarvestMark : 0x340 + crop);
    for (int k = 0; k < weeks; ++k)
        plan.push_back(kCropTileBase + 8 * crop + c.P6() + k / n);
    plan.push_back(kHarvestMark);
    for (int r = row, k = 0; r < 240; ++r, ++k)
    {
        if (k >= static_cast<int>(plan.size()))
            k = c.Perennial() ? std::min(c.regrowAge + 1, static_cast<int>(plan.size()) - 1) : 0;
        _s.SetScheduleCode(r, field, plan[static_cast<size_t>(k)]);
    }
    return true;
}

static bool IsPlanCell(int code)
{
    return code == 1 || code == 2 || code == 3 || code == 0x1B8 || code == 0xC6 || code == 0x354 || (code >= 0x340 && code <= 0x34F) ||
           (code >= 688 && code < 688 + 128);
}

bool Simulation::StampOperation(int field, int row, int code)
{
    if (row < 0 || row >= 240 || IsPlanCell(_s.ScheduleCode(row, field)))
        return false;
    _s.SetScheduleCode(row, field, code);
    return true;
}

void Simulation::CutPlan(int field, int row)
{
    if (row < 0 || row >= 240)
        return;
    const int code = _s.ScheduleCode(row, field);
    if (code >= 0x390 && code <= 0x393)
    {
        _s.SetScheduleCode(row, field, 0); // remove one operation
        return;
    }
    // cut the crop plan from this week on: the crop is cleared that week
    _s.SetScheduleCode(row, field, kFallow);
    for (int r = row + 1; r < 240; ++r)
    {
        const int c = _s.ScheduleCode(r, field);
        if (IsPlanCell(c) && !(c >= 0x340 && c <= 0x34F))
            _s.SetScheduleCode(r, field, 0);
        else
            break;
    }
}

// Weekly execution of the schedule timeline.
void Simulation::ExecuteSchedule()
{
    const int row = _s.cal.AbsWeek();
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (!f.InUse())
            continue;
        const int code = _s.ScheduleCode(row, i);
        const int state = f.State();
        if (code >= 0x340 && code <= 0x34F)
        {
            std::string why;
            // the same crop again leaves three quarters of the pests, disease and weeds; another crop (rotation)
            // clears them (read from the original's plant step)
            const bool same = (f.Crop() & 15) == code - 0x340;
            for (const auto& [type, o] : { std::pair{ 8, 0x12 }, std::pair{ 6, 0x14 }, std::pair{ 10, 0x13 } })
                Adjust(f, type, same ? f.Get8(o) >> 2 : f.Get8(o));
            if (!StartPlanting(f, code - 0x340, why))
            {
                Message(why + " The field is cleared.");
                ClearField(f);
            }
            else
                RollPressures(f);
        }
        else if (code >= 0x390 && code <= 0x393)
        {
            std::string why;
            if (!StartSpray(f, 9 - (code - 0x390), why, false)) // 0x390 fungicide 9, 0x391 herbicide 8, 0x392 pesticide 7, 0x393 fertilize 6
                Message(why);
        }
        else if ((code == 0x1B8 || code == 0xEF) && state == 4)
            StartHarvest(f);
        else if (code == 0x354)
        {
            f.SetState(2);
            f.SetSub(0);
        }
        else if (code == 0xC6 && state == 4 && f.Contract() == 0)
            LockContract(f);
        else if (code == 0xD1 && state != 0)
        {
            ClearField(f);
        }
    }
}

int64_t Simulation::HarvestValue(Field& f, int* maturity, int* quality) const
{
    const int crop = f.Crop() & 15;
    const CropDef& c = _s.crops[crop];
    const int price = f.Contract() ? f.Contract() : _s.MarketPrice(crop);
    // harvest value, exact integer semantics
    const int unit = price / 10;
    const int delay = c.HeatNeed() ? SignedByte((f.HeatDeficit() / (c.HeatNeed() + 2)) & 0xFF) : 0;
    const int x = (SignedByte(f.Age()) - delay) & 0xFFFF;
    int64_t T = std::min<int64_t>(0xFFFF, static_cast<int64_t>(x) * 65536 / std::max(1, c.Weeks()));
    if (T < 0x2000)
        T = 0;
    if (maturity)
        *maturity = static_cast<int>(std::min<int64_t>(80, 5 * (T >> 12))); // schedule window bar, after the cut-off
    const int64_t q = std::min<int64_t>(65535, (65535 - T) + f.Stress());
    if (quality)
        *quality = static_cast<int>(q);
    if (q > 51000)
        return 0;
    return ((static_cast<int64_t>(c.Yield() * unit) & 0xFFFF) * (65535 - q)) >> 16;
}

// A storage building's slots: one per footprint cell, slot (lx, ly) = lx * 3 + ly; per slot a picture word at
// +0x0A (0xD9 empty), a content word at +0x22 (0xFFFF free) and the stored item's index byte at +0x3A
// (the original's store and clear routines).
size_t Simulation::SiloSlot(int k)
{
    return static_cast<size_t>(0x22 + 2 * ((k / 2) * 3 + (k % 2)));
}

bool Simulation::StoreHarvest(Field& f, int crop, int quality)
{
    // the nearest silo with a free slot (structure types 0x40 small silo: 1 slot, 0x41 large silo: 2 x 2)
    int best = 0, bestD = 1 << 30, bestSlot = -1;
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        const int cat = Rd16(_s.structures, b);
        if (!(Rd8(_s.structures, b + 2) & 0x20) || (cat != 0x40 && cat != 0x41))
            continue;
        const int slots = cat == 0x40 ? 1 : 4;
        int free = -1;
        for (int k = 0; k < slots && free < 0; ++k)
            if (Rd16(_s.structures, b + SiloSlot(k)) == 0xFFFF)
                free = k;
        if (free < 0)
            continue;
        const int dx = Rd8(_s.structures, b + 4) - f.X(), dy = Rd8(_s.structures, b + 5) - f.Y();
        if (dx * dx + dy * dy < bestD)
        {
            bestD = dx * dx + dy * dy;
            best = i;
            bestSlot = free;
        }
    }
    if (!best)
        return false;
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if (Rd8(_s.items, b + 10) & 0x20)
            continue;
        const size_t sb = static_cast<size_t>(best) * 70;
        const size_t slot = SiloSlot(bestSlot);
        for (size_t o = 0; o < 12; ++o)
            _s.items[b + o] = 0;
        Wr16(_s.items, b, 0xA8 + crop); // harvested crop stack (0xC0 + crop is a seed bag)
        Wr16(_s.items, b + 2, _s.crops[crop].Yield());
        Wr16(_s.items, b + 4, std::min(0xFFFA, quality));
        Wr8(_s.items, b + 8, Rd8(_s.structures, sb + 4) + bestSlot / 2); // the stack lies on its slot's cell
        Wr8(_s.items, b + 9, Rd8(_s.structures, sb + 5) + bestSlot % 2);
        Wr8(_s.items, b + 10, 0x20); // in use; a harvest in a silo is not flagged "stored", so it ages 4 a week (seen live)
        Wr16(_s.structures, sb + slot, 0xA8 + crop);
        Wr16(_s.structures, sb + slot - 0x18, 0x330 + crop); // the stack's picture
        Wr8(_s.structures, sb + 0x3A + (slot - 0x22) / 2, i);
        _s.SetCatalogOwn(50 + (crop & 15), _s.CatalogOwn(50 + (crop & 15)) + 1); // stored-stack count of the crop
        _s.itemCount++;
        return true;
    }
    return false;
}

void Simulation::FreeStorageSlot(int item)
{
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        if (!(Rd8(_s.structures, b + 2) & 0x20) || Rd16(_s.structures, b) < 0x40 || Rd16(_s.structures, b) > 0x45)
            continue;
        // the slot must hold this item: the same index byte and the item's type as content (machines in sheds
        // and barns carry their own small index numbers)
        const int type = Rd16(_s.items, static_cast<size_t>(item) * 12);
        for (int k = 0; k < 12; ++k)
            if (Rd8(_s.structures, b + 0x3A + k) == item && Rd16(_s.structures, b + 0x22 + 2 * k) == type)
            {
                Wr16(_s.structures, b + 0x0A + 2 * k, 0xD9);
                Wr16(_s.structures, b + 0x22 + 2 * k, 0xFFFF);
                Wr8(_s.structures, b + 0x3A + k, 0);
            }
    }
}

void Simulation::Harvest(Field& f)
{
    HarvestPay(f, true);
    HarvestEnd(f);
}

int Simulation::HarvestStorage(const Field& f) const
{
    // where the truck takes the crop: the nearest silo with a free slot, when the crop is stored at all
    if (Field(f).Contract() || HarvestValue(const_cast<Field&>(f)) <= 0)
        return 0;
    int best = 0, bestD = 1 << 30;
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        const int cat = Rd16(_s.structures, b);
        if (!(Rd8(_s.structures, b + 2) & 0x20) || (cat != 0x40 && cat != 0x41))
            continue;
        bool room = false;
        for (int k = 0; k < (cat == 0x40 ? 1 : 4); ++k)
            room = room || Rd16(_s.structures, b + SiloSlot(k)) == 0xFFFF;
        const int dx = Rd8(_s.structures, b + 4) - Field(f).X(), dy = Rd8(_s.structures, b + 5) - Field(f).Y();
        if (room && dx * dx + dy * dy < bestD)
        {
            bestD = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

void Simulation::HarvestPay(Field& f, bool mayStore)
{
    const int crop = f.Crop() & 15;
    const CropDef& c = _s.crops[crop];
    int quality = 65535;
    const int64_t value = HarvestValue(f, nullptr, &quality);
    if (f.Contract())
    {
        _s.SetCatalogOwn(GameState::kCropRecord0 + crop, std::max(0, _s.Contracts(crop) - 1));
        // the difference against today's market is booked
        const int locked = f.Contract();
        f.SetContract(0);
        const int64_t market = HarvestValue(f);
        f.SetContract(locked);
        if (market > value)
            Ledger("exp_futures") += market - value;
        else
            Ledger("rev_futures") += value - market;
    }
    // without a price contract the crop goes into a silo with room (it keeps its quality as the stack's age);
    // with no room it is sold at once
    if (mayStore && value > 0 && !f.Contract() && StoreHarvest(f, crop, quality))
    {
        // the two bytes +0x40/+0x41 (0xFF while a harvest runs) are 0 after a stored harvest and stay 0xFF after a
        // sale (seen in the original's memory; their meaning is not known)
        f.Set8(0x40, 0);
        f.Set8(0x41, 0);
        Message("Harvested " + c.name + ": stored in a silo (worth " + std::to_string(value) + " today).");
    }
    else if (value > 0)
    {
        Credit(168 + crop, value);
        Message("Harvested " + c.name + ": sold for " + std::to_string(value) + ".");
    }
    else
        Message("Harvested " + c.name + ": the crop was worth nothing.");
    f.SetContract(0);
}

void Simulation::ClearStubble(Field& f)
{
    // the pictures a hand harvest scatters over the field (overlay 0x40C / 0x40D) go with the harvest: when the week's
    // deadline finished the harvest before the pickers' three days were over they stayed on for good, also over the
    // next crop (user save BUGHARV, finding F40)
    for (int x = f.X(); x < f.X() + f.W() && x < kMapSize; ++x)
        for (int y = f.Y(); y < f.Y() + f.H() && y < kMapSize; ++y)
        {
            MapCell& c = _s.world.At(x, y);
            const int o = c.overlay & 0x7FF;
            if (c.HasObject() && (o == 0x40C || o == 0x40D))
            {
                c.base &= static_cast<uint16_t>(~0x800);
                c.overlay &= 0xF800;
            }
        }
}

void Simulation::HarvestEnd(Field& f)
{
    ClearStubble(f);
    // the harvest handler's field reset: sub-state, heat deficit, chill, stress and the growth phase are
    // zeroed; a perennial grows on (state 4) from the age in its crop file, an annual field is cleared
    const CropDef& c = _s.crops[f.Crop() & 15];
    f.SetSub(0); f.SetHeatDeficit(0); f.SetChill(0); f.SetStress(0); f.SetPhase(0);
    if (c.Perennial())
    {
        f.SetState(4); f.SetAge(c.regrowAge);
    }
    else
        ClearField(f);
}

void Simulation::FinishJob(Field& f)
{
    const int lead = f.Get16(0x28), impl = f.Get16(0x2A);
    _machines->ReleaseField(f.Index());
    (void)lead;
    (void)impl;
    const bool plant = f.State() == 3;
    CompleteJob(f);
    if (plant)
    {
        // the machines' own finish of a plant job (plant sub-state 11): the heat-need word gets the
        // crop's P10, the growth phase starts next week, and the field shows stage 0
        const CropDef& c = _s.crops[f.Crop() & 15];
        f.Set16(0x22, c.HeatNeed());
        f.SetPhase((_s.cal.week + 1) & 3);
        f.Set16(0x0E, kCropTileBase + (f.Crop() & 15) * 8);
    }
}

// The week deadline: jobs still running are completed and their effects applied.
void Simulation::CompleteJob(Field& f)
{
    const int st = f.State();
    if (st == 3)
    {
        // the original's deadline completion: unlike the machines' own finish it sets neither the
        // growth phase nor the heat-need word, so a field planted in week 0 is grown at once by this week's tick
        f.SetState(4); f.SetSub(0);
        f.Set8(0x04, (f.Get8(0x04) | 4) & ~2);
        f.SetAge(0); f.SetStress(0); f.SetHeatDeficit(0); f.SetChill(0);
        PaintField(f, kCropTileBase + (f.Crop() & 15) * 8, false);
    }
    else if (st >= 6 && st <= 9)
    {
        if (st == 6)
            Adjust(f, 1, 0xFF);
        else
        {
            Adjust(f, 3, 0x20);
            if (st == 7) { Adjust(f, 8, f.Pests()); f.Set8(0x3A, f.Get8(0x3A) >> 2); }
            if (st == 8) { Adjust(f, 10, f.Weeds()); f.Set8(0x3B, f.Get8(0x3B) >> 2); }
            if (st == 9) { Adjust(f, 6, f.Disease()); f.Set8(0x3C, f.Get8(0x3C) >> 2); }
        }
        f.SetState(f.Get8(0x02));
        f.SetSub(f.Get8(0x03));
    }
    else if (st == 5)
    {
        // a harvest that never got going loses the crop; a load on its way is settled
        if (f.Sub() < 3)
        {
            Message("The " + _s.crops[f.Crop() & 15].name + " harvest never started: the crop is lost.");
            ClearField(f);
        }
        else if (f.Sub() == 7)
            HarvestEnd(f); // already sold when it was loaded
        else
            Harvest(f);
    }
}

// ---------------------------------------------------------------- livestock needs
int Simulation::StructureAt(int x, int y) const
{
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        const int cat = Rd16(_s.structures, b);
        if (!(Rd8(_s.structures, b + 2) & 0x20) || cat < 0x40 || cat > 0x48)
            continue;
        const int rec = 12 + cat - 0x40;
        const int w = std::max(1, Rd8(_s.catalog, static_cast<size_t>(rec) * 18 + 8)), h = std::max(1, Rd8(_s.catalog, static_cast<size_t>(rec) * 18 + 9));
        const int bx = Rd8(_s.structures, b + 4), by = Rd8(_s.structures, b + 5);
        if (x >= bx && y >= by && x < bx + w && y < by + h)
            return i;
    }
    return 0;
}

// A right press on a building (called from the farm window's right-button branch) flips
// bit 0 of its flags and redraws its cells. Open: a water tower shows its level picture 0x53 + (level >> 6), any
// other building shows each slot as an empty spot (0xD9) or, for a used slot, its stored picture (below 0x350 on the
// ground word, otherwise as an object over the empty spot). Closed again: the building's own pictures, numbered along
// x first, from the start of its tile family; a water tower is one cell and shows 0x95 once its damage byte is 100 or
// more. The pump and the windmill (0x46, 0x47) are left alone.
int Simulation::ToggleInside(int x, int y)
{
    const int i = StructureAt(x, y);
    if (!i)
        return 0;
    const size_t b = static_cast<size_t>(i) * 70;
    const int cat = Rd16(_s.structures, b);
    if (cat == 0x46 || cat == 0x47)
        return 0;
    const size_t rec = static_cast<size_t>(12 + cat - 0x40) * 18;
    const int w = static_cast<int8_t>(Rd8(_s.catalog, rec + 8)), h = static_cast<int8_t>(Rd8(_s.catalog, rec + 9));
    const int bx = Rd8(_s.structures, b + 4), by = Rd8(_s.structures, b + 5);
    Wr8(_s.structures, b + 2, Rd8(_s.structures, b + 2) ^ 1);
    DrawBuilding(i);
    return i;
}

// a building's map cells from its record: the outside pictures, or (inside shown) the floor with each slot's picture
// (the inside or outside view drawn after flipping the flag)
void Simulation::DrawBuilding(int i)
{
    const size_t b = static_cast<size_t>(i) * 70;
    const int cat = Rd16(_s.structures, b);
    const size_t rec = static_cast<size_t>(12 + cat - 0x40) * 18;
    const int w = static_cast<int8_t>(Rd8(_s.catalog, rec + 8)), h = static_cast<int8_t>(Rd8(_s.catalog, rec + 9));
    const int bx = Rd8(_s.structures, b + 4), by = Rd8(_s.structures, b + 5);
    const uint8_t flags = Rd8(_s.structures, b + 2);
    if (!(flags & 1))
    {
        uint16_t pic = static_cast<uint16_t>(FamilyStart(Rd8(_s.catalog, rec)));
        if (cat == 0x48)
        {
            MapCell& c = _s.world.At(bx, by);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (Rd8(_s.structures, b + 6) < 100 ? pic : 0x95));
            return;
        }
        for (int yy = by; yy < by + h; ++yy)
            for (int xx = bx; xx < bx + w; ++xx)
                _s.world.At(xx, yy).base = pic++;
        return;
    }
    for (int xx = bx; xx < bx + w; ++xx)
        for (int yy = by; yy < by + h; ++yy)
        {
            MapCell& c = _s.world.At(xx, yy);
            if (cat == 0x48)
            {
                c.base = static_cast<uint16_t>((Rd16(_s.structures, b + 0x0A) >> 6) + 0x53);
                continue;
            }
            c.base = 0xD9;
            const size_t k = static_cast<size_t>((xx - bx) * 3 + (yy - by));
            const int pic = Rd16(_s.structures, b + 0x0A + 2 * k), content = Rd16(_s.structures, b + 0x22 + 2 * k);
            if (content == 0xFFFF || content == 0xFFFE)
                continue;
            if (pic < 0x350)
                c.base = static_cast<uint16_t>(pic);
            else
            {
                c.base = 0x8D9;
                c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | pic);
            }
        }
    return;
}

int64_t Simulation::RemoveStructure(int index, bool refund)
{
    const size_t b = static_cast<size_t>(index) * 70;
    const int cat = Rd16(_s.structures, b);
    const int rec = 12 + cat - 0x40;
    const int w = std::max(1, Rd8(_s.catalog, static_cast<size_t>(rec) * 18 + 8)), h = std::max(1, Rd8(_s.catalog, static_cast<size_t>(rec) * 18 + 9));
    const int bx = Rd8(_s.structures, b + 4), by = Rd8(_s.structures, b + 5);
    if (cat == 0x46 || cat == 0x47)
        _irrigation->RemoveSource(bx, by);
    for (int x = bx; x < bx + w && x < kMapSize; ++x)
        for (int y = by; y < by + h && y < kMapSize; ++y)
        {
            Puff(x, y);
            MapCell& c = _s.world.At(x, y);
            const int o = c.overlay & 0x7FF;
            if (o >= 1010 && o <= 1013) // windmill pieces
            {
                c.base &= static_cast<uint16_t>(~0x800);
                c.overlay &= 0xF800;
            }
            if (!c.HasObject())
                c.base = static_cast<uint16_t>((c.base & 0xF800) | GroundTile(_s, _wetness, x, y));
        }
    // stored stacks inside are lost with the building
    for (int k = 0; k < 12; ++k)
    {
        const int it = Rd16(_s.structures, b + 0x22 + 2 * k) == 0xFFFF ? 0 : Rd8(_s.structures, b + 0x3A + k);
        if (it > 0 && it < 512 && Rd16(_s.items, static_cast<size_t>(it) * 12) >= 0xA8 && Rd16(_s.items, static_cast<size_t>(it) * 12) <= 0xB7)
            RemoveItem(it);
    }
    const int64_t value = (!refund || (Rd8(_s.structures, b + 2) & 0x02)) ? 0 : _s.CatalogPrice(rec); // a burst tower is worth nothing
    Wr8(_s.structures, b + 2, 0);
    _s.structureCount = std::max(0, _s.structureCount - 1);
    _s.SetCatalogOwn(rec, std::max(0, _s.CatalogOwn(rec) - 1));
    ReconcileCash();
    const int64_t before = _s.cash;
    _s.cash = std::min<int64_t>(9999999, _s.cash + value);
    PostCash(_s.cash - before, "Structure sold");
    return value;
}

int Simulation::ItemAt(int x, int y, int type) const
{
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if ((Rd8(_s.items, b + 10) & 0x20) && Rd16(_s.items, b) == type && Rd8(_s.items, b + 8) == x && Rd8(_s.items, b + 9) == y)
            return i;
    }
    return 0;
}

int Simulation::NewItem(int type, int amount, int x, int y)
{
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if (Rd8(_s.items, b + 10) & 0x20)
            continue;
        for (size_t o = 0; o < 12; ++o)
            _s.items[b + o] = 0;
        Wr16(_s.items, b, type);
        Wr16(_s.items, b + 2, amount);
        Wr8(_s.items, b + 8, x);
        Wr8(_s.items, b + 9, y);
        Wr8(_s.items, b + 10, 0x20);
        _s.itemCount++;
        return i;
    }
    return 0;
}

// an item used up: one in a building leaves its slot (the building drawn again), one on the
// ground leaves plain ground behind; then its record is let go
void Simulation::TakeItem(int index, bool freeRecord)
{
    const size_t b = static_cast<size_t>(index) * 12;
    const int x = Rd8(_s.items, b + 8), y = Rd8(_s.items, b + 9);
    if (x < kMapSize && y < kMapSize)
    {
        if (const int st = StructureAt(x, y))
        {
            const size_t sb = static_cast<size_t>(st) * 70;
            const int k = (x - Rd8(_s.structures, sb + 4)) * 3 + (y - Rd8(_s.structures, sb + 5));
            if (k >= 0 && k < 12 && Rd8(_s.structures, sb + 0x3A + k) == index)
            {
                Wr16(_s.structures, sb + 0x0A + 2 * k, 0xD9);
                Wr16(_s.structures, sb + 0x22 + 2 * k, 0xFFFF);
                Wr8(_s.structures, sb + 0x3A + k, 0);
                ToggleInside(Rd8(_s.structures, sb + 4), Rd8(_s.structures, sb + 5));
                ToggleInside(Rd8(_s.structures, sb + 4), Rd8(_s.structures, sb + 5));
            }
        }
        else
        {
            MapCell& c = _s.world.At(x, y);
            const int t = c.BaseTile();
            if (t >= 816 && t <= 847)
                c.base = static_cast<uint16_t>((c.base & 0xF800) | GroundTileFor(_wetness, c));
        }
    }
    if (freeRecord)
        RemoveItem(index);
}

void Simulation::RemoveItem(int index)
{
    const size_t b = static_cast<size_t>(index) * 12;
    if (Rd8(_s.items, b + 10) & 0x20)
        _s.itemCount = std::max(0, _s.itemCount - 1); // the count is of live records
    Wr8(_s.items, b + 10, Rd8(_s.items, b + 10) & ~0x20);
}

// hay piles shrink by one a day and show their amount; an empty pile disappears
static int HayTile(int amount) { return amount > 409 ? 1020 : amount >= 308 ? 1021 : amount >= 206 ? 1022 : amount >= 102 ? 1023 : 1024; }

// every 32 ticks: water shimmers between the two pictures of its pair, pumps turn
void Simulation::AmbientAnimation()
{
    for (auto& c : _s.world.cells)
    {
        const int id = c.BaseTile();
        if (id >= 39 && id <= 68 && (_uiRandom() & 3) == 0) // cosmetic: not the game generator
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (39 + (((id - 39) & ~1) | (((id - 39) & 1) ^ 1))));
        else if (id == 120 || id == 121)
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (id ^ 1));
    }
}

// the event grounds' animation every 8 ticks: the ground is a 7 x 5 block of tiles 483-517 one cell
// inside its plot. Rodeo: a 2 x 2 spot in the arena's middle is cleared and one random piece 937-944 drawn on one of
// its cells (the cell is chosen again every 4th pass). Fair: each ground either swaps two of its pictures or shows
// the fixed pieces 526-529 on six cells. Both draw on G2, as the original does.
void Simulation::AnimateStalls()
{
    const int kind = EventKind();
    if (kind == 0)
    {
        for (const auto& p : _stalls)
        {
            MapCell& c = _s.world.At(p[0], p[1]);
            if ((c.overlay & 0x7FF) >= 937 && (c.overlay & 0x7FF) <= 944)
            {
                c.base &= static_cast<uint16_t>(~0x800);
                c.overlay &= 0xF800;
            }
        }
        _stalls.clear();
        return;
    }
    static int spotX = 0, spotY = 0;
    for (int pc = 0; pc < 12; ++pc)
        for (int pr = 0; pr < 12; ++pr)
        {
            if (_s.PlotOwner(pc, pr) != 2 || (_s.PlotType(pc, pr) & 15) != 4)
                continue;
            const int x0 = pc * 8, y0 = pr * 8;
            auto setBase = [&](int dx, int dy, int tile) {
                MapCell& c = _s.world.At(x0 + dx, y0 + dy);
                c.base = static_cast<uint16_t>((c.base & 0xF800) | tile);
            };
            if (kind == 1)
            {
                for (int dx = 2; dx < 4; ++dx)
                    for (int dy = 3; dy < 5; ++dy)
                    {
                        MapCell& c = _s.world.At(x0 + dx, y0 + dy);
                        c.base &= static_cast<uint16_t>(~0x800);
                        const std::array<int, 2> p{ x0 + dx, y0 + dy };
                        if (std::find(_stalls.begin(), _stalls.end(), p) == _stalls.end())
                            _stalls.push_back(p);
                    }
                if ((++_eventPass & 3) == 0)
                {
                    spotX = G2Step() & 1; // G2
                    spotY = G2Step() & 1;
                }
                MapCell& c = _s.world.At(x0 + 2 + spotX, y0 + 3 + spotY);
                c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | (937 + (G2Step() & 7)));
                c.base |= 0x800;
            }
            else if ((G2Step() & 1) == 0) // G2
            {
                setBase(3, 2, 483 + 9);
                setBase(2, 1, 483 + 1);
                setBase(1, 3, 483 + 14);
                setBase(1, 5, 483 + 28);
                setBase(3, 3, 483 + 11);
                setBase(5, 2, 483 + 16);
            }
            else
            {
                setBase(3, 2, 526);
                setBase(2, 1, 527);
                setBase(1, 3, 528);
                setBase(1, 5, 528);
                setBase(3, 3, 529);
                setBase(5, 2, 529);
            }
        }
}

// the ground's own pictures back on the six fair cells (the end of a fair)
static void RestoreFairCells(GameState& s)
{
    for (int pc = 0; pc < 12; ++pc)
        for (int pr = 0; pr < 12; ++pr)
            if (s.PlotOwner(pc, pr) == 2 && (s.PlotType(pc, pr) & 15) == 4)
            {
                static const int cells[6][3] = { { 3, 2, 9 }, { 2, 1, 1 }, { 1, 3, 14 }, { 1, 5, 28 }, { 3, 3, 16 }, { 5, 2, 11 } };
                for (const auto& k : cells)
                {
                    MapCell& c = s.world.At(pc * 8 + k[0], pr * 8 + k[1]);
                    c.base = static_cast<uint16_t>((c.base & 0xF800) | (483 + k[2]));
                }
            }
}

bool Simulation::EventGround(int& x, int& y) const
{
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
            if (_s.PlotOwner(c, r) == 2 && (_s.PlotType(c, r) & 15) == 4)
            {
                x = c * 8;
                y = r * 8;
                return true;
            }
    return false;
}

// the month change into July (rodeo) and October (fair), with an event ground and Messages on
void Simulation::ContestOffers()
{
    const int m = _s.cal.month;
    int gx, gy;
    if ((m != 6 && m != 9) || !_messagesOn || !EventGround(gx, gy))
        return;
    auto& sc = _s.scalars;
    sc["ev_entry"] = 0;
    if (m == 9)
    {
        sc["ev_kind"] = 2;
        sc["ev_countdown"] = 3;
        _song = "carnival.xmi";
        std::vector<int> owned;
        for (int i = 1; i < 256; ++i)
            if (Rd8(_s.animals, static_cast<size_t>(i) * 32 + 2) & 0x20)
                owned.push_back(i);
        if (owned.empty() || _s.cash < 200)
        {
            sc["ev_bet"] = 0xFF;
            Event(-61, 74, "The town fair has opened!");
            return;
        }
        // a random animal of the farm is nominated; its kind gives the result picture (70 + kind)
        const int a = owned[static_cast<size_t>(_rng3.Bounded(static_cast<int>(owned.size())))];
        const int kind = (Rd16(_s.animals, static_cast<size_t>(a) * 32 + 6) - 0xA0) & 3;
        sc["ev_bet"] = kind;
        static const char* names[4] = { "horse", "cow", "pig", "sheep" };
        const std::string n = names[kind];
        _events.push_back({ kFairEvent, 74, "The town fair has opened! Enter your " + n + " in the " + n +
                                                " judging contest? The entry fee is 200.", true });
        Message("The town fair has opened.");
        return;
    }
    if (_s.cash < 1000)
    {
        sc["ev_kind"] = 0;
        return;
    }
    sc["ev_kind"] = 1;
    sc["ev_countdown"] = 2;
    _song = "rodeo.xmi";
    _events.push_back({ kRodeoEvent, 74, "The rodeo is starting: bet 500 on cousin Jed?", true });
    Message("The rodeo is starting.");
}

// the weekly countdown; at zero the result box and the ground back to normal
void Simulation::ContestWeek()
{
    auto& sc = _s.scalars;
    if (sc["ev_countdown"] == 0 || --sc["ev_countdown"] != 0)
        return;
    if (sc["ev_entry"] != 0 && sc["ev_kind"] == 1)
    {
        // the rodeo: Jed wins one time in four and pays 1,000
        if ((_rng.Next() & 3) == 2)
        {
            _events.push_back({ -63, 75, "Cousin Jed won the rodeo! You get 1,000 back, 500 more than the bet.", false });
            Credit(0, 1000, "Rodeo prize");
            Sound("cheers.voc");
        }
        else
        {
            GameEvent e{ -64, 76, "Cousin Jed lost the rodeo, and your 500 with him.", false };
            e.okFace = 17;
            _events.push_back(e);
            Sound("boos.voc");
        }
    }
    else if (sc["ev_entry"] != 0 && sc["ev_kind"] == 2 && sc["ev_bet"] != 0xFF)
    {
        // the fair: first prize half the time, 1,000
        static const char* names[4] = { "horse", "cow", "pig", "sheep" };
        const int kind = static_cast<int>(sc["ev_bet"] & 3);
        if (_rng.Next() & 1)
        {
            _events.push_back({ -65, 70 + kind, std::string("Your ") + names[kind] + " won first prize at the fair! You win 1,000.", false });
            Credit(0, 1000, "Fair prize");
            Sound("cheers.voc");
        }
        else
        {
            _events.push_back({ -66, 74, std::string("Your ") + names[kind] + " did not win. Maybe next year.", false });
            Sound("boos.voc");
        }
        sc["ev_bet"] = 0xFF;
    }
    if (sc["ev_kind"] == 2)
        RestoreFairCells(_s);
    sc["ev_kind"] = 0;
}

bool Simulation::Consume(int x, int y, int type, int amount)
{
    // an animal eats from the hay pile (type 300) or drinks from the trough (type 299) on cell (x, y): the amount
    // falls by the animal's appetite; an emptied pile disappears, an emptied trough shows the empty picture
    // There is no daily decay of hay piles: the routine for that is never called in the original.
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if (!(Rd8(_s.items, b + 10) & 0x20) || Rd16(_s.items, b) != type || Rd8(_s.items, b + 8) != x || Rd8(_s.items, b + 9) != y)
            continue;
        // the original subtracts unsigned 16-bit and only an exact 0 empties the pile or trough, so eating past it would
        // wrap round to an endless pile; its saves never show that, so the engine stops at 0 (engine choice)
        const int left = std::max(0, Rd16(_s.items, b + 2) - amount);
        Wr16(_s.items, b + 2, left);
        MapCell& c = _s.world.At(x, y);
        if (type == 300)
        {
            if (left == 0)
            {
                RemoveItem(i);
                c.base &= static_cast<uint16_t>(~0x800);
                c.overlay &= 0xF800;
            }
            else
                c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | HayTile(left));
        }
        else if (left == 0)
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 175);
        return true;
    }
    return false;
}

void Simulation::RemoveAnimal(int index)
{
    const size_t b = static_cast<size_t>(index) * 32;
    if (!(Rd8(_s.animals, b + 2) & 0x20))
        return;
    // a sheltered animal only frees its barn slot; one on the map leaves its cell
    const bool sheltered = Rd8(_s.animals, b + 2) & 4;
    _animals->Unshelter(index);
    Wr8(_s.animals, b + 2, Rd8(_s.animals, b + 2) & ~0x20);
    _s.animalCount = std::max(0, _s.animalCount - 1);
    if (!sheltered)
        ClearAnimalCell(Rd8(_s.animals, b), Rd8(_s.animals, b + 1));
}

void Simulation::ClearAnimalCell(int x, int y)
{
    Puff(x, y);
    if (x >= kMapSize || y >= kMapSize)
        return;
    MapCell& c = _s.world.At(x, y);
    const int o = c.overlay & 0x7FF;
    if (o >= 946 && o <= 1009)
    {
        c.base &= static_cast<uint16_t>(~0x0800);
        c.overlay &= 0xF800;
    }
}

void Simulation::DailyLivestock()
{
    const int temp = DailyTemperature(); // the day's temperature
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 32;
        const int flags = Rd8(_s.animals, b + 2);
        if (!(flags & 0x20))
            continue;
        const int sp = std::clamp(Rd16(_s.animals, b + 6) - 0xA0, 0, 3);
        int thirst = std::min(255, Rd8(_s.animals, b + 0x0D) + kThirstStep[sp]);
        int hunger = std::min(255, Rd8(_s.animals, b + 0x0E) + kHungerStep[sp]);
        if ((temp < 32 || temp > 95) && !(flags & 4))
            Wr8(_s.animals, b + 8, std::max(0, Rd8(_s.animals, b + 8) - 5));
        // eating, drinking and dying are checked at every animal step (Animals::Needs), as in the original
        Wr8(_s.animals, b + 0x0D, thirst);
        Wr8(_s.animals, b + 0x0E, hunger);
    }
}

// ---------------------------------------------------------------- market
void Simulation::Answer(int id, bool yes)
{
    if (id == kGameOverEvent)
    {
        _gameOverChoice = yes ? 2 : 1;
        if (!yes)
            Event(_gameOverTax ? 37 : 40, 75, _gameOverTax ? "The tax board sold assets to cover your taxes."
                                                           : "The bank sold enough of your assets to pay the loan.");
        return;
    }
    if (!yes)
        return;
    if (id == kBulldozeFieldEvent && _pendingBulldoze > 0 && Spend(-1, 25))
    {
        DeleteField(_pendingBulldoze);
        _pendingBulldoze = 0;
    }
    // YES pays the bet / the entry fee now; the entry word holds the payout flag
    if (id == kRodeoEvent && _s.scalars["ev_kind"] == 1 && Spend(-1, 500, "Rodeo entry"))
        _s.scalars["ev_entry"] = 1000;
    if (id == kFairEvent && _s.scalars["ev_kind"] == 2 && Spend(-1, 200, "Fair entry"))
        _s.scalars["ev_entry"] = 1;
}

void Simulation::SetTownLayout(const std::vector<uint8_t>& cmp)
{
    _townLayout.clear();
    if (cmp.size() < 1920)
        return;
    for (size_t i = 0; i < 960; ++i)
        _townLayout.push_back(static_cast<uint16_t>(cmp[2 * i] | (cmp[2 * i + 1] << 8)));
}

bool Simulation::BuyPlot(int c, int r, std::string& why)
{
    if (c < 0 || r < 0 || c > 11 || r > 11 || _s.PlotOwner(c, r) != 0)
    {
        why = "Only free land can be bought.";
        return false;
    }
    const int64_t price = PlotLandValue(c, r);
    if (!Spend(2000, price))
    {
        why = "Not enough money for this land.";
        return false;
    }
    _s.SetPlotOwner(c, r, 1);
    // every cell marked as your land (overlay flag 0x800), then fenced along the sides that do not border your other
    // land
    for (int x = c * 8; x < c * 8 + 8; ++x)
        for (int y = r * 8; y < r * 8 + 8; ++y)
            _s.world.At(x, y).overlay |= 0x800;
    FencePlot(_s, c, r);
    Message("You bought a plot of land for " + std::to_string(price) + ".");
    return true;
}

bool Simulation::SellPlot(int c, int r, std::string& why, bool income)
{
    if (c < 0 || r < 0 || c > 11 || r > 11 || _s.PlotOwner(c, r) != 1)
    {
        why = "You can only sell your own land.";
        return false;
    }
    if (c == Scalar("farm_plot_x") && r == Scalar("farm_plot_y"))
    {
        Event(13, 74, "The homestead plot cannot be sold.");
        why = "The homestead plot cannot be sold.";
        return false;
    }
    for (int x = c * 8; x < c * 8 + 8; ++x)
        for (int y = r * 8; y < r * 8 + 8; ++y)
            if (FieldIndexAt(x, y))
            {
                Event(14, 74, "Fields on the plot must be removed before it can be sold.");
                why = "Remove the fields on this plot first.";
                return false;
            }
    const int64_t price = PlotSalePrice(c, r);
    // everything on it goes: machines and animals there are lost with the land
    for (int x = c * 8; x < c * 8 + 8; ++x)
        for (int y = r * 8; y < r * 8 + 8; ++y)
        {
            for (int i = 1; i < 127; ++i)
            {
                Machine m(_s.machines, i);
                if (m.InUse() && m.X() == x && m.Y() == y && m.Type() < 8)
                    _machines->Remove(i);
            }
            for (int i = 1; i < 256; ++i)
            {
                const size_t b = static_cast<size_t>(i) * 32;
                if ((Rd8(_s.animals, b + 2) & 0x20) && Rd8(_s.animals, b) == x && Rd8(_s.animals, b + 1) == y)
                    RemoveAnimal(i);
            }
            MapCell& cell = _s.world.At(x, y);
            cell.overlay &= static_cast<uint16_t>(~0x4800); // no longer your land
            if (cell.base & 0x3000)
                continue; // water stays
            cell.base = static_cast<uint16_t>(GroundTile(_s, _wetness, x, y));
            cell.overlay &= 0xA000;
        }
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        if ((Rd8(_s.structures, b + 2) & 0x20) && Rd8(_s.structures, b + 4) / 8 == c && Rd8(_s.structures, b + 5) / 8 == r)
        {
            Wr8(_s.structures, b + 2, 0);
            _s.structureCount = std::max(0, _s.structureCount - 1);
        }
    }
    _s.SetPlotOwner(c, r, 0);
    ReconcileCash();
    const int64_t before = _s.cash;
    _s.cash = std::min<int64_t>(9999999, _s.cash + price);
    PostCash(_s.cash - before, "Land sold");
    Ledger("rev_land") += price;
    if (income)
        Ledger("yearly_income") += price; // the auction's sale is not yearly income (seen live)
    Message("You sold a plot of land for " + std::to_string(price) + ".");
    return true;
}

// ---------------------------------------------------------------- the town
int Simulation::TownLevels() const
{
    int levels = 0;
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
            if (_s.PlotOwner(c, r) == 2)
                levels += _s.PlotType(c, r) >> 4;
    return levels;
}

bool Simulation::TownPickPlot(int& pc, int& pr) const
{
    // rings around the town's home plot, columns outer and rows inner: the first free plot none of whose cells is
    // water or flagged (cell flags 0x1000/0x2000)
    const int h1 = static_cast<int>(Scalar("homestead_x")), h2 = static_cast<int>(Scalar("homestead_y"));
    for (int k = 1; k < 12; ++k)
        for (int c = h1 - k; c <= h1 + k; ++c)
            for (int r = h2 - k; r <= h2 + k; ++r)
            {
                if (c < 0 || r < 0 || c >= 12 || r >= 12 || _s.PlotOwner(c, r) != 0)
                    continue;
                bool bad = false;
                for (int x = c * 8; x < c * 8 + 8 && !bad; ++x)
                    for (int y = r * 8; y < r * 8 + 8 && !bad; ++y)
                        bad = (_s.world.At(x, y).base & 0x3000) != 0;
                if (!bad)
                {
                    pc = c;
                    pr = r;
                    return true;
                }
            }
    return false;
}

bool Simulation::TownOffer()
{
    // the town buys its next plot with its reserve (price 15,000); only then is the player asked to vote
    int c = 0, r = 0;
    constexpr int64_t kPlotPrice = 15000;
    if (!TownPickPlot(c, r) || _townCarry < kPlotPrice)
        return false;
    _townCarry -= kPlotPrice;
    _s.SetPlotOwner(c, r, 2);
    _s.scalars["town_work_x"] = c;
    _s.scalars["town_work_y"] = r;
    return true;
}

bool Simulation::TownVote(int button)
{
    // the four buttons: residential (types 6-13), commercial (1-3), airport (5), recreational (4)
    const int c = static_cast<int>(Scalar("town_work_x")), r = static_cast<int>(Scalar("town_work_y"));
    int type = 5;
    if (button == 0)
        type = 6 + (G2Step() & 7);
    else if (button == 1)
    {
        type = (G2Step() & 3) + 1;
        if (type == 4)
            type = 3;
    }
    else if (button == 3)
        type = 4;
    return PlaceTownPlot(c, r, type);
}

void Simulation::HomesteadTier(int tier)
{
    // nothing when the tier stays; a poorer tier boos with a warning box, a richer one (above the first)
    // cheers with cowman music and a congratulation; the first call after a start or load only draws
    if (tier == _homeTier)
        return;
    if (_homeTier >= 0 && tier < _homeTier)
    {
        Sound("boos.voc");
        Event(-67, 74, "Your cash is shrinking fast, and your home is getting shabbier to match.");
    }
    else if (_homeTier >= 0 && tier > 0)
    {
        Sound("cheers.voc");
        _song = "cowman.xmi";
        Event(-68, -1, "Business is booming! With all that cash, your family has fixed up the house.");
    }
    _homeTier = tier;
    DrawHomestead(_s, static_cast<int>(Scalar("view_x")), static_cast<int>(Scalar("view_y")), tier);
}

void Simulation::TownVoteTimeout()
{
    // an unanswered Town Choice dialog: the type is 1 + G3 bounded 14 (any town plot but the home plot); a refused
    // event ground leaves the plot bought but unbuilt, as in the original
    const int c = static_cast<int>(Scalar("town_work_x")), r = static_cast<int>(Scalar("town_work_y"));
    PlaceTownPlot(c, r, _rng3.Bounded(14) + 1);
}

bool Simulation::TownBuildPlot(int type)
{
    // a plot without a vote (the start of a generated world, tests): picked like the town's own purchase
    int c = 0, r = 0;
    if (!TownPickPlot(c, r))
        return false;
    _s.SetPlotOwner(c, r, 2);
    _s.scalars["town_work_x"] = c;
    _s.scalars["town_work_y"] = r;
    if (!PlaceTownPlot(c, r, type))
    {
        _s.SetPlotOwner(c, r, 0);
        return false;
    }
    return true;
}

bool Simulation::PlaceTownPlot(int c, int r, int type)
{
    // an event ground (type 4) needs fewer than 7 grounds and 16 developed plots per ground
    int grounds = 0, developed = 0;
    for (int cc = 0; cc < 12; ++cc)
        for (int rr = 0; rr < 12; ++rr)
            if (_s.PlotOwner(cc, rr) == 2)
            {
                developed++;
                grounds += _s.PlotType(cc, rr) != 0xFF && (_s.PlotType(cc, rr) & 15) == 4;
            }
    if (type == 4)
    {
        if (grounds * 16 >= developed)
        {
            Message("The town is too small for another event ground.");
            return false;
        }
        if (grounds >= 7)
            return false;
        if (_s.eventGrounds.size() >= 16 && grounds < 8)
        {
            _s.eventGrounds[static_cast<size_t>(grounds)] = static_cast<uint8_t>(c * 8);
            _s.eventGrounds[static_cast<size_t>(8 + grounds)] = static_cast<uint8_t>(r * 8);
        }
        _s.scalars["event_grounds"] = grounds + 1;
    }
    if (type == 5)
        _s.scalars["airports"]++;
    // the plot's cells: roads of block (type mod 5, type div 5) on bare ground, an airport its whole
    // block, the town flag everywhere; the buildings come with the levels (RedrawTownPlot)
    PlaceTownPlotCells(_s, _townLayout, c, r, type);
    _s.SetPlotOwner(c, r, 2);
    // the plot byte is the type at level 0; the homestead and an airport start at level 10
    _s.SetPlotType(c, r, (type == 0 || type == 5) ? (0xA0 | type) : type);
    return true;
}

bool Simulation::TownChangeLevel(int c, int r, int delta)
{
    const int b = _s.PlotType(c, r);
    if ((b & 15) == 5)
        return true; // an airport never changes (the town still pays for it)
    const int level = b >> 4;
    if (delta > 0)
    {
        if (level >= 13)
            return false;
        _s.SetPlotType(c, r, ((level + 1) << 4) | (b & 15));
        RedrawTownPlot(_s, _townLayout, c, r, [this] { return G2Step(); }); // the original's plot redraw
        return true;
    }
    if (level > 0)
    {
        _s.SetPlotType(c, r, ((level - 1) << 4) | (b & 15));
        RedrawTownPlot(_s, _townLayout, c, r, [this] { return G2Step(); });
        return true;
    }
    return TownClearPlot(c, r);
}

bool Simulation::TownClearPlot(int c, int r)
{
    const int h1 = static_cast<int>(Scalar("homestead_x")), h2 = static_cast<int>(Scalar("homestead_y"));
    if (c == h1 && r == h2)
        return false; // the homestead stays
    if ((_s.PlotType(c, r) & 15) == 4 && _s.scalars["event_grounds"] > 0)
        _s.scalars["event_grounds"]--;
    _s.SetPlotOwner(c, r, 0);
    _s.SetPlotType(c, r, 0xFF);
    // the next plot to work: rings around this one for another town plot that is in neither the homestead's
    // column nor its row (the original tests the two separately); else the homestead
    int wc = h1, wr = h2;
    bool found = false;
    for (int k = 1; k < 12 && !found; ++k)
        for (int cc = c - k; cc <= c + k && !found; ++cc)
            for (int rr = r - k; rr <= r + k && !found; ++rr)
                if (cc >= 0 && rr >= 0 && cc < 12 && rr < 12 && _s.PlotOwner(cc, rr) == 2 && cc != h1 && rr != h2)
                {
                    wc = cc;
                    wr = rr;
                    found = true;
                }
    _s.scalars["town_work_x"] = wc;
    _s.scalars["town_work_y"] = wr;
    for (int x = c * 8; x < c * 8 + 8; ++x)
        for (int y = r * 8; y < r * 8 + 8; ++y)
        {
            // the town and object flags go (0x4800 of the overlay word); water (family 1, 39-68) stays,
            // every other cell becomes the ground of the soil wetness, keeping its flag bits
            MapCell& m = _s.world.At(x, y);
            m.overlay = static_cast<uint16_t>(m.overlay & ~0x4800);
            if (m.BaseTile() >= 39 && m.BaseTile() < 69)
                continue;
            m.base = static_cast<uint16_t>((m.base & 0xF800) | GroundTile(_s, _wetness, x, y));
        }
    Message("A town plot has been abandoned.");
    return true;
}

void Simulation::TownGrowthWeek()
{
    // every week the plot the town works on grows a level while the pot holds 25, or loses one while it is negative
    const int c = static_cast<int>(Scalar("town_work_x")), r = static_cast<int>(Scalar("town_work_y"));
    if (c < 0 || r < 0 || c >= 12 || r >= 12 || _s.PlotOwner(c, r) != 2)
        return;
    if (_townPot >= 25)
    {
        if (TownChangeLevel(c, r, +1))
            _townPot -= 25;
    }
    else if (_townPot < 0 && TownChangeLevel(c, r, -1))
    {
        _townPot += 25;
        if (_townPot >= 0)
            _townPot = 0;
    }
    _s.scalars["town_pot"] = _townPot;
}

void Simulation::GenerateHistory(int c)
{
    // from the current price forwards, one week per word, in quarter-price steps: up = +1 or +2 quarters,
    // down = 0 or -1 quarter, each only half the time; the market trend picks the direction (trend 0: r <= 3 down,
    // trend 1: r <= 1 down, trend 2: only r >= 6, up); no clamp, 16-bit values. After the crops are loaded the newest
    // week holds the price again (seen in memory after every load)
    const int price = _s.MarketPrice(c);
    const int q = price >> 2;
    uint16_t p = static_cast<uint16_t>(price);
    for (int i = 0; i < 30; ++i)
    {
        _history[c][i] = p;
        const int r = _rng.Next() & 7;
        const int dir = _trend == 0 ? (r <= 3 ? -1 : 1) : _trend == 1 ? (r <= 1 ? -1 : 1) : (r >= 6 ? 1 : 0);
        if (dir == 0 || !(_rng.Next() & 1))
            continue;
        const int k = (_rng.Next() & 1) + (dir > 0 ? 1 : -1);
        p = static_cast<uint16_t>(p + k * q);
    }
    _history[c][29] = price;
}

bool Simulation::CropSlotInUse(int slot) const
{
    for (int i = 1; i < 98; ++i)
    {
        const Field f = const_cast<GameState&>(_s).FieldAt(i);
        if (f.InUse() && f.Crop() == slot)
            return true;
    }
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if (b + 12 > _s.items.size())
            break;
        const int type = Rd16(_s.items, b);
        if ((Rd8(_s.items, b + 10) & 0x20) && (type == 0xA8 + slot || type == 0xC0 + slot))
            return true;
    }
    return false;
}

int Simulation::FreeCropSlot() const
{
    for (int slot = 15; slot >= 0; --slot)
        if (!CropSlotInUse(slot))
            return slot;
    return -1;
}

bool Simulation::ReplaceCrop(int slot, const std::string& fileName, std::string& why)
{
    // refused (event 43 with the farmer picture) when the file is already loaded in any slot,
    // when a field in use has the crop (whatever its state), or when a seed bag (0xC0 + slot) or stored harvest
    // (0xA8 + slot) of it exists; otherwise the slot takes the file's name, pictures and parameters, its price is the
    // file's base price, its seed price max(5, P18 / 4), and only the newest week of the price history is set (the older
    // weeks keep the replaced crop's values)
    auto lower = [](std::string t) {
        for (char& ch : t)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return t;
    };
    bool refused = false;
    for (int c = 0; c < 16 && !refused; ++c)
    {
        std::string loaded;
        for (int k = 0; k < 14 && _s.cropNames[static_cast<size_t>(c) * 14 + k]; ++k)
            loaded += static_cast<char>(_s.cropNames[static_cast<size_t>(c) * 14 + k]);
        refused = lower(loaded) == lower(fileName);
    }
    for (int i = 1; i < 98 && !refused; ++i)
    {
        Field f = _s.FieldAt(i);
        refused = f.InUse() && f.Crop() == slot;
    }
    for (int i = 1; i < 512 && !refused; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if (b + 12 > _s.items.size())
            break;
        const int type = Rd16(_s.items, b);
        refused = (Rd8(_s.items, b + 10) & 0x20) && (type == 0xA8 + slot || type == 0xC0 + slot);
    }
    if (refused)
    {
        Event(43, 74, "That crop is in use or already loaded. Sell or remove everything that belongs to it first.");
        why = "in use";
        return false;
    }
    CropDef c;
    if (_gameDir.empty() || !LoadCropFile(_gameDir, fileName, c))
    {
        why = "The crop file could not be read.";
        return false;
    }
    for (int k = 0; k < 14; ++k)
        Wr8(_s.cropNames, static_cast<size_t>(slot) * 14 + k, k < static_cast<int>(fileName.size()) ? fileName[k] : 0);
    // a crop file with an entry in the engine's market of the other crops brings its price and history along (the
    // original's Load Crop starts at the base price: the PLANT list's loading is an engine addition)
    std::string up = fileName;
    for (char& ch : up)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const auto spare = std::find_if(_spare.begin(), _spare.end(), [&](const SpareCropMarket& m) { return m.file == up; });
    _s.crops[slot] = c;
    _s.SetMarketPrice(slot, c.BasePrice());
    _s.SetCatalogPrice(66 + slot, c.SeedPrice());
    _history[slot][29] = c.BasePrice();
    if (spare != _spare.end())
    {
        for (int i = 0; i < 30; ++i)
            _history[slot][i] = spare->history[i];
        _s.SetMarketPrice(slot, spare->history[29] & 0xFFFF);
        _s.SetCatalogPrice(50 + slot, spare->history[29] & 0xFFFF);
        _s.SetCatalogPrice(66 + slot, std::max(5, spare->history[29] >> 2));
    }
    return true;
}

void Simulation::DeleteField(int index)
{
    Field f = _s.FieldAt(index);
    if (!f.InUse())
        return;
    _machines->ReleaseField(index);
    for (int x = f.X(); x < f.X() + f.W() && x < kMapSize; ++x)
        for (int y = f.Y(); y < f.Y() + f.H() && y < kMapSize; ++y)
        {
            MapCell& c = _s.world.At(x, y);
            if (((x - f.X()) & 3) == 1 && ((y - f.Y()) & 3) == 1)
                Puff(x, y);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | GroundTile(_s, _wetness, x, y));
            c.overlay &= static_cast<uint16_t>(~0x1000);
        }
    // the original only clears the in-use bit: the rest of the record and the schedule column stay as they were
    f.Set8(0x05, f.Get8(0x05) & ~1);
    _s.fieldCount = std::max(0, _s.fieldCount - 1);
}

void Simulation::AskBulldozeField(int index)
{
    _pendingBulldoze = index;
    _events.push_back({ kBulldozeFieldEvent, 74, "Are you sure you want to destroy this field?", true, -1, -1, 3 }); // YES / CANCEL
}

int Simulation::SpareRandom()
{
    _spareRng = _spareRng * 1103515245u + 12345u;
    return static_cast<int>((_spareRng >> 16) & 0x7FFF);
}

const std::vector<Simulation::SpareCropMarket>& Simulation::SpareMarket()
{
    auto upper = [](std::string t) {
        for (char& ch : t)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return t;
    };
    std::string loaded;
    for (int c = 0; c < 16; ++c)
        loaded += upper(_s.crops[c].fileName) + "|";
    if (loaded == _spareLoaded && !_spare.empty())
        return _spare;
    // (re)built when the slots changed: entries kept for files still not loaded, new ones made
    std::vector<SpareCropMarket> old;
    old.swap(_spare);
    _spareLoaded = loaded;
    std::error_code ec;
    const auto dir = _gameDir.empty() ? std::nullopt : FindFileNoCase(_gameDir, "CROPS");
    if (!dir)
        return _spare;
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(*dir, ec))
    {
        const std::string n = upper(e.path().filename().string());
        if (n.size() > 4 && n.substr(n.size() - 4) == ".CRP" && loaded.find(n + "|") == std::string::npos)
            files.push_back(n);
    }
    std::sort(files.begin(), files.end());
    for (const std::string& n : files)
    {
        auto it = std::find_if(old.begin(), old.end(), [&](const SpareCropMarket& m) { return m.file == n; });
        if (it != old.end())
        {
            _spare.push_back(*it);
            continue;
        }
        CropDef def;
        if (_spare.size() >= 96 || !LoadCropFile(_gameDir, n, def) || def.art.size() < 14 * 128)
            continue;
        SpareCropMarket m;
        m.file = n;
        m.name = def.name;
        m.basePrice = def.BasePrice();
        m.yield = def.Yield();
        const TileSheet t = DecodeTiles16(def.art, 14);
        m.icon.assign(t.Tile(13), t.Tile(13) + 256);
        // the history as the original makes the loaded crops' at game start (GenerateHistory), from the base price
        const int q = m.basePrice >> 2;
        int p = m.basePrice;
        for (int i = 0; i < 30; ++i)
        {
            m.history[i] = p;
            const int r = SpareRandom() & 7;
            const int dir2 = _trend == 0 ? (r <= 3 ? -1 : 1) : _trend == 1 ? (r <= 1 ? -1 : 1) : (r >= 6 ? 1 : 0);
            if (dir2 == 0 || !(SpareRandom() & 1))
                continue;
            p = std::clamp(p + ((SpareRandom() & 1) + (dir2 > 0 ? 1 : -1)) * q, std::max(10, m.basePrice >> 1), 2 * m.basePrice);
        }
        m.history[29] = m.basePrice;
        _spare.push_back(std::move(m));
    }
    return _spare;
}

void Simulation::SpareMarketWeek()
{
    for (SpareCropMarket& m : _spare)
    {
        int p = m.history[29];
        const int step = p >> 3;
        const int d = SpareRandom() & 7;
        const bool up = _trend == 0 ? d <= 3 : _trend == 1 ? d <= 1 : d <= 5;
        const int amount = (SpareRandom() & 3) * step;
        p += up ? amount : -amount;
        p = std::clamp(p, std::max(10, m.basePrice >> 1), std::max(10, 2 * m.basePrice));
        std::rotate(m.history.begin(), m.history.begin() + 1, m.history.end());
        m.history[29] = p;
    }
}

void Simulation::MarketWeek()
{
    SpareMarket(); // made (or brought up to date with the slots) before its week
    SpareMarketWeek();
    const int r = _rng.Next() & 0xF;
    if (r >= 13)
        _trend = _rng3.Bounded(3);
    for (int c = 0; c < 16; ++c)
    {
        // the step starts from the last week of the price history (a disaster shock raises that value, so the shown
        // price follows only now)
        int p = _history[c][29];
        int step = p >> 3;
        if (p == 0) { p = 1; step = 0; }
        const int d = _rng.Next() & 7;
        const bool up = _trend == 0 ? d <= 3 : _trend == 1 ? d <= 1 : d <= 5;
        const int amount = (_rng.Next() & 3) * step;
        p += up ? amount : -amount;
        const int base = _s.crops[c].loaded ? _s.crops[c].BasePrice() : std::max(10, p / 2);
        if (2 * base < p) p = 2 * base;
        if ((base >> 1) > p) p = base >> 1;
        if (p < 10) { p = 10; _trend = 1; }
        _s.SetMarketPrice(c, p & 0xFFFF);
        _s.SetCatalogPrice(50 + c, p & 0xFFFF);              // mirror run
        _s.SetCatalogPrice(66 + c, std::max(5, p >> 2));     // seed price
        std::rotate(_history[c].begin(), _history[c].begin() + 1, _history[c].end());
        _history[c][29] = p;
    }
}

// ---------------------------------------------------------------- week / month / year
// At the week change the week's accumulated rain and drying move the soil wetness: by acc >> 3
// (arithmetic shift) when acc >= 8 or acc <= -8, clamped to 0..12, then acc = 0; otherwise acc carries over.
// A wet week during a flood adds 4 more. Every such week the original re-tiles every bare-ground cell of the map
// (tiles 15..30) to 15 + w + terrain/4, also when w did not change, so tiles left from another wetness (a save made
// in another session) follow too.
void Simulation::WeeklyWetness()
{
    if (_acc > -8 && _acc < 8)
        return;
    int step = _acc >> 3;
    if (_acc >= 8 && _disasters->Flooding())
        step += 4;
    _wetness = std::clamp(_wetness + step, 0, 12);
    _acc = 0;
    for (int x = 0; x < kMapSize; ++x)
        for (int y = 0; y < kMapSize; ++y)
        {
            MapCell& c = _s.world.At(x, y);
            const int id = c.BaseTile();
            if (id >= 15 && id <= 30)
                c.base = static_cast<uint16_t>((c.base & 0xF800) | GroundTileFor(_wetness, c));
        }
}

void Simulation::WeekTick()
{
    ContestWeek(); // in the weekly chain, before the week advances and before the month's offers
    // a new car at the town plot's corner, heading north
    _traffic->Add(static_cast<int>(Scalar("homestead_x")) * 8, static_cast<int>(Scalar("homestead_y")) * 8, 0);
    _s.cal.week++;
    if (_s.cal.week >= 4)
    {
        _s.cal.week = 0;
        MonthTick();
    }
    TownGrowthWeek(); // once a week, after the week (and month) advance; seen live: one level a week
    // the gust also decays by two each week; then the weekly weather routine: wetness step, and next week's
    // pattern becomes this week's
    _gust = _gust < 2 ? 0 : _gust - 2;
    WeeklyWetness();
    _weekPattern = _nextPattern;
    ComputePattern(WeatherIndex(1), _nextPattern);
    if (_s.WeatherRain(WeatherIndex(1)) > 3)
    {
        Message("Heavy rain is expected next week.");
        Sound("thunder.voc");
    }
    _disasters->Week();
    _machines->Week();
    {
        // mood music: distinct problem types on the farm against cash
        int bits = 0;
        for (int i = 1; i < 98; ++i)
        {
            Field f = _s.FieldAt(i);
            if (f.InUse())
                bits |= f.Status();
        }
        int types = 0;
        for (int b = 0; b < 8; ++b)
            types += (bits >> b) & 1;
        if (EventKind() == 0)
        {
            if (types > 3 && _s.cash < 5000)
                _song = "dfarm.xmi";
            else if (types < 2 && _s.cash > 500000)
                _song = "ffarm.xmi";
            else if (_song == "rodeo.xmi" || _song == "carnival.xmi")
                _song = "genfarm.xmi";
        }
    }
    // 1. deadline: jobs of the past week are completed
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (f.InUse() && (f.State() == 3 || f.State() == 5 || (f.State() >= 6 && f.State() <= 9)))
        {
            _machines->ReleaseField(i);
            CompleteJob(f);
        }
    }
    // 2. the schedule starts this week's jobs
    ExecuteSchedule();
    // 3. growth
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (f.InUse() && f.State() == 4)
            WeeklyGrowth(f);
    }
    // 4. market, ageing, next week's weather
    MarketWeek();
    for (int i = 1; i < 127; ++i)
    {
        // buildings age; old water towers may burst
        const size_t b = static_cast<size_t>(i) * 70;
        const int flags = Rd8(_s.structures, b + 2);
        if (!(flags & 0x20) || (flags & 0x02))
            continue;
        // age in weeks at +8, condition (age / 4, 0xFF from 1020 weeks) at +6
        const int age = (Rd16(_s.structures, b + 8) + 1) & 0xFFFF;
        Wr16(_s.structures, b + 8, age);
        Wr8(_s.structures, b + 6, age >= 0x3FC ? 0xFF : age >> 2);
        if (Rd16(_s.structures, b) == 0x48 && age > 480 && (_rng.Next() & 0x3F) == 7)
        {
            Wr8(_s.structures, b + 6, 0xFF);
            Wr16(_s.structures, b + 0x0A, 0);
            Wr8(_s.structures, b + 2, flags | 0x02);
            const int x = Rd8(_s.structures, b + 4), y = Rd8(_s.structures, b + 5);
            if (x < kMapSize && y < kMapSize)
                _s.world.At(x, y).base = static_cast<uint16_t>((_s.world.At(x, y).base & 0xF800) | 149);
            Event(44, 74, "A water tower has burst. Bulldoze it and buy a new one.");
        }
    }
    for (int i = 1; i < 127; ++i)
    {
        // placed objects (barrels): age word +0x0A and age byte +9 (it wraps), every week
        const size_t b = static_cast<size_t>(i) * 20;
        if (!(Rd8(_s.objects20, b + 2) & 0x20))
            continue;
        Wr16(_s.objects20, b + 0x0A, (Rd16(_s.objects20, b + 0x0A) + 1) & 0xFFFF);
        Wr8(_s.objects20, b + 9, (Rd8(_s.objects20, b + 9) + 1) & 0xFF);
    }
    for (int i = 1; i < 512; ++i)
    {
        // troughs are refilled every week
        const size_t b = static_cast<size_t>(i) * 12;
        if (!(Rd8(_s.items, b + 10) & 0x20) || Rd16(_s.items, b) != 299 || Rd16(_s.items, b + 2) != 0)
            continue; // only an empty trough is refilled
        // the water comes from the water towers: twice what they give, at most 64 drawn
        const int drawn = DrawWater(64);
        Wr16(_s.items, b + 2, 2 * drawn);
        const int x = Rd8(_s.items, b + 8), y = Rd8(_s.items, b + 9);
        if (drawn && x < kMapSize && y < kMapSize)
            _s.world.At(x, y).base = static_cast<uint16_t>((_s.world.At(x, y).base & 0xF800) | 176);
    }
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        const int flags = Rd8(_s.items, b + 10);
        if (!(flags & 0x20))
            continue;
        const int type = Rd16(_s.items, b);
        Wr16(_s.items, b + 6, (Rd16(_s.items, b + 6) + 1) & 0xFFFF); // every record's week counter
        if ((type >= 0xA8 && type <= 0xBF) || (type >= 0xC0 && type <= 0xD7))
        {
            int age = Rd16(_s.items, b + 4);
            if (age < 0xFFFA)
                age = std::min(0xFFFA, age + ((flags & 4) ? 1 : 4));
            Wr16(_s.items, b + 4, age);
        }
    }
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 32;
        if (!(Rd8(_s.animals, b + 2) & 0x20))
            continue;
        const int sp = std::clamp(Rd16(_s.animals, b + 6) - 0xA0, 0, 3);
        if ((_rng.Next() & 0x7F) == 0x4B)
        {
            // the animal's noise, 1 in 128 a week (G1; the call picks a cow or sheep recording with G2)
            static const char* const kOne[4] = { "horse1.voc", nullptr, "pig.voc", nullptr };
            Sound(kOne[sp] ? kOne[sp] : (sp == 1 ? ((G2Step() & 1) ? "cow2.voc" : "cow1.voc") : ((G2Step() & 1) ? "sheep2.voc" : "sheep1.voc")));
        }
        const int age = Rd16(_s.animals, b + 0x0A) + 1;
        Wr16(_s.animals, b + 0x0A, age);
        if (age > kAdultAge[sp])
            Wr8(_s.animals, b + 0x12, 1);
        if (age > kLifespan[sp])
        {
            Wr8(_s.animals, b + 2, Rd8(_s.animals, b + 2) & ~0x20);
            _s.animalCount = std::max(0, _s.animalCount - 1);
            ClearAnimalCell(Rd8(_s.animals, b), Rd8(_s.animals, b + 1));
            Message(std::string("A ") + kAnimalNames[sp] + " died of old age.");
            Sound("explode.voc");
            continue;
        }
        // pregnancy (conception is monthly, see Conception)
        const int flags = Rd8(_s.animals, b + 2);
        if ((flags & 2) && (flags & 1))
        {
            const int weeks = Rd8(_s.animals, b + 0x1C) + 1; // gestation counter, weeks
            Wr8(_s.animals, b + 0x1C, weeks);
            if (weeks > kGestation[sp])
            {
                Wr8(_s.animals, b + 2, flags & ~1);
                Wr8(_s.animals, b + 0x1C, 0);
                const int ax = Rd8(_s.animals, b), ay = Rd8(_s.animals, b + 1);
                for (int k = 0; k < 9; ++k)
                    if (_animals->Place(ax + k % 3 - 1, ay + k / 3 - 1, sp))
                    {
                        Message(std::string("A ") + kAnimalNames[sp] + " was born.");
                        // a newborn is added like a bought animal: the livestock asset grows by its catalogue price
                        _s.scalars["asset_livestock"] += _s.CatalogPrice(kAnimalCatalog[sp]);
                        break;
                    }
            }
        }
    }
    // at a month change, conception follows the births (the month block runs after the weekly chain)
    if (_s.cal.week == 0)
        Conception();
    // the weekly chain ends with birds singing one week in four (both draws from G2)
    if ((G2Step() & 3) == 2)
        Sound((G2Step() & 1) ? "bird1.voc" : "bird2.voc");
}

bool Simulation::Liquidate(int64_t debt, bool tax)
{
    // the auction: assets are sold class by class and after every sale the debt
    // is charged; it stops as soon as the charge succeeds. The crop-stack phase charges the debt to the Loan Interest
    // line for taxes and to the Taxes line for the bank (the original's category pair), every later phase to the
    // Machinery line (category 0). Nothing is added for the auction.
    const int stackCategory = tax ? 1000 : 2500;
    // the auction opens with its announcement: events.dat 36 (tax board) or 39 (bank), posted as the plain id
    Event(tax ? 36 : 39, 76, tax ? "Your taxes were not paid in the grace period: your assets will be auctioned."
                                 : "The bank forecloses: your assets will be auctioned to pay the loan.");
    for (const SaleItem& it : SaleList(_s, *this, 0))
    {
        // a stored crop stack, credited once to Crop Sales
        const size_t b = static_cast<size_t>(it.index) * 12;
        const int crop = (Rd16(_s.items, b) - 0xA8) & 15;
        FreeStorageSlot(it.index);
        RemoveItem(it.index);
        _s.SetCatalogOwn(50 + crop, std::max(0, _s.CatalogOwn(50 + crop) - 1));
        Credit(168 + crop, it.value);
        if (Spend(stackCategory, debt))
            return true;
    }
    for (int kind : { 2, 1 }) // animals, then owned machines
        for (const SaleItem& it : SaleList(_s, *this, kind))
        {
            ShopSell(_s, *this, it);
            if (Spend(0, debt))
                return true;
        }
    // then land, except the homestead plot: its fields are deleted first; the price is not yearly income
    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
        {
            if (_s.PlotOwner(c, r) != 1 || (c == Scalar("farm_plot_x") && r == Scalar("farm_plot_y")))
                continue;
            // the fields go first, found as the original finds them (the field search over the plot's x, y, 8, 8): a field
            // whose 8 x 8 block holds any of the corners (x, y), (x + 8, y), (x, y + 8), (x + 8, y + 8). So every
            // field overlapping the plot goes, and also one that starts just right of or below it
            for (int i = 1; i < 98; ++i)
            {
                Field f = _s.FieldAt(i);
                if (!f.InUse())
                    continue;
                auto holds = [&](int px, int py) { return f.X() <= px && px < f.X() + 8 && f.Y() <= py && py < f.Y() + 8; };
                const int x = c * 8, y = r * 8;
                if (holds(x, y) || holds(x + 8, y) || holds(x, y + 8) || holds(x + 8, y + 8))
                    DeleteField(i);
            }
            std::string why;
            if (SellPlot(c, r, why, false))
                Message("A plot of your land was auctioned.");
            if (Spend(0, debt))
                return true;
        }
    // nothing left to sell: the farm is lost. The auction itself asks (farmer picture 74, YES / NO): YES
    // returns 0, so the caller shows nothing and a new game starts; NO, or no answer within 1280 ticks, returns
    // nonzero, which the caller takes for a paid debt: it shows the "sold enough" notice and the program then ends
    // (checked on the running original)
    _bankrupt = true;
    _gameOverTax = tax;
    GameEvent e{ kGameOverEvent, 74, "Your homestead had to go to pay your debts. Do you want to start a new game?", true };
    e.status = false;
    e.always = true;
    _events.push_back(e);
    return false;
}

void Simulation::Conception()
{
    // conception, called only from the month block: an adult, non-pregnant female with condition > 110 becomes
    // pregnant (no male is needed); the weekly chain then counts the pregnancy up to the birth
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 32;
        const int flags = Rd8(_s.animals, b + 2);
        if (!(flags & 0x20) || !(flags & 2) || (flags & 1))
            continue;
        const int sp = std::clamp(Rd16(_s.animals, b + 6) - 0xA0, 0, 3);
        if (Rd16(_s.animals, b + 0x0A) > kAdultAge[sp] && Rd8(_s.animals, b + 8) > 0x6E)
        {
            Wr8(_s.animals, b + 2, flags | 1);
            Wr8(_s.animals, b + 0x1C, 0);
        }
    }
}

void Simulation::MonthTick()
{
    // the original's month change, in its order: the July / October contest offer (it runs at the week rollover,
    // before the month block), the month advances (the generator is nudged by the month number 1..12), the loan
    // counter, the tax counter (or, without one, the July / October tax warning), the year end, the quarterly
    // collection, conception (run from WeekTick after the weekly livestock pass, because the original's weekly chain
    // with the births comes before its month block). A late loan payment clears what is owed but leaves the balance
    // as it is.
    const int next = _s.cal.month + 1; // 1..12
    _s.cal.month = next % 12;
    const int m = _s.cal.month;
    ContestOffers();
    _rng.MonthNudge(next);
    const int64_t owed = Ledger("loan_payment");
    if (_loanGrace > 0)
    {
        if (Spend(1000, owed)) { _loanGrace = 0; Event(42, 75, "The bank thanks you: the quarter's loan payment is made."); }
        else if (--_loanGrace == 0)
        {
            if (Liquidate(owed, false)) { Event(40, 75, "The bank sold enough of your assets to pay the loan."); Sound("explode.voc"); }
        }
        else Event(41, 74, "Another month has passed and the loan payment is still due.");
    }
    if (_taxGrace > 0)
    {
        int64_t& due = Ledger("tax_due");
        if (Spend(2500, due)) { due = 0; _taxGrace = 0; Event(35, 76, "Your back taxes are paid in full."); }
        else if (--_taxGrace == 0)
        {
            if (Liquidate(due, true)) { due = 0; Event(37, 75, "The tax board sold assets to cover your taxes."); }
        }
        else Event(34, 74, "Another month has passed and your taxes are still unpaid.");
        _s.scalars["tax_counter"] = _taxGrace;
    }
    else if ((m == 6 || m == 9) && _s.cash < EstimatedTax())
        Event(33, 74, "Your cash is below the estimated yearly tax.");
    if (next == 12)
        YearTick();
    int64_t& balance = Ledger("loan_balance");
    const int64_t payment = Ledger("loan_payment");
    // quarterly loan collection (January, March, July, October)
    if ((m == 0 || m == 2 || m == 6 || m == 9) && balance > 0 && payment > 0)
    {
        if (Spend(1000, payment))
        {
            balance = std::max<int64_t>(0, balance - payment);
            _townCarry += payment >> 1; // the bank's share goes to the town's reserve and pot
            _townPot += payment >> 2;
            _s.scalars["town_pot"] = _townPot;
            if (balance == 0)
                Ledger("loan_payment") = 0;
        }
        else
        {
            _loanGrace = 4;
            Event(38, 74, "You could not make the loan payment. The bank will foreclose in three months.");
        }
    }
}

void Simulation::YearTick()
{
    // taxes and the town fund
    const int64_t tax = EstimatedTax();
    int64_t& due = Ledger("tax_due");
    const int64_t T = 200LL * TownLevels(); // recounted at the year end
    bool offer = false;
    Ledger("yearly_income") = 0;
    if (Spend(2500, tax))
    {
        due = 0;
        _townPot += tax;
        if (_townPot >= T)
        {
            _townCarry += tax >> 2;
            _townPot -= (tax >> 2) + T;
            offer = true;
        }
        else if (_townPot + _townCarry >= T)
        {
            _townCarry += _townPot - T;
            _townPot = 0;
        }
        else
        {
            _townPot = _townPot + _townCarry - T;
            _townCarry = 0;
        }
    }
    else
    {
        due = due + (due >> 2) + (tax >> 2) + tax;
        _townPot = -tax;
        _taxGrace = 3;
        _s.scalars["tax_counter"] = _taxGrace;
        Event(31, 76, "You could not pay this year's taxes. You have three months of grace.");
    }
    _s.scalars["town_pot"] = _townPot;
    if (offer && TownOffer())
        Event(kTownVoteEvent, 66, "The town has bought another plot and asks for your vote.");
    // yearly lines reset
    for (const char* line : { "rev_crops", "rev_machinery", "rev_land", "rev_futures", "rev_animals", "exp_seed", "exp_machinery", "exp_land",
                              "exp_futures", "exp_structures", "exp_livestock", "exp_chemicals", "exp_interest", "exp_leases", "exp_misc" })
        Ledger(line) = 0;
    Ledger("rain_days") = 0; // the year's rain-day count starts again
    _lastTax = tax;
    Ledger("exp_taxes") = 0; // the year's tax bucket is emptied; last year's tax is kept apart for the balance sheet
    _interest = 8 + (_rng.Next() & 7);
    EvaluationSnapshot();
    // the homestead picture follows the cash on hand: tiers below 60,000 / 200,000 / 400,000 / 550,000 /
    // 750,000 / 999,999; at or above that, or with debts, it stays as it is
    {
        static const int64_t kBounds[6] = { 60000, 200000, 400000, 550000, 750000, 999999 };
        for (int t = 0; t < 6; ++t)
            if (_s.cash >= 0 && _s.cash < kBounds[t])
            {
                HomesteadTier(t);
                break;
            }
    }
    _yearEval = true; // the year end opens the Evaluation window
    SlideTimeline();
    _s.cal.year++;
}

// the timeline slides by one year; a field in use gets fallow weeks in the new last year and then its
// plan carried on to the end (the year routine of the schedule file)
void Simulation::SlideTimeline()
{
    for (int field = 1; field < 98; ++field)
    {
        const bool used = _s.FieldAt(field).InUse();
        for (int r = 0; r < 240; ++r)
            _s.SetScheduleCode(r, field, r + 48 < 240 ? _s.ScheduleCode(r + 48, field) : used ? kFallow : 0);
        if (used)
            ExtendPlan(field);
    }
}

} // namespace osf

namespace osf {
void Simulation::ShockPrice(int crop, int sens)
{
    // the last history value gets += value >> sensitivity, capped at 4 x base
    const CropDef& d = _s.crops[crop & 15];
    int& h = _history[static_cast<size_t>(crop & 15)][29];
    h = std::min(4 * std::max(1, d.BasePrice()), h + (h >> sens));
}
} // namespace osf

namespace osf {
void Simulation::Shorelines()
{
    // the original's whole-map shoreline pass: every water-flagged cell (not a pump or a road bridge, not
    // flagged 0x4000 on the overlay) gets the shore picture of its four neighbours, or interior water
    static const int shore[16] = { 55, 51, 47, 49, 43, 57, 45, 65, 39, 53, 59, 67, 41, 61, 63, -1 };
    static const int dx[4] = { -1, 0, 1, 0 }, dy[4] = { 0, 1, 0, -1 };
    auto water = [&](int x, int y) { return x < 0 || y < 0 || x >= kMapSize || y >= kMapSize || (_s.world.At(x, y).base & 0x2000); };
    for (int x = 0; x < kMapSize; ++x)
        for (int y = 0; y < kMapSize; ++y)
        {
            MapCell& c = _s.world.At(x, y);
            const int t = c.BaseTile();
            if (!(c.base & 0x2000) || t == 120 || t == 121 || t == 172 || t == 173 || (c.overlay & 0x4000))
                continue;
            int mask = 0;
            for (int k = 0; k < 4; ++k)
                if (!water(x + dx[k], y + dy[k]))
                    mask |= 1 << k;
            const int id = (shore[mask] < 0 ? 55 : shore[mask]) + (G2Step() & 1);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | id);
        }
}
} // namespace osf

namespace osf {
void Simulation::BulldozeCell(int x, int y)
{
    // the original's per-cell bulldoze routine without the tool's charge (tornado and locusts use it): a building is
    // wrecked, a crop cell is stripped (+256 field damage), a trough or hay pile is removed, roads, ditches, fences,
    // gates, trees, valves and road bridges turn to ground; rocks cost 25 to clear. Machines and animals are not touched.
    if (x < 0 || y < 0 || x >= kMapSize || y >= kMapSize)
        return;
    MapCell& c = _s.world.At(x, y);
    if (const int st = StructureAt(x, y))
    {
        RemoveStructure(st, false);
        return;
    }
    const int id = c.BaseTile();
    if (const int fi = FieldIndexAt(x, y))
    {
        if (id >= 0x2A8 && id < 0x348)
        {
            c.base = static_cast<uint16_t>((c.base & 0xF824) | 0x24);
            Field f = _s.FieldAt(fi);
            f.SetStress(std::min(65535, f.Stress() + 0x100));
        }
        return;
    }
    if (id == 175 || id == 176)
    {
        if (const int it = ItemAt(x, y, 299))
            RemoveItem(it);
    }
    else if (c.HasObject() && (c.overlay & 0x7FF) >= 1020 && (c.overlay & 0x7FF) <= 1024)
    {
        if (const int it = ItemAt(x, y, 300))
            RemoveItem(it);
        c.base &= static_cast<uint16_t>(~0x800);
        c.overlay &= 0xF800;
        return;
    }
    const bool clearable = (id >= 150 && id <= 171) || (id >= 87 && id <= 108) || (id >= 177 && id <= 191) || (id >= 131 && id <= 146) ||
                           id == 175 || id == 176 || (id >= 122 && id <= 125) || id == 172 || id == 173 || (id >= 535 && id <= 538);
    if (!clearable)
        return;
    if (id >= 535 && id <= 538 && !Spend(500, 25))
        return;
    Puff(x, y);
    c.base = static_cast<uint16_t>((c.base & 0xF800) | GroundTile(_s, _wetness, x, y));
    ReshapeAround(_s, x, y);
}
} // namespace osf
