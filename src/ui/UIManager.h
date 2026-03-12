#pragma once

#include "src/ui/IUIBackend.h"      // UIElementHandle, kInvalidUIElement, Rect
#include "src/ui/ui_types.h"        // GameMode, GameState, ActiveTool
#include "src/interfaces/IClock.h"  // IClock — full include (available at Phase 0)
#include "src/interfaces/LoanTerms.h"  // LoanTerms
#include <unordered_map>
#include <cstdint>

// Forward declarations — do NOT #include these headers in UIManager.h.
// IAudioSystem, ICitySimulation, IRenderer, ITerrainQuery are declared as pointers only.
// InputEvent is used as a const-reference parameter — forward declaration is valid in C++.
class IAudioSystem;
class ICitySimulation;
class IRenderer;
class ITerrainQuery;

// INCLUDE PROHIBITION: Do NOT replace this forward declaration with
// #include "src/platform/input_event.h". The platform header must not
// be pulled into every translation unit that includes UIManager.h —
// doing so creates circular-include ambiguity at Phase 3 when UIManager
// is included by simulation and audio components that must not depend on
// the platform layer. The full include belongs in UIManager.cpp ONLY.
struct InputEvent;

// Panel forward declarations — full includes are in UIManager.cpp ONLY.
// Headers that #include UIManager.h must not gain transitive dependencies
// on panel implementation headers.
class NotificationManager;
class MainMenuPanel;
class HUD;
class TaxRatePanel;
class Minimap;
class InspectorPanel;
class PauseMenuPanel;
class SettingsPanel;
class ModalDialog;

