// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/TableFormats.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace osf {

// The Map window's picture: each cell of the world as the 2 x 2 block of overview colours of its tile
// (assets/TableFormats.h), scaled into the window's box, with the original's XOR drawing laid over it (the chosen plot
// and the view frame). Split from the window so that it can be checked without one.
//
// With an indexed table (the archive's) the picture holds palette indices and its XOR is "index ^ 15", as the original
// draws, so the colours come out as the palette gives them. With an RGB table (a pack's) it holds RGB and its XOR
// inverts the colour.
class OverviewImage
{
public:
    // `tile[x * cells + y]` is the tile id of the cell in column x, row y of a cells x cells world
    OverviewImage(const OverviewTable& table, const std::vector<int>& tile, int cells);

    int Side() const { return _side; } // 2 * cells until ScaleTo
    bool Rgb() const { return _rgb; }
    // the picture scaled (nearest pixel) to side x side
    void ScaleTo(int side);
    // the XOR drawing of one pixel; outside the picture nothing happens
    void Invert(int x, int y);
    // the picture as ARGB; `paletteArgb` turns an indexed table's colour index into ARGB (unused for an RGB table)
    std::vector<uint32_t> ToArgb(const std::function<uint32_t(int)>& paletteArgb) const;

private:
    bool _rgb = false;
    int _side = 0;
    std::vector<uint8_t> _index;  // indexed table: a palette index each pixel
    std::vector<uint32_t> _argb;  // RGB table: an opaque ARGB each pixel
};

} // namespace osf
