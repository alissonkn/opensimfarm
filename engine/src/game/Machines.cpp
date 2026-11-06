// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Machines.h"

#include "game/Simulation.h"

#include <algorithm>
#include <climits>
#include <queue>
#include <cstdio>
#include <cstdlib>

namespace osf {

namespace {

constexpr int kDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }; // heading 0 = −y, 2 = +x, 4 = +y, 6 = −x
constexpr int kDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
constexpr int kBlocked = 20000;
constexpr int kPlowed = 0x0C, kBareSoil = 0x24, kCropTileBase = 688;
// field record: the two machines working it (lead, implement)
constexpr int kLead = 0x28, kImpl = 0x2A;
enum : int { kTractor = 0, kPlow = 1, kPlanter = 2, kSprayer = 3, kHarvester = 4, kTrailer = 5, kTruck = 7 };

bool IsGround(int id) { return id >= 15 && id <= 30; }
bool IsRoad(int id) { return (id >= 150 && id <= 171) || (id >= 188 && id <= 191) || (id >= 118 && id <= 119); } // bridges too
int Dist2(int x0, int y0, int x1, int y1) { return (x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1); }

} // namespace

int Machines::TileFor(int type, int heading, bool loaded)
{
    const int h = (heading & 7) / 2;
    switch (type)
    {
    case kTractor: return 848 + h;
    case kPlow: return 852 + h;
    case kPlanter: return 856 + h;
    case kSprayer: return 868 + 2 * h;
    case kHarvester: return 860 + 2 * h;
    case kTrailer: return 876 + 4 * h + (loaded ? 2 : 0);
    case kTruck: return 896 + 4 * h + (loaded ? 2 : 0);
    default: return 848 + h;
    }
}

static constexpr int kWait = 100, kWaitTicks = 24; // RouteStep: wait this tick; at most this many ticks
static bool IsImplement(int type) { return type == kPlow || type == kPlanter || type == kSprayer || type == kTrailer; }
int Machines::Cells(int type) { return type == kHarvester || type == kTrailer || type == kTruck ? 2 : 1; }

Machines::Machines(GameState& s, Simulation& sim) : _s(s), _sim(sim)
{
    // a machine picture (tiles 848 .. 911) on a cell no drawn machine covers is a half truck left by an earlier engine
    // version (user finding F38) and saved with the game: it is taken away. The original's own farms have none.
    std::vector<bool> covered(static_cast<size_t>(_s.world.size) * _s.world.size, false);
    for (int i = 1; i < 127; ++i)
    {
        Machine m(_s.machines, i);
        if (!m.InUse() || !(m.Flags() & 0x80))
            continue;
        const bool v = (m.Heading() & 7) == 0 || (m.Heading() & 7) == 4;
        for (int k = 0; k < Cells(m.Type()); ++k)
        {
            const int x = m.X() + (v ? 0 : k), y = m.Y() + (v ? k : 0);
            if (_s.world.In(x, y))
                covered[static_cast<size_t>(x) * _s.world.size + y] = true;
        }
    }
    for (int x = 0; x < _s.world.size; ++x)
        for (int y = 0; y < _s.world.size; ++y)
        {
            MapCell& c = _s.world.At(x, y);
            const int o = c.overlay & 0x7FF;
            if (c.HasObject() && o >= 848 && o <= 911 && !covered[static_cast<size_t>(x) * _s.world.size + y])
            {
                c.base &= static_cast<uint16_t>(~0x800);
                c.overlay &= 0xF800;
            }
        }
}

// ---------------------------------------------------------------- map stamping
void Machines::Lift(Machine& m)
{
    // only a machine that is drawn takes its own picture away: one that could not be drawn (another machine stood
    // there) used to clear the other machine's cells, which then showed half a truck (user finding F38)
    const bool drawn = (m.Flags() & 0x80) != 0;
    const int own0 = TileFor(m.Type(), m.Heading()), own1 = TileFor(m.Type(), m.Heading(), true) + Cells(m.Type()) - 1;
    for (int k = 0; k < Cells(m.Type()); ++k)
    {
        const bool vertical = (m.Heading() & 7) == 0 || (m.Heading() & 7) == 4;
        const int x = m.X() + (vertical ? 0 : k), y = m.Y() + (vertical ? k : 0);
        if (!_s.world.In(x, y) || !drawn)
            continue;
        MapCell& c = _s.world.At(x, y);
        const int o = c.overlay & 0x7FF;
        if (c.HasObject() && o >= own0 && o <= own1)
        {
            c.base &= static_cast<uint16_t>(~0x800);
            c.overlay &= 0xF800;
        }
    }
    m.SetFlags(m.Flags() & ~0x80);
}

void Machines::Stamp(Machine& m)
{
    const bool vertical = (m.Heading() & 7) == 0 || (m.Heading() & 7) == 4;
    const int n = Cells(m.Type());
    for (int k = 0; k < n; ++k)
    {
        const int x = m.X() + (vertical ? 0 : k), y = m.Y() + (vertical ? k : 0);
        if (!_s.world.In(x, y))
            return; // blocked: the machine stays unmarked
        if (_s.world.At(x, y).HasObject())
        {
            // an animal in the way kicks and loses its female bit
            _sim.Herds().KickAt(x, y, true);
            return;
        }
    }
    for (int k = 0; k < n; ++k)
    {
        const int x = m.X() + (vertical ? 0 : k), y = m.Y() + (vertical ? k : 0);
        MapCell& c = _s.world.At(x, y);
        c.base |= 0x800;
        c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | (TileFor(m.Type(), m.Heading()) + k));
    }
    m.SetFlags(m.Flags() | 0x80);
}

void Machines::StampTowed(Machine& lead)
{
    const int p = lead.Partner();
    if (!p)
        return;
    Machine imp(_s.machines, p);
    Lift(imp);
    const int h = lead.Heading() & 7;
    const int back = (h + 4) & 7;
    imp.SetHeading(h);
    imp.SetPos(std::clamp(lead.X() + kDx[back] * Cells(imp.Type()), 0, _s.world.size - 1), std::clamp(lead.Y() + kDy[back] * Cells(imp.Type()), 0, _s.world.size - 1));
    if (h == 0 && Cells(lead.Type()) == 2)
        imp.SetPos(imp.X(), std::min(_s.world.size - 1, lead.Y() + 2)); // behind a two-cell vehicle facing north
    Stamp(imp);
}

