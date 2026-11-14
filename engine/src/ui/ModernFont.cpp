// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/ModernFont.h"

#include <algorithm>
#include <cmath>
#include <string>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace osf {

struct BuiltinFont { const char* file; const unsigned char* data; unsigned long size; };
extern const BuiltinFont kBuiltinFonts[];
extern const int kBuiltinFontCount;

namespace {

// the fonts on offer, in menu order: name, file (built in from src/third_party/fonts)
struct Offered { const char* name; const char* file; };
constexpr Offered kOffered[] = {
    { "Original", nullptr },
    { "Space Mono", "SpaceMono-Bold.ttf" },
    { "IBM Plex Mono", "IBMPlexMono-Bold.ttf" },
    { "JetBrains Mono", "JetBrainsMono-Bold.ttf" },
    { "Press Start 2P", "PressStart2P-Regular.ttf" },
};
constexpr int kOfferedCount = static_cast<int>(sizeof kOffered / sizeof kOffered[0]);

const BuiltinFont* Builtin(int font)
{
    if (font <= 0 || font >= kOfferedCount)
        return nullptr;
    for (int i = 0; i < kBuiltinFontCount; ++i)
        if (std::string(kBuiltinFonts[i].file) == kOffered[font].file)
            return &kBuiltinFonts[i];
    return nullptr;
}

// the font's edges made a little firmer than plain cover, so small letters do not look washed out
constexpr double kContrast = 1.3;

// the codes the game's own glyph stays for: controls, the tick on '^', the capital marks' slots (0x80-0x9F)
bool GameGlyph(int c) { return c <= 0x20 || c == '^' || c == 0x7F || (c >= 0x80 && c <= 0xA0); }

} // namespace

int FontCount() { return kOfferedCount; }

const char* FontName(int f) { return f >= 0 && f < kOfferedCount ? kOffered[f].name : ""; }

std::vector<uint32_t> ModernFontAtlas(const Font8& font8, int detail, int which)
{
    const BuiltinFont* file = Builtin(which);
    stbtt_fontinfo font;
    if (!file || file->size == 0 || !stbtt_InitFont(&font, file->data, stbtt_GetFontOffsetForIndex(file->data, 0)))
        return {};
    const int d = std::max(1, detail), cw = 8 * d, ch = kModernFontCellH * d, W = 16 * cw;
    std::vector<uint32_t> atlas(static_cast<size_t>(W) * 16 * ch, 0);
    auto put = [&](int c, int x, int y, double a) {
        if (x < 0 || x >= cw || y < 0 || y >= ch || a <= 0)
            return;
        uint32_t& p = atlas[static_cast<size_t>((c / 16) * ch + y) * W + (c % 16) * cw + x];
        const uint32_t v = static_cast<uint32_t>(std::lround(std::min(1.0, a) * 255));
        p = std::max(p, (v << 24) | 0xFFFFFFu);
    };
    // capitals 7 units tall, on whole pixels; the baseline under the original's 7th row
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!stbtt_GetCodepointBox(&font, 'H', &x0, &y0, &x1, &y1) || y1 <= 0)
        return {};
    const float scale = static_cast<float>(7 * d) / static_cast<float>(y1);
    const int baseline = (kModernFontAbove + 7) * d;
    for (int c = 0; c < 256; ++c)
    {
        const int code = c == 0xAD ? '-' : c; // the soft hyphen, shown where a word is broken
        if (GameGlyph(c) || stbtt_FindGlyphIndex(&font, code) == 0)
        {
            for (int y = 0; y < 8 * d; ++y)
                for (int x = 0; x < cw; ++x)
                    if (font8.glyphs[static_cast<size_t>(c)][static_cast<size_t>(y / d)] & (0x80 >> (x / d)))
                        put(c, x, kModernFontAbove * d + y, 1.0);
            continue;
        }
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&font, code, &advance, &bearing);
        const float left = (static_cast<float>(cw) - static_cast<float>(advance) * scale) / 2; // centred in the cell
        const float whole = std::floor(left);
        int gw = 0, gh = 0, ox = 0, oy = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmapSubpixel(&font, scale, scale, left - whole, 0, code, &gw, &gh, &ox, &oy);
        if (!bitmap)
            continue;
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x)
            {
                const double a = std::clamp((bitmap[y * gw + x] / 255.0 - 0.5) * kContrast + 0.5, 0.0, 1.0);
                put(c, static_cast<int>(whole) + ox + x, baseline + oy + y, a);
            }
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    return atlas;
}

} // namespace osf
