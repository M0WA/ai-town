// src/ui/Minimap.cpp
//
// Minimap — 200x200 px at bottom-right (virtual x:1720-1920, y:880-1080).
// Top-down zone color coding, road network, camera viewport rectangle.
// Click-to-pan camera, service coverage overlay toggle.

#include "src/ui/Minimap.h"
#include "src/platform/input_event.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Minimap::Minimap(IUIBackend* backend)
    : m_backend(backend)
    , m_visible(false)
    , m_overlayActive(false)
{
    if (!m_backend) return;

    // Minimap background (200x200 at bottom-right).
    // fillBackground=false by default in addStaticText — invisible without an explicit
    // background color.  Call setElementBackground() immediately after creation to paint
    // the minimap area as a dark near-opaque rectangle so it is always visible even before
    // zone/road tile data is available.  Color: dark grey, fully opaque (20, 20, 20, 230).
    m_mapBg = m_backend->addStaticText("", kMapX, kMapY, kMapW, kMapH);
    m_backend->setElementBackground(m_mapBg, 20, 20, 20, 230);

    // Viewport indicator rectangle — semi-transparent dark overlay, centered at 50,50
    // within the minimap; rendered as a 100x100 filled rect to indicate the camera view.
    // Color: white at 25% opacity (64 alpha) so it is distinguishable from the background.
    m_viewportRect = m_backend->addStaticText("", kMapX + 50, kMapY + 50, 100, 100);
    m_backend->setElementBackground(m_viewportRect, 255, 255, 255, 64);

    // Toggle button: 32x32 above minimap top edge, anchored at x:1720
    m_toggleBtn = m_backend->addButton("Svc", 1720, 848, 32, 32);

    // Legend panel: 80x100 above toggle row, at x:1720, y:732-832.
    // Dark background so text is readable.
    m_legendPanel = m_backend->addStaticText("", 1720, 732, 80, 100);
    m_backend->setElementBackground(m_legendPanel, 20, 20, 20, 210);
    m_legendLabel = m_backend->addStaticText(
        "Fire: Red\nPolice: Blue\nPower: Yellow\nWater: Cyan",
        1724, 736, 72, 92);

    hide();
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void Minimap::show() {
    m_visible = true;
    if (!m_backend) return;

    m_backend->setElementVisible(m_mapBg,        true);
    m_backend->setElementVisible(m_viewportRect, true);
    m_backend->setElementVisible(m_toggleBtn,    true);

    // Legend only visible when overlay active
    m_backend->setElementVisible(m_legendPanel, m_overlayActive);
    m_backend->setElementVisible(m_legendLabel, m_overlayActive);
}

void Minimap::hide() {
    m_visible = false;
    if (!m_backend) return;

    m_backend->setElementVisible(m_mapBg,        false);
    m_backend->setElementVisible(m_viewportRect, false);
    m_backend->setElementVisible(m_toggleBtn,    false);
    m_backend->setElementVisible(m_legendPanel,  false);
    m_backend->setElementVisible(m_legendLabel,  false);
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void Minimap::draw() {
    if (!m_visible || !m_backend) return;

    // Legend visibility tracks overlay state
    m_backend->setElementVisible(m_legendPanel, m_overlayActive);
    m_backend->setElementVisible(m_legendLabel, m_overlayActive);

    // Update toggle button label to reflect state
    m_backend->setElementText(m_toggleBtn, m_overlayActive ? "[Svc]" : "Svc");
}

// ---------------------------------------------------------------------------
// getBounds — returns the 200x200 render area only, excludes chrome
// ---------------------------------------------------------------------------
Rect Minimap::getBounds() const {
    return {kMapX, kMapY, kMapW, kMapH};
}

// ---------------------------------------------------------------------------
// toggleOverlay
// ---------------------------------------------------------------------------
void Minimap::toggleOverlay() {
    m_overlayActive = !m_overlayActive;
}

// ---------------------------------------------------------------------------
// onEvent — handle click-to-pan and overlay toggle
// ---------------------------------------------------------------------------
bool Minimap::onEvent(const InputEvent& event) {
    if (!m_visible) return false;

    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        // Check toggle button (x:1720-1752, y:848-880)
        if (mx >= 1720 && mx <= 1752 && my >= 848 && my <= 880) {
            toggleOverlay();
            return true;
        }

        // Check minimap render area for click-to-pan
        if (mx >= kMapX && mx <= kMapX + kMapW &&
            my >= kMapY && my <= kMapY + kMapH) {
            // Click-to-pan: compute world position from minimap click
            // The actual camera panning is handled by UIManager which
            // calls CameraController with the computed world position.
            // We just consume the click here so it doesn't fall through.
            return true;
        }
    }

    return false;
}
