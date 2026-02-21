# Testability Architecture

- Simulation logic must **not** depend directly on Irrlicht or OpenAL APIs
- `UIManager` must depend on an `IUIBackend` interface for all Irrlicht `IGUIEnvironment` calls, enabling `src/ui/` to be tested with a `MockUIBackend` in unit tests without a display. The interface uses **opaque `UIElementHandle` (uint32_t)** instead of raw Irrlicht pointers — this fully severs the compile-time dependency on Irrlicht headers in any translation unit that only includes `IUIBackend.h`. The concrete `IrrlichtUIBackend` maintains an internal `std::unordered_map<UIElementHandle, IGUIElement*>` to map handles to real objects. **Source location**: `IUIBackend.h` lives in `src/ui/`; `IrrlichtUIBackend.h/.cpp` live in `src/rendering/` (since it depends on Irrlicht headers). `MockUIBackend` lives in `tests/ui/mock_ui_backend.h`. This placement ensures the `src/ui/` coverage gate does not pull in Irrlicht headers and the `src/rendering/` exclusion correctly covers `IrrlichtUIBackend`. **IMPORTANT: IUIBackend.h MUST be placed in `src/ui/` (not `src/interfaces/`). This is an intentional exception to the `src/interfaces/` pattern. IUIBackend is a UI-layer-only interface; placing it in `src/ui/` ensures its coverage is captured under the 80% coverage gate. All other shared interfaces (IClock, ISimulationRNG, ISimulationPauser, ICitySimulation) live in `src/interfaces/` as usual.**

```cpp
using UIElementHandle = uint32_t;
static constexpr UIElementHandle kInvalidUIElement = 0;

// Rect struct used by IUIBackend::getElementRect — MUST be defined BEFORE IUIBackend
// to avoid a forward-declaration-as-return-type ambiguity in the virtual method signature.
// Placing the definition after the class compiles on some compilers but is non-conforming
// and breaks with strict C++ parsing rules for return types in virtual method declarations.
struct Rect { int x{0}, y{0}, w{0}, h{0}; };

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
    virtual Rect            getElementRect(UIElementHandle handle) const = 0;   // {x, y, w, h} in virtual space; for position/size assertions
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
};
```

`MockUIBackend` returns arbitrary non-zero integer handles (e.g., an incrementing counter) with no real objects — unit tests that call UIManager methods never dereference Irrlicht pointers, making `src/ui/` genuinely headless-testable and the 80% coverage gate achievable.

- **`UIScaler` testability**: `UIScaler` must accept viewport dimensions at construction (`UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY)`) rather than reading from a live `IVideoDriver`. Tests construct `UIScaler(1920, 1080, 1280, 720, 0, 90)` directly to validate coordinate projection and letterbox offset math without a display. The `unproject` method returns `UIScaler::VirtualPoint` — a nested struct, NOT at namespace scope, to avoid ODR violations. The five named unit tests that must be authored in `tests/ui/ui_scaler_test.cpp` are:
  1. `UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly`: construct with (1920, 1080, 1280, 720, 0, 90); unproject (640, 450) → virtual (960, 540).
  2. `UIScaler_FullNative_NoOffset_ProjectsIdentity`: construct with (1920, 1080, 1920, 1080, 0, 0); unproject (960, 540) → virtual (960, 540).
  3. `UIScaler_PillarboxOffset_UnprojectsCenterCorrectly`: construct with (1920, 1080, 1440, 1080, 240, 0); unproject (960, 540) → virtual (960, 540).
  4. `UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero`: construct with (1920, 1080, 1280, 720, 0, 90); unproject (640, 80) → virtual y clamped to 0 (actual_y=80 < offsetY=90 produces negative pre-clamp virtual_y, clamped to 0).
  5. `UIScaler_GetViewportRect_ReturnsCorrectOffsets`: construct with (1920, 1080, 1280, 720, 0, 90); `getViewportRect()` returns {x:0, y:90, w:1280, h:720}.
