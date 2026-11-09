// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Animals.h"
#include "core/Lang.h"
#include <cstdio>
#include <cstdlib>

#include "game/Simulation.h"

#include <climits>

namespace osf {

namespace {
constexpr int kDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr int kDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
constexpr int kHungerLimit[4] = { 50, 35, 56, 56 };
constexpr int kThirstLimit[4] = { 160, 160, 80, 64 };
constexpr int kBrokenFence = 0xC0;
size_t Rec(int i) { return static_cast<size_t>(i) * 32; }
} // namespace

bool Animals::Passable(int x, int y) const
{
    // the base tile must be a trough (175-176), ground (15-30), road (150-171), rock (535-538), tree
    // (131-146), crop (680-840), an open gate or broken fence (189, 191, 192, 193), or tiles 36 / 12; a cell holding an
    // object passes only when its overlay is 962 (the family bounds read from memory)
    if (!_s.world.In(x, y))
        return false;
    const MapCell& c = _s.world.At(x, y);
    const int id = c.BaseTile();
    const bool tile = (id >= 175 && id <= 176) || (id >= 15 && id <= 30) || (id >= 150 && id <= 171) || (id >= 535 && id <= 538) ||
                      (id >= 131 && id <= 146) || (id >= 680 && id <= 840) || id == 189 || id == 191 || id == 192 || id == 193 ||
                      id == 0x24 || id == 0x0C;
    if (!tile)
        return false;
    return !c.HasObject() || (c.overlay & 0x7FF) == 962;
}

void Animals::Lift(int x, int y)
{
    if (!_s.world.In(x, y))
        return;
    MapCell& c = _s.world.At(x, y);
    const int o = c.overlay & 0x7FF;
    if (c.HasObject() && ((o >= 946 && o <= 1009) || (o >= kKickPicture && o < kKickPicture + 4)))
    {
        c.base &= static_cast<uint16_t>(~0x800);
        c.overlay &= 0xF800;
    }
}

void Animals::Stamp(int i)
{
    const size_t b = Rec(i);
    const int x = Rd8(_s.animals, b), y = Rd8(_s.animals, b + 1);
    if (!_s.world.In(x, y))
        return;
    MapCell& c = _s.world.At(x, y);
    c.base |= 0x800;
    // a kicking animal shows its kick picture (0x36E + species record) wherever it walks
    const int pic = (Rd8(_s.animals, b + 2) & 8) ? kKickPicture + ((Rd16(_s.animals, b + 6) - 0xA0) & 3)
                                                 : TileFor(Rd16(_s.animals, b + 6) - 0xA0, Rd8(_s.animals, b + 4), Rd8(_s.animals, b + 0x12));
    c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | pic);
}

// a car or a machine ran into the animal: a smack, the kick picture and flag 8 with the time; the
// animal goes on walking with that picture for 640 ticks, then calls; a machine also clears the animal's
// female bit
void Animals::KickAt(int x, int y, bool machine)
{
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = Rec(i);
        const int fl = Rd8(_s.animals, b + 2);
        if (!(fl & 0x20) || Rd8(_s.animals, b) != x || Rd8(_s.animals, b + 1) != y)
            continue;
        if (fl & 8)
            return;
        _sim.PlaySound("smack.voc", x, y);
        if (std::getenv("OSF_KICKLOG")) // diagnostic for comparison runs
            std::fprintf(stderr, "kick %d/%d/%d animal %d sp %d at %d,%d %s%s\n", _s.cal.year, _s.cal.month + 1, _s.cal.week + 1, i,
                         Rd16(_s.animals, b + 6) - 0xA0, x, y, machine ? "machine" : "car", (fl & 2) ? " female" : "");
        Wr8(_s.animals, b + 2, (fl | 8) & (machine ? ~2 : 0xFF));
        Wr16(_s.animals, b + 0x1E, _tick & 0xFFFF);
        Stamp(i);
        return;
    }
}

