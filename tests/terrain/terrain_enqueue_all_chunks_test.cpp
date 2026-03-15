// terrain_enqueue_all_chunks_test.cpp — Coverage tests for TerrainSystem::enqueueAllChunks().
//
// enqueueAllChunks() is structurally identical to buildAllChunks() but omits
// the trailing flushPendingRebuilds() call.  It is used by the Phase 11
// loading-screen loop where the caller drives flushing per-frame.
//
// Tests:
//   1. EnqueueAllChunks_PopulatesDeque — after generate() + enqueueAllChunks(),
//      pendingRebuildCount() > 0 (all chunks are queued, none processed yet).
//   2. EnqueueAllChunks_EmptyBeforeGenerate — calling enqueueAllChunks() before
//      generate() is a no-op (deque stays empty).
//   3. EnqueueAllChunks_CountMatchesBuildAllChunks — the number of queued
//      requests equals the expected chunk count (ceiling(mapTilesX/32) *
//      ceiling(mapTilesZ/32)).
//   4. EnqueueAllChunks_FlushProcessesAllChunks — after enqueueAllChunks(),
//      calling flushPendingRebuilds() drains the deque to zero.
//
// Label: "unit" (no display or GL context required)
// Target: terrain_tests (via target_sources)
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           implementation/phase-11.md §enqueueAllChunks loading-screen loop

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/terrain/TerrainSystem.h"
#include "src/interfaces/ITerrainRNG.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/ManualClock.h"

#include <memory>
#include <vector>

using namespace testing;

// ---------------------------------------------------------------------------
// FlatRNGEnqueue — deterministic ITerrainRNG returning 0.0f.
// Produces an all-flat heightmap so generate() always returns true on the
// first attempt: 100% flat >> 20% minimum, entire map is one flat region.
// Defined locally — other files define their own copy.
// ---------------------------------------------------------------------------
class FlatRNGEnqueue : public ITerrainRNG {
public:
    float nextFloat() override { return 0.0f; }
    int   nextInt(int min, int /*max*/) override { return min; }
    void  reseed(uint64_t) override {}
};

// ---------------------------------------------------------------------------
// TerrainEnqueueAllChunksTest fixture
// ---------------------------------------------------------------------------
class TerrainEnqueueAllChunksTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_renderer = std::make_unique<NiceMock<MockRenderer>>();
        m_clock    = std::make_unique<ManualClock>();
        m_sim      = std::make_unique<TerrainSystem>(m_renderer.get(), m_clock.get());
    }

    // TearDown: reset m_sim before mocks are destroyed to satisfy destructor-path contract.
    void TearDown() override {
        m_sim.reset();
        m_clock.reset();
        m_renderer.reset();
    }

    // Helper: generate a flat map with the given tile dimensions.
    bool generateFlat(int tilesX, int tilesZ, float cellSize = 4.0f) {
        FlatRNGEnqueue rng;
        return m_sim->generate(tilesX, tilesZ, cellSize, &rng, /*maxRetries=*/0);
    }

    std::unique_ptr<NiceMock<MockRenderer>> m_renderer;
    std::unique_ptr<ManualClock>            m_clock;
    std::unique_ptr<TerrainSystem>          m_sim;
};

// ---------------------------------------------------------------------------
// Test 1: enqueueAllChunks_PopulatesDeque
//
// After generate() + enqueueAllChunks(), pendingRebuildCount() must be > 0.
// Unlike buildAllChunks(), enqueueAllChunks() must NOT call
// flushPendingRebuilds() — so the deque must still be full after the call.
// ---------------------------------------------------------------------------
TEST_F(TerrainEnqueueAllChunksTest, EnqueueAllChunks_PopulatesDeque) {
    // 64x64 tile map -> 2x2 = 4 chunks (32 tiles per chunk).
    ASSERT_TRUE(generateFlat(64, 64))
        << "generate() must return true for a flat 64x64 map";

    EXPECT_EQ(m_sim->pendingRebuildCount(), 0)
        << "Deque must be empty before enqueueAllChunks()";

    m_sim->enqueueAllChunks();

    EXPECT_GT(m_sim->pendingRebuildCount(), 0)
        << "enqueueAllChunks() must populate the rebuild deque";
}

// ---------------------------------------------------------------------------
// Test 2: enqueueAllChunks_EmptyBeforeGenerate
//
// Calling enqueueAllChunks() before generate() is a no-op — the generated
// heightmap is empty, so the early-return guard fires immediately.
// ---------------------------------------------------------------------------
TEST_F(TerrainEnqueueAllChunksTest, EnqueueAllChunks_EmptyBeforeGenerate) {
    ASSERT_TRUE(m_sim->getGeneratedHeightmap().empty())
        << "Heightmap must be empty before generate()";

    m_sim->enqueueAllChunks();  // Must not crash; early-return guard fires.

    EXPECT_EQ(m_sim->pendingRebuildCount(), 0)
        << "enqueueAllChunks() before generate() must leave the deque empty";
}

