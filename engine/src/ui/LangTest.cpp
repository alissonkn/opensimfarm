// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
// --langtest: every built-in language is checked headless: its translations convert to the font's encoding, use only
// characters the font draws and keep the {0} {1} ... of the English text; the words printed over the pictures and
// the other texts drawn into fixed boxes fit their room.
#include "ui/LangTest.h"
#include "assets/Megadata.h"
#include "assets/Picture.h"
#include "core/Lang.h"
#include "ui/Gfx.h"
#include "ui/PictureText.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <cstdio>
#include <regex>
#include <set>
#include <sstream>

namespace osf {

namespace {

std::multiset<std::string> Holes(const std::string& s)
{
    std::multiset<std::string> out;
    static const std::regex hole("\\{[0-9]\\}");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), hole); it != std::sregex_iterator(); ++it)
        out.insert(it->str());
    return out;
}

std::vector<std::string> Lines(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line))
        out.push_back(line);
    return out;
}

int WrappedLines(const std::string& s, int room)
{
    std::istringstream in(s);
    std::string word, line;
    int n = 0;
    while (in >> word)
    {
        const std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && Gfx::TextWidth(next) > room)
        {
            ++n;
            line = word;
        }
        else
            line = next;
    }
    return n + (line.empty() ? 0 : 1);
}

