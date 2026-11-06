// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/PixelArt.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace osf {

std::vector<uint8_t> CalmMask(const uint8_t* tile16)
{
    auto at = [&](int x, int y) { return tile16[std::clamp(y, 0, 15) * 16 + std::clamp(x, 0, 15)]; };
    std::vector<uint8_t> calm(256);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            // of the neighbour pairs in the 5 x 5 square around the pixel that touch the pixel's own colour, the
            // share that differ: a texture's scattered pixels differ in about half of theirs or more, a shape's
            // pixels in a few (only along its edge), whatever lies beside the shape
            const uint8_t own = at(x, y);
            int touch = 0, differ = 0;
            auto pair = [&](uint8_t a, uint8_t b) {
                if (a == own || b == own)
                {
                    ++touch;
                    differ += a != b;
                }
            };
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 1; ++i)
                {
                    pair(at(x + i, y + j), at(x + i + 1, y + j));
                    pair(at(x + j, y + i), at(x + j, y + i + 1));
                }
            calm[static_cast<size_t>(y * 16 + x)] = differ * 100 < touch * kBusyShare;
        }
    return calm;
}

std::vector<uint8_t> Double(const std::vector<uint8_t>& px, int s, const std::vector<uint8_t>* calm)
{
    std::vector<uint8_t> out(static_cast<size_t>(4 * s * s));
    auto at = [&](int x, int y) { return px[static_cast<size_t>(std::clamp(y, 0, s - 1) * s + std::clamp(x, 0, s - 1))]; };
    const int w = 2 * s;
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x)
        {
            const uint8_t p = at(x, y), up = at(x, y - 1), left = at(x - 1, y), right = at(x + 1, y), down = at(x, y + 1);
            const size_t o = static_cast<size_t>(2 * y) * w + 2 * x;
            if (calm && !(*calm)[static_cast<size_t>(y * s + x)])
            {
                out[o] = out[o + 1] = out[o + w] = out[o + w + 1] = p; // inside a texture: as it is
                continue;
            }
            const uint8_t ul = at(x - 1, y - 1), ur = at(x + 1, y - 1), dl = at(x - 1, y + 1), dr = at(x + 1, y + 1);
            // each quarter of the enlarged pixel: the neighbours on its two sides agree with each other, the pixel
            // is not inside a run of that colour along either line (else a straight edge would bend), and the
            // diagonal pixel of that corner has the colour too, so there is a solid area to round off: a dither
            // (the ground's scattered pixels) has the other colour there and stays as it is
            const uint8_t tl = (left == up && left != down && up != right && ul == up) ? up : p;
            const uint8_t tr = (up == right && up != left && right != down && ur == up) ? right : p;
            const uint8_t bl = (down == left && down != right && left != up && dl == left) ? left : p;
            const uint8_t br = (right == down && right != up && down != left && dr == down) ? down : p;
            out[o] = tl;
            out[o + 1] = tr;
            out[o + w] = bl;
            out[o + w + 1] = br;
        }
    return out;
}

namespace {

// sRGB <-> linear light, through tables
const std::array<float, 256>& ToLinear()
{
    static const std::array<float, 256> t = [] {
        std::array<float, 256> a{};
        for (int i = 0; i < 256; ++i)
        {
            const double c = i / 255.0;
            a[static_cast<size_t>(i)] = static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return a;
    }();
    return t;
}

uint8_t FromLinear(float v)
{
    static const std::array<uint8_t, 4097> t = [] {
        std::array<uint8_t, 4097> a{};
        for (int i = 0; i <= 4096; ++i)
        {
            const double l = i / 4096.0;
            const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
            a[static_cast<size_t>(i)] = static_cast<uint8_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255));
        }
        return a;
    }();
    return t[static_cast<size_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 4096))];
}

