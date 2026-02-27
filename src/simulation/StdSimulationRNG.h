#pragma once
#include "ISimulationRNG.h"
#include <random>
#include <cstdint>

// StdSimulationRNG — production ISimulationRNG backed by std::mt19937.
//
// Seeded with std::random_device at construction. Thread-safety: not thread-safe;
// CitySimulation is single-threaded (ticked only from the main thread).
//
// Source: src/simulation/StdSimulationRNG.h

class StdSimulationRNG : public ISimulationRNG {
public:
    StdSimulationRNG() : m_engine(std::random_device{}()) {}

    // Returns uniform integer in [min, max] (inclusive on both ends).
    int nextInt(int min, int max) override {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(m_engine);
    }

    // Returns uniform float in [0.0, 1.0).
    float nextFloat() override {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(m_engine);
    }

private:
    std::mt19937 m_engine;
};
