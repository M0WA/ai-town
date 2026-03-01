# UIManager

- A `UIManager` class owns the `IGUIEnvironment` instance and all top-level `IGUIElement` panels
- No other class directly instantiates GUI elements at the top level

## IUIBackend Header Placement

`IUIBackend.h` is placed in `src/ui/` (not `src/interfaces/`) because it is part of the UI subsystem abstraction boundary — it defines the contract between `UIManager` and its rendering backend. All other shared cross-subsystem interfaces live in `src/interfaces/`. See `architecture/testing/testability-architecture.md` for the authoritative placement rationale and the `MockUIBackend` (test-facing) definition.

## IUIBackend Method Contract

The total method count is **17**. (**Conditional**: if the Phase 8 virtual `draw()` spike confirms `IGUIElement::draw()` is non-virtual, a `virtual void drawAlphaOverlays() {}` method is appended as method 18, updating the count to **18**; both this file and `testability-architecture.md` must be updated at that point — see `implementation/phase-8.md` §IrrlichtUIBackend setElementAlpha fallback for the spike procedure.) `testability-architecture.md` is the test-facing authority (`MockUIBackend`); `ui-manager.md` is the production-facing authority (`IrrlichtUIBackend`). Both files must remain consistent — any method added to one must be reflected in the other.

```cpp
class IUIBackend {
public:
    virtual ~IUIBackend() = default;

    // 1. Create a static text label. Returns an opaque handle for the element.
    virtual UIElementHandle addStaticText(const std::string& text, int x, int y, int w, int h) = 0;

    // 2. Create a clickable button. Returns an opaque handle for the element.
    virtual UIElementHandle addButton(const std::string& label, int x, int y, int w, int h) = 0;

    // 3. Destroy the element associated with the handle and release its resources.
    virtual void removeElement(UIElementHandle handle) = 0;

    // 4. Replace the displayed text of an existing element.
    virtual void setElementText(UIElementHandle handle, const std::string& text) = 0;

    // 5. Show or hide an element. Hidden elements do not receive input events.
    virtual void setElementVisible(UIElementHandle handle, bool visible) = 0;

    // 6. Query whether an element is currently visible.
    virtual bool isElementVisible(UIElementHandle handle) const = 0;

    // 7. Enable or disable an element. Disabled elements remain visible but are
    //    grayed out and do not respond to interaction (distinct from setElementVisible).
    //    Required for modal speed-selector graying and undo-button disabling.
    virtual void setElementEnabled(UIElementHandle handle, bool enabled) = 0;

    // 8. Query whether an element is currently enabled (interactive).
    virtual bool isElementEnabled(UIElementHandle handle) const = 0;

    // 9. Set the opacity of an element. alpha is in [0.0, 1.0].
    virtual void setElementAlpha(UIElementHandle handle, float alpha) = 0;

    // 10. Assign a texture (identified by its own UIElementHandle) to an image element.
    //     The textureHandle must have been obtained via loadTexture() (method 17).
    virtual void setElementImage(UIElementHandle handle, UIElementHandle textureHandle) = 0;

    // 11. Return the current displayed text of an element. Used in test assertions.
    virtual std::string getElementText(UIElementHandle handle) const = 0;

    // 12. Return the bounding rectangle of an element in virtual coordinate space.
    //     Returns Rect{x, y, w, h} (defined in IUIBackend.h — no Irrlicht dependency).
    //     IrrlichtUIBackend::getElementRect() converts from irr::core::rect<irr::s32>
    //     internally before returning Rect, keeping Irrlicht headers out of src/ui/.
    //     Used in position/size assertions.
    virtual Rect getElementRect(UIElementHandle handle) const = 0;

    // 13. Return the physical screen width in pixels (driver resolution).
    virtual int getScreenWidth() const = 0;

    // 14. Return the physical screen height in pixels (driver resolution).
    virtual int getScreenHeight() const = 0;

    // 15. Return the virtual UI canvas width (always 1920 in V1).
    //     All layout coordinates are in virtual space — see resolution-ui-scaling.md.
    //     UIManager and all panel code must call this instead of hardcoding 1920.
    virtual int getVirtualWidth() const = 0;

    // 16. Return the virtual UI canvas height (always 1080 in V1).
    //     UIManager and all panel code must call this instead of hardcoding 1080.
    virtual int getVirtualHeight() const = 0;

    // 17. Load a texture from disk and return an opaque handle that can be passed as the
    //     second argument to setElementImage(). Returns kInvalidUIElement on failure (file
    //     not found, unsupported format, or driver error). The backend owns the loaded
    //     texture resource; call removeElement(handle) to release it when no longer needed.
    virtual UIElementHandle loadTexture(const std::string& path) = 0;
};
```

