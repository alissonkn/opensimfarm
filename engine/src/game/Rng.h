// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>

namespace osf {

// The gameplay generator G1: 16-bit Galois LFSR, polynomial 0x3500,
// period 16,383. Callers use the low byte. Kept as in the original so the game has the same "feel"
// (e.g. the correlated direction/amount pair of the market step).
class Rng
{
public:
    explicit Rng(uint16_t seed = 0x2345) : _s(seed ? seed : 0x2345) {}
    uint16_t Next()
    {
        const uint16_t carry = _s & 1;
        _s = static_cast<uint16_t>(_s >> 1);
        if (carry)
            _s ^= 0x3500;
        return _s;
    }
    uint16_t State() const { return _s; }
    void SetState(uint16_t s) { _s = s ? s : 0x2345; }
    // monthly nudge: state += newMonth*4, reset to 1234 above 0x1FFF
    void MonthNudge(int newMonth)
    {
        int s = _s + newMonth * 4;
        _s = static_cast<uint16_t>(s > 0x1FFF ? 1234 : s);
    }

private:
    uint16_t _s;
};

// G3: bounded 8-bit LFSR (polynomial 0xB8) returning 0..n-1.
class Rng3
{
public:
    void SetState(uint8_t s) { _s = s; }
    uint8_t State() const { return _s; }
    int Bounded(int n)
    {
        int x = Step(_s);
        _s = static_cast<uint8_t>(x);
        while (x > n)
            x = Step(x);
        return x == n ? 0 : x;
    }

private:
    static int Step(int x) { return (x & 1) ? ((x >> 1) ^ 0xB8) : (x >> 1); }
    uint8_t _s = 0x33;
};

} // namespace osf
