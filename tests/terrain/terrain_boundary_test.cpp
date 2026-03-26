// terrain_boundary_test.cpp — Phase 11l Deliverable 1 tests.
//
// Tests for the terrain stitching-hole fix in TerrainSystem::setTileHeight().
//
// Before the fix, setTileHeight() only enqueued the single chunk that "owns"
// the tile.  When tileX or tileZ is a chunk-boundary coordinate, up to 3
// additional adjacent chunks share the boundary vertex but were never enqueued,
// causing visible seams between chunks.  After the fix, affectedChunkIds()
// returns all chunks that share the vertex and the enqueue loop covers them all.
//
// Test 1: TerrainSystem_SetTileHeight_AtChunkBoundary_BothChunksEnqueued
//   Calls setTileHeight(4, 0, 5.0f) on a 8×8 map with chunkSize=4.
//   tileX=4 is the boundary between chunk(0,0) and chunk(1,0).
//   Asserts that getPendingRebuildIds() contains both chunk IDs (0 and 1).
//
// Test 2: TerrainSystem_SetTileHeight_Interior_OnlyOwningChunkEnqueued
//   Calls setTileHeight(2, 2, 5.0f) on the same map — an interior tile.
//   Asserts that getPendingRebuildIds() contains exactly 1 chunk ID.
//
// Test 3: TerrainSystem_SetTileHeight_CornerBoundary_AllFourChunksEnqueued
//   Calls setTileHeight(4, 4, 5.0f) — the exact corner where four chunks meet
//   on a 8×8 map with chunkSize=4.  All four chunk IDs must be enqueued.
//
// Label: "unit" (no display or GL context required)
// Target: terrain_tests
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           §setTileHeight Write Path
//           implementation/phase-11l.md §Deliverable 1

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/terrain/TerrainSystem.h"
#include "tests/simulation/ManualClock.h"
#include "tests/simulation/MockRenderer.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace testing;

// ---------------------------------------------------------------------------
// FlatRNG_Boundary — deterministic ITerrainRNG returning 0.0f.
// A flat heightmap satisfies the generate() playability constraints only when
// the map is >= 50×50 tiles (100% flat tiles, one contiguous region covering
// the whole map).  For our 8×8 test map we pass maxRetries=0 and accept that
// generate() may return false — what matters is that m_generatedHeightmap is
// populated so setTileHeight() can proceed.
// ---------------------------------------------------------------------------
class FlatRNG_Boundary : public ITerrainRNG {
public:
    float nextFloat() override { return 0.0f; }
    int   nextInt(int min, int /*max*/) override { return min; }
    void  reseed(uint64_t) override {}
};

// ---------------------------------------------------------------------------
// TerrainBoundaryTest fixture
//
// Map: 8×8 tiles, chunkSize = kTerrainLOD0GridSize (32) tiles per side.
// Because 8 < 32, the entire map fits in a single 32-tile chunk grid with
// chunksX = chunksZ = 1.  That means ALL tiles belong to chunk 0 — unsuitable
// for boundary testing.
//
// To expose the boundary we use a map that is at least 2×chunkSize tiles wide.
// kTerrainLOD0GridSize = 32, so a 64×64 map gives 2×2 = 4 chunks.
//
// Chunk layout for a 64×64 map (chunkTiles=32, chunksX=2):
//   chunk 0 = (cx=0, cz=0) → tiles [0..31] × [0..31]
//   chunk 1 = (cx=1, cz=0) → tiles [32..63] × [0..31]
//   chunk 2 = (cx=0, cz=1) → tiles [0..31]  × [32..63]
//   chunk 3 = (cx=1, cz=1) → tiles [32..63] × [32..63]
//
// Chunk ID formula: cz * chunksX + cx
//   chunk 0: 0*2+0 = 0
//   chunk 1: 0*2+1 = 1
//   chunk 2: 1*2+0 = 2
//   chunk 3: 1*2+1 = 3
// ---------------------------------------------------------------------------
class TerrainBoundaryTest : public ::testing::Test {
protected:
    static constexpr int kMapTiles = 64;  // must be >= 2 * kTerrainLOD0GridSize

    void SetUp() override {
        m_renderer = std::make_unique<NiceMock<MockRenderer>>();
        m_clock    = std::make_unique<ManualClock>();
        m_sys      = std::make_unique<TerrainSystem>(m_renderer.get(), m_clock.get());

        // generate() may return false (8×8 < 50×50 playability requirement) but
        // still populates m_generatedHeightmap, enabling setTileHeight() to work.
        FlatRNG_Boundary rng;
        m_sys->generate(kMapTiles, kMapTiles, /*cellSize=*/1.0f, &rng, /*maxRetries=*/0);
    }

