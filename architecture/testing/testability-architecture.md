# Testability Architecture

- Simulation logic must **not** depend directly on Irrlicht or OpenAL APIs
- `UIManager` must depend on an `IUIBackend` interface for all Irrlicht `IGUIEnvironment` calls, enabling `src/ui/` to be tested with a `MockUIBackend` in unit tests without a display. The interface uses **opaque `UIElementHandle` (uint32_t)** instead of raw Irrlicht pointers — this fully severs the compile-time dependency on Irrlicht headers in any translation unit that only includes `IUIBackend.h`. The concrete `IrrlichtUIBackend` maintains an internal `std::unordered_map<UIElementHandle, IGUIElement*>` to map handles to real objects. **Source location**: `IUIBackend.h` lives in `src/interfaces/` (moved from `src/ui/` in
  Phase 10b Feature 3 — all pure-virtual interfaces MUST reside in `src/interfaces/` per
  project convention); `IrrlichtUIBackend.h/.cpp` live in `src/rendering/` (since it depends
  on Irrlicht headers). `MockUIBackend` lives in `tests/ui/MockUIBackend.h` (renamed to
  `tests/ui/MockUIBackend.h` in Phase 10b Feature 3). `src/interfaces/` is not excluded from
  lcov, so coverage is captured correctly under the 80% gate.

```cpp
using UIElementHandle = uint32_t;
static constexpr UIElementHandle kInvalidUIElement = 0;

// UIRect struct used by IUIBackend::getElementRect — MUST be defined BEFORE IUIBackend
// to avoid a forward-declaration-as-return-type ambiguity in the virtual method signature.
// Placing the definition after the class compiles on some compilers but is non-conforming
// and breaks with strict C++ parsing rules for return types in virtual method declarations.
struct UIRect { int x{0}, y{0}, w{0}, h{0}; };

class IUIBackend {
public:
    virtual ~IUIBackend() = default;
    virtual UIElementHandle addStaticText(const std::string& text, int x, int y, int w, int h) = 0;
    virtual UIElementHandle addButton(const std::string& label, int x, int y, int w, int h) = 0;
    virtual void            removeElement(UIElementHandle handle) = 0;
    virtual void            setElementText(UIElementHandle handle, const std::string& text) = 0;
    virtual void            setElementVisible(UIElementHandle handle, bool visible) = 0;
    virtual bool            isElementVisible(UIElementHandle handle) const = 0;
    virtual void            setElementEnabled(UIElementHandle handle, bool enabled) = 0;  // grayed-out vs interactive
    virtual bool            isElementEnabled(UIElementHandle handle) const = 0;
    virtual void            setElementAlpha(UIElementHandle handle, float alpha) = 0;  // [0.0, 1.0]
    virtual void            setElementImage(UIElementHandle handle, UIElementHandle textureHandle) = 0;
    virtual std::string     getElementText(UIElementHandle handle) const = 0;   // for test assertions on displayed values
    virtual UIRect          getElementRect(UIElementHandle handle) const = 0;   // {x, y, w, h} in virtual space; for position/size assertions
    virtual int             getScreenWidth()  const = 0;
    virtual int             getScreenHeight() const = 0;
    // Returns the virtual UI canvas width (always 1920 in V1).
    // All UI layout coordinates are defined in virtual 1920×1080 space (see resolution-ui-scaling.md).
    // UIManager and all panel code must call getVirtualWidth()/getVirtualHeight() instead of
    // hardcoding 1920/1080, so that the virtual coordinate space is a single source of truth.
    virtual int             getVirtualWidth()  const = 0;
    // Returns the virtual UI canvas height (always 1080 in V1).
    virtual int             getVirtualHeight() const = 0;
    // Load a texture from disk and return an opaque handle for setElementImage().
    // Returns kInvalidUIElement on failure. Backend owns the resource; call removeElement() to release.
    virtual UIElementHandle loadTexture(const std::string& path) = 0;
    // 18. Set a filled background colour on an IGUIStaticText element.
    //     r, g, b, a are each in [0, 255]. Added in Phase 9b for the Minimap dark-panel fix.
    //     Only valid for addStaticText() handles. Has no effect on button elements.
    //     NAMING NOTE: earlier spec drafts used setElementBackgroundColor(handle, uint32_t argb)
    //     with a packed ARGB argument. The canonical form throughout the codebase uses four
    //     separate int channels (r, g, b, a). All call sites must use this 4-channel form.
    //     See architecture/ui-ux/minimap.md §IUIBackend method 18.
    virtual void setElementBackground(UIElementHandle handle, int r, int g, int b, int a) = 0;
    // 19. Apply the monospace font (hud_mono_font.xml) to an IGUIStaticText element.
    //     Panel code calls this immediately after addStaticText() for every numeric readout element.
    //     In IrrlichtUIBackend: calls IGUIStaticText::setOverrideFont(m_hudMonoFont); no-op when m_hudMonoFont is null.
    //     (Member renamed from m_monoFont to m_hudMonoFont in Phase 11g.)
    //     In MockUIBackend: MOCK_METHOD stub. Labels and button text MUST NOT call this method.
    //     Added in Phase 10. See architecture/ui-ux/hud-layout.md §Font Loading — Monospace requirement.
    virtual void setElementMonoFont(UIElementHandle handle) = 0;
    // 20. Reposition and resize an existing element in-place without destroying
    //     its handle. Coordinates are in virtual 1920×1080 space.
    //     In IrrlichtUIBackend: updates stored virtualRect + calls setRelativePosition().
    //     In MockUIBackend: MOCK_METHOD stub.
    //     Added in Phase 10 for the modal dialog centring fix.
    //     See architecture/ui-ux/modal-dialog-system.md §Element Repositioning.
    virtual void setElementRect(UIElementHandle handle, int x, int y, int w, int h) = 0;
    // 21. Override the text colour of a static text element.
    //     r, g, b are in [0, 255]; alpha is fixed at 255 (fully opaque).
    //     In IrrlichtUIBackend: calls IGUIStaticText::setOverrideColor(SColor(255, r, g, b)).
    //     In MockUIBackend: MOCK_METHOD stub.
    //     Added post-Phase-10.
    virtual void setElementTextColor(UIElementHandle handle, int r, int g, int b) = 0;
    // 22. Draw a filled rectangle directly into the frame without creating a persistent element.
    //     x, y, w, h are in virtual 1920×1080 coordinate space. r, g, b, a are each in [0, 255].
    //     In IrrlichtUIBackend: calls IVideoDriver::draw2DRectangle(SColor(a,r,g,b), recti).
    //     No UIElementHandle is created or returned — this is a transient per-frame draw call.
    //     Must be called inside a frame render pass (between beginScene/endScene).
    //     Used by Minimap::drawOverlay() for tile colour overlays without element leakage.
    //     In MockUIBackend: MOCK_METHOD stub. In StubUIBackend: no-op override.
    //     Added in Phase 11p. See architecture/ui-ux/ui-manager.md §IUIBackend Method Contract.
    virtual void fillColoredRect(int x, int y, int w, int h, int r, int g, int b, int a) = 0;
};
```

`MockUIBackend` **source location**: `tests/ui/MockUIBackend.h`. The file contains 22 `MOCK_METHOD` entries — one per `IUIBackend` virtual method — as of Phase 11p (method 19 `setElementMonoFont` added in Phase 10; method 20 `setElementRect` added in Phase 10; method 21 `setElementTextColor` added post-Phase-10; method 22 `fillColoredRect` added in Phase 11p). **Rule**: whenever a new virtual method is added to `IUIBackend`, a matching `MOCK_METHOD` entry MUST be added to `tests/ui/MockUIBackend.h` in the same commit. The spec description above (inline `IUIBackend` class block) and `tests/ui/MockUIBackend.h` must always have the same method count. `ui-manager.md` §IUIBackend Method Contract is the production-facing authority; this file is the test-facing authority; both must remain consistent.

`MockUIBackend` returns arbitrary non-zero integer handles (e.g., an incrementing counter) with no real objects — unit tests that call UIManager methods never dereference Irrlicht pointers, making `src/ui/` genuinely headless-testable and the 95% coverage gate achievable.

- **`UIScaler` testability**: `UIScaler` must accept viewport dimensions at construction (`UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY)`) rather than reading from a live `IVideoDriver`. Tests construct `UIScaler(1920, 1080, 1280, 720, 0, 90)` directly to validate coordinate projection and letterbox offset math without a display. The `unproject` method returns `UIScaler::VirtualPoint` — a nested struct, NOT at namespace scope, to avoid ODR violations. The five named unit tests that must be authored in `tests/ui/ui_scaler_test.cpp` are:
  1. `UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly`: construct with (1920, 1080, 1280, 720, 0, 90); unproject (640, 450) → virtual (960, 540).
  2. `UIScaler_FullNative_NoOffset_ProjectsIdentity`: construct with (1920, 1080, 1920, 1080, 0, 0); unproject (960, 540) → virtual (960, 540).
  3. `UIScaler_PillarboxOffset_UnprojectsCenterCorrectly`: construct with (1920, 1080, 1440, 1080, 240, 0); unproject (960, 540) → virtual (960, 540).
  4. `UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero`: construct with (1920, 1080, 1280, 720, 0, 90); unproject (640, 80) → virtual y clamped to 0 (actual_y=80 < offsetY=90 produces negative pre-clamp virtual_y, clamped to 0).
  5. `UIScaler_GetViewportRect_ReturnsCorrectOffsets`: construct with (1920, 1080, 1280, 720, 0, 90); `getViewportRect()` returns {x:0, y:90, w:1280, h:720}.
- **`NotificationManager` testability**: **CRITICAL — Constructor parameter types are fixed.** The correct constructor signature (Phase 10 and later) is: `NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock, IAudioSystem* audio)`. The `ICitySimulation*` parameter type (NOT `ISimulationPauser*`) is mandatory: `NotificationManager` calls `m_sim->setPaused(true)` on CRITICAL toast auto-pause; `setPaused()` is inherited from `ISimulationPauser`. `UIManager` already holds `m_sim` as `ICitySimulation*`, so no downcast is needed. `NotificationManager` does NOT call `getConsecutiveDeficitMonths()`; `UIManager::update()` is the exclusive polling bridge for deficit-month-based toast dispatch. If a Phase 1/2 stub mistakenly used `ISimulationPauser*`, Phase 3 MUST correct it to `ICitySimulation*`. The `IAudioSystem*` parameter (fourth, added in Phase 10) allows `postCritical()` and `postNormal()` to fire `UI_TOAST` SFX when a toast becomes visible. Before Phase 10, pass `nullptr`; every audio call site is guarded by `if (m_audio)`. **Phase 10 test update**: all existing test fixtures that construct `NotificationManager` directly must be updated to pass a fourth `IAudioSystem*` argument — either `nullptr` (tests not exercising toast audio) or `NiceMock<MockAudioSystem>` (tests verifying SFX behaviour). `MockAudioSystem` is in `tests/simulation/MockAudioSystem.h`.

  **Interface inheritance contract** (required for type safety): `ICitySimulation` extends `ISimulationPauser`, `IEconomyQuery`, `IZoningActions`, and `ISimulationState` (defined in `src/interfaces/ICitySimulation.h`). This allows `NotificationManager` to accept `ICitySimulation*` and safely call inherited `setPaused(bool)` without an explicit cast. Tests that construct `NotificationManager` with a mock must use `NiceMock<MockCitySimulation>` (which implements both `ICitySimulation` and `ISimulationPauser` via inheritance), NOT `MockSimulationPauser` alone (which only implements `ISimulationPauser` and cannot be passed as `ICitySimulation*`).

  **Source locations**: `ISimulationPauser.h` lives in `src/interfaces/` (NOT `src/simulation/` — placing it in `src/simulation/` creates a latent circular dependency when `src/ui/` headers include it, violating the `src/ui/` → `src/simulation/` prohibition). `src/interfaces/` is a dependency-free common header directory that both `src/simulation/` and `src/ui/` may safely include. `MockCitySimulation` lives in `tests/ui/MockCitySimulation.h` (used by UI tests that need to verify pause/resume calls and simulation queries without pulling in the concrete `CitySimulation`).

  **Test setup**: Tests inject `MockUIBackend` + `NiceMock<MockCitySimulation>` + `ManualClock` + `nullptr` (for `IAudioSystem*` — Phase 10 adds the fourth parameter; tests not exercising toast audio pass `nullptr`) and call `update()` with controlled time advances to verify queue ordering, auto-dismiss timing, CRITICAL vs Normal band placement, and log-fallback behavior. Tests that verify `ui_toast` SFX behaviour must inject `NiceMock<MockAudioSystem>` (from `tests/simulation/MockAudioSystem.h`) as the fourth argument instead of `nullptr`. `NotificationManager` exposes a public `dismissCriticalToast(UIElementHandle handle)` method — this is the production API called by the UI event handler when the player clicks, presses Enter, or presses Delete on a CRITICAL toast; it is not a test-only backdoor. Tests call this method to simulate player dismissal. Additional required test cases:
  - `CriticalToast_OnPost_AutoPausesCalled`: posting a CRITICAL toast calls `setPaused(true)` exactly once when the CRITICAL queue transitions from empty to non-empty.
  - `CriticalToast_OnLastDismiss_NoAutoResume`: calling `dismissCriticalToast(handle)` on the last remaining CRITICAL toast does **NOT** call `setPaused(false)` — auto-resume requires explicit player unpause. Verify `setPaused(false)` is never called by `NotificationManager` on CRITICAL toast dismissal.
  - `CriticalToast_SecondPost_NoDoublePause`: posting a second CRITICAL toast while one is already active does NOT call `setPaused(true)` again.
  - `NotificationSystem_AutoPause_OnFirstCriticalToast` _(Phase 8 deliverable)_: construct `NotificationManager` with `NiceMock<MockCitySimulation>` + `NiceMock<MockUIBackend>` + `ManualClock` + `nullptr`; post one CRITICAL toast when the CRITICAL queue is empty; verify `setPaused(true)` is called exactly once. Primary named test for `tests/ui/notification_system_test.cpp` Phase 8 expansion.
  - `NotificationSystem_NoPause_OnNormalToast` _(Phase 8 deliverable)_: post a Normal-severity toast (not CRITICAL) to `NotificationManager`; verify `setPaused(true)` is never called — Normal toasts must not trigger auto-pause.
  - `NotificationSFX_ToastVisible_UIToastSoundFires` _(Phase 10 deliverable)_: construct `NotificationManager` with `NiceMock<MockUIBackend>` + `NiceMock<MockCitySimulation>` + `ManualClock` + `NiceMock<MockAudioSystem>`; post a Normal toast; verify `playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` is called exactly once when the toast becomes visible (not on enqueue when the queue is at capacity). Named test for `tests/ui/notification_system_test.cpp` Phase 10 expansion. See `hud-layout.md` Phase 10 Audio Wiring — `ui_toast` section for the full guard rule.

- **`UIManagerDeficitIntegrationTest` fixture** _(Phase 8 deliverable — canonical cross-subsystem fixture for deficit-streak CRITICAL toast dispatch)_ in `tests/ui/notification_system_test.cpp`. This is a **separate fixture from `NotificationManagerTest`** — it tests the full dispatch path from `pollPendingNotification()` through `UIManager::update()` to `NotificationManager` CRITICAL toast dispatch and `IAudioSystem::triggerStinger()`. **Fixture setup**: real `UIManager` constructed with `NiceMock<MockUIBackend>`, `NiceMock<MockCitySimulation>`, `NiceMock<MockAudioSystem>`, and `ManualClock`. `MockAudioSystem` is injected as `IAudioSystem*` so that `triggerStinger` calls are interceptable and verifiable. **Include path**: `MockAudioSystem` is in `tests/simulation/MockAudioSystem.h` (NOT `tests/ui/` — audio mocks live alongside simulation mocks). `TearDown()` resets `ui_` to `nullptr` before mock destruction (mandatory: MockUIBackend destruction while UIManager holds a pointer to it causes a use-after-free crash in strict-mock verification).

  **Approved mock policy deviation**: `NiceMock<MockAudioSystem>` (not `StrictMock`) is used here because `UIManager`'s `if(m_audio)` null-check guard requires a non-null injectable mock; `StrictMock` would require exhaustive construction-time `EXPECT_CALL` setup for all audio interactions. This leniency is compensated by explicit `Times(0)` assertions on negative-stinger tests (`UIManagerDeficit_Month1_NoStingerFired`).

  Eight required test cases:
  - `UIManagerDeficit_Month1_ToastDispatched_NoStinger` _(Phase 8 deliverable)_: set up `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` and `ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(1))`; place both EXPECT*CALLs **before** calling `ui*.update(dt)`— EXPECT_CALL must precede the action under test: (a) **positive assertion**`EXPECT*CALL(backend*, addStaticText(HasSubstr("2 months"), _, _, _, _)).Times(AtLeast(1))`— prevents test passing trivially if dispatch chain is never entered; (b) **negative assertion**`EXPECT*CALL(audio*, triggerStinger(_)).Times(0)`— stinger must NOT fire on month-1 (condition`== 2 AND m_lastDeficitMonths < 2`not met — currentMonths is 1, not 2); then call`ui_.update(dt)`. **ON_CALL stubs are required**: `pollPendingNotification()`is no longer the dispatch trigger for deficit toasts — toast dispatch was changed to direct polling of`getConsecutiveDeficitMonths()`; the `BudgetDeficitWarn`notification no longer drives the deficit-toast branch in`UIManager::update()`. The `pollPendingNotification()`stub returns`false`(no pending notification) to prevent surprise notification processing on unrelated notification types. The critical path is entered via`getConsecutiveDeficitMonths()`returning 1; without the`getConsecutiveDeficitMonths()` stub, NiceMock returns 0 and both assertions pass trivially for the wrong reason — a silent false-pass.

  - `UIManagerDeficit_Month2_ToastDispatched_StingerFires` _(Phase 8 deliverable)_: set up `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` and `ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2))`; place `EXPECT_CALL(audio_, triggerStinger(StingerType::CRISIS)).Times(1)` **before** calling `ui_.update(dt)` — EXPECT*CALL must precede the action under test (GMock anti-pattern: setting expectations after the call will cause them to be verified before the call fires, potentially failing or passing for wrong reasons); then call `ui*.update(dt)`. **Production branch condition is `== 2`AND`m_lastDeficitMonths < 2`**: implementers must use `currentMonths == 2 AND m_lastDeficitMonths < 2`in the UIManager branch predicate — using`>= 2`would cause re-fires at month 3+ violating the 'at most once per deficit streak' rule in dynamic-soundscape.md. **ON_CALL stubs are required**:`pollPendingNotification()`is no longer the dispatch trigger for deficit toasts — toast dispatch was changed to direct polling of`getConsecutiveDeficitMonths()`; the stub returns `false`(no pending notification) to prevent surprise notification processing on unrelated notification types. The critical path is entered via`getConsecutiveDeficitMonths()`returning 2; without the`getConsecutiveDeficitMonths()`stub, NiceMock returns 0, the`== 2`condition is never true, and the`Times(1)` expectation fails at teardown — another silent false-pass pattern that stubs prevent.

  - `UIManagerDeficit_RapidFireCooldown_SecondStingerDropped` _(Phase 8 deliverable)_: uses a 3-update sequence to isolate the cooldown as the sole suppressor. Place `EXPECT_CALL(audio_, triggerStinger(StingerType::CRISIS)).Times(1)` **before** all updates so GMock can verify the total call count across all three update cycles. Stub `getConsecutiveDeficitMonths()` using a **stateful lambda** — **`ON_CALL` does NOT support `.InSequence()` and will not compile; `InSequence` is an `EXPECT_CALL`-only modifier and MUST NOT be used with `ON_CALL`**. The correct pattern:

    ```cpp
    int seq_idx = 0;
    std::vector<int> seq_values = {2, 2, 0, 0, 2, 2}; // 2 calls/tick × 3 ticks (illustrative)
    ON_CALL(sim_, getConsecutiveDeficitMonths())
        .WillByDefault([&seq_values, &seq_idx]() {
            return seq_values[seq_idx < (int)seq_values.size()
                              ? seq_idx++ : (int)seq_values.size()-1];
        });
    ```

    The vector size must cover all `getConsecutiveDeficitMonths()` calls per tick × number of ticks; the illustrative `{2, 2, 0, 0, 2, 2}` covers 2 calls/tick × 3 ticks — adjust to match the actual call count in the `update()` implementation. The test flow: (1) first `ui_.update(dt)` — lambda returns 2 → edge-detect fires (2==2 AND m*lastDeficitMonths==0<2); stinger triggers; m_lastDeficitMonths set to 2; (2) advance `ManualClock` by `1.0` second; (3) second `ui*.update(dt)`— lambda returns 0 → m_lastDeficitMonths resets to 0; no stinger (count ≠ 2); (4) advance`ManualClock`by`2.0`more seconds (total 3 seconds elapsed since first stinger, less than the 5-second cooldown); (5) third`ui\_.update(dt)`— lambda returns 2 → edge-detect passes (2==2 AND m_lastDeficitMonths==0<2) BUT cooldown 3 s < 5 s → stinger is suppressed by the cooldown; verify`Times(1)`at teardown — the cooldown is the sole suppressor in the third update. This test requires`ManualClock`advancement and verifies that the`IAudioSystem::triggerStinger()`5-second minimum-between-same-type-triggers rule (per`architecture/audio-architecture/dynamic-soundscape.md`) is enforced at the `UIManager::update()`call site, not just inside`AudioSystem`. **Why the third stinger is dropped**: suppressed by the stinger cooldown (the edge-detect passes on the 0→2 re-entry, but the 5s cooldown is not yet expired).

  - `UIManagerDeficit_PerStreakSingleFire_NoReFireAfterCooldown_SameStreak` _(Phase 8 deliverable)_: stub `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` and `ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2))`; place `EXPECT_CALL(audio_, triggerStinger(StingerType::CRISIS)).Times(1)` before the first update call; call `ui_.update(dt)` (stinger fires); advance `ManualClock` by 6.0 seconds (greater than the 5-second cooldown); call `ui_.update(dt)` a second time; verify the stinger does NOT fire again despite the cooldown having expired — distinguishes per-streak single-fire behavior from cooldown-only suppression. **ON_CALL stub note**: `pollPendingNotification()` is no longer the dispatch trigger for deficit toasts (dispatch was changed to direct polling of `getConsecutiveDeficitMonths()`); the stub returns `false` (no pending notification) to prevent surprise notification processing on unrelated notification types.

  - `UIManagerDeficit_CounterZero_NoToastNoStinger` _(Phase 8 deliverable)_: stub `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` (no notification) and `ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0))`; place `EXPECT_CALL(backend_, addStaticText(_,_,_,_,_)).Times(0)` and `EXPECT_CALL(audio_, triggerStinger(_)).Times(0)` BEFORE `ui_.update(dt)`; call `ui_.update(dt)`; verify the test passes — baseline quiescent state with no events and zero streak produces no toast and no stinger.

  - `UIManagerDeficit_StreakBreak_RecoveryToastDispatched` _(Phase 8 deliverable)_: set `m_lastDeficitMonths` to 1 by first calling `ui_.update(dt)` with `getConsecutiveDeficitMonths()` returning 1 (advancing internal state), then stub `ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0))` (streak drops from 1 to 0); place `EXPECT_CALL(backend_, addStaticText(HasSubstr("Recovering"), _, _, _, _)).Times(AtLeast(1))` BEFORE the second `ui_.update(dt)` call — verifies that when `getConsecutiveDeficitMonths()` drops from 1 to 0 (`m_lastDeficitMonths > 0 AND currentMonths == 0`), `UIManager::update()` dispatches a "Finances Recovering" Normal-queue toast to `NotificationManager`. The `HasSubstr("Recovering")` matcher must match the exact toast message string defined in `UIManager`; implementers MUST use a message containing "Recovering" in the recovery toast dispatch branch. Stub `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` on both update calls to prevent unrelated notification processing.

  - `UIManagerDeficit_Month1StreakBreak_ReenablesFutureStreak` _(Phase 8 deliverable)_: verify that after a streak break (consecutive deficit months drops to 0), a subsequent month-1 deficit re-fires the CRITICAL toast (streak tracking resets). Three-phase sequence: (1) stub `getConsecutiveDeficitMonths()` returning 1, call `ui_.update(dt)` (month-1 CRITICAL toast fires, `m_lastDeficitMonths` set to 1); (2) stub `getConsecutiveDeficitMonths()` returning 0, call `ui_.update(dt)` (streak break, recovery toast, `m_lastDeficitMonths` reset to 0); (3) stub `getConsecutiveDeficitMonths()` returning 1, place `EXPECT_CALL(backend_, addStaticText(HasSubstr("2 months"), _, _, _, _)).Times(AtLeast(1))` BEFORE the third `ui_.update(dt)` call — verifies that `UIManager` treats the re-entry into month-1 deficit as a fresh streak start and posts the month-1 CRITICAL toast again. Without this reset, a buggy implementation that never clears `m_lastDeficitMonths` on streak break would silently suppress future month-1 toasts. Stub `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` on all three update calls.

  - `UIManagerDeficit_Month3_SandboxMode_NoGameOverModal` _(Phase 8 deliverable)_: stub `ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(3))` and stub `ON_CALL(sim_, getGameMode()).WillByDefault(Return(GameMode::Sandbox))`; place `EXPECT_CALL(backend_, showGameOverModal()).Times(0)` (or equivalent modal-trigger assertion on `MockUIBackend`) BEFORE `ui_.update(dt)`; call `ui_.update(dt)`; verify the test passes — when `GameMode::Sandbox`, a month-3 deficit streak does NOT call `transitionToGameOver()` (or its equivalent UI trigger), confirming the Sandbox guard at the UIManager game-over branch. Stub `ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false))` to prevent unrelated notification processing. The `Times(0)` assertion is the primary enforcement: without it, a missing Sandbox guard would silently fire the game-over modal and the test would still pass because `NiceMock` ignores unexpected calls by default.

