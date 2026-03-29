## Phase 11n: Architecture Spec Consistency — CI/CD, Game Design, UI/UX, Graphics, Textures, Cross-Domain Interfaces

**Status: OPEN**

### Goal

Fix all 52 approved specification inconsistencies identified in the full architecture review
(`architecture-review/architecture-review.md`, `architecture-review-inconsistency-fixes.md`).
Every fix in this phase is a **spec-only edit** — no C++ source files are touched.
Downstream code impacts (e.g. renaming `getDemandPressurePct` → `getZoneDemandFactor`,
adding `src/interfaces/vec3.h`) are noted per deliverable and deferred to subsequent phases.

Each deliverable item references its INC number from the review for traceability.

---

### Deliverables

---

#### 1. CI/CD — vcpkg & caching architecture (Group A)

**Files:** `architecture/ci-cd/caching.md`, `architecture/ci-cd/dependency-management.md`

- [ ] **A-1** `caching.md`: Replace the Linux vcpkg caching section with a note explaining
  that Linux builds use a pre-baked Docker image (`/opt/vcpkg_installed`,
  `VCPKG_MANIFEST_INSTALL=OFF`) and therefore require no `actions/cache` step for vcpkg.
  Clarify that the 4-component cache key (`runner.os`, `COMPILER_VERSION`,
  `hashFiles('vcpkg.json')`, `vcpkg_commit_id`) applies to the **Windows job only**.
  Mark the FetchContent caching section "N/A — all dependencies are vcpkg-managed;
  no FetchContent is used in this project." _(INC-001, INC-014)_

- [ ] **A-2** `dependency-management.md`: Clarify that Linux CI derives vcpkg from the
  Docker image rather than a live `lukka/run-vcpkg` invocation. Add a note: "`lukka/run-vcpkg`
  is used only in the Windows job. Linux jobs set `VCPKG_MANIFEST_INSTALL=OFF` and read
  from `/opt/vcpkg_installed` which is baked into the CI image at image-build time." _(INC-002)_

---

#### 2. CI/CD — lcov `--ignore-errors` flags (Group B)

**Files:** `architecture/testing/coverage.md`

- [ ] **B-1** `coverage.md` — `lcov --capture` step: Change `--ignore-errors mismatch,inconsistent`
  to `--ignore-errors mismatch,inconsistent,version`. Add rationale comment: "`version`
  suppresses the gcov version-string mismatch emitted when build and capture gcov versions
  differ slightly." Apply to every code block in the file that shows a `lcov --capture`
  invocation. _(INC-003, INC-067)_

- [ ] **B-2** `coverage.md` — `lcov --remove` step: Change `--ignore-errors unused` to
  `--ignore-errors unused,inconsistent`. Add rationale: "lcov 2.x emits inconsistent data
  errors during `--remove` when processing coverage data with lambda inlining." _(INC-031)_

---

#### 3. CI/CD — workflow snippet corrections (Group C)

**Files:** `architecture/ci-cd/dependency-management.md`, `architecture/ci-cd/github-actions-workflow.md`,
`architecture/ci-cd/caching.md`

- [ ] **C-1** `dependency-management.md` §Step B "Verify GLEW vcpkg install": Change
  `glew.lib` → `glew32.lib`. Add note: "On Windows, the vcpkg GLEW portfile uses a libname
  override that installs `glew32.lib` rather than `glew.lib`." _(INC-013)_

- [ ] **C-2** `dependency-management.md` apt package list: Add `libxxf86vm-dev` with the
  comment "required by Irrlicht (`-lXxf86vm`); omitting this causes a linker error during
  Irrlicht build." Align with `architecture/testing/headless-ci-testing.md` which already
  lists it as required. _(INC-015)_

- [ ] **C-3** `github-actions-workflow.md` §markdown-lint step: Change the install snippet
  from `npm install -g markdownlint-cli` to `npm install -g markdownlint-cli@0.47.0`.
  Reword the adjacent note from "To pin to a specific version use `@0.47.0`" to
  "MUST pin to a specific version; current pin: `@0.47.0`." _(INC-016)_

