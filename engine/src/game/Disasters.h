// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <random>

namespace osf {

class Simulation;

// Disasters. Ids: 1 tornado, 2 drought, 3 flood, 4 locusts, 5 frost, 6 windstorm.
class Disasters
{
public:
    enum Id { kTornado = 1, kDrought = 2, kFlood = 3, kLocusts = 4, kFrost = 5, kWindstorm = 6 };
    Disasters(GameState& s, Simulation& sim) : _s(s), _sim(sim) {}

    bool Start(int id);                 // false when disabled or already active
    bool Active(int id) const { return (_active >> id) & 1; }
    void SetDisabled(bool off) { _disabled = off; }
    bool Disabled() const { return _disabled; }

    void Tick();                        // timer: tornado / locust movement, flood sweep
    void Day(int wind, int temperature, int& wetness); // daily effects and wind triggers
    void Week();                        // July dice, flood counter
    bool Flooding() const { return Active(kFlood); }
    bool Frost() const { return Active(kFrost); }

    // positions for drawing (cells)
    int TornadoX() const { return _tx; }
    int TornadoY() const { return _ty; }
    int SwarmX() const { return _lx; }
    int SwarmY() const { return _ly; }
    int Frame() const { return _frame; }

private:
    void Stop(int id);
    void Shock(int sensitivityParam);
    void FloodCell(int x, int y);
    int Rand(int n) { return static_cast<int>(_rng() % static_cast<unsigned>(n)); }
    int Axis() { const int v = static_cast<int>(_rng() & 1); return (_rng() & 1) ? -v : v; } // 0, or +-1

    GameState& _s;
    Simulation& _sim;
    std::mt19937 _rng{ 0x5F4D };
    int _active = 0;
    bool _disabled = false;
    int _tx = 0, _ty = 0, _tdx = 0, _tdy = 0;
    int64_t _tStamp = 0; // raw timer at the tornado's start
    int _lx = 0, _ly = 0, _lx0 = 0, _ly0 = 0, _lCount = 0, _tCount = 0;
    int64_t _lStamp = 0, _lMoveStamp = 0, _tMoveStamp = 0;
    int _floodDays = 0, _floodPos = 0, _frostDays = 0;
    int _tick = 0, _frame = 0;
};

} // namespace osf
