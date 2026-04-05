#pragma once
#include "src/interfaces/IUIBackend.h"      // UIElementHandle, IUIBackend, UIRect
#include "src/interfaces/ICitySimulation.h" // ICitySimulation
#include "src/interfaces/camera_state.h"    // CameraState
#include "src/interfaces/IClock.h"          // IClock
#include "src/interfaces/simulation_types.h" // QueryResult, ServiceCoverageTile, RoadSegmentSpeed
#include <functional>
#include <vector>

class IAudioSystem;
struct InputEvent;

// MinimapOverlay — which overlay mode is currently active on the minimap.
enum class MinimapOverlay {
    None,            // default: no overlay (zone colours only)
    ServiceCoverage, // service building coverage tinting
    Traffic,         // road congestion colour-coding
};

// Minimap — 200x200 px at bottom-right (x:1720-1920, y:880-1080) in virtual space.
// Top-down zone color coding (R=green, C=blue, I=yellow), road network.
// Camera viewport rectangle (white outline, clamped 8x8 to 190x190).
// Click-to-pan, service coverage overlay toggle.
class Minimap {
public:
    Minimap(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock);

    void show();
    void hide();
    void draw();
    void drawOverlay();
    bool onEvent(const InputEvent& event);
    UIRect getBounds() const;
    UIRect getWidgetFootprint() const;

    bool isOverlayActive() const { return m_overlayActive; }
    void toggleOverlay();

    void setSimulation(ICitySimulation* sim);
    void setOverlayMode(MinimapOverlay mode);
    MinimapOverlay getOverlayMode() const { return m_overlayMode; }

    void setCameraState(const CameraState& state);
    void setPanCallback(std::function<void(float, float)> cb);
    void onBudgetTicks(int count);

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    IClock*          m_clock{nullptr};
    bool m_visible{false};
    bool m_overlayActive{false};
    MinimapOverlay m_overlayMode{MinimapOverlay::ServiceCoverage};

    CameraState m_cameraState{};
    std::function<void(float, float)> m_panCallback{};
    int m_pendingTicks{0};

    // Minimap render area: x:1720-1920, y:880-1080 (200x200 px)
    static constexpr int kMapX = 1720;
    static constexpr int kMapY = 880;
    static constexpr int kMapW = 200;
    static constexpr int kMapH = 200;

    UIElementHandle m_mapBg{kInvalidUIElement};

    // Toggle buttons (32x32 above minimap)
    UIElementHandle m_toggleBtnSvc{kInvalidUIElement};  // Service Coverage toggle, x:1720, y:848, 32x32
    UIElementHandle m_toggleBtnTfc{kInvalidUIElement};  // Traffic Congestion toggle, x:1684, y:848, 32x32

    // Label strip (above toggle row)
    UIElementHandle m_labelStrip{kInvalidUIElement};

    // Legend panel (shown when overlay active, above toggle row)
    UIElementHandle m_legendPanel{kInvalidUIElement};
    UIElementHandle m_legendLabel{kInvalidUIElement};

    // Tile data caches (refreshed on budget ticks)
    std::vector<QueryResult>         m_tileCache;
    std::vector<ServiceCoverageTile> m_serviceCoverageCache;
    std::vector<RoadSegmentSpeed>    m_roadSpeedCache;
    int                              m_tileCacheW{0};
    int                              m_tileCacheD{0};
};
