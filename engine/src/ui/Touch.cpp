// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/Touch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace osf {

TouchGestures::Finger* TouchGestures::Find(int64_t id)
{
    for (auto& f : _fingers)
        if (f.id == id)
            return &f;
    return nullptr;
}

void TouchGestures::Mid(float& x, float& y, float& d) const
{
    const Finger& a = _fingers[0];
    const Finger& b = _fingers[1];
    x = (a.x + b.x) / 2;
    y = (a.y + b.y) / 2;
    d = std::max(1.0f, std::hypot(a.x - b.x, a.y - b.y));
}

void TouchGestures::Down(int64_t id, float x, float y, uint32_t t)
{
    if (Find(id))
        return;
    _fingers.push_back({ id, x, y });
    if (_fingers.size() == 1)
    {
        _state = State::Pending;
        _x0 = x;
        _y0 = y;
        _t0 = t;
    }
    else if (_fingers.size() == 2 && _state != State::Done && _state != State::ToolDrag)
    {
        // a second finger: no tap or long press any more; pan and pinch from here
        _state = State::Pinching;
        Mid(_midX, _midY, _dist);
    }
}

void TouchGestures::Move(int64_t id, float x, float y, uint32_t)
{
    Finger* f = Find(id);
    if (!f)
        return;
    const float px = f->x, py = f->y;
    f->x = x;
    f->y = y;
    if (_state == State::ToolDrag && id != _fingers[0].id)
        return; // only the first finger moves the tool
    switch (_state)
    {
    case State::Pending:
        if (std::hypot(x - _x0, y - _y0) < slop)
            return;
        _state = State::Panning;
        _out.push_back({ TouchAction::Pan, 0, 0, x - _x0, y - _y0 }); // the whole way from the start: no jump lost
        return;
    case State::ToolDrag:
        if (x != px || y != py)
            _out.push_back({ TouchAction::DragMove, x, y });
        return;
    case State::Panning:
        if (x != px || y != py)
            _out.push_back({ TouchAction::Pan, 0, 0, x - px, y - py });
        return;
    case State::Pinching:
    {
        if (_fingers.size() < 2)
            return;
        float mx, my, d;
        Mid(mx, my, d);
        if (mx != _midX || my != _midY)
            _out.push_back({ TouchAction::Pan, 0, 0, mx - _midX, my - _midY });
        _midX = mx;
        _midY = my;
        if (smooth)
        {
            // the picture grows or shrinks with the distance between the fingers, around the point between them
            if (d != _dist)
            {
                TouchAction a{ TouchAction::Zoom, mx, my, 0, 0, d > _dist ? +1 : -1 };
                a.factor = d / _dist;
                _out.push_back(a);
                _dist = d;
            }
            return;
        }
        // one zoom level per pinchStep of finger distance, around the point between the fingers
        while (d >= _dist * pinchStep)
        {
            _out.push_back({ TouchAction::Zoom, mx, my, 0, 0, +1 });
            _dist *= pinchStep;
        }
        while (d <= _dist / pinchStep)
        {
            _out.push_back({ TouchAction::Zoom, mx, my, 0, 0, -1 });
            _dist /= pinchStep;
        }
        return;
    }
    default:
        return;
    }
}

void TouchGestures::Up(int64_t id, float x, float y, uint32_t t)
{
    Finger* f = Find(id);
    if (!f)
        return;
    Move(id, x, y, t);
    if (_state == State::Pending && _fingers.size() == 1)
        _out.push_back({ TouchAction::Tap, _x0, _y0 });
    if (_state == State::ToolDrag && id == _fingers[0].id)
    {
        _out.push_back({ TouchAction::DragEnd, x, y });
        _state = State::Done;
    }
    _fingers.erase(std::remove_if(_fingers.begin(), _fingers.end(), [id](const Finger& g) { return g.id == id; }), _fingers.end());
    if (_fingers.empty())
        _state = State::Idle;
    else if (_state == State::Pinching)
        _state = State::Done; // one finger of a pinch lifted: the other one does nothing until it lifts too
}

void TouchGestures::Tick(uint32_t t)
{
    if (_state == State::Pending && holdDragMs && t - _t0 >= holdDragMs)
    {
        _out.push_back({ TouchAction::DragStart, _x0, _y0 });
        _state = State::ToolDrag;
        return;
    }
    if (_state == State::Pending && t - _t0 >= longPressMs)
    {
        _out.push_back({ TouchAction::LongPress, _x0, _y0 });
        _state = State::Done;
    }
}

void TouchGestures::Cancel()
{
    _fingers.clear();
    _state = State::Idle;
}

std::vector<TouchAction> TouchGestures::Take()
{
    std::vector<TouchAction> r;
    r.swap(_out);
    return r;
}

