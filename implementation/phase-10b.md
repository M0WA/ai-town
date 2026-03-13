## Phase 10b: Terrain Flattening & Sky Clouds

### Goal

Deliver two visual enhancements that make placement feedback and skybox believable: terrain
flattening writes the placed tile's height (and blended neighbour heights) back to the
persistent LOD0 heightmap and rebuilds affected chunks; a scrolling cloud plane adds a
lightweight animated cloud layer above the sky dome.

### Naming Conventions

All new files introduced in this phase MUST follow project naming conventions:

- **CamelCase** for C++ class files: `.cpp` and `.h` files that define a class use CamelCase
  (e.g., `TerrainSystem.cpp`, `IrrlichtRenderer.h`).
- **`I`-prefix for interfaces**: pure-virtual interface headers live under `src/interfaces/`
  and are prefixed with `I` (e.g., `src/interfaces/ITerrainQuery.h`,
  `src/interfaces/IRenderer.h`).
- **`snake_case` for C-style non-class headers**: headers containing only constants, enums,
  or POD structs (no class definition) use `snake_case`
  (e.g., `shader_constants.h`, `terrain_types.h`).

### Deliverables

#### Feature 1: Terrain Flattening on Placement

##### graphics-dev-irrlicht

- [x] Add `setTileHeight(int tileX, int tileZ, float height)` as a pure-virtual method to
  `src/interfaces/ITerrainQuery.h`. Method sets the persistent LOD0 heightmap value at
  `(tileX, tileZ)` to `height`, enqueues `ChunkRebuildRequest`s for all affected chunks,
  then triggers neighbour blending (see below). Returns immediately; chunk rebuilds are
  processed by `TerrainSystem::update()` at the 2-per-frame cap, or synchronously during
  `flushPendingRebuilds()`. Add with a documentation comment consistent with the existing
  `getHeightAt()` style in `ITerrainQuery.h`:

  ```cpp
  // Sets the persistent LOD0 heightmap height at (tileX, tileZ) to height,
  // applies weighted neighbour blending to the 8 surrounding tiles, and enqueues
  // ChunkRebuildRequests for all affected chunks.
  // Out-of-bounds coordinates are silently ignored.
  virtual void setTileHeight(int tileX, int tileZ, float height) = 0;
  ```

  (ref: `architecture/game-design/terrain-interaction.md`,
  `architecture/graphics-architecture/procedural-terrain.md`)
- [x] Implement `TerrainSystem::setTileHeight(int tileX, int tileZ, float height)`:
  - Write `height` into the persistent LOD0 heightmap array at `(tileX, tileZ)`.
  - Apply neighbour blending: for each of the 8 surrounding tiles, lerp the neighbour's
    current height toward `height` using a falloff factor. Cardinal neighbours (N/S/E/W)
    use the factor confirmed by `gamedesign-lookandfeel` sign-off deliverable; diagonal
    neighbours (NE/NW/SE/SW) use the diagonal factor confirmed by the same sign-off (see
    Risks & Spikes — blending values are design-owner decisions, not engineering defaults).
    All neighbour coordinates are clamped to `[0, mapTilesX-1]` × `[0, mapTilesZ-1]`
    before any heightmap write. Out-of-bounds neighbours are silently skipped.
  - After writing the centre tile and all in-bounds neighbours, enqueue
    `ChunkRebuildRequest` for every chunk that contains at least one modified tile. Chunk
    deduplication is already handled by `TerrainSystem::update()`'s
    `processedThisFrame` set.
  - (ref: `architecture/graphics-architecture/procedural-terrain.md` — Deque
    deduplication section)
- [x] Add `void setTileHeight(int tileX, int tileZ, float height) override {}` no-op to
  `ManualTerrainQuery` in `tests/simulation/manual_terrain_query.h` so that
  `ManualTerrainQuery` remains a concrete (non-abstract) class against the updated
  `ITerrainQuery`. (`getHeightAt()` override already present from Phase 9b — do NOT
  re-add it.) **Sequencing constraint**: the `setTileHeight()` pure-virtual addition to
  `ITerrainQuery.h` and this no-op override in `ManualTerrainQuery` MUST land in the same
  commit — merging the interface change without the override instantly makes
  `ManualTerrainQuery` abstract, breaking all 17+ simulation unit tests that instantiate
  it. `graphics-dev-irrlicht` is responsible for including the no-op in the same PR;
  `test-dev-cpp` upgrades to the stateful form in a subsequent PR. (ref:
  `architecture/graphics-architecture/procedural-terrain.md` — `ITerrainQuery` interface
  promotion section)
