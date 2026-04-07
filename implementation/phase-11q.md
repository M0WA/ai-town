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

1. **Fix 2a** — Add `kRoadSurfaceYBias` and `kTileSize` constants to `render_constants.h`
   and replace the function-local `B = 0.25f` in `buildTileRoadMesh()` with the shared
   constant. (Fix 2b applies `kRoadSurfaceYBias` in `moveVehicleAgent()` — Fix 2a only
   touches `buildTileRoadMesh()`.)
2. **Fix 2d** — Correct stale comment (0.10 m → 0.25 m). Zero risk; cosmetic only.
3. **Fix 1b** — Assign vehicle zone from destination tile at spawn (primary zone fix).
4. **Fix 1c** — Re-query destination zone on trip completion (retroactive update).
5. **Fix 1d** — Detect zone change in agent sync loop → despawn + respawn renderer node
   with correct mesh, release and re-acquire audio source pair.
6. **Fix 2b** — Add `getHeightAtWorld(float, float)` bilinear interpolation to
   `TerrainSystem`; expose on `ITerrainQuery`; use in `moveVehicleAgent()`.
7. **Fix 2c** — Replace `Y = 0.0f` in `spawnVehicleAgent()` with bilinear terrain query
   plus road bias (requires Fix 2b to land first).
8. **Fix 2e** — Compute terrain normal from three height samples and apply pitch/roll to
   vehicle scene node in `moveVehicleAgent()` (and `spawnVehicleAgent()` post-Fix-2c).
   Requires Fix 2b to land first (uses `getHeightAtWorld()`).
9. **Tests** — Unit tests locking in both bug-fix behaviours.

---

### Deliverables

---

#### 0. `vehicle_mesh_path.h` — Extract zone-to-mesh mapping as a testable free function

**Prerequisite for Tests (Deliverable 8)**

`vehicleMeshPath(ZoneType zone, int variantIdx)` must be declared in a standalone header
so it can be unit-tested without a live Irrlicht context. `variantIdx` selects among the
three Residential car variants deterministically (caller passes `static_cast<int>(handle) % 3`
so each vehicle always uses the same model across re-spawns).

- [ ] **`src/rendering/vehicle_mesh_path.h`** (new file) — Declare and define
  `vehicleMeshPath()` as an `inline` free function:

  ```cpp
  #pragma once
  #include "src/interfaces/simulation_types.h"  // ZoneType
  #include "src/platform/PlatformUtils.h"        // getAssetsDir()
  #include <array>
  #include <string>

  /// Returns the LOD0 mesh asset path for the given zone type.
  /// Commercial → bus_standard_lod0.b3d, Industrial → truck_cargo_lod0.b3d.
  /// Residential: round-robins among car_sedan, car_hatchback, car_suv via
  /// variantIdx % 3 (pass static_cast<int>(handle) % 3 for deterministic per-vehicle
  /// assignment; pass 0 for the car_sedan default / fallback for unknown zones).
  inline std::string vehicleMeshPath(ZoneType zone, int variantIdx = 0) {
      const std::string kVehicleDir = getAssetsDir() + "/3d/vehicles/";
      if (zone == ZoneType::Commercial) return kVehicleDir + "bus_standard_lod0.b3d";
      if (zone == ZoneType::Industrial) return kVehicleDir + "truck_cargo_lod0.b3d";
      // Residential — round-robin across three car variants.
      static const std::array<const char*, 3> kResidential = {{
          "car_sedan_lod0.b3d",
          "car_hatchback_lod0.b3d",
          "car_suv_lod0.b3d",
      }};
      return kVehicleDir + kResidential[static_cast<unsigned>(variantIdx) % 3];
  }
  ```

- [ ] **`src/rendering/IrrlichtRenderer.cpp`** — In `spawnVehicleAgent()`, replace any
  existing inline zone→mesh selection logic with a call to
  `vehicleMeshPath(zone, static_cast<int>(handle) % 3)` (include `vehicle_mesh_path.h`).

