## Phase 11q9: Fix Multi-Tile Zone Overlay + Service Building Non-Origin Demolish

**Status: TODO**

**Prerequisite**: phase-11q8 merged.

### Goal

Two related multi-tile footprint bugs make higher-density zones and service buildings
behave incorrectly:

**Bug 1 — Zone overlay shows only the origin tile after placement.**
After placing a Medium (2×2) or High (3×3) density zone, the zone-colour overlay
rendered on the map highlights only the origin tile (the clicked tile). The remaining
N²−1 footprint tiles have no overlay colour, so the player sees one green square where
4 or 9 should appear. The simulation data is correct (all N×N tiles are marked
`isZoned`); only the UI overlay update is wrong.

Root cause: `doTerrainPlacement` in `UIManager.cpp` (~line 1334) inserts a single
overlay key `hitZ * mapTilesX + hitX` into `m_world.overlayMap`. For a 2×2 footprint,
the other three tile keys are never inserted, so they receive no colour.

**Bug 2 — Service building can only be demolished from its origin tile.**
A service building occupies a 2×2 footprint but only clicking its origin tile
`(sb.x, sb.z)` with the Demolish tool removes it. Clicking any other footprint tile
does nothing because:

1. Service building tiles are **not** stored in `m_zoning.m_tiles`, so the zone-tile
   redirect (`footprintOriginX != -1`) in `demolishTile` never fires.
2. The `hadServiceBuilding` check (`sb.x == tileX && sb.z == tileZ`) only matches the
   origin tile.
3. The resulting early-return (`it == end && !hadServiceBuilding`) silently no-ops.

Root cause: `CitySimulation::demolishTile` (~line 554) has no redirect path for
non-origin service building tiles.

---

### Issues to Fix

#### 1. Zone overlay: stamp all N×N footprint keys in `doTerrainPlacement`

In `UIManager.cpp`, inside `doTerrainPlacement`, replace the single-key overlay insert
(~lines 1333–1345) with a loop that stamps every tile in the N×N footprint.

**Before** (single origin key):

```cpp
if (m_world.mapTilesX > 0 && m_world.mapTilesZ > 0) {
    // Phase 11m: insert overlay entry only when the tile was actually zoned.
    QueryResult placed = m_sim->queryTile(hitX, hitZ);
    if (placed.isZoned) {
        int64_t key = static_cast<int64_t>(hitZ) * m_world.mapTilesX
                      + hitX;
        ZoneType  zoneType   = static_cast<ZoneType>(m_world.selectedZoneType);
        DensityTier densityTier = static_cast<DensityTier>(m_world.selectedDensityTier);
        static constexpr size_t kOverlayCap = 100000u;
        if (m_world.overlayMap.size() < kOverlayCap) {
            m_world.overlayMap[key] = computeZoneOverlayColor(zoneType, densityTier);
        }
        if (m_renderer) {
            m_renderer->setZoneOverlay(m_world.mapTilesX, m_world.mapTilesZ, m_world.overlayMap);
        }
    }
}
```

**After** (all N×N footprint keys):

```cpp
if (m_world.mapTilesX > 0 && m_world.mapTilesZ > 0) {
    // Phase 11q9: stamp all N×N footprint tiles in the overlay map so the
    // zone colour covers the full placed footprint, not just the origin tile.
    QueryResult placed = m_sim->queryTile(hitX, hitZ);
    if (placed.isZoned) {
        ZoneType    zoneType    = static_cast<ZoneType>(m_world.selectedZoneType);
        DensityTier densityTier = static_cast<DensityTier>(m_world.selectedDensityTier);
        const int   N           = m_world.selectedDensityTier + 1;  // 1/2/3
        const uint32_t ovColor  = computeZoneOverlayColor(zoneType, densityTier);
        static constexpr size_t kOverlayCap = 100000u;
        for (int dz = 0; dz < N; ++dz) {
            for (int dx = 0; dx < N; ++dx) {
                if (m_world.overlayMap.size() >= kOverlayCap) break;
                int64_t fkey = static_cast<int64_t>(hitZ + dz) * m_world.mapTilesX
                               + (hitX + dx);
                m_world.overlayMap[fkey] = ovColor;
            }
        }
        if (m_renderer) {
            m_renderer->setZoneOverlay(m_world.mapTilesX, m_world.mapTilesZ,
                                       m_world.overlayMap);
        }
    }
}
```

The `N` variable is already computed two blocks earlier for the road-mesh-rebuild
border ring (`const int N = m_world.selectedDensityTier + 1;`) — do NOT duplicate the
variable; hoist or reuse that binding. The two blocks are within the same
`case ActiveTool::Zone:` scope, so the name is available.

---

#### 2. Service building demolish: redirect non-origin tiles to origin

In `CitySimulation::demolishTile` (`src/simulation/CitySimulation.cpp`), after the
existing zone-tile redirect block (~lines 547–552) and **before** the
`hadServiceBuilding` loop, insert a service-building non-origin redirect:

```cpp
// Phase 11q9: redirect non-origin service building tiles to the origin.
// Service buildings are not stored in m_tiles, so the zone-tile redirect above
// does not fire for them.  Check all service building footprints explicitly.
{
    const int sN = Zoning::serviceFootprintSize();
    for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
        if (tileX == sb.x && tileZ == sb.z) break;  // origin — fall through to normal path
        if (tileX >= sb.x && tileX < sb.x + sN &&
            tileZ >= sb.z && tileZ < sb.z + sN) {
            demolishTile(sb.x, sb.z);
            return;
        }
    }
}
```

The `break` on the origin match stops the loop early so the normal `hadServiceBuilding`
path runs for origin-tile clicks. Non-origin footprint tiles recursively call
`demolishTile(sb.x, sb.z)`, which will then find `hadServiceBuilding=true` and complete
the full removal (mesh removal + `m_serviceBuildings` erase).

Multiple recursive calls for the same building are safe: after the first origin demolish
the service building is erased from `m_serviceBuildings`; subsequent calls find
`hadServiceBuilding=false` and `it==end`, and return early as a no-op.

---

#### 3. Add zone-overlay multi-tile tests in `tests/ui/uimanager_zone_overlay_test.cpp`

Add two new tests to `ZoneOverlayTest` verifying that placing Medium and High density
zones stamps all footprint tile keys in the overlay map. Append after the three
existing tests.

**Test 4** — Medium 2×2 zone stamps all four overlay keys:

```cpp
// ---------------------------------------------------------------------------
// Test 4: UIManager_MedZonePlacement_AddsAllFootprintTilesToOverlay
//
// Select Medium density Residential and place at tile (3, 2).
// After LMB down + up, setZoneOverlay must be called with a map that contains
// all four 2×2 footprint keys:
//   (3,2)→ key = 2*10+3 = 23
//   (4,2)→ key = 2*10+4 = 24
//   (3,3)→ key = 3*10+3 = 33
//   (4,3)→ key = 3*10+4 = 34
// All four must carry the Residential/Medium ARGB 0xB480CC80.
// (Map dimensions: 10×10, set in SetUp.)
// ---------------------------------------------------------------------------
TEST_F(ZoneOverlayTest, UIManager_MedZonePlacement_AddsAllFootprintTilesToOverlay)
{
    activateZoneTool();

    // Select Medium density — zone sub-panel row 1 (density buttons y ~170).
    // UIManager maps density rows by vertical position inside the zone sub-panel.
    // Send a click on the Medium-row button (y ≈ 170, inside zone sub-panel x≈200).
    InputEvent densityClick{};
    densityClick.type  = InputEvent::Type::MouseButtonDown;
    densityClick.button = 0;
    densityClick.x      = 200;
    densityClick.y      = 170;  // Medium density row in zone sub-panel
    densityClick.physX  = 200;
    densityClick.physY  = 170;
    uiManager_->onEvent(densityClick);
    uiManager_->setSelectedDensityTierForTest(1);  // 1 = Medium

    stubPickTile(3, 2);

    ZoneOverlayMap capturedMap;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .Times(AtLeast(1))
        .WillRepeatedly(SaveArg<2>(&capturedMap));

    EXPECT_CALL(sim_, queryTile(_, _)).Times(AnyNumber()).WillRepeatedly(Return(QueryResult{}));
    QueryResult placed{};
    placed.isZoned = true;
    EXPECT_CALL(sim_, queryTile(3, 2))
        .WillOnce(Return(QueryResult{}))
        .WillRepeatedly(Return(placed));

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    // All four 2×2 footprint keys must be present in the overlay map.
    const int64_t k23 = 2*10 + 3, k24 = 2*10 + 4;
    const int64_t k33 = 3*10 + 3, k34 = 3*10 + 4;
    EXPECT_GT(capturedMap.count(k23), 0u) << "key 23 (3,2) must be in overlay";
    EXPECT_GT(capturedMap.count(k24), 0u) << "key 24 (4,2) must be in overlay";
    EXPECT_GT(capturedMap.count(k33), 0u) << "key 33 (3,3) must be in overlay";
    EXPECT_GT(capturedMap.count(k34), 0u) << "key 34 (4,3) must be in overlay";
}
```

**Test 5** — High 3×3 zone stamps all nine overlay keys:

```cpp
// ---------------------------------------------------------------------------
// Test 5: UIManager_HighZonePlacement_AddsAllFootprintTilesToOverlay
//
// Select High density Residential and place at tile (1, 1).
// setZoneOverlay must be called with a map containing all nine 3×3 keys:
//   (1,1)=11  (2,1)=12  (3,1)=13
//   (1,2)=21  (2,2)=22  (3,2)=23
//   (1,3)=31  (2,3)=32  (3,3)=33
// (keys = tileZ*10 + tileX, mapTilesX=10)
// ---------------------------------------------------------------------------
TEST_F(ZoneOverlayTest, UIManager_HighZonePlacement_AddsAllFootprintTilesToOverlay)
{
    activateZoneTool();
    uiManager_->setSelectedDensityTierForTest(2);  // 2 = High

    stubPickTile(1, 1);

    ZoneOverlayMap capturedMap;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .Times(AtLeast(1))
        .WillRepeatedly(SaveArg<2>(&capturedMap));

    EXPECT_CALL(sim_, queryTile(_, _)).Times(AnyNumber()).WillRepeatedly(Return(QueryResult{}));
    QueryResult placed{};
    placed.isZoned = true;
    EXPECT_CALL(sim_, queryTile(1, 1))
        .WillOnce(Return(QueryResult{}))
        .WillRepeatedly(Return(placed));

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    for (int dz = 0; dz < 3; ++dz) {
        for (int dx = 0; dx < 3; ++dx) {
            int64_t k = static_cast<int64_t>(1 + dz) * 10 + (1 + dx);
            EXPECT_GT(capturedMap.count(k), 0u)
                << "key " << k << " (" << (1+dx) << "," << (1+dz)
                << ") must be in overlay for 3×3 High zone";
        }
    }
}
```

**Test seam required**: Both tests call `uiManager_->setSelectedDensityTierForTest(int)`.
Add this test seam to `UIManager.h` alongside the existing `setOverlayMapForTest`:

```cpp
// setSelectedDensityTierForTest — test seam: bypass the zone sub-panel click
// sequence and directly set m_world.selectedDensityTier.
void setSelectedDensityTierForTest(int tier) {
    m_world.selectedDensityTier = tier;
}
```

---

#### 4. Add service building non-origin demolish test in `tests/simulation/footprint_test.cpp`

Append to `footprint_test.cpp` after the existing `FootprintTest_ServiceBuilding_*`
tests:

```cpp
// ============================================================================
// FootprintTest_ServiceBuilding_DemolishFromNonOriginTile
// Demolishing a service building by clicking any non-origin footprint tile
// must remove the entire service building — not silently no-op.
//
// Setup: FireStation placed at (3, 3) with road at (2, 3).
// Footprint tiles: (3,3), (4,3), (3,4), (4,4).
// Demolish from (4,4) — the far corner (not the origin).
// After demolish: all four footprint tiles must report no service building.
// ============================================================================
TEST_F(FootprintTest, FootprintTest_ServiceBuilding_DemolishFromNonOriginTile)
{
    sim_->placeRoad(2, 3);
    sim_->placeServiceBuilding(3, 3, ServiceBuildingType::FireStation);

    // Verify all four tiles are occupied before demolish.
    ASSERT_TRUE(isTileOccupied(3, 3)) << "pre-condition: (3,3) occupied";
    ASSERT_TRUE(isTileOccupied(4, 3)) << "pre-condition: (4,3) occupied";
    ASSERT_TRUE(isTileOccupied(3, 4)) << "pre-condition: (3,4) occupied";
    ASSERT_TRUE(isTileOccupied(4, 4)) << "pre-condition: (4,4) occupied";

    // Demolish from the non-origin far corner.
    sim_->demolishTile(4, 4);

    // All four footprint tiles must now be free.
    EXPECT_FALSE(isTileOccupied(3, 3))
        << "(3,3): service building must be removed after demolish from non-origin tile";
    EXPECT_FALSE(isTileOccupied(4, 3))
        << "(4,3): service building footprint must be cleared";
    EXPECT_FALSE(isTileOccupied(3, 4))
        << "(3,4): service building footprint must be cleared";
    EXPECT_FALSE(isTileOccupied(4, 4))
        << "(4,4): demolish origin tile — must be cleared";
}
```

---

### Exit Criteria

- [x] `make build` succeeds with no warnings.
- [x] `ctest -LE "integration|requires-opengl"` passes.
- [x] `ctest --test-dir build -R ZoneOverlay --output-on-failure` discovers all 5 overlay
      tests (3 existing + 2 new), all passing.
- [x] `ctest --test-dir build -R FootprintTest_ServiceBuilding_DemolishFromNonOriginTile
      --output-on-failure` passes.
- [x] Manual play test — Zone overlay:
      Select Medium density, place a zone; confirm 4 tiles in the 2×2 footprint receive
      the zone colour overlay (not just 1). Same for High density (9 tiles).
- [x] Manual play test — Service building demolish:
      Place a Fire Station, activate Demolish, click any non-origin tile of the 2×2
      footprint; confirm the entire building is removed (mesh disappears, all 4 tiles
      become placeable).
- [x] `python3 tools/cognitive_complexity.py src/ui/UIManager.cpp` and
      `src/simulation/CitySimulation.cpp` — no function score worsens beyond its
      current value; `doTerrainPlacement` must not exceed 25.
- [x] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
      reports no errors.
