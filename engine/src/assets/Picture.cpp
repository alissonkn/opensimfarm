// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/Picture.h"

namespace osf {

static uint16_t U16(const std::vector<uint8_t>& d, size_t o) { return static_cast<uint16_t>(d[o] | (d[o + 1] << 8)); }

// Planes stored one after another: plane k starts at k * bytesPerRow * h.
static void DecodePlanes(const uint8_t* src, int bytesPerRow, int h, Picture& out)
{
    out.w = bytesPerRow * 8;
    out.h = h;
    out.px.assign(static_cast<size_t>(out.w) * h, 0);
    const size_t plane = static_cast<size_t>(bytesPerRow) * h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < out.w; ++x)
        {
            const size_t o = static_cast<size_t>(y) * bytesPerRow + x / 8;
            const int bit = 0x80 >> (x & 7);
            int v = 0;
            for (int k = 0; k < 4; ++k)
                if (src[k * plane + o] & bit)
                    v |= 1 << k;
            out.px[static_cast<size_t>(y) * out.w + x] = static_cast<uint8_t>(v);
        }
}

std::vector<uint8_t> DecodeLzss(const std::vector<uint8_t>& src)
{
    // Okumura LZSS: ring of 4096 bytes filled with spaces, writing from 4096 - 18, flag
    // byte LSB first, 1 = literal, 0 = (position low 8, position high 4 | length - 3)
    std::vector<uint8_t> out, ring(4096, ' ');
    size_t r = 4096 - 18, i = 0;
    unsigned flags = 0;
    while (i < src.size())
    {
        flags >>= 1;
        if (!(flags & 0x100))
        {
            flags = src[i++] | 0xFF00u;
            if (i >= src.size())
                break;
        }
        if (flags & 1)
        {
            const uint8_t c = src[i++];
            out.push_back(c);
            ring[r] = c;
            r = (r + 1) & 4095;
        }
        else
        {
            if (i + 1 >= src.size())
                break;
            const int pos = src[i] | ((src[i + 1] & 0xF0) << 4), len = (src[i + 1] & 0x0F) + 3;
            i += 2;
            for (int k = 0; k < len; ++k)
            {
                const uint8_t c = ring[static_cast<size_t>(pos + k) & 4095];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) & 4095;
            }
        }
    }
    return out;
}

bool DecodePlanarBitmap(const std::vector<uint8_t>& d, Picture& out)
{
    if (d.size() < 8)
        return false;
    const int wb = U16(d, 0), h = U16(d, 2), planes = U16(d, 4);
    if (planes != 4 || wb == 0 || h == 0 || d.size() < 8 + static_cast<size_t>(wb) * h * 4)
        return false;
    DecodePlanes(&d[8], wb, h, out);
    return true;
}

bool DecodeButtonFace(const std::vector<uint8_t>& d, int face, Picture& out, int widthBytes, int height)
{
    const size_t size = static_cast<size_t>(widthBytes) * height * 4;
    if (face < 0 || d.size() < (face + 1) * size)
        return false;
    DecodePlanes(&d[face * size], widthBytes, height, out);
    return true;
}

bool DecodeBankRecord(const std::vector<uint8_t>& d, int index, Picture& out)
{
    if (d.size() < 20)
        return false;
    const int count = U16(d, 18);
    size_t o = 20;
    for (int i = 0; i < count && o + 2 <= d.size(); ++i)
    {
        const size_t size = U16(d, o);
        if (o + 2 + size > d.size())
            return false;
        if (i == index)
        {
            const int h = static_cast<int>(size / (14 * 4));
            if (h == 0)
                return false;
            DecodePlanes(&d[o + 2], 14, h, out);
            return true;
        }
        o += 2 + size;
    }
    return false;
}

bool DecodeSprite(const std::vector<uint8_t>& d, int frame, Picture& out)
{
    if (d.size() < 10)
        return false;
    const int frames = U16(d, 0), h = U16(d, 2), bpr = U16(d, 4), plane = U16(d, 6);
    if (frames == 0 || frame < 0 || frame >= frames || plane != bpr * h || d.size() < 10 + static_cast<size_t>(frames) * 5 * plane)
        return false;
    const uint8_t* src = &d[10 + static_cast<size_t>(frame) * 5 * plane];
    DecodePlanes(src + plane, bpr, h, out);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < out.w; ++x)
            if (!(src[y * bpr + x / 8] & (0x80 >> (x & 7))))
                out.px[static_cast<size_t>(y) * out.w + x] = 255;
    return true;
}

bool DecodeFullScreen(const std::vector<uint8_t>& d, Picture& out)
{
    if (d.size() != 153600)
        return false;
    out.w = 640;
    out.h = 480;
    out.px.assign(640 * 480, 0);
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 640; ++x)
        {
            int v = 0;
            for (int k = 0; k < 4; ++k)
                if (d[static_cast<size_t>(y) * 320 + k * 80 + x / 8] & (0x80 >> (x & 7)))
                    v |= 1 << k;
            out.px[static_cast<size_t>(y) * 640 + x] = static_cast<uint8_t>(v);
        }
    return true;
}

bool DecodeFont(const std::vector<uint8_t>& d, Font8& out)
{
    if (d.size() < 32 * 95)
        return false;
    out = Font8{};
    for (int c = 0x20; c < 256; ++c)
    {
        const size_t o = static_cast<size_t>(c - 0x20) * 32;
        if (o + 8 > d.size())
            break;
        for (int r = 0; r < 8; ++r)
            out.glyphs[c][r] = d[o + r];
    }
    return true;
}

} // namespace osf
