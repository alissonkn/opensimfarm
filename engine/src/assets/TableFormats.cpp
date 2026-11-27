// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/TableFormats.h"

#include <cctype>
#include <sstream>

namespace osf {

bool DecodeClimateTable(const std::vector<uint8_t>& blob, std::vector<int>& out)
{
    out.clear();
    std::string text;
    for (uint8_t c : blob)
        text += (c == ',' || c == '\r' || c == '\n') ? ' ' : static_cast<char>(c);
    std::istringstream in(text);
    int v;
    while (in >> v && out.size() < static_cast<size_t>(kClimateWeeks))
        out.push_back(v);
    return out.size() == static_cast<size_t>(kClimateWeeks);
}

std::vector<uint16_t> DecodeTownLayout(const std::vector<uint8_t>& blob)
{
    std::vector<uint16_t> out;
    if (blob.size() < static_cast<size_t>(kTownLayoutWords) * 2)
        return out;
    for (size_t i = 0; i < static_cast<size_t>(kTownLayoutWords); ++i)
        out.push_back(static_cast<uint16_t>(blob[2 * i] | (blob[2 * i + 1] << 8)));
    return out;
}

std::vector<std::string> DecodeTextLines(const std::vector<uint8_t>& blob, bool blankPads)
{
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t c : blob)
    {
        if (c == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else if (c != '\r')
            cur += static_cast<char>(blankPads && (c == '@' || c == '~') ? ' ' : c);
    }
    lines.push_back(cur);
    return lines;
}

std::vector<std::string> EventLines(const std::vector<std::string>& lines, int id)
{
    std::vector<std::string> out;
    for (int k = 0; k < kEventShownLines; ++k)
    {
        const size_t i = static_cast<size_t>(id) * kEventLines + k;
        out.push_back(id >= 0 && i < lines.size() ? lines[i] : std::string());
    }
    return out;
}

std::vector<std::string> ItemLines(const std::vector<std::string>& lines, int id)
{
    return EventLines(lines, id); // (the same 8-line records)
}

std::map<std::string, std::vector<std::string>> DecodeScenarioGrids(const std::vector<uint8_t>& blob)
{
    std::map<std::string, std::vector<std::string>> grids;
    std::string section, line;
    for (uint8_t c : blob)
    {
        if (c == '\r')
            continue;
        if (c != '\n')
        {
            line += static_cast<char>(c);
            continue;
        }
        if (line.rfind("//", 0) == 0)
        {
            section.clear();
            for (char ch : line.substr(2))
                section += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        else if (!section.empty() && line.size() >= static_cast<size_t>(kRegionSize) + 1 && line[kRegionSize] == ' ')
            grids[section].push_back(line.substr(0, kRegionSize));
        line.clear();
    }
    return grids;
}

int OverviewIndex(const std::vector<uint8_t>& blob, int tile, int row, int column)
{
    const size_t at = static_cast<size_t>(2 * tile + row);
    if (tile < 0 || row < 0 || row > 1 || column < 0 || column > 1 || at >= blob.size())
        return 0;
    int idx = 0;
    for (int plane = 0; plane < 4; ++plane)
        idx |= ((blob[at] >> (6 - 2 * plane + (1 - column))) & 1) << plane;
    return idx;
}

uint32_t OverviewTable::Argb(int tile, int row, int column) const
{
    const size_t at = (static_cast<size_t>(tile) * 4 + static_cast<size_t>(row * 2 + column)) * 4;
    if (!rgb || tile < 0 || row < 0 || row > 1 || column < 0 || column > 1 || at + 3 >= blob.size())
        return 0xFF000000u;
    return 0xFF000000u | (static_cast<uint32_t>(blob[at]) << 16) | (static_cast<uint32_t>(blob[at + 1]) << 8) | blob[at + 2];
}

OverviewTable MakeOverview(const std::vector<uint8_t>& rgbEntry, const std::vector<uint8_t>& indexedEntry)
{
    OverviewTable t;
    if (rgbEntry.size() >= static_cast<size_t>(kOverviewRgbBytes))
    {
        t.blob = rgbEntry;
        t.rgb = true;
    }
    else
        t.blob = indexedEntry;
    return t;
}

int CardOpArguments(int op, bool& hasText)
{
    hasText = false;
    switch (op)
    {
    case 0x83: return 5;
    case 0x84: return 4;
    case 0x86: return 3;
    case 0x87: hasText = true; return 3;
    case 0x89: return 3;
    case 0x8a: hasText = true; return 4;
    case 0x8b: return 2;
    case 0x8c: return 5;
    default: return -1;
    }
}

std::vector<CardOp> DecodeCard(const std::vector<uint8_t>& d)
{
    std::vector<CardOp> ops;
    size_t i = 0;
    while (i < d.size() && d[i] != 0xFF)
    {
        bool hasText = false;
        const int n = CardOpArguments(d[i], hasText);
        if (n < 0)
            break;
        CardOp o;
        o.op = d[i++];
        for (int k = 0; k < n && i + 1 < d.size(); ++k, i += 2)
            o.a.push_back(d[i] | (d[i + 1] << 8));
        if (hasText)
        {
            while (i < d.size() && d[i])
                o.text += static_cast<char>(d[i++]);
            ++i;
        }
        ops.push_back(std::move(o));
    }
    return ops;
}

} // namespace osf