int TouchSelfTest()
{
    int failures = 0;
    auto expect = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        failures += !ok;
    };
    auto count = [](const std::vector<TouchAction>& v, TouchAction::Kind k) {
        int n = 0;
        for (const auto& a : v)
            n += a.kind == k;
        return n;
    };
    {
        TouchGestures g;
        g.Down(1, 100, 100, 0);
        g.Move(1, 104, 103, 50);
        g.Up(1, 104, 103, 120);
        const auto a = g.Take();
        expect(a.size() == 1 && a[0].kind == TouchAction::Tap && a[0].x == 100 && a[0].y == 100 && !g.Active(),
               "a short touch that hardly moves is a tap where the finger went down");
    }
    {
        TouchGestures g;
        g.Down(1, 100, 100, 0);
        g.Move(1, 130, 90, 30);
        g.Move(1, 160, 80, 60);
        g.Up(1, 160, 80, 90);
        const auto a = g.Take();
        float dx = 0, dy = 0;
        for (const auto& x : a)
            if (x.kind == TouchAction::Pan)
                dx += x.dx, dy += x.dy;
        expect(count(a, TouchAction::Tap) == 0 && dx == 60 && dy == -20,
               "one finger dragged pans by the whole way it moved (" + std::to_string(dx) + ", " + std::to_string(dy) + "), no tap");
    }
    {
        TouchGestures g;
        g.Down(1, 50, 60, 1000);
        g.Tick(1300);
        const bool early = g.Take().empty();
        g.Tick(1600);
        const auto a = g.Take();
        g.Up(1, 50, 60, 1700);
        expect(early && a.size() == 1 && a[0].kind == TouchAction::LongPress && a[0].x == 50 && g.Take().empty(),
               "a press held still for 0.55 s is a long press, and its release adds no tap");
    }
    {
        TouchGestures g;
        g.Down(1, 100, 100, 0);
        g.Move(1, 140, 100, 100);
        g.Tick(2000);
        g.Up(1, 140, 100, 2100);
        expect(count(g.Take(), TouchAction::LongPress) == 0, "a pan held long is no long press");
    }
    {
        TouchGestures g;
        g.Down(1, 100, 100, 0);
        g.Down(2, 200, 100, 10);
        g.Move(2, 300, 100, 50);   // distance 100 -> 200: 1.3 and 1.69 below 2
        g.Move(1, 50, 100, 80);    // 250: up to 2.197
        auto a = g.Take();
        const int in = count(a, TouchAction::Zoom);
        bool allIn = true;
        for (const auto& x : a)
            if (x.kind == TouchAction::Zoom)
                allIn = allIn && x.dir == 1;
        g.Move(2, 60, 100, 120);   // 10: far out
        a = g.Take();
        int out = 0;
        for (const auto& x : a)
            out += x.kind == TouchAction::Zoom && x.dir == -1;
        g.Up(1, 50, 100, 200);
        g.Move(2, 400, 400, 210);  // the other finger after the pinch: nothing
        g.Up(2, 400, 400, 220);
        a = g.Take();
        expect(in == 3 && allIn && out >= 4 && a.empty() && !g.Active(),
               "two fingers pinch: zoom in " + std::to_string(in) + " levels, out " + std::to_string(out) +
                   ", no tap or pan from the finger left after it");
    }
    {
        TouchGestures g;
        g.Down(1, 100, 100, 0);
        g.Down(2, 200, 100, 0);
        g.Move(1, 120, 130, 20);
        g.Move(2, 220, 130, 20);
        float dx = 0, dy = 0;
        int zooms = 0;
        for (const auto& x : g.Take())
        {
            if (x.kind == TouchAction::Pan)
                dx += x.dx, dy += x.dy;
            zooms += x.kind == TouchAction::Zoom;
        }
        expect(dx == 20 && dy == 30 && zooms == 0, "two fingers moved together pan by their midpoint, no zoom");
    }
    {
        // a press held still with a tool: the tool goes with the finger, no pan and no long press
        TouchGestures g;
        g.holdDragMs = 250;
        g.Down(1, 100, 100, 0);
        g.Tick(100);
        const bool early = g.Take().empty();
        g.Tick(260);
        g.Move(1, 160, 100, 300);
        g.Tick(900);
        g.Up(1, 200, 100, 1000);
        const auto a = g.Take();
        const bool ok = a.size() == 4 && a[0].kind == TouchAction::DragStart && a[0].x == 100 && a[1].kind == TouchAction::DragMove &&
                        a[1].x == 160 && a[2].kind == TouchAction::DragMove && a[3].kind == TouchAction::DragEnd && a[3].x == 200;
        g.holdDragMs = 250;
        g.Down(1, 100, 100, 2000);
        g.Move(1, 160, 100, 2100); // moved before the hold: a pan as always
        g.Up(1, 160, 100, 2200);
        expect(early && ok && count(g.Take(), TouchAction::Pan) > 0, "a held press drags the tool; a quick drag still pans");
    }
    {
        // the smooth pinch: the zoom factors multiply up to the change of the finger distance
        TouchGestures g;
        g.smooth = true;
        g.Down(1, 100, 100, 0);
        g.Down(2, 200, 100, 10);
        g.Move(2, 250, 100, 50);   // 100 -> 150
        g.Move(1, 50, 100, 80);    // 200
        g.Move(2, 150, 100, 120);  // 100
        float in = 1, total = 1;
        int zooms = 0;
        for (const auto& x : g.Take())
            if (x.kind == TouchAction::Zoom)
            {
                total *= x.factor;
                if (zooms++ < 2)
                    in *= x.factor;
            }
        expect(zooms == 3 && std::fabs(in - 2.0f) < 0.001f && std::fabs(total - 1.0f) < 0.001f,
               "a smooth pinch zooms with the fingers: x" + std::to_string(in) + " then back to x" + std::to_string(total));
    }
    {
        TouchGestures g;
        g.Down(1, 10, 10, 0);
        g.Cancel();
        g.Up(1, 10, 10, 50);
        expect(g.Take().empty() && !g.Active(), "a cancelled touch does nothing");
    }
    return failures;
}

} // namespace osf
