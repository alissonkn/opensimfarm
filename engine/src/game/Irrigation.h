// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <array>
#include <cstdint>
#include <vector>

namespace osf {

class Simulation;

// Water in the ditches, pass by pass as the original runs it. Passes alternate between
// a fill pass (parity 1: every full ditch is first emptied, then the flood from the sources fills the connected
// ditches and marks them) and an emptying pass (parity 0: the fields are refreshed first, then the flood empties the
// connected ditches and clears the marks). A pass is continued every `period` timer ticks with four queue steps; it
// ends when its queue is empty (power-0 entries are re-queued, so a weak junction can stall the water for good, as in
// the original). The fields recount their ditches at the start of an emptying pass and at the end of a fill pass.
class Irrigation
{
public:
    Irrigation(GameState& s, Simulation& sim);
    void Tick();
    static bool IsFullDitch(int id) { return id >= 98 && id <= 108; }
    static bool IsEmptyDitch(int id) { return id >= 87 && id <= 97; }
    static bool IsValve(int id) { return id >= 122 && id <= 125; }
    static bool ValveOpen(int id) { return id == 122 || id == 124; }
    // source list (kept in the save): a pump registers power 64, a windmill 48
    void AddSource(int x, int y, int kind);
    void RemoveSource(int x, int y);

private:
    struct Entry { uint8_t x, y, p; };
    void Pass();
    bool Steps(int count);
    void Push(int x, int y, int p);
    bool Pop(Entry& e);
    void Flip(int x, int y);
    bool Touched(int x, int y) const;
    int Mask(int x, int y, int& mask) const;
    bool SourceBesideWater(int x, int y) const;
    int Sources() const;

    GameState& _s;
    Simulation& _sim;
    std::array<Entry, 128> _queue{};
    int _head = 0, _tail = 0;
    int _parity = 0;
    bool _running = false;
    uint16_t _timer = 0, _last = 0;
};

} // namespace osf
