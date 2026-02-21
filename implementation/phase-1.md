## Phase 1: Irrlicht Device Shell & Camera

### Goal

Establish the Irrlicht device lifecycle, render loop call order, `CameraController`, `IRenderer` interface, and `IrrlichtUIBackend` compile target — giving all subsequent phases a working, compilation-verified display target to build against, before any GL capability queries or texture infrastructure is added in Phase 2.

### Deliverables

#### Graphics / Render System

- [ ] `RenderSystem` class: owns `IrrlichtDevice*` (RAII, `device->drop()` in destructor); `EDT_OPENGL` on all platforms; log and abort if OpenGL unavailable (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] `SIrrlichtCreationParameters`: `EDT_OPENGL`, `Bits=32`, `ZBufferBits=24`, `Stencil=true`, `AntiAlias=4`, `Vsync=false`, initial window `1280×720` (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] Render loop call order enforced: `beginScene` → `sceneManager->drawAll()` → `UIManager::draw()` → `endScene()`. **`m_gui->drawAll()` is NOT called by `RenderSystem`** — per `architecture/graphics-architecture/irrlicht-device-lifecycle.md` and `architecture/ui-ux/ui-manager.md`, `UIManager::draw()` issues explicit per-panel draw calls in Z-order via `IUIBackend`; calling `m_gui->drawAll()` would bypass the explicit layering required for the background scrim and modal overlay. The `IRenderer` facade exposes `endFrame()` as its abstract method name — this wraps `driver->endScene()` internally (Irrlicht has no `endFrame()` method; calling `endFrame()` on any Irrlicht object is a compile error). **Per-frame execution sequence rule** (`graphics-dev-irrlicht`): ALL simulation logic updates and audio updates (`CitySimulation::tick()`, `AudioSystem::syncListenerToCamera()`, `AudioSystem::update()`) MUST execute BEFORE `RenderSystem::beginFrame()` (`driver->beginScene()`) — never interleave logic updates inside the begin/end scene block. The canonical 8-step frame sequence is:
  1. Poll events
  2. Game logic tick (`CitySimulation::tick()`)
  3. `CameraController::update(dt)`
  4a. `AudioSystem::syncListenerToCamera()` — listener position/orientation committed to OpenAL FIRST
  4b. `AudioSystem::update(realDelta)` — then process pending audio command queue entries
  5. `RenderSystem::beginFrame()` (`driver->beginScene()`)
  6. `sceneManager->drawAll()`
  7. `UIManager::draw()`
  8. `RenderSystem::endFrame()` (`driver->endScene()`)

  **OAL-2 ordering rule**: `CameraController::update(dt)` at step 3 MUST execute BEFORE `AudioSystem::syncListenerToCamera()` at step 4a — the listener must read the camera position AFTER it has been updated for the current frame. **Step 4a MUST precede step 4b** — `update()` dispatches pending gain/positional commands using the listener position written by `syncListenerToCamera()`; reversing the order leaves those commands one frame stale. The OAL-2 rule comment at the main loop call site must document this sub-ordering. This sequence must be documented as a comment at the main loop call site. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`, `architecture/ui-ux/ui-manager.md`)

- [ ] `IRenderer` interface in `src/interfaces/` (alongside `IClock.h`, `ISimulationRNG.h`) with `beginFrame()`, `endFrame()`, `drawScene()`, `loadTexture()`, `setCamera()`; `TextureHandle` (uint32_t) typedef and `kInvalidTexture = 0` sentinel defined in `IRenderer.h` before the class declaration; `CameraParams` struct defined in the same `IRenderer.h` header (position `vec3`, target `vec3`, `fovDegrees=45.0f`, `nearClip=0.1f`, `farClip=3000.0f`); concrete `IrrlichtRenderer` in `src/rendering/`. **`IrrlichtRenderer` method responsibilities**: `beginFrame()` calls `driver->beginScene(true, true, SColor(255,0,0,0))`; `drawScene()` calls `sceneManager->drawAll()` ONLY (does NOT call `beginScene` or `endScene`); `endFrame()` calls `driver->endScene()` ONLY. `UIManager::draw()` is called by the main loop BETWEEN `renderer->drawScene()` and `renderer->endFrame()` — it is NOT called inside any `IRenderer` method. Main loop call order: `renderer->beginFrame()` → `renderer->drawScene()` → `uiManager->draw()` → `renderer->endFrame()`. Any implementation that wraps `UIManager::draw()` inside `endFrame()` or combines begin/draw/end into a single method violates the UIManager draw-order spec. (ref: `architecture/testing/testability-architecture.md`)
- [ ] Camera created via `sceneManager->addCameraSceneNode()` only (never FPS/Maya variants); post-creation grab/drop-guarded animator removal loop:

  ```cpp
  while (camera->getAnimators().size() > 0) {
      ISceneNodeAnimator* anim = *camera->getAnimators().begin();
      anim->grab();
      camera->removeAnimator(anim);
      anim->drop();
  }
  ```

  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)

#### Platform Layer

- [ ] `src/platform/input_event.h` — defines the `InputEvent` struct with `Type` enum (`MouseMove`, `MouseButtonDown`, `MouseButtonUp`, `MouseWheel`, `KeyDown`, `KeyUp`, `WindowFocusGained`, `WindowFocusLost`), `x`/`y` (virtual 1920×1080 space, for UI hit-testing), `physX`/`physY` (physical pixels, for drag-delta in `CameraController`), `button`, `wheelDelta`, `keyCode` fields. The platform `IEventReceiver` populates both `x/y` (from `UIScaler::unproject()`) and `physX/physY` (raw `SEvent` coordinates before unproject). This header is required for `CameraController::OnInputEvent()` and `UIManager::onEvent()`. (ref: `architecture/testing/testability-architecture.md`)
- [ ] Platform `IEventReceiver` adapter in `src/platform/` that translates Irrlicht `SEvent` to `InputEvent` — populates `x/y` via `UIScaler::unproject()` and `physX/physY` from raw `SEvent` mouse coordinates; forwards to `UIManager::onEvent()` then to `CameraController::OnInputEvent()` per the 6-priority input arbitration chain. **Short-circuit dispatch rule (MUST be authored in Phase 1)**: call `UIManager::onEvent(event)` first; only if it returns `false` (event not consumed) forward to `CameraController::OnInputEvent(event)`. Exception — camera pass-through events (MMB drag: `MouseButtonDown/Up button=2`, `MouseMove` while MMB held, `MouseWheel`; RMB drag: `MouseButtonDown/Up button=1`, `MouseMove` while RMB held) always reach `CameraController` regardless of `UIManager::onEvent()` return value, per `input-arbitration.md` Priority 1 pass-through rule. This dispatch contract must be authored in Phase 1 even though UIManager is a stub — Phase 3 integration must not require structural rework of the adapter. (ref: `architecture/ui-ux/input-arbitration.md`, `architecture/testing/testability-architecture.md`)
- [ ] `src/platform/WallClock.cpp` — implements `WallClock::nowSeconds()` using `std::chrono::steady_clock`; the `WallClock.h` stub header was delivered in Phase 0 in `src/interfaces/`; this Phase 1 deliverable is the concrete `std::chrono`-based body only. (ref: `architecture/testing/testability-architecture.md` IClock section)

#### CameraController

- [ ] `CameraController` class in `src/ui/`: pan (Middle-mouse-button drag + Arrow keys), zoom (scroll wheel), rotate (Right-mouse-button drag only — Q/E NOT bound); pitch clamped to `[-70°, -20°]` inclusive bounds (`std::clamp` semantics); **default window mode is `windowed` (not fullscreen) at 1920×1080 virtual resolution**; edge scrolling ON by default in exclusive fullscreen, OFF by default in windowed mode; `m_appHasFocus` flag disables edge-scroll on focus loss (note: `m_appHasFocus = false` overrides edge-scroll at the input processing point but does NOT change the stored `m_edgeScrollEnabled` value — restoring focus resumes edge-scroll if enabled is true); constructor must accept `bool startInFullscreen` parameter setting initial `m_edgeScrollEnabled` state; public API must include `bool isEdgeScrollEnabled() const` returning the current value of `m_edgeScrollEnabled` — this accessor is required by test case 6; **public API must also include `void setEdgeScrollEnabled(bool enabled)` as a setter that updates `m_edgeScrollEnabled`** — this setter is required by the Phase 8 Settings panel wiring; edge-scroll activation band is **20 px** from the virtual viewport edge (x < 20, x > 1900, y < 20, y > 1060 in 1920×1080 virtual space). (ref: `architecture/ui-ux/camera-controls.md`, `architecture/testing/testability-architecture.md`)
- [ ] `CameraController` accepts `InputEvent` struct from `src/platform/input_event.h`; `OnInputEvent(const InputEvent&)` method replaces `OnEvent(SEvent&)` in the `CameraController` public interface; returns `true` (consumed) for all relevant event types. (ref: `architecture/testing/testability-architecture.md`)

  **NOTE (UX-1 — Drag-delta coordinate space)**: Drag-delta calculations for pan (MMB drag) and rotate (RMB drag) MUST use physical pixel delta via `InputEvent.physX`/`InputEvent.physY` — NOT the virtual-space `InputEvent.x`/`InputEvent.y` values. Applying `unproject()` to drag deltas would scale camera sensitivity with viewport scaling, producing incorrect pan/rotate speed at non-native resolutions. Camera controller unit tests must inject `physX`/`physY` values for drag-delta tests (cases 4 and 5) and document this in test comments. (ref: `architecture/ui-ux/camera-controls.md` Drag-delta coordinate space section, `architecture/testing/testability-architecture.md`)

- [ ] `CameraController::update(float dt)` calls `camera->setPosition()` and `camera->setTarget()` every frame before `sceneManager->drawAll()`; exposes `getCameraState()` returning a `CameraState`-equivalent struct for test assertions. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] `CameraController` source location: `src/ui/` (it is an input/UI concern, not a rendering concern), ensuring it is covered by the `src/ui/` 80% coverage gate. (ref: `architecture/testing/testability-architecture.md`)

#### UI Backend Compile Target

- [ ] `IrrlichtUIBackend` in `src/rendering/` with `std::unordered_map<UIElementHandle, IGUIElement*>` — compile target only at Phase 1 (full 17-method implementation is a Phase 3 deliverable); the Phase 1 `IrrlichtUIBackend` need only compile and link against Irrlicht. `IUIBackend.h` lives in `src/ui/` (not `src/interfaces/`) per the canonical placement rule. **The Phase 1 stub MUST be a concrete non-abstract class.** It MUST include: (1) `#include "src/ui/IUIBackend.h"` so `UIElementHandle` and `kInvalidUIElement` resolve; (2) `std::unordered_map<UIElementHandle, IGUIElement*> m_elements` member; (3) stub override bodies for ALL 17 `IUIBackend` pure virtual methods (returning `kInvalidUIElement`, `0`, `false`, empty string, or `Rect{}` as appropriate). An `IrrlichtUIBackend` that leaves any method as pure-virtual will be abstract and cannot be instantiated in Phase 3 integration tests. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/ui-manager.md`)

