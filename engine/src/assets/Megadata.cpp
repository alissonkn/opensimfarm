// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/Megadata.h"

#include "core/Paths.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace osf {

static std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool Megadata::Open(const std::string& gameDir, std::string& error)
{
    auto idxPath = FindFileNoCase(gameDir, "MEGADATA.IDX");
    auto eeaPath = FindFileNoCase(gameDir, "MEGADATA.EEA");
    if (!idxPath || !eeaPath)
    {
        error = "MEGADATA.IDX / MEGADATA.EEA not found in " + gameDir;
        return false;
    }
    const auto idx = ReadFile(*idxPath);
    _data = ReadFile(*eeaPath);
    if (idx.size() < 40 || idx.size() % 20 != 0)
    {
        error = "MEGADATA.IDX has an unexpected size";
        return false;
    }
    const size_t count = idx.size() / 20;
    auto offsetOf = [&](size_t i) {
        const uint8_t* p = &idx[i * 20 + 16];
        return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
    };
    if (offsetOf(count - 1) != _data.size())
    {
        error = "MEGADATA.EEA does not match its index (different game version?)";
        return false;
    }
    for (size_t i = 0; i + 1 < count; ++i)
    {
        const char* n = reinterpret_cast<const char*>(&idx[i * 20]);
        size_t len = 0;
        while (len < 16 && n[len]) ++len;
        std::string name(n, len);
        _entries[Lower(name)] = { offsetOf(i), offsetOf(i + 1) - offsetOf(i) };
    }
    return true;
}

bool Megadata::Has(const std::string& name) const
{
    return _entries.count(Lower(name)) != 0;
}

std::vector<uint8_t> Megadata::Read(const std::string& name) const
{
    auto it = _entries.find(Lower(name));
    if (it == _entries.end())
        return {};
    const auto [off, size] = it->second;
    return std::vector<uint8_t>(_data.begin() + off, _data.begin() + off + size);
}

} // namespace osf
