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
    virtual UIElementHandle addStaticText(const std::wstring& text, int x, int y, int w, int h) = 0;
    virtual UIElementHandle addButton(int x, int y, int w, int h, const std::wstring& label) = 0;
    virtual void            removeElement(UIElementHandle handle) = 0;
    virtual void            setElementText(UIElementHandle handle, const std::wstring& text) = 0;
    virtual void            setElementVisible(UIElementHandle handle, bool visible) = 0;
    virtual bool            isElementVisible(UIElementHandle handle) const = 0;
    virtual void            setElementEnabled(UIElementHandle handle, bool enabled) = 0;  // grayed-out vs interactive
    virtual bool            isElementEnabled(UIElementHandle handle) const = 0;
    virtual void            setElementAlpha(UIElementHandle handle, float alpha) = 0;  // [0.0, 1.0]
    virtual void            setElementImage(UIElementHandle handle, const std::string& atlasKey, int srcX, int srcY, int srcW, int srcH) = 0;
    virtual std::wstring    getElementText(UIElementHandle handle) const = 0;   // for test assertions on displayed values
    virtual Rect            getElementRect(UIElementHandle handle) const = 0;   // {x, y, w, h} in virtual space; for position/size assertions
    virtual int             getScreenWidth()  const = 0;
    virtual int             getScreenHeight() const = 0;
};
```

`MockUIBackend` returns arbitrary non-zero integer handles (e.g., an incrementing counter) with no real objects — unit tests that call UIManager methods never dereference Irrlicht pointers, making `src/ui/` genuinely headless-testable and the 80% coverage gate achievable.

- **`UIScaler` testability**: `UIScaler` must accept viewport dimensions at construction (`UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY)`) rather than reading from a live `IVideoDriver`. Tests construct `UIScaler(1920, 1080, 1280, 720, 0, 90)` directly to validate coordinate projection and letterbox offset math without a display. The `unproject` method returns `UIScaler::VirtualPoint` — a nested struct, NOT at namespace scope, to avoid ODR violations. The five named unit tests that must be authored in `tests/ui/ui_scaler_test.cpp` are:
  1. `UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly`: construct with (1920, 1080, 1280, 720, 0, 90); unproject (640, 450) → virtual (960, 720).
  2. `UIScaler_FullNative_NoOffset_ProjectsIdentity`: construct with (1920, 1080, 1920, 1080, 0, 0); unproject (960, 540) → virtual (960, 540).
  3. `UIScaler_PillarboxOffset_UnprojectsCenterCorrectly`: construct with (1920, 1080, 1440, 1080, 240, 0); unproject (960, 540) → virtual (960, 540).
  4. `UIScaler_MouseOutsideViewport_ClampedToEdge`: construct with (1920, 1080, 1280, 720, 0, 90); unproject (640, 80) → virtual y clamped to 0 (actual_y=80 < offsetY=90 produces negative pre-clamp virtual_y, clamped to 0).
  5. `UIScaler_GetViewportRect_ReturnsCorrectOffsets`: construct with (1920, 1080, 1280, 720, 0, 90); `getViewportRect()` returns {x:0, y:90, w:1280, h:720}.
- **`NotificationManager` testability**: `NotificationManager` must accept `IClock*` for dismiss-after-5s timing, `IUIBackend*` for element creation, and `ISimulationPauser*` (interface: `virtual void setPaused(bool) = 0`) for auto-pause injection. **Source location**: `ISimulationPauser.h` lives in **`src/interfaces/`** (NOT `src/simulation/`) — placing it in `src/simulation/` creates a latent circular dependency when `src/ui/` headers include it (`src/ui/` → `src/simulation/` is a prohibited dependency direction; UI must not depend on simulation headers). `src/interfaces/` is a dependency-free common header directory that both `src/simulation/` and `src/ui/` may include. `MockSimulationPauser` lives in `tests/ui/mock_simulation_pauser.h` (used by UI tests that need to verify pause/resume calls without pulling in `CitySimulation`). Tests inject `ManualClock` + `MockUIBackend` + a mock `ISimulationPauser` and call `update()` with controlled time advances to verify queue ordering, auto-dismiss timing, CRITICAL vs Normal band placement, and log-fallback behavior. `NotificationManager` exposes a public `dismissCriticalToast(UIElementHandle handle)` method — this is the production API called by the UI event handler when the player clicks, presses Enter, or presses Delete on a CRITICAL toast; it is not a test-only backdoor. Tests call this method to simulate player dismissal. Additional required test cases:
  - `CriticalToast_OnPost_AutoPausesCalled`: posting a CRITICAL toast calls `setPaused(true)` exactly once when the CRITICAL queue transitions from empty to non-empty.
  - `CriticalToast_OnLastDismiss_NoAutoResume`: calling `dismissCriticalToast(handle)` on the last remaining CRITICAL toast does **NOT** call `setPaused(false)` — auto-resume requires explicit player unpause. Verify `setPaused(false)` is never called by `NotificationManager` on CRITICAL toast dismissal.
  - `CriticalToast_SecondPost_NoDoublePause`: posting a second CRITICAL toast while one is already active does NOT call `setPaused(true)` again.
- **`CameraController` testability**: `CameraController`'s pan/zoom/rotate input processing must be unit-testable by injecting synthetic `InputEvent` structs (defined in `src/platform/input_event.h`). The controller must accept a `CameraState` struct (position, target, pitch, yaw) and expose `getCameraState()` — tests drive events in, read state out, verify pitch clamping at [−70°, −20°] and edge-scroll behavior without a live scene node. **Source location**: `CameraController.h/.cpp` live in `src/ui/` (it is an input/UI concern, not a rendering concern); test file is `tests/ui/camera_controller_test.cpp`. This placement ensures `CameraController` is covered by the `src/ui/` 80% coverage gate.
- **`CameraController` input abstraction**: `CameraController` must accept an `InputEvent` struct (defined in `src/platform/input_event.h`) rather than Irrlicht's `SEvent`, to avoid pulling Irrlicht headers into test translation units:

  ```cpp
  // src/platform/input_event.h
  struct InputEvent {
      enum class Type {
          MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel, KeyDown, KeyUp,
          WindowFocusGained, WindowFocusLost  // required for UIManager pause-on-alt-tab and input arbitration
      };
      Type type;
      int x{0}, y{0};        // cursor position in virtual 1920×1080 space
      int button{0};          // 0=left, 1=right, 2=middle (for mouse button events)
      float wheelDelta{0.f};  // for MouseWheel events
      int keyCode{0};         // SDL2-style key code (for key events)
  };
  ```

  The concrete `IEventReceiver` implementation in `src/platform/` translates `SEvent` to `InputEvent` before forwarding to `CameraController`. Test files in `tests/ui/` construct `InputEvent` structs directly — no Irrlicht headers required. `CameraController::OnInputEvent(const InputEvent&)` replaces `IEventReceiver::OnEvent(const SEvent&)` in the `CameraController` public interface.
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
  1. `ModalDialog_OnOpen_SimulationIsPaused`: calling `UIManager::showModal()` calls `CitySimulation::setPaused(true)` before returning.
  2. `ModalDialog_OnOpen_SpeedSelectorIsDisabled`: `IUIBackend::setElementEnabled(..., false)` is called on the speed selector handle (not `setElementVisible` — the selector remains visible but non-interactive).
  3. `ModalDialog_OnClose_SimulationResumes`: dismissing the modal calls `setPaused(false)` and calls `setElementEnabled(..., true)` on the speed selector to re-enable it.
  4. `UndoSystem_BlockedDuringModal_HotkeyIgnored`: injecting a Ctrl+Z `InputEvent` while modal is active does NOT call any undo operation.
  5. `UndoSystem_BlockedDuringModal_ButtonGrayedOut`: while modal is active, `setElementEnabled(..., false)` is called on the undo button element via `IUIBackend`.
  6. `CriticalToast_DuringModal_IsQueued_NotDisplayed`: posting a CRITICAL toast while a blocking modal is active queues the toast but does NOT display it immediately (no `addStaticText` call to `IUIBackend` for the toast element). After modal dismissal, the toast becomes visible (a deferred `addStaticText` call is verified).
  7. `CriticalToast_DuringModal_AutoPauseDeferred`: CRITICAL toast auto-pause logic does not fire while a blocking modal is active; `setPaused(true)` is NOT called a second time for the toast arrival (the modal pause is already active). After modal dismissal, if the CRITICAL queue is non-empty, auto-pause state is re-evaluated once.
  8. `ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated`: post a CRITICAL toast while a blocking modal is active (verifies no second `setPaused(true)` call during modal-active period), then dismiss the modal (`UIManager::closeModal()`), then verify: (a) the queued CRITICAL toast is now displayed (`addStaticText` called on `MockUIBackend`), and (b) `setPaused(true)` is called **once more** during `closeModal()` re-evaluation — meaning **twice total** across the test (once on modal open, once on re-evaluation in `closeModal()` because CRITICAL queue is non-empty); `setPaused(false)` is NOT called — simulation stays paused because the CRITICAL toast remains active after modal close. **Reconciliation with StrictMock matrix**: The StrictMock Expected Call Matrix entry for this test specifies `setPaused(true) × 2` (total) and `setPaused(false) × 0` — the prose description above matches this. The "exactly once" wording in prior spec drafts referred to the re-evaluation step only (one call within `closeModal()`), not the total across the test; this was ambiguous and has been corrected to "once more during closeModal()". This test exercises the deferred re-evaluation path explicitly — without it, the re-evaluation call in the `closeModal()` code path is unverified and can be silently dropped. **Deferred `addStaticText` call timing**: The CRITICAL toast's `addStaticText` call to `MockUIBackend` MUST occur synchronously within the same `closeModal()` call stack — NOT deferred to the next `update()` tick. This is a firm implementation requirement: the `closeModal()` implementation must call the display logic synchronously, not schedule it for the next frame. Tests assert the element handle's presence immediately after `closeModal()` returns, with no intervening `update()` call. Implementations that defer display to `update()` do not meet this requirement and must be refactored.
  9. `Modal_SpeedSelectorGrayed_DespiteCriticalToast_SpeedAccessible_WhenModalOnly`: when only a CRITICAL toast is active (no modal), the speed selector remains ENABLED (accessible per CRITICAL-toast-pause spec). This distinguishes modal-pause (selector grayed) from CRITICAL-toast-pause (selector accessible).
  10. `ModalDialog_OnClose_WithEmptyCriticalQueue_NoAutoRePause`: open a modal (verifies `setPaused(true)` called once), then dismiss the modal with no CRITICAL toasts in the queue, then verify: (a) `setPaused(false)` is called exactly once (simulation resumes), and (b) `setPaused(true)` is NOT called again during `closeModal()`. This is the inverse of test 8 — it confirms that the CRITICAL toast auto-pause re-evaluation in `closeModal()` does NOT call `setPaused(true)` when the CRITICAL queue is empty, preventing a spurious re-pause on normal modal dismiss.
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
      // Call in TearDown() for any fixture using ManualRNG in strict mode.
      // Throws std::logic_error if either sequence has unconsumed values remaining.
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
- **`ITerrainRNG`** — injectable RNG interface for deterministic terrain generation testing. **Source location**: `ITerrainRNG.h` lives in `src/terrain/`; `MockTerrainRNG` lives in `tests/terrain/mock_terrain_rng.h`. The `TerrainGenerator_AlwaysTerminates_WithinReSeedLimit` property test requires an injectable mock that counts re-seed calls:

  ```cpp
  class ITerrainRNG {
  public:
      virtual ~ITerrainRNG() = default;
      virtual float nextFloat() = 0;              // [0.0, 1.0) — continuous noise and feature probability
      virtual int   nextInt(int min, int max) = 0; // inclusive [min, max] — discrete terrain feature counts, tile selection
      virtual void  reseed(uint64_t seed) = 0;    // called when generator retries with a new seed
  };
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

  // ICitySimulation extends ISimulationPauser so UIManager can pass m_sim to NotificationManager
  // as ISimulationPauser* without requiring a separate constructor parameter. setPaused(bool) is
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
      virtual float getNextUnlockThreshold(Difficulty d) const = 0; // Called by Density Unlock Preview Tooltip to compute proximity to next tier

      // City rating — called by HUD to display star rating:
      virtual int getCityRating() const = 0;  // 0-5 stars; called by HUD city-rating display

      // Demand pressure — called by HUD demand pressure bar per budget tick:
      virtual float getDemandPressurePct(ZoneType zone) const = 0;  // Called by HUD demand pressure bar (R/C/I bars)

      // Undo state — called by HUD undo button to determine enabled/disabled state and countdown:
      virtual bool hasUndoPendingAction() const = 0;  // Called by HUD undo button to gray out when no action is pending
      virtual double getUndoExpiryTimeSeconds() const = 0;  // Returns IClock::nowSeconds() value when the pending undo action expires; 0.0 if no action pending
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
      MOCK_METHOD(float, getNextUnlockThreshold, (Difficulty d), (const, override)); // Called by Density Unlock Preview Tooltip

      // City rating:
      MOCK_METHOD(int, getCityRating, (), (const, override));  // Called by HUD city-rating display (0-5 stars)

      // Demand pressure:
      MOCK_METHOD(float, getDemandPressurePct, (ZoneType zone), (const, override));  // Called by HUD demand pressure bar (R/C/I bars)

      // Undo state:
      MOCK_METHOD(bool, hasUndoPendingAction, (), (const, override));  // Called by HUD undo button to gray out when no action is pending
      MOCK_METHOD(double, getUndoExpiryTimeSeconds, (), (const, override));  // Returns IClock::nowSeconds() value when the pending undo action expires; 0.0 if no action pending
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