- **`CameraController` testability**: `CameraController`'s pan/zoom/rotate input processing must be unit-testable by injecting synthetic `InputEvent` structs (defined in `src/platform/input_event.h`). The controller must accept a `CameraState` struct (position, target, pitch, yaw) and expose `getCameraState()` — tests drive events in, read state out, verify pitch clamping at [−70°, −20°] and edge-scroll behavior without a live scene node. `CameraController` must also expose `bool isEdgeScrollEnabled() const` as a public accessor returning the current value of `m_edgeScrollEnabled`; this is required by test case 6 (`CameraController_EdgeScroll_EnabledByDefaultInFullscreen`) to assert constructor initial state without input injection. **Source location**: `CameraController.h/.cpp` live in `src/ui/` (it is an input/UI concern, not a rendering concern); test file is `tests/ui/camera_controller_test.cpp`. This placement ensures `CameraController` is covered by the `src/ui/` 95% coverage gate.
- **`CameraController` input abstraction**: `CameraController` must accept an `InputEvent` struct (defined in `src/platform/input_event.h`) rather than Irrlicht's `SEvent`, to avoid pulling Irrlicht headers into test translation units:

  ```cpp
  // src/platform/input_event.h
  struct InputEvent {
      enum class Type {
          MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel, KeyDown, KeyUp,
          WindowFocusGained, WindowFocusLost  // required for UIManager pause-on-alt-tab and input arbitration
      };
      Type type;
      int x{0}, y{0};        // cursor position in virtual 1920×1080 space (for UI hit-testing in UIManager)
      int physX{0}, physY{0}; // cursor position in physical pixels (for drag-delta in CameraController)
                               // drag-delta MUST use physX/physY, NOT x/y — see camera-controls.md UX-1 note.
                               // The platform IEventReceiver populates both fields: x/y from UIScaler::unproject(),
                               // physX/physY from the raw SEvent coordinates before unproject().
      int button{0};          // 0=left, 1=right, 2=middle (for mouse button events)
      float wheelDelta{0.f};  // for MouseWheel events
      int keyCode{0};         // SDL2-style key code (for key events)
  };
  ```

  The concrete `IEventReceiver` implementation in `src/platform/` translates `SEvent` to `InputEvent` before forwarding to `CameraController`. Test files in `tests/ui/` construct `InputEvent` structs directly — no Irrlicht headers required. `CameraController::OnInputEvent(const InputEvent&)` replaces `IEventReceiver::OnEvent(const SEvent&)` in the `CameraController` public interface.

  **Required Named Test Cases** — all 9 test cases must be authored in `tests/ui/camera_controller_test.cpp` and registered under the `ui_tests` CMake target (label `unit`). Per `architecture/ui-ux/camera-controls.md`, pitch clamp tests must use exact equality assertions (`EXPECT_EQ` / `EXPECT_FLOAT_EQ`) rather than strictly-less-than comparisons, because the spec defines inclusive bounds using `std::clamp` semantics:

  Note: `MouseWheel` drives zoom distance ONLY — it MUST NOT be used in pitch-clamp test cases, as scroll wheel events do not affect pitch and produce a test that never reaches the pitch clamp boundary.
  1. `CameraController_PitchClamp_AtUpperBound_ExactlyMinus20` — inject `InputEvent{Type::MouseButtonDown, button=1}` (right mouse button) followed by repeated `InputEvent{Type::MouseMove, physX=prevPhysX, physY=prevPhysY - N}` events (RMB upward drag, decreasing physY each iteration) driving pitch toward the −20° upper bound; assert `getCameraState().pitch == -20.0f` using `EXPECT_FLOAT_EQ` (not `EXPECT_LT` — the bound is inclusive and pitch must equal exactly −20°, not merely be less than some value above it).
  2. `CameraController_PitchClamp_AtLowerBound_ExactlyMinus70` — inject `InputEvent{Type::MouseButtonDown, button=1}` (right mouse button) followed by repeated `InputEvent{Type::MouseMove, physX=prevPhysX, physY=prevPhysY + N}` events (RMB downward drag, increasing physY each iteration) driving pitch toward the −70° lower bound; assert `getCameraState().pitch == -70.0f` using `EXPECT_FLOAT_EQ` (not `EXPECT_GT` — the bound is inclusive and pitch must equal exactly −70°).
  3. `CameraController_EdgeScroll_DisabledOnFocusLoss` — record initial `getCameraState().position`; inject `InputEvent{Type::WindowFocusLost}`; inject `InputEvent{Type::MouseMove, x=0, y=540}` (cursor at left edge of 1920-wide virtual space, which would normally trigger left edge-scroll); assert `getCameraState().position` is unchanged, confirming that `m_appHasFocus = false` suppresses edge-scroll input processing.
  4. `CameraController_RightMouseRotate_MovesYaw` — record initial yaw via `getCameraState().yaw`; inject `InputEvent{Type::MouseButtonDown, button=1}` (right mouse button) followed by `InputEvent{Type::MouseMove, physX=prevPhysX+10, physY=prevPhysY}` (horizontal drag of 10 physical pixels — `CameraController` reads `physX`/`physY` for drag-delta, NOT `x`/`y`); assert `getCameraState().yaw != initialYaw`, confirming that RMB drag drives yaw rotation. (Exact delta magnitude is implementation-defined; this test verifies directional sensitivity, not a precise angle.) Test comment must state: "physX/physY are physical pixel coordinates for drag-delta per UX-1."
  5. `CameraController_MiddleMousePan_MovesPosition` — record initial position via `getCameraState().position`; inject `InputEvent{Type::MouseButtonDown, button=2}` (middle mouse button) followed by `InputEvent{Type::MouseMove, physX=prevPhysX+5, physY=prevPhysY}` (horizontal drag of 5 physical pixels — `CameraController` reads `physX`/`physY` for drag-delta); assert `getCameraState().position` differs from the initial position (at least one component changed), confirming that MMB drag drives camera pan. Test comment must state: "physX/physY are physical pixel coordinates for drag-delta per UX-1."
  6. `CameraController_EdgeScroll_EnabledByDefaultInFullscreen` — construct `CameraController` with `startInFullscreen=true`; immediately call `isEdgeScrollEnabled()` without any intervening call to `setEdgeScrollEnabled(true)`; assert the return value is `true`. This confirms that the constructor enables edge scroll automatically when `startInFullscreen=true`, so the player does not need to manually activate edge scrolling when launching in fullscreen mode.
  7. `CameraController_SetEdgeScroll_Enabled_Then_FocusLost_DoesNotClearEnabled`
     — Construct with `startInFullscreen=false` (edge scroll disabled by default).
     Call `setEdgeScrollEnabled(true)` → assert `isEdgeScrollEnabled()` returns `true`.
     Inject `InputEvent{Type::WindowFocusLost}` → assert `isEdgeScrollEnabled()` STILL
     returns `true` (focus loss must NOT mutate `m_edgeScrollEnabled`; it only sets
     `m_appHasFocus = false`).
     Inject `InputEvent{Type::MouseMove, x=0, y=540}` (left edge) → assert camera
     position UNCHANGED (edge scroll suppressed because `m_appHasFocus = false`
     despite `m_edgeScrollEnabled = true`).
     **MANDATORY final step** — Inject `InputEvent{Type::WindowFocusGained}` then
     `InputEvent{Type::MouseMove, x=0, y=540}` (left edge — within the 20px activation
     band) → assert that `getCameraState().position` DID change (edge scroll is active
     again after focus restored, since `m_edgeScrollEnabled` was never changed).
     This step is **not optional**: without it, a broken implementation that permanently
     disables edge scroll after `WindowFocusLost` (never re-enabling on `WindowFocusGained`)
     would still pass all prior assertions. The re-enablement assertion is the only step
     that distinguishes a correct implementation from one with a latent re-enable defect.
     Use `EXPECT_EQ` for bool assertions; use `EXPECT_NE` for camera position change.
  8. `CameraController_EdgeScroll_DisabledByDefaultInWindowed` — construct `CameraController` with `startInFullscreen=false`; immediately call `isEdgeScrollEnabled()` without any intervening call to `setEdgeScrollEnabled()`; assert the return value is `false`. This is the symmetric counterpart to test case 6 and confirms the windowed-default rule from `architecture/ui-ux/camera-controls.md`.
  9. `CameraController_KeyboardPanIgnoresSensitivity` — **(Phase 8 enforcement point)** construct `CameraController` with a non-unit sensitivity multiplier (e.g., `sensitivityMultiplier=2.0`); inject a keyboard pan event (e.g., `InputEvent{Type::KeyDown, key=KEY_A}` or the left-pan hotkey from `architecture/ui-ux/hotkey-scheme.md`); assert that the camera position delta equals the expected **unscaled** pan step, confirming that keyboard pan speed is NOT multiplied by `sensitivityMultiplier`. Mouse drag pan (RMB, MMB) applies `sensitivityMultiplier`; keyboard pan must not. This test must pass from Phase 1 to establish the invariant before Phase 8 adds the sensitivity slider. Placing the test here ensures that the Phase 8 sensitivity implementation cannot accidentally apply the multiplier to keyboard pan without breaking this test.

- **`QueryPanel` testability**: `QueryPanel::computePanelPosition(int cursorX, int cursorY, ScreenRect tileBounds)` must be a pure function (no side effects, no Irrlicht dependency) returning a `ScreenRect` (`ScreenRect` is added to `IRenderer.h` by Phase 9b Deliverable B as `struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; }` — plain POD, Irrlicht-free; `inspector_panel.h` may include `IRenderer.h` without violating the Irrlicht-free UI rule). Required test cases:
  1. **Primary placement**: cursor at (100, 100) with no tile overlap → verify panel placed at (140, 140).
  2. **Fallback placement**: primary position overlaps tile bounds → verify panel moves to above-left fallback position.
  3. **Edge clamping**: four sub-cases (left, right, top, bottom edge) verifying `clamp()` correctness — panel never extends outside [0, 1920−240] × [0, 1080−160].
  4. **Third fallback (edge-snap)**: cursor at screen center (960, 540) with a large tile bounding box that overlaps BOTH primary and fallback positions; verify panel is snapped to the correct screen edge per the edge-snap formula:
     - `edge_x = (cursorX <= screenWidth / 2) ? (screenWidth - panelWidth) : 0` — panel moves to the right screen edge when cursor is in the left half **or exactly at center** (`<=`), left screen edge when cursor is strictly in the right half. The `<=` (not `<`) is intentional: the center pixel is treated as left-half, consistent with screen-split conventions.
     - `edge_y = clamp(cursorY - panelHeight / 2, 0, screenHeight - panelHeight)` — panel is vertically centered on the cursor, clamped to screen bounds
     - Panel dimensions: 240×160 px (virtual); screen: 1920×1080 (virtual)
     - Expected for cursor at (960, 540): since 960 == screenWidth/2, the `<=` tie-break maps to left-half (use right edge): `edge_x = 1920 - 240 = 1680`; `edge_y = clamp(540 - 80, 0, 920) = 460`. Verify panel rect = {1680, 460, 240, 160}.
- **`KeyBindings` testability**: `KeyBindings` conflict detection and Q/E reservation logic must be unit-tested in `tests/ui/` with no Irrlicht or display dependency. Required test cases:
  1. Assigning a key already used by another action: conflict detection triggers; `keybindings.json` is NOT written; the conflicting state is not persisted.
  2. Swap resolution: action A gets B's key, action B gets A's key; both assignments are applied atomically; `keybindings.json` is written exactly once after the swap.
  3. Attempting to assign Q or E: `isReservedKey("KeyQ")` returns true; assignment is rejected immediately; the conflict detection Swap/Cancel flow is NOT entered.
  4. Loading `keybindings.json` containing Q or E for any action: the binding is ignored; a warning is logged; the default binding for that action is restored in memory.
  5. A conflict-free assignment: `keybindings.json` is written; subsequent load of the file produces the same binding state (round-trip test).
- **`ModalDialog` + auto-pause testability** (tests in `tests/ui/` and `tests/simulation/`). Required test cases:
  1. `ModalDialog_OnOpen_SimulationIsPaused` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: calling `UIManager::showModal()` calls `CitySimulation::setPaused(true)` before returning.
  2. `ModalDialog_OnOpen_SpeedSelectorIsDisabled` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: `IUIBackend::setElementEnabled(..., false)` is called on the speed selector handle (not `setElementVisible` — the selector remains visible but non-interactive).
  3. `ModalDialog_OnClose_SimulationResumes` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: dismissing the modal calls `setPaused(false)` and calls `setElementEnabled(..., true)` on the speed selector to re-enable it. **Pre-condition**: the fixture MUST ensure `isPaused() == false` before calling `showModal()`, so that `UIManager` observes an unpaused simulation and sets its internal `m_didPauseSim` flag to `true` on modal open. Include `EXPECT_CALL(sim_, isPaused()).WillOnce(Return(false))` as part of the `showModal()` setup stub. **Assertion note**: `setPaused(false)` is called on `closeModal()` only when `m_didPauseSim == true` (i.e., only when `UIManager` paused the simulation when opening this modal); if the simulation is already paused at `showModal()` time, `m_didPauseSim` is left `false` and `setPaused(false)` will NOT be called on `closeModal()` — testing from an already-paused initial state would produce a false pass because the assertion would vacuously hold for the wrong reason. **Ordering assertion (mandatory)**: `closeModal()` MUST call `setPaused(false)` before `setModalActive(false)`. Enforce this with `::testing::InSequence seq;` declared before both `EXPECT_CALL`s so GMock fails immediately if the order is violated.

     ```cpp
     ::testing::InSequence seq;
     // setPaused(false) must fire BEFORE setModalActive(false):
     // closeModal() must unpause the simulation before clearing the modal-active flag
     // so that any code observing isModalActive()==false can assume the simulation
     // is already unpaused and will not see a transient paused+non-modal state.
     EXPECT_CALL(sim_, setPaused(false)).Times(1);
     EXPECT_CALL(ui_,  setModalActive(false)).Times(1);
     uiManager_->closeModal();
     ```

     The `InSequence` guard covers exactly these two calls. Do NOT use `.After()` syntax here — `InSequence` is the preferred style for consecutive paired assertions in this test suite (consistent with `UIManagerDrawOrderTest` usage above). An implementation that reverses the order (`setModalActive(false)` before `setPaused(false)`) will fail this test with a GMock sequence violation, which is the intended enforcement.

  4. `UndoSystem_BlockedDuringModal_HotkeyIgnored` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: injecting a Ctrl+Z `InputEvent` while modal is active does NOT call any undo operation.
  5. `UndoSystem_BlockedDuringModal_ButtonGrayedOut` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: while modal is active, `setElementEnabled(..., false)` is called on the undo button element via `IUIBackend`.
  6. `CriticalToast_DuringModal_IsQueued_NotDisplayed` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: posting a CRITICAL toast while a blocking modal is active queues the toast but does NOT display it immediately (no `addStaticText` call to `IUIBackend` for the toast element). After modal dismissal, the toast becomes visible (a deferred `addStaticText` call is verified).
  7. `CriticalToast_DuringModal_AutoPauseDeferred` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: CRITICAL toast auto-pause logic does not fire while a blocking modal is active; `setPaused(true)` is NOT called a second time for the toast arrival (the modal pause is already active). After modal dismissal, if the CRITICAL queue is non-empty, auto-pause state is re-evaluated once.
  8. `ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: post a CRITICAL toast while a blocking modal is active (verifies no second `setPaused(true)` call during modal-active period), then dismiss the modal (`UIManager::closeModal()`), then verify: (a) the queued CRITICAL toast is now displayed (`addStaticText` called on `MockUIBackend`), and (b) `setPaused(true)` is called **once more** during `closeModal()` re-evaluation — meaning **twice total** across the test (once on modal open, once on re-evaluation in `closeModal()` because CRITICAL queue is non-empty); `setPaused(false)` is NOT called — simulation stays paused because the CRITICAL toast remains active after modal close. **Reconciliation with StrictMock matrix**: The StrictMock Expected Call Matrix entry for this test specifies `setPaused(true) × 2` (total) and `setPaused(false) × 0` — the prose description above matches this. The "exactly once" wording in prior spec drafts referred to the re-evaluation step only (one call within `closeModal()`), not the total across the test; this was ambiguous and has been corrected to "once more during closeModal()". This test exercises the deferred re-evaluation path explicitly — without it, the re-evaluation call in the `closeModal()` code path is unverified and can be silently dropped. **Deferred `addStaticText` call timing**: The CRITICAL toast's `addStaticText` call to `MockUIBackend` MUST occur synchronously within the same `closeModal()` call stack — NOT deferred to the next `update()` tick. This is a firm implementation requirement: the `closeModal()` implementation must call the display logic synchronously, not schedule it for the next frame. Tests assert the element handle's presence immediately after `closeModal()` returns, with no intervening `update()` call. Implementations that defer display to `update()` do not meet this requirement and must be refactored.
  9. `Modal_SpeedSelectorGrayed_DespiteCriticalToast_SpeedAccessible_WhenModalOnly` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: when only a CRITICAL toast is active (no modal), the speed selector remains ENABLED (accessible per CRITICAL-toast-pause spec). This distinguishes modal-pause (selector grayed) from CRITICAL-toast-pause (selector accessible).
  10. `ModalDialog_OnClose_WithEmptyCriticalQueue_NoAutoRePause` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: open a modal (verifies `setPaused(true)` called once), then dismiss the modal with no CRITICAL toasts in the queue, then verify: (a) `setPaused(false)` is called exactly once (simulation resumes), and (b) `setPaused(true)` is NOT called again during `closeModal()`. This is the inverse of test 8 — it confirms that the CRITICAL toast auto-pause re-evaluation in `closeModal()` does NOT call `setPaused(true)` when the CRITICAL queue is empty, preventing a spurious re-pause on normal modal dismiss.
  11. `CriticalToast_HiddenWhileModalActive_ReappearsAfterClose` _(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)_: fixture is `NotificationManagerTest` (using `NiceMock<MockUIBackend>` + `NiceMock<MockCitySimulation>` + `ManualClock`); file is `notification_system_test.cpp`. Test sequence: (1) open a blocking modal so `isModalActive()` returns `true`; (2) post a CRITICAL toast to `NotificationManager` — verify `isElementVisible(toastHandle)` returns `false` immediately (toast is created in a hidden state because a modal is active, i.e., no `addStaticText` call or `setElementVisible(..., true)` is made while the modal-active flag is set); (3) call `closeModal()` synchronously; (4) advance `ManualClock` by one frame delta and call `NotificationManager::update(dt)` once; (5) verify `isElementVisible(toastHandle)` returns `true` — the CRITICAL toast that was hidden during modal-active state reappears (becomes visible) in the next frame after `closeModal()`. This test exercises the Priority 2 dual-guard interaction path: the modal-active guard suppresses toast display, and the post-close re-evaluation path restores visibility. The `NiceMock<MockCitySimulation>` satisfies the `ICitySimulation*` constructor parameter without requiring exhaustive stubs; the `NiceMock<MockUIBackend>` tracks `isElementVisible` state through delegated ON_CALL return values keyed by handle.
  12. `BondModal_ExhaustedUses_ButtonGrayedOut` _(Phase 8 deliverable — no Phase 3 stub; authored in full in Phase 8)_: construct `UIManager` with `NiceMock<MockUIBackend>` and `NiceMock<MockCitySimulation>` stubbed to return 0 from `ICitySimulation::getOutstandingBondUses()`; trigger the forced loan dialog Screen 2 render path (the screen showing the three action options); verify `IUIBackend::setElementEnabled(bondButtonHandle, false)` is called — confirming the Emergency Municipal Bond option is grayed when no bond uses remain. This test did not exist at Phase 3 time and has no fixture stub. Test file: `tests/ui/modal_dialog_test.cpp`. Mock policy: `NiceMock<MockUIBackend>` + `NiceMock<MockCitySimulation>`. **Approved mock policy deviation**: `NiceMock<MockUIBackend>` (not `StrictMock`) is used here because `BondModalTest` constructs a full `UIManager`, whose constructor creates all panels (HUD, Minimap, MainMenu, etc.) producing dozens of `addStaticText`/`addButton`/`setElementVisible` calls. `StrictMock` would require 50+ `EXPECT_CALL` stubs in `SetUp()`, defeating the fixture's purpose. This is the same pragmatic rationale as `UIManagerModalTest` (see NiceMock + explicit `Times()` CONTRACT above). The `setElementEnabled(_, false).Times(AtLeast(1))` assertion compensates by explicitly verifying the critical grayed-button call. **Fixture isolation**: `BondModalTest` MUST be a standalone test class that does NOT inherit from `UIManagerModalTest`. Cross-reference: `architecture/ui-ux/modal-dialog-system.md` forced loan Screen 2 spec; `architecture/game-design/economy-model.md` bond-uses-per-difficulty table.

## SettingsPanelTest Fixture (Phase 8)

**Fixture setup**: `NiceMock<MockUIBackend>` + `ManualClock` + `StrictMock<MockAudioSystem> audio_`; `TearDown()` resets `panel_` (or equivalent `SettingsPanel` pointer) to `nullptr` before BOTH `MockUIBackend` AND `StrictMock<MockAudioSystem>` are destroyed. The `StrictMock<MockAudioSystem>` is the higher-risk destructor path: strict mocks fire unexpected-call errors on destruction if any unmatched call occurs during teardown, so `panel_` must be fully reset first.

**Why `StrictMock` (not `NiceMock`) for `MockAudioSystem`**: The three volume-control test cases (`setMasterVolume`, `setMusicVolume`, `setSFXVolume`) exercise direct `IAudioSystem` calls made by the slider callbacks in `SettingsPanel`. These calls must be verified exactly — every slider movement must produce the correct `IAudioSystem` call with the correct gain value, and no unexpected `IAudioSystem` calls must occur. `NiceMock` would silently swallow unexpected calls, hiding regressions where a slider inadvertently triggers the wrong volume method or triggers it more times than expected. `StrictMock` enforces that only calls declared via `EXPECT_CALL` occur, making the fixture self-auditing for the volume-control path.

**Three required test cases**:

- `GraphicsTab_CountdownExpiry_AutoRevertsSettings` — Apply settings changes (stub a resolution change callback); advance `ManualClock` by 11 seconds (past the 10-second countdown); call `SettingsPanel::update()` after each advancement; verify that settings revert to pre-Apply values (the applied change is cancelled) — verifies the `IClock::nowSeconds()` countdown expiry path.

- `GraphicsTab_ConfirmBeforeExpiry_SettingsRetained` — Apply settings changes; call confirm within 5 seconds (before 10-second expiry); advance `ManualClock` to 11 seconds; call `SettingsPanel::update()` after each advancement; verify settings are NOT reverted — verifies that player confirmation before expiry permanently applies the change.

- `GraphicsTab_CountdownText_DecrementsEachSecond` — Apply settings changes; advance `ManualClock` one second at a time for 3 seconds; after each second advance, call `SettingsPanel::update()`; use `EXPECT_CALL(backend_, setElementText(countdownLabelHandle, HasSubstr("Reverting in"))).Times(AtLeast(3))` (placed before update calls) to verify the modal body label is updated with the correct per-second countdown text on each tick (ref: `architecture/ui-ux/settings-pause-menu.md` — "displayed numerically", "decrements each real second").

- **`HUD` testability — undo countdown and density unlock preview** (tests in `tests/ui/undo_button_test.cpp` and `tests/ui/budget_detail_panel_test.cpp`). Required Phase 8 test cases:
  - `UndoCountdown_AmberAt10xSpeed_ImmediatelyOnAction` _(Phase 8 deliverable)_: construct `HUD` with `ManualClock` at simulation speed 10×; take an undoable action; assert that the undo button label is amber immediately at action time (`t=0`), because the total undo window at 10× speed is ≤6 real seconds, meeting the amber-on-creation threshold. Verifies `hud-layout.md` rule: "set amber if `remainingSeconds < 5.0 || totalWindowSeconds <= 6.0`" — the `totalWindowSeconds <= 6.0` branch fires at 10× speed from the moment the action is taken.
  - `DensityUnlockPreview_HiddenWhenSentinelReturned` _(Phase 8 deliverable)_: construct `HUD` with `MockCitySimulation` stubbed to return `SimulationConstants::kNoUnlockThreshold` (`−1.0f`) from `getNextUnlockThreshold()`; call `HUD::update()`; verify `IUIBackend::setElementVisible(densityUnlockHandle, false)` is called (element hidden) and no label text is set to `"−1"` or `"−1.0"`. Verifies the sentinel guard from `hud-layout.md`: the HUD MUST intercept `kNoUnlockThreshold` before any formatting occurs and hide the element unconditionally.
- **`ISimulationRNG`** — injectable RNG interface for deterministic simulation testing: Service degradation (random building selection at −10% budget surplus) and any other simulation-layer random draws must use this interface rather than `std::rand()` or a global `std::mt19937`. Tests inject a `ManualRNG` that returns a preset sequence. **Source location**: `ISimulationRNG.h` lives in `src/interfaces/`; `ManualRNG` lives in `tests/simulation/ManualRNG.h` (used by simulation tests) — **not** in `src/` (it is a test double, never linked into production code).

  **Spec entry**:

  ```text
  Header: src/interfaces/ISimulationRNG.h
  Methods:
    virtual float nextFloat()              = 0;  // uniform [0.0, 1.0)
    virtual int   nextInt(int min, int max) = 0;  // inclusive [min, max]
    virtual ~ISimulationRNG() = default;
  Implementations:
    StdRNG    — production (std::mt19937)
    ManualRNG — tests (returns pre-loaded sequence)
  ```

  Cross-reference: `architecture/game-design/service-coverage.md` (ISimulationRNG usage for service degradation rolls — see testability-architecture.md §ISimulationRNG for the canonical interface definition).

  ```cpp
  class ISimulationRNG {
  public:
      virtual ~ISimulationRNG() = default;
      virtual int nextInt(int min, int max) = 0;  // inclusive [min, max]
      virtual float nextFloat() = 0;              // [0.0, 1.0)
  };
  class ManualRNG : public ISimulationRNG {
  public:
      // Two separate sequences: one for nextInt(), one for nextFloat().
      // Using a single shared sequence causes corruption when code under test
      // calls nextInt() and nextFloat() in interleaved order — each call type
      // must consume from its own independent queue.
      // floatSeq stores float values directly in [0.0, 1.0) — NOT scaled integers.
      // This allows the full [0.0, 1.0) range to be represented, including values
      // close to 1.0 (e.g. 0.9999f). The default is a single {0.5f}.
      // strict=true (default): throws if either sequence is exhausted before the test ends.
      // strict=false: wraps around (for tests that intentionally loop a short sequence).
      // Always use strict=true in unit tests — wrap-around silently hides bugs where
      // code under test calls nextInt()/nextFloat() more times than expected.
      explicit ManualRNG(std::initializer_list<int>   intSeq,
                         std::initializer_list<float> floatSeq = {0.5f},
                         bool strict = true)
          : m_intSeq(intSeq), m_floatSeq(floatSeq), m_strict(strict) {
          if (m_intSeq.empty()) {
              throw std::invalid_argument("ManualRNG: intSeq must not be empty");
          }
          if (m_floatSeq.empty()) {
              throw std::invalid_argument("ManualRNG: floatSeq must not be empty");
          }
          for (float v : m_floatSeq) {
              if (v < 0.0f || v >= 1.0f) {
                  throw std::out_of_range(
                      "ManualRNG: floatSeq value out of [0.0, 1.0) range — fix test data");
              }
          }
      }
      // **Fixture initialization warning**: When `ManualRNG` is a fixture member, it MUST be
      // initialized as `ManualRNG rng_{{0}}` — a single-element integer sequence. Using `rng_{}`
      // or `rng_{{}}` both construct with an empty `initializer_list<int>` and will throw
      // `std::invalid_argument` during fixture construction (in `SetUp()`), aborting every test
      // in the fixture with a confusing error message. The double-brace form `{{0}}` is required
      // to create a non-empty initializer list.
      // Returns the next value from the int preset sequence.
      // In strict mode (default): throws std::logic_error if the sequence is exhausted.
      // In non-strict mode: wraps around to the beginning of the sequence.
      // CONTRACT: The stored values ARE the literal return values — they are NOT
      // clamped to [min, max]. Tests must ensure stored values are within the
      // expected range for the code under test.
      // A throw fires immediately if a stored value is out of range, catching
      // test-data bugs at the point of call in both Debug and Release builds.
      // Do NOT use assert() here — assert() is stripped in Release builds, causing
      // out-of-range values to silently pass through in production test runs.
      int nextInt(int min, int max) override {
          if (m_intIdx >= m_intSeq.size()) {
              if (m_strict)
                  throw std::logic_error(
                      "ManualRNG: int sequence exhausted — code under test called nextInt() "
                      "more times than the sequence length. Either add more values to the "
                      "sequence or use ManualRNG({...}, {...}, /*strict=*/false) to allow wrap-around.");
              m_intIdx = 0;  // non-strict: wrap around
          }
          int v = m_intSeq[m_intIdx++];
          if (v < min || v > max)
              throw std::out_of_range(
                  "ManualRNG: stored value out of expected [min, max] range — fix test sequence data");
          return v;
      }
      // Returns the next float value directly from the float preset sequence.
      // In strict mode (default): throws std::logic_error if the sequence is exhausted.
      // In non-strict mode: wraps around.
      // CONTRACT: All values stored in floatSeq MUST be in [0.0, 1.0) — validated
      // at construction time with a throw (not a debug assert) so the check survives
      // Release builds. The full range [0.0, 1.0) is representable, including values
      // close to 1.0. Do not use scaled integers (e.g. 999/1000) — use float literals
      // directly: ManualRNG({0}, {0.5f, 0.99f}).
      float nextFloat() override {
          if (m_floatIdx >= m_floatSeq.size()) {
              if (m_strict)
                  throw std::logic_error(
                      "ManualRNG: float sequence exhausted — code under test called nextFloat() "
                      "more times than the sequence length.");
              m_floatIdx = 0;  // non-strict: wrap around
          }
          return m_floatSeq[m_floatIdx++];
      }
      // Asserts that all values in both sequences have been consumed.
      // Throws std::logic_error if either sequence has unconsumed values remaining.
      //
      // USAGE POLICY — when to call verifyAllConsumed():
      //
      // (a) Test-local ManualRNG (declared inside a test body):
      //     Call verifyAllConsumed() at the end of the test body before the variable goes
      //     out of scope. This is the most common pattern.
      //
      //     Example:
      //       TEST_F(CitySimulationUnitTest, ServiceDegradation_RandomBuildingSelected) {
      //           ManualRNG localRng({3}, {0.5f});
      //           // ... inject localRng, run test ...
      //           localRng.verifyAllConsumed();  // <-- at end of test body
      //       }
      //
      // (b) Fixture member ManualRNG that is re-seeded in SetUp() each test:
      //     TearDown() MAY call verifyAllConsumed() ONLY if SetUp() unconditionally
      //     re-seeds the fixture member before every test — i.e., the member is always
      //     in a fresh state at test start and TearDown() can safely assert it was fully
      //     consumed. If any test in the fixture intentionally leaves values unconsumed
      //     (e.g. zero-revenue tests where nextInt() is never called), do NOT call
      //     verifyAllConsumed() in TearDown(); instead, use per-test-body calls as in (a).
      //
      //     The standard CitySimulationUnitTest fixture (rng_{{0}}) does NOT call
      //     verifyAllConsumed() in TearDown() because zero-revenue tests never consume
      //     the sequence — see the fixture's TearDown() comment for rationale.
      //
      // Rule of thumb: if ALL tests in a fixture consume ALL RNG values, TearDown() is
      // acceptable. If even one test leaves values unconsumed, use test-body calls only.
      void verifyAllConsumed() const {
          if (m_intIdx != m_intSeq.size())
              throw std::logic_error("ManualRNG: int sequence over-provisioned — " +
                  std::to_string(m_intSeq.size() - m_intIdx) + " values not consumed");
          if (m_floatIdx != m_floatSeq.size())
              throw std::logic_error("ManualRNG: float sequence over-provisioned — " +
                  std::to_string(m_floatSeq.size() - m_floatIdx) + " values not consumed");
      }
  private:
      std::vector<int>   m_intSeq;
      std::vector<float> m_floatSeq;
      size_t m_intIdx{0};
      size_t m_floatIdx{0};
      bool   m_strict{true};
  };
  ```

  `CitySimulation` accepts `ISimulationRNG*` at construction; production code passes a `std::mt19937`-backed implementation.

  **Required Self-Tests** — the following 6 named test cases must be registered in `tests/simulation/manual_rng_test.cpp` under the `simulation_tests` CMake target and must compile and pass before any code path that uses `ManualRNG` is considered verified:
  1. `ManualRNG_VerifyAllConsumed_ThrowsOnOverProvision` — construct `ManualRNG({0, 1}, {0.5f, 0.5f})` (2 ints, 2 floats); call `nextInt(0, 1)` exactly once (leaving the second int unconsumed); call `verifyAllConsumed()` → expect `std::logic_error` to be thrown, confirming that over-provisioning is detected.
  2. `ManualRNG_VerifyAllConsumed_NoThrowWhenFullyConsumed` — construct `ManualRNG({0, 1}, {0.5f, 0.5f})` (2 ints, 2 floats); consume all values (`nextInt` twice, `nextFloat` twice); call `verifyAllConsumed()` → expect no exception to be thrown.
  3. `ManualRNG_EmptyIntSeq_ThrowsAtConstruction` — construct `ManualRNG({}, {0.5f})` (empty int sequence, one float) → expect `std::invalid_argument` to be thrown at construction time, not at first `nextInt()` call.
  4. `ManualRNG_FloatSeqOutOfRange_ThrowsAtConstruction` — construct `ManualRNG({0}, {1.0f})` (one int, one float equal to 1.0f which is not in [0.0, 1.0)) → expect `std::out_of_range` to be thrown at construction time, confirming the inclusive-upper-bound guard fires.
  5. `ManualRNG_EmptyFloatSeq_ThrowsAtConstruction` — construct `ManualRNG({0}, {})` (non-empty int sequence, empty float sequence) → expect `std::invalid_argument` to be thrown at construction time, not at first `nextFloat()` call. This covers the `m_floatSeq.empty()` guard in the constructor body, which is distinct from the out-of-range float guard tested in case 4.
  6. `ManualRNG_NextInt_OutOfRange_ThrowsAtCallTime` — construct `ManualRNG({5}, {0.5f})` with a stored int value of 5; call `nextInt(0, 3)` (where `5 > 3`, so the stored value is outside `[min, max]`) → expect `std::out_of_range` to be thrown at call time (not at construction time). This validates the per-call range guard in `nextInt()`: the implementation throws immediately when the stored preset value falls outside the `[min, max]` bounds supplied by the caller, catching test-data bugs in both Debug and Release builds. Note: this test exercises the call-time range check only — the constructor does not validate int sequence values at construction (unlike float values which are range-checked at construction). Also verify the converse: a within-range call does NOT throw — construct `ManualRNG({2}, {0.5f})` and call `nextInt(0, 3)` → expect return value `2` and no exception.

- **`ITerrainRNG`** — injectable RNG interface for deterministic terrain generation testing. **Source location**: `ITerrainRNG.h` lives in `src/interfaces/` (moved from `src/terrain/` in Phase 10b Feature 3); `MockTerrainRNG` lives in `tests/terrain/MockTerrainRNG.h` (renamed from `mock_terrain_rng.h` to CamelCase in Phase 10b Feature 3). The `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` property test requires an injectable mock that counts re-seed calls:

  ```cpp
  class ITerrainRNG {
  public:
      virtual ~ITerrainRNG() = default;
      virtual float nextFloat() = 0;              // [0.0, 1.0) — continuous noise and feature probability
      virtual int   nextInt(int min, int max) = 0; // inclusive [min, max] — discrete terrain feature counts, tile selection
      virtual void  reseed(uint64_t seed) = 0;    // called when generator retries with a new seed
  };
  // NAMING CONVENTION NOTE: `MockTerrainRNG` does NOT use GMock macros (`MOCK_METHOD`).
  // It is a manual stub with a real `std::mt19937_64` engine. The CamelCase `Mock` prefix
  // (after Phase 10b Feature 3 rename from `mock_terrain_rng.h`) matches the `'*/Mock*.h'`
  // lcov exclusion pattern added in Phase 10b. The name `MockTerrainRNG` (not
  // `ManualTerrainRNG`) is canonical — do not rename.
  class MockTerrainRNG : public ITerrainRNG {
  public:
      explicit MockTerrainRNG(uint64_t seed) : m_rng(seed) {}
      float nextFloat() override { return std::uniform_real_distribution<float>(0.f, 1.f)(m_rng); }
      int   nextInt(int min, int max) override { return std::uniform_int_distribution<int>(min, max)(m_rng); }
      void reseed(uint64_t seed) override {
          m_rng.seed(seed);
          ++m_reseedCount;
      }
      int reseedCount() const { return m_reseedCount; }
  private:
      std::mt19937_64 m_rng;
      int m_reseedCount{0};
  };
  ```

  `TerrainGenerator` accepts `ITerrainRNG*` at construction; production code passes a `std::mt19937_64`-backed implementation. `MockTerrainRNG` is used in `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` to verify the reseed count stays ≤ 100. **Constructor signatures**: `TerrainGenerator(uint64_t seed)` for production use (constructs an internal `std::mt19937_64`-backed `ITerrainRNG`); `TerrainGenerator(uint64_t seed, ITerrainRNG* rng)` for testing (accepts injected `ITerrainRNG*` as non-owning pointer — the mock outlives the generator in all test fixtures).

  **CRITICAL — production constructor coverage**: The fixed-seed regression tests (`TerrainGenerator_OutputAlwaysMeetsConstraint`, `TerrainGenerator_OutputHasContiguousFlatArea`, `TerrainGenerator_PrimaryRegressionSeed_MeetsBothConstraints`) MUST use the single-argument production constructor `TerrainGenerator(seed)` — NOT the two-argument injectable form. Using the two-argument form in fixed-seed tests would leave the production constructor path (which constructs the internal `mt19937_64` RNG) with zero test coverage. The property test `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` uses the two-argument form with `MockTerrainRNG` (to count reseeds), but this does not cover the production constructor's RNG initialization code. The three fixed-seed `TEST_F` tests cover the production constructor path. **This division is mandatory**: property test → two-argument form with mock; fixed-seed regression tests → single-argument production form.

- **`ICitySimulation`** — interface enabling `UIManager` to call simulation control methods without depending on the concrete `CitySimulation` class. **Source location**: `ICitySimulation.h` lives in `src/interfaces/` (alongside `ISimulationRNG.h`, `IClock.h`, and `ISimulationPauser.h` — the shared dependency-free header directory that both `src/simulation/` and `src/ui/` may include). `UIManager` must accept `ICitySimulation*` (not a concrete `CitySimulation*`) to enable headless testing. `MockCitySimulation` lives in `tests/ui/MockCitySimulation.h`.

  **Sub-interface decomposition (Phase 11q1)**: The full 44-method `ICitySimulation`
  interface is decomposed into three focused sub-interfaces plus 7 own methods:
  12 in `IEconomyQuery` + 9 in `IZoningActions` + 15 in `ISimulationState` +
  7 own + 1 inherited from `ISimulationPauser` = 44 total. All sub-interface headers live
  in `src/interfaces/`. `MockCitySimulation` inherits them all through
  `ICitySimulation`, so no `MOCK_METHOD` changes are needed.

  **`IEconomyQuery`** (`src/interfaces/IEconomyQuery.h`) -- 12 methods covering
  economy state and tax controls: `getTreasuryBalance`, `getCurrentMonthlyRevenue`,
  `getOutstandingDebt`, `estimateMonthlyUpkeep`, `setTaxRate`, `getTaxRate`,
  `getTaxRevenue`, `getWagesCost`, `getRoadMaintenanceCost`,
  `getServiceUpkeepCost`, `getUtilityFeeRevenue`, `getOutstandingBondUses`.

  **`IZoningActions`** (`src/interfaces/IZoningActions.h`) -- 9 methods covering
  placement mutations, tile queries, and undo: `placeZone`, `placeRoad`,
  `demolishTile`, `undoLastAction`, `placeServiceBuilding`, `queryTile`,
  `isWithinRoadRange`, `hasUndoPendingAction`, `getUndoExpiryTimeSeconds`.

  **`ISimulationState`** (`src/interfaces/ISimulationState.h`) -- 15 methods
  covering per-frame state for rendering, audio, and population/progression
  queries: `getAgentPositions`, `getIntersectionSignalStates`,
  `getRoadSegmentSpeeds`, `getServiceCoverage`, `getMapTilesX`, `getMapTilesZ`,
  `getSimulationTime`, `getTimeOfDay`, `getNextUnlockThreshold`,
  `getZoneDemandFactor`, `getTrafficDemandFactor`, `getTotalPopulation`,
  `getCityRating`, `getConsecutiveDeficitMonths`, `getDensityUnlockState`.

  ```cpp
  // src/interfaces/ICitySimulation.h
  // SpeedMultiplier is the canonical enum defined in src/interfaces/simulation_types.h.
  // ICitySimulation.h must #include "simulation_types.h" to get SpeedMultiplier, ZoneType, and Difficulty
  // as complete types — forward declarations are insufficient for by-value parameters.
  // NOTE: The canonical Difficulty enumerator names are Easy, Normal, Hard (PascalCase),
  // matching Phase 0's src/interfaces/simulation_types.h. Do NOT use EASY, NORMAL, HARD (all-caps) —
  // those names do not exist and will cause a compile error.
  #include "simulation_types.h"
  #include "ISimulationPauser.h"
  #include "IEconomyQuery.h"
  #include "IZoningActions.h"
  #include "ISimulationState.h"

  // BudgetDeficitWarn and SimulationNotification are defined in src/interfaces/simulation_types.h
  // (already included above):
  //   struct BudgetDeficitWarn {};
  //   using SimulationNotification = std::variant<std::monostate, BudgetDeficitWarn, ...>;

  // ICitySimulation extends ISimulationPauser and three focused sub-interfaces.
  // The full 44-method set is reachable via inheritance; ICitySimulation declares
  // only 7 own methods. All callers that hold ICitySimulation* are untouched.
  // Total method breakdown: 12 (IEconomyQuery) + 9 (IZoningActions) +
  //   15 (ISimulationState) + 7 (own) + 1 (ISimulationPauser) = 44.
  class ICitySimulation
      : public ISimulationPauser   // setPaused(bool) inherited
      , public IEconomyQuery       // 12 economy state + tax control methods
      , public IZoningActions      // 9 placement mutation, tile query, undo methods
      , public ISimulationState {  // 15 per-frame state, population, progression methods
  public:
      virtual ~ICitySimulation() = default;
      // Speed / pause control
      virtual void            setSpeed(SpeedMultiplier speed) = 0;
      virtual bool            isPaused()             const = 0;
      virtual SpeedMultiplier getSpeedMultiplier()   const = 0;
      // Lifecycle
      virtual void   reset(int64_t startingFunds)               = 0;
      virtual bool   applyLoadedJson(const std::string& json)   = 0;
      // Event queue — returns true and fills `out` if a notification is pending;
      // returns false if the queue is empty.
      //
      // BudgetDeficitWarn semantics: enqueued by CitySimulation once per budget tick when
      // budget_surplus_pct <= -0.25 (the forced-loan warning threshold at -25% deficit; see
      // architecture/game-design/economy-model.md). It is NOT enqueued for minor deficits
      // where budget_surplus_pct > -0.25 (deficits less severe than 25%). This ensures the
      // event is only fired as part of the bankruptcy-warning system, not for everyday
      // budget imbalances.
      //
      // UIManager reads getConsecutiveDeficitMonths() via DIRECT POLLING each frame in UIManager::update()
      // (NOT triggered by BudgetDeficitWarn receipt). UIManager tracks m_lastDeficitMonths; the CRISIS
      // stinger fires when currentMonths == 2 AND m_lastDeficitMonths < 2. BudgetDeficitWarn receipt
      // determines which warning toast to dispatch only.
      virtual bool   pollPendingNotification(SimulationNotification& out) = 0;
      // Simulation timing
      virtual int    consumeBudgetTicks() = 0;
  };
  ```

  ```cpp
  // tests/ui/MockCitySimulation.h
  #include "gmock/gmock.h"
  #include "src/interfaces/ICitySimulation.h"

  class MockCitySimulation : public ICitySimulation {
  public:
      // ISimulationPauser (1 method):
      MOCK_METHOD(void, setPaused, (bool paused), (override));

      // ICitySimulation own methods (7):
      MOCK_METHOD(void, setSpeed, (SpeedMultiplier speed), (override));
      MOCK_METHOD(bool, isPaused, (), (const, override));
      MOCK_METHOD(SpeedMultiplier, getSpeedMultiplier, (), (const, override));
      MOCK_METHOD(void, reset, (int64_t startingFunds), (override));
      MOCK_METHOD(bool, applyLoadedJson, (const std::string& json), (override));
      MOCK_METHOD(bool, pollPendingNotification, (SimulationNotification& out), (override));
      MOCK_METHOD(int, consumeBudgetTicks, (), (override));

      // IEconomyQuery (12 methods):
      MOCK_METHOD(float, getTreasuryBalance, (), (const, override));
      MOCK_METHOD(float, getCurrentMonthlyRevenue, (), (const, override));
      MOCK_METHOD(float, getOutstandingDebt, (), (const, override));
      MOCK_METHOD(float, estimateMonthlyUpkeep, (), (const, override));
      MOCK_METHOD(void, setTaxRate, (ZoneType zone, float rate), (override));
      MOCK_METHOD(float, getTaxRate, (ZoneType zone), (const, override));
      MOCK_METHOD(float, getTaxRevenue, (ZoneType zone), (const, override));
      MOCK_METHOD(float, getWagesCost, (), (const, override));
      MOCK_METHOD(float, getRoadMaintenanceCost, (), (const, override));
      MOCK_METHOD(float, getServiceUpkeepCost, (), (const, override));
      MOCK_METHOD(float, getUtilityFeeRevenue, (), (const, override));
      MOCK_METHOD(int, getOutstandingBondUses, (), (const, override));

      // IZoningActions (9 methods):
      MOCK_METHOD(void, placeZone, (int x, int z, ZoneType t, DensityTier tier, int earthworksCost), (override));
      MOCK_METHOD(void, placeRoad, (int x, int z, int earthworksCost), (override));
      MOCK_METHOD(void, demolishTile, (int x, int z), (override));
      MOCK_METHOD(void, undoLastAction, (), (override));
      MOCK_METHOD(void, placeServiceBuilding, (int x, int z, ServiceBuildingType type, int earthworksCost), (override));
      MOCK_METHOD(QueryResult, queryTile, (int x, int z), (const, override));
      MOCK_METHOD(bool, isWithinRoadRange, (int x, int z, DensityTier tier), (const, override));
      MOCK_METHOD(bool, hasUndoPendingAction, (), (const, override));
      MOCK_METHOD(double, getUndoExpiryTimeSeconds, (), (const, override));

      // ISimulationState (15 methods):
      MOCK_METHOD((std::vector<AgentState>), getAgentPositions, (), (const, override));
      MOCK_METHOD((std::vector<IntersectionSignalState>), getIntersectionSignalStates, (), (const, override));
      MOCK_METHOD((std::vector<RoadSegmentSpeed>), getRoadSegmentSpeeds, (), (const, override));
      MOCK_METHOD((std::vector<ServiceCoverageTile>), getServiceCoverage, (), (const, override));
      MOCK_METHOD(int, getMapTilesX, (), (const, override));
      MOCK_METHOD(int, getMapTilesZ, (), (const, override));
      MOCK_METHOD(SimulationTime, getSimulationTime, (), (const, override));
      MOCK_METHOD(TimeOfDay, getTimeOfDay, (), (const, override));
      MOCK_METHOD(float, getNextUnlockThreshold, (Difficulty d), (const, override));
      MOCK_METHOD(float, getZoneDemandFactor, (ZoneType zone), (const, override));
      MOCK_METHOD(float, getTrafficDemandFactor, (ZoneType zone), (const, override));
      MOCK_METHOD(int, getTotalPopulation, (), (const, override));
      MOCK_METHOD(CityRatingTier, getCityRating, (), (const, override));
      MOCK_METHOD(int, getConsecutiveDeficitMonths, (), (const, override));
      MOCK_METHOD(DensityUnlockState, getDensityUnlockState, (), (const, override));
  };
  ```

  `UIManager` constructor accepts `ICitySimulation*` as a non-owning pointer. The full constructor signature is `UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)`. The `clock` parameter is passed to `NotificationManager` at construction (for dismiss-after-5s timing) and to the `HUD` component for grace-period and undo-countdown displays. In the `UIManagerModalTest` fixture, `sim_` is declared as `NiceMock<MockCitySimulation>` and passed to `UIManager` at construction: `UIManager(&backend_, &audio_, &sim_, &clock_)`. In tests that assert specific pause/resume call counts (modal tests 1, 3, 8, 10), `EXPECT_CALL(sim_, setPaused(...))` is set up on the `NiceMock<MockCitySimulation>` before the action under test. NiceMock is used rather than StrictMock here because the focus of these tests is on `MockUIBackend` call patterns; simulation pause/resume expectations are set selectively per test rather than requiring every possible call to be declared up front.

- **Canonical UI class constructor parameter order**: ALL UI panel and manager classes MUST accept their injected dependencies in the following fixed order:
  `(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)`
  - This order is authoritative for every present and future UI panel class: `UIManager`, `NotificationManager`, `HUD`, `FinancesPanel`, `InspectorPanel`, `SettingsPanel`, and any panel added in later phases.
  - Maintaining a consistent order allows test fixtures to reuse the same member declaration block (`MockUIBackend backend_; NiceMock<MockAudioSystem> audio_; NiceMock<MockCitySimulation> sim_; ManualClock clock_;`) without reordering per panel.
  - Confirmed examples: `UIManager(IUIBackend*, IAudioSystem*, ICitySimulation*, IClock*)` (Phase 3+), `NotificationManager` receives `IClock*` at construction forwarded from UIManager, `FinancesPanel` (Phase 11m) follows the same order.
  - Classes that do not need all four dependencies MUST still respect left-to-right subset ordering (e.g., a panel that needs only `IUIBackend*` and `IClock*` must NOT reorder them to `IClock*` first).

- **Thread-safety annotations**: Use Clang's thread-safety analysis attributes (`GUARDED_BY`, `REQUIRES`, `EXCLUDES`) on Clang builds; document-only `// thread-safe` or `// main-thread-only` comments as fallback on MSVC. Enable `-Wthread-safety` in CMake for Clang builds.