void Animals::HerdCentres()
{
    // the original's herd centres, kept with its indexing slip: the sums and counts are addressed with
    // the raw species word (0xA0..0xA3) while the averaging reads slots 0..3, so in memory
    //   count bytes   block byte k (k = 0..3), zeroed;  species s counts into byte s - 1
    //   sum words     block byte 4 + 4k (x) / 6 + 4k (y), zeroed; species s adds into byte 4s
    // i.e. a species' sums land one slot lower, the horses' sums land on the four count bytes, and slot 3 gets none.
    // A slot whose count byte is above 0 (a signed byte) is divided by it and gets G2 & 3 per axis. The step reads
    // horse -> slot 2, cow -> slot 0, pig -> slot 1, sheep -> slot 3, so content sheep walk toward the
    // map corner and the others toward skewed points (seen live: slot 3 stays (0, 0), slot 2 holds the sheep sums)
    std::array<uint8_t, 20> m{}; // the 20-byte block of counts and sums
    auto addWord = [&](int o, int v) {
        if (o < 0) // the byte before the block: unused by the averaging
            return;
        const int w = (m[static_cast<size_t>(o)] | (m[static_cast<size_t>(o) + 1] << 8)) + v;
        m[static_cast<size_t>(o)] = static_cast<uint8_t>(w);
        m[static_cast<size_t>(o) + 1] = static_cast<uint8_t>(w >> 8);
    };
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = Rec(i);
        if (!(Rd8(_s.animals, b + 2) & 0x20))
            continue;
        const int sp = (Rd16(_s.animals, b + 6) - 0xA0) & 3;
        addWord(4 * sp, Rd8(_s.animals, b));
        addWord(4 * sp + 2, Rd8(_s.animals, b + 1));
        if (sp > 0)
            ++m[static_cast<size_t>(sp - 1)];
    }
    for (int k = 0; k < 4; ++k)
    {
        const size_t o = 4 + 4 * static_cast<size_t>(k);
        int x = m[o] | (m[o + 1] << 8), y = m[o + 2] | (m[o + 3] << 8);
        const int n = static_cast<int8_t>(m[static_cast<size_t>(k)]);
        if (n > 0)
        {
            x = x / n + (_sim.G2Step() & 3); // G2
            y = y / n + (_sim.G2Step() & 3);
        }
        _slotX[static_cast<size_t>(k)] = static_cast<int16_t>(x);
        _slotY[static_cast<size_t>(k)] = static_cast<int16_t>(y);
    }
    static const int kSlot[4] = { 2, 0, 1, 3 }; // horse, cow, pig, sheep
    for (int sp = 0; sp < 4; ++sp)
    {
        _cx[static_cast<size_t>(sp)] = _slotX[static_cast<size_t>(kSlot[sp])];
        _cy[static_cast<size_t>(sp)] = _slotY[static_cast<size_t>(kSlot[sp])];
    }
}

// heading toward a target: the sign pair of (animal - target) picks an entry of the
// eight neighbour steps, the heading is the opposite one; on the target itself the heading is north (0)
int Animals::HeadingTo(int x, int y, int tx, int ty)
{
    const int sx = (x > tx) - (x < tx), sy = (y > ty) - (y < ty);
    for (int k = 0; k < 8; ++k)
        if (kDx[k] == sx && kDy[k] == sy)
            return (k + 4) & 7;
    return 0;
}

bool Animals::NearestItem(int x, int y, int type, int& tx, int& ty, int& best) const
{
    // the nearest item record of a type that is not empty (amount 0 is skipped: an emptied hay pile or a dry trough is no
    // target), squared distance, a strictly nearer one wins
    bool found = false;
    for (int r = 1; r < 512; ++r)
    {
        const size_t b = static_cast<size_t>(r) * 12;
        if (!(Rd8(_s.items, b + 10) & 0x20) || Rd16(_s.items, b) != type || Rd16(_s.items, b + 2) == 0)
            continue;
        const int ix = Rd8(_s.items, b + 8), iy = Rd8(_s.items, b + 9);
        const int d = (ix - x) * (ix - x) + (iy - y) * (iy - y);
        if (d < best)
        {
            best = d;
            tx = ix;
            ty = iy;
            found = true;
        }
    }
    return found;
}