`IRenderer` uses opaque `TextureHandle` (uint32_t) instead of `ITexture*` — the same pattern as `IUIBackend` with `UIElementHandle`. This fully severs the compile-time dependency on Irrlicht headers in any translation unit that only includes `IRenderer.h`, including all simulation test files. `MockRenderer::loadTexture()` returns an incrementing non-zero integer. The concrete `IrrlichtRenderer` maintains `std::unordered_map<TextureHandle, ITexture*>` internally.

- **Shared mock header cross-target pattern**: `MockAudioSystem` and `MockRenderer` are defined in `tests/simulation/mock_audio_system.h` and `tests/simulation/mock_renderer.h` respectively. `ManualClock` is defined in `tests/simulation/manual_clock.h`. These headers are shared across multiple CMake test targets (`simulation_tests`, `ui_tests`, `audio_tests`). To avoid ODR (One Definition Rule) violations, these headers must be HEADER-ONLY GMock declarations (using MOCK_METHOD macros only, no definitions). Each test target that uses any of these shared headers MUST add `tests/simulation/` to its `target_include_directories`. This include path coupling is intentional and must be documented explicitly in the CMakeLists.txt for each consuming target. The ODR rule is safe because each test binary links into its own separate executable scope — there is no shared library or link-time merging across test targets. Required `target_include_directories` entries for each consuming target:

  ```cmake
  # simulation_tests — owns the shared mock headers; also needs src/interfaces/ and ${CMAKE_SOURCE_DIR}
  # for project-root-relative includes like #include "src/interfaces/IClock.h" in simulation_smoke_test.cpp
  target_include_directories(simulation_tests PRIVATE tests/simulation/ src/interfaces/ ${CMAKE_SOURCE_DIR})

  # ui_tests — uses MockAudioSystem, MockRenderer, ManualClock from tests/simulation/;
  # MockUIBackend, MockCitySimulation, MockSimulationPauser from tests/ui/;
  # IUIBackend.h from src/ui/; interface headers from src/interfaces/
  target_include_directories(ui_tests PRIVATE tests/simulation/ tests/ui/ src/interfaces/ src/ui/ ${CMAKE_SOURCE_DIR})

  # audio_tests — uses MockAudioSystem from tests/simulation/;
  # IAudioSystem.h from src/interfaces/; audio_constants.h from src/audio/
  target_include_directories(audio_tests PRIVATE tests/simulation/ src/interfaces/ src/audio/ ${CMAKE_SOURCE_DIR})

  # integration_tests — needs shared mock paths and UI header paths for Phase 1+ integration tests
  target_include_directories(integration_tests PRIVATE tests/simulation/ tests/ui/ src/interfaces/ src/ui/ ${CMAKE_SOURCE_DIR})

  # terrain_tests — needs tests/simulation/ for ManualClock (if timing tests added in Phase 2+),
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

```cpp
// UIManager modal/toast tests — use StrictMock for UIBackend; NiceMock for audio
// (audio calls during UI tests are irrelevant noise, not the focus of the assertions).
// TearDown() is required: UIManager destructor calls IUIBackend::removeElement() for all
// live elements; without EXPECT_CALL coverage or NiceMock, StrictMock<MockUIBackend>
// would fail on those teardown calls. Explicitly reset ui_ before MockUIBackend is destroyed.
class UIManagerModalTest : public ::testing::Test {
protected:
    ::testing::StrictMock<MockUIBackend>       backend_;
    ::testing::NiceMock<MockAudioSystem>       audio_;
    ::testing::NiceMock<MockCitySimulation>    sim_;
    ManualClock                                clock_;
    std::unique_ptr<UIManager>                 ui_;
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }
    void TearDown() override {
        // UIManager destructor calls backend_.removeElement() for all live UI elements.
        // Reset ui_ here (before backend_ is destroyed) so destructor calls happen while
        // MockUIBackend is still alive. Without this, the destructor runs in the wrong
        // order (after mock destruction), producing a use-after-destroy crash or
        // spurious StrictMock "unexpected call" failures on removeElement().
        ui_.reset();
    }
};

// Unit tests — use StrictMock to catch unexpected calls:
class CitySimulationUnitTest : public ::testing::Test {
protected:
    ::testing::StrictMock<MockRenderer>    renderer_;
    ::testing::StrictMock<MockAudioSystem> audio_;
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
        sim_.reset();
    }
};

// Property-based / integration tests — use NiceMock to suppress unrelated call noise:
class CitySimulationPropertyTest : public ::testing::Test {
protected:
    ::testing::NiceMock<MockRenderer>    renderer_;
    ::testing::NiceMock<MockAudioSystem> audio_;
    ManualRNG                            rng_{{0}};
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
        sim_.reset();
    }
};
```
