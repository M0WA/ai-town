// TerrainSystem.cpp — Phase 5 full implementation.
//
// Manages terrain chunk LOD rebuild deque, per-frame deduplication,
// and deterministic flushPendingRebuilds() with IClock-based 100 ms budget.
//
// LOD rebuild strategy: FULL NODE REBUILD (never setMesh — vertex counts differ per LOD).
// SceneEntityManager::destroy() called on old node BEFORE creating new node.
// Chunk IDs stored in m_activeChunks (NOT raw node pointers).
//
// Per procedural-terrain.md:
//   - update() pops at most 2 entries per call
//   - Per-frame dedup set processedThisFrame prevents same-chunk double rebuild
//   - flushPendingRebuilds() processes until deque empty or 100 ms budget exhausted
//   - flushPendingRebuilds() is unblocked by Phase 2 Checkbox B (setMesh spike verified)
//
// See architecture/graphics-architecture/procedural-terrain.md
// See architecture/graphics-architecture/scene-graph-ownership.md

#include "TerrainSystem.h"

#include <algorithm>   // std::stable_sort
#include <cmath>       // std::atan, std::sqrt, std::abs
#include <queue>       // std::queue (BFS for contiguous region)

// IRenderer is forward-declared in TerrainSystem.h.
// Include the actual header here for any method calls.
#include "../interfaces/IRenderer.h"
#include "../rendering/render_constants.h"  // RenderConstants::kTileSize (getHeightAtWorld)
// IClock is included via TerrainSystem.h (full definition required for subclassing in tests).

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TerrainSystem::TerrainSystem(IRenderer* renderer, IClock* clock)
    : m_renderer{renderer}
    , m_clock{clock}
{
}

// ---------------------------------------------------------------------------
// LOD hysteresis distance accessors (static)
// Per architecture/asset-standards/3d-model-standards.md terrain LOD table:
//   LOD0→LOD1: switch-out >100 m, switch-in <92 m  (8 m hysteresis band)
//   LOD1→LOD2: switch-out >300 m, switch-in <285 m (15 m hysteresis band)
//
// A-28: replaced if-else chains with static constexpr arrays indexed by fromLOD.
// Index 0 = LOD0 thresholds; index 1 = LOD1 thresholds.
// fromLOD values outside [0,1] return 0.0f (clamped to index 1 and checked).
// ---------------------------------------------------------------------------
float TerrainSystem::lodSwitchOutDistance(int fromLOD) {
    static constexpr float kSwitchOut[] = {
        kLOD0to1SwitchOut,  // fromLOD 0: 100.5f (strictly > 100 m)
        kLOD1to2SwitchOut,  // fromLOD 1: 300.5f (strictly > 300 m)
    };
    if (fromLOD < 0 || fromLOD > 1) return 0.0f;
    return kSwitchOut[fromLOD];
}

float TerrainSystem::lodSwitchInDistance(int fromLOD) {
    static constexpr float kSwitchIn[] = {
        kLOD0to1SwitchIn,   // fromLOD 0:  91.5f (strictly < 92 m)
        kLOD1to2SwitchIn,   // fromLOD 1: 284.5f (strictly < 285 m)
    };
    if (fromLOD < 0 || fromLOD > 1) return 0.0f;
    return kSwitchIn[fromLOD];
}

// ---------------------------------------------------------------------------
// registerChunkAtLOD / unregisterChunk / getChunkLOD
// ---------------------------------------------------------------------------
void TerrainSystem::registerChunkAtLOD(uint64_t chunkId, int currentLOD) {
    m_activeChunks[chunkId] = currentLOD;
}

void TerrainSystem::unregisterChunk(uint64_t chunkId) {
    m_activeChunks.erase(chunkId);
    m_chunkWorldOrigins.erase(chunkId);
    m_chunkHeightmaps.erase(chunkId);
}

// ---------------------------------------------------------------------------
// clearAllChunks() — remove all active terrain chunk scene nodes and clear
// the internal chunk tracking maps.
// ---------------------------------------------------------------------------
void TerrainSystem::clearAllChunks() {
    for (const auto& kv : m_activeChunks) {
        if (m_renderer) {
            m_renderer->removeTerrainChunk(kv.first);
        }
    }
    m_activeChunks.clear();
    m_chunkWorldOrigins.clear();
    m_chunkHeightmaps.clear();
    m_rebuildDeque.clear();
}

void TerrainSystem::registerChunkPosition(uint64_t chunkId, float worldOriginX, float worldOriginZ) {
    m_chunkWorldOrigins[chunkId] = ChunkOrigin{worldOriginX, worldOriginZ};
}

void TerrainSystem::registerChunkHeightmap(uint64_t chunkId, std::vector<float> heightmap) {
    m_chunkHeightmaps[chunkId] = std::move(heightmap);
}

int TerrainSystem::getChunkLOD(uint64_t chunkId) const {
    auto it = m_activeChunks.find(chunkId);
    if (it == m_activeChunks.end()) return -1;
    return it->second;
}

// ---------------------------------------------------------------------------
// enqueueRebuild
// ---------------------------------------------------------------------------
void TerrainSystem::enqueueRebuild(uint64_t chunkId, int targetLOD, float distanceToCamera) {
    ChunkRebuildRequest req;
    req.chunkId          = chunkId;
    req.targetLOD        = targetLOD;
    req.distanceToCamera = distanceToCamera;
    // Insert in sorted order (nearest-first) per procedural-terrain.md.
    // std::lower_bound + deque::insert keeps the deque sorted in O(n) per insertion
    // (O(log n) comparisons + O(n) shift) — O(n log n) total for N insertions.
    // This is equivalent to stable_sort after push_back but avoids a full re-sort
    // on every call when most insertions land near the back (newly enqueued distant chunks).
    auto pos = std::lower_bound(m_rebuildDeque.begin(), m_rebuildDeque.end(), req,
                                [](const ChunkRebuildRequest& a, const ChunkRebuildRequest& b) {
                                    return a.distanceToCamera < b.distanceToCamera;
                                });
    m_rebuildDeque.insert(pos, req);
}