#### Windows DLL CI

- [ ] **Irrlicht.dll on Windows** (`graphics-dev-irrlicht` + `cicd-dev-github`): add a CMake post-build copy rule (`add_custom_command(TARGET aitown_render POST_BUILD ...)`) for `Irrlicht.dll` to the output directory on Windows. vcpkg defaults to `x64-windows` (dynamic) triplet on Windows runners, so `Irrlicht.dll` is a required runtime dependency from Phase 1 onward. **CO-LANDING REQUIREMENT**: The `add_custom_command(TARGET aitown_render POST_BUILD ...)` copy rule MUST be authored in the SAME CMakeLists.txt commit as the `target_link_libraries(aitown_render ... Irrlicht ...)` line — they must co-land. Using the wrong target name (`aitown` instead of `aitown_render`) causes the post-build rule to never fire.

  **CI-2 — Irrlicht linkage format by platform**:
  - **Linux**: `libIrrlicht.a` (static) — vcpkg `x64-linux` triplet; no DLL exists on Linux; do NOT add a Linux `libIrrlicht.a` path check to `build-linux`.
  - **Windows**: `Irrlicht.dll` must be present in `build/Release/` before the test step runs; the post-build copy rule handles this; the existing Phase 0 CI hard-fail verification step must pass.
  - **DLL verification step update (co-landing requirement)**: The `build-windows` CI job's DLL verification PowerShell step MUST be updated to include `Irrlicht.dll` alongside `soft_oal.dll` and `default.mhr`. The check must use: `if (-not (Test-Path 'build/Release/Irrlicht.dll')) { Write-Error 'Irrlicht.dll not found in build/Release/'; exit 1 }` (PS 5.1 compatible — do NOT use `Test-Path ... || exit 1`, which is PS 7+ only). This CI YAML change MUST co-land in the same atomicity commit as the CMake `add_custom_command` copy rule — they are a single atomic unit.

  (ref: `architecture/ci-cd/dependency-management.md`)

  **CI-CRITICAL — Irrlicht.dll grace window**: The grace window downgrade applies ONLY to the `Irrlicht.dll` check — NOT to the existing `soft_oal.dll` and `default.mhr` hard-fail checks. The Phase 0 `soft_oal.dll` and `default.mhr` hard-fail checks must remain untouched throughout the grace window. Correct approach: (1) Add a new separate "Verify Irrlicht.dll present (warning-only during grace window)" PowerShell step using `Write-Warning` without `exit 1`, placed after the existing hard-fail step for `soft_oal.dll`/`default.mhr`. (2) In the co-landing atomicity commit (`target_link_libraries(aitown_render PRIVATE Irrlicht)` + `add_custom_command` DLL copy), remove this warning-only step and add `Irrlicht.dll` to the main "Verify required DLLs are present" hard-fail step. Without this grace window, every `develop` push between Phase 0 completion and the Phase 1 Irrlicht linkage commit will fail Windows CI immediately.