// ---------------------------------------------------------------- steering
int Machines::Cost(const Machine& m, int x, int y) const
{
    if (!_s.world.In(x, y))
        return kBlocked;
    int cost;
    if (Dist2(x, y, m.TargetX(), m.TargetY()) <= 4)
        cost = 0;
    else
    {
        const int id = _s.world.At(x, y).BaseTile();
        cost = IsRoad(id) ? 0 : IsGround(id) ? 85 : kBlocked;
    }
    for (int k = 0; k < 3; ++k)
        if (m.Hist(2 * k) == x && m.Hist(2 * k + 1) == y)
            cost += 200;
    return cost;
}

bool Machines::smartDriving = false;

bool Machines::PlanRoute(const Machine& m, Route& r) const
{
    // Dijkstra over the four neighbours: roads cost 1, bare ground 4; the cells near the destination (the steering's
    // free zone, distance^2 <= 4) cost 1 whatever is on them; everything else is closed
    r = Route{};
    r.tx = m.TargetX();
    r.ty = m.TargetY();
    if (!_s.world.In(r.tx, r.ty) || !_s.world.In(m.X(), m.Y()))
        return false;
    const int N = _s.world.size * _s.world.size;
    std::vector<int> dist(N, INT_MAX), from(N, -1);
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const int start = m.X() * _s.world.size + m.Y(), goal = r.tx * _s.world.size + r.ty;
    dist[start] = 0;
    open.push({ 0, start });
    while (!open.empty())
    {
        const auto [d, c] = open.top();
        open.pop();
        if (d != dist[c])
            continue;
        if (c == goal)
            break;
        const int cx = c / _s.world.size, cy = c % _s.world.size;
        for (int h = 0; h < 8; h += 2)
        {
            const int nx = cx + kDx[h], ny = cy + kDy[h];
            if (!_s.world.In(nx, ny))
                continue;
            int step;
            if (Dist2(nx, ny, r.tx, r.ty) <= 4)
                step = 1;
            else
            {
                // the planner also drives over road bridges (172, 173), which the original's steering treats as
                // blocked, so ditches crossed by a road no longer cut the farm in two
                const int id = _s.world.At(nx, ny).BaseTile();
                step = IsRoad(id) || id == 172 || id == 173 ? 1 : IsGround(id) ? 4 : -1;
            }
            if (step < 0)
                continue;
            const int n = nx * _s.world.size + ny;
            if (d + step < dist[n])
            {
                dist[n] = d + step;
                from[n] = c;
                open.push({ dist[n], n });
            }
        }
    }
    if (dist[goal] == INT_MAX)
        return false;
    for (int c = goal; c != start; c = from[c])
        r.cells.push_back(c);
    std::reverse(r.cells.begin(), r.cells.end());
    return !r.cells.empty();
}

bool Machines::MachineInWay(const Machine& m, int x, int y, int heading) const
{
    // the cells the machine would cover there; its own cells and its partner's do not count
    const bool vertical = (heading & 7) == 0 || (heading & 7) == 4;
    auto covers = [&](const Machine& q, int cx, int cy) {
        const bool v = (q.Heading() & 7) == 0 || (q.Heading() & 7) == 4;
        for (int k = 0; k < Cells(q.Type()); ++k)
            if (q.X() + (v ? 0 : k) == cx && q.Y() + (v ? k : 0) == cy)
                return true;
        return false;
    };
    for (int k = 0; k < Cells(m.Type()); ++k)
    {
        const int cx = x + (vertical ? 0 : k), cy = y + (vertical ? k : 0);
        if (!_s.world.In(cx, cy))
            continue;
        const MapCell& c = _s.world.At(cx, cy);
        const int o = c.overlay & 0x7FF;
        if (!c.HasObject() || o < 848 || o > 945)
            continue;
        if (covers(m, cx, cy))
            continue;
        if (m.Partner() && covers(Machine(const_cast<std::vector<uint8_t>&>(_s.machines), m.Partner()), cx, cy))
            continue;
        return true;
    }
    return false;
}

int Machines::RouteStep(Machine& m)
{
    if (!smartDriving)
        return -1;
    Route& r = _routes[m.index];
    const int here = m.X() * _s.world.size + m.Y();
    auto valid = [&] {
        if (r.tx != m.TargetX() || r.ty != m.TargetY() || r.next >= r.cells.size())
            return false;
        // on the route: the next cell must be a neighbour of the machine and still open
        if (r.next > 0 && r.cells[r.next - 1] != here)
            return false;
        if (r.next == 0 && std::abs(r.cells[0] / _s.world.size - m.X()) + std::abs(r.cells[0] % _s.world.size - m.Y()) != 1)
            return false;
        const int nx = r.cells[r.next] / _s.world.size, ny = r.cells[r.next] % _s.world.size;
        if (Dist2(nx, ny, r.tx, r.ty) <= 4)
            return true;
        const int id = _s.world.At(nx, ny).BaseTile();
        return IsRoad(id) || IsGround(id) || id == 172 || id == 173;
    };
    if (!valid() && !PlanRoute(m, r))
    {
        r = Route{};
        return -1;
    }
    const int nx = r.cells[r.next] / _s.world.size, ny = r.cells[r.next] % _s.world.size;
    for (int h = 0; h < 8; h += 2)
        if (m.X() + kDx[h] == nx && m.Y() + kDy[h] == ny)
        {
            // another machine on the next cells: wait behind it (a queue on the road), but not for ever, so two
            // machines meeting head on still pass (overlapping for a moment, as the original's steering lets them)
            if (MachineInWay(m, nx, ny, h) && r.waited < kWaitTicks)
            {
                r.waited++;
                return kWait;
            }
            r.waited = 0;
            r.next++;
            r.steps++;
            return h;
        }
    r = Route{};
    return -1;
}