- **`NotificationManager` testability**: `NotificationManager` must accept `IUIBackend*` for element creation, `ICitySimulation*` for auto-pause injection and deficit-streak queries, and `IClock*` for dismiss-after-5s timing. The correct constructor signature is: `NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock)`. **Source location**: `ISimulationPauser.h` lives in **`src/interfaces/`** (NOT `src/simulation/`) — placing it in `src/simulation/` creates a latent circular dependency when `src/ui/` headers include it (`src/ui/` → `src/simulation/` is a prohibited dependency direction; UI must not depend on simulation headers). `src/interfaces/` is a dependency-free common header directory that both `src/simulation/` and `src/ui/` may include. `MockCitySimulation` lives in `tests/ui/mock_city_simulation.h` (used by UI tests that need to verify pause/resume calls and simulation queries without pulling in `CitySimulation`). Tests inject `MockUIBackend` + `MockCitySimulation` + `ManualClock` and call `update()` with controlled time advances to verify queue ordering, auto-dismiss timing, CRITICAL vs Normal band placement, and log-fallback behavior. `NotificationManager` exposes a public `dismissCriticalToast(UIElementHandle handle)` method — this is the production API called by the UI event handler when the player clicks, presses Enter, or presses Delete on a CRITICAL toast; it is not a test-only backdoor. Tests call this method to simulate player dismissal. Additional required test cases:
  - `CriticalToast_OnPost_AutoPausesCalled`: posting a CRITICAL toast calls `setPaused(true)` exactly once when the CRITICAL queue transitions from empty to non-empty.
  - `CriticalToast_OnLastDismiss_NoAutoResume`: calling `dismissCriticalToast(handle)` on the last remaining CRITICAL toast does **NOT** call `setPaused(false)` — auto-resume requires explicit player unpause. Verify `setPaused(false)` is never called by `NotificationManager` on CRITICAL toast dismissal.
  - `CriticalToast_SecondPost_NoDoublePause`: posting a second CRITICAL toast while one is already active does NOT call `setPaused(true)` again.
- **`CameraController` testability**: `CameraController`'s pan/zoom/rotate input processing must be unit-testable by injecting synthetic `InputEvent` structs (defined in `src/platform/input_event.h`). The controller must accept a `CameraState` struct (position, target, pitch, yaw) and expose `getCameraState()` — tests drive events in, read state out, verify pitch clamping at [−70°, −20°] and edge-scroll behavior without a live scene node. `CameraController` must also expose `bool isEdgeScrollEnabled() const` as a public accessor returning the current value of `m_edgeScrollEnabled`; this is required by test case 6 (`CameraController_EdgeScroll_EnabledByDefaultInFullscreen`) to assert constructor initial state without input injection. **Source location**: `CameraController.h/.cpp` live in `src/ui/` (it is an input/UI concern, not a rendering concern); test file is `tests/ui/camera_controller_test.cpp`. This placement ensures `CameraController` is covered by the `src/ui/` 80% coverage gate.
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

  **Required Named Test Cases** — all 7 test cases must be authored in `tests/ui/camera_controller_test.cpp` and registered under the `ui_tests` CMake target (label `unit`). Per `architecture/ui-ux/camera-controls.md`, pitch clamp tests must use exact equality assertions (`EXPECT_EQ` / `EXPECT_FLOAT_EQ`) rather than strictly-less-than comparisons, because the spec defines inclusive bounds using `std::clamp` semantics:
  1. `CameraController_PitchClamp_AtUpperBound_ExactlyMinus20` — inject a sequence of `MouseWheel` events that would drive pitch above −20° (e.g., repeated positive wheel delta); call `getCameraState()`; assert `getCameraState().pitch == -20.0f` using `EXPECT_FLOAT_EQ` (not `EXPECT_LT` — the bound is inclusive and pitch must equal exactly −20°, not merely be less than some value above it).
  2. `CameraController_PitchClamp_AtLowerBound_ExactlyMinus70` — inject a sequence of `MouseWheel` events that would drive pitch below −70° (e.g., repeated negative wheel delta); call `getCameraState()`; assert `getCameraState().pitch == -70.0f` using `EXPECT_FLOAT_EQ` (not `EXPECT_GT` — the bound is inclusive and pitch must equal exactly −70°).
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
     Inject `InputEvent{Type::WindowFocusGained}` then
     `InputEvent{Type::MouseMove, x=0, y=540}` → assert camera position DID change
     (edge scroll active again after focus restored, since `m_edgeScrollEnabled` was
     never changed).
     Use `EXPECT_EQ` for bool assertions; use `EXPECT_NE` for camera position change.