void Animals::Step(int i)
{
    // the original's animal step
    const size_t b = Rec(i);
    const int x = Rd8(_s.animals, b), y = Rd8(_s.animals, b + 1);
    const int sp = (Rd16(_s.animals, b + 6) - 0xA0) & 3;
    const int hay = Rd8(_s.animals, b + 0x0D), water = Rd8(_s.animals, b + 0x0E);
    int h = Rd8(_s.animals, b + 4) & 7;
    int tx = -1, ty = 0; // the target; off the map the animal turns toward it (none: (-1, 0))
    if ((Rd8(_s.animals, b + 2) & 8) && ((_tick - Rd16(_s.animals, b + 0x1E)) & 0xFFFF) > 640)
    {
        Wr8(_s.animals, b + 2, Rd8(_s.animals, b + 2) & ~8);
        static const char* const kCall[4][2] = { { "horse1.voc", "horse1.voc" }, { "cow1.voc", "cow2.voc" }, { "pig.voc", "pig.voc" }, { "sheep1.voc", "sheep2.voc" } };
        _sim.PlaySound(kCall[sp][_sim.G2Step() & 1], Rd8(_s.animals, b), Rd8(_s.animals, b + 1)); // G2
    }
    if (Rd8(_s.animals, b + 5) != 0)
    {
        // a random walk after a blocked step (or a purchase): a random heading for each remaining step
        h = _sim.G2Step() & 7; // G2
        Wr8(_s.animals, b + 5, Rd8(_s.animals, b + 5) - 1);
    }
    else if (water > (kHungerLimit[sp] >> 4))
    {
        // the nearest trough (item 299, not empty) or barn (0x44 / 0x45, nearer only); none: keep going
        int best = 1000000;
        NearestItem(x, y, 299, tx, ty, best);
        for (int cat : { 0x44, 0x45 })
        {
            int bd = INT_MAX, bx = -1, by = -1;
            for (int k = 1; k < 127; ++k)
            {
                const size_t sb = static_cast<size_t>(k) * 70;
                if (!(Rd8(_s.structures, sb + 2) & 0x20) || Rd16(_s.structures, sb) != cat)
                    continue;
                const int sx = Rd8(_s.structures, sb + 4), sy = Rd8(_s.structures, sb + 5);
                const int d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
                if (d < bd)
                {
                    bd = d;
                    bx = sx;
                    by = sy;
                }
            }
            if (bx >= 0 && bd < best)
            {
                best = bd;
                tx = bx;
                ty = by;
            }
        }
        if (tx != -1)
        {
            h = HeadingTo(x, y, tx, ty);
            if ((_sim.G1() & 0xF) == 3) // G1
                h = (h + 1) & 7;
        }
    }
    else if (hay > (kThirstLimit[sp] >> 4))
    {
        // the nearest hay pile (item 300); without one a random cell 2..9 inside the nearest field
        int best = INT_MAX;
        if (NearestItem(x, y, 300, tx, ty, best))
        {
            h = HeadingTo(x, y, tx, ty);
            if ((_sim.G1() & 0xF) == 6) // G1
                h = (h + 1) & 7;
        }
        else
        {
            int fx = -1, fy = -1;
            for (int f = 1; f < 98; ++f)
            {
                Field fl = _s.FieldAt(f);
                if (!fl.InUse())
                    continue;
                const int d = (fl.X() - x) * (fl.X() - x) + (fl.Y() - y) * (fl.Y() - y);
                if (d < best)
                {
                    best = d;
                    fx = fl.X();
                    fy = fl.Y();
                }
            }
            if (fx >= 0)
            {
                tx = fx + (_sim.G2Step() & 7) + 2; // G2
                ty = fy + (_sim.G2Step() & 7) + 2;
                h = HeadingTo(x, y, tx, ty);
            }
        }
    }
    else
    {
        // content: shy away from the pointer cell (when the map shows one), else back to the herd now and then
        // when more than 40 (squared) away from its centre; otherwise the animal keeps its heading
        tx = _cx[sp];
        ty = _cy[sp];
        const int dh = (_cx[sp] - x) * (_cx[sp] - x) + (_cy[sp] - y) * (_cy[sp] - y);
        const int dp = _pointerX < 0 ? INT_MAX : (_pointerX - x) * (_pointerX - x) + (_pointerY - y) * (_pointerY - y);
        if (dp < 0x32)
            h = (HeadingTo(x, y, _pointerX, _pointerY) + 4) & 7;
        else if (dh > 0x28 && (_sim.G2Step() & 7) < 4) // G2
            h = HeadingTo(x, y, _cx[sp], _cy[sp]);
    }
    Wr8(_s.animals, b + 4, h);
    const int nx = x + kDx[h], ny = y + kDy[h];
    if (!_s.world.In(nx, ny))
    {
        Wr8(_s.animals, b + 4, HeadingTo(x, y, tx, ty)); // the map edge: turn toward the target, no step
        return;
    }
    MapCell& dest = _s.world.At(nx, ny);
    const int id = dest.BaseTile();
    if ((id >= 460 && id <= 480) || id == 0xD9)
    {
        // a barn tile (or an empty storage slot): a barn takes the animal in, anything else holds it back
        const int st = _sim.StructureAt(nx, ny);
        const int cat = st ? Rd16(_s.structures, static_cast<size_t>(st) * 70) : 0;
        if (cat == 0x44 || cat == 0x45)
        {
            Lift(x, y);
            EnterBarn(i, st);
        }
        return;
    }
    if (!Passable(nx, ny))
    {
        // blocked: a needy animal may break a fence (1/128); then a random turn and eight random steps
        if (id >= 177 && id <= 187 && (hay > kThirstLimit[sp] / 2 || water > kHungerLimit[sp] / 2) && (_sim.G1() & 0x7F) == 5) // G1
        {
            dest.base = static_cast<uint16_t>((dest.base & 0xF800) | kBrokenFence);
            _sim.Notify(Tr("A fence has broken. Your livestock may run loose."));
        }
        Wr8(_s.animals, b + 4, (h + (_sim.G1() & 7)) & 7); // G1
        Wr8(_s.animals, b + 5, 8);
        Stamp(i);
        return;
    }
    Lift(x, y);
    Wr8(_s.animals, b, nx);
    Wr8(_s.animals, b + 1, ny);
    Stamp(i);
    // stepping on a crop of a non-orchard field: the field takes damage, the cell is bare and the animal has
    // grazed (its hay need is 0)
    if (id >= 680 && id <= 840)
        if (const int fi = _sim.FieldIndexAt(nx, ny))
        {
            Field f = _s.FieldAt(fi);
            if (!_s.crops[f.Crop() & 15].Orchard())
            {
                f.SetStress(std::min(65535, f.Stress() + 1024));
                dest.base = static_cast<uint16_t>((dest.base & 0xF824) | 0x24);
                Wr8(_s.animals, b + 0x0D, 0);
            }
        }
}

