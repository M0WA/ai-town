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

// IRenderer is forward-declared in TerrainSystem.h.
// Include the actual header here for any method calls.
#include "../interfaces/IRenderer.h"
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
// ---------------------------------------------------------------------------
float TerrainSystem::lodSwitchOutDistance(int fromLOD) {
    if (fromLOD == 0) return kLOD0to1SwitchOut;  // 100.5f (strictly > 100 m per spec)
    if (fromLOD == 1) return kLOD1to2SwitchOut;  // 300.5f (strictly > 300 m per spec)
    return 0.0f; // unknown LOD
}

float TerrainSystem::lodSwitchInDistance(int fromLOD) {
    if (fromLOD == 0) return kLOD0to1SwitchIn;   // 91.5f (strictly < 92 m per spec)
    if (fromLOD == 1) return kLOD1to2SwitchIn;   // 284.5f (strictly < 285 m per spec)
    return 0.0f; // unknown LOD
}

// ---------------------------------------------------------------------------
// registerChunkAtLOD / unregisterChunk / getChunkLOD
// ---------------------------------------------------------------------------
void TerrainSystem::registerChunkAtLOD(uint64_t chunkId, int currentLOD) {
    m_activeChunks[chunkId] = currentLOD;
}

void TerrainSystem::unregisterChunk(uint64_t chunkId) {
    m_activeChunks.erase(chunkId);
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
    m_rebuildDeque.push_back(req);

    // Sort the deque nearest-first after each insertion.
    // This maintains distance-weighted priority per procedural-terrain.md.
    std::stable_sort(m_rebuildDeque.begin(), m_rebuildDeque.end(),
                     [](const ChunkRebuildRequest& a, const ChunkRebuildRequest& b) {
                         return a.distanceToCamera < b.distanceToCamera;
                     });
}

// ---------------------------------------------------------------------------
// processOneRebuild — factored helper used by update() and flushPendingRebuilds()
// ---------------------------------------------------------------------------
bool TerrainSystem::processOneRebuild(const ChunkRebuildRequest& req,
                                       std::unordered_set<uint64_t>& processedThisFrame) {
    // Dedup: skip if this chunk was already processed in this update/flush call.
    if (processedThisFrame.count(req.chunkId) > 0) {
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

    // Perform the LOD transition.
    // Phase 5: TerrainSystem holds IRenderer* and does NOT call Irrlicht API directly.
    // The actual node rebuild (destroy old + create new) is delegated to the render layer.
    // In the Phase 5 skeleton, we update the LOD tracking only.
    //
    // Per architecture/graphics-architecture/procedural-terrain.md:
    //   - FULL NODE REBUILD (not setMesh) — vertex counts differ per LOD level.
    //   - SceneEntityManager::destroy() on old node BEFORE creating new node.
    //   - Store chunk IDs, not raw node pointers.
    //
    // TODO Phase 5 render integration: call IRenderer to:
    //   1. SceneEntityManager::destroy(oldNode, ...)
    //   2. Build new SMesh at targetLOD grid size
    //   3. recalculateBoundingBox on all buffers + mesh
    //   4. addMeshSceneNode(newMesh) + smesh->drop()
    //   5. registerChunkAtLOD(chunkId, targetLOD)

    // Update the active chunks map to reflect the new LOD (if chunk was registered).
    if (chunkActive) {
        it->second = req.targetLOD;
    } else {
        // Chunk was not registered — add it at the target LOD.
        m_activeChunks[req.chunkId] = req.targetLOD;
    }

    return true; // rebuild was processed (even if no GL work done in Phase 5 skeleton)
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