#### validate-assets CI Stub

- [ ] **`validate-assets` CI job stub** (`cicd-dev-github`): introduce the `validate-assets` CI job as an always-passing stub (exits 0) and wire it into `all-checks-pass` `needs:` immediately — the `all-checks-pass` Phase 1+ 5-job form must include `validate-assets`. The Phase 1 stub job runs `python tools/validate_assets.py`. The Phase 1 implementer MUST resolve the `actions/setup-python` SHA live at implementation time — see the Risks & Spikes section. CI-5 in Phase 4 is a re-verification/audit step, not the initial SHA pin.

  **Four-item atomicity requirement** (all four items MUST land in the same commit — a partial commit silently breaks the gate): (1) `tools/validate_assets.py` placeholder script (always exits 0), (2) `validate-assets` CI job YAML entry with `timeout-minutes: 10`, (3) `run: python tools/validate_assets.py` step within that job, (4) `validate-assets` added to `all-checks-pass` `needs:` list in the Phase 1+ 5-job form: `needs: [build-linux, build-windows, coverage-linux, markdown-lint, validate-assets]`. The `timeout-minutes: 10` field is MANDATORY per `architecture/ci-cd/github-actions-workflow.md` — this permanent job definition persists unchanged into Phase 5 and a missing timeout will leave the Phase 5 real validation exposed to runaway processes. (ref: `architecture/ci-cd/github-actions-workflow.md` Phase 1+ form)

  **Branch protection re-confirmation** (`cicd-dev-github`): After the `validate-assets` job runs at least once on a PR targeting `develop`, re-confirm branch protection rules for both `main` and `develop` per `architecture/ci-cd/branch-protection.md` Phase 1 addition procedure. The `all-checks-pass` check name is unchanged; the 5-upstream-job wiring must be verified.