int Animals::AnimalAt(int x, int y) const
{
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = Rec(i);
        if (b + 32 <= _s.animals.size() && (Rd8(_s.animals, b + 2) & 0x20) && !(Rd8(_s.animals, b + 2) & 0x04) && Rd8(_s.animals, b) == x &&
            Rd8(_s.animals, b + 1) == y)
            return i;
    }
    return 0;
}

bool Animals::MoveTo(int i, int x, int y)
{
    const size_t b = Rec(i);
    if (!_s.world.In(x, y) || _s.world.At(x, y).HasObject() || !Passable(x, y))
        return false;
    Lift(Rd8(_s.animals, b), Rd8(_s.animals, b + 1));
    Wr8(_s.animals, b, x);
    Wr8(_s.animals, b + 1, y);
    Stamp(i);
    return true;
}

bool Animals::Place(int x, int y, int species)
{
    if (!Passable(x, y))
        return false;
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = Rec(i);
        if (Rd8(_s.animals, b + 2) & 0x20)
            continue;
        for (size_t o = 0; o < 32; ++o)
            _s.animals[b + o] = 0;
        Wr8(_s.animals, b, x);
        Wr8(_s.animals, b + 1, y);
        Wr8(_s.animals, b + 4, _sim.G2Step() & 7);                    // G2
        Wr8(_s.animals, b + 2, 0x20 | ((_sim.G1() & 1) ? 2 : 0)); // G1
        Wr16(_s.animals, b + 6, 0xA0 + (species & 3));
        Wr8(_s.animals, b + 8, 0xFF);
        Wr8(_s.animals, b + 5, 1); // one random step first
        Wr8(_s.animals, b + 0x0C, 1);
        _s.animalCount++;
        Stamp(i);
        return true;
    }
    return false;
}

