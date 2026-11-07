// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include <algorithm>
#include "ui/Gfx.h"
#include "ui/PictureText.h"

#include <functional>
#include "ui/SmallFont.h"
#include <array>
#include <cctype>
#include <cmath>

#include "assets/Megadata.h"

#include <vector>

namespace osf {

namespace {
float g_mouseRatio = 1.0f;
int g_mouseOffset = 0;
}

void SetMouseRatio(float r) { g_mouseRatio = r > 0 ? r : 1.0f; }
float MouseRatio() { return g_mouseRatio; }
void SetMouseOffset(int windowPixels) { g_mouseOffset = windowPixels; }
int MouseOffset() { return g_mouseOffset; }
uint32_t MouseState(int* x, int* y)
{
    int mx = 0, my = 0;
    const uint32_t b = SDL_GetMouseState(&mx, &my);
    if (x)
        *x = static_cast<int>(std::floor(static_cast<float>(mx - g_mouseOffset) * g_mouseRatio));
    if (y)
        *y = static_cast<int>(std::floor(static_cast<float>(my) * g_mouseRatio)); // no frame along the top
    return b;
}


static constexpr int kTileColumns = 32;

SDL_Texture* Gfx::MakeTexture(const Picture& p, int transparent)
{
    std::vector<uint32_t> argb(static_cast<size_t>(p.w) * p.h);
    for (size_t i = 0; i < argb.size(); ++i)
    {
        const int c = p.px[i];
        if (c == transparent)
        {
            argb[i] = 0;
            continue;
        }
        argb[i] = 0xFF000000u | (_pal[c & 15][0] << 16) | (_pal[c & 15][1] << 8) | _pal[c & 15][2];
    }
    SDL_Texture* t = SDL_CreateTexture(_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, p.w, p.h);
    if (!t)
        return nullptr;
    SDL_UpdateTexture(t, nullptr, argb.data(), p.w * 4);
    SDL_SetTextureBlendMode(t, transparent >= 0 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    return t;
}


namespace {

// Latin-1 letters for the translations (an engine addition): the original font holds only ASCII, so accented
// letters are made from its own letters with marks drawn for OpenSimFarm. A small letter keeps its shape and gets
// the mark in the two free rows above it; a capital drops two of its repeated rows to make room for the mark.
enum Mark { kNoMark, kGrave, kAcute, kCirc, kTilde, kDiaer, kRing, kCedilla };
struct Composed
{
    unsigned char code;
    char base;
    Mark mark;
};
constexpr Composed kComposed[] = {
    { 0xC0, 'A', kGrave }, { 0xC1, 'A', kAcute }, { 0xC2, 'A', kCirc }, { 0xC3, 'A', kTilde }, { 0xC4, 'A', kDiaer },
    { 0xC5, 'A', kRing }, { 0xC7, 'C', kCedilla }, { 0xC8, 'E', kGrave }, { 0xC9, 'E', kAcute }, { 0xCA, 'E', kCirc },
    { 0xCB, 'E', kDiaer }, { 0xCC, 'I', kGrave }, { 0xCD, 'I', kAcute }, { 0xCE, 'I', kCirc }, { 0xCF, 'I', kDiaer },
    { 0xD1, 'N', kTilde }, { 0xD2, 'O', kGrave }, { 0xD3, 'O', kAcute }, { 0xD4, 'O', kCirc }, { 0xD5, 'O', kTilde },
    { 0xD6, 'O', kDiaer }, { 0xD9, 'U', kGrave }, { 0xDA, 'U', kAcute }, { 0xDB, 'U', kCirc }, { 0xDC, 'U', kDiaer },
    { 0xDD, 'Y', kAcute },
    { 0xE0, 'a', kGrave }, { 0xE1, 'a', kAcute }, { 0xE2, 'a', kCirc }, { 0xE3, 'a', kTilde }, { 0xE4, 'a', kDiaer },
    { 0xE5, 'a', kRing }, { 0xE7, 'c', kCedilla }, { 0xE8, 'e', kGrave }, { 0xE9, 'e', kAcute }, { 0xEA, 'e', kCirc },
    { 0xEB, 'e', kDiaer }, { 0xEC, 'i', kGrave }, { 0xED, 'i', kAcute }, { 0xEE, 'i', kCirc }, { 0xEF, 'i', kDiaer },
    { 0xF1, 'n', kTilde }, { 0xF2, 'o', kGrave }, { 0xF3, 'o', kAcute }, { 0xF4, 'o', kCirc }, { 0xF5, 'o', kTilde },
    { 0xF6, 'o', kDiaer }, { 0xF9, 'u', kGrave }, { 0xFA, 'u', kAcute }, { 0xFB, 'u', kCirc }, { 0xFC, 'u', kDiaer },
    { 0xFD, 'y', kAcute }, { 0xFF, 'y', kDiaer },
};
// the marks for the 8 x 8 font: two rows above the letter (the cedilla: one row below it)
constexpr uint8_t kMark8[8][2] = { { 0, 0 }, { 0x30, 0x18 }, { 0x0C, 0x18 }, { 0x3C, 0x66 }, { 0x76, 0xDC },
                                   { 0x66, 0x00 }, { 0x18, 0x18 }, { 0x18, 0x00 } };
// thinner marks for capitals, whose tops have no free row under the mark
constexpr uint8_t kMarkCap[8][2] = { { 0, 0 }, { 0x20, 0x10 }, { 0x04, 0x08 }, { 0x18, 0x24 }, { 0x3A, 0x5C },
                                     { 0x24, 0x00 }, { 0x18, 0x18 }, { 0x18, 0x00 } };
// and for the small font: four pixels wide (high nibble), drawn in the two rows above the letter
constexpr uint8_t kMarkSmall[8][2] = { { 0, 0 }, { 0xC0, 0x60 }, { 0x30, 0x60 }, { 0x60, 0x90 }, { 0x50, 0xA0 },
                                       { 0x00, 0x90 }, { 0x60, 0x60 }, { 0x60, 0x00 } };

const Composed* FindComposed(unsigned char c)
{
    for (const Composed& k : kComposed)
        if (k.code == c)
            return &k;
    return nullptr;
}

void AddLatin1(Font8& f)
{
    for (const Composed& k : kComposed)
    {
        const auto& base = f.glyphs[static_cast<unsigned char>(k.base)];
        std::array<uint8_t, 8> g = base;
        if (k.mark == kCedilla)
            g[7] = kMark8[kCedilla][0];
        else if (std::islower(static_cast<unsigned char>(k.base)))
        {
            g[0] = kMark8[k.mark][0]; // (replaces the dot of the i)
            g[1] = kMark8[k.mark][1];
        }
        else
        {
            // a capital keeps six of its seven rows (a pointed top goes first, as on the A, so the mark sits on a
            // flat top; then a row that repeats the one above it, the last such first): the mark's lower row is the
            // cell's top row and its upper row is drawn in the row above the cell (glyph 0x80 + mark, its row 7)
            std::vector<uint8_t> rows(base.begin(), base.begin() + 7);
            auto bits = [](uint8_t v) { int n = 0; for (; v; v &= static_cast<uint8_t>(v - 1)) ++n; return n; };
            if (bits(rows[0]) < bits(rows[1]))
                rows.erase(rows.begin());
            while (rows.size() > 6)
            {
                size_t drop = rows.size() / 2;
                for (size_t i = rows.size() - 1; i >= 1; --i)
                    if (rows[i] == rows[i - 1])
                    {
                        drop = i;
                        break;
                    }
                rows.erase(rows.begin() + static_cast<long>(drop));
            }
            g = {};
            g[0] = kMarkCap[k.mark][1];
            for (size_t r = 0; r < 6; ++r)
                g[1 + r] = rows[r];
            f.glyphs[0x80 + k.mark] = {};
            f.glyphs[0x80 + k.mark][7] = kMarkCap[k.mark][0];
            Gfx::SetCapitalMark(k.code, 0x80 + k.mark);
        }
        f.glyphs[k.code] = g;
    }
    auto flipped = [&](char c, bool mirror) {
        std::array<uint8_t, 8> g{};
        for (int r = 0; r < 7; ++r)
        {
            uint8_t v = f.glyphs[static_cast<unsigned char>(c)][6 - r], m = 0;
            for (int b = 0; b < 8 && mirror; ++b)
                if (v & (1 << b))
                    m |= static_cast<uint8_t>(0x80 >> b);
            g[r] = mirror ? static_cast<uint8_t>(m >> 1) : v;
        }
        return g;
    };
    f.glyphs[0xA1] = flipped('!', false);                                      // inverted exclamation mark
    f.glyphs[0xBF] = flipped('?', true);                                       // inverted question mark
    f.glyphs[0xB0] = { 0x38, 0x28, 0x38, 0, 0, 0, 0, 0 };                      // degree sign
    f.glyphs[0xAA] = { 0x3C, 0x6C, 0x3C, 0x00, 0x7C, 0, 0, 0 };                // feminine ordinal
    f.glyphs[0xBA] = { 0x38, 0x6C, 0x38, 0x00, 0x7C, 0, 0, 0 };                // masculine ordinal
    f.glyphs[0xAB] = { 0, 0x36, 0x6C, 0xD8, 0x6C, 0x36, 0, 0 };                // left guillemet
    f.glyphs[0xBB] = { 0, 0xD8, 0x6C, 0x36, 0x6C, 0xD8, 0, 0 };                // right guillemet
    f.glyphs[0xB7] = { 0, 0, 0, 0x18, 0x18, 0, 0, 0 };                         // middle dot
    f.glyphs[0xD7] = { 0, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0, 0 };                // multiplication sign
}

} // namespace

void Gfx::PrepareFont(Font8& font)
{
    AddLatin1(font);
    for (int c = 0; c < 256; ++c)
    {
        uint8_t bits = 0;
        for (const uint8_t row : font.glyphs[c])
            bits |= row;
        int l = 0, r = 7;
        while (l < 8 && !(bits & (0x80 >> l)))
            ++l;
        while (r > l && !(bits & (0x80 >> r)))
            --r;
        s_ink[c][0] = static_cast<uint8_t>(l < 8 ? l : 0);
        s_ink[c][1] = static_cast<uint8_t>(l < 8 ? r : 3);
    }
}

bool Gfx::Init(SDL_Renderer* r, const Megadata& mega, const MapGraphics& map)
{
    _r = r;
    _mega = &mega;
    _pal = map.palette;

    // font atlas: 16x16 grid of 8x8 glyphs, white on transparent (tinted per draw)
    Font8 font;
    if (!DecodeFont(mega.Read("egafont.fnt"), font))
        return false;
    PrepareFont(font);
    std::vector<uint32_t> glyphs(128 * 128, 0);
    for (int c = 0; c < 256; ++c)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (font.glyphs[c][y] & (0x80 >> x))
                    glyphs[static_cast<size_t>((c / 16) * 8 + y) * 128 + (c % 16) * 8 + x] = 0xFFFFFFFF;
    _font = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 128, 128);
    if (!_font)
        return false;
    SDL_UpdateTexture(_font, nullptr, glyphs.data(), 128 * 4);
    SDL_SetTextureBlendMode(_font, SDL_BLENDMODE_BLEND);

    if (!SetPlainTiles(map.plain))
        return false;
    Picture masked;
    masked.w = kTileColumns * 16;
    masked.h = (map.masked.count + kTileColumns - 1) / kTileColumns * 16;
    masked.px.assign(static_cast<size_t>(masked.w) * masked.h, 7);
    for (int t = 0; t < map.masked.count; ++t)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                masked.px[static_cast<size_t>((t / kTileColumns) * 16 + y) * masked.w + (t % kTileColumns) * 16 + x] =
                    map.masked.Tile(t)[y * 16 + x];
    _masked = MakeTexture(masked, 7);

    return _tiles != nullptr;
}

bool Gfx::SetPlainTiles(const TileSheet& plain)
{
    // plain tiles (icons used by the interface live in the same sheet as the map tiles)
    _plain = plain;
    Picture atlas;
    atlas.w = kTileColumns * 16;
    atlas.h = (plain.count + kTileColumns - 1) / kTileColumns * 16;
    atlas.px.assign(static_cast<size_t>(atlas.w) * atlas.h, 0);
    for (int t = 0; t < plain.count; ++t)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                atlas.px[static_cast<size_t>((t / kTileColumns) * 16 + y) * atlas.w + (t % kTileColumns) * 16 + x] = plain.Tile(t)[y * 16 + x];
    if (_tiles)
        SDL_DestroyTexture(_tiles);
    _tiles = MakeTexture(atlas, -1);

    _tileColour.resize(plain.count);
    for (int t = 0; t < plain.count; ++t)
    {
        int sum[3] = { 0, 0, 0 };
        for (int i = 0; i < 256; ++i)
            for (int k = 0; k < 3; ++k)
                sum[k] += _pal[plain.Tile(t)[i]][k];
        _tileColour[t] = 0xFF000000u | ((sum[0] / 256) << 16) | ((sum[1] / 256) << 8) | (sum[2] / 256);
    }
    return _tiles != nullptr;
}

void Gfx::SetPlayPalette(int which)
{
    if (which == _playPalette || !_mega)
        return;
    static const char* const names[3] = { "simfarm.pal", "rainpal.pal", "frostpal.pal" };
    const std::vector<uint8_t> pal = _mega->Read(names[std::clamp(which, 0, 2)]);
    if (pal.size() < 48)
        return;
    _playPalette = which;
    _pal = LoadPalette(pal);
    // everything drawn from palette indices is made again with the new colours; the masked tiles keep colour 7 as
    // their transparent colour, so they look the same
    for (auto& [k, t] : _pictures)
        if (t)
            SDL_DestroyTexture(t);
    _pictures.clear();
    const TileSheet plain = _plain;
    SetPlainTiles(plain);
}

Gfx::~Gfx()
{
    for (auto& [k, t] : _pictures)
        if (t)
            SDL_DestroyTexture(t);
    if (_font)
        SDL_DestroyTexture(_font);
    if (_tiles)
        SDL_DestroyTexture(_tiles);
    if (_masked)
        SDL_DestroyTexture(_masked);
    if (_pixels)
        SDL_DestroyTexture(_pixels);
}

void Gfx::DrawPixels(const std::vector<uint32_t>& argb, int w, int h, int x, int y, int dw, int dh)
{
    if (!_pixels || _pixelsW != w || _pixelsH != h)
    {
        if (_pixels)
            SDL_DestroyTexture(_pixels);
        _pixels = SDL_CreateTexture(_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        _pixelsW = w;
        _pixelsH = h;
    }
    if (!_pixels)
        return;
    SDL_UpdateTexture(_pixels, nullptr, argb.data(), w * 4);
    const SDL_Rect dst = R(x, y, dw, dh);
    SDL_RenderCopy(_r, _pixels, nullptr, &dst);
}

SDL_Texture* Gfx::PictureTexture(const std::string& key)
{
    auto it = _pictures.find(key);
    if (it != _pictures.end())
        return it->second;
    Picture p;
    bool ok = false;
    const auto hash = key.find('#');
    if (key.find('@') != std::string::npos)
        ok = false; // sprites below
    else if (const auto dollar = key.find(".til$"); dollar != std::string::npos)
    {
        // buttons.til and fldgrph.til: 48 x 24 faces; wethvane.til: the Weather window's two 32 x 32 wind-rotor frames
        const std::string file = key.substr(0, dollar + 4);
        const bool vane = file == "wethvane.til";
        ok = DecodeButtonFace(_mega->Read(file), std::stoi(key.substr(dollar + 5)), p, vane ? 4 : 6, vane ? 32 : 24);
    }
    else if (hash == std::string::npos && key.size() > 4 && key.substr(key.size() - 4) == ".lzs")
        ok = DecodePlanarBitmap(DecodeLzss(_mega->Read(key)), p);
    else if (hash == std::string::npos)
        ok = DecodePlanarBitmap(_mega->Read(key), p);
    else
        ok = DecodeBankRecord(_mega->Read(key.substr(0, hash)), std::stoi(key.substr(hash + 1)), p);
    if (key.find('@') != std::string::npos)
    {
        const auto at = key.find('@');
        ok = DecodeSprite(_mega->Read(key.substr(0, at)), std::stoi(key.substr(at + 1)), p);
    }
    SDL_Texture* t = ok ? MakeTexture(p, key.find('@') != std::string::npos ? 255 : -1) : nullptr;
    _pictures[key] = t;
    return t;
}

void Gfx::Fill(int x, int y, int w, int h, int c)
{
    SDL_SetRenderDrawBlendMode(_r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(_r, _pal[c][0], _pal[c][1], _pal[c][2], 255);
    const SDL_Rect rc = R(x, y, w, h);
    SDL_RenderFillRect(_r, &rc);
}

void Gfx::Frame(int x, int y, int w, int h, int c)
{
    Fill(x, y, w, 1, c);
    Fill(x, y + h - 1, w, 1, c);
    Fill(x, y, 1, h, c);
    Fill(x + w - 1, y, 1, h, c);
}

void Gfx::Bevel(int x, int y, int w, int h, bool raised)
{
    const int a = raised ? kWhite : kGrey, b = raised ? kGrey : kWhite;
    Fill(x, y, w, 1, a);
    Fill(x, y, 1, h, a);
    Fill(x, y + h - 1, w, 1, b);
    Fill(x + w - 1, y, 1, h, b);
}

void Gfx::Line(int x0, int y0, int x1, int y1, int c)
{
    SDL_SetRenderDrawColor(_r, _pal[c][0], _pal[c][1], _pal[c][2], 255);
    for (int o = 0; o < _scale; ++o)
        SDL_RenderDrawLine(_r, x0 * _scale + o, y0 * _scale, x1 * _scale + o, y1 * _scale);
    for (int o = 1; o < _scale; ++o)
        SDL_RenderDrawLine(_r, x0 * _scale, y0 * _scale + o, x1 * _scale, y1 * _scale + o);
}

void Gfx::DrawPicture(const std::string& name, int x, int y)
{
    SDL_Texture* t = PictureTexture(name);
    if (!t)
        return;
    int w, h;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    const SDL_Rect dst = R(x, y, w, h);
    SDL_RenderCopy(_r, t, nullptr, &dst);
    DrawPictureText(*this, name, x, y);
}

void Gfx::DrawPicturePart(const std::string& name, int sx, int sy, int w, int h, int x, int y)
{
    SDL_Texture* t = PictureTexture(name);
    if (!t)
        return;
    const SDL_Rect src{ sx, sy, w, h };
    const SDL_Rect dst = R(x, y, w, h);
    SDL_RenderCopy(_r, t, &src, &dst);
    DrawPictureText(*this, name, x, y, sx, sy, w, h);
}

void Gfx::DrawFullScreen(const std::string& file, const std::string& palette, const SDL_Rect& dst)
{
    const std::string key = file + "|" + palette;
    auto it = _pictures.find(key);
    if (it == _pictures.end())
    {
        Picture p;
        SDL_Texture* t = nullptr;
        if (DecodeFullScreen(_mega->Read(file), p))
        {
            const Palette saved = _pal;
            const std::vector<uint8_t> pal = _mega->Read(palette);
            if (pal.size() >= 48)
                _pal = LoadPalette(pal);
            t = MakeTexture(p, -1);
            _pal = saved;
        }
        it = _pictures.emplace(key, t).first;
    }
    if (it->second)
        SDL_RenderCopy(_r, it->second, nullptr, &dst);
}

void Gfx::DrawSprite(const std::string& file, int frame, const SDL_Rect& dst)
{
    SDL_Texture* t = PictureTexture(file + "@" + std::to_string(frame));
    if (t)
        SDL_RenderCopy(_r, t, nullptr, &dst);
}

void Gfx::DrawBank(const std::string& bank, int index, int x, int y)
{
    DrawPicture(bank + "#" + std::to_string(index), x, y);
}

void Gfx::DrawButton(int face, int x, int y)
{
    // the face and its one-pixel shadow, two pixels down and right (seen on the Farm Expert's buttons)
    DrawPicture("buttons.til$" + std::to_string(face), x, y);
    Fill(x + 48, y + 2, 1, 23, kGrey);
    Fill(x + 2, y + 24, 47, 1, kGrey);
}

void Gfx::DrawTile(int id, int x, int y, int size)
{
    const SDL_Rect src{ (id % kTileColumns) * 16, (id / kTileColumns) * 16, 16, 16 };
    const SDL_Rect dst = R(x, y, size, size);
    SDL_RenderCopy(_r, _tiles, &src, &dst);
}

void Gfx::DrawTileAt(int id, const SDL_Rect& dst)
{
    const SDL_Rect src{ (id % kTileColumns) * 16, (id / kTileColumns) * 16, 16, 16 };
    SDL_RenderCopy(_r, _tiles, &src, &dst);
}

void Gfx::DrawAnyTile(int id, int x, int y, int size)
{
    if (id < 848)
    {
        DrawTile(id, x, y, size);
        return;
    }
    const int m = id - 848;
    const SDL_Rect src{ (m % kTileColumns) * 16, (m / kTileColumns) * 16, 16, 16 };
    const SDL_Rect dst = R(x, y, size, size);
    SDL_RenderCopy(_r, _masked, &src, &dst);
}

void Gfx::Text(int x, int y, const std::string& s, int c)
{
    SDL_SetTextureColorMod(_font, _pal[c][0], _pal[c][1], _pal[c][2]);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto ch = static_cast<unsigned char>(s[i]);
        if (ch <= 0x20)
            continue;
        const SDL_Rect src{ (ch % 16) * 8, (ch / 16) * 8, 8, 8 };
        const SDL_Rect dst = R(x + static_cast<int>(i) * 8, y, 8, 8);
        SDL_RenderCopy(_r, _font, &src, &dst);
        DrawCapitalMark(ch, x + static_cast<int>(i) * 8, y);
    }
}

// the ink columns of each glyph (a blank glyph counts as columns 0..3), for TextFit's tight spacing
uint8_t Gfx::s_ink[256][2] = {};
uint8_t Gfx::s_capMark[256] = {};

void Gfx::DrawCapitalMark(unsigned char ch, int x, int y)
{
    if (const int m = s_capMark[ch])
    {
        const SDL_Rect src{ (m % 16) * 8, (m / 16) * 8 + 7, 8, 1 };
        const SDL_Rect dst = R(x, y - 1, 8, 1);
        SDL_RenderCopy(_r, _font, &src, &dst);
    }
}

// spacing used by TextFit: 8 = monospaced, else the gap in pixels between the letters' ink
static int FitGap(const std::string& s, int room, int& width)
{
    width = static_cast<int>(s.size()) * 8;
    if (width <= room)
        return 8;
    for (int gap = 1; gap >= 0; --gap)
    {
        width = 0;
        for (const char ch : s)
        {
            const auto u = static_cast<unsigned char>(ch);
            width += Gfx::InkWidth(u) + gap;
        }
        width -= gap;
        if (width <= room || gap == 0)
            return gap;
    }
    return 0;
}

int Gfx::FitWidth(const std::string& s, int room)
{
    int width = 0;
    FitGap(s, room, width);
    return width;
}

void Gfx::TextFit(int x, int y, int room, const std::string& s, int c, int align)
{
    int width = 0;
    const int gap = FitGap(s, room, width);
    int pen = align == 1 ? x + (room - width) / 2 : align == 2 ? x + room - width : x;
    if (gap == 8)
    {
        Text(pen, y, s, c);
        return;
    }
    SDL_SetTextureColorMod(_font, _pal[c][0], _pal[c][1], _pal[c][2]);
    for (const char ch : s)
    {
        const auto u = static_cast<unsigned char>(ch);
        if (u > 0x20)
        {
            const SDL_Rect src{ (u % 16) * 8, (u / 16) * 8, 8, 8 };
            const SDL_Rect dst = R(pen - s_ink[u][0], y, 8, 8);
            SDL_RenderCopy(_r, _font, &src, &dst);
            DrawCapitalMark(u, pen - s_ink[u][0], y);
        }
        pen += InkWidth(u) + gap;
    }
}

// an accented letter in the small font is its plain capital with a mark above it (see AddLatin1)
static unsigned char SmallBase(unsigned char ch, Mark& mark)
{
    mark = kNoMark;
    if (const Composed* k = ch >= 0xC0 ? FindComposed(ch) : nullptr)
    {
        mark = k->mark;
        return static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(k->base)));
    }
    return ch;
}

