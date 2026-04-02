#pragma once

#include "src/interfaces/IUIBackend.h"      // UIElementHandle, kInvalidUIElement, UIRect
#include "src/ui/ui_types.h"        // GameMode, GameState, ActiveTool
#include "src/interfaces/IClock.h"  // IClock — full include (available at Phase 0)
#include "src/interfaces/simulation_types.h"  // CityRatingTier — used as m_previousCityRating type
#include "src/interfaces/LoanTerms.h"  // LoanTerms
#include "key_bindings.h"           // KeyBindings — loaded at startup from keybindings.json
#include <array>
#include <unordered_map>
#include <cstdint>

// Forward declarations — do NOT #include these headers in UIManager.h.
// IAudioSystem, ICitySimulation, IRenderer, ITerrainQuery are declared as pointers only.
// InputEvent is used as a const-reference parameter — forward declaration is valid in C++.
class IAudioSystem;
class ICitySimulation;
class IRenderer;
class ITerrainQuery;
class ISaveSystem;
namespace irr { class ILogger; }

// INCLUDE PROHIBITION: Do NOT replace this forward declaration with
// #include "src/platform/input_event.h". The platform header must not
// be pulled into every translation unit that includes UIManager.h —
// doing so creates circular-include ambiguity at Phase 3 when UIManager
// is included by simulation and audio components that must not depend on
// the platform layer. The full include belongs in UIManager.cpp ONLY.
struct InputEvent;

// MainMenuPanel.h is included here (not forward-declared) because NewGameParams
// depends on the MapSize enum defined in that header.
#include "src/ui/MainMenuPanel.h"

// Panel forward declarations — full includes are in UIManager.cpp ONLY.
// Headers that #include UIManager.h must not gain transitive dependencies
// on panel implementation headers.
class NotificationManager;
class HUD;
class FinancesPanel;
class Minimap;
class InspectorPanel;
class PauseMenuPanel;
class SettingsPanel;
class ModalDialog;

// NewGameParams — parameters for starting a new game session.
// V1 hardcodes Sandbox mode; gameMode field is intentionally absent.
struct NewGameParams {
    MapSize mapSize{};
    int seed{0};
    int difficulty{0};
};

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
    // m_world.overlayMap is cleared and setZoneOverlay({}) is issued before updating dimensions
    // so stale overlay keys from the old map width cannot corrupt the new map.
    void setMapDimensions(int mapTilesX, int mapTilesZ);

    // getActiveTool — returns the current active tool state (for test observability).
    ActiveTool getActiveTool() const;

    // --- Test seam API (methods called only from unit tests) ---

    // setDemolishConfirm — enable/disable the demolish confirmation modal.
    // When false, demolishTile() is called immediately without showing a modal.
    // Used by tests to suppress the modal; production default is true (confirm ON).
    void setDemolishConfirm(bool enabled) { m_demolishConfirmEnabled = enabled; }

    // setOverlayMapForTest — test-seam to pre-populate m_world.overlayMap without
    // routing 100K UI events through onEvent().
    // Used exclusively by WorldInteraction_OverlayCap_100K_StillCalls to
    // inject exactly kOverlayCap entries so the cap-enforcement path can be
    // exercised with a single subsequent placement. Production code never calls
    // this method.
    // (ref: architecture/testing/testability-architecture.md — test seam pattern)
    [[deprecated("for tests only")]]
    void setOverlayMapForTest(const std::unordered_map<int64_t, uint32_t>& map) {
        m_world.overlayMap = map;
    }

    // onNewGame — reset all world-interaction state for a new game load.
    // Clears m_world.overlayMap, calls setZoneOverlay({}) if renderer is non-null,
    // resets m_world.activeTool to None, and clears m_world.hoveredTileX/Z.
    // Called from UIManager::transitionToGameplay() or test harness.
    void onNewGame();

    // Returns true when camera input (RMB drag, MMB drag) should be active.
    // Used by EventReceiver to suppress camera rotation in non-gameplay states.
    bool isGameplayOrPaused() const;

    // Returns true when a blocking modal dialog is currently active.
    // Used by the Priority-2 dual-guard and by tests.
    bool hasActiveModal() const;

    // Set the loading-terrain gate. While true, update() is a no-op
    // (terrain generation is in progress; no UI polling should occur).
    void setLoadingTerrain(bool loading);

    // Phase 11: Called once by the loading controller after terrain build and
    // deserialization completes, before the first UIManager::update() tick.
    // Seeds m_previousCityRating from the current sim state so that the per-frame
    // stinger_milestone detector does not fire a spurious MILESTONE on the first frame.
    // Also seeds m_lastDeficitMonths to prevent spurious game-over warnings on load.
    void onGameLoaded();

    // Phase 11: Load keybindings from the platform-specific config path:
    //   Linux   — ~/.config/aitown/keybindings.json
    //   Windows — %APPDATA%\aitown\keybindings.json
    // Silently uses defaults if the file is absent (normal first-run state).
    void loadKeybindings();

    // Phase 11c: Apply keybindings — update m_keyBindings and persist to disk.
    // Called from SettingsPanel via the m_keybindingsApplyFn callback.
    void applyKeybindings(const KeyBindings& b);

    // Phase 11: Wire the SaveSystem to UIManager for manual save and quit-guard.
    // Accepts ISaveSystem* so tests can pass MockSaveSystem without a real SaveSystem.
    // Called from main.cpp after SaveSystem is constructed.
    void setSaveSystem(ISaveSystem* saveSystem);

    // Phase 11l: Supply the Irrlicht logger so KeyBindings::load() can route
    // warnings through irr::ILogger* instead of falling back to stderr.
    // Called from main.cpp after device creation.  nullptr is safe (no-op logging).
    void setLogger(irr::ILogger* logger);

    // Phase 11: Update Load Game button enabled state in MainMenuPanel.
    // When available=false (default): button grayed, tooltip "No saves found."
    // When available=true: button enabled.
    // Called from main.cpp after SaveSystem::hasSaveData() is checked.
    void setSaveAvailable(bool available);

    // Phase 11: Set the save-state status text beneath the Load Game button.
    // "" hides the label. "No saves found." for first run; corrupted message for bad saves.
    void setSaveStatusText(const std::string& text);

    // Phase 11l: Return the map tile dimension selected in MainMenuPanel New Game screen.
    // Returns the integer value of MapSize (128, 512, or 1024).
    // Used by main.cpp to pass to TerrainSystem::generate() when starting a new game.
    int getPendingMapTiles() const;

    // Returns true when the user requested application quit
    // (from Main Menu Quit or Pause Menu Quit to Desktop).
    // Polled by main.cpp to break the frame loop.
    bool isQuitRequested() const;

    // Transition from Gameplay/Paused back to MainMenu state.
    // Called when "Quit to Main Menu" is selected from PauseMenuPanel.
    void transitionToMainMenu();

    // Phase 11m: New-game request polling and handling.
    // consumeNewGameRequest — returns true and atomically resets the pending flag
    //   when a new-game request has been posted (subsequent-game path only).
    bool consumeNewGameRequest();

    // getNewGameParams — returns the stored new-game parameters (map size, seed, difficulty).
    NewGameParams getNewGameParams();

    // consumeLoadGameRequest — returns true and provides the saved JSON when a load-game
    //   request has been deferred for main.cpp to handle (subsequent-game path).
    //   Atomically clears the pending flag and moves the JSON string to jsonOut.
    //   Returns false if no load is pending.
    bool consumeLoadGameRequest(std::string& jsonOut);

    // rebuildCityFromSim — recreate all renderer scene nodes from current sim state.
    // Called from main.cpp after applyLoadedJson() and terrain regeneration complete.
    // Public so main.cpp can invoke it; formerly private rebuildRendererFromSim().
    void rebuildCityFromSim();

