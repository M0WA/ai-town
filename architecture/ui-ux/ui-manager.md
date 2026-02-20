# UIManager

- A `UIManager` class owns the `IGUIEnvironment` instance and all top-level `IGUIElement` panels
- No other class directly instantiates GUI elements at the top level

## Class Structure

Required includes for `UIManager`:

- `#include "src/ui/ui_types.h"` — provides `GameMode` enum (`Sandbox`, `Scenario`) used by `transitionToGameplay(GameMode)` and checked by `transitionToGameOver()`
- `#include "src/interfaces/LoanTerms.h"` — provides the `LoanTerms` struct used in `showForcedLoanDialog(const LoanTerms& terms)`. Without this include, `UIManager.h` fails to compile wherever `LoanTerms` appears in method signatures.

```cpp
class UIManager {
public:
    // backend: IUIBackend implementation (UIManager does NOT own it)
    // audio: IAudioSystem (for UI sound effects; UIManager does NOT own it)
    // sim: ICitySimulation (for reading and controlling simulation state; UIManager does NOT own it)
    // NOTE: UIManager passes m_sim to NotificationManager as ISimulationPauser* — this is valid
    // because ICitySimulation extends ISimulationPauser (see architecture/testing/testability-architecture.md).
    // No additional constructor parameter is required.
    UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim);
    ~UIManager();

    // Called from main event receiver BEFORE game logic processes events.
    // Returns true if event was consumed (do not pass to game logic).
    // InputEvent is from src/platform/input_event.h.
    // NOTE: The concrete platform layer (in src/platform/) translates irr::SEvent to InputEvent
    // before forwarding to UIManager::onEvent(). This keeps Irrlicht headers out of src/ui/
    // translation units, consistent with the IUIBackend/UIElementHandle abstraction pattern.
    bool onEvent(const InputEvent& event);

    // Per-frame update — call BEFORE sceneManager->drawAll().
    // realDeltaSeconds: wall-clock delta (not simulation delta).
    void update(float realDeltaSeconds);

    // Render all GUI panels — call AFTER sceneManager->drawAll() and BEFORE endScene().
    void draw();

    // State transitions (called by game loop):
    void transitionToGameplay(GameMode mode);  // Sets m_gameMode = mode and transitions state from MainMenu to Gameplay; hides main menu; shows HUD + minimap
    void transitionToPaused();     // shows pause menu overlay
    void transitionToGameplay_fromPaused(); // hides pause menu overlay
    void transitionToGameOver();   // shows non-dismissible game-over modal; No-op in Sandbox mode (m_gameMode == GameMode::Sandbox). Only valid in Scenario mode (m_gameMode == GameMode::Scenario).

    // Settings access (callable from MainMenuPanel during GameState::MainMenu):
    void showSettings();  // callable from MainMenuPanel during GameState::MainMenu;
                          // shows SettingsPanel without transitioning simulation state.
                          // MainMenuPanel calls this via UIManager (it does NOT hold a
                          // direct SettingsPanel* reference — UIManager owns all panels).

    // Modal access (used by CitySimulation event callbacks):
    void showForcedLoanDialog(const LoanTerms& terms);
    void showGameOverModal(int64_t totalDebt, int monthsInDeficit);
    void closeModal();  // safe to call even if no modal active

private:
    IUIBackend*             m_backend{nullptr}; // non-owning
    GameState               m_state{GameState::MainMenu};
    GameMode                m_gameMode{GameMode::Sandbox};  // set by transitionToGameplay(GameMode)

    // Owned panels (created in constructor; destroyed in destructor):
    // INVARIANT: NotificationManager MUST be constructed first. Every subsequent panel
    // may enqueue a notification during its own construction; NotificationManager must
    // already be live when that happens. No panel may be constructed before m_notifications
    // is fully initialised.
    NotificationManager*    m_notifications{nullptr};  // created first — see invariant above
    MainMenuPanel*          m_mainMenu{nullptr};        // created second; hidden on transitionToGameplay()
    HUD*                    m_hud{nullptr};
    TaxRatePanel*           m_taxPanel{nullptr};
    Minimap*                m_minimap{nullptr};
    InspectorPanel*         m_inspector{nullptr};
    PauseMenuPanel*         m_pauseMenu{nullptr};
    SettingsPanel*          m_settings{nullptr};
    ModalDialog*            m_modal{nullptr};          // created last (highest Z-order)

    ICitySimulation*        m_sim{nullptr};   // non-owning
    IAudioSystem*           m_audio{nullptr}; // non-owning
};
```

