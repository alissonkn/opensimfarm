// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace osf {

// The original resource archive: MEGADATA.IDX = records of name[16] + u32 offset (the last record is a sentinel
// whose offset equals the archive size); MEGADATA.EEA = the concatenated resources.
class Megadata
{
public:
    bool Open(const std::string& gameDir, std::string& error);
    bool Has(const std::string& name) const;
    // Returns the resource bytes, or an empty vector when missing. Names are matched case-insensitively.
    std::vector<uint8_t> Read(const std::string& name) const;
    const std::map<std::string, std::pair<uint32_t, uint32_t>>& Entries() const { return _entries; }

private:
    std::vector<uint8_t> _data;
    std::map<std::string, std::pair<uint32_t, uint32_t>> _entries; // lower-case name -> (offset, size)
};

} // namespace osf
