## Phase 11q2: Fix SonarCloud HIGH Maintainability Issues (S134 + S3776)

**Status: Planned**

**Prerequisite**: phase-11q1 (decomposed `CitySimulation.cpp` into `Zoning`, `Population`,
`Traffic`, `Economy`, `SimTiming`).
**Blocks**: nothing — purely internal refactor, no interface changes.

### Goal

Fix all 32 open HIGH-impact SonarCloud MAINTAINABILITY issues introduced by the
phase-11q1 decomposition. Every issue is one of two rules:

- **`cpp:S134`** — control-flow nesting depth exceeds 3 (`if|for|do|while|switch`)
- **`cpp:S3776`** — function cognitive complexity exceeds 25

All 32 issues are spread across three files:

| File | Issues |
|---|---|
| `src/simulation/Zoning.cpp` | 22 |
| `src/simulation/Population.cpp` | 8 |
| `src/simulation/CitySimulation.cpp` | 2 |

**Fix strategy:** extract deeply-nested inner blocks and oversized functions into
focused private helper methods. Zero gameplay behaviour changes — no simulation
logic is altered. The only functional change in this phase is adding the missing
`was_powered` and `was_water_covered` serialization fields to bring the save
format into compliance with `architecture/game-design/service-coverage.md`.

---

### Deliverables

---

#### 1. Fix `src/simulation/Zoning.cpp` — 22 issues

##### 1a. `Zoning::nearestRoadDistance` (S134 line 45)

The static function has four nested loops (footprint × footprint × rx × rz = depth 4).

- [ ] Add private static helper `nearestRoadFromCell(tiles, fx, fz, curMin) → int`
  that runs the inner `rx`/`rz` scan for a single footprint cell and returns the
  updated minimum distance.
- [ ] Refactor `nearestRoadDistance` to call `nearestRoadFromCell` in the footprint
  loops, reducing nesting to ≤ 3.
- [ ] Add declaration to `Zoning.h` private section.

##### 1b. `Zoning::getServiceCoverage` (S134 line 239)

Three nested loops (service buildings × dz × dx) with an `if` guard inside = depth 4.

- [ ] Add private static helper `collectCoverageTiles(sb, radius) →
  std::vector<ServiceCoverageTile>` that runs the inner `dz`/`dx` loops and
  returns the tile list for one service building.
- [ ] Refactor `getServiceCoverage` to call `collectCoverageTiles` per building and
  append results, reducing nesting to ≤ 3.
- [ ] Add declaration to `Zoning.h` private section.

##### 1c. `Zoning::computePowerCoverage` (S3776 complexity 35, S134 lines 324–329)

The BFS expansion `for (int d = 0; d < 4; ...)` is nested inside `while → for(sb)`
reaching depth 4 with guarding `if` statements.

- [ ] Add private member helper `runPowerBfs(sb, bfsDepth&, maxDepth&) const` that
  seeds the BFS queue from the service building footprint and runs the full BFS
  expansion, returning the populated `bfsDepth` map and `maxDepth`.
  - Internal nesting inside `runPowerBfs`: `for fdx → for fdz` (seed) and
    `while → for(d=0..3) → if` — all ≤ 3 levels.
- [ ] Refactor `computePowerCoverage` to call `runPowerBfs`, reducing its nesting
  and complexity to within limits.
- [ ] Add declaration to `Zoning.h` private section.

##### 1d. `Zoning::buildPowerCoverageCache` (S3776 complexity 47, S134 lines 395–432)

Same BFS pattern as above plus a radial fallback loop with depth-4 `if` guards.

- [ ] Refactor `buildPowerCoverageCache` to call `runPowerBfs` (from 1c above),
  eliminating the BFS nesting violations at lines 395–400.
- [ ] Add private member helper `addRadialFallbackCoverage(sb, radiusTiles,
  bfsDepth)` that runs the outer `dx`/`dz` radial scan and inserts keys into
  `m_powerCoverageCache`, keeping nesting ≤ 3.
- [ ] Refactor `buildPowerCoverageCache` to call `addRadialFallbackCoverage`,
  eliminating violations at lines 429–432.
- [ ] Add `addRadialFallbackCoverage` declaration to `Zoning.h` private section.

##### 1e. `Zoning::applyDesirabilityScores` (S3776 complexity 112, S134 lines 496–630)

