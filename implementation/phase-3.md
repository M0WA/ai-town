## Phase 3: Interface Layer & Test Infrastructure

### Goal

Establish all cross-cutting interface contracts (`ICitySimulation`, `IUIBackend`, `IAudioSystem`), the `UIManager` shell, all mock/manual-test doubles, and every CMake test target required by Phase 4 and later — giving all subsequent phases a fully stub-compiled, testable codebase to build against.

### Deliverables

#### Audio Interface & Stubs

- [ ] `IAudioSystem.h` in `src/interfaces/` — **Phase 0 created a stub with 11 method signatures. Phase 3 VERIFIES and LOCKS it — not re-authors it from scratch.** Phase 3 verifies it includes all 11 methods as defined in `architecture/audio-architecture/audio-system.md`: `playSound`, `playPositionalSound`, `stopSound`, `setMusicTrack`, `setSpeed`, `triggerStinger`, `syncListenerToCamera`, `setGameOverState`, `setTimeOfDay`, `transitionToGameplay`, `update`; all game-domain types declared in `src/interfaces/audio_types.h` — `IAudioSystem.h` includes `audio_types.h` (no OpenAL dependency). Phase 3 also verifies it **`#include "simulation_types.h"` for `SimSpeed`** and explicitly verifies the `syncListenerToCamera(const CameraState& cam)` parameter signature (the parameter MUST be typed `const CameraState&`, not a plain `vec3` or any other type). Do NOT define `SimSpeed` or `SpeedMultiplier` directly in `IAudioSystem.h` or `audio_types.h`; `simulation_types.h` is the canonical owner. Omitting this include causes duplicate-type compile errors in translation units that include both `IAudioSystem.h` and `ICitySimulation.h`. Phase 3 also verifies that the `IClock*` dependency in the `AudioSystem` constructor is properly referenced in `src/audio/audio_system.h` (the stub declares `explicit AudioSystem(IClock* clock)`). **Phase 3 does NOT re-author this interface — it verifies and locks it.** **Intra-phase dependency**: `IAudioSystem.h` must be verified and locked by `sound-dev-opensoftal` before `UIManager` shell implementation begins in Phase 3. **`SimSpeed` vs `SpeedMultiplier` type relationship — RESOLVED**: The canonical enum is `SpeedMultiplier` with exactly 4 values as defined in `architecture/game-design/simulation-time.md`:

  ```cpp
  enum class SpeedMultiplier {
      Paused = 0,  // simulation frozen; real-time multiplier = 0
      x1     = 1,  // 1× real-time
      x3     = 2,  // 3× real-time (default starting speed)
      x10    = 3,  // 10× real-time
  };
  // Type alias for audio-facing API:
  using SimSpeed = SpeedMultiplier;
  ```

  The 5-value form (`PAUSED=0, SLOW=1, NORMAL=2, FAST=3, VERY_FAST=4`) is **NOT** canonical and must NOT appear in `simulation_types.h`. `using SimSpeed = SpeedMultiplier;` makes them identical — no conversion is required at `m_audio->setSpeed(m_sim->getSpeed())` call sites. Phase 3 exit criterion: `simulation_types.h` with the 4-value `SpeedMultiplier` enum and `using SimSpeed = SpeedMultiplier;` alias compiles cleanly. (ref: `architecture/game-design/simulation-time.md`, `architecture/audio-architecture/audio-system.md`, `architecture/testing/testability-architecture.md`)
