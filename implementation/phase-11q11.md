## Phase 11q11: Demand-Proportional Building Upgrade Speed

**Status: TODO**

**Prerequisite**: phase-11q10 merged.

### Goal

Building density upgrades currently fire up to 20% of all eligible zone tiles per budget
tick the moment demand clears the gate threshold — regardless of whether demand is 0.51 or
1.0. A city popping above the threshold for the first time can see dozens of buildings
transform in a single month, which feels abrupt and unrealistic.

This phase replaces the fixed 20%-cap with a **demand-scaled rate**: the number of tiles
that can upgrade in a single budget tick rises linearly from 0% (at the threshold) to 20%
(at full demand). Low-demand cities experience a slow, organic trickle of upgrades; only
cities with consistently high demand see rapid neighbourhood densification.

---

### Root Cause

`Population::processUpgradeWave()` (`src/simulation/Population.cpp`, lines ≈ 497–502)
computes the per-tick upgrade quota as a flat fraction of total zone tiles:

```cpp
int maxUpgrades = static_cast<int>(
    SimulationConstants::density_max_upgrade_rate_per_tick * static_cast<float>(totalZoneTiles));
maxUpgrades = std::max(1, maxUpgrades);
```

The demand value (`demandForZone`) is only used as a binary gate — if it is below
`density_upgrade_wave_demand_threshold` (0.50) the loop is skipped; otherwise the full
20%-cap applies immediately. The actual magnitude of demand has no effect on upgrade speed.

---

### Fix

#### 1. Scale upgrade quota by demand in `Population::processUpgradeWave()`

File: `src/simulation/Population.cpp` — inside the `for (int tierIdx = 0; ...)` loop,
after the `totalZoneTiles` count, replace the fixed-rate `maxUpgrades` block.

**Before:**

```cpp
        int maxUpgrades = static_cast<int>(
            SimulationConstants::density_max_upgrade_rate_per_tick * static_cast<float>(totalZoneTiles));
        maxUpgrades = std::max(1, maxUpgrades);
```

**After:**

```cpp
        // Scale upgrade quota linearly by how far demand exceeds the gate threshold.
        // demandExcess == 0 at threshold → near-zero rate (trickle of 1 tile).
        // demandExcess == 1 at peak demand → full density_max_upgrade_rate_per_tick.
        float demandExcess = (demandForZone - SimulationConstants::density_upgrade_wave_demand_threshold)
                             / (1.0f - SimulationConstants::density_upgrade_wave_demand_threshold);
        demandExcess = std::min(1.0f, demandExcess);   // hard cap: never exceed peak-demand rate
        float scaledRate  = demandExcess * SimulationConstants::density_max_upgrade_rate_per_tick;
        int maxUpgrades   = std::max(1, static_cast<int>(scaledRate * static_cast<float>(totalZoneTiles)));
```

No other source files require changes. The two existing constants retain their current
values and semantics:

| Constant                                | Value   | Meaning after this change                         |
| --------------------------------------- | ------- | ------------------------------------------------- |
| `density_upgrade_wave_demand_threshold` | `0.50f` | Minimum demand for the wave to fire at all (gate) |
| `density_max_upgrade_rate_per_tick`     | `0.20f` | Upgrade rate fraction at **peak** demand (1.0)    |

---

#### 2. Update `population-density-growth.md`

File: `architecture/game-design/population-density-growth.md` — append a row to the
`SimulationConstants Mapping` table:

| Constant name                                                | Value   | Spec meaning                                                                                                                                                                         |
| ------------------------------------------------------------ | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `SimulationConstants::density_upgrade_wave_demand_threshold` | `0.50f` | Gate: upgrade wave does not fire when demand is below this value. Also the lower bound for the demand-scaled rate formula (excess = 0 at this value).                                |
| `SimulationConstants::density_max_upgrade_rate_per_tick`     | `0.20f` | Maximum fraction of zone tiles that may upgrade in one budget tick; reached only when demand == 1.0. At demand == threshold the effective rate approaches 0 (min-clamped to 1 tile). |

