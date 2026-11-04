// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Traffic.h"

#include "game/Simulation.h"

#include <algorithm>

namespace osf {

namespace {
// headings 0 / 2 / 4 / 6 = north, east, south, west (the even entries of the eight-step table)
constexpr int kDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr int kDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
constexpr int kBit[4] = { 1, 2, 4, 8 };  // neighbour bit of each heading
constexpr int kBack[4] = { 4, 8, 1, 2 }; // the bit of the way back
size_t Rec(int i) { return static_cast<size_t>(i) * 8; }
bool Paved(int t) { return t >= 150 && t < 161; }
bool Dirt(int t) { return t >= 161 && t < 172; }
bool Bridge(int t) { return t >= 172 && t < 174; }
int Bits(int m) { return ((m >> 0) & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1); }
} // namespace

void Traffic::Add(int x, int y, int heading)
{
    if (_s.carCount > 15)
        return;
    for (int i = 0; i < 16; ++i)
        if (!(Rd8(_s.traffic, Rec(i)) & 1))
        {
            Wr8(_s.traffic, Rec(i), Rd8(_s.traffic, Rec(i)) | 3);
            Wr16(_s.traffic, Rec(i) + 2, x);
            Wr16(_s.traffic, Rec(i) + 4, y);
            Wr8(_s.traffic, Rec(i) + 6, heading);
            Wr8(_s.traffic, Rec(i) + 7, _sim.G2Step() & 3); // G2
            _s.carCount++;
            return;
        }
}

void Traffic::Remove(int i)
{
    if (Rd8(_s.traffic, Rec(i)) & 1)
    {
        Wr8(_s.traffic, Rec(i), Rd8(_s.traffic, Rec(i)) & 0xFE);
        _s.carCount = std::max(0, _s.carCount - 1);
    }
}

// the four neighbours that are road (paved, dirt or a bridge): bit 1 north, 2 east, 4 south, 8 west
int Traffic::Mask(int x, int y) const
{
    auto road = [&](int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= kMapSize || cy >= kMapSize)
            return false;
        const int t = _s.world.At(cx, cy).BaseTile();
        return Paved(t) || Dirt(t) || Bridge(t);
    };
    return (road(x, y - 1) ? 1 : 0) | (road(x + 1, y) ? 2 : 0) | (road(x, y + 1) ? 4 : 0) | (road(x - 1, y) ? 8 : 0);
}

// the next cell: with more than one road neighbour the way back is left out; half the time a random
// road direction is taken instead of the heading; a cell that is not drivable turns the car right, at most four
// times and never back to where it started turning
bool Traffic::Choose(int heading, int x, int y, int& nx, int& ny, int& nh)
{
    int h = heading, tries = 0;
    for (;;)
    {
        int m = Mask(x, y);
        if (Bits(m) > 1)
            m &= ~kBack[(heading >> 1) & 3];
        if ((_sim.G2Step() & 1) && Bits(m) >= 1) // G2
        {
            int d = _sim.G2Step() & 3;
            while (!(kBit[d] & m))
                d = (d + 1) & 3;
            heading = h = d * 2;
        }
        nx = std::clamp(x + kDx[h], 0, kMapSize - 1);
        ny = std::clamp(y + kDy[h], 0, kMapSize - 1);
        const MapCell& c = _s.world.At(nx, ny);
        const int t = c.BaseTile();
        if (Paved(t) || Bridge(t) || (Dirt(t) && !(c.overlay & 0x0800)))
        {
            nh = h;
            return true;
        }
        h = (h + 2) & 7;
        ++tries;
        if (heading == h || tries > 4)
            return false;
    }
}

// one step: a shown car leaves its cell first; it is drawn on the next cell unless something is
// there (it drives on unseen); an animal it runs into kicks
void Traffic::Step(int i)
{
    const size_t b = Rec(i);
    int fl = Rd8(_s.traffic, b);
    const int x = Rd16(_s.traffic, b + 2), y = Rd16(_s.traffic, b + 4), kind = Rd8(_s.traffic, b + 7);
    if (x < 0 || y < 0 || x >= kMapSize || y >= kMapSize)
    {
        Remove(i);
        return;
    }
    if (fl & 2)
    {
        MapCell& c = _s.world.At(x, y);
        c.base &= static_cast<uint16_t>(~0x0800);
        c.overlay &= 0xF800;
    }
    int nx, ny, nh;
    if (!Choose(Rd8(_s.traffic, b + 6), x, y, nx, ny, nh))
    {
        Remove(i);
        return;
    }
    MapCell& n = _s.world.At(nx, ny);
    if (n.HasObject())
    {
        if (fl & 2)
            _sim.Herds().KickAt(nx, ny);
        fl &= ~2;
    }
    else
    {
        fl |= 2;
        n.base |= 0x0800;
        n.overlay = static_cast<uint16_t>((n.overlay & 0xF800) | ((0xE5 + kind) * 4 + nh / 2));
    }
    Wr16(_s.traffic, b + 2, nx);
    Wr16(_s.traffic, b + 4, ny);
    Wr8(_s.traffic, b + 6, nh);
    Wr8(_s.traffic, b, fl);
}

void Traffic::Tick(int period)
{
    if (++_sinceStep < period)
        return;
    _sinceStep = 0;
    for (int i = 0; i < 16; ++i)
        if (Rd8(_s.traffic, Rec(i)) & 1)
            Step(i);
}

} // namespace osf
