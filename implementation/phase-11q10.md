## Phase 11q10: Fix Coverage Inspector Showing 1% Instead of 0% / 100%

**Status: DONE**

**Prerequisite**: phase-11q9 merged.

### Goal

When the player uses the Query tool to inspect any tile, the service coverage row
displays "Fire:1% Pol:1% Pwr:1% Wtr:1%" for any covered tile.  Even service
buildings themselves show "1%" when inspected.  Uncovered tiles correctly show
"0%", but covered tiles should show "100%".  This applies to all four services
(Fire, Police, Power, Water).

---

### Root Cause

`ServiceCoverage` (defined in `src/interfaces/simulation_types.h`) is documented
as storing **percentages** (0–100, with −1.0f meaning N/A).  However,
`Zoning::queryTile()` stores the raw return values of
`computeRadialCoverage()` / `computePowerCoverage()` directly, and both
functions return a **boolean float**: `1.0f` (covered) or `0.0f` (not covered).

`QueryPanel.cpp` then formats these fields with `"%.0f%%"`:

```text
result.coverage.power == 1.0f → "1%"     ← bug: should be "100%"
result.coverage.power == 0.0f → "0%"     ← correct
```

The fix is a one-line-per-field change at the **only assignment site**,
`Zoning::queryTile()` (`src/simulation/Zoning.cpp:217-220`), multiplying each
coverage helper return value by `100.0f` before storing it.

`computeRadialCoverage()` and `computePowerCoverage()` are called directly by
`Economy.cpp` and `computeFirePoliceCoverageGap()` using their 0/1 boolean
semantics — those callers are **not** changed.

---

### Fix

#### 1. Scale coverage to 0–100 in `Zoning::queryTile()`

File: `src/simulation/Zoning.cpp`, lines 217–220.

**Before:**

```cpp
    result.coverage.fire   = hasFireStation ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::FireStation)   : -1.0f;
    result.coverage.police = hasPolice      ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::PoliceStation) : -1.0f;
    result.coverage.water  = hasWater       ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::WaterTower)    : -1.0f;
    result.coverage.power  = hasPower       ? computePowerCoverage(tileX, tileZ)                                     : -1.0f;
```

**After:**

```cpp
    // Phase 11q10: computeRadialCoverage / computePowerCoverage return a boolean
    // float (0.0 = not covered, 1.0 = covered).  ServiceCoverage stores percentages
    // (0–100); multiply by 100 so the inspector displays "0%" / "100%", not "0%" / "1%".
    result.coverage.fire   = hasFireStation ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::FireStation)   * 100.0f : -1.0f;
    result.coverage.police = hasPolice      ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::PoliceStation) * 100.0f : -1.0f;
    result.coverage.water  = hasWater       ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::WaterTower)    * 100.0f : -1.0f;
    result.coverage.power  = hasPower       ? computePowerCoverage(tileX, tileZ)                                     * 100.0f : -1.0f;
```

No other source files require changes.

---

#### 2. Add regression test in `tests/simulation/service_coverage_test.cpp`

Append after the last existing test in `ServiceTest`.  The new test asserts the
**exact value** (`100.0f`) to prevent the 0–1 vs 0–100 regression recurring.

```cpp
// ---------------------------------------------------------------------------
// Test N: CoverageDisplay_CoveredTile_Returns100Percent
//
// Regression test for Phase 11q10: coverage values stored in QueryResult must
// be on a 0–100 scale, not a 0–1 boolean scale, so the inspector displays
// "100%" (not "1%") for covered tiles.
//
// Setup: Power plant at (0,0); residential zone at (2,0) (BFS depth 1).
// After one tick, coverage.power for the residential tile must be 100.0f,
// and coverage.power for an uncovered tile must be 0.0f.
// ---------------------------------------------------------------------------
TEST_F(ServiceTest, CoverageDisplay_CoveredTile_Returns100Percent)
{
    cs()->addServiceBuilding(0, 0, 3);  // PowerPlant at (0,0)

    // Road within 3 tiles of zone tile (2,0): place at (3,0).
    sim_->placeRoad(3, 0, 0);
    sim_->placeZone(2, 0, ZoneType::Residential, DensityTier::Low);

    // Place a second residential tile far from the plant so it is NOT covered.
    // Road at (30,0) keeps it valid; power BFS cannot reach (25,0) from (0,0)
    // through an otherwise-empty grid.
    sim_->placeRoad(30, 0, 0);
    sim_->placeZone(25, 0, ZoneType::Residential, DensityTier::Low);

    runTicks(1);

    QueryResult covered   = sim_->queryTile(2, 0);
    QueryResult uncovered = sim_->queryTile(25, 0);

    EXPECT_FLOAT_EQ(covered.coverage.power, 100.0f)
        << "Covered tile must report coverage.power == 100.0f, not 1.0f";

    EXPECT_FLOAT_EQ(uncovered.coverage.power, 0.0f)
        << "Uncovered tile must report coverage.power == 0.0f";
}
```

---

### Existing tests — no change required

All three existing `ServiceTest` coverage assertions remain valid after the fix:

| Test | Assertion | After fix |
|------|-----------|-----------|
| `PowerCoverage_ConnectedTiles_AreCovered` | `EXPECT_GT(r.coverage.power, 0.0f)` | 100.0 > 0.0 ✓ |
| `PowerCoverage_ZeroTilesInRange_ReturnsNASentinel` | `EXPECT_FLOAT_EQ(..., -1.0f)` | −1.0f unchanged ✓ |
| `PowerCoverage_DeficitDegradation_ReducesBFSRadius` | `EXPECT_EQ(degraded.coverage.power, 0.0f)` | 0.0 × 100 = 0.0 ✓ |

---

### Exit Criteria

- [x] `make build` succeeds with no warnings.
- [x] `ctest -LE "integration|requires-opengl"` passes (all existing coverage tests
      still pass; new regression test passes).
- [x] `ctest --test-dir build -R CoverageDisplay_CoveredTile_Returns100Percent
      --output-on-failure` passes.
- [x] Manual play test: place a Power Plant; open Query tool; click the tile
      immediately adjacent to the plant — inspector must show "Pwr:100%" (not
      "Pwr:1%"). Click a tile far away with no road connection — must show "Pwr:0%".
- [x] Manual play test: click the Power Plant tile itself — inspector (service
      building panel) must show "Power: 100%" (not "Power: 1%").
- [x] Manual play test: same verification for Water Tower, Fire Station, and Police
      Station using their respective coverage rows.
- [x] `python3 tools/cognitive_complexity.py src/simulation/Zoning.cpp` — no
      function score increases; `queryTile` score is unchanged (comment + inline
      multiplications, no new control flow).
- [x] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
      reports no errors.
