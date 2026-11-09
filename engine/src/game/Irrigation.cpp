// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Irrigation.h"

#include "game/Simulation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace osf {

namespace {
constexpr uint16_t kTouched = 0x8000; // cell flag: reached by the flood of the current fill
constexpr uint16_t kWaterCell = 0x2000; // cell flag: open water (river, lake)
constexpr int kPumpPower = 0x40;
constexpr int kWindmillKind = 0x30;
constexpr int kDx[4] = { 0, 1, 0, -1 }; // north, east, south, west
constexpr int kDy[4] = { -1, 0, 1, 0 };
bool IsPump(int id) { return id == 120 || id == 121; }
} // namespace

Irrigation::Irrigation(GameState& s, Simulation& sim) : _s(s), _sim(sim)
{
    _timer = static_cast<uint16_t>(_sim.Scalar("timer"));
}

int Irrigation::Sources() const
{
    return _s.irrigationFlag.empty() || _s.irrigationLists.size() < 192 ? 0 : std::min<int>(64, _s.irrigationFlag[0]);
}

void Irrigation::AddSource(int x, int y, int kind)
{
    const int n = Sources();
    if (n >= 64 || _s.irrigationFlag.empty() || _s.irrigationLists.size() < 192)
        return;
    _s.irrigationLists[static_cast<size_t>(3 * n)] = static_cast<uint8_t>(x);
    _s.irrigationLists[static_cast<size_t>(3 * n + 1)] = static_cast<uint8_t>(y);
    _s.irrigationLists[static_cast<size_t>(3 * n + 2)] = static_cast<uint8_t>(kind);
    _s.irrigationFlag[0] = static_cast<uint8_t>(n + 1);
}

void Irrigation::RemoveSource(int x, int y)
{
    // the entry is found by its cell and the last entry takes its place
    const int n = Sources();
    for (int i = 0; i < n; ++i)
        if (_s.irrigationLists[static_cast<size_t>(3 * i)] == x && _s.irrigationLists[static_cast<size_t>(3 * i + 1)] == y)
        {
            for (int k = 0; k < 3; ++k)
            {
                _s.irrigationLists[static_cast<size_t>(3 * i + k)] = _s.irrigationLists[static_cast<size_t>(3 * (n - 1) + k)];
                _s.irrigationLists[static_cast<size_t>(3 * (n - 1) + k)] = 0;
            }
            _s.irrigationFlag[0] = static_cast<uint8_t>(n - 1);
            return;
        }
}

void Irrigation::Flip(int x, int y)
{
    // queuing a cell: a fill marks it and fills an empty ditch, an emptying pass unmarks it and empties a full one.
    // The coordinates arrive as bytes, so -1 becomes 255 and is clamped to the far edge.
    MapCell& c = _s.world.At(_s.world.Clamp(x & 0xFF), _s.world.Clamp(y & 0xFF));
    const int id = c.BaseTile();
    if (_parity)
    {
        c.base |= kTouched;
        if (IsEmptyDitch(id))
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (id + 11));
    }
    else
    {
        c.base &= static_cast<uint16_t>(~kTouched);
        if (IsFullDitch(id))
            c.base = static_cast<uint16_t>((c.base & 0xF800) | (id - 11));
    }
}

bool Irrigation::Touched(int x, int y) const
{
    // "already done in this pass": marked in a fill, unmarked in an emptying pass
    const bool marked = (_s.world.At(_s.world.Clamp(x), _s.world.Clamp(y)).base & kTouched) != 0;
    return _parity ? marked : !marked;
}

int Irrigation::Mask(int x, int y, int& mask) const
{
    // connectable neighbours in the order north, east, south, west: ditch-group pieces (ids 87-120), open valves,
    // pumps and road-bridge pieces; an emptying pass also passes closed valves
    mask = 0;
    int n = 0;
    for (int k = 0; k < 4; ++k)
    {
        const int nx = x + kDx[k], ny = y + kDy[k];
        if (!_s.world.In(nx, ny))
            continue;
        const int id = _s.world.At(nx, ny).BaseTile();
        const bool piece = (id >= 87 && id <= 120) || id == 122 || id == 124 || id == 172 || id == 173 || IsPump(id) ||
                           (!_parity && (id == 123 || id == 125));
        if (piece && !Touched(nx, ny))
        {
            mask |= 1 << k;
            n++;
        }
    }
    return n;
}

void Irrigation::Push(int x, int y, int p)
{
    Flip(x, y);
    _queue[static_cast<size_t>(_tail)] = { static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(p) };
    _tail = (_tail + 1) & 0x7F; // a ring of 128 entries without an overflow check, as in the original
}