- [ ] `AudioSystem` stub header `src/audio/audio_system.h` (`sound-dev-opensoftal`): header-only stub (no AL code, no implementation). Declares:

  ```cpp
  class AudioSystem : public IAudioSystem {
  public:
      // BEHAVIORAL CONTRACT locked in Phase 3:
      // If ALC_EXT_thread_local_context is absent at AudioSystem construction,
      // the constructor MUST throw std::runtime_error — no silent fallback is permitted.
      //
      // Extension detection method (LOCKED): use alcGetProcAddress(m_device, "alcSetThreadContext") ONLY.
      // Do NOT use alcIsExtensionPresent — it does not load the function pointer and creates a
      // dual-call pattern where the function may not be linkable directly. If alcGetProcAddress
      // returns null → throw std::runtime_error before launching the audio thread.
      // m_fnSetThreadCtx stores the result; Phase 7 audio thread calls m_fnSetThreadCtx(m_context).
      //
      // Phase 3 stub: the constructor body is empty. No AL or ALC calls are made in Phase 3.
      // The Phase 7 constructor sequence (including the temporary alcMakeContextCurrent(m_context)
      // main-thread bind) is documented in architecture/audio-architecture/audio-system.md
      // and implemented only in Phase 7.
      //
      // BEHAVIORAL CONTRACT for setGameOverState() in V1 (locked Phase 3):
      // Phase 7 stub body MUST be:
      //   void AudioSystem::setGameOverState(bool active) {
      //       // V1: no-op — Sandbox mode has no game-over condition.
      //       LOG_WARNING("setGameOverState() called in V1 Sandbox mode — no-op");
      //       return;
      //   }
      // Do NOT use m_scenarioMode — that member is not declared in V1.
      explicit AudioSystem(IClock* clock);
      // All IAudioSystem pure-virtual methods declared override; bodies empty/throw; no AL calls

      // SHUTDOWN CONTRACT locked in Phase 3:
      // The full shutdown sequence is documented in architecture/audio-architecture/audio-thread-shutdown.md.
      // Key invariant (locked here for Phase 7 implementation): after audio thread joins, the main thread
      // must re-bind the AL context before performing AL cleanup. See audio-thread-shutdown.md step 3.5.
      // Member names for the context and thread-local context flag are frozen: m_context, m_useThreadLocalCtx.
      // Do NOT rename these in Phase 7 without updating this comment.
      //
      // CRITICAL LOOP BOUND NOTE (for Phase 7 implementer):
      // Step 4a (source stop + detach): loop over indices 0..kSFXPoolSize-1 = 0..57
      //   (covers evictable SFX + stingers + reserved slot — NOT stream sources at 58..61)
      // Step 4b (EFX filter delete): loop over indices 0..kEvictableSFXCount-1 = 0..54 ONLY
      //   (m_occlusionFilter[] is sized kEvictableSFXCount = 55; accessing [55], [56], [57] is OOB UB)
      // NEVER use kSFXPoolSize or kTotalSources as the EFX filter loop bound.
      // See audio-thread-shutdown.md step 4b for the rationale.
  private:
      IClock* m_clock{nullptr};
      double m_lastDuckWakeTime{0.0};
      // FROZEN MEMBER NAMES (locked in Phase 3; do NOT rename in Phase 7 without updating this comment):
      //   m_clock, m_lastDuckWakeTime
      // NOTE: All remaining member variables (m_device, m_context, m_stopThread, m_audioThread,
      // m_useThreadLocalCtx, m_fnSetThreadCtx, m_duckState, etc.) are Phase 7 additions.
      // See architecture/audio-architecture/audio-system.md for the full canonical member list.
      // Do NOT add AL-typed members here — audio_system.h must include ZERO OpenAL headers.

      // FROZEN MEMBER NAMES for Phase 7 (commented-out; declare here to lock naming):
      // bool m_useThreadLocalCtx{false};
      // ALCcontext* m_context{nullptr};
      // using FnSetThreadCtx = int(*)(ALCcontext*);  // LOCAL alias — do NOT use PFNALCSETTHREADCONTEXTPROC
      // FnSetThreadCtx m_fnSetThreadCtx{nullptr};
      // struct ALCcontext_struct; using ALCcontext = ALCcontext_struct;  // forward-decl only
      //
      // SA-3 — m_occlusionGainTarget FROZEN (locked Phase 3):
      // std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount];  // Phase 7
      // MANDATORY: std::atomic<float> is REQUIRED — main thread writes, audio thread reads (concurrent access).
      // A plain float[] would be a C++ data race (UB). Initialize all elements to 1.0f before thread launch.
  };
  ```

  **CRITICAL — NO OpenAL includes**: `src/audio/audio_system.h` MUST NOT include `<AL/al.h>`, `<AL/alc.h>`, `<AL/alext.h>`, or any OpenAL header — not directly and not transitively. Violation breaks `audio_tests` compilation on CI (OpenAL::OpenAL is not linked to `audio_tests`). All override method stubs and the constructor must be defined inline in the header with empty bodies. No companion `audio_system_stub.cpp` should be created in Phase 3. Private member variable declarations in the stub must use only forward-declarable types — no `ALuint`, `ALCdevice*`, or any AL-prefixed type in the stub header. (ref: `architecture/audio-architecture/audio-system.md`)