// the BUY window's description: words on lines of 13 characters
int BuyLines(const std::string& s)
{
    std::istringstream in(s);
    std::string word, line;
    int n = 0;
    while (in >> word)
    {
        if (!line.empty() && line.size() + 1 + word.size() > 13)
        {
            ++n;
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    return n + (line.empty() ? 0 : 1);
}

// the text boxes of a Farm Expert card: width and English text, in order
std::vector<std::pair<int, std::string>> CardTexts(const Megadata& mega, int card)
{
    static const std::map<int, std::pair<int, bool>> spec = { { 0x83, { 5, false } }, { 0x84, { 4, false } }, { 0x86, { 3, false } },
                                                              { 0x87, { 3, true } }, { 0x89, { 3, false } }, { 0x8a, { 4, true } },
                                                              { 0x8b, { 2, false } }, { 0x8c, { 5, false } } };
    std::vector<std::pair<int, std::string>> out;
    const std::vector<uint8_t> d = mega.Read("card" + std::to_string(card) + ".hc");
    size_t i = 0;
    while (i < d.size() && d[i] != 0xFF)
    {
        const auto it = spec.find(d[i]);
        if (it == spec.end())
            break;
        const int op = d[i++];
        std::vector<int> a;
        for (int k = 0; k < it->second.first && i + 1 < d.size(); ++k, i += 2)
            a.push_back(d[i] | (d[i + 1] << 8));
        std::string text;
        if (it->second.second)
        {
            while (i < d.size() && d[i])
                text += static_cast<char>(d[i++]);
            ++i;
        }
        if (op == 0x87 && a.size() == 3)
            out.emplace_back(a[2], text);
    }
    return out;
}

} // namespace

int LangSelfTest(const Megadata& mega)
{
    Font8 font;
    if (!DecodeFont(mega.Read("egafont.fnt"), font))
    {
        std::printf("langtest: the font could not be read\n");
        return 1;
    }
    Gfx::PrepareFont(font);
    auto drawable = [&](unsigned char c) {
        if (c == ' ')
            return true;
        for (const uint8_t row : font.glyphs[c])
            if (row)
                return c >= 0x20 && c != 0x7F && (c < 0x80 || c >= 0xA0);
        return false;
    };

    // texts the engine draws into a box of fixed width with TextFit, by key, and the box's width
    static const std::pair<const char*, int> kBoxes[] = {
        { "season box:Winter", 48 }, { "season box:Spring", 48 }, { "season box:Summer", 48 }, { "season box:Fall", 48 },
        { "evaporation:Slow", 76 }, { "evaporation:Average", 76 }, { "evaporation:Fast", 76 },
        { "Mouse", 100 }, { "Right button", 100 }, { "Scroll", 100 }, { "Zoom", 100 }, { "Speed", 100 }, { "Save, load", 100 },
        { "Ctrl+1/2/3", 100 }, { "Esc", 100 }, { "Fields", 100 }, { "Water", 100 }, { "Examine", 100 }, { "Land", 100 },
        { "Start", 100 }, { "Crop duster", 100 },
    };

    int fails = 0, texts = 0;
    const std::vector<Dictionary> dicts = BuiltInDictionaries();
    for (const Dictionary& d : dicts)
    {
        auto fail = [&](const std::string& what, const std::string& text) {
            if (++fails <= 40)
                std::printf("FAIL %s: %s: %s\n", d.code.c_str(), what.c_str(), text.c_str());
        };
        for (const auto& [en, tr] : d.entries)
        {
            ++texts;
            bool lossless = true;
            const std::string l1 = Utf8ToLatin1(tr, &lossless);
            if (!lossless)
                fail("a character outside Latin-1", tr);
            for (const char ch : l1)
                if (ch != '\n' && !drawable(static_cast<unsigned char>(ch)))
                {
                    fail("a character the font does not draw", tr);
                    break;
                }
            if (en.rfind("crop:", 0) != 0 && Holes(en) != Holes(tr))
                fail("{0} {1} ... differ from the English", en);
            // texts of the game files: their boxes
            const size_t cut = l1.find('~');
            const std::string head = l1.substr(0, cut), rest = cut == std::string::npos ? std::string() : l1.substr(cut + 1);
            if (en.rfind("event:", 0) == 0)
            {
                std::string shown = l1;
                if (cut != std::string::npos && cut < 27)
                    shown = std::string((27 - cut) / 2, '`') + l1;
                if (Gfx::WrapHeight(224, shown) > 56)
                    fail("more than 7 lines in the message box", en);
            }
            else if (en.rfind("item:", 0) == 0)
            {
                std::string desc = rest;
                std::replace(desc.begin(), desc.end(), '~', ' ');
                if (BuyLines(desc) > 10)
                    fail("more than 10 lines in the BUY window", en);
                if (Gfx::FitWidth(head, 104) > 104)
                    fail("name wider than the BUY window's", en);
            }
            else if (en.rfind("crophelp:", 0) == 0 && BuyLines(l1) > 10)
                fail("more than 10 lines in the BUY window", en);
            else if (en.rfind("card:", 0) == 0)
            {
                const int card = std::atoi(en.c_str() + 5);
                const int box = std::atoi(en.c_str() + en.find(':', 5) + 1);
                const auto boxes = CardTexts(mega, card);
                if (box >= static_cast<int>(boxes.size()))
                    fail("no such text box", en);
                else
                {
                    const auto& [width, english] = boxes[static_cast<size_t>(box)];
                    const int was = Gfx::WrapHeight(width, english), now = Gfx::WrapHeight(width, l1);
                    if (now > (was <= 8 ? 8 : was + 16))
                        fail("longer than the card has room for (" + std::to_string(now / 8) + " lines, English " + std::to_string(was / 8) + ")", en);
                }
            }
        }
        SetLanguage(d.code);
        for (const PictureTextInfo& l : PictureTexts())
        {
            const std::string tr = Tr(l.english);
            if (tr == l.english)
                continue;
            if (l.maxLines > 0)
            {
                if (WrappedLines(tr, l.room) > l.maxLines)
                    fail("too long for " + l.picture + " (" + std::to_string(l.maxLines) + " lines)", Latin1ToUtf8(tr));
                continue;
            }
            const std::vector<std::string> lines = Lines(tr);
            if (static_cast<int>(lines.size()) > l.lines)
                fail("more lines than " + l.picture + " has room for", Latin1ToUtf8(tr));
            for (const std::string& line : lines)
                if (Gfx::FitWidth(line, l.room) > l.room)
                    fail("wider than its place in " + l.picture + " (" + std::to_string(l.room) + " px)", Latin1ToUtf8(line));
        }
        for (const auto& [key, room] : kBoxes)
            if (const std::string tr = Tr(std::string(key)); tr != key && Gfx::FitWidth(tr, room) > room)
                fail("wider than its box (" + std::to_string(room) + " px)", Latin1ToUtf8(tr));
    }
    SetLanguage("en");
    std::printf("langtest %s (%zu languages, %d texts, %zu picture labels, %d failures)\n", fails ? "FAIL" : "ok", dicts.size(),
                texts, PictureTexts().size(), fails);
    return fails ? 1 : 0;
}

} // namespace osf
