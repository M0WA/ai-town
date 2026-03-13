# Procedural Terrain

- Do **not** use `ITerrainSceneNode`
- Terrain built as **chunked `IMeshBuffer` grids** from runtime-generated heightmap data
- `TerrainChunk` accepts a float heightmap buffer, `gridSize`, and `cellSize`; builds an `SMesh` and attaches it to an `IMeshSceneNode`
- **SMesh lifetime** — correct call order:

  ```cpp
  SMesh* smesh = buildTerrainMesh(heightData, gridSize, cellSize);
  // MANDATORY: recalculate bounding boxes before attaching to scene graph
  // Omitting this leaves a degenerate box at the origin — frustum culling will
  // incorrectly cull the chunk when the camera is not near (0,0,0)
  for (u32 i = 0; i < smesh->getMeshBufferCount(); ++i)
      smesh->getMeshBuffer(i)->recalculateBoundingBox();
  smesh->recalculateBoundingBox();  // must come AFTER all buffer recalculations
  IMeshSceneNode* node = sceneManager->addMeshSceneNode(smesh); // grabs the mesh internally
  smesh->drop(); // safe to drop NOW — addMeshSceneNode has already called grab()
  // NEVER call drop() before addMeshSceneNode(); that would free the mesh before the scene node acquires it
  ```

- **LOD transitions**: Managed by `TerrainSystem::update(float dt)` which processes a `std::deque<ChunkRebuildRequest>` (distance-weighted, nearest-first priority); pops **at most 2 entries per call** to amortize GPU upload cost. **LOD swap strategy differs by entity type**: For buildings and static vehicles, swap the mesh reference via `node->setMesh(newLODMesh)` — this replaces the mesh on the existing scene node, preserving its position, rotation, scale, and material assignments without touching the scene graph structure. Only create a new scene node when the node must be destroyed (entity death, chunk unload). For terrain chunks, a full node rebuild is required because LOD level changes involve different vertex counts (32×32 vs 16×16 vs 8×8 grid), which cannot be swapped with `setMesh()` alone.
  - **MANDATORY — `setMesh` requires bounding box recalculation before the call**: Before calling `node->setMesh(newLODMesh)`, the new mesh's bounding boxes must be recalculated — identical to the bounding box requirement for terrain mesh attachment. A stale bounding box from the previous LOD level causes incorrect frustum culling at the new LOD (the node may be invisible or always visible regardless of camera position). Required call order:

    ```cpp
    for (u32 i = 0; i < newLODMesh->getMeshBufferCount(); ++i)
        newLODMesh->getMeshBuffer(i)->recalculateBoundingBox();
    newLODMesh->recalculateBoundingBox();  // must come AFTER all buffer recalculations
    node->setMesh(newLODMesh);   // Irrlicht calls grab() on newLODMesh internally → ref_count becomes 2
    newLODMesh->drop();          // release caller's ref → ref_count drops to 1 (owned by scene node only)
    // CRITICAL: do NOT access newLODMesh after this drop() — it may still exist (ref_count=1) but the
    // caller no longer has a valid reference. Only the scene node holds the mesh reference now.
    ```

    **`recalculateBoundingBox()` type requirement**: The mesh pointer must be typed as `SMesh*` — see `scene-graph-ownership.md — LOD Swap — Bounding Box Requirement` for the full rule (do NOT use a `getMesh()` return value typed as `IMesh*`).
  - **CRITICAL — LOD rebuild must call `node->remove()` on the old node**: When replacing a chunk's scene node with a new LOD level, the old `IMeshSceneNode*` must be explicitly removed via `SceneEntityManager::destroy()` (which calls `node->remove()`) **before** creating the new node. Failing to remove the old node leaves orphaned nodes accumulating in the scene graph each LOD transition, causing unbounded memory growth and redundant render calls.
  - **Deque pointer safety**: The `ChunkRebuildRequest` struct must store a **`uint64_t` chunk ID** (not a raw `IMeshSceneNode*`). Before processing a request, validate the chunk is still live in `TerrainSystem::m_activeChunks` by ID. If not found (chunk was unloaded while request was queued), discard the request without dereferencing any pointer.
  - **Deque deduplication** (prevents same-frame double rebuild): `TerrainSystem::update()` must maintain a per-frame `std::unordered_set<uint64_t> processedThisFrame`. Before processing a request, skip if `processedThisFrame` already contains the chunk ID. Also skip if `it->second.currentLOD == req.targetLOD` (chunk already at the requested level — prevents redundant rebuilds from stale queued requests). This ensures the "never transitions up and down in the same frame" invariant is structurally enforced, not just documented as a property of `LODNode`.
  - **`TerrainSystem::flushPendingRebuilds()`**: `TerrainSystem` exposes a `flushPendingRebuilds()` method called once during the loading screen (after all initial chunks are queued) that processes the entire rebuild deque synchronously in a single frame — **bypassing the 2-per-frame cap**. This eliminates the startup LOD thrashing that would otherwise occur over the first ~N/2 frames as the deque drains at the normal rate. The method processes until the deque is empty or a per-call GPU upload time budget (default: 100 ms) is exhausted, whichever comes first, to prevent the loading screen from stalling visibly. The loading spinner progress bar should advance during this flush.

  **IClock Injection for Deterministic Testing**

  `TerrainSystem` constructor signature: `TerrainSystem(IRenderer* renderer, IClock* clock)`

  The `flushPendingRebuilds()` 100 ms wall-clock budget is measured via `m_clock->nowSeconds()`
  (NOT `std::chrono::steady_clock` directly), enabling deterministic testing:

  ```cpp
  void TerrainSystem::flushPendingRebuilds() {
      double start = m_clock->nowSeconds();
      while (!m_rebuildDeque.empty()) {
          if (m_clock->nowSeconds() - start >= 0.100) break;  // 100 ms budget
          // process one rebuild...
          m_rebuildDeque.pop_front();
      }
  }
  ```

  - Production: inject `WallClock` (`std::chrono::steady_clock`-based)
  - Tests: inject `ManualClock` to advance time programmatically

  `TerrainSystem_FlushPendingRebuilds_BudgetExhausted_StopsAfterBudget` test:

  1. Enqueue 10 rebuilds.
  2. Use `ManualClock` that returns `start + 0.101` on the second `nowSeconds()` call.
  3. After `flushPendingRebuilds()`, assert that < 10 rebuilds were processed (budget was hit).

