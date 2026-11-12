// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/WindowsEdition.h"

#include "core/Paths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

namespace osf {

namespace {

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

uint16_t U16(const std::vector<uint8_t>& d, size_t o) { return o + 2 <= d.size() ? static_cast<uint16_t>(d[o] | (d[o + 1] << 8)) : 0; }
uint32_t U32(const std::vector<uint8_t>& d, size_t o) { return o + 4 <= d.size() ? U16(d, o) | (static_cast<uint32_t>(U16(d, o + 2)) << 16) : 0; }
void Put16(std::vector<uint8_t>& d, int v)
{
    d.push_back(static_cast<uint8_t>(v & 0xFF));
    d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

std::string Upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// ---------------------------------------------------------------- the program's resources (16-bit Windows "NE")
// The header's resource table: an alignment shift, then for each type an id (0 ends the table), a count, four
// reserved bytes and per resource its offset and length (in alignment units), flags, id and four reserved bytes.
// An id with the top bit set is a number, otherwise the offset of a length-prefixed name from the table's start.
using Resources = std::map<std::pair<int, std::string>, std::vector<uint8_t>>; // (type, name or "#id") -> bytes

bool ReadResources(const std::vector<uint8_t>& exe, Resources& out)
{
    if (exe.size() < 0x40 || exe[0] != 'M' || exe[1] != 'Z')
        return false;
    const size_t ne = U32(exe, 0x3C);
    if (ne + 0x40 > exe.size() || exe[ne] != 'N' || exe[ne + 1] != 'E')
        return false;
    const size_t table = ne + U16(exe, ne + 0x24);
    const int shift = U16(exe, table);
    auto name = [&](int id) -> std::string {
        if (id & 0x8000)
            return "#" + std::to_string(id & 0x7FFF);
        const size_t at = table + static_cast<size_t>(id);
        if (at >= exe.size())
            return {};
        const size_t len = exe[at];
        return at + 1 + len <= exe.size() ? Upper(std::string(exe.begin() + static_cast<long>(at) + 1, exe.begin() + static_cast<long>(at + 1 + len))) : std::string();
    };
    size_t p = table + 2;
    while (p + 8 <= exe.size())
    {
        const int type = U16(exe, p);
        if (type == 0)
            break;
        const int count = U16(exe, p + 2);
        p += 8;
        for (int i = 0; i < count && p + 12 <= exe.size(); ++i, p += 12)
        {
            const size_t off = static_cast<size_t>(U16(exe, p)) << shift, len = static_cast<size_t>(U16(exe, p + 2)) << shift;
            if (off + len > exe.size())
                continue;
            out[{ type & 0x8000 ? type & 0x7FFF : -1, name(U16(exe, p + 6)) }] =
                std::vector<uint8_t>(exe.begin() + static_cast<long>(off), exe.begin() + static_cast<long>(off + len));
        }
    }
    return !out.empty();
}

constexpr int kCursorType = 1, kBitmapType = 2;

// A 16-colour (or 1-bit) device-independent bitmap: one colour index per pixel, top row first, and its colour table.
struct Dib
{
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    std::array<std::array<uint8_t, 3>, 16> colours{};
    uint8_t At(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h ? px[static_cast<size_t>(y) * w + x] : 0; }
    void Set(int x, int y, uint8_t v)
    {
        if (x >= 0 && y >= 0 && x < w && y < h)
            px[static_cast<size_t>(y) * w + x] = v;
    }
};

// header size, width, height (bottom-up when positive), planes, bits per pixel, ..., colours used; then the colour
// table (blue, green, red, 0) and the rows, each padded to four bytes
bool DecodeDib(const std::vector<uint8_t>& d, size_t at, Dib& out, int rows = 0)
{
    const uint32_t hs = U32(d, at);
    if (hs < 40 || at + hs > d.size())
        return false;
    const int w = static_cast<int>(U32(d, at + 4)), h = static_cast<int>(static_cast<int32_t>(U32(d, at + 8))), bpp = U16(d, at + 14);
    if (w <= 0 || h == 0 || (bpp != 4 && bpp != 1))
        return false;
    const int colours = U32(d, at + 32) ? static_cast<int>(U32(d, at + 32)) : 1 << bpp;
    const int height = rows ? rows : std::abs(h);
    const size_t stride = static_cast<size_t>((w * bpp + 31) / 32) * 4, bits = at + hs + 4 * static_cast<size_t>(colours);
    if (bits + stride * static_cast<size_t>(height) > d.size())
        return false;
    out.w = w;
    out.h = height;
    for (int i = 0; i < 16 && i < colours; ++i)
        out.colours[static_cast<size_t>(i)] = { d[at + hs + 4 * i + 2], d[at + hs + 4 * i + 1], d[at + hs + 4 * i] };
    out.px.assign(static_cast<size_t>(w) * height, 0);
    for (int y = 0; y < height; ++y)
    {
        const size_t row = bits + stride * static_cast<size_t>(h > 0 ? height - 1 - y : y);
        for (int x = 0; x < w; ++x)
            out.px[static_cast<size_t>(y) * w + x] = bpp == 4 ? static_cast<uint8_t>((d[row + x / 2] >> (x % 2 ? 0 : 4)) & 15)
                                                              : static_cast<uint8_t>((d[row + x / 8] >> (7 - x % 8)) & 1);
    }
    return true;
}

// ---------------------------------------------------------------- the DOS formats the engine reads
// four planes one after another (plane k = bit k of the colour), rows of w / 8 bytes, the leftmost pixel in the top bit
void AppendPlanes(std::vector<uint8_t>& out, const Dib& p, int x0, int y0, int w, int h)
{
    for (int k = 0; k < 4; ++k)
        for (int y = 0; y < h; ++y)
            for (int bx = 0; bx < w / 8; ++bx)
            {
                uint8_t b = 0;
                for (int i = 0; i < 8; ++i)
                    if ((p.At(x0 + bx * 8 + i, y0 + y) >> k) & 1)
                        b |= static_cast<uint8_t>(0x80 >> i);
                out.push_back(b);
            }
}

// a window picture (.bmp): width in bytes, height, 4 planes, 0; then the planes
std::vector<uint8_t> PlanarBitmap(const Dib& p)
{
    std::vector<uint8_t> out;
    Put16(out, p.w / 8);
    Put16(out, p.h);
    Put16(out, 4);
    Put16(out, 0);
    AppendPlanes(out, p, 0, 0, p.w / 8 * 8, p.h);
    return out;
}

// a full-screen picture (.int): 640 x 480, each row as its four planes of 80 bytes
std::vector<uint8_t> FullScreen(const Dib& p, int ox, int oy)
{
    std::vector<uint8_t> out;
    out.reserve(153600);
    for (int y = 0; y < 480; ++y)
        for (int k = 0; k < 4; ++k)
            for (int bx = 0; bx < 80; ++bx)
            {
                uint8_t b = 0;
                for (int i = 0; i < 8; ++i)
                    if ((p.At(bx * 8 + i - ox, y - oy) >> k) & 1)
                        b |= static_cast<uint8_t>(0x80 >> i);
                out.push_back(b);
            }
    return out;
}

// 48 bytes, 6-bit values
std::vector<uint8_t> PaletteFile(const Dib& p)
{
    std::vector<uint8_t> out;
    for (const auto& c : p.colours)
        for (uint8_t v : c)
            out.push_back(static_cast<uint8_t>(v >> 2));
    return out;
}

// 16 x 16 tiles of a sheet 40 tiles wide, from tile row `firstRow`, 128 bytes each
std::vector<uint8_t> Tiles(const Dib& sheet, int firstRow, int count)
{
    std::vector<uint8_t> out;
    for (int t = 0; t < count; ++t)
        AppendPlanes(out, sheet, (t % 40) * 16, (firstRow + t / 40) * 16, 16, 16);
    return out;
}

// faces of w x h side by side, one after another
std::vector<uint8_t> Faces(const Dib& strip, int w, int h)
{
    std::vector<uint8_t> out;
    for (int x = 0; x + w <= strip.w; x += w)
        AppendPlanes(out, strip, x, 0, w, h);
    return out;
}

// A sprite (.spr) from a strip of cells, each frame's picture followed by its mask (black = drawn, white = see-through):
// frames, height, bytes a row, bytes a plane, 0x1FF; then per frame the mask plane (1 = drawn) and the four planes.
std::vector<uint8_t> Sprite(const Dib& strip, int w, int h, int frames)
{
    std::vector<uint8_t> out;
    const int bpr = w / 8;
    Put16(out, frames);
    Put16(out, h);
    Put16(out, bpr);
    Put16(out, bpr * h);
    Put16(out, 0x1FF);
    for (int f = 0; f < frames; ++f)
    {
        const int x0 = 2 * f * w;
        for (int y = 0; y < h; ++y)
            for (int bx = 0; bx < bpr; ++bx)
            {
                uint8_t b = 0;
                for (int i = 0; i < 8; ++i)
                    if (strip.At(x0 + w + bx * 8 + i, y) == 0)
                        b |= static_cast<uint8_t>(0x80 >> i);
                out.push_back(b);
            }
        AppendPlanes(out, strip, x0, 0, w, h);
    }
    return out;
}

// The font (egafont.fnt): 120 characters from the space, 32 bytes each (the 8 rows four times), the leftmost pixel in
// the top bit. The program's font sheet has 40 cells of 8 x 8 a row, black ink, the space in cell 120.
std::vector<uint8_t> Font(const Dib& sheet)
{
    std::vector<uint8_t> out;
    for (int c = 0; c < 120; ++c)
    {
        const int cell = 120 + c, x0 = (cell % 40) * 8, y0 = (cell / 40) * 8;
        std::array<uint8_t, 8> rows{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (sheet.At(x0 + x, y0 + y) == 0)
                    rows[static_cast<size_t>(y)] |= static_cast<uint8_t>(0x80 >> x);
        for (int k = 0; k < 4; ++k)
            out.insert(out.end(), rows.begin(), rows.end());
    }
    return out;
}

// LZSS as the engine reads it, all literals: a flag byte 0xFF before every eight bytes
std::vector<uint8_t> StoredLzss(const std::vector<uint8_t>& raw)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < raw.size(); i += 8)
    {
        out.push_back(0xFF);
        out.insert(out.end(), raw.begin() + static_cast<long>(i), raw.begin() + static_cast<long>(std::min(raw.size(), i + 8)));
    }
    return out;
}

// ---------------------------------------------------------------- pictures the Windows edition does not have
// The file window (filegenr.bmp, 208 x 208): the Windows edition uses the system's file dialogs. It is made from the
// frame of its 208 x 208 Examine window, a name box, a list box with a scroll bar of the arrow buttons and the
// CLOSE button face, at the places the engine's file window draws its names and takes its clicks.
Dib FileWindow(const Dib& frame, const Dib& font, const Dib& buttons, const Dib& up, const Dib& down)
{
    Dib p = frame;
    const int mid = p.h / 2;
    int left = 0, right = p.w - 1, bottom = p.h - 1;
    while (left < p.w / 2 && p.At(left, mid) != 7)
        ++left;
    while (right > p.w / 2 && p.At(right, mid) != 7)
        --right;
    while (bottom > mid && p.At(p.w / 2, bottom) != 7)
        --bottom;
    // the sides run up to the top (the title bar is drawn above the picture), the inside is cleared
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x)
            if (y <= bottom)
                p.Set(x, y, x < left || x > right ? frame.At(x, mid) : 7);
    auto sunken = [&](int x0, int y0, int x1, int y1) {
        for (int k = 0; k < 2; ++k)
        {
            for (int x = x0 + k; x <= x1 - k; ++x)
            {
                p.Set(x, y0 + k, 8);
                p.Set(x, y1 - k, 15);
            }
            for (int y = y0 + k; y <= y1 - k; ++y)
            {
                p.Set(x0 + k, y, 8);
                p.Set(x1 - k, y, 15);
            }
        }
    };
    auto label = [&](int x, int y, const char* s) {
        for (; *s; ++s, x += 8)
        {
            const int cell = 120 + (static_cast<unsigned char>(*s) - 0x20);
            for (int gy = 0; gy < 8; ++gy)
                for (int gx = 0; gx < 8; ++gx)
                    if (font.At((cell % 40) * 8 + gx, (cell / 40) * 8 + gy) == 0)
                        p.Set(x + gx, y + gy, 0);
        }
    };
    auto paste = [&](const Dib& src, int sx, int sy, int w, int h, int x, int y) {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                p.Set(x + i, y + j, src.At(sx + i, sy + j));
    };
    label(22, 13, "FILE NAME");
    sunken(22, 22, 121, 33);
    label(22, 45, "DIRECTORY");
    sunken(22, 54, 126, 185);
    // scroll bar: arrows at the top and bottom, a dotted track between, a black outline
    paste(up, 0, 0, 16, 16, 128, 56);
    paste(down, 0, 0, 16, 16, 128, 168);
    for (int y = 72; y < 168; ++y)
        for (int x = 128; x < 144; ++x)
            p.Set(x, y, (x + y) % 2 ? 15 : 8);
    for (int y = 56; y < 184; ++y)
    {
        p.Set(128, y, 0);
        p.Set(143, y, 0);
    }
    paste(buttons, 15 * 48, 0, 48, 24, 144, 56); // CLOSE
    return p;
}

// the edit palette (editeasy.lzs, 48 x 208): the program's tool bar has the twelve tools as 24 x 24 icons in a row,
// in the order of the original's two-column grid
std::vector<uint8_t> EditPalette(const Dib& bar)
{
    Dib p;
    p.w = 48;
    p.h = 208;
    p.px.assign(48 * 208, 7);
    for (int i = 0; i < 12; ++i)
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 24; ++x)
                p.Set(24 * (i % 2) + x, 24 * (i / 2) + y, bar.At(24 * i + x, y));
    std::vector<uint8_t> raw;
    Put16(raw, 6);
    Put16(raw, 208);
    Put16(raw, 4);
    Put16(raw, 1);
    AppendPlanes(raw, p, 0, 0, 48, 208);
    return StoredLzss(raw);
}

