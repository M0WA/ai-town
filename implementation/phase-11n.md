## Phase 11n: Architecture Spec Consistency — CI/CD, Game Design, UI/UX, Graphics, Textures, Cross-Domain Interfaces

**Status: DONE**

### Goal

Fix specification inconsistencies identified in the full architecture review
(`architecture-review/architecture-review.md`, `architecture-review-inconsistency-fixes.md`).
Original count: 52 approved items. After OD resolution: **51 active checkboxes** (I-7 removed
as its premise was wrong; B-1, C-4, C-6 pre-completed as already applied in the codebase).

Every fix in this phase is a **spec-only edit** — no C++ source files are touched.
Downstream code impacts (e.g. renaming `getDemandPressurePct` → `getZoneDemandFactor`,
adding `src/interfaces/vec3.h`) are noted per deliverable and deferred to subsequent phases.

> **Design change note (E-1):** The service-building placement rule in `service-coverage.md`
> is being deliberately changed (not just consistency-fixed) to require road adjacency and
> reject placement on road-occupied tiles. This aligns the spec with `zoning-system.md` and
> was accepted as a game-design decision (OD-2 Option B).

Each deliverable item references its INC number from the review for traceability.

---

### Deliverables

---

#### 1. CI/CD — vcpkg & caching architecture (Group A)

**Files:** `architecture/ci-cd/caching.md`, `architecture/ci-cd/dependency-management.md`

- [x] **A-1** `caching.md`: Replace the Linux vcpkg caching section with a note explaining
  that Linux builds use a pre-baked Docker image (`/opt/vcpkg_installed`,
  `VCPKG_MANIFEST_INSTALL=OFF`) and therefore require no `actions/cache` step for vcpkg.
  Clarify that the 4-component cache key (`runner.os`, `COMPILER_VERSION`,
  `hashFiles('vcpkg.json')`, `vcpkg_commit_id`) applies to the **Windows job only**.
  Add a clarifying note to the FetchContent key entry confirming it applies to the
  `coverage-linux` job only (not all jobs). The FetchContent key IS in active use for
  `coverage-linux` and MUST NOT be removed or marked N/A. _(INC-001, INC-014)_

- [x] **A-2** `dependency-management.md`: Clarify that Linux CI derives vcpkg from the
  Docker image rather than a live `lukka/run-vcpkg` invocation. Add a note: "`lukka/run-vcpkg`
  is used only in the Windows job. Linux jobs set `VCPKG_MANIFEST_INSTALL=OFF` and read
  from `/opt/vcpkg_installed` which is baked into the CI image at image-build time." _(INC-002)_

---

#### 2. CI/CD — lcov `--ignore-errors` flags (Group B)

**Files:** `architecture/testing/coverage.md`

- [x] **B-1** `coverage.md` — `lcov --capture` step: Change `--ignore-errors mismatch,inconsistent`
  to `--ignore-errors mismatch,inconsistent,version`. _(INC-003, INC-067)_
  > **PRE-COMPLETED**: `coverage.md` already contains `--ignore-errors mismatch,inconsistent,version`
  > with the rationale comment. No file change needed.

- [x] **B-2** `coverage.md` — `lcov --remove` step: Change `--ignore-errors unused` to
  `--ignore-errors unused,inconsistent`. Add rationale: "lcov 2.x emits inconsistent data
  errors during `--remove` when processing coverage data with lambda inlining." _(INC-031)_

---

#### 3. CI/CD — workflow snippet corrections (Group C)

**Files:** `architecture/ci-cd/dependency-management.md`, `architecture/ci-cd/github-actions-workflow.md`,
`architecture/ci-cd/caching.md`

- [x] **C-1** `dependency-management.md` §Step B "Verify GLEW vcpkg install": Change
  `glew.lib` → `glew32.lib`. Add note: "On Windows, the vcpkg GLEW portfile uses a libname
  override that installs `glew32.lib` rather than `glew.lib`." _(INC-013)_

- [x] **C-2** `dependency-management.md` apt package list: Add `libxxf86vm-dev` with the
  comment "required by Irrlicht (`-lXxf86vm`); omitting this causes a linker error during
  Irrlicht build." Align with `architecture/testing/headless-ci-testing.md` which already
  lists it as required. _(INC-015)_

- [x] **C-3** `github-actions-workflow.md` §markdown-lint step: Change the install snippet
  from `npm install -g markdownlint-cli` to `npm install -g markdownlint-cli@0.47.0`.
  Reword the adjacent note from "To pin to a specific version use `@0.47.0`" to
  "MUST pin to a specific version; current pin: `@0.47.0`." _(INC-016)_

- [x] **C-4** `github-actions-workflow.md` Linux coverage job: Change `--base-directory .`
  to `--base-directory "${GITHUB_WORKSPACE}"` in the `lcov --capture` snippet. _(INC-029)_
  > **PRE-COMPLETED**: `github-actions-workflow.md` line 431 already uses
  > `--base-directory ${{ github.workspace }}`. No file change needed.

