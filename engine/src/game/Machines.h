// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <array>
#include <map>

namespace osf {

class Simulation;

// One 56-byte machine record (byte positions counted from the record's catalogue word, checked against the
// prebuilt farms).
class Machine
{
public:
    Machine(std::vector<uint8_t>& t, int i) : index(i), _t(t), _b(static_cast<size_t>(i) * 56) {}
    int Type() const { return Rd16(_t, _b); }
    int Flags() const { return Rd8(_t, _b + 2); }
    void SetFlags(int v) { Wr8(_t, _b + 2, v); }
    int X() const { return Rd8(_t, _b + 5); }
    int Y() const { return Rd8(_t, _b + 6); }
    void SetPos(int x, int y) { Wr8(_t, _b + 5, x); Wr8(_t, _b + 6, y); }
    int TargetX() const { return Rd8(_t, _b + 7); }
    int TargetY() const { return Rd8(_t, _b + 8); }
    void SetTarget(int x, int y) { Wr8(_t, _b + 7, x); Wr8(_t, _b + 8, y); }
    int Hist(int k) const { return Rd8(_t, _b + 9 + k); }      // 3 cells: (x1,y1) (x2,y2) (x3,y3)
    void SetHist(int k, int v) { Wr8(_t, _b + 9 + k, v); }
    int Heading() const { return Rd8(_t, _b + 0x0F); }          // 0 north, 2 east, 4 south, 6 west
    void SetHeading(int h) { Wr8(_t, _b + 0x0F, h); }
    int Wear() const { return Rd8(_t, _b + 0x11); }
    void AddWear(int w) { Wr8(_t, _b + 0x11, std::min(255, Wear() + w)); }
    int Usage() const { return Rd16(_t, _b + 0x12); }            // weeks since the last usage wear (0..4)
    void SetUsage(int v) { Wr16(_t, _b + 0x12, v); }
    int Partner() const { return Rd16(_t, _b + 0x2E); }
    void SetPartner(int m) { Wr16(_t, _b + 0x2E, m); }
    int HomeX() const { return Rd8(_t, _b + 0x32); }
    int HomeY() const { return Rd8(_t, _b + 0x33); }
    int Stuck() const { return Rd8(_t, _b + 0x37); }
    void SetStuck(int v) { Wr8(_t, _b + 0x37, v); }
    bool InUse() const { return (Flags() & 0x20) != 0; }
    bool Placed() const { return (Flags() & 0x80) != 0; }
    bool Leased() const { return (Flags() & 0x40) != 0; }
    const int index;

private:
    std::vector<uint8_t>& _t;
    size_t _b;
};

// Machines driving to the fields and working them.
// Steps are driven by the timer: one step every second tick at the running speeds, every tick at Ultra.
class Machines
{
public:
    Machines(GameState& s, Simulation& sim);
    void Tick(bool ultra);                 // one timer tick (50 Hz)
    void ReleaseField(int field);          // the week deadline ends a job: everything goes home
    void Week();
    void WorkWear(int index, int amount);  // wear from a finished job
    int Dispatch(int type, int x, int y);  // nearest idle machine of a type, or a leased one; 0 = none
    int Create(int type, int x, int y, bool leased = false); // a new machine parked at (x, y); 0 = table full
    void Remove(int index);                                   // sold or destroyed
    bool FootprintFree(int type, int x, int y) const;         // facing north
    int MachineAt(int x, int y) const;                        // in-use machine whose footprint covers a cell
    // the Move tool's drop: out of its building if it was stored there, then it drives to (x, y)
    void PlayerMove(int index, int x, int y);
    static int TileFor(int type, int heading, bool loaded = false);
    static int Cells(int type);            // footprint length along the heading (1 or 2)
    // saves written by earlier engine builds could hold a two-cell machine split over unrelated building slots
    // (stored turned but written upright): such buildings get their machines stored again. Returns those buildings
    std::vector<int> RepairStorage();

private:
    void StepField(int field);
    bool Drive(Machine& m);                // one steering step; false when arrived
    void Lift(Machine& m);
    void Stamp(Machine& m);
    void StampTowed(Machine& lead);
    int Cost(const Machine& m, int x, int y) const;
    bool Arrived(const Machine& m) const { return m.X() == m.TargetX() && m.Y() == m.TargetY(); }
    void SendHome(int index);
    void StepBarrels();
    bool SweepStep(Field& f, Machine& lead, int paintTile);
    int Lease(int type);
    int BuildingAt(int x, int y) const;    // shed or barn covering a cell
    bool Store(int index);                 // park a machine in the building at its cell
    void Unstore(int index);               // take it out

    GameState& _s;
    Simulation& _sim;
    int _tick = 0, _lastStuckNote = -100000, _lastPickerNote = -100000, _lastMachineNote = -100000;
    int _offRoad = 0;
    // the engine's route planner (user finding Q10; off = the original's greedy steering): a shortest route over
    // roads (cheap) and bare ground to the destination, planned when a machine sets out and again when it is
    // blocked; not saved (planned again after loading)
    struct Route
    {
        int tx = -1, ty = -1;
        std::vector<int> cells; // x * map size + y, from the first step to the destination
        size_t next = 0;
        int steps = 0;
        int waited = 0; // ticks spent waiting for another machine on the next cells
    };
    std::map<int, Route> _routes;
    bool PlanRoute(const Machine& m, Route& r) const;
    bool MachineInWay(const Machine& m, int x, int y, int heading) const; // another machine's picture on those cells
    int RouteStep(Machine& m);  // heading of the next step on the planned route, -1 = none (steer as the original)
public:
    static bool smartDriving;   // set from the settings by the program; tests and headless runs keep the original's
private:
    std::map<int, int> _truck;            // field -> truck machine during a harvest
    std::map<int, int> _sweepDir;         // field -> +1 / -1
    std::map<int, int> _barrelMachine;    // barrel -> tractor carrying it
};

} // namespace osf