- **`QueryPanel` testability**: `QueryPanel::computePanelPosition(int cursorX, int cursorY, Rect tileBounds)` must be a pure function (no side effects, no Irrlicht dependency) returning a `Rect`. Required test cases:
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
  1. `ModalDialog_OnOpen_SimulationIsPaused` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: calling `UIManager::showModal()` calls `CitySimulation::setPaused(true)` before returning.
  2. `ModalDialog_OnOpen_SpeedSelectorIsDisabled` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: `IUIBackend::setElementEnabled(..., false)` is called on the speed selector handle (not `setElementVisible` — the selector remains visible but non-interactive).
  3. `ModalDialog_OnClose_SimulationResumes` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: dismissing the modal calls `setPaused(false)` and calls `setElementEnabled(..., true)` on the speed selector to re-enable it.
  4. `UndoSystem_BlockedDuringModal_HotkeyIgnored` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: injecting a Ctrl+Z `InputEvent` while modal is active does NOT call any undo operation.
  5. `UndoSystem_BlockedDuringModal_ButtonGrayedOut` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: while modal is active, `setElementEnabled(..., false)` is called on the undo button element via `IUIBackend`.
  6. `CriticalToast_DuringModal_IsQueued_NotDisplayed` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: posting a CRITICAL toast while a blocking modal is active queues the toast but does NOT display it immediately (no `addStaticText` call to `IUIBackend` for the toast element). After modal dismissal, the toast becomes visible (a deferred `addStaticText` call is verified).
  7. `CriticalToast_DuringModal_AutoPauseDeferred` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: CRITICAL toast auto-pause logic does not fire while a blocking modal is active; `setPaused(true)` is NOT called a second time for the toast arrival (the modal pause is already active). After modal dismissal, if the CRITICAL queue is non-empty, auto-pause state is re-evaluated once.
  8. `ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: post a CRITICAL toast while a blocking modal is active (verifies no second `setPaused(true)` call during modal-active period), then dismiss the modal (`UIManager::closeModal()`), then verify: (a) the queued CRITICAL toast is now displayed (`addStaticText` called on `MockUIBackend`), and (b) `setPaused(true)` is called **once more** during `closeModal()` re-evaluation — meaning **twice total** across the test (once on modal open, once on re-evaluation in `closeModal()` because CRITICAL queue is non-empty); `setPaused(false)` is NOT called — simulation stays paused because the CRITICAL toast remains active after modal close. **Reconciliation with StrictMock matrix**: The StrictMock Expected Call Matrix entry for this test specifies `setPaused(true) × 2` (total) and `setPaused(false) × 0` — the prose description above matches this. The "exactly once" wording in prior spec drafts referred to the re-evaluation step only (one call within `closeModal()`), not the total across the test; this was ambiguous and has been corrected to "once more during closeModal()". This test exercises the deferred re-evaluation path explicitly — without it, the re-evaluation call in the `closeModal()` code path is unverified and can be silently dropped. **Deferred `addStaticText` call timing**: The CRITICAL toast's `addStaticText` call to `MockUIBackend` MUST occur synchronously within the same `closeModal()` call stack — NOT deferred to the next `update()` tick. This is a firm implementation requirement: the `closeModal()` implementation must call the display logic synchronously, not schedule it for the next frame. Tests assert the element handle's presence immediately after `closeModal()` returns, with no intervening `update()` call. Implementations that defer display to `update()` do not meet this requirement and must be refactored.
  9. `Modal_SpeedSelectorGrayed_DespiteCriticalToast_SpeedAccessible_WhenModalOnly` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: when only a CRITICAL toast is active (no modal), the speed selector remains ENABLED (accessible per CRITICAL-toast-pause spec). This distinguishes modal-pause (selector grayed) from CRITICAL-toast-pause (selector accessible).
  10. `ModalDialog_OnClose_WithEmptyCriticalQueue_NoAutoRePause` *(Phase 8 deliverable — Phase 3 delivers fixture stub with no test body)*: open a modal (verifies `setPaused(true)` called once), then dismiss the modal with no CRITICAL toasts in the queue, then verify: (a) `setPaused(false)` is called exactly once (simulation resumes), and (b) `setPaused(true)` is NOT called again during `closeModal()`. This is the inverse of test 8 — it confirms that the CRITICAL toast auto-pause re-evaluation in `closeModal()` does NOT call `setPaused(true)` when the CRITICAL queue is empty, preventing a spurious re-pause on normal modal dismiss.
- **`ISimulationRNG`** — injectable RNG interface for deterministic simulation testing: Service degradation (random building selection at −10% budget surplus) and any other simulation-layer random draws must use this interface rather than `std::rand()` or a global `std::mt19937`. Tests inject a `ManualRNG` that returns a preset sequence. **Source location**: `ISimulationRNG.h` lives in `src/interfaces/`; `ManualRNG` lives in `tests/simulation/manual_rng.h` (used by simulation tests) — **not** in `src/` (it is a test double, never linked into production code).

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
- **`ITerrainRNG`** — injectable RNG interface for deterministic terrain generation testing. **Source location**: `ITerrainRNG.h` lives in `src/terrain/`; `MockTerrainRNG` lives in `tests/terrain/mock_terrain_rng.h`. The `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` property test requires an injectable mock that counts re-seed calls:

  ```cpp
  class ITerrainRNG {
  public:
      virtual ~ITerrainRNG() = default;
      virtual float nextFloat() = 0;              // [0.0, 1.0) — continuous noise and feature probability
      virtual int   nextInt(int min, int max) = 0; // inclusive [min, max] — discrete terrain feature counts, tile selection
      virtual void  reseed(uint64_t seed) = 0;    // called when generator retries with a new seed
  };
  // NAMING CONVENTION NOTE: Despite the `mock_` prefix, `MockTerrainRNG` does NOT use
  // GMock macros (`MOCK_METHOD`). It is a manual stub with a real `std::mt19937_64` engine.
  // The `mock_` prefix is used intentionally so the `'*/mock_*.h'` lcov exclusion pattern
  // applies to this test-double header. The name `MockTerrainRNG` (not `ManualTerrainRNG`)
  // is canonical — do not rename.
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
- **`ICitySimulation`** — interface enabling `UIManager` to call simulation control methods without depending on the concrete `CitySimulation` class. **Source location**: `ICitySimulation.h` lives in `src/interfaces/` (alongside `ISimulationRNG.h`, `IClock.h`, and `ISimulationPauser.h` — the shared dependency-free header directory that both `src/simulation/` and `src/ui/` may include). `UIManager` must accept `ICitySimulation*` (not a concrete `CitySimulation*`) to enable headless testing. `MockCitySimulation` lives in `tests/ui/mock_city_simulation.h`.

  ```cpp
  // src/interfaces/ICitySimulation.h
  // SpeedMultiplier is the canonical enum defined in simulation_types.h.
  // ICitySimulation.h must #include "simulation_types.h" to get SpeedMultiplier, ZoneType, and Difficulty
  // as complete types — forward declarations are insufficient for by-value parameters.
  // NOTE: The canonical Difficulty enumerator names are Easy, Normal, Hard (PascalCase),
  // matching Phase 0's simulation_types.h. Do NOT use EASY, NORMAL, HARD (all-caps) —
  // those names do not exist and will cause a compile error.
  #include "simulation_types.h"
  #include "ISimulationPauser.h"

  // ICitySimulation extends ISimulationPauser so that CitySimulation implements both interfaces
  // through a single concrete class. UIManager passes m_sim to NotificationManager as ICitySimulation*
  // — NotificationManager needs getConsecutiveDeficitMonths() (on ICitySimulation) as well as
  // setPaused(bool) (inherited from ISimulationPauser). setPaused(bool) is
  // inherited from ISimulationPauser and implemented by CitySimulation.
  class ICitySimulation : public ISimulationPauser {
  public:
      virtual ~ICitySimulation() = default;
      // setPaused(bool paused) is inherited from ISimulationPauser — do not redeclare here.
      virtual void setSpeed(SpeedMultiplier speed) = 0;
      // State-query methods used by UIManager panels:
      virtual bool isPaused() const = 0;
      virtual SpeedMultiplier getSpeed() const = 0;

      // Economy/treasury queries — called by HUD resource bar and Budget Detail Panel:
      virtual float getTreasuryBalance() const = 0;          // Called by HUD resource bar to display treasury balance
      virtual float getCurrentMonthlyRevenue() const = 0;    // Called by Budget Detail Panel for net monthly balance line
      virtual float getOutstandingDebt() const = 0;          // Called by HUD persistent debt indicator
      virtual float estimateMonthlyUpkeep() const = 0;       // Called by grace period tooltip and Budget Detail Panel upkeep lines
      virtual float getNextUnlockThreshold(Difficulty d) const = 0; // Called by Density Unlock Preview Tooltip to compute proximity to next tier.
      // Returns the difficulty-adjusted dollar threshold for the next pending density tier unlock,
      // or SimulationConstants::kNoUnlockThreshold (-1.0f) when all six tiers are already unlocked.
      // Contract: never returns 0.0f or NaN. Callers MUST guard with (result < 0.0f) before displaying.
      // Cross-reference: architecture/game-design/economy-model.md (getNextUnlockThreshold return semantics).

      // City rating — called by HUD to display star rating:
      // Phase 1 stub: returns int [0, 5]. Phase 3 upgrades to CityRatingTier enum (Village/Town/
      // City/Metropolis/Megalopolis) once CityRatingTier is added to simulation_types.h.
      // See implementation/phase-3.md for the upgrade deliverable and game-progression-modes.md
      // for the CityRatingTier enum definition.
      virtual int getCityRating() const = 0;  // Phase 1: int [0,5]; Phase 3 upgrades to CityRatingTier

      // Demand pressure — called by HUD demand pressure bar per budget tick.
      // Returns the city-wide effective demand for the given zone type as a float in [0.0, 1.0].
      // This is the AGGREGATE, POST-FLOOR value used for UI display — a weighted average of
      // effective_demand_factor across all tiles of that zone type after applying bootstrap decay,
      // the demand floor (R ≥ 0.20, C ≥ 0.10, I ≥ 0.10 post-tick-6), and the traffic smoothstep
      // combination rule. This value is what the HUD demand pressure bars display directly.
      // It is NOT the raw traffic demand factor (see getTrafficDemandFactor below).
      // Per-tile demand is available separately via QueryResult::demand_pressure_pct (Inspector Panel).
      // Cross-reference: architecture/game-design/zoning-system.md (effective_demand_factor combination rule).
      virtual float getDemandPressurePct(ZoneType zone) const = 0;

      // Population — called by HUD population display and density-unlock checks:
      virtual int getTotalPopulation() const = 0;  // Called by HUD population counter and density-unlock preview

      // Undo state — called by HUD undo button to determine enabled/disabled state and countdown:
      virtual bool hasUndoPendingAction() const = 0;  // Called by HUD undo button to gray out when no action is pending
      virtual double getUndoExpiryTimeSeconds() const = 0;  // Returns IClock::nowSeconds() value when the pending undo action expires; 0.0 if no action pending

      // Game-over flow — deficit streak accessor. Returns the number of consecutive budget ticks
      // in which the deficit was ≥ −50%. Returns 0 during the grace period.
      // NotificationManager reads this to determine which progressive warning toast to fire:
      //   0 = no warning, 1 = "2 months to bankruptcy", 2 = "1 month to bankruptcy", 3+ = game-over trigger.
      // Cross-reference: architecture/game-design/game-over-flow.md.
      virtual int getConsecutiveDeficitMonths() const = 0;

      // Traffic demand factor accessor — returns the INTERNAL traffic simulation multiplier for
      // the given ZoneType, derived exclusively from the rolling travel-time window smoothstep.
      // R/C zones use a 5-tick window; I zones use a 3-tick window. Returns a value in [0.0, 1.0]
      // where 1.0 = no travel-time penalty and 0.0 = maximum congestion penalty.
      // DISTINCT from getDemandPressurePct: this is the raw traffic-only component BEFORE combining
      // with bootstrap decay, capacity-ratio signals, or demand floors. It is used internally by
      // the traffic simulation to modulate zone growth, and is exposed here solely for:
      //   (a) Phase 11 save/load round-trip tests that verify the rolling-window state persists, and
      //   (b) the traffic rolling-average serialization requirement in implementation/phase-11.md.
      // The HUD demand bars display getDemandPressurePct (the post-combination aggregate), NOT this value.
      // Cross-reference: implementation/phase-11.md (Traffic demand factor serialization).
      virtual float getTrafficDemandFactor(ZoneType zone) const = 0;

      // Density-unlock state accessor — returns a snapshot of all density-unlock counters and flags.
      // Required for Phase 11 save round-trip test to verify counter persistence across save/load.
      // Cross-reference: implementation/phase-11.md (getDensityUnlockState deliverable).
      // DensityUnlockState is defined in simulation_types.h alongside ZoneType and SpeedMultiplier.
      //   struct DensityUnlockState {
      //       int  consecutive_months_above_threshold[6];  // 0–2 range; one counter per density tier
      //       bool unlock_flags[6];                        // true if the corresponding tier is unlocked
      //   };
      virtual DensityUnlockState getDensityUnlockState() const = 0;
  };
  ```

  ```cpp
  // tests/ui/mock_city_simulation.h
  #include "gmock/gmock.h"
  #include "src/interfaces/ICitySimulation.h"

  class MockCitySimulation : public ICitySimulation {
  public:
      MOCK_METHOD(void, setPaused, (bool paused), (override));
      MOCK_METHOD(void, setSpeed, (SpeedMultiplier speed), (override));
      MOCK_METHOD(bool, isPaused, (), (const, override));
      MOCK_METHOD(SpeedMultiplier, getSpeed, (), (const, override));

      // Economy/treasury queries:
      MOCK_METHOD(float, getTreasuryBalance, (), (const, override));          // Called by HUD resource bar to display treasury balance
      MOCK_METHOD(float, getCurrentMonthlyRevenue, (), (const, override));    // Called by Budget Detail Panel for net monthly balance line
      MOCK_METHOD(float, getOutstandingDebt, (), (const, override));          // Called by HUD persistent debt indicator
      MOCK_METHOD(float, estimateMonthlyUpkeep, (), (const, override));       // Called by grace period tooltip and Budget Detail Panel upkeep lines
      MOCK_METHOD(float, getNextUnlockThreshold, (Difficulty d), (const, override)); // Called by Density Unlock Preview Tooltip.
      // Returns kNoUnlockThreshold (-1.0f) when all tiers unlocked; positive dollar value otherwise.
      // Tests that exercise the "all tiers unlocked" branch must return SimulationConstants::kNoUnlockThreshold.

      // City rating (Phase 1: returns int [0,5]; Phase 3 upgrades to CityRatingTier):
      MOCK_METHOD(int, getCityRating, (), (const, override));  // Phase 3 changes to CityRatingTier

      // Demand pressure — UI display aggregate. Returns the post-floor, post-bootstrap, post-combination
      // effective_demand_factor for the given zone type. Used by HUD demand pressure bars.
      // NOT the same as getTrafficDemandFactor (see below for distinction).
      MOCK_METHOD(float, getDemandPressurePct, (ZoneType zone), (const, override));

      // Population:
      MOCK_METHOD(int, getTotalPopulation, (), (const, override));  // Called by HUD population counter and density-unlock preview

      // Undo state:
      MOCK_METHOD(bool, hasUndoPendingAction, (), (const, override));  // Called by HUD undo button to gray out when no action is pending
      MOCK_METHOD(double, getUndoExpiryTimeSeconds, (), (const, override));  // Returns IClock::nowSeconds() value when the pending undo action expires; 0.0 if no action pending

      // Game-over flow — deficit streak accessor. Returns 0 during grace period.
      // Cross-reference: architecture/game-design/game-over-flow.md.
      MOCK_METHOD(int, getConsecutiveDeficitMonths, (), (const, override));

      // Traffic demand factor accessor. Returns the INTERNAL traffic-only multiplier in [0.0, 1.0]
      // from the rolling travel-time window BEFORE bootstrap/floor combination. R/C = 5-tick window;
      // I = 3-tick window. Exposed for Phase 11 save/load round-trip serialization only.
      // HUD bars read getDemandPressurePct (the post-combination aggregate), not this value.
      // Cross-reference: implementation/phase-11.md (Traffic demand factor serialization).
      MOCK_METHOD(float, getTrafficDemandFactor, (ZoneType), (const, override));

      // Density-unlock state accessor. Returns consecutive-month counters and unlock flags for all 6
      // density tiers. Cross-reference: implementation/phase-11.md (getDensityUnlockState deliverable).
      MOCK_METHOD(DensityUnlockState, getDensityUnlockState, (), (const, override));
  };
  ```

  `UIManager` constructor accepts `ICitySimulation*` as a non-owning pointer. The full constructor signature is `UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)`. The `clock` parameter is passed to `NotificationManager` at construction (for dismiss-after-5s timing) and to the `HUD` component for grace-period and undo-countdown displays. In the `UIManagerModalTest` fixture, `sim_` is declared as `NiceMock<MockCitySimulation>` and passed to `UIManager` at construction: `UIManager(&backend_, &audio_, &sim_, &clock_)`. In tests that assert specific pause/resume call counts (modal tests 1, 3, 8, 10), `EXPECT_CALL(sim_, setPaused(...))` is set up on the `NiceMock<MockCitySimulation>` before the action under test. NiceMock is used rather than StrictMock here because the focus of these tests is on `MockUIBackend` call patterns; simulation pause/resume expectations are set selectively per test rather than requiring every possible call to be declared up front.