- [x] **C-5** `caching.md` line referencing `softprops/action-gh-release`: Replace the
  `<40-CHAR-SHA>` placeholder with `9d7c94cfd0a1f3ed45544c887983e9fa900f0564` and add
  comment `# v2.1.0`. _(INC-030)_

- [x] **C-6** `github-actions-workflow.md` Linux unit and integration test steps: Add
  `ALSOFT_DRIVERS: "null"` to the `env:` block alongside the existing `AITOWN_HEADLESS: "1"`.
  _(INC-032)_
  > **PRE-COMPLETED**: `ALSOFT_DRIVERS: 'null'` is already present in all three Linux test
  > step `env:` blocks (unit, integration, opengl). No file change needed.

- [x] **C-7** `caching.md` cache key description: Replace the vague "OS" component label
  with the explicit expression `${{ runner.os }}` so the key format is unambiguous
  and matches the actual workflow syntax. _(INC-033)_

- [x] **C-8** `github-actions-workflow.md` Windows build job: Add a pre-test verification
  step after the build step.

  ```yaml
  - name: Verify Phase 7 DLLs and HRTF data present
    shell: pwsh
    run: |
      if (-not (Test-Path "build\soft_oal.dll")) {
        Write-Error "soft_oal.dll not found in build\ — rename step failed or DLL was not copied."
        exit 1
      }
      if (-not (Test-Path "build\default.mhr")) {
        Write-Error "default.mhr not found in build\ — HRTF post-build copy rule failed."
        exit 1
      }
  ```

  Both checks are hard-fails per Phase 7 (`hrtf-initialization.md`). _(INC-042)_

---

#### 4. Testing — coverage gate & parsing method (Group D)

**Files:** `architecture/testing/coverage.md`

- [x] **D-1** `coverage.md` — Add a phase timeline table near the top of the file:

  | Phase | Gate | Notes |
  |---|---|---|
  | 4 | Informational only | `lcov --summary` output logged; no threshold enforced |
  | 5 | ≥ 80% | First enforced gate via awk |
  | 6+ | ≥ 95% (target 95–98%) | Current enforced gate; `make test` fails below 95% |

  _(INC-004)_

- [x] **D-2** `coverage.md` §Phase 4 src/ui/ gate: Replace the `lcov --list` column-parsing
  approach with the direct awk SF/LH/LF parse of the `.info` file (the same version-agnostic
  method used in the deployed workflow). Remove the lcov 2.x `|` delimiter preflight check
  as it is no longer needed. _(INC-017)_

---

#### 5. Game Design — core mechanic contradictions (Group E)

**Files:** `architecture/game-design/service-coverage.md`, `architecture/game-design/zoning-system.md`

- [x] **E-1** `service-coverage.md` §Placement Rules: Remove the sentence "This exemption is
  authoritative and supersedes any implementation-phase plan note." Rewrite to align with
  `zoning-system.md`: "Service buildings may be placed on unzoned tiles or tiles already
  carrying a zone designation, but MUST have at least one cardinal-adjacent road tile (4-directional,
  distance = 1). Placement on a road-occupied tile is rejected. Road adjacency is enforced
  at placement time the same way as for zone buildings." _(INC-009)_

- [x] **E-2** `zoning-system.md` §Density upgrade wave re-evaluation: Change the demand
  threshold from `0.50` to `0.75`. Rename the constant reference from
  `density_upgrade_wave_demand_threshold` to `density_upgrade_threshold` to distinguish it
  from `construction_delay_demand_threshold` (which remains at 0.50). Add a clarifying
  note: "Note: `construction_delay_demand_threshold = 0.50` (minimum demand for a zone tile
  to start construction). `density_upgrade_threshold = 0.75` (minimum demand for a built
  tile to qualify for a density upgrade). These are distinct constants."
  > **Downstream code impact (deferred):** `SimulationConstants` in the implementation
  > must split the single 0.50 constant into two separate named constants before Phase 12
  > closes. No code change in this phase.
  _(INC-010)_

- [x] **E-3** All architecture spec files: Rename every occurrence of
  `ICitySimulation::getDemandPressurePct(ZoneType)` to `ICitySimulation::getZoneDemandFactor(ZoneType)`.
  Add a disambiguation note where the method is first defined:
  "`getZoneDemandFactor(ZoneType)` returns the city-wide effective demand in [0.0, 1.0]
  (1.0 = maximum demand pressure). This is DISTINCT from `QueryResult::demandPressurePct`
  which is `(1.0f − effective_demand_factor) × 100` — an inverse per-tile percentage used
  only in the query inspector panel."
  Files to update: `traffic-system.md`, `hud-layout.md`, `testability-architecture.md`,
  any other spec file referencing `getDemandPressurePct`.
  > **Downstream code impact (deferred):** `ICitySimulation.h`, `CitySimulation.cpp`,
  > `UIManager.cpp`, and `MockCitySimulation.h` must be updated before Phase 12 closes.
  _(INC-011)_