- [ ] `src/interfaces/sound_ids.h` (`sound-dev-opensoftal`): defines ALL V1 `SoundId` constants as `constexpr SoundId` named values per the locked SoundId Assignment Table in `architecture/audio-architecture/v1-audio-asset-manifest.md`. Constants must be grouped into clearly commented sections with the SoundId group layout comment block at the top. Also defines ALL V1 `MusicTrackId` constants: `constexpr MusicTrackId MUSIC_MAIN_MENU_01 = 1`, `MUSIC_MAIN_MENU_02 = 2`, `MUSIC_CALM_01 = 3`, `MUSIC_CALM_02 = 4`, `MUSIC_GROWTH_01 = 5`, `MUSIC_GROWTH_02 = 6`, `MUSIC_CRISIS_01 = 7`, `MUSIC_CRISIS_02 = 8`. `sound_ids.h` must include `audio_types.h`. All simulation and UI code that calls `IAudioSystem::playSound()`, `playPositionalSound()`, or `setMusicTrack()` must use named constants from this file — raw integer literals are prohibited. Owner: `sound-dev-opensoftal`; delivery required before Phase 6 `EarthworksCost_Nonzero_FiresAudioCallback` test is written (test uses `EXPECT_CALL(*m_audio, playPositionalSound(SFX_EARTHWORKS, _, _))`). (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [ ] `src/interfaces/audio_types.h` — **Phase 0 created this file as a stub. Phase 3 EXTENDS and VERIFIES it — not re-authors it from scratch.** Phase 3 verifies or adds: `using SoundId = uint32_t`; `using MusicTrackId = uint32_t`; `using SoundHandle = uint32_t`; `enum class TimeOfDay { DAY, DUSK, NIGHT, DAWN }`; `enum class SoundPriority { LOW = 0, NORMAL = 1, HIGH = 2, CRITICAL = 3 }`. Phase 3 adds the `StingerType` enum class and source pool layout constants in the canonical declaration order (constants first, enum second, static_asserts third):

  ```cpp
  // Step 1: Source pool layout constants
  constexpr int kEvictableSFXCount   = 55;  // sources[0..54]
  constexpr int kStingerCount        = 2;   // V1: sources[55..56] (CRISIS + MILESTONE)
  constexpr int kSFXPoolSize         = 58;  // 55 evictable + 2 stingers + 1 reserved (sources[57])
  constexpr int kStreamSourceCount   = 4;   // sources[58..61] (2 music + 2 ambient beds)
  constexpr int kTotalSources        = 62;  // total alGenSources(62, ...)
  constexpr int kTransientReserveStart = 51;
  constexpr int kMaxVehiclePairs     = 12;

  // Step 2: StingerType enum (values derived from kEvictableSFXCount)
  enum class StingerType {
      CRISIS    = kEvictableSFXCount,      // = 55; maps to sources[55]
      MILESTONE = kEvictableSFXCount + 1,  // = 56; maps to sources[56]
  };

  // Step 3: static_asserts (reference both constants AND StingerType)
  static_assert(kEvictableSFXCount + kStingerCount + 1 + kStreamSourceCount == kTotalSources, ...);
  static_assert(static_cast<int>(StingerType::CRISIS) == kEvictableSFXCount, ...);
  static_assert(static_cast<int>(StingerType::MILESTONE) == kEvictableSFXCount + 1, ...);
  static_assert(kTransientReserveStart < kEvictableSFXCount, ...);
  static_assert(kMaxVehiclePairs * 2 <= kEvictableSFXCount, ...);
  ```

  Phase 3 also adds `constexpr float kZoneLoopMaxPreloadDurationSeconds = 18.0f;`. Do NOT define `SimSpeed` or `SpeedMultiplier` in `audio_types.h`. (ref: `architecture/audio-architecture/audio-system.md`, `architecture/audio-architecture/source-pool.md`)
- [ ] **`src/audio/al_check.h` Phase 3 stub** (`sound-dev-opensoftal`): create `src/audio/al_check.h` with two inline no-op stub functions. Must NOT include any OpenAL headers:

  ```cpp
  #pragma once
  // Phase 3 stubs — no-op. Phase 7 replaces with real AL error checking.
  inline void alCheckError(const char* /*op*/) { /* Phase 7: real impl */ }
  inline void alcCheckError(void* /*device*/, const char* /*op*/) { /* Phase 7: real impl */ }
  ```

  **`alcCheckError` call site cast contract (locked Phase 3)**: Phase 7 call sites MUST cast `ALCdevice*` to `void*` at each `alcCheckError` call site. Example: `alcCheckError(static_cast<void*>(m_device), "alcMakeContextCurrent");` (ref: `architecture/audio-architecture/error-checking.md`)
- [ ] **libvorbisfile and RapidCheck linkage in `audio_tests`**: add `Vorbis::vorbisfile`, `rapidcheck`, and `rapidcheck_gtest` to `audio_tests` `target_link_libraries` now. **`audio_tests` include directories** — the Phase 3 CMake amendment must verify that `target_include_directories(audio_tests PRIVATE ...)` includes ALL of: `tests/simulation/`, `src/interfaces/`, `src/audio/`, and `${CMAKE_SOURCE_DIR}`. (ref: `architecture/ci-cd/dependency-management.md`, `architecture/testing/testability-architecture.md`)
- [ ] `MockAudioSystem` in `tests/simulation/mock_audio_system.h` with GMock `MOCK_METHOD` declarations for all 11 `IAudioSystem` methods. **Compile-smoke requirement**: `MockAudioSystem_InstantiatesCleanly` test in `tests/audio/audio_smoke_test.cpp` passes — confirms all 11 vtable methods are declared and `NiceMock<MockAudioSystem>` is fully constructible. (ref: `architecture/audio-architecture/audio-system.md`, `architecture/testing/testability-architecture.md`)

#### Simulation Interface & Types

- [ ] `ICitySimulation.h` in `src/interfaces/` with minimum method signatures: `setPaused(bool)`, `isPaused() const`, `setSpeed(SpeedMultiplier)`, `getSpeed() const`, `hasUndoPendingAction() const`, `getUndoExpiryTimeSeconds() const`, `CityRatingTier getCityRating() const`, `getDemandPressurePct(ZoneType) const`, `getTreasuryBalance() const`, `getCurrentMonthlyRevenue() const`, `getOutstandingDebt() const`, `estimateMonthlyUpkeep() const`, `getNextUnlockThreshold(Difficulty) const`, `getTotalPopulation() const`, `getConsecutiveDeficitMonths() const`, `getTrafficDemandFactor(ZoneType) const`, `getDensityUnlockState() const`. **`ICitySimulation` extends `ISimulationPauser`**: `class ICitySimulation : public ISimulationPauser`. `ICitySimulation.h` must `#include "simulation_types.h"` to get `Difficulty`, `ZoneType`, `SpeedMultiplier`, and `DensityUnlockState` as complete types. `MockCitySimulation` in `tests/ui/mock_city_simulation.h` with corresponding `MOCK_METHOD` declarations. (ref: `architecture/testing/testability-architecture.md`)

  **Default starting speed**: `kDefaultSimSpeed = SpeedMultiplier::x3` MUST be defined in `simulation_types.h`. In `MockCitySimulation` fixtures, use `ON_CALL(mock, getSpeed()).WillByDefault(Return(SpeedMultiplier::x3))` as the correct default.

- [ ] **`CityRatingTier` enum** — add to `simulation_types.h` as a Phase 3 deliverable:

  ```cpp
  enum class CityRatingTier { Village, Town, City, Metropolis, Megalopolis };
  ```

- [ ] `ManualRNG` in `tests/simulation/manual_rng.h`: two independent sequences (`intSeq` and `floatSeq`); strict mode (default) throws `std::logic_error` on sequence exhaustion; validates all `floatSeq` values in [0.0, 1.0) at construction time with a throw; non-strict mode wraps around. **`ManualRNG::verifyAllConsumed()` method**: asserts `m_intIdx == m_intSeq.size()` AND `m_floatIdx == m_floatSeq.size()`. Call `rng_.verifyAllConsumed()` in `TearDown()` for any fixture using `ManualRNG` in strict mode. **`ManualRNG` self-tests** (`test-dev-cpp`): add all 6 required named test cases in `tests/simulation/manual_rng_test.cpp`:
  1. `ManualRNG_VerifyAllConsumed_ThrowsOnOverProvision`
  2. `ManualRNG_VerifyAllConsumed_NoThrowWhenFullyConsumed`
  3. `ManualRNG_EmptyIntSeq_ThrowsAtConstruction`
  4. `ManualRNG_FloatSeqOutOfRange_ThrowsAtConstruction`
  5. `ManualRNG_EmptyFloatSeq_ThrowsAtConstruction`
  6. `ManualRNG_NextInt_OutOfRange_ThrowsAtCallTime`

  **Fixture initialization note (CRITICAL)**: In ALL fixture definitions that declare a `ManualRNG` member, the member MUST be initialized as `ManualRNG rng_{{0}}` (single-element initializer list with one integer). Using `rng_{}` or `rng_{{}}` passes an empty `initializer_list<int>` and throws `std::invalid_argument` in `SetUp()`, aborting every test in the fixture. NOTE: `AudioSystem` does NOT accept `ISimulationRNG*` — `audio_tests` fixtures must NOT declare `ManualRNG` members.

  **Compile-only stub tests** (`test-dev-cpp`): add two compile-only stub tests in `tests/simulation/city_simulation_stub_test.cpp`:

  ```cpp
  TEST(CitySimulation_DeficitStreakCounter_IncrementsModeIndependently, Phase3Stub) { SUCCEED(); }
  TEST(CitySimulation_AutoSlowMode_IndependentOfSimSpeed, Phase3Stub) { SUCCEED(); }
  ```

  **CMake registration is a Phase 3 deliverable** — final-form `simulation_tests` CMake:

  ```cmake
  add_executable(simulation_tests
      tests/simulation/smoke_test.cpp                 # Phase 0 — must be preserved
      tests/simulation/manual_rng_test.cpp            # Phase 3 addition
      tests/simulation/city_simulation_stub_test.cpp  # Phase 3 addition
  )
  ```

  (ref: `architecture/testing/testability-architecture.md`, `architecture/testing/framework.md`)
- [ ] `ManualClock` in `tests/simulation/manual_clock.h` (ref: `architecture/testing/testability-architecture.md`). **`ManualClock` must also be included in the `tests/audio/` CMake target's include directories** — add `tests/simulation/` to `target_include_directories(audio_tests PRIVATE ...)`.
- [ ] `MockRenderer` in `tests/simulation/mock_renderer.h` returning incrementing non-zero `TextureHandle` values starting from 1.
  - `TextureHandle` (uint32_t) typedef and `kInvalidTexture = 0` sentinel defined in `IRenderer.h` before the class declaration
  - `CameraParams` struct defined in the same `IRenderer.h` header
- [ ] `WallClock` implementation in `src/platform/WallClock.cpp` — Phase 3 VERIFIES that `src/platform/WallClock.cpp` is present and contains a `nowSeconds()` body using `std::chrono::steady_clock` (Phase 1 deliverable — do NOT re-author). If the file is absent or the body is missing, that is a Phase 1 regression that must be fixed before Phase 3 closes. (ref: `architecture/testing/testability-architecture.md`)
- [ ] `NullSimulationPauser` in `src/interfaces/null_simulation_pauser.h` — no-op `ISimulationPauser` implementation. (ref: `architecture/testing/testability-architecture.md`)

#### UI Interface & UIManager Shell

- [ ] `IUIBackend` interface in `src/ui/`: all required methods — `addStaticText`, `addButton`, `removeElement`, `setElementText`, `setElementVisible`, `isElementVisible`, `setElementEnabled`, `isElementEnabled`, `setElementAlpha`, `setElementImage`, `getElementText`, `getElementRect`, `getScreenWidth`, `getScreenHeight`, `getVirtualWidth() const`, `getVirtualHeight() const`, `loadTexture(const std::string& path)` — **17 methods total (including `loadTexture()`)**. **`IUIBackend.h` MUST be placed in `src/ui/` (NOT `src/interfaces/`).** `IrrlichtUIBackend` stub in `src/rendering/` must provide no-op stub implementations for ALL 17 methods. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/ui-manager.md`)
- [ ] `UIElementHandle` (uint32_t), `kInvalidUIElement = 0`, `Rect` struct defined before `IUIBackend` in `IUIBackend.h` (ref: `architecture/testing/testability-architecture.md`)
- [ ] `MockUIBackend` in `tests/ui/mock_ui_backend.h` (ref: `architecture/testing/testability-architecture.md`)
- [ ] `MockSimulationPauser` in `tests/ui/mock_simulation_pauser.h` with `MOCK_METHOD(void, setPaused, (bool), (override))`. **Phase 6 `NotificationManager` auto-pause tests use `MockCitySimulation` (NOT `MockSimulationPauser`)** — `MockSimulationPauser` is used only for contexts where a bare `ISimulationPauser*` is needed. (ref: `architecture/testing/testability-architecture.md`)
- [ ] **Phase 3 VERIFIES** that `UIScaler::unproject()` and `UIScaler::getViewportRect()` are implemented (Phase 1 deliverables — do NOT re-author). If either is absent or still a stub returning zero/empty, that is a Phase 1 regression requiring a fix before Phase 3 can close. Phase 3 confirms `UIScaler` has constructor signature `UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY)`. **`UIScaler` must expose `VirtualPoint unproject(int physicalX, int physicalY) const;` as a public method, where `VirtualPoint` is a nested struct `{ int x; int y; }` declared INSIDE `UIScaler`.** **`UIScaler` unit tests — VERIFY**: all 5 named `UIScaler` unit tests from Phase 1 are Phase 1 deliverables. Phase 3 verifies that they still pass after Phase 3 UIScaler VERIFY. Phase 3 adds one additional compile-only stub:
  1. `UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly` — **Phase 1 deliverable; VERIFY passes**
  2. `UIScaler_FullNative_NoOffset_ProjectsIdentity` — **Phase 1 deliverable; VERIFY passes**
  3. `UIScaler_PillarboxOffset_UnprojectsCenterCorrectly` — **Phase 1 deliverable; VERIFY passes**
  4. `UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero` — **Phase 1 deliverable; VERIFY passes**
  5. `UIScaler_GetViewportRect_ReturnsCorrectOffsets` — **Phase 1 deliverable; VERIFY passes**
  6. `UIScaler_MouseInBottomBlackBar_VirtualY_ClampedToMax` — Phase 3 compile-only stub; Phase 6 fills real assertion.

  Tests 1–5 authored in Phase 1 and must pass before Phase 4 begins. (ref: `architecture/ui-ux/resolution-ui-scaling.md`, `architecture/testing/testability-architecture.md`)
- [ ] `src/platform/input_event.h` — Phase 3 VERIFIES this file is present with the complete `InputEvent` struct (Phase 1 deliverable — do NOT re-author). Fields include: `x`, `y` (virtual 1920×1080 space, for UI hit-testing), `physX`, `physY` (physical pixels, for drag-delta in `CameraController`), `button`, `wheelDelta`, `keyCode`. **The `physX`/`physY` fields are Phase 1 deliverables — Phase 3 VERIFIES these fields are present, not re-authors them.** The concrete Irrlicht `IEventReceiver` adapter calls `UIScaler::unproject()` and stores the result in `InputEvent.x`/`InputEvent.y` before forwarding to `UIManager::onEvent()`. (ref: `architecture/testing/testability-architecture.md`)
- [ ] **`NotificationManager` stub class** (`gamedesign-ux`): `src/ui/notification_manager.h` / `notification_manager.cpp`: constructor signature `NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock)`. **Reason for `ICitySimulation*` (not `ISimulationPauser*`)**: `NotificationManager` needs `getConsecutiveDeficitMonths()` which is defined on `ICitySimulation`, not on the `ISimulationPauser` subset. Phase 6 signatures locked in Phase 3 even though bodies are Phase 6 deliverables:
  - `void postCritical(const std::string& title, const std::string& body)` — no-op in Phase 3
  - `void postNormal(const std::string& title, const std::string& body)` — no-op in Phase 3
  - `void dismissCriticalToast(UIElementHandle handle)` — must include `UIElementHandle handle` parameter
  - `bool onEvent(const InputEvent& event)` — stub returning `false`
  - `bool hasCriticalToastVisible() const` — stub returning `false`
  - `void update()` — no-op
  - `void draw()` — calls `m_backend->setElementVisible(kNotifSentinel, true)` using locally-defined `constexpr UIElementHandle kNotifSentinel = 0xDEAD0105u`. **Two-sided magic number pattern**: production `.cpp` defines sentinel locally; `tests/ui/panel_sentinel_handles.h` mirrors the same value `kNotificationSentinel = 0xDEAD0105u`. Production code must NEVER include `panel_sentinel_handles.h`.

  **BEHAVIORAL CONTRACT locked in Phase 3** (Phase 6 implementation required): When the CRITICAL queue transitions from empty to non-empty, MUST call `m_sim->setPaused(true)` — auto-pause per `notification-system.md`. MUST NOT call `m_sim->setPaused(false)` on CRITICAL toast dismissal. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/input-arbitration.md`)