- **Thread-safety annotations**: Use Clang's thread-safety analysis attributes (`GUARDED_BY`, `REQUIRES`, `EXCLUDES`) on Clang builds; document-only `// thread-safe` or `// main-thread-only` comments as fallback on MSVC. Enable `-Wthread-safety` in CMake for Clang builds.

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

class IRenderer {   // main-thread-only
public:
    virtual ~IRenderer() = default;
    virtual void          beginFrame() = 0;              // main-thread-only
    virtual void          endFrame() = 0;                // main-thread-only
    virtual void          drawScene() = 0;               // main-thread-only
    virtual TextureHandle loadTexture(const std::string& path) = 0;  // main-thread-only; returns kInvalidTexture on failure
    virtual void          setCamera(const CameraParams& p) = 0;       // main-thread-only
};

// Canonical IAudioSystem — 11 methods. Authoritative definition in audio-architecture/audio-system.md.
// Uses only game-domain types (SoundId, SoundHandle, MusicTrackId, StingerType, SimSpeed,
// SoundPriority, TimeOfDay, vec3, CameraState). Never expose ALuint, ALfloat, or AL_* constants through this interface.
// Forward declarations (defined in game-domain headers, not OpenAL headers):
//   struct vec3;              // 3-component float vector (X, Y, Z)
//   struct CameraState;       // position (vec3), forward (vec3), up (vec3)
//   enum class SimSpeed;
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
};

