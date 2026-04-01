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
  - **CRITICAL — LOD rebuild must run the full eviction sequence on the old node**: When replacing a chunk's scene node with a new LOD level, the full eviction sequence must execute on the old node **before** creating the new node — in this order:

    1. Iterate all material slots on the old node — for every texture unit `t` in
       `[0, MATERIAL_MAX_TEXTURES)`, call `oldNode->getMaterial(t).setTexture(t, nullptr)`.
       This releases the driver's reference to each `ITexture*` so
       `TextureCache::evictUnreferenced()` can reclaim textures that are no longer
       referenced by any live node.
    2. `m_driver->setMaterial(SMaterial{})` — flush the driver's last-bound material
       state. Irrlicht caches the last-bound material; omitting this flush leaves stale
       texture pointers in the driver state until the next draw call overwrites them.
    3. Remove the old node from the scene graph via `SceneEntityManager::destroy()`,
       which calls `node->remove()`. Do NOT access the old node pointer after this step.

    See `scene-graph-ownership.md §Tile Node Eviction Sequence` for the canonical
    eviction pattern that terrain chunk rebuilds follow.

    Failing to remove the old node (or to clear texture references before removal)
    leaves orphaned nodes accumulating in the scene graph each LOD transition, causing
    unbounded memory growth and redundant render calls, as well as preventing
    unreferenced textures from being reclaimed by `evictUnreferenced()`.
  - **Deque pointer safety**: The `ChunkRebuildRequest` struct must store a **`uint64_t` chunk ID** (not a raw `IMeshSceneNode*`). Before processing a request, validate the chunk is still live in `TerrainSystem::m_activeChunks` by ID. If not found (chunk was unloaded while request was queued), discard the request without dereferencing any pointer.
  - **Deque deduplication** (prevents same-frame double rebuild): `TerrainSystem::update()` must maintain a per-frame `std::unordered_set<uint64_t> processedThisFrame`. Before processing a request, skip if `processedThisFrame` already contains the chunk ID. Also skip if `it->second.currentLOD == req.targetLOD` (chunk already at the requested level — prevents redundant rebuilds from stale queued requests). This ensures the "never transitions up and down in the same frame" invariant is structurally enforced, not just documented as a property of `LODNode`.
  - **`TerrainSystem::flushPendingRebuilds()`**: `TerrainSystem` exposes a `flushPendingRebuilds()` method called **once per frame** during the loading screen loop. It bypasses the normal 2-per-frame cap and processes as many rebuild requests as possible, but it is **not** a blocking loop-until-empty: it exits after a 100 ms CPU budget per call regardless of remaining queue depth, to prevent the loading screen from stalling visibly. `TerrainSystem::update(dt)` is also called every loading-screen frame; it continues draining any requests that `flushPendingRebuilds()` did not reach within its budget. Together, the two calls drain the rebuild deque progressively over multiple frames until empty. This cooperative approach eliminates the startup LOD thrashing that would otherwise occur if only the 2-per-frame normal cap were active, while still keeping the spinner animated and the UI responsive each frame.

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

Because vertex `(tileX, tileZ)` is shared by up to four chunks (see four-chunk
boundary vertex rule in Step 3), up to four `m_chunkHeightmaps` entries may require
updating — not just the entry for the chunk that "owns" tile `(tileX, tileZ)`.

For each affected chunk, determine its origin tile coordinates `(chunkMinTileX, chunkMinTileZ)`. Compute local array indices as: `localX = tileX − chunkMinTileX`, `localZ = tileZ − chunkMinTileZ`. Write the new height to the heightmap array at index `localZ × (chunkSize + 1) + localX`.

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
tile belongs to.

**Boundary vertex four-chunk rule**: Vertex `(mx, mz)` is shared by up to four
chunks — the chunks whose tile ranges include that coordinate as any of their four
corners. These are the chunks owning tiles `(mx, mz)`, `(mx-1, mz)`, `(mx, mz-1)`,
and `(mx-1, mz-1)` (all four clamped to `[0, m_mapTilesX-1] × [0, m_mapTilesZ-1]`
before conversion). Convert each of these four tile coordinates to a chunk ID via
`chunkIdOf()`. Deduplicate the resulting chunk IDs (a tile in the interior of a chunk
maps to only one ID; a tile on a chunk-boundary edge maps to two; a tile exactly at a
chunk corner maps to four). Mark each unique chunk `currentLOD = -1` and enqueue it
for rebuild. Without this rule, chunks adjacent to the modified tile share the boundary
vertex in their rendered mesh but never receive a rebuild request, producing visible
height seams ("holes") at chunk boundaries after terrain flattening.