- [ ] **C-4** `github-actions-workflow.md` Linux coverage job: Change `--base-directory .`
  to `--base-directory "${GITHUB_WORKSPACE}"` in the `lcov --capture` snippet. Add note:
  "`$GITHUB_WORKSPACE` resolves to the container path inside the CI image, which is more
  portable than `.` on non-standard runner layouts." _(INC-029)_

- [ ] **C-5** `caching.md` line referencing `softprops/action-gh-release`: Replace the
  `<40-CHAR-SHA>` placeholder with `9d7c94cfd0a1f3ed45544c887983e9fa900f0564` and add
  comment `# v2.1.0`. _(INC-030)_

- [ ] **C-6** `github-actions-workflow.md` Linux unit and integration test steps: Add
  `ALSOFT_DRIVERS: "null"` to the `env:` block alongside the existing `AITOWN_HEADLESS: "1"`.
  Add inline comment: "null driver required on headless Linux — prevents OpenAL from
  attempting to open a real audio device." _(INC-032)_

- [ ] **C-7** `caching.md` cache key description: Replace the vague "OS" component label
  with the explicit expression `${{ runner.os }}` so the key format is unambiguous
  and matches the actual workflow syntax. _(INC-033)_

- [ ] **C-8** `github-actions-workflow.md` Windows build job: Add a pre-test verification
  step after the build step:
  ```yaml
  - name: Verify soft_oal.dll present
    shell: pwsh
    run: |
      if (-not (Test-Path "build\soft_oal.dll")) {
        Write-Error "soft_oal.dll not found in build\ — rename step failed or DLL was not copied."
        exit 1
      }
  ```
  This is a hard-fail per Phase 7 (`hrtf-initialization.md`). _(INC-042)_

---

#### 4. Testing — coverage gate & parsing method (Group D)

**Files:** `architecture/testing/coverage.md`

- [ ] **D-1** `coverage.md` — Add a phase timeline table near the top of the file:

  | Phase | Gate | Notes |
  |---|---|---|
  | 4 | Informational only | `lcov --summary` output logged; no threshold enforced |
  | 5 | ≥ 80% | First enforced gate via awk |
  | 6+ | ≥ 95% (target 95–98%) | Current enforced gate; `make test` fails below 95% |

  _(INC-004)_

- [ ] **D-2** `coverage.md` §Phase 4 src/ui/ gate: Replace the `lcov --list` column-parsing
  approach with the direct awk SF/LH/LF parse of the `.info` file (the same version-agnostic
  method used in the deployed workflow). Remove the lcov 2.x `|` delimiter preflight check
  as it is no longer needed. _(INC-017)_

---

#### 5. Game Design — core mechanic contradictions (Group E)

**Files:** `architecture/game-design/service-coverage.md`, `architecture/game-design/zoning-system.md`

- [ ] **E-1** `service-coverage.md` §Placement Rules: Remove the sentence "This exemption is
  authoritative and supersedes any implementation-phase plan note." Rewrite to align with
  `zoning-system.md`: "Service buildings may be placed on unzoned tiles or tiles already
  carrying a zone designation, but MUST have at least one cardinal-adjacent road tile (4-directional,
  distance = 1). Placement on a road-occupied tile is rejected. Road adjacency is enforced
  at placement time the same way as for zone buildings." _(INC-009)_

- [ ] **E-2** `zoning-system.md` §Density upgrade wave re-evaluation: Change the demand
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

- [ ] **E-3** All architecture spec files: Rename every occurrence of
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

