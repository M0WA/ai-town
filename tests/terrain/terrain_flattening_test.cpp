// terrain_flattening_test.cpp — Phase 10b Feature 1 tests.
//
// Tests for TerrainSystem::setTileHeight() (the ITerrainQuery write-side API).
//
// Test 1: TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild
//   Verifies that setTileHeight() on a tile enqueues at least one
//   ChunkRebuildRequest (observable via TerrainSystem::pendingRebuildCount()).
//   A tile on a chunk boundary causes two adjacent chunks to be enqueued.
//
// Test 2: TerrainFlattening_NeighborBlend_ClampedToMapBounds
//   Verifies that setTileHeight() on a corner tile (0,0):
//   - Does NOT crash or write out-of-bounds (ASAN clean).
//   - Blends in-bounds cardinal neighbours toward the target height.
//   - Out-of-bounds (negative) neighbours are silently skipped.
//
// Label: "unit" (no display or GL context required)
// Target: terrain_tests
// Spec ref: architecture/graphics-architecture/procedural-terrain.md §setTileHeight Write Path
//           implementation/phase-10b.md §test-dev-cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/terrain/TerrainSystem.h"
#include "tests/simulation/ManualClock.h"    // ManualClock
#include "tests/simulation/MockRenderer.h"   // NiceMock<MockRenderer>

#include <memory>
#include <vector>

using namespace testing;

// ---------------------------------------------------------------------------
// FlatRNG — deterministic ITerrainRNG that always returns 0.0f.
// Produces a flat heightmap (all heights = 0.0f) so generate() always returns
// true on the first attempt: 100% flat tiles >> 20% minimum, and the entire
// map is one contiguous flat region >> 50×50 minimum.
// Defined locally — terrain_system_test.cpp's copy is not visible here.
// ---------------------------------------------------------------------------
class FlatRNG2 : public ITerrainRNG {
public:
    float nextFloat() override { return 0.0f; }
    int   nextInt(int min, int /*max*/) override { return min; }
    void  reseed(uint64_t) override {}
};

// ---------------------------------------------------------------------------
// TerrainFlatteningTest fixture
// ---------------------------------------------------------------------------
class TerrainFlatteningTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_renderer = std::make_unique<NiceMock<MockRenderer>>();
        m_clock    = std::make_unique<ManualClock>();
        m_sim      = std::make_unique<TerrainSystem>(m_renderer.get(), m_clock.get());
    }

    // TearDown: reset m_sim BEFORE the mocks are destroyed.
    // Satisfies the destructor-path contract: all pending GMock expectations on
    // m_renderer must be verified while the mock object is still alive.
    void TearDown() override {
        m_sim.reset();
        m_clock.reset();
        m_renderer.reset();
    }

    // Helper: generate a flat map and return true on success.
    // Uses 100×100 tiles — must be >= 50×50 to satisfy the contiguous flat region constraint.
    bool generateFlatMap(int tilesX = 100, int tilesZ = 100) {
        FlatRNG2 rng;
        return m_sim->generate(tilesX, tilesZ, /*cellSize=*/4.0f, &rng, /*maxRetries=*/0);
    }

    std::unique_ptr<NiceMock<MockRenderer>> m_renderer;
    std::unique_ptr<ManualClock>            m_clock;
    std::unique_ptr<TerrainSystem>          m_sim;
};

// ---------------------------------------------------------------------------
// TerrainFlatteningTest / TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild
//
// Construct a TerrainSystem, generate a small flat map, then call
// setTileHeight() on a valid interior tile.  Assert that at least one
// ChunkRebuildRequest was enqueued (pendingRebuildCount() >= 1).
//
// Additional: call setTileHeight() on a tile at position (31, 0) — the last
// tile of chunk 0 on a 64-tile-wide map laid out in 32-tile chunks.
// That tile's east neighbour (32, 0) falls in chunk 1, so two distinct chunks
// are affected → pendingRebuildCount() must be >= 2 after the second call.
//
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           §setTileHeight Write Path
// ---------------------------------------------------------------------------
TEST_F(TerrainFlatteningTest, TerrainFlattening_SetTileHeight_EnqueuesChunkRebuild) {
    // Use a 64×64 map so chunk-boundary tile (31, 0) is valid.
    FlatRNG2 rng;
    bool playable = m_sim->generate(/*mapTilesX=*/64, /*mapTilesZ=*/64,
                                    /*cellSize=*/4.0f, &rng, /*maxRetries=*/0);
    ASSERT_TRUE(playable);

    // Call setTileHeight() on an interior tile.
    const int kTileX = 5;
    const int kTileZ = 5;
    ASSERT_EQ(m_sim->pendingRebuildCount(), 0)
        << "Rebuild deque must be empty before setTileHeight() is called";

    m_sim->setTileHeight(kTileX, kTileZ, /*height=*/10.0f);

    EXPECT_GE(m_sim->pendingRebuildCount(), 1)
        << "setTileHeight() must enqueue at least one ChunkRebuildRequest";

    // Drain the deque so the chunk-boundary test starts clean.
    // update() pops at most 2 per frame; call enough times to clear all.
    for (int i = 0; i < 20; ++i) {
        m_sim->update(0.016f);
    }
    ASSERT_EQ(m_sim->pendingRebuildCount(), 0) << "Deque must be empty after draining";

    // Tile (31, 0): the last column of chunk 0.  Its east neighbour (32, 0) is
    // in chunk 1.  Setting height here must enqueue at least 2 distinct chunk IDs.
    m_sim->setTileHeight(/*tileX=*/31, /*tileZ=*/0, /*height=*/5.0f);

    EXPECT_GE(m_sim->pendingRebuildCount(), 2)
        << "setTileHeight() on a chunk-boundary tile must enqueue requests for "
           "at least 2 chunks (the tile's own chunk and its east-neighbour chunk)";
}

