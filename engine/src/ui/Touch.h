// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once

#include <cstdint>
#include <vector>

namespace osf {

// What a touch on the farm map means: one finger dragged pans the map, a short tap is a click, a press held still
// is a right click (valves, gates, a building's inside, or letting go of the tool), two fingers pan and pinch-zoom.
struct TouchAction
{
    enum Kind { Tap, LongPress, Pan, Zoom, DragStart, DragMove, DragEnd }; // Drag*: a held press that moves the tool
    Kind kind;
    float x = 0, y = 0;   // Tap / LongPress: where the finger went down; Zoom: between the two fingers
    float dx = 0, dy = 0; // Pan: how far the content moved, in the same pixels as the fingers
    int dir = 0;          // Zoom: +1 in, -1 out
    float factor = 1.0f;  // Zoom, smooth pinch: how much the picture grows (the finger distance's change)
};

// The gesture recognizer, free of SDL so it can be tested by itself. Positions are in window pixels, times in ms.
class TouchGestures
{
public:
    float slop = 12.0f;        // a finger that moved less than this is still a tap
    uint32_t longPressMs = 550;
    float pinchStep = 1.3f;    // the distance between the fingers changes by this factor per zoom level
    bool smooth = false;       // a pinch zooms with the fingers (factor) instead of in whole levels (dir)
    // a press held still this long becomes a drag of the chosen tool (DragStart at once, then DragMove as the finger
    // goes, DragEnd when it lifts) instead of a long press; 0: off. Set before the first finger goes down
    uint32_t holdDragMs = 0;

    void Down(int64_t id, float x, float y, uint32_t t);
    void Move(int64_t id, float x, float y, uint32_t t);
    void Up(int64_t id, float x, float y, uint32_t t);
    void Tick(uint32_t t);     // the long press, called every frame
    void Cancel();             // forget the touch (the game took it elsewhere)
    bool Active() const { return !_fingers.empty(); }
    std::vector<TouchAction> Take();

private:
    struct Finger { int64_t id; float x, y; };
    enum class State { Idle, Pending, Panning, Pinching, ToolDrag, Done };
    Finger* Find(int64_t id);
    void Mid(float& x, float& y, float& d) const;

    std::vector<Finger> _fingers;
    std::vector<TouchAction> _out;
    State _state = State::Idle;
    float _x0 = 0, _y0 = 0;    // where the first finger went down
    uint32_t _t0 = 0;
    float _midX = 0, _midY = 0, _dist = 0; // the pinch's last midpoint and its distance at the last zoom step
};

// --touchtest: the gestures on scripted fingers; returns the number of failed checks
int TouchSelfTest();

} // namespace osf
