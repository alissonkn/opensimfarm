// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <map>
#include <string>
#include <vector>

namespace osf {

// A parsed JSON value (RFC 8259), enough for the engine's own metadata files (tile packs). Objects keep their keys
// in a map: a duplicated key keeps its last value.
struct Json
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    bool IsObject() const { return type == Type::Object; }
    bool IsArray() const { return type == Type::Array; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    // the member `key` of an object, or null when this is no object or has no such member
    const Json* Get(const std::string& key) const;
    // typed reads of a member, `fallback` when it is missing or of another type
    int Int(const std::string& key, int fallback) const;
    std::string Str(const std::string& key, const std::string& fallback = std::string()) const;
};

// Parses `text` (UTF-8, an optional byte order mark is skipped). False with `error` ("line L: ...") on bad input,
// including nesting deeper than 64 levels and text after the value.
bool ParseJson(const std::string& text, Json& out, std::string& error);

} // namespace osf
