// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <deque>
#include <string>

namespace osf {

class Simulation;

// The crop duster. Plane record: +0x11 wear, +0x14 fuel (0-255), +0x20 load (0-255),
// +0x22 chemical item (0xDD pesticide, 0xDE herbicide, 0xDF fungicide), +0x32/+0x33 hangar cell.
class Flight
{
public:
    Flight(GameState& s, Simulation& sim) : _s(s), _sim(sim) {}

    // buying and the AIRPLANE window
    std::string Buy();                                  // empty = bought, else the reason
    static int Fuel(const GameState& s, int m) { return Rd16(s.machines, static_cast<size_t>(m) * 56 + 0x14); }
    static int Load(const GameState& s, int m) { return Rd16(s.machines, static_cast<size_t>(m) * 56 + 0x20); }
    static int Chemical(const GameState& s, int m) { return Rd16(s.machines, static_cast<size_t>(m) * 56 + 0x22); }
    static int Wear(const GameState& s, int m) { return Rd8(s.machines, static_cast<size_t>(m) * 56 + 0x11); }
    bool Refuel(int m);                                 // 1 per unit
    bool Reload(int m);                                 // 20 per unit
    bool Repair(int m);                                 // 10 x wear
    void SetChemical(int m, int item);
    int PlaneAt(int x, int y) const;                    // a parked plane on this cell, or 0

    // flying: the game clock stops while the plane is in the air
    bool Start(int m);
    bool Flying() const { return _plane != 0; }
    bool KeysWaiting() const { return !_keys.empty(); } // keys not read by a pass yet
    int PlaneIndex() const { return _plane; }
    void Key(int key);                                  // 'L' 'R' turn, 'S' spray toggle, '+' '-' spray on/off, 'U' 'D' altitude, 'E' end
    void Tick();                                        // one timer tick
    // the plane's cell and the centre of the original's view, which stays on the plane except at the map edges
    float X() const { return kHalfW + _sx + _xo + 0.5f; }
    float Y() const { return kHalfH + _sy + _yo + 0.5f; }
    float ViewX() const { return _sx + kWinW / 32.0f; }
    float ViewY() const { return _sy + kWinH / 32.0f; }
    int Heading() const { return _heading; }            // 0 N, 1 NE, 2 E ... 7 NW
    int Altitude() const { return _alt; }
    bool Spraying() const { return _spray; }

private:
    void End();
    void Park(int m, bool stamp);
    void Pass();
    void North(int bounce);
    void South(int bounce);
    void East();
    void West(int limit);

    // the original's farm window: 544 x 368 pixels, i.e. 34 x 23 cells; the flight works in its scroll origin
    // (_sx, _sy) and the plane's offset from the window centre (_xo, _yo), both in cells
    static constexpr int kWinW = 544, kWinH = 368;
    static constexpr int kHalfW = kWinW >> 5, kHalfH = kWinH >> 5;
    static constexpr int kXMax = kHalfW - 3, kYMax = kHalfH - 3;
    // the scroll limits from the map size (96: 0x60 and 0x5F; a larger world, an engine addition, moves them)
    int SxMax() const { return _s.world.size - (kWinW >> 4); }
    int SyMax() const { return _s.world.size - 1 - (kWinH >> 4); }
    // one pass every 2 timer ticks, the code's minimum wait (measured on the running original: 25 cells a second,
    // with the 50 Hz clock)
    static constexpr int kPassTicks = 2;

    GameState& _s;
    Simulation& _sim;
    int _plane = 0;
    int _sx = 0, _sy = 0, _xo = 0, _yo = 0;
    int _heading = 6, _alt = 10;
    bool _spray = false, _ending = false;
    int _now = 0, _passStamp = 0, _wearStamp = 0, _fuelStamp = 0, _sprayStamp = 0, _passes = 0;
    std::deque<int> _keys;                              // read one per pass
};

} // namespace osf
