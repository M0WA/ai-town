#pragma once

#include "src/ui/IUIBackend.h"      // UIElementHandle, kInvalidUIElement, Rect
#include "src/ui/ui_types.h"        // GameMode, GameState
#include "src/interfaces/IClock.h"  // IClock — full include (available at Phase 0)
#include "src/interfaces/LoanTerms.h"  // LoanTerms

// Forward declarations — do NOT #include these headers in UIManager.h.
// IAudioSystem and ICitySimulation are declared as pointers only.
// InputEvent is used as a const-reference parameter — forward declaration is valid in C++.
class IAudioSystem;
class ICitySimulation;

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

    // Returns true when a blocking modal dialog is currently active.
    // Used by the Priority-2 dual-guard and by tests.
    bool hasActiveModal() const;

private:
    IUIBackend*      m_backend{nullptr};
    IAudioSystem*    m_audio{nullptr};
    ICitySimulation* m_sim{nullptr};
    IClock*          m_clock{nullptr};

    // UI state machine
    GameState  m_state{GameState::MainMenu};
    GameMode   m_gameMode{GameMode::Sandbox};

    // Unsaved-changes indicator state
    bool m_hasUnsavedChanges{false};

    // GD-H3 bridge: last-known deficit streak; -1 means "not yet polled"
    int m_lastKnownDeficitStreak{-1};

    // Background scrim element shown behind modal dialogs.
    // kInvalidUIElement (0) until Phase 6 creates the real element.
    UIElementHandle m_scrimHandle{kInvalidUIElement};

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
