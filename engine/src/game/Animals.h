// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <array>


namespace osf {

class Simulation;

// Animals walking on the map: herd centres every 320 ticks, one step for every
// animal outside a barn every 1 / 8 / 16 / 32 ticks (Ultra / Fast / Normal / Slow) (animals in a barn walk out now and then), towards food, water or the herd; fences may break; loose animals trample crops.
// Record: +0 x, +1 y, +2 flags (0x20 in use, 2 female, 1 pregnant, 4 sheltered), +4 heading 0..7, +5 random steps left,
// +6 species (0xA0 horse, 0xA1 cow, 0xA2 pig, 0xA3 sheep), +0x0D hay need, +0x0E water need, +0x12 adult (sprite),
// +0x1E tick of the last kick (flag 8 while kicking).
class Animals
{
public:
    Animals(GameState& s, Simulation& sim) : _s(s), _sim(sim) {}
    void Tick(int period);                         // one timer tick; a step pass every `period` ticks
    static int TileFor(int species, int heading, int adult) { return 946 + 16 * (species & 3) + 2 * (heading & 7) + (adult & 1); }
    int CentreX(int species) const { return _cx[static_cast<size_t>(species & 3)]; } // the herd centre (tests)
    int CentreY(int species) const { return _cy[static_cast<size_t>(species & 3)]; }
    void SetPointer(int x, int y) { _pointerX = x; _pointerY = y; } // map cell under the mouse (-1: none); animals shy from it
    bool Place(int x, int y, int species);         // a bought animal
    // the Move tool on animals (an engine addition, user finding Q20; the original's Move tool leaves animals alone)
    int AnimalAt(int x, int y) const;              // an animal standing on the cell (not in a barn), else 0
    bool MoveTo(int index, int x, int y);          // to a free cell an animal may stand on
    void Unshelter(int index);                     // out of its barn slot (flag 4 cleared), for a removed animal
    void KickAt(int x, int y, bool machine = false); // the animal on this cell kicks (bumped by a car or a machine)
    static constexpr int kKickPicture = 0x40E;     // horse, cow, pig, sheep kicking: 0x40E-0x411

private:
    void HerdCentres();
    void Step(int index);
    void Needs(int index);
    bool EnterBarn(int index, int structure);
    void LeaveBarn(int index);
    void Weaken(int index);                        // condition -1
    bool FeedAt(int index, bool hay);
    bool Passable(int x, int y) const;
    void Lift(int x, int y);
    void Stamp(int index);
    bool NearestItem(int x, int y, int type, int& tx, int& ty, int& best) const;
    static int HeadingTo(int x, int y, int tx, int ty);

    GameState& _s;
    Simulation& _sim;
    int _tick = 0, _herdStamp = -1000, _sinceStep = 0, _lastHungry = -100000, _lastThirsty = -100000;
    std::array<int, 4> _cx{}, _cy{};         // the centre each species walks back to (see HerdCentres)
    std::array<int16_t, 4> _slotX{}, _slotY{}; // the four centre slots as the original fills them
    int _pointerX = -1, _pointerY = -1;
};

} // namespace osf