- **`vec3` type alias** — lightweight 3-component float vector used across all simulation and audio interfaces to avoid pulling Irrlicht headers into test translation units.

  **Spec entry**:

  ```text
  Header: src/interfaces/vec3.h
  Definition:
    struct vec3 { float x, y, z; };
    // Does NOT include any Irrlicht headers.
    // IAudioSystem.h, ISpatialAudio.h, and all simulation interfaces use vec3.
    // IrrlichtRenderer converts vec3 <-> irr::core::vector3df at the boundary.
  ```

  Cross-references: `architecture/audio-architecture/audio-system.md` (vec3 used in `playPositionalSound` and `syncListenerToCamera` — canonical header is `src/interfaces/vec3.h`); `architecture/audio-architecture/spatial-audio.md` (vec3 used for `CameraState` fields — canonical header is `src/interfaces/vec3.h`); `architecture/game-design/traffic-system.md` (vec3 used in positional sound call sites — canonical header is `src/interfaces/vec3.h`).

## Interface Definitions (minimum required method signatures)

```cpp
using TextureHandle = uint32_t;
static constexpr TextureHandle kInvalidTexture = 0;

// CameraParams — passed to IRenderer::setCamera() each frame.
// Defined in IRenderer.h (alongside IRenderer) since it is only used as a parameter to IRenderer.
// Not shared with IAudioSystem — that interface uses CameraState (position/forward/up vectors)
// for 3D spatial audio listener placement, which differs from the renderer's FOV/clip-plane needs.
struct CameraParams {
    vec3  position{};          // world-space camera eye position
    vec3  target{};            // world-space look-at target (NOT a direction vector)
    float fovDegrees{45.0f};   // horizontal field of view in degrees
    float nearClip{0.1f};      // near clip plane distance in metres
    float farClip{3000.0f};    // far clip plane distance in metres (covers 1024×1024 map + sky)
};

// ScreenRect — plain-old-data screen bounding rectangle in physical pixels.
// Defined in IRenderer.h alongside IRenderer to keep IRenderer.h Irrlicht-free.
// Do NOT use irr::core::rect<irr::s32> at any call site crossing the IRenderer boundary.
struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; };

class IRenderer {   // main-thread-only
public:
    virtual ~IRenderer() = default;
    virtual void          beginFrame() = 0;
    virtual void          endFrame() = 0;
    virtual void          drawScene() = 0;
    virtual TextureHandle loadTexture(const std::string& path) = 0;
    virtual void          setCamera(const CameraParams& p) = 0;
    virtual void          rebuildTerrainChunk(const TerrainChunkRebuildParams& params) = 0;

    // Phase 9b additions — world interaction:
    virtual bool          pickTerrainTile(int screenX, int screenY,
                                          int& tileX, int& tileZ) const = 0;
    virtual void          setTileHoverHighlight(int tileX, int tileZ, int footprintSize = 1) = 0;
    virtual void          setZoneOverlay(int mapTilesX, int mapTilesZ,
                                         const std::unordered_map<uint64_t, uint32_t>& sparseOverlay) = 0;
    virtual ScreenRect    getTileScreenBounds(int tileX, int tileZ) const = 0;

    // Phase 10 addition — listener position query for pre-acquisition distance culls.
    // Returns the current camera/listener position in world space as a vec3.
    // Used by Traffic::doTrafficSignalTick() (called from CitySimulation::tick()) to perform
    // the 80 m pre-acquisition distance cull for sfx_intersection_tick before calling
    // IAudioSystem::playPositionalSound().
    // IrrlichtRenderer::getListenerPosition() returns the position component of the last
    // CameraParams passed to setCamera(). MockRenderer::getListenerPosition() returns
    // vec3{0,0,0} by default (suitable for distance-cull tests that set a specific value
    // via ON_CALL).
    virtual vec3          getListenerPosition() const = 0;

    // Phase 10 additions — building mesh spawning and road mesh rendering.
    // Called by CitySimulation after successful placeZone/placeRoad/placeServiceBuilding/demolishTile.
    // IrrlichtRenderer implementation: loads .b3d via BuildingAssetLoader, creates scene node via
    // SceneEntityManager::spawnBuilding(), registers with LODNode system.
    // No-op on empty assetBaseName or missing .b3d file (log warning, do not assert).
    // MockRenderer: MOCK_METHOD stubs; no default ON_CALL action (returns void).
    // See phase-10.md City Rendering deliverables section for full wiring contract.
    virtual void          placeBuildingMesh(int tileX, int tileZ, const std::string& assetBaseName) = 0;
    virtual void          removeBuildingMesh(int tileX, int tileZ) = 0;
    virtual void          placeRoadMesh(int tileX, int tileZ) = 0;
    virtual void          removeRoadMesh(int tileX, int tileZ) = 0;
    // type is ServiceBuildingType enum value: PowerPlant=0, WaterTower=1, FireStation=2, PoliceStation=3.
    // Asset path: assets/3d/buildings/svc_<type>_lod0.b3d per 3d-model-standards.md naming convention.
    virtual void          placeServiceBuildingMesh(int tileX, int tileZ, ServiceBuildingType type) = 0;
    virtual void          removeServiceBuildingMesh(int tileX, int tileZ) = 0;

    // Phase 10 — Vehicle rendering API (pre-agent, manually-placed vehicles).
    virtual void          placeVehicle(uint32_t vehicleId, const std::string& assetName,
                                       float worldX, float worldY, float worldZ, float yawDegrees) = 0;
    virtual void          moveVehicle(uint32_t vehicleId,
                                      float worldX, float worldY, float worldZ, float yawDegrees) = 0;
    virtual void          removeVehicle(uint32_t vehicleId) = 0;

    // Phase 10 — Multi-tile placement preview.
    // Phase 11d extends to two-list signature: freeTiles (green) + blockedTiles (red).
    // blockedTiles defaults to {} at call sites but MockRenderer requires the full signature.
    virtual void          setTilePlacementPreview(const std::vector<std::pair<int,int>>& freeTiles,
                                                   uint32_t freeArgb,
                                                   const std::vector<std::pair<int,int>>& blockedTiles) = 0;

    // Phase 11d — Traffic agent rendering API.
    // These coexist with Phase 10 placeVehicle/moveVehicle/removeVehicle; both sets must be present.
    // AgentHandle is defined in src/interfaces/simulation_types.h (not in IRenderer.h) to avoid ODR violations.
    virtual void          spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ, ZoneType zone) = 0;
    virtual void          moveVehicleAgent(AgentHandle handle, int tileX, int tileZ, float headingDeg) = 0;
    virtual void          despawnVehicleAgent(AgentHandle handle) = 0;

    // Phase 11d — Traffic signal visual state.
    // SignalPhase is defined in src/interfaces/simulation_types.h: enum class SignalPhase { Green, Red };
    virtual void          setIntersectionSignalState(int tileX, int tileZ, SignalPhase phase) = 0;

    // Phase 11d — Service coverage radius overlay.
    // showServiceCoverageOverlay: renders wireframe circle (or BFS tile highlight for PowerPlant)
    //   at the building's coverage radius. degraded=true halves the radius.
    // hideServiceCoverageOverlay: removes the overlay. Called on Inspector close.
    virtual void          showServiceCoverageOverlay(int tileX, int tileZ,
                                                      ServiceBuildingType type, bool degraded) = 0;
    virtual void          hideServiceCoverageOverlay() = 0;
};

// MockRenderer — GMock implementation of IRenderer (27 methods as of Phase 11d).
// Method count: 5 base (beginFrame/endFrame/drawScene/loadTexture/setCamera) +
//   1 (rebuildTerrainChunk) + 4 Phase 9b (pickTerrainTile/setTileHoverHighlight/setZoneOverlay/
//   getTileScreenBounds) + 1 Phase 10 (getListenerPosition) +
//   6 Phase 10 rendering (placeBuildingMesh/removeBuildingMesh/placeRoadMesh/removeRoadMesh/
//   placeServiceBuildingMesh/removeServiceBuildingMesh) +
//   3 Phase 10 vehicle (placeVehicle/moveVehicle/removeVehicle) +
//   1 Phase 10 preview (setTilePlacementPreview — Phase 11d extended to two-list signature) +
//   6 Phase 11d (spawnVehicleAgent/moveVehicleAgent/despawnVehicleAgent/
//   setIntersectionSignalState/showServiceCoverageOverlay/hideServiceCoverageOverlay) = 27 total.
// Note: getListenerPosition was added in Phase 10 context (already counted above) —
//   Phase 11d Deliverable 0 originally listed it as one of 7; it was pre-landed in Phase 10.
//
// Phase 11d additions (6 new MOCK_METHOD stubs — must be present in MockRenderer after
// Deliverable 0 Day-One Commit; prerequisite for Deliverables 3d and 4c test authoring):
//   spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ, ZoneType zone) → void
//   moveVehicleAgent(AgentHandle handle, int tileX, int tileZ, float headingDeg) → void
//   despawnVehicleAgent(AgentHandle handle) → void
//   setIntersectionSignalState(int tileX, int tileZ, SignalPhase phase) → void
//   showServiceCoverageOverlay(int tileX, int tileZ, ServiceBuildingType type, bool degraded) → void
//   hideServiceCoverageOverlay() → void
// Note: spawnVehicleAgent/moveVehicleAgent/despawnVehicleAgent coexist with Phase 10's
//   placeVehicle/moveVehicle/removeVehicle — both sets of methods must be present.
// Note: setTilePlacementPreview extended from (tiles, argb) to (freeTiles, freeArgb, blockedTiles)
//   in Phase 11d Deliverable 5d. blockedTiles defaults to {} at call sites but MockMethod
//   requires the full three-parameter signature.
// Source location: tests/simulation/MockRenderer.h
// Shared across simulation_tests, ui_tests (via tests/simulation/ include path).
//
// Default ON_CALL actions (set in MockRenderer constructor):
//   loadTexture               — returns incrementing non-zero integer
//   pickTerrainTile           — returns false (no terrain hit; tileX/tileZ unchanged)
//   getTileScreenBounds       — returns ScreenRect{} (zero-initialised)
//   getListenerPosition       — returns vec3{0.0f, 0.0f, 0.0f} (origin; override per-test for
//                               distance-cull scenarios via ON_CALL(renderer_, getListenerPosition())
//                               .WillByDefault(Return(vec3{x, y, z})))
//   placeBuildingMesh         — no default action (void return; NiceMock ignores; StrictMock
//   removeBuildingMesh          requires explicit EXPECT_CALL for each call site exercised by test)
//   placeRoadMesh             — no default action (same rule as placeBuildingMesh)
//   removeRoadMesh            — no default action
//   placeServiceBuildingMesh  — no default action
//   removeServiceBuildingMesh — no default action
//   spawnVehicleAgent         — no return value (void)
//   moveVehicleAgent          — no default action (void return)
//   despawnVehicleAgent       — no default action (void return)
//   setIntersectionSignalState — no default action (void return)
//   showServiceCoverageOverlay — no default action (void return)
//   hideServiceCoverageOverlay — no default action (void return)
//   setTilePlacementPreview   — no default action (void return; three-parameter form since Phase 11d)
//
// IMPORTANT — CitySimulationUnitTest uses StrictMock<MockRenderer>. After Phase 10 wiring,
// any test that exercises placeZone(), placeRoad(), placeServiceBuilding(), demolishTile(),
// or doDensityUnlockTick() will receive calls to the 6 rendering methods above.
// Each such test MUST declare EXPECT_CALL stubs for every rendering method call triggered
// by the code path under test. Alternatively, switch the renderer_ fixture member to
// NiceMock<MockRenderer> for tests where rendering method calls are incidental (not the
// focus of the assertion). See StrictMock Expected Call Matrix below for canonical patterns.
//
// Phase 9b stub usage in WorldInteractionTest:
//   EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
//       .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
//   EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));
//   EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).WillOnce(SaveArg<2>(&capturedMap));
//
// IMPORTANT — StrictMock in WorldInteractionTest (all non-mock-configured IRenderer methods):
// WorldInteractionTest uses StrictMock<MockRenderer>. EVERY IRenderer call made by the
// production code under test must be covered by an EXPECT_CALL or ON_CALL. The methods
// beginFrame, endFrame, drawScene, setCamera, rebuildTerrainChunk are NOT called during
// UIManager::onEvent() or UIManager::update() — they are render-pass methods called from
// the main game loop outside UIManager. Tests that construct a real UIManager and send
// events do NOT need EXPECT_CALL stubs for these five methods.
//
// The four Phase 9b methods (pickTerrainTile, setTileHoverHighlight, setZoneOverlay,
// getTileScreenBounds) ARE called from UIManager::onEvent() during tests. Each test
// must declare EXPECT_CALL for each of these that the production code exercises.
//
// The Phase 11d method getListenerPosition() IS called from Traffic::doTrafficSignalTick()
// (called from CitySimulation::tick()) during the sfx_intersection_tick 80 m pre-cull.
// Simulation tests that exercise tick() with traffic signals must either set ON_CALL for
// getListenerPosition() or use NiceMock.

// Canonical IAudioSystem — 19 methods (Phase 10 added setMusicIntensity; Phase 11d added acquireVehicleEnginePair, releaseVehicleEnginePair, updateVehicleAudio; Phase 11m added transitionToMainMenu). Authoritative definition in audio-architecture/audio-system.md.
// Uses only game-domain types (SoundId, SoundHandle, MusicTrackId, StingerType, SimSpeed,
// SoundPriority, TimeOfDay, vec3, CameraState). Never expose ALuint, ALfloat, or AL_* constants through this interface.
// Forward declarations (defined in game-domain headers, not OpenAL headers):
//   struct vec3;              // 3-component float vector (X, Y, Z)
//   struct CameraState;       // position (vec3), forward (vec3), up (vec3)
//   // NOTE: SimSpeed is a type alias (using SimSpeed = SpeedMultiplier) — DO NOT forward-declare
//   //   as "enum class SimSpeed;" (type aliases cannot be forward-declared; will cause compile error).
//   //   IAudioSystem.h must #include "simulation_types.h" instead.
//   // NOTE: ZoneType (enum class: Residential, Commercial, Industrial) is also from src/interfaces/simulation_types.h
//   //   (added Phase 11d for acquireVehicleEnginePair). Since src/interfaces/simulation_types.h is already included
//   //   for SimSpeed, ZoneType is automatically available — do NOT add a separate forward declaration.
//   enum class StingerType;
//   enum class SoundPriority; // LOW=0, NORMAL=1, HIGH=2, CRITICAL=3 — controls SFX pool eviction
//   enum class TimeOfDay;     // DAY, DUSK, NIGHT, DAWN — drives ambient bed and music intensity
//   using SoundId      = uint32_t;
//   using MusicTrackId = uint32_t;
//   using SoundHandle  = uint32_t;  // opaque handle returned by playSound / playPositionalSound
class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    // Play a non-positional (2D) one-shot sound.  priority controls SFX pool eviction.
    // gain is a linear multiplier [0.0, 1.0].
    // Returns an opaque SoundHandle that can be passed to stopSound().
    virtual SoundHandle playSound(SoundId id, SoundPriority priority, float gain = 1.0f) = 0;

    // Play a world-positioned (3D) one-shot sound at pos.  priority controls SFX pool eviction.
    // Returns an opaque SoundHandle that can be passed to stopSound().
    virtual SoundHandle playPositionalSound(SoundId id, vec3 pos, SoundPriority priority, float gain = 1.0f) = 0;

    // Stop a previously-started sound identified by handle.
    // Silently ignored if the handle is stale (source already finished or recycled).
    virtual void stopSound(SoundHandle handle) = 0;

    // Begin streaming the specified music track (with beat-boundary crossfade from current track).
    virtual void setMusicTrack(MusicTrackId id) = 0;

    // Notify the audio system of the current simulation speed so that
    // time-of-day audio transitions can be collapsed at high speed.
    virtual void setSpeed(SimSpeed speed) = 0;

    // Fire a one-shot stinger of the given type.  Subject to the 5 s minimum
    // between same-type triggers and the drop-if-already-playing rule.
    virtual void triggerStinger(StingerType type) = 0;

    // Synchronise the OpenAL listener position and orientation to the current camera.
    // Must be called once per frame from the main thread after Irrlicht updates the camera.
    virtual void syncListenerToCamera(const CameraState& cam) = 0;

    // Activate game-over audio fade sequence (post-V1 Scenario mode only).
    // Sets m_gameOverFade = true; audio thread fades all stems over 2 s then stops them.
    virtual void setGameOverState(bool active) = 0;

    // Notify the audio system that the in-game clock has crossed a time-of-day boundary.
    // Called by CitySimulation whenever the simulated hour transitions between DAY/DUSK/NIGHT/DAWN.
    // Also called at game start to establish the initial ambient bed before the first frame.
    virtual void setTimeOfDay(TimeOfDay tod) = 0;

    // Transition from main menu audio to gameplay audio.
    // Called by UIManager when the player starts a new game or loads a saved game.
    // Must not be called while gameplay audio is already active (undefined behaviour).
    virtual void transitionToGameplay() = 0;

    // Per-frame update called from the main game loop.
    // realDeltaSeconds is wall-clock elapsed time since the previous call.
    // Responsibilities: advance occlusion raycast budget, push time-of-day transitions,
    // and forward any pending crossfade or zone-layer source updates.
    virtual void update(float realDeltaSeconds) = 0;

    // Volume control — see audio-system.md Volume Control API section.
    // Phase 8 creates the Settings > Audio slider UI elements; Phase 9 wires them to AudioSystem.
    virtual void setMasterVolume(float gain) = 0;
    virtual void setMusicVolume(float gain) = 0;
    virtual void setSFXVolume(float gain) = 0;

    // Set the music intensity tier driven by live simulation state (Phase 10).
    // Threshold conditions: CALM (budget_surplus_pct >= 0%), GROWTH (net pop positive),
    // CRISIS (consecutive_deficit_months >= 2). Priority: CRISIS > GROWTH > CALM.
    virtual void setMusicIntensity(MusicIntensity intensity) = 0;

    // Phase 11d — vehicle engine audio pair management. Returns source-pool indices for
    // idle and move engine loops assigned to one traffic agent. ZoneType selects the
    // vehicle subtype (Residential → car 1.0×, Commercial → bus 0.85×, Industrial → truck 0.85×).
    virtual std::pair<int,int> acquireVehicleEnginePair(ZoneType zone) = 0;
    virtual void releaseVehicleEnginePair(int idleIdx, int moveIdx) = 0;
    // Per-frame audio state push (Phase 11d). Called by main.cpp for each active vehicle
    // after getAgentPositions() and before drawScene(). AudioSystem applies crossblend,
    // pitch, and AL_POSITION on the audio thread.
    virtual void updateVehicleAudio(int idleIdx, int moveIdx,
                                    float speedFraction,
                                    float worldX, float worldZ) = 0;
};

// MockAudioSystem — GMock implementation of IAudioSystem's 19 methods.
// Source location: tests/simulation/MockAudioSystem.h
// Shared across simulation_tests, ui_tests, audio_tests CMake targets (header-only).
class MockAudioSystem : public IAudioSystem {
public:
    MOCK_METHOD(SoundHandle, playSound,             (SoundId id, SoundPriority priority, float gain), (override));
    MOCK_METHOD(SoundHandle, playPositionalSound,   (SoundId id, vec3 pos, SoundPriority priority, float gain), (override));
    MOCK_METHOD(void,        stopSound,             (SoundHandle handle),                        (override));
    MOCK_METHOD(void,        setMusicTrack,         (MusicTrackId id),                           (override));
    MOCK_METHOD(void,        setSpeed,              (SimSpeed speed),                            (override));
    MOCK_METHOD(void,        triggerStinger,        (StingerType type),                          (override));
    MOCK_METHOD(void,        syncListenerToCamera,  (const CameraState& cam),                   (override));
    MOCK_METHOD(void,        setGameOverState,      (bool active),                               (override));
    MOCK_METHOD(void,        setTimeOfDay,          (TimeOfDay tod),                             (override));
    MOCK_METHOD(void,        transitionToGameplay,  (),                                          (override));
    MOCK_METHOD(void,        update,                (float realDeltaSeconds),                    (override));
    MOCK_METHOD(void,        setMasterVolume,       (float gain),                                (override));
    MOCK_METHOD(void,        setMusicVolume,        (float gain),                                (override));
    MOCK_METHOD(void,        setSFXVolume,          (float gain),                                (override));
    MOCK_METHOD(void,        setMusicIntensity,     (MusicIntensity intensity),                   (override));
    MOCK_METHOD((std::pair<int,int>), acquireVehicleEnginePair, (ZoneType zone),                 (override));
    MOCK_METHOD(void,        releaseVehicleEnginePair, (int idleIdx, int moveIdx),               (override));
    MOCK_METHOD(void,        updateVehicleAudio, (int, int, float, float, float),                (override));
};
```

