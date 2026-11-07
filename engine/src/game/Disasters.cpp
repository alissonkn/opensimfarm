// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Disasters.h"
#include "core/Lang.h"

#include "game/Machines.h"
#include "game/Simulation.h"

#include <algorithm>

namespace osf {

namespace {
constexpr int kDryBed = 28;
} // namespace

void Disasters::Shock(int param)
{
    // prices of sensitive crops jump
    for (int c = 0; c < 16; ++c)
    {
        const CropDef& d = _s.crops[c];
        const int sens = d.loaded ? d.B(param) : 0;
        if (!sens)
            continue;
        _sim.ShockPrice(c, sens); // the shown price follows at the next weekly market step
    }
}

void Disasters::Stop(int id)
{
    if (!Active(id))
        return;
    // a tornado cannot end before 0x780 ticks of the raw 50 Hz timer (38.4 s, whatever the game speed) have passed
    // since it started; every request before that is refused
    if (id == kTornado && _sim.RawTicks() - _tStamp < 0x780)
        return;
    _active &= ~(1 << id);
    switch (id)
    {
    case kTornado: _sim.DisasterEvent(20, 63, Tr("The tornado has passed.")); break;
    case kFlood:
        // the water goes back: cells flooded outside the water's reach become dry bed again (flags cleared)
        for (auto& c : _s.world.cells)
            if ((c.base & 0x2000) && !(c.base & 0x1000))
            {
                const int t = c.BaseTile();
                if (t == 120 || t == 121 || t == 172 || t == 173)
                    c.base &= static_cast<uint16_t>(~0x2000);
                else
                    c.base = kDryBed;
            }
        _sim.Shorelines();
        _sim.DisasterEvent(56, 62, Tr("The flood waters are going down."));
        break;
    case kWindstorm: _sim.DisasterEvent(57, 57, Tr("The wind is back to normal.")); break;
    case kDrought:
        // the ponds fill again: every cell in the water's reach becomes water
        for (auto& c : _s.world.cells)
            if ((c.base & 0x1000) && !(c.base & 0x2000))
            {
                const int t = c.BaseTile();
                if (!(t == 120 || t == 121 || t == 172 || t == 173))
                    c.base = static_cast<uint16_t>((c.base & 0xF800) | 54);
                c.base |= 0x2000;
            }
        _sim.Shorelines();
        _sim.DisasterEvent(58, 58, Tr("Rain has ended the drought."));
        break;
    default: break;
    }
}

bool Disasters::Start(int id)
{
    if (_disabled)
        return false;
    // the original's disaster request calls the market shock before it tests the active flags, so every start
    // request raises the prices, also one refused because the disaster is already running (five Flood clicks in a
    // row: oranges' last history week 1071 -> 7616, the 4x cap; seen live 2026-11-02). The daily wind rule asks for a
    // windstorm on every windy day, so the shock repeats while it blows.
    if (id == kDrought)
        Shock(51);
    else if (id == kFlood)
        Shock(50);
    else if (id == kFrost || id == kWindstorm)
        Shock(52);
    if (Active(id))
        return false;
    // start rules of the original's disaster request: a windstorm or locusts do not start during a tornado; a tornado, a windstorm or a
    // frost ends a locust swarm
    if ((id == kWindstorm || id == kLocusts) && Active(kTornado))
        return false;
    if ((id == kTornado || id == kWindstorm || id == kFrost) && Active(kLocusts))
        Stop(kLocusts);
    _active |= 1 << id;
    switch (id)
    {
    case kTornado:
        _tx = 16 + Rand(64);
        _ty = 16 + Rand(64);
        _tdx = Axis();
        _tdy = Axis();
        _tMoveStamp = _sim.RawTicks();
        _tCount = 0;
        _tStamp = _sim.RawTicks(); // the original stamps the raw timer after the warning box (the engine's clock stands still under the box)
        _sim.DisasterEvent(52, 63, Tr("Tornado warning: a tornado has been sighted."), true); // the only box Messages does not hide
        _sim.AutoGoto(_tx, _ty);
        _sim.PlaySound("tornado.voc", _tx, _ty);
        break;
    case kDrought:
    {
        _sim.DisasterEvent(53, 58, Tr("Drought warning: ponds and ditches are drying up."));
        // sweep in place: shores recede
        for (int x = 0; x < _s.world.size; ++x)
            for (int y = 0; y < _s.world.size; ++y)
            {
                MapCell& c = _s.world.At(x, y);
                const int id2 = c.BaseTile();
                if (!(c.base & 0x3000) || id2 == 120 || id2 == 121 || id2 == 172 || id2 == 173)
                    continue;
                int dry = 0;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (!_s.world.In(x + dx, y + dy) || !(_s.world.At(x + dx, y + dy).base & 0x3000))
                            dry++;
                if (dry >= 3)
                    c.base = static_cast<uint16_t>(((c.base & 0xF800) & ~0x2000) | kDryBed);
            }
        _sim.Shorelines();
        break;
    }
    case kFlood:
        _floodDays = 0;
        _floodPos = 0;
        _sim.DisasterEvent(54, 62, Tr("Flood warning: the river has burst its banks."));
        _sim.PlaySound("flood.voc");
        break;
    case kLocusts:
    {
        int fi = 0;
        for (int k = 0; k < 200 && !fi; ++k)
        {
            const int f = 1 + Rand(97);
            if (_s.FieldAt(f).InUse())
                fi = f;
        }
        if (!fi)
        {
            Stop(id);
            return false;
        }
        Field f = _s.FieldAt(fi);
        _lx0 = _lx = f.X();
        _ly0 = _ly = f.Y();
        _lStamp = _lMoveStamp = _sim.RawTicks(); // stamped after the box
        _lCount = 0;
        _sim.DisasterEvent(55, 47, Tr("Locusts: a swarm is over your fields."));
        _sim.AutoGoto(_lx0, _ly0); // the target field's corner
        _sim.PlaySound("swarm.voc", _lx0, _ly0);
        break;
    }
    case kFrost:
    {
        _frostDays = std::max(1, _sim.Temperature() - 28);
        for (int i = 1; i < 98; ++i)
        {
            Field f = _s.FieldAt(i);
            if (f.InUse() && _s.crops[f.Crop() & 15].FrostShift() != 0)
                f.SetStress(std::min(65535, f.Stress() + 255));
        }
        _sim.DisasterEvent(49, 56, Tr("Frost warning: a hard frost has hit your crops."));
        break;
    }
    case kWindstorm:
        _sim.RaiseGust(40);
        _sim.DisasterEvent(48, 57, Tr("Windstorm warning: strong winds are blowing away your topsoil."));
        _sim.PlaySound("wind.voc");
        break;
    default: break;
    }
    return true;
}

void Disasters::FloodCell(int x, int y)
{
    if (!_s.world.In(x, y))
        return;
    if (const int fi = _sim.FieldIndexAt(x, y))
    {
        Field f = _s.FieldAt(fi);
        if (f.State() == 0 || f.State() == 4)
        {
            _sim.DeleteField(fi);
            _sim.Notify(Tr("The flood has washed a field away."));
        }
    }
    MapCell& c = _s.world.At(x, y);
    const int o = c.overlay & 0x7FF;
    if (c.HasObject() && o >= 848 && o <= 945)
        for (int i = 1; i < 127; ++i)
        {
            Machine m(_s.machines, i);
            if (m.InUse() && m.X() == x && m.Y() == y)
            {
                _sim.Fleet().Remove(i);
                _sim.Notify(Tr("The flood has destroyed a machine."));
            }
        }
    if (const int st = _sim.StructureAt(x, y))
    {
        _sim.RemoveStructure(st, false); // washed away: nothing is paid back
        _sim.Notify(Tr("The flood has destroyed a building."));
    }
    if (c.BaseTile() != 172 && c.BaseTile() != 173)
        c.base |= 0x2000; // flooded (the tile word's flag)
}

void Disasters::Tick()
{
    ++_tick;
    if (_tick % 8 == 0)
        _frame = (_frame + 1) & 3;
    // the funnel and the swarm run on the raw 50 Hz timer: every time more than I ticks have passed
    // (I by speed: Ultra 1, Fast 2, else 4) a counter steps, and every fourth step moves them
    const int interval = _s.speedDelay <= 64 ? 1 : _s.speedDelay <= 128 ? 2 : 4;
    const int64_t now = _sim.RawTicks();
    if (Active(kLocusts) && now - _lMoveStamp > interval)
    {
        if (++_lCount > 3)
        {
            _lCount = 0;
            // the swarm strips four cells around it, then moves one cell on each axis
            _sim.BulldozeCell(_lx + 1, _ly + 2);
            _sim.BulldozeCell(_lx + 2, _ly + 2);
            _sim.BulldozeCell(_lx + 1, _ly + 1);
            _sim.BulldozeCell(_lx, _ly + 1);
            _lx = std::clamp(_lx + ((_sim.G2Step() & 1) ? 1 : -1), _lx0, _lx0 + 5);
            _ly = std::clamp(_ly + ((_sim.G2Step() & 1) ? 1 : -1), _ly0, _ly0 + 5);
        }
        _lMoveStamp = now;
        if (now - _lStamp > 0x200) // 0x200 raw ticks (10.24 s) after the swarm's box
            Stop(kLocusts);
    }
    if (Active(kTornado) && now - _tMoveStamp > interval)
    {
        if (++_tCount > 3)
        {
            _tCount = 0;
            _tx += _tdx;
            _ty += _tdy;
            _sim.BulldozeCell(_tx + 1, _ty + 2); // the funnel wrecks what it passes over (the bulldozer's cell routine)
            if (_ty > 86)
                _tdy = -1;
            else if (_ty < 10)
                _tdy = 1;
            if ((_rng() & 15) == 6)
            {
                _tdx = Axis();
                _tdy = Axis();
            }
            if (_tx <= 0 || _tx >= _s.world.size - 1)
                Stop(kTornado);
        }
        _tMoveStamp = now;
    }
    const int fn = _s.world.size - 2; // 94 on the original map
    if (Active(kFlood) && _floodPos < fn * fn)
        for (int k = 0; k < 64 && _floodPos < fn * fn; ++k, ++_floodPos)
        {
            const int x = _floodPos / fn, y = _floodPos % fn;
            if (!(_s.world.At(x, y).base & 0x1000)) // the water-reach flag of the tile word
                continue;
            for (int bx = x - 2; bx <= x; ++bx)
                for (int by = y - 1; by <= y + 1; ++by)
                    FloodCell(bx, by);
        }
    if (Active(kFlood))
        _sim.Shorelines(); // the original re-tiles the whole map after every sweep batch: flooded cells turn to water
}

void Disasters::Day(int wind, int temperature, int& wetness)
{
    (void)temperature;
    if (!_disabled)
    {
        if (wind >= 35)
            Start(kWindstorm);
        if (wind > 70)
            Start(kTornado);
    }
    if (Active(kWindstorm) && wind < 35)
        Stop(kWindstorm);
    if (Active(kTornado) && wind < 70)
        Stop(kTornado);
    if (Active(kWindstorm))
    {
        // wind exposure: tree positions in the two rings around each field
        for (int i = 1; i < 98; ++i)
        {
            Field f = _s.FieldAt(i);
            if (!f.InUse())
                continue;
            // the original's wind exposure: the field grown by 2 cells; every column
            // counts once if a tree stands in the two top or two bottom rows of that frame, every row once if one
            // stands in its two left or two right columns (bounds in bytes, clamped to the map)
            // (a larger world's cells above 127 are real cells, so there only the map clamps)
            const bool large = _s.world.size > 128;
            auto clampc = [&](int v) { return std::clamp(large ? v : static_cast<int>(static_cast<int8_t>(v & 0xFF)), 0, _s.world.size - 1); };
            const int x0 = clampc(f.X() - 2), x1 = clampc(f.X() + f.W() + 2), y0 = clampc(f.Y() - 2), y1 = clampc(f.Y() + f.H() + 2);
            auto tree = [&](int x, int y) {
                if (!_s.world.In(x, y))
                    return false;
                const int id = _s.world.At(x, y).BaseTile();
                return id >= 131 && id < 131 + 16;
            };
            int exposure = 0;
            for (int x = x0; x < x1; ++x)
                exposure += tree(x, y0) || tree(x, y0 + 1) || tree(x, y1) || tree(x, y1 - 1);
            for (int y = y0; y < y1; ++y)
                exposure += tree(x0, y) || tree(x0 + 1, y) || tree(x1, y) || tree(x1 - 1, y);
            f.Set8(0x37, exposure & 0xFF);
            if (exposure < 8)
                f.Set8(0x1B, std::max(0, f.NutrientA() - 1));
        }
    }
    if (Active(kDrought))
    {
        if (wetness >= 3)
            Stop(kDrought);
        else
            wetness = 0;
    }
    if (Active(kFlood) && _floodDays > 8) // the counter goes up once a week, so a flood lasts nine weeks
        Stop(kFlood);
    if (Active(kFrost))
    {
        _frostDays -= 2;
        if (_frostDays <= 0)
            Stop(kFrost);
    }
}

void Disasters::Week()
{
    if (Active(kFlood))
        _floodDays++; // the original's flood counter is advanced in the weekly chain
    if (_disabled || _s.cal.month != 6)
        return;
    if ((_rng() & 127) == 0)
        Start(kWindstorm);
    else if ((_rng() & 255) == 0)
        Start(kTornado);
    if ((_rng() & 63) == 0)
        Start(kLocusts);
}

} // namespace osf
