// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/Animals.h"
#include "game/Traffic.h"
#include "game/Disasters.h"
#include "game/Flight.h"
#include "game/Irrigation.h"
#include "game/Machines.h"
#include "game/Rng.h"

#include <array>

// the game clock: the original registers its tick handler with the timer service at a 20000 us period (50 Hz; measured
// live: 50.0 ticks a second). The PC timer divisor 0x3FFF (72.8 Hz) in its code is not the game clock.
constexpr double kTimerHz = 50.0;
#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace osf {

enum class Speed { Ultra, Fast, Normal, Slow, Paused };

// An event shown in a modal box: events.dat record, alltiles.btl picture, and our own short wording for the status line.
constexpr int kTownVoteEvent = -32; // GameEvent id that asks for the Town Choice vote
constexpr int kBulldozeFieldEvent = -33; // "destroy this field?" (the bulldozer on a field)
constexpr int kGameOverEvent = -34;      // the farm is lost: start a new game? (YES / NO)
constexpr int kRodeoEvent = 61;          // events.dat 61: bet 500 on cousin Jed?
constexpr int kFairEvent = -62;          // enter an animal in the judging contest for 200? (own text)

struct GameEvent
{
    int id;
    int picture;
    std::string text;
    bool question = false;   // YES / NO box; the answer goes to Simulation::Answer
    int x = -1, y = -1;      // unused position (AutoGoto goes through Simulation::AutoGoto)
    int noFace = 11;         // the second button's face in buttons.til (11 NO, 3 CANCEL)
    int okFace = 0;          // a one-button box's face (0 OK, 17 RATS)
    bool status = true;      // also written on the status line
    bool always = false;     // shown even with Options > Messages off
    // the interface's own boxes (quit, new game, saved): handled by GameUi, not by Simulation::Answer
    std::vector<int> faces{}; // button faces in buttons.til, left to right (overrides question / okFace)
    int textId = -1;         // events.dat text shown in a box with an engine id
    int uiAction = 0;        // GameUi::UiAnswer's case
    int timeoutButton = 0;   // the button an unanswered box counts as
    int escButton = -1;      // the button Esc presses (-1: none)
};

// The running simulation (milestone 2): clock, weather, soil, daily/weekly/monthly/yearly steps, crop growth,
// schedule execution with job completion at the week deadline, market, bank and taxes.
// Machine choreography and the UI are in Machines and GameUi.
class Simulation
{
public:
    void FreeStorageSlot(int item);         // the storage slot holding an item is emptied
    explicit Simulation(GameState& state);

    void Advance(double seconds);            // real time
    void TimerTick();                        // one 50 Hz timer tick: machines, animals, and the day clock
    // the original's raw timer: 50 Hz of real time in the program, whatever the speed and also while
    // paused (not under an event box, where the engine's clock stands still); one per TimerTick in headless runs
    int64_t RawTicks() const { return _rawTicks; }
    void SetSpeed(Speed s);
    // engine option for play: the game speed scales the whole clock (animals, machines, water and days all run
    // 256 / delay times the timer rate, a day is 256 ticks) instead of only the day length as in the original
    void SetScaledClock(bool on) { _scaledClock = on; }
    Speed GetSpeed() const { return _speed; }

