// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/MapView.h"
#include "ui/Gfx.h"
#include "ui/PixelArt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace osf {

SDL_Texture* MapView::BuildAtlas(SDL_Renderer* renderer, const TileSheet& sheet, const Palette& pal, bool seeThrough)
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
                rgba[static_cast<size_t>(oy + y) * w + ox + x] =
                    (seeThrough && c == kSeeThrough) ? 0 : (0xFFu << 24) | (pal[c & 15][0] << 16) | (pal[c & 15][1] << 8) | pal[c & 15][2];
            }
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex)
        return nullptr;
    SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(tex, seeThrough ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    return tex;
}

SDL_Texture* MapView::BuildSmoothAtlas(SDL_Renderer* renderer, const TileSheet& sheet, const Palette& pal, bool seeThrough, int size)
{
    if (!renderer || sheet.count <= 0)
        return nullptr;
    const int rows = (sheet.count + kAtlasColumns - 1) / kAtlasColumns;
    const int w = kAtlasColumns * size, h = rows * size;
    std::vector<uint32_t> argb(static_cast<size_t>(w) * h, 0);
    for (int t = 0; t < sheet.count; ++t)
    {
        const std::vector<uint32_t> px = SmoothTile(sheet.Tile(t), pal, seeThrough, size);
        const int ox = (t % kAtlasColumns) * size, oy = (t / kAtlasColumns) * size;
        for (int y = 0; y < size; ++y)
            std::copy_n(&px[static_cast<size_t>(y) * size], size, &argb[static_cast<size_t>(oy + y) * w + ox]);
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex)
        return nullptr;
    SDL_UpdateTexture(tex, nullptr, argb.data(), w * 4);
    SDL_SetTextureBlendMode(tex, seeThrough ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest); // drawn pixel for pixel
    return tex;
}

void MapView::DropSmooth(bool plainOnly)
{
    for (auto it = _smooth.begin(); it != _smooth.end();)
    {
        Smooth& s = it->second;
        if (s.plain)
            SDL_DestroyTexture(s.plain);
        if (s.events)
            SDL_DestroyTexture(s.events);
        s.plain = s.events = nullptr;
        if (!plainOnly)
        {
            if (s.masked)
                SDL_DestroyTexture(s.masked);
            it = _smooth.erase(it);
        }
        else
            ++it;
    }
}

void MapView::SetModern(bool on)
{
    _modern = on;
    _glides.clear();
    _drifts.clear();
    _prevMover.clear();
}

bool MapView::Init(SDL_Renderer* renderer, const MapGraphics& gfx)
{
    _renderer = renderer;
    _src = gfx;
    _maskedPal = gfx.palette;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest"); // keep the pixel art sharp at any zoom
    _plain = BuildAtlas(renderer, gfx.plain, gfx.palette, false);
    _masked = BuildAtlas(renderer, gfx.masked, gfx.palette, true);
    if (gfx.events.count >= 105)
        _events = BuildAtlas(renderer, gfx.events, gfx.palette, false);
    return _plain && _masked;
}

void MapView::SetPlainTiles(SDL_Renderer* renderer, const MapGraphics& gfx)
{
    _src.plain = gfx.plain;
    _src.palette = gfx.palette;
    DropSmooth(true);
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
    _src.events = gfx.events;
    _src.palette = gfx.palette;
    DropSmooth(true);
    if (SDL_Texture* t = BuildAtlas(renderer, gfx.events, gfx.palette, false))
    {
        if (_events)
            SDL_DestroyTexture(_events);
        _events = t;
    }
}

MapView::~MapView()
{
    DropSmooth(false);
    if (_plain)
        SDL_DestroyTexture(_plain);
    if (_masked)
        SDL_DestroyTexture(_masked);
    if (_events)
        SDL_DestroyTexture(_events);
}

