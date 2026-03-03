// src/ui/QueryPanel.cpp
//
// InspectorPanel (Query Panel) — 240x160 px floating panel showing tile data.
// Implements computePanelPosition() as a pure static function for testability.
// Placement cascade: primary right-below, fallback left-above, edge-snap.
//
// Phase 9b changes:
//   - computePanelPosition signature: screenW/screenH replaced by ScreenRect tileBounds.
//     Edge-snap uses fixed virtual constants 1920 x 1080.
//   - populate() real implementation (was stub in Phase 8).

#include "src/ui/inspector_panel.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/platform/input_event.h"

#include <cstdio>
#include <algorithm>
#include <string>

// Virtual screen dimensions — fixed 1920x1080 base resolution.
// computePanelPosition() uses these for edge-snap bounds.
static constexpr int kVirtualW = 1920;
static constexpr int kVirtualH = 1080;

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

// Axis-aligned rectangle overlap test (AABB).
static bool rectsOverlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

// ---------------------------------------------------------------------------
// computePanelPosition — static pure function for testable placement
//
// Phase 9b signature: (cursorX, cursorY, tileBounds) replacing (clickX, clickY, screenW, screenH).
// Edge-snap uses fixed constants kVirtualW x kVirtualH.
// Three-step cascade per architecture/ui-ux/query-inspector-panel.md:
//   1. Primary: place right-below cursor; reject if off-screen OR overlaps tileBounds.
//   2. Fallback: place left-above cursor; reject if off-screen OR overlaps tileBounds.
//   3. Edge-snap: clamp to [0, kVirtualW-pw] x [0, kVirtualH-ph]; always accepted.
// ---------------------------------------------------------------------------
ScreenRect InspectorPanel::computePanelPosition(int cursorX, int cursorY,
                                                const ScreenRect& tileBounds) {
    constexpr int pw     = 240;
    constexpr int ph     = 160;
    constexpr int offset = 40;

    // Step 1: Primary — right-below cursor
    {
        int px = cursorX + offset;
        int py = cursorY + offset;
        // Fits within virtual screen?
        bool fits = (px >= 0 && px + pw <= kVirtualW &&
                     py >= 0 && py + ph <= kVirtualH);
        bool overlaps = rectsOverlap(px, py, pw, ph,
                                     tileBounds.x, tileBounds.y,
                                     tileBounds.w, tileBounds.h);
        if (fits && !overlaps) {
            return {px, py, pw, ph};
        }
    }

    // Step 2: Fallback — left-above cursor
    {
        int fx = cursorX - offset - pw;
        int fy = cursorY - offset - ph;
        bool fits = (fx >= 0 && fx + pw <= kVirtualW &&
                     fy >= 0 && fy + ph <= kVirtualH);
        bool overlaps = rectsOverlap(fx, fy, pw, ph,
                                     tileBounds.x, tileBounds.y,
                                     tileBounds.w, tileBounds.h);
        if (fits && !overlaps) {
            return {fx, fy, pw, ph};
        }
    }

    // Step 3: Edge-snap — always accepted
    {
        // Choose horizontal side: right half of screen → snap to left edge, else right edge.
        int ex = (cursorX > kVirtualW / 2) ? 0 : (kVirtualW - pw);
        int ey = std::max(0, std::min(cursorY - ph / 2, kVirtualH - ph));
        return {ex, ey, pw, ph};
    }
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

    // Use off-screen tileBounds so tile-overlap check never triggers from
    // the legacy show() call path (Phase 8 behaviour preserved).
    ScreenRect noTileBounds{kVirtualW + 1, kVirtualH + 1, 10, 10};
    ScreenRect pos = computePanelPosition(clickX, clickY, noTileBounds);
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
// populate — real Phase 9b implementation
//
// Destroys any previously-created panel elements and recreates them at the
// position computed by computePanelPosition() (three-step cascade).
// Phase 9b: called from UIManager Priority-3 QueryTool open path with real
// cursor position (un-projected) and tile screen bounds (un-projected).
// ---------------------------------------------------------------------------
void InspectorPanel::populate(const QueryResult& result, int tileX, int tileZ,
                               int cursorX, int cursorY, const ScreenRect& tileBounds) {
    m_queryTileX = tileX;
    m_queryTileZ = tileZ;

    ScreenRect pos = computePanelPosition(cursorX, cursorY, tileBounds);
    m_panelX = pos.x;
    m_panelY = pos.y;

    // Destroy and recreate panel elements at the new position.
    // IUIBackend does not provide setElementPosition, so we use destroy-and-recreate.
    if (m_backend) {
        // Remove previously-created handles if they exist.
        // Note: kInvalidUIElement (0) is safe to pass to removeElement in the
        // IUIBackend contract; implementations must guard against invalid handles.
        if (m_panelBg != kInvalidUIElement)           { m_backend->removeElement(m_panelBg);           m_panelBg = kInvalidUIElement; }
        if (m_coordsLabel != kInvalidUIElement)       { m_backend->removeElement(m_coordsLabel);       m_coordsLabel = kInvalidUIElement; }
        if (m_zoneLabel != kInvalidUIElement)         { m_backend->removeElement(m_zoneLabel);         m_zoneLabel = kInvalidUIElement; }
        if (m_popLabel != kInvalidUIElement)          { m_backend->removeElement(m_popLabel);          m_popLabel = kInvalidUIElement; }
        if (m_coverageLabel != kInvalidUIElement)     { m_backend->removeElement(m_coverageLabel);     m_coverageLabel = kInvalidUIElement; }
        if (m_desirabilityLabel != kInvalidUIElement) { m_backend->removeElement(m_desirabilityLabel); m_desirabilityLabel = kInvalidUIElement; }
        if (m_demandLabel != kInvalidUIElement)       { m_backend->removeElement(m_demandLabel);       m_demandLabel = kInvalidUIElement; }

        constexpr int lineH = 20;
        int bx = pos.x;
        int by = pos.y;

        m_panelBg          = m_backend->addStaticText("",     bx,        by,         kPanelW, kPanelH);
        m_coordsLabel      = m_backend->addStaticText("Tile", bx + 4,    by + 4,     kPanelW - 8, lineH);
        m_zoneLabel        = m_backend->addStaticText("Zone", bx + 4,    by + 24,    kPanelW - 8, lineH);
        m_popLabel         = m_backend->addStaticText("Pop",  bx + 4,    by + 44,    kPanelW - 8, lineH);
        m_coverageLabel    = m_backend->addStaticText("Svc",  bx + 4,    by + 64,    kPanelW - 8, lineH);
        m_desirabilityLabel= m_backend->addStaticText("Des",  bx + 4,    by + 84,    kPanelW - 8, lineH);
        m_demandLabel      = m_backend->addStaticText("Dem",  bx + 4,    by + 104,   kPanelW - 8, lineH);
    }

    m_visible = true;

    // Populate text fields immediately so data is visible on the first draw() call.
    // (draw() polls queryTile() again each frame for live data refresh.)
    if (!m_backend) return;

    std::string coordStr = "Tile: (" + std::to_string(tileX) + ", "
                           + std::to_string(tileZ) + ")";
    m_backend->setElementText(m_coordsLabel, coordStr);

    if (result.isZoned) {
        std::string zoneStr = std::string(zoneTypeName(result.zoneType)) + " ("
                              + densityName(result.densityTier) + ")";
        m_backend->setElementText(m_zoneLabel, zoneStr);
        m_backend->setElementText(m_popLabel, "Pop: " + std::to_string(result.population));

        char covBuf[128];
        std::snprintf(covBuf, sizeof(covBuf), "Fire:%.0f%% Pol:%.0f%% Pwr:%.0f%% Wtr:%.0f%%",
                      result.coverage.fire   >= 0.0f ? result.coverage.fire   : 0.0f,
                      result.coverage.police >= 0.0f ? result.coverage.police : 0.0f,
                      result.coverage.power  >= 0.0f ? result.coverage.power  : 0.0f,
                      result.coverage.water  >= 0.0f ? result.coverage.water  : 0.0f);
        m_backend->setElementText(m_coverageLabel, covBuf);

        char desBuf[32];
        std::snprintf(desBuf, sizeof(desBuf), "Desirability: %.0f", result.desirability);
        m_backend->setElementText(m_desirabilityLabel, desBuf);

        char demBuf[32];
        std::snprintf(demBuf, sizeof(demBuf), "Demand: %.0f%%", result.demandPressurePct);
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
// positionElements
// ---------------------------------------------------------------------------
void InspectorPanel::positionElements() {
    // Repositions all panel elements relative to m_panelX, m_panelY.
    // Called after show() sets the panel position.
    // (No-op if backend is null — guards against headless test contexts.)
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
