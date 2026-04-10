## Phase 11q5: Fix SonarCloud HIGH Issues (S5025 Raw Delete + Remaining S3776/S134)

**Status: Planned**

**Prerequisite**: phase-11q3 merged — the S134 + S3776 first-pass extractions in phase-11q3 created the helper structure and reduced CC in many functions; phase-11q5 targets the remaining violations in those same files.

### Goal

Thirty-two OPEN HIGH-severity SonarCloud issues remain across nine source files:

- **S5025** (10 issues): raw `delete` calls in `main.cpp` — replace raw-pointer members of
  `AppSystems` with `std::unique_ptr` and remove the manual deletion block.
- **S3776** (9 issues): cognitive complexity exceeds the 25-point limit in nine functions
  across six files — extract helper methods until each function is ≤ 25 points.
- **S134** (13 issues): control flow nested more than 3 levels deep in four files — the
  S3776 extractions above address most S134 violations; the remaining ones are fixed by
  targeted helper extraction or algorithm simplification.

---

### Issues to Fix

| Rule | File | Line(s) | Detail |
|---|---|---|---|
| S5025 | `src/main.cpp` | 569–578 | 10 raw `delete` calls on `AppSystems` members |
| S3776 | `src/platform/EventReceiver.cpp` | 72 | `handleMouseEvent` CC=42 |
| S3776 | `src/ui/CameraController.cpp` | 84 | `OnInputEvent` CC=33 |
| S3776 | `src/simulation/CitySimulation.cpp` | 285 | `checkZoneFootprintClear` CC=34 |
| S3776 | `src/simulation/CitySimulation.cpp` | 320 | `applyZoneFootprint` CC=33 |
| S3776 | `src/simulation/CitySimulation.cpp` | 997 | `deserializeFromJson` CC=32 |
| S134  | `src/simulation/CitySimulation.cpp` | 306, 359, 361, 626, 658 | nesting > 3 in above functions |
| S3776 | `src/simulation/Population.cpp` | 323 | `applyDensityUpgrade` CC=45 |
| S3776 | `src/terrain/TerrainSystem.cpp` | 426 | `largestContiguousFlatRegion` CC=57 |
| S134  | `src/terrain/TerrainSystem.cpp` | 475–478, 483, 855 | nesting > 3 in above function and `setTileHeight` |
| S3776 | `src/rendering/IrrlichtRenderer.cpp` | 1150 | `setZoneOverlay` CC=27 |
| S134  | `src/rendering/IrrlichtRenderer.cpp` | 439 | nesting > 3 in `setCamera` |
| S3776 | `src/rendering/TextureCache.cpp` | 155 | `loadSRGB` CC=27 |
| S134  | `src/rendering/BuildingAssetLoader.cpp` | 196 | nesting > 3 in `load` |

---

### Deliverables

#### 1. `src/main.cpp` — S5025: Replace raw pointer members with `std::unique_ptr`

The `AppSystems` struct holds 10 raw-pointer members initialised with `new` in
`initSystems()` and deleted in reverse order at the end of `main()`. SonarCloud
S5025 fires on each of the 10 `delete` expressions. The fix is to convert every
raw-owning pointer to `std::unique_ptr<T>`, use `std::make_unique<T>(...)` at
allocation sites, and remove the manual deletion block.

**In `AppSystems` struct** — change the 10 raw-pointer members to `unique_ptr`:

```cpp
// Before:
IrrlichtUIBackend* uiBackend{nullptr};
UIScaler*          uiScaler{nullptr};
CameraController*  cameraController{nullptr};
IrrlichtRenderer*  renderer{nullptr};
AudioSystem*       audioSystem{nullptr};
TerrainSystem*     terrainSystem{nullptr};
CitySimulation*    citySimulation{nullptr};
UIManager*         uiManager{nullptr};
SaveSystem*        saveSystem{nullptr};
EventReceiver*     eventReceiver{nullptr};

// After:
std::unique_ptr<IrrlichtUIBackend> uiBackend;
std::unique_ptr<UIScaler>          uiScaler;
std::unique_ptr<CameraController>  cameraController;
std::unique_ptr<IrrlichtRenderer>  renderer;
std::unique_ptr<AudioSystem>       audioSystem;
std::unique_ptr<TerrainSystem>     terrainSystem;
std::unique_ptr<CitySimulation>    citySimulation;
std::unique_ptr<UIManager>         uiManager;
std::unique_ptr<SaveSystem>        saveSystem;
std::unique_ptr<EventReceiver>     eventReceiver;
```

