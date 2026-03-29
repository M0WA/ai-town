# Architecture Spec — Inconsistency Fix Proposals

Extracted from `architecture-review/architecture-review.md`.
67 inconsistencies grouped into 11 clusters, sorted by severity.
Each item references the original review INC number for traceability.

**Status legend:** `[ ]` pending decision · `[x]` approved · `[-]` skipped

---

## Group A — CI/CD: vcpkg & caching architecture

**Severity:** CRITICAL/HIGH · **Issues:** INC-001, INC-002, INC-014

**What conflicts:**
`caching.md` and `dependency-management.md` document a `lukka/run-vcpkg` + `actions/cache`
architecture that was never built. The actual Linux pipeline uses a pre-baked Docker image
(`/opt/vcpkg_installed`) with `VCPKG_MANIFEST_INSTALL=OFF` — zero `actions/cache` steps,
zero `lukka/run-vcpkg` calls. Also: `caching.md` has a FetchContent caching section but all
deps are vcpkg-managed (no FetchContent used).

**Proposed fixes:**

- [+] **A-1** `caching.md`: Replace the vcpkg Linux caching section with: "Linux builds use a
  pre-baked Docker image — no `actions/cache` for vcpkg. The 4-component cache key applies
  to Windows only." Remove the FetchContent section (or gate it "only if FetchContent is used").
  _(INC-001, INC-014)_
- [+] **A-2** `dependency-management.md`: Clarify Linux uses the Docker image; `lukka/run-vcpkg`
  is Windows-only. _(INC-002)_

---

## Group B — CI/CD: lcov `--ignore-errors` flags

**Severity:** CRITICAL/MEDIUM · **Issues:** INC-003, INC-031, INC-067

**What conflicts:**
Three different sets of flags are documented across files:

- `CLAUDE.md`: `mismatch,inconsistent,version` for `lcov --capture`
- `coverage.md`: `mismatch,inconsistent` (missing `version`) for capture; `unused` for remove
- `_coverage-linux.yml`: `mismatch,inconsistent` for capture; `unused,inconsistent` for remove

**Proposed fixes:**

- [+] **B-1** `coverage.md`: Add `version` to `lcov --capture` flag →
  `--ignore-errors mismatch,inconsistent,version` _(INC-003, INC-067)_
- [+] **B-2** `coverage.md`: Add `inconsistent` to `lcov --remove` flag →
  `--ignore-errors unused,inconsistent` _(INC-031)_

---

## Group C — CI/CD: small workflow snippet corrections

**Severity:** HIGH/MEDIUM · **Issues:** INC-013, INC-015, INC-016, INC-029, INC-030, INC-032, INC-033, INC-042

**What conflicts:**
Eight small spec-vs-reality mismatches in CI/CD spec files.

**Proposed fixes:**

- [+] **C-1** `dependency-management.md`: `glew.lib` → `glew32.lib` + add note about Windows
  portfile libname override. _(INC-013)_
- [+] **C-2** `dependency-management.md`: Add `libxxf86vm-dev` to apt package list (required by
  Irrlicht; currently missing from spec; `headless-ci-testing.md` lists it as required). _(INC-015)_
- [+] **C-3** `github-actions-workflow.md` markdown-lint section: Change `npm install -g markdownlint-cli`
  → `npm install -g markdownlint-cli@0.47.0`; reword note from optional to mandatory pin. _(INC-016)_
- [+] **C-4** `_coverage-linux.yml`: `--base-directory .` → `--base-directory "${GITHUB_WORKSPACE}"`.
  _(INC-029)_
- [+] **C-5** `caching.md` line 37: Replace `<40-CHAR-SHA>` placeholder with
  `9d7c94cfd0a1f3ed45544c887983e9fa900f0564` for `softprops/action-gh-release`. _(INC-030)_
- [+] **C-6** `github-actions-workflow.md` Linux test steps: Explicitly require `ALSOFT_DRIVERS=null`
  alongside `AITOWN_HEADLESS=1` in `env:` for unit and integration test steps. _(INC-032)_
- [+] **C-7** `caching.md`: Clarify OS component in cache key is `${{ runner.os }}` (unambiguous
  expression). _(INC-033)_