Cross-references:

- `architecture/testing/testability-architecture.md` — `MockUIBackend` (GMock implementation), `UIElementHandle` typedef, `Rect` struct, and the rationale for `src/ui/` placement.
- `architecture/ui-ux/resolution-ui-scaling.md` — virtual coordinate space definition and letterbox/pillarbox mapping.

## IUIBackend Virtual Dimension Accessors

All UI layout coordinates in AI Town are defined in **virtual 1920×1080 space** (see `architecture/ui-ux/resolution-ui-scaling.md`). `IUIBackend` exposes two methods that return the virtual canvas dimensions:

```cpp
// Returns the virtual UI canvas width (always 1920 in V1).
virtual int getVirtualWidth()  const = 0;
// Returns the virtual UI canvas height (always 1080 in V1).
virtual int getVirtualHeight() const = 0;
```

`UIManager` and all panel classes **must** call `m_backend->getVirtualWidth()` and `m_backend->getVirtualHeight()` wherever the canvas extents are needed (e.g. positioning panels relative to screen edges, clamping element coordinates). Hardcoding `1920` or `1080` as literals is prohibited — doing so would break layout at non-standard virtual resolutions in any future revision that changes the virtual coordinate space.

In production (`IrrlichtUIBackend`): returns 1920 and 1080 respectively, matching the V1 virtual coordinate space.
In tests (`MockUIBackend`): stubs return 1920 and 1080 by default, so all existing panel unit tests behave correctly without additional `ON_CALL` setup.

## Toolbar Carve-Out Constants

The toolbar carve-out region bounds are **compile-time constants**, NOT values derived at runtime from `IUIBackend::getBounds()` or any equivalent query. The toolbar occupies a fixed layout region in the 1920×1080 virtual coordinate space. All carve-out pixel offsets are declared as `constexpr int` in `src/ui/ui_constants.h`, for example:

```cpp
// src/ui/ui_constants.h
constexpr int kToolbarLeft   = 8;    // virtual x-coordinate (1920×1080 space)
constexpr int kToolbarRight  = 72;   // virtual x-coordinate (1920×1080 space)
constexpr int kToolbarTop    = 64;   // virtual y-coordinate (1920×1080 space)
constexpr int kToolbarBottom = 784;  // virtual y-coordinate covers tool group + undo + demand + active tool
```

All carve-out constants in `ui_constants.h` MUST be in 1920×1080 virtual space — NOT in physical pixel
values or in the 1280×720 minimum resolution space. The UIScaler converts physical pixels to 1920×1080
virtual space; constants must match the virtual space.

These constants are used directly by `UIManager` and panel code (e.g., event carve-out checks in `onEvent()`, input arbitration skip zones). Runtime `IUIBackend` calls must NOT be used to derive these values — the toolbar layout is fixed in the virtual coordinate space and must not depend on element query results. If the toolbar dimensions ever change, update only `ui_constants.h`; no runtime query path is needed.

## Class Structure

Required includes for `UIManager`:

- `#include "src/ui/ui_types.h"` — provides `GameMode` enum (`Sandbox`, `Scenario`) used by `transitionToGameplay(GameMode)` and checked by `transitionToGameOver()`
- `#include "src/interfaces/LoanTerms.h"` — provides the `LoanTerms` struct used in `showForcedLoanDialog(const LoanTerms& terms)`. Without this include, `UIManager.h` fails to compile wherever `LoanTerms` appears in method signatures.
- `#include "src/interfaces/IClock.h"` — full include required (not a forward declaration) because `UIManager::update()` calls `m_clock->nowSeconds()` and tests inject `ManualClock` via this pointer. A forward declaration is insufficient for call-site compilation; the full type definition must be visible in `UIManager.h`. Note: `IAudioSystem`, `ICitySimulation`, and `InputEvent` are forward-declared in `UIManager.h` (not `#included`) because `UIManager.h` only holds pointers/references to them and does not call their methods directly in the header. `UIManager.cpp` carries the full includes for `IAudioSystem`, `ICitySimulation`, and `InputEvent`.