**`audio_tests` smoke test list** — the following test cases must be present in `tests/audio/duck_state_test.cpp` (or a dedicated `tests/audio/audio_smoke_test.cpp`) and registered under the `audio_tests` CMake target (label `unit`):

- `MockAudioSystem_InstantiatesCleanly`: verifies vtable completeness of `MockAudioSystem` — no pure-virtual override can be silently missing. Test body:

  ```cpp
  TEST(AudioSmokeTest, MockAudioSystem_InstantiatesCleanly) {
      NiceMock<MockAudioSystem> mock;
      SUCCEED();
  }
  ```

  `NiceMock` is used here (not `StrictMock`) because the test purpose is construction/destruction only; no calls are expected and no suppression of warnings is needed. If any virtual method is absent from `MockAudioSystem`, instantiation fails at link time — the `SUCCEED()` body is intentionally trivial to isolate the failure to the vtable rather than test logic.

  **Canonical name note**: The exact test suite name `AudioSmokeTest` and case name `MockAudioSystem_InstantiatesCleanly` are canonical. Phase 3 deliverable and Phase 3 exit criteria must reference this exact GTest name. CTest filter: `-R MockAudioSystem_InstantiatesCleanly`. Do not use `TEST(MockAudioSmoke, Instantiates)` or any other form — name drift causes CI `-R` filter expressions to silently match nothing.

**Phase 7 `audio_tests` canonical test names** — the following canonical names are mandated for the Phase 7 audio unit tests added via `target_sources(audio_tests ...)`. CTest `-R` filters reference these names exactly; name drift causes filter expressions to silently match nothing.

| Test Suite                | Test Case                                    | Source File                                  |
| ------------------------- | -------------------------------------------- | -------------------------------------------- |
| `DuckStateMachineTest`    | `IdleToReleasing_CompletesInCorrectDuration` | `tests/audio/duck_state_machine_test.cpp`    |
| `DuckStateMachineTest`    | `InterruptedRelease_RampsFromCurrentGain`    | `tests/audio/duck_state_machine_test.cpp`    |
| `DuckStateMachineTest`    | `UsesWallClockDt_NotFixedIncrement`          | `tests/audio/duck_state_machine_test.cpp`    |
| `DuckStateMachineTest`    | `FirstWake_DtIsNotEpochSized`                | `tests/audio/duck_state_machine_test.cpp`    |
| `OcclusionSmoothingTest`  | `RecycledSlot_ResetsGainToOne`               | `tests/audio/occlusion_smoothing_test.cpp`   |
| `AudioThreadTest`         | `AbsentThreadLocalContext_ConstructorThrows` | `tests/audio/audio_thread_test.cpp`          |
| `OggHeaderValidationTest` | `ValidPlaceholder_ReturnsZero`               | `tests/audio/ogg_header_validation_test.cpp` |
| `OggHeaderValidationTest` | `MusicStem_IsStereo_44100Hz`                 | `tests/audio/ogg_header_validation_test.cpp` |
| `OggHeaderValidationTest` | `ZoneLoop_IsMono_44100Hz`                    | `tests/audio/ogg_header_validation_test.cpp` |

All Phase 7 audio tests carry label `unit` and run without a display device. CTest filter for all Phase 7 audio tests: `-R "DuckStateMachineTest|OcclusionSmoothingTest|AudioThreadTest|OggHeaderValidationTest"`.

**Phase 10 `audio_tests` canonical test names** — the following canonical names are mandated for the Phase 10 audio unit tests added via `target_sources(audio_tests ...)`. CTest `-R` filters reference these names exactly; name drift causes filter expressions to silently match nothing.

| Test Suite            | Test Case                                                      | Source File                                          |
| --------------------- | -------------------------------------------------------------- | ---------------------------------------------------- |
| `CrossfadeTest`       | `Crossfade_InterruptedFormula_NoDomainErrorAtBoundary`         | `tests/audio/crossfade_interrupted_formula_test.cpp` |
| `StingerTest`         | `StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation` | `tests/audio/stinger_milestone_test.cpp`             |
| `AudioStreamTest`     | `AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake`   | `tests/audio/audio_stream_bar_boundary_test.cpp`     |
| `AudioStreamTest`     | `AudioStream_BarBoundary_StreamStart_NoFalseFire`              | `tests/audio/audio_stream_bar_boundary_test.cpp`     |
| `NotificationSFXTest` | `NotificationSFX_EFXBypass_DirectFilterSetToNull`              | `tests/audio/notification_sfx_efx_bypass_test.cpp`   |

All Phase 10 audio tests carry label `unit` and run without a display device or real audio device (headless CI via `IAlcFunctions` seam and `MockAudioSystem`). CTest filter for all Phase 10 audio tests: `-R "CrossfadeTest|StingerTest|AudioStreamTest|NotificationSFXTest"`.

**Phase 10 `ui_tests` canonical test names** — the following canonical names are mandated for the Phase 10 UI unit tests. These extend the existing `notification_system_test.cpp` and are registered under the `ui_tests` CMake target (label `unit`). CTest `-R` filters reference these names exactly.

| Test Suite                | Test Case                                        | Source File                             |
| ------------------------- | ------------------------------------------------ | --------------------------------------- |
| `NotificationManagerTest` | `NotificationSFX_ToastVisible_UIToastSoundFires` | `tests/ui/notification_system_test.cpp` |

**`NotificationSFX_ToastVisible_UIToastSoundFires` test contract** — fixture setup: construct `NotificationManager` with `NiceMock<MockUIBackend>` + `NiceMock<MockCitySimulation>` + `ManualClock` + `NiceMock<MockAudioSystem>` (the fourth argument added in Phase 10). Place `EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)).Times(1)` BEFORE posting any toast. Post a Normal toast via `postNormal()`. Advance `ManualClock` if needed for the toast to become visible (e.g. if the queue was at capacity). Verify the expectation — `UI_TOAST` must fire exactly once when the toast transitions from queued to visible, NOT at enqueue time when the queue is at capacity. **Negative assertion variant**: post a second Normal toast while the first toast occupies the only available visible slot (Normal max-visible = 1 when 2 CRITICAL toasts are visible); verify `playSound(UI_TOAST, ...)` is NOT called until the slot opens. **Mock policy**: `NiceMock<MockAudioSystem>` (not `StrictMock`) because the `NotificationManager` constructor calls no audio methods at construction time, and the test focus is on the toast-visible trigger, not on suppressing all unexpected calls. **CTest filter**: `-R NotificationSFX_ToastVisible_UIToastSoundFires`.

**Phase 10 `simulation_tests` canonical test names** — the following canonical names are mandated for the Phase 10 simulation unit tests. These are added to `simulation_tests` via `target_sources(simulation_tests ...)`.

