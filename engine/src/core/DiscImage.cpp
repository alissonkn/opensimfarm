// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "core/DiscImage.h"
#include "core/InstallDisc.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace osf {

namespace {

constexpr size_t kSector = 2048;

uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

std::string Upper(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

struct Entry
{
    std::string name; // as on the disc, without the ";1"
    uint32_t lba = 0, size = 0;
    bool folder = false;
};

// an opened image: where the 2048 bytes of data of each sector are
struct Image
{
    std::ifstream in;
    size_t stride = 0, offset = 0; // bytes per sector in the file, and where its data starts
    Entry root;

    bool Read(uint32_t lba, uint8_t* out)
    {
        in.clear();
        in.seekg(static_cast<std::streamoff>(lba) * static_cast<std::streamoff>(stride) + static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(out), kSector);
        return static_cast<size_t>(in.gcount()) == kSector;
    }

    // the layout found from the primary volume descriptor (sector 16 on), and with it the top folder
    bool Open(const std::string& path)
    {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return false;
        in.open(path, std::ios::binary);
        if (!in)
            return false;
        // plain 2048-byte sectors, raw 2352-byte Mode 1 sectors (data after 16 bytes) and Mode 2 Form 1 ones (after 24)
        static const size_t kLayouts[][2] = { { 2048, 0 }, { 2352, 16 }, { 2352, 24 } };
        uint8_t s[kSector];
        for (const auto& l : kLayouts)
        {
            stride = l[0];
            offset = l[1];
            for (uint32_t lba = 16; lba < 32 && Read(lba, s); ++lba)
            {
                if (std::memcmp(s + 1, "CD001", 5) != 0 || s[0] == 255)
                    break;
                if (s[0] == 1)
                {
                    root = { "", Le32(s + 156 + 2), Le32(s + 156 + 10), true };
                    return root.size > 0;
                }
            }
        }
        return false;
    }

    // a folder's entries (not itself nor the one above it); false when the image is cut short
    bool List(const Entry& folder, std::vector<Entry>& out)
    {
        uint8_t s[kSector];
        for (uint32_t i = 0; i * kSector < folder.size; ++i)
        {
            if (!Read(folder.lba + i, s))
                return false;
            // no record runs over a sector's end; a zero length means the rest of the sector is empty
            for (size_t at = 0; at + 33 < kSector && s[at] != 0; at += s[at])
            {
                const uint8_t* r = s + at;
                const size_t nameLen = r[32];
                if (r[0] < 33 + nameLen || nameLen == 0 || (nameLen == 1 && r[33] <= 1))
                    continue;
                std::string name(reinterpret_cast<const char*>(r + 33), nameLen);
                if (const auto semi = name.find(';'); semi != std::string::npos)
                    name.resize(semi); // "SETUP.INF;1"
                if (!name.empty() && name.back() == '.')
                    name.pop_back(); // a name without an extension
                if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..")
                    continue;
                out.push_back({ name, Le32(r + 2), Le32(r + 10), (r[25] & 2) != 0 });
            }
        }
        return true;
    }

    // `folder` copied into `target` (with the folders in it, or only its files), skipping the files already there at
    // the same size
    bool Copy(const Entry& folder, const fs::path& target, std::string& error, bool folders = true)
    {
        std::vector<Entry> entries;
        if (!List(folder, entries))
        {
            error = "the disc image is cut short";
            return false;
        }
        std::error_code ec;
        fs::create_directories(target, ec);
        for (const Entry& e : entries)
        {
            const fs::path dest = target / e.name;
            if (e.folder)
            {
                if (folders && !Copy(e, dest, error))
                    return false;
                continue;
            }
            if (fs::file_size(dest, ec) == e.size && !ec)
                continue;
            ec.clear();
            std::vector<uint8_t> data(e.size);
            uint8_t s[kSector];
            for (uint32_t done = 0, k = 0; done < e.size; done += static_cast<uint32_t>(kSector), ++k)
            {
                if (!Read(e.lba + k, s))
                {
                    error = e.name + " could not be read from the disc image";
                    return false;
                }
                std::memcpy(data.data() + done, s, std::min<size_t>(kSector, e.size - done));
            }
            std::ofstream out(dest, std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out)
            {
                error = e.name + " could not be written to " + target.string();
                return false;
            }
        }
        return true;
    }
};

bool Has(const std::vector<Entry>& entries, const char* name)
{
    return std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return !e.folder && Upper(e.name) == name; });
}

bool HoldsGame(const std::vector<Entry>& entries) { return Has(entries, "MEGADATA.IDX") && Has(entries, "MEGADATA.EEA"); }

} // namespace

bool IsDiscImage(const std::string& path)
{
    Image img;
    return img.Open(path);
}

std::optional<std::string> GameFromDiscImage(const std::string& image, const std::string& target, std::string& error)
{
    Image img;
    std::vector<Entry> top;
    if (!img.Open(image) || !img.List(img.root, top))
    {
        error = "not a disc image, or one cut short";
        return std::nullopt;
    }
    // the game's own folder: the top one, or one in it
    if (HoldsGame(top))
        return img.Copy(img.root, target, error) ? std::optional<std::string>(target) : std::nullopt;
    for (const Entry& e : top)
    {
        std::vector<Entry> inside;
        if (e.folder && img.List(e, inside) && HoldsGame(inside))
            return img.Copy(e, target, error) ? std::optional<std::string>(target) : std::nullopt;
    }
    // the setup disc: its top folder's files copied out for a moment and unpacked like the disc itself
    if (Has(top, "SETUP.INF") && Has(top, "MEGADATA.EE_"))
    {
        const fs::path staging = fs::path(target).string() + ".disc";
        std::error_code ec;
        fs::remove_all(staging, ec);
        // (only the files: its folders hold the disc's extras)
        std::optional<std::string> copy;
        const bool ok = img.Copy(img.root, staging, error, false);
        if (ok && IsSetupDisc(staging.string()))
            copy = UnpackSetupDisc(staging.string(), target, error);
        fs::remove_all(staging, ec);
        return copy;
    }
    error = "the disc image holds no SimFarm (MEGADATA.IDX and MEGADATA.EEA, or the setup disc's packed files)";
    return std::nullopt;
}

} // namespace osf