- [ ] **GameMode type prerequisite** (`gamedesign-ux`, `graphics-dev-irrlicht`): verify `src/ui/ui_types.h` exists and contains `enum class GameMode { Sandbox, Scenario };`. If absent from Phase 0, Phase 3 MUST create it.
- [ ] `UIManager` shell: owns `IGUIEnvironment*`; `GameState` enum (`MainMenu`, `Gameplay`, `Paused`, `GameOver` — V1 canonical; `PostWinFreePlaying` is post-V1 and MUST NOT be added); construction order per `architecture/ui-ux/ui-manager.md`: `NotificationManager` (FIRST — invariant) → `MainMenuPanel` (stub; calls `m_mainMenu->show()` in constructor) → `HUD` → `TaxRatePanel` → `Minimap` → `InspectorPanel` → `PauseMenuPanel` (stub, accepts only `IUIBackend*`, exposes `void setSettingsPanel(SettingsPanel*)` setter) → `SettingsPanel` (stub, empty constructor accepting `IUIBackend*`) → `ModalDialog`; draw order (exactly 10 slots): (1) MainMenuPanel → (2) Minimap → (3) HUD → (4) TaxRatePanel → (5) InspectorPanel → (6) NotificationManager toast stack → (7) PauseMenuPanel → (8) SettingsPanel → (9) Background scrim (modal-active) → (10) ModalDialog (topmost). **All 10 draw-order slots in `UIManager::draw()` MUST be wired in Phase 3.** **Event dispatch chain** — all 6 priority tiers structurally present in Phase 3 (stubs acceptable; branching NOT optional). Priority 2 dual-guard (LOCKED): skipped when no CRITICAL toast visible OR modal active. **Toolbar carve-out pixel bounds** in `src/ui/ui_constants.h`: `kToolbarLeft=8`, `kToolbarRight=72`, `kToolbarTop=64`, `kToolbarBottom=784` (all in 1920×1080 virtual space). (ref: `architecture/ui-ux/ui-manager.md`, `architecture/ui-ux/input-arbitration.md`)

  **Phase 3 UIManager required public method stubs** (all locked now to prevent Phase 6 header breaks):
  - `void update(float realDeltaSeconds)` — polls `m_notifications->update()` and `m_sim->getConsecutiveDeficitMonths()` (GD-H3 bridge)
  - `void transitionToPaused()`
  - `void transitionToGameplay_fromPaused()`
  - `void transitionToGameOver()` — stub body: `if (m_gameMode != GameMode::Scenario) return; /* Phase 6: real implementation */`
  - `void showForcedLoanDialog(const LoanTerms& terms)`
  - `void showGameOverModal(int64_t totalDebt, int monthsInDeficit)`
  - `void closeModal()`
  - `void showSettings()`
  - `void transitionToGameplay(GameMode mode)` — stub body must include: `m_gameMode = mode; m_hasUnsavedChanges = false;`
  - `void setUnsavedChanges(bool value)` — stub sets `m_hasUnsavedChanges = value`

  **UIManager member variables required in Phase 3**: `bool m_hasUnsavedChanges{false}`, `int m_lastKnownDeficitStreak{-1}`, `UIElementHandle m_scrimHandle{kInvalidUIElement}`.

  **UIManager constructor signature (4 parameters)**: `UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)`.

  (ref: `architecture/ui-ux/ui-manager.md`, `architecture/ui-ux/input-arbitration.md`)