    // queries for the UI
    int SoilWetness() const { return _wetness; }
    int Wind() const { return _wind; }
    int G2Step(); // one draw of generator G2 (8 shifts of its register)
    int G1() { return _rng.Next(); } // one draw of the gameplay generator G1
    void Shorelines(); // re-tile every water cell's shore picture (after floods and droughts)
    void BulldozeCell(int x, int y); // free per-cell bulldoze (tornado, locusts)
    Irrigation& Water() { return *_irrigation; }
    int Temperature() const { return _s.WeatherTemp(WeatherIndex()); }   // the week's table value
    int DailyTemperature() const { return _dailyTemp; }  // today's: the week's value + 0..4, shown in the weather window
    int TodaySky() const { return _todaySky; }           // today's sky code
    int RainProfileRow() const { return WeatherIndex() - _s.cal.AbsWeek(); }
    int InterestRate() const { return _interest; }
    const std::array<int, 7>& WeekPattern() const { return _weekPattern; }   // 0 clear, 1 mostly clear, 2 cloudy, 3 rain, 4 snow
    const std::array<int, 7>& NextWeekPattern() const { return _nextPattern; }
    const std::array<std::array<int, 30>, 16>& PriceHistory() const { return _history; }
    // the market of the crop files not loaded in a slot (an engine addition, user finding Q27b): each has a price and a
    // 30-week history, made at load from its file's base price and moved every week by the loaded crops' rule with
    // its own random numbers (so the loaded crops' prices go on exactly as the original's); not saved
    struct SpareCropMarket { std::string file, name; int basePrice = 0, yield = 0; std::array<int, 30> history{}; std::vector<uint8_t> icon; };
    const std::vector<SpareCropMarket>& SpareMarket();
    void ShockPrice(int crop, int sensitivity); // disaster shock on the last history week
    int64_t LandValue() const;
    int64_t PlotLandValue(int col, int row, bool asFree = false) const; // one plot; asFree: counted as not owned
    // what selling an owned plot pays: its value counted as a free plot, so it equals the buying price (an engine
    // change, user finding F21: the original pays 100 more than it charged, as the sold plot still counts as owned)
    int64_t PlotSalePrice(int col, int row) const { return PlotLandValue(col, row, true); }
    int WealthTier() const;                          // 0..7 by net worth
    int FieldProblems(Field& f) const;               // problem bits: 1 pests, 2 wet, 4 toxic, 8 dry, 16 weeds, 32 disease, 128 low nutrient
    struct EvalBars { int farm, town, productivity, soil, environment; bool soilOk, environmentOk; };
    EvalBars Evaluation();                           // the five 0..80 gauges of the Evaluation window
    std::vector<int> SuggestedCrops() const;         // crop slots that suit the next 16 weeks
    int64_t EstimatedTax() const;
    int64_t CreditLimit() const;
    bool TakeMessage(std::string& out);      // event / status messages for the UI
    bool TakeSound(std::string& out);
    bool TakeSound(std::string& out, int& cellX, int& cellY); // with the map cell it comes from (-1: none)
    bool TakeEvent(GameEvent& out);
    bool TakeYearEvaluation() { const bool y = _yearEval; _yearEval = false; return y; } // the year-end Evaluation window
    void Puff(int x, int y) { if (_puffs.size() < 64) _puffs.push_back({ x, y }); }   // dust / explosion
    bool TakePuff(int& x, int& y);
    static int GroundTileFor(int wetness, const MapCell& c);
    void Event(int id, int picture, const std::string& text, int x = -1, int y = -1);        // sound effects requested by the simulation

    // money
    bool Spend(int category, int64_t amount, const char* label = nullptr); // label: the money log's text
    void Credit(int category, int64_t amount, const char* label = nullptr);
    // the engine's money log behind the Funds box (an engine addition): every change of the cash with what it was
    // for, newest last; changes made outside Spend / Credit are booked as "Other" by ReconcileCash
    struct CashEntry { int day, week, month, year; int64_t amount; std::string what; int count; };
    const std::deque<CashEntry>& CashLog() const { return _cashLog; }
    void PostCash(int64_t amount, const std::string& what);
    void ReconcileCash();

    // bank
    bool Borrow(int64_t amount);
    bool RepayAll();
    bool Repay(int64_t amount); // engine extension: part of the debt (the original's REPAY always clears it all)
    int64_t LoanBalance() const { return Scalar("loan_balance"); }
    int64_t LoanPayment() const { return Scalar("loan_payment"); }
    int64_t Scalar(const char* name) const { auto it = _s.scalars.find(name); return it == _s.scalars.end() ? 0 : it->second; }

    // field jobs started by the player
    bool StartPlanting(Field& f, int crop, std::string& why);
    bool StartSpray(Field& f, int job, std::string& why, bool checkIdle = true);   // job 6 fertilize, 7 pesticide, 8 herbicide, 9 fungicide
    int FieldIndexAt(int cx, int cy);                       // 0 = no field
    int64_t HarvestValue(Field& f, int* maturity = nullptr, int* quality = nullptr) const; // value if harvested now
    bool LockContract(Field& f);
    bool StartHarvest(Field& f);                            // growing field -> harvest job // price contract on a growing field
    // the field's timeline: row = week of the current year (0..47), later years follow
    void StampPlant(int field, int crop, int row);
    void SlideTimeline();       // the year change's slide of every field's timeline by 48 weeks
    bool ExtendPlan(int field); // the year routine's plan extension to the end of the timeline
    bool StampOperation(int field, int row, int code);     // a marker into a free week
    void CutPlan(int field, int row);
    std::mt19937& Random() { return _uiRandom; }

