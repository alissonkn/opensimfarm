// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <random>

namespace osf {

class Simulation;

// Cars driving along the roads: one starts every
// week at the town plot's corner; each takes a step every 1 / 8 / 16 / 32 ticks (the animal period), keeps to
// paved roads, the bridges 172-173 and free dirt roads, and leaves when it is stuck. A car bumping an animal makes it kick.
// Table (saved): 16 records of 8 bytes: +0 flags (1 in use, 2 shown on the map), +2 x, +4 y (words), +6 heading
// (0 / 2 / 4 / 6), +7 kind (0-3, the picture set); the count byte is kept beside it.
class Traffic
{
public:
    Traffic(GameState& s, Simulation& sim) : _s(s), _sim(sim) {}
    void Tick(int period);
    void Add(int x, int y, int heading); // ignored when all 16 are out

private:
    void Step(int index);
    void Remove(int index);
    bool Choose(int heading, int x, int y, int& nx, int& ny, int& nh);
    int Mask(int x, int y) const;

    GameState& _s;
    Simulation& _sim;
    int _sinceStep = 0;
};

} // namespace osf
