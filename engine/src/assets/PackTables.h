// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/TableFormats.h"

#include <map>
#include <string>
#include <vector>

namespace osf {

// The game's data tables written as JSON, in the "tables" block of a pack's tilepack.json, made into the blobs the
// engine reads (assets/TableFormats.h): same names, same formats. A pack's tables replace the archive's of the same
// name; a table the pack leaves out stays the archive's. Every table is optional. (The overview is the one table
// that is no byte format of the original's: it makes the entry "overview.rgb" of TableFormats.h, RGB where the
// original's far_mapc.mbm holds palette indices.)
//
//   "tables": {
//     "climate": {                    the 12 climate tables: 4 classes (0 mild .. 3 extreme, as the sliders pick) each
//       "rain":        [ T, T, T, T ],    T = a number a week: 48 (one year, repeated 5 times) or 240 (5 years)
//       "temperature": [ T, T, T, T ],    rain: the week's rain class 0..7 (which days it rains); degrees F 0..255
//       "wind":        [ T, T, T, T ]     wind: miles an hour 0..255
//     },
//     "regions": {                    the region map: 18 rows of 18 letters
//       "rain": [ ... ], "temperature": [ ... ], "wind": [ ... ],   'a' + the climate class 0..3 of each cell
//       "hotpoints": [ ... ]          optional: a letter above 'a' marks a cell a farm can be chosen in
//     },
//     "townLayout": { "blocks": [ B x 15 ] },   B = 8 rows of 8 tile ids; block t is plot template type t
//     "events":  [ { "id": 61, "title": "...", "lines": [ "...", ... ] } ],    message boxes' texts by event id
//     "items":   [ { "id": 5, "name": "...", "description": "..." } ],         item help texts by id: an item's tile
//                                                                              family, as the catalogue (game/Template.h) gives it
//     "overview": {                   the overview map's colours: a tile's 2 x 2 block is "#rrggbb" for all four pixels,
//       "default": "#3f9f3f",             or 4 colours (row by row); a tile takes the default, then the ranges in order,
//       "ranges":  [ { "from": 15, "to": 30, "colours": "#80502a" } ],     then its own entry
//       "tiles":   { "17": [ "#80502a", "#6a4020", "#80502a", "#6a4020" ] }
//     }
//   }
//
// Limits that come from the engine's boxes: an event's title and lines are at most 27 characters (the title is
// centred for you), 6 lines; an item's name is at most 13 characters and its description is wrapped to at most 6
// lines of 13; text is printable ASCII from blank to 'z' except '@', '~' and '`' (the file formats' own marks).

using TableBlobs = std::map<std::string, std::vector<uint8_t>>; // lower-case archive name -> bytes

// Makes the blobs of the "tables" block of a manifest's text. No block: no blobs. False with `error` set when
// anything in the block is wrong (`out` is unchanged then).
bool ParsePackTables(const std::string& json, TableBlobs& out, std::string& error);

// Reads `dir`/tilepack.json's tables (a folder without a manifest is an error).
bool LoadPackTables(const std::string& dir, TableBlobs& out, std::string& error);

} // namespace osf