- [+] **C-8** `github-actions-workflow.md` Windows build job: Add `soft_oal.dll` pre-test existence
  check step with meaningful error message on failure. _(INC-042)_

---

## Group D — Testing: coverage gate & parsing method

**Severity:** CRITICAL/HIGH · **Issues:** INC-004, INC-017

**What conflicts:**

1. Agent system prompt says 80% gate; `CLAUDE.md` + `coverage.md` say 95% (Phase 6+).
2. `coverage.md` §Phase 4 documents `lcov --list` column-parsing; actual workflow uses
   direct awk SF/LH/LF parse of `.info` (more reliable, version-agnostic).

**Proposed fixes:**

- [+] **D-1** `coverage.md`: Add phase timeline note at the top: "Phase 4: informational;
  Phase 5: 80% gate; Phase 6+: 95% gate (target range 95–98%)." _(INC-004)_
- [+] **D-2** `coverage.md` §Phase 4: Replace `lcov --list` column-parsing approach with the
  direct awk SF/LH/LF `.info` parse method (matches deployed implementation). _(INC-017)_

---

## Group E — Game Design: core mechanic contradictions

**Severity:** HIGH · **Issues:** INC-009, INC-010, INC-011

**What conflicts:**
Three high-impact contradictions in fundamental game rules — each requires a decision on
which file is authoritative.

**Proposed fixes:**

- [+] **E-1** Service building road adjacency — `zoning-system.md` wins (road adjacency required):
  Remove the absolute "this exemption is authoritative" override clause from `service-coverage.md`.
  Clarify: service buildings can be placed on unzoned or zone-occupied tiles but NOT on road-occupied
  tiles and NOT without a cardinal-adjacent road tile. _(INC-009)_
- [+] **E-2** Density upgrade demand threshold — `population-density-growth.md` wins (threshold = 0.75):
  Update `zoning-system.md` §Density upgrade wave re-evaluation from `0.50` to `0.75`.
  Rename constant to `density_upgrade_threshold` to distinguish it from
  `construction_delay_demand_threshold` (stays 0.50). _(INC-010)_
- [+] **E-3** `getDemandPressurePct()` semantics clash — rename to eliminate ambiguity:
  `ICitySimulation::getDemandPressurePct(ZoneType)` → `getZoneDemandFactor(ZoneType)`
  (returns [0.0, 1.0] direct semantics). Keep `QueryResult::demandPressurePct` as-is
  ([0,100] inverse). Update all spec cross-references. _(INC-011)_

---

## Group F — Game Design: simulation constants & edge cases

**Severity:** HIGH/MEDIUM · **Issues:** INC-012, INC-034, INC-035

**What conflicts:**
Missing constant definitions and underspecified edge case interactions.

**Proposed fixes:**

- [+] **F-1** `game-progression-modes.md`: Add explicit constant table:
  `population_milestone_threshold_1 = 1000`, `_2 = 10000`, `_3 = 50000`, `_4 = 100000`,
  `_5 = 500000`. Note that 100K fires a toast only (no stinger, no City Rating transition).
  _(INC-012)_
- [+] **F-2** `save-system.md`: Clarify forced-loan auto-save interaction with 120s timer:
  "Forced-loan auto-save fires before `UIManager::showModal()` is called. This resets the
  120s timer. The timer pauses for the modal's duration." _(INC-034)_
- [+] **F-3** `traffic-system.md`: Add clarifying note: "Timeout trips (demand*factor = 0.0)
  are distinct from null-path ticks (demand_factor = 0.5 floor). The 0.5 floor only applies
  when ALL ticks in the window are null-path; a mixed window averages below 0.5 without
  applying the floor." *(INC-035)\_

---

## Group G — UI/UX: layout contradictions

**Severity:** CRITICAL/HIGH/MEDIUM · **Issues:** INC-005, INC-020, INC-021, INC-022, INC-051, INC-052, INC-057, INC-063, INC-064, INC-065, INC-066

**What conflicts:**
One critical layout contradiction and ten pixel-level or visual-stacking spec gaps.

**Proposed fixes:**

