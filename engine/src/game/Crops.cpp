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

namespace {
std::vector<CropDef>& PackCropList()
{
    static std::vector<CropDef> crops;
    return crops;
}
std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

void SetPackCrops(std::vector<CropDef> crops) { PackCropList() = std::move(crops); }

bool HavePackCrops() { return !PackCropList().empty(); }

std::vector<std::string> CropFiles(const std::string& gameDir)
{
    std::vector<std::string> files;
    if (HavePackCrops())
    {
        for (const CropDef& c : PackCropList())
            files.push_back(Upper(c.fileName));
        std::sort(files.begin(), files.end());
        return files;
    }
    std::error_code ec;
    const auto dir = gameDir.empty() ? std::nullopt : FindFileNoCase(gameDir, "CROPS");
    if (!dir)
        return files;
    for (const auto& e : std::filesystem::directory_iterator(*dir, ec))
    {
        const std::string n = Upper(e.path().filename().string());
        if (n.size() > 4 && n.substr(n.size() - 4) == ".CRP")
            files.push_back(n);
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> DefaultCropFiles()
{
    if (HavePackCrops())
    {
        std::vector<std::string> files;
        for (size_t i = 0; i < 16 && i < PackCropList().size(); ++i)
            files.push_back(PackCropList()[i].fileName);
        return files;
    }
    return { "corn.crp", "wheat.crp", "barley.crp", "potatoes.crp", "oats.crp", "rice.crp", "sorghum.crp", "soybeans.crp",
             "cotton.crp", "onions.crp", "tomatoes.crp", "lettuce.crp", "carrots.crp", "strawber.crp", "sugar.crp", "peanuts.crp" };
}

std::vector<uint32_t> CropDef::IconArgb16() const
{
    std::vector<uint32_t> out;
    if (artArgb.empty() || artSize < 1 || artArgb.size() < static_cast<size_t>(14) * artSize * artSize)
        return out;
    const uint32_t* tile = &artArgb[static_cast<size_t>(13) * artSize * artSize];
    out.assign(256, 0xFF000000u);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            // the mean of the source pixels this one covers (a box filter, which is right for the shrinking and plain for
            // a small tile)
            const int x0 = x * artSize / 16, x1 = std::max(x0 + 1, (x + 1) * artSize / 16), y0 = y * artSize / 16, y1 = std::max(y0 + 1, (y + 1) * artSize / 16);
            uint32_t sum[3] = { 0, 0, 0 }, n = 0;
            for (int sy = y0; sy < y1 && sy < artSize; ++sy)
                for (int sx = x0; sx < x1 && sx < artSize; ++sx, ++n)
                    for (int k = 0; k < 3; ++k)
                        sum[k] += (tile[sy * artSize + sx] >> (16 - 8 * k)) & 0xFF;
            if (n)
                out[static_cast<size_t>(y) * 16 + x] = 0xFF000000u | ((sum[0] / n) << 16) | ((sum[1] / n) << 8) | (sum[2] / n);
        }
    return out;
}

bool LoadCropFile(const std::string& gameDir, const std::string& fileName, CropDef& out)
{
    if (HavePackCrops())
    {
        const std::string want = Upper(fileName);
        for (const CropDef& c : PackCropList())
            if (Upper(c.fileName) == want)
            {
                out = c;
                out.fileName = fileName; // (as the folder's loader keeps the name it was asked for)
                out.loaded = true;
                return true;
            }
        return false; // (a pack's crops are all there are)
    }
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
