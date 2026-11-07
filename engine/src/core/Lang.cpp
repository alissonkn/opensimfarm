// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "core/Lang.h"
#include "core/Paths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace osf {

// the dictionaries in engine/lang/, built into the program (lang_builtin.cpp, made by cmake/EmbedLang.cmake)
struct BuiltinLang
{
    const char* file;
    const unsigned char* data;
    unsigned long size;
};
extern const BuiltinLang kBuiltinLangs[];
extern const int kBuiltinLangCount;

namespace {

// a .po file: msgid / msgstr pairs (each may continue on "..." lines), comments and the header entry; msgctxt and
// plural forms are not used by the engine
Dictionary ParsePo(const std::string& text, const std::string& code)
{
    Dictionary d;
    d.code = code;
    std::string id, str, *cur = nullptr;
    bool have = false;
    auto unquote = [](const std::string& s) {
        std::string out;
        const size_t a = s.find('"'), b = s.rfind('"');
        if (a == std::string::npos || b <= a)
            return out;
        for (size_t i = a + 1; i < b; ++i)
        {
            if (s[i] == '\\' && i + 1 < b)
            {
                const char e = s[++i];
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            }
            else
                out += s[i];
        }
        return out;
    };
    auto flush = [&]() {
        if (have)
        {
            if (id.empty())
            {
                // the header: "X-Language-Name: ..." names the language in its own words
                std::istringstream h(str);
                std::string line;
                while (std::getline(h, line))
                    if (line.rfind("X-Language-Name:", 0) == 0)
                    {
                        d.name = line.substr(16);
                        d.name.erase(0, d.name.find_first_not_of(' '));
                    }
            }
            else if (!str.empty())
                d.entries.emplace_back(id, str);
        }
        id.clear();
        str.clear();
        cur = nullptr;
        have = false;
    };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos || line[s] == '#')
            continue;
        line = line.substr(s);
        if (line.rfind("msgid ", 0) == 0)
        {
            flush();
            have = true;
            id = unquote(line);
            cur = &id;
        }
        else if (line.rfind("msgstr ", 0) == 0)
        {
            str = unquote(line);
            cur = &str;
        }
        else if (line[0] == '"' && cur)
            *cur += unquote(line);
    }
    flush();
    if (d.name.empty())
        d.name = code;
    return d;
}

std::string FileCode(const std::string& file)
{
    std::string c = std::filesystem::path(file).stem().string();
    return c;
}

std::vector<Dictionary>& Loaded()
{
    static std::vector<Dictionary> all;
    static bool done = false;
    if (!done)
    {
        done = true;
        for (int i = 0; i < kBuiltinLangCount; ++i)
            all.push_back(ParsePo(std::string(reinterpret_cast<const char*>(kBuiltinLangs[i].data), kBuiltinLangs[i].size),
                                  FileCode(kBuiltinLangs[i].file)));
        // the settings folder's languages/*.po: new languages, or a newer copy of a built-in one
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path(ConfigDir()) / "languages";
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        {
            if (e.path().extension() != ".po")
                continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            Dictionary d = ParsePo(ss.str(), FileCode(e.path().string()));
            auto same = std::find_if(all.begin(), all.end(), [&](const Dictionary& x) { return x.code == d.code; });
            if (same != all.end())
                *same = std::move(d);
            else
                all.push_back(std::move(d));
        }
    }
    return all;
}

std::string g_code = "en";
std::map<std::string, std::string> g_table; // English (as in the code) -> translation (Latin-1)

} // namespace

std::string Utf8ToLatin1(const std::string& s, bool* lossless)
{
    std::string out;
    bool ok = true;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c;
        int n = 1;
        if (c >= 0xF0 && i + 3 < s.size())
            cp = ((c & 7u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu), n = 4;
        else if (c >= 0xE0 && i + 2 < s.size())
            cp = ((c & 15u) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu), n = 3;
        else if (c >= 0xC0 && i + 1 < s.size())
            cp = ((c & 31u) << 6) | (s[i + 1] & 0x3Fu), n = 2;
        else if (c >= 0x80)
            cp = '?', ok = false;
        i += static_cast<size_t>(n);
        if (cp == 0x2018 || cp == 0x2019)
            out += '\'';
        else if (cp == 0x201C || cp == 0x201D)
            out += '"';
        else if (cp == 0x2013 || cp == 0x2014)
            out += '-';
        else if (cp == 0x2026)
            out += "...";
        else if (cp == 0xA0)
            out += ' ';
        else if (cp < 0x100)
            out += static_cast<char>(cp);
        else
            out += '?', ok = false;
    }
    if (lossless)
        *lossless = ok;
    return out;
}