// the source pixels (of n) that output pixel i of m covers, with the share of each
struct Span
{
    int first = 0;
    std::vector<float> w;
};
std::vector<Span> Spans(int n, int m)
{
    std::vector<Span> spans(static_cast<size_t>(m));
    for (int i = 0; i < m; ++i)
    {
        const double a = static_cast<double>(i) * n / m, b = static_cast<double>(i + 1) * n / m;
        Span& s = spans[static_cast<size_t>(i)];
        s.first = static_cast<int>(std::floor(a));
        for (int k = s.first; k < b && k < n; ++k)
            s.w.push_back(static_cast<float>(std::min<double>(b, k + 1) - std::max<double>(a, k)));
    }
    return spans;
}

} // namespace

std::vector<uint32_t> SmoothTile(const uint8_t* tile16, const Palette& pal, bool transparent7, int size)
{
    std::vector<uint8_t> px(tile16, tile16 + 256);
    const std::vector<uint8_t> calm16 = CalmMask(tile16);
    std::vector<uint8_t> calm32(1024);
    for (int i = 0; i < 1024; ++i)
        calm32[static_cast<size_t>(i)] = calm16[static_cast<size_t>((i / 64) * 16 + (i % 32) / 2)];
    px = Double(Double(px, 16, &calm16), 32, &calm32); // 64 x 64
    constexpr int n = 64;
    // an object's outline (a tile with see-through parts): its silhouette is rounded on its own, from the shape
    // alone, so a speckled object (a rock's shading, a tree's leaves) still gets a smooth outline while its inside
    // stays as drawn. Where the rounded silhouette covers a see-through pixel it takes the nearest colour of the
    // object; where it cuts a pixel away, that pixel becomes see-through
    std::vector<uint8_t> edge16(256, 0);
    if (transparent7)
    {
        std::vector<uint8_t> solid(256);
        for (int i = 0; i < 256; ++i)
            solid[static_cast<size_t>(i)] = tile16[i] != 7;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                for (int d = 0; d < 4; ++d)
                {
                    const int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                    if (nx >= 0 && ny >= 0 && nx < 16 && ny < 16 && solid[static_cast<size_t>(ny * 16 + nx)] != solid[static_cast<size_t>(y * 16 + x)])
                        edge16[static_cast<size_t>(y * 16 + x)] = 1;
                }
        const std::vector<uint8_t> solid64 = Double(Double(solid, 16), 32);
        const std::vector<uint8_t> before = px;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                uint8_t& c = px[static_cast<size_t>(y * n + x)];
                if (!solid64[static_cast<size_t>(y * n + x)])
                    c = 7;
                else if (c == 7)
                {
                    int best = 1 << 30;
                    for (int j = -3; j <= 3; ++j)
                        for (int i = -3; i <= 3; ++i)
                        {
                            const int xx = x + i, yy = y + j;
                            if (xx < 0 || yy < 0 || xx >= n || yy >= n)
                                continue;
                            const uint8_t o = before[static_cast<size_t>(yy * n + xx)];
                            if (o != 7 && i * i + j * j < best)
                            {
                                best = i * i + j * j;
                                c = o;
                            }
                        }
                }
            }
    }
    const auto& lin = ToLinear();
    // every 64 x 64 pixel once in linear light, premultiplied (see-through pixels add nothing, so no colour
    // bleeds in from them), and whether it belongs to a shape
    std::vector<std::array<float, 4>> src(static_cast<size_t>(n) * n);
    std::vector<float> shape(static_cast<size_t>(n) * n);
    for (int i = 0; i < n * n; ++i)
    {
        const uint8_t c = px[static_cast<size_t>(i)];
        const bool clear = transparent7 && c == 7;
        src[static_cast<size_t>(i)] = clear ? std::array<float, 4>{ 0, 0, 0, 0 }
                                            : std::array<float, 4>{ lin[pal[c][0]], lin[pal[c][1]], lin[pal[c][2]], 1 };
        const size_t s16 = static_cast<size_t>((i / n / 4) * 16 + (i % n) / 4);
        shape[static_cast<size_t>(i)] = calm16[s16] || edge16[s16]; // an outline is blended softly too
    }
    const std::vector<Span> spans = Spans(n, size);
    // the soft blend for shapes: a tent kSoftness output pixels wider than the pixel on each side
    const float scale = static_cast<float>(n) / static_cast<float>(size), reach = scale * (0.5f + kSoftness);
    std::vector<uint32_t> out(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            // the pixel's own area (a box), and how much of it is shape
            std::array<float, 4> box{};
            float total = 0, calm = 0;
            const Span& sy = spans[static_cast<size_t>(y)];
            const Span& sx = spans[static_cast<size_t>(x)];
            for (size_t j = 0; j < sy.w.size(); ++j)
                for (size_t i = 0; i < sx.w.size(); ++i)
                {
                    const float wgt = sy.w[j] * sx.w[i];
                    const size_t k = static_cast<size_t>(sy.first + static_cast<int>(j)) * n + sx.first + static_cast<int>(i);
                    for (int q = 0; q < 4; ++q)
                        box[static_cast<size_t>(q)] += wgt * src[k][static_cast<size_t>(q)];
                    calm += wgt * shape[k];
                    total += wgt;
                }
            for (float& v : box)
                v /= total;
            calm /= total;
            std::array<float, 4> mix = box;
            if (calm > 0 && kSoftness > 0)
            {
                // the tent around the pixel's centre (pixels past the tile's edge repeat the edge)
                std::array<float, 4> tent{};
                float sum = 0;
                const float cx = (x + 0.5f) * scale, cy = (y + 0.5f) * scale;
                const int y0 = static_cast<int>(std::floor(cy - reach)), y1 = static_cast<int>(std::ceil(cy + reach));
                const int x0 = static_cast<int>(std::floor(cx - reach)), x1 = static_cast<int>(std::ceil(cx + reach));
                for (int yy = y0; yy <= y1; ++yy)
                {
                    const float wy = 1 - std::fabs(yy + 0.5f - cy) / reach;
                    if (wy <= 0)
                        continue;
                    for (int xx = x0; xx <= x1; ++xx)
                    {
                        const float wx = 1 - std::fabs(xx + 0.5f - cx) / reach;
                        if (wx <= 0)
                            continue;
                        const size_t k = static_cast<size_t>(std::clamp(yy, 0, n - 1)) * n + std::clamp(xx, 0, n - 1);
                        for (int q = 0; q < 4; ++q)
                            tent[static_cast<size_t>(q)] += wx * wy * src[k][static_cast<size_t>(q)];
                        sum += wx * wy;
                    }
                }
                for (int q = 0; q < 4; ++q)
                    mix[static_cast<size_t>(q)] = box[static_cast<size_t>(q)] * (1 - calm) + tent[static_cast<size_t>(q)] / sum * calm;
            }
            uint32_t v = 0;
            const float a = mix[3];
            if (a > 0.5f / 255)
            {
                const uint32_t alpha = static_cast<uint32_t>(std::lround(255.0f * std::min(1.0f, a)));
                v = (alpha << 24) | (static_cast<uint32_t>(FromLinear(mix[0] / a)) << 16) | (static_cast<uint32_t>(FromLinear(mix[1] / a)) << 8) |
                    FromLinear(mix[2] / a);
            }
            out[static_cast<size_t>(y) * size + x] = v;
        }
    return out;
}

std::vector<uint32_t> PlainTile(const uint8_t* tile16, const Palette& pal, bool transparent7, int size)
{
    std::vector<uint32_t> out(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const uint8_t c = tile16[(y * 16 / size) * 16 + x * 16 / size];
            out[static_cast<size_t>(y) * size + x] =
                (transparent7 && c == 7) ? 0 : (0xFFu << 24) | (pal[c][0] << 16) | (pal[c][1] << 8) | pal[c][2];
        }
    return out;
}

} // namespace osf