- **Terrain Material (Phase 5 — untextured)**:

  Phase 5 terrain has no assigned textures. The material uses:

  - `EMF_LIGHTING = false` — completely unlit (normals computed but unused until Phase 6)
  - `EMF_BACK_FACE_CULLING = false` — both triangle sides rendered; avoids winding-order
    dependency before Phase 6 lighting validates the correct front-face convention
  - Vertex colours: height-interpolated green-to-brown gradient (`SColor(255, 34, 139, 34)`
    at sea level to `SColor(255, 139, 90, 20)` at ~80 m) for visual contrast against the
    sky-blue clear colour. Phase 9 replaces vertex colours with textured materials.

- **Triangle Winding Order (Left-Handed Coordinate System)**:

  Irrlicht uses a **left-handed** coordinate system (Y-up, Z-forward). Front faces are
  **clockwise (CW)** from the viewer's perspective. Terrain is viewed from above (+Y looking
  toward −Y), so front-face normals must point **UP (+Y)**.

  The correct winding for terrain quads (two triangles per quad cell) is:

  ```cpp
  // v0 = (row, col), v1 = (row, col+1), v2 = (row+1, col+1), v3 = (row+1, col)
  // Triangle 1: v0 → v2 → v1  (normal = +Y)
  // Triangle 2: v0 → v3 → v2  (normal = +Y)
  //
  // Proof: (v2−v0) × (v1−v0) = (cs, 0, cs) × (cs, 0, 0) = (0, +cs², 0) → +Y normal ✓
  ```

  **WRONG** winding (produces downward normals → backface culled → invisible terrain):

  ```cpp
  // v0 → v1 → v2, v0 → v2 → v3  ← DO NOT USE
  // (v1−v0) × (v2−v0) = (cs, 0, 0) × (cs, 0, cs) = (0, −cs², 0) → −Y normal ✗
  ```

  This winding rule applies to ALL terrain mesh construction: `IrrlichtRenderer::rebuildTerrainChunk()`,
  `TerrainChunk::buildMesh()`, and any future terrain mesh builder. Backface culling is enabled
  by default in Irrlicht — incorrect winding silently renders nothing (black screen) with no
  error or warning.

