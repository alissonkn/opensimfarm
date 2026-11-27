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

} // namespace osf
