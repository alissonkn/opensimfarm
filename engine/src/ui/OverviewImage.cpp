// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/OverviewImage.h"

namespace osf {

OverviewImage::OverviewImage(const OverviewTable& table, const std::vector<int>& tile, int cells) : _rgb(table.rgb), _side(2 * cells)
{
    const size_t n = static_cast<size_t>(_side) * _side;
    if (_rgb)
        _argb.assign(n, 0xFF000000u);
    else
        _index.assign(n, 0);
    for (int x = 0; x < cells; ++x)
        for (int y = 0; y < cells; ++y)
        {
            const int t = tile[static_cast<size_t>(x) * cells + y];
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                {
                    const size_t at = static_cast<size_t>(2 * y + r) * _side + 2 * x + c;
                    if (_rgb)
                        _argb[at] = table.Argb(t, r, c);
                    else
                        _index[at] = static_cast<uint8_t>(table.Index(t, r, c));
                }
        }
}

void OverviewImage::ScaleTo(int side)
{
    if (side == _side || side <= 0)
        return;
    const int from = _side;
    if (_rgb)
    {
        std::vector<uint32_t> box(static_cast<size_t>(side) * side);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                box[static_cast<size_t>(y) * side + x] = _argb[static_cast<size_t>(y * from / side) * from + x * from / side];
        _argb.swap(box);
    }
    else
    {
        std::vector<uint8_t> box(static_cast<size_t>(side) * side);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                box[static_cast<size_t>(y) * side + x] = _index[static_cast<size_t>(y * from / side) * from + x * from / side];
        _index.swap(box);
    }
    _side = side;
}

void OverviewImage::Invert(int x, int y)
{
    if (x < 0 || y < 0 || x >= _side || y >= _side)
        return;
    const size_t at = static_cast<size_t>(y) * _side + x;
    if (_rgb)
        _argb[at] ^= 0x00FFFFFFu;
    else
        _index[at] ^= 15;
}

std::vector<uint32_t> OverviewImage::ToArgb(const std::function<uint32_t(int)>& paletteArgb) const
{
    if (_rgb)
        return _argb;
    std::vector<uint32_t> out(_index.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = paletteArgb(_index[i]);
    return out;
}

} // namespace osf
