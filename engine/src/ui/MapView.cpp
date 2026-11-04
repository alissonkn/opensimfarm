// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/MapView.h"
#include "ui/Gfx.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace osf {

SDL_Texture* MapView::BuildAtlas(SDL_Renderer* renderer, const TileSheet& sheet, const Palette& pal, bool transparent7)
{
    const int rows = (sheet.count + kAtlasColumns - 1) / kAtlasColumns;
    const int w = kAtlasColumns * 16, h = rows * 16;
    std::vector<uint32_t> rgba(static_cast<size_t>(w) * h, 0);
    for (int t = 0; t < sheet.count; ++t)
    {
        const uint8_t* px = sheet.Tile(t);
        const int ox = (t % kAtlasColumns) * 16, oy = (t / kAtlasColumns) * 16;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const uint8_t c = px[y * 16 + x];
                const uint32_t a = (transparent7 && c == 7) ? 0 : 255;
                rgba[static_cast<size_t>(oy + y) * w + ox + x] = (a << 24) | (pal[c][0] << 16) | (pal[c][1] << 8) | pal[c][2];
            }
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex)
        return nullptr;
    SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(tex, transparent7 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    return tex;
}

bool MapView::Init(SDL_Renderer* renderer, const MapGraphics& gfx)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest"); // keep the pixel art sharp at any zoom
    _plain = BuildAtlas(renderer, gfx.plain, gfx.palette, false);
    _masked = BuildAtlas(renderer, gfx.masked, gfx.palette, true);
    if (gfx.events.count >= 105)
        _events = BuildAtlas(renderer, gfx.events, gfx.palette, false);
    return _plain && _masked;
}

void MapView::SetPlainTiles(SDL_Renderer* renderer, const MapGraphics& gfx)
{
    if (SDL_Texture* t = BuildAtlas(renderer, gfx.plain, gfx.palette, false))
    {
        if (_plain)
            SDL_DestroyTexture(_plain);
        _plain = t;
    }
}

void MapView::SetEventTiles(SDL_Renderer* renderer, const MapGraphics& gfx)
{
    if (gfx.events.count < 105)
        return;
    if (SDL_Texture* t = BuildAtlas(renderer, gfx.events, gfx.palette, false))
    {
        if (_events)
            SDL_DestroyTexture(_events);
        _events = t;
    }
}

MapView::~MapView()
{
    if (_plain)
        SDL_DestroyTexture(_plain);
    if (_masked)
        SDL_DestroyTexture(_masked);
    if (_events)
        SDL_DestroyTexture(_events);
}

void MapView::ClampCamera()
{
    const float worldPx = kMapSize * 16.0f;
    const float visW = _viewW / _zoom, visH = _viewH / _zoom;
    _camX = visW >= worldPx ? (worldPx - visW) / 2 : std::clamp(_camX, 0.0f, worldPx - visW);
    _camY = visH >= worldPx ? (worldPx - visH) / 2 : std::clamp(_camY, 0.0f, worldPx - visH);
}

bool MapView::CellAt(int sx, int sy, int& cx, int& cy) const
{
    sx -= _left;
    sy -= _top;
    if (sx < 0 || sy < 0 || sx >= _viewW || sy >= _viewH)
        return false;
    // on the same pixel grid as Draw: the camera in whole screen pixels
    const double tile = 16.0 * _zoom;
    cx = static_cast<int>(std::floor((std::lround(_camX * _zoom) + sx) / tile));
    cy = static_cast<int>(std::floor((std::lround(_camY * _zoom) + sy) / tile));
    return cx >= 0 && cy >= 0 && cx < kMapSize && cy < kMapSize;
}

void MapView::HandleEvent(const SDL_Event& e)
{
    switch (e.type)
    {
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE)
        {
            _dragging = true;
            _lastMouseX = e.button.x;
            _lastMouseY = e.button.y;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE)
            _dragging = false;
        break;
    case SDL_MOUSEMOTION:
        if (_dragging)
        {
            _camX -= (e.motion.x - _lastMouseX) / _zoom;
            _camY -= (e.motion.y - _lastMouseY) / _zoom;
            _lastMouseX = e.motion.x;
            _lastMouseY = e.motion.y;
            ClampCamera();
        }
        break;
    case SDL_MOUSEWHEEL:
    {
        // zoom around the mouse pointer
        int mx, my;
        MouseState(&mx, &my);
        if (e.wheel.y != 0)
            ZoomStep(e.wheel.y > 0 ? 1 : -1, mx - _left, my - _top);
        break;
    }
    default:
        break;
    }
}