    void FinishJob(Field& f);
    void Notify(const std::string& m) { Message(m); }                               // a job script finished: effects, machines go home
    Machines& Fleet() { return *_machines; }
    Animals& Herds() { return *_animals; }
    Traffic& Cars() { return *_traffic; }
    int TimerTicks() const { return _ambient; } // timer ticks run so far (they stand still while paused)
    Disasters& Hazards() { return *_disasters; }
    Flight& Plane() { return *_flight; }
    void PlaySound(const char* name, int cellX = -1, int cellY = -1) { Sound(name, cellX, cellY); }
    void RaiseGust(int windAtLeast) { if (_wind < 30) { _gust += windAtLeast - _wind; _wind = windAtLeast; } }
    void DeleteField(int index);
    bool ReplaceCrop(int slot, const std::string& fileName, std::string& why); // Load Crop
    bool CropSlotInUse(int slot) const; // a field has the crop, or a seed bag / stored harvest of it exists
    int FreeCropSlot() const;           // the last slot whose crop is not in use, -1 if none (the engine's PLANT list)
    // a disaster's warning box: no status-line text; Messages off hides it unless `always`
    void DisasterEvent(int id, int picture, const std::string& text, bool always = false);
    // Options > AutoGoto: the view puts this cell at its centre (field clear, tornado, locusts)
    void AutoGoto(int x, int y);
    bool TakeGoto(int& x, int& y);
    void GenerateHistory(int crop);
    void SetGameDir(const std::string& d) { _gameDir = d; }
    void SetTownLayout(const std::vector<uint8_t>& cmp);   // townly0.cmp: 40x24 tile ids = 15 town plot templates
    bool TownBuildPlot(int type);   // a plot without a vote (new world, tests)
    bool TownVote(int button);
    void TownVoteTimeout();
    bool TownOffer();                // the town buys its next plot (year end; tests)          // the dialog left unanswered: a random plot type 1-14       // Town Choice: 0 residential, 1 commercial, 2 airport, 3 recreational
    void AskBulldozeField(int index); // the bulldozer on a field asks first; YES deletes it for 25
    void StatusRefresh(Field& f);    // the original's field status refresh (N byte, markers)
    void HarvestPay(Field& f, bool mayStore); // value, contract settlement, store or sell
    void HarvestEnd(Field& f);
    void ClearStubble(Field& f); // the hand harvest's scattered pictures off the field                // the field after the harvest (cleared, or perennial reset)
    int HarvestStorage(const Field& f) const; // the silo the truck drives to, 0 = sold at once
    void RollPressures(Field& f);
    int TownLevels() const;
    int64_t LastTax() const { return _lastTax; }
    bool BuyPlot(int col, int row, std::string& why);    // land
    bool SellPlot(int col, int row, std::string& why, bool income = true); // income: the price also counts as yearly income
    void Answer(int eventId, bool yes);
    int EventKind() { return static_cast<int>(_s.scalars["ev_kind"]); } // 0 none, 1 rodeo, 2 fair: the ground's tile bank
    void SetMessagesOn(bool on) { _messagesOn = on; } // Options > Messages: without it there are no rodeo / fair
    const std::string& Song() const { return _song; }   // music chosen by the game // rodeo bet / fair entry // 1 residential, 2 commercial, 4 recreational, 5 airport
    void RemoveAnimal(int index);
    bool Bankrupt() const { return _bankrupt; }
    int GameOverChoice() const { return _gameOverChoice; } // 0 not answered, 1 leave the program, 2 new game
    bool Liquidate(int64_t debt, bool tax);      // the auction; true when the debt was paid
    // field moisture and ditch counts (and the status refresh), see Simulation.cpp
    void RefreshFields(bool withStatus);
    // loading a game during play keeps what the original keeps in memory across a load (not in the save): the
    // random generators, today's weather, soil wetness, interest rate, town reserve and the price histories
    void InheritSession(const Simulation& old);
    // 12-byte item records: 299 water trough, 300 hay pile, 0xC0+crop harvested stack
    int ItemAt(int x, int y, int type) const;
    int StructureAt(int x, int y) const;          // building covering a cell, or 0
    void DrawBuilding(int index);                 // its map cells: outside pictures, or the inside view from its slots
    int ToggleInside(int x, int y);               // right click: show a building's inside or its outside again (0 = none)
    int64_t RemoveStructure(int index, bool refund = true);           // bulldozed: sold at its price, ground restored
    void RefillItem(int index, int amount); // sets an item record's amount
    int NewItem(int type, int amount, int x, int y);
    void RemoveItem(int index);
    void TakeItem(int index, bool freeRecord = true);  // used up: out of its building slot or off the ground, then removed
    void ClearAnimalCell(int x, int y);
    bool Consume(int x, int y, int type, int amount);
    void AmbientAnimation();
    void PaintMarkers(Field& f, const int (&slots)[3]);
    void AnimateStalls();
    void ContestWeek();      // the weekly countdown of the rodeo / fair and its result
    void Conception();       // monthly
    void ContestOffers();    // the offers at the month change into July / October
    bool EventGround(int& x, int& y) const; // the first event ground's top-left cell

