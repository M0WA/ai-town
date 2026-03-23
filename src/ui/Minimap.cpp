// src/ui/Minimap.cpp
//
// Minimap — 200x200 px at bottom-right (virtual x:1720-1920, y:880-1080).
// Top-down zone color coding, road network, camera viewport rectangle.
// Click-to-pan camera, service coverage overlay toggle.
//
// Phase 11d Deliverable 3c: Traffic overlay — road tile congestion colour-coding.
// Phase 11d Deliverable 4b: ServiceCoverage overlay — per-service tile tinting.

#include "src/ui/Minimap.h"
#include "src/platform/input_event.h"
#include "src/interfaces/simulation_types.h"  // RoadSegmentSpeed, ServiceCoverageTile, ServiceBuildingType

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
    // zone/road tile data is available.  Color: dark navy 88% opaque (13, 27, 42, 217).
    // Phase 10c Glass City Colour Pass: dark navy replaces dark grey.
    m_mapBg = m_backend->addStaticText("", kMapX, kMapY, kMapW, kMapH);
    m_backend->setElementBackground(m_mapBg, 13, 27, 42, 217);

    // Viewport indicator rectangle — semi-transparent dark overlay, centered at 50,50
    // within the minimap; rendered as a 100x100 filled rect to indicate the camera view.
    // Color: white at 25% opacity (64 alpha) so it is distinguishable from the background.
    m_viewportRect = m_backend->addStaticText("", kMapX + 50, kMapY + 50, 100, 100);
    m_backend->setElementBackground(m_viewportRect, 255, 255, 255, 64);

    // Toggle button: 32x32 above minimap top edge, anchored at x:1720
    m_toggleBtn = m_backend->addButton("Svc", 1720, 848, 32, 32);

    // Legend panel: 80x100 above toggle row, at x:1720, y:732-832.
    // Dark navy background so text is readable. Phase 10c Glass City Colour Pass.
    m_legendPanel = m_backend->addStaticText("", 1720, 732, 80, 100);
    m_backend->setElementBackground(m_legendPanel, 13, 27, 42, 209);
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

    // Update toggle button label to reflect current overlay mode.
    if (!m_overlayActive) {
        m_backend->setElementText(m_toggleBtn, "Svc");
    } else if (m_overlayMode == MinimapOverlay::Traffic) {
        m_backend->setElementText(m_toggleBtn, "[Tfc]");
    } else {
        m_backend->setElementText(m_toggleBtn, "[Svc]");
    }

    // Phase 11d Deliverable 3c: Traffic congestion overlay.
    // When active and simulation is available, colour each road tile by speed fraction:
    //   >= 0.4 → Green  #27AE60 (free-flow)
    //   0.31-0.39 → Orange #E67E22 (mild congestion)
    //   <= 0.30 → Red   #E74C3C (moderate-heavy congestion)
    if (m_overlayActive && m_overlayMode == MinimapOverlay::Traffic && m_sim) {
        const auto& speeds = m_sim->getRoadSegmentSpeeds();
        // Minimap maps tiles linearly to the 200x200 pixel area.
        // Without map dimension knowledge we render a proportional overlay using
        // backend draw primitives. The minimap spans tiles [0, mapW)×[0, mapH) over
        // the 200×200 px area. Since we don't have map dimensions here, we use a
        // fixed 128-tile reference (the V1 default map size).
        // Each tile maps to ~1.5 px (200/128); we render a 2×2 px dot per tile.
        // This is a best-effort visual overlay; precise pixel mapping requires map
        // dimensions injected via setSimulation or a separate setMapDimensions setter.
        constexpr float kRefMapTiles = 128.0f;
        constexpr float kMinimapPx = 200.0f;
        const float scale = kMinimapPx / kRefMapTiles;

        for (const auto& seg : speeds) {
            int px = kMapX + static_cast<int>(seg.tileX * scale);
            int py = kMapY + static_cast<int>(seg.tileZ * scale);
            if (px < kMapX || px >= kMapX + kMapW || py < kMapY || py >= kMapY + kMapH) continue;

            // Select colour by speed fraction.
            int r, g, b;
            if (seg.speedFraction >= 0.4f) {
                r = 0x27; g = 0xAE; b = 0x60;  // Green #27AE60
            } else if (seg.speedFraction >= 0.31f) {
                r = 0xE6; g = 0x7E; b = 0x22;  // Orange #E67E22
            } else {
                r = 0xE7; g = 0x4C; b = 0x3C;  // Red #E74C3C
            }
            // Draw a 2×2 pixel dot for each road tile.
            UIElementHandle dot = m_backend->addStaticText("", px, py, 2, 2);
            if (dot != kInvalidUIElement) {
                m_backend->setElementBackground(dot, r, g, b, 200);
            }
        }
    }

    // Phase 11d Deliverable 4b: Service coverage overlay.
    // When active and simulation is available, colour covered tiles by service type:
    //   Fire Station:   red  #C0392B
    //   Police Station: blue #2E4482
    //   Power Plant:    yellow #F1C40F
    //   Water Tower:    cyan #1ABC9C
    if (m_overlayActive && m_overlayMode == MinimapOverlay::ServiceCoverage && m_sim) {
        const auto& coverage = m_sim->getServiceCoverage();
        constexpr float kRefMapTiles = 128.0f;
        constexpr float kMinimapPx = 200.0f;
        const float scale = kMinimapPx / kRefMapTiles;

        for (const auto& sct : coverage) {
            int px = kMapX + static_cast<int>(sct.tileX * scale);
            int py = kMapY + static_cast<int>(sct.tileZ * scale);
            if (px < kMapX || px >= kMapX + kMapW || py < kMapY || py >= kMapY + kMapH) continue;

            int r, g, b;
            switch (sct.coveredBy) {
                case ServiceBuildingType::FireStation:
                    r = 0xC0; g = 0x39; b = 0x2B; break;  // #C0392B
                case ServiceBuildingType::PoliceStation:
                    r = 0x2E; g = 0x44; b = 0x82; break;  // #2E4482
                case ServiceBuildingType::PowerPlant:
                    r = 0xF1; g = 0xC4; b = 0x0F; break;  // #F1C40F
                case ServiceBuildingType::WaterTower:
                    r = 0x1A; g = 0xBC; b = 0x9C; break;  // #1ABC9C
                default:
                    continue;
            }
            UIElementHandle dot = m_backend->addStaticText("", px, py, 2, 2);
            if (dot != kInvalidUIElement) {
                m_backend->setElementBackground(dot, r, g, b, 180);
            }
        }
    }
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