// MockAudioSystem — GMock implementation of IAudioSystem's 11 methods.
// Source location: tests/simulation/mock_audio_system.h
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

`IRenderer` uses opaque `TextureHandle` (uint32_t) instead of `ITexture*` — the same pattern as `IUIBackend` with `UIElementHandle`. This fully severs the compile-time dependency on Irrlicht headers in any translation unit that only includes `IRenderer.h`, including all simulation test files. `MockRenderer::loadTexture()` returns an incrementing non-zero integer. The concrete `IrrlichtRenderer` maintains `std::unordered_map<TextureHandle, ITexture*>` internally.

- **Shared mock header cross-target pattern**: `MockAudioSystem` and `MockRenderer` are defined in `tests/simulation/mock_audio_system.h` and `tests/simulation/mock_renderer.h` respectively. `ManualClock` is defined in `tests/simulation/manual_clock.h`. These headers are shared across multiple CMake test targets (`simulation_tests`, `ui_tests`, `audio_tests`). To avoid ODR (One Definition Rule) violations, these headers must be HEADER-ONLY GMock declarations (using MOCK_METHOD macros only, no definitions). Each test target that uses any of these shared headers MUST add `tests/simulation/` to its `target_include_directories`. This include path coupling is intentional and must be documented explicitly in the CMakeLists.txt for each consuming target. The ODR rule is safe because each test binary links into its own separate executable scope — there is no shared library or link-time merging across test targets. Required `target_include_directories` entries for each consuming target:

  ```cmake
  # simulation_tests — owns the shared mock headers; also needs src/interfaces/ and ${CMAKE_SOURCE_DIR}
  # for project-root-relative includes like #include "src/interfaces/IClock.h" in simulation_smoke_test.cpp
  target_include_directories(simulation_tests PRIVATE tests/simulation/ src/interfaces/ ${CMAKE_SOURCE_DIR})

  # ui_tests — uses MockAudioSystem, MockRenderer, ManualClock from tests/simulation/;
  # MockUIBackend, MockCitySimulation from tests/ui/;
  # IUIBackend.h from src/ui/; interface headers from src/interfaces/
  target_include_directories(ui_tests PRIVATE tests/simulation/ tests/ui/ src/interfaces/ src/ui/ ${CMAKE_SOURCE_DIR})

  # audio_tests — uses MockAudioSystem from tests/simulation/;
  # IAudioSystem.h from src/interfaces/; audio_constants.h from src/audio/
  target_include_directories(audio_tests PRIVATE tests/simulation/ src/interfaces/ src/audio/ ${CMAKE_SOURCE_DIR})

  # integration_tests — needs shared mock paths and UI header paths for Phase 3+ integration tests;
  # src/rendering/ is required because IrrlichtUIBackend.h lives there and integration tests compile against it
  target_include_directories(integration_tests PRIVATE tests/simulation/ tests/ui/ src/interfaces/ src/ui/ src/rendering/ ${CMAKE_SOURCE_DIR})

  # terrain_tests — needs tests/simulation/ for ManualClock (if timing tests added in Phase 5+),
  # tests/terrain/ for MockTerrainRNG, src/terrain/ for ITerrainRNG.h (included by mock_terrain_rng.h
  # via project-root-relative path "#include "src/terrain/ITerrainRNG.h""), and ${CMAKE_SOURCE_DIR}
  # so that project-root-relative includes resolve correctly.
  target_include_directories(terrain_tests PRIVATE tests/simulation/ tests/terrain/ src/terrain/ ${CMAKE_SOURCE_DIR})
  ```

  If a test target needs a specialization, it must subclass the shared mock — not redefine it. This sharing is intentional: the same mock interface is used consistently across all simulation-adjacent tests.