// UIManager — Phase 3 shell implementation.
// Phase 1 locked the 4-method signatures (constructor, onEvent, draw, update).
// Phase 3 adds new public methods and private members without changing those signatures.
// Phase 6 replaces stub bodies with full logic.
//
// Source location: src/ui/ (IUIBackend.h lives here; UIManager depends on it).
// IrrlichtUIBackend lives in src/rendering/ (Irrlicht headers).
//
// Draw order: 10 named slots called explicitly in Z-order from draw().
// m_gui->drawAll() is NOT called — it bypasses the explicit Z-order layering
// required for the background scrim and modal overlay.
//
// Input arbitration: 6-priority chain. Priority 2 uses a dual-guard compound guard:
//   criticalVisible && !modalActive
// This guard MUST be preserved verbatim in the Phase 3 shell.
class UIManager {
public:
    // 4-parameter constructor from architecture/ui-ux/ui-manager.md.
    // Stores all pointers (may be null). Allocates all panels in invariant order.
    // Signature is LOCKED at Phase 1 — Phase 3 does not change it.
    UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock);

    // Destructor — deletes all owned panels in reverse construction order.
    ~UIManager();

    // Handle an input event. Returns true if consumed (event should not propagate).
    // WindowFocusGained/Lost events: always return false (pass-through at Priority 1).
    bool onEvent(const InputEvent& event);

    // Issue explicit per-panel draw calls in Z-order via IUIBackend.
    // Called by IrrlichtRenderer::drawScene() INSIDE the beginScene/endScene pair.
    // m_gui->drawAll() is NOT called — that would bypass the explicit Z-order layering
    // required for the background scrim and modal overlay.
    void draw();

    // Per-frame UI state update (undo countdown, grace-period indicator, notification timers).
    // Called BEFORE beginFrame() per architecture/ui-ux/ui-manager.md.
    // Uses m_clock->nowSeconds() for undo expiry — NOT accumulated realDeltaSeconds.
    // Also polls m_sim->getConsecutiveDeficitMonths() for GD-H3 deficit-streak bridge.
    void update(float realDeltaSeconds);

    // --- State-transition methods (Phase 3 shell; full logic in Phase 6) ---

    // Transition to Paused state: show pause menu, keep HUD visible.
    void transitionToPaused();

    // Transition from Paused back to Gameplay state: hide pause menu.
    void transitionToGameplay_fromPaused();

    // Transition to GameOver state.
    // SANDBOX GUARD: MUST NOT fire when m_gameMode == GameMode::Sandbox.
    // Scenario-only in V1. See architecture/game-design/game-over-flow.md.
    void transitionToGameOver();

    // Show the forced-loan 2-screen dialog (640x400 px virtual).
    // Called by the GD-H3 bridge when CitySimulation signals a forced-loan event.
    void showForcedLoanDialog(const LoanTerms& terms);

    // Show the game-over modal (560x320 px virtual, stub in V1).
    // totalDebt and monthsInDeficit are displayed in the modal body.
    void showGameOverModal(int64_t totalDebt, int monthsInDeficit);

    // Close the active modal dialog (if any) and resume normal input routing.
    void closeModal();

    // Show the settings panel (e.g. from pause menu or toolbar button).
    void showSettings();

    // Transition to Gameplay state from Main Menu.
    // mode determines whether Sandbox or Scenario rules apply.
    // Sets m_gameMode and clears the unsaved-changes flag.
    void transitionToGameplay(GameMode mode);

    // Mark or clear the unsaved-changes indicator dot in the HUD toolbar.
    void setUnsavedChanges(bool value);

    // --- Phase 9b: late-bind setters (called from main.cpp after construction) ---

    // setRenderer — late-bind the IRenderer* used by the world-interaction layer.
    // Called from main.cpp after IrrlichtRenderer is constructed (step 3 of Phase 9b
    // wiring order in Deliverable H).  Must be called before the first frame; guarded
    // internally with a null-check so it is safe to defer but will silently no-op if
    // not called.  NOT on the IRenderer interface (same rationale as IrrlichtRenderer::
    // setTerrainQuery: one-time initialization setter, not a general renderer capability).
    void setRenderer(IRenderer* renderer);

    // setTerrainQuery — late-bind the ITerrainQuery* used for earthworks cost computation.
    // Called from main.cpp after TerrainSystem is constructed (step 4 of Phase 9b wiring).
    void setTerrainQuery(ITerrainQuery* terrain);

    // setMapDimensions — supply the map tile width and depth to UIManager.
    // Must be called from main.cpp after terrain generation completes (step 5 of Phase 9b
    // wiring, via terrainSystem.getMapTilesX() / getMapTilesZ()).
    // Re-call safety: if called a second time (e.g. new-game load with different map size),
    // m_overlayMap is cleared and setZoneOverlay({}) is issued before updating dimensions
    // so stale overlay keys from the old map width cannot corrupt the new map.
    void setMapDimensions(int mapTilesX, int mapTilesZ);

    // getActiveTool — returns the current active tool state (for test observability).
    ActiveTool getActiveTool() const;

    // setDemolishConfirm — enable/disable the demolish confirmation modal.
    // When false, demolishTile() is called immediately without showing a modal.
    // Used by tests to suppress the modal; production default is true (confirm ON).
    void setDemolishConfirm(bool enabled) { m_demolishConfirmEnabled = enabled; }

    // setOverlayMapForTest — test-seam to pre-populate m_overlayMap without
    // routing 100K UI events through onEvent().
    // Used exclusively by WorldInteraction_OverlayCap_100K_StillCalls to
    // inject exactly kOverlayCap entries so the cap-enforcement path can be
    // exercised with a single subsequent placement. Production code never calls
    // this method.
    // (ref: architecture/testing/testability-architecture.md — test seam pattern)
    void setOverlayMapForTest(const std::unordered_map<uint64_t, uint32_t>& map) {
        m_overlayMap = map;
    }

    // onNewGame — reset all world-interaction state for a new game load.
    // Clears m_overlayMap, calls setZoneOverlay({}) if renderer is non-null,
    // resets m_activeTool to None, and clears m_hoveredTileX/Z.
    // Called from UIManager::transitionToGameplay() or test harness.
    void onNewGame();

    // Returns true when a blocking modal dialog is currently active.
    // Used by the Priority-2 dual-guard and by tests.
    bool hasActiveModal() const;

    // Set the loading-terrain gate. While true, update() is a no-op
    // (terrain generation is in progress; no UI polling should occur).
    void setLoadingTerrain(bool loading);

    // Returns true when the user requested application quit
    // (from Main Menu Quit or Pause Menu Quit to Desktop).
    // Polled by main.cpp to break the frame loop.
    bool isQuitRequested() const;

    // Transition from Gameplay/Paused back to MainMenu state.
    // Called when "Quit to Main Menu" is selected from PauseMenuPanel.
    void transitionToMainMenu();