bool Animals::FeedAt(int i, bool hay)
{
    // the first full trough (hay = false, counter +0x0E) or hay pile (counter +0x0D) within 2 cells: the counter's
    // value is eaten from it
    const size_t b = Rec(i);
    const int x = Rd8(_s.animals, b), y = Rd8(_s.animals, b + 1);
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
        {
            if (!_s.world.In(x + dx, y + dy))
                continue;
            const MapCell& c = _s.world.At(x + dx, y + dy);
            const int o = c.overlay & 0x7FF;
            const bool hit = hay ? (c.HasObject() && o >= 1020 && o <= 1024) : c.BaseTile() == 176;
            if (hit && _sim.Consume(x + dx, y + dy, hay ? 300 : 299, Rd8(_s.animals, b + (hay ? 0x0D : 0x0E))))
                return true;
        }
    return false;
}

void Animals::Weaken(int i)
{
    const size_t b = Rec(i);
    Wr8(_s.animals, b + 8, std::max(0, Rd8(_s.animals, b + 8) - 1));
}

void Animals::Needs(int i)
{
    // the original's per-step needs check: a sheltered animal is fed by the building, others drink at a trough and eat
    // at a hay pile (or graze) in reach; past half the species limit it complains and goes looking, past the limit it
    // dies. Counter +0x0E is cleared at a trough, so its messages are about water (the original shows "thirsty" for it);
    // +0x0D is cleared at hay or by grazing, so its messages are about food (the original: livestock cannot reach food)
    const size_t b = Rec(i);
    const int fl = Rd8(_s.animals, b + 2);
    const int sp = (Rd16(_s.animals, b + 6) - 0xA0) & 3;
    static const char* const kDeath[2][4] = { { N_("A horse died of thirst."), N_("A cow died of thirst."), N_("A pig died of thirst."),
                                                 N_("A sheep died of thirst.") },
                                               { N_("A horse starved."), N_("A cow starved."), N_("A pig starved."), N_("A sheep starved.") } };
    // why: 0 no water (counter +0x0E), 1 no food (+0x0D)
    auto die = [&](int why) {
        _sim.Notify(Tr(kDeath[why][sp]));
        _sim.RemoveAnimal(i);
    };
    if (Rd8(_s.animals, b + 0x0E) != 0)
    {
        if (fl & 4)
        {
            Wr8(_s.animals, b + 0x0E, 0);
            Wr8(_s.animals, b + 0x0D, 0);
            Wr8(_s.animals, b + 8, std::min(255, Rd8(_s.animals, b + 8) + 30));
        }
        else if (FeedAt(i, false))
            Wr8(_s.animals, b + 0x0E, 0);
        else
        {
            const int h = Rd8(_s.animals, b + 0x0E);
            if (h > (kHungerLimit[sp] >> 1) && h < kHungerLimit[sp])
            {
                Weaken(i); // every step in need costs one point of condition
                if (_tick - _lastThirsty > 2000)
                {
                    _sim.Notify(Tr("Your livestock are thirsty."));
                    _lastThirsty = _tick;
                }
            }
        }
        if (Rd8(_s.animals, b + 0x0E) > kHungerLimit[sp])
        {
            die(0);
            return;
        }
    }
    if (Rd8(_s.animals, b + 0x0D) != 0)
    {
        if (FeedAt(i, true))
            Wr8(_s.animals, b + 0x0D, 0);
        else if (Rd8(_s.animals, b + 0x0D) > (kThirstLimit[sp] >> 1))
        {
            Weaken(i);
            if (_tick - _lastHungry > 2000)
            {
                _sim.Notify(Tr("Your livestock are hungry."));
                _lastHungry = _tick;
            }
        }
        if (Rd8(_s.animals, b + 0x0D) > kThirstLimit[sp])
            die(1);
    }
}