- [x] Update `IrrlichtRenderer::placeBuildingMesh()`, `placeRoadMesh()`, and
  `placeServiceBuildingMesh()` using the canonical three-step pattern from
  `architecture/graphics-architecture/procedural-terrain.md` (Placement Integration section):
  (1) read the pre-flatten height as `preY = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f`;
  (2) flatten with `if (m_terrain) m_terrain->setTileHeight(tileX, tileZ, preY)`;
  (3) read the post-flatten height as `postY = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f`
  and use `postY` as the scene node Y coordinate. The null guard `if (m_terrain)` mirrors the
  existing Phase 9b null-guard pattern already present in these methods. Use `preY` for the
  `setTileHeight()` argument and `postY` for the node position — these are two separate reads,
  not one. (ref: `architecture/graphics-architecture/procedural-terrain.md`
  — MANDATORY building/road/service-building placement pattern)
- [x] Confirm that `sfx_earthworks` continues to play on placement via the existing
  `CitySimulation` callback wired in Phase 10 — no new audio wiring required in this phase.
  (ref: `architecture/game-design/terrain-interaction.md` — Earthworks is treasury-only
  in V1, now extended with visual modification)

##### gamedesign-lookandfeel

- [x] **Sign-off: neighbour blending falloff factors.** Approved values (reviewed and
  signed off 2026-03-13): **cardinal neighbours (N/S/E/W) = 0.5**, **diagonal neighbours
  (NE/NW/SE/SW) = 0.25**. Blending formula:
  `new_height = lerp(neighbour_current_height, placed_tile_height, factor)`.
  Rationale: cardinal 0.5 produces a noticeable but not extreme slope, giving responsive
  SimCity-style feedback; diagonal 0.25 (half of cardinal) maintains a smooth spatial
  gradient — diagonal distance is √2 × cardinal distance, so a weaker influence is
  geometrically correct. Edge-tile placements produce asymmetric blending (out-of-bounds
  neighbours are skipped); the resulting edge-side cliff is expected V1 behaviour.
  **Design intent**: locking the placed tile to its own pre-flattened height gives
  immediate visual confirmation that the player's action has taken effect, while neighbour
  blending prevents hard seams. This is preferable to averaging all 9 tiles, which would
  shift the placed tile height unpredictably on sloped terrain. (ref:
  `architecture/game-design/terrain-interaction.md`)

##### test-dev-cpp