- [ ] **F-1** `game-progression-modes.md`: Add an explicit constant table:

  | Constant | Value | Effect |
  |---|---|---|
  | `population_milestone_threshold_1` | 1 000 | Toast + stinger + City Rating → Town |
  | `population_milestone_threshold_2` | 10 000 | Toast + stinger + City Rating → City |
  | `population_milestone_threshold_3` | 50 000 | Toast + stinger + City Rating → Metropolis |
  | `population_milestone_threshold_4` | 100 000 | **Toast only** — no stinger, no City Rating change |
  | `population_milestone_threshold_5` | 500 000 | Toast + stinger + City Rating → Megalopolis |

  Add note: "100K is intentionally a toast-only milestone. It does not trigger `stinger_milestone`
  and does not change the City Rating tier." _(INC-012)_

- [ ] **F-2** `save-system.md` §Auto-save triggers: Clarify the forced-loan auto-save
  interaction with the 120-second timer: "The forced-loan auto-save fires on the same tick
  that the deficit condition is detected, before `UIManager::showModal()` is called. This
  resets the 120-second auto-save timer. The timer then pauses for the duration of the
  blocking modal and resumes when the modal is dismissed." _(INC-034)_

- [ ] **F-3** `traffic-system.md` §null-path behavior: Add a clarifying note after the
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

- [ ] **G-1** `hud-layout.md` §Utilities sub-panel: Update to match `ui-manager.md` as the
  canonical source. Correct all four contradicting values:
  - Grid: 2×2 (not 4×1)
  - Button size: 96×48 px (not 64×40 px)
  - Total panel width: 196 px (not 268 px)
  - Top anchor: y:176 (not y:64; aligned with the Utilities toolbar button row)
  _(INC-005, INC-051)_

- [ ] **G-2** `hud-layout.md` §Demand bars: Fix the inline layout note from "y:664–744" to
  "y:664–748" (height = 56 px; y:692 + 56 = 748). Update `ui-manager.md` demand bar
  bounds to y:664–748 for consistency. _(INC-020)_

- [ ] **G-3** `notification-system.md` §CRITICAL toast Z-order: Add a note: "CRITICAL toasts
  render above the resource bar (higher Z-order). The resource bar occupies y:0–56;
  CRITICAL toasts begin at y:20 and therefore overlap the resource bar visually. The toast
  layer MUST be assigned a higher Z-order than the resource bar layer so toasts are not
  occluded." _(INC-021)_

- [ ] **G-4** `hud-layout.md` §kToolbarBottom: Add a cross-reference note: "The `kToolbarBottom
  = 784` constant is the canonical input gate boundary. Its enforcement point is the
  `input-arbitration.md` Priority 5 toolbar dispatch table — both files must agree on this
  value. Any change to toolbar height requires updating both `hud-layout.md` and
  `input-arbitration.md` simultaneously." _(INC-022)_

- [ ] **G-5** `ui-manager.md` `ui_constants.h` block: Add the five missing minimap constants:
  ```cpp
  constexpr s32 kMinimapLeft                  = 1576;
  constexpr s32 kMinimapRight                 = 1920;
  constexpr s32 kMinimapBottom                = 1080;
  constexpr s32 kMinimapWidgetTop             = 880;   // minimap top when no overlay active
  constexpr s32 kMinimapWidgetTopOverlayActive = 920;  // minimap top when overlay panel open
  ```
  (Use actual values from the minimap spec if they differ from the placeholder values above.)
  These are referenced by `input-arbitration.md` Priority 3 dispatch table. _(INC-052)_

- [ ] **G-6** `resolution-ui-scaling.md` Glass City canonical palette table: Add the surplus-green
  entry: `#80C850` — Budget surplus positive balance indicator (used in `finances-panel.md`
  §Budget Section). _(INC-057)_

- [ ] **G-7** `minimap.md` §Water Tower colorblind pattern: Add a note: "The cross-hatch
  pattern is also used for the Industrial demand bar colorblind mode in `hud-layout.md`
  and `resolution-ui-scaling.md`. This reuse is intentional — the two contexts (minimap
  service coverage overlay vs. HUD demand bars) are visually distinct and do not appear
  simultaneously in a way that could cause confusion." _(INC-063)_

