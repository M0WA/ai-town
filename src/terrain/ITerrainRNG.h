#pragma once
#include <cstdint>

// Injectable RNG interface for deterministic terrain generation testing.
// Source location: src/terrain/ (not src/interfaces/) — ITerrainRNG is tightly
// coupled to the terrain subsystem and not shared with other subsystems.
// MockTerrainRNG lives in tests/terrain/mock_terrain_rng.h.
class ITerrainRNG {
public:
    virtual ~ITerrainRNG() = default;
    virtual float    nextFloat() = 0;              // [0.0, 1.0) — continuous noise and feature probability
    virtual int      nextInt(int min, int max) = 0; // inclusive [min, max] — discrete terrain feature counts, tile selection
    virtual void     reseed(uint64_t seed) = 0;    // called when generator retries with a new seed
};
