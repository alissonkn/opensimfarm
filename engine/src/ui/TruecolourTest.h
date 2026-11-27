// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once

namespace osf {

// Headless check of truecolour tile packs (--tilepacktest): the JSON reader, the pack loader on real PNG files made
// in memory, and the map view and interface drawing the tiles at several tile sizes, zooms and render modes with a
// software renderer. Needs no game files. Prints what it checks; returns 0 when all is well.
int TruecolourSelfTest();

// --packpreview folder prefix: loads the tile pack in `folder` and draws all of it with the map view, as
// `prefix`-plain.bmp (the 848 plain tiles, 32 to a row, tile id = row * 32 + column) and `prefix`-masked.bmp (the
// 200 masked tiles, 20 to a row, over ground). 0 when it worked; for artists, and for looking at a pack as the engine
// draws it
int PackPreview(const char* folder, const char* prefix);

} // namespace osf