- **`IClock`** — injectable clock interface for audio timing and loan gate tests. **Source location**: `IClock.h` and `WallClock.h` live in `src/interfaces/`; `ManualClock` lives in `tests/simulation/manual_clock.h` (it is a test double used by both simulation tests and audio tests). `MockTerrainRNG` lives in `tests/terrain/mock_terrain_rng.h`. All test double headers (`manual_*.h`, `mock_*.h`) live under `tests/` — never under `src/`. See `coverage.md` for the full lcov exclusion patterns (including `mock_*` and `manual_*` exclusions).

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

  `AudioSystem` and `CitySimulation` accept `IClock*` at construction for crossfade timing and the forced-loan real-time gate (120 s) respectively. Production code passes `WallClock` which calls `std::chrono::steady_clock`. `ManualClock` allows deterministic time advancement in tests without wall-clock dependencies.
- **Ownership contract**: Simulation objects accept `IRenderer*` and `IAudioSystem*` as non-owning raw pointers. Ownership managed externally. In tests, fixture owns the mock and outlives the system under test:

### StrictMock Expected Call Matrix

**Zero-revenue fixture clarification**: Zero-revenue tests (`BudgetSurplus_ZeroRevenue_ReturnsZero`, `ZeroRevenue_NoDeficitConsequences`, and similar) use the standard `CitySimulationUnitTest` fixture, which includes `ManualRNG rng_{std::initializer_list<int>{0}}` as a member. This is acceptable because zero-revenue code paths never call `nextInt()` or `nextFloat()` — the ManualRNG sequence is never consumed. The key invariant for zero-revenue tests is using `StrictMock<MockAudioSystem>` (any unexpected audio call fails the test immediately). The `ManualRNG` member is harmless in these tests. There is no separate zero-revenue fixture; do not create one.