    void TearDown() override {
        m_sys.reset();
        m_clock.reset();
        m_renderer.reset();
    }

    // Drain the rebuild deque so subsequent tests start from a clean state.
    void drainDeque() {
        for (int i = 0; i < 200; ++i) {
            if (m_sys->pendingRebuildCount() == 0) break;
            m_sys->update(0.016f);
        }
    }

    std::unique_ptr<NiceMock<MockRenderer>> m_renderer;
    std::unique_ptr<ManualClock>            m_clock;
    std::unique_ptr<TerrainSystem>          m_sys;
};

// ---------------------------------------------------------------------------
// Test 1: setTileHeight at an X-axis chunk boundary enqueues both chunks.
//
// tileX=32 is the first tile of chunk 1 (cx=1).  Vertex (32, 0) is shared
// by chunk 0 (owns the tile at x=31) and chunk 1 (owns the tile at x=32).
// Both chunk IDs 0 and 1 must appear in getPendingRebuildIds().
// ---------------------------------------------------------------------------
TEST_F(TerrainBoundaryTest,
       TerrainSystem_SetTileHeight_AtChunkBoundary_BothChunksEnqueued) {

    ASSERT_EQ(m_sys->pendingRebuildCount(), 0)
        << "Rebuild deque must be empty at the start of this test";

    // tileX=32: chunk boundary between chunk 0 (cx=0) and chunk 1 (cx=1).
    m_sys->setTileHeight(/*tileX=*/32, /*tileZ=*/0, /*height=*/5.0f);

    const std::vector<uint64_t> ids = m_sys->getPendingRebuildIds();

    EXPECT_THAT(ids, Contains(uint64_t{0}))
        << "Chunk 0 (west of the boundary) must be enqueued for rebuild";
    EXPECT_THAT(ids, Contains(uint64_t{1}))
        << "Chunk 1 (east of the boundary) must be enqueued for rebuild";
}

// ---------------------------------------------------------------------------
// Test 2: setTileHeight on an interior tile enqueues only 1 unique chunk.
//
// tileX=10, tileZ=10 is well inside chunk 0 (cx=0, cz=0).  None of the 8
// neighbour tiles crosses a chunk boundary, so only chunk 0 should appear in
// getPendingRebuildIds().
// ---------------------------------------------------------------------------
TEST_F(TerrainBoundaryTest,
       TerrainSystem_SetTileHeight_Interior_OnlyOwningChunkEnqueued) {

    ASSERT_EQ(m_sys->pendingRebuildCount(), 0)
        << "Rebuild deque must be empty at the start of this test";

    // tileX=10, tileZ=10: safely interior to chunk 0 (tiles 0..31 in both axes).
    m_sys->setTileHeight(/*tileX=*/10, /*tileZ=*/10, /*height=*/5.0f);

    const std::vector<uint64_t> ids = m_sys->getPendingRebuildIds();

    EXPECT_EQ(ids.size(), std::size_t{1})
        << "An interior tile must enqueue exactly 1 unique chunk ID; got "
        << ids.size();
    EXPECT_THAT(ids, Contains(uint64_t{0}))
        << "The single enqueued chunk must be chunk 0 (owner of interior tile (10,10))";
}

// ---------------------------------------------------------------------------
// Test 3: setTileHeight at a four-way corner enqueues all four chunks.
//
// tileX=32, tileZ=32 is the exact corner where chunks 0, 1, 2, 3 meet.
// Vertex (32, 32) is shared by all four chunks, so all four chunk IDs
// (0, 1, 2, 3) must appear in getPendingRebuildIds().
// ---------------------------------------------------------------------------
TEST_F(TerrainBoundaryTest,
       TerrainSystem_SetTileHeight_CornerBoundary_AllFourChunksEnqueued) {

    ASSERT_EQ(m_sys->pendingRebuildCount(), 0)
        << "Rebuild deque must be empty at the start of this test";

    // tileX=32, tileZ=32: four-way chunk corner.
    m_sys->setTileHeight(/*tileX=*/32, /*tileZ=*/32, /*height=*/7.0f);

    const std::vector<uint64_t> ids = m_sys->getPendingRebuildIds();

    EXPECT_THAT(ids, Contains(uint64_t{0})) << "Chunk 0 (NW) must be enqueued";
    EXPECT_THAT(ids, Contains(uint64_t{1})) << "Chunk 1 (NE) must be enqueued";
    EXPECT_THAT(ids, Contains(uint64_t{2})) << "Chunk 2 (SW) must be enqueued";
    EXPECT_THAT(ids, Contains(uint64_t{3})) << "Chunk 3 (SE) must be enqueued";
}