- **Terrain Generation Startup Wiring** (in `main.cpp`):

  After constructing `TerrainSystem`, the startup sequence is:

  ```text
  1. terrainSystem.generate(mapTilesX, mapTilesZ, cellSize, &rng)
  2. terrainSystem.buildAllChunks()       // subdivides heightmap into chunks, flushes all LOD0 rebuilds
  3. cameraController.setTarget(centerX, centerZ)  // center camera over terrain
  4. Main loop: terrainSystem.update(dt)   // per-frame LOD rebuild processing
  ```

  `buildAllChunks()` must be called AFTER `generate()` populates the heightmap.
  The camera target should be set to the terrain center (mapTilesX × cellSize / 2) to avoid
  starting with the camera pointed at the terrain corner.

- **Heightmap Query API**:

  `TerrainSystem::getHeightAt(int tileX, int tileZ)` returns the exact LOD0 heightmap height sample at the grid-centre of tile `(tileX, tileZ)`. **No interpolation is performed**. This method always queries the persistent LOD0 heightmap array stored in `TerrainSystem`, never the active scene-node mesh geometry (which may be rendered at LOD1 or LOD2). This contract is authoritative for ray-march cursor-to-terrain intersection queries (e.g. `pickTerrainTile()`): the query will always return the exact grid-centre height, regardless of which LOD level is currently rendered for that tile. Cursor positions that fall between grid-centres will not interpolate; callers requiring bilinear-interpolated heights for sub-tile precision must implement interpolation on top of multiple `getHeightAt()` calls.

  **`ITerrainQuery` interface promotion (Phase 9b)**: `getHeightAt` is promoted to the
  `ITerrainQuery` interface (`src/interfaces/ITerrainQuery.h`) so that `IrrlichtRenderer` can
  sample terrain height for zone overlay Y-positions, hover highlight Y-positions, and **mesh
  placement Y-positions** without a direct dependency on the concrete `TerrainSystem` class.
  **Phase 10b further extends `ITerrainQuery`** with the write method `setTileHeight()` and
  the synchronous flush method `flushTerrainRebuilds()` — see the `setTileHeight()` Write
  Path and Neighbour Blending section below.

  **MANDATORY — building, road, and service-building scene nodes must be placed at terrain
  height**: `IrrlichtRenderer::placeBuildingMesh()`, `placeRoadMesh()`, and
  `placeServiceBuildingMesh()` must place their scene node Y coordinate at the flattened
  terrain height `targetH` — NOT by calling `getHeightAt()` after the four `setTileHeight()`
  corner writes. The neighbour blending applied by each `setTileHeight()` call bleeds back
  into adjacent corners, leaving the `(tileX, tileZ)` vertex below `targetH` by the time
  all 4 calls complete. The correct pattern is:

  ```cpp
  // Read pre-flatten heights, compute average targetH, call setTileHeight for all 4 corners,
  // then use targetH directly for Y positioning.
  const float postY = m_terrain ? targetH : 0.0f;
  node->setPosition(irr::core::vector3df(
      static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f,
      postY + 0.10f,   // 10 cm above terrain for roads, buildings, and service buildings
      static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f));
  ```

  Hardcoding `y = 0.0f` places all meshes underground on elevated terrain tiles (terrain
  heights range 0–26 m on a default map). This applies to ALL three mesh placement methods.
  If `m_terrain` is null, fall back to `postY = 0.0f`. The full four-corner flatten
  sequence is documented in the `setTileHeight()` Placement Integration section below.

  **Z-fighting: polygon offset is mandatory** alongside the Y offset. A pure Y offset is
  insufficient at distances beyond ~400 m because 24-bit depth buffer precision degrades
  as Z²: `ΔZ ≈ 2 × Z² × (far − near) / (near × far × 2²⁴)`. At 400 m this is ~7 cm; at
  500 m ~11 cm — larger than any tolerable visual Y offset. All three placement helpers
  apply polygon offset to every material slot on the placed node:

  ```cpp
  mat.PolygonOffsetDirection = irr::video::EPO_FRONT;  // glPolygonOffset(-factor, -factor)
  mat.PolygonOffsetFactor    = 1;                       // 0 = off; 1–7 combined slope+constant bias
  ```

  Note: the vcpkg Irrlicht 1.8.x port exposes only `PolygonOffsetFactor` (3-bit, 0–7) and
  `PolygonOffsetDirection`. There is no separate `PolygonOffsetUnits` field.

  `EPO_FRONT` subtracts a small bias from fragment depth before the depth test, shifting
  the surface "closer" to the camera without altering geometry. This is distance-independent
  (applied in window-space depth units) and eliminates Z-fighting at all camera distances.
  For road meshes the polygon offset is set directly on the `SMeshBuffer` material inside
  `ensureRoadMeshes()` (all three LODs). For building and service-building nodes it is set
  per-material-slot in the post-load material loop in `placeBuildingMesh()` and
  `placeServiceBuildingMesh()`.
  The method signature on `ITerrainQuery` is:

  ```cpp
  // Returns Y-axis terrain height in world-space metres for the tile centre at (tileX, tileZ).
  // Returns 0.0f for out-of-bounds coordinates.
  // MUST query the persistent LOD0 heightmap array — never the active scene-node mesh geometry.
  virtual float getHeightAt(int tileX, int tileZ) const = 0;
  ```

  `TerrainSystem` implements this via its persistent LOD0 heightmap array (trivial — the method
  already exists on `TerrainSystem`; Phase 9b adds the `override` keyword and promotes the
  declaration to the interface).

  `ManualTerrainQuery` (test stub in `tests/simulation/manual_terrain_query.h`) must add:

  ```cpp
  float getHeightAt(int /*tileX*/, int /*tileZ*/) const override { return 0.0f; }
  ```

  This override is required because `getHeightAt()` is pure virtual on `ITerrainQuery`; without
  it `ManualTerrainQuery` fails to compile, blocking all 17 Phase 9b unit tests.

  **Map-dimension accessors** (Phase 9b, concrete `TerrainSystem` only — NOT added to `ITerrainQuery`):

  ```cpp
  // TerrainSystem.h — public accessors, consumed only from main.cpp (not via ITerrainQuery*)
  int   getMapTilesX() const;  // returns m_mapTilesX (set by generate())
  int   getMapTilesZ() const;  // returns m_mapTilesZ (set by generate())
  float getCellSize()  const;  // returns m_cellSize  (set by generate())
  ```

  These are intentionally NOT on `ITerrainQuery` — that interface is minimal by design (slope +
  height queries only). They are consumed from `main.cpp` to call `uiManager.setMapDimensions()`
  and `renderer.setCellSize()`.

