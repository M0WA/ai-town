#pragma once

// terrain_types.h — shared terrain type declarations.
//
// ITerrainLoadProgress — stub interface for loading screen progress reporting.
// Phase 5: no-op stub. Phase 11 wires the loading screen spinner to this callback.
// See architecture/graphics-architecture/procedural-terrain.md — ITerrainLoadProgress.

struct ITerrainLoadProgress {
    // Called after each chunk rebuild during flushPendingRebuilds().
    // done:  number of chunks rebuilt so far.
    // total: total number of chunks to rebuild (size of deque at flush start).
    virtual void onChunkRebuilt(int done, int total) {}
    virtual ~ITerrainLoadProgress() = default;
};
