#pragma once
#include "src/ui/IUIBackend.h"
// UIScaler.h must #include "src/ui/IUIBackend.h" (project-root-relative form)
// so that methods returning Rect compile correctly.
// Defining Rect only in IUIBackend.h and using it in UIScaler.h without the include
// causes an ODR violation if a translation unit includes UIScaler.h without first
// including IUIBackend.h.

// UIScaler — translates between virtual 1920x1080 coordinate space and physical
// viewport coordinates (with letterbox/pillarbox offset support).
// Constructor signature is locked at Phase 0 to prevent Phase 1 parallel teams
// (UI + Camera) from making incompatible assumptions about IVideoDriver* vs. 6-param construction.
// Tests construct UIScaler(1920, 1080, 1280, 720, 0, 90) directly to validate coordinate
// projection and letterbox offset math without a display.
// Full implementation in Phase 1.
class UIScaler {
public:
    // VirtualPoint must be a nested type inside UIScaler to avoid ODR violations.
    // Callers use UIScaler::VirtualPoint.
    struct VirtualPoint { int x; int y; };

    // Locked constructor signature: virtualW, virtualH, viewportW, viewportH, offsetX, offsetY
    UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY);

    // Returns the active viewport x, y, w, h in physical pixels.
    // Used by the platform event receiver before input un-projection.
    Rect getViewportRect() const;

    // Unproject physical coordinates to virtual 1920x1080 space.
    VirtualPoint unproject(int physicalX, int physicalY) const;

private:
    int m_virtualW;
    int m_virtualH;
    int m_viewportW;
    int m_viewportH;
    int m_offsetX;
    int m_offsetY;
};