- **pickTerrainTile DDA Algorithm** (normative specification for `IrrlichtRenderer::pickTerrainTile()`):

  **Background**: The Phase 9b blocking spike (2026-03-02) determined that a naive 4096-step
  linear march on a 1024×1024 terrain costs ~205 µs per call. Because `MouseMove` fires 4–10
  times per frame at 60 FPS on high-DPI mice, sustained cost reaches ~2 ms per frame — exceeding
  the 1 ms world-interaction budget. The O(1) DDA (Digital Differential Analyzer) grid traversal
  algorithm is therefore mandated. It traverses at most `mapTilesX + mapTilesZ` cells (worst case
  2048 for a 1024×1024 map), each requiring one array lookup (≈15 ns L3 hit), yielding ≤30 µs
  worst case even at 10 `MouseMove` events per frame.

  **Algorithm** (Amanatides & Woo 1987, "A Fast Voxel Traversal Algorithm"):

  ```cpp
  // Preconditions:
  //   m_terrain != nullptr  (caller must guard)
  //   m_cellSize > 0
  //   mapTilesX > 0, mapTilesZ > 0  (set via IrrlichtRenderer::setTerrainQuery wiring)
  //   ray obtained from:
  //     irr::core::line3df ray =
  //       smgr->getSceneCollisionManager()
  //           ->getRayFromScreenCoordinates({screenX, screenY}, camera);

  bool IrrlichtRenderer::pickTerrainTile(int screenX, int screenY,
                                          int& tileX, int& tileZ) const
  {
      if (!m_terrain) return false;

      irr::core::line3df ray =
          m_smgr->getSceneCollisionManager()
              ->getRayFromScreenCoordinates({screenX, screenY}, m_camera);

      irr::core::vector3df ro = ray.start;
      irr::core::vector3df rd = (ray.end - ray.start).normalize();

      // Horizontal ray cannot intersect terrain — bail early.
      if (std::fabs(rd.Y) < 1e-5f) return false;

      // --- DDA grid traversal ---
      // Step 1: Determine starting tile (clamp to map bounds).
      int cx = static_cast<int>(ro.X / m_cellSize);
      int cz = static_cast<int>(ro.Z / m_cellSize);
      cx = std::max(0, std::min(cx, m_mapTilesX - 1));
      cz = std::max(0, std::min(cz, m_mapTilesZ - 1));

      // Step 2: DDA step direction per axis (+1 or -1).
      int stepX = (rd.X >= 0.f) ? 1 : -1;
      int stepZ = (rd.Z >= 0.f) ? 1 : -1;

      // Step 3: Distance along ray to the first X and Z boundary crossing.
      float tMaxX, tMaxZ;
      if (std::fabs(rd.X) < 1e-6f) {
          tMaxX = 1e30f;   // ray is axis-aligned in Z — no X crossings
      } else {
          float nextBoundX = (stepX > 0)
              ? (static_cast<float>(cx + 1) * m_cellSize)
              : (static_cast<float>(cx)     * m_cellSize);
          tMaxX = (nextBoundX - ro.X) / rd.X;
      }
      if (std::fabs(rd.Z) < 1e-6f) {
          tMaxZ = 1e30f;
      } else {
          float nextBoundZ = (stepZ > 0)
              ? (static_cast<float>(cz + 1) * m_cellSize)
              : (static_cast<float>(cz)     * m_cellSize);
          tMaxZ = (nextBoundZ - ro.Z) / rd.Z;
      }

      // Step 4: Per-axis tDelta (ray distance between consecutive boundary crossings).
      float tDeltaX = (std::fabs(rd.X) < 1e-6f) ? 1e30f : std::fabs(m_cellSize / rd.X);
      float tDeltaZ = (std::fabs(rd.Z) < 1e-6f) ? 1e30f : std::fabs(m_cellSize / rd.Z);

      // Step 5: Traverse at most (mapTilesX + mapTilesZ) cells.
      int maxSteps = m_mapTilesX + m_mapTilesZ;
      for (int i = 0; i < maxSteps; ++i) {
          // Compute ray parameter t at the centre of the current cell.
          float tc = std::min(tMaxX, tMaxZ) - 0.5f * std::min(tDeltaX, tDeltaZ);
          tc = std::max(tc, 0.f);

          // Ray Y at current cell centre.
          float rayY = ro.Y + tc * rd.Y;

          // Sample terrain height at current cell.
          float h = m_terrain->getHeightAt(cx, cz);

          if (rayY <= h) {
              // Hit — ray has intersected terrain at this cell.
              tileX = std::max(0, std::min(cx, m_mapTilesX - 1));
              tileZ = std::max(0, std::min(cz, m_mapTilesZ - 1));
              return true;
          }

          // Advance to next cell boundary.
          if (tMaxX < tMaxZ) {
              cx    += stepX;
              tMaxX += tDeltaX;
          } else {
              cz    += stepZ;
              tMaxZ += tDeltaZ;
          }

          // Exit map bounds check.
          if (cx < 0 || cx >= m_mapTilesX || cz < 0 || cz >= m_mapTilesZ)
              return false;
      }

      return false;  // ray exited map without hitting terrain
  }
  ```

  **Performance contract**: at most `mapTilesX + mapTilesZ` iterations, each O(1). For a
  1024×1024 map, worst case = 2048 iterations × ~15 ns = ~30 µs. At 10 `MouseMove` events
  per frame at 60 FPS: ~300 µs sustained — within the 1 ms world-interaction budget.

  **IrrlichtRenderer members required by this algorithm** (all on `IrrlichtRenderer` directly,
  NOT on `IRenderer`):

  | Member | Type | Set by | Notes |
  |---|---|---|---|
  | `m_terrain` | `ITerrainQuery*` | `setTerrainQuery()` from `main.cpp` | Non-owning; null until wired |
  | `m_cellSize` | `float` | `setCellSize()` from `main.cpp` | Tile world-space width in metres |
  | `m_mapTilesX` | `int` | `setRendererMapDimensions()` from `main.cpp` | Phase 9b step (2b); default 0 causes immediate DDA early-exit |
  | `m_mapTilesZ` | `int` | `setRendererMapDimensions()` from `main.cpp` | Phase 9b step (2b); default 0 causes immediate DDA early-exit |
  | `m_camera` | `irr::scene::ICameraSceneNode*` | Assigned in `IrrlichtRenderer::init()` | Irrlicht type; lives in `IrrlichtRenderer.h` only; null guard required |

  **`setRendererMapDimensions(int w, int z)`**: new public method on `IrrlichtRenderer`
  (NOT on `IRenderer`). Called from `main.cpp` at step (2b) (after step 2a `setCellSize()`).
  Sets `m_mapTilesX = w` and `m_mapTilesZ = z`. Method name avoids collision with
  `UIManager::setMapDimensions()`.

  **`m_camera` member**: `IrrlichtRenderer` must store `irr::scene::ICameraSceneNode* m_camera`
  pointing to the active camera (already required for `getRayFromScreenCoordinates`). This is
  set during camera creation in `IrrlichtRenderer::init()` or via a `setActiveCamera()` call;
  if not already a member, it must be added as part of Phase 9b Deliverable B.