// zoom levels in window pixels per tile pixel (2 = the original at the usual 2x window): a tile is always a whole
// number of pixels (12 .. 48) and its pixels keep one size at the whole-number levels, so the picture never
// shimmers. Narrowed to half / double size (user finding Q14), then one more level out (Q29)
static constexpr float kZoomLevels[] = { 0.75f, 1.0f, 1.5f, 2.0f, 3.0f }; // 4 dropped (user, Q31)

void MapView::ZoomStep(int dir, int ax, int ay)
{
    const int n = static_cast<int>(sizeof kZoomLevels / sizeof kZoomLevels[0]);
    int cur = 0;
    for (int i = 0; i < n; ++i)
        if (std::fabs(kZoomLevels[i] - _zoom) < std::fabs(kZoomLevels[cur] - _zoom))
            cur = i;
    const int next = std::clamp(cur + dir, 0, n - 1);
    const float wx = _camX + ax / _zoom, wy = _camY + ay / _zoom;
    _zoom = kZoomLevels[next];
    _camX = wx - ax / _zoom;
    _camY = wy - ay / _zoom;
    ClampCamera();
}

void MapView::Update(float dt)
{
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const float speed = 600.0f / _zoom * dt;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
        _camX -= speed;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
        _camX += speed;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
        _camY -= speed;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
        _camY += speed;
    ClampCamera();
}

void MapView::Draw(SDL_Renderer* renderer, int width, int height)
{
    // the caller sets a viewport for the map area; width / height are its size
    _viewW = width;
    _viewH = height;
    ClampCamera();
    if (!_world)
        return;
    // the camera in whole screen pixels, so every tile lands on the same pixel grid
    const int offX = static_cast<int>(std::lround(_camX * _zoom)), offY = static_cast<int>(std::lround(_camY * _zoom));
    const int x0 = std::max(0, static_cast<int>(_camX / 16)), y0 = std::max(0, static_cast<int>(_camY / 16));
    const int x1 = std::min(kMapSize - 1, static_cast<int>((_camX + width / _zoom) / 16));
    const int y1 = std::min(kMapSize - 1, static_cast<int>((_camY + height / _zoom) / 16));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
        {
            const MapCell& c = _world->At(x, y);
            // whole-pixel destination edges so neighbouring tiles never leave seams
            const int dx = static_cast<int>(std::lround(x * 16 * _zoom)) - offX;
            const int dy = static_cast<int>(std::lround(y * 16 * _zoom)) - offY;
            const int dx2 = static_cast<int>(std::lround((x + 1) * 16 * _zoom)) - offX;
            const int dy2 = static_cast<int>(std::lround((y + 1) * 16 * _zoom)) - offY;
            SDL_Rect dst{ dx, dy, dx2 - dx, dy2 - dy };
            const int t = c.BaseTile();
            if (_events && _eventBank > 0 && t >= 483 && t < 518)
            {
                const int e = _eventBank * 35 + t - 483;
                SDL_Rect src{ (e % kAtlasColumns) * 16, (e / kAtlasColumns) * 16, 16, 16 };
                SDL_RenderCopy(renderer, _events, &src, &dst);
            }
            else if (t < 848)
            {
                SDL_Rect src{ (t % kAtlasColumns) * 16, (t / kAtlasColumns) * 16, 16, 16 };
                SDL_RenderCopy(renderer, _plain, &src, &dst);
            }
            if (c.HasObject())
            {
                const int o = c.OverlayTile();
                if (o >= 0 && o < 200)
                {
                    SDL_Rect src{ (o % kAtlasColumns) * 16, (o / kAtlasColumns) * 16, 16, 16 };
                    SDL_RenderCopy(renderer, _masked, &src, &dst);
                }
            }
        }
}

} // namespace osf