---

#### 6. Game Design — simulation constants & edge cases (Group F)

**Files:** `architecture/game-design/game-progression-modes.md`,
`architecture/game-design/save-system.md`, `architecture/game-design/traffic-system.md`

- [x] **F-1** `game-progression-modes.md`: Add an explicit constant table:

  | Constant | Value | Effect |
  |---|---|---|
  | `population_milestone_threshold_1` | 1 000 | Toast + stinger + City Rating → Town |
  | `population_milestone_threshold_2` | 10 000 | Toast + stinger + City Rating → City |
  | `population_milestone_threshold_3` | 50 000 | Toast + stinger + City Rating → Metropolis |
  | `population_milestone_threshold_4` | 100 000 | **Toast only** — no stinger, no City Rating change |
  | `population_milestone_threshold_5` | 500 000 | Toast + stinger + City Rating → Megalopolis |

  Add note: "100K is intentionally a toast-only milestone. It does not trigger `stinger_milestone`
  and does not change the City Rating tier." _(INC-012)_

- [x] **F-2** `save-system.md` §Auto-save triggers: Clarify the forced-loan auto-save
  interaction with the 120-second timer: "The forced-loan auto-save fires on the same tick
  that the deficit condition is detected, before `UIManager::showModal()` is called. This
  resets the 120-second auto-save timer. The timer then pauses for the duration of the
  blocking modal and resumes when the modal is dismissed." _(INC-034)_

- [x] **F-3** `traffic-system.md` §null-path behavior: Add a clarifying note after the
  null-path floor definition: "**Timeout trips vs. null-path ticks:** Timeout trips
  (demand_factor = 0.0) are distinct from null-path ticks (demand_factor = 0.5 neutral
  floor). The 0.5 floor applies ONLY when ALL ticks in the smoothing window are null-path
  (no valid road graph exists). A mixed window containing both timeout trips and valid trips
  will produce an average below 0.5 without applying the floor." _(INC-035)_

---

#### 7. UI/UX — layout contradictions (Group G)

**Files:** `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/ui-manager.md`,
`architecture/ui-ux/notification-system.md`, `architecture/ui-ux/resolution-ui-scaling.md`,
`architecture/ui-ux/minimap.md`, `architecture/ui-ux/modal-dialog-system.md`,
`architecture/ui-ux/input-arbitration.md`

- [x] **G-1** `hud-layout.md` §Utilities sub-panel: Update to match `ui-manager.md` as the
  canonical source. Correct all four contradicting values:
  - Grid: 2×2 (not 4×1)
  - Button size: 96×48 px (not 64×40 px)
  - Total panel width: 196 px (not 268 px)
  - Top anchor: y:176 (not y:64; aligned with the Utilities toolbar button row)
  _(INC-005, INC-051)_

- [x] **G-2** `hud-layout.md` §Demand bars: Fix the inline layout note from "y:664–744" to
  "y:664–748" (height = 56 px; y:692 + 56 = 748). Update `ui-manager.md` demand bar
  bounds to y:664–748 for consistency. _(INC-020)_
  > **PRE-APPLIED**: `hud-layout.md` line 14 stale reference "y:664–744" changed to "y:664–748" to match the authoritative value on line 19. The `ui-manager.md` portion of G-2 requires no change (no 664/748 constants exist in that file). No file change needed.

- [x] **G-3** `notification-system.md` §CRITICAL toast Z-order: Add a note: "CRITICAL toasts
  render above the resource bar (higher Z-order). The resource bar occupies y:0–56;
  CRITICAL toasts begin at y:20 and therefore overlap the resource bar visually. The toast
  layer MUST be assigned a higher Z-order than the resource bar layer so toasts are not
  occluded." _(INC-021)_

- [x] **G-4** `hud-layout.md` §kToolbarBottom: Also change the existing 'Priority 3' reference
  in the `kToolbarBottom` cross-reference line to 'Priority 5' (the toolbar dispatch is
  Priority 5 per `input-arbitration.md`). Then add a cross-reference note: "The `kToolbarBottom
  = 784` constant is the canonical input gate boundary. Its enforcement point is the
  `input-arbitration.md` Priority 5 toolbar dispatch table — both files must agree on this
  value. Any change to toolbar height requires updating both `hud-layout.md` and
  `input-arbitration.md` simultaneously." _(INC-022)_

- [x] **G-5** `ui-manager.md` `ui_constants.h` block: Document all minimap constants.
  Three constants already exist in `src/ui/ui_constants.h` with correct values (verified from
  source). Two are missing. The spec must document all five.

  **Already in `src/ui/ui_constants.h`** (document in spec, no code change needed):

  ```cpp
  constexpr int kMinimapWidgetLeft             = 1576;  // left edge of full widget footprint
  constexpr int kMinimapWidgetTop              = 848;   // toggle row top (no overlay)
  constexpr int kMinimapWidgetTopOverlayActive = 732;   // legend panel top (overlay active)
  ```

  **Missing from `src/ui/ui_constants.h`** (add to both spec and source in a later phase):

  ```cpp
  constexpr int kMinimapRight   = 1920;  // right edge (screen right)
  constexpr int kMinimapBottom  = 1080;  // bottom edge (screen bottom)
  ```

  These are referenced by `input-arbitration.md` Priority 3 dispatch table. _(INC-052)_

- [x] **G-6** `resolution-ui-scaling.md` Glass City canonical palette table: Add the surplus-green
  entry: `#80C850` — Budget surplus positive balance indicator (used in `finances-panel.md`
  §Budget Section). _(INC-057)_