- [+] **G-1** Utilities sub-panel layout (CRITICAL) — `ui-manager.md` wins (2×2 grid at y:176):
  Update `hud-layout.md` to match: 2×2 grid, 96×48 px buttons, total width = 196 px,
  top = y:176. _(INC-005, INC-051)_
- [+] **G-2** Demand bar bottom y — 748 is correct (y:692 + height 56 = 748): Update the inline
  note in `hud-layout.md` and `ui-manager.md` to use y:664–748 consistently. _(INC-020)_
- [+] **G-3** CRITICAL toast Z-order over resource bar: Add to `notification-system.md`:
  "CRITICAL toasts render above the resource bar (higher Z-order)." _(INC-021)_
- [+] **G-4** `kToolbarBottom = 784` cross-reference: Add cross-reference in `hud-layout.md`
  pointing to `input-arbitration.md` as the enforcement point. _(INC-022)_
- [+] **G-5** Minimap constants missing from `ui-manager.md`: Add `kMinimapWidgetTop`,
  `kMinimapWidgetTopOverlayActive`, `kMinimapLeft = 1576`, `kMinimapRight = 1920`,
  `kMinimapBottom = 1080` to the `ui_constants.h` block. _(INC-052)_
- [+] **G-6** Surplus-green `#80C850` not in Glass City palette: Add it to the canonical palette
  table in `resolution-ui-scaling.md`. _(INC-057)_
- [+] **G-7** Cross-hatch pattern reuse (Water Tower minimap + Industrial demand bar): Add a note
  in `minimap.md` and `hud-layout.md` documenting the intentional reuse with context
  disambiguation. _(INC-063)_
- [+] **G-8** Small modal content-fit for demolish confirmation: Add explicit font/layout guidance
  for the Small (480×240 px) modal, or upgrade demolish confirmation to Medium (560×320 px).
  _(INC-064)_
- [+] **G-9** Grace period indicator (y:60–92) + Normal toasts (y:130) gap: Add a note in
  `notification-system.md` confirming the 38 px gap and no overlap. _(INC-065)_
- [+] **G-10** Time controls / unsaved-changes dot zero-gap at x:1796: Clarify intent in
  `hud-layout.md`. If unintentional, shift dot to x:1804 and adjust bell accordingly. _(INC-066)_

---

## Group H — Cross-domain: interface definitions

**Severity:** HIGH/MEDIUM · **Issues:** INC-018, INC-023, INC-024, INC-025, INC-053, INC-054, INC-055

**What conflicts:**
Key interfaces (`IClock`, `ISimulationRNG`, `vec3`) have no canonical spec file; the frame
loop is split across two files; `IUIBackend.h` location is contradicted; header paths
are inconsistent.

**Proposed fixes:**

- [+] **H-1** `IUIBackend.h` location — `testability-architecture.md` wins (`src/interfaces/`):
  Update `ui-manager.md` to remove conflicting claim; add cross-reference to
  `testability-architecture.md`. _(INC-018)_
- [+] **H-2** `IClock` interface: Add `IClock` spec entry to `testability-architecture.md`
  with canonical header (`src/interfaces/IClock.h`) and method list. _(INC-023)_
- [+] **H-3** `ISimulationRNG` interface: Add spec entry to `testability-architecture.md`
  with canonical header (`src/interfaces/ISimulationRNG.h`) and method list (`nextFloat()`,
  etc.). _(INC-024)_
- [+] **H-4** Frame loop split: Add unified end-to-end frame loop table to
  `irrlicht-device-lifecycle.md`; add cross-reference from `simulation-time.md`. _(INC-025)_
- [+] **H-5** `SaveSystem::update()` at step 3c: Add explicit step 3c to the canonical frame
  loop in `simulation-time.md` (immediately after `UIManager::update()` at step 3b). _(INC-053)_
- [+] **H-6** `vec3` type undefined: Define `vec3` alias in a shared header that excludes
  Irrlicht headers (`src/interfaces/vec3.h` or `simulation_math.h`). Document the canonical
  header in `testability-architecture.md`. _(INC-054)_
