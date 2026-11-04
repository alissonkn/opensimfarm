// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Crops.h"

#include "core/Paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace osf {

bool LoadCropFile(const std::string& gameDir, const std::string& fileName, CropDef& out)
{
    auto cropsDir = FindFileNoCase(gameDir, "CROPS");
    if (!cropsDir)
        return false;
    auto path = FindFileNoCase(*cropsDir, fileName);
    if (!path)
        return false;
    std::ifstream f(*path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 5396 + 70)
        return false;
    std::string name(reinterpret_cast<const char*>(&d[5376]), 20);
    name = name.substr(0, name.find('\0'));
    while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
        name.pop_back();
    out.name = name;
    out.regrowAge = d[5376 + 18];
    out.fileName = fileName;
    std::copy(d.begin() + 5396, d.begin() + 5396 + 70, out.p.begin());
    out.art.assign(d.begin(), d.begin() + 1024);
    out.art.insert(out.art.end(), d.begin() + 4608, d.begin() + 5376);
    // the item help card after the parameters: "Name~~" then seven '@'-padded CRLF lines (the text ends at the
    // first empty one)
    out.help.clear();
    const std::string text(d.begin() + 5466, d.end());
    const size_t title = text.find("~~");
    if (title != std::string::npos)
    {
        size_t pos = text.find("\r\n", title);
        bool ended = false;
        for (int k = 0; k < 7 && pos != std::string::npos && pos + 2 < text.size(); ++k)
        {
            const size_t end = text.find("\r\n", pos + 2);
            std::string line = text.substr(pos + 2, (end == std::string::npos ? text.size() : end) - pos - 2);
            line.erase(std::remove(line.begin(), line.end(), '@'), line.end());
            ended = ended || line.empty();
            if (!ended)
                out.help.push_back(line);
            pos = end;
        }
        // then the Crop Information card (Farm Expert): '@'-padded CRLF lines up to an empty one, joined with
        // spaces; '~' marks the card's own line breaks
        out.info.clear();
        while (pos != std::string::npos && pos + 2 < text.size())
        {
            const size_t end = text.find("\r\n", pos + 2);
            std::string line = text.substr(pos + 2, (end == std::string::npos ? text.size() : end) - pos - 2);
            line.erase(std::remove(line.begin(), line.end(), '@'), line.end());
            if (line.empty())
                break;
            out.info += (out.info.empty() ? "" : " ") + line;
            pos = end;
        }
    }
    out.loaded = true;
    return true;
}

} // namespace osf
