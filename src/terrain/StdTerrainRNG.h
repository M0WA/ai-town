#pragma once
#include "ITerrainRNG.h"
#include <random>
#include <cstdint>

// StdTerrainRNG — production ITerrainRNG backed by std::mt19937.
//
// Seeded with std::random_device at construction; reseed() allows
// deterministic re-seeding for terrain generation retries.
// Thread-safety: not thread-safe; terrain generation is single-threaded
// (called only from the main thread).
//
// Source: src/terrain/StdTerrainRNG.h

class StdTerrainRNG : public ITerrainRNG {
public:
    StdTerrainRNG() : m_engine(std::random_device{}()) {}

    float nextFloat() override {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(m_engine);
    }

    int nextInt(int min, int max) override {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(m_engine);
    }

    void reseed(uint64_t seed) override {
        m_engine.seed(static_cast<std::mt19937::result_type>(seed));
    }

private:
    std::mt19937 m_engine;
};
