// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace osf {

// The game's data tables as the engine reads them: byte blobs under fixed archive names (MEGADATA.EEA's entries).
// The decoders here are the engine's one reading of each format, shared by everything that uses a table.
// assets/PackTables.h makes the same blobs from a pack's JSON, so a pack replaces the archive's tables.

// ---- climate: rain0..3.dat, temp0..3.dat, wind0..3.dat
// Text: 240 whole numbers (one a week: 5 years of 48 weeks) separated by commas, blanks or line ends. False unless
// there are exactly 240.
constexpr int kClimateWeeks = 240;
bool DecodeClimateTable(const std::vector<uint8_t>& blob, std::vector<int>& out);

// ---- town layout: townly0.cmp
// 960 little-endian words: a 40 x 24 grid of tile ids (row after row) holding the town's 15 plot templates, 5 blocks
// of 8 x 8 cells across and 3 down. Empty when the blob is shorter than that.
constexpr int kTownBlocksAcross = 5, kTownBlocksDown = 3, kTownBlock = 8, kTownLayoutWords = 960;
std::vector<uint16_t> DecodeTownLayout(const std::vector<uint8_t>& blob);

// ---- text files made of lines: events.dat and itemtext.dat
// The lines of a text blob: split at line feeds, carriage returns dropped, the last line kept even when empty.
// With `blankPads`, '@' (the padding) and '~' (a line break marker) become blanks (the item texts).
std::vector<std::string> DecodeTextLines(const std::vector<uint8_t>& blob, bool blankPads = false);

// events.dat: 8 lines for each event id: a title and six text lines (padded with '@', a '`' is a blank that
// centres a title), then a blank line. The 7 lines of event `id` (blank ones when the file is shorter).
constexpr int kEventLines = 8, kEventShownLines = 7, kEventWidth = 27; // characters a line of the message box holds
std::vector<std::string> EventLines(const std::vector<std::string>& lines, int id);

// itemtext.dat: 128 bytes for each item id: 8 lines of 14 characters padded with '@', each ended by CR LF; the
// first is the name, the next six the description. The 7 lines of item `id` (from DecodeTextLines(blob, true)).
constexpr int kItemRecordBytes = 128, kItemLineWidth = 14;
std::vector<std::string> ItemLines(const std::vector<std::string>& lines, int id);

// ---- scenario.odt: the region map's climate grids
// Sections that start with a line "//name" (the name in lower case) followed by 18-character rows, each row ended by a
// blank. Section name -> its rows: "rain data", "temp data" and "wind data" hold a letter for each cell, 'a' + the
// climate class 0..3; "hotpoint ..." marks the cells a farm can be chosen in with a letter above 'a'.
constexpr int kRegionSize = 18;
std::map<std::string, std::vector<std::string>> DecodeScenarioGrids(const std::vector<uint8_t>& blob);

// ---- overview colours: far_mapc.mbm
// For each tile id 2 bytes, one for each of the 2 pixel rows of the tile's 2 x 2 overview block; a byte holds the 2
// pixels' palette indices (0..15) as four plane fields of 2 bits, the left pixel in the high bit of each.
constexpr int kOverviewTiles = 960;
int OverviewIndex(const std::vector<uint8_t>& blob, int tile, int row, int column); // 0 outside the table

// A pack's overview colours are RGB instead: the entry "overview.rgb" (one a pack adds, as the Windows edition adds
// uicolour.map), 960 tiles x 4 pixels (the tile's 2 x 2 block, row by row) x 4 bytes: red, green, blue, and a
// fourth that is ignored (the pixel is drawn opaque).
constexpr int kOverviewRgbBytes = kOverviewTiles * 4 * 4;

// The overview colours the interface draws with: the pack's RGB entry when it has one, else the archive's palette
// indices. An RGB table needs no palette (and does not change with the weather); an indexed one gives each pixel a
// palette index 0..15.
struct OverviewTable
{
    std::vector<uint8_t> blob; // far_mapc.mbm or overview.rgb
    bool rgb = false;
    // the pixel (row, column of the 2 x 2 block) of `tile`; Index for an indexed table (0 outside it), Argb for an RGB one
    // (opaque black outside it)
    int Index(int tile, int row, int column) const { return rgb ? 0 : OverviewIndex(blob, tile, row, column); }
    uint32_t Argb(int tile, int row, int column) const;
};
OverviewTable MakeOverview(const std::vector<uint8_t>& rgbEntry, const std::vector<uint8_t>& indexedEntry);

// ---- the Farm Expert's cards: card1.hc, card2.hc ...
// A card is a script of operations, each a code byte, 16-bit little-endian arguments and, for the two that have text, a
// zero-terminated string; 0xFF ends it. Coordinates count from the window's top-left corner, the title bar included.
//   0x83 hotspot   x0 y0 x1 y1 card     a click inside goes to `card`
//   0x84 button    x y face card        a button face (buttons.til) that goes to `card`
//   0x86 picture   record x y           a picture of the bank alltiles.btl
//   0x87 text      x y width + text     a text box (the width in pixels)
//   0x89 crop      slot x y             the information of the crop in `slot`
//   0x8a sound     x0 y0 x1 y1 + name   a click inside plays the sound `name`
//   0x8b (two arguments, not used)
//   0x8c tiles     x y count - first    `count` map tiles in a row from tile `first` (the fourth argument is not used)
struct CardOp
{
    int op = 0;
    std::vector<int> a;
    std::string text;
};
// the operations of a card; reading stops at 0xFF, the end of the blob, or an operation it does not know
std::vector<CardOp> DecodeCard(const std::vector<uint8_t>& blob);
// how many 16-bit arguments an operation has and whether a string follows; -1 for an unknown operation
int CardOpArguments(int op, bool& hasText);

} // namespace osf