- [x] **G-7** `minimap.md` §Water Tower colorblind pattern: Add a note: "The cross-hatch
  pattern is also used for the Industrial demand bar colorblind mode in `hud-layout.md`
  and `resolution-ui-scaling.md`. This reuse is intentional — the two contexts (minimap
  service coverage overlay vs. HUD demand bars) are visually distinct and do not appear
  simultaneously in a way that could cause confusion." _(INC-063)_

- [x] **G-8** `modal-dialog-system.md` Small modal (480×240 px): Add a content layout note
  for the demolish confirmation dialog: "Content layout: 14px title, 12px body text
  (max 2 lines), 11px 'Do not ask again' checkbox row, 32px button row with 8px gap —
  total content height ≈ 200 px, which fits within the 240 px height at default scaling.
  If a localized string exceeds 2 lines, the modal MUST be promoted to Medium (560×320 px)."
  _(INC-064)_

- [x] **G-9** `notification-system.md` §Layout constraints: Add a verified layout note:
  "The grace period indicator occupies y:60–92. Normal toasts begin at y:130. The 38 px
  gap (y:92–130) ensures these two elements never overlap. This constraint must be preserved
  if either element's position changes." _(INC-065)_

- [x] **G-10** `hud-layout.md` §Unsaved changes indicator: Clarify the x:1796 shared edge
  with the time controls block (x:1600–1796): "The unsaved changes dot begins at x:1796
  (immediately adjacent to the time controls right edge at x:1796). This zero-gap edge-share
  is intentional — the dot and time controls share the boundary pixel. If this causes visual
  crowding at any supported DPI, shift the dot right by 8 px (x:1804) and the bell
  accordingly, updating both `hud-layout.md` and `ui-manager.md`." _(INC-066)_

---

#### 8. Cross-domain — interface definitions (Group H)

**Files:** `architecture/ui-ux/ui-manager.md`, `architecture/testing/testability-architecture.md`,
`architecture/graphics-architecture/irrlicht-device-lifecycle.md`,
`architecture/game-design/simulation-time.md`

- [x] **H-1** `ui-manager.md` §IUIBackend Header Placement: Remove the statement "`IUIBackend.h`
  is placed in `src/ui/`." Replace with: "`IUIBackend.h` lives in `src/interfaces/` (moved
  from `src/ui/` in Phase 10b Feature 3 — see `testability-architecture.md` §IUIBackend
  for the canonical location and method list)." _(INC-018)_

- [x] **H-2** `testability-architecture.md`: Add a spec entry for `IClock`.

  ```text
  Header: src/interfaces/IClock.h
  Methods:
    virtual double nowSeconds() const = 0;   // seconds since epoch (steady_clock)
    virtual ~IClock() = default;
  Implementations:
    WallClock  — production (std::chrono::steady_clock)
    ManualClock — tests (manually advanced via ManualClock::advance(seconds))
  ```

  Add cross-references from all specs that inject `IClock` (`audio-system.md`,
  `save-system.md`, `economy-model.md`). _(INC-023)_

- [x] **H-3** `testability-architecture.md`: Add a formal spec entry block for `ISimulationRNG`,
  documenting the **existing** interface (OD-4: keep no-arg `nextFloat()`).

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

  Cross-reference from `service-coverage.md` and any other spec referencing `ISimulationRNG`.
  _(INC-024)_