Also, replace the prose sentence in the **Demand-to-density gating** bullet that currently
states the upgrade rate is a fixed 20% cap with a sentence describing the demand-scaled
formula:

> The number of tiles that may upgrade in a single budget tick scales linearly with demand:
> `rate = ((demand − threshold) / (1.0 − threshold)) × density_max_upgrade_rate_per_tick`,
> clamped to a minimum of 1 tile. At demand = 0.50 (threshold) the wave fires with rate ≈ 0
> (1-tile trickle); at demand = 1.0 the full 20% rate applies.

**Also in the Demand-to-density gating bullet of `population-density-growth.md`, update the eligibility sentence:**

> **Before:** "Tiles only auto-upgrade density when the demand factor for that zone type > 0.75 and the appropriate unlock has been achieved."
>
> **After:** "Tiles only auto-upgrade density when the zone-level demand factor for that zone type ≥ `density_upgrade_wave_demand_threshold` (0.50) and the appropriate density tier has been unlocked. **V1 note:** `architecture/game-design/zoning-system.md` documents a per-tile upgrade eligibility threshold of 0.75 (a stricter per-tile concept, distinct from the zone-level wave gate); this per-tile threshold is a design concept deferred to post-V1 and is not currently implemented as a code constant or check in `Population::scanUnlockCandidates()`."

**Note:** There is only one demand threshold constant in the codebase: `density_upgrade_wave_demand_threshold = 0.50f` (the zone-level wave gate). The 0.75 threshold mentioned in `zoning-system.md` is a design concept but is NOT implemented as a code constant in V1; this phase does not need to add it.

---

#### 3. Update `economy-model.md`

File: `architecture/game-design/economy-model.md` — find the sentence near line 41 that
states "at most 20% of eligible tiles per zone type" (the fixed-cap language) and replace
it with language describing the demand-scaled rate:

> **Before:** "…up to 20% of eligible tiles per zone type per tick can be upgraded…"
> **After:** "…the fraction of eligible tiles that can upgrade per tick scales linearly with
> demand excess above the gate threshold: from a minimum of 1 tile at demand = 0.50 up to 20%
> at peak demand (1.0), with a floor of 1 upgrade per zone type per tick…"

Additionally, update the same paragraph's second sentence from:

> **Before:** "The **20% cap** is applied independently to each zone type (R, C, I) — upgrading Residential tiles does not count against the Commercial tile cap."
> **After:** "The demand-scaled rate (up to 20% at peak demand) is applied independently to each zone type (R, C, I) — upgrading Residential tiles does not count against the Commercial tile cap."

And update the third sentence from:

> **Before:** "…the transition smooths over approximately 5 budget ticks."
> **After:** "…the transition speed depends on demand — at peak demand (~20% rate) the transition takes approximately 5 budget ticks; at lower demand it proceeds more slowly."

---

#### 4. Update `zoning-system.md`

File: `architecture/game-design/zoning-system.md` — around line 145, find the parenthetical
"(at most 20% of eligible tiles per zone type per tick)" inside the "upgrade wave fires"
sentence and replace it with the demand-proportional description:

> **Before:** "upgrade wave fires (at most 20% of eligible tiles per zone type per tick)"
> **After:** "upgrade wave fires (fraction of eligible tiles scales with demand:
> `((demand − 0.50) / 0.50) × 20%`, clamped to at least 1 tile)"

**CRITICAL:** When replacing the line 145 text, the formula's threshold constant **MUST** be
`SimulationConstants::density_upgrade_wave_demand_threshold = 0.50f` (the actual V1 zone-level
wave gate). This is the **only** demand threshold implemented as a code constant in V1. Do NOT
reference the `0.75` per-tile threshold mentioned elsewhere in the specs — that is a post-V1
design concept (the per-tile eligibility check `demand_factor >= 0.75`) that is not currently
implemented as a code constant or check in `Population::scanUnlockCandidates()`. The 0.50
constant is the definitive V1 gate for both zone construction and upgrade wave firing.