int Gfx::SmallTextWidth(const std::string& s)
{
    int w = 0;
    Mark mark;
    for (const char chr : s)
    {
        const unsigned char ch = SmallBase(static_cast<unsigned char>(chr), mark);
        if (ch >= 32 && ch <= 126)
            w += kSmallFont[ch - 32].w + kSmallFontGap;
    }
    return w > 0 ? w - kSmallFontGap : 0;
}

void Gfx::SmallText(int x, int y, const std::string& s, int c)
{
    // the engine's own bold capitals (SmallFont.h), one interface pixel per font pixel
    std::vector<SDL_Rect> px;
    for (const char chr : s)
    {
        Mark mark;
        const unsigned char ch = SmallBase(static_cast<unsigned char>(chr), mark);
        if (ch < 32 || ch > 126)
            continue;
        const SmallGlyph& gl = kSmallFont[ch - 32];
        for (int r = 0; r < kSmallFontH; ++r)
            for (int b = 0; b < gl.w; ++b)
                if (gl.rows[r] & (0x80 >> b))
                    px.push_back(R(x + b, y + r, 1, 1));
        if (mark != kNoMark)
        {
            const int mx = x + (gl.w - 4) / 2;
            for (int r = 0; r < 2; ++r)
                for (int b = 0; b < 4; ++b)
                    if (kMarkSmall[mark][r] & (0x80 >> b))
                        px.push_back(mark == kCedilla ? R(mx + b, y + kSmallFontH + r, 1, 1) : R(mx + b, y - 2 + r, 1, 1));
        }
        x += gl.w + kSmallFontGap;
    }
    SDL_SetRenderDrawColor(_r, _pal[c][0], _pal[c][1], _pal[c][2], 255);
    if (!px.empty())
        SDL_RenderFillRects(_r, px.data(), static_cast<int>(px.size()));
}

