// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
// Translated words over the English words printed into the game's pictures (window labels, button faces, the
// toolbar's Date and Funds). Gfx calls it after drawing a picture; in English, or for a word without a translation,
// the picture is left as it is.
#pragma once
#include <climits>
#include <string>
#include <vector>

namespace osf {

class Gfx;

// picture drawn at (dx, dy); for a part of it, the part's source rectangle (labels wholly inside it are drawn)
void DrawPictureText(Gfx& g, const std::string& picture, int dx, int dy, int sx = 0, int sy = 0, int sw = INT_MAX / 2,
                     int sh = INT_MAX / 2);

// for the tests: each label's picture, English key, room in pixels, lines and (paragraphs) most lines
struct PictureTextInfo
{
    std::string picture, english;
    int room, lines, maxLines;
};
std::vector<PictureTextInfo> PictureTexts();

} // namespace osf
