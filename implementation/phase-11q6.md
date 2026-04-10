## Phase 11q6: Fix `despawnVehicleAgent` Use-After-Free + Service Building Inspection + Minimap Rotation + ASAN CI Integration + Audio Side-Channel Bugs

**Status: TODO**

**Prerequisite**: phase-11q5 merged.

### Goal

A SIGSEGV was captured in a core dump from `build_debug/aitown` at frame 15455
(~567 s runtime). The crash occurred inside Mesa's radeonsi driver during a
`memcpy` of vertex data, called from `COpenGLDriver::drawVertexPrimitiveList` →
`CMeshSceneNode::render()` → `CSceneManager::drawAll()` → `IrrlichtRenderer::drawScene()`.

Root cause: `despawnVehicleAgent` violates three invariants simultaneously:

1. **Null-before-remove**: the `m_agentNodes` entry is erased *after* `node->remove()`,
   leaving a transiently dangling pointer in the map.
2. **Shared-mesh lifetime**: multiple agents with the same zone and `handle % 3` variant
   share one `IAnimatedMesh*` from Irrlicht's scene-manager mesh cache. When the last
   node holding that mesh is despawned and `node->remove()` fires the `CMeshSceneNode`
   destructor, the mesh ref-count may hit zero and the mesh is freed. All other live
   nodes sharing the same `.b3d` path then hold dangling `IAnimatedMesh*` pointers. The
   next `drawAll()` passes freed vertex data to the GL driver → SIGSEGV in `memcpy`.
3. **C-4 violation**: `getMaterial(i)` is re-called in the inner `t` loop instead of
   being cached as `SMaterial&` in the outer `i` loop, as required by constraint C-4 in
   `SceneEntityManager.h`.

Five additional pre-existing bugs were discovered during or after the investigation:

- **UI — minimap rotation**: the minimap is rotated 90° anti-clockwise relative to the
  game world, making it visually incorrect. It must be rotated 90° clockwise around its
  centre to align with the world orientation.
- **UI — service building inspection**: when the player inspects a tile occupied by
  a service building (fire station, police station, power plant, water tower)
  via the Query/Inspector panel, the tile is displayed as "Unzoned" instead of showing
  the service building type and its coverage status. The simulation query path
  (`Zoning::queryTile()`) correctly populates `QueryResult::serviceType`, but
  `InspectorPanel::populate()` in `src/ui/QueryPanel.cpp` lacks an
  `else if (result.serviceType != ServiceBuildingType::None)` branch — the code
  falls from the `isRoad` branch directly to the `else` "Unzoned" case.
- **Audio — iterator fragility** in the zone-change respawn path in `main.cpp`: the
  iterator `it` obtained via `find` is used to read `idleIdx`/`moveIdx` after
  `sys.activeAgents[handle]` writes, which could trigger a rehash on a future insertion
  that invalidates `it`. Safe today only by coincidence.
- **Audio — stale `listenerDistanceSq`**: `acquireVehicleEnginePair` initialises
  `listenerDistanceSq = 0.f` and never updates it, defeating the distance-based
  eviction heuristic at full pool capacity.
- **Audio — idle engine inaudible**: `speedFraction` is hardcoded to `1.0f` at
  `main.cpp` line 391, making `gainIdle = 1.0f - speed = 0` always, so
  `SFX_VEHICLE_ENGINE_IDLE` wastes a pool slot with no audible output.

This phase also adds a standalone ASAN CI workflow (the tests introduced here
are only reliable regression gates when ASAN is active) and adds three
regression-test groups.

---

### Issues to Fix

#### 1. `src/rendering/IrrlichtRenderer.cpp` — `despawnVehicleAgent` (lines ~3203–3223)

**Current (buggy):**

```cpp
void IrrlichtRenderer::despawnVehicleAgent(AgentHandle handle)
{
    auto it = m_agentNodes.find(handle);
    if (it == m_agentNodes.end()) return;

    IMeshSceneNode* node = it->second;
    if (node) {
        for (u32 i = 0; i < node->getMaterialCount(); ++i) {
            for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                node->getMaterial(i).setTexture(t, nullptr);   // C-4 violation
            }
        }
        if (m_driver) m_driver->setMaterial(SMaterial{});
        node->remove();       // may free shared mesh → dangling ptrs in other nodes
    }
    m_agentNodes.erase(it);   // erased AFTER remove — violates null-before-remove
}
```

**Required fix** — three changes together:

1. Erase the map entry **before** `node->remove()` (null-before-remove).
2. `grab()` the shared `IAnimatedMesh*` **before** `node->remove()` and `drop()` it
   **after**, so the scene-manager cache cannot free the mesh while other nodes still
   reference the same variant.
3. Cache `SMaterial& mat = node->getMaterial(i)` in the outer loop (C-4).

```cpp
void IrrlichtRenderer::despawnVehicleAgent(AgentHandle handle)
{
    auto it = m_agentNodes.find(handle);
    if (it == m_agentNodes.end()) return;

    IMeshSceneNode* node = it->second;

    // Null-before-remove: erase map entry before any side-effects of node->remove().
    it->second = nullptr;
    m_agentNodes.erase(it);

    if (node) {
        // Grab the shared mesh so the scene-manager cache cannot evict it while
        // other agent nodes for the same variant are still alive (shared-mesh fix).
        // getMesh() returns IMesh*; grab()/drop() are available directly via IReferenceCounted.
        IMesh* sharedMesh = node->getMesh();
        if (sharedMesh) sharedMesh->grab();

        // C-4: getMaterial(i) called exactly once per outer iteration.
        for (u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = node->getMaterial(i);
            for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t)
                mat.setTexture(t, nullptr);
        }
        if (m_driver) m_driver->setMaterial(SMaterial{});
        node->remove();  // safe: mesh ref-count held by grab above

        if (sharedMesh) sharedMesh->drop();  // release; mesh stays alive if others hold it
    }
}
```