## GameState Enum

```cpp
enum class GameState {
    MainMenu,   // main menu + new game flow visible; HUD/minimap hidden
    Gameplay,   // HUD + minimap visible; simulation running
    Paused,     // pause menu overlay active; simulation paused; HUD still behind scrim
    GameOver,   // game-over modal active; all other interaction locked
    // NOTE: PostWinFreePlaying is a post-V1 state for Scenario mode (free-play after scenario
    // victory condition is met). It is NOT present in the V1 enum above.
    // The V1 canonical enum is exactly { MainMenu, Gameplay, Paused, GameOver }.
};
```

Note: `GameOver` is Scenario-mode only. An auxiliary `GameOverReason` enum (to be defined in Phase 5 when Scenario skeleton is implemented) will allow `UIManager` to distinguish `Bankruptcy` from `ScenarioTimeout` modal content.

## Panel Construction Order

Panels are constructed in this order (dependency order — later panels may read earlier panel state):

> **"Always-first" invariant**: `NotificationManager` MUST be fully constructed before any other
> panel is created. Every panel constructor is permitted to enqueue a notification; if
> `NotificationManager` does not yet exist when that call occurs the programme has undefined
> behaviour. This invariant applies to `MainMenuPanel` and every future panel addition.

1. `NotificationManager` — always first; see invariant above
2. `MainMenuPanel` — constructed immediately after `NotificationManager`; visible on startup; UIManager owns its lifetime; hidden by `transitionToGameplay()`
3. `HUD` — resource bar, speed selector, toolbar
4. `TaxRatePanel` — hidden initially
5. `Minimap` — always visible during gameplay
6. `InspectorPanel` — hidden initially; shown on query tool activation
7. `PauseMenuPanel` — hidden initially; shown by `transitionToPaused()`; owns and drives `SettingsPanel` visibility
8. `SettingsPanel` — hidden initially; shown by `PauseMenuPanel` when the player clicks Settings; subordinate to `PauseMenuPanel`
9. `ModalDialog` — created last; topmost Z-order; hidden until explicitly shown

All panels are visible-by-default set to false except `NotificationManager` (always rendering its toast queue), `MainMenuPanel` (visible on startup until `transitionToGameplay()` hides it), and `Minimap` (visible during gameplay). `UIManager::transitionToGameplay()` sets the correct initial visibility for all panels.

## Event Dispatch Priority

`UIManager::onEvent()` dispatches in strict priority order (highest first). Return `true` at the first consuming panel; short-circuit the chain. **This order must match `input-arbitration.md` exactly** — see that file for the canonical description and rationale.