- [ ] **`CMakeLists.txt`** — Add `src/rendering/` to `simulation_tests`
  `target_include_directories()` so the vehicleMeshPath unit tests can `#include
  "vehicle_mesh_path.h"`. Do NOT add `src/platform/` to `simulation_tests` — the
  `VehicleMeshPath` unit tests must avoid depending on `getAssetsDir()` at test time;
  instead they assert only the path suffix using `EndsWith` matchers (see Deliverable 8).

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

- [ ] **`src/rendering/render_constants.h`** — Add two new entries **inside the existing
  `namespace RenderConstants { }` block**:

  ```cpp
  /// Side length of one map tile in world-space metres.
  /// Shared between IrrlichtRenderer and TerrainSystem (getHeightAtWorld).
  static constexpr float kTileSize = 10.0f;

  /// Vertical offset (metres) baked into road surface vertices by buildTileRoadMesh().
  /// Must be added to the terrain height when positioning vehicles on the road surface.
  static constexpr float kRoadSurfaceYBias = 0.25f;
  ```

  Both constants must be inside `namespace RenderConstants { ... }` so they are accessible
  as `RenderConstants::kTileSize` and `RenderConstants::kRoadSurfaceYBias`, or via a
  `using namespace RenderConstants;` at function scope (the pattern already used in
  `moveVehicleAgent()` at `IrrlichtRenderer.cpp:3075`).

