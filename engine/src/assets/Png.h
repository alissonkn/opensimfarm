// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Picture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace osf {

// PNG files (any colour type and bit depth) as truecolour pictures. Built on stb_image, PNG only, from memory, with
// dimensions capped at 16384 so that a bad file cannot ask for gigabytes.

// The size of a PNG without decoding it. False when `png` is not a readable PNG.
bool PngSize(const std::vector<uint8_t>& png, int& w, int& h);

// Decodes `png` into straight-alpha ARGB. False with `error` set when it cannot be decoded.
bool DecodePng(const std::vector<uint8_t>& png, RgbaPicture& out, std::string& error);

} // namespace osf