- [ ] `TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild`: construct a
  `TerrainSystem` with a `ManualClock`; call `setTileHeight()` on a tile at a known chunk
  boundary; assert `TerrainSystem::pendingRebuildCount()` is at least 1 (and at least 2
  for a tile that straddles a chunk boundary). Use the existing
  `TerrainSystem::pendingRebuildCount()` public test-API — do NOT add a `friend`
  declaration or subclass seam. (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] `TerrainFlattening_NeighborBlend_ClampedToMapBounds`: call `setTileHeight()` on a
  corner tile (e.g. `(0, 0)`); assert no out-of-bounds heightmap write occurs (no crash,
  ASAN clean) and that all four in-bounds cardinal neighbours were blended — verified by
  calling `TerrainSystem::getHeightAt()` on each cardinal neighbour and confirming their
  height moved toward the flattened value (i.e. is strictly between the original height
  and the target `flatY`). Out-of-bounds neighbours (the three tiles that would lie outside
  the map at a corner) must produce no write (heights remain unchanged). (ref:
  `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight`: configure a
  `ManualTerrainQuery` with `m_heightBeforeFlat = 5.0f` and `m_heightAfterFlat = 3.0f`
  so the test is non-vacuous (pre- and post-flatten heights differ). Inject it into
  `CitySimulation` alongside a `NiceMock<MockRenderer>` (pre-Feature-3 path: `tests/simulation/mock_renderer.h`).
  Call the placement method; assert `ManualTerrainQuery::m_flattened == true` (confirming
  `setTileHeight()` was invoked) and `ManualTerrainQuery::getHeightAt()` returns `3.0f`
  post-call. `IRenderer` placement methods carry no Y parameter — height verification
  must go through `ManualTerrainQuery`, not `MockRenderer`. Use `NiceMock<MockAudioSystem>`
  for audio (audio calls are incidental; no `EXPECT_CALL` needed for `playPositionalSound`).
  Use `NiceMock<MockRenderer>` for rendering (the assertion is on terrain state, not
  renderer state; declaring `EXPECT_CALL` for `placeBuildingMesh` would obscure intent).
  This test exercises `CitySimulation`'s placement callback through the `IRenderer*`
  interface — do NOT use a real `IrrlichtRenderer` (`unit` label, no OpenGL context
  required). **This test lives in `tests/simulation/terrain_flattening_sim_test.cpp` and
  belongs to `simulation_tests`** (it instantiates `CitySimulation` which links
  `aitown_sim`; `terrain_tests` links only `aitown_terrain` and cannot link `aitown_sim`
  without introducing a circular dependency). After Feature 3 lands, use `MockRenderer.h`
  and `MockAudioSystem.h` (CamelCase). (ref:
  `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] Enhance `ManualTerrainQuery` in `tests/simulation/manual_terrain_query.h` to be
  stateful for Phase 10b tests: add `m_flattened` bool (default `false`),
  `m_heightBeforeFlat` (default `0.0f`), `m_heightAfterFlat` (default `0.0f`), and
  setter helpers `setHeightBeforeFlattening(float)` / `setHeightAfterFlattening(float)`.
  Override `getHeightAt()` to return `m_flattened ? m_heightAfterFlat : m_heightBeforeFlat`.
  Override `setTileHeight()` to set `m_flattened = true`. **This stateful form supersedes
  the no-op assigned to `graphics-dev-irrlicht`**. **BLOCKED**: this deliverable requires
  the `graphics-dev-irrlicht` Step 1 PR (pure-virtual `setTileHeight()` in `ITerrainQuery.h`
  and no-op `ManualTerrainQuery` override) to be merged first — without the pure-virtual declaration, the `override`
  keyword will not compile. Do NOT open a PR for this item until Step 1 is merged.
  Existing tests relying on `return 0.0f` are unaffected because `m_heightBeforeFlat`
  defaults to `0.0f`.
- [ ] `CloudPlane_Init_CreatesCloudNode`: construct an `IrrlichtRenderer` with a real
  driver (not `EDT_NULL`), call `init()`; assert `m_cloudNode != nullptr` (cloud
  initialisation succeeded). Also verify `EDT_NULL` path: assert `m_cloudNode == nullptr`
  when driver type is `EDT_NULL` (init guard triggered). Label `requires-opengl` and add
  to `opengl_tests` — Irrlicht device creation requires X11 even for `EDT_NULL` on Linux;
  `xvfb-run` provides the X server.
- [ ] Wire all new test cases into the appropriate test targets in `CMakeLists.txt`: the
  first two terrain flattening tests (`TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild`
  and `TerrainFlattening_NeighborBlend_ClampedToMapBounds`) live in
  `tests/terrain/terrain_flattening_test.cpp` → add via `target_sources(terrain_tests ...)`
  (label `unit`). The third test (`TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight`)
  lives in `tests/simulation/terrain_flattening_sim_test.cpp` → add via
  `target_sources(simulation_tests ...)` (label `unit`) — it requires `aitown_sim` which
  `terrain_tests` does not link. `CloudPlane_Init_CreatesCloudNode`
  lives in `tests/rendering/cloud_plane_test.cpp` → add **inline** to the
  `add_executable(opengl_tests ...)` call in `CMakeLists.txt`. **Do NOT use
  `target_sources(opengl_tests ...)` for the cloud plane test** — `opengl_tests` prohibits
  `target_sources()` (all sources must be listed inline in `add_executable` to prevent
  ctest discovery timing issues; see `architecture/testing/framework.md`).

---

#### Feature 2: Sky Clouds

##### graphics-artist-2d-texture

- [ ] Author `assets/textures/sky/clouds.png`: seamless tileable cloud pattern,
  1024×1024 RGBA (R=G=B=grey-white luminance; A=cloud density mask 0–255), authored for
  UV tiling (no hard edges at boundaries). Deliver as PNG (not DDS) — see rationale in
  Risks & Spikes. (ref: `architecture/asset-standards/2d-texture-standards.md` — Runtime
  formats, PNG for linear-pool textures)

##### graphics-dev-irrlicht

- [x] Implement `IrrlichtRenderer::initCloudPlane()` called once from
  `IrrlichtRenderer::init()` after sky dome creation:
  - Build a flat `SMesh*` plane mesh (single quad, 2 triangles, CW winding for Irrlicht
    left-handed +Y normal) spanning world coordinates `(−cloudHalfExtent, kCloudAltitude,
    −cloudHalfExtent)` to `(+cloudHalfExtent, kCloudAltitude, +cloudHalfExtent)`.
    `kCloudAltitude = 200.0f` (metres). `cloudHalfExtent` defaults to 1000.0f (2 km ×
    2 km plane).
  - UV coordinates: `(0,0)` at near-left, `(kCloudUVScale, 0)` at near-right,
    `(kCloudUVScale, kCloudUVScale)` at far-right, `(0, kCloudUVScale)` at far-left.
    `kCloudUVScale = 4.0f` (tiles the cloud texture 4× across the 2 km extent).
  - Mandatory: call `recalculateBoundingBox()` on every `SMeshBuffer` then on the `SMesh`
    before `addMeshSceneNode()`. Drop the `SMesh*` after `addMeshSceneNode()` has grabbed
    it. (ref: `architecture/graphics-architecture/procedural-terrain.md` — SMesh lifetime)
  - Material settings on the resulting `IMeshSceneNode*`:
    - `MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL`
    - `Lighting = false` (`EMF_LIGHTING = false`)
    - `BackfaceCulling = false` (`EMF_BACK_FACE_CULLING = false`) so the plane is visible
      from below (camera always looks down)
    - `Texture[0]` = `clouds.png` loaded via `IVideoDriver::getTexture()` (linear pool,
      PNG, not DDS — Irrlicht DDS loader is disabled; see `architecture/graphics-architecture/texture-cache.md`
      — "IVideoDriver::getTexture() cannot load DDS files")
  - Store the cloud plane node as `m_cloudNode` (`IMeshSceneNode*`). Store the initial UV
    offset as `m_cloudUVOffset` (`irr::core::vector2df`, initialised to `{0.f, 0.f}`).
  - Store scroll speeds as constants: `kCloudScrollX = 0.002f` UV units/second,
    `kCloudScrollZ = 0.0008f` UV units/second.
  - (ref: `architecture/graphics-architecture/sky-clouds.md`)
- [x] Implement UV scrolling in `IrrlichtRenderer::update(float dt)` using the exact
  single-expression `std::fmod` form from `architecture/graphics-architecture/sky-clouds.md`
  (Implementation section): atomically increment and wrap each component so no intermediate
  unwrapped value is stored, then apply via `setTextureTranslate`. The normative code is in
  `sky-clouds.md` — reproduce it exactly; do not split into separate increment and wrap
  statements. (ref: `architecture/graphics-architecture/sky-clouds.md` — Implementation)
- [x] Add `irr::video::E_DRIVER_TYPE m_driverType{irr::video::EDT_NULL}` to
  `IrrlichtRenderer`'s private section in `IrrlichtRenderer.h`. Initialise in the
  constructor body: `m_driverType = m_device ? m_device->getVideoDriver()->getDriverType()
  : irr::video::EDT_NULL;`. Read from the already-live device at construction time — NOT
  during `createDevice()` (that is `RenderSystem`'s responsibility; the device is fully
  live when passed to `IrrlichtRenderer`'s constructor).
- [x] Guard `initCloudPlane()` with `if (m_driverType == EDT_NULL) return;` as the
  **first line** — before any mesh construction, `buildCloudMesh()`, or `getTexture()`
  call. Do NOT use `m_smgr == nullptr` as the guard: `m_smgr` is non-null even under
  `EDT_NULL`. Under `EDT_NULL`, `m_cloudNode` remains `nullptr`; the `update()` cloud
  scroll block guards with `if (m_cloudNode)`. (ref:
  `architecture/graphics-architecture/sky-clouds.md` — Headless / EDT_NULL Guard section)

##### cicd-dev-github

- [ ] Add a dedicated step **"Verify clouds.png present"** to `build-linux`,
  `build-windows`, and `coverage-linux` jobs in `.github/workflows/ci.yml`, placed in
  the preflight area alongside the existing Phase 10 asset presence gates. Linux /
  coverage-linux form (`shell: bash`):
  `test -f assets/textures/sky/clouds.png || { echo "ERROR: assets/textures/sky/clouds.png missing"; exit 1; }`.
  Windows form (`shell: pwsh`):
  `if (-not (Test-Path "assets/textures/sky/clouds.png")) { Write-Error "ERROR: assets/textures/sky/clouds.png missing"; exit 1 }`.
  Do NOT use `Test-Path ... || exit 1` — that is PowerShell 7+ syntax only; GitHub
  Actions Windows runners use PS 5.1. Hard-fail only; no warning mode.
  (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [ ] Add **Check #24 — Cloud texture format gate** to `tools/validate_assets.py` as
  function `check_24_clouds_png(assets_dir)` returning a list of error strings. Verify
  `assets/textures/sky/clouds.png` is exactly 1024×1024 pixels and RGBA (4 channels)
  using Pillow (already installed from Phase 10). No-op (return `[]`) when the file does
  not exist. Wire into the `run_all_checks()` dispatcher by adding the tuple
  `("Check #24 (cloud texture format)", check_24_clouds_png)` to the checks list — the
  same tuple-list pattern used for Checks #21–#23 (display-name string + function
  reference; do NOT use `errors += check_24_clouds_png(assets_dir)` as a free-standing
  call; the dispatcher iterates the tuple list). Also add a
  **"Verify check_24 present"** grep step to the `validate-assets` job in `ci.yml` with
  pattern `check_24_clouds_png` (full function name), matching the pattern of the existing
  check_21/22/23 verification steps.
- [ ] Update the `validate-assets` job header comment in `.github/workflows/ci.yml` to
  reference Phase 10b / Check #24. Update the `tools/validate_assets.py` module docstring
  to reference Phase 10b and Check #24. Update
  `architecture/ci-cd/github-actions-workflow.md` phasing summary to add:
  `Phase 10b: Check #24 (cloud texture format gate — clouds.png 1024×1024 RGBA) added;
  no change to the job definition or all-checks-pass wiring.`

---

#### Feature 3: Naming Convention Enforcement

Rename all existing class-header files that violate the CamelCase rule, relocate
misplaced concrete classes from `src/interfaces/`, and move interface headers outside
`src/interfaces/` that should live there. Update every `#include` that references a
renamed path. All CI jobs must remain green after the rename pass.

##### graphics-dev-irrlicht

Rename `src/terrain/` and `src/ui/` class headers and update all `#include` references:

| Current path | Renamed to |
|---|---|
| `src/terrain/terrain_generator.h` | `src/terrain/TerrainGenerator.h` |
| `src/ui/budget_detail_panel.h` | `src/ui/BudgetDetailPanel.h` |
| `src/ui/hud.h` | `src/ui/HUD.h` |
| `src/ui/inspector_panel.h` | `src/ui/InspectorPanel.h` |
| `src/ui/main_menu_panel.h` | `src/ui/MainMenuPanel.h` |
| `src/ui/minimap.h` | `src/ui/Minimap.h` |
| `src/ui/modal_dialog.h` | `src/ui/ModalDialog.h` |
| `src/ui/pause_menu_panel.h` | `src/ui/PauseMenuPanel.h` |
| `src/ui/settings_panel.h` | `src/ui/SettingsPanel.h` |
| `src/ui/tax_rate_panel.h` | `src/ui/TaxRatePanel.h` |

Move interface headers into `src/interfaces/` (currently in wrong location):

| Current path | Moved to | Reason |
|---|---|---|
| `src/ui/IUIBackend.h` | `src/interfaces/IUIBackend.h` | Interface — MUST live in `src/interfaces/` |
| `src/terrain/ITerrainRNG.h` | `src/interfaces/ITerrainRNG.h` | Interface — MUST live in `src/interfaces/` |
| `src/audio/ialc_functions.h` | `src/interfaces/IAlcFunctions.h` | Pure-virtual interface — MUST live in `src/interfaces/`; rename to CamelCase with `I` prefix |

After moving `ITerrainRNG.h`, update all relative `#include "ITerrainRNG.h"` sites to
`#include "src/interfaces/ITerrainRNG.h"` (or the appropriate project-root-relative
path): `src/terrain/StdTerrainRNG.h`, `src/terrain/TerrainSystem.h`,
and `tests/terrain/MockTerrainRNG.h`. (`src/terrain/terrain_generator.h` uses a
forward-declaration `class ITerrainRNG;` only — no `#include` present — so no update
is required in that file.)

After renaming `terrain_generator.h` → `TerrainGenerator.h`, update all
`#include "src/terrain/terrain_generator.h"` sites: `src/terrain/TerrainSystem.h`,
`src/terrain/TerrainSystem.cpp`, and `tests/terrain/terrain_stub.cpp`. Verify with
`grep -r "terrain_generator\.h" src/ tests/` — result must be empty after the rename.

Relocate misplaced concrete-class headers out of `src/interfaces/`:

| Current path | Moved to | Reason |
|---|---|---|
| `src/interfaces/null_simulation_pauser.h` | `src/simulation/NullSimulationPauser.h` | Concrete implementation, not an interface |
| `src/interfaces/WallClock.h` | `src/platform/WallClock.h` | Concrete implementation; `.cpp` already lives in `src/platform/` |

After moving `WallClock.h`, update `#include "src/interfaces/WallClock.h"` →
`#include "src/platform/WallClock.h"` in `src/platform/WallClock.cpp` and
`src/main.cpp`. Add `WallClock` to the cicd verification grep pattern.

Remove backward-compat shim headers (they only `#include` the CamelCase file):

| File | Action |
|---|---|
| `src/terrain/terrain_chunk.h` | Delete; update any remaining `#include "terrain_chunk.h"` to `#include "TerrainChunk.h"` |

##### sound-dev-opensoftal

`src/audio/audio_command_queue.h` defines only a POD struct (`PreloadCommand`) and a
type alias — per the naming convention, C-style headers with only constants, enums, or
POD structs use `snake_case`. No rename needed for this file.

`src/audio/al_check.h` and `src/audio/al_check.cpp` contain only inline free functions
(`alCheckError`, `alcCheckError`) — no class definition. Per the naming convention,
CamelCase applies to class files only; free-function headers use `snake_case`. No rename
needed for this pair.

`src/audio/audio_system.h` is a compatibility redirect shim — delete it and update all
callers:

- `src/audio/AudioSystem.h` lines 17–18: update `#include "src/audio/ialc_functions.h"`
  to `#include "src/interfaces/IAlcFunctions.h"` (**sequencing note**: this include
  update is blocked on `graphics-dev-irrlicht` completing the `IAlcFunctions.h` file
  move to `src/interfaces/`; do not merge this change until that PR lands)
- `src/audio/AudioSystem.cpp`: update `#include "src/audio/ialc_functions.h"` to
  `#include "src/interfaces/IAlcFunctions.h"` (same sequencing gate as above)
- `src/main.cpp`: change `#include "src/audio/audio_system.h"` → `#include "src/audio/AudioSystem.h"`
- `tests/audio/audio_thread_test.cpp` lines 27–28: update `ialc_functions.h` include to
  `src/interfaces/IAlcFunctions.h`; update `audio_system.h` include to `AudioSystem.h`
- `tests/audio/volume_control_test.cpp` line 19: update `audio_system.h` → `AudioSystem.h`

##### cicd-dev-github

After all Feature 3 renames are committed, verify the rename pass is complete:

- [ ] Confirm `build-linux` and `build-windows` CI jobs compile cleanly with zero
  header-not-found errors after the rename commit — a green run on both is the gate.
- [ ] Verify no old lowercase names remain in `CMakeLists.txt` source lists by running:
  `grep -r "audio_command_queue\|ialc_functions\|audio_system\.h\|null_simulation_pauser\|manual_clock\|manual_rng\|manual_terrain_query\|mock_audio_system\|mock_renderer\|simulation_test_base\|mock_terrain_rng\|mock_city_simulation\|mock_simulation_pauser\|mock_ui_backend\|terrain_chunk\|terrain_generator\|budget_detail_panel\|hud\.h\|inspector_panel\|main_menu_panel\|minimap\|modal_dialog\|pause_menu_panel\|settings_panel\|tax_rate_panel" CMakeLists.txt`
  — result must be empty.
- [ ] Verify no stale `src/interfaces/WallClock.h` include paths remain after the move:
  `grep -r "src/interfaces/WallClock\.h" src/ tests/` — result must be empty.
- [ ] Update the `lcov --remove` invocation in `coverage-linux` CI job YAML to add
  CamelCase test-double exclusion patterns alongside the existing lowercase ones:
  `'*/Mock*.h' '*/Mock*.cpp' '*/Manual*.h' '*/Manual*.cpp'`. The lowercase `mock_*` and
  `manual_*` patterns become inoperative on Linux after Phase 10b Feature 3 renames all
  test-double headers to CamelCase. Also update the `coverage-linux` step name/comment
  if it references only the lowercase pattern names. See `architecture/testing/coverage.md`
  for the canonical `lcov --remove` invocation including all four prefix variants.
- [ ] After `ITerrainRNG.h` is moved to `src/interfaces/`, remove `src/terrain/` from
  `terrain_tests`'s `target_include_directories` in `CMakeLists.txt` and confirm
  `terrain_tests` builds and passes cleanly. (The path is now redundant: `ITerrainRNG.h`
  moved to `src/interfaces/`; no other Phase-10b-touched interface remains in `src/terrain/`.)

##### test-dev-cpp

After `ITerrainRNG.h` is moved and `mock_terrain_rng.h` is renamed:

- [ ] Update `architecture/testing/testability-architecture.md` section "ITerrainRNG" to
  confirm the source-location declaration reflects the post-Phase-10b paths:
  `ITerrainRNG.h` in `src/interfaces/`; `MockTerrainRNG` in `tests/terrain/MockTerrainRNG.h`.
  (This has been pre-updated in the spec; verify the implementation matches on merge.)

Rename all test-helper class headers and update every `#include` in test `.cpp` files:

| Current path | Renamed to |
|---|---|
| `tests/simulation/manual_clock.h` | `tests/simulation/ManualClock.h` |
| `tests/simulation/manual_rng.h` | `tests/simulation/ManualRNG.h` |
| `tests/simulation/manual_terrain_query.h` | `tests/simulation/ManualTerrainQuery.h` |
| `tests/simulation/mock_audio_system.h` | `tests/simulation/MockAudioSystem.h` |
| `tests/simulation/mock_renderer.h` | `tests/simulation/MockRenderer.h` |
| `tests/simulation/simulation_test_base.h` | `tests/simulation/SimulationTestBase.h` |
| `tests/terrain/mock_terrain_rng.h` | `tests/terrain/MockTerrainRNG.h` |
| `tests/ui/mock_city_simulation.h` | `tests/ui/MockCitySimulation.h` |
| `tests/ui/mock_simulation_pauser.h` | `tests/ui/MockSimulationPauser.h` |
| `tests/ui/mock_ui_backend.h` | `tests/ui/MockUIBackend.h` |

`tests/ui/panel_sentinel_handles.h` contains only constants/handles (no class definition) —
`snake_case` is correct; no rename needed.

### Exit Criteria

- On tile placement (zone, road, service building), the terrain under the placed tile
  visibly flattens: neighbouring tiles blend smoothly with no hard seams
- `TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild` passes on Linux and
  Windows CI without a real GPU
- `TerrainFlattening_NeighborBlend_ClampedToMapBounds` passes — ASAN clean on corner-tile
  flattening with no out-of-bounds write
- `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight` passes — placed mesh Y
  matches the post-flattened height, not the pre-flattening height
- `gamedesign-lookandfeel` blending falloff factors signed off in writing before
  `setTileHeight()` is merged
- Cloud plane renders above terrain with no Z-fighting against the sky dome
- Clouds scroll continuously in the X and Z UV axes; UV offset wraps cleanly with no
  visible seam after extended play sessions
- Cloud plane invisible under `EDT_NULL` (headless CI runs clean; no crash or GL error);
  `CloudPlane_Init_CreatesCloudNode` passes in `opengl_tests`
- `clouds.png` present and 1024×1024 RGBA (Check #24 green)
- `tools/validate_assets.py` module docstring updated to reference Phase 10b and Check #24
- `validate-assets` job header comment in `.github/workflows/ci.yml` updated to reference
  Phase 10b / Check #24
- All class-header files renamed to CamelCase; misplaced concrete classes relocated out of
  `src/interfaces/`; all `#include` paths updated; no broken includes remain; CI green
- `all-checks-pass` CI gate remains green

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `ITerrainQuery::setTileHeight()`, `TerrainSystem` write path, neighbour blending, chunk rebuild enqueue, placement method updates, cloud plane mesh and UV scrolling, Feature 3 src/terrain + src/ui renames and interface relocations (incl. `IAlcFunctions`) |
| `gamedesign-lookandfeel` | Blending falloff factor sign-off |
| `graphics-artist-2d-texture` | `clouds.png` tileable cloud texture |
| `test-dev-cpp` | Three terrain flattening unit tests, CMake wiring, test-helper class renames |
| `cicd-dev-github` | Check #24 cloud texture gate, cloud asset presence gate in CI jobs, Feature 3 CMakeLists verification |
| `sound-dev-opensoftal` | `src/audio/` shim deletion and include-path updates |

### Dependencies

- Requires Phase 5 complete (`TerrainSystem`, `ITerrainQuery`, `ChunkRebuildRequest`
  deque, `ManualTerrainQuery` stub in test suite)
- Requires Phase 9 complete (sky dome `addSkyDomeSceneNode` already placed; building,
  road, and service-building placement helpers exist on `IrrlichtRenderer`)
- Requires Phase 9b complete (`ITerrainQuery::getHeightAt` promoted to interface;
  `ManualTerrainQuery` with `getHeightAt()` override; placement Y position already reads
  from `m_terrain`)
- Can run in parallel with Phase 10 (Dynamic Soundscape) — no shared interfaces; terrain
  flattening is a pure graphics + simulation concern; `sfx_earthworks` wiring is already
  complete from Phase 10

### Risks & Spikes

- **RISK**: Neighbour blending falloff factors are placeholder estimates (cardinal 0.5,
  diagonal 0.25). Wrong values produce jarring plateaus or excessive terrain distortion
  around placements. **Spike**: `gamedesign-lookandfeel` reviews in-editor with test
  cities before sign-off; blocking gate on merge.
- **RISK**: Enqueueing up to 9 chunk rebuilds per placement (centre + 8 neighbours) may
  exceed two chunks per frame, deferring full visual update across multiple frames. At
  high placement rates (bulk zone painting) the rebuild deque may grow unboundedly.
  **Spike**: measure deque depth during rapid rectangular zone placement on a 1024×1024
  map; if deque exceeds ~50 entries, consider raising the per-frame rebuild cap to 4 for
  placement-triggered rebuilds only (guard with a `PlacementRebuildRequest` priority flag
  in `ChunkRebuildRequest`).
- **RESOLVED**: Cloud plane altitude raised from 150 m to `kCloudAltitude = 200.0f` m,
  providing 120 m clearance above the V1 building max height (~80 m). Clipping on
  elevated terrain peaks is no longer a risk at this altitude.
- **RISK**: `EMT_TRANSPARENT_ALPHA_CHANNEL` cloud plane may sort incorrectly relative to
  transparent zone overlay quads, producing rendering artefacts when both are visible.
  **Spike**: verify depth-sort order on a city with active zone overlays; if artefacts
  appear, set `m_cloudNode->setMaterialFlag(EMF_ZBUFFER, true)` and verify the cloud
  plane passes the depth test behind opaque terrain but above the sky dome.
- **RISK**: Irrlicht texture matrix (`getTextureMatrix(0).setTextureTranslate()`) UV
  scrolling may not be supported for all material types or may be silently ignored on some
  OpenGL drivers. **Spike**: verify UV scrolling works in a minimal `requires-opengl`
  integration test or manual smoke test before committing the scrolling implementation.