#### Camera Pitch Sign-Off Gate

- [ ] **Camera pitch sign-off gate** (`graphics-artist-3d-model`): verify the camera pitch range `[-70°, -20°]` and billboard bake midpoint `−45°` sign-off is on record in `architecture/asset-standards/3d-model-standards.md` Camera Pitch Range section. **The sign-off is already CONFIRMED in the spec** (recorded as: "CONFIRMED — camera pitch range [−70°, −20°] and bake midpoint −45° are final. Reviewed and approved by: graphics-artist-3d-model."). The Phase 1 action is to verify this entry is present and accurate before Phase 9 billboard asset authoring begins. **Phase 9 billboard bake pipeline MUST NOT begin without this sign-off on record.** (ref: `architecture/asset-standards/3d-model-standards.md`)

#### CameraController Unit Tests

- [ ] **`tests/ui/camera_controller_test.cpp`** (`test-dev-cpp`): author all 6 named `CameraController` unit tests registered under the `ui_tests` CMake target (label `unit`) per `architecture/testing/testability-architecture.md`. Tests inject synthetic `InputEvent` structs; `getCameraState()` used for output assertions; no live Irrlicht scene node required. **CMake interim registration**: add `target_sources(ui_tests PRIVATE tests/ui/camera_controller_test.cpp)` to `CMakeLists.txt` as a Phase 1 interim step. This interim `target_sources()` line will be removed and replaced by the Phase 3 Test-C1 consolidated 5-file `add_executable` form. Without this registration, the 6 `CameraController` tests cannot be discovered by CTest and the Phase 1 exit criterion cannot be satisfied. Required named test cases:
  1. `CameraController_PitchClamp_AtUpperBound_ExactlyMinus20` — inject `MouseWheel` events driving pitch above −20°; assert `getCameraState().pitch == -20.0f` using `EXPECT_FLOAT_EQ` (inclusive bound, `std::clamp` semantics — NOT `EXPECT_LT`)
  2. `CameraController_PitchClamp_AtLowerBound_ExactlyMinus70` — inject `MouseWheel` events driving pitch below −70°; assert `getCameraState().pitch == -70.0f` using `EXPECT_FLOAT_EQ` (NOT `EXPECT_GT`)
  3. `CameraController_EdgeScroll_DisabledOnFocusLoss` — inject `WindowFocusLost`; inject `MouseMove` at virtual x=0 (left edge); assert camera position unchanged (m_appHasFocus=false suppresses edge-scroll)
  4. `CameraController_RightMouseRotate_MovesYaw` — inject `MouseButtonDown button=1` (RMB) + `MouseMove physX=prevPhysX+10` (horizontal drag of 10 physical pixels — `CameraController` reads `physX/physY` per UX-1; test comment must state this); assert `getCameraState().yaw != initialYaw`
  5. `CameraController_MiddleMousePan_MovesPosition` — inject `MouseButtonDown button=2` (MMB) + `MouseMove physX=prevPhysX+5` (`CameraController` reads `physX/physY` per UX-1; test comment must state this); assert camera position changed
  6. `CameraController_EdgeScroll_EnabledByDefaultInFullscreen` — construct with `startInFullscreen=true`; call `isEdgeScrollEnabled()` immediately without any `setEdgeScrollEnabled(true)` call; assert `true`

  **Include path strategy for `input_event.h`**: `camera_controller_test.cpp` MUST include `input_event.h` via the project-root-relative form `#include "src/platform/input_event.h"`, relying on `${CMAKE_SOURCE_DIR}` already present in `ui_tests` `target_include_directories`. Do NOT use an unqualified `#include "input_event.h"` — `src/platform/` is not in the Phase 0 `ui_tests` include path and the unqualified form will fail to compile.

  (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/camera-controls.md`)

### Exit Criteria

- Application window opens with blank scene on Linux and Windows
- Camera pans (MMB drag + Arrow keys), rotates (RMB drag), and zooms (scroll wheel) correctly within pitch constraints `[-70°, -20°]` inclusive
- `IrrlichtUIBackend` compiles and links against Irrlicht — full 17-method integration is a Phase 3 deliverable
- `src/platform/input_event.h` is present and the `InputEvent` struct includes both `x/y` (virtual space) and `physX/physY` (physical pixels) fields
- `WallClock.cpp` (`std::chrono::steady_clock` body) is implemented in `src/platform/`
- Windows CI DLL verification step hard-fails on missing `Irrlicht.dll` — the Phase 0 baseline `ci.yml` hard-fail is already in place; the Phase 1 CMake post-build copy rule is added so this existing hard-fail step passes; CI-CRITICAL grace window applied before the co-landing commit and restored in the co-landing commit
- `validate-assets` CI job stub is present in `ci.yml` (job name `validate-assets`, runs `python tools/validate_assets.py`, always exits 0), AND appears in `all-checks-pass` `needs:` list using the Phase 1+ 5-job form: `needs: [build-linux, build-windows, coverage-linux, markdown-lint, validate-assets]` — per `architecture/ci-cd/github-actions-workflow.md` Phase 1+ form. The three-item atomicity commit (`tools/validate_assets.py` placeholder + `validate-assets` CI job YAML + `all-checks-pass` `needs:` update) must be verifiable as a single merged commit. CI passes with stub always-exit-0.
- All 6 named `CameraController` unit tests in `tests/ui/camera_controller_test.cpp` compile and pass under `ctest -LE "integration|requires-opengl"` (label `unit`); pitch clamp tests use `EXPECT_FLOAT_EQ` for exact boundary equality per `architecture/ui-ux/camera-controls.md` inclusive-bound semantics
- Camera pitch sign-off verified: the `graphics-artist-3d-model` role has left a dated Phase 1 sign-off record. Acceptable forms: (a) a dated comment added to the Camera Pitch Range section in `architecture/asset-standards/3d-model-standards.md` during Phase 1 work (separate from the pre-existing CONFIRMED line, timestamped to Phase 1 completion), or (b) a GitHub PR review approval from the `graphics-artist-3d-model` role on the Phase 1 PR. The pre-existing CONFIRMED line alone does NOT satisfy this exit criterion. **Phase 9 billboard bake pipeline (City Assets) MUST NOT begin without this Phase 1 sign-off on record.**
- Branch protection re-confirmation completed for both `main` and `develop` after `validate-assets` job first runs on a PR
- **`requires-opengl` label-routing verification timing**: The `ctest -N -L '^requires-opengl$'` non-zero discovery verification step MUST NOT be added to `build-linux` or `coverage-linux` in Phase 1 — no `requires-opengl` tests exist yet and this step would immediately fail CI. This step is a Phase 2 deliverable, co-landing with `lod_swap_smoke_test.cpp`.

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `RenderSystem` RAII + creation params, render loop call order (8-step canonical sequence; OAL-2 rule enforced; `UIManager::draw()` NOT `m_gui->drawAll()`), `IRenderer` interface (`beginFrame/endFrame/drawScene/loadTexture/setCamera`; `TextureHandle`/`CameraParams` in `IRenderer.h`), camera node setup (`addCameraSceneNode()` only; grab/drop-guarded animator removal loop), `CameraController` class (pan/zoom/rotate; pitch `[-70°,-20°]`; edge scroll; `m_appHasFocus`; `bool startInFullscreen`; `isEdgeScrollEnabled()`; `OnInputEvent()`; drag-delta = physical pixels per UX-1), `IrrlichtUIBackend` compile target, platform `IEventReceiver` adapter (SEvent → InputEvent, populating both virtual x/y and physical physX/physY) |
| `cicd-dev-github` | `Irrlicht.dll` post-build copy rule co-landing review, CI-CRITICAL grace window downgrade/restore, `validate-assets` CI job stub wiring (three-item atomicity commit: placeholder script + CI job YAML + `all-checks-pass` Phase 1+ 5-job `needs:` update), branch protection re-confirmation |
| `test-dev-cpp` | `tests/ui/camera_controller_test.cpp`: all 6 named `CameraController` unit tests (pitch clamp × 2 using `EXPECT_FLOAT_EQ`; edge-scroll focus loss; RMB rotate using `physX/physY`; MMB pan using `physX/physY`; edge-scroll fullscreen default); registered under `ui_tests` CMake target (label `unit`) |
| `graphics-artist-3d-model` | Camera pitch sign-off gate (verify sign-off is on record in `architecture/asset-standards/3d-model-standards.md`; sign-off is already CONFIRMED — Phase 9 billboard bake pipeline may proceed on that basis) |

### Dependencies

- Requires Phase 0 complete

### Risks & Spikes

- **RISK**: Irrlicht.dll co-landing window causes Windows CI hard-fail between Phase 0 and Phase 1 linkage commit → **Spike**: apply CI-CRITICAL grace window (downgrade to `Write-Warning`) before starting Phase 1; restore hard-fail in the atomicity commit.
- **RISK**: `addCameraSceneNode()` animator conflicts with `CameraController` pitch clamping → **Spike**: verify all default animators are removed via the grab/drop-guarded loop in `architecture/graphics-architecture/scene-graph-ownership.md` before `CameraController` takes over; `addCameraSceneNode()` (unlike FPS/Maya variants) does not attach animators by default, so the loop is a defensive precaution.
- **RISK**: `actions/setup-python` SHA placeholder `@<RESOLVE_AT_IMPLEMENTATION_TIME>` in the `validate-assets` job will be flagged by the supply-chain lint step in `build-linux` (lint scans the entire `ci.yml` file) → **Mitigation**: The Phase 1 implementer MUST resolve the `actions/setup-python` SHA live at implementation time using `gh release view --repo actions/setup-python` before merging — the placeholder form MUST NOT be committed. The "CI-5 `actions/setup-python` SHA resolution" listed as a Phase 4 deliverable refers to Phase 4 formally auditing and re-verifying the SHA, not to Phase 1 leaving a placeholder. Phase 1 must supply a real, fully-resolved 40-character SHA. The supply-chain lint in `build-linux` will fail the entire CI if any placeholder remains in `ci.yml`.
- **RISK**: `physX/physY` fields absent from `InputEvent` struct if author follows an older draft → **Spike**: audit `input_event.h` before any camera controller tests run; both virtual `x/y` and physical `physX/physY` fields are mandatory per `architecture/testing/testability-architecture.md` and `architecture/ui-ux/camera-controls.md`.
