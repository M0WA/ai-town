#pragma once
// tests/ui/input_event_helpers.h
//
// Free-function helpers for constructing InputEvent values in UI tests.
// All coordinates are in virtual 1920x1080 space (UIScaler output) unless
// noted. For physical-pixel drag events, set physX/physY manually after
// calling the helper.
//
// Usage:
//   InputEvent ev = makeKeyDown(27);          // Escape
//   InputEvent ev = makeClick(100, 200);      // LMB click at virtual (100, 200)
//   InputEvent ev = makeMouseButtonDown(50, 75); // same as makeClick but explicit name

#include "src/platform/input_event.h"

// makeKeyDown — returns a KeyDown event for the given keyCode.
// keyCode follows SDL2/Irrlicht conventions (e.g. 27 = Escape, 9 = Tab).
inline InputEvent makeKeyDown(int keyCode) {
    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = keyCode;
    return ev;
}

// makeClick — returns a MouseButtonDown event (left button) at virtual (x, y).
// Sets both the virtual (x, y) and physical (physX, physY) coordinates to the
// same values; tests that care about sub-pixel physical coords can adjust
// physX/physY after calling this helper.
inline InputEvent makeClick(int x, int y) {
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;  // 0 = left mouse button
    ev.x      = x;
    ev.y      = y;
    ev.physX  = x;
    ev.physY  = y;
    return ev;
}

// makeMouseButtonDown — alias for makeClick; use when the test description
// benefits from the more explicit name.
inline InputEvent makeMouseButtonDown(int x, int y) {
    return makeClick(x, y);
}
