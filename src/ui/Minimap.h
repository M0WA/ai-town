#pragma once
#include "src/interfaces/IUIBackend.h"      // UIElementHandle, IUIBackend, Rect
#include "src/interfaces/ICitySimulation.h" // ICitySimulation — for getRoadSegmentSpeeds / getServiceCoverage

struct InputEvent;

// MinimapOverlay — which overlay mode is currently active on the minimap.
// Phase 11d Deliverables 3c and 4b add Traffic and ServiceCoverage modes.
enum class MinimapOverlay {
    None,            // default: no overlay (zone colours only)
    ServiceCoverage, // Phase 8 original mode — service building coverage tinting
    Traffic,         // Phase 11d Deliverable 3c — road congestion colour-coding
};

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

    // Phase 11d: wire ICitySimulation pointer so draw() can poll traffic/coverage data.
    // Called from UIManager constructor after m_sim is available.
    void setSimulation(ICitySimulation* sim) { m_sim = sim; }

    // Phase 11d: switch overlay mode explicitly (called from UIManager toolbar button).
    void setOverlayMode(MinimapOverlay mode) { m_overlayMode = mode; }
    MinimapOverlay getOverlayMode() const { return m_overlayMode; }

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};   // Phase 11d: non-owning, may be null
    bool m_visible{false};
    bool m_overlayActive{false};
    MinimapOverlay m_overlayMode{MinimapOverlay::ServiceCoverage}; // default: service coverage

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