- [x] `m_agentNodes` entry erased before `node->remove()`.
- [x] `sharedMesh->grab()` called before `node->remove()`; `sharedMesh->drop()` called after.
- [x] `getMaterial(i)` cached as `SMaterial&` in outer loop (C-4 fixed).
- [x] `architecture/graphics-architecture/scene-graph-ownership.md` updated to document: (a) the null-before-remove invariant for `despawnVehicleAgent` (erase map entry before `node->remove()`), and (b) the `grab()`/`drop()` exception — a temporary hold on the shared `IAnimatedMesh*` to prevent premature mesh deallocation when multiple agent nodes share the same variant's cached mesh.
- [x] `make build` succeeds with no new warnings.
- [x] All existing renderer and agent-node tests pass unchanged.

---

#### 2. `src/rendering/IrrlichtRenderer.cpp` — Audit `evictUnreferenced()` for vehicle atlas

`SceneEntityManager::destroy()` mandates Step 3: `textureCache->evictUnreferenced()`
after clearing texture slots. `despawnVehicleAgent` bypasses `SceneEntityManager`
entirely. Confirm whether `vehicles_diffuse_atlas_d.png` is managed by any
`TextureCache` linear pool:

- If yes: add a `TextureCache* m_vehicleTextureCache` member (or reuse the existing
  linear-pool reference) and call `m_vehicleTextureCache->evictUnreferenced()` after
  the texture-slot clear loop, matching the 4-step spec sequence.
- If no (texture loaded via raw `IVideoDriver::getTexture()` outside any `TextureCache`):
  document this in a comment inside `despawnVehicleAgent` and add a note to
  `architecture/graphics-architecture/texture-cache.md`.

- [x] `vehicles_diffuse_atlas_d.png` TextureCache membership confirmed (yes/no).
- [x] If yes: `evictUnreferenced()` added to `despawnVehicleAgent` after texture-slot clear.
- [x] If no: comment added explaining why Step 3 is omitted here; texture-cache.md updated.

---

#### 3. `src/main.cpp` — Audio iterator fragility in zone-change respawn path

In `runFrame()` (lines ~357–396), the iterator `it` obtained via
`sys.activeAgents.find(handle)` is reused after `sys.activeAgents[handle].idleIdx = ...`
writes. `operator[]` on an existing key does not invalidate iterators per the standard,
but relying on this is fragile and will silently misread stale data if the map ever
rehashes (e.g., future code adds an insert before this block). Fix: refresh `it` via a
second `find` after the zone-change write block.

```cpp
// After the zone-change block that writes sys.activeAgents[handle]:
it = sys.activeAgents.find(handle);   // refresh iterator
```

- [x] Iterator `it` refreshed via second `find` after the zone-change write block.
- [x] All agent-sync integration tests pass unchanged.

---

#### 4. `src/audio/AudioSystem.cpp` — `listenerDistanceSq` never updated after acquire

`acquireVehicleEnginePair` initialises `m_sfxSlots[idleIdx].listenerDistanceSq = 0.f`
and never updates it. The eviction heuristic reads this field to pick the farthest
vehicle to evict, but because it is always 0, eviction is order-of-scan rather than
distance-based.

Fix: in `updateVehicleAudio`, after updating `worldX`/`worldZ`, update `listenerDistanceSq`
in BOTH `AudioSystem::m_sfxSlots` (used by `updateVehicleEngines`) AND the pool's own
`PoolSFXEntry` copy (used by `AudioSourcePool::findEvictionCandidate` for both one-shot SFX
eviction and vehicle-pair eviction):

```cpp
m_sfxSlots[idleIdx].listenerDistanceSq = distSq;
m_sfxSlots[moveIdx].listenerDistanceSq = distSq;
m_pool.updateSFXSlotDistance(idleIdx, distSq);
m_pool.updateSFXSlotDistance(moveIdx, distSq);
m_pool.updateVehiclePairDistance(i, distSq);   // i = pair-slot index from m_vehicleAudio scan
```

