// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"
#include "game/Simulation.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace osf {

// A test player that runs a farm by itself through the same actions a player has (BUY, the EDIT tools, the MAP
// window's land purchase), so long games can be watched or run unattended. It changes no game rule.
// Once a game week it buys the machines the farm needs, clears and plants new fields on a grid that leaves
// lanes for the machines, replants idle fields, sprays fields with problems, lays dirt roads from the farmhouse
// to the fields, opens the fences between its own plots and buys the next plot of land when the money allows.
// The game's boxes are answered for it. A checkpoint save is written at every year change.
class AutoPlayer
{
public:
    AutoPlayer(GameState& s, Simulation& sim, std::string checkpointDir);
    void Tick();                                // after each simulation step: acts once a game day
    bool Answer(const GameEvent& ev);           // a box from the simulation: true when it was answered here
    bool TakeNote(std::string& out);            // what it did, for the status line or the console
    int Fields() const;                         // fields in use

private:
    void Note(const std::string& m);
    void Week();
    void BuyMachines();
    bool Park(int type);
    bool MakeLot();
    std::vector<std::pair<int, int>> ParkingCells() const;
    int MachinesWanted(int fields) const;
    int MachinesOwned() const;
    void OpenInnerFences();
    void Replant();
    void Spray();
    bool NewField();
    bool BuyLand();
    bool RoadTo(int fx, int fy);
    int BestCrop(int avoid) const;
    double Outlook(int crop) const;
    void OneHarvest(int field);
    std::vector<char> FieldMask() const;
    bool SpotOk(int x, int y, const std::vector<char>& fields, int& obstacles, int plotC = -1, int plotR = -1) const;
    bool FarmhouseArea(int x, int y) const;
    int64_t Reserve() const;
    bool Owned(int x, int y) const;
    void Checkpoint();

    GameState& _s;
    Simulation& _sim;
    std::string _dir;
    int _ox = 0, _oy = 0;      // the farm's first plot: the field grid is laid out from it
    int _gx = 1, _gy = 1;      // the field grid's first place
    int _lastDay = -1, _lastWeek = -1, _lastYear = -1, _lastMonth = -1;
    bool _boughtLandThisMonth = false;
    bool _freeSpot = true;      // the last look found a free place for a field on your land
    std::vector<std::pair<int, int>> _lots; // parking lots: grid places kept for machines
    std::map<int, int> _sprayed; // field -> absolute week of its last spray
    std::deque<std::string> _notes;
};

} // namespace osf
