// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace osf {

// A tile pack: truecolour map tiles at any resolution from 8 to 128 pixels a tile, as PNG sheets described by a
// tilepack.json in the pack's folder:
//
//   {
//     "format": 1,
//     "name": "My Farm",                 (optional)
//     "tileSize": 32,                    pixels along a tile's edge: the same for every sheet
//     "sheets": {
//       "plain":  { "file": "plain.png",  "count": 848, "columns": 32 },
//       "masked": { "file": "masked.png", "count": 200, "columns": 20 },
//       "events": { "file": "events.png", "count": 105, "columns": 15 }      (optional)
//     }
//   }
//
// A sheet is a grid of `columns` tiles across; tile i is at column i % columns, row i / columns, and the image is at
// least columns * tileSize wide and rows * tileSize high. The tile numbers are the original game's (MapGraphics:
// 848 plain tiles, 200 masked ones, 105 event ones), so the cells keep their meaning. The masked and event sheets
// use the PNG's alpha channel for see-through pixels; the plain sheet is drawn opaque. File names are plain names
// in the pack's folder.

// Decodes one PNG sheet (`png` = the file's bytes) into `out`, `count` tiles of tileSize x tileSize read from a grid
// `columns` wide. False with `error` set when it is not a PNG, is too small for the grid, or is oversized.
bool DecodeTileSheetPng(const std::vector<uint8_t>& png, int tileSize, int columns, int count, TileSheet& out, std::string& error);

// Reads `dir`/tilepack.json and its sheets into out.plain, out.masked and out.events (out.events stays as it was
// when the pack has none). The palette and every other field of `out` are left alone. All or nothing: on failure
// `out` is unchanged and `error` says what is wrong.
bool LoadTilePack(const std::string& dir, MapGraphics& out, std::string& error);

// Parses a tilepack.json's text into the same result, reading each sheet's PNG through `read` (given the sheet's
// file name; an empty result = unreadable). Split from LoadTilePack so that the format can be tested without files.
struct TilePackSheets
{
    TileSheet plain, masked, events;
    bool hasEvents = false;
    std::string name;
};
bool ParseTilePack(const std::string& json, bool (*read)(void* user, const std::string& file, std::vector<uint8_t>& bytes), void* user,
                   TilePackSheets& out, std::string& error);

} // namespace osf
