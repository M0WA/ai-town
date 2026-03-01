// src/ui/QueryPanel.cpp
//
// InspectorPanel (Query Panel) — 240x160 px floating panel showing tile data.
// Implements computePanelPosition() as a pure static function for testability.
// Placement cascade: primary right-below, fallback left-above, edge-snap.

#include "src/ui/inspector_panel.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/platform/input_event.h"

#include <cstdio>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// Helper: zone type to string
// ---------------------------------------------------------------------------
static const char* zoneTypeName(ZoneType zt) {
    switch (zt) {
        case ZoneType::Residential: return "Residential";
        case ZoneType::Commercial:  return "Commercial";
        case ZoneType::Industrial:  return "Industrial";
    }
    return "Unknown";
}

static const char* densityName(DensityTier dt) {
    switch (dt) {
        case DensityTier::Low:    return "Low";
        case DensityTier::Medium: return "Medium";
        case DensityTier::High:   return "High";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// computePanelPosition — static pure function for testable placement
// ---------------------------------------------------------------------------
Rect InspectorPanel::computePanelPosition(int clickX, int clickY,
                                           int screenW, int screenH) {
    constexpr int pw = 240;
    constexpr int ph = 160;
    constexpr int offset = 40;

    // Primary: right-below cursor
    int px = std::max(0, std::min(clickX + offset, screenW - pw));
    int py = std::max(0, std::min(clickY + offset, screenH - ph));

    // Simple heuristic: if primary position overlaps the click point area,
    // try fallback above-left
    bool primaryOverlaps = (px <= clickX + 20 && px + pw >= clickX - 20 &&
                            py <= clickY + 20 && py + ph >= clickY - 20);

    if (primaryOverlaps) {
        int fx = std::max(0, std::min(clickX - offset - pw, screenW - pw));
        int fy = std::max(0, std::min(clickY - offset - ph, screenH - ph));

        bool fallbackOverlaps = (fx <= clickX + 20 && fx + pw >= clickX - 20 &&
                                 fy <= clickY + 20 && fy + ph >= clickY - 20);

        if (fallbackOverlaps) {
            // Third fallback: edge-snap
            int ex = (clickX <= screenW / 2) ? (screenW - pw) : 0;
            int ey = std::max(0, std::min(clickY - 80, screenH - ph));
            return {ex, ey, pw, ph};
        }
        return {fx, fy, pw, ph};
    }

    return {px, py, pw, ph};
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
InspectorPanel::InspectorPanel(IUIBackend* backend, ICitySimulation* sim)
    : m_backend(backend)
    , m_sim(sim)
    , m_visible(false)
{
    if (!m_backend) return;

    constexpr int lineH = 20;
    // Create elements at origin — we reposition them in show()
    m_panelBg          = m_backend->addStaticText("",     0, 0, kPanelW, kPanelH);
    m_coordsLabel      = m_backend->addStaticText("Tile", 4, 4, kPanelW - 8, lineH);
    m_zoneLabel        = m_backend->addStaticText("Zone", 4, 24, kPanelW - 8, lineH);
    m_popLabel         = m_backend->addStaticText("Pop",  4, 44, kPanelW - 8, lineH);
    m_coverageLabel    = m_backend->addStaticText("Svc",  4, 64, kPanelW - 8, lineH);
    m_desirabilityLabel= m_backend->addStaticText("Des",  4, 84, kPanelW - 8, lineH);
    m_demandLabel      = m_backend->addStaticText("Dem",  4, 104,kPanelW - 8, lineH);

    hide();
}

// ---------------------------------------------------------------------------
// show (with tile + click position)
// ---------------------------------------------------------------------------
void InspectorPanel::show(int tileX, int tileZ, int clickX, int clickY) {
    m_queryTileX = tileX;
    m_queryTileZ = tileZ;

    int screenW = m_backend ? m_backend->getVirtualWidth()  : 1920;
    int screenH = m_backend ? m_backend->getVirtualHeight() : 1080;

    Rect pos = computePanelPosition(clickX, clickY, screenW, screenH);
    m_panelX = pos.x;
    m_panelY = pos.y;

    m_visible = true;
    if (!m_backend) return;

    // Show all elements
    m_backend->setElementVisible(m_panelBg,           true);
    m_backend->setElementVisible(m_coordsLabel,       true);
    m_backend->setElementVisible(m_zoneLabel,         true);
    m_backend->setElementVisible(m_popLabel,          true);
    m_backend->setElementVisible(m_coverageLabel,     true);
    m_backend->setElementVisible(m_desirabilityLabel, true);
    m_backend->setElementVisible(m_demandLabel,       true);
}

void InspectorPanel::show() {
    // Legacy overload: show at default position (center screen)
    show(0, 0, 960, 540);
}

// ---------------------------------------------------------------------------
// hide
// ---------------------------------------------------------------------------
void InspectorPanel::hide() {
    m_visible = false;
    if (!m_backend) return;

    m_backend->setElementVisible(m_panelBg,           false);
    m_backend->setElementVisible(m_coordsLabel,       false);
    m_backend->setElementVisible(m_zoneLabel,         false);
    m_backend->setElementVisible(m_popLabel,          false);
    m_backend->setElementVisible(m_coverageLabel,     false);
    m_backend->setElementVisible(m_desirabilityLabel, false);
    m_backend->setElementVisible(m_demandLabel,       false);
}

// ---------------------------------------------------------------------------
// draw — refresh panel data from simulation
// ---------------------------------------------------------------------------
void InspectorPanel::draw() {
    if (!m_visible || !m_backend || !m_sim) return;

    QueryResult qr = m_sim->queryTile(m_queryTileX, m_queryTileZ);

    // Coordinates
    std::string coordStr = "Tile: (" + std::to_string(qr.tileX) + ", "
                           + std::to_string(qr.tileZ) + ")";
    m_backend->setElementText(m_coordsLabel, coordStr);

    if (qr.isZoned) {
        // Zone type + density
        std::string zoneStr = std::string(zoneTypeName(qr.zoneType)) + " ("
                              + densityName(qr.densityTier) + ")";
        m_backend->setElementText(m_zoneLabel, zoneStr);

        // Population
        m_backend->setElementText(m_popLabel, "Pop: " + std::to_string(qr.population));

        // Service coverage
        char covBuf[128];
        std::snprintf(covBuf, sizeof(covBuf), "Fire:%.0f%% Pol:%.0f%% Pwr:%.0f%% Wtr:%.0f%%",
                      qr.coverage.fire >= 0.0f   ? qr.coverage.fire   : 0.0f,
                      qr.coverage.police >= 0.0f ? qr.coverage.police : 0.0f,
                      qr.coverage.power >= 0.0f  ? qr.coverage.power  : 0.0f,
                      qr.coverage.water >= 0.0f  ? qr.coverage.water  : 0.0f);
        m_backend->setElementText(m_coverageLabel, covBuf);

        // Desirability
        char desBuf[32];
        std::snprintf(desBuf, sizeof(desBuf), "Desirability: %.0f", qr.desirability);
        m_backend->setElementText(m_desirabilityLabel, desBuf);

        // Demand pressure (inverse semantics: 0 = satisfied, 100 = zero demand)
        char demBuf[32];
        std::snprintf(demBuf, sizeof(demBuf), "Demand: %.0f%%", qr.demandPressurePct);
        m_backend->setElementText(m_demandLabel, demBuf);
    } else {
        m_backend->setElementText(m_zoneLabel, "Unzoned");
        m_backend->setElementText(m_popLabel, "");
        m_backend->setElementText(m_coverageLabel, "");
        m_backend->setElementText(m_desirabilityLabel, "");
        m_backend->setElementText(m_demandLabel, "");
    }
}

// ---------------------------------------------------------------------------
// getBounds
// ---------------------------------------------------------------------------
Rect InspectorPanel::getBounds() const {
    return {m_panelX, m_panelY, kPanelW, kPanelH};
}

// ---------------------------------------------------------------------------
// onEvent
// ---------------------------------------------------------------------------
bool InspectorPanel::onEvent(const InputEvent& event) {
    if (!m_visible) return false;

    // Escape closes the panel (consumed — does not open pause menu)
    if (event.type == InputEvent::Type::KeyDown && event.keyCode == 27) {
        hide();
        return true;
    }

    // Click outside panel dismisses it (click NOT consumed)
    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;
        if (mx < m_panelX || mx > m_panelX + kPanelW ||
            my < m_panelY || my > m_panelY + kPanelH) {
            hide();
            return false;
        }
        // Click inside panel — consumed
        return true;
    }

    return false;
}
