// src/ui/Minimap.cpp
//
// Minimap — 200x200 px at bottom-right (virtual x:1720-1920, y:880-1080).
// Top-down zone color coding, road network, camera viewport rectangle.
// Click-to-pan camera, service/traffic overlay toggle.

#include "src/ui/Minimap.h"
#include "src/platform/input_event.h"
#include "src/ui/CameraController.h"  // CameraController::kMaxZoomDistance
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Minimap::Minimap(IUIBackend* backend, IAudioSystem* /*audio*/, ICitySimulation* sim, IClock* clock)
    : m_backend(backend)
    , m_sim(sim)
    , m_clock(clock)
    , m_visible(false)
    , m_overlayActive(false)
{
    if (!m_backend) return;

    // Minimap background (200x200 at bottom-right).
    // Dark navy 88% opaque (13, 27, 42, 217).
    m_mapBg = m_backend->addStaticText("", kMapX, kMapY, kMapW, kMapH);
    m_backend->setElementBackground(m_mapBg, 13, 27, 42, 217);

    // Toggle buttons: 32x32 above minimap top edge
    m_toggleBtnSvc = m_backend->addButton("Svc", 1720, 848, 32, 32);
    m_toggleBtnTfc = m_backend->addButton("Tfc", 1684, 848, 32, 32);

    // Label strip: static text at x:1720, y:832, w:200, h:16; start hidden
    m_labelStrip = m_backend->addStaticText("", 1720, 832, 200, 16);
    m_backend->setElementTextColor(m_labelStrip, 235, 244, 246);

    // Legend panel: static text at x:1720, y:732, w:200, h:100; start hidden
    m_legendPanel = m_backend->addStaticText("", 1720, 732, 200, 100);
    m_backend->setElementBackground(m_legendPanel, 13, 27, 42, 209);

    // Legend content text
    m_legendLabel = m_backend->addStaticText(
        "Fire: Red\nPolice: Blue\nPower: Yellow\nWater: Cyan",
        1724, 736, 192, 92);
    m_backend->setElementTextColor(m_legendLabel, 235, 244, 246);

    hide();
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void Minimap::show() {
    m_visible = true;
    if (!m_backend) return;

    m_backend->setElementVisible(m_mapBg, true);
    m_backend->setElementVisible(m_toggleBtnSvc, true);
    m_backend->setElementVisible(m_toggleBtnTfc, true);

    // Legend and label strip only visible when overlay active
    m_backend->setElementVisible(m_legendPanel, m_overlayActive);
    m_backend->setElementVisible(m_legendLabel, m_overlayActive);
    m_backend->setElementVisible(m_labelStrip, m_overlayActive);
}

void Minimap::hide() {
    m_visible = false;
    if (!m_backend) return;

    m_backend->setElementVisible(m_mapBg, false);
    m_backend->setElementVisible(m_toggleBtnSvc, false);
    m_backend->setElementVisible(m_toggleBtnTfc, false);
    m_backend->setElementVisible(m_legendPanel, false);
    m_backend->setElementVisible(m_legendLabel, false);
    m_backend->setElementVisible(m_labelStrip, false);
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void Minimap::draw() {
    if (!m_visible || !m_backend) return;

    // Budget-tick cache refresh
    if (m_pendingTicks > 0) {
        if (!m_sim) {
            m_pendingTicks = 0;
            return;
        }

        const int mapW = m_sim->getMapTilesX();
        const int mapD = m_sim->getMapTilesZ();
        m_tileCacheW = mapW;
        m_tileCacheD = mapD;
        m_tileCache.resize(static_cast<size_t>(mapW) * static_cast<size_t>(mapD));
        for (int z = 0; z < mapD; ++z) {
            for (int x = 0; x < mapW; ++x) {
                m_tileCache[static_cast<size_t>(z) * mapW + x] = m_sim->queryTile(x, z);
            }
        }

        // Service coverage sorted ascending by priority:
        // FireStation(2) < PoliceStation(3) < PowerPlant(0) < WaterTower(1)
        // Actually the spec says FireStation first, PoliceStation second, PowerPlant third, WaterTower last
        m_serviceCoverageCache = m_sim->getServiceCoverage();
        std::sort(m_serviceCoverageCache.begin(), m_serviceCoverageCache.end(),
            [](const ServiceCoverageTile& a, const ServiceCoverageTile& b) {
                // Priority order: FireStation=0, PoliceStation=1, PowerPlant=2, WaterTower=3
                auto priority = [](ServiceBuildingType t) -> int {
                    switch (t) {
                        case ServiceBuildingType::FireStation:   return 0;
                        case ServiceBuildingType::PoliceStation: return 1;
                        case ServiceBuildingType::PowerPlant:    return 2;
                        case ServiceBuildingType::WaterTower:    return 3;
                        default: return 4;
                    }
                };
                return priority(a.coveredBy) < priority(b.coveredBy);
            });

        m_roadSpeedCache = m_sim->getRoadSegmentSpeeds();
        m_pendingTicks = 0;
    }

    // Update legend visibility
    m_backend->setElementVisible(m_legendPanel, m_overlayActive);
    m_backend->setElementVisible(m_legendLabel, m_overlayActive);
    m_backend->setElementVisible(m_labelStrip, m_overlayActive);

    // Update legend text based on overlay mode
    if (m_overlayActive) {
        if (m_overlayMode == MinimapOverlay::ServiceCoverage) {
            m_backend->setElementText(m_legendLabel,
                "Fire: Red\nPolice: Blue\nPower: Yellow\nWater: Cyan");
            m_backend->setElementText(m_labelStrip, "Service Coverage");
        } else if (m_overlayMode == MinimapOverlay::Traffic) {
            m_backend->setElementText(m_legendLabel,
                "Free: Green\nMild: Orange\nHeavy: Red");
            m_backend->setElementText(m_labelStrip, "Traffic Congestion");
        }
    }

    // Update button opacity: active at 1.0, inactive at 0.65
    if (m_overlayActive && m_overlayMode == MinimapOverlay::ServiceCoverage) {
        m_backend->setElementAlpha(m_toggleBtnSvc, 1.0f);
        m_backend->setElementAlpha(m_toggleBtnTfc, 0.65f);
    } else if (m_overlayActive && m_overlayMode == MinimapOverlay::Traffic) {
        m_backend->setElementAlpha(m_toggleBtnSvc, 0.65f);
        m_backend->setElementAlpha(m_toggleBtnTfc, 1.0f);
    } else {
        m_backend->setElementAlpha(m_toggleBtnSvc, 0.65f);
        m_backend->setElementAlpha(m_toggleBtnTfc, 0.65f);
    }
}

// ---------------------------------------------------------------------------
// drawOverlay — transient per-frame overlay rendering (called after guiEnv->drawAll)
// ---------------------------------------------------------------------------
void Minimap::drawOverlay() {
    if (!m_visible || !m_backend || !m_sim) return;

    static constexpr float kTileSize = 10.0f;
    static_assert(kTileSize == 10.0f, "kTileSize must match IrrlichtRenderer::kTileSize");

    const float worldW = m_sim->getMapTilesX() * kTileSize;
    const float worldD = m_sim->getMapTilesZ() * kTileSize;

    const int tileW = m_tileCacheW > 0 ? std::max(1, kMapW / m_tileCacheW) : 1;
    const int tileH = m_tileCacheD > 0 ? std::max(1, kMapH / m_tileCacheD) : 1;

    // Zone color coding
    for (int z = 0; z < m_tileCacheD; ++z) {
        for (int x = 0; x < m_tileCacheW; ++x) {
            const auto& tile = m_tileCache[static_cast<size_t>(z) * m_tileCacheW + x];
            int px = kMapX + x * tileW;
            int py = kMapY + z * tileH;
            if (!tile.isRoad && tile.isZoned) {
                int r, g, b;
                switch (tile.zoneType) {
                    case ZoneType::Residential: r=0x27; g=0xAE; b=0x60; break;
                    case ZoneType::Commercial:  r=0x29; g=0x80; b=0xB9; break;
                    case ZoneType::Industrial:  r=0xF3; g=0x9C; b=0x12; break;
                    default: continue;
                }
                m_backend->fillColoredRect(px, py, tileW, tileH, r, g, b, 255);
            }
        }
    }

    // Road network
    for (int z = 0; z < m_tileCacheD; ++z) {
        for (int x = 0; x < m_tileCacheW; ++x) {
            const auto& tile = m_tileCache[static_cast<size_t>(z) * m_tileCacheW + x];
            if (tile.isRoad) {
                int px = kMapX + x * tileW;
                int py = kMapY + z * tileH;
                m_backend->fillColoredRect(px, py, tileW, tileH, 0x7F, 0x8C, 0x8D, 255);
            }
        }
    }

    // Service Coverage overlay (painter's algorithm, sorted ascending priority)
    if (m_overlayActive && m_overlayMode == MinimapOverlay::ServiceCoverage) {
        for (const auto& sct : m_serviceCoverageCache) {
            int px = kMapX + (m_tileCacheW > 0 ? sct.tileX * kMapW / m_tileCacheW : 0);
            int py = kMapY + (m_tileCacheD > 0 ? sct.tileZ * kMapH / m_tileCacheD : 0);
            int r, g, b;
            switch (sct.coveredBy) {
                case ServiceBuildingType::FireStation:   r=0xC0; g=0x39; b=0x2B; break;
                case ServiceBuildingType::PoliceStation: r=0x2E; g=0x44; b=0x82; break;
                case ServiceBuildingType::PowerPlant:    r=0xF1; g=0xC4; b=0x0F; break;
                case ServiceBuildingType::WaterTower:    r=0x1A; g=0xBC; b=0x9C; break;
                default: continue;
            }
            m_backend->fillColoredRect(px, py, tileW, tileH, r, g, b, 220);
        }
    }

    // Traffic Congestion overlay
    if (m_overlayActive && m_overlayMode == MinimapOverlay::Traffic) {
        for (const auto& seg : m_roadSpeedCache) {
            int px = kMapX + (m_tileCacheW > 0 ? seg.tileX * kMapW / m_tileCacheW : 0);
            int py = kMapY + (m_tileCacheD > 0 ? seg.tileZ * kMapH / m_tileCacheD : 0);
            int r, g, b;
            if (seg.speedFraction >= 0.4f) {
                r=0x27; g=0xAE; b=0x60;
            } else if (seg.speedFraction > 0.3f) {
                r=0xE6; g=0x7E; b=0x22;
            } else {
                r=0xE7; g=0x4C; b=0x3C;
            }
            m_backend->fillColoredRect(px, py, tileW, tileH, r, g, b, 255);
        }
    }

    // Legend swatches (8x8 px per row)
    if (m_overlayActive && m_overlayMode == MinimapOverlay::ServiceCoverage) {
        const struct { int r, g, b; } svcColors[] = {
            {0xC0,0x39,0x2B}, {0x2E,0x44,0x82}, {0xF1,0xC4,0x0F}, {0x1A,0xBC,0x9C}
        };
        for (int i = 0; i < 4; ++i) {
            m_backend->fillColoredRect(1724, 740 + i*16, 8, 8,
                svcColors[i].r, svcColors[i].g, svcColors[i].b, 255);
        }
    } else if (m_overlayActive && m_overlayMode == MinimapOverlay::Traffic) {
        const struct { int r, g, b; } tfcColors[] = {
            {0x27,0xAE,0x60}, {0xE6,0x7E,0x22}, {0xE7,0x4C,0x3C}
        };
        for (int i = 0; i < 3; ++i) {
            m_backend->fillColoredRect(1724, 740 + i*16, 8, 8,
                tfcColors[i].r, tfcColors[i].g, tfcColors[i].b, 255);
        }
    }

    // Camera viewport rectangle
    if (worldW > 0.f && worldD > 0.f) {
        float cx = (m_cameraState.targetX / worldW) * 200.f;
        float cz = (m_cameraState.targetZ / worldD) * 200.f;
        cx = std::max(0.f, std::min(200.f, cx));
        cz = std::max(0.f, std::min(200.f, cz));

        float side = 200.f * (m_cameraState.zoomDistance / CameraController::kMaxZoomDistance);
        side = std::max(8.f, std::min(190.f, side));

        int rectX = kMapX + static_cast<int>(cx - side/2.f);
        int rectY = kMapY + static_cast<int>(cz - side/2.f);
        int rectW = static_cast<int>(side);
        int rectH = static_cast<int>(side);
        // Clamp to minimap area
        rectX = std::max(kMapX, std::min(kMapX + kMapW - 1, rectX));
        rectY = std::max(kMapY, std::min(kMapY + kMapH - 1, rectY));

        // Four strips: top, bottom, left, right (2px thick)
        m_backend->fillColoredRect(rectX, rectY,           rectW, 2,     255, 255, 255, 200);
        m_backend->fillColoredRect(rectX, rectY+rectH-2,   rectW, 2,     255, 255, 255, 200);
        m_backend->fillColoredRect(rectX, rectY,           2,     rectH, 255, 255, 255, 200);
        m_backend->fillColoredRect(rectX+rectW-2, rectY,   2,     rectH, 255, 255, 255, 200);
    }
}

// ---------------------------------------------------------------------------
// getBounds — returns the 200x200 render area only, excludes chrome
// ---------------------------------------------------------------------------
UIRect Minimap::getBounds() const {
    return {kMapX, kMapY, kMapW, kMapH};
}

// ---------------------------------------------------------------------------
// getWidgetFootprint
// ---------------------------------------------------------------------------
UIRect Minimap::getWidgetFootprint() const {
    return UIRect{1576, m_overlayActive ? 732 : 848, 344, m_overlayActive ? 348 : 232};
}

// ---------------------------------------------------------------------------
// setSimulation
// ---------------------------------------------------------------------------
void Minimap::setSimulation(ICitySimulation* sim) {
    m_sim = sim;
}

// ---------------------------------------------------------------------------
// setOverlayMode
// ---------------------------------------------------------------------------
void Minimap::setOverlayMode(MinimapOverlay mode) {
    if (mode == MinimapOverlay::Traffic && m_sim == nullptr) {
        m_overlayMode = MinimapOverlay::None;
        return;
    }
    m_overlayMode = mode;
}

// ---------------------------------------------------------------------------
// toggleOverlay
// ---------------------------------------------------------------------------
void Minimap::toggleOverlay() {
    m_overlayActive = !m_overlayActive;
}

// ---------------------------------------------------------------------------
// setCameraState
// ---------------------------------------------------------------------------
void Minimap::setCameraState(const CameraState& state) {
    m_cameraState = state;
}

// ---------------------------------------------------------------------------
// setPanCallback
// ---------------------------------------------------------------------------
void Minimap::setPanCallback(std::function<void(float, float)> cb) {
    m_panCallback = std::move(cb);
}

// ---------------------------------------------------------------------------
// onBudgetTicks
// ---------------------------------------------------------------------------
void Minimap::onBudgetTicks(int count) {
    m_pendingTicks = count;
}

// ---------------------------------------------------------------------------
// onEvent — handle click-to-pan and overlay toggle
// ---------------------------------------------------------------------------
bool Minimap::onEvent(const InputEvent& event) {
    if (!m_visible) return false;

    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        // Check Svc toggle button (x:1720-1752, y:848-880)
        if (mx >= 1720 && mx <= 1752 && my >= 848 && my <= 880) {
            if (m_overlayActive && m_overlayMode == MinimapOverlay::ServiceCoverage) {
                // Clicking active button deactivates
                m_overlayActive = false;
            } else {
                m_overlayActive = true;
                m_overlayMode = MinimapOverlay::ServiceCoverage;
            }
            return true;
        }

        // Check Tfc toggle button (x:1684-1716, y:848-880)
        if (mx >= 1684 && mx <= 1716 && my >= 848 && my <= 880) {
            if (m_overlayActive && m_overlayMode == MinimapOverlay::Traffic) {
                m_overlayActive = false;
            } else {
                m_overlayActive = true;
                m_overlayMode = MinimapOverlay::Traffic;
            }
            return true;
        }

        // Check minimap render area for click-to-pan
        if (mx >= kMapX && mx <= kMapX + kMapW &&
            my >= kMapY && my <= kMapY + kMapH) {
            if (m_panCallback && m_sim) {
                static constexpr float kTileSize = 10.0f;
                float worldW = m_sim->getMapTilesX() * kTileSize;
                float worldD = m_sim->getMapTilesZ() * kTileSize;
                if (worldW > 0.f && worldD > 0.f) {
                    float fracX = static_cast<float>(mx - kMapX) / static_cast<float>(kMapW);
                    float fracZ = static_cast<float>(my - kMapY) / static_cast<float>(kMapH);
                    m_panCallback(fracX * worldW, fracZ * worldD);
                }
            }
            return true;
        }
    }

    return false;
}