**Also in the "Density upgrade wave re-evaluation" bullet at line ~145 of
`architecture/game-design/zoning-system.md`, replace the two per-tile eligibility references
that still use the stale `density_upgrade_threshold (0.75)` constant, which contradicts the
V1 note at lines 147-149:**

> **Before (first occurrence):** "A tile that met the `demand_factor >= SimulationConstants::density_upgrade_threshold` (0.75) criterion at tick N may not meet it at tick N+1 (if congestion or service degradation occurs during the wave)"
>
> **After:** "A tile that met the zone-level `demand_factor >= SimulationConstants::density_upgrade_wave_demand_threshold` (0.50) criterion at tick N may not meet it at tick N+1 (e.g. if congestion or service degradation occurs during the wave)"
>
> **Before (second occurrence):** "no remaining eligible tiles meet the `demand_factor >= density_upgrade_threshold` (0.75) criterion"
>
> **After:** "no remaining eligible tiles meet the zone-level `demand_factor >= SimulationConstants::density_upgrade_wave_demand_threshold` (0.50) criterion"

These two replacements align the per-tile eligibility prose with the V1 gate constant
(`density_upgrade_wave_demand_threshold = 0.50f`) and eliminate the contradiction with the
note at lines 147-149 that explicitly states the 0.75 threshold is deferred to post-V1.

**Also on line ~317 of `architecture/game-design/zoning-system.md`:**

> **Before:** "immediately after a tile's density tier is incremented and before the **20%-per-type cap counter** is updated."
> **After:** "immediately after a tile's density tier is incremented and before the per-zone-type upgrade counter is updated."

**Also in zoning-system.md at lines 147–149, replace the current note about "distinct constants" with:**

> **Note:** In V1, both zone construction and density upgrade waves use a unified zone-level gate at `SimulationConstants::density_upgrade_wave_demand_threshold = 0.50f`. The per-tile 0.75 threshold mentioned in Population Density & Growth specs is a design concept deferred to post-V1 and is not currently implemented in V1. This consolidation prevents stale references between `construction_delay_demand_threshold` and `density_upgrade_threshold` constants.

---

#### 5. Add tests in `tests/simulation/zoning_test.cpp`

Append two new tests at the end of the `ZoningTestNice` fixture.

**Test A — Low-demand trickle (≤ 2 upgrades per tick)**

```cpp
// TEST N: DensityUpgradeWave_LowDemand_UpgradesAtMostTwoTilesPerTick
//
// When demand is in [0.75, 0.80] — well above the wave gate
// (density_upgrade_wave_demand_threshold = 0.50) but near the low end of the available
// range, producing a near-minimum demand-scaled rate —
// the demand-scaled rate should be low, constraining upgrades to at most 2 tiles.
//
// Formula verification at demand = 0.78:
//   demandExcess = (0.78 − 0.50) / (1.0 − 0.50) = 0.56
//   scaledRate   = 0.56 × 0.20 = 0.112
//   maxUpgrades  = max(1, floor(0.112 × 20)) = max(1, 2) = 2
// Compare against the old fixed cap: max(1, floor(0.20 × 20)) = 4.
//
// Setup: unlock Med-R.
// Tile layout: 4×5 grid of R-Low tiles at columns 0–3, rows 0–4.
// Roads at (column -1, rows 0–4) and (column 4, rows 0–4) provide Low-density
// road range coverage for all 20 tiles. 12 valid 2×2 upgrade footprints are
// available (columns 0–2 × rows 0–3), guaranteeing maxUpgrades can be satisfied.
// REQUIRED: inject demand directly by calling
//   cs()->testSetZoneDemandFactor(ZoneType::Residential, 0.78f)
// This test-only seam (declaration guarded by #ifdef AITOWN_TESTING_ENABLED)
// lives on CitySimulation and forwards internally to
// Traffic::overrideZoneDemandFactor(zone, value).
// TearDown: call cs()->testSetZoneDemandFactor(ZoneType::Residential, 0.0f),
// or rely on the fixture's existing TearDown that destroys sim_.
// Do NOT rely on multi-tick zone-balance arithmetic to reach a specific demand
// value. Run 1 upgrade tick and check:
// Two-sided: lower bound confirms injection worked (wave fired); upper bound confirms demand-scaling is active.
// assert EXPECT_GE(upgradeCount, 1) — confirms the wave actually fired (demand seam worked and min-clamp applied)
// assert EXPECT_LE(upgradeCount, 2) — confirms the demand-scaled cap limited the wave to at most 2 tiles
```

