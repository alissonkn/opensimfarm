// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Picture.h"
#include "assets/TilePack.h"

#include <map>
#include <string>
#include <vector>

namespace osf {

// The truecolour pictures of a pack: windows, buttons, sprites, full-screen pictures. Each stands in for one of the
// original's pictures, by the key the interface asks for it with (the names of Gfx::PictureTexture):
//
//   "buywndo.bmp"          a window or other picture      "sftitle.int"      a full-screen picture
//   "buttons.til$3"        face 3 of a face file          "airplane.spr@2"   frame 2 of a sprite
//   "alltiles.btl#66"      record 66 of a picture bank    "editeasy.lzs"     a compressed picture
//
// A picture has a *logical size*: the size it takes in the interface's own units (the original's 640 x 480 screen),
// where the interface positions everything. The PNG may have more pixels than that (2 or 4 times as many, say); the
// extra pixels make it sharper, never larger. So a pack's pictures slot into the original's layouts without moving
// anything; a pack that wants other layouts changes the interface's own coordinates.
//
// The manifest (tilepack.json, next to the tile sheets) has an optional "pictures" block:
//
//   "pictures": {
//     "scale": 2,                          pixels per interface unit of every picture without its own size or scale
//                                          (default 1)
//     "single": {                          one picture each, by key
//       "buywndo.bmp": { "file": "ui/buywndo.png" },                      logical size = PNG size / scale
//       "popup.bmp":   { "file": "ui/popup.png", "size": [256, 192] },    or given
//       "about.bmp":   { "file": "ui/about.png", "scale": 4 }
//     },
//     "sets": [                            pictures cut from one grid PNG, by a key template with {n}
//       { "key": "buttons.til${n}", ... }
//     ]
//   }
//
// A set: { "key": "buttons.til${n}", "file": "ui/buttons.png", "count": 18, "columns": 6 (default: all in one row),
// "first": 0 (the first {n}), "scale": 2 or "cell": [48, 24] (the logical size of one cell) }. Cell i is at column
// i % columns, row i / columns; the PNG must hold count cells of equal size.
//
// Keys are matched without regard to case. A key a pack leaves out is drawn from the original's archive as before.
// A pack's picture is drawn as it is: the interface's own overlays on the originals (translated words) are not
// painted over it.

struct PackPicture
{
    RgbaPicture image;
    int logicalW = 0, logicalH = 0; // in interface units
};

class PicturePack
{
public:
    // the picture for `key`, or null
    const PackPicture* Find(const std::string& key) const;
    // adds (or replaces) a picture; the key is kept in lower case
    void Add(const std::string& key, PackPicture picture);
    size_t Size() const { return _pictures.size(); }
    const std::map<std::string, PackPicture>& All() const { return _pictures; }

private:
    std::map<std::string, PackPicture> _pictures;
};

// Reads the "pictures" block of a manifest's text, each PNG through `read`. A manifest without the block gives an
// empty pack. False with `error` set when anything in the block is wrong (nothing is added to `out` then).
bool ParsePicturePack(const std::string& json, PackReader read, void* user, PicturePack& out, std::string& error);

// Reads `dir`/tilepack.json's pictures. A folder without a manifest is an error; a manifest without pictures an
// empty pack.
bool LoadPicturePack(const std::string& dir, PicturePack& out, std::string& error);

} // namespace osf
