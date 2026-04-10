## Phase 11q6: Fix `despawnVehicleAgent` Use-After-Free + Service Building Inspection + Minimap Rotation + ASAN CI Job + Audio Side-Channel Bugs

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
  a service building (fire station, police station, hospital, power plant, water tower)
  via the Query/Inspector panel, the tile is displayed as "Unzoned" instead of showing
  the service building type and its coverage status. The inspector reads zone type from
  the zoning grid, but service buildings are placed outside the regular zone grid and
  the query path does not check `m_zoning.m_serviceBuildings` before falling back to
  the zone tile.
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

This phase also adds an ASAN CI job (the tests introduced here are only reliable
regression gates when ASAN is active) and three regression-test groups.

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
        // getMesh() returns IMesh*; the cast to IAnimatedMesh* is safe here because
        // the mesh was loaded via m_smgr->getMesh() which always returns IAnimatedMesh*.
        IAnimatedMesh* sharedMesh =
            static_cast<IAnimatedMesh*>(node->getMesh());
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

- [ ] `m_agentNodes` entry erased before `node->remove()`.
- [ ] `sharedMesh->grab()` called before `node->remove()`; `sharedMesh->drop()` called after.
- [ ] `getMaterial(i)` cached as `SMaterial&` in outer loop (C-4 fixed).
- [ ] `make build` succeeds with no new warnings.
- [ ] All existing renderer and agent-node tests pass unchanged.

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

- [ ] `vehicles_diffuse_atlas_d.png` TextureCache membership confirmed (yes/no).
- [ ] If yes: `evictUnreferenced()` added to `despawnVehicleAgent` after texture-slot clear.
- [ ] If no: comment added explaining why Step 3 is omitted here; texture-cache.md updated.

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

- [ ] Iterator `it` refreshed via second `find` after the zone-change write block.
- [ ] All agent-sync integration tests pass unchanged.

---

#### 4. `src/audio/AudioSystem.cpp` — `listenerDistanceSq` never updated after acquire

`acquireVehicleEnginePair` initialises `m_sfxSlots[idleIdx].listenerDistanceSq = 0.f`
and never updates it. The eviction heuristic reads this field to pick the farthest
vehicle to evict, but because it is always 0, eviction is order-of-scan rather than
distance-based.

Fix: in `updateVehicleAudio`, after updating `worldX`/`worldZ`, also update
`listenerDistanceSq` for both the idle and move slot indices:

```cpp
m_sfxSlots[idleIdx].listenerDistanceSq = distSq;
m_sfxSlots[moveIdx].listenerDistanceSq = distSq;
```

where `distSq` is the squared distance from listener to agent already computed
in `updateVehicleAudio`.

- [ ] `listenerDistanceSq` written in `updateVehicleAudio` for both idle and move slots.
- [ ] Eviction candidate selection uses the updated distance at full pool capacity.
- [ ] All `AudioSystem` unit tests pass unchanged.

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

- [ ] `speedFraction = 1.0f` hardcode replaced with velocity-derived value,
  OR replaced with `/* TODO phase-11q7: derive from agent velocity */` and a
  tracking issue opened.
- [ ] If replaced with velocity: `gainIdle` audible at low agent speed in manual test.

---

#### 6. `src/simulation/CitySimulation.cpp` + `src/ui/` — Service building shows as "Unzoned" when inspected

When the player clicks on a tile occupied by a service building (fire station,
police station, hospital, power plant, water tower) the Query/Inspector panel
displays "Unzoned" because the inspector reads only from the regular zone tile grid.
Service buildings are stored in `m_zoning.m_serviceBuildings` (a separate list, not
in the zone grid), so the zone-type lookup returns `ZoneType::None` → "Unzoned".

**Fix**: before the zone-tile fallback in the tile-query path, check whether the
queried `(tileX, tileZ)` falls within any service building's footprint. A service
building occupies a 2×2 footprint at `(sb.x, sb.z)`, so the check is:

```cpp
for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
    if (tileX >= sb.x && tileX < sb.x + 2 &&
        tileZ >= sb.z && tileZ < sb.z + 2) {
        // Return service building info instead of zone tile
    }
}
```

The inspector should display:

- Building type name (e.g. "Fire Station", "Police Station", "Hospital",
  "Power Plant", "Water Tower")
- Coverage radius and whether this tile is currently powered / water-covered
  (where applicable)
- Construction cost (read-only, for reference)

**Locate the query path**: find the `ICitySimulation` method(s) called by the
Query/Inspector panel on tile click (see `architecture/ui-ux/query-inspector-panel.md`
and `src/interfaces/ICitySimulation.h`) and trace to where zone type is resolved.
Add or extend the relevant query method to return service building data when the
tile is within a service building footprint.

If the query interface needs a new method, add it to `ICitySimulation.h`,
`MockCitySimulation`, and the relevant test fixtures.

- [ ] Querying a tile inside a service building footprint returns the service building
  type (not "Unzoned") in the inspector panel.