- [x] **H-4** `irrlicht-device-lifecycle.md`: Add a unified end-to-end per-frame loop table
  that merges the 8-step simulation loop (from `simulation-time.md`) with the 11-step
  render loop, producing one authoritative sequence showing all steps in order. Add a
  cross-reference in `simulation-time.md`: "See `irrlicht-device-lifecycle.md §Per-Frame
  Loop` for the canonical combined sequence including render steps." _(INC-025)_

- [x] **H-5** `simulation-time.md` frame loop: Add step 3c explicitly.

  ```text
  3c. SaveSystem::update(realDeltaSeconds)
      — ticks the 120-second auto-save timer; fires auto-save if threshold reached.
      — Runs after UIManager::update() (step 3b), before terrain/audio updates.
  ```

  _(INC-053)_

- [x] **H-6** `testability-architecture.md`: Add a spec entry for the `vec3` type alias.

  ```text
  Header: src/interfaces/vec3.h  (or src/interfaces/simulation_math.h)
  Definition:
    struct vec3 { float x, y, z; };
    // Does NOT include any Irrlicht headers.
    // IAudioSystem.h, ISpatialAudio.h, and all simulation interfaces use vec3.
    // IrrlichtRenderer converts vec3 ↔ irr::core::vector3df at the boundary.
  ```

  Add cross-references in `audio-system.md`, `spatial-audio.md`, and `traffic-system.md`
  pointing to this canonical header.
  > **Downstream code impact (deferred):** `src/interfaces/vec3.h` must be created before
  > Phase 12 audio/simulation code can compile cleanly against IAudioSystem.h.
  _(INC-054)_

- [x] **H-7** All architecture spec files: Standardize every reference to `simulation_types.h`
  to use the fully qualified form `src/interfaces/simulation_types.h`. Affected files
  include at minimum: `zoning-system.md`, `service-coverage.md`, `traffic-system.md`,
  `testability-architecture.md`, `ui-manager.md`, `audio-system.md`. _(INC-055)_

---

#### 9. Graphics/rendering spec contradictions (Group I)

**Files:** `architecture/graphics-architecture/texture-cache.md`,
`architecture/graphics-architecture/scene-graph-ownership.md`,
`architecture/graphics-architecture/model-validator-tool.md`,
`architecture/graphics-architecture/irrlicht-device-lifecycle.md`,
`architecture/graphics-architecture/procedural-terrain.md`,
`architecture/asset-standards/3d-model-standards.md`

- [x] **I-1** `texture-cache.md` §evictUnreferenced() contract: Add an explicit call-site
  safety rule: "`evictUnreferenced()` MUST only be called during the game-logic update
  phase — strictly before `driver->beginScene()` is called. It MUST NOT be called from
  within any Irrlicht scene callback, event handler, or any code path that may execute
  inside `sceneManager->drawAll()`. Violations will cause use-after-free on scene nodes
  whose textures are evicted mid-draw." Cross-reference this rule in
  `irrlicht-device-lifecycle.md` §Frame loop eviction note. _(INC-006)_

- [x] **I-2** `scene-graph-ownership.md` §static_cast WARNING: Replace the blanket
  prohibition with a precise rule: "NEVER use `static_cast<SMesh*>` on an `IMesh*`
  pointer — this is an unsafe downcast that bypasses virtual dispatch and produces
  undefined behaviour if the object is not actually an `SMesh`. This prohibition applies
  only to downcasts. **Upcasting from `IAnimatedMesh*` to `IMesh*` via `static_cast` is
  always safe** because `IAnimatedMesh` publicly inherits `IMesh` — see §B3D Building
  Assets for the canonical usage." _(INC-007)_
  > **PRE-APPLIED**: The `static_cast` WARNING in `scene-graph-ownership.md` has been updated to distinguish unsafe downcasts (`SMesh*` from `IMesh*`) from safe upcasts (`IAnimatedMesh*` to `IMesh*`). No file change needed.

- [x] **I-3** `model-validator-tool.md` §Phase 11d Asset Inventory table: Add the missing
  vehicle row:

  | Category | Count | Asset names |
  |---|---|---|
  | Vehicle LOD0 | 5 | car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo |

  Add a new **"Total LOD0 `.b3d`"** sub-total row = **45** (36 zone buildings + 4 service
  buildings + 5 vehicles). This is a NEW row — do NOT replace the existing "Total `.b3d`"
  all-level row. Also update the existing "Total `.b3d`" all-level count from **92 → 102**
  (adding 5 vehicle LOD0 + 5 vehicle LOD1). The two counts are distinct: 45 = LOD0 only;
  102 = all LOD levels across all asset categories. _(INC-019)_

- [x] **I-4** `model-validator-tool.md` §Road tile category: Add a note: "Road tile geometry
  is **procedurally generated at runtime** via `buildTileRoadMesh()`. No road tile `.b3d`
  asset exists. The validator exercises road tiles by calling the same `buildTileRoadMesh()`
  function as `IrrlichtRenderer` — not by loading a file. This is the identical code path
  per the validator design goal." _(INC-026)_

- [x] **I-5** `irrlicht-device-lifecycle.md` §Construction Sequence table — Step 4 note:
  Add a requirement note immediately below Step 4 ("Camera scene node + CameraController")
  in the construction sequence: "The camera's far-clip distance MUST be set to ≥ 15 000 m.
  Values below 15 000 m will hard-clip the cloud dome vertices.
  (See `sky-clouds.md` §Cloud Dome Geometry.)" If the step already has a note column,
  add to it. If no note column exists for Step 4, create a parenthetical or sub-bullet
  beneath the step. _(INC-036)_

- [x] **I-6** `irrlicht-device-lifecycle.md` §Inline code sample (top of file): Add
  `renderer->update(realDeltaSeconds);` between `terrainSystem->update(dt);` and
  `driver->beginScene(...)`. Add inline comment: `// cloud UV scroll + per-frame renderer state`.
  _(INC-037)_
  > **PRE-APPLIED**: `renderer->update(realDeltaSeconds);` with inline comment
  > `// cloud UV scroll + per-frame renderer state` has been inserted in the
  > `irrlicht-device-lifecycle.md` code block between `terrainSystem->update(dt)` and
  > `driver->beginScene(...)`. No file change needed.