// ---------------------------------------------------------------------------
// Test 3: enqueueAllChunks_CountMatchesBuildAllChunks
//
// The rebuild deque size after enqueueAllChunks() must equal the expected
// chunk count: ceil(mapTilesX / 32) * ceil(mapTilesZ / 32).
//
// For a 96x64 map: ceil(96/32)*ceil(64/32) = 3*2 = 6 chunks.
// ---------------------------------------------------------------------------
TEST_F(TerrainEnqueueAllChunksTest, EnqueueAllChunks_CountMatchesBuildAllChunks) {
    const int tilesX = 96;
    const int tilesZ = 64;
    ASSERT_TRUE(generateFlat(tilesX, tilesZ));

    m_sim->enqueueAllChunks();

    // ceil(96/32) * ceil(64/32) = 3 * 2 = 6 chunks.
    const int expectedChunks = 3 * 2;
    EXPECT_EQ(m_sim->pendingRebuildCount(), expectedChunks)
        << "enqueueAllChunks() must enqueue exactly ceil(mapX/32)*ceil(mapZ/32) chunks";
}

// ---------------------------------------------------------------------------
// Test 4: enqueueAllChunks_FlushProcessesAllChunks
//
// After enqueueAllChunks(), calling flushPendingRebuilds() must drain the
// deque to zero — all enqueued rebuilds are processed synchronously.
// This validates the loading-screen integration pattern:
//   enqueueAllChunks() once, then flushPendingRebuilds() per-frame until
//   pendingRebuildCount() == 0.
// ---------------------------------------------------------------------------
TEST_F(TerrainEnqueueAllChunksTest, EnqueueAllChunks_FlushProcessesAllChunks) {
    // 64x64 -> 4 chunks.
    ASSERT_TRUE(generateFlat(64, 64));

    m_sim->enqueueAllChunks();
    ASSERT_GT(m_sim->pendingRebuildCount(), 0)
        << "Precondition: deque must be non-empty before flush";

    // Flush all pending rebuilds in one call (no clock budget — clock returns 0).
    m_sim->flushPendingRebuilds();

    EXPECT_EQ(m_sim->pendingRebuildCount(), 0)
        << "flushPendingRebuilds() must drain all enqueued chunks to zero";
}

// ---------------------------------------------------------------------------
// Test 5: enqueueAllChunks_NonDivisibleMap_EdgeChunksZeroPadded
//
// When the map dimensions are not evenly divisible by 32, partial edge chunks
// are still registered and enqueued (zero-padded).  The total chunk count
// equals ceil(tilesX/32) * ceil(tilesZ/32).
//
// 50x50 map: ceil(50/32)*ceil(50/32) = 2*2 = 4 chunks.
// (The 50×50 map also satisfies the playability constraint for a flat RNG.)
// ---------------------------------------------------------------------------
TEST_F(TerrainEnqueueAllChunksTest, EnqueueAllChunks_NonDivisibleMap_EdgeChunksZeroPadded) {
    const int tilesX = 50;
    const int tilesZ = 50;
    // A 50x50 map satisfies the >= 50x50 contiguous flat region constraint.
    ASSERT_TRUE(generateFlat(tilesX, tilesZ));

    m_sim->enqueueAllChunks();

    // ceil(50/32) * ceil(50/32) = 2 * 2 = 4 chunks.
    const int expectedChunks = 2 * 2;
    EXPECT_EQ(m_sim->pendingRebuildCount(), expectedChunks)
        << "Non-divisible map must produce ceil(mapX/32)*ceil(mapZ/32) chunks including edge chunks";
}

// ---------------------------------------------------------------------------
// Test 6: enqueueAllChunks_DoesNotFlush_UnlikeBuildsAllChunks
//
// After enqueueAllChunks() the deque is non-empty — a single update() reduces
// it by at most 2 (the per-frame limit) but does NOT drain it completely for
// a 4-chunk map.  This confirms enqueueAllChunks() never calls flushPendingRebuilds().
// ---------------------------------------------------------------------------
TEST_F(TerrainEnqueueAllChunksTest, EnqueueAllChunks_DoesNotFlush_UnlikeBuildsAllChunks) {
    // 64x64 -> 4 chunks.
    ASSERT_TRUE(generateFlat(64, 64));

    m_sim->enqueueAllChunks();
    const int before = m_sim->pendingRebuildCount();
    ASSERT_EQ(before, 4)
        << "Precondition: 64x64 flat map must produce exactly 4 chunks";

    // One update() processes at most 2 rebuilds — leaving at least 2 pending.
    m_sim->update(0.016f);
    EXPECT_GE(m_sim->pendingRebuildCount(), 2)
        << "After one update(), at least 2 chunks must still be pending (enqueueAllChunks does not flush)";
}
