#pragma once
#include "src/terrain/ITerrainRNG.h"
#include <random>
#include <cstdint>

// MockTerrainRNG — handwritten test double for ITerrainRNG.
// Unlike the four tests/simulation/ shared mocks which use GMock,
// MockTerrainRNG uses a real RNG engine and tracks reseed count.
// Used by TerrainGenerator_AlwaysTerminates_WithinReSeedLimit property test
// to verify the reseed count stays <= 100.
// Source location: tests/terrain/ (not tests/simulation/ — terrain-specific).
// Header-only — no .cpp file.
class MockTerrainRNG : public ITerrainRNG {
public:
    explicit MockTerrainRNG(uint64_t seed) : m_rng(seed) {}

    float nextFloat() override {
        return std::uniform_real_distribution<float>(0.f, 1.f)(m_rng);
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
    int m_reseedCount{0};
};