- [x] **I-8** `irrlicht-device-lifecycle.md` §Construction sequence table — step 2
  (`IrrlichtUIBackend`): Add a note: "`IrrlichtUIBackend` MUST be constructed in `main.cpp`
  AFTER `RenderSystem`'s constructor returns. `RenderSystem`'s constructor calls `glewInit()`;
  `IrrlichtUIBackend` requires GLEW to be initialised before it can call any GL extension
  functions. Do NOT make `IrrlichtUIBackend` a member of `RenderSystem` — this would
  invert the construction order." _(INC-039)_

---

#### 10. Texture/asset spec contradictions (Group J)

**Files:** `architecture/asset-standards/2d-texture-standards.md`,
`architecture/asset-standards/building-atlas-layout.md`,
`architecture/graphics-architecture/texture-cache.md`

- [x] **J-1** `2d-texture-standards.md` §Naming convention table: Add `_splat` as a 7th
  recognized suffix:

  | Suffix | Format | Usage |
  |---|---|---|
  | `_splat` | **PNG only** (not DDS) | Terrain splat map — blending weights for terrain texture layers |

  Add note: "Splat maps are loaded as PNG at runtime via `IVideoDriver::getTexture()`.
  `validate_assets.py` MUST accept `.png` files ending in `_splat` and MUST NOT require
  them to be DDS. The canonical filename pattern is `terrain_splat.png`."
  _(INC-008)_
  > **PRE-APPLIED**: `_splat` row (PNG only, terrain splat map) and associated note have been
  > added to the `2d-texture-standards.md` naming convention table. No file change needed.

- [x] **J-2** `2d-texture-standards.md` §Building atlas section: Add a "V1 implementation
  note" box: "**V1 PNG workaround:** The production format for `buildings_atlas_d` is
  DDS DXT1 sRGB (Phase 11+). In V1, the atlas is loaded as `buildings_atlas_d.png` via
  `IVideoDriver::getTexture()` (linear, uncompressed). See `building-atlas-layout.md`
  §V1 PNG workaround for details. Mark DDS artifacts as Phase 11+ deliverables in asset
  pipeline tasks." _(INC-027)_

- [x] **J-3** `2d-texture-standards.md` §Normal map authoring checklist: Add an inline
  note after the DXGI 77 entry: "**DXGI 77 = BC3_UNORM (linear) — this is correct for
  normal maps.** Do NOT use DXGI 78 (BC3_UNORM_SRGB) for normal maps — the sRGB decode
  applied at sample time will corrupt the encoded XY direction vectors and produce
  incorrect lighting." _(INC-047)_

- [x] **J-4** `2d-texture-standards.md` §Naming convention table: Add `_tileable` as an
  8th recognized suffix:

  | Suffix | Format | Usage |
  |---|---|---|
  | `_tileable` | DDS DXT1 sRGB | Road surface tileable texture (e.g. `road_asphalt_tileable.dds`) |

  Add note: "`_tileable` is used as a dispatch key in `texture-cache.md`. The naming
  convention and dispatch table must agree." _(INC-048)_
  > **PRE-APPLIED**: `_tileable` row (DDS DXT1 sRGB, road asphalt) and associated note have been
  > added to the `2d-texture-standards.md` naming convention table. No file change needed.

- [x] **J-5** `building-atlas-layout.md` §Phase 5 sign-off block: Add a correction note
  directly after the sign-off: "> **SUPERSEDED by phase-11e:** Effective UV island
  resolution is now **496×496 px** per variant cell (512×512 cell minus 8 px border
  on each side × 2). The '256×256 effective per island' figure in this sign-off reflects
  the pre-phase-11e shared-cell design and is no longer valid. Current atlas: 8×8 grid,
  512×512 px per cell." _(INC-049)_
  > **PRE-APPLIED**: Supersession note ("SUPERSEDED by phase-11e: effective UV island resolution is now 496×496 px per variant cell") has been added after the Phase 5 sign-off in `building-atlas-layout.md`. No file change needed.

- [x] **J-6** `texture-cache.md` §VRAM estimation formula: Add a note after the 1.33×
  factor explanation: "The 1.33× overhead factor applies equally to **4-level and 5-level
  mip chains** — the additional fifth mip level (e.g. 256×256 for a 4096×4096 atlas) adds
  only ~0.4% of the mip-0 footprint and is negligible for budget estimation purposes." _(INC-050)_

