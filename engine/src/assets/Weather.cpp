// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/Weather.h"

#include "assets/Theme.h"
#include "core/Json.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>

namespace osf {

WeatherLook DefaultWeatherLook()
{
    WeatherLook look;
    look.rain = { 0x30, 0x50, 0x8C, 77 };   // 30%
    look.frost = { 0xE8, 0xF4, 0xFF, 115 }; // 45%
    return look;
}

namespace {

bool Day(const Json& v, const char* name, WeatherTint& tint, std::string& error)
{
    const std::string where = std::string("tilepack.json weather: \"") + name + "\"";
    if (!v.IsObject())
    {
        error = where + " must be an object";
        return false;
    }
    for (const auto& [key, value] : v.object)
    {
        if (key == "colour")
        {
            std::array<uint8_t, 3> rgb{};
            if (!value.IsString() || !ParseColourText(value.string, rgb))
            {
                error = where + " \"colour\" must be a colour like \"#30508c\" or \"#35c\"";
                return false;
            }
            tint.r = rgb[0];
            tint.g = rgb[1];
            tint.b = rgb[2];
        }
        else if (key == "strength")
        {
            if (!value.IsNumber() || !(value.number >= 0 && value.number <= 1))
            {
                error = where + " \"strength\" must be a number from 0 to 1";
                return false;
            }
            tint.alpha = static_cast<uint8_t>(std::lround(value.number * 255));
        }
        else
        {
            error = where + " has \"" + key + "\"; only \"colour\" and \"strength\" are known";
            return false;
        }
    }
    return true;
}

} // namespace

bool ParseWeatherLook(const std::string& text, WeatherLook& out, bool& present, std::string& error)
{
    present = false;
    out = DefaultWeatherLook();
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
    const Json* block = root.Get("weather");
    if (!block)
        return true;
    if (!block->IsObject())
    {
        error = "tilepack.json: \"weather\" must be an object";
        return false;
    }
    WeatherLook made = DefaultWeatherLook();
    for (const auto& [name, value] : block->object)
    {
        if (name != "rain" && name != "frost")
        {
            error = "tilepack.json weather: \"" + name + "\" is not a kind of day (rain, frost)";
            return false;
        }
        if (!Day(value, name.c_str(), name == "rain" ? made.rain : made.frost, error))
            return false;
    }
    out = made;
    present = true;
    return true;
}

bool LoadWeatherLook(const std::string& dir, WeatherLook& out, bool& present, std::string& error)
{
    present = false;
    out = DefaultWeatherLook();
    std::ifstream f(dir + "/tilepack.json", std::ios::binary);
    if (!f)
    {
        error = "no tilepack.json in " + dir;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParseWeatherLook(text, out, present, error);
}

} // namespace osf