- [ ] **`src/rendering/IrrlichtRenderer.h`** — Remove the private class member
  `static constexpr float kTileSize = 10.0f;` (currently at line 322) to eliminate the
  duplicate. All IrrlichtRenderer member functions that use bare `kTileSize` already have
  `using namespace RenderConstants;` in scope (or can add it at function scope), so they
  will transparently resolve to `RenderConstants::kTileSize` after removal. This is a
  single-source-of-truth consolidation with no functional change.

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
  const TileData* dst = findTile(v.dstX, v.dstZ);
  if (dst && dst->isZoned) {
      spawnZone = dst->zone;
  } else {
      // Destination is unzoned: use proportional distribution.
      // 70% Residential, 20% Commercial, 10% Industrial.
      const int roll = m_rng->nextInt(0, 99);
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
  const TileData* newDst = findTile(v.dstX, v.dstZ);
  if (newDst && newDst->isZoned) {
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

> **Already implemented**: `IRenderer::spawnVehicleAgent()` already carries the
> 4-parameter signature `spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ,
> ZoneType zone)` from Phase 11d (`src/interfaces/IRenderer.h:237–238`).
> `IrrlichtRenderer` and `MockRenderer` already implement this signature. No interface
> change is required in this phase.
>
> **Already implemented**: The audio-pair tracking map already exists as
> `std::unordered_map<AgentHandle, AgentAudioState> activeAgents` where `AgentAudioState`
> carries `idleIdx`, `moveIdx`, AND `zone` fields (`src/main.cpp:293–298`). The initial
> zone value is already populated at spawn (`main.cpp:404–412`). No struct or map rename
> is required. The zone-change detection branch (below) uses `activeAgents.find(handle)` to guard against operator[] default-insertion before checking `.zone`
> against `a.zone` to detect when a respawn is needed.

- [ ] **`src/main.cpp:339–440`** (agent sync loop) — Add a zone-change detection branch
  alongside the existing distance-cull despawn/respawn logic
  (`main.cpp:389–394`). For each active agent whose `zone` differs between the cached
  value in `activeAgents[handle]` and the new `AgentState a.zone`:

  ```cpp
  auto it = activeAgents.find(handle);
  if (it != activeAgents.end() && it->second.zone != a.zone) {
      auto& s = it->second;
      // 1. Release the old audio source pair back to the pool.
      audioSystem.releaseVehicleEnginePair(s.idleIdx, s.moveIdx);

      // 2. Remove the stale renderer node.
      renderer.despawnVehicleAgent(handle);

      // 3. Spawn a new node with the correct mesh for the new zone.
      renderer.spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);

      // 4. Acquire a fresh audio source pair for the NEW zone.
      //    Zone determines the engine pitch multiplier applied to both sources
      //    (Residential/car → 1.0, Commercial/bus → 0.85, Industrial/truck → 0.85;
      //    see audio-system.md lines 256-260). A fresh pair is required so the
      //    pitch matches the new vehicle type. If the pool is exhausted, the
      //    vehicle renders visually but runs in silent mode.
      const auto newAud = audioSystem.acquireVehicleEnginePair(a.zone);
      // newAud == {-1, -1} when pool is exhausted — silent mode is
      // acceptable; do NOT assert or log a fatal error here.
      // Silent mode contract: the existing guard at main.cpp:~430
      //   if (it->second.idleIdx >= 0) { audioSystem.updateVehicleAudio(...); }
      // already prevents AL calls on -1 indices for per-frame updateVehicleAudio().
      // Per source-pool.md §releaseVehicleEnginePair, the invalid-index guard
      // (-1 short-circuit) already protects future releaseVehicleEnginePair calls.
      // No new guard code is required.

      // 5. Store updated audio state and new zone value.
      activeAgents[handle].idleIdx = newAud.first;
      activeAgents[handle].moveIdx = newAud.second;
      activeAgents[handle].zone    = a.zone;
  }
  ```

  This sequence mirrors the existing distance-cull despawn/respawn at `main.cpp:389–394`
  and adds audio lifecycle management to prevent orphaned audio sources.
  If `acquireVehicleEnginePair()` returns `{-1, -1}`, the vehicle enters silent mode: it
  renders visually with the correct mesh but produces no engine audio. The existing guards
  at `main.cpp:~430` (for `updateVehicleAudio()`) and in `releaseVehicleEnginePair()`
  (invalid-index short-circuit per `source-pool.md §releaseVehicleEnginePair`) already
  prevent AL errors on `{-1, -1}` indices — no additional guard code is required.

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

- [ ] **`src/terrain/TerrainSystem.cpp:826–833`** — Add `#include "render_constants.h"` at
  the top of the file. Use `using namespace RenderConstants;` at function scope (matching
  the pattern in `IrrlichtRenderer.cpp:3075` for `moveVehicleAgent()`).

  > **Note on the Irrlicht header pull-in**: `render_constants.h` includes `<irrlicht.h>` (for
  > `irr::video::SColor road_lod2_color`). Adding this include to `TerrainSystem.cpp` is
  > intentional — `getHeightAtWorld()` uses only `kTileSize` and `kRoadSurfaceYBias` (plain
  > `float` constants) and standard float arithmetic; no Irrlicht types appear in the function
  > body. The build will not introduce circular dependencies because Irrlicht does not depend on
  > AI Town's terrain code. This is the same include pattern used by `IrrlichtRenderer.cpp`.

  Implement `getHeightAtWorld()`:

  ```cpp
  float TerrainSystem::getHeightAtWorld(float worldX, float worldZ) const {
      using namespace RenderConstants;
      // Convert world coordinates to tile-space (kTileSize from render_constants.h).
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
  replace `getHeightAt(tileX, tileZ)` with `getHeightAtWorld()`. The function body already
  begins with `using namespace RenderConstants;` (line 3075), so bare `kTileSize` and
  `kRoadSurfaceYBias` names are valid without further qualification:

  ```cpp
  // Compute world-space center of the tile.
  const float worldX = (static_cast<float>(tileX) + 0.5f) * kTileSize;
  const float worldZ = (static_cast<float>(tileZ) + 0.5f) * kTileSize;
  y = m_terrain->getHeightAtWorld(worldX, worldZ) + kRoadSurfaceYBias;
  ```

- [ ] **`src/interfaces/ITerrainQuery.h` + `ManualTerrainQuery` (atomic commit)** — Add a
  no-op `getHeightAtWorld()` override to **every** class that implements `ITerrainQuery`.
  Required updates (non-exhaustive — check all `ITerrainQuery` implementors):

  - `ManualTerrainQuery` (`tests/simulation/ManualTerrainQuery.h`)
  - Any other mock or stub that implements `ITerrainQuery`

  ```cpp
  float getHeightAtWorld(float /*worldX*/, float /*worldZ*/) const override {
      return 0.0f;
  }
  ```

  > **Atomicity requirement**: All three of the following MUST land in the same commit:
  > (1) the `getHeightAtWorld()` pure-virtual addition to `ITerrainQuery.h`,
  > (2) the bilinear implementation in `TerrainSystem.h` / `TerrainSystem.cpp` (with
  > `#include "render_constants.h"` added and `using namespace RenderConstants;` at
  > function scope), and
  > (3) the `ManualTerrainQuery` no-op override (and any other `ITerrainQuery` stub).
  > Committing the interface alone leaves `TerrainSystem` abstract and breaks the build;
  > committing `TerrainSystem` without the stubs leaves `ManualTerrainQuery` abstract and
  > breaks all simulation unit tests. See
  > `architecture/testing/testability-architecture.md` §Phase 11q extension.
  > Same rule as Phase 10b `setTileHeight()` atomicity.

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
  same bilinear terrain query plus road bias used by `moveVehicleAgent()`. The `tileX`
  and `tileZ` values arrive as the 2nd and 3rd parameters of `spawnVehicleAgent()`; they
  are used here to compute `worldX`/`worldZ` for the bilinear terrain query. Add
  `using namespace RenderConstants;` at the start of `spawnVehicleAgent()` if not already
  present (check line ~3025; `moveVehicleAgent` at 3075 already has it but the two
  functions have separate bodies):

  ```cpp
  // Before:
  node->setPosition(irr::core::vector3df(spawnX, 0.0f, spawnZ));

  // After (add 'using namespace RenderConstants;' to spawnVehicleAgent body first):
  const float worldX = (static_cast<float>(tileX) + 0.5f) * kTileSize;
  const float worldZ = (static_cast<float>(tileZ) + 0.5f) * kTileSize;
  const float spawnY = m_terrain->getHeightAtWorld(worldX, worldZ) + kRoadSurfaceYBias;
  node->setPosition(irr::core::vector3df(spawnX, spawnY, spawnZ));
  ```

---

#### 8. Fix 2e — Vehicle terrain slope rotation in `moveVehicleAgent()` and `spawnVehicleAgent()`

**Root cause**

Vehicles are placed at the correct height (after Fix 2b) but always remain horizontally level,
causing them to float above or clip into sloped road surfaces. A vehicle driving up a hill should
pitch nose-up; one on a cambered road should roll slightly.

**Prerequisite**: Fix 2b (`getHeightAtWorld()`) must land before this deliverable.

**Approach**

Sample three terrain heights near the vehicle position (centre, +X, +Z) to compute finite-
difference tangent vectors. Their cross product gives the local terrain normal in world space.
Decompose the normal into pitch (X-axis rotation) and roll (Z-axis rotation) and apply them via
`node->setRotation()`, preserving the existing Y (yaw) from the vehicle's heading.

**Code changes**

- [ ] **`src/rendering/IrrlichtRenderer.cpp`** — In `moveVehicleAgent()`, immediately after
  computing `worldX`, `worldZ`, and `y` (the bilinear height), add:

  ```cpp
  // Terrain slope rotation — compute normal from three height samples.
  const float kStep = kTileSize * 0.5f;  // half-tile step for finite difference
  const float hRight = m_terrain->getHeightAtWorld(worldX + kStep, worldZ);
  const float hFront = m_terrain->getHeightAtWorld(worldX, worldZ + kStep);

  // Finite-difference slopes
  const float dhX = (hRight - y + kRoadSurfaceYBias) / kStep;
  const float dhZ = (hFront - y + kRoadSurfaceYBias) / kStep;

  // Terrain normal in Y-up space: N = (-dhX, 1, -dhZ), then normalize.
  const float nx  = -dhX;
  const float ny  =  1.0f;
  const float nz  = -dhZ;
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  const float pitchRad = std::atan2(-nz / len, ny / len);  // nose up/down
  const float rollRad  = std::atan2( nx / len, ny / len);  // lean left/right

  constexpr float kRadToDeg = 180.0f / 3.14159265f;
  const float pitchDeg = pitchRad * kRadToDeg;
  const float rollDeg  = rollRad  * kRadToDeg;

  // Preserve existing yaw (Y-axis heading); replace pitch and roll only.
  const float existingYaw = node->getRotation().Y;
  node->setRotation(irr::core::vector3df(pitchDeg, existingYaw, rollDeg));
  ```

  > **Note**: `y` here is the bilinear height BEFORE the bias is added (i.e., the raw terrain
  > height at the vehicle centre). Subtracting `kRoadSurfaceYBias` from the adjacent samples
  > is not needed because the bias is uniform — slope differences cancel it. Simplified:
  > `dhX = (hRight - (y - kRoadSurfaceYBias)) / kStep` where `y - kRoadSurfaceYBias` is the
  > raw terrain height. In practice, since `hRight` and `y-bias` both already include the road
  > offset uniformly, the delta is simply `(hRight - hCenter_raw) / kStep`. Ensure that `y`
  > in the computation refers to the **raw terrain height** (before bias is added) — rename
  > local variables if needed to avoid confusion.

- [ ] **`src/rendering/IrrlichtRenderer.cpp`** — Apply the same slope rotation in
  `spawnVehicleAgent()` (after Fix 2c adds the terrain query). The spawn node should match
  the rotation the first `moveVehicleAgent()` call will immediately set, so there is no single-
  frame pop from level-to-sloped orientation.

---

#### 9. Tests

**New test cases** — split by test infrastructure requirements:

**`tests/simulation/` (4 tests, `simulation_tests` CMake target, `unit` label)**
These tests are mock-based and require no OpenGL context or Irrlicht device.

- [ ] **`VehicleMeshPath_CommercialZone_ReturnsBusPath`** (`tests/simulation/`) — Include
  `vehicle_mesh_path.h`. Call `vehicleMeshPath(ZoneType::Commercial)` and assert with
  `EXPECT_THAT(path, EndsWith("bus_standard_lod0.b3d"))`. Do NOT use exact-string
  equality — the base prefix comes from `getAssetsDir()` which varies by install path.

- [ ] **`VehicleMeshPath_IndustrialZone_ReturnsTruckPath`** (`tests/simulation/`) — Include
  `vehicle_mesh_path.h`. Call `vehicleMeshPath(ZoneType::Industrial)` and assert with
  `EXPECT_THAT(path, EndsWith("truck_cargo_lod0.b3d"))`. Same rationale: suffix-only
  assertion avoids a runtime dependency on `getAssetsDir()` in tests.

- [ ] **`TrafficVehicle_SpawnOnUnzonedDestination_ZoneIsNotAlwaysResidential`**
  (`tests/simulation/`) — Spawn 100 vehicles with an unzoned destination tile.
  Assert that at least one vehicle has `zone != ZoneType::Residential` (verifying the
  proportional fallback in Fix 1b is exercised).

- [ ] **`TrafficVehicle_ZoneUpdated_OnTripCompletion`** (`tests/simulation/`) — Create
  a vehicle with `zone == ZoneType::Residential`. Set the destination tile's zone to
  `ZoneType::Commercial`. Call `doTrafficVehicleTick()` through a trip completion.
  Assert `v.zone == ZoneType::Commercial` after the tick.

**`tests/rendering/` (2 tests, `opengl_tests` CMake target, `requires-opengl` label)**
These tests must inspect a real Irrlicht scene-node `getPosition().Y` and therefore
require a live `IrrlichtRenderer` instance with an Irrlicht null or OpenGL device.

- [ ] **`CMakeLists.txt`** — Register the two new test `.cpp` files **inline in the
  `add_executable(opengl_tests ...)` call** (do NOT use `target_sources()` — `framework.md`
  prohibits it for `opengl_tests` to prevent ctest discovery timing issues). Then add
  both `src/rendering/` AND `tests/simulation/` to the `opengl_tests`
  `target_include_directories()`. Neither is currently present in the `opengl_tests` block
  (which contains only `src/interfaces/`); both must be added — `src/rendering/` was NOT
  added by Phase 5 as previously assumed, and `tests/simulation/` is required so the
  Y-bias tests can `#include "ManualTerrainQuery.h"`.

- [ ] **`MoveVehicleAgent_FlatTerrain_YIncludesRoadBias`** (`tests/rendering/`) — Create
  `IrrlichtRenderer` with a `ManualTerrainQuery` stub returning `0.0f` for all queries.
  Call `spawnVehicleAgent()` then `moveVehicleAgent()`. Retrieve the spawned scene node
  and assert `node->getPosition().Y == kRoadSurfaceYBias` (i.e. `0.25f`).

- [ ] **`SpawnVehicleAgent_FlatTerrain_YIncludesRoadBias`** (`tests/rendering/`) — Same
  setup. Call `spawnVehicleAgent()` (Fix 2c) and assert the newly created node's
  `getPosition().Y == kRoadSurfaceYBias` without any subsequent `moveVehicleAgent()` call.

---

### Affected Files Summary

| File | Lines | Change |
|---|---|---|
| `src/rendering/vehicle_mesh_path.h` | new file | `inline vehicleMeshPath(ZoneType, int variantIdx=0)` free function; base path via `getAssetsDir() + "/3d/vehicles/"` (includes `src/platform/PlatformUtils.h` read-only); Residential round-robins via `variantIdx % 3` |
| `src/rendering/render_constants.h` | new entries | Add `kTileSize = 10.0f` and `kRoadSurfaceYBias = 0.25f` inside `namespace RenderConstants` |
| `src/rendering/IrrlichtRenderer.h` | line 322 | Remove private `kTileSize` member (consolidated to `RenderConstants::kTileSize`) |
| `src/rendering/IrrlichtRenderer.cpp` | 1868 | Fix stale comment (0.10 m → 0.25 m) |
| `src/rendering/IrrlichtRenderer.cpp` | 1884 | Replace local `B` with `kRoadSurfaceYBias` |
| `src/rendering/IrrlichtRenderer.cpp` | 3025–3027 | Use bilinear terrain query + bias at spawn (Fix 2c) |
| `src/rendering/IrrlichtRenderer.cpp` | 3067–3072 | Add bilinear query + `kRoadSurfaceYBias` to vehicle Y; add pitch/roll from terrain normal (Fix 2b + Fix 2e) |
| `src/terrain/TerrainSystem.h` | — | Declare `getHeightAtWorld(float, float)` override |
| `src/terrain/TerrainSystem.cpp` | 826–833 | Implement `getHeightAtWorld()` with bilinear interpolation |
| `src/interfaces/ITerrainQuery.h` | — | Expose `getHeightAtWorld(float, float)` pure-virtual |
| `src/interfaces/IRenderer.h` | — | No change — `spawnVehicleAgent(AgentHandle, int, int, ZoneType)` 4-param signature already present from Phase 11d |
| `src/simulation/CitySimulation.cpp` | 1766–1808 | Re-assign zone on trip completion (Fix 1c) |
| `src/simulation/CitySimulation.cpp` | ~2450–2480 | Assign zone from destination tile at spawn (Fix 1b) |
| `src/main.cpp` | 339–440 | Add zone-change detection branch (`activeAgents.find(handle)->second.zone != a.zone` → despawn + respawn + audio lifecycle); `AgentAudioState` struct and initial zone population already present from Phase 11d |
| `tests/simulation/` | new cases | 4 unit tests: `vehicleMeshPath` Commercial/Industrial, zone assignment (proportional fallback), trip-completion zone-update |
| `tests/rendering/` | new cases | 2 renderer tests (`requires-opengl`): Y bias verification for `moveVehicleAgent` and `spawnVehicleAgent` |
| `CMakeLists.txt` | `opengl_tests` target | Add BOTH `src/rendering/` AND `tests/simulation/` to `target_include_directories`; neither is currently present (block contains only `src/interfaces/`); `src/rendering/` was NOT added by Phase 5 |
| `CMakeLists.txt` | `simulation_tests` target | Add `src/rendering/` to `target_include_directories` so `VehicleMeshPath` tests can `#include "vehicle_mesh_path.h"` |
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
- [ ] Vehicles pitch and roll to match road surface slope (verified visually and/or by asserting
  `node->getRotation().X != 0` on sloped terrain after `moveVehicleAgent()`).
- [ ] All 4 new `simulation_tests` (unit label) pass.
- [ ] Both new `opengl_tests` (requires-opengl label) pass.
- [ ] All existing `simulation_tests` and `opengl_tests` continue to pass.
- [ ] `all-checks-pass` CI job is green.