// The cursors (cursors.til): six 16 x 16 cursors, each a 128-byte mask block (1 = shown) and a 128-byte image block
// (1 = white), rows of 2 bytes stored four times; then the hot spots. The program's cursors are 32 x 32 with an
// image (XOR) and a mask (AND, 1 = see-through), in the order open hand, closed hand, magnifier, scissors, dollar
// (placing), spray can.
bool Cursors(const Resources& res, std::vector<uint8_t>& out)
{
    static const char* const kNames[6] = { "#2", "#3", "#1", "#6", "#5", "#4" };
    std::vector<uint8_t> hot;
    for (const char* n : kNames)
    {
        const auto it = res.find({ kCursorType, n });
        if (it == res.end() || it->second.size() < 4 + 40 + 8)
            return false;
        const std::vector<uint8_t>& c = it->second;
        hot.push_back(static_cast<uint8_t>(std::min<int>(15, U16(c, 0))));
        hot.push_back(static_cast<uint8_t>(std::min<int>(15, U16(c, 2))));
        // the image and the mask: one bitmap of twice the height, the image first
        const int w = static_cast<int>(U32(c, 8)), h2 = static_cast<int>(U32(c, 12));
        Dib both;
        if (!DecodeDib(c, 4, both, h2))
            return false;
        // the stored rows are bottom-up over both halves: the mask is the upper half of the decoded picture
        const int h = h2 / 2;
        (void)w;
        std::array<uint8_t, 32> mask{}, image{};
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const bool see = both.At(x, y) != 0, white = both.At(x, h + y) != 0;
                const int bit = 0x80 >> (x % 8);
                if (!see)
                    mask[static_cast<size_t>(y * 2 + x / 8)] |= static_cast<uint8_t>(bit);
                if (white)
                    image[static_cast<size_t>(y * 2 + x / 8)] |= static_cast<uint8_t>(bit);
            }
        for (int k = 0; k < 4; ++k)
            out.insert(out.end(), mask.begin(), mask.end());
        for (int k = 0; k < 4; ++k)
            out.insert(out.end(), image.begin(), image.end());
    }
    out.insert(out.end(), hot.begin(), hot.end());
    return true;
}

