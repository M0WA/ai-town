#pragma once
#include "IClock.h"

// Production IClock implementation using std::chrono::steady_clock.
// The body is in src/platform/WallClock.cpp (added in Phase 1).
class WallClock : public IClock {
public:
    double nowSeconds() const override;
};