    // one simulated day (also usable by tests and "fast forward")
    void DayTick();

private:
    int WeatherIndex(int weekOffset = 0) const;
    void WeekTick();
    void WeeklyWetness();
    void MonthTick();
    void YearTick();
    void ComputePattern(int weatherIndex, std::array<int, 7>& out);
    int DrySky(int temperature);
    uint16_t _g2 = 0x1234;   // shift register of generator G2 (dry-day skies)
    void DailyFields(bool alternateDay);
    bool TownPickPlot(int& c, int& r) const;
    bool PlaceTownPlot(int c, int r, int type);
    bool TownChangeLevel(int c, int r, int delta);
    bool TownClearPlot(int c, int r);
    void TownGrowthWeek();
    void DailyLivestock();
    static size_t SiloSlot(int k);          // byte offset of the content word of silo slot k (0..3)
    void FillWaterTowers();                 // daily: a full ditch beside a tower adds water
    int DrawWater(int limit);               // empties towers into a trough; the water taken (<= limit)
    void WeeklyGrowth(Field& f);
    int GrowthRoutine(Field& f, const CropDef& c);
    void Adjust(Field& f, int type, int amount);
    void CompleteJob(Field& f);              // the week deadline
    void ExecuteSchedule();
    void Harvest(Field& f);
    bool StoreHarvest(Field& f, int crop, int quality);
    void ClearField(Field& f);
    int _gotoX = -1, _gotoY = -1;
    void PaintField(Field& f, int tile, bool onlyCropTiles);
    void MarketWeek();
    void Message(const std::string& m);
    void Sound(const char* name, int cellX = -1, int cellY = -1);
    int64_t& Ledger(const char* name) { return _s.scalars[name]; }
    bool OwnsMachine(int type) const;
    int _profileBaseYear = 1993;
    static constexpr int kToxicThreshold = 128; // a constant byte in the original's data; toxic above it
    void EvaluationSnapshot();
    int _evOwned = 0, _evOwnedPrev = 0, _evOwnedDelta = 0, _evTown = 0, _evTownPrev = 0, _evTownDelta = 0, _evTierDelta = 0;

    GameState& _s;
    Rng _rng;
    Rng3 _rng3;
    int _dailyTemp = -1000, _todaySky = 0;
    Speed _speed = Speed::Normal;
    bool _scaledClock = false;
    int64_t _rawTicks = 0;
    double _rawFrac = 0;
    bool _inAdvance = false;
    double _ticks = 0;
    int _wetness = 6, _acc = 0, _wind = 0, _gust = 0;
    bool _markerTurn = false; // the daily pass of days 3 and 7 may change the shown infestation marker (F34)
    int _dayCounter = 0;
    int _interest = 15;
    int _trend = 0;
    int64_t _townPot = 0, _townCarry = 0;
    int64_t _lastTax = 0; // last year's tax (not saved by the original either)
    int _pendingBulldoze = 0;
    int _loanGrace = 0, _taxGrace = 0;
    std::array<int, 7> _weekPattern{}, _nextPattern{};
    std::array<std::array<int, 30>, 16> _history{};
    std::vector<SpareCropMarket> _spare;
    std::string _spareLoaded;  // the slots' file names when _spare was built
    uint32_t _spareRng = 0x2F6B; // the spare market's own random numbers
    int SpareRandom();
    void SpareMarketWeek();
    std::deque<std::string> _messages, _sounds;
    std::deque<std::pair<int, int>> _soundAt; // the map cell of each queued sound (modern sound places it)
    std::deque<CashEntry> _cashLog;
    int64_t _cashSeen = INT64_MIN;
    std::deque<GameEvent> _events;
    std::deque<std::array<int, 2>> _puffs;
    std::string _gameDir;
    std::vector<uint16_t> _townLayout;
    std::string _song = "genfarm.xmi";
    std::unique_ptr<Machines> _machines;
    std::unique_ptr<Animals> _animals;
    std::unique_ptr<Traffic> _traffic;
    std::unique_ptr<Irrigation> _irrigation;
    std::unique_ptr<Disasters> _disasters;
    std::unique_ptr<Flight> _flight;
    int _dayTicks = 0, _ambient = 0;
    bool _bankrupt = false;
    bool _gameOverTax = false;  // the lost auction was the tax board's (else the bank's)
    int _gameOverChoice = 0;
    bool _messagesOn = true;
    bool _yearEval = false;
    int _homeTier = 0;               // the homestead picture's tier (not saved; the original held 0 after a program start
                                     // and a Load, seen live 2026-11-02, so a loaded rich farm cheers at its first year end)
    void HomesteadTier(int tier);
    int _eventPass = 0;
    std::vector<std::array<int, 2>> _stalls;   // the rodeo's arena cells
    std::mt19937 _uiRandom{ 1993 };  // cosmetic choices (tree variants); not the game generator
};

} // namespace osf
