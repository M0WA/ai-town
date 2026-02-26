#pragma once
#include "ITerrainQuery.h"
#include <unordered_map>
#include <cstdint>

// ManualTerrainQuery — test-local implementation of ITerrainQuery.
//
// All tiles default to 0.0° slope (flat; no earthworks cost).
// Individual tile slopes can be set via setSlope(x, z, degrees).
//
// Used by SimulationTestBase and direct test fixtures that need slope-sensitive
// earthworks cost or buildability checks.
//
// This header is test-local (tests/simulation/); production code must never depend on it.

class ManualTerrainQuery : public ITerrainQuery {
public:
    ManualTerrainQuery() = default;

    // Set slope for a specific tile (degrees, [0, 90]).
    void setSlope(int tileX, int tileZ, float degrees) {
        m_slopes[makeKey(tileX, tileZ)] = degrees;
    }

    // ITerrainQuery implementation
    float getSlopeDegrees(int tileX, int tileZ) const override {
        auto it = m_slopes.find(makeKey(tileX, tileZ));
        return (it != m_slopes.end()) ? it->second : 0.0f;
    }

private:
    static int64_t makeKey(int x, int z) {
        return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
    }

    std::unordered_map<int64_t, float> m_slopes;
};