For every unit test that uses `StrictMock<MockAudioSystem>` or `StrictMock<MockRenderer>`, ALL expected calls must be explicitly declared via `EXPECT_CALL` — any unexpected call causes an immediate test failure. The following matrix documents the expected call pattern for each named test scenario. **This matrix must be kept up to date as new test scenarios are added.**

| Test scenario | Expected `MockAudioSystem` calls | Expected `MockRenderer` calls |
|---|---|---|
| Zero-revenue tests (ZeroRevenue_*, ZeroRevenue_NoDeficitConsequences) | None | None |
| Budget surplus / no loan | None | None |
| ServiceDegradation (random building selected) | `playSound(SoundId{sfx_service_degrade}, SoundPriority::NORMAL, 1.0f)` × 1 per degraded building | None |
| ForcedLoanIssued | `playSound(SoundId{sfx_loan_issued}, SoundPriority::NORMAL, 1.0f)` × 1 | None |
| ModalDialog_OnOpen | `setPaused(true)` × 1 (via MockCitySimulation, not IAudioSystem) | None |
| ModalDialog_OnClose (no queued toast) | `setPaused(false)` × 1 (via MockCitySimulation) | None |
| `ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated` (test 8) | Via MockCitySimulation: `setPaused(true)` × 2 (once on modal open; once on re-evaluation in `closeModal()` because CRITICAL queue is non-empty); `setPaused(false)` × 0 (NOT called — simulation stays paused because CRITICAL toast remains active after modal close) | Via MockUIBackend: `addStaticText` × 1 (queued CRITICAL toast displayed synchronously within `closeModal()`) |