- [ ] **G-8** `modal-dialog-system.md` Small modal (480×240 px): Add a content layout note
  for the demolish confirmation dialog: "Content layout: 14px title, 12px body text
  (max 2 lines), 11px 'Do not ask again' checkbox row, 32px button row with 8px gap —
  total content height ≈ 200 px, which fits within the 240 px height at default scaling.
  If a localized string exceeds 2 lines, the modal MUST be promoted to Medium (560×320 px)."
  _(INC-064)_

- [ ] **G-9** `notification-system.md` §Layout constraints: Add a verified layout note:
  "The grace period indicator occupies y:60–92. Normal toasts begin at y:130. The 38 px
  gap (y:92–130) ensures these two elements never overlap. This constraint must be preserved
  if either element's position changes." _(INC-065)_

- [ ] **G-10** `hud-layout.md` §Unsaved changes indicator: Clarify the x:1796 shared edge
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

- [ ] **H-1** `ui-manager.md` §IUIBackend Header Placement: Remove the statement "`IUIBackend.h`
  is placed in `src/ui/`." Replace with: "`IUIBackend.h` lives in `src/interfaces/` (moved
  from `src/ui/` in Phase 10b Feature 3 — see `testability-architecture.md` §IUIBackend
  for the canonical location and method list)." _(INC-018)_

- [ ] **H-2** `testability-architecture.md`: Add a spec entry for `IClock`:
  ```
  Header: src/interfaces/IClock.h
  Methods:
    virtual double now() const = 0;   // seconds since epoch (steady_clock)
    virtual ~IClock() = default;
  Implementations:
    WallClock  — production (std::chrono::steady_clock)
    ManualClock — tests (manually advanced via ManualClock::advance(seconds))
  ```
  Add cross-references from all specs that inject `IClock` (`audio-system.md`,
  `save-system.md`, `economy-model.md`). _(INC-023)_

- [ ] **H-3** `testability-architecture.md`: Add a spec entry for `ISimulationRNG`:
  ```
  Header: src/interfaces/ISimulationRNG.h
  Methods:
    virtual float nextFloat(float lo, float hi) = 0;  // uniform [lo, hi)
    virtual int   nextInt(int lo, int hi)        = 0;  // uniform [lo, hi]
    virtual ~ISimulationRNG() = default;
  Implementations:
    StdRNG    — production (std::mt19937)
    ManualRNG — tests (returns pre-loaded sequence)
  ```
  Cross-reference from `service-coverage.md` and any other spec referencing `ISimulationRNG`.
  _(INC-024)_

- [ ] **H-4** `irrlicht-device-lifecycle.md`: Add a unified end-to-end per-frame loop table
  that merges the 8-step simulation loop (from `simulation-time.md`) with the 11-step
  render loop, producing one authoritative sequence showing all steps in order. Add a
  cross-reference in `simulation-time.md`: "See `irrlicht-device-lifecycle.md §Per-Frame
  Loop` for the canonical combined sequence including render steps." _(INC-025)_

- [ ] **H-5** `simulation-time.md` frame loop: Add step 3c explicitly:
  ```
  3c. SaveSystem::update(realDeltaSeconds)
      — ticks the 120-second auto-save timer; fires auto-save if threshold reached.
      — Runs after UIManager::update() (step 3b), before terrain/audio updates.
  ```
  _(INC-053)_

- [ ] **H-6** `testability-architecture.md`: Add a spec entry for the `vec3` type alias:
  ```
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

- [ ] **H-7** All architecture spec files: Standardize every reference to `simulation_types.h`
  to use the fully qualified form `src/interfaces/simulation_types.h`. Affected files
  include at minimum: `zoning-system.md`, `service-coverage.md`, `traffic-system.md`,
  `testability-architecture.md`, `ui-manager.md`. _(INC-055)_

---

#### 9. Graphics/rendering spec contradictions (Group I)

**Files:** `architecture/graphics-architecture/texture-cache.md`,
`architecture/graphics-architecture/scene-graph-ownership.md`,
`architecture/graphics-architecture/model-validator-tool.md`,
`architecture/graphics-architecture/irrlicht-device-lifecycle.md`,
`architecture/graphics-architecture/procedural-terrain.md`,
`architecture/asset-standards/3d-model-standards.md`

