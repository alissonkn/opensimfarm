// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Flight.h"

#include "game/Machines.h"
#include "game/Simulation.h"

#include <algorithm>
#include <cmath>

namespace osf {

namespace {
constexpr int kPlaneType = 8;
// parked plane: 71 72 on the hangar row, 111 112 below
constexpr int kParked[4] = { 71, 72, 111, 112 };
size_t B(int m) { return static_cast<size_t>(m) * 56; }
} // namespace

int Flight::PlaneAt(int x, int y) const
{
    for (int i = 1; i < 127; ++i)
    {
        Machine m(_s.machines, i);
        if (m.InUse() && m.Type() == kPlaneType && x >= m.HomeX() && x <= m.HomeX() + 1 && y >= m.HomeY() && y <= m.HomeY() + 1 && i != _plane)
            return i;
    }
    return 0;
}

void Flight::Park(int m, bool stamp)
{
    Machine p(_s.machines, m);
    for (int k = 0; k < 4; ++k)
    {
        MapCell& c = _s.world.At(std::min(kMapSize - 1, p.HomeX() + k % 2), std::min(kMapSize - 1, p.HomeY() + k / 2));
        // the hangar tiles under the parked plane are kept in spare bytes of the plane record
        if (stamp)
        {
            Wr16(_s.machines, B(m) + 0x24 + 2 * k, c.base);
            c.base = static_cast<uint16_t>((c.base & 0xF800) | kParked[k]);
        }
        else
            c.base = static_cast<uint16_t>(Rd16(_s.machines, B(m) + 0x24 + 2 * k));
    }
}

std::string Flight::Buy()
{
    // buying the plane: no airport -> event 26; too little cash -> nothing; else the first airport
    // plot (column by column) whose hangar bit 0x20 (slot 3) or 0x40 (slot 5) is free; the plane is parked at
    // (plot x + slot, plot y + 6) and the bit set; all hangars taken -> event 19
    if (_s.scalars["airports"] == 0)
    {
        _sim.Event(26, 74, "There is no airport in town for a crop duster.");
        return "There is no airport in town.";
    }
    if (_s.cash < 6000)
        return "Not enough money.";
    for (int col = 0; col < 12; ++col)
        for (int row = 0; row < 12; ++row)
        {
            const int type = _s.PlotType(col, row);
            if ((type & 15) != 5)
                continue;
            const int slot = !(type & 0x20) ? 3 : !(type & 0x40) ? 5 : -1;
            if (slot < 0)
                continue;
            int index = 0;
            for (int i = 1; i < 127 && !index; ++i)
                if (!Machine(_s.machines, i).InUse())
                    index = i;
            if (!index)
                return "You have too many machines.";
            const int hx = col * 8 + slot, hy = row * 8 + 6;
            for (size_t o = 0; o < 56; ++o)
                _s.machines[B(index) + o] = 0;
            Wr16(_s.machines, B(index), kPlaneType);
            Wr8(_s.machines, B(index) + 2, 0x20);   // in use, parked (not stamped as a machine)
            Wr8(_s.machines, B(index) + 5, hx);
            Wr8(_s.machines, B(index) + 6, hy);
            Wr8(_s.machines, B(index) + 0x32, hx);
            Wr8(_s.machines, B(index) + 0x33, hy);
            Wr16(_s.machines, B(index) + 0x22, 0xDD);
            _s.machineCount++;
            _sim.Spend(kPlaneType, 6000);
            _s.scalars["asset_machinery"] += 6000;
            _s.SetCatalogOwn(8, _s.CatalogOwn(8) + 1);
            _s.scalars["airplanes"]++;
            _s.SetPlotType(col, row, type | (slot == 3 ? 0x20 : 0x40));
            Park(index, true);
            _sim.Event(29, 75, "Your crop duster is waiting at the town airport.");
            return {};
        }
    _sim.Event(19, 74, "The airport has no room for another plane.");
    return "The airport is full.";
}

bool Flight::Refuel(int m)
{
    const int cost = 255 - Fuel(_s, m);
    if (cost <= 0 || !_sim.Spend(kPlaneType, cost))
        return false;
    Wr16(_s.machines, B(m) + 0x14, 255);
    return true;
}

bool Flight::Reload(int m)
{
    const int cost = 20 * (255 - Load(_s, m));
    if (cost <= 0 || !_sim.Spend(220 + Chemical(_s, m) - 0xDC, cost))
        return false;
    Wr16(_s.machines, B(m) + 0x20, 255);
    return true;
}

bool Flight::Repair(int m)
{
    const int cost = 10 * Wear(_s, m);
    if (cost <= 0 || !_sim.Spend(kPlaneType, cost))
        return false;
    Wr8(_s.machines, B(m) + 0x11, 0);
    return true;
}

void Flight::SetChemical(int m, int item) { Wr16(_s.machines, B(m) + 0x22, item); }

bool Flight::Start(int m)
{
    // an empty tank still takes off: the first fuel step (after 32 ticks) ends the flight where the plane is
    if (_plane)
        return false;
    _plane = m;
    Park(m, false);
    Machine p(_s.machines, m);
    // the view is centred on the hangar (its clamps use the window's offsets 48 and 32), and the plane
    // starts at the view centre: next to the map edge that is not over the hangar
    _sx = std::clamp(p.HomeX() - kHalfW, 0, 0x60 - ((48 + kWinW) >> 4));
    _sy = std::clamp(p.HomeY() - kHalfH, 0, 0x60 - ((32 + kWinH) >> 4));
    _xo = _yo = 0;
    _heading = 6;
    _alt = 10;
    _spray = _ending = false;
    _now = _passStamp = _wearStamp = _fuelStamp = _sprayStamp = _passes = 0;
    _keys.clear();
    _sim.PlaySound("plane.voc");
    return true;
}

void Flight::Key(int k)
{
    if (_plane)
        _keys.push_back(k);
}

void Flight::End()
{
    if (!_plane)
        return;
    const int cx = static_cast<int>(X()), cy = static_cast<int>(Y());
    const int m = _plane;
    _plane = 0;
    _spray = false;
    if ((_s.PlotType(cx / 8, cy / 8) & 15) == 5)
    {
        Park(m, true);
        _sim.Notify("The crop duster has landed safely.");
        return;
    }
    // anywhere else: a crash: wreck tiles 73 74 / 113 114 under the plane, the
    // home cells become plain ground (ground family + 8), the hangar bit is cleared and the plane is lost
    static const int kWreck[4] = { 73, 74, 113, 114 };
    for (int k = 0; k < 4; ++k)
    {
        MapCell& w = _s.world.At(std::min(kMapSize - 1, cx + k % 2), std::min(kMapSize - 1, cy + k / 2));
        w.base = static_cast<uint16_t>((w.base & 0xF800) | kWreck[k]);
    }
    Machine p(_s.machines, m);
    for (int k = 0; k < 4; ++k)
    {
        MapCell& h = _s.world.At(std::min(kMapSize - 1, p.HomeX() + k % 2), std::min(kMapSize - 1, p.HomeY() + k / 2));
        h.base = static_cast<uint16_t>((h.base & 0xF800) | 23);
    }
    const int pc = p.HomeX() / 8, pr = p.HomeY() / 8;
    _s.SetPlotType(pc, pr, _s.PlotType(pc, pr) & ~(p.HomeX() % 8 == 3 ? 0x20 : 0x40));
    Wr8(_s.machines, B(m) + 2, 0);
    _s.machineCount = std::max(0, _s.machineCount - 1);
    _s.scalars["asset_machinery"] = std::max<int64_t>(0, _s.scalars["asset_machinery"] - 6000);
    _s.SetCatalogOwn(8, std::max(0, _s.CatalogOwn(8) - 1));
    _sim.PlaySound("explode.voc");
    _sim.Notify("The crop duster has crashed!");
}

void Flight::Tick()
{
    if (!_plane)
        return;
    if (++_now - _passStamp < kPassTicks)
        return;
    _passStamp = _now;
    Pass();
    if (_ending)
        End();
}

// one pass of the flight loop
void Flight::Pass()
{
    if (_now - _wearStamp > 32)
    {
        const int wear = (Wear(_s, _plane) + 1) & 0xFF;
        Wr8(_s.machines, B(_plane) + 0x11, wear);
        if (wear > 250)
            _ending = true;
        _wearStamp = _now;
    }
    if (_now - _fuelStamp > 32)
    {
        int fuel = Fuel(_s, _plane) - 1;
        if (fuel <= 0)
        {
            fuel = 0;
            _ending = true;
        }
        Wr16(_s.machines, B(_plane) + 0x14, fuel);
        _fuelStamp = _now;
    }
    if (_spray && _now - _sprayStamp > 8)
    {
        const int load = Load(_s, _plane);
        if (load)
            Wr16(_s.machines, B(_plane) + 0x20, load - 1);
        else
            _spray = false;
        _sprayStamp = _now;
    }
    if (!_keys.empty())
    {
        const int k = _keys.front();
        _keys.pop_front();
        switch (k)
        {
        case 'S':
            if (!_spray && Load(_s, _plane))
                _spray = true;
            else if (_spray)
                _spray = false;
            break;
        case '+': _spray = _spray || Load(_s, _plane) != 0; break;
        case '-': _spray = false; break;
        case 'L': _heading = (_heading + 7) & 7; break;
        case 'R': _heading = (_heading + 1) & 7; break;
        case 'U': _alt = std::min(100, _alt + 1); break;
        case 'D':
            if (--_alt < 1)
            {
                _alt = 0;
                _ending = true;
            }
            break;
        case 'E': _ending = true; break;
        default: break;
        }
    }
    // the vertical step first; at the edge of the map the plane leaves the window centre, and at the window's
    // margin the heading is forced back (the north-west case clamps x to the vertical margin, a quirk kept)
    switch (_heading)
    {
    case 0: North(1); break;
    case 1: North(2); East(); break;
    case 2: East(); break;
    case 3: South(4); East(); break;
    case 4: South(6); break;
    case 5: South(6); West(kXMax); break;
    case 6: West(kXMax); break;
    default: North(2); West(kYMax); break;
    }
    const int cx = static_cast<int>(X()), cy = static_cast<int>(Y());
    // spraying: every 4th pass the field under the plane is treated
    if (_spray && (++_passes & 3) == 0)
        if (const int fi = _sim.FieldIndexAt(cx, cy))
        {
            Field f = _s.FieldAt(fi);
            f.Set8(0x1E, std::min(255, f.Toxicity() + 2));
            switch (Chemical(_s, _plane))
            {
            case 0xDD: f.Set8(0x12, 0); f.Set8(0x3A, f.Get8(0x3A) >> 1); break;
            case 0xDE: f.Set8(0x13, 0); f.Set8(0x3B, f.Get8(0x3B) >> 1); break;
            default: f.Set8(0x14, 0); f.Set8(0x3C, f.Get8(0x3C) >> 1); break;
            }
        }
    // a tornado nearby brings the plane down
    Disasters& d = _sim.Hazards();
    if (d.Active(Disasters::kTornado))
    {
        const int dx = cx - d.TornadoX(), dy = cy - d.TornadoY();
        if (dx * dx + dy * dy < 5)
            _ending = true;
        else if (dx * dx + dy * dy < 50)
            _sim.Notify("Danger: the tornado is close!");
    }
}

void Flight::North(int bounce)
{
    if (_yo > 0)
    {
        --_yo;
        return;
    }
    if (--_sy < 0)
    {
        _sy = 0;
        if (--_yo < -kYMax)
        {
            _yo = -kYMax;
            _heading = bounce;
        }
    }
}

void Flight::South(int bounce)
{
    if (_yo < 0)
    {
        ++_yo;
        return;
    }
    if (++_sy > kSyMax)
    {
        _sy = kSyMax;
        if (++_yo > kYMax)
        {
            _yo = kYMax;
            _heading = bounce;
        }
    }
}

void Flight::East()
{
    if (_xo < 0)
    {
        ++_xo;
        return;
    }
    if (++_sx > kSxMax)
    {
        _sx = kSxMax;
        if (++_xo > kXMax)
        {
            _xo = kXMax;
            _heading = 4;
        }
    }
}

void Flight::West(int limit)
{
    if (_xo > 0)
    {
        --_xo;
        return;
    }
    if (--_sx < 0)
    {
        _sx = 0;
        if (--_xo < -kXMax)
        {
            _xo = -limit;
            _heading = 0;
        }
    }
}

} // namespace osf
