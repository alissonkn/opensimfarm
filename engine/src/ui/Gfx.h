// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"
#include "assets/Picture.h"
#include "assets/PicturePack.h"
#include "assets/Theme.h"

#include <SDL.h>

#include <algorithm>
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

    void SetScale(int s)
    {
        _scale = s < 1 ? 1 : s;
        if (_plain.TrueColour() && TrueTileDetail() != _tileDetail)
            RemakeTiles(); // truecolour tiles are made at the size they are drawn at
        if (_pack && TrueTileDetail() != _packDetail)
            DropPackPictures(); // so are a pack's pictures (the archive's pictures stay)
    }
    // The truecolour pictures of a pack (assets/PicturePack.h) stand in for the archive's pictures of the same keys
    // from now on (null: back to the archive's). The pack must outlive its use here.
    void SetPicturePack(const PicturePack* pack);
    const PicturePack* PicturePackInUse() const { return _pack; }
    int Scale() const { return _scale; }
    // Modern Render for the interface: every picture (windows, buttons, icons, tiles, sprites, full screens, the
    // font) is kept at `detail` texels per interface pixel, upscaled with the pixel-art scaler (ui/PixelArt.h), except
    // the font, whose pixels are just repeated; 1 =
    // the original pixels, as the original draws them. Changing it remakes the pictures
    void SetDetail(int detail);
    // the font of all the text (Options > Advanced): 0 the game's own, else a modern one (ui/ModernFont.h)
    void SetFont(int font);
    int Font() const { return _fontChoice; }
    // a line in a given font (the Font window shows each font's name in it)
    void TextInFont(int font, int x, int y, const std::string& s, int c);
    int Detail() const { return _detail; }
    // headless check of Modern Render for the interface (--graphicstest): every kind of picture upscaled at each
    // detail, keeping its size and colours; 0 when all is well
    static int SelfTest(const Megadata& mega, const MapGraphics& map);
    SDL_Renderer* Renderer() const { return _r; }
    // drawing cut to a rectangle in interface units until ClearClip
    void Clip(int x, int y, int w, int h) { const SDL_Rect c = R(x, y, w, h); SDL_RenderSetClipRect(_r, &c); }
    void ClearClip() { SDL_RenderSetClipRect(_r, nullptr); }
    const Megadata* Mega() const { return _mega; }
    const Palette& Pal() const { return _pal; }
    // the RGB of one of the engine's interface colours (Colour): the theme's when a pack gave one, else for the
    // original the palette entry (for the Windows edition the Windows colour that stands for it)
    const std::array<uint8_t, 3>& Ui(int c) const { return _hasTheme ? _theme[static_cast<size_t>(c & 15)] : _pal[_ui[static_cast<size_t>(c & 15)]]; }
    // Colour number i (0..15) as opaque ARGB, for pictures drawn pixel by pixel from colour numbers (the crop icons, the
    // overview map): the theme's colour, else the palette's own entry i
    uint32_t Argb16(int i) const
    {
        const auto& p = _hasTheme ? _theme[static_cast<size_t>(i & 15)] : _pal[static_cast<size_t>(i & 15)];
        return 0xFF000000u | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
    }
    // The interface's colours as plain RGB (assets/Theme.h) from now on, in place of the palette's (null: the palette
    // again). The theme does not change with the weather, as the palette does
    void SetTheme(const Theme* theme)
    {
        _hasTheme = theme != nullptr;
        if (theme)
            _theme = *theme;
    }
    bool HasTheme() const { return _hasTheme; }
    // the play palette: 0 = simfarm.pal, 1 = rainpal.pal, 2 = frostpal.pal (they differ only in colour 7). Switching
    // remakes the tile atlas and the cached pictures, as the original reprograms the whole screen's palette
    void SetPlayPalette(int which);
    int PlayPalette() const { return _playPalette; }

    void Fill(int x, int y, int w, int h, int colour);
    void Frame(int x, int y, int w, int h, int colour);
    void Bevel(int x, int y, int w, int h, bool raised);   // light 15 / shadow 13 edges
    void Line(int x0, int y0, int x1, int y1, int colour);
    void DrawPicture(const std::string& name, int x, int y);                        // a .bmp from the archive
    bool PictureSize(const std::string& name, int& w, int& h);                      // its size in interface units
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
    static int WrapHeight(int w, const std::string& text); // the rows Wrap uses for the text
    void DimRect(int x, int y, int w, int h, int alpha);                            // darken (pressed look)
    void DrawFullScreen(const std::string& file, const std::string& palette, const SDL_Rect& dstPixels); // .int with its own palette
    void DrawSprite(const std::string& file, int frame, const SDL_Rect& dstPixels); // a .spr frame in window pixels
    void DrawPixels(const std::vector<uint32_t>& argb, int w, int h, int x, int y, int dw, int dh); // e.g. the overview map
    uint32_t TileColour(int id) const { return id >= 0 && id < static_cast<int>(_tileColour.size()) ? _tileColour[id] : 0xFF000000; }
    static int TextWidth(const std::string& s) { return static_cast<int>(s.size()) * 8; }
    // a text in [x, x + room): monospaced when it fits, else each letter as wide as its ink and one pixel apart, else
    // with no pixel between letters (a translation over a picture's label); align 0 left, 1 centre, 2 right
    // minGap 1: the letters keep a pixel between them however long the text (the status row: touching letters cannot
    // be read); the text may then be wider than room
    void TextFit(int x, int y, int room, const std::string& s, int colour, int align = 0, int minGap = 0);
    // the width TextFit draws s in (more than room when even the tightest spacing does not fit)
    static int FitWidth(const std::string& s, int room, int minGap = 0);

    // the font with the accented letters added (AddLatin1) and its ink widths noted for TextFit (Init does it; the
    // language test calls it without a window)
    static void PrepareFont(Font8& font);
    // an accented capital's mark reaches one row above the character cell (AddLatin1)
    static void SetCapitalMark(unsigned char ch, int glyph) { s_capMark[ch] = static_cast<uint8_t>(glyph); }
    static int InkWidth(unsigned char ch) { return ch <= 0x20 ? 4 : s_ink[ch][1] - s_ink[ch][0] + 1; }

