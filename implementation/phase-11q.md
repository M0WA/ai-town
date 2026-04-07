## Phase 11q: Bug-Fix Batch — Buses/Trucks Never Appear & Vehicle Road Surface Alignment

**Status: Planned**

### Goal

Two confirmed vehicle bugs, fully investigated. Assets are intact —
`bus_standard_lod0.b3d` and `truck_cargo_lod0.b3d` exist on disk. All issues are
in simulation logic and renderer positioning code.

1. **Buses and trucks never appear** — Every `TrafficVehicle` always has
   `zone == ZoneType::Residential` because the zone is never correctly assigned at
   spawn or updated when the vehicle changes route. `vehicleMeshPath()` is therefore
   structurally unable to return the Commercial (bus) or Industrial (truck) mesh paths.

2. **Cars hover above / sink into road surface** — `moveVehicleAgent()` places vehicles
   at raw terrain height with no `+0.25 m` bias, while the visible road surface vertex
   geometry bakes in exactly that bias. A secondary issue is that `getHeightAt()` returns
   the tile corner, not an interpolated center, causing up to `~0.35 m` additional error
   on sloped terrain. `spawnVehicleAgent()` also hard-codes `Y = 0.0f`.

---

### Implementation Order

The deliverables are sequenced from lowest risk to highest effort:

1. **Fix 2a** — Add `kRoadSurfaceYBias` constant and apply it in `moveVehicleAgent()`.
   Single-line change; eliminates 25 cm sink/hover on flat terrain.
2. **Fix 2d** — Correct stale comment (0.10 m → 0.25 m). Zero risk; cosmetic only.
3. **Fix 1b** — Assign vehicle zone from destination tile at spawn (primary zone fix).
4. **Fix 1c** — Re-query destination zone on trip completion (retroactive update).
5. **Fix 1d** — Detect zone change in agent sync loop → despawn + respawn renderer node
   with correct mesh, release and re-acquire audio source pair.
6. **Fix 2b** — Add `getHeightAtWorld(float, float)` bilinear interpolation to
   `TerrainSystem`; expose on `ITerrainQuery`; use in `moveVehicleAgent()`.
7. **Fix 2c** — Replace `Y = 0.0f` in `spawnVehicleAgent()` with bilinear terrain query
   plus road bias (requires Fix 2b to land first).
8. **Tests** — Unit tests locking in both bug-fix behaviours.

---

### Deliverables

---

#### 1. Fix 2a — `kRoadSurfaceYBias` shared constant + `moveVehicleAgent()` Y correction

**Root cause**

`buildTileRoadMesh()` bakes `+0.25 m` into every road surface vertex
(`IrrlichtRenderer.cpp:1884`). The road node is placed at `Y = 0`
(`IrrlichtRenderer.cpp:2198`), so the visible road surface sits at
`terrain_height + 0.25` in world space. `moveVehicleAgent()` positions vehicles at
raw terrain height with no bias (`IrrlichtRenderer.cpp:3067–3072`), leaving vehicles
25 cm below the visible surface.

**Code changes**

- [ ] **`src/rendering/render_constants.h`** — Add a new entry:

  ```cpp
  /// Vertical offset (metres) baked into road surface vertices by buildTileRoadMesh().
  /// Must be added to the terrain height when positioning vehicles on the road surface.
  inline constexpr float kRoadSurfaceYBias = 0.25f;
  ```

- [ ] **`src/rendering/IrrlichtRenderer.cpp:1884`** — Replace the function-local
  `static constexpr float B = 0.25f;` declaration with a direct use of
  `kRoadSurfaceYBias` (include `render_constants.h` if not already included):

  ```cpp
  // Before:
  static constexpr float B = 0.25f;
  // ...
  const float y00 = h00 + B;

  // After:
  const float y00 = h00 + kRoadSurfaceYBias;
  ```

  Apply the same substitution to every occurrence of `B` within `buildTileRoadMesh()`
  (`y10`, `y01`, `y11`).

- [ ] **`src/rendering/IrrlichtRenderer.cpp:3067–3072`** — In `moveVehicleAgent()`,
  change the `y` assignment:

  ```cpp
  // Before:
  y = m_terrain->getHeightAt(tileX, tileZ);

  // After:
  y = m_terrain->getHeightAt(tileX, tileZ) + kRoadSurfaceYBias;
  ```

---

#### 2. Fix 2d — Correct stale comment in `buildTileRoadMesh()`