```cpp
class UIManager {
public:
    // backend: IUIBackend implementation (UIManager does NOT own it)
    // audio: IAudioSystem (for UI sound effects; UIManager does NOT own it)
    // sim: ICitySimulation (for reading and controlling simulation state; UIManager does NOT own it)
    // clock: IClock (forwarded to NotificationManager for 5-second auto-dismiss timing, and to HUD
    //         for undo countdown and grace-period indicator; UIManager does NOT own it).
    //         Production passes WallClock; tests inject ManualClock for deterministic timing.
    // NOTE: UIManager passes m_sim to NotificationManager as ICitySimulation* — this is required
    // because NotificationManager calls m_sim->setPaused(true) on the first CRITICAL toast, and
    // setPaused() is defined on ICitySimulation* (via ISimulationPauser inheritance). m_sim is
    // passed directly with no cast needed (UIManager already holds it as ICitySimulation*).
    // NotificationManager does NOT call getConsecutiveDeficitMonths() — UIManager::update() is
    // the exclusive polling bridge for deficit-month-based toast dispatch (GD-H3).
    UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock);
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
    // NOTE (GD-H3 bridge): UIManager::update() is the EXCLUSIVE polling bridge for the
    // deficit-streak CRITICAL toast (GD-H3). Each frame, update() calls
    // m_sim->getConsecutiveDeficitMonths() and dispatches the CRITICAL toast to
    // NotificationManager when the threshold is crossed. NotificationManager does NOT
    // call getConsecutiveDeficitMonths() — its only simulation interaction is calling
    // m_sim->setPaused(true) (via ISimulationPauser inheritance) when a CRITICAL toast
    // arrives. All deficit-month polling and threshold evaluation lives here, in update().
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

    // Terrain-load gate (called by main game loop):
    // When loading = true, UIManager::update() returns immediately without polling
    // pollPendingNotification() or updating any panel state. When loading = false,
    // normal update() processing resumes.
    // This method is NOT part of any interface — it is a public concrete method on
    // UIManager and is called by the main loop which holds a concrete UIManager reference.
    // It does NOT affect the IUIBackend interface.
    // Do NOT add a Loading state to GameState — V1 uses this boolean flag instead
    // (per architecture/game-design/game-over-flow.md line 19).
    void setLoadingTerrain(bool loading);

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
    UIElementHandle         m_scrimHandle{kInvalidUIElement}; // full-screen 50% black overlay; created in constructor; shown/hidden when modal is active

    ICitySimulation*        m_sim{nullptr};   // non-owning
    IAudioSystem*           m_audio{nullptr}; // non-owning
    IClock*                 m_clock{nullptr}; // non-owning; forwarded to NotificationManager and HUD at construction

    bool                    m_loadingTerrain{false}; // set by setLoadingTerrain(); gates update() early-return
};
```

### setLoadingTerrain(bool) — Terrain-Load Gate

**`setLoadingTerrain(bool loading)`**: Called by the main game loop to suppress `UIManager::update()` and HUD rendering during terrain generation. When `loading = true`, `UIManager::update()` returns immediately without polling `pollPendingNotification()` or updating any panel state. When `loading = false`, normal `update()` processing resumes. This method is NOT part of any interface — it is a public concrete method on `UIManager` and is called by the main loop which has a concrete `UIManager` reference. It does NOT affect the `IUIBackend` interface. **Do NOT add a `Loading` state to `GameState`** — V1 uses this boolean flag instead (per `architecture/game-design/game-over-flow.md` line 19).

## MainMenuPanel show/hide Contract

`MainMenuPanel` exposes `show()` and `hide()` methods with the following defined semantics:

- `show()`: makes the main menu panel visible AND resets its internal state to defaults — the selected option returns to the top-level item (New Game highlighted), any transient UI state (hover highlights, sub-menu open state, in-progress text input) is cleared. This ensures the panel always presents a clean initial state regardless of how it was last left.
- `hide()`: makes the panel invisible. Internal state is NOT reset on hide — `show()` is responsible for the reset when the panel is made visible again.

`UIManager` calls `m_mainMenuPanel.show()` in two situations:

1. During initial startup (the very first frame, before any state transition) — the main menu panel is visible by default from construction, but `show()` is called explicitly to guarantee a clean reset.
2. On return from gameplay to the main menu (if a future "Return to Main Menu" path is added). Any code path that makes the main menu visible MUST call `show()`, not `setVisible(true)` directly, to guarantee state reset.

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

Note: `GameOver` is Scenario-mode only. An auxiliary `GameOverReason` enum (to be defined in Phase 6 when Scenario skeleton is implemented) will allow `UIManager` to distinguish `Bankruptcy` from `ScenarioTimeout` modal content.

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
7. `PauseMenuPanel` — hidden initially; shown by `transitionToPaused()`
8. `SettingsPanel` — hidden initially; shown when the player clicks Settings from either `PauseMenuPanel` or the main-menu Settings path

   **PauseMenuPanel/SettingsPanel wiring contract**: `PauseMenuPanel` accepts only `IUIBackend*` in its
   constructor. After `UIManager` constructs all panels, it calls `m_pauseMenu->setSettingsPanel(m_settings)`
   to wire the pointer. `PauseMenuPanel` exposes a `void setSettingsPanel(SettingsPanel* settings)` method.
   `SettingsPanel` is NOT passed at `PauseMenuPanel` construction time — the setter approach is used so both
   panels can be constructed in dependency order without forward-declaring the full `PauseMenuPanel` constructor
   signature. Phase 3 stub: no-op setter body.
9. `ModalDialog` — created last; topmost Z-order; hidden until explicitly shown

All panels are visible-by-default set to false except `NotificationManager` (always rendering its toast queue), `MainMenuPanel` (visible on startup until `transitionToGameplay()` hides it), and `Minimap` (visible during gameplay). `UIManager::transitionToGameplay()` sets the correct initial visibility for all panels.

## Event Dispatch Priority

`UIManager::onEvent()` dispatches in strict priority order (highest first). Return `true` at the first consuming panel; short-circuit the chain. **This order must match `input-arbitration.md` exactly** — see that file for the canonical description and rationale.