private:
    static uint8_t s_ink[256][2];
    static uint8_t s_capMark[256];
    void DrawCapitalMark(unsigned char ch, int x, int y);
    // one letter in the cell at (x, y), in font `font` (ui/ModernFont.h; 0 or one that cannot be made: the game's)
    void Glyph(unsigned char ch, int x, int y, int font);
    SDL_Texture* FontAtlas(int font); // a modern font's letters at the resolution in use, made when first needed
    void TintFont(int c);
    SDL_Texture* PictureTexture(const std::string& key);
    // a picture's texture at the detail in use, in the palette pal (the play palette when null); cell = the size of
    // the separately upscaled cells of an atlas (a tile, a glyph; 0: the picture as a whole); without
    // `upscale` each pixel is just repeated (the font)
    SDL_Texture* MakeTexture(const Picture& p, int transparent, const Palette* pal = nullptr, int cell = 0, bool upscale = true);
    void MakeFont();
    void MakeMasked();
    // Truecolour tile sheets (a tile pack) are resampled to 16 * _tileDetail pixels a tile, the size they are drawn at
    // (an indexed sheet is upscaled by _detail like every other picture; _tileDetail is then _detail)
    int TrueTileDetail() const { return std::clamp(std::max(_detail, _scale), 1, 4); }
    void RemakeTiles();
    // Drops the picture textures (made again when next drawn); those of the pack too when `packToo`
    void ClearPictures(bool packToo);
    void DropPackPictures(); // only the pack's textures, and the detail they will be made at from now on
    struct PackMeta { int logicalW = 0, logicalH = 0, texW = 0, texH = 0; }; // a pack picture's texture and its interface size
    SDL_Texture* MakePackTexture(const std::string& key, const PackPicture& pic);
    const PackMeta* PackMetaOf(const std::string& key) const;
    SDL_Texture* MakeArgbTexture(const std::vector<uint32_t>& argb, int w, int h, bool blend);
    SDL_Rect TileSrc(int index) const; // tile `index` of an atlas (plain or masked) in texture pixels
    SDL_Rect Src(int x, int y, int w, int h) const { return SDL_Rect{ x * _detail, y * _detail, w * _detail, h * _detail }; }
    SDL_Rect R(int x, int y, int w, int h) const { return SDL_Rect{ x * _scale, y * _scale, w * _scale, h * _scale }; }

    SDL_Renderer* _r = nullptr;
    const Megadata* _mega = nullptr;
    Palette _pal{};
    std::array<uint8_t, 16> _ui{}; // interface colour -> palette index (the same for the DOS edition)
    Theme _theme{};                // the pack's theme, when _hasTheme
    bool _hasTheme = false;
    int _playPalette = 0;
    TileSheet _plain;             // the plain tiles of the atlas (kept to remake it with another palette)
    TileSheet _maskedSheet;       // the masked tiles (kept to remake their atlas at another detail)
    Font8 _font8;                 // the font with its added letters (kept likewise)
    int _detail = 1;
    int _scale = 1;
    int _tileDetail = 1;            // texels per interface pixel in the tile atlases (see TrueTileDetail)
    SDL_Texture* _font = nullptr;   // 16x16 glyph grid, white with alpha
    int _fontChoice = 1;                     // Options > Advanced (ui/ModernFont.h)
    int _fontAtlasDetail = 0;                // the pixels per unit the atlases below were made at
    std::vector<SDL_Texture*> _fontAtlases;  // per font, made when first needed
    uint8_t _tint[3] = { 0, 0, 0 };
    SDL_Texture* _tiles = nullptr;  // plain tile atlas
    SDL_Texture* _masked = nullptr; // masked tile atlas (kSeeThrough transparent)
    std::map<std::string, SDL_Texture*> _pictures;
    std::map<std::string, PackMeta> _packMeta; // the pictures above that come from the pack
    const PicturePack* _pack = nullptr;
    int _packDetail = 0;                       // texels per interface unit the pack's pictures were made at (<= TrueTileDetail)
    std::vector<uint32_t> _tileColour;  // average colour of each plain tile (overview map)
    SDL_Texture* _pixels = nullptr;
    int _pixelsW = 0, _pixelsH = 0;
};

} // namespace osf