// ---------------------------------------------------------------------------
// processOneRebuild — factored helper used by update() and flushPendingRebuilds()
// ---------------------------------------------------------------------------
bool TerrainSystem::processOneRebuild(const ChunkRebuildRequest& req,
                                       std::unordered_set<uint64_t>& processedThisFrame) {
    // Dedup: skip if this chunk was already processed in this update/flush call.
    if (processedThisFrame.find(req.chunkId) != processedThisFrame.end()) {
        return false;
    }

    // Validate: skip if chunk is no longer live (was unloaded while queued).
    // Per procedural-terrain.md: "validate the chunk is still live in TerrainSystem::m_activeChunks
    // by ID. If not found (chunk was unloaded while request was queued), discard the request
    // without dereferencing any pointer."
    auto it = m_activeChunks.find(req.chunkId);
    bool chunkActive = (it != m_activeChunks.end());

    // Skip if chunk is already at the requested LOD level.
    // Catches stale queued requests from camera movement reversals.
    if (chunkActive && it->second == req.targetLOD) {
        return false;
    }

    // Mark as processed this frame/flush to prevent duplicate processing.
    processedThisFrame.insert(req.chunkId);

    // -------------------------------------------------------------------------
    // Steps 1–4: delegate the full node rebuild to IRenderer::rebuildTerrainChunk().
    //
    // Per architecture/graphics-architecture/procedural-terrain.md:
    //   FULL NODE REBUILD required (vertex counts differ per LOD level) — never setMesh.
    //   SceneEntityManager::destroy() on old node BEFORE creating new node.
    //   Chunk IDs stored in m_activeChunks (NOT raw node pointers).
    //
    // TerrainSystem must NOT call Irrlicht API directly — it holds IRenderer* only.
    // All five steps are encapsulated in IRenderer::rebuildTerrainChunk():
    //   Step 1: Remove old scene node (eviction sequence from scene-graph-ownership.md).
    //   Step 2: Build new SMesh* at targetLOD grid size from the chunk's heightmap.
    //   Step 3: recalculateBoundingBox() on every SMeshBuffer AND the SMesh (MANDATORY).
    //   Step 4: addMeshSceneNode(smesh) + smesh->drop().
    //   Step 5: Register new node internally keyed by chunkId.
    //
    // If m_renderer is null (EDT_NULL test context without a real renderer), skip the
    // render call — the LOD tracking update below still runs, keeping the system consistent.
    // -------------------------------------------------------------------------
    if (m_renderer) {
        // Determine target LOD grid size from the spec constants (A-35).
        // Index: 0=LOD0 (32 quads/side), 1=LOD1 (16 quads/side), 2+=LOD2 (8 quads/side).
        static constexpr int kLODGridSizes[] = {
            kTerrainLOD0GridSize,   // LOD0: 32 quads per side
            kTerrainLOD1GridSize,   // LOD1: 16 quads per side
            kTerrainLOD2GridSize,   // LOD2:  8 quads per side
        };
        const int lodIndex = std::min(req.targetLOD, 2);
        const int targetGridSize = kLODGridSizes[lodIndex];

        // Look up the chunk's LOD0 heightmap.
        auto hmapIt = m_chunkHeightmaps.find(req.chunkId);
        if (hmapIt != m_chunkHeightmaps.end()) {
            const std::vector<float>& lod0Hmap = hmapIt->second;
            const int lod0GridSize  = kTerrainLOD0GridSize;     // 32
            const int lod0Verts     = lod0GridSize + 1;         // 33 vertices per side
            const int targetVerts   = targetGridSize + 1;

            // Downsample the LOD0 heightmap to the target LOD resolution by
            // stride-sampling: for each vertex (x,z) in the target grid, sample
            // the corresponding LOD0 vertex at stride = lod0GridSize / targetGridSize.
            //
            // Example: LOD0=32, LOD1=16 → stride=2; LOD2=8 → stride=4.
            // Each LOD grid vertex maps exactly to a LOD0 vertex (power-of-2 division).
            const int stride = (targetGridSize > 0) ? (lod0GridSize / targetGridSize) : 1;

            std::vector<float> downsampledHmap;
            downsampledHmap.resize(static_cast<size_t>(targetVerts * targetVerts));

            for (int tz = 0; tz < targetVerts; ++tz) {
                for (int tx = 0; tx < targetVerts; ++tx) {
                    int srcX = tx * stride;
                    int srcZ = tz * stride;
                    // Clamp to LOD0 grid bounds (handles the last vertex at gridSize).
                    if (srcX > lod0GridSize) srcX = lod0GridSize;
                    if (srcZ > lod0GridSize) srcZ = lod0GridSize;

                    downsampledHmap[static_cast<size_t>(tz * targetVerts + tx)] =
                        lod0Hmap[static_cast<size_t>(srcZ * lod0Verts + srcX)];
                }
            }

            // Look up the chunk's world-space origin (defaulting to (0,0) if not registered).
            float worldOriginX = 0.0f;
            float worldOriginZ = 0.0f;
            auto originIt = m_chunkWorldOrigins.find(req.chunkId);
            if (originIt != m_chunkWorldOrigins.end()) {
                worldOriginX = originIt->second.x;
                worldOriginZ = originIt->second.z;
            }

            // The cell size for the rebuilt mesh: the chunk's physical extent divided by
            // the target grid size. If cellSize is stored in m_cellSize (the map tile size),
            // a chunk at LOD0 covers (lod0GridSize * m_cellSize) world units. The rebuilt
            // mesh keeps the same physical footprint regardless of LOD — only vertex density
            // changes. So cellSize per quad = (lod0GridSize * m_cellSize) / targetGridSize.
            //
            // When m_cellSize == 0 (not yet set via generate()), fall back to 1.0f to avoid
            // division by zero. This is a safe fallback for test contexts.
            const float chunkWorldSize = (m_cellSize > 0.0f)
                ? static_cast<float>(lod0GridSize) * m_cellSize
                : static_cast<float>(lod0GridSize);
            const float targetCellSize = (targetGridSize > 0)
                ? chunkWorldSize / static_cast<float>(targetGridSize)
                : 1.0f;

            TerrainChunkRebuildParams params;
            params.chunkId      = req.chunkId;
            params.heightmap    = std::move(downsampledHmap);
            params.gridSize     = targetGridSize;
            params.cellSize     = targetCellSize;
            params.worldOriginX = worldOriginX;
            params.worldOriginZ = worldOriginZ;

            m_renderer->rebuildTerrainChunk(params);
        }
        // If no heightmap is registered (test context or initial enqueue before registration),
        // skip the render call — LOD tracking update still runs below.
    }

    // -------------------------------------------------------------------------
    // Step 5: Update the active chunks map to reflect the new LOD.
    // This always runs (even when m_renderer is null or heightmap is absent) to keep
    // the deque deduplication and getChunkLOD() lookups consistent.
    // -------------------------------------------------------------------------
    if (chunkActive) {
        it->second = req.targetLOD;
    } else {
        // Chunk was not registered — add it at the target LOD.
        m_activeChunks[req.chunkId] = req.targetLOD;
    }

    return true; // rebuild was processed
}