- [x] **J-7** `texture-cache.md` §Suffix dispatch table: Add the missing `_road_marking`
  entry. `2d-texture-standards.md` already specifies sRGB for road markings (OD-6: keep sRGB).
  The dispatch table simply lacked the entry. Add:

  ```text
  _road_marking  →  loadSRGB()
  ```

  Add a note: "Road marking atlas (`road_marking_atlas.dds`) contains diffuse road surface
  color and lane marking color — visual data requiring gamma-correct sampling. Upload via the
  sRGB path, consistent with `2d-texture-standards.md` §Road marking." _(INC-056)_

---

---

### Open Decisions

The following items require a deliberate decision or pre-application verification before the
implementing agent can proceed. Each entry documents the exact divergence found between the
proposed fix and the current state of the target files.

---

#### OD-1 — B-1 already applied (no-op) ✅ RESOLVED

**Decision:** B-1 marked as pre-completed (`[x]`). No file change needed.

---

#### OD-2 — E-1 rule change ✅ RESOLVED

**Decision:** Option B — accepted as a deliberate game-design decision. E-1 fix as written
applies the road-adjacency rule. Phase goal section updated with a design-change note.
`zoning-system.md` should also be updated as part of E-1 to state the rule explicitly for
service buildings (add to existing Multi-Tile Footprint Placement Rules section).

---

#### OD-3 — G-5 corrected to source code values ✅ RESOLVED

**Decision:** Use source code values. `src/ui/ui_constants.h` already defines
`kMinimapWidgetLeft = 1576`, `kMinimapWidgetTop = 848`, `kMinimapWidgetTopOverlayActive = 732`
with correct values. G-5 checkbox updated to document these existing constants and flag
`kMinimapRight = 1920` and `kMinimapBottom = 1080` as missing from source.

---

#### OD-4 — H-3 signature decision ✅ RESOLVED

**Decision:** Option A — keep existing no-arg `nextFloat()`. H-3 checkbox updated to
document the existing interface (`nextFloat()` returns [0.0, 1.0); `nextInt(int min, int max)`).
No interface change; no downstream code impact.

---

#### OD-5 — I-7 removed ✅ RESOLVED

**Decision:** I-7 removed from the checklist. Both `procedural-terrain.md` and
`3d-model-standards.md` already agree: carriageway `PolygonOffsetFactor = 4`,
center-line `= 5`. The `factor = 1` in `procedural-terrain.md` is for the terrain mesh
material, not the road carriageway. No change needed.

---

#### OD-6 — J-7 sRGB ruling ✅ RESOLVED

**Decision:** Option A — keep sRGB. `2d-texture-standards.md` already specifies sRGB for
road markings. J-7 checkbox updated to add the missing `_road_marking` → `loadSRGB()` entry
to `texture-cache.md`'s dispatch table. No changes to `2d-texture-standards.md` needed.

---

#### OD-7 — Pre-application verification ✅ RESOLVED

All items flagged for pre-application verification have been read. Findings and implementor
guidance are in the table below — no further decisions needed.

