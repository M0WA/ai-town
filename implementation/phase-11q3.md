## Phase 11q3: Fix SonarCloud HIGH Maintainability Issues (S134 + S3776) — Remaining Files

**Status: Planned**

**Prerequisite**: phase-11q2 merged (completed S134 + S3776 fixes in Zoning.cpp,
Population.cpp, and a subset of CitySimulation.cpp).
**Blocks**: nothing — purely internal refactors, no interface changes.

### Goal

Fix all 46 open HIGH-impact SonarCloud MAINTAINABILITY issues across 10 source files.
Every issue is one of two rules:

- **`cpp:S134`** — control-flow nesting depth exceeds 3 (`if|for|do|while|switch`)
- **`cpp:S3776`** — function cognitive complexity exceeds 25

| File | S134 | S3776 | Total |
|---|---|---|---|
| `src/terrain/TerrainSystem.cpp` | 9 | 3 | 12 |
| `src/rendering/IrrlichtRenderer.cpp` | 7 | 3 | 10 |
| `src/simulation/CitySimulation.cpp` | 5 | 4 | 9 |
| `src/platform/EventReceiver.cpp` | 3 | 1 | 4 |
| `src/rendering/BuildingAssetLoader.cpp` | 3 | 1 | 4 |
| `src/simulation/Zoning.cpp` | 2 | 1 | 3 |
| `src/rendering/TextureCache.cpp` | 0 | 1 | 1 |
| `src/simulation/Population.cpp` | 0 | 1 | 1 |
| `src/ui/CameraController.cpp` | 0 | 1 | 1 |
| `src/main.cpp` | 0 | 1 | 1 |
| **Total** | **29** | **17** | **46** |

**Fix strategy**: extract deeply-nested inner blocks and oversized functions into
focused private helper methods (or, for file-local functions like `main`, into
`static` file-local helpers). Zero gameplay or rendering behaviour changes — no
simulation or rendering logic is altered.

---

### Deliverables

---

#### 1. Fix `src/terrain/TerrainSystem.cpp` — 12 issues

##### 1a. `TerrainSystem::generate` — S3776 line 361 (complexity 89) + S134 lines 451–459

The highest-complexity function in the codebase. Inlines heightmap construction,
flat-area scan (nested `z`/`x` tile loops), contiguous-region BFS, and chunk
creation in one body — 5+ levels of nesting through the playability check.

- [ ] Extract the heightmap build (the `buildHeightmap` lambda or equivalent block)
  into a private method
  `void buildHeightmapBuffer(std::vector<float>& hmap, int vertX, int vertZ, ITerrainRNG* rng)`.
- [ ] Extract the flat-tile count scan (the nested loops at ~lines 451–465) into a
  private method
  `int countFlatTiles(const std::vector<float>& hmap, int mapTilesX, int mapTilesZ, float cellSize, float slopeThreshold) const`.
- [ ] Extract the contiguous-flat BFS into a private method
  `int largestContiguousFlatRegion(const std::vector<float>& hmap, int mapTilesX, int mapTilesZ, float cellSize, float slopeThreshold) const`.
- [ ] Extract the per-retry chunk-creation loop into a private method
  `void createChunksFromHeightmap(const std::vector<float>& hmap, int mapTilesX, int mapTilesZ, float cellSize)`.
- [ ] Refactor `generate` to call these helpers; verify complexity drops to ≤ 25 and
  nesting to ≤ 3.
- [ ] Add all four declarations to `TerrainSystem.h` private section.

##### 1b. `TerrainSystem::processOneRebuild` — S3776 line 134 (complexity 37) + S134 line 207

The mesh-buffer fill (per-vertex normal/UV computation) is inlined inside the
rebuild dispatcher, pushing nesting to depth 4 at line 207.

- [ ] Extract the per-vertex fill loop into a private method
  `void fillChunkMeshBuffer(irr::scene::SMeshBuffer* buf, const std::vector<float>& heights, int gridSize, float cellSize)`.
- [ ] Refactor `processOneRebuild` to call the helper; verify complexity drops to ≤ 25.
- [ ] Add declaration to `TerrainSystem.h` private section.

##### 1c. `TerrainSystem::setTileHeight` — S3776 line 679 (complexity 49) + S134 line 794

Contains a nested `syncChunk` lambda and cardinal/diagonal neighbour-ripple loops —
nesting exceeds 3 inside the neighbour propagation (line 794).

