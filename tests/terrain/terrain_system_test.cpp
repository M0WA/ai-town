// terrain_system_test.cpp — Phase 5 TerrainSystem rebuild-deque unit tests.
//
// Tests the TerrainSystem rebuild-deque contract:
//   - At most 2 chunk rebuilds processed per update() call
//   - Same-frame deduplication via processedThisFrame set
//   - Chunks already at target LOD are skipped (no redundant rebuild)
//   - flushPendingRebuilds() respects the 100 ms wall-clock budget via ManualClock
//   - LOD hysteresis distances are >= 8 m (close) and >= 15 m (far) for terrain chunks
//
// All tests use NiceMock<MockRenderer> (NiceMock policy for property/integration
// tests per project mock policy; TerrainSystem is integrated with IRenderer*).
// ManualClock provides deterministic time control for budget-exhaustion tests.
//
// TearDown contract: sim_ and renderer_ pointers are reset explicitly to document
// that TerrainSystem destructor runs BEFORE MockRenderer goes out of scope.
// Without explicit reset, MockRenderer's GMock expectations could fire after
// the mock has already entered its destructor — undefined behaviour.
//
// Label: "unit" (no display or GL context required)
// Decision locked: TerrainSystem holds IRenderer* and never calls Irrlicht API
// directly.  All four rebuild-deque tests belong in terrain_tests (label "unit").
// Spec ref: phase-5.md §TerrainSystem rebuild deque tests
//           architecture/graphics-architecture/procedural-terrain.md §IClock Injection

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/terrain/TerrainSystem.h"
#include "src/interfaces/ITerrainRNG.h"
#include "tests/simulation/MockRenderer.h"   // NiceMock<MockRenderer>
#include "tests/simulation/ManualClock.h"    // ManualClock
#include "tests/terrain/MockTerrainRNG.h"   // MockTerrainRNG

#include <memory>
#include <vector>

using namespace testing;

// ---------------------------------------------------------------------------
// FlatRNG — deterministic ITerrainRNG that always returns 0.0f.
//
// Using nextFloat()=0 produces a heightmap where every vertex height = 0.
// This gives slope = 0 at every tile, satisfying both playability constraints:
//   (1) 100% flat tiles (>> 20% minimum)
//   (2) Entire map is one contiguous flat region (>> 50x50 minimum)
//
// Used by TerrainSystem_FlatTilePercentage_MeetsMinimum and
// TerrainSystem_ContiguousFlatRegion_MeetsMinimum to verify generate() returns
// true (playable) when both constraints are satisfied on the first attempt.
// ---------------------------------------------------------------------------
class FlatRNG : public ITerrainRNG {
public:
    float nextFloat() override { return 0.0f; }
    int   nextInt(int min, int /*max*/) override { return min; }
    void  reseed(uint64_t) override {}
};

// ---------------------------------------------------------------------------
// BudgetExhaustionClock
//
// Specialised test double for the flushPendingRebuilds() budget test.
// Returns a base time on the first call, then base + 0.101 on all subsequent
// calls — simulating the passage of 101 ms after the first rebuild completes.
//
// This is the clock pattern prescribed by:
//   architecture/graphics-architecture/procedural-terrain.md
//   §TerrainSystem_FlushPendingRebuilds_BudgetExhausted_StopsAfterBudget
//
//   1. Enqueue 10 rebuilds.
//   2. ManualClock returns start + 0.101 on the SECOND call to nowSeconds().
//   3. Assert fewer than 10 rebuilds were processed.
// ---------------------------------------------------------------------------
class BudgetExhaustionClock : public IClock {
public:
    double nowSeconds() const override {
        if (m_callCount == 0) {
            ++m_callCount;
            return m_baseTime;
        }
        return m_baseTime + 0.101;  // 101 ms past start — budget exhausted
    }

    void setBaseTime(double t) { m_baseTime = t; m_callCount = 0; }

private:
    double m_baseTime{0.0};
    mutable int m_callCount{0};
};