| Test Suite                   | Test Case                                                         | Source File                                          |
| ---------------------------- | ----------------------------------------------------------------- | ---------------------------------------------------- |
| `AdaptiveMusicIntensityTest` | `AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem`           | `tests/simulation/adaptive_music_intensity_test.cpp` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh`           | `tests/simulation/city_simulation_render_test.cpp`   |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_PlaceRoad_PlacesRoadMesh`               | `tests/simulation/city_simulation_render_test.cpp`   |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_PlaceServiceBuilding_PlacesServiceMesh` | `tests/simulation/city_simulation_render_test.cpp`   |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_DemolishZone_RemovesBuildingMesh`       | `tests/simulation/city_simulation_render_test.cpp`   |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_DemolishRoad_RemovesRoadMesh`           | `tests/simulation/city_simulation_render_test.cpp`   |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh`       | `tests/simulation/city_simulation_render_test.cpp`   |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit`        | `tests/simulation/city_simulation_render_test.cpp`   |

`CitySimulationRenderTest` fixture uses `NiceMock<MockSimRenderer>` (test-local standalone interface) and `NiceMock<MockMusicIntensityReceiver>`, driven by `CitySimulationRenderStub`. Does NOT use `MockRenderer`/`CitySimulation` — the stub models the render-dispatch protocol without the full simulation dependency chain. All `CitySimulationRenderTest` cases carry label `unit`. CTest filter: `-R CitySimulationRenderTest`.

**`Crossfade_InterruptedFormula_NoDomainErrorAtBoundary` test contract** — this test verifies that the interrupted crossfade `t_offset` formula `t_offset=(2/π)×arccos(current_gain_out)` returns 0.0 when `current_gain_out=1.0` and 1.0 when `current_gain_out=0.0`, with no `arccos` domain error at either boundary.

**Fixture**: No mock or device required — the formula is a pure mathematical function extracted to a static helper (or tested via a friend fixture). No audio thread construction needed.

**Test assertions**:

1. `EXPECT_NEAR(computeTOffset(1.0f), 0.0f, 1e-5f)` — at gain_out=1.0 (full outgoing gain), interruption at the very start → t_offset=0 (restart crossfade from scratch).
2. `EXPECT_NEAR(computeTOffset(0.0f), 1.0f, 1e-5f)` — at gain_out=0.0 (outgoing fade complete), interruption at the very end → t_offset=1 (skip directly to end).
3. No `std::domain_error`, `NaN`, or `Inf` at either boundary value.

**Mock policy**: None required (pure formula test).

**Source file**: `tests/audio/crossfade_interrupted_formula_test.cpp`

**CTest filter**: `-R Crossfade_InterruptedFormula_NoDomainErrorAtBoundary`

**`StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation` test contract** — this test
verifies that `UIManager::onCityRatingTransition()` calls `m_audio->triggerStinger(StingerType::MILESTONE)` at City Rating tier transitions and does NOT call it at raw population milestones that do not coincide with a tier transition.

**Fixture**: `StrictMock<MockAudioSystem>` injected into `UIManager`. The test directly calls
`UIManager::onCityRatingTransition(CityRating::TOWN)` (or equivalent) to trigger the tier
transition event; it also directly calls (or simulates) a population update reaching 100K without
a City Rating transition to verify no stinger fires for raw population milestones. No real
`CitySimulation` instance is required — the callback is invoked directly on `UIManager`.

**Test assertions**:

1. `EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE)).Times(1)` — fires at Village→Town at 1K population (City Rating transition).
2. `EXPECT_CALL(audio, triggerStinger(_)).Times(0)` — population reaches 100K but no City Rating transition occurs; no stinger fires.

**Mock policy**: `StrictMock` (unit test with exact call-count expectations on `triggerStinger`).

**Source file**: `tests/audio/stinger_milestone_test.cpp`

**CTest filter**: `-R StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation`

**`AudioStream_BarBoundary_*` test isolation note** — the two `AudioStreamTest` cases test the
bar-boundary formula in isolation. The `AL_BUFFERS_QUEUED` read-once-per-wake ordering and the
`computeSamplesPlayed()` / `computeNextBarBoundary()` formula are extracted to testable static
helper functions (or non-virtual members testable via a friend fixture). The `IAlcFunctions` seam
is used to mock `alGetSourcei(AL_BUFFERS_QUEUED)` return values, exercising the formula without
a real audio device. Neither test constructs an audio thread or streaming loop.

**`NotificationSFX_EFXBypass_DirectFilterSetToNull` test contract** — this test verifies that all
11 EFX-bypassed SFX assets (10 non-positional + 1 positional exception) have
`alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)` called on the acquired OpenAL source before
playback, and that the 2 positional alert SFX are explicitly excluded from the bypass.

**EFX bypass list (10 non-positional assets — bypass REQUIRED)**:

- `ui_click`
- `ui_toast`
- `ui_menu_open`
- `ui_menu_close`
- `sfx_power_out`
- `sfx_water_out`
- `sfx_budget_warn`
- `sfx_loan_issued`
- `sfx_zone_upgrade`
- `sfx_service_degrade`

**EFX bypass positional exception (1 positional asset — bypass REQUIRED despite world-space position)**:

- `sfx_earthworks` — mono positional (`AL_SOURCE_RELATIVE = AL_FALSE`, world-space source at tile
  centroid), but `AL_DIRECT_FILTER: AL_FILTER_NULL` is required because construction occurs on
  open, unoccluded tiles (design choice per `v1-audio-asset-manifest.md`). `AL_SOURCE_RELATIVE`
  must NOT be changed to `AL_TRUE` — the sound remains world-space positional.

**EFX bypass exclusion list (2 positional alert assets — bypass MUST NOT be applied)**:

- `sfx_fire_alert` — mono positional, CRITICAL priority; benefits from EFX occlusion at building
  location; `AL_DIRECT_FILTER` must NOT be set to `AL_FILTER_NULL`.
- `sfx_police_alert` — mono positional, CRITICAL priority; same rationale as `sfx_fire_alert`.

**Fixture and playback path**: The test uses a minimal real `AudioSystem` instance constructed
with a headless ALC context (`EDT_NULL` / null OpenAL device) and a `MockAlcFunctions` (via
`IAlcFunctions` seam) that captures all `alSourcei(src, AL_DIRECT_FILTER, ...)` calls. To drive
the playback path for each SFX asset, the test calls `AudioSystem::playUISound(SfxId)` for UI
sounds, `AudioSystem::playNotificationSound(SfxId)` for notification-category SFX, and
`AudioSystem::playPositionalSound(SFX_EARTHWORKS, position)` for `sfx_earthworks`. The captured
calls are checked against the bypass list after each play invocation.

**Test assertions**:

1. For each of the 10 non-positional assets in the EFX bypass list: verify that
   `alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)` is called on the acquired source before
   `alSourcePlay()`. The test injects a mock or spy on the AL function table (via the
   `IAlcFunctions` seam or equivalent AL-function injection) to capture `alSourcei` calls.
2. For `sfx_earthworks` (positional exception): verify that `alSourcei(src, AL_DIRECT_FILTER,
AL_FILTER_NULL)` is called before `alSourcePlay()`, and that `AL_SOURCE_RELATIVE` is NOT set
   to `AL_TRUE` (the source must remain world-space positional).
3. For `sfx_fire_alert` and `sfx_police_alert`: verify that `alSourcei(src, AL_DIRECT_FILTER,
AL_FILTER_NULL)` is NOT called on their acquired sources — positional sounds must not bypass
   EFX occlusion.

**Mock policy**: `NiceMock` for `MockAudioSystem` (this is a property/integration test that drives
the audio playback path, not a unit test with strict call-count expectations on unrelated methods).

**Source file**: `tests/audio/notification_sfx_efx_bypass_test.cpp`

**CTest filter**: `-R NotificationSFX_EFXBypass_DirectFilterSetToNull`

**`AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem` test contract** — this test verifies that `Population::updateMusicIntensity()` (called from `CitySimulation::doBudgetTick()`) dispatches `IAudioSystem::setMusicIntensity()` with the correct `MusicIntensity` tier when treasury/growth/deficit state changes, matching the thresholds defined in `architecture/game-design/economy-model.md` Music Intensity Tiers section.

**Fixture**: `StrictMock<MockAudioSystem>` injected into a real `CitySimulation` instance at construction. No `UIManager` instance required — the music intensity decision lives entirely in `Population::updateMusicIntensity()` (invoked from `CitySimulation::doBudgetTick()`).

**Test assertions** (minimum three state transitions):

1. Budget surplus (budget_surplus_pct ≥ 0%) with no population growth → `EXPECT_CALL(audio, setMusicIntensity(MusicIntensity::CALM))`.
2. Positive population growth (population this tick > population previous tick), no deficit streak → `EXPECT_CALL(audio, setMusicIntensity(MusicIntensity::GROWTH))`.
3. Two or more consecutive deficit months (consecutive_deficit_months ≥ 2) → `EXPECT_CALL(audio, setMusicIntensity(MusicIntensity::CRISIS))`.

**Mock policy**: `StrictMock` (unit test with exact call-count expectations on `setMusicIntensity`). Priority order (CRISIS > GROWTH > CALM) must be verified: when both CRISIS and GROWTH conditions are true simultaneously, only `CRISIS` fires.

**Source file**: `tests/simulation/adaptive_music_intensity_test.cpp`

**CTest filter**: `-R AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem`

`IRenderer` uses opaque `TextureHandle` (uint32_t) instead of `ITexture*` — the same pattern as `IUIBackend` with `UIElementHandle`. This fully severs the compile-time dependency on Irrlicht headers in any translation unit that only includes `IRenderer.h`, including all simulation test files. `MockRenderer::loadTexture()` returns an incrementing non-zero integer. The concrete `IrrlichtRenderer` maintains `std::unordered_map<TextureHandle, ITexture*>` internally.

- **Shared mock header cross-target pattern**: `MockAudioSystem` and `MockRenderer` are defined in `tests/simulation/MockAudioSystem.h` and `tests/simulation/MockRenderer.h` respectively. `ManualClock` is defined in `tests/simulation/ManualClock.h`. These headers are shared across multiple CMake test targets (`simulation_tests`, `ui_tests`, `audio_tests`). To avoid ODR (One Definition Rule) violations, these headers must be HEADER-ONLY GMock declarations (using MOCK_METHOD macros only, no definitions). Each test target that uses any of these shared headers MUST add `tests/simulation/` to its `target_include_directories`. This include path coupling is intentional and must be documented explicitly in the CMakeLists.txt for each consuming target. The ODR rule is safe because each test binary links into its own separate executable scope — there is no shared library or link-time merging across test targets. Required `target_include_directories` entries for each consuming target:

  ```cmake
  # simulation_tests — owns the shared mock headers; also needs src/simulation/ (for direct
  # includes like #include "CitySimulation.h"), src/interfaces/, and ${CMAKE_SOURCE_DIR}
  # for project-root-relative includes like #include "src/interfaces/IClock.h"
  target_include_directories(simulation_tests PRIVATE tests/simulation/ src/simulation/ src/interfaces/ ${CMAKE_SOURCE_DIR})

  # ui_tests — uses MockAudioSystem, MockRenderer, ManualClock from tests/simulation/;
  # MockUIBackend, MockCitySimulation from tests/ui/;
  # IUIBackend.h from src/interfaces/ (moved in Phase 10b); interface headers from src/interfaces/
  target_include_directories(ui_tests PRIVATE tests/simulation/ tests/ui/ src/interfaces/ ${CMAKE_SOURCE_DIR})

  # REQUIRED for ui_tests AND aitown_ui: enable AITOWN_TESTING_ENABLED so that
  # UIManager test-only methods (handleNewGameRequest, setGameSessionActiveForTest —
  # gated on #ifdef AITOWN_TESTING_ENABLED) are compiled into the ui_tests binary
  # AND into aitown_ui. aitown_ui requires it because handleNewGameRequest and
  # setGameSessionActiveForTest are non-inline functions defined in UIManager.cpp —
  # ui_tests would have undefined-reference link errors if aitown_ui were compiled
  # without the flag. The PRIVATE qualifier ensures the definition does NOT propagate
  # to the aitown binary (the top-level executable). MUST NOT be set on aitown.
  # Pattern: apply to aitown_sim / aitown_audio as well when their test targets need
  # non-inline test helpers (e.g. testForceUnlockDensityTier(), testGetSourceHandle(),
  # testSetZoneDemandFactor()).
  # Added in Phase 11m (aitown_ui/ui_tests), Phase 11q6 (aitown_audio/integration_tests),
  # Phase 11q11 (aitown_sim/simulation_tests).
  target_compile_definitions(ui_tests PRIVATE AITOWN_TESTING_ENABLED=1)
  target_compile_definitions(aitown_ui PRIVATE AITOWN_TESTING_ENABLED=1)

  # REQUIRED for simulation_tests AND aitown_sim (Phase 11q11): enable AITOWN_TESTING_ENABLED
  # so that CitySimulation::testSetZoneDemandFactor() (a non-inline method defined in
  # CitySimulation.cpp, gated on #ifdef AITOWN_TESTING_ENABLED) is compiled into both the
  # library and the test binary. ZoningTestNice uses this seam for deterministic demand
  # injection in upgrade-wave tests. Without AITOWN_TESTING_ENABLED on aitown_sim, the
  # function body is compiled out -> undefined-reference linker error.
  # MUST NOT be set on aitown (production binary).
  target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)
  target_compile_definitions(aitown_sim PRIVATE AITOWN_TESTING_ENABLED=1)

  # audio_tests — uses MockAudioSystem from tests/simulation/;
  # IAudioSystem.h from src/interfaces/; audio_constants.h from src/audio/
  target_include_directories(audio_tests PRIVATE tests/simulation/ src/interfaces/ src/audio/ ${CMAKE_SOURCE_DIR})

  # REQUIRED for integration_tests AND aitown_audio (Phase 11q6): enable AITOWN_TESTING_ENABLED
  # so that AudioSystem::testGetSourceHandle() (a non-inline method defined in AudioSystem.cpp,
  # gated on #ifdef AITOWN_TESTING_ENABLED) is compiled into both the library and the test binary.
  # VehicleReleaseTest is an integration test (constructs real AudioSystem with null-backend thread)
  # so it lives in integration_tests, not audio_tests. Without AITOWN_TESTING_ENABLED on
  # aitown_audio, the function body is compiled out → undefined-reference linker error.
  # MUST NOT be set on aitown (production binary).
  target_compile_definitions(integration_tests PRIVATE AITOWN_TESTING_ENABLED=1)
  target_compile_definitions(aitown_audio PRIVATE AITOWN_TESTING_ENABLED=1)

  # integration_tests — needs shared mock paths and UI header paths for Phase 3+ integration tests;
  # src/rendering/ is required because IrrlichtUIBackend.h lives there and integration tests compile against it
  target_include_directories(integration_tests PRIVATE tests/simulation/ tests/ui/ src/interfaces/ src/ui/ src/rendering/ ${CMAKE_SOURCE_DIR})

  # terrain_tests — needs tests/simulation/ for ManualClock (if timing tests added in Phase 5+),
  # tests/terrain/ for MockTerrainRNG; src/interfaces/ for ITerrainRNG.h (moved from
  # src/terrain/ in Phase 10b Feature 3); ${CMAKE_SOURCE_DIR} so project-root-relative
  # includes resolve correctly. Phase 10b Feature 3 also removes src/terrain/ since
  # ITerrainRNG.h is no longer there; MockTerrainRNG.h (renamed from mock_terrain_rng.h)
  # updates its include to #include "src/interfaces/ITerrainRNG.h".
  target_include_directories(terrain_tests PRIVATE tests/simulation/ tests/terrain/ src/interfaces/ ${CMAKE_SOURCE_DIR})
  ```

  If a test target needs a specialization, it must subclass the shared mock — not redefine it. This sharing is intentional: the same mock interface is used consistently across all simulation-adjacent tests.

- **`AudioSystemVehicleReleaseTest` fixture** (Phase 11q6, `tests/integration/VehicleReleaseTest.cpp`):
  Constructs a real `AudioSystem` with the null OpenAL Soft backend. The fixture pattern is:

  ```cpp
  class AudioSystemVehicleReleaseTest : public ::testing::Test {
  protected:
      ManualClock clock_;
      std::unique_ptr<AudioSystem> audio_;

      void SetUp() override {
          // logger=nullptr  → falls back to stderr
          // clock_          → ManualClock member; allows deterministic timing
          // alcFunctions=nullptr → DefaultAlcFunctions (real ALC); null driver via ALSOFT_DRIVERS=null
          audio_ = std::make_unique<AudioSystem>(nullptr, &clock_, nullptr);
      }

      void TearDown() override {
          audio_.reset();  // joins audio background thread before test teardown
      }
  };
  ```

  `ALSOFT_DRIVERS=null` is set in the ctest environment (CI: `env:` block in workflow step;
  local builds: via `ENVIRONMENT "ALSOFT_DRIVERS=null"` in the `aitown_add_tests()` call —
  see `framework.md` §`aitown_add_tests()` macro), not in the fixture — this ensures
  `alcOpenDevice` succeeds on the null backend and the audio thread starts normally.
  `set_tests_properties()` MUST NOT be used for this purpose (see `AitownTestHelpers.cmake`
  header: it does not propagate to individually-discovered test cases under
  `DISCOVERY_MODE PRE_TEST`). The three constructor arguments map to `AudioSystem`'s declared signature:
  `explicit AudioSystem(irr::ILogger* logger, IClock* clock, IAlcFunctions* alcFunctions = nullptr)`.
  `ManualClock` is declared as a plain member (not a pointer) so no heap allocation is needed and
  lifetime is managed automatically by the fixture.

- **`IClock`** — injectable clock interface for audio timing and loan gate tests.

  **Spec entry**:

  ```text
  Header: src/interfaces/IClock.h
  Methods:
    virtual double nowSeconds() const = 0;   // seconds since epoch (steady_clock)
    virtual ~IClock() = default;
  Implementations:
    WallClock   — production (std::chrono::steady_clock)
    ManualClock — tests (manually advanced via ManualClock::advance(seconds))
  ```

  **Source location**: `IClock.h` and `WallClock.h` live in `src/interfaces/`; `ManualClock` lives in `tests/simulation/ManualClock.h` (it is a test double used by both simulation tests and audio tests). `MockTerrainRNG` lives in `tests/terrain/MockTerrainRNG.h`. All test double headers (`manual_*.h`, `mock_*.h`) live under `tests/` — never under `src/`. See `coverage.md` for the full lcov exclusion patterns (including `mock_*` and `manual_*` exclusions).

  ```cpp
  class IClock {
  public:
      virtual ~IClock() = default;
      virtual double nowSeconds() const = 0;  // wall-clock seconds since epoch or arbitrary start
  };
  class ManualClock : public IClock {
  public:
      double nowSeconds() const override { return m_time; }
      void advance(double seconds) { m_time += seconds; }
  private:
      double m_time{0.0};
  };
  ```

  `AudioSystem` and `CitySimulation` accept `IClock*` at construction for crossfade timing and the forced-loan real-time gate (120 s) respectively. Production code passes `WallClock` which calls `std::chrono::steady_clock`. `ManualClock` allows deterministic time advancement in tests without wall-clock dependencies. Cross-references: `architecture/audio-architecture/audio-system.md` (IClock injection into AudioSystem — see §IClock); `architecture/game-design/economy-model.md` (IClock injection into CitySimulation for grace-period and loan-gate timing — see §IClock); `architecture/game-design/save-system.md` (IClock injection for auto-save timer — see §IClock).

- **`ITerrainQuery`** — injectable terrain interface for world-interaction and slope-cost tests. **Source location**: `ITerrainQuery.h` lives in `src/interfaces/`; `ManualTerrainQuery` lives in `tests/simulation/ManualTerrainQuery.h` (renamed from `manual_terrain_query.h` to CamelCase in Phase 10b Feature 3; alongside `ManualRNG.h` and `ManualClock.h` — all test doubles for injectable simulation interfaces). `ManualTerrainQuery` provides two slope configuration APIs:

  **Global slope** (single `float` overload): sets a uniform slope for ALL tiles. Used by `WorldInteractionTest` when one slope applies to the whole map. **Per-tile slope** (3-argument overload): overrides a specific tile. Per-tile entries take precedence over the global slope. Both APIs coexist:

  ```cpp
  class ManualTerrainQuery : public ITerrainQuery {
  public:
      // Global slope — sets uniform slope for ALL tiles (default 0°, flat terrain).
      // WorldInteractionTest uses this form: terrain_.setSlope(20.0f)
      // to trigger the earthworks guard for the entire test map.
      void setSlope(float degrees) { m_globalSlope = degrees; }

      // Per-tile slope — overrides slope for a specific tile; takes precedence over
      // the global slope. Used by CitySimulation earth-works tests needing distinct
      // slopes per tile (e.g., slope canyon patterns).
      void setSlope(int tileX, int tileZ, float degrees) {
          m_slopes[makeKey(tileX, tileZ)] = degrees;
      }

      // Reset all slope configuration to 0° (flat terrain for all tiles).
      void resetSlope() { m_globalSlope = 0.0f; m_slopes.clear(); }

      // Returns per-tile slope if set, otherwise returns global slope (default 0°).
      float getSlopeDegrees(int tileX, int tileZ) const override {
          auto it = m_slopes.find(makeKey(tileX, tileZ));
          return (it != m_slopes.end()) ? it->second : m_globalSlope;
      }

      // Phase 9b addition: always returns 0.0f (flat world at sea level).
      // Required to satisfy pure-virtual contract of ITerrainQuery::getHeightAt().
      // Phase 9b unit tests that need specific heights inject MockRenderer for the
      // renderer path; no WorldInteractionTest requires non-zero heights here.
      // Phase 10b: this return-0.0f form is superseded by the stateful form below
      // (see "Phase 10b stateful extension"). The stateful form overrides getHeightAt()
      // to return m_heightAfterFlat or m_heightBeforeFlat based on m_flattened.
      float getHeightAt(int /*tileX*/, int /*tileZ*/) const override { return 0.0f; }
  private:
      static int64_t makeKey(int x, int z) {
          return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
      }
      float m_globalSlope{0.0f};
      std::unordered_map<int64_t, float> m_slopes;
  };
  ```

  **CRITICAL — `setSlope()` overload selection**: `WorldInteractionTest` tests that exercise the earthworks guard call the SINGLE-ARGUMENT form `terrain_.setSlope(20.0f)` to apply slope 20° to all tiles uniformly. Calling the THREE-ARGUMENT form `terrain_.setSlope(5, 7, 20.0f)` would only affect tile (5,7) — other tiles remain 0°. Tests that use `MockRenderer::pickTerrainTile` to return tile (5,7) and then expect earthworks cost must call the global-slope form `terrain_.setSlope(20.0f)`.

  Used in `WorldInteractionTest` (Phase 9b) as the `terrain_` fixture member, injected via `uiManager_->setTerrainQuery(&terrain_)`. The `getHeightAt()` override is required because it is pure virtual on `ITerrainQuery`; without it `ManualTerrainQuery` fails to compile, blocking all 17 Phase 9b unit tests.

  **Phase 10b stateful extension**: Phase 10b adds `setTileHeight()` as a pure-virtual method
  to `ITerrainQuery`, requiring a new override in `ManualTerrainQuery`. The Phase 10b form is
  stateful, superseding the Phase 9b return-0.0f form of `getHeightAt()`:

  ```cpp
  // Phase 10b additions — stateful terrain flattening for TerrainFlattening tests.
  bool  m_flattened{false};
  float m_heightBeforeFlat{0.0f};
  float m_heightAfterFlat{0.0f};

  void setHeightBeforeFlattening(float h) { m_heightBeforeFlat = h; }
  void setHeightAfterFlattening(float h)  { m_heightAfterFlat  = h; }

  // Phase 10b override — returns post-flatten height if setTileHeight() was called,
  // otherwise pre-flatten height. Defaults to 0.0f / 0.0f so existing tests are unaffected.
  float getHeightAt(int /*tileX*/, int /*tileZ*/) const override {
      return m_flattened ? m_heightAfterFlat : m_heightBeforeFlat;
  }

  // Phase 10b override — records that flattening occurred; TerrainFlattening tests
  // assert m_flattened == true to confirm setTileHeight() was invoked.
  void setTileHeight(int /*tileX*/, int /*tileZ*/, float /*height*/) override {
      m_flattened = true;
  }
  ```

  `IRenderer` placement methods carry no Y parameter, so height verification in
  `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight` must go through
  `ManualTerrainQuery::m_flattened` and `getHeightAt()`, not through `MockRenderer`.

  **Phase 10b landing sequence**: the `setTileHeight()` pure-virtual addition to
  `ITerrainQuery.h` and the `ManualTerrainQuery` override MUST land in the same commit to
  avoid making `ManualTerrainQuery` abstract and breaking all 17+ simulation unit tests.
  Step 1 (`graphics-dev-irrlicht` PR): add pure-virtual to `ITerrainQuery.h` AND add the
  no-op `void setTileHeight(int, int, float) override {}` to `ManualTerrainQuery` in the
  same commit. Step 2 (`test-dev-cpp` PR): replace the no-op with the stateful form
  described above.

  **Phase 11l extension**: Phase 11l adds per-tile height configuration and per-call
  flatten recording to `ManualTerrainQuery` for the multi-tile footprint tests
  (`IrrlichtRenderer_PlaceMediumBuilding_AllCornerVerticesFlattened`). The extension adds
  `setHeightAt()` for non-uniform `getHeightAt()` returns and a `m_flattenCalls` vector
  that records every `setTileHeight()` invocation with its `{x, z, h}` arguments:

  ```cpp
  // Phase 11l additions — per-tile height configuration and per-call flatten recording.
  std::map<int64_t, float>                  m_tileHeights;   // keyed by makeKey(x, z)
  std::vector<std::tuple<int, int, float>>  m_flattenCalls;  // {x, z, h} per call

  // Set pre-existing height at a specific tile (for getHeightAt() to return).
  // Overrides the Phase 10b global before/after heights for the given tile.
  void setHeightAt(int x, int z, float h) {
      m_tileHeights[makeKey(x, z)] = h;
  }

  // Phase 11l override — appends {x, z, height} to m_flattenCalls AND sets
  // m_flattened = true, preserving the Phase 10b contract.
  void setTileHeight(int tileX, int tileZ, float height) override {
      m_flattened = true;
      m_flattenCalls.emplace_back(tileX, tileZ, height);
  }

  // Phase 11l override — per-tile heights take precedence over Phase 10b globals.
  float getHeightAt(int tileX, int tileZ) const override {
      auto it = m_tileHeights.find(makeKey(tileX, tileZ));
      if (it != m_tileHeights.end()) return it->second;
      return m_flattened ? m_heightAfterFlat : m_heightBeforeFlat;
  }
  ```

  Phase 10b tests are unaffected: they do not call `setHeightAt()`, so `m_tileHeights`
  remains empty; `m_flattened == true` is still set by `setTileHeight()`; `getHeightAt()`
  still returns `m_heightAfterFlat` / `m_heightBeforeFlat` for them.

  **Phase 11l flatten assertion pattern**: call `setHeightAt()` to configure non-uniform
  tile heights, place the building (which calls `setTileHeight()` internally), then assert
  that `m_flattenCalls` contains all expected `{x, z, targetH}` tuples (order-independent
  comparison via sorting or `EXPECT_THAT(..., UnorderedElementsAre(...))`).

  **Phase 11q extension**: Phase 11q adds `getHeightAtWorld(float, float)` as a pure-virtual
  method to `ITerrainQuery`, requiring a bilinear-interpolation override in
  `ManualTerrainQuery`:

  **`kTileSize` locality constraint**: `ManualTerrainQuery::getHeightAtWorld()` must NOT
  `#include "render_constants.h"` — that header includes `irrlicht.h` (which pulls in
  `irr::video::SColor` and other Irrlicht types), and `simulation_tests` does not link
  Irrlicht. Instead, define `kTileSize` locally within the `ManualTerrainQuery` header:

  ```cpp
  // Must match RenderConstants::kTileSize in src/rendering/render_constants.h
  static constexpr float kTileSize = 10.0f;
  ```

  ```cpp
  // Phase 11q override — bilinear interpolation over the m_heights grid.
  // Converts world coordinates to grid indices via kTileSize, samples four
  // corner heights from m_tileHeights (set via setHeightAt()), and bilinearly
  // interpolates.  Tiles absent from m_tileHeights fall back to the Phase 10b
  // m_flattened ? m_heightAfterFlat : m_heightBeforeFlat default — so tests
  // that never call setHeightAt() see a flat plane at the default height,
  // while slope-rotation tests (e.g. MoveVehicleAgent_SlopedTerrain_AppliesPitchAndRoll)
  // configure per-tile heights and receive correct sub-tile interpolation.
  float getHeightAtWorld(float worldX, float worldZ) const override {
      // Integer tile and fractional offset (uses local kTileSize, not render_constants.h)
      float gx = worldX / kTileSize;
      float gz = worldZ / kTileSize;
      int ix = static_cast<int>(std::floor(gx));
      int iz = static_cast<int>(std::floor(gz));
      float fx = gx - ix;   // 0..1
      float fz = gz - iz;   // 0..1

      // Sample four corners — delegates to getHeightAt() which
      // honours m_tileHeights with Phase 10b fallback.
      float h00 = getHeightAt(ix,     iz);
      float h10 = getHeightAt(ix + 1, iz);
      float h01 = getHeightAt(ix,     iz + 1);
      float h11 = getHeightAt(ix + 1, iz + 1);

      // Bilinear interpolation (same formula as TerrainSystem)
      float top    = h00 + (h10 - h00) * fx;
      float bottom = h01 + (h11 - h01) * fx;
      return top + (bottom - top) * fz;
  }
  ```

  This makes `ManualTerrainQuery` the canonical "real-but-configurable" test stub:
  tests control terrain shape via `setHeightAt()` calls and receive the same bilinear
  interpolation that `TerrainSystem::getHeightAtWorld()` uses in production. Tests that
  never call `setHeightAt()` (zone-assignment, audio-lifecycle, flat-terrain Y-position)
  still see a uniform height surface and are unaffected.

  **Phase 11q landing sequence**: all three items MUST land in the same commit — committing
  any subset breaks the build or simulation unit tests:
  1. `ITerrainQuery.h` — add `getHeightAtWorld()` as a pure-virtual method. Committing this
     alone leaves every concrete `ITerrainQuery` implementor (`TerrainSystem`,
     `ManualTerrainQuery`, and any other stub) abstract, breaking the build immediately.
  2. `TerrainSystem.h` + `TerrainSystem.cpp` — add the bilinear implementation
     (requires `#include "render_constants.h"` and `using namespace RenderConstants;`).
     Committing (1)+(2) without (3) still leaves `ManualTerrainQuery` abstract and breaks
     all simulation unit tests that construct it.
  3. `ManualTerrainQuery` (and every other `ITerrainQuery` stub) — add the bilinear
     interpolation override shown above, delegating to the existing `getHeightAt()` and
     `m_tileHeights` infrastructure.

  Only once all three are present does the build and full test suite remain green.
  This is the same 3-item atomicity rule applied for Phase 10b `setTileHeight()`.

  **Y-position tests** (`MoveVehicleAgent_FlatTerrain_VehicleYIncludesBias` and
  `SpawnVehicleAgent_FlatTerrain_VehicleYIncludesBias`) require inspecting a real Irrlicht
  scene-node `getPosition().Y` after a renderer call and are therefore placed in
  `tests/rendering/` with `requires-opengl` label. Those tests create an `IrrlichtRenderer`
  with a `ManualTerrainQuery` stub returning `0.0f`, spawn or move a vehicle agent, then
  assert `node.getPosition().Y == kRoadSurfaceYBias (0.25f)`.

- **`IAlcFunctions`** — injectable interface for ALC function-pointer lookup, enabling the thread-local context extension check in `AudioSystem` to be intercepted in tests without a real OpenAL device. **Source locations** (post-Phase 10b): `IAlcFunctions.h` in `src/interfaces/` (moved from `src/audio/ialc_functions.h` and renamed in Phase 10b Feature 3); `DefaultAlcFunctions.h`/`DefaultAlcFunctions.cpp` remain in `src/audio/`; `MockAlcFunctions` is defined locally in `tests/audio/audio_thread_test.cpp` (single-use — not a shared header). All test double headers live under `tests/` — never under `src/`.

  ```cpp
  class IAlcFunctions {
  public:
      virtual ~IAlcFunctions() = default;
      virtual bool isExtensionPresent(const char* extName) = 0;
      virtual void* getProcAddress(const char* funcName) = 0;
  };
  ```

  `AudioSystem` constructor accepts `IAlcFunctions* alcFunctions = nullptr`; passing `nullptr` activates `DefaultAlcFunctions`, which delegates to the real `alcIsExtensionPresent()` and `alcGetProcAddress()`. **Windows compatibility**: the `IAlcFunctions` seam is the primary and mandatory test path — it works on both Linux and Windows. The weak-symbol override approach is Linux-only and is a secondary option only.

  `AudioThreadTest::AbsentThreadLocalContext_ConstructorThrows` injects a `MockAlcFunctions` configured to return `nullptr` for `getProcAddress("alcSetThreadContext")`, verifying the constructor throws `std::runtime_error` before entering the streaming loop (ref: `implementation/phase-7.md` line 47).

- **Ownership contract**: Simulation objects accept `IRenderer*` and `IAudioSystem*` as non-owning raw pointers. Ownership managed externally. In tests, fixture owns the mock and outlives the system under test:

### StrictMock Expected Call Matrix

**Zero-revenue fixture clarification**: Zero-revenue tests (`BudgetSurplus_ZeroRevenue_ReturnsZero`, `ZeroRevenue_NoDeficitConsequences`, and similar) use the standard `CitySimulationUnitTest` fixture, which includes `ManualRNG rng_{std::initializer_list<int>{0}}` as a member. This is acceptable because zero-revenue code paths never call `nextInt()` or `nextFloat()` — the ManualRNG sequence is never consumed. The key invariant for zero-revenue tests is using `StrictMock<MockAudioSystem>` (any unexpected audio call fails the test immediately). The `ManualRNG` member is harmless in these tests. There is no separate zero-revenue fixture; do not create one.

For every unit test that uses `StrictMock<MockAudioSystem>` or `StrictMock<MockRenderer>`, ALL expected calls must be explicitly declared via `EXPECT_CALL` — any unexpected call causes an immediate test failure. The following matrix documents the expected call pattern for each named test scenario. **This matrix must be kept up to date as new test scenarios are added.**

