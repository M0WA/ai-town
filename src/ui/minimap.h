#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend, Rect

struct InputEvent;

// Minimap — 200x200 px at bottom-right (x:1720-1920, y:880-1080) in virtual space.
// Top-down zone color coding (R=green, C=blue, I=orange), road network.
// Camera viewport rectangle (white outline, clamped 8x8 to 190x190).
// Click-to-pan, service coverage overlay toggle.
class Minimap {
public:
    explicit Minimap(IUIBackend* backend);

    void show();
    void hide();
    void draw();
    bool onEvent(const InputEvent& event);
    Rect getBounds() const;

    bool isOverlayActive() const { return m_overlayActive; }
    void toggleOverlay();

private:
    IUIBackend* m_backend{nullptr};
    bool m_visible{false};
    bool m_overlayActive{false};

    // Minimap render area: x:1720-1920, y:880-1080 (200x200 px)
    static constexpr int kMapX = 1720;
    static constexpr int kMapY = 880;
    static constexpr int kMapW = 200;
    static constexpr int kMapH = 200;

    UIElementHandle m_mapBg{kInvalidUIElement};
    UIElementHandle m_viewportRect{kInvalidUIElement};

    // Toggle button (32x32 above minimap)
    UIElementHandle m_toggleBtn{kInvalidUIElement};

    // Legend panel (shown when overlay active, 80x100 above toggle)
    UIElementHandle m_legendPanel{kInvalidUIElement};
    UIElementHandle m_legendLabel{kInvalidUIElement};
};
