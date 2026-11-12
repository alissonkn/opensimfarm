// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "InstallDisc.h"
#include "Paths.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace osf {

namespace {

// ---- the packed format
// A 14-byte header ("KWAJ", 88 F0 27 D1, the method, the data's offset, flags for optional fields), then the data.
// Method 0 and 1 are stored (1: every byte XOR FF), 2 is LZSS as in the older "SZDD" files, 3 is LZ77 with Huffman
// codes. The bits are read from the top of each byte down; past the end of the data come zero bits.
class BitReader
{
public:
    BitReader(const uint8_t* data, size_t size) : _data(data), _bits(size * 8) {}

    unsigned Bits(int n)
    {
        unsigned v = 0;
        for (int i = 0; i < n; ++i, ++_pos)
        {
            const size_t byte = _pos >> 3;
            const unsigned bit = byte < _bits / 8 ? (_data[byte] >> (7 - (_pos & 7))) & 1u : 0u;
            v = (v << 1) | bit;
        }
        return v;
    }
    // true once a read went past the data: what it read is not part of the file
    bool Over() const { return _pos > _bits; }

private:
    const uint8_t* _data;
    size_t _bits;
    size_t _pos = 0;
};

// canonical Huffman codes: shorter codes first, equal lengths in symbol order
class Huffman
{
public:
    bool Build(const std::vector<int>& lens)
    {
        _count.fill(0);
        _symbols.clear();
        for (int l : lens)
        {
            if (l < 0 || l > 16)
                return false;
            ++_count[l];
        }
        _count[0] = 0;
        int left = 1;
        for (int l = 1; l <= 16; ++l)
        {
            left = left * 2 - _count[l];
            if (left < 0)
                return false; // more codes than the lengths allow
        }
        for (int l = 1; l <= 16; ++l)
            for (size_t s = 0; s < lens.size(); ++s)
                if (lens[s] == l)
                    _symbols.push_back(static_cast<int>(s));
        return !_symbols.empty();
    }

    int Read(BitReader& in) const
    {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= 16; ++l)
        {
            code |= static_cast<int>(in.Bits(1));
            const int n = _count[l];
            if (code - first < n)
                return _symbols[index + code - first];
            index += n;
            first = (first + n) << 1;
            code <<= 1;
        }
        return -1;
    }

private:
    std::array<int, 17> _count{};
    std::vector<int> _symbols;
};

// a table's code lengths, in one of four ways
bool ReadLengths(BitReader& in, unsigned type, size_t count, std::vector<int>& lens)
{
    lens.assign(count, 0);
    int c = 0;
    switch (type)
    {
    case 0: // all the same: as many bits as the symbols need
        c = count == 16 ? 4 : count == 32 ? 5 : count == 64 ? 6 : 8;
        for (auto& l : lens)
            l = c;
        return true;
    case 1: // the first in 4 bits, then each: 0 = the same, 10 = one more, 11 + 4 bits = new
        c = static_cast<int>(in.Bits(4));
        lens[0] = c;
        for (size_t i = 1; i < count; ++i)
        {
            if (in.Bits(1))
                c = in.Bits(1) ? static_cast<int>(in.Bits(4)) : c + 1;
            lens[i] = c;
        }
        return true;
    case 2: // the first in 4 bits, then each 2 bits: 0..2 = one less, the same, one more; 3 + 4 bits = new
        c = static_cast<int>(in.Bits(4));
        lens[0] = c;
        for (size_t i = 1; i < count; ++i)
        {
            const unsigned sel = in.Bits(2);
            c = sel == 3 ? static_cast<int>(in.Bits(4)) : c + static_cast<int>(sel) - 1;
            lens[i] = c;
        }
        return true;
    case 3: // each in 4 bits
        for (auto& l : lens)
            l = static_cast<int>(in.Bits(4));
        return true;
    default:
        return false;
    }
}