1. **Modal**: if any blocking modal is active, it consumes ALL keyboard and mouse events (Escape goes to the modal's Cancel/safe handler, not the pause menu). Camera movement events (MMB drag, RMB drag, scroll wheel) are **NOT consumed** — they pass through to Priority 6 (see `input-arbitration.md` Priority 1 for the camera pass-through rationale). Return `true` for all non-camera events while a modal is shown.
2. **CRITICAL toast dismiss**: if any CRITICAL toast is currently visible AND no blocking modal is active, click/Enter/Delete events are consumed by `NotificationManager` before any other handler. This prevents an Enter key meant to dismiss a CRITICAL toast from activating a toolbar button behind it. Skipped entirely when: (a) no CRITICAL toast is visible, OR (b) a blocking modal is active.
3. **QueryPanel / InspectorPanel**: if visible, forwards mouse events over its bounds and Escape; see input-arbitration.md for toolbar and minimap carve-out exceptions on dismiss-click.
4. **TaxRatePanel**: if visible, forwards mouse events over its bounds; Escape closes it. Outside clicks are NOT consumed (pass-through).
5. **HUD controls** (toolbar clicks, speed selector, minimap interactions; Ctrl+Z (undo) processed here). **SettingsPanel** is NOT a named priority level — when visible it intercepts events as part of this priority tier (UIManager HUD controls), returning its consumed state before the toolbar/minimap handlers run. When `SettingsPanel` is visible, Escape is consumed by `SettingsPanel` at this priority tier — it closes Settings and returns the player to PauseMenu by calling `PauseMenuPanel::show()`. `SettingsPanel::onEvent()` must return `true` for Escape events when visible. This does NOT trigger `transitionToGameplay_fromPaused()`. Escape only reaches the PauseMenu-to-gameplay transition if SettingsPanel is not currently visible. When `SettingsPanel` is NOT visible and `PauseMenuPanel` IS visible: Escape is consumed by `PauseMenuPanel`, which calls `transitionToGameplay_fromPaused()`. This is processed at Priority 5, after SettingsPanel's check, before HUD toolbar handlers.
6. Return `false` — game logic (camera, tool placement) processes the event.

## State Transitions

| From | To | Trigger |
|---|---|---|
| `MainMenu` | `Gameplay` | New Game confirmed / Load Game confirmed; caller passes `GameMode` to `transitionToGameplay(GameMode)` |
| `Gameplay` | `Paused` | Escape key (not consumed by modal or settings) |
| `Paused` | `Gameplay` | Pause menu Resume button |
| `Gameplay` or `Paused` | `GameOver` | `CitySimulation` fires game-over event **(Scenario mode only — `transitionToGameOver()` checks `m_gameMode == GameMode::Scenario` before transitioning; no-op when `m_gameMode == GameMode::Sandbox`)** |

On `transitionToGameplay()`: show HUD, Minimap, NotificationManager; hide MainMenuPanel and all other panels.
On `transitionToPaused()`: show `PauseMenuPanel`; simulation is paused via `CitySimulation::setPaused(true)`.
On `transitionToGameplay_fromPaused()`: hide pause menu overlay; simulation resumes via `CitySimulation::setPaused(false)`.
On `transitionToGameOver()`: show game-over modal; simulation is paused (and stays paused — no resume path after game-over).

## Draw Order (per frame)

Within `UIManager::draw()`, panels are drawn in this order (back to front, matching Z-order):

1. MainMenuPanel (if visible — only during `GameState::MainMenu`)
2. Minimap
3. HUD (resource bar, toolbar, speed selector)
4. TaxRatePanel (if visible)
5. InspectorPanel (if visible)
6. NotificationManager toast stack
7. PauseMenuPanel (if visible)
8. SettingsPanel (if visible — subordinate to PauseMenuPanel; also reachable from main menu via `UIManager::showSettings()` during `GameState::MainMenu`. The same `SettingsPanel` instance is accessible from `PauseMenuPanel` during `GameState::Paused`.)
9. Background scrim (if modal active — full-screen 50% black overlay; drawn above all HUD/panel elements so it visually covers them while the modal is shown)
10. ModalDialog (always topmost)

`UIManager::draw()` issues explicit per-panel draw calls in the back-to-front order listed above (items 1–10). This is the normative approach: each panel exposes a `draw()` method that is called directly by UIManager in the correct sequence. `m_gui->drawAll()` is NOT used — it would bypass the explicit layering required for the background scrim, the modal overlay, and the precise Z-order defined here. Each panel's `IGUIElement` children are managed internally within that panel's own draw call.

## Thread Safety

All `UIManager` methods are main-thread-only. `CitySimulation` callbacks (forced loan, game-over) are delivered via the main-thread event queue — they must not call `UIManager` methods directly from a background thread. Use a thread-safe event queue to marshal events from background threads to the main thread before `UIManager` dispatch.
