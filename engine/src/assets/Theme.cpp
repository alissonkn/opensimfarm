// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/Theme.h"

#include "core/Json.h"

#include <cctype>
#include <fstream>
#include <iterator>

namespace osf {

const char* const kThemeColourNames[16] = { "black", "yellow", "sky", "red", "pink", "navy", "blue", "tan",
                                            "green", "darkGreen", "brown", "colour11", "lightGrey", "grey", "darkGrey", "white" };

Theme DefaultTheme()
{
    static const uint8_t rgb[16][3] = {
        { 0x00, 0x00, 0x00 }, { 0xFF, 0xE1, 0x4D }, { 0x55, 0xAA, 0xFF }, { 0xD0, 0x30, 0x30 }, { 0xF0, 0x80, 0xC0 }, { 0x1C, 0x2A, 0x7A },
        { 0x3A, 0x5F, 0xD0 }, { 0xD8, 0xC0, 0x8A }, { 0x3F, 0x9F, 0x3F }, { 0x1F, 0x6A, 0x2E }, { 0x80, 0x50, 0x2A }, { 0x70, 0xD8, 0xD8 },
        { 0xC4, 0xC4, 0xC4 }, { 0x86, 0x86, 0x86 }, { 0x48, 0x48, 0x48 }, { 0xFF, 0xFF, 0xFF } };
    Theme t{};
    for (int i = 0; i < 16; ++i)
        t[static_cast<size_t>(i)] = { rgb[i][0], rgb[i][1], rgb[i][2] };
    return t;
}

bool ParseColourText(const std::string& text, std::array<uint8_t, 3>& out)
{
    if ((text.size() != 4 && text.size() != 7) || text[0] != '#')
        return false;
    auto hex = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    const size_t digits = text.size() - 1, per = digits / 3;
    for (size_t k = 0; k < 3; ++k)
    {
        int v = 0;
        for (size_t d = 0; d < per; ++d)
        {
            const int h = hex(text[1 + k * per + d]);
            if (h < 0)
                return false;
            v = v * 16 + h;
        }
        out[k] = static_cast<uint8_t>(per == 1 ? v * 17 : v);
    }
    return true;
}

bool ParseTheme(const std::string& text, Theme& out, bool& present, std::string& error)
{
    present = false;
    out = DefaultTheme();
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
    const Json* block = root.Get("theme");
    if (!block)
        return true;
    if (!block->IsObject())
    {
        error = "tilepack.json: \"theme\" must be an object";
        return false;
    }
    Theme made = DefaultTheme();
    for (const auto& [name, value] : block->object)
    {
        int index = -1;
        for (int i = 0; i < 16; ++i)
            if (name == kThemeColourNames[i])
                index = i;
        if (index < 0)
        {
            error = "tilepack.json theme: \"" + name + "\" is not a colour of the interface (black, yellow, sky, red, pink, navy, blue, tan, green, darkGreen, "
                    "brown, colour11, lightGrey, grey, darkGrey, white)";
            return false;
        }
        if (!value.IsString() || !ParseColourText(value.string, made[static_cast<size_t>(index)]))
        {
            error = "tilepack.json theme: \"" + name + "\" must be a colour like \"#c4c4c4\" or \"#ccc\"";
            return false;
        }
    }
    out = made;
    present = true;
    return true;
}

bool LoadTheme(const std::string& dir, Theme& out, bool& present, std::string& error)
{
    present = false;
    out = DefaultTheme();
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParseTheme(text, out, present, error);
}

} // namespace osf