bool Irrigation::Pop(Entry& e)
{
    if (_head == _tail)
        return false;
    e = _queue[static_cast<size_t>(_head)];
    _head = (_head + 1) & 0x7F;
    return true;
}

bool Irrigation::SourceBesideWater(int x, int y) const
{
    // a pump building on the source cell with an open-water cell in its 3x3 neighbourhood
    const int st = _sim.StructureAt(x, y);
    if (!st || Rd16(_s.structures, static_cast<size_t>(st) * 70) != 0x46)
        return false;
    for (int cx = x - 1; cx < x + 2; ++cx)
        for (int cy = y - 1; cy < y + 2; ++cy)
            if (_s.world.At(_s.world.Clamp(cx), _s.world.Clamp(cy)).base & kWaterCell)
                return true;
    return false;
}

bool Irrigation::Steps(int count)
{
    // up to `count` queue steps; true when the queue ran dry before the last of them
    for (int k = 1; k <= count; ++k)
    {
        Entry e;
        if (!Pop(e))
            return k < count;
        int p = e.p;
        if (p == 0)
        {
            Push(e.x, e.y, 0); // a powerless entry goes round again
            continue;
        }
        int mask = 0;
        const int n = Mask(e.x, e.y, mask);
        if (n > 1)
        {
            p /= n;
            for (int d = 0; d < 4; ++d)
                if (mask & (1 << d))
                    Push(e.x + kDx[d], e.y + kDy[d], p);
        }
        else if (n == 1)
        {
            p--;
            int d = 0;
            while (!(mask & (1 << d)))
                d++;
            const int nx = e.x + kDx[d], ny = e.y + kDy[d];
            if (IsPump(_s.world.At(nx, ny).BaseTile()))
                Push(nx, ny, kPumpPower); // a pump re-powers the flow
            else if (p != 0)
                Push(nx, ny, p);
        }
        else
        {
            // a dead end hands its power to the next entry (with the original's arithmetic)
            Entry next;
            if (Pop(next))
                Push(next.x, next.y, (std::min(255, next.p + p) + next.p) & 0xFF);
        }
    }
    return false;
}

void Irrigation::Pass()
{
    int period = static_cast<int>(_sim.Scalar("irr_period"));
    if (period <= 0)
        period = 64;
    if (static_cast<uint16_t>(_timer - _last) < period)
        return;
    if (!_running)
    {
        if (_parity == 0)
        {
            // start of a fill: every full ditch on the map empties first
            for (auto& c : _s.world.cells)
                if (IsFullDitch(c.BaseTile()))
                    c.base = static_cast<uint16_t>((c.base & 0xF800) | (c.BaseTile() - 11));
        }
        _parity ^= 1;
        if (std::getenv("OSF_TRACE_IRR"))
            std::printf("start %u parity %d\n", _timer, _parity);
        if (_parity == 0)
            _sim.RefreshFields(true); // start of an emptying pass: the fields still see the filled ditches
        const int n = Sources();
        if (std::getenv("OSF_TRACE_IRR"))
            for (int i = 0; i < n; ++i)
                std::printf("  source %d,%d kind %d beside water %d\n", _s.irrigationLists[static_cast<size_t>(3 * i)],
                            _s.irrigationLists[static_cast<size_t>(3 * i + 1)], _s.irrigationLists[static_cast<size_t>(3 * i + 2)],
                            SourceBesideWater(_s.irrigationLists[static_cast<size_t>(3 * i)], _s.irrigationLists[static_cast<size_t>(3 * i + 1)]) ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            const int x = _s.irrigationLists[static_cast<size_t>(3 * i)], y = _s.irrigationLists[static_cast<size_t>(3 * i + 1)];
            const int kind = _s.irrigationLists[static_cast<size_t>(3 * i + 2)];
            if (SourceBesideWater(x, y))
                Push(x, y, kind);
            else if (kind == kWindmillKind && (_sim.Wind() >= 9 || _parity == 0))
                Push(x, y + 1, kind); // a windmill away from water pumps in a breeze
        }
        if (_tail != 0)
            _running = true;
    }
    else
        _s.scalars["irr_period"] = 3;
    if (Steps(4))
    {
        if (std::getenv("OSF_TRACE_IRR"))
            std::printf("end %u parity %d\n", _timer, _parity);
        if (_parity)
            _sim.RefreshFields(false); // end of a fill pass
        _running = false;
        _head = _tail = 0;
    }
    _last = _timer;
}

void Irrigation::Tick()
{
    ++_timer;
    Pass();
}

} // namespace osf