| Item | File | Finding | Action |
|---|---|---|---|
| **A-1** | `caching.md` | No distinct "Linux vcpkg caching" or "FetchContent caching" headings exist. The FetchContent key in line 28 is explicitly scoped to the `coverage-linux` job. The "OS" label appears in prose on line 3. | Reword fix: modify inline prose on line 3 to use `${{ runner.os }}`; add a qualifying note to line 28 clarifying the FetchContent key is `coverage-linux`-only; do NOT mark FetchContent as N/A — it is in active use for that job |
| **A-2** | `dependency-management.md` | Lines 26–33 describe `lukka/run-vcpkg` as the CI integration without Windows-only scope. No Docker-image language is present. | Add a scoping sentence to the existing paragraph (lines 26–33) rather than appending a standalone note — the added sentence must qualify that `lukka/run-vcpkg` is invoked in the Windows job; Linux jobs use `VCPKG_MANIFEST_INSTALL=OFF` against `/opt/vcpkg_installed` |
| **C-1** | `dependency-management.md` | Confirmed: both PowerShell paths (lines 234 and 235) use `glew.lib`. Both must be changed. | Update **both** occurrences to `glew32.lib` |
| **C-2** | `dependency-management.md` | Confirmed: neither `build-linux` (line 70) nor `coverage-linux` (line 77) install snippet includes `libxxf86vm-dev`. | Add `libxxf86vm-dev` to **both** install snippets |
| **C-3** | `github-actions-workflow.md` | Line 583: `npm install -g markdownlint-cli` — **no version pin**. Line 594: optional pin note ("To pin to a specific version use `@0.47.0`"). Fix IS needed. | Change line 583 to `npm install -g markdownlint-cli@0.47.0`; change line 594 note from "To pin…" to "MUST pin…" |
| **C-4** | `github-actions-workflow.md` | **Already applied.** Line 431 already uses `--base-directory ${{ github.workspace }}`. | Mark C-4 as pre-completed — no file change needed |
| **C-6** | `github-actions-workflow.md` | **Already applied.** `ALSOFT_DRIVERS: 'null'` is present in all three Linux test steps (unit, integration, opengl). | Mark C-6 as pre-completed — no file change needed |
| **C-8** | `github-actions-workflow.md` | No Phase 7 combined DLL+HRTF hard-fail step found in the Windows job. Only reference to `soft_oal.dll` is in the CPack install section. The single-DLL check proposed by phase-11n is safe to add. However, `dependency-management.md` already specifies a combined step checking **both** `soft_oal.dll` and `default.mhr`. | Add a combined step checking both `soft_oal.dll` AND `default.mhr` (not just `soft_oal.dll` as written in phase-11n). Update the phase-11n snippet accordingly. |
| **E-2** | `zoning-system.md` | Confirmed at line 145: `density_upgrade_wave_demand_threshold = 0.50` exists; at line 163: `construction_delay_demand_threshold = 0.50` exists. Both are at 0.50, confirming the split has not been applied. | Fix IS needed: change `density_upgrade_wave_demand_threshold` → `density_upgrade_threshold = 0.75`; keep `construction_delay_demand_threshold = 0.50` unchanged |
| **F-1** | `game-progression-modes.md` | File documents milestones inline at line 3 and explains the 100K no-stinger rule at line 16. **No explicit constant table** (`population_milestone_threshold_1..5`) exists. No conflicting table to resolve. | Fix IS needed: add the 5-row constant table as proposed |
| **F-2** | `save-system.md` | Line 3 confirms auto-save fires before modal is shown. Missing: explicit statement that this resets the 120s timer and that the timer pauses during the blocking modal. | Fix IS needed: add the timer-reset and modal-pause clarification |
| **G-1** | `hud-layout.md` | Confirmed at lines 114–124: 4×1 single-row grid, y:64, 64×40 px buttons, columns 0–3 = Power Plant / Water Tower / Fire Station / Police Station. Contradicts `ui-manager.md` (2×2, y:176, 96×48). | Fix IS needed. Correct 2×2 cell assignment (per `service-coverage.md`): **Row 0** = (Power Plant col 0, Water Tower col 1); **Row 1** = (Fire Station col 0, Police Station col 1) |
| **G-2** | `hud-layout.md` + `ui-manager.md` | Confirmed: line 14 parenthetical still says `y:664–744` (stale). Primary bounds at line 19 already correct at `y:664–748`. No demand bar bounds constant exists in `ui-manager.md` — grep finds no `664`/`744`/`748` in that file. | Update line 14 parenthetical from `y:664–744` to `y:664–748`. **Scope the `ui-manager.md` portion of G-2 to "no change needed"** — there is no target text to update |
| **G-4** | `hud-layout.md` | Confirmed: line 11 says "Priority 3". The `kToolbarBottom = 784` input gate is Priority 5 in `input-arbitration.md`. | Fix must change "Priority 3" → "Priority 5" on line 11 (in addition to adding the cross-reference note) |
| **I-3** | `model-validator-tool.md` | Confirmed: no Vehicle LOD0 row in the table; line 183 states 45 LOD0 files. Zone(36)+Service(4) = 40, gap of 5 = vehicles. Current "Total `.b3d`: **92**" counts LOD0+LOD1+LOD2 for zone+service only (no vehicles). Vehicles have LOD0+LOD1 each, so adding them raises the all-`.b3d` total to 92+10 = **102**. | Add Vehicle LOD0 row (5). Add a separate "Total LOD0 `.b3d`" row = **45** (36+4+5). Update the "Total `.b3d`" all-level count from 92 → **102** (add 5 vehicle LOD0 + 5 vehicle LOD1). Do NOT replace the 92-total row with "45" — the two counts are distinct |
| **J-5** | `building-atlas-layout.md` | Confirmed: Phase 5 exit criterion block exists (line 340). Line 330 contains "diffuse at DXT1 256x256 effective per island" — the stale value that needs the supersession note. | Fix IS needed: add the "SUPERSEDED by phase-11e: effective UV island resolution is now 496×496 px" correction note after the Phase 5 sign-off paragraph (after line 342) |

---

### Exit Criteria

- [x] All 51 checklist items above are ticked (52 original − 1 removed (I-7); B-1, C-4, C-6 pre-completed).
- [x] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` reports zero errors.
- [x] No new spec contradictions are introduced (spot-check: every edited file's cross-references remain consistent).
- [x] Downstream code impacts noted in E-2, E-3, H-6 are captured as open items in `INDEX.md` §Spec Contradictions Flagged (or as explicit deliverables in a subsequent phase).

---

### Roles

| Deliverable | Primary role |
|---|---|
| 1–4 (CI/CD, Testing) | `cicd-dev-github` |
| 5–6 (Game Design) | `gamedesign-lookandfeel` |
| 7 (UI/UX) | `gamedesign-ux` |
| 8 (Cross-domain interfaces) | `graphics-dev-irrlicht` + `test-dev-cpp` (H-6 audio spec cross-refs: `sound-dev-opensoftal`) |
| 9 (Graphics) | `graphics-dev-irrlicht` |
| 10 (Textures) | `graphics-artist-2d-texture` |
