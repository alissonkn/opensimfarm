// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/Graphics.h"
#include "assets/SaveGame.h"

#include <SDL.h>

#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

namespace osf {

// Draws the 96x96 world with the original tiles at any window size: free zoom (pixel-art scaling),
// scrolling by dragging, arrow keys or the window edge. Cell rule: plain tile,
// then the masked overlay tile when the cell's object flag is set.
// Modern Render (an engine option): the tiles are redrawn smoothly for the zoom in use (ui/PixelArt.h), and the
// things that walk or drive (animals, machines, cars) glide from cell to cell instead of jumping. Only the drawing
// changes: the simulation, the cells and the player's files stay as they are.
class MapView
{
public:
    bool Init(SDL_Renderer* renderer, const MapGraphics& gfx);
    void SetPlainTiles(SDL_Renderer* renderer, const MapGraphics& gfx);   // after crop pictures changed
    void SetEventTiles(SDL_Renderer* renderer, const MapGraphics& gfx);   // after a palette change
    ~MapView();
    void SetModern(bool on);
    bool Modern() const { return _modern; }
    // a thing drawn over the map at cells (cx, cy) with size w x h cells (the tornado, the swarm): its window
    // rectangle, gliding to a new position like the map's movers when Modern Render is on. `id` names the thing
    SDL_Rect SmoothCellRect(int id, int cx, int cy, int w, int h) { return SmoothCellRectAt(id, cx, cy, w, h, Now()); }
    SDL_Rect SmoothCellRectAt(int id, int cx, int cy, int w, int h, double now);
    // headless check of the smoothing and of the glide tracking (--graphicstest); 0 when all is well
    static int SelfTest(const MapGraphics& gfx);
    void SetWorld(const World* world) { _world = world; }
    int MapSize() const { return _world ? _world->size : kOriginalMapSize; }
    void SetEventBank(int bank) { _eventBank = bank; } // the rodeo (1) / fair (2) pictures of the event ground, 0 = none

    void HandleEvent(const SDL_Event& e);
    void Update(float dt);
    void Draw(SDL_Renderer* renderer, int width, int height);

    // Cell under a window position, or false when outside the map.
    bool CellAt(int sx, int sy, int& cx, int& cy) const;
    float Zoom() const { return _zoom; }
    // one step up or down the zoom levels, keeping the world point under (ax, ay) (map-area pixels) in place
    void ZoomStep(int dir, int ax, int ay);
    void ZoomAt(int dir, int sx, int sy) { ZoomStep(dir, sx - _left, sy - _top); } // around a window point (pinch)
    // any zoom between the smallest and the largest level, times factor, around a window point (a smooth pinch);
    // Modern Render then uses the smooth tiles of the nearest level, scaled
    void ZoomBy(float factor, int sx, int sy);
    void PanBy(float dx, float dy);                         // the picture follows a finger by (dx, dy) window pixels
    // the phone layout: every zoom level times this many (window pixels are the phone's own small pixels there);
    // the shown picture keeps its level
    void SetZoomBase(float base);
    float ZoomBase() const { return _zoomBase; }
    void SetPhoneZoom(bool on) { _phoneZoom = on; } // the phone layout's pinch range (ZoomBy)
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
    SDL_Texture* BuildAtlas(SDL_Renderer* renderer, const TileSheet& sheet, const Palette& pal, bool seeThrough);
    SDL_Texture* BuildSmoothAtlas(SDL_Renderer* renderer, const TileSheet& sheet, const Palette& pal, bool seeThrough, int size);
    void ClampCamera();
    void DropSmooth(bool plainOnly);
    // movers: a cell's moving thing changed; finds where each one went and starts its glide
    void TrackMovers(double now);
    bool GlidePosition(int cell, double now, float& px, float& py) const; // world pixels; false when not gliding
    static int MoverKind(int overlay);                                      // -1 for things that do not move
    struct Glide;
    bool GlideAt(const Glide& g, int cell, double now, float& px, float& py) const;
    static double Now();

    // smooth atlases for one shown tile size (lazily built; plain and event tiles change with the palette and crops)
    struct Smooth
    {
        SDL_Texture* plain = nullptr;
        SDL_Texture* masked = nullptr;
        SDL_Texture* events = nullptr;
    };
    void UpdateSmooth(SDL_Texture* Smooth::*which, const TileSheet& oldSheet, const Palette& oldPal, const TileSheet& sheet,
                      const Palette& pal); // after a palette or tile change: the changed tiles redrawn in place
    struct Glide
    {
        float fromX = 0, fromY = 0; // world pixels at the start
        double start = 0, duration = 0;
    };
    struct Drift
    {
        float x = 0, y = 0, fromX = 0, fromY = 0;
        double start = 0, duration = 0, lastMove = -1;
        bool known = false;
    };
    SDL_Renderer* _renderer = nullptr;
    bool _modern = false;
    MapGraphics _src;                       // the sheets and palettes the atlases were last made from
    Palette _maskedPal{};
    Palette _plainPal{}, _eventsPal{}; // the palettes the plain and event atlases were made with
    std::map<int, Smooth> _smooth;          // by tile size in pixels
    std::vector<uint16_t> _prevMover;       // per cell: the moving thing's overlay tile at the last frame (0 none)
    std::vector<double> _arrived;           // per cell: when a glide into it started
    std::unordered_map<int, Glide> _glides; // by destination cell
    std::map<int, double> _pace;            // by mover kind: the usual time between two steps
    const World* _trackedWorld = nullptr;
    std::map<int, Drift> _drifts;           // SmoothCellRect's things

    const World* _world = nullptr;
    SDL_Texture* _plain = nullptr;
    SDL_Texture* _masked = nullptr;
    SDL_Texture* _events = nullptr;
    int _eventBank = 0;
    float _camX = 40 * 16, _camY = 30 * 16; // world pixel at the window's top-left
    float _zoom = 2.0f;
    float _zoomBase = 1.0f;
    bool _phoneZoom = false;
    int _viewW = 1280, _viewH = 720;
    bool _dragging = false;
    int _lastMouseX = 0, _lastMouseY = 0;
    int _left = 0, _top = 0;
    static constexpr int kAtlasColumns = 32;
};

} // namespace osf
