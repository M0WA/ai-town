#pragma once

// TerrainSystem.h — Phase 5 full implementation.
//
// TerrainSystem manages terrain chunk lifecycle: generation, LOD rebuild deque,
// camera-distance-based load/unload, and flushPendingRebuilds().
//
// Constructor: TerrainSystem(IRenderer* renderer, IClock* clock)
//   IClock* is injected for the flushPendingRebuilds() 100 ms wall-clock budget.
//   Production: inject WallClock; Tests: inject ManualClock.
//
// LOD rebuild strategy for terrain (FULL NODE REBUILD — never setMesh):
//   Terrain chunks require a full node rebuild on LOD transition because vertex
//   counts differ between levels (LOD0=1089, LOD1=289, LOD2=81 vertices).
//   SceneEntityManager::destroy() is called on the old node BEFORE creating the new one.
//
// LOD hysteresis distances (from architecture/asset-standards/3d-model-standards.md):
//   LOD0→LOD1: switch-out >100 m, switch-in <92 m  (8 m hysteresis band)
//   LOD1→LOD2: switch-out >300 m, switch-in <285 m (15 m hysteresis band)
//
// Per-frame deduplication:
//   std::unordered_set<uint64_t> processedThisFrame in update();
//   Skip if chunk ID already processed this frame.
//   Skip if currentLOD == req.targetLOD (already at requested level).
//
// flushPendingRebuilds():
//   Processes entire rebuild deque until empty or 100 ms budget exhausted.
//   Bypasses the 2-per-frame cap. Budget measured via IClock::nowSeconds().
//   Called once at map load time; Phase 11 wires loading screen spinner to callback.
//
// ITerrainLoadProgress callback (stub interface):
//   declared in terrain_types.h; flushPendingRebuilds(ITerrainLoadProgress* cb = nullptr)
//   calls cb->onChunkRebuilt() if non-null.
//
// See architecture/graphics-architecture/procedural-terrain.md
// See architecture/graphics-architecture/scene-graph-ownership.md

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

#include "TerrainChunk.h"
#include "terrain_types.h"

// IClock — include full definition so test doubles (BudgetExhaustionClock) can subclass it.
#include "../interfaces/IClock.h"

// Forward declaration for IRenderer (used as pointer only in TerrainSystem).
class IRenderer;

// ChunkRebuildRequest — queued rebuild request for a terrain chunk.
// Stores uint64_t chunkId (NOT raw node pointer) per procedural-terrain.md.
// Before processing, validate chunk is still live in m_activeChunks by ID.
struct ChunkRebuildRequest {
    uint64_t chunkId{0};
    int      targetLOD{0};
    float    distanceToCamera{0.0f}; // used for priority sorting (nearest-first)
};

// TerrainSystem — manages all terrain chunks, LOD rebuild deque, and load/unload.
class TerrainSystem {
public:
    // Constructor: IRenderer* and IClock* are injected.
    // renderer: used for scene graph operations (addMeshSceneNode, destroy).
    //           May be null in EDT_NULL test context (no GL calls made).
    // clock:    used for flushPendingRebuilds() 100 ms wall-clock budget.
    //           Use WallClock in production, ManualClock in tests.
    TerrainSystem(IRenderer* renderer, IClock* clock);

    ~TerrainSystem() = default;

    // Non-copyable / non-movable — owns chunk map and rebuild deque.
    TerrainSystem(const TerrainSystem&)            = delete;
    TerrainSystem& operator=(const TerrainSystem&) = delete;
    TerrainSystem(TerrainSystem&&)                 = delete;
    TerrainSystem& operator=(TerrainSystem&&)      = delete;

    // update() — process at most 2 rebuild requests per call to amortize GPU upload cost.
    // Implements per-frame deduplication (processedThisFrame set).
    // Called each game loop frame.
    // dt: frame delta time in seconds (not used for LOD timing — LOD distances are spatial).
    void update(float dt);

    // flushPendingRebuilds() — synchronously process rebuild deque until empty or 100 ms budget.
    // Bypasses the 2-per-frame cap. Called once at map load time.
    // cb: optional progress callback; called after each processed rebuild.
    //     Pass nullptr if no progress reporting is needed (e.g., EDT_NULL test context).
    void flushPendingRebuilds(ITerrainLoadProgress* cb = nullptr);

    // enqueueRebuild() — enqueue a LOD rebuild request for a chunk.
    // Uses a default distanceToCamera of 0.0 (nearest-first by default).
    // Callers can use the overload with distanceToCamera for priority sorting.
    void enqueueRebuild(uint64_t chunkId, int targetLOD, float distanceToCamera = 0.0f);

    // Accessors for testing.
    int  pendingRebuildCount() const { return static_cast<int>(m_rebuildDeque.size()); }
    bool hasActiveChunk(uint64_t chunkId) const { return m_activeChunks.count(chunkId) > 0; }

    // chunksRebuiltLastFrame() — number of chunk rebuilds processed in the last update() call.
    // Used by tests to verify at-most-2-per-frame and deduplication behaviour.
    int chunksRebuiltLastFrame() const { return m_chunksRebuiltLastFrame; }

    // chunksRebuiltLastFlush() — number of chunk rebuilds processed in the last flushPendingRebuilds() call.
    // Used by tests to verify budget-exhaustion behaviour.
    int chunksRebuiltLastFlush() const { return m_chunksRebuiltLastFlush; }

    // registerChunkAtLOD() — register a chunk with its current LOD level.
    // Called after a chunk is built and its scene node is created.
    // Named registerChunkAtLOD (not registerChunk) per test API expectations.
    void registerChunkAtLOD(uint64_t chunkId, int currentLOD);

    // unregisterChunk() — remove a chunk from the active map.
    // Called when a chunk is unloaded (out of camera range).
    void unregisterChunk(uint64_t chunkId);

    // getChunkLOD() — return the current LOD of a registered chunk, or -1 if not found.
    int getChunkLOD(uint64_t chunkId) const;

    // LOD hysteresis distance accessors (per architecture/asset-standards/3d-model-standards.md).
    // lodSwitchOutDistance(fromLOD): distance at which to switch AWAY from fromLOD.
    // lodSwitchInDistance(fromLOD):  distance at which to switch BACK to fromLOD.
    //
    // fromLOD 0 → LOD0→LOD1 transition: switch-out >100 m, switch-in <92 m
    // fromLOD 1 → LOD1→LOD2 transition: switch-out >300 m, switch-in <285 m
    static float lodSwitchOutDistance(int fromLOD);
    static float lodSwitchInDistance(int fromLOD);

private:
    // Process a single rebuild request (factored out for reuse between update() and flush()).
    // Returns true if the request was processed, false if skipped (dedup, already at LOD).
    bool processOneRebuild(const ChunkRebuildRequest& req,
                           std::unordered_set<uint64_t>& processedThisFrame);

    IRenderer* m_renderer;  // may be null in EDT_NULL test context
    IClock*    m_clock;     // injected for deterministic timing in tests

    // Rebuild deque — nearest-first priority, distance-weighted.
    // ChunkRebuildRequest stores uint64_t chunkId (NOT raw node pointer).
    std::deque<ChunkRebuildRequest> m_rebuildDeque;

    // Active chunks map: chunkId → current LOD level.
    // Used to validate deque entries (discards requests for unloaded chunks).
    // Also used to skip redundant rebuilds (currentLOD == targetLOD).
    std::unordered_map<uint64_t, int> m_activeChunks;

    // Counters for test assertions.
    int m_chunksRebuiltLastFrame{0};
    int m_chunksRebuiltLastFlush{0};
};