---

## `setTileHeight()` Write Path and Neighbour Blending (Phase 10b)

### Purpose

`ITerrainQuery::setTileHeight(int tileX, int tileZ, float height)` is the write-side
counterpart to `getHeightAt()`. It is called by `IrrlichtRenderer` placement helpers
(`placeBuildingMesh`, `placeRoadMesh`, `placeServiceBuildingMesh`) to flatten the terrain
under and around a placed tile, producing smooth visual transitions instead of hard seams
between placed structures and sloped terrain.

### Interface Declaration

```cpp
// src/interfaces/ITerrainQuery.h
// Writes `height` into the persistent LOD0 heightmap at (tileX, tileZ),
// applies neighbour blending to the 8 surrounding tiles, and enqueues
// ChunkRebuildRequests for all affected chunks.
// Out-of-bounds coordinates are silently ignored.
virtual void setTileHeight(int tileX, int tileZ, float height) = 0;
```

### TerrainSystem Implementation

`TerrainSystem::setTileHeight(int tileX, int tileZ, float height)` performs three steps:

**Step 1 — Centre tile write**: Write `height` into the persistent LOD0 heightmap array
at `(tileX, tileZ)`. Clamp `(tileX, tileZ)` to `[0, m_mapTilesX-1]` ×
`[0, m_mapTilesZ-1]` before the write.

