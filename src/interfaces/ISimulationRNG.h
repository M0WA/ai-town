#pragma once

// Injectable RNG interface for deterministic simulation testing.
// Production code passes a std::mt19937-backed implementation.
// Tests inject ManualRNG (tests/simulation/manual_rng.h) for preset sequences.
class ISimulationRNG {
public:
    virtual ~ISimulationRNG() = default;
    virtual int   nextInt(int min, int max) = 0;  // inclusive [min, max]
    virtual float nextFloat() = 0;                // [0.0, 1.0)
};