**Test B — High-demand wave (near-max rate)**

```cpp
// TEST N+1: DensityUpgradeWave_HighDemand_UpgradesAtHighRate
//
// When demand is ≥ 0.90 the demand-scaled rate rises to ≥ 16%, producing a clearly
// larger upgrade batch than the low-demand trickle.
//
// Formula verification at demand = 0.92:
//   demandExcess = (0.92 − 0.50) / (1.0 − 0.50) = 0.84
//   scaledRate   = 0.84 × 0.20 = 0.168
//   maxUpgrades  = max(1, floor(0.168 × 20)) = max(1, 3) = 3
//
// Setup: unlock Med-R.
// Tile layout: 4×5 grid of R-Low tiles at columns 0–3, rows 0–4.
// Roads at (column -1, rows 0–4) and (column 4, rows 0–4) provide Low-density
// road range coverage for all 20 tiles. 12 valid 2×2 upgrade footprints are
// available (columns 0–2 × rows 0–3), guaranteeing maxUpgrades can be satisfied.
// REQUIRED: inject demand directly by calling
//   cs()->testSetZoneDemandFactor(ZoneType::Residential, 0.92f)
// This test-only seam (declaration guarded by #ifdef AITOWN_TESTING_ENABLED)
// lives on CitySimulation and forwards internally to
// Traffic::overrideZoneDemandFactor(zone, value).
// TearDown: call cs()->testSetZoneDemandFactor(ZoneType::Residential, 0.0f),
// or rely on the fixture's existing TearDown that destroys sim_.
// Do NOT rely on multi-tick zone-balance arithmetic to reach a specific demand
// value. Run 1 upgrade tick and assert:
//
// Two-sided: EXPECT_GE(upgradeCount, 3) AND EXPECT_LE(upgradeCount, 3)
//   Lower bound EXPECT_GE(upgradeCount, 3) — confirms high demand produced
//     at least 3 upgrades (more than the low-demand trickle which is ≤ 2).
//   Upper bound EXPECT_LE(upgradeCount, 3) — confirms the demand-scaled cap
//     limited the wave to exactly 3 tiles; old fixed-rate code (maxUpgrades =
//     max(1, floor(0.20 × 20)) = 4) would produce 4, which fails this bound.
```

Both tests use the `ZoningTestNice` fixture (existing `NiceMock` mocks).

---

### Affected files