**CRITICAL — dual heightmap sync**: The write MUST update BOTH `m_generatedHeightmap`
AND every entry in `m_chunkHeightmaps` that covers the modified tile. `processOneRebuild`
reads vertex heights from `m_chunkHeightmaps` (the per-chunk copy), not from
`m_generatedHeightmap` (the global flat array). Writing only `m_generatedHeightmap`
causes `processOneRebuild` to rebuild the chunk mesh with the old (pre-flatten) heights,
silently discarding the height change. Both arrays must be kept in sync on every write.

**Step 2 — Neighbour blending**: For each of the 8 surrounding tiles, compute the
neighbour's new height by linearly interpolating toward `height`:

```cpp
// Cardinal neighbours (N, S, E, W):
newH = lerp(currentH, height, kCardinalFalloff)
// Diagonal neighbours (NE, NW, SE, SW):
newH = lerp(currentH, height, kDiagonalFalloff)
```

Where `lerp(a, b, t) = a + t * (b - a)`.

`kCardinalFalloff` and `kDiagonalFalloff` are confirmed by `gamedesign-lookandfeel`
sign-off in Phase 10b (reference starting point: cardinal 0.5, diagonal 0.25). These
values are NOT engineering defaults — they are design decisions and MUST NOT be committed
before the sign-off is recorded.

All eight neighbour coordinates must be clamped to map bounds before any read or write.
Out-of-bounds neighbours are silently skipped (no write, no crash).

**Step 3 — Chunk rebuild enqueue**: After all heightmap writes (centre + in-bounds
neighbours), iterate over all modified tile coordinates and determine which chunk(s) each
tile belongs to. For each unique chunk affected:

1. **Mark the chunk dirty (LOD = -1) in `m_activeChunks`** before calling
   `enqueueRebuild`. `processOneRebuild` contains an "already at target LOD" guard:

   ```cpp
   if (it->second.currentLOD == req.targetLOD) return;  // skip rebuild
   ```

   If the chunk's `currentLOD` is not reset to −1 before the enqueue, the guard
   treats the already-rendered LOD level as "current" and silently discards the
   height-change rebuild request. The LOD is reset during the next actual rebuild, so
   the chunk remains permanently stuck with the old (pre-flatten) geometry.