// ---------------------------------------------------------------------------
// TerrainSystemTest fixture
// ---------------------------------------------------------------------------
class TerrainSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_renderer = std::make_unique<NiceMock<MockRenderer>>();
        m_clock    = std::make_unique<ManualClock>();
        m_sim      = std::make_unique<TerrainSystem>(m_renderer.get(), m_clock.get());
    }

    // TearDown: reset m_sim BEFORE the mocks are destroyed.
    // This satisfies the destructor-path contract: all pending GMock expectations
    // on m_renderer must be verified/satisfied while the mock object is still alive.
    // If m_sim's destructor calls methods on m_renderer (e.g., to remove scene nodes),
    // those calls must complete before the mock's EXPECT_CALL machinery shuts down.
    void TearDown() override {
        m_sim.reset();      // TerrainSystem destructor runs here
        m_clock.reset();    // ManualClock destructor runs here
        m_renderer.reset(); // MockRenderer destructor (verifies expectations) runs here
    }

    std::unique_ptr<NiceMock<MockRenderer>> m_renderer;
    std::unique_ptr<ManualClock>            m_clock;
    std::unique_ptr<TerrainSystem>          m_sim;
};

// ---------------------------------------------------------------------------
// TerrainSystemTest_RebuildDeque_ProcessesAtMostTwoPerFrame
//
// Enqueue 5 ChunkRebuildRequests (each a distinct chunk ID, distinct target LOD).
// Call update(0.016f) once.
// Verify the deque shrinks by at most 2 (i.e., at least 3 remain pending).
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, RebuildDeque_ProcessesAtMostTwoPerFrame) {
    // Enqueue 5 rebuild requests: chunk IDs 10–14, target LOD 1.
    for (uint64_t id = 10; id < 15; ++id) {
        m_sim->enqueueRebuild(id, /*targetLOD=*/1);
    }

    const int beforeCount = m_sim->pendingRebuildCount();
    ASSERT_EQ(beforeCount, 5) << "All 5 requests must be in the deque before update()";

    m_sim->update(0.016f);

    const int afterCount = m_sim->pendingRebuildCount();
    // At most 2 processed per frame -> at least 3 remain.
    EXPECT_LE(beforeCount - afterCount, 2)
        << "update() must process at most 2 rebuilds per frame";
    EXPECT_GE(afterCount, 3)
        << "At least 3 rebuilds must remain pending after one update()";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_RebuildDeque_DeduplicatesWithinFrame
//
// Enqueue the same chunk ID twice (same targetLOD).
// Call update().
// Verify the second occurrence is skipped: at most 1 rebuild performed for
// that chunk within the single frame, not 2.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, RebuildDeque_DeduplicatesWithinFrame) {
    const uint64_t kDuplicateId = 42u;

    // Enqueue the same chunk ID twice — simulates a double-enqueue
    // (e.g., LOD distance check fires twice before the deque drains).
    m_sim->enqueueRebuild(kDuplicateId, /*targetLOD=*/1);
    m_sim->enqueueRebuild(kDuplicateId, /*targetLOD=*/1);

    ASSERT_EQ(m_sim->pendingRebuildCount(), 2)
        << "Both entries must be accepted into the deque before update()";

    m_sim->update(0.016f);

    // processedThisFrame contains kDuplicateId after the first pop.
    // The second occurrence must be skipped (not rebuilt) within the same frame.
    // Total processed <= 1 unique chunk, regardless of how many times it appeared.
    EXPECT_LE(m_sim->chunksRebuiltLastFrame(), 1)
        << "Duplicate chunk ID must be processed at most once per frame";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_RebuildDeque_SkipsIfAlreadyAtTargetLOD
//
// Enqueue a chunk that is already at its target LOD (currentLOD == targetLOD).
// Verify the rebuild is skipped (not counted as processed), and the deque entry
// is consumed without triggering a scene node rebuild.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, RebuildDeque_SkipsIfAlreadyAtTargetLOD) {
    const uint64_t kChunkId = 7u;
    const int kLOD = 1;

    // Register the chunk at LOD1 and then enqueue a request for LOD1 again.
    m_sim->registerChunkAtLOD(kChunkId, kLOD);
    m_sim->enqueueRebuild(kChunkId, /*targetLOD=*/kLOD);

    ASSERT_EQ(m_sim->pendingRebuildCount(), 1);

    m_sim->update(0.016f);

    // The deque entry should have been consumed (popped) without rebuilding.
    EXPECT_EQ(m_sim->pendingRebuildCount(), 0)
        << "The skip-same-LOD entry must be consumed from the deque";
    EXPECT_EQ(m_sim->chunksRebuiltLastFrame(), 0)
        << "No rebuild should occur when currentLOD == targetLOD";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_FlushPendingRebuilds_BudgetExhausted_StopsAfterBudget
//
// IClock injection test: the 100 ms wall-clock budget in flushPendingRebuilds()
// is measured via m_clock->nowSeconds().  By injecting BudgetExhaustionClock
// (returns start+0.101 on the SECOND call), we trigger budget exhaustion after
// the FIRST rebuild is processed — without real-time delays.
//
// Steps (per architecture/graphics-architecture/procedural-terrain.md):
//   1. Enqueue 10 distinct rebuild requests.
//   2. Replace the clock with BudgetExhaustionClock.
//   3. Call flushPendingRebuilds().
//   4. Assert that fewer than 10 rebuilds were processed.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, FlushPendingRebuilds_BudgetExhausted_StopsAfterBudget) {
    // Enqueue 10 distinct chunks at target LOD 1.
    for (uint64_t id = 100; id < 110; ++id) {
        m_sim->enqueueRebuild(id, /*targetLOD=*/1);
    }
    ASSERT_EQ(m_sim->pendingRebuildCount(), 10)
        << "All 10 requests must be queued before flush";

    // Re-create TerrainSystem with BudgetExhaustionClock injected.
    // We reset m_sim (destroying the old instance) then construct a new one
    // with the specialised clock.  This avoids modifying the test fixture clock
    // mid-test, which would violate the single-responsibility of ManualClock.
    BudgetExhaustionClock budgetClock;
    budgetClock.setBaseTime(0.0);

    // Construct a new TerrainSystem directly with the budget-exhaustion clock.
    // NiceMock<MockRenderer> is already available via m_renderer.
    TerrainSystem simWithBudgetClock(m_renderer.get(), &budgetClock);

    // Re-enqueue into the new instance.
    for (uint64_t id = 100; id < 110; ++id) {
        simWithBudgetClock.enqueueRebuild(id, /*targetLOD=*/1);
    }
    ASSERT_EQ(simWithBudgetClock.pendingRebuildCount(), 10);

    simWithBudgetClock.flushPendingRebuilds();

    // Budget exhausted after first rebuild (clock returned start+0.101 on second call).
    // Therefore fewer than 10 rebuilds were processed.
    EXPECT_LT(simWithBudgetClock.chunksRebuiltLastFlush(), 10)
        << "flushPendingRebuilds() must stop when 100 ms budget is exceeded";
    // At least 1 rebuild completed (the one before the clock reported budget exhaustion).
    EXPECT_GE(simWithBudgetClock.chunksRebuiltLastFlush(), 1)
        << "At least 1 rebuild must complete before budget is checked";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_LOD_HysteresisGap_At_Least8mClose_At_Least15mFar
//
// Verifies LOD hysteresis distances for terrain chunks per the table in
// architecture/asset-standards/3d-model-standards.md §LOD Distance Thresholds:
//
//   Terrain chunk: LOD0->LOD1 switch-out > 100 m, switch-in < 92 m (8 m gap)
//                  LOD1->LOD2 switch-out > 300 m, switch-in < 285 m (15 m gap)
//
// TerrainSystem exposes lodSwitchOutDistance(fromLOD) and
// lodSwitchInDistance(fromLOD) for the LOD0->LOD1 transition.
// The hysteresis gap = switchOut - switchIn must be >= minimum.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, LOD_HysteresisGap_At_Least8mClose_At_Least15mFar) {
    // LOD0 -> LOD1 (close transition): 8 m minimum hysteresis band.
    const float switchOut01 = m_sim->lodSwitchOutDistance(/*fromLOD=*/0);
    const float switchIn01  = m_sim->lodSwitchInDistance(/*fromLOD=*/0);
    const float gap01 = switchOut01 - switchIn01;

    EXPECT_GT(switchOut01, 100.0f)
        << "LOD0->LOD1 switch-out must be > 100 m per 3d-model-standards.md";
    EXPECT_LT(switchIn01, 92.0f)
        << "LOD0->LOD1 switch-in must be < 92 m per 3d-model-standards.md";
    EXPECT_GE(gap01, 8.0f)
        << "LOD0->LOD1 hysteresis gap must be >= 8 m (terrain chunk exception per spec)";

    // LOD1 -> LOD2 (far transition): 15 m minimum hysteresis band.
    const float switchOut12 = m_sim->lodSwitchOutDistance(/*fromLOD=*/1);
    const float switchIn12  = m_sim->lodSwitchInDistance(/*fromLOD=*/1);
    const float gap12 = switchOut12 - switchIn12;

    EXPECT_GT(switchOut12, 300.0f)
        << "LOD1->LOD2 switch-out must be > 300 m per 3d-model-standards.md";
    EXPECT_LT(switchIn12, 285.0f)
        << "LOD1->LOD2 switch-in must be < 285 m per 3d-model-standards.md";
    EXPECT_GE(gap12, 15.0f)
        << "LOD1->LOD2 hysteresis gap must be >= 15 m per 3d-model-standards.md";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_FlatTilePercentage_MeetsMinimum
//
// Verifies playability constraint (1): generate() returns true only when at
// least 20% of total map tiles have slope < 15 degrees.
//
// Approach: inject FlatRNG (nextFloat()=0) → all vertex heights = 0 → all
// tile slopes = 0 degrees → flat tile percentage = 100% >= 20%.
// A 100×100 tile map is used; maxRetries=0 so the first attempt must satisfy
// the constraint.
//
// Spec ref: phase-5.md §Map playability guarantee
//           architecture/game-design/terrain-interaction.md §Buildability
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, FlatTilePercentage_MeetsMinimum) {
    FlatRNG flatRng;
    // 100×100 tile map, 4 m cell size.  All heights = 0 → 100% flat tiles.
    bool playable = m_sim->generate(/*mapTilesX=*/100, /*mapTilesZ=*/100,
                                    /*cellSize=*/4.0f, &flatRng, /*maxRetries=*/0);
    EXPECT_TRUE(playable)
        << "generate() must return true when flat tile percentage (100%) >= 20% minimum";
    // Verify that a non-empty heightmap was stored.
    EXPECT_FALSE(m_sim->getGeneratedHeightmap().empty())
        << "generate() must populate m_generatedHeightmap on success";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_ContiguousFlatRegion_MeetsMinimum
//
// Verifies playability constraint (2): generate() returns true only when at
// least one contiguous flat region of >= 50×50 tiles exists.
//
// Approach: inject FlatRNG (nextFloat()=0) → all slopes = 0 → entire 100×100
// map is one flat region (100×100 >> 50×50 minimum).
// maxRetries=0 — the first attempt must satisfy both constraints.
//
// Spec ref: phase-5.md §Map playability guarantee
//           architecture/game-design/terrain-interaction.md §Buildability
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, ContiguousFlatRegion_MeetsMinimum) {
    FlatRNG flatRng;
    // 100×100 tile map.  All heights = 0 → entire map is one flat connected
    // component with bounding box 100×100 >> 50×50 minimum.
    bool playable = m_sim->generate(/*mapTilesX=*/100, /*mapTilesZ=*/100,
                                    /*cellSize=*/4.0f, &flatRng, /*maxRetries=*/0);
    EXPECT_TRUE(playable)
        << "generate() must return true when a contiguous flat region >= 50×50 exists";
    // The generated heightmap must have (100+1)*(100+1) = 10201 vertices.
    EXPECT_EQ(m_sim->getGeneratedHeightmap().size(), static_cast<size_t>(101 * 101))
        << "Heightmap vertex count must be (mapTilesX+1)*(mapTilesZ+1) = 101*101";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_Generate_HeightmapIsNonEmpty
//
// Verifies that after calling generate(), getGeneratedHeightmap() returns a
// non-empty vector of size (mapTilesX+1)*(mapTilesZ+1).
// Uses FlatRNG (all zeros) so slope = 0 everywhere, guaranteeing playability.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, Generate_HeightmapIsNonEmpty) {
    FlatRNG flatRng;
    const int tilesX = 64;
    const int tilesZ = 64;

    EXPECT_TRUE(m_sim->getGeneratedHeightmap().empty())
        << "Heightmap must be empty before generate() is called";

    m_sim->generate(tilesX, tilesZ, /*cellSize=*/2.0f, &flatRng, /*maxRetries=*/0);

    const auto& hmap = m_sim->getGeneratedHeightmap();
    EXPECT_FALSE(hmap.empty())
        << "getGeneratedHeightmap() must be non-empty after generate()";
    EXPECT_EQ(hmap.size(), static_cast<size_t>((tilesX + 1) * (tilesZ + 1)))
        << "Heightmap size must be (tilesX+1)*(tilesZ+1)";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_Generate_ReturnsFalse_WhenConstraintsNotMet
//
// Uses a SteepRNG that always returns 1.0f, producing a heightmap with large
// vertical variation. On a small 10×10 map the slope will be very steep,
// making it impossible to satisfy the 50×50 contiguous-flat constraint with
// maxRetries=0.  generate() must still return a value (false) and store the
// heightmap — it must NOT abort or throw.
//
// The exact return value depends on whether the random amplitudes happen to
// produce >= 20% flat tiles at the given cellSize, so we only assert that:
//   (1) getGeneratedHeightmap() is populated (function completed)
//   (2) The heightmap has the correct size
// ---------------------------------------------------------------------------
class SteepRNG : public ITerrainRNG {
public:
    float nextFloat() override { return 1.0f; } // Maximum height everywhere
    int   nextInt(int /*min*/, int max) override { return max; }
    void  reseed(uint64_t) override { ++m_reseedCount; }
    int   reseedCount() const { return m_reseedCount; }
private:
    int m_reseedCount{0};
};

TEST_F(TerrainSystemTest, Generate_PopulatesHeightmap_EvenOnPlayabilityFailure) {
    SteepRNG steepRng;
    // 10×10 map — too small for 50×50 constraint regardless of terrain shape.
    m_sim->generate(/*mapTilesX=*/10, /*mapTilesZ=*/10,
                    /*cellSize=*/1.0f, &steepRng, /*maxRetries=*/0);

    // Heightmap must always be populated regardless of playability result.
    const auto& hmap = m_sim->getGeneratedHeightmap();
    EXPECT_FALSE(hmap.empty())
        << "generate() must populate the heightmap even when playability fails";
    EXPECT_EQ(hmap.size(), static_cast<size_t>(11 * 11))
        << "Heightmap size must be (10+1)*(10+1)=121";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_Generate_ReseededOnRetry
//
// Verifies that when the first attempt fails playability, generate() calls
// rng->reseed() for each retry.  Uses a small map (10×10) that cannot satisfy
// the 50×50 contiguous-flat constraint, so all attempts will fail.
// After maxRetries=5 retries, reseedCount() must equal 5 (one reseed per retry,
// but NOT on attempt 0 which uses the original seed).
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, Generate_ReseededOnRetry) {
    SteepRNG steepRng;
    const int maxRetries = 5;
    // Small 10×10 map cannot satisfy 50×50 contiguous-flat constraint.
    m_sim->generate(/*mapTilesX=*/10, /*mapTilesZ=*/10,
                    /*cellSize=*/1.0f, &steepRng, maxRetries);

    // reseed() should have been called once per retry attempt (attempts 1..5),
    // not on attempt 0 (uses original seed).
    EXPECT_EQ(steepRng.reseedCount(), maxRetries)
        << "reseed() must be called exactly maxRetries times (once per retry, not on attempt 0)";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_UnregisterChunk_RemovesFromActiveMap
//
// Verifies that unregisterChunk() removes a previously registered chunk from
// the active chunk map.  After unregistering, hasActiveChunk() must return
// false and getChunkLOD() must return -1.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, UnregisterChunk_RemovesFromActiveMap) {
    const uint64_t kChunkId = 99u;

    // Register a chunk at LOD 0.
    m_sim->registerChunkAtLOD(kChunkId, /*currentLOD=*/0);
    ASSERT_TRUE(m_sim->hasActiveChunk(kChunkId))
        << "Chunk must be present after registerChunkAtLOD()";
    ASSERT_EQ(m_sim->getChunkLOD(kChunkId), 0)
        << "getChunkLOD() must return 0 after registering at LOD 0";

    // Unregister the chunk.
    m_sim->unregisterChunk(kChunkId);

    EXPECT_FALSE(m_sim->hasActiveChunk(kChunkId))
        << "Chunk must not be present after unregisterChunk()";
    EXPECT_EQ(m_sim->getChunkLOD(kChunkId), -1)
        << "getChunkLOD() must return -1 for an unregistered chunk";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_UnregisterChunk_NonExistent_IsNoop
//
// Verifies that calling unregisterChunk() on a chunk ID that was never
// registered is a no-op (does not crash or corrupt state).
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, UnregisterChunk_NonExistent_IsNoop) {
    const uint64_t kNeverRegisteredId = 12345u;
    ASSERT_FALSE(m_sim->hasActiveChunk(kNeverRegisteredId));

    // Must not crash.
    EXPECT_NO_FATAL_FAILURE(m_sim->unregisterChunk(kNeverRegisteredId));
    EXPECT_FALSE(m_sim->hasActiveChunk(kNeverRegisteredId));
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_GetChunkLOD_ReturnsCorrectLOD
//
// Verifies that getChunkLOD() returns the current LOD of a registered chunk.
// Also verifies that after a rebuild updates the LOD, getChunkLOD() reflects
// the new value.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, GetChunkLOD_ReturnsCorrectLOD) {
    const uint64_t kChunkId = 55u;

    // -1 for unregistered.
    EXPECT_EQ(m_sim->getChunkLOD(kChunkId), -1)
        << "getChunkLOD() must return -1 for an unregistered chunk";

    // Register at LOD 2.
    m_sim->registerChunkAtLOD(kChunkId, /*currentLOD=*/2);
    EXPECT_EQ(m_sim->getChunkLOD(kChunkId), 2)
        << "getChunkLOD() must return 2 after registerChunkAtLOD(..., 2)";

    // Process a rebuild to LOD 0 — should update the tracked LOD.
    m_sim->enqueueRebuild(kChunkId, /*targetLOD=*/0);
    m_sim->update(0.016f);

    EXPECT_EQ(m_sim->getChunkLOD(kChunkId), 0)
        << "getChunkLOD() must return 0 after rebuild to LOD 0";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_GetGeneratedHeightmap_EmptyBeforeGenerate
//
// Verifies that getGeneratedHeightmap() returns an empty vector before
// generate() is called.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, GetGeneratedHeightmap_EmptyBeforeGenerate) {
    EXPECT_TRUE(m_sim->getGeneratedHeightmap().empty())
        << "getGeneratedHeightmap() must return empty vector before generate() is called";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_Generate_FlatMap_AllHeightsZero
//
// Verifies that FlatRNG (nextFloat()=0) produces a heightmap where all vertex
// heights are exactly 0.0f.  This validates that the RNG injection is actually
// used in the heightmap construction — not a hardcoded constant.
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, Generate_FlatMap_AllHeightsZero) {
    FlatRNG flatRng;
    m_sim->generate(/*mapTilesX=*/10, /*mapTilesZ=*/10,
                    /*cellSize=*/2.0f, &flatRng, /*maxRetries=*/0);

    const auto& hmap = m_sim->getGeneratedHeightmap();
    ASSERT_FALSE(hmap.empty());

    for (size_t i = 0; i < hmap.size(); ++i) {
        EXPECT_FLOAT_EQ(hmap[i], 0.0f)
            << "All heights must be 0.0f when FlatRNG (nextFloat()=0) is used; failed at index "
            << i;
    }
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_ProcessOneRebuild_WithHeightmap_LOD1_CallsRebuildChunk
//
// Registers a chunk at LOD 0, supplies its LOD0 heightmap and world origin,
// then enqueues a rebuild to LOD 1.  Expects IRenderer::rebuildTerrainChunk()
// to be called exactly once and verifies that the chunk's tracked LOD is
// updated to 1 after update().
//
// The heightmap has (kTerrainLOD0GridSize+1)^2 = 33*33 = 1089 entries (all 0.0f).
// This exercises the targetLOD == 1 branch in processOneRebuild().
//
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           §TerrainSystem rebuild deque tests
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, ProcessOneRebuild_WithHeightmap_LOD1_CallsRebuildChunk) {
    const uint64_t kChunkId = 200u;

    // Register the chunk at LOD 0.
    m_sim->registerChunkAtLOD(kChunkId, /*currentLOD=*/0);

    // Register the LOD0 heightmap: (kTerrainLOD0GridSize+1)^2 = 33*33 = 1089 floats, all 0.0f.
    const int kVertexCount = (kTerrainLOD0GridSize + 1) * (kTerrainLOD0GridSize + 1);
    m_sim->registerChunkHeightmap(kChunkId, std::vector<float>(kVertexCount, 0.0f));

    // Register the chunk world origin.
    m_sim->registerChunkPosition(kChunkId, /*worldOriginX=*/0.0f, /*worldOriginZ=*/0.0f);

    // Expect rebuildTerrainChunk() to be called exactly once during update().
    EXPECT_CALL(*m_renderer, rebuildTerrainChunk(::testing::_)).Times(1);

    // Enqueue rebuild to LOD 1 and process it.
    m_sim->enqueueRebuild(kChunkId, /*targetLOD=*/1);
    m_sim->update(0.016f);

    // The chunk's tracked LOD must be updated to 1.
    EXPECT_EQ(m_sim->getChunkLOD(kChunkId), 1)
        << "getChunkLOD() must return 1 after a successful rebuild to LOD 1";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_ProcessOneRebuild_WithHeightmap_LOD2_CallsRebuildChunk
//
// Same structure as the LOD1 test above, but targets LOD 2.
// Exercises the targetLOD >= 2 branch in processOneRebuild().
//
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           §TerrainSystem rebuild deque tests
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, ProcessOneRebuild_WithHeightmap_LOD2_CallsRebuildChunk) {
    const uint64_t kChunkId = 201u;

    // Register the chunk at LOD 0.
    m_sim->registerChunkAtLOD(kChunkId, /*currentLOD=*/0);

    // Register the LOD0 heightmap: 33*33 = 1089 floats, all 0.0f.
    const int kVertexCount = (kTerrainLOD0GridSize + 1) * (kTerrainLOD0GridSize + 1);
    m_sim->registerChunkHeightmap(kChunkId, std::vector<float>(kVertexCount, 0.0f));

    // Register the chunk world origin.
    m_sim->registerChunkPosition(kChunkId, /*worldOriginX=*/0.0f, /*worldOriginZ=*/0.0f);

    // Expect rebuildTerrainChunk() to be called exactly once during update().
    EXPECT_CALL(*m_renderer, rebuildTerrainChunk(::testing::_)).Times(1);

    // Enqueue rebuild to LOD 2 and process it.
    m_sim->enqueueRebuild(kChunkId, /*targetLOD=*/2);
    m_sim->update(0.016f);

    // The chunk's tracked LOD must be updated to 2.
    EXPECT_EQ(m_sim->getChunkLOD(kChunkId), 2)
        << "getChunkLOD() must return 2 after a successful rebuild to LOD 2";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_GetSlopeDegrees_BeforeGenerate_ReturnsZero
//
// Verifies that getSlopeDegrees() returns 0.0f before generate() is called.
// The ITerrainQuery contract specifies a flat-stub return before heightmap
// data is available.
//
// Spec ref: architecture/game-design/terrain-interaction.md §Buildability
//           architecture/testing/testability-architecture.md §ITerrainQuery
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, GetSlopeDegrees_BeforeGenerate_ReturnsZero) {
    // No generate() call — heightmap is empty.
    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees(0, 0), 0.0f)
        << "getSlopeDegrees() must return 0.0f before generate() is called";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_GetSlopeDegrees_AfterFlatGenerate_ReturnsZero
//
// After generate() with FlatRNG (all heights = 0.0f), every tile slope must
// be 0.0f because adjacent vertex heights are equal → gradient = 0 → atan(0) = 0.
//
// Tests both an edge tile (0,0) and an interior tile (5,5) to confirm the slope
// query is consistent across the generated heightmap.
//
// Spec ref: architecture/game-design/terrain-interaction.md §Buildability
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, GetSlopeDegrees_AfterFlatGenerate_ReturnsZero) {
    FlatRNG flatRng;
    m_sim->generate(/*mapTilesX=*/10, /*mapTilesZ=*/10,
                    /*cellSize=*/4.0f, &flatRng, /*maxRetries=*/0);

    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees(0, 0), 0.0f)
        << "getSlopeDegrees(0,0) must be 0.0f on a flat heightmap";
    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees(5, 5), 0.0f)
        << "getSlopeDegrees(5,5) must be 0.0f on a flat heightmap";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_GetSlopeDegrees_OutOfBounds_ReturnsZero
