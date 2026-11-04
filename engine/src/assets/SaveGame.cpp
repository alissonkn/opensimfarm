// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/SaveGame.h"

#include <fstream>
#include <iterator>

namespace osf {

// Section sizes of the original save layout, in file order, up to the map.
static constexpr size_t kSectionsBeforeMap[] = {
    1,              // version
    2, 256 * 32,    // animals
    2, 127 * 20,    // 20-byte object table
    2, 127 * 56,    // 56-byte machine / storage table
    2, 98 * 68,     // fields
    2, 127 * 70,    // buildings
    2, 512 * 12,    // items / containers
    1, 16 * 8,      // town-event visitors
    98 * 240 * 2,   // schedule timeline
};
static constexpr size_t kSaveSize = 139072;

bool LoadWorldFromSave(const std::string& path, World& world, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() != kSaveSize)
    {
        error = "not an original SimFarm save/scenario: " + path;
        return false;
    }
    size_t off = 0;
    for (size_t s : kSectionsBeforeMap)
        off += s;
    // 9,216 cells x 5 bytes, stored cell by cell in map order: base word, overlay word, terrain byte.
    for (size_t i = 0; i < world.cells.size(); ++i)
    {
        const uint8_t* c = &d[off + i * 5];
        world.cells[i].base = static_cast<uint16_t>(c[0] | (c[1] << 8));
        world.cells[i].overlay = static_cast<uint16_t>(c[2] | (c[3] << 8));
        world.cells[i].terrain = c[4];
    }
    return true;
}

} // namespace osf