- [ ] Extract the `writeHeight` + `syncChunk` composite into a private method
  `void writeHeightAndSyncChunks(int tx, int tz, float h)`.
- [ ] Extract the cardinal/diagonal neighbour ripple (lines ~720–800) into a private
  method `void propagateHeightRipple(int tileX, int tileZ)`.
- [ ] Refactor `setTileHeight` to call both helpers; verify complexity drops to ≤ 25
  and nesting to ≤ 3.
- [ ] Add both declarations to `TerrainSystem.h` private section.

##### 1d. `TerrainSystem::buildAllChunks` — S134 line 557

The per-chunk mesh build is inlined inside the `cx`/`cz` double loop, pushing
nesting to depth 4.

- [ ] Extract the per-chunk build body into a private method
  `void buildOneChunk(int cx, int cz, int chunkTiles, float cellSize)`.
- [ ] Refactor `buildAllChunks` to call the helper; verify nesting drops to ≤ 3.
- [ ] Add declaration to `TerrainSystem.h` private section.

##### 1e. `TerrainSystem::enqueueAllChunks` — S134 line 615

The priority-queue distance computation and insert are inlined inside the
`cx`/`cz` loop.

- [ ] Extract the per-chunk distance computation and enqueue into a private method
  `void enqueueOneChunk(int cx, int cz, int chunkTiles, float cellSize, const irr::core::vector3df& cameraPos)`.
- [ ] Refactor `enqueueAllChunks` to call the helper; verify nesting drops to ≤ 3.
- [ ] Add declaration to `TerrainSystem.h` private section.

---

#### 2. Fix `src/rendering/IrrlichtRenderer.cpp` — 10 issues

##### 2a. `IrrlichtRenderer::showServiceCoverageOverlay` — S3776 line 3254 (complexity 55) + S134 lines 3396, 3397, 3413, 3417

Inlines both the BFS path (PowerPlant) and the radius-enumeration path (other
service types) plus the SMesh buffer assembly — four levels of nesting inside both
paths.

- [ ] Extract the BFS tile collection into a private method
  `void collectBFSCoverageTiles(int tileX, int tileZ, int maxDepth, int mapW, int mapH, std::vector<irr::core::vector2di>& out)`.
- [ ] Extract the radius tile collection into a private method
  `void collectRadiusCoverageTiles(int tileX, int tileZ, float radiusM, int mapW, int mapH, std::vector<irr::core::vector2di>& out)`.
- [ ] Extract the SMesh buffer assembly into a private method
  `irr::scene::SMesh* buildCoverageMesh(const std::vector<irr::core::vector2di>& tiles, irr::video::SColor color, float kTileSize)`.
- [ ] Refactor `showServiceCoverageOverlay` to call these three helpers; verify
  complexity drops to ≤ 25 and nesting to ≤ 3.
- [ ] Add all three declarations to `IrrlichtRenderer.h` private section.

##### 2b. `IrrlichtRenderer::setZoneOverlay` — S3776 line 1107 (complexity 29)

Slightly over the threshold; the SMeshBuffer flush path is duplicated inline.

- [ ] Extract the per-buffer flush into a private method
  `void flushZoneOverlayBuffer(irr::scene::SMeshBuffer* buf, irr::scene::SMesh* mesh, int& quadsInCur)`.
- [ ] Refactor `setZoneOverlay` to call the helper; verify complexity drops to ≤ 25.
- [ ] Add declaration to `IrrlichtRenderer.h` private section.

##### 2c. `IrrlichtRenderer::pickTerrainTile` — S3776 line 691 (complexity 28)

DDA traversal with several initialisation and guard branches slightly over
threshold.

- [ ] Extract the DDA inner-loop body (the tMax/step advance + bounds check) into a
  private method
  `bool ddaAdvance(float& tMaxX, float& tMaxZ, float tDeltaX, float tDeltaZ, int& cx, int& cz, int dirX, int dirZ, int mapTilesX, int mapTilesZ) const`.
- [ ] Refactor `pickTerrainTile` to call the helper; verify complexity drops to ≤ 25.
- [ ] Add declaration to `IrrlichtRenderer.h` private section.

##### 2d. `IrrlichtRenderer::evictLODNodeRegistry` — S134 line 212

The `if (node && frameAge > threshold)` eviction body is nested at depth 4 inside
the registry iteration.