2. Enqueue a `ChunkRebuildRequest`. Chunk deduplication by `uint64_t` chunk ID is
   already enforced in `TerrainSystem::update()`'s `processedThisFrame` set; duplicate
   entries in the deque are harmless but the enqueue loop should avoid unnecessary
   enqueues by checking whether a given chunk ID is already in the deque (optional
   optimisation — not required for correctness).

### Placement Integration

`IrrlichtRenderer::placeBuildingMesh()` and `placeServiceBuildingMesh()` call
`setTileHeight()` four times before creating the scene node — once per tile corner —
to flatten all 4 vertices of the tile quad to the average height, then place the flat
mesh on the flattened terrain.

`IrrlichtRenderer::placeRoadMesh()` uses a different strategy: **terrain-conforming sloped
road placement** (added after Phase 10b). Roads follow the terrain up to a 15° maximum
slope instead of being forced flat. The function signature is:

```cpp
// Public IRenderer override — delegates to internal extended version.
void placeRoadMesh(int tileX, int tileZ) override;

// Internal implementation (private).
// flattenTerrain: run slope-clamping setTileHeight() sequence if true.
// rebuildNeighbors: rebuild cardinal road neighbor meshes if true.
void placeRoadMesh(int tileX, int tileZ, bool flattenTerrain, bool rebuildNeighbors);
```

#### Road tile placement — conditional slope flattening

```text
max slope angle = atan(sqrt(dX² + dZ²))  where dX = (h10 - h00) / kTileSize, etc.
```

If `slopeAngle <= 15°`: corners are used as-is — no terrain modification, road follows
the natural terrain.

If `slopeAngle > 15°`: scale each corner's deviation from the tile average so the max
gradient equals `tan(15°)`:

```cpp
const float scale = tan(15°) / slopeMax;
const float avg   = (h00 + h10 + h01 + h11) * 0.25f;
h00 = avg + (h00 - avg) * scale;  // and similarly for h10, h01, h11
```

The (possibly adjusted) corner heights are then written back with `setTileHeight()` and
flushed with `flushTerrainRebuilds()`.

#### Per-tile LOD0 road mesh (terrain-conforming)

`buildTileRoadMesh(h00, h10, h01, h11)` builds a new `SMesh*` per tile. Vertex Y
positions carry the absolute world-space terrain heights plus a 10 cm bias:

```text
v0 = (-H, h00+0.10, -H)   back-left   (tileX,   tileZ)
v1 = (+H, h10+0.10, -H)   back-right  (tileX+1, tileZ)
v2 = (+H, h11+0.10, +H)   front-right (tileX+1, tileZ+1)
v3 = (-H, h01+0.10, +H)   front-left  (tileX,   tileZ+1)
```

Where `H = kTileSize / 2 = 5 m`. The scene node is positioned at world X/Z centre
with `Y = 0` — all height is baked into vertex positions so no double-offset occurs.

Kerb geometry uses the same corner heights so kerb bases follow the terrain edge.
Polygon offset (`EPO_FRONT`, factor=1) is applied to the mesh buffer material as the
primary Z-fighting defence.

LOD1 and LOD2 remain shared flat quads (used at 50–150 m and 150–300 m respectively).

#### Neighbor edge matching

After placing a road tile, all 4 cardinal neighbors that already have road nodes have
their meshes rebuilt (mesh only — no re-flattening, no recursive re-flattening):

```cpp
if (rebuildNeighbors) {
    for each cardinal neighbor (nx, nz):
        if m_roadNodes.count(tileKey(nx, nz)) > 0:
            placeRoadMesh(nx, nz, /*flattenTerrain=*/false, /*rebuildNeighbors=*/false);
}
```

This ensures neighbor road tiles always reflect the current terrain heights at their
shared edge — eliminating visible gaps where a newly-flattened tile meets an existing
road neighbor.

#### Buildings and service buildings (unchanged)

`placeBuildingMesh()` and `placeServiceBuildingMesh()` retain the original flat-quad
flattening pattern:

```cpp
const float h00 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ)     : 0.0f;
const float h10 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ)     : 0.0f;
const float h01 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ + 1) : 0.0f;
const float h11 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ + 1) : 0.0f;
const float targetH = (h00 + h10 + h01 + h11) * 0.25f;
if (m_terrain) {
    m_terrain->setTileHeight(tileX,     tileZ,     targetH);
    m_terrain->setTileHeight(tileX + 1, tileZ,     targetH);
    m_terrain->setTileHeight(tileX,     tileZ + 1, targetH);
    m_terrain->setTileHeight(tileX + 1, tileZ + 1, targetH);
}
if (m_terrain) m_terrain->flushTerrainRebuilds();

// Use targetH directly — NOT getHeightAt() after setTileHeight().
const float postY = m_terrain ? targetH : 0.0f;
node->setPosition(irr::core::vector3df(
    static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f,
    postY + 0.10f,
    static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f));
```

**Critical invariant**: `postY` must be `targetH`, not `getHeightAt(tileX, tileZ)` after
the four corner writes. The neighbour blending from each subsequent `setTileHeight()` call
bleeds back into the `(tileX, tileZ)` vertex, leaving it below `targetH` when all 4 calls
complete. Reading the corrupted vertex height for Y-positioning causes the mesh to sink
into the terrain — exactly the bug that `flushTerrainRebuilds()` alone cannot fix.

### flushTerrainRebuilds() — Synchronous Geometry Update After Placement

`ITerrainQuery` exposes a second write-side method added in Phase 10b:

```cpp
// src/interfaces/ITerrainQuery.h
/// Flush all pending terrain chunk rebuilds synchronously.
/// Called after setTileHeight to ensure terrain geometry matches the new
/// heightmap data before the next render frame.
virtual void flushTerrainRebuilds() = 0;
```

`TerrainSystem::flushTerrainRebuilds()` delegates directly to `flushPendingRebuilds()`:

```cpp
void TerrainSystem::flushTerrainRebuilds() {
    flushPendingRebuilds();
}
```

**Why this is necessary**: `TerrainSystem::update()` processes at most 2 chunk rebuilds
per frame (the normal amortised cap). After `setTileHeight()` enqueues rebuild requests
for the affected chunks, those rebuilds would otherwise be spread across multiple frames.
During those frames the terrain geometry still shows the original (pre-flatten) heights
while the road/building node is already positioned at `targetH`, making the terrain mesh
appear raised above the structure until the pending rebuilds are processed. Calling
`flushTerrainRebuilds()` immediately after all four `setTileHeight()` calls processes
every pending rebuild synchronously before the next render frame is drawn.

`IrrlichtRenderer` placement helpers call it as:

```cpp
if (m_terrain) m_terrain->flushTerrainRebuilds();
```

immediately after the four `setTileHeight()` calls and before computing `postY = targetH`.

### ManualTerrainQuery Stub

`ManualTerrainQuery` (test stub in `tests/simulation/manual_terrain_query.h`) must add
no-op overrides for both write-side methods to remain a concrete class:

```cpp
void setTileHeight(int /*tileX*/, int /*tileZ*/, float /*height*/) override {}
void flushTerrainRebuilds() override {}
```

These overrides are required because both methods are pure virtual on `ITerrainQuery`;
without them, `ManualTerrainQuery` fails to compile and all simulation tests that use it
are broken.

### pendingRebuildCount() Test API

```cpp
// Returns the number of ChunkRebuildRequests currently pending in the deque.
// Exposed for unit testing only — do NOT call in production rendering paths.
// Returns the raw deque depth (not deduplicated); duplicate chunk IDs may be present.
int pendingRebuildCount() const;
```

This method is used by `TerrainFlattening_SetTileHeight_EnqueuesRebuildForAffectedChunks`
to assert that a `setTileHeight()` call enqueues at least one (or two, for a chunk-boundary
tile) rebuild requests. It is declared `public` on `TerrainSystem` solely to avoid
requiring `friend` declarations or subclass seams in tests. Do not call from any non-test
code path.

### Rebuild Budget Interaction

`setTileHeight()` enqueues up to 9 `ChunkRebuildRequest`s per call (one per modified
tile, across at most 4 chunks for a corner placement). The standard 2-per-frame cap in
`TerrainSystem::update()` applies; all affected chunks are fully rebuilt within at most 5
frames at normal framerate. During loading screen `flushPendingRebuilds()` processes all
pending rebuilds synchronously (bypassing the cap), ensuring no deferred terrain
flattening is visible to the player after a save-load cycle.

- Chunks loaded/unloaded based on camera distance
