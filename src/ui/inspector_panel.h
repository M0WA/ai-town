#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend, Rect

// Forward declaration — avoid pulling ICitySimulation.h into every consumer.
class ICitySimulation;
struct InputEvent;

// InspectorPanel (Query Panel) — 240x160 px floating panel showing tile data.
// Activated by Query tool mode; shows zone type, population, service coverage,
// desirability, and demand pressure for a selected tile.
class InspectorPanel {
public:
    InspectorPanel(IUIBackend* backend, ICitySimulation* sim);

    // Show the panel for the given tile coordinates.
    // clickX/clickY are the virtual-space cursor position used for placement.
    void show(int tileX, int tileZ, int clickX, int clickY);

    // Legacy overload used by UIManager (show at default location)
    void show();

    void hide();
    void draw();
    bool onEvent(const InputEvent& event);
    bool isOpen() const { return m_visible; }
    Rect getBounds() const;

    // Pure function for testable panel placement logic.
    // Implements 3-step cascade: primary right-below, fallback left-above, edge-snap.
    static Rect computePanelPosition(int clickX, int clickY, int screenW, int screenH);

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    bool             m_visible{false};

    static constexpr int kPanelW = 240;
    static constexpr int kPanelH = 160;

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

    void positionElements();
};