- [ ] **Minimap, TaxRatePanel, and InspectorPanel stub classes** (`gamedesign-ux`): all three in `src/ui/`, each with constructor accepting `IUIBackend*`; public methods `show()` (no-op), `hide()` (no-op), `draw()` (calls `m_backend->setElementVisible(kSentinel, true)`), **`Rect getBounds() const`** (stub returning `{0,0,0,0}`). Using `irr::core::rect<s32>` as the return type is prohibited — pulls Irrlicht headers into `src/ui/` headers, violating testability isolation. (ref: `architecture/testing/testability-architecture.md`)

  **UX-4 — Phase 3 `getBounds()` call site requirement**: Phase 3 MUST include at least one call to `m_minimap->getBounds()` at the Priority 3 input arbitration site in `UIManager::onEvent()`.

  **Phase 6 carve-out test stub (Phase 3 exit criterion)**: Phase 3 MUST register `InspectorPanel_DismissClick_MinimapAreaPassesThrough` compile-only stub (body `SUCCEED()`) in `tests/ui/`.
- [ ] **`src/interfaces/LoanTerms.h` stub** (`test-dev-cpp`): `struct LoanTerms { float amount{0.0f}; int repaymentTicks{0}; float interestRate{0.05f}; };`
- [ ] **`src/ui/key_bindings.h` stub** (`graphics-dev-irrlicht`): contains default values for all hotkeys from `architecture/ui-ux/hotkey-scheme.md`, a `load(const std::string& path)` method stub, and an `isReservedKey(const std::string& key)` method stub that returns `true` for "Q" and "E". `CameraController` must accept a `const KeyBindings&` reference. Three test cases in `tests/ui/key_bindings_test.cpp`: `KeyBindings_IsReservedKey_Q_ReturnsTrue`, `KeyBindings_IsReservedKey_E_ReturnsTrue`, `KeyBindings_IsReservedKey_W_ReturnsFalse`. (ref: `architecture/ui-ux/hotkey-scheme.md`, `architecture/testing/testability-architecture.md`)
- [ ] **HUD class stub** in `src/ui/hud.h` (`gamedesign-ux`): constructor accepts **4 parameters**: `IUIBackend* backend`, `IAudioSystem* audio`, `IClock* clock`, `ICitySimulation* sim`. ALL FOUR parameters MUST be stored as member variables. **HUD owns `BudgetDetailPanel` internally** — `BudgetDetailPanel` is NOT a top-level UIManager panel. Phase 3 HUD stub: (a) forward-declare `class BudgetDetailPanel;` in `hud.h`; (b) declare `BudgetDetailPanel* m_budgetDetail{nullptr};`; (c) create companion stub `src/ui/budget_detail_panel.h`; (d) declare `UIElementHandle m_unsavedDotHandle{kInvalidUIElement}`. (ref: `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/ui-manager.md`, `architecture/testing/testability-architecture.md`)