For each unique chunk affected:

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

`IrrlichtRenderer::placeRoadMesh()` uses **terrain-conforming sloped road placement**.
Roads follow terrain up to a **5% maximum grade** (0.05 rise/run); steeper terrain is
flattened. The function signature is:

```cpp
// Public IRenderer override — delegates to internal extended version.
void placeRoadMesh(int tileX, int tileZ) override;

// Internal implementation (private).
// flattenTerrain: run grade-clamping setTileHeight() sequence if true.
// rebuildNeighbors: rebuild road tile meshes in 5×5 area if true.
void placeRoadMesh(int tileX, int tileZ, bool flattenTerrain, bool rebuildNeighbors);
```

#### Road tile placement — two-phase terrain flattening

`placeRoadMesh` uses a two-phase approach to ensure all road and terrain meshes remain
consistent after placement.

**Phase 1 — flatten main tile only, then flush**

```cpp
static constexpr float kMaxRoadGrade = 0.05f;  // 5% = 0.05 rise/run

const float dX0 = (h10 - h00) / kTileSize;  const float dX1 = (h11 - h01) / kTileSize;
const float dZ0 = (h01 - h00) / kTileSize;  const float dZ1 = (h11 - h10) / kTileSize;
const float gradeMax = max(mag(dX0,dZ0), mag(dX0,dZ1), mag(dX1,dZ0), mag(dX1,dZ1));

if (gradeMax > kMaxRoadGrade) {
    const float scale = kMaxRoadGrade / gradeMax;
    const float avg   = (h00 + h10 + h01 + h11) * 0.25f;
    h_i = avg + (h_i - avg) * scale;   // for each of h00, h10, h01, h11
    // write all 4 corners via setTileHeight — no flush yet
}
m_terrain->flushTerrainRebuilds();   // single flush after all writes
```

**Do not flatten neighbour tiles in Phase 1.** Each neighbour's `setTileHeight` reads
already-modified shared corners, computes a different average, and writes different
values back — Phase 2 would then re-read the corrupted values and produce an
inconsistent main tile mesh.

**Phase 2 — re-read heights and build mesh**

Re-read all 4 corners from terrain after the flush (canonical values after all writes
and blending). Build the terrain-conforming road quad:

```text
v0 = (-H, h00+0.25, -H)   back-left   (tileX,   tileZ)
v1 = (+H, h10+0.25, -H)   back-right  (tileX+1, tileZ)
v2 = (+H, h11+0.25, +H)   front-right (tileX+1, tileZ+1)
v3 = (-H, h01+0.25, +H)   front-left  (tileX,   tileZ+1)
```

Y bias = **0.25 m**. A 0.10 m bias causes Z-fighting at oblique angles; 0.25 m
eliminates it at all normal camera angles. The scene node Y = 0 — heights are baked
into vertex positions. No kerb geometry — roads are plain flat quads tiling edge-to-edge
(kerbs extended ±0.15 m beyond the tile boundary, causing Z-fight seams with neighbors).

**Phase 3 — rebuild 5×5 road area**

`setTileHeight()` applies weighted blending to the 8 surrounding vertices per call
(cardinal ×0.5, diagonal ×0.25). The four corner writes together modify vertices up to
2 tile-lengths away. Road tiles within ±2 in each axis can have stale meshes:

```cpp
for (int dz = -2; dz <= 2; ++dz)
    for (int dx = -2; dx <= 2; ++dx) {
        if (dx == 0 && dz == 0) continue;
        if road tile exists at (tileX+dx, tileZ+dz):
            placeRoadMesh(nx, nz, flattenTerrain=false, rebuildNeighbors=false);
    }
```