void MapView::ClampCamera()
{
    const float worldPx = MapSize() * 16.0f;
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
    return cx >= 0 && cy >= 0 && cx < MapSize() && cy < MapSize();
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
    const int x1 = std::min(MapSize() - 1, static_cast<int>((_camX + width / _zoom) / 16));
    const int y1 = std::min(MapSize() - 1, static_cast<int>((_camY + height / _zoom) / 16));
    // Modern Render: the smooth atlases for this tile size, and the movers' glides
    const double now = Now();
    const int size = static_cast<int>(std::lround(16 * _zoom));
    const Smooth* sm = nullptr;
    if (_modern && size > 0)
    {
        auto it = _smooth.find(size);
        if (it == _smooth.end())
            it = _smooth.emplace(size, Smooth{}).first;
        Smooth& s = it->second;
        if (!s.plain)
            s.plain = BuildSmoothAtlas(renderer, _src.plain, _src.palette, false, size);
        if (!s.masked)
            s.masked = BuildSmoothAtlas(renderer, _src.masked, _maskedPal, true, size);
        if (!s.events && _src.events.count >= 105)
            s.events = BuildSmoothAtlas(renderer, _src.events, _src.palette, false, size);
        if (s.plain && s.masked)
            sm = &s;
        TrackMovers(now);
    }
    const int cellPx = sm ? size : 16;
    SDL_Texture* const plainTex = sm ? sm->plain : _plain;
    SDL_Texture* const maskedTex = sm ? sm->masked : _masked;
    SDL_Texture* const eventTex = sm ? sm->events : _events;
    auto srcRect = [&](int i) { return SDL_Rect{ (i % kAtlasColumns) * cellPx, (i / kAtlasColumns) * cellPx, cellPx, cellPx }; };
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
            if (eventTex && _eventBank > 0 && t >= 483 && t < 518)
            {
                const SDL_Rect src = srcRect(_eventBank * 35 + t - 483);
                SDL_RenderCopy(renderer, eventTex, &src, &dst);
            }
            else if (t < 848)
            {
                const SDL_Rect src = srcRect(t);
                SDL_RenderCopy(renderer, plainTex, &src, &dst);
            }
            if (c.HasObject())
            {
                const int o = c.OverlayTile();
                if (sm && _glides.count(x * MapSize() + y))
                    continue; // drawn on its way below
                if (o >= 0 && o < 200)
                {
                    const SDL_Rect src = srcRect(o);
                    SDL_RenderCopy(renderer, maskedTex, &src, &dst);
                }
            }
        }
    if (!sm)
        return;
    // the movers on their way, over the cells
    for (const auto& g : _glides)
    {
        const MapCell& c = _world->cells[static_cast<size_t>(g.first)];
        const int o = c.OverlayTile();
        float px, py;
        if (!c.HasObject() || o < 0 || o >= 200 || !GlidePosition(g.first, now, px, py))
            continue;
        const SDL_Rect dst{ static_cast<int>(std::lround(px * _zoom)) - offX, static_cast<int>(std::lround(py * _zoom)) - offY, size, size };
        const SDL_Rect src = srcRect(o);
        SDL_RenderCopy(renderer, maskedTex, &src, &dst);
    }

}

// ---------------------------------------------------------------- Modern Render: movers

double MapView::Now()
{
    return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
}

// the overlay tiles of things that move by themselves, grouped so a turn or a load does not break the match:
// 0..6 the machines (tractor, plow, planter, harvester, sprayer, trailer, truck; every heading and part), 10.. the
// cars by kind, 20..23 the animals by species (walking or kicking)
int MapView::MoverKind(int o)
{
    if (o >= 848 && o < 852) return 0;
    if (o >= 852 && o < 856) return 1;
    if (o >= 856 && o < 860) return 2;
    if (o >= 860 && o < 868) return 3;
    if (o >= 868 && o < 876) return 4;
    if (o >= 876 && o < 892) return 5;
    if (o >= 896 && o < 912) return 6;
    // cars: four kinds x four headings; 937-944 above them are the rodeo riders, which pop up
    // on the arena's cells as in the original and must not glide (user, 2026-11-06)
    if (o >= 916 && o < 932) return 10 + (o - 916) / 4;
    if (o >= 946 && o < 1010) return 20 + (o - 946) / 16;
    if (o >= 1038 && o < 1042) return 20 + (o - 1038);
    return -1;
}