//
// Verifies that getSlopeDegrees() returns 0.0f for out-of-bounds tile indices
// (negative or >= mapTiles dimension).  Out-of-bounds access must not crash
// and must return the flat-stub sentinel value.
//
// Spec ref: architecture/game-design/terrain-interaction.md §Buildability
//           TerrainSystem.h — "Returns 0.0f for out-of-bounds tiles"
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, GetSlopeDegrees_OutOfBounds_ReturnsZero) {
    FlatRNG flatRng;
    // 10×10 tile map — valid tile indices are [0,9] in each axis.
    m_sim->generate(/*mapTilesX=*/10, /*mapTilesZ=*/10,
                    /*cellSize=*/4.0f, &flatRng, /*maxRetries=*/0);

    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees(-1,  0), 0.0f)
        << "getSlopeDegrees(-1, 0) must return 0.0f (out of bounds)";
    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees(10,  0), 0.0f)
        << "getSlopeDegrees(10, 0) must return 0.0f (== mapTilesX, out of bounds)";
    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees( 0, -1), 0.0f)
        << "getSlopeDegrees(0, -1) must return 0.0f (out of bounds)";
    EXPECT_FLOAT_EQ(m_sim->getSlopeDegrees( 0, 10), 0.0f)
        << "getSlopeDegrees(0, 10) must return 0.0f (== mapTilesZ, out of bounds)";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_LOD_SwitchDistance_UnknownLOD_ReturnsZero