- [ ] Extract the single-entry eviction into a private method
  `void evictOneRegistryEntry(LODNodeRegistry& reg, uint64_t id)`.
- [ ] Refactor `evictLODNodeRegistry` to call the helper; verify nesting drops to ≤ 3.
- [ ] Add declaration to `IrrlichtRenderer.h` private section.

##### 2e. `IrrlichtRenderer::setCamera` — S134 line 432

The `if (m_camera)` → `if (m_terrain)` frustum-update chain reaches depth 4 at
line 432.

- [ ] Extract the frustum/clip-distance update block into a private method
  `void updateCameraFrustum(const CameraParams& p)`.
- [ ] Refactor `setCamera` to call the helper; verify nesting drops to ≤ 3.
- [ ] Add declaration to `IrrlichtRenderer.h` private section.

##### 2f. `IrrlichtRenderer::placeVehicle` — S134 line 2565

The material-binding loop (`for (u32 m ...) { if (!mat.getTexture(0)) … }`) is
at depth 4 inside the post-load node setup.

- [ ] Extract the atlas-binding loop into a private method
  `void bindVehicleAtlasMaterials(irr::scene::ISceneNode* node)`.
- [ ] Refactor `placeVehicle` to call the helper; verify nesting drops to ≤ 3.
- [ ] Add declaration to `IrrlichtRenderer.h` private section.

---

#### 3. Fix `src/simulation/CitySimulation.cpp` — 9 issues

##### 3a. `CitySimulation::placeZone` — S3776 line 285 (complexity 77) + S134 lines 311, 383, 385

`placeZone` is the highest-complexity function in the simulation layer. It inlines
footprint-bounds guard (N×N double loop), tile-occupancy check, cost computation,
road-proximity check, tile-state mutation, and renderer calls in one body.

- [ ] Add private helper
  `bool checkZoneFootprintClear(int tileX, int tileZ, int N) const` — runs the
  N×N occupancy loop; returns `false` on any occupied or out-of-bounds tile.
  Eliminates the `dx`/`dz` double loop at depth 4 (lines ~300–335).
- [ ] Add private helper
  `void applyZoneFootprint(int tileX, int tileZ, ZoneType type, DensityTier tier, int N)` —
  writes tile state and calls renderer for the full footprint (lines ~360–410).
- [ ] Refactor `placeZone` to call both helpers; verify complexity drops to ≤ 25 and
  nesting to ≤ 3.
- [ ] Add both declarations to `CitySimulation.h` private section.

##### 3b. `CitySimulation::demolishTile` — S3776 line 508 (complexity 27)

Just over the threshold. The renderer-removal dispatch and service-building erase
are inlined in sequence.

- [ ] Add private helper
  `void removeTileFromScene(int tileX, int tileZ, bool wasRoad, bool hadServiceBuilding, const TileData& prev)` —
  contains the `if (m_renderer)` renderer-removal block and the
  `std::remove_if` service-building erase (lines ~565–590).
- [ ] Refactor `demolishTile` to call the helper; verify complexity drops to ≤ 25.
- [ ] Add declaration to `CitySimulation.h` private section.

##### 3c. `CitySimulation::placeServiceBuilding` — S3776 line 596 (complexity 35) + S134 lines 607, 626

Two nested loops for footprint-clear check against existing tiles and existing
service buildings reach depth 4 at lines 607 and 626.

- [ ] Add private helper
  `bool checkServiceFootprintClear(int tileX, int tileZ, int sN) const` — runs the
  `dx`/`dz` footprint loop plus the existing-service-building overlap check
  (lines ~607–635).
- [ ] Refactor `placeServiceBuilding` to call the helper; verify complexity drops to
  ≤ 25 and nesting to ≤ 3.
- [ ] Add declaration to `CitySimulation.h` private section.

##### 3d. `CitySimulation::deserializeFromJson` — S3776 line 845 (complexity 51)

Large JSON parse with deeply nested `try`/`catch` blocks and many sequential field
reads.

- [ ] Add private helper
  `bool parseZoningSection(const nlohmann::json& j, std::string& err)` — parses
  the `"tiles"` and service-building arrays.
- [ ] Add private helper
  `bool parseTrafficSection(const nlohmann::json& j, std::string& err)` — parses
  the `"traffic"` sub-object.