bool MapView::GlidePosition(int cell, double now, float& px, float& py) const
{
    const auto it = _glides.find(cell);
    return it != _glides.end() && GlideAt(it->second, cell, now, px, py);
}

bool MapView::GlideAt(const Glide& g, int cell, double now, float& px, float& py) const
{
    const float toX = static_cast<float>(cell / MapSize()) * 16, toY = static_cast<float>(cell % MapSize()) * 16;
    const double f = g.duration > 0 ? (now - g.start) / g.duration : 1.0;
    if (f >= 1)
        return false;
    const float k = static_cast<float>(std::max(0.0, f));
    px = g.fromX + (toX - g.fromX) * k;
    py = g.fromY + (toY - g.fromY) * k;
    return true;
}

void MapView::TrackMovers(double now)
{
    if (!_world)
        return;
    const size_t n = static_cast<size_t>(MapSize()) * MapSize();
    std::vector<uint16_t> cur(n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        const MapCell& c = _world->cells[i];
        const int o = c.overlay & 0x7FF;
        if (c.HasObject() && MoverKind(o) >= 0)
            cur[i] = static_cast<uint16_t>(o);
    }
    if (_prevMover.size() != n || _trackedWorld != _world)
    {
        _prevMover.swap(cur);
        _arrived.assign(n, -1.0);
        _glides.clear();
        _trackedWorld = _world;
        return;
    }
    for (auto it = _glides.begin(); it != _glides.end();) // finished glides
        if (now >= it->second.start + it->second.duration)
            it = _glides.erase(it);
        else
            ++it;
    std::vector<int> gone, came;
    for (size_t i = 0; i < n; ++i)
        if (_prevMover[i] != cur[i])
        {
            if (_prevMover[i])
                gone.push_back(static_cast<int>(i));
            if (cur[i])
                came.push_back(static_cast<int>(i));
        }
    if (gone.empty() && came.empty())
        return;
    if (gone.size() > 300 || came.size() > 300)
    {
        // a new map or a loaded game, not a step
        _glides.clear();
        _prevMover.swap(cur);
        return;
    }
    // pair every new place with the place the same kind of thing left (at most two cells away: a step, or two
    // steps between two frames at Ultra): the same picture first, nearest first, so a two-cell machine's parts
    // keep their order; then a changed picture (a turn, a kick, a load picked up)
    struct Pair
    {
        float score;
        int came, gone;
    };
    std::vector<Pair> pairs;
    for (size_t a = 0; a < came.size(); ++a)
        for (size_t r = 0; r < gone.size(); ++r)
        {
            const int ca = came[a], cg = gone[r];
            if (MoverKind(cur[static_cast<size_t>(ca)]) != MoverKind(_prevMover[static_cast<size_t>(cg)]))
                continue;
            const int d = std::max(std::abs(ca / MapSize() - cg / MapSize()), std::abs(ca % MapSize() - cg % MapSize()));
            if (d > 2)
                continue;
            // an animal steps the way its new picture faces (heading 0 = up, clockwise; Animals::TileFor), so
            // a place a neighbour left in another direction is not its own: pairing by picture and distance
            // alone mixed up the animals of a herd, and they glided sideways (user, 2026-11-06). At Ultra two
            // steps can fall between two frames: then only the last one has to follow the picture
            const int pic = cur[static_cast<size_t>(ca)];
            if (pic >= 946 && pic < 1010)
            {
                static constexpr int kStepX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, kStepY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
                const int h = ((pic - 946) & 15) >> 1;
                const int rx = ca / MapSize() - cg / MapSize() - kStepX[h], ry = ca % MapSize() - cg % MapSize() - kStepY[h];
                const int rest = std::max(std::abs(rx), std::abs(ry));
                if (d == 1 ? rest != 0 : rest != 1)
                    continue;
            }
            const bool same = pic == _prevMover[static_cast<size_t>(cg)];
            pairs.push_back({ static_cast<float>(d) + (same ? 0.0f : 3.0f), static_cast<int>(a), static_cast<int>(r) });
        }
    std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& p, const Pair& q) { return p.score < q.score; });
    std::vector<char> cameUsed(came.size(), 0), goneUsed(gone.size(), 0);
    const std::unordered_map<int, Glide> before = _glides;
    std::vector<std::pair<int, Glide>> started;
    for (const Pair& p : pairs)
    {
        if (cameUsed[static_cast<size_t>(p.came)] || goneUsed[static_cast<size_t>(p.gone)])
            continue;
        cameUsed[static_cast<size_t>(p.came)] = goneUsed[static_cast<size_t>(p.gone)] = 1;
        const int to = came[static_cast<size_t>(p.came)], from = gone[static_cast<size_t>(p.gone)];
        if (to == from)
            continue; // a turn or a kick on the spot: any glide into the cell goes on
        // it starts where it is drawn now (still on its way from the last step, or at its cell)
        Glide g;
        g.fromX = static_cast<float>(from / MapSize()) * 16;
        g.fromY = static_cast<float>(from % MapSize()) * 16;
        float px, py;
        const auto old = before.find(from);
        if (old != before.end() && GlideAt(old->second, from, now, px, py))
        {
            g.fromX = px;
            g.fromY = py;
        }
        // as long as the time between its last two steps, so it arrives as the next step is taken
        double& pace = _pace.try_emplace(MoverKind(cur[static_cast<size_t>(to)]), 0.3).first->second;
        const double since = _arrived[static_cast<size_t>(from)] >= 0 ? now - _arrived[static_cast<size_t>(from)] : -1;
        if (since > 0.005 && since < 1.0)
            pace = since;
        g.start = now;
        g.duration = std::clamp(pace, 0.02, 0.8);
        started.emplace_back(to, g);
    }
    // a place left without a match (into a building, sold, moved far by the Move tool): its glide ends
    for (size_t r = 0; r < gone.size(); ++r)
        _glides.erase(gone[r]);
    for (const auto& s : started)
    {
        _glides[s.first] = s.second;
        _arrived[static_cast<size_t>(s.first)] = now;
    }
    _prevMover.swap(cur);
}