private:
    IUIBackend*      m_backend{nullptr};
    IAudioSystem*    m_audio{nullptr};
    ICitySimulation* m_sim{nullptr};
    IClock*          m_clock{nullptr};

    // UI state machine
    GameState  m_state{GameState::MainMenu};
    GameMode   m_gameMode{GameMode::Sandbox};

    // --- Phase 9b: late-bound renderer and terrain query (set via setters after construction) ---
    IRenderer*    m_renderer{nullptr};
    ITerrainQuery* m_terrain{nullptr};

    // --- Phase 9b: active tool state ---
    // Default: None (camera-only mode, same as Phase 8 initial state).
    // Set by Priority-5 toolbar dispatch and hotkeys Z/R/U/D/I.
    ActiveTool m_activeTool{ActiveTool::None};

    // --- Phase 9b: zone sub-panel selection state ---
    // ZoneType and DensityTier are defined in simulation_types.h; forward-declared here
    // to avoid pulling the full simulation header into UIManager.h.  The concrete values
    // are used only in UIManager.cpp.
    // Default: Residential + Low (leftmost/topmost button in the 3x3 grid).
    int m_selectedZoneType{0};    // 0=Residential, 1=Commercial, 2=Industrial
    int m_selectedDensityTier{0}; // 0=Low, 1=Medium, 2=High

    // --- Phase 9b: utilities sub-panel selection state ---
    // Default: PowerPlant (index 0 in ServiceBuildingType enum).
    int m_selectedServiceBuilding{0}; // matches ServiceBuildingType enum ordinal

    // --- Phase 9b: map dimensions (set via setMapDimensions() from main.cpp) ---
    // Both default to 0; overlay writes are skipped until setMapDimensions() has been called.
    int m_mapTilesX{0};
    int m_mapTilesZ{0};

    // --- Phase 9b: sparse zone overlay map ---
    // Key: tileZ * m_mapTilesX + tileX  (uint64_t)
    // Value: ARGB colour (0xAARRGGBB)
    // Updated on each successful placeZone() or demolishTile() call.
    // Passed directly to IRenderer::setZoneOverlay() — sparse entries only.
    // Capped at 100K entries for V1 (enforced in the overlay-insert path).
    std::unordered_map<uint64_t, uint32_t> m_overlayMap;

    // --- Phase 9b: last hovered tile coordinates ---
    // Stored by the MouseMove handler and consumed by the left-click handler.
    // {-1, -1} means no valid hovered tile (ray missed or no active tool).
    int m_hoveredTileX{-1};
    int m_hoveredTileZ{-1};

    // Left mouse button held state — tracked for drag-to-zone/road/demolish.
    // Set true on MouseButtonDown button==0, false on MouseButtonUp button==0.
    bool m_lmbHeld{false};

    // --- Phase 10: Zone rectangular selection anchor ---
    // Set to the first tile clicked when Zone tool LMB is pressed.
    // Reset to {-1,-1} on LMB release (after filling the rectangle).
    // While held (-1 means no active rect drag), drag does NOT fill tiles — only
    // the hover highlight moves. On release, ALL tiles in the axis-aligned rectangle
    // [min(anchor,current), max(anchor,current)] are filled via doTerrainPlacement().
    // Road, Utilities, and Demolish tools retain their original tile-by-tile drag
    // behavior; only Zone uses this deferred rectangular fill pattern.
    int m_zoneAnchorX{-1};
    int m_zoneAnchorZ{-1};

    // --- Phase 9b: Zone sub-panel button handles (3×3 grid: col=zone R/C/I, row=density Low/Med/High) ---
    // Created during UIManager construction via m_backend->addButton().
    // Stored in row-major order: m_zoneSubPanelBtns[densityRow * 3 + zoneCol].
    // All 9 initialized to kInvalidUIElement; populated if m_backend is non-null.
    UIElementHandle m_zoneSubPanelBtns[9]{
        kInvalidUIElement, kInvalidUIElement, kInvalidUIElement,
        kInvalidUIElement, kInvalidUIElement, kInvalidUIElement,
        kInvalidUIElement, kInvalidUIElement, kInvalidUIElement
    };

    // --- Phase 9b: Utilities sub-panel button handles (2×2 grid) ---
    // Layout: [0]=PowerPlant, [1]=WaterTower, [2]=FireStation, [3]=PoliceStation.
    // Matches ServiceBuildingType enum ordinals.
    UIElementHandle m_utilSubPanelBtns[4]{
        kInvalidUIElement, kInvalidUIElement,
        kInvalidUIElement, kInvalidUIElement
    };

    // Unsaved-changes indicator state
    bool m_hasUnsavedChanges{false};

    // --- Phase 8: deficit-streak polling (GD-H3 bridge) ---

    // Edge-detect: last polled value of getConsecutiveDeficitMonths().
    // Initialized to 0 so the first poll at month 1 triggers the edge.
    int m_lastDeficitMonths{0};

    // Cooldown for CRISIS stinger (5 s minimum gap).
    // Initialized to -5.0 so the first fire always passes the cooldown check.
    double m_lastCrisisStingerFireTime{-5.0};

    // Cooldown for MILESTONE stinger (5 s minimum gap per StingerType).
    // Initialized to -5.0 so the first City Rating transition always fires.
    // Phase 10: triggerStinger(MILESTONE) fires on CityRatingTransition notifications only
    // (NOT on raw PopulationMilestone events). Edge-detected via notification queue polling.
    double m_lastMilestoneStingerFireTime{-5.0};

    // --- Phase 8: loading gate ---
    // While true, update() returns immediately (terrain generation in progress).
    bool m_loadingTerrain{false};

    // --- Phase 8: application quit flag ---
    // Set when MainMenu Quit or PauseMenu Quit to Desktop is consumed.
    // Polled by main.cpp via isQuitRequested() to break the frame loop.
    bool m_quitRequested{false};

    // --- Phase 8: speed selector element handle (created by HUD) ---
    UIElementHandle m_speedSelectorHandle{kInvalidUIElement};

    // --- Phase 8: modal-pause tracking ---
    // True if UIManager called setPaused(true) when opening a modal.
    // closeModal() only calls setPaused(false) if this flag is true.
    bool m_didPauseSim{false};

    // --- Phase 8: panel open-state tracking for input arbitration ---
    bool m_inspectorOpen{false};
    bool m_taxPanelOpen{false};

    // --- Phase 8: Ctrl-key state tracking for Ctrl+Z ---
    bool m_ctrlDown{false};

    // --- Phase 9b: demolish confirmation gate ---
    // Mirrors Settings > Gameplay "Confirm before demolish" (default ON).
    // Tests set this to false to suppress the modal and call demolishTile directly.
    bool m_demolishConfirmEnabled{true};

    // Background scrim element shown behind modal dialogs.
    // kInvalidUIElement (0) until Phase 6 creates the real element.
    UIElementHandle m_scrimHandle{kInvalidUIElement};

    // Helper: show/hide Zone and Utilities sub-panels based on m_activeTool.
    // Called whenever m_activeTool changes (toolbar click or hotkey).
    void updateSubPanelVisibility();

    // Helper: execute terrain placement at (hitX, hitZ) for the current active tool.
    // Called from both the MouseButtonDown handler and the MouseMove drag handler.
    // Performs earthworks cost computation, slope guard, sim dispatch, and overlay update.
    // Returns true if the event should be consumed (placement occurred or was blocked).
    bool doTerrainPlacement(int hitX, int hitZ);

    // --- Owned panels (allocated in UIManager constructor, deleted in destructor) ---
    // Construction/destruction order is INVARIANT:
    //   NotificationManager is constructed FIRST and destroyed LAST.
    //   All others are constructed in the order listed and destroyed in reverse.
    NotificationManager* m_notifications{nullptr};
    MainMenuPanel*       m_mainMenu{nullptr};
    HUD*                 m_hud{nullptr};
    TaxRatePanel*        m_taxPanel{nullptr};
    Minimap*             m_minimap{nullptr};
    InspectorPanel*      m_inspector{nullptr};
    PauseMenuPanel*      m_pauseMenu{nullptr};
    SettingsPanel*       m_settings{nullptr};
    ModalDialog*         m_modal{nullptr};
};
