#pragma once
#include "ITerrainQuery.h"
#include <unordered_map>
#include <cstdint>

// ManualTerrainQuery — test-local implementation of ITerrainQuery.
//
// All tiles default to 0.0° slope (flat; no earthworks cost) and 0.0f height.
//
// TWO slope configuration APIs (both available; use whichever is appropriate):
//
//   (a) Global slope:  setSlope(float degrees)
//       Sets a uniform slope returned for ALL tiles (including tiles not explicitly
//       configured via the per-tile API). Used by WorldInteractionTest when the test
//       needs a single slope value for all tiles, e.g.:
//         terrain_.setSlope(20.0f);  // all tiles slope 20°
//
//   (b) Per-tile slope: setSlope(int tileX, int tileZ, float degrees)
//       Overrides the slope for a specific tile. If a per-tile entry exists, it takes
//       precedence over the global slope. Used by CitySimulation earth-works tests that
//       need distinct slopes per tile.
//
// getHeightAt() always returns 0.0f — Phase 9b unit tests that need specific heights
// inject MockRenderer for the renderer path; no Phase 9b test requires ManualTerrainQuery
// to return non-zero heights. This override is required because getHeightAt() is pure
// virtual on ITerrainQuery; without it ManualTerrainQuery fails to compile, blocking
// all 17 Phase 9b unit tests. (ref: implementation/phase-9b.md Deliverable E)
//
// Lookup priority for getSlopeDegrees(tileX, tileZ):
//   1. Per-tile entry (set via setSlope(x, z, degrees)) — takes precedence.
//   2. Global slope (set via setSlope(float degrees)) — fallback for all other tiles.
//   3. Default 0.0f — when neither per-tile entry nor global slope has been set.
//
// Used by SimulationTestBase, CitySimulationUnitTest, and WorldInteractionTest.
// This header is test-local (tests/simulation/); production code must never depend on it.

class ManualTerrainQuery : public ITerrainQuery {
public:
    ManualTerrainQuery() = default;

    // Set a uniform slope for ALL tiles (global override).
    // Used by WorldInteractionTest: `terrain_.setSlope(20.0f)` to trigger earthworks guard.
    // Per-tile entries (set via setSlope(x, z, degrees)) still take precedence over this value.
    void setSlope(float degrees) { m_globalSlope = degrees; }

    // Set slope for a specific tile (degrees, [0, 90]).
    // Takes precedence over the global slope for that tile.
    void setSlope(int tileX, int tileZ, float degrees) {
        m_slopes[makeKey(tileX, tileZ)] = degrees;
    }

    // Reset to default (flat terrain, 0° slope for all tiles).
    void resetSlope() {
        m_globalSlope = 0.0f;
        m_slopes.clear();
    }

    // ITerrainQuery implementation
    float getSlopeDegrees(int tileX, int tileZ) const override {
        auto it = m_slopes.find(makeKey(tileX, tileZ));
        if (it != m_slopes.end()) return it->second;  // per-tile entry takes precedence
        return m_globalSlope;                           // fall back to global slope
    }

    // getHeightAt() — returns m_heightAfterFlat if m_flattened is true, otherwise
    // m_heightBeforeFlat.  Both default to 0.0f so all existing tests that rely on
    // a 0.0f return are unaffected.
    float getHeightAt(int /*tileX*/, int /*tileZ*/) const override {
        return m_flattened ? m_heightAfterFlat : m_heightBeforeFlat;
    }

    // setTileHeight() — stateful form: marks m_flattened = true so tests can assert
    // that CitySimulation called this method during placement.
    void setTileHeight(int /*tileX*/, int /*tileZ*/, float /*height*/) override {
        m_flattened = true;
    }

    // flushTerrainRebuilds() — no-op in tests; real TerrainSystem delegates to
    // flushPendingRebuilds(). Required because flushTerrainRebuilds() is pure virtual
    // on ITerrainQuery; without this override ManualTerrainQuery fails to compile.
    void flushTerrainRebuilds() override {}


    // Height configuration helpers — set before constructing CitySimulation.
    void setHeightBeforeFlattening(float h) { m_heightBeforeFlat = h; }
    void setHeightAfterFlattening(float h)  { m_heightAfterFlat  = h; }

    // State observable by tests.
    bool  m_flattened{false};
    float m_heightBeforeFlat{0.0f};
    float m_heightAfterFlat{0.0f};

private:
    static int64_t makeKey(int x, int z) {
        return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
    }

    float m_globalSlope{0.0f};
    std::unordered_map<int64_t, float> m_slopes;
};