where `distSq` is computed inside `updateVehicleAudio` as `(worldX - m_listenerX)² + (worldZ - m_listenerZ)²`; `m_listenerX` and `m_listenerZ` are new `float` members of `AudioSystem` updated each frame by `syncListenerToCamera` (which already receives the listener's world-space position).

Note: `AudioSourcePool::m_sfxSlots` and `AudioSystem::m_sfxSlots` are **separate arrays**.
`findEvictionCandidate` reads the pool's copy, so both must be kept in sync.
`m_pool.updateVehiclePairDistance(i, distSq)` updates `VehiclePairSlot.listenerDistanceSq`
in the pool's `m_vehiclePairs[i]` entry — this is the field read by the vehicle-pair eviction
heuristic in `acquireVehicleEnginePair` (see `source-pool.md §updateVehiclePairDistance`).

- [x] `m_listenerX` and `m_listenerZ` added as `float` members of `AudioSystem`;
  `syncListenerToCamera` updated to write `m_listenerX = cam.position.x;` and
  `m_listenerZ = cam.position.z` after setting `AL_POSITION` — these are the listener
  coordinates read by `updateVehicleAudio` to compute `distSq`.
- [x] `listenerDistanceSq` written in `updateVehicleAudio` for both idle and move slots
  (both `AudioSystem::m_sfxSlots` and `m_pool.updateSFXSlotDistance`).
- [x] `m_pool.updateVehiclePairDistance(i, distSq)` called in the same `m_vehicleAudio[i]`
  scan loop to keep `VehiclePairSlot.listenerDistanceSq` current for vehicle-pair eviction.
- [x] Eviction candidate selection uses the updated distance at full pool capacity.
- [x] All `AudioSystem` unit tests pass unchanged.

---

#### 5. `src/main.cpp` — `speedFraction` hardcoded to `1.0f` (idle engine permanently inaudible)

At `main.cpp` line ~391:

```cpp
const float speedFraction = 1.0f;   // ← hardcode — idle gain always 0
```

`gainIdle = 1.0f - speedFraction = 0` always, making `SFX_VEHICLE_ENGINE_IDLE`
permanently inaudible. Fix: derive `speedFraction` from actual agent velocity.
The agent has `(tileX, tileZ)` position; compare to previous-frame position stored
in `AgentAudioState` (or a new `prevTileX`/`prevTileZ` pair) and divide by expected
max speed, clamping to `[0.0f, 1.0f]`.

If per-agent previous-position tracking is too invasive for this phase, at minimum
replace the hardcode with a comment and a TODO ticket; the idle/move blend will be
addressed in a dedicated audio-polish phase. The hardcode must not stay silently.

- [x] `speedFraction = 1.0f` hardcode replaced with velocity-derived value,
  OR replaced with `/* TODO phase-11q7: derive from agent velocity */` and a
  tracking issue opened.
- [x] If replaced with velocity: `gainIdle` audible at low agent speed in manual test.

---

#### 6. `src/ui/QueryPanel.cpp` — Service building shows as "Unzoned" when inspected

When the player clicks on a tile occupied by a service building (fire station,
police station, power plant, water tower) the Query/Inspector panel
displays "Unzoned". The simulation query path already works correctly:
`Zoning::queryTile()` populates `QueryResult::serviceType` with the correct
`ServiceBuildingType` when the queried tile falls within a service building
footprint (see `src/simulation/Zoning.cpp` lines 172-180). The bug is purely
in the display layer: `InspectorPanel::populate()` in `src/ui/QueryPanel.cpp`
(around lines 273-278) checks `isRoad` first, then falls directly to the
`else` "Unzoned" case without an intermediate branch for service buildings.

**Fix**: in `InspectorPanel::populate()`, add an
`else if (result.serviceType != ServiceBuildingType::None)` branch between
the `isRoad` branch and the `else` "Unzoned" fallback. This branch should
display:

- Building type name (e.g. "Fire Station", "Police Station",
  "Power Plant", "Water Tower")
- Coverage radius and whether this tile is currently powered / water-covered
  (where applicable)
- Construction cost (read-only, for reference)

No changes to `ICitySimulation.h`, `MockCitySimulation`, or the simulation
query path are needed — the data is already present in `QueryResult`.

- [x] `InspectorPanel::populate()` has the
  `else if (result.serviceType != ServiceBuildingType::None)` branch
  between the `isRoad` branch and the `else` "Unzoned" fallback.
- [x] Querying a tile inside a service building footprint shows service
  building type (not "Unzoned") in the inspector panel.
- [x] Querying a tile outside all service building footprints is unaffected.
- [x] At least one unit test in `tests/ui/query_panel_test.cpp` (target `ui_tests`, label `unit`) covers the "tile inside service building footprint → inspector shows service building type (not Unzoned)" path via a `MockUIBackend` fixture that drives `InspectorPanel::populate()`; `query_panel_test.cpp` is already registered in `ui_tests` via `target_sources` so no new CMake changes needed.
- [x] All existing tests pass unchanged.

---

#### 7. `src/ui/Minimap.cpp` — Minimap orientation does not track camera yaw

The minimap uses a static world-space mapping and ignores `m_cameraState.yaw`.
The camera-forward direction is not aligned with the minimap's top edge, making
navigation confusing when the camera is rotated. The minimap should rotate so
that the camera's forward direction always points toward the top of the minimap,
with a compass North indicator showing the current world orientation.

**Design**: camera-centred, camera-forward = minimap top.

- The **camera target** `(targetX, targetZ)` maps to the minimap **centre pixel**
  `(kMapX + 100, kMapY + 100)`.
- All tile positions are expressed relative to the camera target and rotated by
  `-yaw_rad` (counter-rotating the world so the camera's forward direction ends
  up at the top).
- A **North indicator** — a small filled rect labelled "N", or a small arrow — is
  drawn at the minimap border at angular offset `yaw_rad` from the top:
  `(kMapX + 100 + 90*sinf(yaw_rad), kMapY + 100 - 90*cosf(yaw_rad))`.
  This gives the player an absolute bearing reference regardless of camera rotation.
- The **viewport indicator** stays centred at `(kMapX+100, kMapY+100)` — since the
  camera target is always the centre, no translation is needed. The side-length
  formula from Phase 11p is unchanged:
  `side = 200 × (zoomDistance / kMaxZoomDistance)`, clamped to [8, 190].

**Tile-drawing loop** (replace the current `px = kMapX + x * tileW` pattern
in `drawOverlay()` for zone tiles, road tiles, service-coverage overlay, and
traffic-congestion overlay):

```cpp
static constexpr float kDegToRad = 3.14159265f / 180.0f;
const float yawRad  = m_cameraState.yaw * kDegToRad;
const float cosA    = cosf(-yawRad);   // negative: counter-rotate world beneath camera
const float sinA    = sinf(-yawRad);
const float scaleX  = static_cast<float>(kMapW) / worldW;   // px/m
const float scaleZ  = static_cast<float>(kMapH) / worldD;
const float centreX = kMapX + kMapW * 0.5f;
const float centreZ = kMapY + kMapH * 0.5f;

// Per tile (tx, tz):
const float wx   = tx * kTileSize;          // world metres
const float wz   = tz * kTileSize;
const float relX = wx - m_cameraState.targetX;
const float relZ = wz - m_cameraState.targetZ;
const float rotX = relX * cosA - relZ * sinA;
const float rotZ = relX * sinA + relZ * cosA;
const int   px   = static_cast<int>(centreX + rotX * scaleX);
const int   py   = static_cast<int>(centreZ + rotZ * scaleZ);
// Cull tiles whose pixel falls outside the minimap area:
if (px < kMapX || px >= kMapX + kMapW || py < kMapY || py >= kMapY + kMapH) continue;
```

Use `tileW` × `tileH` as the per-tile rect size (unchanged). The `cosA`/`sinA`
constants should be computed once before the tile loop, not inside it.

**Click-to-pan** (replace `m_panCallback(fracX * worldW, fracZ * worldD)`):

```cpp
// Offset of click from minimap centre in pixels, converted to metres
const float offX = (static_cast<float>(e.x) - centreX) / scaleX;
const float offZ = (static_cast<float>(e.y) - centreZ) / scaleZ;
// Rotate click offset back to world space (+yaw undoes the -yaw tile rotation)
const float worldOffX = offX * cosf(yawRad) + offZ * sinf(yawRad);
const float worldOffZ = -offX * sinf(yawRad) + offZ * cosf(yawRad);
m_panCallback(m_cameraState.targetX + worldOffX, m_cameraState.targetZ + worldOffZ);
```

Note: verify the rotation sign produces correct behaviour (clicking top-of-minimap
pans toward camera-forward direction) with a manual test before finalising.

- [x] Tile-drawing loop in `drawOverlay()` (zone tiles and road tiles) updated to
  rotate around the minimap centre using `-yaw_rad`; camera target = centre pixel.
- [x] `cosA`/`sinA` computed once before the tile loop (not per-tile).
- [x] Tiles culled when their pixel position falls outside `[kMapX, kMapX+kMapW) ×
  [kMapY, kMapY+kMapH)`.
- [x] Viewport indicator remains centred at `(kMapX+100, kMapY+100)`; side-length
  formula unchanged from Phase 11p; four-strip draw unchanged.
- [x] North indicator rendered in `drawOverlay()`: small "N" text or filled rect
  at `(kMapX + 100 + 90*sinf(yaw_rad), kMapY + 100 - 90*cosf(yaw_rad))`,
  white fill, drawn after zone/road tiles.
- [x] Service-coverage overlay loop updated with the same per-tile rotation.
- [x] Traffic-congestion overlay loop updated with the same per-tile rotation.
- [x] Click-to-pan handler in `onEvent()` updated to unrotate click offset by
  `+yaw_rad` before adding to `targetX`/`targetZ`.
- [ ] Manual visual check: whatever direction the camera faces, that direction is
  at the top of the minimap; North indicator moves as camera rotates.
- [x] `architecture/ui-ux/minimap.md` updated: camera-following coordinate mapping
  documented; camera target = centre pixel; North indicator formula; updated
  click-to-pan formula; viewport indicator centred at (100, 100).
- [x] All existing minimap unit tests pass; update expected pixel coordinates in
  any test that asserts specific pixel positions.

---

### New Tests

New test files are distributed across four targets based on their GL and audio dependencies:

- **`opengl_tests`**: Tests A, C, and all additional `AgentDespawnRenderTest` cases (require an OpenGL context). Per `framework.md` line 126, source files MUST be listed **inline** in the existing `add_executable(opengl_tests ...)` block in `CMakeLists.txt` — NOT via `target_sources()`.
- **`integration_tests`**: Test B (`SceneEntityManagerDestroyOrderTest`, uses EDT_NULL device) — add via `target_sources(integration_tests PRIVATE ...)`.
- **`integration_tests`** (audio): `VehicleReleaseTest` — add via `target_sources(integration_tests PRIVATE ...)`
  (uses real `AudioSystem` + null-backend thread → integration test, not unit test).
- **`ui_tests`**: Issue 6 unit test (service building inspection display, label `unit`) — add to `tests/ui/query_panel_test.cpp` via `MockUIBackend` fixture; `query_panel_test.cpp` is already registered in `ui_tests` so no new CMake changes needed.

All tests that call `m_smgr->drawAll()` (or `IrrlichtRenderer::drawScene()`) must
wrap that call in a `driver->beginScene(true, true, SColor(255,0,0,0))` /
`driver->endScene()` frame boundary. Irrlicht may silently skip vertex submission
without this, producing false-green results.

Tests A and C are only reliable regression gates when built with ASAN (see §6 below).
Without ASAN, a freed page that has not yet been reclaimed may still be readable,
reproducing the "no crash despite UAF" scenario from production (frame 15455).

#### Test A — `tests/rendering/AgentDespawnRenderTest.cpp`

**`AgentDespawnRenderTest::DespawnThenDrawScene_Clean`** (`requires-opengl`)

Directly reproduces the crash path: despawn one agent, then draw the scene.

```text
1. Create 1×1 EDT_OPENGL device + IrrlichtRenderer (ManualTerrainQuery pattern from VehicleYBiasTest.cpp).
2. spawnVehicleAgent(handle=0, zone=Residential).
3. if (!agentNodeForTest(0)) { GTEST_SKIP() << "vehicle mesh asset not found"; }
4. despawnVehicleAgent(handle=0).
5. driver->beginScene(true, true, SColor(255,0,0,0));
6. smgr->drawAll();                         // would crash before fix
7. driver->endScene();
8. SUCCEED() — reaching here means no crash; ASAN clean exit confirms no UAF.
```

- [x] Test file `tests/rendering/AgentDespawnRenderTest.cpp` created.
- [x] Added inline to `add_executable(opengl_tests ...)` in `CMakeLists.txt`.
- [x] `GTEST_SKIP()` asset guard present.
- [x] `beginScene`/`endScene` wrapper present around `drawAll()`.
- [x] Passes on fixed code; would fault on unfixed code under ASAN.

#### Test B — `tests/integration/SceneEntityManagerDestroyOrderTest.cpp`

**`SceneEntityManagerDestroyOrderTest::NullsEntityBeforeNodeRemove`** (`integration` label)

Verifies the null-before-remove ordering contract on `SceneEntityManager::destroy()`.
This is the canonical eviction pattern — testing it catches future regressions in any
eviction path that delegates to `SceneEntityManager`.

Uses EDT_NULL device (no GL context needed). Add to `integration_tests` target, not
`opengl_tests`.

```text
1. Create EDT_NULL device + scene manager.
2. Create a minimal TestEntity struct with setNode(ISceneNode*) that records:
     bool nullWasSetBeforeRemoveWasCalled = false;
   Override a subclass of ISceneNode (or use a custom node) so remove() sets a flag
   and asserts entity.getNode() == nullptr at that moment.
   Practical alternative: after SceneEntityManager::destroy(entity) returns, simply
   assert entity.getNode() == nullptr — which is the observable postcondition.
3. Call SceneEntityManager::destroy(entity).
4. ASSERT_EQ(entity.getNode(), nullptr).
```

- [x] Test file `tests/integration/SceneEntityManagerDestroyOrderTest.cpp` created.
- [x] Added via `target_sources(integration_tests PRIVATE tests/integration/SceneEntityManagerDestroyOrderTest.cpp)` in `CMakeLists.txt` with label `integration`.
- [x] Uses EDT_NULL — no xvfb dependency.
- [x] Passes on current `SceneEntityManager::destroy()` implementation.

#### Test C — `tests/rendering/SharedMeshRefCountTest.cpp`

**`SharedMeshRefCountTest::LastAgentDespawn_OtherNodesUnaffected`** (`requires-opengl`)

Reproduces the shared-mesh crash: two agents sharing the same `.b3d` path, first
despawned, second must still render without fault.

The two agents MUST share the same `IAnimatedMesh*` from the scene-manager cache.
This requires same zone type AND same `handle % 3` variant index. Use `handle=0` and
`handle=3` (both `% 3 == 0`, both `ZoneType::Residential` → same `car_sedan_lod0.b3d`).

```text
1. Create 1×1 EDT_OPENGL device + IrrlichtRenderer (VehicleYBiasTest pattern).
2. spawnVehicleAgent(handle=0, zone=Residential).
3. spawnVehicleAgent(handle=3, zone=Residential).
4. if (!agentNodeForTest(0) || !agentNodeForTest(3)) { GTEST_SKIP() << "mesh assets not found"; }
5. ASSERT_EQ(agentNodeForTest(0)->getMesh(), agentNodeForTest(3)->getMesh())
      << "precondition: both nodes must share the same IAnimatedMesh*";
6. despawnVehicleAgent(handle=0).
7. driver->beginScene(true, true, SColor(255,0,0,0));
8. smgr->drawAll();                         // would crash before fix (node 3 dangling)
9. driver->endScene();
10. SUCCEED() — ASAN clean exit confirms mesh still alive for node 3.
```

- [x] Test file `tests/rendering/SharedMeshRefCountTest.cpp` created.
- [x] Added inline to `add_executable(opengl_tests ...)` in `CMakeLists.txt`.
- [x] Handles 0 and 3 used (same `% 3` remainder, same zone).
- [x] `ASSERT_EQ(node0->getMesh(), node3->getMesh())` precondition check present.
- [x] `GTEST_SKIP()` asset guard present.
- [x] `beginScene`/`endScene` wrapper present.
- [x] Passes on fixed code; would fault on unfixed code under ASAN.

#### Additional Tests — `tests/rendering/AgentDespawnRenderTest.cpp` (same file as Test A)

**`AgentDespawnRenderTest::DespawnNonexistentHandle_NoOp`** (`requires-opengl`)

```text
1. Create IrrlichtRenderer, do NOT call spawnVehicleAgent.
2. despawnVehicleAgent(handle=99).
3. SUCCEED() — early-return guard must not crash.
```

**`AgentDespawnRenderTest::DespawnAllAgents_DrawScene_Clean`** (`requires-opengl`)

```text
1. Spawn handles 0, 1, 2 (variants 0, 1, 2 — three distinct meshes).
2. if any node is null: GTEST_SKIP().
3. despawnVehicleAgent(0), despawnVehicleAgent(1), despawnVehicleAgent(2).
4. beginScene / drawAll / endScene.
5. SUCCEED().
```

**`AgentDespawnRenderTest::SpawnSameHandleTwice_NoLeak`** (`requires-opengl`)

```text
1. spawnVehicleAgent(handle=0, zone=Residential).
2. spawnVehicleAgent(handle=0, zone=Commercial).  // triggers replace-guard despawn internally
3. if (!agentNodeForTest(0)) { GTEST_SKIP(); }
4. ASSERT_EQ(m_agentNodes.count(0), 1u)  // only one entry, no leak.
   — access via agentNodeForTest accessor.
5. beginScene / drawAll / endScene.
6. SUCCEED().
```

- [x] `DespawnNonexistentHandle_NoOp` implemented and passes.
- [x] `DespawnAllAgents_DrawScene_Clean` implemented and passes.
- [x] `SpawnSameHandleTwice_NoLeak` implemented and passes.

#### Audio regression tests — `tests/integration/VehicleReleaseTest.cpp` (new file, `integration_tests` target)

This test constructs a real `AudioSystem` with the null OpenAL Soft backend (`ALSOFT_DRIVERS=null`)
and a running audio background thread. Per `framework.md`, tests using a real audio driver belong
in `integration_tests` (label `integration`), NOT `audio_tests` (label `unit`).

**Synchronization contract**: the audio background thread runs normally on the null backend.
After `releaseVehicleEnginePair`, the test polls AL source state in a bounded loop (max 200 ms,
5 ms sleep per iteration) until `AL_STOPPED` is observed or the timeout elapses — at which point
the test fails. AL source handles are obtained through a `#ifdef AITOWN_TESTING_ENABLED` test
accessor on the concrete `AudioSystem` class (same pattern as `Population::testForceUnlockDensityTier`
in `Population.h`). The accessor is `unsigned int testGetSourceHandle(int poolIdx) const` and returns
`m_sources[poolIdx]`. This avoids exposing AL handles in the production interface.

**`AudioSystemVehicleReleaseTest::SourceStoppedAfterRelease`**

```text
1. Acquire a pair via acquireVehicleEnginePair(zone) → (idleIdx, moveIdx).
2. Obtain AL handles: srcIdle = audio.testGetSourceHandle(idleIdx),
                      srcMove = audio.testGetSourceHandle(moveIdx).
3. Wait for audio thread to process pendingInit: poll alGetSourcei(srcIdle/srcMove, AL_SOURCE_STATE)
   == AL_PLAYING (max 200 ms / 5 ms intervals).
4. releaseVehicleEnginePair(idleIdx, moveIdx).
5. Poll alGetSourcei(srcIdle/srcMove, AL_SOURCE_STATE) until AL_STOPPED
   (max 200 ms / 5 ms intervals); fail if timeout elapses.
6. ASSERT alGetSourcei(srcIdle, AL_SOURCE_STATE) == AL_STOPPED.
7. ASSERT alGetSourcei(srcIdle, AL_BUFFER) == 0 (buffer detached).
8. Repeat assertions 6–7 for srcMove.
```

**`AudioSystemVehicleReleaseTest::SlotReacquirableAfterRelease`**

```text
1. Acquire a pair via acquireVehicleEnginePair(zone) → (idleIdx, moveIdx).
2. Obtain AL handles: srcIdle = audio.testGetSourceHandle(idleIdx),
                      srcMove = audio.testGetSourceHandle(moveIdx).
3. Wait for audio thread to process pendingInit: poll alGetSourcei(srcIdle/srcMove, AL_SOURCE_STATE)
   == AL_PLAYING (max 200 ms / 5 ms intervals).
4. releaseVehicleEnginePair(idleIdx, moveIdx).
5. Poll alGetSourcei(srcIdle/srcMove, AL_SOURCE_STATE) until AL_STOPPED
   (max 200 ms / 5 ms intervals); fail if timeout elapses.
6. Re-acquire via acquireVehicleEnginePair(zone) → (idleIdx2, moveIdx2).
7. ASSERT idleIdx2 >= 0 && moveIdx2 >= 0 — pool slot is genuinely free and reusable.
8. (Optional) ASSERT idleIdx2 == idleIdx && moveIdx2 == moveIdx — released slots are recycled.
```

- [x] Test source file `tests/integration/VehicleReleaseTest.cpp` created.
- [x] Added via `target_sources(integration_tests PRIVATE tests/integration/VehicleReleaseTest.cpp)` in `CMakeLists.txt`.
- [x] `AudioSystem` exposes `unsigned int testGetSourceHandle(int poolIdx) const` under `#ifdef AITOWN_TESTING_ENABLED`
  (returns `m_sources[poolIdx]`); declared in `AudioSystem.h` alongside `Population::testForceUnlockDensityTier`.
- [x] `CMakeLists.txt` adds `target_compile_definitions(aitown_audio PRIVATE AITOWN_TESTING_ENABLED=1)` and
  `target_compile_definitions(integration_tests PRIVATE AITOWN_TESTING_ENABLED=1)` so the `#ifdef`-guarded
  accessor is compiled into the library and visible to the test TU (same pattern as `aitown_ui`/`ui_tests`).
- [x] `CMakeLists.txt` adds `target_link_libraries(integration_tests PRIVATE aitown_audio)` if not already present.
- [x] `CMakeLists.txt` adds `target_link_libraries(integration_tests PRIVATE OpenAL::OpenAL)` so
  `alGetSourcei()` calls in VehicleReleaseTest compile (`OpenAL::OpenAL` is `PRIVATE` on `aitown_audio`
  and its headers do not propagate to consumers).
- [x] `AudioSystemVehicleReleaseTest` fixture: `SetUp()` constructs `AudioSystem(nullptr, &clock_, nullptr)`
  — `logger=nullptr` (falls back to stderr), `clock_` is a `ManualClock` member, `alcFunctions=nullptr`
  (activates `DefaultAlcFunctions`, real ALC). `TearDown()` destructs `AudioSystem` (joins audio thread).
  `clock_` is declared as `ManualClock clock_;` in the fixture class.
- [x] `cmake/AitownTestHelpers.cmake`: extend the `aitown_add_tests` macro to accept an
  optional `ENVIRONMENT` keyword (e.g. `ENVIRONMENT "ALSOFT_DRIVERS=null"`) forwarded as
  `PROPERTIES ENVIRONMENT` in the underlying `gtest_discover_tests()` call.
  `set_tests_properties()` MUST NOT be used — per the `AitownTestHelpers.cmake` header comment,
  it targets only the statically-created wrapper test, not individually-discovered test cases
  under `DISCOVERY_MODE PRE_TEST`, so the property silently fails to propagate.
- [x] `aitown_add_tests(integration_tests LABEL "integration")` in `CMakeLists.txt` updated to
  `aitown_add_tests(integration_tests LABEL "integration" ENVIRONMENT "ALSOFT_DRIVERS=null")`
  so all `integration_tests` test cases inherit the null audio backend (consistent with the CI
  integration-test step `env:` block which already sets `ALSOFT_DRIVERS=null` globally for all
  integration tests).
- [x] `architecture/testing/framework.md` updated: (a) `aitown_add_tests()` macro definition
  extended with optional `ENVIRONMENT` keyword (parsed via `cmake_parse_arguments`, forwarded
  as `PROPERTIES ENVIRONMENT` in `gtest_discover_tests()`); (b) `integration_tests` example
  updated to `aitown_add_tests(integration_tests LABEL "integration" ENVIRONMENT "ALSOFT_DRIVERS=null")`;
  (c) Phase 11q6 test files (`AgentDespawnRenderTest.cpp`, `SharedMeshRefCountTest.cpp`) added
  as inline commented entries inside `add_executable(opengl_tests ...)`.
- [x] `AudioSystem` constructed with `ALSOFT_DRIVERS=null` null backend; audio background thread runs normally.
- [x] All AL state assertions use bounded polling (max 200 ms / 5 ms intervals); test fails on timeout.
- [x] `SourceStoppedAfterRelease` implemented; asserts AL_STOPPED and AL_BUFFER==0 after bounded poll.
- [x] `SlotReacquirableAfterRelease` implemented; confirms pool slot recycled.
- [x] Both tests pass under the null-driver / headless audio seam.

#### Spec Update — Test Catalogue

- [x] `architecture/testing/testability-architecture.md` updated: fixture definitions for `AgentDespawnRenderTest`, `SharedMeshRefCountTest`, `SceneEntityManagerDestroyOrderTest`, and `AudioSystemVehicleReleaseTest` added; test catalogue table extended with all 9 new test cases (fixture name, canonical test name, source file, CMake target, label); the existing `query_panel_test.cpp` fixture row is extended with the Issue 6 service-building inspection test case (`InspectorPanel::populate()` shows service building type instead of "Unzoned" for tiles inside a service building footprint); CTest filter command added for Phase 11q6 tests.

---

### 6. ASAN CI Integration (Standalone Workflow)

Tests A and C are only reliable regression gates under AddressSanitizer. ASAN
requires compile-time instrumentation, so it needs a standalone reusable workflow
(`_asan-linux.yml`) with its own configure → build → test pipeline.

ASAN must NOT be mixed with `--coverage` instrumentation — the ASAN runtime and
gcov runtime share shadow memory and produce spurious results.

#### 6a. New CMake preset `ci-linux-asan` in `CMakePresets.json`

```json
{
  "name": "ci-linux-asan",
  "inherits": "ci-linux",
  "cacheVariables": {
    "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
    "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined",
    "ENABLE_COVERAGE": "OFF",
    "CMAKE_C_COMPILER_LAUNCHER": "",
    "CMAKE_CXX_COMPILER_LAUNCHER": ""
  }
}
```

`ci-linux` inherits `base` which sets `CMAKE_C_COMPILER_LAUNCHER=ccache` and
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`. ASAN-instrumented object files must not be shared
with non-ASAN builds via the ccache key, so ccache is explicitly disabled in
`ci-linux-asan` by clearing both launcher variables. Cold-cache compilation is accepted
within the 45-minute timeout budget.

- [x] `ci-linux-asan` preset added to `CMakePresets.json`.
- [x] `ci-linux-asan` preset sets `CMAKE_C_COMPILER_LAUNCHER: ""` and
  `CMAKE_CXX_COMPILER_LAUNCHER: ""` to disable ccache (prevents ASAN-instrumented objects
  from cross-contaminating the shared ccache with non-ASAN builds).

#### 6b. New `_asan-linux.yml` reusable workflow

`_test-linux.yml` downloads pre-built artifacts and has no configure or build steps.
ASAN requires compile-time instrumentation (`-fsanitize=address,undefined`), so ASAN
needs its own full configure → build → test pipeline as a standalone reusable workflow
`.github/workflows/_asan-linux.yml`.

`_asan-linux.yml` runs inside the GHCR container `ghcr.io/m0wa/aitown-ci-linux`
(the same image as `_build-linux.yml` and `_coverage-linux.yml`) with
`options: --user root`. This is required because the configure step uses
`/opt/vcpkg_installed` which is pre-baked into that container image.

`_asan-linux.yml` must:

1. Check out the repository.
2. Configure with `cmake --preset ci-linux-asan` with VCPKG env vars:

   ```text
   VCPKG_MANIFEST_INSTALL=OFF
   -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed
   ```

3. Build: `cmake --build build --parallel`.
4. Run only the `requires-opengl` tests under `xvfb-run`:

   ```text
   xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure
   ```

   with env:

   ```text
   ASAN_OPTIONS: halt_on_error=1:detect_leaks=0
   LSAN_OPTIONS: detect_leaks=0
   LIBGL_ALWAYS_SOFTWARE: "1"
   ALSOFT_DRIVERS: "null"
   ```

5. No `lcov` or coverage report steps.
6. `timeout-minutes: 45` (configure + compile + xvfb execution).

- [x] `_asan-linux.yml` runs in the GHCR container with `options: --user root` (same image as `_build-linux.yml`).
- [x] `.github/workflows/_asan-linux.yml` created with configure + build + `requires-opengl` test steps.
- [x] Configure step uses `ci-linux-asan` preset; VCPKG env vars set (`VCPKG_MANIFEST_INSTALL=OFF`, `-DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed`).
- [x] `ASAN_OPTIONS` and `LSAN_OPTIONS` set in the test step `env:`.
- [x] `_asan-linux.yml` declares `permissions: { packages: read, contents: read }` at the job level (required for GHCR container pull and `actions/checkout`).
- [x] No lcov/coverage steps.
- [x] `timeout-minutes: 45` set on the workflow's job.

#### 6c. Wire `asan-linux` into `ci.yml` and `all-checks-pass`

Add a new `asan-linux` top-level job in `ci.yml` that calls `_asan-linux.yml`.
This increases the total `ci.yml` job count from twelve to thirteen.
Add `asan-linux` to the `needs:` list of the `all-checks-pass` gate job so a UAF
detected by ASAN fails the gate.

`asan-linux` dependencies: `needs: [supply-chain-lint, validate-assets, prepare]`
(same upstream gates as `build-linux` — no dependency on the non-ASAN build artifacts).

- [x] `asan-linux` job added to `ci.yml`; calls `_asan-linux.yml`.
- [x] `asan-linux` job in `ci.yml` declares `permissions: { packages: read, contents: read }` at
  the job level (top-level `ci.yml` has `permissions: {}` which overrides the GitHub default;
  without this job-level block the GHCR container pull is denied and `actions/checkout` has no
  `contents: read` permission).
- [x] `asan-linux` has `needs: [supply-chain-lint, validate-assets, prepare]`.
- [x] `asan-linux` added to `all-checks-pass` `needs:` list.
- [x] `"${{ needs.asan-linux.result }}"` added to the `results=()` bash array inside the
  `all-checks-pass` gate step's `run:` block (matching the pattern of the existing seven
  result entries so a failing ASAN job actually fails the gate, not merely blocks it).
- [x] `architecture/ci-cd/github-actions-workflow.md` verified — confirm the existing `asan-linux` entries
  (job count 13, jobs-table row, dependency-graph node, timeout-table entry, standalone `_asan-linux.yml`
  ASAN paragraph) are consistent with the implemented workflow; update only if the implementation deviates
  from the already-documented spec.
- [x] `architecture/ci-cd/github-actions-workflow.md` digest update instructions updated:
  `_asan-linux.yml` appears in both the step-9 file list and the "Known issue" paragraph
  (making five files total alongside `_build-linux.yml`, `_test-linux.yml`,
  `_coverage-linux.yml`, `.devcontainer/Dockerfile`).

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q6.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` succeeds on Linux (no new compiler warnings or errors).
- [ ] `ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure` —
  all unit tests pass.
- [ ] `ctest --test-dir build -L "^integration$" --output-on-failure` — all integration
  tests pass (including new `SceneEntityManagerDestroyOrderTest`).
- [ ] `xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$"
  --output-on-failure` — all OpenGL tests pass (including Tests A, C, and the three
  additional agent-despawn tests).
- [ ] `make test` passes — ≥95% total line coverage gate holds.
- [ ] `asan-linux` job (`_asan-linux.yml` with preset `ci-linux-asan`) passes — Tests A
  and C pass under `-fsanitize=address,undefined` with
  `ASAN_OPTIONS=halt_on_error=1:detect_leaks=0`.
- [ ] No ASAN error reported for `despawnVehicleAgent` on unfixed code with Tests A/C
  (i.e., reverting the fix and running under ASAN should produce a detectable error —
  confirm this manually before merging).
- [ ] Manual test: spawn several agents of the same zone, run for a few seconds, despawn
  and respawn repeatedly — no crash observed.
- [ ] `listenerDistanceSq` fix verified: at full vehicle pool capacity, the eviction
  candidate is the farthest agent rather than index 0.
