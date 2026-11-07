// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Crops.h"
#include "core/Lang.h"
#include <cctype>

#include "core/Paths.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace osf {

std::string CropNameFor(const std::string& fileName, const std::string& name)
{
    std::string id = "crop:";
    for (char ch : std::filesystem::path(fileName).filename().string())
        id += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return TrId(id, name);
}

std::string CropName(const CropDef& c) { return CropNameFor(c.fileName, c.name); }

static std::string CropId(const char* kind, const std::string& fileName)
{
    std::string id = kind;
    for (char ch : std::filesystem::path(fileName).filename().string())
        id += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return id;
}

std::vector<std::string> CropHelp(const CropDef& c)
{
    const std::string tr = TrId(CropId("crophelp:", c.fileName), "");
    if (tr.empty())
        return c.help;
    return { tr };
}

std::string CropInfo(const CropDef& c)
{
    if (CurrentLanguage() == "en")
        return c.info;
    // the card's parts ('~' between them): the crop's name, then "Label: value"; a value's words and phrases are
    // translated where the dictionary has them (cropinfo:Low, cropinfo:Degree Days, ...)
    static const char* const kPhrases[] = { N_("cropinfo:Chill Hours"), N_("cropinfo:Degree Days"), N_("cropinfo:Avoid freezing"),
                                            N_("cropinfo:Do not freeze"), N_("cropinfo:Days"), N_("cropinfo:Low"),
                                            N_("cropinfo:High"), N_("cropinfo:Yes"), N_("cropinfo:No") };
    auto value = [&](std::string v) {
        for (const char* key : kPhrases)
        {
            const std::string en = std::strchr(key, ':') + 1;
            const std::string tr = TrId(key, en);
            for (size_t p = v.find(en); p != std::string::npos; p = v.find(en, p))
            {
                const bool startOk = p == 0 || !std::isalpha(static_cast<unsigned char>(v[p - 1]));
                const bool endOk = p + en.size() >= v.size() || !std::isalpha(static_cast<unsigned char>(v[p + en.size()]));
                if (startOk && endOk)
                {
                    v.replace(p, en.size(), tr);
                    p += tr.size();
                }
                else
                    ++p;
            }
        }
        return v;
    };
    std::string out, part;
    std::istringstream in(c.info);
    bool first = true;
    while (std::getline(in, part, '~'))
    {
        std::string trimmed = part;
        while (!trimmed.empty() && trimmed.back() == ' ')
            trimmed.pop_back();
        const size_t colon = trimmed.find(':');
        std::string shown;
        if (first && trimmed == c.name)
            shown = CropName(c);
        else if (colon != std::string::npos)
        {
            const std::string label = trimmed.substr(0, colon);
            static const char* const kLabels[] = { N_("cropinfo:Temperature"), N_("cropinfo:Rain"), N_("cropinfo:Pest Resistance"),
                                                   N_("cropinfo:Weed Resistance"), N_("cropinfo:Disease Resistance"),
                                                   N_("cropinfo:Harvest Storage Temp"), N_("cropinfo:Replant Every Harvest") };
            shown = label;
            for (const char* key : kLabels)
                if (label == std::strchr(key, ':') + 1)
                    shown = TrId(key, label);
            shown += ":" + value(trimmed.substr(colon + 1));
        }
        else
            shown = value(trimmed);
        out += (first ? "" : "~") + shown;
        first = false;
    }
    return out;
}

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
