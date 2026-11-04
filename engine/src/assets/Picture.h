// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace osf {

// A 16-colour picture decoded to one palette index per pixel.
struct Picture
{
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool Empty() const { return w == 0 || h == 0; }
};

// Window background (.bmp): u16 width in bytes, u16 height, u16 planes (4), u16 unused, then the four
// planes one after another, MSB = leftmost pixel.
bool DecodePlanarBitmap(const std::vector<uint8_t>& data, Picture& out);

// LZSS-compressed files of the archive (.lzs).
std::vector<uint8_t> DecodeLzss(const std::vector<uint8_t>& data);

// Picture bank (.btl): u16 version, 16-byte name, u16 count, then records of u16 size + planar data
// 14 bytes per row (112 px wide), planes one after another.
bool DecodeBankRecord(const std::vector<uint8_t>& data, int index, Picture& out);

// Push-button faces (buttons.til): 18 faces of 48x24, 576 bytes each, planes one after another
// in this order: OK BUY SELL CANCEL GOTO USE EXIT LOAD NEW USED YES NO SAVE NEXT PREV CLOSE TOP RATS.
bool DecodeButtonFace(const std::vector<uint8_t>& data, int face, Picture& out, int widthBytes = 6, int height = 24);

// Sprite (.spr): u16 frames, height, bytes per row, bytes per plane, 0x1FF; then per frame a mask plane
// (1 = opaque) and four colour planes. Transparent pixels get index 255.
bool DecodeSprite(const std::vector<uint8_t>& data, int frame, Picture& out);

// Full-screen picture (.int, uncompressed ones): 640x480, for each row plane 0..3 of 80 bytes.
bool DecodeFullScreen(const std::vector<uint8_t>& data, Picture& out);

// UI font (egafont.fnt): 8x8 glyphs, one byte per row (MSB left); each character takes 32 bytes (the 8 rows
// stored four times), 120 characters starting with the space character.
struct Font8
{
    std::array<std::array<uint8_t, 8>, 256> glyphs{};
};
bool DecodeFont(const std::vector<uint8_t>& data, Font8& out);

} // namespace osf
