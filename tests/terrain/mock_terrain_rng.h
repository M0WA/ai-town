#pragma once
#include "src/terrain/ITerrainRNG.h"
#include <random>
#include <cstdint>

// Manual stub — NOT a GMock mock. Uses mt19937_64 for deterministic test sequences.
// Do NOT replace with MOCK_METHOD.
// reseedCount() tracks the number of times reseed() has been called —
// used by TerrainGenerator_AlwaysTerminates_WithinReSeedLimit to verify <= 100 reseeds.
class MockTerrainRNG : public ITerrainRNG {
public:
    explicit MockTerrainRNG(uint64_t seed = 42) : m_rng(seed), m_reseedCount(0) {}

    float nextFloat() override {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
    }

    int nextInt(int min, int max) override {
        return std::uniform_int_distribution<int>(min, max)(m_rng);
    }

    void reseed(uint64_t seed) override {
        m_rng.seed(seed);
        ++m_reseedCount;
    }

    int reseedCount() const { return m_reseedCount; }

private:
    std::mt19937_64 m_rng;
    int m_reseedCount;
};
