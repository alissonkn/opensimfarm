// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "core/Json.h"

#include <cmath>
#include <cstdlib>

namespace osf {

const Json* Json::Get(const std::string& key) const
{
    if (type != Type::Object)
        return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

int Json::Int(const std::string& key, int fallback) const
{
    const Json* v = Get(key);
    if (!v || v->type != Type::Number || !std::isfinite(v->number) || std::fabs(v->number) > 2e9)
        return fallback;
    return static_cast<int>(v->number);
}

std::string Json::Str(const std::string& key, const std::string& fallback) const
{
    const Json* v = Get(key);
    return v && v->type == Type::String ? v->string : fallback;
}

namespace {

struct Parser
{
    const std::string& s;
    size_t i = 0;
    std::string error;

    explicit Parser(const std::string& text) : s(text) {}

    bool Fail(const std::string& what)
    {
        if (error.empty())
        {
            int line = 1;
            for (size_t k = 0; k < i && k < s.size(); ++k)
                line += s[k] == '\n';
            error = "line " + std::to_string(line) + ": " + what;
        }
        return false;
    }
    void Space()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    bool Literal(const char* word)
    {
        size_t k = 0;
        while (word[k])
            if (i + k >= s.size() || s[i + k] != word[k++])
                return false;
        i += k;
        return true;
    }
    static void Utf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800)
            out += { static_cast<char>(0xC0 | (cp >> 6)), static_cast<char>(0x80 | (cp & 0x3F)) };
        else if (cp < 0x10000)
            out += { static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F)) };
        else
            out += { static_cast<char>(0xF0 | (cp >> 18)), static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                     static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F)) };
    }
    bool Hex4(unsigned& v)
    {
        if (i + 4 > s.size())
            return Fail("bad \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k)
        {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }
    bool String(std::string& out)
    {
        ++i; // the opening quote
        for (;;)
        {
            if (i >= s.size())
                return Fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"')
                return true;
            if (c < 0x20)
                return Fail("control character in a string");
            if (c != '\\')
            {
                out += static_cast<char>(c);
                continue;
            }
            if (i >= s.size())
                return Fail("unterminated string");
            switch (s[i++])
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned cp;
                if (!Hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00) // a surrogate pair
                {
                    unsigned lo;
                    if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u')
                        return Fail("lone surrogate");
                    i += 2;
                    if (!Hex4(lo) || lo < 0xDC00 || lo >= 0xE000)
                        return Fail("lone surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                else if (cp >= 0xDC00 && cp < 0xE000)
                    return Fail("lone surrogate");
                Utf8(out, cp);
                break;
            }
            default: return Fail("bad escape");
            }
        }
    }
    bool Number(double& v)
    {
        const size_t start = i;
        if (i < s.size() && s[i] == '-')
            ++i;
        if (i < s.size() && s[i] == '0')
            ++i;
        else if (i < s.size() && s[i] >= '1' && s[i] <= '9')
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
        else
            return Fail("bad number");
        if (i < s.size() && s[i] == '.')
        {
            ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9')
                return Fail("bad number");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
        {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9')
                return Fail("bad number");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
        }
        v = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        return true;
    }
    bool Value(Json& out, int depth)
    {
        if (depth > 64)
            return Fail("nested too deeply");
        Space();
        if (i >= s.size())
            return Fail("unexpected end");
        const char c = s[i];
        if (c == '{')
        {
            ++i;
            out.type = Json::Type::Object;
            Space();
            if (i < s.size() && s[i] == '}')
                return ++i, true;
            for (;;)
            {
                Space();
                if (i >= s.size() || s[i] != '"')
                    return Fail("a member name was expected");
                std::string key;
                if (!String(key))
                    return false;
                Space();
                if (i >= s.size() || s[i] != ':')
                    return Fail("':' was expected");
                ++i;
                if (!Value(out.object[key], depth + 1))
                    return false;
                Space();
                if (i < s.size() && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}')
                    return ++i, true;
                return Fail("',' or '}' was expected");
            }
        }
        if (c == '[')
        {
            ++i;
            out.type = Json::Type::Array;
            Space();
            if (i < s.size() && s[i] == ']')
                return ++i, true;
            for (;;)
            {
                out.array.emplace_back();
                if (!Value(out.array.back(), depth + 1))
                    return false;
                Space();
                if (i < s.size() && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']')
                    return ++i, true;
                return Fail("',' or ']' was expected");
            }
        }
        if (c == '"')
        {
            out.type = Json::Type::String;
            return String(out.string);
        }
        if (c == 't' || c == 'f' || c == 'n')
        {
            if (Literal("true")) { out.type = Json::Type::Bool; out.boolean = true; return true; }
            if (Literal("false")) { out.type = Json::Type::Bool; out.boolean = false; return true; }
            if (Literal("null")) { out.type = Json::Type::Null; return true; }
            return Fail("unknown word");
        }
        out.type = Json::Type::Number;
        return Number(out.number);
    }
};

} // namespace

bool ParseJson(const std::string& text, Json& out, std::string& error)
{
    Parser p(text);
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        p.i = 3;
    out = Json();
    if (!p.Value(out, 0))
    {
        error = p.error;
        out = Json();
        return false;
    }
    p.Space();
    if (p.i != text.size())
    {
        p.Fail("text after the value");
        error = p.error;
        out = Json();
        return false;
    }
    return true;
}

} // namespace osf