- [ ] Add private helper
  `bool parseEconomySection(const nlohmann::json& j, int64_t& treasury, float taxRates[3], std::string& err)` —
  parses treasury, tax rates, bonds, and deficit counters.
- [ ] Refactor `deserializeFromJson` to delegate to these three helpers; verify
  complexity drops to ≤ 25.
- [ ] Add all three declarations to `CitySimulation.h` private section.

---

#### 4. Fix `src/platform/EventReceiver.cpp` — 4 issues

##### 4a. `EventReceiver::OnEvent` — S3776 line 18 (complexity 83) + S134 lines 60, 173, 174

`OnEvent` is a monolithic handler for GUI, keyboard, mouse, and window events in
one `if`/`switch` chain — the worst-case complexity in the platform layer.

- [ ] Extract GUI button click handling into a private method
  `bool handleGuiEvent(const irr::SEvent::SGUIEvent& guiEvt, InputEvent& out)`.
- [ ] Extract keyboard event handling into a private method
  `bool handleKeyboardEvent(const irr::SEvent::SKeyInput& key, InputEvent& out)`.
- [ ] Extract mouse event handling into a private method
  `bool handleMouseEvent(const irr::SEvent::SMouseInput& mouse, InputEvent& out)`.
- [ ] Refactor `OnEvent` to call these three helpers; verify complexity drops to ≤ 25
  and nesting to ≤ 3.
- [ ] Add all three declarations to `EventReceiver.h` private section.

---

#### 5. Fix `src/rendering/BuildingAssetLoader.cpp` — 4 issues

##### 5a. `BuildingAssetLoader::load` — S134 line 219

The atlas-path resolution (`if (buildingPos != npos) … else if (vehiclePos != npos)`)
is nested inside the LOD load loop at depth 4 (line 219).

- [ ] Extract the atlas-path resolution block into a private method
  `std::string resolveAtlasPath(const std::string& basePath) const`.
- [ ] Refactor `load` to call the helper; verify nesting drops to ≤ 3.
- [ ] Add declaration to `BuildingAssetLoader.h` private section.

##### 5b. `BuildingAssetLoader::parseMeta` — S3776 line 261 (complexity 36) + S134 lines 323, 331

Manual `strstr`-based JSON scanning with nested guard branches reaches depth 4 at
lines 323 and 331.

- [ ] Extract the `lod_distances` array parse into a private static method
  `static bool parseLodDistances(const std::string& content, float& lod0, float& lod1, float& cull)`.
- [ ] Extract the `atlas_cell` row/col parse into a private static method
  `static bool parseAtlasCell(const std::string& content, int& row, int& col)`.
- [ ] Refactor `parseMeta` to call both helpers; verify complexity drops to ≤ 25 and
  nesting to ≤ 3.
- [ ] Add both declarations to `BuildingAssetLoader.h` private section.

---

#### 6. Fix `src/simulation/Zoning.cpp` — 3 issues

##### 6a. `Zoning::applyDesirabilityScores` — S3776 line 578 (complexity 29) + S134 lines 598, 606

Iterates all tiles with nested zone-type dispatch and per-type desirability scoring,
reaching depth 4 at lines 598 and 606.

- [ ] Extract the per-tile desirability scoring block into a private non-static method
  `float computeTileDesirability(TileData& tile, int tileX, int tileZ, bool hasFireStation, bool hasPolice, bool hasWaterTower, bool hasPowerPlant, IAudioSystem* audio)` — non-static because it calls non-static private instance methods on `Zoning` such as `computeNeighborDesirabilityDelta` and `updateWaterState`.
- [ ] Refactor `applyDesirabilityScores` to call the helper; verify complexity drops
  to ≤ 25 and nesting to ≤ 3.
- [ ] Add declaration to `Zoning.h` private section.

---

#### 7. Fix `src/rendering/TextureCache.cpp` — 1 issue

##### 7a. `TextureCache::loadSRGB` — S3776 line 155 (complexity 48)

Long function dispatching on atlas type (buildings, vehicles, fallback) with
inline mip-level loops and OpenGL format checks.

- [ ] Extract the DDS header loading and validation block into a private method
  `bool parseDDSHeader(const std::vector<uint8_t>& fileData, long fileSize, uint32_t& width, uint32_t& height, uint32_t& mipCount, uint32_t& fourCC, std::string& error)`.