bool UnpackLzh(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    BitReader in(data, size);
    std::array<uint8_t, 4096> window;
    window.fill(0x20);
    unsigned pos = 0;

    unsigned types[6];
    for (auto& t : types)
        t = in.Bits(4); // six for byte alignment; the sixth is not used
    // match lengths after a match, match lengths after literals, literal run lengths, offsets' high bits, literals
    static const size_t kSizes[5] = { 16, 16, 32, 64, 256 };
    Huffman trees[5];
    for (int t = 0; t < 5; ++t)
    {
        std::vector<int> lens;
        if (!ReadLengths(in, types[t], kSizes[t], lens) || !trees[t].Build(lens))
            return false;
    }
    Huffman& afterMatch = trees[0];
    Huffman& afterLiterals = trees[1];
    Huffman& runLength = trees[2];
    Huffman& offsetHigh = trees[3];
    Huffman& literal = trees[4];

    auto put = [&](uint8_t b) {
        window[pos] = b;
        pos = (pos + 1) & 4095;
        out.push_back(b);
    };
    bool literalRun = false; // the last thing was a run of literals shorter than the longest one
    for (;;)
    {
        int len = (literalRun ? afterLiterals : afterMatch).Read(in);
        if (in.Over())
            return true;
        if (len < 0)
            return false;
        if (len > 0)
        {
            len += 2;
            literalRun = false;
            const int hi = offsetHigh.Read(in);
            const unsigned offset = (static_cast<unsigned>(hi) << 6) | in.Bits(6);
            if (in.Over())
                return true;
            if (hi < 0)
                return false;
            while (len-- > 0)
                put(window[(pos + 4096 - offset) & 4095]);
        }
        else
        {
            int run = runLength.Read(in);
            if (in.Over())
                return true;
            if (run < 0)
                return false;
            ++run;
            literalRun = run != 32;
            while (run-- > 0)
            {
                const int b = literal.Read(in);
                if (in.Over())
                    return true;
                if (b < 0)
                    return false;
                put(static_cast<uint8_t>(b));
            }
        }
    }
}

bool UnpackLzss(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    std::array<uint8_t, 4096> window;
    window.fill(0x20);
    unsigned pos = 4096 - 16;
    size_t i = 0;
    while (i < size)
    {
        const uint8_t flags = data[i++];
        for (int b = 0; b < 8 && i < size; ++b)
        {
            if (flags & (1 << b))
            {
                window[pos] = data[i++];
                out.push_back(window[pos]);
                pos = (pos + 1) & 4095;
            }
            else
            {
                if (i + 1 >= size)
                    return true;
                unsigned at = data[i] | ((data[i + 1] & 0xF0u) << 4);
                int len = (data[i + 1] & 0x0F) + 3;
                i += 2;
                while (len-- > 0)
                {
                    window[pos] = window[at];
                    out.push_back(window[pos]);
                    pos = (pos + 1) & 4095;
                    at = (at + 1) & 4095;
                }
            }
        }
    }
    return true;
}