//
// Verifies that lodSwitchOutDistance() and lodSwitchInDistance() return 0.0f
// for fromLOD values that are not handled by the implementation (i.e., anything
// other than 0 or 1).  fromLOD=2 (LOD2 cannot transition further) and
// fromLOD=-1 (invalid) must both produce 0.0f.
//
// Spec ref: architecture/graphics-architecture/procedural-terrain.md
//           §LOD hysteresis distances
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, LOD_SwitchDistance_UnknownLOD_ReturnsZero) {
    // fromLOD=2: not a valid transition origin (LOD2 is the coarsest level).
    EXPECT_FLOAT_EQ(TerrainSystem::lodSwitchOutDistance(2), 0.0f)
        << "lodSwitchOutDistance(2) must return 0.0f for an unrecognised fromLOD";
    EXPECT_FLOAT_EQ(TerrainSystem::lodSwitchInDistance(2), 0.0f)
        << "lodSwitchInDistance(2) must return 0.0f for an unrecognised fromLOD";

    // fromLOD=-1: entirely invalid.
    EXPECT_FLOAT_EQ(TerrainSystem::lodSwitchOutDistance(-1), 0.0f)
        << "lodSwitchOutDistance(-1) must return 0.0f for an invalid fromLOD";
    EXPECT_FLOAT_EQ(TerrainSystem::lodSwitchInDistance(-1), 0.0f)
        << "lodSwitchInDistance(-1) must return 0.0f for an invalid fromLOD";
}