- [ ] Extract the raw-GL compressed upload loop (the `glCompressedTexImage2D` mip-level iteration) into a private method
  `GLuint uploadDXTCompressed(const std::vector<uint8_t>& fileData, long fileSize, uint32_t width, uint32_t height, uint32_t mipCount, uint32_t fourCC, int maxMipLevel, bool isBillboard)`.
- [ ] Refactor `loadSRGB` to call both helpers; verify complexity drops to ≤ 25.
- [ ] Add both declarations to `TextureCache.h` private section.

---

#### 8. Fix `src/simulation/Population.cpp` — 1 issue

##### 8a. `Population::applyDensityUpgrade` — S3776 line 243 (complexity 53)

Inlines upgrade-eligibility check, new-footprint clear check, footprint tile
rewrite, retry counter management, and renderer/audio notification.

- [ ] Add private method
  `bool validateUpgradeFootprint(Zoning& zoning, int tx, int tz, DensityTier targetDensity, int newN) const` —
  checks that the new N×N footprint is fully clear.
- [ ] Add private method
  `void applyUpgradeFootprint(Zoning& zoning, int tx, int tz, ZoneType zone, DensityTier targetDensity, int newN)` —
  writes tile state for the upgraded footprint.
- [ ] Add private method
  `void notifyUpgradeResult(int tx, int tz, ZoneType zone, DensityTier targetDensity, IRenderer* renderer, IAudioSystem* audio, std::queue<SimulationNotification>& notifications, int& sfxCalls)` —
  consolidates renderer mesh call, audio SFX, and notification push.
- [ ] Refactor `applyDensityUpgrade` to call these three helpers; verify complexity
  drops to ≤ 25.
- [ ] Add all three declarations to `Population.h` private section.

---

#### 9. Fix `src/ui/CameraController.cpp` — 1 issue

##### 9a. `CameraController::OnInputEvent` — S3776 line 35 (complexity 33)

Large `switch` on `InputEvent::Type` with nested `if` chains in the mouse-move and
zoom cases.

- [ ] Extract the mouse-drag update logic (the `Type::MouseMove` arm with
  `if (m_rmbDragActive)` / `if (m_mmbDragActive)` branches) into a private method
  `void applyMouseDrag(float dx, float dy)`.
- [ ] Extract the scroll-zoom logic (the `Type::MouseWheelScrolled` arm) into a
  private method `void applyScrollZoom(float delta)`.
- [ ] Refactor `OnInputEvent` to call both helpers; verify complexity drops to ≤ 25.
- [ ] Add both declarations to `CameraController.h` private section.

---

#### 10. Fix `src/main.cpp` — 1 issue

##### 10a. `main` — S3776 line 46 (complexity 47)

`main` inlines Irrlicht device construction, all system construction, the game loop,
and shutdown all together in one function.

- [ ] Extract all system construction and wiring (from device creation through
  `EventReceiver`, `UIManager`, `CitySimulation`, `AudioSystem` construction) into a
  `static` file-local function `static bool initSystems(...)` returning `false` on
  fatal error.
- [ ] Extract the per-frame game loop body (the `while (device->run())` body) into a
  `static` file-local function `static void runFrame(...)`.
- [ ] Refactor `main` to call `initSystems` then the `while` loop calling `runFrame`;
  verify complexity drops to ≤ 25.
- [ ] No new types or headers needed — these are `static` helpers at file scope.

---

#### 11. Build and test

- [ ] `make build` — fix all compiler errors.
- [ ] `ctest -LE "integration|requires-opengl"` — zero regressions.
- [ ] `ctest -L "^integration$"` — zero regressions.
- [ ] `xvfb-run --auto-servernum ctest -L "^requires-opengl$"` — zero regressions.
- [ ] `make test` — coverage build passes with ≥95% total line coverage and per-file 85% floor for all modified `src/simulation/` files (`Zoning.cpp`, `Population.cpp`, `CitySimulation.cpp`).

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q3.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` passes with zero new warnings.
- [ ] All three ctest suites pass with zero regressions.
- [ ] `make test` passes — ≥95% total line coverage gate enforced (per `architecture/testing/coverage.md`).
- [ ] Per-file 85% floor passes for all modified `src/simulation/` files (`Zoning.cpp`, `Population.cpp`, `CitySimulation.cpp`).
- [ ] SonarCloud re-scan confirms zero open HIGH MAINTAINABILITY issues in the project.