The largest function in `Zoning.cpp`. Outer for-tile loop with deep residential
neighbour scan, service coverage checks, water/power state updates, and alert firing.

- [ ] Add `computeNeighborDesirabilityDelta(x, z) const → float`:
  runs the 11×11 neighbour scan and returns the net desirability delta.
  Max internal nesting: `for dz → for dx → if` = 3. ✓
- [ ] Add `computeFirePoliceCoverageGap(x, z, hasFireStation, hasPolice) const → bool`:
  checks ONLY fire and police radial coverage and returns whether either service
  is uncovered for tile (x, z). This helper does NOT check water or power
  coverage — those are the exclusive responsibility of `updateWaterState` and
  `updatePowerState` (see below). No ifs nested beyond L1 inside the helper. ✓
- [ ] The all-services-absent short-circuit is a **caller** concern: in the
  refactored `applyDesirabilityScores`, the caller checks
  `if (!hasFireStation && !hasPolice && !hasWater && !hasPower)` and, if true,
  sets `anyUncovered = true` without calling `computeFirePoliceCoverageGap`,
  `updateWaterState`, or `updatePowerState`. This avoids double-counting and
  keeps the guard visible at the call site.
- [ ] Add `updateWaterState(tile&, x, z, hasWater, anyUncovered&, audio)`:
  computes water coverage, updates `tile.wasWaterCovered`, sets `anyUncovered`,
  plays `SFX_WATER_OUT` if coverage just dropped. No ifs beyond L1. ✓
  **Water uncovered contributions belong exclusively to this helper** — no other
  helper or caller logic may duplicate water coverage checks.
- [ ] Add `updatePowerState(tile&, x, z, hasPower, anyUncovered&, audio)`:
  same pattern for power coverage cache. No ifs beyond L1. ✓
  **Power uncovered contributions belong exclusively to this helper** — no other
  helper or caller logic may duplicate power coverage checks.
- [ ] Add `fireDesirabilityAlert(tile&, x, z, hasFireStation, hasPolice, audio)`:
  checks `tile.alertFired`; if `hasFireStation` plays `SFX_FIRE_ALERT`, else if
  `hasPolice` plays `SFX_POLICE_ALERT` (fire station takes priority per
  `architecture/game-design/service-coverage.md`); sets `tile.alertFired = true`.
  (`hasFireStation` and `hasPolice` passed to this helper are the **city-wide
  service existence booleans** produced by `buildServiceCoverageMap`, NOT per-tile
  radial coverage values — per-tile fire/police coverage is handled by
  `computeFirePoliceCoverageGap`. This city-wide boolean behavior matches the
  existing implementation and is intentionally preserved unchanged by this
  zero-behavior-change phase; the spec-vs-implementation misalignment — where
  `architecture/game-design/service-coverage.md` specifies per-tile radial
  coverage for alert SFX — is a pre-existing deviation that is out of scope
  here and should be tracked as future work.)
  Similarly, the spec (`architecture/game-design/service-coverage.md`) attributes
  `SFX_POWER_OUT` and `SFX_WATER_OUT` playback to `Zoning::doServiceDegradationTick()`,
  but these calls actually reside in `Zoning::applyDesirabilityScores()`. This phase
  correctly wraps them into the `updateWaterState` and `updatePowerState` helpers (both
  called from `applyDesirabilityScores`); the spec-side attribution error was corrected
  separately and is not a deliverable of this phase.
  No nesting beyond L1. ✓
