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
// use the PNG's alpha channel for see-through pixels; the plain sheet is drawn opaque.
//
// "tileSize" and "sheets" are optional together: a pack may bring only pictures (assets/PicturePack.h, the same
// tilepack.json's "pictures" block). File names are relative paths in the pack's folder ("plain.png", "ui/x.png"):
// '/' between folders, never "..", a drive letter, a backslash or a leading '/'.

// Decodes one PNG sheet (`png` = the file's bytes) into `out`, `count` tiles of tileSize x tileSize read from a grid
// `columns` wide. False with `error` set when it is not a PNG, is too small for the grid, or is oversized.
bool DecodeTileSheetPng(const std::vector<uint8_t>& png, int tileSize, int columns, int count, TileSheet& out, std::string& error);

// Reads `dir`/tilepack.json and its sheets into out.plain, out.masked and out.events (out.events stays as it was
// when the pack has none). The palette and every other field of `out` are left alone. All or nothing: on failure
// `out` is unchanged and `error` says what is wrong. A pack without "sheets" succeeds and leaves `out` alone; `loaded`
// (if given) tells which happened.
bool LoadTilePack(const std::string& dir, MapGraphics& out, std::string& error, bool* loaded = nullptr);

// true when `file` is an acceptable file name inside a pack (see above)
bool IsPackFileName(const std::string& file);

// reads a pack file: false when it cannot be read
using PackReader = bool (*)(void* user, const std::string& file, std::vector<uint8_t>& bytes);

// Reads the file `file` (IsPackFileName) of the pack folder `dir` (at most 64 MB). False when the name is not
// acceptable or the file cannot be read.
bool ReadPackFile(const std::string& dir, const std::string& file, std::vector<uint8_t>& bytes);

// Parses a tilepack.json's text into the same result, reading each sheet's PNG through `read` (given the sheet's
// file name; an empty result = unreadable). Split from LoadTilePack so that the format can be tested without files.
struct TilePackSheets
{
    TileSheet plain, masked, events;
    bool hasEvents = false;
    bool hasSheets = false; // false: the manifest has no "sheets" (a pictures-only pack)
    std::string name;
};
bool ParseTilePack(const std::string& json, PackReader read, void* user, TilePackSheets& out, std::string& error);

} // namespace osf