// The interface tiles the Windows edition left empty (one flat colour; it used the system's title bars and scroll
// bars): the title bar (active 1, 2 and 75, inactive 118, 119 and 76, the close box 0), the scroll bar parts (3 to 11:
// a corner, the up arrow, the track, the thumb, the down arrow, then the same across with the left and right arrows)
// and the plain buttons 194 and 195. They are drawn here in the Windows colours, the arrows from the program's arrow
// buttons.
void InterfaceTiles(std::vector<uint8_t>& tiles, const Dib& up, const Dib& down)
{
    using Tile = std::array<uint8_t, 256>;
    auto put = [&](int id, const Tile& t) {
        const size_t at = static_cast<size_t>(id) * 128;
        if (at + 128 > tiles.size())
            return;
        // only over a flat tile: each of its planes all set or all clear
        for (int k = 0; k < 128; ++k)
        {
            const uint8_t b = tiles[at + static_cast<size_t>(k)];
            if ((b != 0 && b != 0xFF) || b != tiles[at + static_cast<size_t>(k / 32 * 32)])
                return;
        }
        Dib flat;
        flat.w = 16;
        flat.h = 16;
        flat.px.assign(t.begin(), t.end());
        std::vector<uint8_t> enc;
        AppendPlanes(enc, flat, 0, 0, 16, 16);
        std::copy(enc.begin(), enc.end(), tiles.begin() + static_cast<long>(at));
    };
    auto raised = [] {
        Tile t;
        t.fill(7);
        for (int i = 0; i < 16; ++i)
        {
            t[static_cast<size_t>(i)] = t[static_cast<size_t>(i * 16)] = 15;
            t[static_cast<size_t>(14 * 16 + i)] = t[static_cast<size_t>(i * 16 + 14)] = 8;
            t[static_cast<size_t>(15 * 16 + i)] = t[static_cast<size_t>(i * 16 + 15)] = 0;
        }
        return t;
    };
    auto from = [](const Dib& d, bool across) {
        Tile t;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                t[static_cast<size_t>(y * 16 + x)] = across ? d.At(y, x) : d.At(x, y);
        return t;
    };
    auto caption = [](uint8_t colour, bool leftEnd, bool rightEnd) {
        Tile t;
        t.fill(colour);
        for (int i = 0; i < 16; ++i)
        {
            t[static_cast<size_t>(i)] = t[static_cast<size_t>(15 * 16 + i)] = 0;
            if (leftEnd)
                t[static_cast<size_t>(i * 16)] = 0;
            if (rightEnd)
                t[static_cast<size_t>(i * 16 + 15)] = 0;
        }
        return t;
    };
    Tile track;
    for (int i = 0; i < 256; ++i)
        track[static_cast<size_t>(i)] = (i / 16 + i % 16) % 2 ? 15 : 7;
    Tile corner = raised();
    for (int i = 4; i < 12; ++i)
    {
        corner[static_cast<size_t>(4 * 16 + i)] = corner[static_cast<size_t>(i * 16 + 4)] = 8;
        corner[static_cast<size_t>(11 * 16 + i)] = corner[static_cast<size_t>(i * 16 + 11)] = 15;
    }
    Tile close = caption(7, true, true);
    for (int i = 0; i < 16; ++i)
        close[static_cast<size_t>(i * 16)] = close[static_cast<size_t>(i * 16 + 15)] = 0;
    for (int x = 3; x <= 12; ++x)
    {
        close[static_cast<size_t>(7 * 16 + x)] = close[static_cast<size_t>(8 * 16 + x)] = 15;
        close[static_cast<size_t>(9 * 16 + x + 1)] = 0;
    }
    close[static_cast<size_t>(7 * 16 + 13)] = close[static_cast<size_t>(8 * 16 + 13)] = 0;
    put(0, close);
    put(1, caption(4, false, false));
    put(2, caption(4, false, true));
    put(75, caption(4, true, false));
    put(118, caption(8, false, false));
    put(119, caption(8, false, true));
    put(76, caption(8, true, false));
    put(3, corner);
    put(4, from(up, false));
    put(5, track);
    put(6, raised());
    put(7, from(down, false));
    put(8, from(up, true));
    put(9, track);
    put(10, raised());
    put(11, from(down, true));
    put(194, raised());
    put(195, raised());
}