- [ ] Refactor `applyDesirabilityScores` to call these helpers. Final shape:
  - Outer for-tile loop (L1) → if Residential (L2) → helper calls (no nesting) ✓
  - All-services-absent guard at call site (L2):
    `if (!hasFireStation && !hasPolice && !hasWater && !hasPower) { anyUncovered = true; }`
    else call `computeFirePoliceCoverageGap`, `updateWaterState`,
    `updatePowerState` individually — each contributes to `anyUncovered`. ✓
  - Inline uncovered penalty: merge double-nested `if anyUncovered → if !firstTick`
    into single `if (anyUncovered && !tile.firstDesirabilityTick)` to stay ≤ 3. ✓
  - Service-recovery `else if` branch: the
    `else if (!anyUncovered && (hasFireStation || hasPolice || hasWater || hasPower)) { desirability += SimulationConstants::service_recovery_desirability_per_tick; }`
    branch that immediately follows the penalty condition in the existing code must be
    preserved unchanged. This implements the service-coverage recovery rate
    (ref: `architecture/game-design/service-coverage.md`). ✓
  - Alert block at end: `if (isZoned&&Residential&&audio)` (L2) →
    `if (desirability <= threshold)` (L3) → `fireDesirabilityAlert(...)` (no deeper nesting);
    the `else` branch (desirability > threshold) must reset `tile.alertFired = false`
    in the CALLER — NOT inside `fireDesirabilityAlert`. This reset allows alerts to
    re-fire in future desirability-collapse episodes. Final shape:
    `if (desirability <= threshold) { fireDesirabilityAlert(...); } else { tile.alertFired = false; }` ✓
- [ ] Add all five new declarations to `Zoning.h` private section.

##### 1f. `Zoning::doServiceDegradationTick` (S134 line 630)

`if (roll < threshold)` is at depth 4 inside:
`if (budget ≤ threshold)` → `for idx` → `if (!sb.degraded)` → `if (roll <...)`.

- [ ] Add `tryDegradeService(sb&, rng, audio, notifications)`: checks `sb.degraded`,
  rolls probability, sets `sb.degraded = true`, plays `SFX_SERVICE_DEGRADE`, pushes
  `ServiceDegraded` notification. Max nesting inside: `if → if` = 2. ✓

  **PowerPlant exception (MUST remain in caller)**: The
  `if (sb.type == ServiceBuildingType::PowerPlant) { sb.degraded = true; continue; }`
  guard at the top of the loop must remain in the CALLER, executing BEFORE
  `tryDegradeService` is called. `tryDegradeService` is called only for radius-based
  services (FireStation, PoliceStation, WaterTower). PowerPlant degradation is
  deterministic (no RNG roll, no SFX\_SERVICE\_DEGRADE) per the service-coverage spec
  (`architecture/game-design/service-coverage.md`).

- [ ] Refactor `doServiceDegradationTick` to call `tryDegradeService` instead of the
  inline `if (!sb.degraded)` block. Nesting reduced to ≤ 3. ✓
- [ ] Add `tryDegradeService` declaration to `Zoning.h` private section.

---

#### 2. Fix `src/simulation/Population.cpp` — 8 issues

##### 2a. `Population::doPopulationTick` (S134 lines 143, 147)

`if (baseName.size() >= 2 && variantNum...)` and `if (renderer)` sit at depth 4 inside:
`for tiles → if underConstruction → if demand ≥ threshold → if ...`.

- [ ] Add private static helper `completeConstruction(tile&, key, traffic, renderer)`:
  checks demand threshold; if met, clears `underConstruction`, builds the asset name
  with variant suffix, calls `renderer->placeBuildingMesh`. Max nesting: L1 = 1. ✓
- [ ] Refactor `doPopulationTick` to call `completeConstruction` inside the
  `if (tile.underConstruction && footprintOriginX == -1)` block.
  Nesting in caller: `for → if` = 2. ✓
- [ ] Add `completeConstruction` declaration to `Population.h` private section
  (as `static`).

##### 2b. `Population::applyDensityUpgrade` (S3776 complexity 101, S134 lines 225, 228, 261, 273)

Two violation clusters:

**Blocker-scan (lines 225/228)** — `if` statements inside
`for dx → for dz → if isZoned` reach depth 4.

- [ ] Add file-scope (anonymous namespace in `Population.cpp`) helper struct
  `DemoEntry { int x; int z; int64_t originKey; }` (currently defined inline).
- [ ] Add file-scope static helper
  `checkZonedNeighbor(tiles, ft, fx, fz, candKey, targetZone, targetDensity,
  toDemo&) → bool`:
  examines one zoned footprint cell; returns `true` if it is a blocker, or
  `false` (and optionally appends to `toDemo`) if demotable. Max nesting: 2. ✓
- [ ] Refactor the `for dx / for dz` scan in `applyDensityUpgrade` to call
  `checkZonedNeighbor` for the `if (ft.isZoned)` branch.
  Nesting in caller: `for → for → if isZoned` = 3. ✓