`cameraNode` (`irr::scene::ICameraSceneNode*`) is NOT changed — it is scene-graph-owned
by Irrlicht, not by `AppSystems`. `RenderSystem renderSystem` and `WallClock wallClock`
are already value members (RAII); leave them unchanged.

Add `#include <memory>` at the top of `main.cpp` (or verify it is already present).

**In `initSystems()`** — change every `sys.X = new X(...)` to `sys.X = std::make_unique<X>(...)`:

```cpp
sys.uiBackend = std::make_unique<IrrlichtUIBackend>(device);
sys.uiScaler  = std::make_unique<UIScaler>(...);
sys.cameraController = std::make_unique<CameraController>(...);
sys.renderer  = std::make_unique<IrrlichtRenderer>(device, nullptr);
sys.audioSystem = std::make_unique<AudioSystem>(device->getLogger(), &sys.wallClock);
sys.terrainSystem = std::make_unique<TerrainSystem>(sys.renderer.get(), &sys.wallClock);
sys.citySimulation = std::make_unique<CitySimulation>(...);
sys.uiManager = std::make_unique<UIManager>(sys.uiBackend.get(), sys.audioSystem.get(),
                                             sys.citySimulation.get(), &sys.wallClock);
sys.saveSystem = std::make_unique<SaveSystem>(&sys.wallClock);
sys.eventReceiver = std::make_unique<EventReceiver>(sys.uiScaler.get(), sys.uiManager.get(),
                                                     sys.cameraController.get(),
                                                     sys.uiBackend.get());
```

Wherever the value of a `unique_ptr` member is passed to another constructor or method as a
raw pointer, append `.get()`. All passing sites already use raw pointers; the callee
signatures are unchanged — only the caller supplies `.get()`.

**In `main()`** — delete the entire manual cleanup block:

```cpp
// REMOVE these 10 lines entirely:
delete sys.eventReceiver;
delete sys.saveSystem;
delete sys.uiManager;
delete sys.citySimulation;
delete sys.terrainSystem;
delete sys.audioSystem;
delete sys.renderer;
delete sys.cameraController;
delete sys.uiScaler;
delete sys.uiBackend;
```

The `unique_ptr` members in `AppSystems` are destroyed in reverse declaration order
(C++ struct destruction guarantee), which matches the existing manual deletion order:
`eventReceiver` last-declared → destroyed first; `uiBackend` first-declared → destroyed last;
`renderSystem` (RAII value, first member) → destroyed after all `unique_ptr` members.
No reordering of struct member declarations is required.

- [x] `AppSystems` raw-pointer members converted to `std::unique_ptr<T>`.
- [x] All `sys.X = new X(...)` lines use `std::make_unique<X>(...)`.
- [x] All pass-through sites use `.get()` where a raw pointer is required.
- [x] Manual delete block (lines 568–578) removed from `main()`.
- [x] No `new` expression paired with a matching `delete` remains in `main.cpp`.
- [x] Build succeeds and no unit/integration tests regress.

---

#### 2. `src/platform/EventReceiver.cpp` / `.h` — S3776: Extract mouse switch-case bodies

`handleMouseEvent` (line 72, CC=42) contains a `switch` over 8 mouse-event types, each
with 3–6 statements and nested conditions. Extract each case body into a private helper:

| New private method | Mouse event case |
|---|---|
| `bool handleLMBDown(InputEvent& out)` | `EMIE_LMOUSE_PRESSED_DOWN` |
| `bool handleRMBDown(InputEvent& out)` | `EMIE_RMOUSE_PRESSED_DOWN` |
| `bool handleMMBDown(InputEvent& out)` | `EMIE_MMOUSE_PRESSED_DOWN` |
| `bool handleLMBUp(InputEvent& out)` | `EMIE_LMOUSE_LEFT_UP` |
| `bool handleRMBUp(InputEvent& out)` | `EMIE_RMOUSE_LEFT_UP` |
| `bool handleMMBUp(InputEvent& out)` | `EMIE_MMOUSE_LEFT_UP` |
| `bool handleMouseMoved(InputEvent& out)` | `EMIE_MOUSE_MOVED` |
| `bool handleMouseWheel(InputEvent& out)` | `EMIE_MOUSE_WHEEL` |

`handleMouseEvent` becomes an 8-case switch with a single `return helperX(out)` per case.
Target CC ≤ 10 for `handleMouseEvent` after extraction.

Add all 8 declarations to the `private:` section of `EventReceiver.h`.

- [x] 8 private helper methods extracted; bodies moved verbatim from switch cases.
- [x] `handleMouseEvent` body is a pure dispatch switch (one call per case).
- [x] SonarCloud S3776 on `EventReceiver.cpp:72` resolves (CC ≤ 25).
- [x] All existing `EventReceiver` unit and integration tests pass unchanged.

---

#### 3. `src/ui/CameraController.cpp` / `.h` — S3776: Extract `OnInputEvent` case handlers

`OnInputEvent` (line 84, CC=33) dispatches on `event.type` with nested if-chains inside
several cases. Extract the complex cases into private helpers:

| New private method | InputEvent case |
|---|---|
| `bool handleRMBDown(const InputEvent& e)` | `Type::MouseButtonDown`, button==1 |
| `bool handleMMBDown(const InputEvent& e)` | `Type::MouseButtonDown`, button==2 |
| `bool handleRMBUp(const InputEvent& e)` | `Type::MouseButtonUp`, button==1 |
| `bool handleMMBUp(const InputEvent& e)` | `Type::MouseButtonUp`, button==2 |
| `bool handleMouseMoved(const InputEvent& e)` | `Type::MouseMove` |

`OnInputEvent` becomes a top-level switch with simple delegation. Target CC ≤ 15.

Add all 5 declarations to the `private:` section of `CameraController.h`.

- [x] 5 private helpers extracted; `OnInputEvent` CC ≤ 25.
- [x] SonarCloud S3776 on `CameraController.cpp:84` resolves.
- [x] All 9 existing `CameraController` unit tests pass unchanged.

---

#### 4. `src/simulation/CitySimulation.cpp` / `.h` — S3776 + S134

##### 4a. `checkZoneFootprintClear` (line 285, CC=34) + S134 at line 306

The service-building overlap guard (lines 301–311) contains 5 nested loops/conditions
that must be replaced — NOT extracted verbatim — using an AABB rectangle-overlap test.
A `ServiceBuilding` occupies a 2×2 footprint at `(sb.x, sb.z)`, so its bounding box is
`[sb.x, sb.x+2) × [sb.z, sb.z+2)`. The zone footprint is `[tileX, tileX+N) × [tileZ, tileZ+N)`.
Two axis-aligned rectangles overlap iff their projections on both axes overlap simultaneously.
Extract into a new private method using this AABB predicate (nesting depth: 1 for + 1 if = 2 levels):

```cpp
// In CitySimulation.h (private):
bool checkServiceBuildingOverlap(int tileX, int tileZ, int N) const;
```

```cpp
// In CitySimulation.cpp:
bool CitySimulation::checkServiceBuildingOverlap(int tileX, int tileZ, int N) const {
    for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
        if (sb.x < tileX + N && sb.x + 2 > tileX &&
            sb.z < tileZ + N && sb.z + 2 > tileZ)
            return true;  // AABB overlap detected
    }
    return false;
}
```

This replaces the O(n·N²) five-loop brute-force with an O(n) AABB check, eliminates all
S134 nesting violations (nesting depth 2 ≤ 3), and cannot introduce a new S134 in the helper.

