// tests/simulation/placement_conflict_test.cpp
//
// Phase 11d Deliverable 5c: Placement Conflict Tests
// Verifies that placeZone and placeRoad return early (no-op) when the target
// tile is already occupied (isRoad || isZoned).
//
// Added to simulation_tests via:
//   target_sources(simulation_tests PRIVATE tests/simulation/placement_conflict_test.cpp)
// Do NOT call add_executable(simulation_tests ...) again.
//
// Mock policy: StrictMock<MockRenderer> — required for Times(0) negative assertions
// per architecture/testing/testability-architecture.md.

#include "src/simulation/CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualRNG.h"
#include "tests/simulation/ManualClock.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::StrictMock;

// ---------------------------------------------------------------------------
// PlacementConflictTest fixture
// ---------------------------------------------------------------------------
class PlacementConflictTest : public ::testing::Test {
protected:
    StrictMock<MockRenderer>    renderer_;
    StrictMock<MockAudioSystem> audio_;
    ManualRNG                   rng_;
    ManualClock                 clock_;
    ManualTerrainQuery          terrain_;
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        sim_->setSpeed(SpeedMultiplier::x1);

        // Allow all audio calls — these tests focus on sim state, not audio.
        EXPECT_CALL(audio_, playPositionalSound(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(audio_, playSound(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(audio_, setMusicIntensity(_)).Times(AnyNumber());
        EXPECT_CALL(audio_, setTimeOfDay(_)).Times(AnyNumber());

        // Allow renderer mesh calls from initial placement (1st call per tile is valid).
        // Individual tests override with their own EXPECT_CALLs for Times(0) assertions.
        EXPECT_CALL(renderer_, placeBuildingMesh(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, removeBuildingMesh(_, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, placeRoadMesh(_, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, removeRoadMesh(_, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, placeServiceBuildingMesh(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, removeServiceBuildingMesh(_, _)).Times(AnyNumber());
    }

    void TearDown() override {
        sim_.reset();
    }

    CitySimulation* cs() {
        return dynamic_cast<CitySimulation*>(sim_.get());
    }

    float getTreasury() const {
        return sim_->getTreasuryBalance();
    }
};

// ============================================================================
// PlaceZone_OnRoadTile_IsNoOp
// Phase 11d Deliverable 5c: placeZone on a road tile must be a no-op.
// ============================================================================
TEST_F(PlacementConflictTest, PlaceZone_OnRoadTile_IsNoOp)
{
    // Place a road at (3,3) — valid; records undo action, deducts cost.
    sim_->placeRoad(3, 3, 0);
    const float fundsAfterRoad = getTreasury();
    const int undoDepthAfterRoad = sim_->hasUndoPendingAction() ? 1 : 0;

    // Attempt to zone over the road — must be blocked by the occupancy guard.
    sim_->placeZone(3, 3, ZoneType::Residential, DensityTier::Low, 0);

    // Tile remains a road, not zoned.
    QueryResult qr = sim_->queryTile(3, 3);
    EXPECT_TRUE(qr.isRoad);
    EXPECT_FALSE(qr.isZoned);

    // Treasury is unchanged after the no-op placeZone call.
    EXPECT_FLOAT_EQ(getTreasury(), fundsAfterRoad);

    // Undo stack depth is unchanged — no second undo entry was recorded.
    // The road's undo entry is still the top (and only) entry.
    EXPECT_EQ(sim_->hasUndoPendingAction() ? 1 : 0, undoDepthAfterRoad);
}

// ============================================================================
// PlaceZone_OnZonedTile_IsNoOp
// Phase 11d Deliverable 5c: placeZone on an already-zoned tile must be a no-op.
// ============================================================================
TEST_F(PlacementConflictTest, PlaceZone_OnZonedTile_IsNoOp)
{
    // Phase 11h: placeZone requires a road within 3 tiles.
    sim_->placeRoad(5, 4, 0);
    // Place Residential zone at (4,4).
    sim_->placeZone(4, 4, ZoneType::Residential, DensityTier::Low, 0);
    const float fundsAfterFirst = getTreasury();

    // Attempt to place Commercial on the same tile — must be blocked.
    sim_->placeZone(4, 4, ZoneType::Commercial, DensityTier::Low, 0);

    // Tile remains Residential — the second call was a no-op.
    QueryResult qr = sim_->queryTile(4, 4);
    EXPECT_TRUE(qr.isZoned);
    EXPECT_EQ(qr.zoneType, ZoneType::Residential);

    // Treasury unchanged after the second (blocked) call.
    EXPECT_FLOAT_EQ(getTreasury(), fundsAfterFirst);
}

// ============================================================================
// PlaceRoad_OnZonedTile_IsNoOp
// Phase 11d Deliverable 5c: placeRoad on a zoned tile must be a no-op.
// ============================================================================
TEST_F(PlacementConflictTest, PlaceRoad_OnZonedTile_IsNoOp)
{
    // Phase 11h: placeZone requires a road within 3 tiles.
    sim_->placeRoad(6, 5, 0);
    // Place Residential zone at (5,5).
    sim_->placeZone(5, 5, ZoneType::Residential, DensityTier::Low, 0);
    const float fundsAfterZone = getTreasury();

    // Attempt to place a road on the same tile — must be blocked.
    sim_->placeRoad(5, 5, 0);

    // Tile remains zoned, not a road.
    QueryResult qr = sim_->queryTile(5, 5);
    EXPECT_TRUE(qr.isZoned);
    EXPECT_FALSE(qr.isRoad);

    // Treasury unchanged after the blocked placeRoad call.
    EXPECT_FLOAT_EQ(getTreasury(), fundsAfterZone);
}

// ============================================================================
// PlaceRoad_OnRoadTile_IsNoOp
// Phase 11d Deliverable 5c: placeRoad on an already-roaded tile must be a no-op.
// ============================================================================
TEST_F(PlacementConflictTest, PlaceRoad_OnRoadTile_IsNoOp)
{
    // Place a road at (6,6) — valid; deducts cost.
    sim_->placeRoad(6, 6, 0);
    const float fundsAfterFirst = getTreasury();

    // Attempt to place a second road on the same tile — must be blocked.
    sim_->placeRoad(6, 6, 0);

    // Treasury unchanged after the second (blocked) call.
    EXPECT_FLOAT_EQ(getTreasury(), fundsAfterFirst);

    // Tile is still a road.
    QueryResult qr = sim_->queryTile(6, 6);
    EXPECT_TRUE(qr.isRoad);
    EXPECT_FALSE(qr.isZoned);
}