// ---------------------------------------------------------------------------
// TerrainSystemTest_FlushPendingRebuilds_WithProgressCallback
//
// Verifies that flushPendingRebuilds() invokes the ITerrainLoadProgress
// callback at least once when rebuilds are present.
//
// Uses a local CountingProgress stub that increments callCount each time
// onChunkRebuilt() is called.  Three distinct chunks (IDs 300–302) are
// enqueued so that at least one callback fires even if the budget runs out
// after the first rebuild.
//
// Spec ref: TerrainSystem.h §flushPendingRebuilds()
//           architecture/graphics-architecture/procedural-terrain.md
//           §ITerrainLoadProgress callback
// ---------------------------------------------------------------------------
TEST_F(TerrainSystemTest, FlushPendingRebuilds_WithProgressCallback) {
    // Local progress stub — counts onChunkRebuilt() invocations.
    struct CountingProgress : public ITerrainLoadProgress {
        int callCount{0};
        void onChunkRebuilt(int /*done*/, int /*total*/) override {
            ++callCount;
        }
    };

    // Register three distinct chunks at LOD 0, supply their heightmaps and origins.
    const int kVertexCount = (kTerrainLOD0GridSize + 1) * (kTerrainLOD0GridSize + 1);
    for (uint64_t id = 300u; id <= 302u; ++id) {
        m_sim->registerChunkAtLOD(id, /*currentLOD=*/0);
        m_sim->registerChunkHeightmap(id, std::vector<float>(kVertexCount, 0.0f));
        m_sim->registerChunkPosition(id, /*worldOriginX=*/0.0f, /*worldOriginZ=*/0.0f);
        m_sim->enqueueRebuild(id, /*targetLOD=*/1);
    }

    ASSERT_EQ(m_sim->pendingRebuildCount(), 3)
        << "All 3 rebuild requests must be queued before flushPendingRebuilds()";

    CountingProgress progress;
    m_sim->flushPendingRebuilds(&progress);

    // At least one callback must have fired (one rebuild completed at minimum).
    EXPECT_GE(progress.callCount, 1)
        << "onChunkRebuilt() must be called at least once during flushPendingRebuilds()";
}
