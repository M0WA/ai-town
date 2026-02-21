#pragma once
#include "src/interfaces/IClock.h"

// ManualClock — deterministic test double for IClock.
// Allows controlled time advancement in tests without wall-clock dependencies.
// Used by both simulation tests and audio tests.
// Source location: tests/simulation/ (shared across multiple CMake test targets).
class ManualClock : public IClock {
public:
    double nowSeconds() const override { return m_time; }
    void advance(double seconds) { m_time += seconds; }
private:
    double m_time{0.0};
};