bool Machines::Drive(Machine& m)
{
    if (Arrived(m))
    {
        m.SetStuck(0);
        _routes.erase(m.index);
        return false;
    }
    const int routed = RouteStep(m);
    if (routed == kWait)
        return true; // standing behind another machine this tick
    if (std::getenv("OSF_DRIVELOG"))
        std::printf("drive %d type %d at %d,%d to %d,%d %s tick %d\n", m.index, m.Type(), m.X(), m.Y(), m.TargetX(), m.TargetY(),
                    routed >= 0 ? "route" : "steer", _tick);
    if (routed >= 0)
    {
        const int nx = m.X() + kDx[routed], ny = m.Y() + kDy[routed];
        Lift(m);
        m.SetHist(4, m.Hist(2));
        m.SetHist(5, m.Hist(3));
        m.SetHist(2, m.Hist(0));
        m.SetHist(3, m.Hist(1));
        m.SetHist(0, m.X());
        m.SetHist(1, m.Y());
        m.SetHeading(routed);
        m.SetPos(nx, ny);
        if (IsGround(_s.world.At(nx, ny).BaseTile()) && ++_offRoad > 10)
        {
            _offRoad = 0;
            if (m.Type() != 6)
                m.AddWear(2);
            if (m.Partner())
                Machine(_s.machines, m.Partner()).AddWear(2);
        }
        Stamp(m);
        StampTowed(m);
        m.SetStuck(0); // a planned route never counts as stuck
        if (Arrived(m))
        {
            _routes.erase(m.index);
            return false;
        }
        return true;
    }
    // (signed bytes as the original; on a larger world, an engine addition, a distance can pass 127 and is kept whole)
    auto s8 = [&](int v) { return _s.world.size > 128 ? v : static_cast<int>(static_cast<int8_t>(v)); };
    const int dx = s8(m.X() - m.TargetX()), dy = s8(m.Y() - m.TargetY());
    int heading;
    if (std::abs(dy) >= std::abs(dx))
        heading = dy < 0 ? 4 : 0;
    else
        heading = dx < 0 ? 2 : 6;
    const int left = (heading + 6) & 7, right = (heading + 2) & 7;
    const int cf = Cost(m, m.X() + kDx[heading], m.Y() + kDy[heading]);
    const int cl = Cost(m, m.X() + kDx[left], m.Y() + kDy[left]) + 5;
    const int cr = Cost(m, m.X() + kDx[right], m.Y() + kDy[right]) + 5;
    int choice = heading;
    if (cf > 0x75A && cl > 0x75A && cr > 0x75A)
    {
        // nowhere to go: placed on the destination
        Lift(m);
        m.SetPos(m.TargetX(), m.TargetY());
        Stamp(m);
        StampTowed(m);
        m.SetStuck(0);
        return false;
    }
    if (cf <= cl && cf <= cr)
        choice = heading;
    else if (cl <= cr)
        choice = left;
    else
        choice = right;

    const int nx = m.X() + kDx[choice], ny = m.Y() + kDy[choice];
    // the step is always taken: the steering looks at the terrain only; an object on the new cell is found
    // by the occupancy test when the machine is stamped (an animal there kicks), and the machine is then not drawn
    Lift(m);
    m.SetHist(4, m.Hist(2));
    m.SetHist(5, m.Hist(3));
    m.SetHist(2, m.Hist(0));
    m.SetHist(3, m.Hist(1));
    m.SetHist(0, m.X());
    m.SetHist(1, m.Y());
    m.SetHeading(choice);
    m.SetPos(nx, ny);
    if (IsGround(_s.world.At(nx, ny).BaseTile()) && ++_offRoad > 10)
    {
        // driving on bare ground wears machines
        _offRoad = 0;
        if (m.Type() != 6)
            m.AddWear(2);
        if (m.Partner())
            Machine(_s.machines, m.Partner()).AddWear(2);
    }
    // (no valve switching here: in the original only a mouse press switches a valve)
    Stamp(m);
    StampTowed(m);
    if (Arrived(m))
    {
        m.SetStuck(0);
        return false;
    }
    m.SetStuck(m.Stuck() + 1);
    if (static_cast<int8_t>(m.Stuck()) >= 0x40)
    {
        // 64 steps without arriving: towed to the destination, with wear and the hint
        Lift(m);
        m.SetPos(m.TargetX(), m.TargetY());
        if (m.Type() != 6)
            m.AddWear(10);
        Stamp(m);
        StampTowed(m);
        m.SetStuck(0);
        if (std::getenv("OSF_TOWLOG"))
            std::printf("tow %d type %d to %d,%d\n", m.index, m.Type(), m.TargetX(), m.TargetY());
        if (_tick - _lastStuckNote > 2000) // the hint once in a while, not for every machine
        {
            _sim.Notify("A stuck machine was towed to where it was going. Roads help.");
            _lastStuckNote = _tick;
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- storage in sheds and barns
// A machine parked on a cell of a shed or barn (types 0x42-0x45) is put into the building's slot grid like any stored
// object: the first block of free slots that fits its footprint (columns outer, rows inner, then the
// turned footprint), each slot getting the machine's index, its type as content and its picture; the machine moves
// to the block's first cell and is flagged stored (4) instead of placed. Dispatch takes it out again.
namespace {
constexpr int kMachinePicture[9] = { 0x350, 0x354, 0x358, 0x364, 0x35C, 0x36C, 0x37C, 0x380, 0x404 };
}

int Machines::BuildingAt(int x, int y) const
{
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        const int cat = Rd16(_s.structures, b);
        if (!(Rd8(_s.structures, b + 2) & 0x20) || cat < 0x42 || cat > 0x45)
            continue;
        const size_t rec = static_cast<size_t>(12 + cat - 0x40) * 18;
        const int w = _s.catalog[rec + 8], h = _s.catalog[rec + 9];
        const int bx = Rd8(_s.structures, b + 4), by = Rd8(_s.structures, b + 5);
        if (x >= bx && y >= by && x < bx + w && y < by + h)
            return i;
    }
    return 0;
}

bool Machines::Store(int index)
{
    Machine m(_s.machines, index);
    if (m.Flags() & 0x04)
        return true; // already stored
    const int st = BuildingAt(m.X(), m.Y());
    if (!st || m.Type() > 8)
        return false;
    const size_t b = static_cast<size_t>(st) * 70;
    const size_t rec = static_cast<size_t>(12 + Rd16(_s.structures, b) - 0x40) * 18;
    const int W = _s.catalog[rec + 8], H = _s.catalog[rec + 9];
    const size_t mrec = static_cast<size_t>(m.Type()) * 18;
    const int w = std::max(1, static_cast<int>(_s.catalog[mrec + 8])), h = std::max(1, static_cast<int>(_s.catalog[mrec + 9]));
    auto slot = [](int lx, int ly) { return lx * 3 + ly; };
    auto fits = [&](int lx, int ly, int fw, int fh) {
        if (lx + fw > W || ly + fh > H)
            return false;
        for (int i = lx; i < lx + fw; ++i)
            for (int j = ly; j < ly + fh; ++j)
                if (slot(i, j) >= 12 || Rd16(_s.structures, b + 0x22 + 2 * static_cast<size_t>(slot(i, j))) != 0xFFFF)
                    return false;
        return true;
    };
    for (int lx = 0; lx < W; ++lx)
        for (int ly = 0; ly < H; ++ly)
            if (fits(lx, ly, w, h) || fits(lx, ly, h, w))
            {
                // as the original's placing: upright first, else turned; a turned machine
                // fills h columns of w rows with its turned pictures, w * h * (catalogue byte 10) further on
                const bool turned = !fits(lx, ly, w, h);
                const int cw = turned ? h : w, ch = turned ? w : h;
                int k = turned ? w * h * _s.catalog[mrec + 10] : 0;
                for (int i = lx; i < lx + cw; ++i)
                    for (int j = ly; j < ly + ch; ++j)
                    {
                        const size_t s = static_cast<size_t>(slot(i, j));
                        if (s >= 12)
                            continue;
                        Wr8(_s.structures, b + 0x3A + s, index);
                        Wr16(_s.structures, b + 0x22 + 2 * s, m.Type());
                        Wr16(_s.structures, b + 0x0A + 2 * s, kMachinePicture[m.Type()] + k++);
                    }
                Lift(m);
                m.SetPos(Rd8(_s.structures, b + 4) + lx, Rd8(_s.structures, b + 5) + ly);
                m.SetFlags((m.Flags() | 0x04) & ~0x80);
                return true;
            }
    return false;
}

std::vector<int> Machines::RepairStorage()
{
    std::vector<int> repaired;
    for (int st = 1; st < 127; ++st)
    {
        const size_t b = static_cast<size_t>(st) * 70;
        if (!(Rd8(_s.structures, b + 2) & 0x20))
            continue;
        std::map<int, std::vector<int>> slots; // machine index -> its slots, in slot order
        for (int k = 0; k < 12; ++k)
            if (Rd16(_s.structures, b + 0x22 + 2 * static_cast<size_t>(k)) < 0x40)
                slots[Rd8(_s.structures, b + 0x3A + static_cast<size_t>(k))].push_back(k);
        bool bad = false;
        for (const auto& [index, list] : slots)
        {
            if (index <= 0 || index >= 127)
            {
                bad = true;
                break;
            }
            Machine m(_s.machines, index);
            const size_t mrec = static_cast<size_t>(m.Type()) * 18;
            if (m.Type() > 8 || !m.InUse())
            {
                bad = true;
                break;
            }
            const int w = std::max(1, static_cast<int>(_s.catalog[mrec + 8])), h = std::max(1, static_cast<int>(_s.catalog[mrec + 9]));
            int x0 = 99, y0 = 99, x1 = -1, y1 = -1;
            for (int k : list)
            {
                x0 = std::min(x0, k / 3), x1 = std::max(x1, k / 3);
                y0 = std::min(y0, k % 3), y1 = std::max(y1, k % 3);
            }
            const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
            const bool upright = bw == w && bh == h, turned = bw == h && bh == w;
            if (static_cast<int>(list.size()) != w * h || (!upright && !turned))
            {
                bad = true;
                break;
            }
            // the pictures must run on in slot order from the upright or the turned first picture
            const int first = kMachinePicture[m.Type()] + (upright ? 0 : w * h * _s.catalog[mrec + 10]);
            for (size_t n = 0; n < list.size(); ++n)
                if (Rd16(_s.structures, b + 0x0A + 2 * static_cast<size_t>(list[n])) != first + static_cast<int>(n))
                    bad = true;
            if (bad)
                break;
        }
        if (!bad)
            continue;
        std::vector<int> machines;
        for (const auto& kv : slots)
            if (kv.first > 0 && kv.first < 127)
                machines.push_back(kv.first);
        for (int k = 0; k < 12; ++k)
            if (Rd16(_s.structures, b + 0x22 + 2 * static_cast<size_t>(k)) < 0x40)
            {
                Wr8(_s.structures, b + 0x3A + static_cast<size_t>(k), 0);
                Wr16(_s.structures, b + 0x22 + 2 * static_cast<size_t>(k), 0xFFFF);
                Wr16(_s.structures, b + 0x0A + 2 * static_cast<size_t>(k), 0xD9);
            }
        for (int index : machines)
        {
            Machine m(_s.machines, index);
            const int flags = m.Flags();
            m.SetFlags(flags & ~0x04);
            m.SetPos(Rd8(_s.structures, b + 4), Rd8(_s.structures, b + 5));
            if (!Store(index))
                m.SetFlags(flags); // no room after all: left as it was (cannot happen for a layout the engine wrote)
        }
        repaired.push_back(st);
    }
    return repaired;
}

void Machines::Unstore(int index)
{
    Machine m(_s.machines, index);
    if (const int st = BuildingAt(m.X(), m.Y()))
    {
        const size_t b = static_cast<size_t>(st) * 70;
        for (size_t s = 0; s < 12; ++s)
            if (Rd8(_s.structures, b + 0x3A + s) == index && Rd16(_s.structures, b + 0x22 + 2 * s) < 0x40)
            {
                Wr8(_s.structures, b + 0x3A + s, 0);
                Wr16(_s.structures, b + 0x22 + 2 * s, 0xFFFF);
                Wr16(_s.structures, b + 0x0A + 2 * s, 0xD9);
            }
    }
    m.SetFlags(m.Flags() & ~0x04);
}

// ---------------------------------------------------------------- dispatch and leasing
int Machines::Create(int type, int x, int y, bool leased)
{
    for (int i = 1; i < 127; ++i)
    {
        Machine m(_s.machines, i);
        if (m.InUse())
            continue;
        const size_t b = static_cast<size_t>(i) * 56;
        for (size_t o = 0; o < 56; ++o)
            _s.machines[b + o] = 0;
        Wr16(_s.machines, b, type);
        m.SetFlags(leased ? (0x20 | 0x40 | 0x01) : 0x20);
        m.SetPos(x, y);
        m.SetTarget(x, y);
        Wr8(_s.machines, b + 0x32, x);
        Wr8(_s.machines, b + 0x33, y);
        _s.machineCount++;
        Stamp(m);
        return i;
    }
    return 0;
}

void Machines::Remove(int index)
{
    Machine m(_s.machines, index);
    if (!m.InUse())
        return;
    if (m.Partner())
    {
        Machine p(_s.machines, m.Partner());
        p.SetPartner(0);
    }
    // freeing a plane's record: its four home cells become ground (ground base + 8) and the
    // airport plot's hangar bit is cleared (seen live after a SELL: cells 23, plot 0xE5 -> 0xA5)
    if (m.Type() == 8)
    {
        for (int k = 0; k < 4; ++k)
        {
            MapCell& c = _s.world.At(std::min(_s.world.size - 1, m.HomeX() + k % 2), std::min(_s.world.size - 1, m.HomeY() + k / 2));
            c.base = static_cast<uint16_t>((c.base & 0xF800) | 23);
        }
        const int pc = m.HomeX() / 8, pr = m.HomeY() / 8, slot = m.HomeX() % 8;
        if (slot == 3 || slot == 5)
            _s.SetPlotType(pc, pr, _s.PlotType(pc, pr) & ~(slot == 3 ? 0x20 : 0x40));
    }
    Lift(m);
    m.SetFlags(0);
    _s.machineCount = std::max(0, _s.machineCount - 1);
    _sim.Puff(m.X(), m.Y());
}

bool Machines::FootprintFree(int type, int x, int y) const
{
    for (int k = 0; k < Cells(type); ++k)
    {
        if (!_s.world.In(x, y + k))
            return false;
        const MapCell& c = _s.world.At(x, y + k);
        if (c.HasObject() || !(IsGround(c.BaseTile()) || IsRoad(c.BaseTile())) || _s.PlotOwner(x / 8, (y + k) / 8) != 1)
            return false;
    }
    return true;
}

int Machines::Lease(int type)
{
    const int hx = static_cast<int>(_sim.Scalar("homestead_x")), hy = static_cast<int>(_sim.Scalar("homestead_y"));
    return Create(type, std::clamp(hx * 8 + 1, 0, _s.world.size - 1), std::clamp(hy * 8 + 5, 0, _s.world.size - 1), true);
}

int Machines::Dispatch(int type, int x, int y)
{
    int best = 0, bestD = INT_MAX;
    bool owned = false;
    for (int i = 1; i < 127; ++i)
    {
        Machine m(_s.machines, i);
        if (!m.InUse() || m.Type() != type)
            continue;
        if (!m.Leased())
            owned = true;
        if ((m.Wear() & 0xC0) == 0xC0 || (m.Flags() & (0x01 | 0x08 | 0x40)) || m.Partner())
            continue;
        const int d = Dist2(m.X(), m.Y(), x, y);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    // no idle machine of the type: with AutoLease on (menu flag 6) one is leased for half its catalogue price,
    // paid now; without the money nothing happens and the job waits
    (void)owned;
    if (!best && _s.scalars["menu_6"] && _sim.Spend(1500, _s.CatalogPrice(type) / 2))
        best = Lease(type);
    else if (!best && _tick - _lastMachineNote > 2000)
    {
        // the original waits without a word; the engine says why the job does not start (user finding F39)
        static const char* const kName[8] = { "tractor", "plow", "planter", "sprayer", "harvester", "trailer", "machine", "truck" };
        const std::string name = type >= 0 && type < 8 ? kName[type] : "machine";
        _sim.Notify(!_s.scalars["menu_6"] ? "A job waits for a free " + name + ": buy one or turn on Options > AutoLease."
                                          : "A job waits for a " + name + ": there is not enough money to lease one.");
        _lastMachineNote = _tick;
    }
    if (best)
    {
        Machine m(_s.machines, best);
        if (m.Flags() & 0x04)
            Unstore(best); // out of its shed or barn
        m.SetFlags(m.Flags() | 0x01 | 0x08);
        if (!m.Placed())
        {
            // out of its shed: appear at the home cell
            m.SetTarget(m.X(), m.Y());
            Stamp(m);
        }
    }
    return best;
}

int Machines::MachineAt(int x, int y) const
{
    for (int i = 1; i < 127; ++i)
    {
        Machine m(_s.machines, i);
        if (!m.InUse())
            continue;
        const bool vertical = (m.Heading() & 7) == 0 || (m.Heading() & 7) == 4;
        const int n = Cells(m.Type());
        const int w = vertical ? 1 : n, h = vertical ? n : 1;
        if (x >= m.X() && x < m.X() + w && y >= m.Y() && y < m.Y() + h)
            return i;
    }
    return 0;
}

void Machines::PlayerMove(int index, int x, int y)
{
    Machine m(_s.machines, index);
    if (m.Flags() & 0x04)
    {
        // out of the shed or barn; the building's open view is drawn again without it
        const int bx = m.X(), by = m.Y();
        Unstore(index);
        _sim.ToggleInside(bx, by);
        _sim.ToggleInside(bx, by);
    }
    if (!m.Placed())
        Stamp(m);
    // driving (flag 2) and the Move tool's mark (bit 3 of the next byte, set by the original, read nowhere)
    m.SetTarget(x, y);
    m.SetFlags(m.Flags() | 0x02);
    Wr8(_s.machines, static_cast<size_t>(index) * 56 + 3, Rd8(_s.machines, static_cast<size_t>(index) * 56 + 3) | 0x08);
}

void Machines::SendHome(int index)
{
    if (!index)
        return;
    Machine m(_s.machines, index);
    m.SetTarget(m.HomeX(), m.HomeY());
    m.SetFlags((m.Flags() | 0x02) & ~0x08);
}

// ---------------------------------------------------------------- field sweep
bool Machines::SweepStep(Field& f, Machine& lead, int paintTile)
{
    const int x0 = f.X(), x1 = f.X() + f.W() - 1, y1 = f.Y() + f.H() - 1;
    int& dir = _sweepDir[f.Index()];
    if (dir == 0)
        dir = 1;
    // paint the cell being left
    if (paintTile >= 0 && lead.X() >= x0 && lead.X() <= x1 && lead.Y() <= y1)
    {
        MapCell& c = _s.world.At(lead.X(), lead.Y());
        const int id = c.BaseTile();
        if (!(id >= 539 && id <= 634) && !IsRoad(id))
            c.base = static_cast<uint16_t>((c.base & 0xF800) | paintTile);
    }
    int nx = lead.X() + dir, ny = lead.Y();
    if (nx > x1 + 1 || nx < x0 - 1)
    {
        dir = -dir;
        nx = lead.X();
        ny = lead.Y() + 1;
    }
    if (ny > y1)
        return true;
    Lift(lead);
    lead.SetHeading(ny != lead.Y() ? 4 : dir > 0 ? 2 : 6);
    lead.SetPos(std::clamp(nx, 0, _s.world.size - 1), ny);
    // every step runs the occupancy test over the lead's whole footprint (the catalogue
    // size, turned with the heading: a harvester driving along a row covers two cells), in order; the first animal
    // found kicks (and loses its female bit), and the machine and its implement are not drawn for this step (F7E4).
    // The towed implement's cells are tested when it is stamped (StampTowed)
    const bool vertical = (lead.Heading() & 7) == 0 || (lead.Heading() & 7) == 4;
    int hx = -1, hy = -1;
    for (int k = 0; k < Cells(lead.Type()) && hx < 0; ++k)
    {
        const int cx = lead.X() + (vertical ? 0 : k), cy = lead.Y() + (vertical ? k : 0);
        if (_s.world.In(cx, cy) && _s.world.At(cx, cy).HasObject())
        {
            hx = cx;
            hy = cy;
        }
    }
    if (hx >= 0)
    {
        _sim.Herds().KickAt(hx, hy, true);
        lead.SetFlags(lead.Flags() & ~0x80);
        if (lead.Partner())
        {
            Machine imp(_s.machines, lead.Partner());
            Lift(imp);
            imp.SetFlags(imp.Flags() & ~0x80);
        }
        return false;
    }
    MapCell& c = _s.world.At(lead.X(), lead.Y());
    c.base |= 0x800;
    c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | TileFor(lead.Type(), lead.Heading()));
    lead.SetFlags(lead.Flags() | 0x80);
    StampTowed(lead);
    return false;
}

// ---------------------------------------------------------------- job scripts
void Machines::StepField(int fi)
{
    Field f = _s.FieldAt(fi);
    const int st = f.State();
    const int sub = f.Sub();
    const int ex = f.X(), ey = f.Y();
    auto lead = [&] { return Machine(_s.machines, f.Get16(kLead)); };
    auto impl = [&] { return Machine(_s.machines, f.Get16(kImpl)); };
    auto valid = [&](int m) { return m > 0 && m < 127 && Machine(_s.machines, m).InUse(); };
    auto goTo = [&](Machine m, int x, int y) {
        m.SetTarget(x, y);
        m.SetFlags(m.Flags() | 0x02);
    };
    auto hitch = [&](Machine l, Machine i) {
        Lift(i);
        l.SetPartner(i.index);
        i.SetPartner(l.index);
        StampTowed(l);
    };
    auto unhitchHome = [&](Machine l) {
        if (!l.Partner())
            return;
        Machine i(_s.machines, l.Partner());
        Lift(i);
        l.SetPartner(0);
        i.SetPartner(0);
        i.SetPos(i.HomeX(), i.HomeY());
        i.SetFlags(i.Flags() & ~(0x01 | 0x08 | 0x02));
        if (i.Leased())
            i.SetFlags(0);
        else
            Stamp(i);
    };
    auto startSweep = [&](Machine l) {
        Lift(l);
        l.SetPos(std::max(0, ex - 1), ey);
        l.SetHeading(2);
        _sweepDir[fi] = 1;
        Stamp(l);
        StampTowed(l);
    };
    // a script step that needs a machine that has gone away restarts the job
    if (sub > 1 && !(st == 5 && (sub >= 10 || sub == 6 || sub == 7)) && !valid(f.Get16(kLead))) // truck-only steps
    {
        f.SetSub(0);
        return;
    }

    if (st == 3 || (st >= 6 && st <= 9))
    {
        const bool plant = st == 3;
        const int implType = plant ? (sub < 7 ? kPlow : kPlanter) : kSprayer;
        switch (sub)
        {
        case 0: f.SetSub(1); break;
        case 1:
            if (const int t = Dispatch(kTractor, ex, ey))
            {
                f.Set16(kLead, t);
                f.SetSub(2);
            }
            break;
        case 2:
        case 7:
            if (const int p = Dispatch(implType, ex, ey))
            {
                f.Set16(kImpl, p);
                Machine i = impl();
                goTo(lead(), i.X(), i.Y());
                f.SetSub(sub + 1);
            }
            break;
        case 3:
        case 8:
            if (Machine l = lead(); !Drive(l))
            {
                hitch(lead(), impl());
                goTo(lead(), ex, ey);
                f.SetSub(plant && sub == 8 ? 10 : 4);
            }
            break;
        case 4:
        case 10:
            if (Machine l = lead(); !Drive(l))
            {
                startSweep(lead());
                f.SetSub(sub + 1);
            }
            break;
        case 5:
        case 11:
        {
            Machine l = lead();
            const int paint = !plant ? -1 : sub == 5 ? kPlowed : kCropTileBase + 8 * (f.Crop() & 15);
            if (SweepStep(f, l, paint))
            {
                if (plant && sub == 5)
                {
                    goTo(l, l.HomeX(), l.HomeY()); // stays assigned: the planter comes next
                    f.SetSub(6);
                }
                else
                {
                    SendHome(l.index);
                    _sim.FinishJob(f);
                }
            }
            break;
        }
        case 6:
            if (Machine l = lead(); !Drive(l))
            {
                unhitchHome(lead());
                f.SetSub(7);
            }
            break;
        default: f.SetSub(0); break;
        }
        return;
    }

    if (st == 5)
    {
        switch (sub)
        {
        case 0:
            if (const int h = Dispatch(kHarvester, ex, ey))
            {
                f.Set16(kLead, h);
                f.SetSub(1);
            }
            break;
        case 1:
            if (const int t = Dispatch(kTrailer, ex, ey))
            {
                f.Set16(kImpl, t);
                Machine i = impl();
                goTo(lead(), i.X(), i.Y());
                f.SetSub(2);
            }
            break;
        case 2:
            if (const int k = Dispatch(kTruck, ex, ey))
            {
                _truck[fi] = k;
                goTo(Machine(_s.machines, k), std::max(0, ex - 2), ey);
            }
            f.SetSub(3); // the harvest goes ahead without a truck; the crop is then sold on the spot
            break;
        case 3:
            if (Machine l = lead(); !Drive(l))
            {
                hitch(lead(), impl());
                goTo(lead(), ex, ey);
                f.SetSub(4);
            }
            break;
        case 4:
            if (Machine l = lead(); !Drive(l))
            {
                startSweep(lead());
                f.SetSub(5);
            }
            break;
        case 5:
        {
            Machine l = lead();
            const bool perennial = _s.crops[f.Crop() & 15].Perennial();
            if (SweepStep(f, l, perennial ? -1 : kBareSoil))
            {
                // the crop is loaded on the truck: to a silo with room (stored when it gets there), else sold now and
                // driven off the farm (the field is free when the truck has gone)
                SendHome(l.index);
                f.Set16(kLead, 0);
                const int silo = _sim.HarvestStorage(f);
                if (!_truck.count(fi))
                {
                    _sim.FinishJob(f); // no truck: settled at once
                    break;
                }
                Machine k(_s.machines, _truck[fi]);
                if (silo)
                {
                    goTo(k, Rd8(_s.structures, static_cast<size_t>(silo) * 70 + 4), Rd8(_s.structures, static_cast<size_t>(silo) * 70 + 5));
                    f.SetSub(6);
                }
                else
                {
                    _sim.HarvestPay(f, false);
                    goTo(k, 0, 32);
                    f.SetSub(7);
                }
            }
            break;
        }
        case 6:
        case 7:
        {
            if (_truck.count(fi))
            {
                Machine k(_s.machines, _truck[fi]);
                if (Drive(k))
                    break;
                SendHome(_truck[fi]);
                _truck.erase(fi);
            }
            if (sub == 6)
                _sim.HarvestPay(f, true);
            ReleaseField(fi);
            _sim.HarvestEnd(f);
            break;
        }
        case 10:
            // hand harvest: the picking crew is paid (posting code 500, a bucket the ledger does not show)
            if (_sim.Spend(500, f.Get16(0x38) ? f.Get16(0x38) : 500))
                f.SetSub(11);
            else if (_tick - _lastPickerNote > 2000)
            {
                _sim.Notify("Not enough money to pay the pickers.");
                _lastPickerNote = _tick;
            }
            break;
        case 11:
            if (const int k = Dispatch(kTruck, ex, ey))
            {
                _truck[fi] = k;
                goTo(Machine(_s.machines, k), ex, ey);
                f.SetSub(12);
            }
            break;
        case 12:
            if (Machine k(_s.machines, _truck.count(fi) ? _truck[fi] : 0); _truck.count(fi) && Drive(k))
                break;
            // stubble scattered over the field: 6 cells in 8 get one of the two stubble pictures on the overlay layer
            f.Set8(0x30, _s.cal.day);
            for (int x = f.X(); x < f.X() + f.W(); ++x)
                for (int y = f.Y(); y < f.Y() + f.H(); ++y)
                {
                    MapCell& c = _s.world.At(x, y);
                    if (c.base & 0x800)
                        continue;
                    if ((_sim.G2Step() & 7) < 6)
                    {
                        c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | (0x40C + (_sim.G2Step() & 1)));
                        c.base |= 0x800;
                    }
                }
            f.SetSub(13);
            break;
        case 13:
            if (((_s.cal.day - f.Get8(0x30)) & 0xFFFF) < 3)
                break;
            for (int x = f.X(); x < f.X() + f.W(); ++x)
                for (int y = f.Y(); y < f.Y() + f.H(); ++y)
                {
                    MapCell& c = _s.world.At(x, y);
                    const int o = c.overlay & 0x7FF;
                    if (o == 0x40C || o == 0x40D)
                    {
                        // the stubble goes (a machine or animal standing there keeps its object flag)
                        c.base &= static_cast<uint16_t>(~0x800);
                        c.overlay &= 0xF800;
                    }
                    // an annual field is bare soil again, its sign and status markers stay (user finding F39: after
                    // a hand harvest the field had lost its sign)
                    const int id = c.BaseTile();
                    if (!_s.crops[f.Crop() & 15].Perennial() && !(id >= 539 && id <= 634))
                        c.base = static_cast<uint16_t>((c.base & 0xF824) | 0x24);
                }
            if (_truck.count(fi))
            {
                SendHome(_truck[fi]);
                _truck.erase(fi);
            }
            _sim.FinishJob(f); // load, then store or sell
            break;
        default: f.SetSub(0); break;
        }
    }
}

void Machines::WorkWear(int index, int amount)
{
    if (index <= 0 || index >= 127)
        return;
    Machine m(_s.machines, index);
    if (m.InUse() && m.Type() != 6)
        m.AddWear(m.Usage() / 4 + amount);
}

void Machines::Week()
{
    // the loop stops when it has seen as many machines in use as the live count, which a sale lowers:
    // in a week with a sale the last machine of the table is skipped (seen live: a worn-out tractor and plane were
    // sold a week apart)
    for (int i = 1, seen = 0; i < 127 && seen < _s.machineCount; ++i)
    {
        Machine m(_s.machines, i);
        if (!m.InUse())
            continue;
        ++seen;
        // the usage counter of every machine: past 4 weeks it adds wear and starts again
        m.SetUsage(m.Usage() + 1);
        if (m.Usage() > 4)
        {
            WorkWear(i, 1);
            m.SetUsage(0);
        }
        // a worn-out machine is sold off: wear 255 and not leased; the sale is refused
        // for a reserved machine or one on a job (flags 0x01 / 0x08). It pays the machine's value (price / 8) as a
        // sale: cash, Machinery Sales and the yearly income. Any machine, the crop duster included; a parked plane's
        // four cells become plain ground and its hangar flag stays set. Then the explosion, and unless a building
        // covers the machine's cell, quick message 73 (a machine was lost).
        if (m.Wear() < 255 || m.Leased() || (m.Flags() & 0x09))
            continue;
        const int type = m.Type(), x = m.X(), y = m.Y();
        const int64_t value = _s.CatalogPrice(type) >> 3;
        // the footprint from the catalogue size: the plane 2 x 2, harvester, trailer and truck 1 x 2 turned
        // to 2 x 1 when heading east or west, the rest 1 x 1
        const bool across = m.Heading() == 2 || m.Heading() == 6;
        const int len = Cells(type);
        const int w = type == 8 ? 2 : across ? len : 1, cells = type == 8 ? 2 : across ? 1 : len;
        Remove(i);
        _s.scalars["asset_machinery"] = std::max<int64_t>(0, _s.scalars["asset_machinery"] - _s.CatalogPrice(type));
        _s.SetCatalogOwn(type, std::max(0, _s.CatalogOwn(type) - 1));
        _sim.Credit(type, value);
        _s.scalars["yearly_income"] += value;
        _sim.PlaySound("explode.voc", x, y);
        if (!_sim.StructureAt(x, y))
        {
            // outside a building its footprint gets the dust puff, which leaves bare ground when it ends
            for (int dx = 0; dx < w; ++dx)
                for (int dy = 0; dy < cells; ++dy)
                    if (_s.world.In(x + dx, y + dy))
                    {
                        MapCell& c = _s.world.At(x + dx, y + dy);
                        c.base = static_cast<uint16_t>((c.base & 0xF800) | Simulation::GroundTileFor(_sim.SoilWetness(), c));
                        _sim.Puff(x + dx, y + dy);
                    }
            _sim.Notify("A machine has been lost: it was worn out.");
        }
    }
}

void Machines::ReleaseField(int fi)
{
    Field f = _s.FieldAt(fi);
    for (int o : { kLead, kImpl })
    {
        const int m = f.Get16(o);
        WorkWear(m, 2);
        if (m > 0 && m < 127 && Machine(_s.machines, m).InUse())
            SendHome(m);
        f.Set16(o, 0);
    }
    if (_truck.count(fi))
    {
        SendHome(_truck[fi]);
        _truck.erase(fi);
    }
    _sweepDir.erase(fi);
}

// ---------------------------------------------------------------- barrel delivery
// barrel record: +0 item type, +2 flags (0x20 in use), +4 state (1 requested, 2 machine on its way, 3 carrying),
// +5/+6 cell, +9 / +0x0A age (weekly), +13/+14 destination; the carrying tractor is kept by the engine. The Move tool
// does not use this (it moves a barrel at once); a state set in a loaded save still runs here
void Machines::StepBarrels()
{
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 20;
        if (!(Rd8(_s.objects20, b + 2) & 0x20))
            continue;
        const int state = Rd8(_s.objects20, b + 4);
        const int bx = Rd8(_s.objects20, b + 5), by = Rd8(_s.objects20, b + 6);
        if (state == 1)
        {
            if (const int t = Dispatch(kTractor, bx, by))
            {
                _barrelMachine[i] = t;
                Machine m(_s.machines, t);
                m.SetTarget(bx, by);
                m.SetFlags(m.Flags() | 0x02);
                Wr8(_s.objects20, b + 4, 2);
            }
        }
        else if (state == 2 || state == 3)
        {
            const int t = _barrelMachine.count(i) ? _barrelMachine[i] : 0;
            if (t <= 0 || t >= 127 || !Machine(_s.machines, t).InUse())
            {
                Wr8(_s.objects20, b + 4, 1); // the tractor went away: ask again
                continue;
            }
            Machine m(_s.machines, t);
            if (Drive(m))
                continue;
            if (state == 2)
            {
                // picked up: the barrel leaves the map and rides on the tractor
                MapCell& c = _s.world.At(bx, by);
                if ((c.overlay & 0x7FF) >= 912 && (c.overlay & 0x7FF) <= 915)
                {
                    c.base &= static_cast<uint16_t>(~0x800);
                    c.overlay &= 0xF800;
                    Stamp(m);
                }
                m.SetTarget(Rd8(_s.objects20, b + 13), Rd8(_s.objects20, b + 14));
                m.SetFlags(m.Flags() | 0x02);
                Wr8(_s.objects20, b + 4, 3);
            }
            else
            {
                // set down beside the tractor's stop, on the destination if it is free
                int dx = Rd8(_s.objects20, b + 13), dy = Rd8(_s.objects20, b + 14);
                if (_s.world.At(dx, dy).HasObject())
                {
                    dx = m.X();
                    dy = std::min(_s.world.size - 1, m.Y() + 1);
                }
                MapCell& c = _s.world.At(dx, dy);
                if (!c.HasObject())
                {
                    c.base |= 0x800;
                    c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | (915 - (Rd16(_s.objects20, b) - 0xDC)));
                }
                Wr8(_s.objects20, b + 5, dx);
                Wr8(_s.objects20, b + 6, dy);
                Wr8(_s.objects20, b + 4, 0);
                _barrelMachine.erase(i);
                SendHome(t);
            }
        }
    }
}