- [ ] **`src/rendering/IrrlichtRenderer.cpp:1868`** — Update the comment from
  `// road surface lifted +0.10 m above terrain` (or similar) to
  `// road surface lifted +0.25 m above terrain (kRoadSurfaceYBias)`.

---

#### 3. Fix 1b — Assign vehicle zone from destination tile at spawn

**Root cause**

`placeRoad()` (`CitySimulation.cpp:2464–2468`) spawns a vehicle and attempts to
inherit a zone type from the four immediate 1-tile neighbours of the newly placed road
tile. Roads are placed before zones in practice, so the neighbours are almost always
unzoned and `zone` stays at the default `ZoneType::Residential`.

**Canonical approach**: derive zone from the destination tile's zone instead.
`TrafficVehicle` already stores `dstX`/`dstZ`; look up
`findTile(dstX, dstZ)->zone` at spawn time and fall back to a proportional
distribution when the destination is unzoned.

**Code changes**

- [ ] **`src/simulation/CitySimulation.cpp:~2450–2480`** (inside `placeRoad()`, vehicle
  spawn block) — Replace the 4-neighbour zone-inheritance loop with:

  ```cpp
  // Derive zone from the destination tile.
  ZoneType spawnZone = ZoneType::Residential; // fallback default
  const Tile* dst = findTile(v.dstX, v.dstZ);
  if (dst && dst->zone != ZoneType::None) {
      spawnZone = dst->zone;
  } else {
      // Destination is unzoned: use proportional distribution.
      // 70% Residential, 20% Commercial, 10% Industrial.
      const int roll = m_rng->nextInt(100);
      if (roll < 70)       spawnZone = ZoneType::Residential;
      else if (roll < 90)  spawnZone = ZoneType::Commercial;
      else                 spawnZone = ZoneType::Industrial;
  }
  v.zone = spawnZone;
  ```

  Remove the now-redundant neighbour-scan loop entirely.

---

#### 4. Fix 1c — Re-query destination zone on trip completion

**Root cause**

Long-lived vehicles never have their zone re-evaluated when their route changes. If
the city is re-zoned after vehicles were spawned, the vehicles keep stale zone values
indefinitely.

**Code changes**

- [ ] **`src/simulation/CitySimulation.cpp:1766–1808`** (inside `doTrafficVehicleTick()`,
  at the point where a completed trip assigns a new destination) — After the new
  `dstX`/`dstZ` are assigned, re-query the destination tile's zone:

  ```cpp
  // Re-evaluate zone from new destination.
  const Tile* newDst = findTile(v.dstX, v.dstZ);
  if (newDst && newDst->zone != ZoneType::None) {
      v.zone = newDst->zone;
  }
  // If destination is still unzoned, retain the existing zone value;
  // Fix 1d will detect any actual change and re-spawn the renderer node.
  ```

---

#### 5. Fix 1d — Detect zone change in agent sync loop → despawn + respawn with audio lifecycle

**Root cause**

Even after Fixes 1b and 1c correctly update `v.zone`, the renderer node was spawned
with the old (wrong) mesh and is never refreshed unless the vehicle goes through a
distance-cull cycle.

**Code changes**

- [ ] **`src/main.cpp:339–440`** (agent sync loop) — Add a zone-change detection branch
  alongside the existing distance-cull despawn/respawn logic
  (`main.cpp:389–394`). For each active agent whose `zone` differs between the cached
  value in `activeAgents[handle]` and the new `AgentState a.zone`:

  ```cpp
  if (activeAgents[handle].zone != a.zone) {
      // 1. Release the old audio source pair back to the pool.
      const auto& aud = activeAgents[handle].audio;
      audioSystem.releaseVehicleEnginePair(aud.idleIdx, aud.moveIdx);

      // 2. Remove the stale renderer node.
      renderer.despawnVehicleAgent(handle);

      // 3. Spawn a new node with the correct mesh for the new zone.
      renderer.spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);

      // 4. Acquire a fresh audio source pair. If the pool is exhausted,
      //    the vehicle renders visually but runs in silent mode.
      const auto newAud = audioSystem.acquireVehicleEnginePair();
      // newAud == {-1, -1} when pool is exhausted — silent mode is
      // acceptable; do NOT assert or log a fatal error here.

      // 5. Store updated audio state and new zone value.
      activeAgents[handle].audio = newAud;
      activeAgents[handle].zone  = a.zone;
  }
  ```

  This sequence mirrors the existing distance-cull despawn/respawn at `main.cpp:389–394`
  and adds audio lifecycle management to prevent orphaned audio sources.

---

#### 6. Fix 2b — `getHeightAtWorld()` bilinear interpolation in `TerrainSystem`