#ifdef AITOWN_TESTING_ENABLED
    // handleNewGameRequest — test seam: stores params, then either sets the pending flag
    //   (subsequent-game path) or calls transitionToGameplay() directly (first-game path).
    void handleNewGameRequest(const NewGameParams& params);

    // setGameSessionActiveForTest — directly set m_session.active for tests.
    void setGameSessionActiveForTest(bool value);
#endif  // AITOWN_TESTING_ENABLED

private:
    // D-4 / UI-3: WorldInteractionState — groups all active-tool and map-interaction members.
    // Extracted from the flat private section to make the tool-related cohesion explicit.
    struct WorldInteractionState {
        // Active tool selection (Phase 9b)
        ActiveTool activeTool{ActiveTool::None};

        // Zone sub-panel selection (Phase 9b)
        // ZoneType / DensityTier are ints here to avoid pulling simulation headers into UIManager.h.
        int selectedZoneType{0};       // 0=Residential, 1=Commercial, 2=Industrial
        int selectedDensityTier{0};    // 0=Low, 1=Medium, 2=High
        int selectedServiceBuilding{0}; // matches ServiceBuildingType enum ordinal

        // Map dimensions (set via setMapDimensions() from main.cpp)
        int mapTilesX{0};
        int mapTilesZ{0};

        // Sparse zone overlay map
        // Key: static_cast<int64_t>(tileZ) * mapTilesX + tileX (int64_t); Value: ARGB colour (0xAARRGGBB).
        std::unordered_map<int64_t, uint32_t> overlayMap;

        // Last hovered tile coordinates ({-1,-1} = no valid tile)
        int hoveredTileX{-1};
        int hoveredTileZ{-1};

        // LMB held state (drag-to-zone / road / demolish)
        bool lmbHeld{false};

        // Zone rectangular selection anchor (Phase 10)
        // Set on first LMB press; reset on release after rectangle fill.
        int zoneAnchorX{-1};
        int zoneAnchorZ{-1};

        // Zone sub-panel button handles (3×3 grid, row-major: densityRow*3+zoneCol)
        std::array<UIElementHandle, 9> zoneSubPanelBtns{};

        // Utilities sub-panel button handles (2×2 grid)
        // Layout: [0]=PowerPlant, [1]=WaterTower, [2]=FireStation, [3]=PoliceStation.
        std::array<UIElementHandle, 4> utilSubPanelBtns{};

        // Overlay refresh counter (Phase 11m)
        int overlayRefreshCounter{0};

        // Demolish pending tile and modal gate (Phase 11h)
        int  demolishPendingTileX{-1};
        int  demolishPendingTileZ{-1};
        bool demolishModalPending{false};
    };
    WorldInteractionState m_world;

    // D-4 / UI-3: GameSessionState — groups all session-lifecycle and pending-request members.
    struct GameSessionState {
        // True once the first transitionToGameplay() has been called.
        // NOT reset by transitionToMainMenu() — stays true for subsequent-game path.
        bool active{false};

        // Pending new-game request (set when a new game arrives during an active session)
        bool        newGamePending{false};
        NewGameParams newGameParams{};

        // Pending load-game request
        bool        loadGamePending{false};
        std::string pendingLoadJson;

        // Pending quit action after unsaved-changes modal (type defined in ui_types.h)
        PendingQuitAction pendingQuit{PendingQuitAction::None};

        // Pending save-failure retry (manual save failure modal is open)
        bool pendingSaveFailure{false};

        // Application quit flag (polled by main.cpp via isQuitRequested())
        bool quitRequested{false};

        // Unsaved-changes indicator
        bool hasUnsavedChanges{false};
    };
    GameSessionState m_session;

    // --- Injected dependencies ---
    IUIBackend*    m_backend{nullptr};
    IAudioSystem*  m_audio{nullptr};
    ICitySimulation* m_sim{nullptr};
    IClock*        m_clock{nullptr};
    IRenderer*     m_renderer{nullptr};
    ITerrainQuery* m_terrain{nullptr};
    irr::ILogger*  m_logger{nullptr};
    ISaveSystem*   m_saveSystem{nullptr};

    // --- Game state machine ---
    GameState  m_state{GameState::MainMenu};
    GameMode   m_gameMode{GameMode::Sandbox};

    // --- Key bindings ---
    KeyBindings m_keyBindings{};

    // --- Pending quit action type (defined in ui_types.h as top-level PendingQuitAction) ---
    // D-13: PendingQuitAction moved from GameSessionState to ui_types.h.
    // Use PendingQuitAction::None / Desktop / ToMenu directly.

    // --- Stinger and city-rating tracking ---
    int            m_lastDeficitMonths{0};
    double         m_lastCrisisStingerFireTime{-5.0};
    double         m_lastMilestoneStingerFireTime{-5.0};
    CityRatingTier m_previousCityRating{CityRatingTier::Village};

    // --- UI element handles and panel state ---
    UIElementHandle m_speedSelectorHandle{kInvalidUIElement};
    UIElementHandle m_scrimHandle{kInvalidUIElement};
    bool m_loadingTerrain{false};
    bool m_didPauseSim{false};
    bool m_inspectorOpen{false};
    bool m_financesPanelOpen{false};
    bool m_ctrlDown{false};
    bool m_demolishConfirmEnabled{true};

    // D-23 / UI-23: update() sub-methods — thin dispatcher calls these in sequence.

    // Poll MainMenuPanel for new-game / load-game / settings / quit requests.
    // Returns true if a state transition occurred and update() should return early.
    bool pollMainMenuRequests();

    // Poll PauseMenuPanel for save, quit-to-desktop, and quit-to-menu requests.
    // Returns true if a state transition occurred and update() should return early.
    bool pollPauseMenuRequests();

    // Check if an unsaved-changes or save-failure modal has just closed and
    // dispatch the corresponding quit / retry / cancel action.
    // Returns true if a state transition occurred and update() should return early.
    bool updateModalDialogState();

    // HUD/overlay per-frame updates: deficit-streak polling, stinger milestone,
    // speed selector text, notification polling, and overlay refresh.
    void updateHUDState(float realDeltaSeconds);

    // Helper: compute the ARGB overlay color for a zoned tile (under-construction state).
    // Returns one of the 9 pre-defined ARGB values from the 3×3 (zone × density) table.
    static uint32_t computeZoneOverlayColor(ZoneType zone, DensityTier density);

    // Helper: update m_keyBindings and write keybindings.json.
    // Called from applyKeybindings().
    void saveKeybindings(const KeyBindings& b);

    // Helper: show/hide Zone and Utilities sub-panels based on m_world.activeTool.
    // Called whenever m_world.activeTool changes (toolbar click or hotkey).
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
    Minimap*             m_minimap{nullptr};
    InspectorPanel*      m_inspector{nullptr};
    PauseMenuPanel*      m_pauseMenu{nullptr};
    SettingsPanel*       m_settings{nullptr};
    ModalDialog*         m_modal{nullptr};
};