SDL_Rect MapView::SmoothCellRectAt(int id, int cx, int cy, int w, int h, double now)
{
    if (!_modern)
    {
        _drifts.erase(id);
        return CellRect(cx, cy, w, h);
    }
    Drift& d = _drifts[id];
    const float tx = static_cast<float>(cx), ty = static_cast<float>(cy);
    auto at = [&](float& x, float& y) {
        const double f = d.duration > 0 ? std::clamp((now - d.start) / d.duration, 0.0, 1.0) : 1.0;
        x = d.fromX + (d.x - d.fromX) * static_cast<float>(f);
        y = d.fromY + (d.y - d.fromY) * static_cast<float>(f);
    };
    if (!d.known)
    {
        d = Drift{};
        d.x = d.fromX = tx;
        d.y = d.fromY = ty;
        d.known = true;
        d.lastMove = now;
    }
    else if (tx != d.x || ty != d.y)
    {
        float x, y;
        at(x, y);
        const double since = now - d.lastMove;
        if (std::fabs(tx - x) > 4 || std::fabs(ty - y) > 4)
        {
            d.fromX = tx; // far away: it starts there
            d.fromY = ty;
            d.duration = 0;
        }
        else
        {
            d.fromX = x;
            d.fromY = y;
            d.duration = std::clamp(since > 0.005 && since < 1.0 ? since : (d.duration > 0 ? d.duration : 0.2), 0.02, 0.8);
        }
        d.x = tx;
        d.y = ty;
        d.start = d.lastMove = now;
    }
    float x, y;
    at(x, y);
    const long ox = std::lround(_camX * _zoom), oy = std::lround(_camY * _zoom); // as Draw
    const int x0 = static_cast<int>(std::lround(x * 16 * _zoom) - ox) + _left, y0 = static_cast<int>(std::lround(y * 16 * _zoom) - oy) + _top;
    return SDL_Rect{ x0, y0, static_cast<int>(std::lround(w * 16 * _zoom)), static_cast<int>(std::lround(h * 16 * _zoom)) };
}