std::string Latin1ToUtf8(const std::string& s)
{
    std::string out;
    for (const char ch : s)
    {
        const auto u = static_cast<unsigned char>(ch);
        if (u < 0x80)
            out += ch;
        else
        {
            out += static_cast<char>(0xC0 | (u >> 6));
            out += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
    return out;
}

std::string SortKey(const std::string& s)
{
    // Latin-1 0xC0..0xFF to their base letters (0xD7 and 0xF7 are signs, kept apart at the end)
    static const char kBase[] = "aaaaaaaceeeeiiiidnooooo*ouuuuyts"
                                "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string out;
    for (const char ch : s)
    {
        const auto u = static_cast<unsigned char>(ch);
        if (u >= 0xC0)
            out += kBase[u - 0xC0];
        else
            out += static_cast<char>(std::tolower(u));
    }
    return out;
}

std::vector<Dictionary> BuiltInDictionaries()
{
    std::vector<Dictionary> out;
    for (int i = 0; i < kBuiltinLangCount; ++i)
        out.push_back(ParsePo(std::string(reinterpret_cast<const char*>(kBuiltinLangs[i].data), kBuiltinLangs[i].size),
                              FileCode(kBuiltinLangs[i].file)));
    return out;
}

std::vector<LanguageInfo> AvailableLanguages()
{
    std::vector<LanguageInfo> out;
    for (const Dictionary& d : Loaded())
        if (d.code != "en")
            out.push_back({ d.code, Utf8ToLatin1(d.name) });
    std::sort(out.begin(), out.end(), [](const LanguageInfo& a, const LanguageInfo& b) { return a.name < b.name; });
    out.insert(out.begin(), { "en", "English" });
    return out;
}

void SetLanguage(const std::string& code)
{
    g_table.clear();
    g_code = "en";
    for (const Dictionary& d : Loaded())
        if (d.code == code && code != "en")
        {
            g_code = code;
            for (const auto& [en, tr] : d.entries)
                g_table[en] = Utf8ToLatin1(tr);
        }
}

const std::string& CurrentLanguage() { return g_code; }

std::string SystemLanguage()
{
    std::string loc;
#ifdef _WIN32
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
        for (const wchar_t* p = name; *p; ++p)
            loc += *p == L'-' ? '_' : static_cast<char>(*p);
#else
    for (const char* var : { "LC_ALL", "LC_MESSAGES", "LANG" })
        if (const char* v = std::getenv(var); v && *v)
        {
            loc = v;
            break;
        }
#endif
    loc = loc.substr(0, loc.find_first_of(".@"));
    if (loc.empty() || loc == "C" || loc == "POSIX")
        return "en";
    const std::string lang = loc.substr(0, loc.find('_'));
    std::string partial;
    for (const LanguageInfo& l : AvailableLanguages())
    {
        if (l.code == loc)
            return l.code;
        if (partial.empty() && l.code.substr(0, l.code.find('_')) == lang)
            partial = l.code;
    }
    return partial.empty() ? "en" : partial;
}

const char* Tr(const char* english)
{
    if (g_table.empty() || !english)
        return english;
    const auto it = g_table.find(english);
    return it == g_table.end() ? english : it->second.c_str();
}

std::string Tr(const std::string& english)
{
    if (g_table.empty())
        return english;
    const auto it = g_table.find(english);
    return it == g_table.end() ? english : it->second;
}

std::string TrFormat(const char* english, const std::vector<std::string>& args)
{
    const std::string f = Tr(english);
    std::string out;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] == '{' && i + 2 < f.size() && std::isdigit(static_cast<unsigned char>(f[i + 1])) && f[i + 2] == '}')
        {
            const size_t k = static_cast<size_t>(f[i + 1] - '0');
            if (k < args.size())
                out += args[k];
            i += 2;
        }
        else
            out += f[i];
    }
    return out;
}

std::string TrId(const std::string& id, const std::string& fallback)
{
    if (g_table.empty())
        return fallback;
    const auto it = g_table.find(id);
    return it == g_table.end() ? fallback : it->second;
}

} // namespace osf
