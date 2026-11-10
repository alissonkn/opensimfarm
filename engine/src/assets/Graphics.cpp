// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/Graphics.h"

#include "assets/Megadata.h"

#include <algorithm>

namespace osf {

Palette LoadPalette(const std::vector<uint8_t>& pal)
{
    Palette p{};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c)
        {
            const int v = static_cast<size_t>(i * 3 + c) < pal.size() ? pal[i * 3 + c] & 0x3F : 0;
            p[i][c] = static_cast<uint8_t>(std::min(255, v * 4 + (v >> 4)));
        }
    return p;
}

TileSheet DecodeTiles16(const std::vector<uint8_t>& data, int maxTiles)
{
    TileSheet sheet;
    sheet.count = std::min(maxTiles, static_cast<int>(data.size() / 128));
    sheet.pixels.resize(static_cast<size_t>(sheet.count) * 256);
    for (int t = 0; t < sheet.count; ++t)
    {
        const uint8_t* src = &data[static_cast<size_t>(t) * 128];
        uint8_t* dst = &sheet.pixels[static_cast<size_t>(t) * 256];
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                uint8_t v = 0;
                for (int plane = 0; plane < 4; ++plane)
                {
                    const uint8_t b = src[plane * 32 + y * 2 + (x >> 3)];
                    v |= static_cast<uint8_t>(((b >> (7 - (x & 7))) & 1) << plane);
                }
                dst[y * 16 + x] = v;
            }
    }
    return sheet;
}

bool ApplyCropArt(TileSheet& sheet, int slot, const std::vector<uint8_t>& art)
{
    if (art.size() < 14 * 128 || slot < 0 || slot >= 16 || sheet.count < 848)
        return false;
    const TileSheet t = DecodeTiles16(art, 14);
    for (int k = 0; k < 14; ++k)
    {
        const int id = k < 8 ? 688 + 8 * slot + k : k < 12 ? 539 + 4 * slot + (k - 8) : (k == 12 ? 816 : 832) + slot;
        std::copy(t.Tile(k), t.Tile(k) + 256, sheet.pixels.begin() + static_cast<long>(id) * 256);
    }
    return true;
}

bool LoadMapGraphics(const Megadata& mega, MapGraphics& out)
{
    const auto pal = mega.Read("simfarm.pal");
    const auto plain = mega.Read("ega16til.til");
    const auto masked = mega.Read("mskdtile.til");
    if (pal.size() < 48 || plain.size() < 848 * 128 || masked.size() < 200 * 128)
        return false;
    out.palette = LoadPalette(pal);
    out.windowsEdition = mega.WindowsEdition();
    out.plain = DecodeTiles16(plain, 848);
    out.masked = DecodeTiles16(masked, 200);
    // the see-through pixels: the Windows edition's own masks (1 bit a pixel, 1 = see-through), else colour 7
    const auto mask = mega.Read("mskdtile.msk");
    for (int t = 0; t < out.masked.count; ++t)
        for (int i = 0; i < 256; ++i)
        {
            uint8_t& c = out.masked.pixels[static_cast<size_t>(t) * 256 + i];
            const size_t bit = static_cast<size_t>(t) * 256 + i;
            if (mask.size() >= 200 * 32 ? ((mask[bit / 8] >> (7 - bit % 8)) & 1) != 0 : c == 7)
                c = kSeeThrough;
        }
    out.events = DecodeTiles16(mega.Read("twnevent.til"), 105);
    return true;
}

} // namespace osf
