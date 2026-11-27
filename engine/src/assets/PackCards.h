// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/PackTables.h"
#include "assets/TilePack.h"

#include <string>

namespace osf {

// The Farm Expert's cards (what the original keeps as card1.hc, card2.hc ...) as JSON, in an optional "cards" block of
// tilepack.json. The Expert opens on card 1; a card is a list of things to draw and places to click:
//
//   "cards": [
//     { "id": 1, "items": [
//         { "picture": 66, "x": 24, "y": 40 },                              a picture of the bank alltiles.btl
//         { "text": "Welcome to the farm.", "x": 120, "y": 40, "width": 240 },   a text box, wrapped to the width (pixels)
//         { "button": 4, "x": 300, "y": 36, "goto": 2 },                    a button face (buttons.til) that opens card 2
//         { "hotspot": [24, 100, 200, 116], "goto": 3 },                    a click inside opens card 3
//         { "tiles": { "first": 539, "count": 4 }, "x": 24, "y": 150 },      4 map tiles in a row from tile 539
//         { "crop": 0, "x": 24, "y": 90 },                                  the information of the crop in slot 0
//         { "sound": [20, 20, 60, 60], "play": "cow1.voc" }                 a click inside plays a sound
//     ] } ]
//
// Positions count from the window's top-left corner, the title bar (16) included: the window is 400 x 224, and its CLOSE
// button sits at (336, 200) unless a card puts a button of its own there (a back button). Button faces are the 18 of
// buttons.til (0 OK, 1 BUY, 2 SELL, 3 CANCEL, 4 GOTO, 5 USE, 6 EXIT, 7 LOAD, 8 NEW, 9 USED, 10 YES, 11 NO, 12 SAVE, 13 NEXT,
// 14 PREV, 15 CLOSE, 16 TOP, 17 RATS). Text is printable ASCII, at most 800 characters a box. A pack must have card 1,
// every "goto" must name a card the pack has, and there are at most 64 cards of 60 items.
//
// Each card becomes the archive entry card<id>.hc that the Expert reads (assets/TableFormats.h, DecodeCard).

// Reads the "cards" block of a manifest's text into `out` as the archive entries card<id>.hc. No block: nothing. False with
// `error` set when anything in it is wrong (`out` is unchanged then).
bool ParsePackCards(const std::string& manifestJson, TableBlobs& out, std::string& error);

// Reads `dir`/tilepack.json's cards (a folder without a manifest is an error).
bool LoadPackCards(const std::string& dir, TableBlobs& out, std::string& error);

} // namespace osf