#### Camera Controller Tests

- [ ] Camera controller unit tests in `tests/ui/camera_controller_test.cpp`. The following 8 named test cases (all Phase 1 deliverables) are REQUIRED to pass before Phase 4 begins:
  1. `CameraController_PitchClamp_AtUpperBound_ExactlyMinus20`
  2. `CameraController_PitchClamp_AtLowerBound_ExactlyMinus70`
  3. `CameraController_EdgeScroll_DisabledOnFocusLoss`
  4. `CameraController_RightMouseRotate_MovesYaw`
  5. `CameraController_MiddleMousePan_MovesPosition`
  6. `CameraController_EdgeScroll_EnabledByDefaultInFullscreen`
  7. `CameraController_SetEdgeScroll_Enabled_Then_FocusLost_DoesNotClearEnabled`
  8. `CameraController_EdgeScroll_DisabledByDefaultInWindowed`
  9. `CameraController, EdgeScrollActivatesAt20pxBand` (Phase 3 compile-only stub — body `SUCCEED()`)

  Tests 1–8 must pass as Phase 3 exit criteria (all authored in Phase 1). Test 9 is a Phase 3 compile-only stub; the real assertion is a Phase 6 deliverable. (ref: `architecture/ui-ux/camera-controls.md`, `architecture/testing/testability-architecture.md`)

#### UIManager Draw-Order Tests

- [ ] **`UIManagerDrawOrderTest` fixture** (`test-dev-cpp`): a fixture using **`NiceMock<MockUIBackend>`** (NOT `StrictMock`) with GMock `InSequence` to verify all 10 draw slots are called in the correct Z-order sequence when `UIManager::draw()` is invoked. **Mock policy: NiceMock for ALL mock panels and the backend mock.** Panel sentinel constants live in test-only header `tests/ui/panel_sentinel_handles.h` — NOT in any production panel header. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/ui-manager.md`)

  **CONTRACT**: Each panel stub's `draw()` MUST call `setElementVisible(kSentinelHandle, true)` — without this observable call, `InSequence` enforcement is vacuously satisfied.

  Two additional test cases are Phase 3 deliverables:
  1. `DrawOrder_ModalActive_ScrimAndModalFireAfterPanels` — modal explicitly activated; verifies slots 9 and 10 fire AFTER slot 6
  2. `DrawOrder_PauseMenuVisible_SlotSevenFiresAfterNotification` — calls `ui_->transitionToPaused()` (no-op stub) and verifies via `InSequence` that `kPauseMenuSentinel` fires after `kNotificationSentinel`

  **`ModalDialog` Phase 3 stub `m_active` requirement**: `ModalDialog` Phase 3 stub MUST store `bool m_active{false}`; `show()` sets `m_active = true`; `isActive()` returns `m_active`. **`ModalDialog` constructor MUST accept `ICitySimulation*` as a parameter**. (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`UIManagerModalTest` fixture TearDown contract** (`test-dev-cpp`): the `UIManagerModalTest` fixture must define an explicit `TearDown()` that calls `ui_.reset()`. **Required member declaration order**: `NiceMock<MockUIBackend> backend_` (first), `NiceMock<MockAudioSystem> audio_` (second), `NiceMock<MockCitySimulation> sim_` (third), `std::unique_ptr<UIManager> ui_` (last). **Mock policy for `UIManagerModalTest`**: ALL THREE mock members must use `NiceMock`.

  **Phase 3 smoke test (REQUIRED)**:

  ```cpp
  TEST_F(UIManagerModalTest, FixtureConstructsAndDestructsCleanly) { SUCCEED(); }
  ```

  **Phase 3 event routing test stubs (REQUIRED)** — compile-only (`SUCCEED()`):

  ```cpp
  TEST_F(UIManagerModalTest, EscapeClosesSettingsAndReturnsToPauseMenu) { SUCCEED(); }
  TEST_F(UIManagerModalTest, EscapeClosesSettingsAndReturnsToMainMenu) { SUCCEED(); }
  TEST_F(UIManagerModalTest, ScrimBlocksHUDClickWhenModalActive) { SUCCEED(); }
  ```

  (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/input-arbitration.md`)

