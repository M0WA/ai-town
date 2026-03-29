# AI Town — Technical Squad Architecture Review

**Reviewers**: Senior C++ Developer (Irrlicht), Senior C++ Developer (OpenAL Soft),
Senior C++ Test Engineer, Senior GitHub Pipeline Engineer

**Date**: 2026-03-29

**Scope**: All architecture spec files in `/workspace/architecture/` reviewed for
gaps, problems, inconsistencies, duplicates, and missing content.
No source files were modified.

---

## Table of Contents

1. [Graphics Architecture](#1-graphics-architecture)
2. [Audio Architecture](#2-audio-architecture)
3. [Testing Architecture](#3-testing-architecture)
4. [CI/CD Architecture](#4-cicd-architecture)
5. [Game Design — Technical Feasibility](#5-game-design--technical-feasibility)
6. [UI/UX — Technical Feasibility](#6-uiux--technical-feasibility)
7. [Asset Standards — Technical Feasibility](#7-asset-standards--technical-feasibility)
8. [Cross-Domain Issues](#8-cross-domain-issues)

---

## 1. Graphics Architecture

### 1.1 Irrlicht Device Lifecycle (`irrlicht-device-lifecycle.md`)

**[GAP] MEDIUM — Frame-rate cap implementation detail missing**
The spec mandates a 60 FPS cap via `std::this_thread::sleep_for` but does not specify
how `realDelta` is computed when sleep causes the frame to run longer than 1/60 s
(e.g. under load). If `realDelta` is capped at 1/60 s (ignoring catch-up), fast
simulation speeds may fall behind. If it is not capped, a sleep overshoot accumulates
into the simulation. The spec should explicitly state: "realDelta is the raw wall-clock
delta even if it exceeds 1/60 s — no clamping is applied."

**[GAP] MEDIUM — Construction sequence step 16 (setUIManager) is described as "late-binding"
but no thread-safety guarantee is given**
Step 16 calls `renderer->setUIManager(uiManager)` after the Irrlicht device is fully
constructed. If any background thread (e.g. the audio thread spawned at step 14) could
call a renderer accessor between steps 14 and 16, a data race exists. The spec should
either state that the audio thread never touches the renderer, or add an explicit
contract that `setUIManager` is called before the audio thread is started.

**[GAP] LOW — Missing specification for what happens if `beginScene` fails**
The spec lists the mandatory 11-step per-frame loop but does not specify the recovery
path if `beginScene()` returns `false` (e.g. window minimised on Windows, driver lost).
The implementation will need to handle this; a note should be added.

**[INCONSISTENCY] MEDIUM — The spec says the loading screen does not call
`guiEnvironment->drawAll()`** but the same 11-step loop (which includes drawAll at step 6)
is described as the canonical frame loop. The exception for the loading screen carves
out a separate code path that is not fully documented — it is unclear whether steps 5
(AudioSystem::update), 4a (syncListenerToCamera), and 3b (UIManager::update) also fire
during the loading screen. The spec should enumerate which steps are active/inactive
during the loading-screen state.

---

### 1.2 Procedural Terrain (`procedural-terrain.md`)

**[GAP] HIGH — kCardinalFalloff / kDiagonalFalloff values are "not yet signed off"**
The spec explicitly notes that neighbour-blending falloff constants have not received
final sign-off. These values directly affect terrain visual quality and the
`setTileHeight` earthworks mesh outcome. Until signed off, any property-based test that
verifies blending correctness cannot be written against a stable spec. The sign-off
must happen before Phase 10b implementation begins, or the spec must mark these as
"subject to tuning" with explicit provisional values and a Phase 10b acceptance test
that validates the final values.

**[GAP] MEDIUM — DDA pickTerrainTile ray-terrain miss path**
The spec describes the DDA algorithm for ray-terrain intersection but does not define
what `pickTerrainTile()` returns when the ray leaves the terrain bounds without
intersecting any chunk (the "miss" path). The caller in `UIManager` (Priority 3 inspector
open path) checks the return value, but the return type and miss sentinel are not
specified in this file. Cross-reference to a definition in `IRenderer.h` is needed.

**[GAP] MEDIUM — flushPendingRebuilds 100 ms budget: wall-clock or sim-clock?**
`flushPendingRebuilds()` is specified to run with a 100 ms time budget. However, the
spec does not state whether this budget is measured against `IClock::nowSeconds()`
(wall-clock) or simulation time. For deterministic CI tests the distinction matters.
The IClock injection seam is mentioned, but the contract (wall-clock seconds) should
be made explicit.

**[MISSING] MEDIUM — No spec for how terrain chunks handle the boundary between the
map edge and "out of bounds"**
The DDA algorithm will eventually step off the edge of the map. The spec does not
define whether out-of-bounds tiles are clamped, wrapped, or treated as a miss. This
omission will result in implementation guesswork at boundary conditions.

---

### 1.3 Scene Graph Ownership (`scene-graph-ownership.md`)

**[PROBLEM] HIGH — Irrlicht compiled with `-fno-rtti`**
The spec correctly warns that `dynamic_cast` on Irrlicht types will SIGSEGV. However,
there is no corresponding note in `IrrlichtRenderer.h` or `SceneEntityManager.h`
directing developers to avoid `dynamic_cast` within those files. A `// WARNING:
dynamic_cast on Irrlicht types is UNDEFINED BEHAVIOR (-fno-rtti)` comment should be
mandated in the spec as a required header-level comment in those files.

**[GAP] MEDIUM — Zone overlay u16 batching: no spec for overflow handling**
The spec caps zone overlay SMeshBuffer at 10,922 quads (u16 limit). However, it does
not define what happens when more than 10,922 zone tiles are active simultaneously
(a 512×512 map has 262,144 tiles). The overflow path — allocate a second
SMeshBuffer, create a second scene node — is not specified. Without this, a large
city will produce rendering corruption (truncated overlay) or a crash from integer
overflow in the index buffer.

**[GAP] MEDIUM — Permanent renderer-internal nodes (sky dome, cloud plane) exempt from
SceneEntityManager: no cleanup contract**
The spec states these nodes are exempt from `SceneEntityManager`. However, it does not
specify when and how they are cleaned up on renderer shutdown. The Irrlicht device
`drop()` will remove all remaining scene nodes, but if the sky dome holds extra
references (e.g. via the cloud dome shader callback `grab()`), a reference leak may
occur. The shutdown sequence for renderer-internal nodes should be documented.

**[INCONSISTENCY] LOW — B3D asset pattern "borrowed from Irrlicht mesh cache, no drop after
setMesh"**
This is correct per Irrlicht's `setMesh()` contract, but the spec in `texture-cache.md`
describes a separate eviction path for textures. A note should clarify that B3D mesh
eviction (from the Irrlicht mesh cache) is NOT covered by `TextureCache` and follows
Irrlicht's internal cache management.

---

### 1.4 Texture Cache (`texture-cache.md`)

**[PROBLEM] HIGH — EDT_NULL guard for raw OpenGL calls is described in
`2d-texture-standards.md` but not in `texture-cache.md`**
`TextureCache::loadSRGB()` calls `glGenTextures` / `glCompressedTexImage2D`. The spec
in `2d-texture-standards.md` mandates an `EDT_NULL` guard before any raw GL call, but
`texture-cache.md` does not repeat or reference this guard. A developer reading only
`texture-cache.md` (the canonical texture loading spec) will miss the required guard,
leading to UB in integration tests. The guard must be documented in `texture-cache.md`
or a cross-reference to `2d-texture-standards.md` must be made explicit.

**[GAP] MEDIUM — LRU eviction policy: no maximum cache size specified**
`texture-cache.md` documents three pools and an eviction path, but never specifies the
maximum memory budget (in MB) before LRU eviction fires. Without a budget the
`evictUnreferenced()` decision point is implementation-defined. This is a functional gap
— the spec should state a nominal VRAM budget (e.g. 256 MB) or admit that eviction is
demand-driven and bounded by GPU VRAM.

**[GAP] LOW — Mip-map generation not specified for the raw-GL upload path**
`TextureCache::loadSRGB()` uploads via `glCompressedTexImage2D` but the spec does not
state whether `glGenerateMipmap` is called or whether the DDS file is expected to
contain all mip levels. For city assets at distance, missing mipmaps will produce
aliasing. The spec should either mandate full mip chains in DDS files or specify
`glGenerateMipmap` after upload.

---

### 1.5 Shader Loading (`shader-loading.md`)

**[PROBLEM] HIGH — "Failure path safety: cb is destroyed on matType==-1"**
The spec states that when `addHighLevelShaderMaterialFromFiles` returns -1, the
callback `cb` must not be accessed after `drop()`. However, the spec does not address
the case where the failure occurs mid-frame when other systems may already hold a
pointer to the callback (e.g. stored in a renderer member). If `drop()` deletes the
callback and a renderer member still holds the raw pointer, a use-after-free results.
The spec should mandate that all renderer members pointing to a callback are nulled
immediately on failure before calling `drop()`.

**[GAP] MEDIUM — Save/restore GL_ACTIVE_TEXTURE: no spec for what unit is restored**
The spec mandates save/restore of `GL_ACTIVE_TEXTURE` inside `OnSetConstants()` to
prevent Irrlicht state corruption, but does not specify which unit Irrlicht expects to
be active after `OnSetConstants()` returns. If Irrlicht assumes GL_TEXTURE0 is active
but the shader callback restore-saves a non-0 unit, subsequent Irrlicht texture binds
may go to the wrong unit. The spec should state: "restore to `GL_TEXTURE0` after
terrain splat shader setup, regardless of which unit was active on entry."

**[GAP] LOW — ui_quad raw GL path: shader compile error handling not specified**
The `ui_quad` shader uses `glCreateShader`/`glCompileShader` directly. The spec
documents the normal path but not what happens if shader compilation fails (e.g.
driver version incompatibility). The error path should at minimum log the GLSL
info log and fall back to a solid-color material.

---

### 1.6 Sky and Clouds (`sky-clouds.md`)

**[PROBLEM] HIGH — kCloudAltitude = -1000m is below ground for all terrain**
The spec places the cloud plane at Y = -1000 m (kCloudAltitude). With the terrain
starting near Y = 0, this means the cloud plane renders below all terrain. The spec
says the camera is above the dome looking down at pitch [-70°, -20°], which means
the cloud plane at -1000 m would appear at the horizon or below it. The rationale
for a negative kCloudAltitude must be explained; if this is intentional (placing clouds
below the horizon as atmospheric haze), it should be called out explicitly. If this is
a typo and the intended value is +1000 m, it is a CRITICAL bug in the spec.

**[GAP] MEDIUM — CloudDomeShaderCallback keeps its own reference via `void* m_cloudShaderCbRaw`**
The spec documents this pattern to avoid the raw pointer being deleted while in use, but
does not specify when this self-reference is released. If the renderer is destroyed
while the callback is still alive (e.g. due to a refcount bug), memory leaks. The
shutdown sequence for the cloud dome callback should explicitly state that
`m_cloudShaderCbRaw->drop()` is called in the renderer's destructor after the scene
manager is cleaned up.

**[INCONSISTENCY] MEDIUM — farClip must be 15000m but irrlicht-device-lifecycle.md
does not mention this**
`sky-clouds.md` requires `farClip >= 15000 m` to prevent hard-clipping of dome
vertices. The Irrlicht device lifecycle spec does not list camera farClip as a
construction parameter. If the farClip is set in `IrrlichtRenderer::init()` and later
overwritten by any camera-construction code that uses a default farClip, the cloud dome
will clip. The device lifecycle spec should reference the farClip requirement or mandate
that farClip is set in the same step that creates the camera scene node.

---

### 1.7 Benchmark Tool (`benchmark-tool.md`)

**[GAP] LOW — Second ISceneManager lifetime not specified**
The benchmark tool creates a second `ISceneManager*` via `smgr->createNewSceneManager(false)`.
The spec does not state when this scene manager is dropped. If the benchmark tool exits
before calling `drop()`, Irrlicht will leak the scene manager and all its nodes. The spec
should mandate `drop()` on the second scene manager in the benchmark tool's destructor.

---

### 1.8 Model Validator Tool (`model-validator-tool.md`)

**[GAP] LOW — Road tiles under Vehicles category**
The spec states road tiles use "the identical code path as `IrrlichtRenderer`" in the
model validator. However, `3d-model-standards.md` now specifies that road tile geometry
is procedurally generated at runtime (no `.b3d` file). The model validator must
therefore also construct road tile meshes procedurally rather than loading from disk.
The spec should note this distinction and confirm the validator calls the same
`buildTileRoadMesh()` function.

---

## 2. Audio Architecture

### 2.1 AudioSystem (`audio-system.md`)

**[GAP] HIGH — `setGameOverState()` is specified as a V1 no-op with LOG_WARNING**
but no test verifies this no-op contract. If a future implementer accidentally
makes `setGameOverState()` functional (e.g. modifying music state), existing tests
will not catch the regression. A test `AudioSystem_SetGameOverState_IsNoOp` should
be mandated in the testing spec.

**[GAP] MEDIUM — IAudioSystem interface evolution tracking**
The spec notes the interface evolved across Phase 7 → Phase 10 → Phase 11d → Phase 11m.
There is no single authoritative method count or interface version marker. If a test
mock or a future platform backend is built from an older spec snapshot, it will silently
miss methods. A comment block in `IAudioSystem.h` with a method count assertion (similar
to the 21-method comment in `IUIBackend`) should be mandated.

**[GAP] MEDIUM — transitionToGameplay / transitionToMainMenu symmetry not fully specified**
The spec lists both methods as Phase 11m additions but does not document their full
contracts (e.g., does `transitionToMainMenu()` stop all vehicle engine pairs? does it
reset the stems? does it wait for crossfade completion?). This is an implementation
contract gap that will cause divergent behavior across implementations.

---

### 2.2 Audio Thread Shutdown (`audio-thread-shutdown.md`)

**[PROBLEM] HIGH — Step 3.5 requires rebinding the context to the main thread after join**
The spec requires `alcMakeContextCurrent(m_context)` on the main thread after the audio
thread joins, before AL object cleanup. However, `alcMakeContextCurrent` is a
process-wide operation (not thread-local for the main context), and the spec also
mandates `alcSetThreadContext` for the audio thread. If both are in use simultaneously
during teardown, the order dependency is fragile. The spec should clarify: does the
audio thread call `alcSetThreadContext(nullptr)` before it exits, and does the main
thread then call `alcMakeContextCurrent(m_context)` to re-establish ownership? The
current wording implies both but does not sequence them explicitly.

**[INCONSISTENCY] MEDIUM — kEvictableSFXCount (55) as shutdown loop guard vs.
kSFXPoolSize (58)**
The spec uses `kEvictableSFXCount = 55` as the loop bound for EFX filter cleanup
because stingers and reserved sources do not have EFX filters. This is correct, but
`source-pool.md` defines the pool boundaries differently. The spec should add a
cross-reference: "kEvictableSFXCount = 55 = kTotalSources(62) − kStingerCount(2) −
kReservedCount(1) − kStreamSourceCount(4)" to prevent the constant from diverging
from the pool layout spec.

---

### 2.3 Error Checking (`error-checking.md`)

**[GAP] LOW — `alcCheckError` uses `void*` parameter to avoid AL/alc.h in header,
but the production call sites cast back to `ALCdevice*`**
The `void*` signature means type-checking is lost at the call site. If a caller
accidentally passes a non-device pointer (e.g. a `void*` handle to a different object),
the resulting `alcGetError()` call will operate on garbage. The spec should mandate a
comment at every call site: `// alcCheckError(device, ...)` with the actual type, and
optionally a `static_assert(sizeof(ALCdevice*) == sizeof(void*))` guard.

---

### 2.4 HRTF Initialization (`hrtf-initialization.md`)

**[GAP] MEDIUM — `default.mhr` copy via POST_BUILD: no fallback if file is absent**
The spec mandates a CMake POST_BUILD rule to copy `default.mhr` to the build output
directory. If the file is absent (e.g. a clean checkout with missing LFS content or
a vcpkg version that no longer ships HRTF data), the HRTF init will silently fall back
to stereo panning without warning. The spec should mandate a `file(COPY ... DESTINATION
... RESULT_VARIABLE rv)` check in CMakeLists.txt and a build error if the file is
missing.

**[INCONSISTENCY] MEDIUM — `OpenAL32.dll` renamed to `soft_oal.dll` via POST_BUILD**
The spec says this is a "hard-fail" in Phase 7. However, `ci-cd/github-actions-workflow.md`
does not list a verification step that confirms `soft_oal.dll` is present in the Windows
build artifacts before running tests. If the POST_BUILD rename step is skipped or fails
silently (e.g. `file(RENAME ...)` with no error check), Windows CI tests will fail
with a cryptic DLL-load error rather than a meaningful build error.

---

### 2.5 Source Pool (`source-pool.md`)

**[GAP] HIGH — kMaxVehiclePairs = 12 (24 pool slots / 2 per vehicle) is derived
from kEvictableSFXCount = 55, but the vehicle pair slots are carved out of the
evictable SFX pool**
The spec states vehicle engine sources are `sources[kVehiclePairStart..kVehiclePairEnd]`
within the evictable SFX range but does not define `kVehiclePairStart` or `kVehiclePairEnd`
as named constants. These values will be hardcoded implicitly. The spec should define
them as `constexpr` values in `audio_constants.h` with a static_assert confirming
`kVehiclePairEnd − kVehiclePairStart + 1 == kMaxVehiclePairs * 2`.

**[INCONSISTENCY] LOW — Post-V1 4-step GAME_OVER stinger promotion sequence is
documented in `source-pool.md` but the stinger types spec in `audio-system.md`
does not cross-reference this post-V1 behavior**
Future implementers extending the stinger system may not find the promotion sequence
spec. A cross-reference should be added.

---

### 2.6 Streaming Architecture (`streaming-architecture.md`)

**[PROBLEM] HIGH — Pattern A (SPSC tryPop) vs Pattern B (std::vector push_back): must
pick one**
The spec explicitly states "must pick one" for the pre-load queue drain pattern but
leaves the choice open. This is an unresolved design decision that will produce
inconsistent implementations if two developers implement different audio subsystems.
The decision must be made and the spec updated before implementation.

**[GAP] MEDIUM — `alGetError()` before `alBufferData` to clear stale errors**
The spec correctly mandates this, but does not state what action to take if the
pre-clear reveals a genuinely stale error (i.e. a non-AL_NO_ERROR state before
`alBufferData` is called). The spec should clarify: "log the stale error via
`alCheckError()` before clearing, then proceed; do not abort the buffer upload on
stale errors since they reflect prior frames."

**[GAP] MEDIUM — kSamplesPerBuffer = 16384 frames and buffer count**
The spec defines the per-buffer frame count but does not state how many buffers are
allocated per stream source. The circular buffer queue depth (typically 3–4 buffers
for OGG streaming) is not specified. Without this, the decode loop's queue-depth
invariants are implementation-defined.

---

### 2.7 Spatial Audio (`spatial-audio.md`)

**[GAP] MEDIUM — Z-negation for AL_ORIENTATION is specified but only for the
`syncListenerToCamera` path. The spec does not state whether positional SFX sources
also require Z-negation when setting `AL_POSITION`**
If `playPositionalSound()` receives an Irrlicht world-space position `(x, y, z)` and
sets `alSource3f(source, AL_POSITION, x, y, z)` without negating Z, all positional
audio will be spatially mirrored on the Z axis. The spec should explicitly state:
"all AL_POSITION calls on SFX sources must also negate Z to match the listener
coordinate convention."

**[PROBLEM] MEDIUM — `alcMakeContextCurrent` process-wide binding "permanent at runtime"**
The spec states the process-wide context binding is never cleared while
`syncListenerToCamera` is active. However, if the audio system is destroyed while a
modal dialog is on screen (e.g. the game-over path), `syncListenerToCamera` may no
longer be called. The spec should clarify: "after `AudioSystem::shutdown()` is called,
`syncListenerToCamera` must not be called; the renderer must null the audio system
pointer before entering the shutdown path."

---

### 2.8 Audio Occlusion (`audio-occlusion.md`)

**[GAP] MEDIUM — Mid-loop EFX filter allocation failure: "must delete the partially-
allocated filter immediately"**
The spec mandates immediate cleanup on partial allocation failure but does not specify
the AL call sequence: should `alDeleteFilters(1, &filter)` be called even if
`alGenFilters` returned successfully but `alFilterf` failed? The spec should show the
exact cleanup sequence.

**[INCONSISTENCY] LOW — `m_efxAllocationAttempted` vs `m_efxAvailable` in shutdown vs.
occlusion paths**
`audio-thread-shutdown.md` uses `m_efxAllocationAttempted` as the shutdown loop guard.
`audio-occlusion.md` uses `m_efxAvailable` as the guard for applying filter values.
These are documented as two separate booleans with distinct roles, which is correct.
However, neither file cross-references the other, making it easy for an implementer
to conflate them. Both files should add: "See `audio-occlusion.md`/`audio-thread-shutdown.md`
for the complementary boolean."

---

### 2.9 Dynamic Soundscape (`dynamic-soundscape.md`)

**[PROBLEM] HIGH — `m_lastDuckWakeTime` must be initialized BEFORE `notify_one()`**
The spec explicitly states this constraint. However, there is no corresponding test
that verifies initialization order — the constraint is impossible to test without
inspecting thread interleaving. At minimum, a code-review checklist item should
be added to the spec: "Reviewer must verify `m_lastDuckWakeTime` initialization
appears before `m_initCV.notify_one()` in the audio thread startup sequence."

**[GAP] MEDIUM — In V1 DUCKED state, only sources[55] and sources[56] are checked
(NOT sources[57])**
The spec states this exclusion but does not explain why sources[57] (the reserved
source) is excluded. The rationale (sources[57] is a reserved non-stinger source that
should not be ducked) should be documented to prevent a future implementer from
"fixing" this apparent off-by-one.

**[GAP] LOW — Stinger loudness target -18 LUFS / -1 dBTP**
This loudness target is specified in `dynamic-soundscape.md` but is not cross-referenced
in `v1-audio-asset-manifest.md`. The artist spec and the technical spec may diverge
if one is updated without the other.

---

### 2.10 V1 Audio Asset Manifest (`v1-audio-asset-manifest.md`)

**[INCONSISTENCY] MEDIUM — Vehicle engine minimum duration: spec says ≥ 6 s,
but `audio-asset-formats.md` tier boundary is "5–19.999 s for OGG pre-loaded"**
If a vehicle engine OGG is exactly 5.0 s, it falls in the "5–19.999 s" pre-load
tier per `audio-asset-formats.md` but violates the "≥ 6 s" minimum in the manifest.
The manifest should be the binding contract; `audio-asset-formats.md` should note
the vehicle engine exception: "Vehicle engine loops: minimum 6 s per `v1-audio-asset-manifest.md`."

**[INCONSISTENCY] LOW — `sfx_vehicle_horn` priority: manifest says HIGH priority;
`source-pool.md` does not call out horn as a HIGH-priority sound in its pool pressure
examples**
This is a minor documentation inconsistency. `source-pool.md` should add sfx_vehicle_horn
to its HIGH-priority example list.

---

## 3. Testing Architecture

### 3.1 Framework (`testing/framework.md`)

**[GAP] MEDIUM — `aitown_add_tests()` macro is documented as the canonical helper
but its implementation in `AitownTestHelpers.cmake` is not cross-referenced in this file**
A developer reading `framework.md` must know to look in `AitownTestHelpers.cmake` for
the macro definition. This should be an explicit cross-reference: "Defined in
`cmake/AitownTestHelpers.cmake`; see also `ci-cd/dependency-management.md` for the
vcpkg discovery mode requirement."

**[GAP] MEDIUM — terrain_tests TIMEOUT 300 / DISCOVERY_TIMEOUT 60 are specified
but no rationale is given**
Without a rationale, future maintainers may reduce these timeouts to match other
test targets, causing intermittent CI failures on slow machines. The spec should
note: "300 s timeout is required because terrain mesh rebuild tests iterate over
large chunk arrays at multiple LOD levels and may take 200+ s on CI-class hardware."

---

### 3.2 Coverage (`testing/coverage.md`)

**[PROBLEM] HIGH — lcov `--ignore-errors mismatch,inconsistent,version` is specified
as a comma-separated single flag, but CLAUDE.md says only `mismatch,inconsistent`
(not `version`)**
`coverage.md` lists three error codes; `CLAUDE.md` (Notes for AI Assistants) lists
only two. If the `version` suppressor is needed (GCC/gcov version-string mismatch)
and is missing from the CLAUDE.md guidance, any agent or developer following CLAUDE.md
for local builds will see spurious failures. Either CLAUDE.md must be updated to
add `version`, or `coverage.md` should be the sole authority and CLAUDE.md should
reference it rather than repeating the flag.

**[GAP] MEDIUM — No spec for how coverage_filtered.info is verified to be non-empty**
After `lcov --remove`, if all paths are excluded by mistake (e.g. a broken glob
pattern), `coverage_filtered.info` will be empty and `lcov --summary` will report 0%
coverage, which the awk gate will fail. The CI spec should add a step that checks the
filtered info contains at least one source file before running the coverage gate.

**[GAP] LOW — Four exclusion prefixes (`mock_*`, `manual_*`, `Mock*`, `Manual*`) but
no exclusion for auto-generated files from vcpkg**
vcpkg-managed headers may be included inline in test builds via template instantiation,
adding them to the coverage data. The `${BUILD_DIR}/_deps/*` pattern is mentioned but
CLAUDE.md warns it "never exists". The exclusion for vcpkg headers should use
`"*/.fetchcontent_cache/*"` AND `"*/vcpkg_installed/*"` to be safe.

---

### 3.3 Testability Architecture (`testing/testability-architecture.md`)

**[GAP] HIGH — `NotificationManager` constructor signature is
`(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*)` but no test verifies
the constructor succeeds when `IAudioSystem*` is nullptr (for tests that do not
need audio)**
The fourth parameter was added late (Phase 11m). If any existing test constructs
`NotificationManager` with 3 arguments (pre-Phase-11m signature), it will fail to
compile. The spec should explicitly state: "all four parameters are mandatory; nullptr
is NOT a valid value for any parameter in V1 tests."

**[GAP] MEDIUM — `UIManager` 8 integration test cases are enumerated but no test
specifies the teardown order**
The spec says "add `TearDown()` to explicitly reset `sim_` and document
destructor-path contract" but does not specify what "reset" means — whether it is
`sim_.reset()` (for `unique_ptr`) or `sim_ = nullptr` or `sim_.release()`. The
teardown contract must be unambiguous to avoid ASAN failures on test shutdown.

**[INCONSISTENCY] MEDIUM — `MockUIBackend` is described as the test-facing authority
for `IUIBackend` (21 methods), but `hud-layout.md` references `setMouseCursor()` as
deferred to Phase 12 — implying a 22nd method will be added post-V1**
If `setMouseCursor()` is added to `IUIBackend` in Phase 12, the method count increases
to 22. The spec should note: "Phase 12 will add `setMouseCursor()` as method 22; at
that time both `ui-manager.md` and `testability-architecture.md` must be updated
simultaneously." Without this note, the Phase 12 implementer may add the method to
`IrrlichtUIBackend` without updating `MockUIBackend`, breaking test builds.

---

### 3.4 Headless CI Testing (`testing/headless-ci-testing.md`)

**[GAP] MEDIUM — Phase 11b: container mode / xvfb pre-installed noted, but no spec
for what happens if xvfb fails to start**
The CI workflow runs `xvfb-run --auto-servernum` before OpenGL tests. If xvfb fails
to start (e.g. display server conflict, missing DISPLAY socket), the test binary will
receive a DISPLAY that has no OpenGL server and will exit with a connection error.
The spec should mandate a `xvfb-run` health check step before the test step, or
specify that `--auto-servernum` is sufficient.

**[GAP] LOW — No spec for memory or resource limits during headless OpenGL tests**
Headless Irrlicht with a software renderer (or mesa `llvmpipe`) may use significant
RAM. CI runners with 7 GB RAM may OOM-kill the test process. The spec should note
whether `LIBGL_ALWAYS_SOFTWARE=1` or `MESA_GL_VERSION_OVERRIDE` is expected, and
whether a RAM limit exists.

---

### 3.5 Property-Based Tests (`testing/property-based-tests.md`)

**[GAP] MEDIUM — Economy invariant: interest computed on outstanding balance BEFORE
repayment that tick**
The spec defines the order explicitly. However, the RapidCheck property does not
specify how to handle the edge case where `outstanding_balance < repayment_this_tick`
(final repayment tick where the remainder is absorbed). If the test uses generic
`rc::gen::arbitrary<int>()` for the balance, it may generate negative balances that
cause the invariant to fail for the wrong reason. The spec should mandate that the
generator is constrained to positive balances.

**[GAP] LOW — ManualClock `advance(121.0)` before debt cap rc::check**
The spec requires `advance(121.0)` to clear the 120 s grace period gate. However,
it does not state whether `advance()` is cumulative or absolute. If a test calls
`advance(60.0)` earlier in setup and then `advance(121.0)` again, the total may be
181 s, which clears the gate twice. The spec should use absolute timestamps or
document that `advance()` is additive.

---

### 3.6 Procedural Generation Seeds (`testing/procedural-generation-seeds.md`)

**[GAP] MEDIUM — "All generators accept uint64_t seed; log seed on RapidCheck failure"**
The spec does not define which C++ logging function to use for seed logging on failure.
If each generator implements its own logging, the output format will differ, making
seed-reproduction from CI logs difficult. A canonical format
(`SEED: <decimal uint64>`) should be mandated.

---

## 4. CI/CD Architecture

### 4.1 GitHub Actions Workflow (`ci-cd/github-actions-workflow.md`)

**[PROBLEM] HIGH — Supply-chain SHA lint is the first step after checkout in
build-linux, but AITOWN_HEADLESS is set as an env var on the unit and integration
test steps and NOT on the requires-opengl step**
This is correctly documented, but the spec does not include a step that VERIFIES
`AITOWN_HEADLESS` is absent from the requires-opengl step environment. If a developer
copy-pastes the env block from the unit test step, the OpenGL test will silently
run headlessly and produce incorrect coverage. A lint or diff-based check should
be mandated.

**[GAP] HIGH — No spec for handling CI runner out-of-disk-space**
vcpkg builds can consume 5–15 GB of disk space. GitHub-hosted Linux runners have
approximately 14 GB available. If a vcpkg port build fails mid-way due to disk
exhaustion, the error message is a generic CMake failure with no disk-space context.
The workflow spec should add a disk-space check step early in build-linux/build-windows.

**[GAP] MEDIUM — job timeout-minutes: build-linux=30, build-windows=40, coverage-linux=60**
The spec sets these timeouts but does not document how they were derived. If a
dependency is added that increases build time by 10 minutes, the timeout will be
silently violated. The spec should note that these timeouts include vcpkg build time
and should be increased if new vcpkg ports are added.

**[INCONSISTENCY] MEDIUM — AITOWN_HEADLESS=1 is set on the unit and integration test
steps but the spec says `ALSOFT_DRIVERS=null` is also required on both steps**
`ci-cd/github-actions-workflow.md` is the canonical workflow spec, but CLAUDE.md
(Notes for AI Assistants / Windows CI section) says both env vars are needed.
The Linux section should be audited to confirm `ALSOFT_DRIVERS=null` is also set
for the Linux unit/integration test steps, not just Windows.

---

### 4.2 Dependency Management (`ci-cd/dependency-management.md`)

**[GAP] HIGH — `fmt` must be an explicit dependency because openal-soft vcpkg
portfile devendors it**
This is correctly documented. However, there is no automated check that `fmt` appears
in `vcpkg.json` — a vcpkg baseline bump that silently removes `fmt` (if it becomes
a transitive dependency again in a future baseline) would break builds. The supply-chain
lint step should add a check: "confirm `fmt` is in `vcpkg.json` features list."

**[GAP] MEDIUM — `VCPKG_COMMIT_ID` at workflow level (not job level) is a policy
decision that is documented but not enforced**
If a developer adds a new job and accidentally sets `VCPKG_COMMIT_ID` at the job level
(overriding the workflow-level value), the build will use a different vcpkg baseline
for that job. The spec should mandate a supply-chain lint step that checks all job-level
env blocks for `VCPKG_COMMIT_ID` and fails if found.

---

### 4.3 Caching (`ci-cd/caching.md`)

**[GAP] MEDIUM — coverage-linux must use a distinct ccache key (-coverage suffix)**
This is documented, but the spec does not state what happens if the non-coverage cache
is populated but the coverage cache is empty (cold start). The coverage build may
spuriously use the non-coverage ccache hit (if the key structure allows fallback via
`restore-keys`). The spec should document whether `restore-keys` is used for the
ccache step and, if so, whether cross-contamination between coverage and non-coverage
caches is acceptable.

**[GAP] MEDIUM — BOTH build-linux AND coverage-linux need independent compiler-detect steps**
This is documented in CLAUDE.md but not in `caching.md`. `caching.md` should
explicitly state: "the compiler-detect step must be present in BOTH the build-linux
and coverage-linux jobs — do not share a single step output across jobs."

**[INCONSISTENCY] LOW — Compiler cache key includes 4 components but the spec does not
state whether the 4-component key format produces cache invalidation when moving
between GitHub-hosted runners (e.g. ubuntu-22.04 vs ubuntu-24.04)**
If the runner OS version changes, the "OS" component of the key changes, correctly
invalidating the cache. However, the spec should explicitly name the OS component as
`${{ runner.os }}-${{ matrix.os }}` rather than just "OS" to make the implementation
unambiguous.

---

### 4.4 Branch Protection (`ci-cd/branch-protection.md`)

**[PROBLEM] HIGH — `all-checks-pass` must have `if: always()` to prevent skip-on-failure**
This is documented but is a common implementation mistake. The spec should include
a YAML snippet showing the exact `if: always()` placement, not just a prose description.
Without the snippet, developers may place `if: always()` at the wrong level
(e.g., on a step within the job rather than on the job itself).

**[GAP] LOW — Protection on both `main` and `develop` is documented but the spec does
not state whether `force-push` is disabled**
GitHub branch protection has a "Restrict force pushes" option that is distinct from
the required-status-checks rule. The spec should explicitly state: "force pushes
are disabled on both `main` and `develop`."

---

## 5. Game Design — Technical Feasibility

### 5.1 Simulation Time (`game-design/simulation-time.md`)

**[GAP] MEDIUM — Frame-loop step 3b `UIManager::update()` and step 3 `CameraController::update()`
are given identical step numbers (3 and 3b) in the canonical 8-step loop**
Steps 3 and 3b appear to be sequential sub-steps. However, the spec does not clarify
whether 3 and 3b must be sequential (no other code between them) or whether they can
be interleaved with other operations. The numbering should be made unambiguous.

**[INCONSISTENCY] MEDIUM — `SaveSystem::update(realDeltaSeconds)` is at step 3c but is
not listed in the 8-step frame loop in `simulation-time.md`**
`save-system.md` references a "step 3c" in the main loop for `SaveSystem::update()`.
`simulation-time.md`'s canonical frame loop only shows 7 distinct steps and does not
include step 3c. The two files are inconsistent on the canonical frame loop
structure.

---

### 5.2 Economy Model / Zoning / Population

**[GAP] MEDIUM — Density unlock thresholds reference `economy-model.md` as the
authoritative source, but `population-density-growth.md` partially reproduces them**
`population-density-growth.md` lists unlock thresholds with the caveat "Economy Model
is the authoritative source." If the two files diverge (e.g. during a rebalance),
the implementation will use one and tests will verify the other. The reproduction in
`population-density-growth.md` should either be removed or replaced with an explicit
XREF-only note.

**[MISSING] MEDIUM — No spec for `getDemandPressurePct()` return type precision**
`hud-layout.md` notes the return type is `float` in `[0.0, 1.0]` and warns the HUD
must multiply by 100. However, `zoning-system.md` and `simulation-time.md` do not
define the interface method at all. `ICitySimulation` method signatures (beyond what
is in `testability-architecture.md`) are scattered across multiple spec files with
no single master list.

---

### 5.3 Terrain Interaction (`game-design/terrain-interaction.md`)

**[GAP] MEDIUM — Phase 10b terrain mesh modification: `setTileHeight()` is called for
all 4 corners but neighbour chunk boundaries are not addressed**
When a tile at the edge of a chunk has its corners flattened, the adjacent chunk's
heightmap is not updated. The spec describes the flattening of all 4 corners of the
placed tile, but a corner vertex is shared between up to 4 chunks. Not rebuilding
adjacent chunks will produce visible seams at chunk boundaries after earthworks.
`procedural-terrain.md` addresses the neighbour sync for the LOD rebuild deque but
does not specifically mandate it for the earthworks path.

---

### 5.4 Game Over Flow (`game-design/game-over-flow.md`)

**[GAP] MEDIUM — "Load Last Save" from the game-over modal uses the loading-screen
path, but the spec does not specify what happens if no save exists**
`save-system.md` states the Load Last Save button is grayed out with a tooltip
if no save exists. However, `game-over-flow.md` describes the transition path without
addressing the no-save case. If the game-over modal's "Load Last Save" button is
activated with no save files (e.g. the player never saved), the spec's behavior is
undefined.

---

### 5.5 Save System (`game-design/save-system.md`)

**[GAP] MEDIUM — `ISaveSystem::loadMostRecentSave()` returns a `LoadResult` enum,
but the enum is not defined in `save-system.md`**
The spec defines `LoadResult::Ok`, `LoadResult::NoSaveFound`, and
`LoadResult::Corrupted` inline in the spec. However, the canonical location for this
enum (simulation_types.h? save_system_types.h?) is not specified. Without a canonical
header location, implementations will put the enum in different places, causing ODR
issues.

**[GAP] LOW — `building_variant_counters` array: index formula is `zone * 3 + tier`**
The spec states the array has exactly 9 elements. However, it does not specify the
`zone` and `tier` enum values or their ordering. If `ZoneType::Residential = 0`,
`Commercial = 1`, `Industrial = 2` and `DensityTier::Low = 0`, `Medium = 1`, `High = 2`,
the formula works. But if the enum values change or are non-contiguous, the formula
breaks. The spec should mandate that the enum values are 0/1/2 contiguous and add
a static_assert.

---

## 6. UI/UX — Technical Feasibility

### 6.1 Input Arbitration (`ui-ux/input-arbitration.md`)

**[PROBLEM] HIGH — Priority 2 dual-guard (criticalVisible && !modalActive) is extensively
documented but the spec does not mandate a unit test for the same-frame race
condition (modal + CRITICAL toast activate on the same tick)**
`testability-architecture.md` includes 8 UIManager integration tests but none of
them covers the same-frame modal + CRITICAL toast race. Without a test, the dual-guard
may be silently removed by a refactor. A test `UIManager_SameFrameModalAndCriticalToast_ModalTakesPrecedence`
should be mandated.

**[GAP] MEDIUM — CameraController receives `EMIE_RMOUSE_LEFT_UP` unconditionally,
even after UIManager consumes the event**
The spec explicitly states this requirement to prevent the drag flag from getting
stuck. However, `EventReceiver` is not described in detail in any spec file — the
contract is stated in `input-arbitration.md` but the implementation location
(`src/platform/EventReceiver.cpp`) is not referenced. A developer may implement
the UIManager Ctrl+Z handler without knowing that `CameraController` must also
receive RMB up. A reference to `EventReceiver.cpp` should be added.

**[GAP] MEDIUM — `WindowFocusGained`/`WindowFocusLost` must NOT be consumed by
UIManager (any priority), but there is no test for this**
The spec mandates these events pass through unconditionally. Without a test that
verifies UIManager returns `false` for focus events, a refactor that accidentally
consumes them will break edge-scroll suppression on Alt+Tab silently.

---

### 6.2 HUD Layout (`ui-ux/hud-layout.md`)

**[INCONSISTENCY] HIGH — `kToolbarBottom = 784` is the input gate, but
`input-arbitration.md` Priority 3 toolbar carve-out also states `virtual x: 8–72 px,
y: 64–784 px`**
Both files agree on the value, but `hud-layout.md` explicitly warns "DO NOT use y:600
as an input gate threshold" while `input-arbitration.md` lists the correct 784 value
in its dispatch table. The consistency is good, but `hud-layout.md` should add a
cross-reference: "See `input-arbitration.md` Priority 5 toolbar dispatch table for
the enforcement point — both files must agree on `kToolbarBottom = 784`."

**[GAP] MEDIUM — Demand pressure bar inverse semantics warning**
`hud-layout.md` warns: "Do NOT use `QueryResult::demandPressurePct` directly to fill
the HUD demand bar — it uses the complementary definition." However,
`query-inspector-panel.md` (not yet reviewed in full but referenced) defines
`demandPressurePct` as `(1.0f − effective_demand_factor) × 100`. This inversion is
a known footgun. A `static_assert` or a type alias (`InverseDemandPct` vs
`DemandFillPct`) should be considered to make the inversion compile-time-visible
rather than runtime-detectable.

---

### 6.3 Notification System (`ui-ux/notification-system.md`)

**[GAP] MEDIUM — CRITICAL toast keyboard navigation: "first CRITICAL toast receives
keyboard focus automatically when it becomes visible"**
The spec does not define how keyboard focus is transferred back to the underlying
HUD after the last CRITICAL toast is dismissed. If focus remains on a now-removed
UI element, subsequent keyboard input may be lost. The spec should state:
"after the last CRITICAL toast is dismissed, keyboard focus returns to the
previously-focused HUD element (or the speed selector if no element was focused)."

**[GAP] LOW — Notification log stores full text of truncated toasts but no spec
for log size limit**
Toasts beyond depth 10 are logged to the notification log. However, the log has no
specified maximum depth. Over a long play session the log may grow unboundedly.
A maximum log depth (e.g. 200 entries) and a FIFO eviction policy should be specified.

---

### 6.4 UIManager (`ui-ux/ui-manager.md`)

**[GAP] MEDIUM — `onGameLoaded()` is referenced in `main-menu-new-game-flow.md` as
a required post-load call but its full contract is not specified in `ui-manager.md`**
`main-menu-new-game-flow.md` states: "The loading controller must call
`UIManager::onGameLoaded()` after deserialization completes and before the first
`UIManager::update()` tick." `ui-manager.md` does not document `onGameLoaded()` in
its method list. The method's contract (what state it resets, which panels it hides/shows)
must be specified.

---

## 7. Asset Standards — Technical Feasibility

### 7.1 3D Model Standards (`asset-standards/3d-model-standards.md`)

**[INCONSISTENCY] HIGH — Road tile mesh is "procedurally generated in C++ at runtime"
but the model validator spec (`model-validator-tool.md`) uses the same code path**
The model validator is defined as a tool that validates authored B3D assets.
If road tiles are code-generated, the validator's road tile test is validating code
behavior, not an asset. The model validator spec should explicitly call out which
asset categories it covers (skipping road tiles) and which are code-generated.

**[GAP] MEDIUM — LOD distance thresholds for road tiles: "same as small buildings/props"**
Road tiles use LOD0→LOD1 at 30 m / 25 m and LOD1→LOD2 at 100 m / 90 m per
the spec text. However, the LOD distance threshold table in the spec does not have a
row for "Road tiles" — they are only mentioned in the narrative. The table should have
an explicit road tile row to prevent confusion with the procedural terrain chunk row
(which uses entirely different distances: 100 m / 300 m).

**[GAP] MEDIUM — `validate_assets.py` must NOT look for road tile `.b3d` files**
This rule is stated but the spec does not define what `validate_assets.py` should do
if it encounters a road tile asset path in a manifest. Should it error? Warn? Skip?
The policy should be explicit.

---

### 7.2 2D Texture Standards (`asset-standards/2d-texture-standards.md`)

**[PROBLEM] HIGH — `GL_EXT_texture_sRGB` extension check at `RenderSystem::init()`:
`glewIsExtensionSupported` is called after `createDevice()` returns, but the spec does
not state whether GLEW is initialized before this call**
`glewInit()` must be called after an OpenGL context is created and made current.
The spec notes the check happens "after `createDevice()`", but GLEW initialization
is not mentioned in the Irrlicht device lifecycle spec. If GLEW is not initialized
before the extension check, `glewIsExtensionSupported` will return `GL_FALSE` (or
crash) even on a capable GPU. The lifecycle spec should mandate:
"call `glewInit()` immediately after `createDevice()` and before any `glew*` calls."

**[GAP] MEDIUM — `GL_MAX_TEXTURE_SIZE` initialized to 2048 under EDT_NULL**
The EDT_NULL fallback of 2048 is reasonable for most textures but may be too small
for the 1024×1024 building atlas. If any code uses `m_maxTextureSize` to gate atlas
dimension selection, and tests run under EDT_NULL, the atlas may be sized to 2048
rather than the actual asset dimension. Tests that verify atlas layout must either
mock the `getMaxTextureSize()` call or explicitly set the EDT_NULL fallback to a
value large enough for all test cases.

---

### 7.3 Building Atlas Layout (`asset-standards/building-atlas-layout.md`)

(Not read in full — only referenced indirectly. Flagging for completeness.)

**[MISSING] LOW — Atlas layout spec cross-reference in `texture-cache.md`**
`texture-cache.md` mentions atlas mip chains and DDS uploads but does not
cross-reference `building-atlas-layout.md`. A developer implementing `TextureCache`
may not know the atlas layout constraints (clamped at 4 mip levels) apply
specifically to the building atlas. A cross-reference should be added.

---

## 8. Cross-Domain Issues

**[INCONSISTENCY] HIGH — IClock injection is specified for AudioSystem, CitySimulation,
UndoSystem/HUD, and SaveSystem, but the canonical IClock definition (header location,
methods) is only specified in `testability-architecture.md`**
Four different subsystems depend on `IClock`, but the interface is only formally
defined in the testing spec. The interface definition should live in
`src/interfaces/IClock.h` with a cross-reference from every spec that uses it.
Currently a developer reading `economy-model.md` or `save-system.md` has no pointer
to the IClock contract.

**[INCONSISTENCY] HIGH — ISimulationRNG injection is required in `CitySimulation`
constructor but the interface definition is only in CLAUDE.md (Notes for AI Assistants)**
`service-coverage.md` references `ISimulationRNG::nextFloat()` but neither the
interface header location (`src/interfaces/ISimulationRNG.h`?) nor its method list
is documented in any architecture spec file. The interface must have its own spec
entry in `architecture/testing/testability-architecture.md` or a new file under
`src/interfaces/`.

**[INCONSISTENCY] HIGH — Frame loop canonical definition exists in both
`simulation-time.md` (8 steps, step-2 CitySimulation::tick) and
`irrlicht-device-lifecycle.md` (11-step render loop starting with beginScene)**
These are two partially overlapping descriptions of the same per-frame loop.
`simulation-time.md` covers the pre-render simulation steps (1–4b + SaveSystem at 3c);
`irrlicht-device-lifecycle.md` covers the render steps (beginScene through drawAll).
Neither file cross-references the other to establish the unified 8-step frame loop.
A single canonical frame loop should be defined in one place (e.g.,
`irrlicht-device-lifecycle.md`) with cross-references from all other specs.

**[GAP] HIGH — `ICitySimulation` interface has no canonical method list spec file**
`ICitySimulation` methods are referenced across at least 8 spec files
(`testability-architecture.md`, `game-over-flow.md`, `zoning-system.md`,
`save-system.md`, `service-coverage.md`, `hud-layout.md`, `traffic-system.md`,
`simulation-time.md`). There is no single file that lists all required methods.
An `ICitySimulation` interface spec (or at minimum a method count comment in the
header similar to `IUIBackend`'s "21 methods" contract) is needed to prevent
implementations from missing methods that are only mentioned in distant spec files.

**[GAP] HIGH — `IRenderer` interface has no canonical method list spec file**
Similar to `ICitySimulation`, `IRenderer` methods are referenced across:
`testability-architecture.md`, `traffic-system.md` (getListenerPosition()),
`hud-layout.md` (setZoneOverlay, setTileHoverHighlight, setTilePlacementPreview,
getTileScreenBounds), `input-arbitration.md` (pickTerrainTile). There is no spec
file that enumerates all `IRenderer` pure-virtual methods with their signatures.
`src/interfaces/IRenderer.h` must be the authoritative source, but the spec files
should cross-reference it.

**[PROBLEM] HIGH — The earthworks path in Phase 10b (`terrain-interaction.md`)
calls `setTileHeight()` on all 4 corners of a placed tile but does NOT address the
chunk-boundary seam problem**
A tile at chunk boundary (e.g. tileX = 63, tileZ = 0 on a 64-tile-wide chunk) has
its northeast corner shared with the adjacent chunk. Calling `setTileHeight` on that
corner updates the shared heightmap, but the adjacent chunk's mesh is not queued for
rebuild. The `procedural-terrain.md` LOD rebuild spec covers general heightmap changes
but does not specifically mandate neighbour-chunk invalidation for the earthworks path.
The Phase 10b spec in `terrain-interaction.md` should explicitly state:
"for each corner vertex modified by `setTileHeight()`, all chunks that share that
vertex must be queued for rebuild."

**[INCONSISTENCY] MEDIUM — `vec3` type: used throughout audio and simulation specs as
`vec3{x, y, z}` but the concrete type (`irr::core::vector3df` alias? custom struct?)
is not specified in any architecture file**
`traffic-system.md`, `audio-system.md`, `spatial-audio.md`, and `terrain-interaction.md`
all use `vec3` as if it is a well-known type. If it is an alias for
`irr::core::vector3df`, including it in `IAudioSystem.h` (which must not include
Irrlicht headers per the architecture) creates a dependency problem. The `vec3`
type alias must be defined in a shared header (`src/interfaces/vec3.h` or similar)
that does NOT include any Irrlicht headers.

**[INCONSISTENCY] MEDIUM — `ServiceBuildingType` enum is defined in `simulation_types.h`
per `service-coverage.md`, but `ZoneType`, `DensityTier`, `SpeedMultiplier`, and
`CityRatingTier` are also described as living in `simulation_types.h` or
`src/interfaces/simulation_types.h` (referenced across multiple specs)**
The spec does not consistently name the header — some files say `simulation_types.h`
and some say `src/interfaces/simulation_types.h`. The fully qualified path must be
uniform across all spec files to prevent header-include divergence.

**[GAP] MEDIUM — No spec for how `GameMode` enum (Sandbox/Scenario) is communicated
from UIManager to CitySimulation**
`game-over-flow.md` explicitly states `CitySimulation` must NOT reference `GameMode`.
`UIManager` checks `GameMode` before calling `transitionToGameOver()`. But the spec
does not define where `GameMode` is stored, how it is set at game start, or which
header it lives in. The `main-menu-new-game-flow.md` spec passes `GameMode::Sandbox`
to `transitionToGameplay()` but this method is not defined in any interface spec.

**[GAP] MEDIUM — `ISaveSystem` interface is referenced but never formally spec'd**
`save-system.md` describes `ISaveSystem::loadMostRecentSave()` and `saveToSlot()` but
there is no spec file that enumerates all `ISaveSystem` pure-virtual methods, their
signatures, or the header location. Like `ICitySimulation`, this interface needs a
canonical method list.

**[GAP] LOW — No spec for the `vec3 distance()` free function used in
`traffic-system.md` (signal cull)**
The 80 m pre-acquisition cull calls `distance(listenerPos, signalPos)`. This free
function must be defined somewhere, but no spec file specifies where. If it is the
Irrlicht `irr::core::vector3df::getDistanceTo()` method, then `CitySimulation` would
need to include Irrlicht headers — which conflicts with the testability architecture
that keeps simulation logic Irrlicht-free. A `distance()` free function taking two
`vec3` arguments must be defined in the `vec3` header or a `simulation_math.h` header
that is Irrlicht-free.

---

*End of AI Town Technical Squad Architecture Review*