> **Post-V1 stinger scenarios**: `StingerType::GAME_OVER` (game-over stinger, fires in Scenario mode) is not defined in the V1 `StingerType` enum (`{ CRISIS, MILESTONE }` only). Do not reference `StingerType::GAME_OVER` in any V1 test or production code — it does not exist until Scenario mode is implemented post-V1. When Scenario mode is added post-V1, a new matrix row will be added here.

**Important**: `CitySimulation::setPaused()` is NOT an `IAudioSystem` call — it is a `CitySimulation` method called by `UIManager`. The matrix above uses "MockAudioSystem" loosely for audio effects; the pause/resume calls go to a `MockCitySimulation` (or directly to `CitySimulation` with injected mocks). Distinguish the two mock targets in test setup.

### UIManagerDrawOrderTest CONTRACT

`UIManagerDrawOrderTest` verifies that `UIManager::draw()` issues panel draw calls in the correct back-to-front Z-order (see `architecture/ui-ux/ui-manager.md` Draw Order section). Because `IUIBackend` has no `drawRect()` or equivalent rendering primitive, ordering is verified via a per-panel sentinel call: each panel stub's `draw()` implementation calls `setElementVisible(handle, true)` using a fixed dummy `UIElementHandle` sentinel value unique to that panel. The 17 methods of `IUIBackend` do not include a draw primitive — `setElementVisible` is the only side-effectful call that can serve as an observable ordering probe without adding a new method to the interface.

**Anti-no-op requirement**: Each panel stub's `draw()` method MUST call `m_backend->setElementVisible(handles::kXxxSentinel, true)` using the UNIQUE per-panel `UIElementHandle` sentinel constant from `tests/ui/panel_sentinel_handles.h` (e.g., `handles::kMinimapSentinel = 101u`, `handles::kHUDSentinel = 102u`, etc.). A `draw()` method that does nothing (no-op body) vacuously satisfies `InSequence` constraints — ALL constraints are trivially satisfied when zero `setElementVisible` calls fire. The test MUST fail if `UIManager::draw()` skips any panel. The `InSequence` guard is only meaningful when each expected call actually fires.

Panel stubs used in `UIManagerDrawOrderTest` are NOT "no-op shells" — they must contain the sentinel `setElementVisible` call even if all other methods on the stub are no-ops. A stub `draw()` body of `{}` (empty) or `// TODO` is a defect: the draw-order test becomes vacuously green and provides zero ordering coverage.

**CONTRACT**: Each test-stub panel (e.g. `StubHUD`, `StubMinimap`, `StubTaxRatePanel`, etc.) used by `UIManagerDrawOrderTest` must:

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
    constexpr UIElementHandle kTaxRateSentinel       = 103u;
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
    // 4. TaxRatePanel (hidden — sentinel NOT expected)
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
    // 4. TaxRatePanel (hidden — sentinel NOT expected)
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
    void TearDown() override {
        // Reset ui_ before mock objects are destroyed so UIManager destructor calls
        // (e.g. backend_.removeElement()) happen while MockUIBackend is still alive.
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
//     // Hidden panels (MainMenu, TaxRate, Inspector, PauseMenu, Settings, Scrim, Modal)
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
    void TearDown() override {
        // UIManager destructor calls backend_.removeElement() for all live UI elements.
        // The explicit ui_.reset() here is a defensive practice — the current declaration
        // order already satisfies the destruction invariant automatically (ui_ declared last
        // is destroyed first in reverse order), but future fixture modifications could
        // inadvertently reorder members. The explicit TearDown() makes the destruction
        // contract immune to member reordering.
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
    void TearDown() override {
        // CitySimulation destructor must NOT call audio_ or renderer_ methods.
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
    void TearDown() override {
        // REQUIRED: CitySimulation must be destroyed before mock objects.
        // NiceMock suppresses unexpected-call warnings but does NOT protect
        // against use-after-destroy if CitySimulation destructor calls mocks.
        // Members are destroyed in reverse declaration order (sim_ last),
        // so sim_.reset() here ensures correct destruction order.
        // NOTE: verifyAllConsumed() is NOT called here — the shared rng_{{0}} is a
        // placeholder; property tests that drive random draws must use a local RNG.
        // See ManualRNG exemption comment above the class definition for full rationale.
        sim_.reset();
    }
};
```