| Test scenario                                                                                                                                                                                                                                                                                                                                                                                                                                               | Expected `MockAudioSystem` calls                                                                                                                                                                                                                                       | Expected `MockRenderer` calls                                                                                                                                                                                                          |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Zero-revenue tests (ZeroRevenue\_\*, ZeroRevenue_NoDeficitConsequences)                                                                                                                                                                                                                                                                                                                                                                                     | None                                                                                                                                                                                                                                                                   | None                                                                                                                                                                                                                                   |
| Budget surplus / no loan                                                                                                                                                                                                                                                                                                                                                                                                                                    | None                                                                                                                                                                                                                                                                   | None                                                                                                                                                                                                                                   |
| ServiceDegradation (random building selected)                                                                                                                                                                                                                                                                                                                                                                                                               | `playSound(SoundId{sfx_service_degrade}, SoundPriority::NORMAL, 1.0f)` × 1 per degraded building                                                                                                                                                                       | None                                                                                                                                                                                                                                   |
| ForcedLoanIssued                                                                                                                                                                                                                                                                                                                                                                                                                                            | `playSound(SoundId{sfx_loan_issued}, SoundPriority::NORMAL, 1.0f)` × 1                                                                                                                                                                                                 | None                                                                                                                                                                                                                                   |
| ModalDialog_OnOpen                                                                                                                                                                                                                                                                                                                                                                                                                                          | `setPaused(true)` × 1 (via MockCitySimulation, not IAudioSystem)                                                                                                                                                                                                       | None                                                                                                                                                                                                                                   |
| ModalDialog_OnClose (no queued toast)                                                                                                                                                                                                                                                                                                                                                                                                                       | `setPaused(false)` × 1 (via MockCitySimulation)                                                                                                                                                                                                                        | None                                                                                                                                                                                                                                   |
| `ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated` (test 8)                                                                                                                                                                                                                                                                                                                                                                                 | Via MockCitySimulation: `setPaused(true)` × 2 (once on modal open; once on re-evaluation in `closeModal()` because CRITICAL queue is non-empty); `setPaused(false)` × 0 (NOT called — simulation stays paused because CRITICAL toast remains active after modal close) | Via MockUIBackend: `addStaticText` × 1 (queued CRITICAL toast displayed synchronously within `closeModal()`)                                                                                                                           |
| `WorldInteraction_ZonePlacement_CallsPlaceZone`                                                                                                                                                                                                                                                                                                                                                                                                             | None (NiceMock audio unused)                                                                                                                                                                                                                                           | `pickTerrainTile(_, _, _, _)` → returns true + tileX=5 tileZ=7 × 1; `setTileHoverHighlight` not called on MouseButtonDown; `setZoneOverlay(_, _, _)` × 1 (on successful placement); `MockCitySimulation::placeZone(5, 7, _, _, 0)` × 1 |
| `WorldInteraction_RoadPlacement_CallsPlaceRoad`                                                                                                                                                                                                                                                                                                                                                                                                             | None                                                                                                                                                                                                                                                                   | `pickTerrainTile(_, _, _, _)` × 1 (returns tile 5,7); `setZoneOverlay` NOT called (road placement does not update zone overlay); `MockCitySimulation::placeRoad(5, 7, 0)` × 1                                                          |
| `WorldInteraction_DemolishTool_SteepSlope_NoEarthworksGuard`                                                                                                                                                                                                                                                                                                                                                                                                | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1; `MockCitySimulation::demolishTile(5, 7)` × 1 (no earthworks guard on demolish)                                                                                                                                  |
| `WorldInteraction_ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace`                                                                                                                                                                                                                                                                                                                                                                                      | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1; `MockCitySimulation::placeZone(_, _, _, _, _)` × 0 (not called — blocked by earthworks guard); `MockUIBackend::addStaticText(HasSubstr("insufficient funds"), _, _, _, _)` × AtLeast(1)                         |
| `WorldInteraction_QueryTool_CallsQueryTile`                                                                                                                                                                                                                                                                                                                                                                                                                 | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1; `getTileScreenBounds(5, 7)` × 1; `MockCitySimulation::queryTile(5, 7)` × 1                                                                                                                                      |
| `WorldInteraction_NoActiveTool_LeftClickIgnored`                                                                                                                                                                                                                                                                                                                                                                                                            | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 0 (no ray-cast when no tool active); `MockCitySimulation::placeZone(_, _, _, _, _)` × 0; `MockCitySimulation::placeRoad(_, _, _)` × 0                                                                              |
| `WorldInteraction_ModalActive_LeftClickNotDispatched`                                                                                                                                                                                                                                                                                                                                                                                                       | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 0 (modal Priority 1 consumes event before world layer); `MockCitySimulation::placeZone(_, _, _, _, _)` × 0                                                                                                         |
| `WorldInteraction_HoverHighlight_SetOnMouseMove`                                                                                                                                                                                                                                                                                                                                                                                                            | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1 (returns tile 3,4); `setTileHoverHighlight(3, 4, _)` × AtLeast(1)                                                                                                                                                |
| `WorldInteraction_HoverHighlight_ClearedOnMiss`                                                                                                                                                                                                                                                                                                                                                                                                             | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1 (returns false); `setTileHoverHighlight(-1, -1, 0)` × AtLeast(1)                                                                                                                                                 |
| `WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry`                                                                                                                                                                                                                                                                                                                                                                                                 | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1; `setZoneOverlay(_, _, _)` × 1 (SaveArg used to capture sparse map; assert key 43 maps to `0x6000FF00u`)                                                                                                         |
| `WorldInteraction_Demolish_SparseOverlay_ErasesEntry`                                                                                                                                                                                                                                                                                                                                                                                                       | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × AtLeast(2) (zone placement + demolish); `setZoneOverlay` × 2 (one after placement, one after demolish; second call has empty map)                                                                                  |
| `WorldInteraction_UtilitiesPlacement_CallsPlaceServiceBuilding`                                                                                                                                                                                                                                                                                                                                                                                             | None                                                                                                                                                                                                                                                                   | `pickTerrainTile` × 1 (returns tile 5,7); `MockCitySimulation::placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0)` × 1                                                                                                    |
| `CitySimulation_PlaceRoad_FiresSFXRoadBuild`                                                                                                                                                                                                                                                                                                                                                                                                                | `StrictMock<MockAudioSystem>::playPositionalSound(SFX_ROAD_BUILD, _, _, _)` × 1                                                                                                                                                                                        | None (`NiceMock<MockRenderer>` — renderer call is incidental)                                                                                                                                                                          |
| **Phase 10 rendering rows — `CitySimulationRenderTest` cases (use `CitySimulationRenderStub` + `NiceMock<MockSimRenderer>`; no audio mock — stub does not fire audio calls); these correlations apply to `CitySimulation` integration tests that exercise the same code paths with `StrictMock<MockRenderer>` + `MockAudioSystem`**                                                                                                                         |                                                                                                                                                                                                                                                                        |                                                                                                                                                                                                                                        |
| `CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh` (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                          | `playPositionalSound(SFX_BUILD_PLACE, _, _, _)` × 1 (integration test only; stub test uses no audio mock)                                                                                                                                                              | `placeBuildingMesh(tile, ZoneType::Residential, 0)` × 1                                                                                                                                                                                |
| `CitySimulationRenderTest_PlaceRoad_PlacesRoadMesh` (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                              | `playPositionalSound(SFX_ROAD_BUILD, _, _, _)` × 1 (integration only)                                                                                                                                                                                                  | `placeRoadMesh(tile)` × 1                                                                                                                                                                                                              |
| `CitySimulationRenderTest_PlaceServiceBuilding_PlacesServiceMesh` (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                | `playPositionalSound(SFX_BUILD_PLACE, _, _, _)` × 1 (integration only)                                                                                                                                                                                                 | `placeServiceBuildingMesh(tile, ServiceBuildingType::FireStation)` × 1                                                                                                                                                                 |
| `CitySimulationRenderTest_DemolishZone_RemovesBuildingMesh` (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                      | `playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _)` × 1 (integration only)                                                                                                                                                                                              | `removeBuildingMesh(tile)` × 1                                                                                                                                                                                                         |
| `CitySimulationRenderTest_DemolishRoad_RemovesRoadMesh` (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                          | `playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _)` × 1 (integration only)                                                                                                                                                                                              | `removeRoadMesh(tile)` × 1                                                                                                                                                                                                             |
| `CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh` (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                      | `playSound(SFX_ZONE_UPGRADE, _, _)` × 1 (integration only; cap up to 3 per tick)                                                                                                                                                                                       | `removeBuildingMesh(tile)` × 1; `placeBuildingMesh(tile, zone, newTier)` × 1                                                                                                                                                           |
| Tests exercising `tick()` with earthworks cost > 0 (Phase 10)                                                                                                                                                                                                                                                                                                                                                                                               | `playPositionalSound(SFX_EARTHWORKS, _, _, _)` × 1 before `SFX_BUILD_PLACE`/`SFX_ROAD_BUILD`                                                                                                                                                                           | `placeBuildingMesh` or `placeRoadMesh` × 1 (same tile)                                                                                                                                                                                 |
| `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight` (Phase 10b)                                                                                                                                                                                                                                                                                                                                                                                    | `NiceMock<MockAudioSystem>` — audio calls (SFX_BUILD_PLACE, optionally SFX_EARTHWORKS) are incidental to the flattening assertion; no `EXPECT_CALL` needed                                                                                                             | `NiceMock<MockRenderer>` — the assertion is on `ManualTerrainQuery::m_flattened`, not on renderer behavior; no `EXPECT_CALL` needed for `placeBuildingMesh`                                                                            |
| **Guidance**: For tests where rendering method calls are incidental to the assertion, switch `renderer_` to `NiceMock<MockRenderer>` — this avoids declaring EXPECT_CALL for every mesh placement side-effect and is the approved approach for audio-focused tests (`CitySimulation_PlaceRoad_FiresSFXRoadBuild` already uses `NiceMock<MockRenderer>` for this reason). Use `StrictMock<MockRenderer>` only when the test is asserting rendering behavior. |                                                                                                                                                                                                                                                                        |                                                                                                                                                                                                                                        |

> **Post-V1 stinger scenarios**: `StingerType::GAME_OVER` (game-over stinger, fires in Scenario mode) is not defined in the V1 `StingerType` enum (`{ CRISIS, MILESTONE }` only). Do not reference `StingerType::GAME_OVER` in any V1 test or production code — it does not exist until Scenario mode is implemented post-V1. When Scenario mode is added post-V1, a new matrix row will be added here.

**Important**: `CitySimulation::setPaused()` is NOT an `IAudioSystem` call — it is a `CitySimulation` method called by `UIManager`. The matrix above uses "MockAudioSystem" loosely for audio effects; the pause/resume calls go to a `MockCitySimulation` (or directly to `CitySimulation` with injected mocks). Distinguish the two mock targets in test setup.

### UIManagerDrawOrderTest CONTRACT

`UIManagerDrawOrderTest` verifies that `UIManager::draw()` issues panel draw calls in the correct back-to-front Z-order (see `architecture/ui-ux/ui-manager.md` Draw Order section). Because `IUIBackend` has no `drawRect()` or equivalent rendering primitive, ordering is verified via a per-panel sentinel call: each panel stub's `draw()` implementation calls `setElementVisible(handle, true)` using a fixed dummy `UIElementHandle` sentinel value unique to that panel. The 17 methods of `IUIBackend` do not include a draw primitive — `setElementVisible` is the only side-effectful call that can serve as an observable ordering probe without adding a new method to the interface.

**Anti-no-op requirement**: Each panel stub's `draw()` method MUST call `m_backend->setElementVisible(handles::kXxxSentinel, true)` using the UNIQUE per-panel `UIElementHandle` sentinel constant from `tests/ui/panel_sentinel_handles.h` (e.g., `handles::kMinimapSentinel = 101u`, `handles::kHUDSentinel = 102u`, etc.). A `draw()` method that does nothing (no-op body) vacuously satisfies `InSequence` constraints — ALL constraints are trivially satisfied when zero `setElementVisible` calls fire. The test MUST fail if `UIManager::draw()` skips any panel. The `InSequence` guard is only meaningful when each expected call actually fires.

Panel stubs used in `UIManagerDrawOrderTest` are NOT "no-op shells" — they must contain the sentinel `setElementVisible` call even if all other methods on the stub are no-ops. A stub `draw()` body of `{}` (empty) or `// TODO` is a defect: the draw-order test becomes vacuously green and provides zero ordering coverage.

**CONTRACT**: Each test-stub panel (e.g. `StubHUD`, `StubMinimap`, `StubFinancesPanel`, etc.) used by `UIManagerDrawOrderTest` must:

1. Hold a fixed dummy `UIElementHandle` sentinel sourced from `tests/ui/panel_sentinel_handles.h` (e.g. `handles::kHUDSentinel` — a unique non-zero value per panel class to distinguish calls in the expectation sequence).
2. Implement `draw()` as a single call: `m_backend->setElementVisible(handles::kXxxSentinel, true);` — NOT a no-op. The production panel classes do NOT include this header.
3. Accept `IUIBackend*` at construction so that `MockUIBackend` can be injected.

**Visibility guard location**: The visibility check lives in `UIManager::draw()`, which only calls `panel->draw()` when the panel is visible. The panel stub's own `draw()` body MUST call the sentinel unconditionally — no internal visibility guard is needed in the stub. A hidden panel in a test state simply will not have its `draw()` called by UIManager, so its sentinel will not fire. This is correct behavior and does NOT violate the CONTRACT. A stub `draw()` body of `{}` (no sentinel call) is a defect — the stub must call the sentinel.

**`tests/ui/panel_sentinel_handles.h` specification**: This is a test-only header (NOT included in any production code). It defines sentinel `UIElementHandle` constants for all panels used in `UIManagerDrawOrderTest`. Each sentinel is a distinct non-zero value:

```cpp
// tests/ui/panel_sentinel_handles.h
// TEST-ONLY: Do NOT include in production panel headers.
namespace handles {
    constexpr UIElementHandle kMainMenuSentinel      = 100u;
    constexpr UIElementHandle kMinimapSentinel       = 101u;
    constexpr UIElementHandle kHUDSentinel           = 102u;
    constexpr UIElementHandle kFinancesSentinel       = 103u;
    constexpr UIElementHandle kInspectorSentinel     = 104u;
    constexpr UIElementHandle kNotificationSentinel  = 0xDEAD0105u;
    constexpr UIElementHandle kPauseMenuSentinel     = 106u;
    constexpr UIElementHandle kSettingsSentinel      = 107u;
    constexpr UIElementHandle kScrimSentinel         = 108u;
    constexpr UIElementHandle kModalSentinel         = 109u;
}
```

Each panel stub's `draw()` in `UIManagerDrawOrderTest` calls `m_backend->setElementVisible(handles::kXxxSentinel, true)`. The production panel classes do NOT include this header.

**RULE — sentinel magic values are TEST-ONLY constants**: The sentinel `UIElementHandle` constants defined in `tests/ui/panel_sentinel_handles.h` (e.g. `handles::kNotificationSentinel = 0xDEAD0105u`) are for test code only. Production panel stubs (`notification_manager.cpp`, `hud.cpp`, etc.) MUST NOT `#include "tests/ui/panel_sentinel_handles.h"`. Instead, the Phase 3 production stub for each panel declares its own local `constexpr UIElementHandle` in the `.cpp` file with the matching magic value. The test header and the production `.cpp` file both use the same numeric literal; the test header is never part of the production build graph.

**Phase 3 NotificationManager sentinel mechanism** — the two-sided contract:

1. **Production side** (`notification_manager.cpp`): The Phase 3 stub declares a local constant and uses it in `draw()`:

   ```cpp
   // notification_manager.cpp  (Phase 3 stub — NOT the Phase 8 implementation)
   namespace {
       // Phase 8 REPLACE THIS: real IUIBackend element handle
       constexpr UIElementHandle kNotifSentinel = 0xDEAD0105u;
   }

   void NotificationManager::draw() {
       m_backend->setElementVisible(kNotifSentinel, true);
   }
   ```

   `kNotifSentinel` is defined locally in the `.cpp` file. The file does NOT include `tests/ui/panel_sentinel_handles.h` or any test header. There are no `#ifdef TEST` guards — the constant is simply an anonymous-namespace literal baked into the Phase 3 stub.

2. **Test side** (`tests/ui/panel_sentinel_handles.h`): The header declares the same numeric value under the `handles` namespace:

   ```cpp
   // tests/ui/panel_sentinel_handles.h  — TEST-ONLY, never included in src/
   namespace handles {
       constexpr UIElementHandle kNotificationSentinel = 0xDEAD0105u;  // matches kNotifSentinel in notification_manager.cpp
       // ... other panel sentinels
   }
   ```

   Both sides agree on `0xDEAD0105u`. The identity of the value bridges production and test without any shared header.

3. **Test assertion** (`tests/ui/notification_manager_test.cpp`):

   ```cpp
   EXPECT_CALL(*mock, setElementVisible(handles::kNotificationSentinel, true));
   ```

   This expectation fires because `notification_manager.cpp` calls `setElementVisible(0xDEAD0105u, true)` and `handles::kNotificationSentinel == 0xDEAD0105u`. No include of `tests/ui/panel_sentinel_handles.h` is needed in `notification_manager.cpp`.

4. **Phase 8 replacement**: When the real `NotificationManager::draw()` is implemented, the `constexpr UIElementHandle kNotifSentinel` line and the single `setElementVisible` call in `draw()` are replaced with real `IUIBackend` calls. The `// Phase 8 REPLACE THIS: real IUIBackend element handle` comment in the Phase 3 stub is the implementer's marker.

**Deficit-streak bridge testing note**: Phase 6 `CitySimulation` unit tests verify only that `getConsecutiveDeficitMonths()` returns the correct integer value. The full toast-dispatch chain (`UIManager::update()` polling to `NotificationManager::postCritical()`) is an integration test concern verified in Phase 8 using a real `UIManager` wired to a `MockUIBackend`. `MockUIManager` is NOT required — `UIManager` is always tested via its `IUIBackend` mock. See `game-over-flow.md` for the polling bridge design.

**`EXPECT_CALL` setup**: Use `InSequence seq;` and declare one `EXPECT_CALL(backend_, setElementVisible(handles::kXxxSentinel, true))` per panel in the exact back-to-front draw order specified in `ui-manager.md`. The `InSequence` guard causes GMock to fail immediately if any panel fires its sentinel out of order.

Example skeleton for the draw-order test (all 10 panel slots, back-to-front):

```cpp
TEST_F(UIManagerDrawOrderTest, DrawOrder_BackToFront_MatchesSpec) {
    // Transition to Gameplay so all panels are in their visible-by-default states
    // (MainMenuPanel hidden; HUD, Minimap, NotificationManager visible).
    ui_->transitionToGameplay(GameMode::Sandbox);

    ::testing::InSequence seq;
    // Back-to-front order per ui-manager.md Draw Order section:
    // 1. MainMenuPanel (hidden during Gameplay — sentinel NOT expected)
    // 2. Minimap
    EXPECT_CALL(backend_, setElementVisible(handles::kMinimapSentinel, true)).Times(1);
    // 3. HUD
    EXPECT_CALL(backend_, setElementVisible(handles::kHUDSentinel, true)).Times(1);
    // 4. FinancesPanel (hidden — sentinel NOT expected)
    // 5. InspectorPanel (hidden — sentinel NOT expected)
    // 6. NotificationManager toast stack
    EXPECT_CALL(backend_, setElementVisible(handles::kNotificationSentinel, true)).Times(1);
    // 7. PauseMenuPanel (hidden — sentinel NOT expected)
    // 8. SettingsPanel (hidden — sentinel NOT expected)
    // 9. Background scrim (not modal active — sentinel NOT expected)
    // 10. ModalDialog (hidden — sentinel NOT expected)

    ui_->draw();
}
```

Hidden panels (those whose `draw()` is guarded by a visibility check) must NOT call `setElementVisible` on their sentinel during `UIManager::draw()` when they are not visible — the `InSequence` expectation list covers only panels whose draw path is active in the given test state. Tests for other draw states (e.g. pause menu visible, modal active) follow the same pattern with the appropriate panels added to the expectation sequence.

**Second required draw-order test case — `UIManagerDrawOrder_ModalActive`**: When a modal dialog is active, the modal overlay (scrim + modal panel) must render above all HUD elements. This test verifies the back-to-front ordering contract holds in the modal-active draw state. The test must be authored in `tests/ui/ui_manager_draw_order_test.cpp` under the `UIManagerDrawOrderTest` fixture and registered under the `ui_tests` CMake target.

Test body skeleton:

```cpp
TEST_F(UIManagerDrawOrderTest, UIManagerDrawOrder_ModalActive) {
    // Transition to gameplay so HUD, Minimap, and NotificationManager are visible.
    ui_->transitionToGameplay(GameMode::Sandbox);
    // Open a modal dialog — this must make the scrim and modal panel visible.
    ui_->showModal(/* any ModalConfig */);

    ::testing::InSequence seq;
    // Back-to-front order with modal active (per ui-manager.md Draw Order section):
    // 1. MainMenuPanel (hidden — sentinel NOT expected)
    // 2. Minimap
    EXPECT_CALL(backend_, setElementVisible(handles::kMinimapSentinel,          true)).Times(1);
    // 3. HUD
    EXPECT_CALL(backend_, setElementVisible(handles::kHUDSentinel,              true)).Times(1);
    // 4. FinancesPanel (hidden — sentinel NOT expected)
    // 5. InspectorPanel (hidden — sentinel NOT expected)
    // 6. NotificationManager toast stack
    EXPECT_CALL(backend_, setElementVisible(handles::kNotificationSentinel,     true)).Times(1);
    // 7. PauseMenuPanel (hidden — sentinel NOT expected)
    // 8. SettingsPanel (hidden — sentinel NOT expected)
    // 9. Background scrim (modal IS active — sentinel EXPECTED, must appear AFTER all HUD elements)
    EXPECT_CALL(backend_, setElementVisible(handles::kScrimSentinel,            true)).Times(1);
    // 10. ModalDialog (modal IS active — sentinel EXPECTED, must appear AFTER scrim)
    EXPECT_CALL(backend_, setElementVisible(handles::kModalSentinel,            true)).Times(1);

    ui_->draw();
    // The InSequence guard guarantees:
    // (a) scrim fires AFTER kNotificationSentinel — modal overlay is above all HUD layers.
    // (b) modal fires AFTER scrim — dialog box is above its own background scrim.
    // Any out-of-order sentinel call causes an immediate GMock test failure.
}
```

The key assertions this test makes:

1. `handles::kScrimSentinel` fires after `handles::kNotificationSentinel` — the scrim draw call is ordered above every HUD layer, confirming the modal background covers HUD content.
2. `handles::kModalSentinel` fires after `handles::kScrimSentinel` — the modal dialog panel draws on top of its own scrim, placing interactive modal content at the topmost Z-order.
3. Both HUD panels (`kMinimapSentinel`, `kHUDSentinel`) and the notification stack (`kNotificationSentinel`) still fire in their normal relative order below the scrim — the modal-active state does NOT suppress HUD draws, it merely adds scrim and modal at the top.

This test is distinct from `DrawOrder_BackToFront_MatchesSpec` (which verifies the no-modal draw state). Both tests MUST be present: the first verifies the baseline draw order; the second verifies that modal activation correctly inserts the scrim and modal panel above all HUD elements without disrupting the HUD draw sequence itself.

```cpp
// UIManager draw-order tests — use NiceMock<MockUIBackend>.
// Rationale: Panel stub constructors call addStaticText() and other backend methods
// during UIManager construction. StrictMock fails on unexpected calls; NiceMock ignores
// them and only enforces explicit EXPECT_CALL expectations. All 8 active panels have
// draw-order tests using this fixture.
//
// Panel sentinel handles are defined in tests/ui/panel_sentinel_handles.h (see spec
// section below). Each stub panel's draw() calls setElementVisible(handles::kXxxSentinel,
// true) — NOT a no-op. InSequence is used to verify back-to-front draw order.
class UIManagerDrawOrderTest : public ::testing::Test {
protected:
    ::testing::NiceMock<MockUIBackend>         backend_;
    ::testing::NiceMock<MockAudioSystem>       audio_;
    ::testing::NiceMock<MockCitySimulation>    sim_;
    ManualClock                                clock_;
    std::unique_ptr<UIManager>                 ui_;
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }
    // **Mandatory**: This fixture MUST include the TearDown() override below.
    // Explicitly calling ui_.reset() before the fixture destructs documents and enforces
    // the destructor-path contract, preventing order-of-destruction issues with NiceMock
    // expectations. The current member declaration order (ui_ declared last) satisfies
    // the invariant automatically, but future reordering would silently break it.
    // The explicit TearDown() makes the contract immune to member reordering.
    void TearDown() override {
        // **Mandatory**: Reset ui_ before mock objects are destroyed so UIManager
        // destructor calls (e.g. backend_.removeElement()) happen while MockUIBackend
        // is still alive. This enforces the destructor-path contract.
        ui_.reset();
    }
};

// UIManagerDrawOrderTest — InSequence draw-order verification pattern:
//
// TEST_F(UIManagerDrawOrderTest, DrawOrder_BackToFront_MatchesSpec) {
//     ui_->transitionToGameplay(GameMode::Sandbox);
//     ::testing::InSequence seq;
//     EXPECT_CALL(backend_, setElementVisible(handles::kMinimapSentinel,           true)).Times(1);
//     EXPECT_CALL(backend_, setElementVisible(handles::kHUDSentinel,               true)).Times(1);
//     EXPECT_CALL(backend_, setElementVisible(handles::kNotificationSentinel,      true)).Times(1);
//     // Hidden panels (MainMenu, Finances, Inspector, PauseMenu, Settings, Scrim, Modal)
//     // are NOT listed here — their draw() is guarded and fires no sentinel.
//     ui_->draw();
// }

// UIManager modal/toast tests — use NiceMock for UIBackend, audio, and sim.
// Rationale for NiceMock<MockUIBackend>: UIManager construction creates all panels
// (including HUD which calls addStaticText for the unsaved-changes dot). StrictMock
// would fail on those unexpected construction-time calls. NiceMock ignores them and
// only enforces explicit EXPECT_CALL expectations set per test.
// TearDown() is required: UIManager destructor calls IUIBackend::removeElement() for all
// live elements; explicitly reset ui_ before MockUIBackend is destroyed to ensure
// correct destruction order.
//
// IMPORTANT: All three mock members (backend_, audio_, sim_) MUST be NiceMock.
// Using StrictMock<MockCitySimulation> for sim_ will cause SetUp() to fail immediately
// on any UIManager constructor call that queries simulation state without a pre-set
// EXPECT_CALL. For tests that need strict call-count verification on backend_ or sim_,
// use selective EXPECT_CALL with explicit Times() before the action under test.
//
// NICEMOCK + Times(0) CONTRACT:
// NiceMock silently ignores any call for which no EXPECT_CALL has been set — including
// calls that MUST NOT occur. This means that verifying the ABSENCE of a call requires
// an explicit EXPECT_CALL(...).Times(0) even on a NiceMock. Without it, a forbidden call
// is silently swallowed and the test passes incorrectly.
//
// MANDATORY APPROACH (option b — retain NiceMock, add explicit Times(0)):
// Every "must not be called" entry in the StrictMock Expected Call Matrix (e.g.
// setPaused(false) × 0 in test 8, setPaused(true) × 0 in test 10) MUST be expressed as
// an explicit EXPECT_CALL on the NiceMock member BEFORE the action under test:
//
//   EXPECT_CALL(sim_, setPaused(false)).Times(0);   // must not be called
//   EXPECT_CALL(sim_, setPaused(true)).Times(2);    // called exactly twice
//   ui_->showModal(...);
//   ui_->closeModal();
//
// A NiceMock without an explicit Times(0) CANNOT verify absence of calls. Every modal
// test that lists "× 0" in the StrictMock Expected Call Matrix MUST set the corresponding
// EXPECT_CALL(...).Times(0) in the test body. Omitting a Times(0) expectation means the
// test is NOT verifying that the call does not happen — it is silently passing.
//
// NOTE: Switching sim_ to StrictMock (option a) is NOT used here because UIManager's
// constructor queries simulation state, which would require EXPECT_CALL stubs for every
// query in SetUp() — defeating the fixture's purpose of minimising boilerplate. The
// NiceMock + explicit Times(0) approach (option b) is the mandatory pattern for this fixture.
class UIManagerModalTest : public ::testing::Test {
protected:
    ::testing::NiceMock<MockUIBackend>         backend_;
    ::testing::NiceMock<MockAudioSystem>       audio_;
    ::testing::NiceMock<MockCitySimulation>    sim_;
    ManualClock                                clock_;
    std::unique_ptr<UIManager>                 ui_;
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }
    // **Mandatory**: Each fixture MUST include a TearDown() override that explicitly calls
    // ui_.reset() before the fixture destructs. This documents and enforces the
    // destructor-path contract, preventing order-of-destruction issues with NiceMock
    // expectations. Without it, a future member reordering could cause UIManager's
    // destructor to fire after MockUIBackend is destroyed, producing use-after-destroy.
    void TearDown() override {
        // **Mandatory**: ui_.reset() must be called here. UIManager destructor calls
        // backend_.removeElement() for all live UI elements. Explicitly resetting ui_
        // before mock objects are destroyed ensures those destructor calls happen while
        // MockUIBackend is still alive, enforcing the destructor-path contract. The current
        // declaration order already satisfies the invariant automatically (ui_ declared last
        // is destroyed first in reverse order), but future fixture modifications could
        // inadvertently reorder members. The explicit TearDown() makes the contract
        // immune to member reordering.
        ui_.reset();
    }
};

// Unit tests — use StrictMock to catch unexpected calls:
class CitySimulationUnitTest : public ::testing::Test {
protected:
    ::testing::StrictMock<MockRenderer>    renderer_;
    ::testing::StrictMock<MockAudioSystem> audio_;
    // NOTE: TearDown does NOT call rng_.verifyAllConsumed() for the standard fixture.
    // Tests that need consumed-count verification must use a LOCAL ManualRNG with strict=true
    // and call verifyAllConsumed() in the test body explicitly. The shared rng_{0} member
    // uses strict=true but TearDown does not auto-verify, to allow zero-revenue tests that
    // never call nextInt() without causing over-provision throws.
    ManualRNG                              rng_{{0}};
    ManualClock                            clock_;
    std::unique_ptr<CitySimulation>        sim_;
    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(&renderer_, &audio_, &rng_, &clock_);
    }
    // **Mandatory**: Each fixture MUST include a TearDown() override that explicitly calls
    // sim_.reset() before the fixture destructs. This documents and enforces the
    // destructor-path contract, preventing order-of-destruction issues with StrictMock
    // expectations. CitySimulation must be destroyed before its injected mock dependencies
    // (renderer_, audio_). Without an explicit sim_.reset(), StrictMock will report
    // unexpected calls if CitySimulation's destructor ever calls renderer_ or audio_
    // after their expectations have been verified and cleared by the test framework.
    void TearDown() override {
        // **Mandatory**: sim_.reset() must be called here to enforce the destructor-path
        // contract. CitySimulation destructor must NOT call audio_ or renderer_ methods.
        // This is an explicit design contract. If that contract changes, add EXPECT_CALL
        // expectations here BEFORE sim_.reset() to avoid spurious StrictMock failures.
        // IMPORTANT: Do NOT call rng_.verifyAllConsumed() here — zero-revenue tests never
        // consume the rng_{0} sequence, and auto-verifying would throw for those tests.
        // Tests that require consumed-count verification must declare a LOCAL ManualRNG
        // and call verifyAllConsumed() explicitly at the end of the test body instead.
        sim_.reset();
    }
};

// Property-based / integration tests — use NiceMock to suppress unrelated call noise:
//
// RNG NOTE: Property tests that use RapidCheck's rc::gen generators must NOT inject
// ManualRNG — RapidCheck controls the random draws. For rc::check invariant tests, use
// a ProductionRNG or the real injectable RNG (not a ManualRNG). The shared rng_{{0}} in
// CitySimulationPropertyTest is a placeholder for property tests that exercise deterministic
// code paths only. Property tests that require randomness must declare a local RNG suitable
// for their draw pattern. The fixture's TearDown() does not call verifyAllConsumed() because
// the shared member is a placeholder.
//
// ManualRNG exemption: The rng_{{0}} member exists solely to satisfy CitySimulation's
// constructor requirement for an ISimulationRNG*. Property tests that drive randomness
// via RapidCheck's rc::gen infrastructure do NOT call nextInt() or nextFloat() on this
// member. TearDown() does NOT call verifyAllConsumed() — doing so would throw
// std::logic_error on every property test since the sequence is never consumed. This is
// the sole sanctioned exception to the over-provision detection rule established by
// ManualRNG_VerifyAllConsumed_ThrowsOnOverProvision.
class CitySimulationPropertyTest : public ::testing::Test {
protected:
    ::testing::NiceMock<MockRenderer>    renderer_;
    ::testing::NiceMock<MockAudioSystem> audio_;
    ManualRNG                            rng_{{0}};  // placeholder for deterministic-path tests only; see RNG NOTE above
    ManualClock                          clock_;
    std::unique_ptr<CitySimulation>      sim_;
    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(&renderer_, &audio_, &rng_, &clock_);
    }
    // **Mandatory**: Each fixture MUST include a TearDown() override that explicitly calls
    // sim_.reset() before the fixture destructs. This documents and enforces the
    // destructor-path contract, preventing order-of-destruction issues with NiceMock
    // expectations. NiceMock suppresses unexpected-call warnings but does NOT protect
    // against use-after-destroy if CitySimulation's destructor calls mock methods after
    // the mock objects have been destroyed. The explicit TearDown() ensures CitySimulation
    // is torn down first regardless of member declaration order.
    void TearDown() override {
        // **Mandatory**: sim_.reset() must be called here to enforce the destructor-path
        // contract. CitySimulation must be destroyed before mock objects. NiceMock
        // suppresses unexpected-call warnings but does NOT protect against use-after-destroy
        // if CitySimulation destructor calls mocks. Members are destroyed in reverse
        // declaration order (sim_ last), so sim_.reset() here ensures correct destruction
        // order regardless of any future fixture member reordering.
        // NOTE: verifyAllConsumed() is NOT called here — the shared rng_{{0}} is a
        // placeholder; property tests that drive random draws must use a local RNG.
        // See ManualRNG exemption comment above the class definition for full rationale.
        sim_.reset();
    }
};
```

### `NiceSimulationTestBase` Contract (Phase 11o)

#### Purpose

`NiceSimulationTestBase` is the base fixture for **behavioral and integration-style** simulation tests — tests that exercise a code path and assert on outcomes (return values, state changes, side-effects) **without asserting exact mock call counts** on every injected dependency. Use it when uninteresting calls to `renderer_` or `audio_` are noise rather than signal.

Contrast with `CitySimulationUnitTest`, which uses `StrictMock<>` on all injected mocks and requires an explicit `EXPECT_CALL` for every call the system under test makes. `StrictMock` is correct for precision call-count assertions; it becomes boilerplate overhead when the test does not care about renderer or audio interactions.

**Decision guide**:

| Scenario                                                                  | Use                                       |
| ------------------------------------------------------------------------- | ----------------------------------------- |
| Assert exact renderer/audio call counts (e.g., `placeBuildingMesh` × 1)   | `CitySimulationUnitTest` (`StrictMock`)   |
| Assert simulation state outcome; renderer/audio calls are incidental      | `NiceSimulationTestBase` (`NiceMock`)     |
| RapidCheck property invariant; randomness driven by RapidCheck generators | `CitySimulationPropertyTest` (`NiceMock`) |
| Behavioral tests — need `NiceMock` but NOT RapidCheck                     | `NiceSimulationTestBase` (`NiceMock`)     |

`NiceSimulationTestBase` fills the gap between `CitySimulationUnitTest` (strict, precision) and `CitySimulationPropertyTest` (property-based). Property tests inject a placeholder `ManualRNG` and do NOT call `verifyAllConsumed()`. Behavioral tests that use `NiceSimulationTestBase` MAY call `verifyAllConsumed()` in the test body when exact RNG consumption counts matter — it is not suppressed at the fixture level.

#### Constructor Parameters / Member Types

All injected dependencies are identical to `CitySimulationUnitTest`, but wrapped in `NiceMock<>`:

```cpp
// Behavioral / integration-style tests — NiceMock suppresses unexpected-call warnings
// on renderer_ and audio_ so tests can focus on simulation state assertions.
// Use this base when exact call counts on renderer or audio are NOT the test focus.
// For precise call-count assertions, use CitySimulationUnitTest (StrictMock) instead.
class NiceSimulationTestBase : public ::testing::Test {
protected:
    ::testing::NiceMock<MockRenderer>    renderer_;
    ::testing::NiceMock<MockAudioSystem> audio_;
    // ManualRNG default sequence {0}: satisfies CitySimulation constructor requirement.
    // Tests that need deterministic RNG draws should declare a LOCAL ManualRNG in the
    // test body and inject it via a re-constructed sim_ (or use ON_CALL / rng_ override).
    // TearDown() does NOT call rng_.verifyAllConsumed() — zero-revenue behavioral tests
    // never consume the sequence; auto-verifying would throw for those tests.
    ManualRNG                            rng_{{0}};
    ManualClock                          clock_;
    std::unique_ptr<CitySimulation>      sim_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(&renderer_, &audio_, &rng_, &clock_);
    }

    // **Mandatory**: TearDown() MUST explicitly call sim_.reset() before the fixture
    // destructs. This documents and enforces the destructor-path contract:
    // CitySimulation must be destroyed before its injected mock dependencies
    // (renderer_, audio_). NiceMock suppresses unexpected-call WARNINGS but does NOT
    // protect against use-after-destroy if CitySimulation's destructor calls a mock
    // method after that mock has been destroyed. The explicit sim_.reset() ensures
    // correct destruction order regardless of member declaration order, making the
    // contract immune to future fixture member reordering.
    void TearDown() override {
        // **Mandatory**: sim_.reset() must be called here.
        // CitySimulation destructor must NOT call audio_ or renderer_ methods.
        // This is an explicit design contract. If that contract changes, add EXPECT_CALL
        // expectations BEFORE sim_.reset() to avoid use-after-destroy.
        // NOTE: verifyAllConsumed() is NOT called here — see rng_ comment above.
        sim_.reset();
    }

    // Downcast sim_ to the concrete CitySimulation type to access methods not
    // present on the ICitySimulation interface. Uses static_cast (sim_ is always
    // constructed as CitySimulation) with a null guard so a misconfigured fixture
    // fails with a clear ASSERT rather than a hard crash.
    CitySimulation* cs() {
        auto* ptr = static_cast<CitySimulation*>(sim_.get());
        EXPECT_NE(ptr, nullptr) << "cs(): sim_ is null — was SetUp() called?";
        return ptr;
    }

    // Advance simulation time by n ticks, each of duration
    // SimulationConstants::SECONDS_PER_BUDGET_TICK. Calls clock_.advance(dt)
    // then cs()->tick(dt) for each tick so that both the injected clock and the
    // simulation state stay in sync. Eliminates per-test boilerplate of manually
    // looping clock.advance() + sim_->tick().
    void runTicks(int n) {
        const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
        for (int i = 0; i < n; ++i) {
            clock_.advance(dt);
            cs()->tick(dt);
        }
    }
};
```

#### Mock Types

| Member      | Type in `NiceSimulationTestBase`    | Type in `CitySimulationUnitTest`    |
| ----------- | ----------------------------------- | ----------------------------------- |
| `renderer_` | `NiceMock<MockRenderer>`            | `StrictMock<MockRenderer>`          |
| `audio_`    | `NiceMock<MockAudioSystem>`         | `StrictMock<MockAudioSystem>`       |
| `rng_`      | `ManualRNG{{0}}` (same)             | `ManualRNG{{0}}` (same)             |
| `clock_`    | `ManualClock` (same)                | `ManualClock` (same)                |
| `sim_`      | `unique_ptr<CitySimulation>` (same) | `unique_ptr<CitySimulation>` (same) |

`NiceMock<MockAudioSystem>` silently ignores calls for which no `EXPECT_CALL` is set — including calls that must NOT occur. When a behavioral test needs to assert the **absence** of a specific call (e.g., no audio during a no-op tick), use an explicit `EXPECT_CALL(audio_, ...).Times(0)` — NiceMock will NOT catch it otherwise.

#### SetUp / TearDown Sequences

The SetUp/TearDown sequences follow the same pattern as `CitySimulationUnitTest`:

1. **SetUp**: construct `sim_` from injected `renderer_`, `audio_`, `rng_`, `clock_`.
2. **TearDown**: call `sim_.reset()` explicitly before the fixture destructs — this enforces the destructor-path contract. Mock objects (`renderer_`, `audio_`) are destroyed after `sim_` returns from `reset()`.
3. **`verifyAllConsumed()` policy**: NOT called in `TearDown()`. Tests that require exact RNG consumption counts must call `rng_.verifyAllConsumed()` (or a local `ManualRNG::verifyAllConsumed()`) explicitly at the end of the test body.

#### Protected Helpers

`NiceSimulationTestBase` exposes two protected helper methods that eliminate repetitive boilerplate present in every NiceMock-based simulation fixture:

**`cs()`** — returns `sim_.get()` downcast to `CitySimulation*` via `static_cast`. Because `sim_` is always constructed as a `CitySimulation` object, the cast is safe; the helper adds an `EXPECT_NE(ptr, nullptr)` guard so a misconfigured fixture (e.g. `SetUp` not called) fails with a clear assertion message rather than a null-pointer crash. Use `cs()` whenever a test needs to call a `CitySimulation`-specific method that is not declared on the `ICitySimulation` interface (e.g. `setSpeed`, `setModalOpen`, `addServiceBuilding`, `serializeToJson`, `setMapDimensions`).

**`runTicks(int n)`** — advances the simulation by `n` budget ticks. Each tick advances `clock_` by `SimulationConstants::SECONDS_PER_BUDGET_TICK` seconds and then calls `cs()->tick(dt)`, keeping the injected `ManualClock` and the `CitySimulation` internal state in sync. This replaces the per-test pattern of writing a manual `for` loop with `clock_.advance(...)` + `sim_->tick(...)`. Pass `n = 1` for a single-tick state check; pass larger values to exercise multi-tick convergence behaviour (e.g. service-coverage propagation, forced-loan expiry, density level-up).

#### When to Choose Each Fixture

- **`CitySimulationUnitTest` (`StrictMock`)**: the test is verifying that a specific renderer or audio method is called exactly N times. Any unexpected call is a test failure. Use for `placeBuildingMesh`, `placeRoadMesh`, `playPositionalSound`, etc. call-count assertions. Requires exhaustive `EXPECT_CALL` coverage of every call the code path under test makes.

- **`NiceSimulationTestBase` (`NiceMock`)**: the test is verifying simulation state (tile zones, balances, density levels, service coverage, serialization round-trips, etc.) and renderer/audio calls are incidental. Unexpected calls are suppressed rather than failing. Reduces boilerplate when the test does not care about rendering side-effects. Still enforces absence of calls via explicit `EXPECT_CALL(...).Times(0)` when needed.

- **`CitySimulationPropertyTest` (`NiceMock` + RapidCheck)**: property invariant tests driven by RapidCheck `rc::gen` generators. The `rng_{{0}}` member is a placeholder only; property tests must NOT call `verifyAllConsumed()` in `TearDown()`. Choose this fixture exclusively for `rc::check`-based tests.

### WorldInteractionTest Fixture (Phase 9b)

`WorldInteractionTest` is the canonical test fixture for all Phase 9b world-interaction unit tests
(17 tests in `tests/ui/world_interaction_test.cpp`, registered under the `ui_tests` CMake target).

**Source file**: `tests/ui/world_interaction_test.cpp`
**CMake registration**: `target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp)` —
do NOT call `add_executable` or `aitown_add_tests` again (duplicate target error).
**Label**: `unit` (no `requires-opengl`).

**Fixture design**: All 17 `WorldInteractionTest` methods share a single fixture. The fixture
uses `StrictMock<MockCitySimulation>` and `StrictMock<MockRenderer>` to catch unexpected calls.
`NiceMock<MockUIBackend>` suppresses noise from UIManager constructor panel-creation calls
(50+ `addStaticText`/`addButton`/`setElementVisible` calls — `StrictMock<MockUIBackend>` would
require exhaustive construction-time `EXPECT_CALL` setup, defeating the fixture's purpose).

```cpp
// tests/ui/world_interaction_test.cpp
class WorldInteractionTest : public ::testing::Test {
protected:
    // Declaration order: mocks declared BEFORE uiManager_ so that
    // they are destroyed AFTER uiManager_ (reverse declaration order).
    // TearDown() makes this explicit regardless of member order.
    ::testing::StrictMock<MockCitySimulation> sim_;
    ::testing::StrictMock<MockRenderer>       renderer_;
    ManualTerrainQuery                        terrain_;   // global slope defaults to 0°
    ::testing::NiceMock<MockUIBackend>        backend_;
    ManualClock                               clock_;
    std::unique_ptr<UIManager>                uiManager_;

    void SetUp() override {
        // Construct UIManager with its locked 4-parameter constructor.
        // renderer_ and terrain_ are wired via setters (Phase 9b additions).
        uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
        uiManager_->setRenderer(&renderer_);
        uiManager_->setTerrainQuery(&terrain_);
        // setMapDimensions(10, 10) establishes m_mapTilesX=10, m_mapTilesZ=10 so that
        // zone overlay key computations use concrete, test-predictable values:
        //   tile (tileX=3, tileZ=4) → key = 4 * 10 + 3 = 43.
        uiManager_->setMapDimensions(10, 10);
    }

    // **Mandatory TearDown**: explicitly destroys UIManager before StrictMock members.
    // UIManager destructor calls backend_.removeElement() for all live UI elements;
    // it also holds raw m_renderer_ and m_terrain_ pointers that must be released
    // before the mock objects they point to are destroyed. Failure to reset uiManager_
    // here causes use-after-destroy in StrictMock verification.
    // The current member declaration order (uiManager_ declared last) automatically
    // satisfies this invariant via reverse-destruction order, but the explicit TearDown()
    // is mandatory to make the contract immune to future member reordering.
    void TearDown() override {
        uiManager_.reset();
    }
};
```

**SetUp sequence contract (REQUIRED)**:

1. `std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_)` — audio is `nullptr`
   (UIManager checks `if(m_audio)` before every audio call; passing null is safe here since
   no Phase 9b test exercises the audio path through UIManager).
2. `uiManager_->setRenderer(&renderer_)` — REQUIRED before any event that triggers
   `pickTerrainTile()`. Without this, the null-check guard in the world-interaction block
   returns `false` immediately and no `placeZone`/`placeRoad` calls fire.
3. `uiManager_->setTerrainQuery(&terrain_)` — REQUIRED before any event that triggers the
   earthworks cost computation (slope guard). Without this, `m_terrain` is null and
   `getSlopeDegrees()` is unreachable.
4. `uiManager_->setMapDimensions(10, 10)` — REQUIRED before any zone overlay test. Without
   this, `m_mapTilesX == 0` and all overlay writes are skipped by the guard.

**StrictMock expectations for each test**: See the StrictMock Expected Call Matrix section above
for the complete per-test expected call specification. Common patterns:

```cpp
// Stubbing pickTerrainTile to return tile (5,7):
EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
    .WillOnce(::testing::DoAll(
        ::testing::SetArgReferee<2>(5),
        ::testing::SetArgReferee<3>(7),
        ::testing::Return(true)));

// Stub getTileScreenBounds (needed when Query tool fires a tile lookup):
ON_CALL(renderer_, getTileScreenBounds(5, 7))
    .WillByDefault(::testing::Return(ScreenRect{100, 100, 50, 50}));

// Capture the sparse overlay map for overlay-content assertions:
std::unordered_map<uint64_t, uint32_t> capturedMap;
EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
    .WillOnce(::testing::SaveArg<2>(&capturedMap));

// Verify zone overlay key and colour after zone placement at (3,4):
// key = tileZ * m_mapTilesX + tileX = 4 * 10 + 3 = 43
ASSERT_EQ(capturedMap.size(), 1u);
EXPECT_EQ(capturedMap.at(43u), 0x6000FF00u);  // Residential green
```

**Active tool injection**: Tests set the active tool by simulating a toolbar button click event
or by calling `uiManager_->setActiveTool(ActiveTool::Zone)` if a direct setter is available.
The preferred approach is via toolbar button click to exercise the full dispatch path:

```cpp
// Set active tool to Zone via toolbar button click (Priority 5 dispatch):
InputEvent zoneBtn;
zoneBtn.type = InputEvent::Type::MouseButtonDown;
zoneBtn.x = 36;   // toolbar center x (virtual coords)
zoneBtn.y = 80;   // y:64-112 = Zone button row (virtual coords)
zoneBtn.button = 0;
uiManager_->onEvent(zoneBtn);
// m_activeTool is now ActiveTool::Zone
```

**Slope guard testing** (earthworks guard): Call `terrain_.setSlope(20.0f)` (global form, ≥15°
threshold triggers earthworks) BEFORE sending the left-click event. Set
`MockCitySimulation::getTreasuryBalance()` to return 0.0f (insufficient funds) to verify the
slope guard fires and `placeZone` is NOT called:

```cpp
terrain_.setSlope(20.0f);  // above 15° threshold; earthworks cost = 500 * clamp((20-15)/30,0,2) > 0
ON_CALL(sim_, getTreasuryBalance()).WillByDefault(::testing::Return(0.0f));
EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);  // must NOT be called
EXPECT_CALL(backend_, addStaticText(::testing::HasSubstr("insufficient funds"), _, _, _, _))
    .Times(::testing::AtLeast(1));
// ... send left-click event
```

**Overlay cap test** (`WorldInteraction_OverlayCap_100K_StillCalls`): driving 100,000
placements via 100K simulated events is impractical. Instead, access the overlay map
through a test-friend mechanism or call a hypothetical `injectOverlayEntry()` test helper.
Absent a test-friend, place 100K entries in a single-loop test using distinct tile coordinates
(vary tileX from 0 to 9 and tileZ from 0 to 9999 for a 10×10000 set — verify all with
`setMapDimensions(10, 10001)` to accommodate). The test verifies the 100,001st entry is
rejected and `setZoneOverlay` is still called with `size() <= 100000`.

**NiceMock MockUIBackend rationale**: UIManager's constructor calls `addStaticText`,
`addButton`, and `setElementVisible` for all panels (HUD, Minimap, MainMenu, etc.) during
initialization — typically 50+ backend calls. `StrictMock<MockUIBackend>` would require
exhaustive `EXPECT_CALL` stubs in `SetUp()` before any test-specific assertions can be set.
`NiceMock<MockUIBackend>` suppresses all unexpected calls silently, allowing test bodies to
declare only the specific `EXPECT_CALL` assertions relevant to the test being verified.
This is the same rationale as `UIManagerModalTest` and `UIManagerDeficitIntegrationTest`.
Compensating assertions: every test that needs to verify a specific `MockUIBackend` call
(e.g., `addStaticText(HasSubstr("insufficient funds"), ...)`) sets an explicit `EXPECT_CALL`
with `Times(AtLeast(1))` to prevent the `NiceMock` leniency from masking missing calls.

**Phase 9b canonical test names** (CTest `-R` filter expressions reference these exactly):

| Test Suite             | Test Case                                             | Fixture                |
| ---------------------- | ----------------------------------------------------- | ---------------------- |
| `WorldInteractionTest` | `ZonePlacement_CallsPlaceZone`                        | `WorldInteractionTest` |
| `WorldInteractionTest` | `RoadPlacement_CallsPlaceRoad`                        | `WorldInteractionTest` |
| `WorldInteractionTest` | `DemolishTool_SteepSlope_NoEarthworksGuard`           | `WorldInteractionTest` |
| `WorldInteractionTest` | `ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace` | `WorldInteractionTest` |
| `WorldInteractionTest` | `QueryTool_CallsQueryTile`                            | `WorldInteractionTest` |
| `WorldInteractionTest` | `NoActiveTool_LeftClickIgnored`                       | `WorldInteractionTest` |
| `WorldInteractionTest` | `ModalActive_LeftClickNotDispatched`                  | `WorldInteractionTest` |
| `WorldInteractionTest` | `HoverHighlight_SetOnMouseMove`                       | `WorldInteractionTest` |
| `WorldInteractionTest` | `HoverHighlight_ClearedOnMiss`                        | `WorldInteractionTest` |
| `WorldInteractionTest` | `UtilitiesPlacement_CallsPlaceServiceBuilding`        | `WorldInteractionTest` |
| `WorldInteractionTest` | `ZonePlacement_SparseOverlay_InsertsEntry`            | `WorldInteractionTest` |
| `WorldInteractionTest` | `Demolish_SparseOverlay_ErasesEntry`                  | `WorldInteractionTest` |
| `WorldInteractionTest` | `NewGameLoad_ClearsOverlay`                           | `WorldInteractionTest` |
| `WorldInteractionTest` | `OverlayCap_100K_StillCalls`                          | `WorldInteractionTest` |
| `WorldInteractionTest` | `SetMapDimensions_Recall_ClearsOverlay`               | `WorldInteractionTest` |
| `WorldInteractionTest` | `ZoneSubPanel_ButtonsInitialized`                     | `WorldInteractionTest` |
| `WorldInteractionTest` | `UtilitiesSubPanel_ButtonsInitialized`                | `WorldInteractionTest` |
| `AudioSimTest`         | `CitySimulation_PlaceRoad_FiresSFXRoadBuild`          | `AudioSimTest`         |

CTest filter for all Phase 9b world-interaction tests:
`-R "WorldInteractionTest|CitySimulation_PlaceRoad_FiresSFXRoadBuild"`

## Phase 10 Simulation Test Table

### `CitySimulationRenderTest` Fixture

**Source file**: `tests/simulation/city_simulation_render_test.cpp`

**CMake wiring**: `target_sources(simulation_tests PRIVATE tests/simulation/city_simulation_render_test.cpp)`.
Do NOT call `add_executable(simulation_tests ...)` or `aitown_add_tests(simulation_tests ...)` again — that
creates a duplicate target. Add `target_sources` only.

**Design rationale**: Uses a test-local `CitySimulationRenderStub` (a minimal dispatch-protocol model)
and `ISimRenderer` (a test-local standalone interface using `TileCoord`-based API). Does NOT use the
full `CitySimulation` class or `IRenderer`/`MockRenderer` — `IRenderer` has 27 pure virtuals as of Phase 11d; extending
it in a test would require all of them mocked. The stub faithfully models the render-dispatch contract
(same `placeBuildingMesh`, `placeRoadMesh`, `removeBuildingMesh`, `removeRoadMesh`, `placeServiceBuildingMesh`
call sequence) without incurring the `ManualRNG`, `ManualClock`, treasury, and audio side-effect
dependencies of the full simulation.

**Fixture setup**: Uses `NiceMock<MockSimRenderer>` and `NiceMock<MockMusicIntensityReceiver>`.

```cpp
class CitySimulationRenderTest : public ::testing::Test {
protected:
    void SetUp() override {
        renderer_      = std::make_unique<NiceMock<MockSimRenderer>>();
        musicReceiver_ = std::make_unique<NiceMock<MockMusicIntensityReceiver>>();
        sim_ = std::make_unique<CitySimulationRenderStub>(
            renderer_.get(), musicReceiver_.get());
    }
    void TearDown() override {
        sim_.reset();
        renderer_.reset();
        musicReceiver_.reset();
    }

    std::unique_ptr<NiceMock<MockSimRenderer>>            renderer_;
    std::unique_ptr<NiceMock<MockMusicIntensityReceiver>> musicReceiver_;
    std::unique_ptr<CitySimulationRenderStub>             sim_;
};
```

**Label**: `unit`. CTest filter: `-R CitySimulationRenderTest`.

| Test Case                                                         | What is verified                                                                                                                                     |
| ----------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh`           | `placeZone()` calls `placeBuildingMesh()` with correct TileCoord, ZoneType, and density tier                                                         |
| `CitySimulationRenderTest_PlaceRoad_PlacesRoadMesh`               | `placeRoad()` calls `placeRoadMesh()` with correct TileCoord                                                                                         |
| `CitySimulationRenderTest_DemolishZone_RemovesBuildingMesh`       | `demolishTile()` on zone tile calls `removeBuildingMesh()`, NOT `removeRoadMesh()`                                                                   |
| `CitySimulationRenderTest_DemolishRoad_RemovesRoadMesh`           | `demolishTile()` on road tile calls `removeRoadMesh()`, NOT `removeBuildingMesh()`                                                                   |
| `CitySimulationRenderTest_PlaceServiceBuilding_PlacesServiceMesh` | `placeServiceBuilding()` calls `placeServiceBuildingMesh()` with correct TileCoord and ServiceBuildingType                                           |
| `CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh`       | `testForceUnlockDensityTier()` calls `removeBuildingMesh()` then `placeBuildingMesh()` (upgraded tier) in order; guarded by `AITOWN_TESTING_ENABLED` |
| `CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit`        | Two consecutive deficit ticks trigger CALM→CRISIS; third tick no duplicate; recovery fires CRISIS→CALM                                               |

#### `CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh`

```cpp
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh)
{
    const TileCoord tile{5, 3};
    EXPECT_CALL(*renderer_,
        placeBuildingMesh(
            ::testing::Field(&TileCoord::x, 5),
            ZoneType::Residential, 0))
        .Times(1);
    sim_->placeZone(tile, ZoneType::Residential, /*densityTier=*/0);
}
```

#### `CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh`

Guarded by `#ifdef AITOWN_TESTING_ENABLED`; skipped via `GTEST_SKIP()` when undefined.
`testForceUnlockDensityTier()` sets the internal unlock flag directly, bypassing the
3-consecutive-month revenue gate — must NOT be compiled into production builds.

```cpp
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh)
{
#ifdef AITOWN_TESTING_ENABLED
    const TileCoord tile{4, 4};
    sim_->placeZone(tile, ZoneType::Residential, 0);
    {
        ::testing::InSequence seq;
        EXPECT_CALL(*renderer_,
            removeBuildingMesh(::testing::Field(&TileCoord::x, 4))).Times(1);
        EXPECT_CALL(*renderer_,
            placeBuildingMesh(
                ::testing::Field(&TileCoord::x, 4),
                ZoneType::Residential, 1)).Times(1);
    }
    sim_->testForceUnlockDensityTier(tile, ZoneType::Residential, /*newTier=*/1);
#else
    GTEST_SKIP() << "AITOWN_TESTING_ENABLED not set — density tier test skipped";
#endif
}
```

#### `CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit`

```cpp
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit)
{
    sim_->simulateBudgetTick(-0.60f);  // deficit month 1 — still CALM
    EXPECT_CALL(*musicReceiver_, setMusicIntensity(MusicIntensity::CRISIS)).Times(1);
    sim_->simulateBudgetTick(-0.60f);  // deficit month 2 → CRISIS
    sim_->simulateBudgetTick(-0.60f);  // deficit month 3 — no change
    EXPECT_CALL(*musicReceiver_, setMusicIntensity(MusicIntensity::CALM)).Times(1);
    sim_->simulateBudgetTick(+0.20f);  // recovery → CALM
}
```

### `AdaptiveMusicIntensityTest` Fixture

**Source file**: `tests/simulation/adaptive_music_intensity_test.cpp`

**CMake wiring**: `target_sources(simulation_tests PRIVATE tests/simulation/adaptive_music_intensity_test.cpp)`.
Do NOT call `add_executable` or `aitown_add_tests` again.

**Test target**: `simulation_tests` (NOT `audio_tests`).

**Design**: Uses a test-local `IMusicIntensityReceiver` interface and `MockMusicIntensityReceiver`
(strict mock) with a `dispatchMusicIntensityIfChanged()` helper that models the state-change dispatch
rule. Does NOT use the full `CitySimulation` — the dispatch protocol is verified directly without the
full economic simulation dependency chain.

**Fixture setup**:

```cpp
class AdaptiveMusicIntensityTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_ = std::make_unique<StrictMock<MockMusicIntensityReceiver>>();
    }
    void TearDown() override { mock_.reset(); }

    std::unique_ptr<StrictMock<MockMusicIntensityReceiver>> mock_;
};
```

**Mock policy**: `StrictMock` (per CLAUDE.md unit-test policy) — any unexpected call to
`setMusicIntensity()` immediately fails the test, enforcing that same-state transitions do NOT
dispatch.

**What is verified**: `dispatchMusicIntensityIfChanged()` calls `setMusicIntensity()` with the correct
`MusicIntensity` value when state changes, and does NOT call it when state is unchanged (all 6
permutations of CALM/GROWTH/CRISIS transitions, plus same-state no-ops, plus direct skip transitions).

**Required test cases**:

#### `AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem`

Single multi-phase test covering all transitions. Verifies CALM→GROWTH, GROWTH→CRISIS, CRISIS→CALM,
same-state no-ops (3 combinations), CALM→CRISIS skip, CRISIS→GROWTH skip, and the 2-consecutive-deficit
CRISIS trigger sequence.

**CTest filter**: `-R AdaptiveMusicIntensityTest`

### Phase 10 Canonical Test Name Summary

| Test Suite                   | Test Case                                                         | Source File                                          | CMake Target       | Label  |
| ---------------------------- | ----------------------------------------------------------------- | ---------------------------------------------------- | ------------------ | ------ |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh`           | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_PlaceRoad_PlacesRoadMesh`               | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_DemolishZone_RemovesBuildingMesh`       | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_DemolishRoad_RemovesRoadMesh`           | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_PlaceServiceBuilding_PlacesServiceMesh` | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh`       | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `CitySimulationRenderTest`   | `CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit`        | `tests/simulation/city_simulation_render_test.cpp`   | `simulation_tests` | `unit` |
| `AdaptiveMusicIntensityTest` | `AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem`           | `tests/simulation/adaptive_music_intensity_test.cpp` | `simulation_tests` | `unit` |

CTest filter for all Phase 10 simulation render and music intensity tests:
`-R "CitySimulationRenderTest|AdaptiveMusicIntensityTest"`

### Phase 10b Canonical Test Name Summary

| Test Suite                 | Test Case                                                    | Source File                                        | CMake Target       | Label             |
| -------------------------- | ------------------------------------------------------------ | -------------------------------------------------- | ------------------ | ----------------- |
| `TerrainFlatteningTest`    | `TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild`       | `tests/terrain/terrain_flattening_test.cpp`        | `terrain_tests`    | `unit`            |
| `TerrainFlatteningTest`    | `TerrainFlattening_NeighborBlend_ClampedToMapBounds`         | `tests/terrain/terrain_flattening_test.cpp`        | `terrain_tests`    | `unit`            |
| `TerrainFlatteningSimTest` | `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight` | `tests/simulation/terrain_flattening_sim_test.cpp` | `simulation_tests` | `unit`            |
| `CloudPlaneTest`           | `CloudPlane_Init_CreatesCloudNode`                           | `tests/rendering/cloud_plane_test.cpp`             | `opengl_tests`     | `requires-opengl` |

CTest filter for all Phase 10b terrain flattening and cloud plane tests:
`-R "TerrainFlatteningTest|TerrainFlatteningSimTest|CloudPlaneTest"`

### MinimapOverlayTest / MinimapElementLeakTest Fixture (Phase 11p)

Two test fixtures cover the Phase 11p minimap overlay tests. Both live in
`tests/ui/minimap_overlay_test.cpp` under the `ui_tests` CMake target with label `unit`.

#### `MinimapOverlayTest` Fixture (Groups 1--9 and 11)

The primary fixture for minimap overlay draw logic, colour mapping, camera-frustum
indicator, zoom interaction, and click-to-pan dispatch.

**Members**:

```cpp
class MinimapOverlayTest : public ::testing::Test {
protected:
    ::testing::NiceMock<MockUIBackend>       m_backend;
    ::testing::NiceMock<MockCitySimulation>  m_sim;
    ManualClock                              m_clock;
    std::unique_ptr<Minimap>                 m_minimap;

    void SetUp() override {
        // Non-zero map dimensions prevent division-by-zero in the viewport
        // projection formula (worldW = getMapTilesX() * kTileSize used as divisor).
        ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(64));
        ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(64));
        // Defensive guard: prevents accidental double-consumption if Minimap ever
        // incorrectly calls consumeBudgetTicks() directly (does NOT populate cache).
        ON_CALL(m_sim, consumeBudgetTicks()).WillByDefault(Return(0));
        m_minimap = std::make_unique<Minimap>(&m_backend, nullptr, &m_sim, &m_clock);
    }

    void TearDown() override {
        // **Mandatory destructor-path contract**: Reset m_minimap to nullptr
        // BEFORE NiceMock<MockUIBackend> and NiceMock<MockCitySimulation> are
        // destroyed. During Minimap destruction, removeElement() and other
        // backend calls may fire. If the mocks are already destroyed at that
        // point, the test process crashes with a use-after-free. Resetting
        // here also prevents unexpected removeElement() calls from triggering
        // NiceMock warnings that could obscure real test failures.
        m_minimap.reset();
    }
};
```

**NiceMock rationale**: `Minimap` constructor and `drawOverlay()` invoke many
incidental `IUIBackend` methods (element creation, visibility toggling, alpha
setting) and `ICitySimulation` queries (`getMapTilesX`, `getMapTilesZ`,
`queryTile`). Using `NiceMock` silences these unconfigured calls while explicit
`EXPECT_CALL` declarations in individual tests still enforce the contracts under
examination. This matches the `NiceMock` pattern established by
`UIManagerModalTest` and `NiceSimulationTestBase`.

**Budget-tick injection**: `Minimap` populates its internal tile-cache snapshot
when `onBudgetTicks(int count)` is called — this is the forwarded path from
`UIManager::update()`. Tests that need a populated cache MUST call this method
directly before exercising draw logic; they must NOT attempt to trigger cache
population via `consumeBudgetTicks()` mock overrides, because `Minimap` never
calls `consumeBudgetTicks()` itself (that call belongs to `UIManager`). The
correct three-step sequence for any test requiring a populated cache is:

1. `m_minimap->onBudgetTicks(1);` -- populate tile-cache snapshot
2. `m_minimap->draw();` -- advance internal state
3. `m_minimap->drawOverlay();` -- exercise the code under test

A defensive `ON_CALL(m_sim, consumeBudgetTicks()).WillByDefault(Return(0))` in
fixture `SetUp()` prevents accidental double-consumption if `Minimap` ever
incorrectly calls `consumeBudgetTicks()` directly, but this guard does **not**
trigger cache population on its own.

**Null-sim construction variant**: three Phase 11d tests exercise the code path
where no `ICitySimulation` is wired to `Minimap` — specifically:
`SetOverlayMode_Traffic_NoSim_FallsBackToNone`,
`Draw_TrafficOverlay_NoSimulation_NoQueryCall`, and
`Draw_ServiceCoverageOverlay_NoSimulation_NoQueryCall`. These three tests MUST
construct `Minimap` with `nullptr` as the `ICitySimulation*` parameter:

```cpp
minimap_ = std::make_unique<Minimap>(&m_backend, nullptr, nullptr, &m_clock);
```

All other tests in the fixture use `&m_sim`. The three null-sim tests may be
kept in the same fixture file but must not use the shared `SetUp()` — either
override `SetUp()` in a separate `MinimapNoSimTest` fixture, or construct the
minimap directly inside each test body, bypassing `SetUp()`.

#### `MinimapElementLeakTest` Fixture (Group 10)

A separate fixture class dedicated to verifying that `Minimap::drawOverlay()`
does not leak UI elements across successive draw passes. The fixture enforces
that repeated draw calls reuse existing colour-rect primitives rather than
creating new text or button elements.

**Members**:

```cpp
class MinimapElementLeakTest : public ::testing::Test {
protected:
    ::testing::NiceMock<MockUIBackend>       m_backend;
    ::testing::NiceMock<MockCitySimulation>  m_sim;
    std::unique_ptr<Minimap>                 m_minimap;

    void SetUp() override {
        // Non-zero map dimensions prevent division-by-zero in viewport formula.
        ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(64));
        ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(64));
        // Default queryTile returns a zoned Residential tile so drawOverlay()
        // emits at least one fillColoredRect call, satisfying Times(AtLeast(1)).
        QueryResult zonedTile;
        zonedTile.isZoned   = true;
        zonedTile.zoneType  = ZoneType::Residential;
        ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(zonedTile));
        m_minimap = std::make_unique<Minimap>(&m_backend, nullptr, &m_sim, nullptr);
        // Populate the tile cache so drawOverlay() has data to render.
        m_minimap->onBudgetTicks(1);
    }

    void TearDown() override {
        // Same destructor-path contract as MinimapOverlayTest — reset
        // m_minimap before mocks are destroyed to prevent use-after-free
        // and suppress spurious NiceMock warnings from removeElement().
        m_minimap.reset();
    }
};
```

**Core contracts enforced per draw pass**:

- `EXPECT_CALL(m_backend, addStaticText(...)).Times(0)` -- no new text elements
  created during a redraw (elements are created once, then reused).
- `EXPECT_CALL(m_backend, addButton(...)).Times(0)` -- no new button elements
  created during a redraw.
- `EXPECT_CALL(m_backend, fillColoredRect(...)).Times(AtLeast(1))` -- at least
  one colour-rect draw occurs per pass, preventing vacuous tests that pass
  because `drawOverlay()` silently short-circuits.

These three expectations together guarantee that the minimap draw path is both
active (non-vacuous) and leak-free (no element accumulation).

**CTest filter**: `-R "MinimapOverlayTest|MinimapElementLeakTest"`

### Phase 11q Canonical Test Name Summary

| Test Suite             | Test Case                                                                      | Source File                            | CMake Target       | Label             |
| ---------------------- | ------------------------------------------------------------------------------ | -------------------------------------- | ------------------ | ----------------- |
| `TrafficVehicleTest`   | `VehicleMeshPath_CommercialZone_ReturnsBusMesh`                                | `tests/simulation/VehicleZoneTest.cpp` | `simulation_tests` | `unit`            |
| `TrafficVehicleTest`   | `VehicleMeshPath_IndustrialZone_ReturnsTruckMesh`                              | `tests/simulation/VehicleZoneTest.cpp` | `simulation_tests` | `unit`            |
| `TrafficVehicleTest`   | `TrafficVehicle_SpawnOnUnzonedDestination_FallsBackToProportionalDistribution` | `tests/simulation/VehicleZoneTest.cpp` | `simulation_tests` | `unit`            |
| `TrafficVehicleTest`   | `TrafficVehicle_ZoneUpdated_OnTripCompletion`                                  | `tests/simulation/VehicleZoneTest.cpp` | `simulation_tests` | `unit`            |
| `IrrlichtRendererTest` | `MoveVehicleAgent_FlatTerrain_VehicleYIncludesBias`                            | `tests/rendering/VehicleYBiasTest.cpp` | `opengl_tests`     | `requires-opengl` |
| `IrrlichtRendererTest` | `SpawnVehicleAgent_FlatTerrain_VehicleYIncludesBias`                           | `tests/rendering/VehicleYBiasTest.cpp` | `opengl_tests`     | `requires-opengl` |
| `IrrlichtRendererTest` | `MoveVehicleAgent_SlopedTerrain_AppliesPitchAndRoll`                           | `tests/rendering/VehicleYBiasTest.cpp` | `opengl_tests`     | `requires-opengl` |
| `IrrlichtRendererTest` | `MoveVehicleAgent_SlopedTerrain_YawRelativeDecomposition`                      | `tests/rendering/VehicleYBiasTest.cpp` | `opengl_tests`     | `requires-opengl` |

**Test details**:

- `VehicleMeshPath_CommercialZone_ReturnsBusMesh` — `vehicleMeshPath()` returns
  the bus mesh path for a Commercial zone destination.
- `VehicleMeshPath_IndustrialZone_ReturnsTruckMesh` — `vehicleMeshPath()` returns
  the truck mesh path for an Industrial zone destination.
- `TrafficVehicle_SpawnOnUnzonedDestination_FallsBackToProportionalDistribution` —
  when the destination tile is unzoned, the vehicle type falls back to the 70/20/10
  RNG proportional distribution; uses `ManualRNG` seeded with `[72]` and asserts
  the result maps to Commercial.
- `TrafficVehicle_ZoneUpdated_OnTripCompletion` — the vehicle zone type is
  re-evaluated via the public `tick()` API when the destination changes to a
  zoned tile.
- `MoveVehicleAgent_FlatTerrain_VehicleYIncludesBias` — after `moveVehicleAgent()`
  on flat terrain, the scene node Y coordinate includes `kRoadSurfaceYBias`.
- `SpawnVehicleAgent_FlatTerrain_VehicleYIncludesBias` — at spawn time on flat
  terrain, the scene node Y coordinate includes `kRoadSurfaceYBias`.
- `MoveVehicleAgent_SlopedTerrain_AppliesPitchAndRoll` — on a Z-slope
  (`+Z = 1.0`) with `yaw = 0`, `rotation.X != 0` (pitch) and `rotation.Z == 0`
  (no roll).
- `MoveVehicleAgent_SlopedTerrain_YawRelativeDecomposition` — on a pure Z-slope
  (`+Z = 1.0`) with `yaw = 90`, the Z-axis slope is perpendicular to the
  vehicle heading, producing roll (`rotation.Z != 0`) and no pitch
  (`rotation.X == 0` within 1e-3 tolerance). Complement of the yaw=0 test;
  together they prove yaw-relative decomposition correctness.

CTest filter for all Phase 11q vehicle zone and Y-bias tests:
`-R "TrafficVehicleTest|IrrlichtRendererTest.*(VehicleY|SlopedTerrain)"`

### Phase 11q6 Test Fixtures

Four test fixtures cover the Phase 11q6 vehicle-agent despawn UAF fix,
shared-mesh reference-count safety, `SceneEntityManager::destroy()` ordering
contract, and audio source-pool release after vehicle despawn. Two fixtures
require an OpenGL context (`opengl_tests`, label `requires-opengl`); two use
headless backends (`integration_tests`, label `integration`).

#### `AgentDespawnRenderTest` Fixture

**Source file**: `tests/rendering/AgentDespawnRenderTest.cpp`

**CMake target**: `opengl_tests` (added inline to `add_executable(opengl_tests ...)`
per `framework.md` line 126 -- NOT via `target_sources()`).

**Label**: `requires-opengl`.

**Design rationale**: Directly reproduces the crash path from the
`despawnVehicleAgent` UAF (production frame 15455). Uses a real 1x1 EDT_OPENGL
device and `IrrlichtRenderer` instance (same pattern as Phase 11q
`VehicleYBiasTest.cpp`). All `drawAll()` calls are wrapped in
`beginScene`/`endScene` to match the production draw loop. A `GTEST_SKIP()` asset
guard skips the test gracefully when the vehicle mesh file is not found on disk.

**Fixture setup**:

```cpp
class AgentDespawnRenderTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice*          device_   = nullptr;
    irr::video::IVideoDriver*     driver_   = nullptr;
    irr::scene::ISceneManager*    smgr_     = nullptr;
    std::unique_ptr<IrrlichtRenderer> renderer_;

    void SetUp() override {
        // 1x1 EDT_OPENGL device -- minimal GL context for drawAll() validation.
        irr::SIrrlichtCreationParameters params;
        params.DriverType  = irr::video::EDT_OPENGL;
        params.WindowSize  = irr::core::dimension2d<irr::u32>(1, 1);
        params.Stencilbuffer = false;
        params.Vsync         = false;
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr) << "EDT_OPENGL device creation failed";
        driver_ = device_->getVideoDriver();
        smgr_   = device_->getSceneManager();
        renderer_ = std::make_unique<IrrlichtRenderer>(device_);
    }

    void TearDown() override {
        renderer_.reset();  // release renderer before device drop
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};
```

**Asset guard pattern** (used in each test that spawns agents):

```cpp
renderer_->spawnVehicleAgent(handle, zone);
if (!renderer_->agentNodeForTest(handle)) {
    GTEST_SKIP() << "vehicle mesh asset not found";
}
```

**beginScene/endScene wrapper** (used around every `drawAll()` call):

```cpp
driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
smgr_->drawAll();
driver_->endScene();
```

#### `SharedMeshRefCountTest` Fixture

**Source file**: `tests/rendering/SharedMeshRefCountTest.cpp`

**CMake target**: `opengl_tests` (added inline to `add_executable(opengl_tests ...)`).

**Label**: `requires-opengl`.

**Design rationale**: Reproduces the shared-mesh crash where two vehicle agents
share the same `IAnimatedMesh*` from the scene-manager cache (same zone type AND
same `handle % 3` variant index). Handles 0 and 3 are used (both `% 3 == 0`,
both `ZoneType::Residential` -- same `car_sedan_lod0.b3d`). A precondition
`ASSERT_EQ` verifies both nodes share the same mesh pointer before exercising
the despawn path. Uses the same 1x1 EDT_OPENGL device pattern and asset guard
as `AgentDespawnRenderTest`.

**Fixture setup**:

```cpp
class SharedMeshRefCountTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice*          device_   = nullptr;
    irr::video::IVideoDriver*     driver_   = nullptr;
    irr::scene::ISceneManager*    smgr_     = nullptr;
    std::unique_ptr<IrrlichtRenderer> renderer_;

    void SetUp() override {
        // 1x1 EDT_OPENGL device -- same pattern as AgentDespawnRenderTest.
        irr::SIrrlichtCreationParameters params;
        params.DriverType  = irr::video::EDT_OPENGL;
        params.WindowSize  = irr::core::dimension2d<irr::u32>(1, 1);
        params.Stencilbuffer = false;
        params.Vsync         = false;
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr) << "EDT_OPENGL device creation failed";
        driver_ = device_->getVideoDriver();
        smgr_   = device_->getSceneManager();
        renderer_ = std::make_unique<IrrlichtRenderer>(device_);
    }

    void TearDown() override {
        renderer_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};
```

**Shared-mesh precondition check** (required before exercising despawn):

```cpp
ASSERT_EQ(renderer_->agentNodeForTest(0)->getMesh(),
          renderer_->agentNodeForTest(3)->getMesh())
    << "precondition: both nodes must share the same IAnimatedMesh*";
```

#### `SceneEntityManagerDestroyOrderTest` Fixture

**Source file**: `tests/integration/SceneEntityManagerDestroyOrderTest.cpp`

**CMake target**: `integration_tests` (added via
`target_sources(integration_tests PRIVATE tests/integration/SceneEntityManagerDestroyOrderTest.cpp)`).

**Label**: `integration`.

**Design rationale**: Verifies the null-before-remove ordering contract on
`SceneEntityManager::destroy()` -- the canonical eviction pattern. Uses an
EDT_NULL device (no GL context needed, no xvfb dependency). The observable
postcondition is `entity.getNode() == nullptr` after `destroy()` returns.

**Fixture setup**:

```cpp
class SceneEntityManagerDestroyOrderTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice*       device_ = nullptr;
    irr::scene::ISceneManager* smgr_   = nullptr;

    void SetUp() override {
        // EDT_NULL -- no GL context required; runs in headless CI without xvfb.
        device_ = irr::createDevice(irr::video::EDT_NULL,
                                    irr::core::dimension2d<irr::u32>(1, 1));
        ASSERT_NE(device_, nullptr) << "EDT_NULL device creation failed";
        smgr_ = device_->getSceneManager();
    }

    void TearDown() override {
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};
```

### Phase 11q6 Canonical Test Name Summary

| Test Suite                           | Test Case                                       | Source File                                                | CMake Target        | Label             |
| ------------------------------------ | ----------------------------------------------- | ---------------------------------------------------------- | ------------------- | ----------------- |
| `AgentDespawnRenderTest`             | `DespawnThenDrawScene_Clean`                    | `tests/rendering/AgentDespawnRenderTest.cpp`               | `opengl_tests`      | `requires-opengl` |
| `AgentDespawnRenderTest`             | `DespawnNonexistentHandle_NoOp`                 | `tests/rendering/AgentDespawnRenderTest.cpp`               | `opengl_tests`      | `requires-opengl` |
| `AgentDespawnRenderTest`             | `DespawnAllAgents_DrawScene_Clean`              | `tests/rendering/AgentDespawnRenderTest.cpp`               | `opengl_tests`      | `requires-opengl` |
| `AgentDespawnRenderTest`             | `SpawnSameHandleTwice_NoLeak`                   | `tests/rendering/AgentDespawnRenderTest.cpp`               | `opengl_tests`      | `requires-opengl` |
| `SceneEntityManagerDestroyOrderTest` | `NullsEntityBeforeNodeRemove`                   | `tests/integration/SceneEntityManagerDestroyOrderTest.cpp` | `integration_tests` | `integration`     |
| `SharedMeshRefCountTest`             | `LastAgentDespawn_OtherNodesUnaffected`         | `tests/rendering/SharedMeshRefCountTest.cpp`               | `opengl_tests`      | `requires-opengl` |
| `AudioSystemVehicleReleaseTest`      | `SourceStoppedAfterRelease`                     | `tests/integration/VehicleReleaseTest.cpp`                 | `integration_tests` | `integration`     |
| `AudioSystemVehicleReleaseTest`      | `SlotReacquirableAfterRelease`                  | `tests/integration/VehicleReleaseTest.cpp`                 | `integration_tests` | `integration`     |
| `QueryPanelIntegrationTest`          | `Populate_ServiceBuilding_ShowsType_NotUnzoned` | `tests/ui/query_panel_test.cpp`                            | `ui_tests`          | `unit`            |

**Test details**:

- `DespawnThenDrawScene_Clean` -- spawns 1 agent (handle=0), despawns it, then
  calls `drawAll()` inside `beginScene`/`endScene`. Reaching `SUCCEED()` without
  crash confirms the UAF is fixed; ASAN clean exit provides secondary validation.
- `DespawnNonexistentHandle_NoOp` -- calls `despawnVehicleAgent(handle=99)`
  without any prior spawn. The early-return guard must not crash or fault.
- `DespawnAllAgents_DrawScene_Clean` -- spawns 3 agents (handles 0, 1, 2 --
  three distinct mesh variants), despawns all three, then calls `drawAll()`.
  Verifies the empty-scene draw path after bulk eviction.
- `SpawnSameHandleTwice_NoLeak` -- spawns handle=0 twice (Residential then
  Commercial). The internal replace-guard must despawn the first before spawning
  the second; asserts only one entry exists in the agent-node map (no leak).
  Followed by `drawAll()` to confirm the replacement node renders cleanly.
- `NullsEntityBeforeNodeRemove` -- creates a minimal entity with an EDT_NULL
  scene node, calls `SceneEntityManager::destroy()`, asserts
  `entity.getNode() == nullptr` as the postcondition. Validates the
  null-before-remove ordering contract without requiring an OpenGL context.
- `LastAgentDespawn_OtherNodesUnaffected` -- spawns agents with handles 0 and 3
  (both `% 3 == 0`, both `ZoneType::Residential` -- same cached
  `IAnimatedMesh*`). Despawns handle 0, then calls `drawAll()`. The surviving
  agent (handle 3) must still render without fault because the shared mesh's
  reference count was correctly maintained.
- `SourceStoppedAfterRelease` -- acquires a vehicle engine pair via
  `acquireVehicleEnginePair(zone)`, retrieves the underlying AL source handles
  via `testGetSourceHandle()`, calls `releaseVehicleEnginePair()`, then asserts
  both sources are in the `AL_STOPPED` state (not `AL_PLAYING` or
  `AL_PAUSED`). Confirms the audio source-pool cleanup path after vehicle
  despawn.
- `SlotReacquirableAfterRelease` -- after releasing a vehicle engine pair,
  calls `acquireVehicleEnginePair()` again and asserts that valid pool indices
  are returned (>= 0). Confirms the pool slot is genuinely freed and
  reusable, not leaked.
- `Populate_ServiceBuilding_ShowsType_NotUnzoned` -- constructs a `QueryResult`
  with `isZoned=false`, `isRoad=false`, and
  `serviceType=ServiceBuildingType::FireStation` (tile inside a service building
  footprint). Calls `InspectorPanel::populate()` via the `MockUIBackend` fixture
  and asserts that `setElementText` is called with a string containing
  "Fire Station" (not "Unzoned"). Verifies the
  `else if (result.serviceType != ServiceBuildingType::None)` branch added by
  Phase 11q6 Issue 6.

**CTest filter** for all Phase 11q6 tests:
`-R "AgentDespawnRenderTest|SharedMeshRefCountTest|SceneEntityManagerDestroyOrderTest|AudioSystemVehicleReleaseTest|QueryPanelIntegrationTest.Populate_ServiceBuilding_ShowsType_NotUnzoned"`

### Phase 11q12 Canonical Test Name Summary

| Test Suite | Test Case | Source File | CMake Target | Label |
| --- | --- | --- | --- | --- |
| `MeshFormatUtilsTest` | `NullFS_PLYPresent_ReturnsPLYPath` | `tests/integration/MeshFormatUtilsTest.cpp` | `integration_tests` | `integration` |
| `MeshFormatUtilsTest` | `NullFS_B3DOnly_ReturnsB3DPath` | `tests/integration/MeshFormatUtilsTest.cpp` | `integration_tests` | `integration` |
| `MeshFormatUtilsTest` | `NullFS_NeitherPresent_ReturnsB3DPath` | `tests/integration/MeshFormatUtilsTest.cpp` | `integration_tests` | `integration` |
| `MeshFormatUtilsIFSTest` | `IFileSystem_PLYPresent_ReturnsPLYPath` | `tests/integration/MeshFormatUtilsTest.cpp` | `integration_tests` | `integration` |
| `MeshFormatUtilsIFSTest` | `IFileSystem_B3DOnly_ReturnsB3DPath` | `tests/integration/MeshFormatUtilsTest.cpp` | `integration_tests` | `integration` |
| `MeshFormatUtilsIFSTest` | `IFileSystem_NeitherPresent_ReturnsB3DPath` | `tests/integration/MeshFormatUtilsTest.cpp` | `integration_tests` | `integration` |

**Test details**:

- `NullFS_PLYPresent_ReturnsPLYPath` -- calls `resolveModelPath()` with `nullptr`
  for `IFileSystem*` and a base path where a `.ply` file exists. Asserts the
  returned path has the `.ply` extension, confirming PLY-first resolution priority.
- `NullFS_B3DOnly_ReturnsB3DPath` -- calls `resolveModelPath()` with `nullptr`
  for `IFileSystem*` and a base path where only a `.b3d` file exists (no `.ply`).
  Asserts the returned path has the `.b3d` extension, confirming fallback to
  legacy format.
- `NullFS_NeitherPresent_ReturnsB3DPath` -- calls `resolveModelPath()` with
  `nullptr` for `IFileSystem*` and a base path where neither `.ply` nor `.b3d`
  exists. Asserts the returned path ends with `.b3d`, confirming B3D fallback
  when neither format file exists (caller handles load failure).
- `IFileSystem_PLYPresent_ReturnsPLYPath` -- same as the nullptr variant but uses
  the Irrlicht `IFileSystem` overload with an EDT_NULL device. Validates that the
  IFileSystem-backed path resolution finds `.ply` files correctly.
- `IFileSystem_B3DOnly_ReturnsB3DPath` -- IFileSystem overload variant; confirms
  `.b3d` fallback works through the Irrlicht file system layer.
- `IFileSystem_NeitherPresent_ReturnsB3DPath` -- IFileSystem overload variant.
  Asserts the returned path ends with `.b3d`, confirming B3D fallback when
  neither format file exists (caller handles load failure).

**CTest filter** for all Phase 11q12 tests:
`-R "MeshFormatUtilsTest\.|MeshFormatUtilsIFSTest\."`

### `AITOWN_TESTING_ENABLED` Guarded Test Seams

Every test-only method guarded by `#ifdef AITOWN_TESTING_ENABLED` is catalogued here
with its signature, owning class, guard, forwarding target (if any), and usage contract.
The compile definition MUST be set on both the library target and the test target that
calls the seam (PRIVATE on both, so it does not propagate to the production `aitown`
binary). See the CMake compile-definitions block above for the full list of
target pairs.

#### `CitySimulation::testForceUnlockDensityTier`

- **Declaration**: `CitySimulation.h`, guarded by `#ifdef AITOWN_TESTING_ENABLED`
- **Definition**: `CitySimulation.cpp`, also inside the guard
- **Signature**: `void testForceUnlockDensityTier(TileCoord tile, ZoneType zone, int newTier)`
- **Behaviour**: Sets the internal density-tier unlock flag directly, bypassing the
  3-consecutive-month revenue gate. Must NOT be compiled into production builds.
- **CMake targets**: `aitown_sim` and `simulation_tests` both require
  `AITOWN_TESTING_ENABLED=1`
- **Used by**: `CitySimulationRenderTest` fixture
  (`tests/simulation/city_simulation_render_test.cpp`)

#### `AudioSystem::testGetSourceHandle`

- **Declaration**: `AudioSystem.h`, guarded by `#ifdef AITOWN_TESTING_ENABLED`
- **Definition**: `AudioSystem.cpp`, also inside the guard
- **Signature**: `ALuint testGetSourceHandle(int poolIndex) const`
- **Behaviour**: Returns the raw AL source handle for a given source-pool index.
  Used to assert source state (`AL_STOPPED`) after `releaseVehicleEnginePair()`.
- **CMake targets**: `aitown_audio` and `integration_tests` both require
  `AITOWN_TESTING_ENABLED=1`
- **Used by**: `AudioSystemVehicleReleaseTest` fixture
  (`tests/integration/VehicleReleaseTest.cpp`)

#### `UIManager::handleNewGameRequest` / `UIManager::setGameSessionActiveForTest`

- **Declaration**: `UIManager.h`, guarded by `#ifdef AITOWN_TESTING_ENABLED`
- **Definition**: `UIManager.cpp`, also inside the guard
- **Behaviour**: `handleNewGameRequest` triggers the new-game flow without UI
  interaction. `setGameSessionActiveForTest` sets the internal game-session flag
  directly for test setup.
- **CMake targets**: `aitown_ui` and `ui_tests` both require
  `AITOWN_TESTING_ENABLED=1`
- **Used by**: UI test fixtures in `tests/ui/`

#### `CitySimulation::testSetZoneDemandFactor`

- **Declaration**: `CitySimulation.h`, guarded by `#ifdef AITOWN_TESTING_ENABLED`
- **Definition**: `CitySimulation.cpp`, also inside the guard
- **Signature**: `void testSetZoneDemandFactor(ZoneType zone, float value)`
- **Behaviour**: Forwards to `m_traffic.overrideZoneDemandFactor(zone, value)`.
  Allows tests to inject a deterministic demand factor for a specific zone type,
  bypassing the computed demand calculation in `Traffic::getZoneDemandFactor()`.
  Passing `0.0f` as the `value` sentinel resets the override -- `Traffic` returns
  its computed value when no override is active.
- **Forwarding target**: `Traffic::overrideZoneDemandFactor(ZoneType, float)` --
  this method has no preprocessor guard; it is only called from the one guarded
  `CitySimulation` method above.
- **CMake targets**: Both `aitown_sim` and `simulation_tests` must have
  `AITOWN_TESTING_ENABLED=1` set (PRIVATE). Without the flag on `aitown_sim`, the
  function body is compiled out and the test binary gets an undefined-reference
  linker error. MUST NOT be set on `aitown` (production binary).
- **Used by**: `ZoningTestNice` fixture for deterministic demand injection in
  upgrade-wave tests. TearDown must destroy the `CitySimulation`
  (`sim_.reset()`) or explicitly call `testSetZoneDemandFactor(zone, 0.0f)`
  to reset the override -- the `NiceSimulationTestBase` TearDown satisfies
  this requirement via `sim_.reset()`.