| File                                                    | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/simulation/Population.cpp`                         | Replace fixed `maxUpgrades` formula with demand-scaled version                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| `architecture/game-design/population-density-growth.md` | Document demand-scaled rate; update constants table; and update eligibility prose sentence from 0.75 to `density_upgrade_wave_demand_threshold` (0.50) with V1/post-V1 disambiguation note                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `architecture/game-design/economy-model.md`             | Replace fixed-20%-cap language with demand-scaled formula description                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| `architecture/game-design/zoning-system.md`             | Replace line ~145 parenthetical with demand-proportional rate description; replace the two per-tile `density_upgrade_threshold (0.75)` references at line ~145 with `density_upgrade_wave_demand_threshold (0.50)` to align with V1 gate constant; update line ~317 "cap counter" reference; update lines 147-149 note about "distinct constants" to clarify V1 consolidation at 0.50 |
| `src/simulation/CitySimulation.h`                       | Add `testSetZoneDemandFactor(ZoneType, float)` declaration guarded by `#ifdef AITOWN_TESTING_ENABLED`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| `src/simulation/CitySimulation.cpp`                     | Implement `testSetZoneDemandFactor` guarded by `#ifdef AITOWN_TESTING_ENABLED` (both the `.cpp` body and the `.h` declaration are inside this guard); the body forwards to `m_traffic.overrideZoneDemandFactor(zone, value)` — safe to compile into `libaitown_sim.a` once the CMakeLists change below defines the flag                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `src/simulation/Traffic.h`                              | Add `overrideZoneDemandFactor(ZoneType, float)` (no preprocessor guard; only called from the one guarded `CitySimulation` method)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `src/simulation/Traffic.cpp`                            | Implement `overrideZoneDemandFactor`; stores override; `getZoneDemandFactor` returns override when set, otherwise computed value; reset by passing `0.0f` sentinel                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `tests/simulation/zoning_test.cpp`                      | Add two regression tests (low-demand trickle ≤ 2, high-demand wave ≥ 3)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `CMakeLists.txt`                                        | ADD a NEW `target_compile_definitions(aitown_sim PRIVATE AITOWN_TESTING_ENABLED=1)` line immediately before the existing `target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)` line (currently at line 625). Do NOT modify the existing `simulation_tests` definition; only ADD the `aitown_sim` line. Reason: test code linking `simulation_tests` includes `CitySimulation.h` which declares `testSetZoneDemandFactor(ZoneType, float)` guarded by `#ifdef AITOWN_TESTING_ENABLED`. For this method to be visible at compile time, BOTH the library (`aitown_sim`, which compiles `CitySimulation.cpp` with the guarded definition) AND the test target (`simulation_tests`, which includes the header) must have this flag set. This follows the framework.md Phase 11q6 pattern (CMakeLists lines 986–987) where both `aitown_audio` and `integration_tests` receive the flag. Update the comment on the compile-definitions block to clarify: "AITOWN_TESTING_ENABLED enables CitySimulation::testSetZoneDemandFactor(). MUST be set on both aitown_sim (compiled into library) and simulation_tests (test TU)." |
| `architecture/testing/testability-architecture.md`      | Document the new `CitySimulation::testSetZoneDemandFactor(ZoneType, float)` and `Traffic::overrideZoneDemandFactor(ZoneType, float)` test seams: signature, `#ifdef AITOWN_TESTING_ENABLED` guard, `0.0f` sentinel reset contract, and the requirement that both `aitown_sim` and `simulation_tests` targets have the `AITOWN_TESTING_ENABLED` flag set                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `architecture/testing/framework.md`                     | Add `target_compile_definitions(aitown_sim PRIVATE AITOWN_TESTING_ENABLED=1)` to the `simulation_tests` CMake example block alongside the existing `simulation_tests` definition, with a comment explaining both are required for `CitySimulation::testSetZoneDemandFactor`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |

No changes to `simulation_constants.h` (constants retain their values and names).
No changes to `Zoning.cpp` or any other file not listed above.

---

### Exit Criteria

- [ ] `make build` succeeds with no new warnings.
- [ ] `ctest -LE "integration|requires-opengl"` passes (all existing upgrade-wave tests
      still pass; two new tests pass).
- [ ] `ctest --test-dir build -R DensityUpgradeWave_LowDemand --output-on-failure` passes.
- [ ] `ctest --test-dir build -R DensityUpgradeWave_HighDemand --output-on-failure` passes.
- [ ] Manual play test: start a new city and zone a medium-sized residential area; observe
      that density upgrades arriving shortly after the Med-R unlock happen as a slow trickle
      (1–3 buildings per month) rather than a large simultaneous wave.
- [ ] Manual play test: grow a large, high-demand city; observe that buildings densify
      noticeably faster (many tiles per month) compared to the trickle seen at low demand.
- [ ] `python3 tools/cognitive_complexity.py src/simulation/Population.cpp` — `processUpgradeWave`
      score does not increase (two extra arithmetic statements, no new control flow branches).
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
      reports no errors.