**Root cause**

`TerrainSystem::getHeightAt(int tileX, int tileZ)` returns the top-left vertex of tile
`(tileX, tileZ)` (`TerrainSystem.cpp:832–833`). Vehicles are positioned at the tile
center, but the height query uses only one corner. On sloped terrain the discrepancy
can reach `~0.35 m`, causing residual hover/sink after Fix 2a on non-flat ground.

**Interface change**

- [ ] **`src/interfaces/ITerrainQuery.h`** — Add a new pure-virtual method:

  ```cpp
  /// Bilinearly interpolate terrain height at an arbitrary world position.
  /// worldX and worldZ are in world-space metres.
  /// Returns the interpolated height (metres) across the four tile-corner heights
  /// that surround (worldX, worldZ).
  virtual float getHeightAtWorld(float worldX, float worldZ) const = 0;
  ```

- [ ] **`src/terrain/TerrainSystem.h`** — Declare the override:

  ```cpp
  float getHeightAtWorld(float worldX, float worldZ) const override;
  ```

- [ ] **`src/terrain/TerrainSystem.cpp:826–833`** — Implement `getHeightAtWorld()`:

  ```cpp
  float TerrainSystem::getHeightAtWorld(float worldX, float worldZ) const {
      // Convert world coordinates to tile-space (1 tile = kTileSize metres).
      const float tx = worldX / kTileSize;
      const float tz = worldZ / kTileSize;

      // Integer tile indices for the four surrounding corners.
      const int x0 = static_cast<int>(std::floor(tx));
      const int z0 = static_cast<int>(std::floor(tz));
      const int x1 = x0 + 1;
      const int z1 = z0 + 1;

      // Fractional offsets within the tile cell.
      const float fx = tx - static_cast<float>(x0);
      const float fz = tz - static_cast<float>(z0);

      // Sample all four corners, clamped to valid tile range.
      const float h00 = getHeightAt(x0, z0);
      const float h10 = getHeightAt(x1, z0);
      const float h01 = getHeightAt(x0, z1);
      const float h11 = getHeightAt(x1, z1);

      // Bilinear interpolation.
      const float h0 = h00 + fx * (h10 - h00);
      const float h1 = h01 + fx * (h11 - h01);
      return h0 + fz * (h1 - h0);
  }
  ```

- [ ] **`src/rendering/IrrlichtRenderer.cpp:3067–3072`** — In `moveVehicleAgent()`,
  replace `getHeightAt(tileX, tileZ)` with `getHeightAtWorld()`:

  ```cpp
  // Compute world-space center of the tile.
  const float worldX = (static_cast<float>(tileX) + 0.5f) * kTileSize;
  const float worldZ = (static_cast<float>(tileZ) + 0.5f) * kTileSize;
  y = m_terrain->getHeightAtWorld(worldX, worldZ) + kRoadSurfaceYBias;
  ```

- [ ] **Mock / stub updates** — Add a no-op `getHeightAtWorld()` override to every class
  that implements `ITerrainQuery` (e.g. `ManualTerrainQuery`, any mock used in tests):

  ```cpp
  float getHeightAtWorld(float /*worldX*/, float /*worldZ*/) const override {
      return 0.0f;
  }
  ```

---

#### 7. Fix 2c — Replace hard-coded `Y = 0.0f` in `spawnVehicleAgent()`

**Root cause**

`spawnVehicleAgent()` (`IrrlichtRenderer.cpp:3025–3027`) places the new node at
`Y = 0.0f`. The following `moveVehicleAgent()` call in `main.cpp:423` corrects this
within the same tick, but on non-flat terrain the node briefly appears at the world
origin.

**Prerequisite**: Fix 2b must land before this deliverable.

**Code change**

- [ ] **`src/rendering/IrrlichtRenderer.cpp:3025–3027`** — Replace `Y = 0.0f` with the
  same bilinear terrain query plus road bias used by `moveVehicleAgent()`:

  ```cpp
  // Before:
  node->setPosition(irr::core::vector3df(spawnX, 0.0f, spawnZ));

  // After:
  const float worldX = (static_cast<float>(tileX) + 0.5f) * kTileSize;
  const float worldZ = (static_cast<float>(tileZ) + 0.5f) * kTileSize;
  const float spawnY = m_terrain->getHeightAtWorld(worldX, worldZ) + kRoadSurfaceYBias;
  node->setPosition(irr::core::vector3df(spawnX, spawnY, spawnZ));
  ```

---

#### 8. Tests

**New test cases** — add to the appropriate existing test source files
(`tests/rendering/` and `tests/simulation/`).