- [ ] **I-1** `texture-cache.md` §evictUnreferenced() contract: Add an explicit call-site
  safety rule: "`evictUnreferenced()` MUST only be called during the game-logic update
  phase — strictly before `driver->beginScene()` is called. It MUST NOT be called from
  within any Irrlicht scene callback, event handler, or any code path that may execute
  inside `sceneManager->drawAll()`. Violations will cause use-after-free on scene nodes
  whose textures are evicted mid-draw." Cross-reference this rule in
  `irrlicht-device-lifecycle.md` §Frame loop eviction note. _(INC-006)_

- [ ] **I-2** `scene-graph-ownership.md` §static_cast WARNING: Replace the blanket
  prohibition with a precise rule: "NEVER use `static_cast<SMesh*>` on an `IMesh*`
  pointer — this is an unsafe downcast that bypasses virtual dispatch and produces
  undefined behaviour if the object is not actually an `SMesh`. This prohibition applies
  only to downcasts. **Upcasting from `IAnimatedMesh*` to `IMesh*` via `static_cast` is
  always safe** because `IAnimatedMesh` publicly inherits `IMesh` — see §B3D Building
  Assets for the canonical usage." _(INC-007)_

- [ ] **I-3** `model-validator-tool.md` §Phase 11d Asset Inventory table: Add the missing
  vehicle row:

  | Category | Count | Asset names |
  |---|---|---|
  | Vehicle LOD0 | 5 | car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo |

  Update the sub-total line to: "**Total: 45** (36 zone buildings + 4 service buildings
  + 5 vehicles)." _(INC-019)_

- [ ] **I-4** `model-validator-tool.md` §Road tile category: Add a note: "Road tile geometry
  is **procedurally generated at runtime** via `buildTileRoadMesh()`. No road tile `.b3d`
  asset exists. The validator exercises road tiles by calling the same `buildTileRoadMesh()`
  function as `IrrlichtRenderer` — not by loading a file. This is the identical code path
  per the validator design goal." _(INC-026)_

- [ ] **I-5** `irrlicht-device-lifecycle.md` §Camera scene node construction: Add a
  requirement note: "The camera's far-clip distance MUST be set to ≥ 15 000 m.
  Values below 15 000 m will hard-clip the cloud dome vertices.
  (See `sky-clouds.md` §farClip requirement.)" _(INC-036)_

- [ ] **I-6** `irrlicht-device-lifecycle.md` §Inline code sample (top of file): Add
  `renderer->update(realDeltaSeconds);` between `terrainSystem->update(dt);` and
  `driver->beginScene(...)`. Add inline comment: `// cloud UV scroll + per-frame renderer state`.
  _(INC-037)_

- [ ] **I-7** `procedural-terrain.md` and `3d-model-standards.md` §Polygon offset: Reconcile
  the carriageway `PolygonOffsetFactor`. The canonical value is **1** (per `procedural-terrain.md`).
  Update `3d-model-standards.md` §Center-line strip to use `PolygonOffsetFactor = 2`
  (one step above carriageway = 1) and remove the comment "one step above the carriageway's
  `factor = 4`" — that comment is wrong. _(INC-038)_

- [ ] **I-8** `irrlicht-device-lifecycle.md` §Construction sequence table — step 2
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

- [ ] **J-1** `2d-texture-standards.md` §Naming convention table: Add `_splat` as a 7th
  recognized suffix:

  | Suffix | Format | Usage |
  |---|---|---|
  | `_splat` | **PNG only** (not DDS) | Terrain splat map — blending weights for terrain texture layers |

  Add note: "Splat maps are loaded as PNG at runtime via `IVideoDriver::getTexture()`.
  `validate_assets.py` MUST accept `.png` files ending in `_splat` and MUST NOT require
  them to be DDS. The canonical filename pattern is `terrain_splat.png`."
  _(INC-008)_