**Demo-clearance (lines 261/273)** — `if tileIt == end` and `if !insideNewFP`
inside `for de → for ddx → for ddz` reach depth 4.

- [ ] Add file-scope struct `OuterTile { int x; int z; ZoneType zone; }` (currently
  inline).
- [ ] Add file-scope static helper
  `clearFootprintCell(tiles&, cellX, cellZ, tx, tz, newN, outerTiles&)`:
  looks up one tile, computes `insideNewFP`, resets tile fields, appends to
  `outerTiles` if outside. Max nesting: 1. ✓
- [ ] Refactor the `for de / for ddx / for ddz` demo loop to call
  `clearFootprintCell`. Nesting in caller: `for → for → for` = 3. ✓

##### 2c. `Population::doDensityUnlockTick` (S3776 complexity 35)

The function mixes a `switch` for scale, threshold-checking, and the upgrade wave
loop, pushing complexity over 25.

- [ ] Add private static helper `difficultyToUnlockScale(Difficulty) → float`:
  one-switch function returning the correct scale constant.
- [ ] Add private member helper `tickUnlockProgress(economy, scale)`:
  the `for (int i = 0; i < 6; ...)` threshold-check loop that advances
  `consecutive_months_above_threshold` and sets `unlock_flags`. Complexity ≈ 8. ✓
- [ ] Add private member helper `processUpgradeWave(zoning, traffic,
  wasAlreadyUnlocked*, renderer, audio, sfxCallsThisTick&, notifications)`:
  the second `for (int tierIdx = 0; ...)` loop that scans candidates and calls
  `applyDensityUpgrade`. Complexity ≈ 10. ✓
- [ ] Refactor `doDensityUnlockTick` to call the three helpers.
  Remaining complexity ≈ 4 (well under 25). ✓
- [ ] Add all three declarations to `Population.h` private section.

---

#### Intermediate coverage gate

- [ ] **Intermediate coverage gate**: After completing all helper extractions
  (Deliverables 1 and 2), run `make test` locally and verify the per-file 85%
  coverage floor for `Zoning.cpp`, `Population.cpp`, and `CitySimulation.cpp`
  (using the `awk` pipeline from `architecture/testing/coverage.md`). If any file
  drops below 85%, add targeted unit tests for the newly extracted helpers before
  proceeding to Deliverable 3.

---

#### 3. Fix `src/simulation/CitySimulation.cpp` — 2 issues

##### 3a. Border-ring terrain flattening (S134 line 385)

`if (bit != m_zoning.m_tiles.end() && bit->second.isRoad)` is at depth 4 inside:
`if (m_terrain) → for dx → for dz`.

- [ ] Introduce a local lambda `flattenIfRoad(bx, bz)` immediately inside the
  `if (m_terrain)` block. The lambda looks up the tile and calls
  `m_terrain->setTileHeight` if it is a road tile. Max nesting inside lambda: 1. ✓
- [ ] Replace the inline `auto bit = ...` + `if (bit != ...)` block with a call to
  the lambda. Nesting in caller: `if → for → for` = 3. ✓
- [ ] No header change required (lambda is local).

##### 3b. `CitySimulation::deserializeFromJson` + `serializeToJson` — replace hand-rolled parser with `nlohmann/json`

The manual JSON parser in `deserializeFromJson` has cognitive complexity 598 — almost
entirely because three nested object loops (tiles array, service\_buildings, scenario
state) are inlined in the dispatch `while`. Splitting into helpers reduces complexity
but leaves brittle hand-rolled code. The correct fix is to use a proven,
platform-independent library.

**Library chosen:** `nlohmann-json` (vcpkg port `nlohmann-json`, header-only,
MIT licence, cross-platform Linux/Windows, CMake target
`nlohmann_json::nlohmann_json`). Already present at the current vcpkg baseline
`b2f068faf45a3f04145bec0f52a66526ad590227`.

- [x] Add `"nlohmann-json"` to the `dependencies` array in `vcpkg.json`.
- [ ] Add to `CMakeLists.txt` (near the other `find_package` calls):

  ```cmake
  find_package(nlohmann_json CONFIG REQUIRED)
  ```

  and link `nlohmann_json::nlohmann_json` to `aitown_sim` (the target that
  compiles `CitySimulation.cpp`) via
  `target_link_libraries(aitown_sim PRIVATE nlohmann_json::nlohmann_json)`.