`flattenTerrain=false` prevents cascading height writes; `rebuildNeighbors=false`
prevents infinite recursion.

#### Road tile scene node material requirements

Road LOD0 mesh has 4 buffers: `[0]` carriageway, `[1]` south kerb, `[2]` north kerb,
`[3]` center-line. The post-bind loop must preserve the per-buffer `PolygonOffsetFactor`
set at mesh-creation time: buffers 0–2 use `factor = 4`; buffer 3 (center-line) uses
`factor = 5` to win the depth test against the carriageway. **Do not overwrite buffer 3's
factor in the post-bind loop.**

```cpp
for (u32 m = 0; m < node->getMaterialCount(); ++m) {
    SMaterial& mat = node->getMaterial(m);
    mat.Lighting               = false;
    mat.BackfaceCulling        = false;   // MANDATORY — see below
    mat.PolygonOffsetDirection = irr::video::EPO_FRONT;
    if (m != 3) mat.PolygonOffsetFactor = 4;  // buffer 3 keeps factor=5 from mesh creation
}
node->setAutomaticCulling(irr::scene::EAC_OFF);  // MANDATORY — see below
```

**`BackfaceCulling = false` (MANDATORY)**: Road tiles tilt up to 5% with terrain. At
oblique angles one triangle of the quad faces away from the camera — default
`BackfaceCulling = true` silently culls it, making half the tile disappear. Must be set
on **both** the `SMeshBuffer` material and the node's own material copies (Irrlicht's
`CMeshSceneNode::copyMaterials()` is version-dependent; setting explicitly on the node
after `addMeshSceneNode()` is the only reliable guarantee).

**`EAC_OFF` (MANDATORY)**: Road tile AABBs are nearly flat; oblique angles cause
false AABB frustum rejection. Disabling automatic culling for road nodes is required.

**MANDATORY — bounding box Y-extent expansion**: After `buf->recalculateBoundingBox()`,
expand Y span to at least **0.5 m** before `addMeshSceneNode()`:

```cpp
core::aabbox3df box = buf->getBoundingBox();
const float yMid = (box.MaxEdge.Y + box.MinEdge.Y) * 0.5f;
if (box.MaxEdge.Y - box.MinEdge.Y < 0.5f) {
    box.MinEdge.Y = yMid - 0.25f;
    box.MaxEdge.Y = yMid + 0.25f;
    buf->setBoundingBox(box);
}
mesh->addMeshBuffer(buf);
buf->drop();
mesh->recalculateBoundingBox();
```

LOD1 and LOD2 remain shared flat quads (used at 50–150 m and 150–300 m respectively).

#### Buildings and service buildings — flat-quad flattening + road rebuild

`placeBuildingMesh()` and `placeServiceBuildingMesh()` flatten all 4 corners to the
tile average, flush, then **rebuild road tiles in the same 5×5 area** — because
`setTileHeight()`'s blending propagates to vertices 2 tiles away, making nearby road
meshes stale:

```cpp
const float targetH = (h00 + h10 + h01 + h11) * 0.25f;
m_terrain->setTileHeight(tileX,     tileZ,     targetH);
m_terrain->setTileHeight(tileX + 1, tileZ,     targetH);
m_terrain->setTileHeight(tileX,     tileZ + 1, targetH);
m_terrain->setTileHeight(tileX + 1, tileZ + 1, targetH);
m_terrain->flushTerrainRebuilds();

// Rebuild road tiles within ±2 — same blending radius as placeRoadMesh Phase 3.
for (int dz = -2; dz <= 2; ++dz)
    for (int dx = -2; dx <= 2; ++dx) {
        if (dx == 0 && dz == 0) continue;
        if road tile exists at (tileX+dx, tileZ+dz):
            placeRoadMesh(nx, nz, flattenTerrain=false, rebuildNeighbors=false);
    }

// Use targetH directly — NOT getHeightAt() after setTileHeight().
const float postY = m_terrain ? targetH : 0.0f;
node->setPosition(... postY + 0.10f ...);
```