// ---------------------------------------------------------------------------
// TerrainFlatteningTest / TerrainFlattening_NeighborBlend_ClampedToMapBounds
//
// Call setTileHeight() on the corner tile (0, 0) of a 10×10 map where all
// heights start at 0.0f.  At this corner:
//   - In-bounds cardinal neighbours: (1,0) and (0,1) — east and south.
//   - Out-of-bounds positions: (-1,0), (0,-1), and three diagonals.
//
// Assertions:
//   1. No crash (ASAN clean, no exception).
//   2. getHeightAt(1, 0) is strictly between 0.0f and the target height
//      (cardinal lerp factor = 0.5 → blended to target*0.5).
//   3. getHeightAt(0, 1) is strictly between 0.0f and the target height.
//   4. Out-of-bounds negative indices return 0.0f (unchanged, no write).
//
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           §setTileHeight Write Path  (cardinal falloff = 0.5)
// ---------------------------------------------------------------------------
TEST_F(TerrainFlatteningTest, TerrainFlattening_NeighborBlend_ClampedToMapBounds) {
    // Use 100×100 — generate() requires >= 50×50 contiguous flat region to return true.
    ASSERT_TRUE(generateFlatMap(/*tilesX=*/100, /*tilesZ=*/100));

    // All heights start at 0.0f (FlatRNG2).
    ASSERT_FLOAT_EQ(m_sim->getHeightAt(0, 0), 0.0f);
    ASSERT_FLOAT_EQ(m_sim->getHeightAt(1, 0), 0.0f);
    ASSERT_FLOAT_EQ(m_sim->getHeightAt(0, 1), 0.0f);

    const float kTargetHeight = 20.0f;

    // Must not crash — out-of-bounds neighbours are silently skipped.
    EXPECT_NO_FATAL_FAILURE(m_sim->setTileHeight(/*tileX=*/0, /*tileZ=*/0, kTargetHeight));

    // Centre tile must be set exactly to kTargetHeight.
    EXPECT_FLOAT_EQ(m_sim->getHeightAt(0, 0), kTargetHeight)
        << "Centre tile height must equal the target height after setTileHeight()";

    // East cardinal neighbour (1,0): lerp(0, kTargetHeight, 0.5) = kTargetHeight * 0.5.
    const float eastH = m_sim->getHeightAt(1, 0);
    EXPECT_GT(eastH, 0.0f)
        << "East neighbour (1,0) must have moved above 0.0f after blending";
    EXPECT_LT(eastH, kTargetHeight)
        << "East neighbour (1,0) must be below the target height (not fully flattened)";

    // South cardinal neighbour (0,1): same lerp factor.
    const float southH = m_sim->getHeightAt(0, 1);
    EXPECT_GT(southH, 0.0f)
        << "South neighbour (0,1) must have moved above 0.0f after blending";
    EXPECT_LT(southH, kTargetHeight)
        << "South neighbour (0,1) must be below the target height (not fully flattened)";

    // Out-of-bounds neighbours must not write: getHeightAt returns 0.0f for them.
    EXPECT_FLOAT_EQ(m_sim->getHeightAt(-1,  0), 0.0f)
        << "Out-of-bounds tile (-1,0) must remain unwritten (returns 0.0f)";
    EXPECT_FLOAT_EQ(m_sim->getHeightAt( 0, -1), 0.0f)
        << "Out-of-bounds tile (0,-1) must remain unwritten (returns 0.0f)";

    // At least one ChunkRebuildRequest must have been enqueued.
    EXPECT_GE(m_sim->pendingRebuildCount(), 1)
        << "setTileHeight() on corner tile (0,0) must enqueue at least one rebuild";
}
