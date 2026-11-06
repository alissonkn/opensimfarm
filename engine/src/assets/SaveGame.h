// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace osf {

// The original's map is 96 x 96 cells. OpenSimFarm can also make larger worlds (an engine addition: 144 x 144, more
// land to farm); the size travels with the map, and the land is always plots of 8 x 8 cells.
constexpr int kOriginalMapSize = 96, kLargeMapSize = 144, kPlotCells = 8;

// One map cell: base word (tile id in bits 0-10, flags above), overlay word, terrain byte.
struct MapCell
{
    uint16_t base = 0;
    uint16_t overlay = 0;
    uint8_t terrain = 0;
    int BaseTile() const { return base & 0x7FF; }
    bool HasObject() const { return (base & 0x0800) != 0; }
    int OverlayTile() const { return (overlay & 0x7FF) - 0x350; } // index into the masked sheet
};

// The map of a saved game or prebuilt farm (.SFM / .SSM): size x size cells, column-major (x * size + y).
struct World
{
    int size = kOriginalMapSize;
    std::vector<MapCell> cells = std::vector<MapCell>(kOriginalMapSize * kOriginalMapSize);
    void Resize(int n) { size = n; cells.assign(static_cast<size_t>(n) * n, MapCell{}); }
    bool In(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size; }
    int Clamp(int v) const { return v < 0 ? 0 : v >= size ? size - 1 : v; }
    int Plots() const { return size / kPlotCells; } // plots along each side: 12 on the original's map
    MapCell& At(int x, int y) { return cells[static_cast<size_t>(x) * size + y]; }
    const MapCell& At(int x, int y) const { return cells[static_cast<size_t>(x) * size + y]; }
};

// Saved games. The original's file is 139,072 bytes with a 96 x 96 map. OpenSimFarm writes the same layout (a
// larger map has a larger map section and plot table) and puts an 8-byte signature at the end: "OSFM", the format
// version, the map size (16 bits, little-endian) and a spare byte. The original reads its fixed sections and ignores
// the signature (checked: it loads such a 96 x 96 save); a file without the signature is an original 96 x 96 one.
constexpr size_t kOriginalSaveSize = 139072, kSaveSignatureSize = 8;
size_t SaveBodySize(int mapSize);                                     // the file without the signature
int SaveFileMapSize(const std::vector<uint8_t>& file, size_t& bodySize); // 0: not a save this engine can read
void AppendSaveSignature(std::vector<uint8_t>& file, int mapSize);

} // namespace osf
