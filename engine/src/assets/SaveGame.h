// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace osf {

constexpr int kMapSize = 96;

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

// The map of an original saved game or prebuilt farm (.SFM / .SSM).
// Only the map is read for now; the rest of the state follows with the simulation milestones.
struct World
{
    std::vector<MapCell> cells = std::vector<MapCell>(kMapSize * kMapSize);
    MapCell& At(int x, int y) { return cells[static_cast<size_t>(x) * kMapSize + y]; } // column-major: x*96 + y
    const MapCell& At(int x, int y) const { return cells[static_cast<size_t>(x) * kMapSize + y]; }
};

bool LoadWorldFromSave(const std::string& path, World& world, std::string& error);

} // namespace osf