- [+] **H-7** `simulation_types.h` path: Standardize all spec references to the fully qualified
  path `src/interfaces/simulation_types.h`. _(INC-055)_

---

## Group I — Graphics/rendering spec contradictions

**Severity:** CRITICAL/HIGH/MEDIUM · **Issues:** INC-006, INC-007, INC-019, INC-026, INC-036, INC-037, INC-038, INC-039

**What conflicts:**
Two internal contradictions within individual spec files plus six cross-file conflicts
in the graphics architecture domain.

**Proposed fixes:**

- [+] **I-1** `evictUnreferenced()` call-site safety (CRITICAL): Add explicit rule to
  `texture-cache.md`: "`evictUnreferenced()` MUST only be called in the game-logic update
  phase (before `driver->beginScene()`), never from Irrlicht scene callbacks or event
  handlers that may fire inside `drawAll()`." Cross-reference in
  `irrlicht-device-lifecycle.md`. _(INC-006)_
- [+] **I-2** `static_cast` WARNING (CRITICAL): Clarify in `scene-graph-ownership.md`:
  "This prohibition applies only to downcasts (`IMesh*` → `SMesh*`). Upcasting
  `IAnimatedMesh*` → `IMesh*` is always safe." _(INC-007)_
- [+] **I-3** Model validator inventory: Add vehicle row to the asset inventory table:
  "Vehicle LOD0 | 5 | car*sedan, car_hatchback, car_suv, bus_standard, truck_cargo".
  Update sub-total to 45 = 40 + 5. *(INC-019)\_
- [+] **I-4** Road tile mesh: Update `model-validator-tool.md` to explicitly state road tiles
  are procedurally generated via `buildTileRoadMesh()`; no road tile `.b3d` asset exists.
  _(INC-026)_
- [+] **I-5** `farClip` requirement: Add to `irrlicht-device-lifecycle.md` camera construction
  step: "Set `farClip >= 15000 m` to prevent cloud dome hard-clipping (see `sky-clouds.md`)."
  _(INC-036)_
- [+] **I-6** `renderer->update()` missing from inline code sample: Add it to the inline sample
  between `terrainSystem->update()` and `driver->beginScene()`. _(INC-037)_
- [+] **I-7** Polygon offset factor — decide canonical carriageway value and propagate:
  If carriageway = 1 (per `procedural-terrain.md`), update `3d-model-standards.md` center-line
  strip to `factor = 2`. If carriageway = 4, update `procedural-terrain.md`. Both files must
  agree. _(INC-038)_
- [+] **I-8** `glewInit()` ordering: Add note to step 2 of construction sequence table in
  `irrlicht-device-lifecycle.md`: "`IrrlichtUIBackend` must be constructed AFTER
  `RenderSystem`'s constructor completes (which calls `glewInit()`). Do NOT construct
  it as a `RenderSystem` member." _(INC-039)_

---

## Group J — Texture/asset spec contradictions

**Severity:** CRITICAL/HIGH/MEDIUM · **Issues:** INC-008, INC-027, INC-047, INC-048, INC-049, INC-050, INC-056

**What conflicts:**
Suffix allowlist gaps, stale sign-off content, and two ambiguous upload-path decisions.

**Proposed fixes:**

- [+] **J-1** `_splat` suffix (CRITICAL): Add `_splat` (PNG only, not DDS) as a 7th recognized
  suffix in `2d-texture-standards.md` naming convention table. State canonical filename
  pattern. Confirm `validate_assets.py` accepts PNG files with `_splat` suffix. _(INC-008)_
- [+] **J-2** Building atlas V1 PNG workaround: Add matching "V1 PNG workaround" note in
  `2d-texture-standards.md` §building atlas; mark production DDS path as "Phase 11+".
  _(INC-027)_
- [+] **J-3** Normal map DXGI 77 vs 78: Add inline note to `2d-texture-standards.md` normal
  map checklist: "DXGI 77 = BC3*UNORM (linear) — correct for normal maps. Do NOT use
  DXGI 78 (BC3_UNORM_SRGB) — sRGB decode corrupts direction vectors." *(INC-047)\_