// ---------------------------------------------------------------- --graphicstest

int MapView::SelfTest(const MapGraphics& gfx)
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            ++fails;
    };
    // the doubling rule: a straight edge stays straight, a flat block stays flat, a diagonal step gets rounded
    {
        std::vector<uint8_t> half(256), flat(256, 5), diag(256), dither(256);
        for (int i = 0; i < 256; ++i)
        {
            half[static_cast<size_t>(i)] = i < 128 ? 1 : 2;
            diag[static_cast<size_t>(i)] = (i % 16) > (i / 16) ? 1 : 2;
            dither[static_cast<size_t>(i)] = ((i % 16) + (i / 16)) % 2 ? 3 : 4;
        }
        const std::vector<uint8_t> h2 = Double(half, 16), f2 = Double(flat, 16), d2 = Double(diag, 16), c2 = Double(dither, 16);
        bool straight = true, same = true, kept = true;
        for (int i = 0; i < 1024; ++i)
        {
            straight = straight && h2[static_cast<size_t>(i)] == (i < 512 ? 1 : 2);
            same = same && f2[static_cast<size_t>(i)] == 5;
            kept = kept && c2[static_cast<size_t>(i)] == dither[static_cast<size_t>((i / 32 / 2) * 16 + (i % 32) / 2)];
        }
        check(kept, "doubling leaves a dither as it is");
        int rounded = 0; // quarters taking the neighbour's colour along the diagonal
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                for (int q = 0; q < 4; ++q)
                    rounded += d2[static_cast<size_t>((2 * y + q / 2) * 32 + 2 * x + q % 2)] != diag[static_cast<size_t>(y * 16 + x)];
        check(straight, "doubling keeps a straight edge straight");
        check(same, "doubling keeps a flat block flat");
        check(rounded >= 14, "doubling rounds a diagonal");
    }
    // the player's tiles: colours kept on the whole, edges softened, see-through parts kept
    if (gfx.plain.count > 0 && gfx.masked.count > 0)
    {
        double drift = 0, maxDrift = 0;
        long softened = 0, flatTiles = 0, flatOk = 0;
        for (int t = 0; t < gfx.plain.count; ++t)
        {
            const std::vector<uint32_t> s = SmoothTile(gfx.plain.Tile(t), gfx.palette, false, 32), p = PlainTile(gfx.plain.Tile(t), gfx.palette, false, 32);
            double sum[2][3] = {};
            for (size_t i = 0; i < s.size(); ++i)
                for (int k = 0; k < 3; ++k)
                {
                    sum[0][k] += (s[i] >> (16 - 8 * k)) & 0xFF;
                    sum[1][k] += (p[i] >> (16 - 8 * k)) & 0xFF;
                }
            for (int k = 0; k < 3; ++k)
            {
                const double d = std::fabs(sum[0][k] - sum[1][k]) / static_cast<double>(s.size());
                drift += d / 3;
                maxDrift = std::max(maxDrift, d);
            }
            bool flat = true;
            for (int i = 1; i < 256; ++i)
                flat = flat && gfx.plain.Tile(t)[i] == gfx.plain.Tile(t)[0];
            if (flat)
            {
                ++flatTiles;
                flatOk += s == p;
            }
            for (size_t i = 0; i < s.size(); ++i)
            {
                bool inPalette = false;
                for (const auto& c : gfx.palette)
                    inPalette = inPalette || (s[i] & 0xFFFFFF) == ((static_cast<uint32_t>(c[0]) << 16) | (c[1] << 8) | c[2]);
                softened += !inPalette;
            }
        }
        std::printf("     plain tiles: %d, mean colour change %.2f / 255 (largest %.1f), %ld smoothed edge pixels, %ld of %ld flat tiles unchanged\n",
                    gfx.plain.count, drift / gfx.plain.count, maxDrift, softened, flatOk, flatTiles);
        check(drift / gfx.plain.count < 3.0, "smoothing keeps the tiles' colours");
        check(softened > 1000, "smoothing softens edges");
        check(flatOk == flatTiles, "smoothing leaves flat tiles exactly as they are");
        long groundSame = 0, groundAll = 0; // the plain ground (tiles 15-30) is a texture: kept as it is
        for (int t = 15; t <= 30 && t < gfx.plain.count; ++t)
        {
            const std::vector<uint32_t> s = SmoothTile(gfx.plain.Tile(t), gfx.palette, false, 32), p = PlainTile(gfx.plain.Tile(t), gfx.palette, false, 32);
            for (size_t i = 0; i < s.size(); ++i)
            {
                groundSame += s[i] == p[i];
                ++groundAll;
            }
        }
        std::printf("     ground tiles: %.1f %% of the pixels unchanged\n", 100.0 * groundSame / std::max(1L, groundAll));
        // (the Windows edition's ground is dithered in larger patches, a little more of it is taken for shapes)
        check(groundSame * 100 >= groundAll * (gfx.windowsEdition ? 95 : 97), "the ground's texture stays as it is");
        long seeS = 0, seeP = 0, bleed = 0;
        // the see-through pixels are no colour of the palette: changing every colour the tile does not use must not
        // change the smoothed tile (no fringe from a see-through colour)
        for (int t = 0; t < gfx.masked.count; ++t)
        {
            const std::vector<uint32_t> s = SmoothTile(gfx.masked.Tile(t), gfx.palette, true, 32), p = PlainTile(gfx.masked.Tile(t), gfx.palette, true, 32);
            Palette marked = gfx.palette;
            for (int c = 0; c < 16; ++c)
                if (std::find(gfx.masked.Tile(t), gfx.masked.Tile(t) + 256, c) == gfx.masked.Tile(t) + 256)
                    marked[static_cast<size_t>(c)] = { 255, 0, 255 };
            const std::vector<uint32_t> m = SmoothTile(gfx.masked.Tile(t), marked, true, 32);
            for (size_t i = 0; i < s.size(); ++i)
            {
                seeS += (s[i] >> 24) < 128;
                seeP += (p[i] >> 24) == 0;
                bleed += s[i] != m[i];
            }
        }
        std::printf("     masked tiles: see-through pixels %ld smoothed / %ld plain, %ld pixels tinted by the see-through colour\n", seeS, seeP, bleed);
        check(std::labs(seeS - seeP) * 20 < seeP, "smoothing keeps the see-through parts");
        check(bleed == 0, "no fringe from the see-through colour");
    }
    else
        check(false, "the player's tiles could be read");
    // the glide tracking on a test map
    {
        World w;
        MapView v;
        v.SetWorld(&w);
        v.SetModern(true);
        auto put = [&](int x, int y, int tile) {
            w.At(x, y).base = static_cast<uint16_t>(w.At(x, y).base | 0x800);
            w.At(x, y).overlay = static_cast<uint16_t>(tile);
        };
        auto lift = [&](int x, int y) {
            w.At(x, y).base = static_cast<uint16_t>(w.At(x, y).base & ~0x800);
            w.At(x, y).overlay = 0;
        };
        auto pos = [&](int x, int y, double now, float& px, float& py) { return v.GlidePosition(x * v.MapSize() + y, now, px, py); };
        float px = 0, py = 0;
        put(10, 10, 946 + 16 + 2);    // a cow facing east
        put(20, 20, 860);             // a harvester across (20, 20) and (21, 20)
        put(21, 20, 861);
        put(40, 40, 916);             // a car
        v.TrackMovers(0.0);
        lift(10, 10);
        put(11, 10, 946 + 16 + 4);    // the cow steps east, its picture turned
        v.TrackMovers(0.3);
        check(pos(11, 10, 0.45, px, py) && std::fabs(px - 168) < 0.01f && std::fabs(py - 160) < 0.01f, "a step east glides from the old cell");
        lift(11, 10);
        put(12, 10, 946 + 16 + 4);
        v.TrackMovers(0.6);           // the next step 0.3 s later: the same pace, from where it arrived
        check(pos(12, 10, 0.75, px, py) && std::fabs(px - 184) < 0.01f, "the next step glides on at the same pace");
        check(!pos(12, 10, 0.95, px, py), "a glide ends at its cell");
        lift(12, 10);
        put(30, 10, 946 + 16 + 4);    // carried far away (the Move tool)
        v.TrackMovers(0.7);
        check(!pos(30, 10, 0.71, px, py), "a jump far away does not glide");
        put(30, 10, 0x40E + 1);       // a kick on the spot
        v.TrackMovers(0.8);
        check(!pos(30, 10, 0.81, px, py), "a kick on the spot does not glide");
        lift(20, 20);                 // the harvester moves east by a cell
        put(21, 20, 860);
        put(22, 20, 861);
        v.TrackMovers(1.0);
        float qx = 0, qy = 0;
        check(pos(21, 20, 1.15, px, py) && pos(22, 20, 1.15, qx, qy) && std::fabs(px - 328) < 0.01f && std::fabs(qx - 344) < 0.01f,
              "a two-cell machine glides as one piece");
        // two cows side by side: one turns east and steps, the other (facing east) steps north. By picture and
        // distance alone the first cow's new cell was paired with the second cow's old one (a sideways glide)
        put(60, 60, 946 + 16 + 8);    // facing south
        put(62, 61, 946 + 16 + 4);    // facing east
        v.TrackMovers(2.0);
        lift(60, 60);
        put(61, 60, 946 + 16 + 4);    // turned east, one cell east
        lift(62, 61);
        put(62, 60, 946 + 16 + 0);    // turned north, one cell north
        v.TrackMovers(2.3);
        check(pos(61, 60, 2.45, px, py) && std::fabs(px - 61 * 16 + 8) < 0.01f && std::fabs(py - 60 * 16) < 0.01f &&
                  pos(62, 60, 2.45, qx, qy) && std::fabs(qx - 62 * 16) < 0.01f && std::fabs(qy - 60 * 16 - 8) < 0.01f,
              "animals in a herd glide the way they face");
        put(70, 70, 937);             // a rodeo rider on one arena cell, then on the next one
        v.TrackMovers(2.5);
        lift(70, 70);
        put(71, 70, 940);
        v.TrackMovers(2.6);
        check(!pos(71, 70, 2.65, px, py), "a rodeo rider does not glide");
        lift(40, 40);                 // the car drives north
        put(40, 39, 916 + 1);
        v.TrackMovers(1.1);
        check(pos(40, 39, 1.2, px, py) && std::fabs(py - (39 * 16 + 16 * (1 - 0.1f / 0.3f))) < 0.05f, "a car glides");
        // many changes at once (a loaded game) do not glide
        for (int i = 0; i < 400; ++i)
            put(i % 96, 60 + i / 96, 946);
        v.TrackMovers(1.2);
        check(!pos(0, 60, 1.21, px, py), "a new map does not glide");
        // the tornado: drawn over the map at its cells (zoom 2: 32 window pixels a cell, the view at the map's corner)
        v._camX = v._camY = 0;
        v._zoom = 2.0f;
        SDL_Rect a = v.SmoothCellRectAt(0, 10, 10, 3, 3, 5.0);
        v.SmoothCellRectAt(0, 11, 10, 3, 3, 5.4); // a step east 0.4 s later
        SDL_Rect b = v.SmoothCellRectAt(0, 11, 10, 3, 3, 5.6);
        SDL_Rect c = v.SmoothCellRectAt(0, 11, 10, 3, 3, 5.9);
        check(a.x == 320 && b.x == 336 && c.x == 352 && b.w == 96, "the tornado glides to its next cell");
        v.SmoothCellRectAt(0, 50, 50, 3, 3, 6.0); // a new tornado far away
        b = v.SmoothCellRectAt(0, 50, 50, 3, 3, 6.05);
        check(b.x == 1600 && b.y == 1600, "a tornado far away starts there");
        v.SetModern(false);
        b = v.SmoothCellRectAt(0, 12, 10, 3, 3, 6.1);
        check(b.x == 384, "without Modern Render it is drawn at its cell");
    }
    std::printf("graphicstest %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}

} // namespace osf