#### CI/CD

- [ ] **Integration label routing verification step** (`cicd-dev-github`): Add the 'Verify integration test routing (non-zero discovery)' CI step to `build-linux` and `coverage-linux`, co-landing with the `integration_tests` CMake target registration. This step runs `ctest -N -L '^integration$'` and fails if 0 tests are discovered. This is the phase-gated step deferred from Phase 1 per `architecture/ci-cd/github-actions-workflow.md`. **This step MUST be added in the same commit that registers the `integration_tests` CMake target** — adding it before `integration_tests` exists causes immediate CI failure with 0 discovered tests.

#### CMake Test Targets

- [ ] **`ui_tests` CMake target — consolidated final form (Test-C1)**:

  ```cmake
  add_executable(ui_tests
      tests/ui/camera_controller_test.cpp
      tests/ui/ui_scaler_test.cpp
      tests/ui/key_bindings_test.cpp
      tests/ui/ui_manager_draw_order_test.cpp
      tests/ui/ui_manager_modal_test.cpp
  )
  ```

  Links `aitown_ui GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest`; registered via `aitown_add_tests(ui_tests LABEL "unit")`. Include directories:

  ```cmake
  target_include_directories(ui_tests PRIVATE
      tests/simulation/ tests/ui/ src/interfaces/ src/ui/ ${CMAKE_SOURCE_DIR})
  ```

  **`target_sources()` is PROHIBITED for this Phase 3 amendment** — must use the consolidated `add_executable` form. (ref: `architecture/testing/framework.md`, `architecture/testing/testability-architecture.md`)

