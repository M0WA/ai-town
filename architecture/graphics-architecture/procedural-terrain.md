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

- Chunks loaded/unloaded based on camera distance