1. **Modal**: if any blocking modal is active, it consumes ALL keyboard and mouse events (Escape goes to the modal's Cancel/safe handler, not the pause menu). Camera movement events (MMB drag, RMB drag, scroll wheel) are **NOT consumed** — they pass through to Priority 6 (see `input-arbitration.md` Priority 1 for the camera pass-through rationale). Return `true` for all non-camera events while a modal is shown.
2. **CRITICAL toast dismiss**: if any CRITICAL toast is currently visible AND no blocking modal is active, click/Enter/Delete events are consumed by `NotificationManager` before any other handler. This prevents an Enter key meant to dismiss a CRITICAL toast from activating a toolbar button behind it. Skipped entirely when: (a) no CRITICAL toast is visible, OR (b) a blocking modal is active.

   **Compound guard (mandatory)** — the dispatch to `NotificationManager` MUST use both conditions as a compound guard. Checking only `criticalVisible` is insufficient per `input-arbitration.md` Priority 2 dual-guard section:

   ```cpp
   // Priority 2 dual-guard — BOTH conditions required
   bool criticalVisible = m_notifications->hasCriticalToastVisible();
   bool modalActive     = m_modal && m_modal->isActive();
   if (criticalVisible && !modalActive) {
       // dispatch to NotificationManager for CRITICAL toast dismiss
   }
   ```

   The compound guard is mandatory — checking only `criticalVisible` is insufficient per `input-arbitration.md` Priority 2 dual-guard section. Same-frame state transitions can leave `criticalVisible == true` while a modal is simultaneously active (e.g., the game-over modal shown on the same frame a deficit CRITICAL toast is visible). Without the `!modalActive` check, Priority 2 would consume the event before Priority 1's modal handler, bypassing the modal's input lock.
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

> **NOTE**: Construction order (see §Panel Construction Order) reflects dependency — panels are constructed
> so earlier panels can be referenced by later ones. Draw order (back-to-front) reflects Z-ordering and
> intentionally differs from construction order. `UIManager::draw()` MUST iterate panels in draw order,
> NOT construction order.

Within `UIManager::draw()`, panels are drawn in this order (back to front, matching Z-order):

1. MainMenuPanel (if visible — only during `GameState::MainMenu`)
2. Minimap — **all Minimap chrome elements (toggle row, label strip, legend panel) are drawn inside `Minimap::draw()` at this slot**. These chrome elements are internal to the `Minimap` class and are NOT promoted to separate UIManager draw slots. HUD at slot 3 intentionally renders above any overlap with Minimap chrome. This ordering is normative and MUST NOT be changed to "fix" perceived z-order issues: moving the Minimap draw call later in the sequence would break the `UIManagerDrawOrderTest` ordering contract.
3. HUD (resource bar, toolbar, speed selector)
4. TaxRatePanel (if visible)
5. InspectorPanel (if visible)
6. NotificationManager toast stack
7. PauseMenuPanel (if visible)
8. SettingsPanel (if visible — subordinate to PauseMenuPanel; also reachable from main menu via `UIManager::showSettings()` during `GameState::MainMenu`. The same `SettingsPanel` instance is accessible from `PauseMenuPanel` during `GameState::Paused`.)
9. Background scrim (if modal active — full-screen 50% black overlay; drawn above all HUD/panel elements so
   it visually covers them while the modal is shown). The scrim element (`m_scrimHandle`) is an
   event-consuming element: while a modal is active it consumes left-click and right-click events that fall
   outside the modal dialog bounds, preventing interaction with HUD/panel elements beneath it. Scroll-wheel,
   MMB drag, and RMB drag events are NOT consumed by the scrim — camera events always pass through (see
   Priority 1 in `input-arbitration.md`).
10. ModalDialog (always topmost)

**BudgetDetailPanel ownership**: `BudgetDetailPanel` is owned and drawn by the `HUD` class internally — it is a detail overlay triggered by hovering the treasury balance field in the resource bar. It is NOT a top-level `UIManager` panel and does NOT appear in UIManager's panel member list or draw order. UIManager's draw order has exactly 10 slots as specified above.

`UIManager::draw()` issues explicit per-panel draw calls in the back-to-front order listed above (items 1–10). This is the normative approach: each panel exposes a `draw()` method that is called directly by UIManager in the correct sequence. Each panel's `draw()` method updates element state (visibility, text, alpha) via `IUIBackend` setter methods but does NOT render pixels directly. After `UIManager::draw()` completes, `IrrlichtRenderer::drawScene()` calls `guiEnvironment->drawAll()` which renders all visible `IGUIElement` nodes to the framebuffer. Because `UIManager::draw()` has already set the correct visibility on every element (non-active panels hide theirs), `drawAll()` only paints the intended elements. The Z-order concern (scrim must cover panels; modal must be topmost) is handled by visibility management — panels control which elements are visible/hidden before `drawAll()` executes.

## Thread Safety

All `UIManager` methods are main-thread-only. `CitySimulation` callbacks (forced loan, game-over) are delivered via the main-thread event queue — they must not call `UIManager` methods directly from a background thread. Use a thread-safe event queue to marshal events from background threads to the main thread before `UIManager` dispatch.

## Frame Loop Integration

`UIManager::update(float realDeltaSeconds)` MUST be called once per frame on the main thread BEFORE `driver->beginScene()` (before `RenderSystem::beginFrame()`). It MUST NOT be called inside the `beginScene()`/`endScene()` block.

The canonical call site is after `CameraController::update(dt)` (step 3) and is itself step 3b in the 8-step canonical frame sequence. Calling `update()` after `beginScene()` would mutate timer state mid-frame, producing one-frame-stale notification dismiss behavior — for example, a toast whose auto-dismiss timer expires during `beginScene()` would remain visible for one extra frame because the dismiss callback fires after `draw()` has already submitted the toast geometry to the GPU.

The required frame sequence (abbreviated to the relevant steps) is shown from two perspectives.

**Main game loop view** — what `src/main.cpp` calls directly:

```text
1. Poll OS events (IrrlichtDevice::run())
2. Process simulation tick (CitySimulation::update(dt))
3. CameraController::update(dt)
3b. UIManager::update(realDeltaSeconds)    <-- MUST be here, before beginScene()
4. RenderSystem::beginFrame()              <-- driver->beginScene() is called inside here
5. renderer->drawScene()                   <-- internally calls sceneManager->drawAll() THEN uiManager->draw()
6. RenderSystem::endFrame()                <-- driver->endScene() is called inside here
7. IrrlichtDevice::yield() / sleep
```

`src/main.cpp` must not call `sceneManager->drawAll()` or `UIManager::draw()` directly. Both are internal to `IrrlichtRenderer::drawScene()`. Calling either from `main.cpp` would bypass the `IRenderer` abstraction and break the header dependency rules described in `architecture/graphics-architecture/irrlicht-device-lifecycle.md`.

**IrrlichtRenderer::drawScene() internal view** — what happens inside step 5 above:

```text
Inside IrrlichtRenderer::drawScene():
  a. sceneManager->drawAll()       // 3D scene pass
  b. uiManager->draw()             // per-panel Z-order state update (visibility, text, alpha)
  c. guiEnvironment->drawAll()     // render all visible IGUIElement nodes to framebuffer
```

Step (b) updates element state but does not render pixels. Step (c) paints all elements that are currently visible. Because (b) has already toggled visibility for the correct game state (e.g. main menu elements hidden during gameplay), (c) only renders what should be on screen. This sequence is the authoritative render loop defined in `architecture/graphics-architecture/irrlicht-device-lifecycle.md`. Any change to the ordering must be made there first.

`UIManager::draw()` (internal step b) is called after `sceneManager->drawAll()` and before `guiEnvironment->drawAll()` and `endScene()` — this is correct and distinct from `UIManager::update()` (step 3b of the main loop), which must precede `beginScene()`. The two methods serve different purposes: `update()` advances timer state and dispatches dismissal logic; `draw()` updates element state that must be current when `guiEnvironment->drawAll()` renders within the `beginScene()`/`endScene()` block.

## MainMenu State Input Routing

When `m_state == GameState::MainMenu`, `UIManager::onEvent()` routes input with SettingsPanel taking priority over MainMenuPanel. If `SettingsPanel::isVisible()` returns true, events are dispatched to `SettingsPanel::onEvent()` first — if consumed, the event does not reach MainMenuPanel. If SettingsPanel does not consume the event (or is not visible), the event falls through to `MainMenuPanel::onEvent()`. MainMenuPanel consumes all events while visible (returns `true` for everything) to prevent HUD and camera input during the main menu. This routing must be checked before Priority 5 in the dispatch chain.

This priority ordering is required because `UIManager::showSettings()` (called from the MainMenu polling path) shows the SettingsPanel overlay on top of MainMenuPanel. Without SettingsPanel priority, Escape and Cancel clicks in the settings overlay are consumed by MainMenuPanel (which swallows all input), preventing the player from closing settings and leaving stale settings elements visible on screen.

**Main menu hide/restore on settings open/close**: `UIManager::showSettings()` calls `m_mainMenu->hide()` before `m_settings->show()` when `m_state == GameState::MainMenu`. This prevents main menu elements from remaining visible behind the settings overlay. When the settings panel closes (Escape or Cancel), the MainMenu routing block detects that `m_settings->isVisible()` changed from true to false and calls `m_mainMenu->show()` to restore the main menu. This hide/restore cycle is transparent to both panels — neither MainMenuPanel nor SettingsPanel holds a reference to the other.

## Paused State Input Routing

When `m_state == GameState::Paused`, `UIManager::onEvent()` routes input to `PauseMenuPanel` (and `SettingsPanel` if opened from the pause menu) before the Priority 5 HUD controls. This ensures that mouse clicks, Escape, and Enter reach the pause menu panels instead of falling through to inactive HUD handlers.

The dispatch order within the Paused state:

1. **SettingsPanel (if visible)**: events go to `SettingsPanel::onEvent()` first. If settings closes (Escape or Cancel), `PauseMenuPanel::show()` is called to restore the pause menu overlay.
2. **PauseMenuPanel (if visible)**: events go to `PauseMenuPanel::onEvent()`. If PauseMenuPanel hides itself (Resume via Escape, Enter, or button click), `UIManager` checks whether SettingsPanel just opened (PauseMenu→Settings path). If settings is NOT visible, `transitionToGameplay_fromPaused()` is called to resume gameplay. If settings IS visible, the state remains Paused (the player navigated to settings, not back to gameplay).

This routing must be placed before the Priority 5 Escape handler. Without it, mouse clicks on pause menu buttons fall through to `return false` (Priority 6 — camera), and Escape in the Paused state bypasses `PauseMenuPanel::onEvent()` entirely (the Priority 5 Escape handler calls `transitionToGameplay_fromPaused()` directly without letting PauseMenuPanel update its internal state).

## MainMenu Polling Communication

`MainMenuPanel` uses a polling-based communication pattern instead of direct method calls on `UIManager`. When the player clicks "Start City", "Settings", or "Quit", `MainMenuPanel` sets an internal flag (`m_startGameRequested`, `m_settingsRequested`, or `m_quitRequested`) and returns `true` from `onEvent()`. `UIManager::update()` polls these flags each frame via `consumeStartGameRequest()`, `consumeSettingsRequest()`, and `consumeQuitRequest()` — these are consume-once methods that read and clear the flag atomically. This avoids a circular dependency (MainMenuPanel does not hold a UIManager pointer) and keeps the communication unidirectional: panels set flags, UIManager polls and acts.

## PauseMenu Polling Communication

`PauseMenuPanel` uses the same polling pattern as `MainMenuPanel`. When the player clicks "Quit to Desktop" or "Quit to Main Menu", `PauseMenuPanel` sets an internal flag (`m_quitDesktopRequested` or `m_quitToMenuRequested`), calls `hide()`, and returns `true` from `onEvent()`. `UIManager::update()` polls these flags each frame via `consumeQuitDesktopRequest()` and `consumeQuitToMenuRequest()`. "Quit to Desktop" sets `m_quitRequested` on UIManager, which `main.cpp` polls via `isQuitRequested()` to call `device->closeDevice()`. "Quit to Main Menu" calls `transitionToMainMenu()` which hides gameplay panels and shows the main menu.

## Application Quit Flow

Application quit is requested from two entry points:

1. **Main Menu → Quit button**: `MainMenuPanel` sets `m_quitRequested`, polled by `UIManager::update()` via `consumeQuitRequest()`, which sets `UIManager::m_quitRequested`.
2. **Pause Menu → Quit to Desktop button**: `PauseMenuPanel` sets `m_quitDesktopRequested`, polled by `UIManager::update()` via `consumeQuitDesktopRequest()`, which sets `UIManager::m_quitRequested`.

In both cases, `main.cpp` checks `uiManager.isQuitRequested()` after `uiManager.update()` in the frame loop. When true, it calls `device->closeDevice()` and skips rendering for that frame. The device loop exits naturally on the next iteration when `device->run()` returns false.

## Audio Transition at Gameplay Start

`UIManager::transitionToGameplay()` calls `m_audio->setTimeOfDay(TimeOfDay::DAY)` before `m_audio->transitionToGameplay()`. This establishes the correct ambient bed selection (new games start at DAY). `AudioSystem::transitionToGameplay()` then starts both the ambient bed on stream slot 2 and the default calm music stem (MUSIC_CALM_01) on stream slot 0 via `setMusicTrack()`.

## CitySimulation Audio Wiring

`CitySimulation` receives `&audioSystem` (not `nullptr`) at construction in `main.cpp`. This enables all simulation-driven SFX: build/place sounds, demolish sounds, earthworks sounds, budget warning sounds, loan-issued sounds, and zone upgrade sounds. Each call site is guarded with `if (m_audio)` null-checks.