- [ ] **`terrain_tests` CMake target skeleton** (`test-dev-cpp`): create `terrain_tests` CMake target with stub source file `tests/terrain/terrain_stub.cpp` (single `TEST` calling `SUCCEED()`). Apply `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)`. Link `aitown_terrain GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest` proactively.

  ```cmake
  target_include_directories(terrain_tests PRIVATE
      tests/simulation/ tests/terrain/ src/terrain/ ${CMAKE_SOURCE_DIR})
  ```

  **Phase 3 requirement**: `terrain_stub.cpp` MUST include BOTH `src/terrain/terrain_chunk.h` AND `src/terrain/ITerrainRNG.h`. Also create stubs:
  - `src/terrain/terrain_chunk.h`: `class TerrainChunk {};` with include guard
  - `src/terrain/terrain_generator.h`: both constructor signatures declared (empty bodies `{}`, NOT `= delete`)
  - `src/terrain/ITerrainRNG.h`: stub with virtual `nextFloat()`, `nextInt()`, `reseed()` methods
  - `tests/terrain/mock_terrain_rng.h`: `MockTerrainRNG` as a **manual stub** (NOT GMock `MOCK_METHOD`) with `std::mt19937_64` engine, `reseedCount()` accessor, and manual implementations of all `ITerrainRNG` virtual methods

  Create `aitown_terrain` as an INTERFACE CMake library target: `add_library(aitown_terrain INTERFACE)`. Phase 5 converts this to a real STATIC or OBJECT library. (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`integration_tests` CMake target**: initial source file `tests/integration/irrlicht_ui_backend_test.cpp` contains a single `EDT_NULL` test that constructs `IrrlichtUIBackend` with an `EDT_NULL` device and calls `addStaticText()`, asserting a non-zero handle is returned; registered via `aitown_add_tests(integration_tests LABEL "integration")`.

  ```cmake
  target_link_libraries(integration_tests PRIVATE
      aitown_render aitown_ui
      GTest::gtest_main GTest::gmock
      rapidcheck rapidcheck_gtest)
  target_include_directories(integration_tests PRIVATE
      tests/simulation/ tests/ui/ src/interfaces/ src/ui/ src/rendering/ ${CMAKE_SOURCE_DIR})
  ```

  **The existing Phase 0 `integration_tests` `target_link_libraries` must be amended in Phase 3 to add `aitown_render aitown_ui`**. (ref: `architecture/testing/framework.md`, `architecture/testing/testability-architecture.md`)

### Exit Criteria

- All 6 `ManualRNG` self-tests pass: `ctest --test-dir build -LE 'integration|requires-opengl' -R ManualRNG --output-on-failure`
- All 5 named UIScaler unit tests (Phase 1 deliverables) verified to still pass; compile-only stub `UIScaler_MouseInBottomBlackBar_VirtualY_ClampedToMax` added and passing as a Phase 3 compile-only stub
- All 8 CameraController named test cases (Phase 1 deliverables) pass; compile-only stub 9 passes in headless CI
- `UIManagerDrawOrderTest` passes: all 10 draw slots called in correct Z-order sequence with `InSequence` enforcement
- `UIManagerModalTest.FixtureConstructsAndDestructsCleanly` passes
- Three `KeyBindings` unit tests pass: `ctest --test-dir build -LE 'integration|requires-opengl' -R KeyBindings`
- `MockAudioSystem_InstantiatesCleanly` test passes in `audio_tests`
- `NiceMock<MockAudioSystem>` compiles in BOTH `audio_tests` AND `ui_tests` CMake targets — verified by `cmake --build build --target audio_tests && cmake --build build --target ui_tests`
- `simulation_tests` Final-form with `city_simulation_stub_test.cpp` third source — `ctest -R Smoke` still passes after amendment
- `terrain_tests` compiles with `terrain_chunk.h`, `ITerrainRNG.h`, AND `terrain_generator.h` included; `MockTerrainRNG` uses manual `std::mt19937_64` stub (not GMock `MOCK_METHOD`); `TerrainGenerator` stub constructors use empty bodies `{}` (not `= delete`)
- `integration_tests` target correctly wired: `ctest --test-dir build -N -L '^integration$'` lists at least one test name
- `src/audio/audio_system.h` contains zero OpenAL includes — verified by `grep -r "AL/al" src/audio/audio_system.h` returning no matches
- `simulation_types.h` compiles cleanly with the canonical 4-value `enum class SpeedMultiplier` and `using SimSpeed = SpeedMultiplier;`
- `UIManager` event dispatch chain is structurally correct: all 6 priority tiers present in `UIManager::onEvent()`, Priority 2 has both guards; verified by code review before Phase 4 begins
- `transitionToGameOver()` Phase 3 stub contains the Sandbox guard: `if (m_gameMode != GameMode::Scenario) return;` — verified by code review before Phase 4 begins
- `src/audio/al_check.h` exists at the canonical path with two inline no-op stubs; no OpenAL headers included transitively
- `IAudioSystem.h` declares `syncListenerToCamera(const CameraState& cam)` with the `const CameraState&` parameter signature as specified in `architecture/audio-architecture/audio-system.md` — verified by `sound-dev-opensoftal` before Phase 3 closes

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `IrrlichtUIBackend` full-compile target, `WallClock.cpp` implementation, `IRenderer` interface; review `key_bindings.h` CMakeLists change |
| `gamedesign-ux` | `UIManager` shell (construction order, draw-order slots, 6-priority event chain, Priority 2 dual-guard, `transitionToGameplay(GameMode)`, `transitionToGameOver()` Sandbox guard, `showSettings()` stub, `BudgetDetailPanel` NOT in UIManager draw-order), HUD class stub (4-param constructor, `m_unsavedDotHandle`), `UIScaler` VERIFY (confirm Phase 1 implemented `unproject()` + `getViewportRect()`; confirm 6-parameter constructor + `VirtualPoint unproject()` nested struct are present and correctly implemented per `architecture/ui-ux/resolution-ui-scaling.md`), `IUIBackend` interface design, `MockSimulationPauser` stub, `NotificationManager` stub (constructor takes `ICitySimulation*`; Phase 6 signatures locked; `draw()` calls sentinel), `Minimap` stub with `getBounds() const`, `TaxRatePanel` stub, `InspectorPanel` stub |
| `test-dev-cpp` | Camera controller unit tests (verify 8 named Phase 1 cases pass + add 1 Phase 3 compile-only stub), `UIManagerDrawOrderTest` (NiceMock for ALL THREE mocks; GMock InSequence; 10 draw slots; additional test cases), `MockRenderer`, `MockUIBackend`, `MockCitySimulation` wiring, `ManualRNG` (6 self-tests, Phase 3 CMake registration), `ManualClock`, `NullSimulationPauser`, `LoanTerms.h` stub, `KeyBindings.h` stub (3 named test cases), `ui_tests` (consolidated Phase 3 form: 5 source files), `integration_tests`, `terrain_tests` CMake targets, `ITerrainRNG.h` stub, `MockTerrainRNG` (manual stub with `std::mt19937_64`, NOT GMock), `UIManagerModalTest` fixture TearDown stub (NiceMock for ALL THREE mocks; 4 compile-only stubs), `panel_sentinel_handles.h` test-only header |
| `sound-dev-opensoftal` | Verify and lock `IAudioSystem.h` (11 method signatures); **verify `IAudioSystem.h` declares `syncListenerToCamera(const CameraState& cam)` with the `const CameraState&` parameter signature as specified in `architecture/audio-architecture/audio-system.md`** (this is the Phase 3 lock of the forward-reference noted in Phase 1 `sound-dev-opensoftal` sign-off); verify and extend `audio_types.h` (canonical declaration order: constants → `StingerType` → static_assert; pool-index WARNING comment; `kZoneLoopMaxPreloadDurationSeconds = 18.0f`); author `sound_ids.h`; author `MockAudioSystem` in `tests/simulation/mock_audio_system.h`; author `AudioSystem` stub header `src/audio/audio_system.h` (no AL includes; BEHAVIORAL CONTRACT; SHUTDOWN CONTRACT); create `src/audio/al_check.h` no-op stub; verify `audio_tests` include directories cover all 4 required paths |
| `cicd-dev-github` | Add 'Verify integration test routing (non-zero discovery)' step to `build-linux` and `coverage-linux` in the same commit as `integration_tests` CMake target registration; verify step fails if 0 integration tests discovered |

### Dependencies

- Requires Phase 2 complete (GL capability queries, `TextureCache` skeleton, `IRenderer` interface, GLEW spike resolved)
- Requires Phase 1 complete (for `IrrlichtUIBackend` compile target, camera + `IrrlichtDevice` lifecycle)

### Risks & Spikes

- **RISK**: `UIManager` construction order violated — `NotificationManager` constructed after another panel — causes crash when that panel enqueues a notification during its own construction. **Mitigation**: `UIManagerDrawOrderTest` fixture must verify construction succeeds without crash; code review must verify construction order matches spec before Phase 3 closes.
- **RISK**: `ManualRNG` fixture initialization error (`rng_{}` instead of `rng_{{0}}`) causes all tests in a fixture to fail with a cryptic `std::invalid_argument`. **Mitigation**: document in Phase 3 plan; 6 self-tests catch this pattern early.
- **RISK**: `vec3.h` and `camera_state.h` headers have companion `.cpp` files — causes `ui_tests` link failures if `aitown_sim` not linked. **Mitigation**: verify both are header-only before Phase 3 closes; if non-inline functions needed, add `aitown_sim` to `target_link_libraries(ui_tests ...)`.