In `checkZoneFootprintClear`, replace the inlined block with:

```cpp
if (checkServiceBuildingOverlap(tileX, tileZ, N)) return false;
```

Target CC ≤ 20 for `checkZoneFootprintClear`.

##### 4b. `applyZoneFootprint` (line 320, CC=33) + S134 at lines 359, 361

The border-ring terrain flattening block (lines 350–366) contains a nested for-for with
two guarding `if` branches. Extract into a private helper:

```cpp
// In CitySimulation.h (private):
void flattenBorderRing(int tileX, int tileZ, int N, float flatHeight);
```

Move the entire `if (m_terrain) { ... }` block into this helper. In `applyZoneFootprint`,
replace with:

```cpp
flattenBorderRing(tileX, tileZ, N, flatHeight);
```

Target CC ≤ 20 for `applyZoneFootprint`.

##### 4c. `deserializeFromJson` (line 997, CC=32)

The function contains two distinct try-catch/guard patterns:

1. **Scalar fields** (4 blocks): `map_tiles_x`, `map_tiles_z`, `outstanding_bond_uses`,
   `consecutive_deficit_months` — each uses `try { field = j.at(key).get<T>(); } catch (...) { errorOut = "missing key"; return false; }`.
2. **Optional array fields** (3 blocks): `population_milestone_fired` (5-element bool array),
   `building_variant_counters` (9-element int array), and the `density_unlock_flags` +
   `density_unlock_revenue_counter` compound section — each uses a `j.contains(key)` guard
   with index-bounded iteration loops that a simple template cannot replace.

**Scalar helper** — extract a private template that replaces only the 4 scalar try-catch
blocks:

```cpp
// In CitySimulation.h (private):
template<typename T>
bool parseJsonField(const nlohmann::json& j, const char* key,
                    T& out, std::string& errorOut) const;
```

```cpp
template<typename T>
bool CitySimulation::parseJsonField(const nlohmann::json& j, const char* key,
                                     T& out, std::string& errorOut) const {
    try { out = j.at(key).template get<T>(); return true; }
    catch (...) { errorOut = std::string("missing ") + key; return false; }
}
```

Replace the 4 scalar `try { field = j.at("key").get<T>(); } catch (...) { ... return false; }`
blocks (`map_tiles_x`, `map_tiles_z`, `outstanding_bond_uses`, `consecutive_deficit_months`)
in `deserializeFromJson` with `if (!parseJsonField(j, "key", field, errorOut)) return false;`.

**Optional array helpers** — extract two helpers for the optional array blocks that use
`j.contains(key)` guards with bounded iteration (these cannot be replaced by the scalar
template):

```cpp
// In CitySimulation.h (private):
bool parseOptionalBoolArray(const nlohmann::json& j, const char* key,
                            bool* out, int maxCount, std::string& errorOut) const;
bool parseOptionalIntArray(const nlohmann::json& j, const char* key,
                           int* out, int maxCount, std::string& errorOut) const;
```

Each reads the array only if `j.contains(key)`, iterates up to `maxCount` elements, and
returns false with `errorOut` set on exception. `parseOptionalBoolArray` replaces the
`population_milestone_fired` block (5-element bool array); `parseOptionalIntArray` replaces
the `building_variant_counters` block (9-element int array).

**Density unlock section** — also extract the `density_unlock_flags` and
`density_unlock_revenue_counter` array-parsing blocks into a private method:

```cpp
bool parseDensityUnlockSection(const nlohmann::json& j,
                                DensityUnlockState& out,
                                std::string& errorOut) const;
```

`parseJsonField<T>` + `parseOptionalBoolArray` + `parseOptionalIntArray` +
`parseDensityUnlockSection` together reduce `deserializeFromJson` to CC ≤ 20.

##### 4d. S134 at lines 626, 658 (`checkServiceFootprintClear` and `placeServiceBuilding`)

`checkServiceFootprintClear` (line 619) has nesting at lines 626: for-for with inner for.
Extract the service-building collision sub-check into a lambda or private helper:

