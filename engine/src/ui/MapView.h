// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"
#include "assets/SaveGame.h"

#include <SDL.h>

#include <cmath>

namespace osf {

// Draws the 96x96 world with the original tiles at any window size: free zoom (pixel-art scaling),
// scrolling by dragging, arrow keys or the window edge. Cell rule: plain tile,
// then the masked overlay tile when the cell's object flag is set.
class MapView
{
public:
    bool Init(SDL_Renderer* renderer, const MapGraphics& gfx);
    void SetPlainTiles(SDL_Renderer* renderer, const MapGraphics& gfx);   // after crop pictures changed
    void SetEventTiles(SDL_Renderer* renderer, const MapGraphics& gfx);   // after a palette change
    ~MapView();
    void SetWorld(const World* world) { _world = world; }
    void SetEventBank(int bank) { _eventBank = bank; } // the rodeo (1) / fair (2) pictures of the event ground, 0 = none

    void HandleEvent(const SDL_Event& e);
    void Update(float dt);
    void Draw(SDL_Renderer* renderer, int width, int height);

    // Cell under a window position, or false when outside the map.
    bool CellAt(int sx, int sy, int& cx, int& cy) const;
    float Zoom() const { return _zoom; }
    // one step up or down the zoom levels, keeping the world point under (ax, ay) (map-area pixels) in place
    void ZoomStep(int dir, int ax, int ay);
    // visible cell rectangle and re-centring, for the overview map
    void VisibleCells(float& x0, float& y0, float& w, float& h) const { x0 = _camX / 16; y0 = _camY / 16; w = _viewW / _zoom / 16; h = _viewH / _zoom / 16; }
    void SetTopLeft(int cx, int cy) { _camX = cx * 16.0f; _camY = cy * 16.0f; ClampCamera(); }
    void SetCamera(int px, int py) { _camX = static_cast<float>(px); _camY = static_cast<float>(py); ClampCamera(); }
    void CenterOn(float cx, float cy) { _camX = cx * 16 - _viewW / _zoom / 2; _camY = cy * 16 - _viewH / _zoom / 2; ClampCamera(); }
    // window-pixel rectangle covering cells (cx, cy) .. (cx + w - 1, cy + h - 1)
    SDL_Rect CellRect(int cx, int cy, int w = 1, int h = 1) const
    {
        const long ox = std::lround(_camX * _zoom), oy = std::lround(_camY * _zoom); // as Draw
        const int x0 = static_cast<int>(std::lround(cx * 16 * _zoom) - ox) + _left, y0 = static_cast<int>(std::lround(cy * 16 * _zoom) - oy) + _top;
        const int x1 = static_cast<int>(std::lround((cx + w) * 16 * _zoom) - ox) + _left, y1 = static_cast<int>(std::lround((cy + h) * 16 * _zoom) - oy) + _top;
        return SDL_Rect{ x0, y0, x1 - x0, y1 - y0 };
    }
    void SetOrigin(int left, int top) { _left = left; _top = top; } // the map area's top-left in window pixels

private:
    SDL_Texture* BuildAtlas(SDL_Renderer* renderer, const TileSheet& sheet, const Palette& pal, bool transparent7);
    void ClampCamera();

    const World* _world = nullptr;
    SDL_Texture* _plain = nullptr;
    SDL_Texture* _masked = nullptr;
    SDL_Texture* _events = nullptr;
    int _eventBank = 0;
    float _camX = 40 * 16, _camY = 30 * 16; // world pixel at the window's top-left
    float _zoom = 2.0f;
    int _viewW = 1280, _viewH = 720;
    bool _dragging = false;
    int _lastMouseX = 0, _lastMouseY = 0;
    int _left = 0, _top = 0;
    static constexpr int kAtlasColumns = 32;
};

} // namespace osf
