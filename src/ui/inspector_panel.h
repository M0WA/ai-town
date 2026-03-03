#pragma once
#include "src/ui/IUIBackend.h"      // UIElementHandle, IUIBackend, Rect
#include "src/interfaces/IRenderer.h"  // ScreenRect — Irrlicht-free bounding-box type

// Forward declarations — avoid pulling full headers into every consumer.
class ICitySimulation;
struct InputEvent;
struct QueryResult;

// InspectorPanel (Query Panel) — 240x160 px floating panel showing tile data.
// Activated by Query tool mode; shows zone type, population, service coverage,
// desirability, and demand pressure for a selected tile.
//
// Phase 9b changes:
//   - computePanelPosition signature updated: screenW/screenH parameters replaced by
//     const ScreenRect& tileBounds (tile-overlap detection per
//     architecture/ui-ux/query-inspector-panel.md — Tile overlap prevention).
//     Edge-snap derives virtual screen bounds from fixed constants 1920 x 1080.
//   - populate(QueryResult, tileX, tileZ) fills in real implementation from Phase 8 stub.
//
// NOTE: all tests that call computePanelPosition must be updated to pass a ScreenRect
// tileBounds argument instead of screenW/screenH.  Pass ScreenRect{1000, 1000, 10, 10}
// to replicate the Phase 8 off-screen / non-overlapping behaviour for existing tests.
class InspectorPanel {
public:
    InspectorPanel(IUIBackend* backend, ICitySimulation* sim);

    // Show the panel for the given tile coordinates.
    // clickX/clickY are the virtual-space cursor position used for placement.
    void show(int tileX, int tileZ, int clickX, int clickY);

    // Legacy overload used by UIManager (show at default location)
    void show();

    // populate — fill in real tile query data and (re)position the panel.
    // Phase 8 delivered a stub with empty body; Phase 9b fills in the real implementation.
    // Destroys any previously-created panel elements and recreates them at the new position
    // computed by computePanelPosition() using the three-step cascade from
    // architecture/ui-ux/query-inspector-panel.md.
    // cursorX/cursorY: virtual-space cursor position (un-projected from physical pixels).
    // tileBounds: queried tile bounding box in virtual space (un-projected from getTileScreenBounds).
    void populate(const QueryResult& result, int tileX, int tileZ,
                  int cursorX, int cursorY, const ScreenRect& tileBounds);

    void hide();
    void draw();
    bool onEvent(const InputEvent& event);
    bool isOpen() const { return m_visible; }
    Rect getBounds() const;

    // computePanelPosition — pure function for testable panel placement logic.
    //
    // Implements the 3-step cascade from architecture/ui-ux/query-inspector-panel.md:
    //   Step 1: Primary placement — right-below the cursor (cursorX+offset, cursorY+offset).
    //           Accepted if the resulting rect fits within 1920x1080 AND does not overlap
    //           tileBounds.
    //   Step 2: Fallback placement — left-above the cursor.
    //           Accepted if the resulting rect fits within 1920x1080 AND does not overlap
    //           tileBounds.
    //   Step 3: Edge-snap — clamp to [0, 1920-panelW] x [0, 1080-panelH].
    //           Always accepted as the final fallback.
    //
    // Phase 9b: screenW/screenH parameters replaced by tileBounds.
    // Edge-snap uses fixed constants 1920 x 1080 for virtual screen bounds.
    // Returns ScreenRect in virtual 1920x1080 space.
    //
    // Migration guide for tests:
    //   Replace computePanelPosition(clickX, clickY, 1920, 1080)
    //   with    computePanelPosition(clickX, clickY, ScreenRect{1000,1000,10,10})
    //   The off-screen tileBounds do not trigger the overlap check, so all existing
    //   assertions about primary/fallback/edge-snap positions remain valid.
    static ScreenRect computePanelPosition(int cursorX, int cursorY,
                                           const ScreenRect& tileBounds);

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    bool             m_visible{false};

    static constexpr int kPanelW = 240;
    static constexpr int kPanelH = 160;

    // Data refresh cadence — per architecture/ui-ux/query-inspector-panel.md.
    // Economy/service refresh: once per budget tick (~2 s at 60 FPS approximation).
    // Traffic refresh: every 10 simulation frames per spec.
    // Staleness label: shown when economy data is >1 s old (>kStalenessFrames draw calls).
    // m_lastEconomyFrame is initialised to -kEconomyRefreshFrames so that the very first
    // draw() call after show() always triggers an immediate refresh (no blank-frame window).
    static constexpr int kEconomyRefreshFrames = 120; // ~2 s at 60 FPS; approximates budget tick
    static constexpr int kTrafficRefreshFrames  = 10;  // per spec: every 10 simulation frames
    static constexpr int kStalenessFrames       = 60;  // show label when data is > ~1 s stale

    int m_panelX{0};
    int m_panelY{0};
    int m_queryTileX{0};
    int m_queryTileZ{0};

    // Panel elements
    UIElementHandle m_panelBg{kInvalidUIElement};
    UIElementHandle m_coordsLabel{kInvalidUIElement};
    UIElementHandle m_zoneLabel{kInvalidUIElement};
    UIElementHandle m_popLabel{kInvalidUIElement};
    UIElementHandle m_coverageLabel{kInvalidUIElement};
    UIElementHandle m_desirabilityLabel{kInvalidUIElement};
    UIElementHandle m_demandLabel{kInvalidUIElement};
    UIElementHandle m_updatedLabel{kInvalidUIElement};  // "Updated N s ago" staleness line

    // Refresh cadence frame counters.
    int m_drawFrame{0};
    int m_lastEconomyFrame{-kEconomyRefreshFrames};  // triggers refresh on first draw()
    int m_lastTrafficFrame{0};

    void positionElements();
};
