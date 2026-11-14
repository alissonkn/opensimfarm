// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/PixelArt.h"

#include <algorithm>
#include <cmath>

namespace osf {

namespace {

inline int Al(uint32_t p) { return static_cast<int>(p >> 24); }
inline int Rd(uint32_t p) { return static_cast<int>((p >> 16) & 0xFF); }
inline int Gr(uint32_t p) { return static_cast<int>((p >> 8) & 0xFF); }
inline int Bl(uint32_t p) { return static_cast<int>(p & 0xFF); }

// brightness for the rules that prefer the darker colour (see-through counts as darkest)
inline int Luma(uint32_t p) { return Al(p) == 0 ? -1 : Rd(p) * 2126 + Gr(p) * 7152 + Bl(p) * 722; }

inline bool AnyEq3(uint32_t e, uint32_t a, uint32_t b, uint32_t c) { return e == a || e == b || e == c; }
inline bool AllEq2(uint32_t e, uint32_t a, uint32_t b) { return e == a && e == b; }
inline bool AllEq3(uint32_t e, uint32_t a, uint32_t b, uint32_t c) { return e == a && e == b && e == c; }
inline bool AllEq4(uint32_t e, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return e == a && e == b && e == c && e == d; }
inline bool NoneEq2(uint32_t e, uint32_t a, uint32_t b) { return e != a && e != b; }
inline bool NoneEq4(uint32_t e, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return e != a && e != b && e != c && e != d; }

} // namespace

std::vector<uint32_t> Double(const std::vector<uint32_t>& src, int w, int h)
{
    const int W = w * 2;
    std::vector<uint32_t> out(static_cast<size_t>(W) * h * 2);
    auto at = [&](int x, int y) { return src[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            // A B C        the pixel E becomes J K
            // D E F                            L M
            // G H I        P, Q, R, S: two pixels up, left, right and down
            const uint32_t A = at(x - 1, y - 1), B = at(x, y - 1), C = at(x + 1, y - 1);
            const uint32_t D = at(x - 1, y), E = at(x, y), F = at(x + 1, y);
            const uint32_t G = at(x - 1, y + 1), H = at(x, y + 1), I = at(x + 1, y + 1);
            uint32_t J = E, K = E, L = E, M = E;
            if (!(A == E && B == E && C == E && D == E && F == E && G == E && H == E && I == E))
            {
                const uint32_t P = at(x, y - 2), S = at(x, y + 2), Q = at(x - 2, y), R = at(x + 2, y);
                const int Bl = Luma(B), Dl = Luma(D), El = Luma(E), Fl = Luma(F), Hl = Luma(H);

                // 1:1 slopes
                if ((D == B && D != H && D != F) && (El >= Dl || E == A) && AnyEq3(E, A, C, G) && (El < Dl || A != D || E != P || E != Q))
                    J = D;
                if ((B == F && B != D && B != H) && (El >= Bl || E == C) && AnyEq3(E, A, C, I) && (El < Bl || C != B || E != P || E != R))
                    K = B;
                if ((H == D && H != F && H != B) && (El >= Hl || E == G) && AnyEq3(E, A, G, I) && (El < Hl || G != H || E != S || E != Q))
                    L = H;
                if ((F == H && F != B && F != D) && (El >= Fl || E == I) && AnyEq3(E, C, G, I) && (El < Fl || I != H || E != R || E != S))
                    M = F;

                // thin lines crossing
                if (E != F && AllEq4(E, C, I, D, Q) && AllEq2(F, B, H) && F != at(x + 3, y))
                    K = M = F;
                if (E != D && AllEq4(E, A, G, F, R) && AllEq2(D, B, H) && D != at(x - 3, y))
                    J = L = D;
                if (E != H && AllEq4(E, G, I, B, P) && AllEq2(H, D, F) && H != at(x, y + 3))
                    L = M = H;
                if (E != B && AllEq4(E, A, C, H, S) && AllEq2(B, D, F) && B != at(x, y - 3))
                    J = K = B;

                // tips of triangles
                if (Bl < El && AllEq4(E, G, H, I, S) && NoneEq4(E, A, D, C, F))
                    J = K = B;
                if (Hl < El && AllEq4(E, A, B, C, P) && NoneEq4(E, D, G, I, F))
                    L = M = H;
                if (Fl < El && AllEq4(E, A, D, G, Q) && NoneEq4(E, B, C, I, H))
                    K = M = F;
                if (Dl < El && AllEq4(E, C, F, I, R) && NoneEq4(E, B, A, G, H))
                    J = L = D;

                // 2:1 slopes
                if (H != B)
                {
                    if (H != A && H != E && H != C)
                    {
                        if (AllEq3(H, G, F, R) && NoneEq2(H, D, at(x + 2, y - 1)))
                            L = M;
                        if (AllEq3(H, I, D, Q) && NoneEq2(H, F, at(x - 2, y - 1)))
                            M = L;
                    }
                    if (B != I && B != G && B != E)
                    {
                        if (AllEq3(B, A, F, R) && NoneEq2(B, D, at(x + 2, y + 1)))
                            J = K;
                        if (AllEq3(B, C, D, Q) && NoneEq2(B, F, at(x - 2, y + 1)))
                            K = J;
                    }
                }
                if (F != D)
                {
                    if (D != I && D != E && D != C)
                    {
                        if (AllEq3(D, A, H, S) && NoneEq2(D, B, at(x + 1, y + 2)))
                            J = L;
                        if (AllEq3(D, G, B, P) && NoneEq2(D, H, at(x + 1, y - 2)))
                            L = J;
                    }
                    if (F != E && F != A && F != G)
                    {
                        if (AllEq3(F, C, H, S) && NoneEq2(F, B, at(x - 1, y + 2)))
                            K = M;
                        if (AllEq3(F, I, B, P) && NoneEq2(F, H, at(x - 1, y - 2)))
                            M = K;
                    }
                }
            }
            uint32_t* o = &out[static_cast<size_t>(y) * 2 * W + static_cast<size_t>(x) * 2];
            o[0] = J;
            o[1] = K;
            o[W] = L;
            o[W + 1] = M;
        }
    return out;
}

std::vector<uint32_t> Upscale(const std::vector<uint32_t>& src, int w, int h, int factor)
{
    if (factor <= 1 || w <= 0 || h <= 0)
        return src;
    std::vector<uint32_t> big = Double(src, w, h);
    if (factor == 2)
        return big;
    big = Double(big, w * 2, h * 2);
    return factor == 4 ? big : Resample(big, w * 4, h * 4, w * factor, h * factor);
}

std::vector<uint32_t> Resample(const std::vector<uint32_t>& src, int w, int h, int outW, int outH)
{
    if (outW == w && outH == h)
        return src;
    std::vector<uint32_t> out(static_cast<size_t>(outW) * outH);
    for (int y = 0; y < outH; ++y)
    {
        const double y0 = static_cast<double>(y) * h / outH, y1 = static_cast<double>(y + 1) * h / outH;
        for (int x = 0; x < outW; ++x)
        {
            const double x0 = static_cast<double>(x) * w / outW, x1 = static_cast<double>(x + 1) * w / outW;
            double acc[4] = {}, area = 0;
            for (int sy = static_cast<int>(y0); sy < y1 && sy < h; ++sy)
            {
                const double wy = std::min<double>(y1, sy + 1) - std::max<double>(y0, sy);
                for (int sx = static_cast<int>(x0); sx < x1 && sx < w; ++sx)
                {
                    const double wgt = wy * (std::min<double>(x1, sx + 1) - std::max<double>(x0, sx));
                    const uint32_t p = src[static_cast<size_t>(sy) * w + sx];
                    const double a = Al(p) * wgt;
                    acc[0] += a;
                    acc[1] += Rd(p) * a;
                    acc[2] += Gr(p) * a;
                    acc[3] += Bl(p) * a;
                    area += wgt;
                }
            }
            uint32_t v = 0;
            if (acc[0] > 0)
                v = (static_cast<uint32_t>(std::lround(acc[0] / area)) << 24) | (static_cast<uint32_t>(std::lround(acc[1] / acc[0])) << 16) |
                    (static_cast<uint32_t>(std::lround(acc[2] / acc[0])) << 8) | static_cast<uint32_t>(std::lround(acc[3] / acc[0]));
            out[static_cast<size_t>(y) * outW + x] = v;
        }
    }
    return out;
}

std::vector<uint32_t> UpscalePicture(const uint8_t* px, int w, int h, const Palette& pal, int seeThrough, int outW, int outH)
{
    std::vector<uint32_t> argb = PlainPicture(px, w, h, pal, seeThrough, w, h);
    if (w <= 0 || h <= 0 || outW <= 0 || outH <= 0)
        return std::vector<uint32_t>(static_cast<size_t>(std::max(0, outW)) * std::max(0, outH));
    const int need = std::max((outW + w - 1) / w, (outH + h - 1) / h);
    // doubled once or twice, then each pixel repeated: an exact multiple when the size allows one
    const int doubled = need >= 4 && need % 2 == 0 && need % 4 != 0 ? 2 : need >= 3 ? 4 : need == 2 ? 2 : 1;
    const int repeat = std::max(1, (need + doubled - 1) / doubled);
    std::vector<uint32_t> big = doubled > 1 ? Double(argb, w, h) : argb;
    if (doubled == 4)
        big = Double(big, w * 2, h * 2);
    int bw = w * doubled, bh = h * doubled;
    if (repeat > 1)
    {
        std::vector<uint32_t> rep(static_cast<size_t>(bw) * repeat * bh * repeat);
        for (int y = 0; y < bh * repeat; ++y)
            for (int x = 0; x < bw * repeat; ++x)
                rep[static_cast<size_t>(y) * bw * repeat + x] = big[static_cast<size_t>(y / repeat) * bw + x / repeat];
        big.swap(rep);
        bw *= repeat;
        bh *= repeat;
    }
    return Resample(big, bw, bh, outW, outH);
}

std::vector<uint32_t> PlainPicture(const uint8_t* px, int w, int h, const Palette& pal, int seeThrough, int outW, int outH)
{
    std::vector<uint32_t> out(static_cast<size_t>(std::max(0, outW)) * std::max(0, outH));
    for (int y = 0; y < outH; ++y)
        for (int x = 0; x < outW; ++x)
        {
            const uint8_t c = px[static_cast<size_t>(y * h / outH) * w + x * w / outW];
            out[static_cast<size_t>(y) * outW + x] =
                (seeThrough >= 0 && c == seeThrough) ? 0 : (0xFFu << 24) | (pal[c & 15][0] << 16) | (pal[c & 15][1] << 8) | pal[c & 15][2];
        }
    return out;
}

} // namespace osf