std::string Lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Upper(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string Trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool ReadAll(const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool IsKwaj(const std::vector<uint8_t>& d)
{
    static const uint8_t kMagic[8] = { 'K', 'W', 'A', 'J', 0x88, 0xF0, 0x27, 0xD1 };
    return d.size() >= 14 && std::equal(kMagic, kMagic + 8, d.begin());
}

// one file the setup program copies: its name on the disc, its name and folder once installed, its size
struct DiscFile
{
    std::string packed, name, folder;
    uintmax_t size = 0;
};

// The setup script lists the files in sections, one line each:
//   1, corn.crp,,,, 1996-02-22,,,, !READONLY,,,,,, 3558,,,,,
// field 1 is the name, field 11 a new name for the installed copy (when it has one) and field 15 the size. The game's
// files are in three sections; the others are the setup program's and the encyclopedia's.
std::vector<DiscFile> GameFiles(const fs::path& setupInf)
{
    static const std::pair<const char*, const char*> kSections[] = { { "farm", "" }, { "crops", "CROPS" }, { "farmsound", "SOUND" } };
    std::vector<DiscFile> files;
    std::ifstream in(setupInf);
    std::string line;
    const char* folder = nullptr;
    while (std::getline(in, line))
    {
        line = Trim(line);
        if (!line.empty() && line[0] == '[')
        {
            folder = nullptr;
            const std::string section = Lower(line.substr(1, line.find(']') - 1));
            for (const auto& [name, dir] : kSections)
                if (section == name)
                    folder = dir;
            continue;
        }
        if (!folder || line.empty())
            continue;
        std::vector<std::string> fields;
        std::stringstream ss(line);
        for (std::string f; std::getline(ss, f, ',');)
            fields.push_back(Trim(f));
        if (fields.size() < 16 || fields[1].size() < 2)
            continue;
        DiscFile f;
        f.name = Upper(fields[11].empty() ? fields[1] : fields[11]);
        f.packed = fields[1].substr(0, fields[1].size() - 1) + "_";
        f.folder = folder;
        try { f.size = std::stoull(fields[15]); } catch (...) { f.size = 0; }
        if (f.name.find('/') == std::string::npos && f.name.find('\\') == std::string::npos && f.name.find("..") == std::string::npos)
            files.push_back(std::move(f));
    }
    return files;
}

} // namespace

bool UnpackKwaj(const std::vector<uint8_t>& packed, std::vector<uint8_t>& out)
{
    out.clear();
    if (!IsKwaj(packed))
        return false;
    const unsigned method = packed[8] | (packed[9] << 8);
    const size_t start = packed[10] | (packed[11] << 8);
    if (start > packed.size())
        return false;
    const uint8_t* data = packed.data() + start;
    const size_t size = packed.size() - start;
    switch (method)
    {
    case 0:
        out.assign(data, data + size);
        return true;
    case 1:
        for (size_t i = 0; i < size; ++i)
            out.push_back(static_cast<uint8_t>(data[i] ^ 0xFF));
        return true;
    case 2:
        return UnpackLzss(data, size, out);
    case 3:
        return UnpackLzh(data, size, out);
    default:
        return false;
    }
}

bool IsSetupDisc(const std::string& dir)
{
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec) || !FindFileNoCase(dir, "SETUP.INF"))
        return false;
    const auto mega = FindFileNoCase(dir, "MEGADATA.EE_");
    if (!mega)
        return false;
    std::ifstream in(*mega, std::ios::binary);
    char magic[4] = {};
    in.read(magic, 4);
    return in && std::string(magic, 4) == "KWAJ";
}

std::optional<std::string> UnpackSetupDisc(const std::string& disc, const std::string& target, std::string& error)
{
    const auto inf = FindFileNoCase(disc, "SETUP.INF");
    const std::vector<DiscFile> files = inf ? GameFiles(*inf) : std::vector<DiscFile>{};
    bool hasIdx = false, hasEea = false;
    for (const auto& f : files)
    {
        hasIdx = hasIdx || f.name == "MEGADATA.IDX";
        hasEea = hasEea || f.name == "MEGADATA.EEA";
    }
    if (!hasIdx || !hasEea)
    {
        error = "the disc's SETUP.INF does not list MEGADATA.IDX and MEGADATA.EEA";
        return std::nullopt;
    }
    std::error_code ec;
    for (const auto& f : files)
    {
        const fs::path dir = f.folder.empty() ? fs::path(target) : fs::path(target) / f.folder;
        const fs::path dest = dir / f.name;
        if (f.size && fs::file_size(dest, ec) == f.size && !ec)
            continue; // unpacked before
        ec.clear();
        const auto src = FindFileNoCase(disc, f.packed);
        if (!src)
        {
            // the game's own files must be there; the others (the read-me, the help) may be left out
            if (f.name == "MEGADATA.IDX" || f.name == "MEGADATA.EEA" || f.name == "WSIMFARM.EXE" || f.name == "SCEN0.SSM")
            {
                error = f.packed + " is missing from the disc";
                return std::nullopt;
            }
            continue;
        }
        std::vector<uint8_t> packed, data;
        if (!ReadAll(*src, packed))
        {
            error = f.packed + " could not be read from the disc";
            return std::nullopt;
        }
        if (IsKwaj(packed))
        {
            if (!UnpackKwaj(packed, data))
            {
                error = f.packed + " could not be unpacked";
                return std::nullopt;
            }
        }
        else
            data = std::move(packed); // stored as it is
        if (f.size && data.size() != f.size)
        {
            error = f.packed + " unpacked to " + std::to_string(data.size()) + " bytes instead of " + std::to_string(f.size);
            return std::nullopt;
        }
        fs::create_directories(dir, ec);
        const fs::path part = dest.string() + ".part";
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out)
            {
                error = "could not write " + dest.string();
                return std::nullopt;
            }
        }
        fs::rename(part, dest, ec);
        if (ec)
        {
            error = "could not write " + dest.string();
            return std::nullopt;
        }
    }
    return target;
}

} // namespace osf