void Machines::Tick(bool ultra)
{
    // machines act every second timer tick at the running speeds, every tick at Ultra
    if (!ultra && (++_tick & 1))
        return;
    for (int i = 1; i < 98; ++i)
    {
        Field f = _s.FieldAt(i);
        if (f.InUse() && (f.State() == 3 || f.State() == 5 || (f.State() >= 6 && f.State() <= 9)))
            StepField(i);
    }
    StepBarrels();
    // machines travelling on their own (home runs)
    for (int i = 1; i < 127; ++i)
    {
        Machine m(_s.machines, i);
        if (!m.InUse() || !(m.Flags() & 0x02) || (m.Flags() & 0x08))
            continue;
        // with the route planner an implement hitched to its lead rides behind it and is parked when the lead gets
        // home; driven on its own as well, it and the lead put each other back on every step and never arrived
        // (user findings F25 / F37: machines busy for weeks, so they could not be sold)
        if (smartDriving && IsImplement(m.Type()) && m.Partner() && Machine(_s.machines, m.Partner()).Partner() == i)
            continue;
        if (Drive(m))
            continue;
        // arrived home: unhitch, park, release
        if (m.Partner())
        {
            Machine p(_s.machines, m.Partner());
            Lift(p);
            m.SetPartner(0);
            p.SetPartner(0);
            p.SetPos(p.HomeX(), p.HomeY());
            p.SetFlags(p.Flags() & ~(0x01 | 0x08 | 0x02));
            if (p.Leased())
            {
                p.SetFlags(0x40); // a returned leased machine: only its leased bit is left
                _s.machineCount = std::max(0, _s.machineCount - 1);
            }
            else if (!Store(p.index))
                Stamp(p);
        }
        m.SetFlags(m.Flags() & ~(0x01 | 0x02));
        const int home = _s.world.At(m.X(), m.Y()).BaseTile();
        if (m.Leased())
        {
            Lift(m);
            m.SetFlags(0x40); // leased machines disappear when their job ends (only the leased bit is left)
            _s.machineCount = std::max(0, _s.machineCount - 1);
        }
        else if (!Store(i) && !IsGround(home) && !IsRoad(home))
            Lift(m); // parked inside a building that does not take machines
    }
}

} // namespace osf