// ---------------------------------------------------------------------------
// update — process at most 2 rebuild requests per call
// ---------------------------------------------------------------------------
void TerrainSystem::update(float /*dt*/) {
    // Per-frame deduplication set — prevents same-chunk double rebuild within one frame.
    // Declared per update() call per procedural-terrain.md.
    std::unordered_set<uint64_t> processedThisFrame;

    m_chunksRebuiltLastFrame = 0;
    int processedCount = 0;

    // Process at most 2 entries per frame per procedural-terrain.md spec.
    while (!m_rebuildDeque.empty() && processedCount < 2) {
        ChunkRebuildRequest req = m_rebuildDeque.front();
        m_rebuildDeque.pop_front();

        if (processOneRebuild(req, processedThisFrame)) {
            ++processedCount;
            ++m_chunksRebuiltLastFrame;
        }
        // If processOneRebuild returns false, the request is discarded without counting.
    }
}

// ---------------------------------------------------------------------------
// flushPendingRebuilds — process until deque empty or 100 ms budget exhausted
// ---------------------------------------------------------------------------
void TerrainSystem::flushPendingRebuilds(ITerrainLoadProgress* cb) {
    double start = m_clock ? m_clock->nowSeconds() : 0.0;
    int total = static_cast<int>(m_rebuildDeque.size());
    int done  = 0;

    // Per-flush deduplication set.
    std::unordered_set<uint64_t> processedThisFrame;

    m_chunksRebuiltLastFlush = 0;

    while (!m_rebuildDeque.empty()) {
        // Check 100 ms budget per iteration (after at least one rebuild attempt).
        // Budget is checked AFTER the first rebuild to guarantee at least 1 completes
        // per procedural-terrain.md: "processes the entire rebuild deque until empty
        // or a per-call GPU upload time budget (default: 100 ms) is exhausted".
        if (m_clock && done > 0) {
            if (m_clock->nowSeconds() - start >= 0.100) {
                break;  // Budget exhausted.
            }
        }

        ChunkRebuildRequest req = m_rebuildDeque.front();
        m_rebuildDeque.pop_front();

        if (processOneRebuild(req, processedThisFrame)) {
            ++done;
            ++m_chunksRebuiltLastFlush;
            if (cb) {
                cb->onChunkRebuilt(done, total);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// flushTerrainRebuilds() — ITerrainQuery implementation.
// Delegates to flushPendingRebuilds() so that all enqueued chunk rebuilds
// (from setTileHeight calls during placement) are processed synchronously
// before the next render frame, eliminating the chunk rebuild delay that
// would otherwise cause road/building meshes to appear sunken below the terrain.
// ---------------------------------------------------------------------------
void TerrainSystem::flushTerrainRebuilds() {
    flushPendingRebuilds();
}

// ---------------------------------------------------------------------------
// generate() — procedural map generation with playability guarantee.
//
// Playability constraints (architecture/game-design/terrain-interaction.md):
//   (1) >= 20% of total map tiles must be flat (getSlopeDegrees < 15.0 degrees).
//   (2) At least one contiguous flat region of >= 50x50 tiles must exist.
//
// Strategy:
//   - Build a flat heightmap buffer for the full (mapTilesX+1)*(mapTilesZ+1) vertex grid.
//   - Use ITerrainRNG::nextFloat() to add procedural height variation.
//   - Construct a TerrainChunk for the full map (or a sampling chunk) to evaluate
//     getSlopeDegrees() across all tiles.
//   - If constraints are not met, call rng->reseed() and retry up to maxRetries.
//
// For V1 scope, the full map is treated as one large terrain grid for the
// playability check. Production code will subdivide into chunks per camera distance.
// ---------------------------------------------------------------------------
bool TerrainSystem::generate(int mapTilesX, int mapTilesZ, float cellSize,
                              ITerrainRNG* rng, int maxRetries) {
    // Clear stale chunk nodes from any previous generate() call before building new ones.
    clearAllChunks();

    static constexpr float kFlatSlopeThreshold = 15.0f; // degrees
    static constexpr float kMinFlatPercent     = 0.20f; // 20% of tiles
    static constexpr int   kMinContiguousSize  = 50;    // 50x50 tiles

    const int vertX = mapTilesX + 1;
    const int vertZ = mapTilesZ + 1;
    const int totalVerts = vertX * vertZ;
    const int totalTiles = mapTilesX * mapTilesZ;

    auto buildHeightmap = [&](std::vector<float>& hmap) {
        hmap.resize(static_cast<size_t>(totalVerts));
        // Simple procedural: low-frequency Perlin-like sum using ITerrainRNG::nextFloat().
        // Amplitude: 20 m over the map (typical hilly terrain).
        for (int z = 0; z < vertZ; ++z) {
            for (int x = 0; x < vertX; ++x) {
                // Overlay 3 octaves of noise at decreasing amplitude.
                float h = rng->nextFloat() * 20.0f    // coarse (20 m)
                        + rng->nextFloat() *  5.0f    // medium (5 m)
                        + rng->nextFloat() *  1.0f;   // fine (1 m)
                hmap[static_cast<size_t>(z * vertX + x)] = h;
            }
        }
    };

    // Helper: count flat tiles and find the largest contiguous flat region via BFS.
    // Returns {flatCount, largestContiguousWidth, largestContiguousHeight}.
    // "Contiguous" here means the BFS finds a connected component; we check if any
    // rectangular subregion of size >= 50x50 exists inside the component.
    // For simplicity, we check if the flat-tile bounding box of the largest connected
    // component is >= 50x50 (conservative but correct for typical terrain distributions).
    auto evaluatePlayability = [&](const std::vector<float>& hmap,
                                   int& outFlatCount, int& outLargestBFSSize) {
        // Build a flat-tile mask.
        // Slope at tile (tx, tz) is computed from the 2x2 quad corner heights.
        // We approximate using TerrainChunk::getSlopeDegrees logic for the full grid.
        std::vector<bool> isFlat(static_cast<size_t>(totalTiles), false);

        for (int tz = 0; tz < mapTilesZ; ++tz) {
            for (int tx = 0; tx < mapTilesX; ++tx) {
                // Heights of the quad corners (vertex indices in the heightmap).
                float h00 = hmap[static_cast<size_t>(tz       * vertX + tx    )];
                float h10 = hmap[static_cast<size_t>(tz       * vertX + tx + 1)];
                float h01 = hmap[static_cast<size_t>((tz + 1) * vertX + tx    )];

                float dx = (h10 - h00) / cellSize;
                float dz = (h01 - h00) / cellSize;
                float slopeRad = std::atan(std::sqrt(dx * dx + dz * dz));
                static constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
                float slopeDeg = slopeRad * kRadToDeg;

                isFlat[static_cast<size_t>(tz * mapTilesX + tx)] = (slopeDeg < kFlatSlopeThreshold);
            }
        }

        // Count flat tiles.
        outFlatCount = 0;
        for (bool f : isFlat) {
            if (f) ++outFlatCount;
        }

        // BFS to find the largest connected flat region (4-connected).
        std::vector<bool> visited(static_cast<size_t>(totalTiles), false);
        outLargestBFSSize = 0;

        for (int startZ = 0; startZ < mapTilesZ; ++startZ) {
            for (int startX = 0; startX < mapTilesX; ++startX) {
                int startIdx = startZ * mapTilesX + startX;
                if (!isFlat[startIdx] || visited[startIdx]) continue;

                // BFS from this tile.
                std::queue<int> q;
                q.push(startIdx);
                visited[startIdx] = true;
                int componentSize = 0;

                // Track the bounding box of the BFS component.
                int minX = startX, maxX = startX;
                int minZ = startZ, maxZ = startZ;

                while (!q.empty()) {
                    int idx = q.front(); q.pop();
                    ++componentSize;

                    int cx = idx % mapTilesX;
                    int cz = idx / mapTilesX;
                    if (cx < minX) minX = cx;
                    if (cx > maxX) maxX = cx;
                    if (cz < minZ) minZ = cz;
                    if (cz > maxZ) maxZ = cz;

                    // 4-connected neighbours.
                    const int dx4[4] = {1, -1, 0,  0};
                    const int dz4[4] = {0,  0, 1, -1};
                    for (int d = 0; d < 4; ++d) {
                        int nx = cx + dx4[d];
                        int nz = cz + dz4[d];
                        if (nx < 0 || nx >= mapTilesX) continue;
                        if (nz < 0 || nz >= mapTilesZ) continue;
                        int nIdx = nz * mapTilesX + nx;
                        if (!isFlat[nIdx] || visited[nIdx]) continue;
                        visited[nIdx] = true;
                        q.push(nIdx);
                    }
                }

                // Use the bounding-box area as a conservative proxy for the
                // "does a 50x50 region fit" check.  A tighter check would require
                // a maximum-rectangle-in-histogram algorithm; for V1 the bounding
                // box approximation is sufficient.
                int bbW = (maxX - minX + 1);
                int bbH = (maxZ - minZ + 1);
                int bbMin = (bbW < bbH) ? bbW : bbH;
                if (bbMin > outLargestBFSSize) {
                    outLargestBFSSize = bbMin;
                }
            }
        }
    };

    bool playable = false;
    std::vector<float> heightmap;

    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        if (attempt > 0) {
            // Re-seed with a derived seed (attempt-based) and retry.
            rng->reseed(static_cast<uint64_t>(attempt) * 0x9E3779B97F4A7C15ULL);
        }

        buildHeightmap(heightmap);

        int flatCount = 0;
        int largestContiguousMinDim = 0;
        evaluatePlayability(heightmap, flatCount, largestContiguousMinDim);

        float flatPercent = static_cast<float>(flatCount) / static_cast<float>(totalTiles);
        bool constraint1 = (flatPercent >= kMinFlatPercent);
        bool constraint2 = (largestContiguousMinDim >= kMinContiguousSize);

        if (constraint1 && constraint2) {
            playable = true;
            break;
        }
    }

    // Store the generated heightmap and map dimensions for getSlopeDegrees() lookups.
    m_generatedHeightmap = std::move(heightmap);
    m_mapTilesX = mapTilesX;
    m_mapTilesZ = mapTilesZ;
    m_cellSize  = cellSize;

    return playable;
}

// ---------------------------------------------------------------------------
// buildAllChunks() — divide the generated heightmap into chunks, register, and flush.
//
// Each chunk covers kTerrainLOD0GridSize (32) tiles per side.
// The full-map heightmap has (m_mapTilesX+1) * (m_mapTilesZ+1) vertex samples.
// Each chunk needs (kTerrainLOD0GridSize+1)^2 = 33*33 = 1089 vertex samples.
//
// For partial edge chunks (map not evenly divisible by 32), the chunk's
// heightmap is zero-padded beyond the map boundary.
// ---------------------------------------------------------------------------
void TerrainSystem::buildAllChunks() {
    if (m_generatedHeightmap.empty() || m_mapTilesX <= 0 || m_mapTilesZ <= 0) {
        return;
    }

    const int chunkTiles = kTerrainLOD0GridSize;  // 32 tiles per chunk side
    const int chunkVerts = chunkTiles + 1;         // 33 vertices per chunk side
    const int mapVertX   = m_mapTilesX + 1;        // full-map vertex width

    // Number of chunks in each dimension (ceiling division for partial edge chunks).
    const int chunksX = (m_mapTilesX + chunkTiles - 1) / chunkTiles;
    const int chunksZ = (m_mapTilesZ + chunkTiles - 1) / chunkTiles;

    for (int cz = 0; cz < chunksZ; ++cz) {
        for (int cx = 0; cx < chunksX; ++cx) {
            uint64_t chunkId = static_cast<uint64_t>(cz * chunksX + cx);

            // World-space origin of this chunk's (0,0) vertex corner.
            float worldOriginX = static_cast<float>(cx * chunkTiles) * m_cellSize;
            float worldOriginZ = static_cast<float>(cz * chunkTiles) * m_cellSize;

            // Tile offset of this chunk in the full map.
            int tileOffsetX = cx * chunkTiles;
            int tileOffsetZ = cz * chunkTiles;

            // Extract the chunk's LOD0 heightmap from the full-map heightmap.
            std::vector<float> chunkHmap(static_cast<size_t>(chunkVerts * chunkVerts), 0.0f);
            for (int vz = 0; vz < chunkVerts; ++vz) {
                for (int vx = 0; vx < chunkVerts; ++vx) {
                    int mapX = tileOffsetX + vx;
                    int mapZ = tileOffsetZ + vz;
                    if (mapX < mapVertX && mapZ < (m_mapTilesZ + 1)) {
                        chunkHmap[static_cast<size_t>(vz * chunkVerts + vx)] =
                            m_generatedHeightmap[static_cast<size_t>(mapZ * mapVertX + mapX)];
                    }
                    // else: zero-padded (already 0.0f from initialization)
                }
            }

            // Register the chunk: LOD, position, and heightmap.
            registerChunkAtLOD(chunkId, -1);  // -1 = not yet built; enqueueRebuild will set LOD0
            registerChunkPosition(chunkId, worldOriginX, worldOriginZ);
            registerChunkHeightmap(chunkId, std::move(chunkHmap));

            // Enqueue a LOD0 rebuild.
            enqueueRebuild(chunkId, 0, 0.0f);
        }
    }

    // Synchronously process all pending rebuilds (creates scene nodes via IRenderer).
    flushPendingRebuilds();
}

// ---------------------------------------------------------------------------
// enqueueAllChunks() — register and enqueue all LOD0 rebuilds without flushing.
// Identical to buildAllChunks() but omits the trailing flushPendingRebuilds() call.
// Used by the Phase 11 loading-screen loop: caller drives flushPendingRebuilds()
// once per frame until pendingRebuildCount() reaches 0.
// ---------------------------------------------------------------------------
void TerrainSystem::enqueueAllChunks() {
    if (m_generatedHeightmap.empty() || m_mapTilesX <= 0 || m_mapTilesZ <= 0) {
        return;
    }

    // Clear stale chunk nodes from any previous enqueueAllChunks() call.
    clearAllChunks();

    const int chunkTiles = kTerrainLOD0GridSize;
    const int chunkVerts = chunkTiles + 1;
    const int mapVertX   = m_mapTilesX + 1;

    const int chunksX = (m_mapTilesX + chunkTiles - 1) / chunkTiles;
    const int chunksZ = (m_mapTilesZ + chunkTiles - 1) / chunkTiles;

    for (int cz = 0; cz < chunksZ; ++cz) {
        for (int cx = 0; cx < chunksX; ++cx) {
            uint64_t chunkId = static_cast<uint64_t>(cz * chunksX + cx);

            float worldOriginX = static_cast<float>(cx * chunkTiles) * m_cellSize;
            float worldOriginZ = static_cast<float>(cz * chunkTiles) * m_cellSize;

            int tileOffsetX = cx * chunkTiles;
            int tileOffsetZ = cz * chunkTiles;

            std::vector<float> chunkHmap(static_cast<size_t>(chunkVerts * chunkVerts), 0.0f);
            for (int vz = 0; vz < chunkVerts; ++vz) {
                for (int vx = 0; vx < chunkVerts; ++vx) {
                    int mapX = tileOffsetX + vx;
                    int mapZ = tileOffsetZ + vz;
                    if (mapX < mapVertX && mapZ < (m_mapTilesZ + 1)) {
                        chunkHmap[static_cast<size_t>(vz * chunkVerts + vx)] =
                            m_generatedHeightmap[static_cast<size_t>(mapZ * mapVertX + mapX)];
                    }
                }
            }

            registerChunkAtLOD(chunkId, -1);
            registerChunkPosition(chunkId, worldOriginX, worldOriginZ);
            registerChunkHeightmap(chunkId, std::move(chunkHmap));
            enqueueRebuild(chunkId, 0, 0.0f);
        }
    }
    // Caller is responsible for calling flushPendingRebuilds() per-frame.
}

// ---------------------------------------------------------------------------
// getGeneratedHeightmap() — accessor for the heightmap produced by generate().
// ---------------------------------------------------------------------------
const std::vector<float>& TerrainSystem::getGeneratedHeightmap() const {
    return m_generatedHeightmap;
}

// ---------------------------------------------------------------------------
// getSlopeDegrees() — ITerrainQuery implementation.
// Returns 0.0f (flat) for out-of-bounds tiles or before generate() is called.
// Uses the same gradient formula as generate()'s playability check.
// ---------------------------------------------------------------------------
float TerrainSystem::getSlopeDegrees(int tileX, int tileZ) const {
    if (m_generatedHeightmap.empty() ||
        tileX < 0 || tileX >= m_mapTilesX ||
        tileZ < 0 || tileZ >= m_mapTilesZ) {
        return 0.0f;
    }

    const int vertX = m_mapTilesX + 1;
    const float h00 = m_generatedHeightmap[static_cast<size_t>( tileZ      * vertX + tileX    )];
    const float h10 = m_generatedHeightmap[static_cast<size_t>( tileZ      * vertX + tileX + 1)];
    const float h01 = m_generatedHeightmap[static_cast<size_t>((tileZ + 1) * vertX + tileX    )];

    const float dx = (h10 - h00) / m_cellSize;
    const float dz = (h01 - h00) / m_cellSize;
    const float slopeRad = std::atan(std::sqrt(dx * dx + dz * dz));
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    return slopeRad * kRadToDeg;
}

// ---------------------------------------------------------------------------
// setTileHeight() — ITerrainQuery write-side implementation (Phase 10b).
//
// Step 1: write height into the persistent LOD0 heightmap at (tileX, tileZ).
// Step 2: apply neighbour blending to all 8 surrounding tiles (cardinal 0.5, diagonal 0.25).
//         lerp(a, b, t) = a + t * (b - a)
// Step 3: enqueue ChunkRebuildRequest for every chunk containing a modified tile.
//         Dedup is handled by processedThisFrame in update() — duplicates are harmless.
//
// kCardinalFalloff = 0.5  (gamedesign-lookandfeel sign-off 2026-03-13)
// kDiagonalFalloff = 0.25 (same sign-off)
//
// (ref: architecture/graphics-architecture/procedural-terrain.md — setTileHeight Write Path)
// ---------------------------------------------------------------------------
void TerrainSystem::setTileHeight(int tileX, int tileZ, float height)
{
    // Out-of-bounds centre tile: silently ignore.
    if (m_generatedHeightmap.empty() ||
        tileX < 0 || tileX >= m_mapTilesX ||
        tileZ < 0 || tileZ >= m_mapTilesZ) {
        return;
    }

    static constexpr float kCardinalFalloff = 0.5f;
    static constexpr float kDiagonalFalloff = 0.25f;

    const int vertX = m_mapTilesX + 1;

    // Chunk layout constants — needed by writeHeight to sync m_chunkHeightmaps.
    const int chunkTiles = kTerrainLOD0GridSize;  // 32 tiles per chunk side
    const int chunkVerts = chunkTiles + 1;         // 33 vertices per chunk side
    const int chunksX    = (m_mapTilesX + chunkTiles - 1) / chunkTiles;

    // Helper: write to global heightmap AND sync the corresponding per-chunk
    // heightmap(s) in m_chunkHeightmaps. processOneRebuild reads from
    // m_chunkHeightmaps, so without this sync the terrain geometry never updates.
    //
    // A vertex at (tx, tz) is shared between up to 4 chunks when it sits on
    // a chunk boundary (tx % chunkTiles == 0 or tz % chunkTiles == 0). All
    // owning chunks are updated so every rebuild sees the fresh height.
    auto writeHeight = [&](int tx, int tz, float h) {
        // Clamp to bounds.
        if (tx < 0 || tx >= m_mapTilesX || tz < 0 || tz >= m_mapTilesZ) return;
        // Update the global heightmap.
        m_generatedHeightmap[static_cast<size_t>(tz * vertX + tx)] = h;
        // Sync every chunk heightmap that contains vertex (tx, tz).
        int cx = tx / chunkTiles;
        int cz = tz / chunkTiles;
        int lx = tx % chunkTiles;
        int lz = tz % chunkTiles;
        auto syncChunk = [&](int ccx, int ccz, int llx, int llz) {
            if (ccx < 0 || ccz < 0) return;
            uint64_t cid = static_cast<uint64_t>(ccz * chunksX + ccx);
            auto it = m_chunkHeightmaps.find(cid);
            if (it == m_chunkHeightmaps.end()) return;
            if (llx < 0 || llx >= chunkVerts || llz < 0 || llz >= chunkVerts) return;
            it->second[static_cast<size_t>(llz * chunkVerts + llx)] = h;
        };
        syncChunk(cx,     cz,     lx,         lz);
        if (lx == 0 && cx > 0) syncChunk(cx - 1, cz,     chunkTiles, lz);
        if (lz == 0 && cz > 0) syncChunk(cx,     cz - 1, lx,         chunkTiles);
        if (lx == 0 && lz == 0 && cx > 0 && cz > 0)
            syncChunk(cx - 1, cz - 1, chunkTiles, chunkTiles);
    };

    // Step 1: write centre tile height.
    writeHeight(tileX, tileZ, height);

    // Step 2: apply neighbour blending.
    // Neighbour offsets: {dx, dz, falloff}
    // Cardinal: N(0,-1), S(0,+1), E(+1,0), W(-1,0)
    // Diagonal: NE(+1,-1), NW(-1,-1), SE(+1,+1), SW(-1,+1)
    struct NeighbourDef { int dx; int dz; float falloff; };
    static constexpr NeighbourDef kNeighbours[8] = {
        {  0, -1, kCardinalFalloff },  // N
        {  0, +1, kCardinalFalloff },  // S
        { +1,  0, kCardinalFalloff },  // E
        { -1,  0, kCardinalFalloff },  // W
        { +1, -1, kDiagonalFalloff },  // NE
        { -1, -1, kDiagonalFalloff },  // NW
        { +1, +1, kDiagonalFalloff },  // SE
        { -1, +1, kDiagonalFalloff },  // SW
    };

    for (const auto& n : kNeighbours) {
        int nx = tileX + n.dx;
        int nz = tileZ + n.dz;
        // Skip out-of-bounds neighbours.
        if (nx < 0 || nx >= m_mapTilesX || nz < 0 || nz >= m_mapTilesZ) continue;

        float currentH = m_generatedHeightmap[static_cast<size_t>(nz * vertX + nx)];
        // lerp(currentH, height, falloff) = currentH + falloff * (height - currentH)
        float newH = currentH + n.falloff * (height - currentH);
        writeHeight(nx, nz, newH);
    }

    // Step 3: enqueue ChunkRebuildRequest for every chunk that shares a vertex with
    // any modified tile.  Each modified tile has up to 4 adjacent chunks sharing its
    // boundary vertex (affectedChunkIds handles clamping + deduplication).
    //
    // Using affectedChunkIds() instead of a single chunkX/chunkZ division fixes
    // the stitching-hole bug: a tile exactly on a chunk boundary (tx % chunkTiles == 0
    // or tz % chunkTiles == 0) was previously only enqueuing its "owner" chunk, leaving
    // the adjacent chunk with a stale mesh and a visible seam.
    //
    // Collect the superset of chunk IDs across all modified tiles, then deduplicate
    // before enqueuing so each chunk only gets one rebuild request per setTileHeight call.

    // Collect affected tile coords (centre + in-bounds neighbours).
    struct TileCoord { int tx; int tz; };
    TileCoord modifiedTiles[9];
    int modifiedCount = 0;

    modifiedTiles[modifiedCount++] = { tileX, tileZ };
    for (const auto& n : kNeighbours) {
        int nx = tileX + n.dx;
        int nz = tileZ + n.dz;
        if (nx >= 0 && nx < m_mapTilesX && nz >= 0 && nz < m_mapTilesZ) {
            modifiedTiles[modifiedCount++] = { nx, nz };
        }
    }

    // Gather all chunk IDs affected by any of the modified tiles, deduplicated.
    std::vector<uint64_t> chunksToRebuild;
    chunksToRebuild.reserve(16); // upper bound: 9 tiles * 4 candidates each
    for (int i = 0; i < modifiedCount; ++i) {
        for (uint64_t cid : affectedChunkIds(modifiedTiles[i].tx, modifiedTiles[i].tz)) {
            bool already = false;
            for (uint64_t existing : chunksToRebuild) {
                if (existing == cid) { already = true; break; }
            }
            if (!already) chunksToRebuild.push_back(cid);
        }
    }

    for (uint64_t chunkId : chunksToRebuild) {
        // Mark the chunk dirty (LOD = -1) so processOneRebuild's
        // "already at LOD" guard does not discard this height-change rebuild.
        // Without this, a LOD0 chunk receiving a LOD0 rebuild request is skipped.
        auto activeIt = m_activeChunks.find(chunkId);
        if (activeIt != m_activeChunks.end()) {
            activeIt->second = -1;
        }
        enqueueRebuild(chunkId, 0, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// getHeightAt() — ITerrainQuery implementation (Phase 9b Deliverable E).
// Returns Y-axis terrain height in world-space metres for the tile centre at
// grid position (tileX, tileZ). Returns 0.0f for out-of-bounds coordinates or
// before generate() is called. Always queries the persistent LOD0 heightmap
// array (m_generatedHeightmap) — never scene-node geometry.
//
// Index formula: the heightmap is (m_mapTilesX+1) * (m_mapTilesZ+1) vertex samples
// in row-major order (z * vertX + x). The tile centre height is approximated by
// the top-left vertex of the tile cell (index z * vertX + x), which is the same
// sample used by getSlopeDegrees() and by buildAllChunks() for geometry generation.
// Sub-tile interpolation is not performed — callers requiring bilinear-interpolated
// heights must implement interpolation on top of multiple getHeightAt() calls.
// (ref: architecture/graphics-architecture/procedural-terrain.md — Heightmap Query API)
// ---------------------------------------------------------------------------
float TerrainSystem::getHeightAt(int tileX, int tileZ) const {
    if (m_generatedHeightmap.empty() ||
        tileX < 0 || tileX >= m_mapTilesX ||
        tileZ < 0 || tileZ >= m_mapTilesZ) {
        return 0.0f;
    }
    const int vertX = m_mapTilesX + 1;
    return m_generatedHeightmap[static_cast<size_t>(tileZ * vertX + tileX)];
}

// ---------------------------------------------------------------------------
// getHeightAtWorld() — bilinear interpolation at arbitrary world coords.
// Uses the persistent LOD0 heightmap (same source as getHeightAt()).
// (ref: implementation/phase-11q.md Fix 2b)
// ---------------------------------------------------------------------------
float TerrainSystem::getHeightAtWorld(float worldX, float worldZ) const {
    using namespace RenderConstants;
    const float tx = worldX / kTileSize;
    const float tz = worldZ / kTileSize;

    const int x0 = static_cast<int>(std::floor(tx));
    const int z0 = static_cast<int>(std::floor(tz));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float fx = tx - static_cast<float>(x0);
    const float fz = tz - static_cast<float>(z0);

    const float h00 = getHeightAt(x0, z0);
    const float h10 = getHeightAt(x1, z0);
    const float h01 = getHeightAt(x0, z1);
    const float h11 = getHeightAt(x1, z1);

    const float h0 = h00 + fx * (h10 - h00);
    const float h1 = h01 + fx * (h11 - h01);
    return h0 + fz * (h1 - h0);
}

// ---------------------------------------------------------------------------
// getMapTilesX() / getMapTilesZ() / getCellSize() — dimension accessors.
// Phase 9b Deliverable E.1: used by main.cpp to call
//   uiManager.setMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ())
//   renderer.setCellSize(terrainSystem.getCellSize())
// after terrain generation. NOT part of ITerrainQuery — minimal interface design.
// ---------------------------------------------------------------------------
int   TerrainSystem::getMapTilesX() const { return m_mapTilesX; }
int   TerrainSystem::getMapTilesZ() const { return m_mapTilesZ; }
float TerrainSystem::getCellSize()  const { return m_cellSize; }

// ---------------------------------------------------------------------------
// getPendingRebuildIds() — test API.
// Returns a sorted, deduplicated vector of chunk IDs currently in m_rebuildDeque.
// Preserves no particular ordering beyond the sort — callers must not rely on
// insertion order. Sorting is used for deduplication only.
// ---------------------------------------------------------------------------
std::vector<uint64_t> TerrainSystem::getPendingRebuildIds() const {
    std::vector<uint64_t> ids;
    ids.reserve(m_rebuildDeque.size());
    for (const auto& req : m_rebuildDeque) {
        ids.push_back(req.chunkId);
    }
    // Deduplicate (sort, then erase consecutive duplicates).
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// ---------------------------------------------------------------------------
// affectedChunkIds() — private helper.
//
// A vertex at map position (tileX, tileZ) is the shared corner between up to
// four chunks:
//   - The chunk that "owns" tile (tileX,   tileZ)
//   - The chunk that "owns" tile (tileX-1, tileZ)   [to the west]
//   - The chunk that "owns" tile (tileX,   tileZ-1) [to the north]
//   - The chunk that "owns" tile (tileX-1, tileZ-1) [to the northwest]
//
// Each candidate tile coordinate is clamped to [0, mapTiles-1] before the
// chunk conversion, so map-edge and interior cases naturally collapse.
// Duplicate chunk IDs (when the clamped positions land in the same chunk) are
// removed before returning.
// ---------------------------------------------------------------------------
std::vector<uint64_t> TerrainSystem::affectedChunkIds(int tileX, int tileZ) const {
    if (m_mapTilesX <= 0 || m_mapTilesZ <= 0) return {};

    const int chunkTiles = kTerrainLOD0GridSize;  // 32 tiles per chunk side
    const int chunksX    = (m_mapTilesX + chunkTiles - 1) / chunkTiles;

    // Clamp a tile coordinate to valid map bounds, then return its chunk ID.
    auto tileToChunkId = [&](int tx, int tz) -> uint64_t {
        if (tx < 0) tx = 0;
        if (tz < 0) tz = 0;
        if (tx >= m_mapTilesX) tx = m_mapTilesX - 1;
        if (tz >= m_mapTilesZ) tz = m_mapTilesZ - 1;
        int cx = tx / chunkTiles;
        int cz = tz / chunkTiles;
        return static_cast<uint64_t>(cz * chunksX + cx);
    };

    // Four candidate positions that share vertex (tileX, tileZ).
    uint64_t ids[4] = {
        tileToChunkId(tileX,     tileZ),
        tileToChunkId(tileX - 1, tileZ),
        tileToChunkId(tileX,     tileZ - 1),
        tileToChunkId(tileX - 1, tileZ - 1),
    };

    // Deduplicate while preserving order of first occurrence.
    std::vector<uint64_t> result;
    result.reserve(4);
    for (uint64_t id : ids) {
        bool found = false;
        for (uint64_t existing : result) {
            if (existing == id) { found = true; break; }
        }
        if (!found) result.push_back(id);
    }
    return result;
}