```cpp
// Replace the inner loop at lines 625–629:
bool overlapsSb = std::any_of(m_zoning.m_serviceBuildings.begin(),
                               m_zoning.m_serviceBuildings.end(),
    [fx, fz, sN](const ServiceBuilding& sb) {
        return fx >= sb.x && fx < sb.x + sN && fz >= sb.z && fz < sb.z + sN;
    });
if (overlapsSb) return false;
```

Note: the predicate uses `sN` (the caller's footprint size) to match the existing code
semantics exactly -- this is a structural refactoring only and does not change the
collision logic.

`placeServiceBuilding` S134 at line 658: the road-adjacency check has 3 nested loops
(`dx`, `dz`, `d`). Extract into:

```cpp
bool CitySimulation::hasRoadAdjacent(int tileX, int tileZ, int sN) const;
```

Move the triple-loop into this helper and replace the inline block with a single call.

- [x] `checkServiceBuildingOverlap` extracted; `checkZoneFootprintClear` CC ≤ 25.
- [x] `flattenBorderRing` extracted; `applyZoneFootprint` CC ≤ 25.
- [x] `parseJsonField` template + `parseOptionalBoolArray` + `parseOptionalIntArray` + `parseDensityUnlockSection` added; `deserializeFromJson` CC ≤ 25.
- [x] `checkServiceFootprintClear` S134 resolved via `std::any_of` or extracted lambda.
- [x] `hasRoadAdjacent` extracted; S134 at line 658 resolved.
- [x] SonarCloud S3776 on `CitySimulation.cpp:285`, `320`, `997` and S134 on `306`, `359`, `361`, `626`, `658` resolve.
- [x] All existing `CitySimulation` and `simulation_tests` pass unchanged.

---

#### 5. `src/simulation/Population.cpp` / `.h` — S3776: Extract soft-blocker loop from `applyDensityUpgrade`

`applyDensityUpgrade` (line 323, CC=45) contains a doubly-nested for loop with complex
branch conditions to collect demolition candidates. The double-for loop (lines 352-369)
in the current post-Phase-11q3 source is still inline; this extraction addresses the
remaining CC=45 SonarCloud issue.

**Step 1 — Promote `DemoEntry` to `Population.h`**: `DemoEntry` is currently defined in an
anonymous namespace inside `Population.cpp` (line 22). Because anonymous-namespace types
are translation-unit-local, the header cannot reference `DemoEntry` in the
`collectDemoTargets` signature. Move the struct out of the anonymous namespace and into
the `private` section of the `Population` class in `Population.h`:

```cpp
// In Population.h — add to the private section of the Population class:
struct DemoEntry { int x; int z; int64_t originKey; };
```

Then remove the `DemoEntry` line from the anonymous namespace in `Population.cpp`.
`OuterTile`, `checkZonedNeighbor`, and `clearFootprintCell` remain file-local in the
anonymous namespace — they are not referenced from the header.

**Step 2 — Extract `collectDemoTargets`**: With `DemoEntry` now visible in the header,
declare the new private helper:

```cpp
// In Population.h (private):
bool collectDemoTargets(Zoning& zoning, int tx, int tz, int newN,
                        ZoneType targetZone, DensityTier targetDensity,
                        int64_t candKey,
                        std::vector<DemoEntry>& toDemo) const;
// Returns true if a hard blocker was found (hasBlocker), false if targets collected cleanly.
```

In the out-of-class definition in `Population.cpp`, the parameter type uses the
fully-qualified nested name:

```cpp
bool Population::collectDemoTargets(Zoning& zoning, int tx, int tz, int newN,
                                     ZoneType targetZone, DensityTier targetDensity,
                                     int64_t candKey,
                                     std::vector<Population::DemoEntry>& toDemo) const {
    // ... moved loop body ...
}
```

Note: `collectDemoTargets` internally calls `checkZonedNeighbor` (already extracted in
Phase 11q3 as a private/static helper); its signature remains unchanged.

Move the double-for loop (lines 352–369) into this helper. In `applyDensityUpgrade`,
replace with:

```cpp
std::vector<DemoEntry> toDemo;
bool hasBlocker = collectDemoTargets(zoning, tx, tz, newN,
                                     targetZone, targetDensity,
                                     candKey, toDemo);
if (hasBlocker) { retryCount++; return false; }
```

Target CC ≤ 20 for `applyDensityUpgrade`.

- [x] `DemoEntry` struct moved from anonymous namespace in `Population.cpp` to `private` section of `Population` class in `Population.h`; `OuterTile`, `checkZonedNeighbor`, `clearFootprintCell` remain file-local in the anonymous namespace.
- [x] `collectDemoTargets` extracted; `applyDensityUpgrade` CC ≤ 25.
- [x] SonarCloud S3776 on `Population.cpp:323` resolves.
- [x] All `Population`-related simulation tests pass unchanged.

---

#### 6. `src/terrain/TerrainSystem.cpp` / `.h` — S3776 + S134

##### 6a. `largestContiguousFlatRegion` (line 426, CC=57) + S134 at lines 475–478, 483

The BFS component expansion loop (lines 469–493) contains nested assignments and
4-connected neighbour exploration with multiple guard conditions. Extract the BFS
body into a private helper:

```cpp
// In TerrainSystem.h (private):
struct BfsComponent {
    int size{0};
    int minX, maxX, minZ, maxZ;
};

void expandBfsComponent(int startIdx, int mapTilesX, int mapTilesZ,
                        const std::vector<bool>& isFlat,
                        std::vector<bool>& visited,
                        BfsComponent& out) const;
```

Move the `while (!q.empty())` loop body into `expandBfsComponent`, including the
min/max bounding-box tracking and the 4-connected neighbour push. In
`largestContiguousFlatRegion`, replace the BFS body with:

```cpp
BfsComponent comp;
expandBfsComponent(idx, mapTilesX, mapTilesZ, isFlat, visited, comp);
// use comp.size, comp.minX, comp.maxX, comp.minZ, comp.maxZ
```

Target CC ≤ 25 for `largestContiguousFlatRegion`.

##### 6b. S134 at line 855 (`setTileHeight` / chunk deduplication)

The chunk-ID deduplication loop (lines 851–858) has a triple-nested structure
(for → range-for → for). Replace the manual linear-scan deduplication with
`std::find`:

```cpp
// Before:
for (uint64_t cid : affectedChunkIds(modifiedTiles[i].tx, modifiedTiles[i].tz)) {
    bool already = false;
    for (uint64_t existing : chunksToRebuild) {
        if (existing == cid) { already = true; break; }
    }
    if (!already) chunksToRebuild.push_back(cid);
}

// After:
for (uint64_t cid : affectedChunkIds(modifiedTiles[i].tx, modifiedTiles[i].tz)) {
    if (std::find(chunksToRebuild.begin(), chunksToRebuild.end(), cid)
            == chunksToRebuild.end()) {
        chunksToRebuild.push_back(cid);
    }
}
```

Include `<algorithm>` if not already present.

- [x] `expandBfsComponent` extracted; `largestContiguousFlatRegion` CC ≤ 25.
- [x] S134 at lines 475–478, 483 resolved by extraction.
- [x] Chunk deduplication loop simplified with `std::find`; S134 at line 855 resolved.
- [x] SonarCloud S3776 on `TerrainSystem.cpp:426` and S134 on `475–478`, `483`, `855` resolve.
- [x] All `TerrainSystem` tests pass unchanged.

---

#### 7. `src/rendering/IrrlichtRenderer.cpp` / `.h` — S3776 + S134

##### 7a. `setZoneOverlay` (line 1150, CC=27)

The material texture clearing loop (lines 1160–1164) adds branching inside the function.
Extract into a private helper:

```cpp
// In IrrlichtRenderer.h (private):
void clearOverlayNodeTextures();
```

Move the `for (u32 m = 0; ...)` block (eviction sequence) into this helper. Call it
from `setZoneOverlay` in place of the inlined loop. Target CC ≤ 22 for `setZoneOverlay`.

##### 7b. S134 at line 439 (debug logging block in `setCamera`)

The `#ifndef NDEBUG` block contains a nested `if (m_camera->getAnimators().size() > 0)`
with a further `if (m_device && m_device->getLogger())`. Extract into a private helper:

```cpp
// In IrrlichtRenderer.h (private):
void logUnexpectedAnimators(size_t count) const;
```

Move the `snprintf` + `log()` call body into this helper. In `setCamera`, replace with:

```cpp
#ifndef NDEBUG
if (m_camera->getAnimators().size() > 0)
    logUnexpectedAnimators(m_camera->getAnimators().size());
#endif
```

- [x] `clearOverlayNodeTextures()` extracted; `setZoneOverlay` CC ≤ 25.
- [x] `logUnexpectedAnimators()` extracted; S134 at line 439 resolved.
- [x] SonarCloud S3776 on `IrrlichtRenderer.cpp:1150` and S134 on `439` resolve.
- [x] All existing renderer tests pass unchanged.

---

#### 8. `src/rendering/TextureCache.cpp` / `.h` — S3776: Extract `loadSRGB` path resolution

`loadSRGB` (line 155, CC=27) contains an early-return exception for the vehicle atlas
followed by a path-resolution block for the main atlas fallback. Extract the
effective-path resolution into a private helper:

```cpp
// In TextureCache.h (private):
std::string resolveEffectiveSRGBPath(const std::string& path,
                                     const std::string& basename) const;
```

`resolveEffectiveSRGBPath` returns the path to load (either `path` unchanged, or the
2048-fallback path for `buildings_atlas_d.dds` on constrained hardware), and emits the
diagnostic warning if a fallback is used. In `loadSRGB`, replace the inline block with:

```cpp
std::string effectivePath = resolveEffectiveSRGBPath(path, basename);
```

Target CC ≤ 22 for `loadSRGB`.

- [x] `resolveEffectiveSRGBPath` extracted; `loadSRGB` CC ≤ 25.
- [x] SonarCloud S3776 on `TextureCache.cpp:155` resolves.
- [x] All `TextureCache` tests pass unchanged.

---

#### 9. `src/rendering/BuildingAssetLoader.cpp` / `.h` — S134: Extract atlas application

The atlas texture application block (lines 188–205) has 3 nested `if` guards followed
by an inner `for` loop (nesting depth 4). Extract into a private helper:

```cpp
// In BuildingAssetLoader.h (private):
void applyAtlasTexture(irr::scene::ISceneNode* node, const std::string& basePath) const;
```

Move the entire `if (m_driver) { ... }` block into this helper. In `load`,
replace with:

```cpp
applyAtlasTexture(node, basePath);
```

- [x] `applyAtlasTexture` extracted; S134 at line 196 resolved.
- [x] SonarCloud S134 on `BuildingAssetLoader.cpp:196` resolves.
- [x] All `BuildingAssetLoader` tests pass unchanged.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q5.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` succeeds on Linux (no new compiler warnings or errors).
- [ ] `ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure` — all
  unit tests pass.
- [ ] `ctest --test-dir build -L "^integration$" --output-on-failure` — all integration
  tests pass.
- [ ] `xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure` — all OpenGL tests pass.
- [ ] `make test` passes — ≥95% total line coverage gate enforced (per `architecture/testing/coverage.md`); per-file 85% floor holds for all `src/simulation/` files (`CitySimulation.cpp`, `Economy.cpp`, `Population.cpp`, `SaveSystem.cpp`, `SimTiming.cpp`, `Traffic.cpp`, `Zoning.cpp`).
- [ ] SonarCloud re-scan shows all 32 HIGH issues listed in the Issues table above resolved:
  - All 10 S5025 issues on `main.cpp` lines 569–578 resolved.
  - All 9 S3776 issues on the 9 functions listed resolved.
  - All 13 S134 issues on the lines listed resolved.
- [ ] No new `delete` expression paired with a matching `new` introduced in `main.cpp`.
- [ ] No new HIGH-severity issues introduced by the refactoring.