- [ ] Querying a tile outside all service building footprints is unaffected.
- [ ] `ICitySimulation.h` extended if a new query method is needed; `MockCitySimulation`
  updated accordingly.
- [ ] At least one unit test covers the "tile inside service building footprint →
  inspector shows service building type" path.
- [ ] All existing `CitySimulation` and UI tests pass unchanged.

---

#### 7. `src/ui/Minimap.cpp` — Minimap rotated 90° anti-clockwise (incorrect orientation)

The minimap renders the city rotated 90° anti-clockwise relative to the actual game
world. The player sees North at the left edge instead of at the top, making the
minimap misleading for navigation.

**Fix**: apply a 90° clockwise rotation to the minimap render pass so world-North
maps to the top of the minimap panel. The rotation must be applied around the
minimap's own centre point so the bounds do not shift.

Locate the tile-drawing loop in `Minimap::draw()` (or `Minimap::drawOverlay()`)
where tile `(x, z)` world coordinates are mapped to minimap pixel coordinates.
Apply the coordinate transformation:

```cpp
// Before (incorrect — produces 90° anti-clockwise result):
int px = x * tilePixelW;
int py = z * tilePixelH;

// After (90° clockwise rotation around centre):
// For a map of mapW × mapH tiles and a minimap of pixW × pixH pixels:
int px = (mapH - 1 - z) * tilePixelW;   // z maps to x-axis (left→right = south→north)
int py = x * tilePixelH;                 // x maps to y-axis (top→bottom = west→east)
```

The exact formula depends on the current mapping in the source — read the code
first and adjust accordingly. The key invariant after the fix:

- World coordinate `(x=0, z=0)` (top-left of the world grid / north-west corner)
  maps to the **top-left** pixel of the minimap.
- World coordinate `(x=maxX, z=0)` maps to the **bottom-left** pixel.
- World coordinate `(x=0, z=maxZ)` maps to the **top-right** pixel.

Verify against `architecture/ui-ux/minimap.md` for the canonical coordinate
conventions and any existing pixel-mapping contract.