// The overview colours (far_mapc.mbm): per tile two bytes, the top and bottom halves, each two pixels with the bits of
// four planes interleaved (plane p of the left pixel in bit 7 - 2p, of the right one in bit 6 - 2p). The Windows
// edition's table does not match its own tiles in that layout, so each quarter of a tile gets its most common colour.
std::vector<uint8_t> OverviewColours(const Dib& sheet, std::vector<uint8_t> table)
{
    table.resize(std::max<size_t>(table.size(), 880 * 2), 0);
    for (int t = 0; t < 880; ++t)
        for (int r = 0; r < 2; ++r)
        {
            uint8_t b = 0;
            for (int c = 0; c < 2; ++c)
            {
                std::array<int, 16> count{};
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        ++count[sheet.At((t % 40) * 16 + c * 8 + x, (t / 40) * 16 + r * 8 + y) & 15];
                const int v = static_cast<int>(std::max_element(count.begin(), count.end()) - count.begin());
                for (int p = 0; p < 4; ++p)
                    if ((v >> p) & 1)
                        b |= static_cast<uint8_t>(1 << (7 - 2 * p - c));
            }
            table[static_cast<size_t>(2 * t + r)] = b;
        }
    return table;
}

// A picture bank (.btl): u16 version, 16-byte name, u16 count, then records of u16 size + pixels 112 wide. The
// Windows edition's records hold two pixels a byte (the left one in the high half), rows from the top; they are
// turned into the DOS edition's four planes.
std::vector<uint8_t> PlanarBank(const std::vector<uint8_t>& win)
{
    if (win.size() < 20)
        return {};
    std::vector<uint8_t> out(win.begin(), win.begin() + 20);
    const int count = U16(win, 18);
    size_t o = 20;
    for (int i = 0; i < count && o + 2 <= win.size(); ++i)
    {
        const size_t size = U16(win, o);
        if (o + 2 + size > win.size())
            break;
        Dib p;
        p.w = 112;
        p.h = static_cast<int>(size / 56);
        p.px.resize(static_cast<size_t>(p.w) * p.h);
        for (size_t k = 0; k < p.px.size(); ++k)
            p.px[k] = static_cast<uint8_t>((win[o + 2 + k / 2] >> (k % 2 ? 0 : 4)) & 15);
        Put16(out, static_cast<int>(size));
        AppendPlanes(out, p, 0, 0, p.w, p.h);
        o += 2 + size;
    }
    return out;
}