bool Animals::EnterBarn(int i, int st)
{
    // the store routine for a 1 x 1 animal: the first free slot (columns outer, rows inner) gets the
    // animal's index, its species as content and its picture; the animal moves to that cell, flag 4, +0x10 = barn
    const size_t b = Rec(i), sb = static_cast<size_t>(st) * 70;
    const size_t rec = static_cast<size_t>(12 + Rd16(_s.structures, sb) - 0x40) * 18;
    const int W = _s.catalog[rec + 8], H = _s.catalog[rec + 9];
    const int type = Rd16(_s.animals, b + 6);
    for (int lx = 0; lx < W; ++lx)
        for (int ly = 0; ly < H; ++ly)
        {
            const size_t s = static_cast<size_t>(lx * 3 + ly);
            if (s >= 12 || Rd16(_s.structures, sb + 0x22 + 2 * s) != 0xFFFF)
                continue;
            Wr8(_s.structures, sb + 0x3A + s, i);
            Wr16(_s.structures, sb + 0x22 + 2 * s, type);
            Wr16(_s.structures, sb + 0x0A + 2 * s, 946 + 16 * ((type - 0xA0) & 3));
            Wr8(_s.animals, b, Rd8(_s.structures, sb + 4) + lx);
            Wr8(_s.animals, b + 1, Rd8(_s.structures, sb + 5) + ly);
            Wr8(_s.animals, b + 2, Rd8(_s.animals, b + 2) | 4);
            Wr16(_s.animals, b + 0x10, st);
            return true;
        }
    return false;
}

void Animals::LeaveBarn(int i)
{
    // out to the first walkable of the three cells left of the barn's corner, freeing its slot
    const size_t b = Rec(i);
    const int st = Rd16(_s.animals, b + 0x10);
    const size_t sb = static_cast<size_t>(st) * 70;
    const int bx = Rd8(_s.structures, sb + 4), by = Rd8(_s.structures, sb + 5);
    for (int k = 0; k < 3; ++k)
        if (Passable(bx - 1, by + k))
        {
            Unshelter(i);
            Wr8(_s.animals, b, bx - 1);
            Wr8(_s.animals, b + 1, by + k);
            return;
        }
}

void Animals::Unshelter(int i)
{
    const size_t b = Rec(i);
    if (!(Rd8(_s.animals, b + 2) & 4))
        return;
    const size_t sb = static_cast<size_t>(Rd16(_s.animals, b + 0x10)) * 70;
    const int lx = Rd8(_s.animals, b) - Rd8(_s.structures, sb + 4), ly = Rd8(_s.animals, b + 1) - Rd8(_s.structures, sb + 5);
    const int s = lx * 3 + ly;
    if (lx >= 0 && ly >= 0 && s < 12)
    {
        Wr16(_s.structures, sb + 0x0A + 2 * static_cast<size_t>(s), 0xD9);
        Wr16(_s.structures, sb + 0x22 + 2 * static_cast<size_t>(s), 0xFFFF);
        Wr8(_s.structures, sb + 0x3A + static_cast<size_t>(s), 0);
    }
    Wr8(_s.animals, b + 2, Rd8(_s.animals, b + 2) & ~4);
}

void Animals::Tick(int period)
{
    // the herd centres are recomputed when more than 320 ticks have passed since the last time, so every 321
    // ticks
    ++_tick;
    if (_tick - _herdStamp > 320)
    {
        HerdCentres();
        _herdStamp = _tick;
    }
    if (++_sinceStep < period)
        return;
    _sinceStep = 0;
    for (int i = 1; i < 256; ++i)
    {
        const int fl = Rd8(_s.animals, Rec(i) + 2);
        if (!(fl & 0x20))
            continue;
        if (fl & 4)
        {
            // in a barn: no step and no needs check, the water need is met, and one time in eight
            // the animal walks out west of the barn
            Wr8(_s.animals, Rec(i) + 0x0E, 0);
            if ((_sim.G2Step() & 7) == 5) // G2
                LeaveBarn(i);
            continue;
        }
        Step(i);
        Needs(i);
    }
}

} // namespace osf