- [ ] **J-2** `2d-texture-standards.md` §Building atlas section: Add a "V1 implementation
  note" box: "**V1 PNG workaround:** The production format for `buildings_atlas_d` is
  DDS DXT1 sRGB (Phase 11+). In V1, the atlas is loaded as `buildings_atlas_d.png` via
  `IVideoDriver::getTexture()` (linear, uncompressed). See `building-atlas-layout.md`
  §V1 PNG workaround for details. Mark DDS artifacts as Phase 11+ deliverables in asset
  pipeline tasks." _(INC-027)_

- [ ] **J-3** `2d-texture-standards.md` §Normal map authoring checklist: Add an inline
  note after the DXGI 77 entry: "**DXGI 77 = BC3_UNORM (linear) — this is correct for
  normal maps.** Do NOT use DXGI 78 (BC3_UNORM_SRGB) for normal maps — the sRGB decode
  applied at sample time will corrupt the encoded XY direction vectors and produce
  incorrect lighting." _(INC-047)_

- [ ] **J-4** `2d-texture-standards.md` §Naming convention table: Add `_tileable` as an
  8th recognized suffix:

  | Suffix | Format | Usage |
  |---|---|---|
  | `_tileable` | DDS DXT1 sRGB | Road surface tileable texture (e.g. `road_asphalt_tileable.dds`) |

  Add note: "`_tileable` is used as a dispatch key in `texture-cache.md`. The naming
  convention and dispatch table must agree." _(INC-048)_

- [ ] **J-5** `building-atlas-layout.md` §Phase 5 sign-off block: Add a correction note
  directly after the sign-off: "> **SUPERSEDED by phase-11e:** Effective UV island
  resolution is now **496×496 px** per variant cell (512×512 cell minus 8 px border
  on each side × 2). The '256×256 effective per island' figure in this sign-off reflects
  the pre-phase-11e shared-cell design and is no longer valid. Current atlas: 8×8 grid,
  512×512 px per cell." _(INC-049)_

- [ ] **J-6** `texture-cache.md` §VRAM estimation formula: Add a note after the 1.33×
  factor explanation: "The 1.33× overhead factor applies equally to **4-level and 5-level
  mip chains** — the additional fifth mip level (e.g. 256×256 for a 4096×4096 atlas) adds
  only ~0.4% of the mip-0 footprint and is negligible for budget estimation purposes." _(INC-050)_

- [ ] **J-7** `building-atlas-layout.md` §Road Marking Atlas and `2d-texture-standards.md`
  §Road marking: Add an authoritative ruling on the sRGB vs. linear decision:
  "**Road marking atlas upload path: linear (not sRGB).** Road marking channel data
  (white lane lines, crosswalk stripes) is authored as **grayscale mask values**, not
  perceptual diffuse colors. The shader multiplies the mask against a hardcoded perceptual
  color constant at sample time — the texture data itself is linear mask data.
  Linear upload is correct. `texture-cache.md` dispatch table entry for `_road_marking`
  is correct as-is." _(INC-056)_

---

### Exit Criteria

- [ ] All 52 checklist items above are ticked.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` reports zero errors.
- [ ] No new spec contradictions are introduced (spot-check: every edited file's cross-references remain consistent).
- [ ] Downstream code impacts noted in E-2, E-3, H-6 are captured as open items in `INDEX.md` §Spec Contradictions Flagged (or as explicit deliverables in a subsequent phase).

---

### Roles

| Deliverable | Primary role |
|---|---|
| 1–4 (CI/CD, Testing) | `cicd-dev-github` |
| 5–6 (Game Design) | `gamedesign-lookandfeel` |
| 7 (UI/UX) | `gamedesign-ux` |
| 8 (Cross-domain interfaces) | `graphics-dev-irrlicht` + `test-dev-cpp` |
| 9 (Graphics) | `graphics-dev-irrlicht` |
| 10 (Textures) | `graphics-artist-2d-texture` |
