#pragma once
// Phase 3 stub. Phase 5 implements the full TerrainGenerator.
// Both constructor signatures are declared here to lock the interface.
class ITerrainRNG;  // forward declaration

class TerrainGenerator {
public:
    // Constructor 1: production path (seeded RNG)
    explicit TerrainGenerator(unsigned int seed) {}
    // Constructor 2: test path (injected RNG)
    explicit TerrainGenerator(ITerrainRNG* rng) {}
};