void Gfx::TextRight(int right, int y, const std::string& s, int c) { Text(right - TextWidth(s), y, s, c); }
void Gfx::TextCentre(int centre, int y, const std::string& s, int c) { Text(centre - TextWidth(s) / 2, y, s, c); }

void Gfx::DimRect(int x, int y, int w, int h, int alpha)
{
    SDL_SetRenderDrawBlendMode(_r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(_r, 0, 0, 0, static_cast<Uint8>(alpha));
    const SDL_Rect rc = R(x, y, w, h);
    SDL_RenderFillRect(_r, &rc);
    SDL_SetRenderDrawBlendMode(_r, SDL_BLENDMODE_NONE);
}

// The original's word wrap, step by step: each word is looked up first (its length skips any blanks,
// '@' padding and '~' before it), goes to a new row when 8 x length + column + 8 passes the width, and is drawn
// after one blank cell. Blanks, '@' and characters above 'z' are skipped; '~' starts a new row; '`' is a blank.
// So a word that does not fit can move down a row before the '~' that comes ahead of it.
// the text box's layout: each word with its position (draw may be empty); returns the height used
static int WrapLayout(int w, const std::string& text, const std::function<void(int, int, const std::string&)>& draw)
{
    // (the Latin-1 letters of a translation, 0xA0 and up, belong to words)
    auto sep = [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return c == '@' || u <= ' ' || (u > 'z' && u < 0xA0);
    };
    size_t p = 0;
    int cx = 0, cy = 0;
    while (p < text.size() && text[p])
    {
        size_t q = p;
        while (q < text.size() && text[q] && sep(text[q]))
            ++q;
        int len = 0;
        while (q < text.size() && text[q] && !sep(text[q]))
            ++q, ++len;
        if (8 * len + cx + 8 > w)
        {
            cy += 8;
            cx = 0;
        }
        cx += 8; // the blank cell before the word
        while (p < text.size() && text[p] && text[p] != '~' && sep(text[p]))
            ++p;
        std::string word;
        while (p < text.size() && text[p] && text[p] != '~' && !sep(text[p]))
            word += text[p] == '`' ? ' ' : text[p], ++p;
        if (draw)
            draw(cx, cy, word);
        cx += 8 * static_cast<int>(word.size());
        if (p < text.size() && text[p] == '~')
        {
            cy += 8;
            cx = 0;
            ++p;
        }
    }
    return cx > 0 ? cy + 8 : cy;
}

void Gfx::Wrap(int x, int y, int w, const std::string& text, int colour)
{
    WrapLayout(w, text, [&](int cx, int cy, const std::string& word) { Text(x + cx, y + cy, word, colour); });
}

int Gfx::WrapHeight(int w, const std::string& text) { return WrapLayout(w, text, nullptr); }

} // namespace osf