// the see-through pixels of the masked tiles: 1 bit a pixel (1 = see-through), 32 bytes a tile
std::vector<uint8_t> TileMasks(const Dib& sheet, int firstRow, int count)
{
    std::vector<uint8_t> out;
    for (int t = 0; t < count; ++t)
        for (int y = 0; y < 16; ++y)
            for (int bx = 0; bx < 2; ++bx)
            {
                uint8_t b = 0;
                for (int i = 0; i < 8; ++i)
                    if (sheet.At((t % 40) * 16 + bx * 8 + i, (firstRow + t / 40) * 16 + y) == 15)
                        b |= static_cast<uint8_t>(0x80 >> i);
                out.push_back(b);
            }
    return out;
}

} // namespace

bool IsWindowsEdition(const std::string& gameDir)
{
    return FindFileNoCase(gameDir, "WSIMFARM.EXE").has_value();
}

bool MakeWindowsEntries(const std::string& gameDir, const std::map<std::string, std::vector<uint8_t>>& archive,
                        std::map<std::string, std::vector<uint8_t>>& out, std::string& error)
{
    auto archived = [&](const char* name) {
        const auto it = archive.find(name);
        return it == archive.end() ? std::vector<uint8_t>() : it->second;
    };
    for (const char* bank : { "alltiles.btl", "wethgrnd.btl", "wethsky.btl" })
        if (const std::vector<uint8_t> b = archived(bank); !b.empty())
            out[bank] = PlanarBank(b);
    const auto exePath = FindFileNoCase(gameDir, "WSIMFARM.EXE");
    Resources res;
    if (!exePath || !ReadResources(ReadFile(*exePath), res))
    {
        error = "WSIMFARM.EXE cannot be read";
        return false;
    }
    std::map<std::string, Dib> pic;
    // The pictures' colour tables differ a little (most have the standard Windows colours with dark ones at 128, some
    // at 191, the down arrows have grey and light grey the other way round): every picture is put in the colours of
    // the tile sheet (the standard ones), each of its colours as the nearest one there. The two title pictures keep
    // their own colours (they are drawn with their own palettes).
    Dib standard;
    {
        const auto r = res.find({ kBitmapType, "WTILE" });
        if (r == res.end() || !DecodeDib(r->second, 0, standard))
        {
            error = "WSIMFARM.EXE lacks the tile sheet";
            return false;
        }
    }
    auto bitmap = [&](const std::string& name) -> const Dib* {
        auto it = pic.find(name);
        if (it == pic.end())
        {
            const auto r = res.find({ kBitmapType, name });
            Dib d;
            if (r == res.end() || !DecodeDib(r->second, 0, d))
                return nullptr;
            if (name != "SFTITLE" && name != "PRESENTS")
            {
                std::array<uint8_t, 16> to{};
                for (size_t i = 0; i < 16; ++i)
                {
                    int best = 1 << 30;
                    for (size_t j = 0; j < 16; ++j)
                    {
                        int dist = 0;
                        for (size_t k = 0; k < 3; ++k)
                            dist += (d.colours[i][k] - standard.colours[j][k]) * (d.colours[i][k] - standard.colours[j][k]);
                        if (dist < best)
                        {
                            best = dist;
                            to[i] = static_cast<uint8_t>(j);
                        }
                    }
                }
                for (uint8_t& v : d.px)
                    v = to[v & 15];
                d.colours = standard.colours;
            }
            it = pic.emplace(name, std::move(d)).first;
        }
        return &it->second;
    };
    std::vector<std::string> missing;
    auto need = [&](const std::string& name) -> const Dib* {
        const Dib* d = bitmap(name);
        if (!d)
            missing.push_back(name);
        return d;
    };

    // window pictures
    static const std::pair<const char*, const char*> kWindows[] = {
        { "about.bmp", "ABOUTWNDO" },     { "airpwndo.bmp", "AIRPWNDO" },  { "bankterm.bmp", "BANKTERM" }, { "bankhelp.bmp", "BANKHELP" },
        { "buywndo.bmp", "BUYWNDO" },     { "cashflow.bmp", "CASHFLOW" },  { "choscrop.bmp", "CHOSECROP" }, { "cntlbtnh.bmp", "#2" },
        { "evaluate.bmp", "EVALUATE" },   { "examine.bmp", "EXAMINE" },    { "farmburo.bmp", "FARMEXPERT" }, { "fdstatus.bmp", "FIELDSTATUS" },
        { "futures.bmp", "FUTURES" },     { "maphelp.bmp", "MAPHELP" },    { "mapwndo.bmp", "MAPWNDO" },   { "popup.bmp", "POPUP" },
        { "sellwndo.bmp", "SELLWNDO" },   { "twnchoic.bmp", "TOWNCHOICE" }, { "weather.bmp", "WEATHER" },   { "lzrdlogo.bmp", "LIZARDLOGO" },
    };
    for (const auto& [dos, win] : kWindows)
        if (const Dib* d = need(win))
            out[dos] = PlanarBitmap(*d);

    // full-screen pictures with their own colours; the region choice and terrain design pictures are windows in the
    // Windows edition, placed where the DOS edition's screens have them
    if (const Dib* d = need("SFTITLE"))
    {
        out["sftitle.int"] = FullScreen(*d, 0, 0);
        out["sftitle.pal"] = PaletteFile(*d);
    }
    if (const Dib* d = need("PRESENTS"))
    {
        out["prsnt480.int"] = FullScreen(*d, 0, 0);
        out["prsnt480.pal"] = PaletteFile(*d);
    }
    if (const Dib* d = need("SCENARIO"))
    {
        out["scenario.int"] = FullScreen(*d, 80, 16);
        out["scenario.pal"] = PaletteFile(*d);
    }
    if (const Dib* d = need("DEFWORLD"))
        out["defworld.int"] = FullScreen(*d, 129, 34);

    // tiles: the sheet holds the 880 plain tiles in 22 rows of 40, then 200 masked tiles in 5 rows and their masks
    if (const Dib* d = need("WTILE"))
    {
        out["simfarm.pal"] = PaletteFile(*d);
        out["ega16til.til"] = Tiles(*d, 0, 880);
        if (const Dib *up = need("UUARROW"), *down = need("UDARROW"); up && down)
            InterfaceTiles(out["ega16til.til"], *up, *down);
        out["mskdtile.til"] = Tiles(*d, 22, 200);
        out["mskdtile.msk"] = TileMasks(*d, 27, 200);
        out["far_mapc.mbm"] = OverviewColours(*d, archived("far_mapc.mbm"));
    }
    // the engine's own interface colours (Colour in Gfx.h) in the Windows colours its pictures show them in
    out["uicolour.map"] = { 0, 11, 14, 1, 9, 4, 12, 3, 2, 2, 3, 7, 7, 8, 8, 15 };

    if (const Dib* d = need("BUTTONS"))
        out["buttons.til"] = Faces(*d, 48, 24);
    if (const Dib* d = need("FIELDGRAPH"))
        out["fldgrph.til"] = Faces(*d, 48, 24);
    if (const Dib* d = need("WEATHERVANE"))
        out["wethvane.til"] = Faces(*d, 32, 32);
    if (const Dib* d = need("#134"))
        out["editeasy.lzs"] = EditPalette(*d);
    if (const Dib* d = need("FFONT"))
        out["egafont.fnt"] = Font(*d);
    {
        const Dib *frame = need("EXAMINE"), *font = need("FFONT"), *buttons = need("BUTTONS"), *up = need("UUARROW"), *down = need("UDARROW");
        if (frame && font && buttons && up && down)
            out["filegenr.bmp"] = PlanarBitmap(FileWindow(*frame, *font, *buttons, *up, *down));
    }

    // sprites
    if (const Dib* d = need("AIRPLANE"))
        out["airplane.spr"] = Sprite(*d, 32, 32, 8);
    if (const Dib* d = need("APSHADOW"))
        out["apshadow.spr"] = Sprite(*d, 32, 32, 8);
    if (const Dib* d = need("TORNADO"))
        out["tornado.spr"] = Sprite(*d, 48, 48, 4);
    if (const Dib* d = need("LOCUSTS"))
        out["swarm.spr"] = Sprite(*d, 64, 40, 4);
    if (const Dib* d = need("COWMAN"))
    {
        // one dance of five frames (the DOS edition has three)
        const std::vector<uint8_t> dance = Sprite(*d, 64, 88, 5);
        out["cowman1.spr"] = dance;
        out["cowman2.spr"] = dance;
        out["cowman3.spr"] = dance;
    }
    {
        std::vector<uint8_t> cur;
        if (Cursors(res, cur))
            out["cursors.til"] = std::move(cur);
        else
            missing.push_back("cursors");
    }

    // sounds (.WAV) and songs (.MID) under the DOS names
    if (const auto sound = FindFileNoCase(gameDir, "SOUND"))
    {
        static const std::pair<const char*, const char*> kFiles[] = {
            { "bird1.voc", "BIRD1.WAV" },     { "bird2.voc", "BIRD2.WAV" },     { "boos.voc", "BOOS.WAV" },         { "bulldoze.voc", "BULLDOZE.WAV" },
            { "cheers.voc", "CHEERS.WAV" },   { "click.voc", "CLICK.WAV" },     { "compbeep.voc", "COMPBEEP.WAV" }, { "cow1.voc", "COW1.WAV" },
            { "cow2.voc", "COW2.WAV" },       { "explode.voc", "EXPLODE2.WAV" }, { "flood.voc", "FLOOD2.WAV" },     { "horse1.voc", "HORSE1.WAV" },
            { "pig.voc", "PIG.WAV" },         { "plane.voc", "PLANE.WAV" },     { "plopitem.voc", "PLOPITEM.WAV" }, { "ploptool.voc", "PLOPTOOL.WAV" },
            { "rooster.voc", "ROOSTER.WAV" }, { "sheep1.voc", "SHEEP1.WAV" },   { "sheep2.voc", "SHEEP2.WAV" },     { "smack.voc", "SMACK.WAV" },
            { "swarm.voc", "SWARM.WAV" },     { "thunder.voc", "THUNDER.WAV" }, { "tornado.voc", "TORNADO2.WAV" },  { "uhoh.voc", "UHOH.WAV" },
            { "wind.voc", "WIND.WAV" },       { "alien.xmi", "ALIEN.MID" },     { "amain.xmi", "AMAIN.MID" },       { "carnival.xmi", "CARNIVAL.MID" },
            { "cowman.xmi", "COWMAN.MID" },   { "dfarm.xmi", "DFARM.MID" },     { "ffarm.xmi", "FFARM.MID" },       { "genfarm.xmi", "GENFARM.MID" },
            { "rodeo.xmi", "RODEO.MID" },
        };
        for (const auto& [dos, win] : kFiles)
        {
            // the setup program installs EXPLODE2.WAV, FLOOD2.WAV and TORNADO2.WAV without the 2
            std::string other = win;
            if (const auto two = other.find("2.WAV"); two != std::string::npos && other != "BIRD2.WAV" && other != "COW2.WAV" && other != "SHEEP2.WAV")
                other.erase(two, 1);
            if (auto path = FindFileNoCase(*sound, win); path || (path = FindFileNoCase(*sound, other)))
                out[dos] = ReadFile(*path);
        }
    }

    if (!missing.empty())
    {
        error = "WSIMFARM.EXE lacks the pictures";
        for (const auto& m : missing)
            error += " " + m;
        return false;
    }
    return true;
}

} // namespace osf