Also update the camera viewport frustum projection in `Minimap::drawOverlay()`
(the transient draw that shows the player's current view frustum on the minimap)
with the same rotation so the frustum marker stays aligned after the fix.

- [ ] Minimap tile-drawing loop updated with 90° clockwise rotation around centre.
- [ ] Camera viewport frustum projection updated with the same rotation.
- [ ] Manual visual check: minimap top edge = world north, left edge = world west.
- [ ] All existing minimap unit tests pass; update expected pixel coordinates in any
  test that asserts specific pixel positions if the rotation changes them.

---

### New Tests

All new test files go into the **`opengl_tests`** target. Per `framework.md` line 126,
source files MUST be listed **inline** in the existing `add_executable(opengl_tests ...)`
block in `CMakeLists.txt` — NOT via `target_sources()`.

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

- [ ] Test file `tests/rendering/AgentDespawnRenderTest.cpp` created.
- [ ] Added inline to `add_executable(opengl_tests ...)` in `CMakeLists.txt`.
- [ ] `GTEST_SKIP()` asset guard present.
- [ ] `beginScene`/`endScene` wrapper present around `drawAll()`.
- [ ] Passes on fixed code; would fault on unfixed code under ASAN.

#### Test B — `tests/rendering/SceneEntityManagerDestroyOrderTest.cpp`

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

- [ ] Test file `tests/rendering/SceneEntityManagerDestroyOrderTest.cpp` created.
- [ ] Added to `add_executable(integration_tests ...)` with label `integration`.
- [ ] Uses EDT_NULL — no xvfb dependency.
- [ ] Passes on current `SceneEntityManager::destroy()` implementation.

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

- [ ] Test file `tests/rendering/SharedMeshRefCountTest.cpp` created.
- [ ] Added inline to `add_executable(opengl_tests ...)` in `CMakeLists.txt`.
- [ ] Handles 0 and 3 used (same `% 3` remainder, same zone).
- [ ] `ASSERT_EQ(node0->getMesh(), node3->getMesh())` precondition check present.
- [ ] `GTEST_SKIP()` asset guard present.
- [ ] `beginScene`/`endScene` wrapper present.
- [ ] Passes on fixed code; would fault on unfixed code under ASAN.

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

- [ ] `DespawnNonexistentHandle_NoOp` implemented and passes.
- [ ] `DespawnAllAgents_DrawScene_Clean` implemented and passes.
- [ ] `SpawnSameHandleTwice_NoLeak` implemented and passes.

#### Audio regression tests — `tests/audio/AudioSystem` (existing target)

**`AudioSystemVehicleReleaseTest::SourceStoppedAfterRelease`**

```text
1. Acquire a pair via acquireVehicleEnginePair(zone).
2. Drive one updateVehicleEngines tick (processes pendingInit → alSourcePlay).
3. releaseVehicleEnginePair(idleIdx, moveIdx).
4. Drive one more updateVehicleEngines tick (processes pendingRelease → alSourceStop).
5. ASSERT AL_SOURCE_STATE == AL_STOPPED for both sources.
6. ASSERT AL_BUFFER == 0 (buffer detached).
```

**`AudioSystemVehicleReleaseTest::SlotReacquirableAfterRelease`**

```text
After SourceStoppedAfterRelease completes:
1. acquireVehicleEnginePair(zone) again.
2. ASSERT returned indices are valid (>= 0) — pool slot is genuinely free.
```

- [ ] `SourceStoppedAfterRelease` implemented; asserts AL_STOPPED and AL_BUFFER==0.
- [ ] `SlotReacquirableAfterRelease` implemented; confirms pool slot recycled.
- [ ] Both tests pass under the null-driver / headless audio seam.

---

### 6. ASAN CI Job (`asan-linux`)

Tests A and C are only reliable regression gates under AddressSanitizer. A freed page
that has not been reclaimed may still be readable in a short test, reproducing the
production scenario (crash at frame 15455, not frame 1). Without ASAN the tests pass
non-deterministically on the unfixed code.

ASAN must NOT be mixed with `--coverage` instrumentation — the ASAN runtime and
gcov runtime share shadow memory and produce spurious results. The `asan-linux` job
must be independent of `ci-linux-coverage`.

#### 6a. New CMake preset `ci-linux-asan` in `CMakePresets.json`

```json
{
  "name": "ci-linux-asan",
  "inherits": "ci-linux",
  "cacheVariables": {
    "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
    "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined",
    "ENABLE_COVERAGE": "OFF"
  }
}
```

- [ ] `ci-linux-asan` preset added to `CMakePresets.json`.

#### 6b. New CI job `_asan-linux.yml` (reusable workflow)

Runs the `opengl_tests` binary under xvfb with ASAN enabled. Mirrors the structure
of `_test-linux.yml` for the `requires-opengl` step only.

Key requirements:

- Runs on the same `ghcr.io/m0wa/aitown-ci-linux` image as other Linux jobs.
- Configures with `cmake --preset ci-linux-asan`.
- Sets `ASAN_OPTIONS=halt_on_error=1:detect_leaks=0` (no LSan — container may not
  have ptrace for leak detection; halt on first error for CI).
- Sets `LSAN_OPTIONS=detect_leaks=0`.
- Runs `xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$"
  --output-on-failure`.
- Does NOT run `lcov` or generate coverage reports.

```yaml
# .github/workflows/_asan-linux.yml
name: ASAN Linux
on:
  workflow_call:

jobs:
  asan-linux:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/m0wa/aitown-ci-linux@sha256:<same-digest-as-ci.yml>
    steps:
      - uses: actions/checkout@<pinned-sha>
      - name: Configure (ASAN)
        run: cmake --preset ci-linux-asan
        env:
          VCPKG_ROOT: /opt/vcpkg
      - name: Build
        run: cmake --build build -- -j$(nproc)
      - name: Run requires-opengl tests under ASAN
        run: xvfb-run --auto-servernum ctest --test-dir build
               -L "^requires-opengl$" --output-on-failure
        env:
          ASAN_OPTIONS: halt_on_error=1:detect_leaks=0
          LSAN_OPTIONS: detect_leaks=0
          LIBGL_ALWAYS_SOFTWARE: "1"
```

- [ ] `.github/workflows/_asan-linux.yml` created with the above structure.
- [ ] `ci-linux-asan` preset used for configure step.
- [ ] `ASAN_OPTIONS` and `LSAN_OPTIONS` set correctly.
- [ ] No lcov/coverage steps present.

#### 6c. Wire `asan-linux` into `ci.yml` and `all-checks-pass`

- Add `uses: ./.github/workflows/_asan-linux.yml` call job to `ci.yml`.
- Add `asan-linux` to the `needs:` list of the `all-checks-pass` gate job so the
  gate fails if ASAN catches a UAF.

- [ ] `asan-linux` call job added to `ci.yml`.
- [ ] `asan-linux` listed in `all-checks-pass` gate `needs:`.
- [ ] CI passes end-to-end with the new job on the fixed code.

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
- [ ] ASAN CI job (`asan-linux`) passes — Tests A and C pass under
  `-fsanitize=address,undefined` with `ASAN_OPTIONS=halt_on_error=1:detect_leaks=0`.
- [ ] No ASAN error reported for `despawnVehicleAgent` on unfixed code with Tests A/C
  (i.e., reverting the fix and running under ASAN should produce a detectable error —
  confirm this manually before merging).
- [ ] Manual test: spawn several agents of the same zone, run for a few seconds, despawn
  and respawn repeatedly — no crash observed.
- [ ] `listenerDistanceSq` fix verified: at full vehicle pool capacity, the eviction
  candidate is the farthest agent rather than index 0.
