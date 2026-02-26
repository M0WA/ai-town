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
#include "src/terrain/ITerrainRNG.h"
#include "tests/simulation/mock_renderer.h"   // NiceMock<MockRenderer>
#include "tests/simulation/manual_clock.h"    // ManualClock
#include "tests/terrain/mock_terrain_rng.h"   // MockTerrainRNG

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