- [ ] Add `nlohmann_json::nlohmann_json` to
  `target_link_libraries(simulation_tests PRIVATE ...)` in `CMakeLists.txt`.
  The `aitown_sim` target links `nlohmann_json::nlohmann_json` as `PRIVATE`,
  so the include path does NOT propagate to `simulation_tests`. Without this
  explicit link, any test file using `#include <nlohmann/json.hpp>` for
  programmatic JSON manipulation (e.g., `nlohmann::json::parse()` + field
  mutation + `j.dump()`) will fail to compile with a "file not found" error.
- [ ] Verify `architecture/testing/framework.md` already includes
  `nlohmann_json::nlohmann_json` in the `target_link_libraries(simulation_tests
  PRIVATE ...)` example (added as part of Phase 11q2 spec preparation); if it is
  already present, no spec change is needed; if absent, add it with a comment
  noting Phase 11q2+ programmatic JSON manipulation.
- [x] Rebuild the CI Docker image with `nlohmann-json` baked in. Linux CI runs with
  `VCPKG_MANIFEST_INSTALL=OFF` and relies on packages installed at image build
  time; without a rebuilt image, `find_package(nlohmann_json)` will fail in the
  `build-linux`, `test-linux`, and `coverage-linux` jobs. Because
  `docker-ci-image.yml` only auto-triggers on pushes to `main` or `develop`
  (not feature branches), and `workflow_dispatch` checks out the default branch
  (not the caller's branch), this requires **two-step merge sequencing**:

  **Step 1 — preparatory PR to `develop`**: Open a small PR to `develop`
  containing ONLY the `vcpkg.json` change that adds `"nlohmann-json"` to the
  `dependencies` array. Once this PR merges, `docker-ci-image.yml` auto-triggers
  and publishes a new CI image with `nlohmann-json` baked in.

  **Before opening the preparatory PR** perform both of these checks:

  1. Confirm that `docker/ci-linux/Dockerfile` installs packages in vcpkg
     manifest mode (i.e., it reads `vcpkg.json` during the Docker build). If the
     Dockerfile uses an explicit `vcpkg install <port-list>` invocation rather
     than manifest mode, that explicit list must also be updated to include
     `nlohmann-json` alongside the `vcpkg.json` change.
  2. Verify that `nlohmann-json` is available at the `msvc.yml` `VCPKG_COMMIT_ID`
     baseline by running
     `gh api "/repos/microsoft/vcpkg/contents/ports/nlohmann-json?ref=<VCPKG_COMMIT_ID>"`
     (substitute the actual commit ID from `.github/workflows/msvc.yml`). This
     check is required because `msvc.yml` triggers on `develop` pushes — once
     Step 1 merges the updated `vcpkg.json` to `develop`, `msvc.yml` will
     attempt to install `nlohmann-json` at its own independent baseline. If the
     port is absent, `msvc.yml` will fail on every `develop` push until a
     baseline bump is landed. If the port is NOT present, bump the `msvc.yml`
     `VCPKG_COMMIT_ID` to a baseline that includes `nlohmann-json` as part of
     the Step 1 preparatory PR.

  **Step 2 — main phase PR**: After the new image digest is published, deliver
  all remaining code changes (nlohmann migration in `CitySimulation.cpp`,
  `CMakeLists.txt` `find_package`/`target_link_libraries` update, test updates,
  helper extractions) in the main phase PR. This PR must also update the
  container digest pins:
  - `.devcontainer/Dockerfile` `FROM` line (digest-only — no tag; see CLAUDE.md atomicity item 4)
  - `.github/workflows/_build-linux.yml` `container: image:` digest (line 25)
  - `.github/workflows/_coverage-linux.yml` `container: image:` digest (line 22)
  - `.github/workflows/_test-linux.yml` `container: image:` digest (line 40)

  **Pre-merge check -- msvc.yml baseline**: `msvc.yml` has its own independent
  `VCPKG_COMMIT_ID` baseline pin and does not use a container image (no digest
  to update). Before merging Step 2, verify that `nlohmann-json` is available
  at the `VCPKG_COMMIT_ID` commit used in `msvc.yml` by checking
  `gh api "/repos/microsoft/vcpkg/contents/ports/nlohmann-json?ref=<VCPKG_COMMIT_ID>"`.
  If the port is present, no baseline bump is needed for `msvc.yml`. (This
  mirrors the `irrlicht` port verification pattern used in prior baseline bumps.)
  Since `msvc.yml` uses manifest mode with `vcpkg.json`, adding `nlohmann-json`
  to `vcpkg.json` is sufficient — no changes to `msvc.yml` are needed provided
  the port exists at the pinned baseline (confirmed by the `gh api` check above).

  (`ci.yml` itself contains no `container: image:` lines — the digest pins
  live exclusively in the reusable workflows above.)
  Use digest-only format (`ghcr.io/m0wa/aitown-ci-linux@sha256:<digest>`) per
  CLAUDE.md atomicity item 4 (the `:tag@sha256:digest` double-separator breaks
  Docker layer caching).
- [ ] Add `#include <nlohmann/json.hpp>` in `CitySimulation.cpp`; remove the entire
  hand-rolled anonymous namespace (`skipWs`, `expect`, `parseString`, `parseInt64`,
  `parseFloat`, `parseBool`, `parseKey` plus any helpers).
- [ ] Rewrite `deserializeFromJson` using the nlohmann API:
  - Parse with `nlohmann::json::parse(json, nullptr, /*exceptions=*/false)` and
    check `.is_discarded()` for the error path.
  - Access fields using three tiers based on their presence in the save format history:
    - **Tier 1 — Required-since-schema-v1** (documented as "MUST include" in
      `architecture/game-design/save-system.md`; present in every valid save since
      the game launched): use `.at("key").get<T>()` inside a try/catch block. In the
      catch, set `errorOut = "missing or invalid <key>"` and return. This preserves
      existing error-detection behavior — a corrupted save missing `treasury_balance`
      returns `LoadResult::Corrupted` instead of silently loading with $0. Top-level
      Tier 1 fields: `treasury_balance`, `tax_rates`, `tiles` (array),
      `service_buildings` (array), `outstanding_bond_uses`,
      `consecutive_deficit_months`, `total_ticks`, `month`, `year`,
      `outstanding_debt`. Core per-tile fields (`zone`, `density`, `is_zoned`,
      `is_road`, `population`, `desirability`) also use `.at()` within the tile
      loop. `schema_version` is a special case: it uses `.at()` for the early
      version-check and the function returns immediately if missing or wrong.
    - **Tier 2 — Added in later phases, required in new saves, may be absent in
      very old saves**: use `j.value("key", defaultValue)`. Fields:
      `speed_multiplier`, `population_milestone_fired`, `building_variant_counters`,
      `fp_origin_x` / `fp_origin_z` per tile, `is_abandoned`, `under_construction`,
      `building_variant_num`, `alert_fired` (default `false` — already written by
      the hand-rolled serializer; `j.value()` handles any very old saves that
      predate it).
    - **Tier 3 — New in this phase** (absent in ALL pre-Phase-11q2 saves; the
      hand-rolled serializer never wrote these keys): use
      `j.value("key", defaultValue)` with the spec-defined default.
      Fields: `was_powered` (default `true`), `was_water_covered` (default `true`),
      per the Per-Tile Audio Transition Fields table in
      `architecture/game-design/service-coverage.md`.

    **Important**: Tier 1 fields use `.at()` to preserve the existing
    error-detection contract — changing these to `j.value()` would silently accept
    corrupted saves that the current implementation correctly rejects. Only Tier 2
    and Tier 3 fields (added progressively across phases) use `j.value()`. Tiers
    2 and 3 are mutually exclusive: Tier 2 fields were written by the hand-rolled
    serializer; Tier 3 fields (`was_powered`, `was_water_covered`) are new to
    this phase. This distinction upholds the "zero gameplay behaviour changes"
    goal for both valid and corrupted save handling.
  - Iterate tile/service-building arrays with a range-for.
  - Target cognitive complexity ≤ 25 (all nesting is flat key-access, no manual loops
    over characters). ✓
- [ ] Rewrite `serializeToJson` using the nlohmann API (`nlohmann::json j; j["key"] = value; … return j.dump(2);`), replacing the current hand-built string concatenation.
  (`serializeToJson` has no SonarCloud violations today but the manual string building
  is equally fragile — replacing both sides is the cleaner delta.)
  - Note: the nlohmann `serializeToJson` rewrite MUST add `"was_powered"` and `"was_water_covered"` to the per-tile output alongside the existing `"alert_fired"` field — the hand-rolled serializer omits these two fields, making this a net-new addition that brings the serializer into compliance with `architecture/game-design/service-coverage.md` (Per-Tile Audio Transition Fields: "All three must be serialised in the save file to prevent spurious SFX re-fire on load"). These fields are written for **every tile** in the tiles array (not filtered by zone type), because `TileData` defines `wasPowered` and `wasWaterCovered` for all zone types in `Zoning.h`. Corresponding deserialization must use `j.value("was_powered", true)`, `j.value("was_water_covered", true)`, and `j.value("alert_fired", false)` (backward-compatible defaults — `alertFired` defaults to `false` per the Per-Tile Audio Transition Fields table in `architecture/game-design/service-coverage.md`, ensuring the first-alert SFX is not suppressed on tiles loaded from pre-Phase-10 saves).
- [x] Update `.devcontainer/Dockerfile` to ensure `nlohmann-json` is available in
  the dev container (it is installed via vcpkg during the container build, so no
  extra system package is needed — verify the container build still passes).
- [ ] No header change to `CitySimulation.h` required; the JSON functions keep the
  same signatures.
- [ ] Review all `Serialize_*` / `Deserialize_*` unit tests in **all** test files
  that call `serializeToJson()` or `deserializeFromJson()`. At minimum the
  following files require review:
  - `tests/simulation/city_simulation_extra_test.cpp`
  - `tests/simulation/simulation_comprehensive_integration_test.cpp`
  - `tests/simulation/city_simulation_reset_test.cpp`
  - `tests/simulation/save_system_test.cpp`
  - `tests/simulation/save_system_integration_test.cpp`

  **Blocking prerequisite** — before starting ANY test file updates, run
  `grep -rn "serializeToJson\|deserializeFromJson" tests/` to discover any
  additional callers not listed above. Every file discovered by this grep MUST
  be reviewed and updated before the test update work proceeds; do not begin
  modifying test files until the full caller set is known. If the grep
  discovers callers in `tests/integration/`, `nlohmann_json::nlohmann_json`
  must also be added to
  `target_link_libraries(integration_tests PRIVATE nlohmann_json::nlohmann_json)`
  in `CMakeLists.txt` — the `aitown_sim` PRIVATE link does not propagate
  include paths, so integration test files using `#include <nlohmann/json.hpp>`
  for programmatic JSON manipulation will fail to compile without this
  explicit link (same rationale as the `simulation_tests` link above).

  The review must cover four categories of test changes:

  1. **Error-message assertions**: update tests that assert specific hand-rolled
     parser error messages (e.g. expected-token strings from `expect()`,
     `parseString()`, `parseInt64()`) to match
     `nlohmann::json::exception::what()` output.
  2. **Hand-rolled parser helper tests**: rewrite or remove tests that exercise
     parser helpers (`skipWs`, `expect`, `parseString`, `parseInt64`,
     `parseFloat`, `parseBool`, `parseKey`) that no longer exist, replacing
     them with equivalent behavioural tests that verify the same
     serialisation/deserialisation contracts through the nlohmann API.
  3. **Format-dependent string manipulation tests**: review and update ALL tests
     that call `serializeToJson()` and then manipulate the output via string
     `find`/`replace`/`insert` patterns (e.g. `json.find("\"tiles\": [\n")`,
     `json.find("{\"x\":")`, `patchUnlockFlags()` helpers).
     With `nlohmann::json::dump(2)`, the output format (indentation, whitespace,
     key ordering) differs from the hand-rolled serializer, causing these
     patterns to silently miss through `if (pos != std::string::npos)` guards
     and pass vacuously without exercising the intended deserialization contract.
     Replace fragile string-pattern JSON manipulation with the programmatic
     approach:

     ```cpp
     nlohmann::json j = nlohmann::json::parse(cs()->serializeToJson());
     j["field"] = newValue;
     cs()->deserializeFromJson(j.dump(), err);
     ```

     This is format-independent and consistent with the nlohmann migration.
     `simulation_comprehensive_integration_test.cpp` contains ~7 serialization
     round-trip tests (`SerializeDeserialize_EmptyCity_RoundTrips`,
     `WithZones_TreasuryPreserved`, `TaxRates_Preserved`,
     `SimulationTime_Preserved`, `Corruption_ReturnsFalse`, and ~2
     `applyLoadedJson` tests) that must be verified for format-dependent
     assertions.
  4. **Backward-compatible default and round-trip verification for
     `was_powered` / `was_water_covered`**: Deliverable 3b adds `was_powered`
     and `was_water_covered` as net-new fields in `serializeToJson`. Add at
     least the following new tests:
     - **Missing-key defaults**: Deserialize a JSON string that contains a
       valid tile entry but LACKS the `was_powered` and `was_water_covered`
       keys entirely (simulating a save from a pre-Phase-11q2 game version).
       Assert the loaded tile has `wasPowered == true` and
       `wasWaterCovered == true` — the backward-compatible defaults specified
       in the Per-Tile Audio Transition Fields table of
       `architecture/game-design/service-coverage.md`.
     - **Round-trip (true case)**: Construct a city state where a tile has
       `wasPowered = true` and `wasWaterCovered = true`, serialize with
       `serializeToJson`, deserialize the output with `deserializeFromJson`,
       and assert the values survive the cycle unchanged.
     - **Round-trip (false case)**: Same as above but with
       `wasPowered = false` and `wasWaterCovered = false`, verifying the
       non-default values also round-trip correctly.

     **Test file placement**: Place the three new tests (`missing-key defaults`,
     `round-trip true case`, `round-trip false case`) in
     `tests/simulation/save_system_real_test.cpp` — it already contains the
     `SaveSystemWithSimTest` fixture providing a real `CitySimulation` instance
     wired with mocks, which is exactly what these tests need to call
     `serializeToJson`/`deserializeFromJson` on. Do **not** place them in
     `save_system_test.cpp` (its anonymous-namespace stub classes
     `ICitySimulationSerializable` / `ISaveSystem` conflict with the real
     headers) or `save_system_integration_test.cpp` (that file belongs to the
     `integration_tests` CMake target with label `integration`, whereas these
     are unit-level tests under `simulation_tests` with label `unit`). Per
     `architecture/testing/coverage.md` Coverage Test Placement Convention and
     the stub/real split exception (lines 321-325) -- do **not** create a new
     standalone or gap-style test file for these tests.

---

#### 4. Update `Zoning.h` and `Population.h`

- [ ] Add a `private:` section to `Zoning.h` (currently has none) with declarations
  for all new helpers added in section 1.
- [ ] Add declarations for the helpers added in section 2c to the existing `private:`
  section of `Population.h`.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q2.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` passes with zero new warnings or errors.
- [ ] `ctest -LE "integration|requires-opengl"` — zero regressions.
- [ ] `ctest -L "^integration$"` — zero regressions.
- [ ] `xvfb-run --auto-servernum ctest -L "^requires-opengl$"` — zero regressions.
- [ ] SonarCloud re-scan on `fix/phase-11q2` shows all 32 open HIGH issues resolved:
  - `cpp:S134` violations gone from `Zoning.cpp`, `Population.cpp`,
    `CitySimulation.cpp`.
  - `cpp:S3776` violations gone from `Zoning::applyDesirabilityScores` (was 112),
    `Zoning::buildPowerCoverageCache` (47), `Zoning::computePowerCoverage` (35),
    `Population::applyDensityUpgrade` (101), `Population::doDensityUnlockTick` (35),
    `CitySimulation::deserializeFromJson` (598).
- [ ] `nlohmann-json` compiles and links on both Linux (CI `build-linux` job) and
  Windows (CI `build-windows` job) without additional system packages.
- [ ] All existing JSON serialisation tests pass: `ctest -R "Serialize|Deserialize"`
  — zero failures; any test whose assertions depended on hand-rolled parser error
  messages has been updated to match nlohmann error output.
- [ ] `make test` passes (enforces >=95% total line coverage gate per `architecture/testing/coverage.md`).
- [ ] Per-file 85% floor check passes for all modified `src/simulation/` files (`Zoning.cpp`, `Population.cpp`, `CitySimulation.cpp`) — run the `awk` pipeline from `architecture/testing/coverage.md` against `coverage_filtered.info`.
