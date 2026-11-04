// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"
#include "assets/Picture.h"

#include <SDL.h>

#include <map>
#include <string>
#include <vector>

namespace osf {

class Megadata;

// Palette indices of the main palette used by the interface.
enum Colour : int
{
    kBlack = 0, kYellow = 1, kSky = 2, kRed = 3, kPink = 4, kNavy = 5, kBlue = 6, kTan = 7,
    kGreen = 8, kDarkGreen = 9, kBrown = 10, kLightGrey = 12, kGrey = 13, kDarkGrey = 14, kWhite = 15,
};

// The frame can be drawn at a whole-number scale into an offscreen picture that is then shrunk to the window
// (a window smaller than 640 x 400 at the chosen scale). Window mouse positions times this ratio give positions in
// the drawn picture; MouseState is SDL_GetMouseState already converted.
void SetMouseRatio(float r);
float MouseRatio();
// the frame drawn at the sides and bottom of the interface (user finding Q24): window pixels between the left window
// edge and the picture (there is none along the top)
void SetMouseOffset(int windowPixels);
int MouseOffset();
uint32_t MouseState(int* x, int* y);

// Interface drawing in the original 640x480 coordinate system, multiplied by an integer UI scale so the
// interface stays sharp on large screens. Pictures come from the player's MEGADATA archive.
class Gfx
{
public:
    bool Init(SDL_Renderer* r, const Megadata& mega, const MapGraphics& map);
    bool SetPlainTiles(const TileSheet& plain);   // (re)build the plain tile atlas, e.g. after crop pictures changed
    ~Gfx();

    void SetScale(int s) { _scale = s < 1 ? 1 : s; }
    int Scale() const { return _scale; }
    SDL_Renderer* Renderer() const { return _r; }
    const Megadata* Mega() const { return _mega; }
    const Palette& Pal() const { return _pal; }
    // the play palette: 0 = simfarm.pal, 1 = rainpal.pal, 2 = frostpal.pal (they differ only in colour 7). Switching
    // remakes the tile atlas and the cached pictures, as the original reprograms the whole screen's palette
    void SetPlayPalette(int which);
    int PlayPalette() const { return _playPalette; }

    void Fill(int x, int y, int w, int h, int colour);
    void Frame(int x, int y, int w, int h, int colour);
    void Bevel(int x, int y, int w, int h, bool raised);   // light 15 / shadow 13 edges
    void Line(int x0, int y0, int x1, int y1, int colour);
    void DrawPicture(const std::string& name, int x, int y);                        // a .bmp from the archive
    void DrawPicturePart(const std::string& name, int sx, int sy, int w, int h, int x, int y);
    void DrawBank(const std::string& bank, int index, int x, int y);                // a .btl record
    void DrawButton(int face, int x, int y);                                         // a 48x24 face of buttons.til
    void DrawTile(int id, int x, int y, int size = 16);                             // plain 16x16 tile
    void DrawAnyTile(int id, int x, int y, int size = 16);
    void DrawTileAt(int id, const SDL_Rect& dstPixels);                              // a plain tile in window pixels                          // plain (< 848) or masked (848..1047)
    void Text(int x, int y, const std::string& s, int colour);
    void TextRight(int right, int y, const std::string& s, int colour);
    void TextCentre(int centre, int y, const std::string& s, int colour);
    // smaller text: the engine's own 4 x 6 font, 5 pixels a character (palette names, weather summary; Q15, Q22)
    void SmallText(int x, int y, const std::string& s, int colour);
    static int SmallTextWidth(const std::string& s);
    // the game's text box: 8 px in from x, lines of (w - 8) / 8 characters, 8 px apart; words are split by spaces,
    // '@', line ends and control characters, '~' starts a new line, '`' prints as a blank
    void Wrap(int x, int y, int w, const std::string& text, int colour);
    void DimRect(int x, int y, int w, int h, int alpha);                            // darken (pressed look)
    void DrawFullScreen(const std::string& file, const std::string& palette, const SDL_Rect& dstPixels); // .int with its own palette
    void DrawSprite(const std::string& file, int frame, const SDL_Rect& dstPixels); // a .spr frame in window pixels
    void DrawPixels(const std::vector<uint32_t>& argb, int w, int h, int x, int y, int dw, int dh); // e.g. the overview map
    uint32_t TileColour(int id) const { return id >= 0 && id < static_cast<int>(_tileColour.size()) ? _tileColour[id] : 0xFF000000; }
    static int TextWidth(const std::string& s) { return static_cast<int>(s.size()) * 8; }

private:
    SDL_Texture* PictureTexture(const std::string& key);
    SDL_Texture* MakeTexture(const Picture& p, int transparent);
    SDL_Rect R(int x, int y, int w, int h) const { return SDL_Rect{ x * _scale, y * _scale, w * _scale, h * _scale }; }

    SDL_Renderer* _r = nullptr;
    const Megadata* _mega = nullptr;
    Palette _pal{};
    int _playPalette = 0;
    TileSheet _plain;             // the plain tiles of the atlas (kept to remake it with another palette)
    int _scale = 1;
    SDL_Texture* _font = nullptr;   // 16x16 glyph grid, white with alpha
    SDL_Texture* _tiles = nullptr;  // plain tile atlas
    SDL_Texture* _masked = nullptr; // masked tile atlas (colour 7 transparent)
    std::map<std::string, SDL_Texture*> _pictures;
    std::vector<uint32_t> _tileColour;  // average colour of each plain tile (overview map)
    SDL_Texture* _pixels = nullptr;
    int _pixelsW = 0, _pixelsH = 0;
};

} // namespace osf
