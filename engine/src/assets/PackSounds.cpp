// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/PackSounds.h"

#include "core/Json.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace osf {

namespace {

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool EndsWith(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

} // namespace

bool ParsePackSounds(const std::string& text, PackReader read, void* user, TableBlobs& out, std::string& error)
{
    Json root;
    if (!ParseJson(text, root, error))
    {
        error = "tilepack.json: " + error;
        return false;
    }
    if (!root.IsObject() || root.Int("format", 0) != 1)
    {
        error = "tilepack.json: \"format\" must be 1";
        return false;
    }
    const Json* block = root.Get("sounds");
    if (!block)
        return true;
    if (!block->IsObject())
    {
        error = "tilepack.json: \"sounds\" must be an object";
        return false;
    }
    TableBlobs made;
    for (const auto& [rawKey, value] : block->object)
    {
        const std::string key = Lower(rawKey), where = "tilepack.json sounds \"" + rawKey + "\"";
        const bool effect = EndsWith(key, ".voc"), song = EndsWith(key, ".xmi");
        if ((!effect && !song) || key.size() > 16 || key.find_first_of("/\\") != std::string::npos)
        {
            error = where + " is not a sound the game asks for: an effect ends in .voc, a song in .xmi (at most 16 characters)";
            return false;
        }
        if (!value.IsString() || !IsPackFileName(value.string))
        {
            error = where + " must be the name of a file in the pack";
            return false;
        }
        const std::string file = Lower(value.string);
        if (effect ? !EndsWith(file, ".wav") : !(EndsWith(file, ".mid") || EndsWith(file, ".midi")))
        {
            error = where + (effect ? " must be a .wav file" : " must be a .mid file");
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!read(user, value.string, bytes) || bytes.empty() || bytes.size() > 8u * 1024 * 1024)
        {
            error = value.string + " cannot be read (or is over 8 MB)";
            return false;
        }
        const bool wave = bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WAVE", 4) == 0;
        const bool midi = bytes.size() >= 14 && std::memcmp(bytes.data(), "MThd", 4) == 0;
        if (effect ? !wave : !midi)
        {
            error = value.string + (effect ? " is not a WAVE file" : " is not a standard MIDI file");
            return false;
        }
        made[key] = std::move(bytes);
    }
    for (auto& [name, bytes] : made)
        out[name] = std::move(bytes);
    return true;
}

namespace {
struct DirReader { const std::string& dir; };
bool ReadFromDir(void* user, const std::string& file, std::vector<uint8_t>& bytes)
{
    return ReadPackFile(static_cast<const DirReader*>(user)->dir, file, bytes);
}
} // namespace

bool LoadPackSounds(const std::string& dir, TableBlobs& out, std::string& error)
{
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    DirReader reader{ dir };
    return ParsePackSounds(text, ReadFromDir, &reader, out, error);
}

} // namespace osf