**Note**: The example above shows Low-density (1×1) building placement. For buildings with multi-tile footprints (Medium: N=2, High: N=3, Service: N=2), the caller must iterate the full N×N grid and call `setTileHeight()` for every corner vertex of the footprint. See `architecture/game-design/terrain-interaction.md` — *Multi-tile footprint extension* — for the loop pattern.

**Critical invariant**: `postY` must be `targetH`, not `getHeightAt(tileX, tileZ)` after
the four corner writes. The neighbour blending from each subsequent `setTileHeight()` call
bleeds back into the `(tileX, tileZ)` vertex, leaving it below `targetH` when all 4 calls
complete. Reading the corrupted vertex height for Y-positioning causes the mesh to sink
into the terrain.

**Zone placement — border-ring road-tile flatten (Phase 11m)**: After flattening the N×N
footprint, the caller (`CitySimulation::placeZone()`) must also call `setTileHeight()` for
every road tile within the 1-tile orthogonal+diagonal border ring of the footprint (a
(N+2)×(N+2) candidate set minus the N×N footprint itself), bringing adjacent roads to the
same `flatHeight`. Non-road tiles in the border ring are NOT modified. This prevents road
geometry from intersecting the terrain after zone-triggered terrain flattening. After the
border-ring loop completes, the caller must call `flushTerrainRebuilds()` (see
*flushTerrainRebuilds() — Synchronous Geometry Update After Placement* below) to
synchronously apply all height changes to terrain geometry before the next render frame;
without this call, terrain chunks remain at pre-flatten heights for multiple frames.

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

## Map Size Presets

Three named presets map the New Game screen selection to `mapTilesX`/`mapTilesZ` values passed to `TerrainSystem::generate()`. All maps are square (`mapTilesX == mapTilesZ`).

| Preset | `mapTilesX` / `mapTilesZ` | Approx. world size (at 10 m/tile) |
|---|---|---|
| `kSmall` | 128 | 1 280 m × 1 280 m |
| `kMedium` | 512 | 5 120 m × 5 120 m |
| `kLarge` | 1024 | 10 240 m × 10 240 m |

`TerrainSystem::generate()` already accepts arbitrary `mapTilesX`/`mapTilesZ` — the presets are UI-facing aliases, not internal constants in `TerrainSystem`. They are defined in `MainMenuPanel.h` as `enum class MapSize { kSmall = 128, kMedium = 512, kLarge = 1024 }`.

### getPendingRebuildIds() Test API

```cpp
// Returns the deduplicated set of chunk IDs currently queued for rebuild.
// Exposed for unit testing only — do NOT call in production rendering paths.
// Duplicates are removed in the returned vector; order is unspecified.
std::vector<uint64_t> getPendingRebuildIds() const;
```

This method is used by tests that must verify **which specific chunk IDs** are enqueued
for rebuild — for example,
`TerrainSystem_SetTileHeight_AtChunkBoundary_BothChunksEnqueued` (phase-11l Deliverable 1)
asserts that both the `(0,0)` and `(1,0)` chunk IDs appear after a boundary tile write.
`pendingRebuildCount()` cannot distinguish this case because it returns only a raw count
and may include duplicates. `getPendingRebuildIds()` returns each chunk ID at most once,
regardless of how many times that chunk was enqueued. It is declared `public` on
`TerrainSystem` solely to avoid requiring `friend` declarations or subclass seams in
tests. Do not call from any non-test code path.

### Rebuild Budget Interaction

`setTileHeight()` enqueues up to 9 `ChunkRebuildRequest`s per call (one per modified
tile, across at most 4 chunks for a corner placement). The standard 2-per-frame cap in
`TerrainSystem::update()` applies; all affected chunks are fully rebuilt within at most 5
frames at normal framerate. During the loading screen, `flushPendingRebuilds()` is called once per frame with a
100 ms CPU budget per call (not a blocking loop-until-empty); `terrainSystem->update(dt)`
is also called each frame to drain any requests not reached within that budget. For
placement-triggered rebuilds (at most ~9 chunks per `setTileHeight()` call), the 100 ms
budget is nearly always sufficient to drain the small pending queue in the single
`flushTerrainRebuilds()` call made immediately after placement, ensuring terrain geometry
matches the new heightmap before the next render frame is drawn.

- Chunks loaded/unloaded based on camera distance