- [ ] **`VehicleMeshPath_CommercialZone_ReturnsBusPath`** (`tests/rendering/`) — Call
  `vehicleMeshPath(ZoneType::Commercial)` (or equivalent accessor) and assert the
  returned path ends with `"bus_standard_lod0.b3d"`.

- [ ] **`VehicleMeshPath_IndustrialZone_ReturnsTruckPath`** (`tests/rendering/`) — Call
  `vehicleMeshPath(ZoneType::Industrial)` and assert the returned path ends with
  `"truck_cargo_lod0.b3d"`.

- [ ] **`MoveVehicleAgent_FlatTerrain_YIncludesRoadBias`** (`tests/rendering/`) — Use
  a `ManualTerrainQuery` stub returning `0.0f` for all height queries. Call
  `moveVehicleAgent()` and assert the node Y position equals `kRoadSurfaceYBias`
  (i.e. `0.25f`).

- [ ] **`SpawnVehicleAgent_FlatTerrain_YIncludesRoadBias`** (`tests/rendering/`) — Same
  stub. Call `spawnVehicleAgent()` (Fix 2c) and assert the node Y position equals
  `kRoadSurfaceYBias`.

- [ ] **`TrafficVehicle_SpawnOnUnzonedDestination_ZoneIsNotAlwaysResidential`**
  (`tests/simulation/`) — Spawn 100 vehicles with an unzoned destination tile.
  Assert that at least one vehicle has `zone != ZoneType::Residential` (verifying the
  proportional fallback in Fix 1b is exercised).

- [ ] **`TrafficVehicle_ZoneUpdated_OnTripCompletion`** (`tests/simulation/`) — Create
  a vehicle with `zone == ZoneType::Residential`. Set the destination tile's zone to
  `ZoneType::Commercial`. Call `doTrafficVehicleTick()` through a trip completion.
  Assert `v.zone == ZoneType::Commercial` after the tick.

---

### Affected Files Summary

| File | Lines | Change |
|---|---|---|
| `src/rendering/render_constants.h` | new entry | Add `kRoadSurfaceYBias = 0.25f` |
| `src/rendering/IrrlichtRenderer.cpp` | 1868 | Fix stale comment (0.10 m → 0.25 m) |
| `src/rendering/IrrlichtRenderer.cpp` | 1884 | Replace local `B` with `kRoadSurfaceYBias` |
| `src/rendering/IrrlichtRenderer.cpp` | 3025–3027 | Use bilinear terrain query + bias at spawn (Fix 2c) |
| `src/rendering/IrrlichtRenderer.cpp` | 3067–3072 | Add bilinear query + `kRoadSurfaceYBias` to vehicle Y (Fix 2b) |
| `src/terrain/TerrainSystem.h` | — | Declare `getHeightAtWorld(float, float)` override |
| `src/terrain/TerrainSystem.cpp` | 826–833 | Implement `getHeightAtWorld()` with bilinear interpolation |
| `src/interfaces/ITerrainQuery.h` | — | Expose `getHeightAtWorld(float, float)` pure-virtual |
| `src/simulation/CitySimulation.cpp` | 1766–1808 | Re-assign zone on trip completion (Fix 1c) |
| `src/simulation/CitySimulation.cpp` | ~2450–2480 | Assign zone from destination tile at spawn (Fix 1b) |
| `src/main.cpp` | 339–440 | Detect zone change → despawn + respawn + audio lifecycle (Fix 1d) |
| `tests/rendering/` | new cases | `vehicleMeshPath` Commercial/Industrial + Y bias tests |
| `tests/simulation/` | new cases | Zone assignment and trip-completion zone-update tests |
| Any `ITerrainQuery` mock/stub | — | Add no-op `getHeightAtWorld()` override |

---

### Exit Criteria

- [ ] `bus_standard_lod0.b3d` and `truck_cargo_lod0.b3d` appear in-game when Commercial
  and Industrial zones are placed adjacent to roads.
- [ ] Vehicle Y position on flat terrain equals `terrain_height + 0.25` (verified by unit
  test `MoveVehicleAgent_FlatTerrain_YIncludesRoadBias`).
- [ ] Vehicle Y position on sloped terrain no longer diverges from the road surface
  (bilinear interpolation active).
- [ ] `spawnVehicleAgent()` no longer places the node at world origin on non-flat terrain.
- [ ] All 6 new unit tests pass.
- [ ] All existing `simulation_tests` and `rendering_tests` continue to pass.
- [ ] `all-checks-pass` CI job is green.
