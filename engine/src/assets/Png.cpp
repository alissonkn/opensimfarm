// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/Png.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "third_party/stb_image.h"

namespace osf {

bool PngSize(const std::vector<uint8_t>& png, int& w, int& h)
{
    int comp = 0;
    w = h = 0;
    return !png.empty() && png.size() < 0x7FFFFFFF && stbi_info_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &comp) && w > 0 && h > 0;
}

bool DecodePng(const std::vector<uint8_t>& png, RgbaPicture& out, std::string& error)
{
    int w = 0, h = 0, comp = 0;
    if (!PngSize(png, w, h))
    {
        error = "not a readable PNG";
        return false;
    }
    stbi_uc* rgba = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &comp, 4);
    if (!rgba)
    {
        error = std::string("the PNG cannot be decoded: ") + stbi_failure_reason();
        return false;
    }
    RgbaPicture pic;
    pic.w = w;
    pic.h = h;
    pic.argb.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < pic.argb.size(); ++i)
    {
        const stbi_uc* p = rgba + i * 4;
        pic.argb[i] = (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
    }
    stbi_image_free(rgba);
    out = std::move(pic);
    return true;
}

} // namespace osf