- [+] **J-4** `_tileable` suffix: Add `_tileable` to the naming convention table in
  `2d-texture-standards.md` (sRGB DXT5, road surface only). _(INC-048)_
- [+] **J-5** Stale Phase 5 sign-off in `building-atlas-layout.md`: Add correction note
  directly under sign-off: "UPDATED phase-11e: effective resolution per UV island is now
  496×496 px. The '256×256 effective per island' figure is superseded." _(INC-049)_
- [+] **J-6** VRAM formula label: Add note to `texture-cache.md`: "The 1.33× factor applies
  to both 4-level and 5-level mip chains — the 5th level adds <0.4%." _(INC-050)_
- [+] **J-7** Road marking atlas sRGB vs. linear — requires an authoritative decision:
  Add explicit ruling in both `building-atlas-layout.md` and `2d-texture-standards.md`:
  either "road markings are grayscale mask data → linear is correct" OR "road markings are
  perceptual colors → sRGB required; update upload path and `texture-cache.md` dispatch table."
  _(INC-056)_

---

## Group K — Audio architecture contradictions

**Severity:** HIGH/MEDIUM/LOW · **Issues:** INC-028, INC-040, INC-041, INC-043, INC-044, INC-045, INC-046, INC-058, INC-059, INC-060, INC-061, INC-062

**What conflicts:**
Twelve audio spec issues — one implementation-critical code path bug, several cross-file
reference gaps, and a few stale/ambiguous spec statements.

**Proposed fixes:**

- [-] **K-1** `openStreamOGG` flush code (HIGH) _(INC-028)_
- [-] **K-2** Stinger crisis trigger _(INC-045)_
- [-] **K-3** Vehicle engine minimum duration _(INC-043)_
- [-] **K-4** Music stem duration spec _(INC-044)_
- [-] **K-5** Pattern A/B canonical designation _(INC-046, INC-062)_
- [-] **K-6** `MockUIBackend` method count _(INC-040)_
- [-] **K-7** `kEvictableSFXCount` derivation _(INC-041)_
- [-] **K-8** EFX boolean cross-references _(INC-058)_
- [-] **K-9** GAME_OVER stinger promotion cross-reference _(INC-059)_
- [-] **K-10** `sfx_vehicle_horn` priority _(INC-060)_
- [-] **K-11** Ambient bed loop description _(INC-061)_

---

## Summary

| Group                                 | Severity             | Issues                                                         | Fixes        |
| ------------------------------------- | -------------------- | -------------------------------------------------------------- | ------------ |
| A — CI/CD vcpkg architecture          | CRITICAL/HIGH        | INC-001, 002, 014                                              | 2            |
| B — CI/CD lcov flags                  | CRITICAL/MEDIUM      | INC-003, 031, 067                                              | 2            |
| C — CI/CD workflow snippets           | HIGH/MEDIUM          | INC-013, 015, 016, 029, 030, 032, 033, 042                     | 8            |
| D — Testing: coverage gate            | CRITICAL/HIGH        | INC-004, 017                                                   | 2            |
| E — Game Design: core mechanics       | HIGH                 | INC-009, 010, 011                                              | 3            |
| F — Game Design: constants/edge cases | HIGH/MEDIUM          | INC-012, 034, 035                                              | 3            |
| G — UI/UX: layout                     | CRITICAL/HIGH/MEDIUM | INC-005, 020, 021, 022, 051, 052, 057, 063, 064, 065, 066      | 10           |
| H — Cross-domain interfaces           | HIGH/MEDIUM          | INC-018, 023, 024, 025, 053, 054, 055                          | 7            |
| I — Graphics/rendering                | CRITICAL/HIGH/MEDIUM | INC-006, 007, 019, 026, 036, 037, 038, 039                     | 8            |
| J — Texture/asset                     | CRITICAL/HIGH/MEDIUM | INC-008, 027, 047, 048, 049, 050, 056                          | 7            |
| K — Audio architecture                | HIGH/MEDIUM/LOW      | INC-028, 040, 041, 043, 044, 045, 046, 058, 059, 060, 061, 062 | 11           |
| **Total**                             |                      | **67 inconsistencies**                                         | **63 fixes** |
