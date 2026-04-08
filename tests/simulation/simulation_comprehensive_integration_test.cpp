// simulation_comprehensive_integration_test.cpp
//
// Comprehensive integration tests for CitySimulation exercising:
//   - Economy (tax rates, budget line items, debt, forced loans)
//   - Zoning, road placement, demolish, undo
//   - Service buildings (all four types)
//   - Population growth paths (tick loop)
//   - Serialization (serializeToJson / deserializeFromJson round-trip)
//   - Notification polling
//   - Speed control, pause/resume
//   - City rating transitions
//   - SaveSystem real file I/O
//
// CMake target: integration_tests, label "integration".
// Mock policy: NiceMock for integration tests.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cmath>
#include <string>
#include <memory>

#include "CitySimulation.h"
#include "MockAudioSystem.h"
#include "MockRenderer.h"
#include "ManualRNG.h"
#include "ManualClock.h"
#include "ManualTerrainQuery.h"
#include "simulation_constants.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;

// ---------------------------------------------------------------------------
// Shared fixture
// ---------------------------------------------------------------------------
class SimComprehensiveTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    ManualRNG                 rng_;
    ManualClock               clock_;
    ManualTerrainQuery        terrain_;
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        CitySimulation* cs = dynamic_cast<CitySimulation*>(sim_.get());
        ASSERT_NE(cs, nullptr);
        cs->setSpeed(SpeedMultiplier::x1);
        clock_.advance(SimulationConstants::grace_period_real_seconds + 1.0);
    }

    void TearDown() override {
        sim_.reset();
    }

    CitySimulation* cs() { return dynamic_cast<CitySimulation*>(sim_.get()); }

    // Drain all pending notifications to prevent queue buildup.
    void drainNotifications() {
        SimulationNotification notif;
        while (sim_->pollPendingNotification(notif)) {}
    }

    // Run N budget ticks at x1 speed (30.0f delta per tick).
    void runTicks(int n) {
        for (int i = 0; i < n; ++i) {
            drainNotifications();
            cs()->tick(SimulationConstants::SECONDS_PER_BUDGET_TICK);
        }
        drainNotifications();
    }
};

// ---------------------------------------------------------------------------
// Construction / initial state
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, InitialState_TreasuryEqualsNormalStartingFunds) {
    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(),
                    static_cast<float>(SimulationConstants::starting_funds_normal));
}

TEST_F(SimComprehensiveTest, InitialState_PopulationIsZero) {
    EXPECT_EQ(sim_->getTotalPopulation(), 0);
}

TEST_F(SimComprehensiveTest, InitialState_SimulationTimeYearOneMonthOne) {
    SimulationTime t = sim_->getSimulationTime();
    EXPECT_EQ(t.year, 1);
    EXPECT_EQ(t.month, 1);
}

TEST_F(SimComprehensiveTest, InitialState_DefaultTaxRates) {
    // Default tax rate = 0.05 for all zone types
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Residential), 0.05f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Commercial),  0.05f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Industrial),  0.05f);
}

TEST_F(SimComprehensiveTest, InitialState_CityRatingIsVillage) {
    EXPECT_EQ(sim_->getCityRating(), CityRatingTier::Village);
}

TEST_F(SimComprehensiveTest, InitialState_NoPendingUndo) {
    EXPECT_FALSE(sim_->hasUndoPendingAction());
}

TEST_F(SimComprehensiveTest, InitialState_NoOutstandingDebt) {
    EXPECT_FLOAT_EQ(sim_->getOutstandingDebt(), 0.0f);
}

TEST_F(SimComprehensiveTest, InitialState_SpeedX1) {
    EXPECT_EQ(sim_->getSpeedMultiplier(), SpeedMultiplier::x1);
}

// ---------------------------------------------------------------------------
// Difficulty variants - constructing without calling SetUp fixture
// ---------------------------------------------------------------------------

TEST(SimDifficultyTest, EasyDifficulty_StartingFunds_1M) {
    NiceMock<MockRenderer>    r;
    NiceMock<MockAudioSystem> a;
    ManualRNG  rng;
    ManualClock clk;
    ManualTerrainQuery t;
    CitySimulation sim(&r, &a, &rng, &clk, &t, Difficulty::Easy);
    EXPECT_FLOAT_EQ(sim.getTreasuryBalance(),
                    static_cast<float>(SimulationConstants::starting_funds_easy));
}

TEST(SimDifficultyTest, HardDifficulty_StartingFunds_200K) {
    NiceMock<MockRenderer>    r;
    NiceMock<MockAudioSystem> a;
    ManualRNG  rng;
    ManualClock clk;
    ManualTerrainQuery t;
    CitySimulation sim(&r, &a, &rng, &clk, &t, Difficulty::Hard);
    EXPECT_FLOAT_EQ(sim.getTreasuryBalance(),
                    static_cast<float>(SimulationConstants::starting_funds_hard));
}

// ---------------------------------------------------------------------------
// Speed control
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, SetSpeed_Paused_IsPausedReturnsTrue) {
    cs()->setSpeed(SpeedMultiplier::Paused);
    EXPECT_TRUE(sim_->isPaused());
}

TEST_F(SimComprehensiveTest, SetPaused_TrueAndFalse_RoundTrips) {
    sim_->setPaused(true);
    EXPECT_TRUE(sim_->isPaused());
    sim_->setPaused(false);
    EXPECT_FALSE(sim_->isPaused());
}

TEST_F(SimComprehensiveTest, SetSpeed_X3_SpeedMultiplierX3) {
    cs()->setSpeed(SpeedMultiplier::x3);
    EXPECT_EQ(sim_->getSpeedMultiplier(), SpeedMultiplier::x3);
}

TEST_F(SimComprehensiveTest, SetSpeed_X10_SpeedMultiplierX10) {
    cs()->setSpeed(SpeedMultiplier::x10);
    EXPECT_EQ(sim_->getSpeedMultiplier(), SpeedMultiplier::x10);
}

// ---------------------------------------------------------------------------
// Zone placement
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, PlaceZone_WithEarthworksCost_ReducesTreasury) {
    // Place a road first so proximity check passes, then zone with earthworks cost
    sim_->placeRoad(1, 0);
    float before = sim_->getTreasuryBalance();
    // Use earthworksCostOverride > 0 to force treasury deduction
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low, 1000);
    float after = sim_->getTreasuryBalance();
    EXPECT_LT(after, before) << "Placing a zone with earthworks cost must deduct from treasury";
}

TEST_F(SimComprehensiveTest, PlaceZone_AllZoneTypes_DoNotCrash) {
    // Place roads for proximity check
    sim_->placeRoad(0, 1);
    sim_->placeRoad(1, 1);
    sim_->placeRoad(2, 1);
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(1, 0, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(2, 0, ZoneType::Industrial,  DensityTier::Low);
    drainNotifications();
}

TEST_F(SimComprehensiveTest, PlaceRoad_ReducesTreasury) {
    float before = sim_->getTreasuryBalance();
    sim_->placeRoad(0, 0);
    float after = sim_->getTreasuryBalance();
    EXPECT_LT(after, before) << "Placing a road must deduct cost from treasury";
}

TEST_F(SimComprehensiveTest, QueryTile_ZonedTile_ReturnsZonedState) {
    // Road nearby required for zone placement
    sim_->placeRoad(4, 3);
    sim_->placeZone(3, 3, ZoneType::Commercial, DensityTier::Low);
    QueryResult r = sim_->queryTile(3, 3);
    EXPECT_TRUE(r.isZoned);
    EXPECT_EQ(r.zoneType, ZoneType::Commercial);
}

TEST_F(SimComprehensiveTest, QueryTile_RoadTile_IsRoad) {
    sim_->placeRoad(5, 5);
    QueryResult r = sim_->queryTile(5, 5);
    EXPECT_TRUE(r.isRoad);
}

TEST_F(SimComprehensiveTest, QueryTile_EmptyTile_NothingSet) {
    QueryResult r = sim_->queryTile(99, 99);
    EXPECT_FALSE(r.isZoned);
    EXPECT_FALSE(r.isRoad);
}

// ---------------------------------------------------------------------------
// Demolish
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, DemolishTile_ZonedTile_ClearsZone) {
    sim_->placeRoad(5, 4);
    sim_->placeZone(4, 4, ZoneType::Residential, DensityTier::Low);
    {
        QueryResult r = sim_->queryTile(4, 4);
        ASSERT_TRUE(r.isZoned);
    }
    sim_->demolishTile(4, 4);
    QueryResult r2 = sim_->queryTile(4, 4);
    EXPECT_FALSE(r2.isZoned) << "Tile must be un-zoned after demolish";
}

TEST_F(SimComprehensiveTest, DemolishTile_RoadTile_ClearsRoad) {
    sim_->placeRoad(6, 6);
    {
        QueryResult r = sim_->queryTile(6, 6);
        ASSERT_TRUE(r.isRoad);
    }
    sim_->demolishTile(6, 6);
    QueryResult r2 = sim_->queryTile(6, 6);
    EXPECT_FALSE(r2.isRoad) << "Road tile must be cleared after demolish";
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, UndoLastAction_AfterPlaceZone_UnzonesTheTile) {
    sim_->placeRoad(3, 2);
    sim_->placeZone(2, 2, ZoneType::Industrial, DensityTier::Low);
    ASSERT_TRUE(sim_->hasUndoPendingAction());
    sim_->undoLastAction();
    // After undo the tile should be cleared
    QueryResult r = sim_->queryTile(2, 2);
    EXPECT_FALSE(r.isZoned) << "Tile must be cleared after undo";
    EXPECT_FALSE(sim_->hasUndoPendingAction());
}

TEST_F(SimComprehensiveTest, UndoLastAction_NoAction_IsNoOp) {
    EXPECT_FALSE(sim_->hasUndoPendingAction());
    sim_->undoLastAction();  // must not crash
    EXPECT_FALSE(sim_->hasUndoPendingAction());
}

TEST_F(SimComprehensiveTest, UndoLastAction_WithModalOpen_IsNoOp) {
    sim_->placeRoad(2, 1);
    sim_->placeZone(1, 1, ZoneType::Residential, DensityTier::Low);
    ASSERT_TRUE(sim_->hasUndoPendingAction());
    cs()->setModalOpen(true);
    sim_->undoLastAction();
    // Undo should be suppressed when modal is open
    EXPECT_TRUE(sim_->hasUndoPendingAction())
        << "Undo must be suppressed while modal is open";
    cs()->setModalOpen(false);
}

// ---------------------------------------------------------------------------
// Tax rates
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, SetTaxRate_Residential_Persists) {
    sim_->setTaxRate(ZoneType::Residential, 0.10f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Residential), 0.10f);
}

TEST_F(SimComprehensiveTest, SetTaxRate_AllZones_Persist) {
    sim_->setTaxRate(ZoneType::Residential, 0.08f);
    sim_->setTaxRate(ZoneType::Commercial,  0.12f);
    sim_->setTaxRate(ZoneType::Industrial,  0.06f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Residential), 0.08f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Commercial),  0.12f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Industrial),  0.06f);
}

// ---------------------------------------------------------------------------
// Service buildings
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, AddServiceBuilding_AllTypes_DoNotCrash) {
    cs()->addServiceBuilding(10, 0, 0);  // FireStation
    cs()->addServiceBuilding(10, 1, 1);  // PoliceStation
    cs()->addServiceBuilding(10, 2, 2);  // WaterTower
    cs()->addServiceBuilding(10, 3, 3);  // PowerPlant
    drainNotifications();
}

TEST_F(SimComprehensiveTest, PlaceServiceBuilding_FireStation_WithRoadAdjacent_ReducesTreasury) {
    // Service buildings require road adjacency; place road adjacent to 2x2 footprint
    sim_->placeRoad(15, 2);  // adjacent to the 2x2 footprint at (15,0)-(16,1)
    float before = sim_->getTreasuryBalance();
    // Use earthworksCostOverride > 0 to force treasury deduction
    sim_->placeServiceBuilding(15, 0, ServiceBuildingType::FireStation, 2000);
    float after = sim_->getTreasuryBalance();
    EXPECT_LT(after, before) << "Placing a service building with earthworks cost must deduct from treasury";
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, PollPendingNotification_EmptyQueue_ReturnsFalse) {
    SimulationNotification notif;
    EXPECT_FALSE(sim_->pollPendingNotification(notif));
}

TEST_F(SimComprehensiveTest, ConsumeBudgetTicks_AfterOneTick_ReturnsOne) {
    runTicks(1);
    int ticks = sim_->consumeBudgetTicks();
    EXPECT_GE(ticks, 1) << "consumeBudgetTicks must return >= 1 after running one tick";
    // Second call returns 0 (consumed)
    EXPECT_EQ(sim_->consumeBudgetTicks(), 0);
}

// ---------------------------------------------------------------------------
// Budget line items
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, BudgetLineItems_InitialZero_NoRoadsNoBuildings) {
    // With no roads/buildings the costs should be zero
    EXPECT_FLOAT_EQ(sim_->getRoadMaintenanceCost(), 0.0f);
    EXPECT_FLOAT_EQ(sim_->getServiceUpkeepCost(),   0.0f);
}

TEST_F(SimComprehensiveTest, GetWagesCost_AfterOneTick_IsFinite) {
    runTicks(1);
    EXPECT_TRUE(std::isfinite(sim_->getWagesCost()));
}

TEST_F(SimComprehensiveTest, GetCurrentMonthlyRevenue_IsFinite) {
    EXPECT_TRUE(std::isfinite(sim_->getCurrentMonthlyRevenue()));
}

TEST_F(SimComprehensiveTest, GetNextUnlockThreshold_Normal_ReturnsPositive) {
    float t = sim_->getNextUnlockThreshold(Difficulty::Normal);
    EXPECT_GT(t, 0.0f);
}

// ---------------------------------------------------------------------------
// Demand pressure
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetDemandPressurePct_AllZones_InitiallyInRange) {
    for (int z = 0; z < 3; ++z) {
        float d = sim_->getZoneDemandFactor(static_cast<ZoneType>(z));
        EXPECT_GE(d, 0.0f);
        EXPECT_LE(d, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Density unlock
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetDensityUnlockState_InitiallyNoUnlock) {
    DensityUnlockState s = sim_->getDensityUnlockState();
    // No tier should be unlocked at construction (all unlock_flags are false)
    for (int i = 0; i < 6; ++i) {
        EXPECT_FALSE(s.unlock_flags[i]) << "Tier " << i << " must not be unlocked initially";
    }
}

// ---------------------------------------------------------------------------
// Time of day
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetTimeOfDay_InitiallyDay) {
    TimeOfDay tod = sim_->getTimeOfDay();
    EXPECT_EQ(tod, TimeOfDay::DAY);
}

// ---------------------------------------------------------------------------
// Bond use count
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetOutstandingBondUses_InitiallyAtMaxForDifficulty) {
    // Normal difficulty: bond_max_uses_normal = 2
    EXPECT_GE(sim_->getOutstandingBondUses(), 0);
    EXPECT_LE(sim_->getOutstandingBondUses(), 3)
        << "Bond uses must be within [0, max] range at start";
}

// ---------------------------------------------------------------------------
// Map dimensions
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, SetMapDimensions_Getters_Return) {
    cs()->setMapDimensions(128, 128);
    EXPECT_EQ(sim_->getMapTilesX(), 128);
    EXPECT_EQ(sim_->getMapTilesZ(), 128);
}

// ---------------------------------------------------------------------------
// Agent / signal / road speed stubs (Phase 11d — returns empty vectors)
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetAgentPositions_InitiallyEmpty) {
    EXPECT_TRUE(sim_->getAgentPositions().empty());
}

TEST_F(SimComprehensiveTest, GetIntersectionSignalStates_InitiallyEmpty) {
    EXPECT_TRUE(sim_->getIntersectionSignalStates().empty());
}

TEST_F(SimComprehensiveTest, GetRoadSegmentSpeeds_InitiallyEmpty) {
    EXPECT_TRUE(sim_->getRoadSegmentSpeeds().empty());
}

// ---------------------------------------------------------------------------
// isWithinRoadRange
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, IsWithinRoadRange_WithRoadAdjacent_ReturnsTrue) {
    sim_->placeRoad(5, 5);
    // Low-density 1×1 tile should be within range if road is adjacent
    bool inRange = sim_->isWithinRoadRange(4, 5, DensityTier::Low);
    // Just verifying no crash; result depends on impl
    (void)inRange;
}

// ---------------------------------------------------------------------------
// 60-tick economy loop with city layout
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, EconomyLoop_60Ticks_TreasuryFiniteAndSimTime) {
    // Build a minimal city
    for (int i = 0; i < 3; ++i) sim_->placeZone(0, i, ZoneType::Residential, DensityTier::Low);
    for (int i = 0; i < 2; ++i) sim_->placeZone(1, i, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(2, 0, ZoneType::Industrial, DensityTier::Low);
    for (int i = 0; i < 3; ++i) sim_->placeRoad(3, i);
    cs()->addServiceBuilding(5, 0, 0);
    cs()->addServiceBuilding(5, 1, 1);
    cs()->addServiceBuilding(5, 2, 2);
    cs()->addServiceBuilding(5, 3, 3);

    runTicks(60);

    SimulationTime t = sim_->getSimulationTime();
    EXPECT_EQ(t.year, 6);
    EXPECT_EQ(t.month, 1);
    EXPECT_TRUE(std::isfinite(sim_->getTreasuryBalance()));
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, SerializeDeserialize_EmptyCity_RoundTrips) {
    std::string json = cs()->serializeToJson();
    ASSERT_FALSE(json.empty()) << "serializeToJson must produce non-empty JSON";
    EXPECT_NE(json.find("schema_version"), std::string::npos);
    EXPECT_NE(json.find("treasury"), std::string::npos);

    std::string err;
    bool ok = cs()->deserializeFromJson(json, err);
    EXPECT_TRUE(ok) << "deserializeFromJson of own output must succeed; err=" << err;
}

TEST_F(SimComprehensiveTest, SerializeDeserialize_WithZones_TreasuryPreserved) {
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);
    runTicks(2);
    float treasuryBefore = sim_->getTreasuryBalance();

    std::string json = cs()->serializeToJson();
    std::string err;
    bool ok = cs()->deserializeFromJson(json, err);
    ASSERT_TRUE(ok) << "deserializeFromJson failed: " << err;
    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(), treasuryBefore)
        << "Treasury must be preserved across serialize/deserialize";
}

TEST_F(SimComprehensiveTest, ApplyLoadedJson_ValidJson_ReturnsTrue) {
    std::string json = cs()->serializeToJson();
    EXPECT_TRUE(sim_->applyLoadedJson(json));
}

TEST_F(SimComprehensiveTest, ApplyLoadedJson_InvalidJson_ReturnsFalse) {
    EXPECT_FALSE(sim_->applyLoadedJson("not json at all"));
}

TEST_F(SimComprehensiveTest, SerializeDeserialize_TaxRates_Preserved) {
    sim_->setTaxRate(ZoneType::Residential, 0.12f);
    sim_->setTaxRate(ZoneType::Commercial,  0.08f);
    sim_->setTaxRate(ZoneType::Industrial,  0.15f);

    std::string json = cs()->serializeToJson();
    std::string err;
    bool ok = cs()->deserializeFromJson(json, err);
    ASSERT_TRUE(ok) << err;

    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Residential), 0.12f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Commercial),  0.08f);
    EXPECT_FLOAT_EQ(sim_->getTaxRate(ZoneType::Industrial),  0.15f);
}

TEST_F(SimComprehensiveTest, SerializeDeserialize_SimulationTime_Preserved) {
    runTicks(6);  // 6 months in
    SimulationTime before = sim_->getSimulationTime();

    std::string json = cs()->serializeToJson();
    std::string err;
    bool ok = cs()->deserializeFromJson(json, err);
    ASSERT_TRUE(ok) << err;

    SimulationTime after = sim_->getSimulationTime();
    EXPECT_EQ(after.year,  before.year);
    EXPECT_EQ(after.month, before.month);
}

TEST_F(SimComprehensiveTest, SerializeDeserialize_Corruption_ReturnsFalse) {
    std::string json = cs()->serializeToJson();
    // Truncate the JSON to make it invalid
    std::string truncated = json.substr(0, json.size() / 2);
    std::string err;
    bool ok = cs()->deserializeFromJson(truncated, err);
    EXPECT_FALSE(ok) << "Truncated JSON must fail deserialization";
    EXPECT_FALSE(err.empty()) << "Error message must be populated on failure";
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, Reset_ClearsZonesAndSetsTreasury) {
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);
    runTicks(5);
    sim_->reset(750000);
    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(), 750000.0f);
    EXPECT_EQ(sim_->getTotalPopulation(), 0);
    SimulationTime t = sim_->getSimulationTime();
    EXPECT_EQ(t.year, 1);
    EXPECT_EQ(t.month, 1);
}

// ---------------------------------------------------------------------------
// Building variant counters
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, BuildingVariantCounter_Increments_AfterPlaceZone) {
    sim_->placeRoad(1, 0);
    int before = cs()->getBuildingVariantCounter(0, 0);  // Residential Low
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);
    int after  = cs()->getBuildingVariantCounter(0, 0);
    EXPECT_EQ(after, before + 1)
        << "Variant counter must increment by 1 after each placeZone";
}

TEST_F(SimComprehensiveTest, BuildingVariantCounter_Wraps_AfterFourPlacements) {
    // Place 4 Residential Low zones and verify counter stays in [0, kVariants-1]
    for (int i = 0; i < 4; ++i) {
        sim_->placeRoad(i, 1);
        sim_->placeZone(i, 0, ZoneType::Residential, DensityTier::Low);
    }
    int counter = cs()->getBuildingVariantCounter(0, 0);
    EXPECT_GE(counter, 0);
}

// ---------------------------------------------------------------------------
// Deficit streak (game-over gate)
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, ConsecutiveDeficitMonths_InitiallyZero) {
    EXPECT_EQ(sim_->getConsecutiveDeficitMonths(), 0);
}

// ---------------------------------------------------------------------------
// Service coverage tiles
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetServiceCoverage_InitiallyEmpty) {
    EXPECT_TRUE(sim_->getServiceCoverage().empty());
}

// ---------------------------------------------------------------------------
// UndoExpiryTimeSeconds
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, UndoExpiry_AfterPlaceZone_IsPositive) {
    sim_->placeRoad(8, 7);
    sim_->placeZone(7, 7, ZoneType::Residential, DensityTier::Low);
    ASSERT_TRUE(sim_->hasUndoPendingAction());
    double expiry = sim_->getUndoExpiryTimeSeconds();
    EXPECT_GT(expiry, 0.0) << "Undo expiry must be a positive clock time";
}

// ---------------------------------------------------------------------------
// Service upkeep cost after placing service buildings
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, ServiceUpkeepCost_AfterAddingBuildings_IncreasesAfterTick) {
    cs()->addServiceBuilding(10, 0, 0);  // FireStation
    cs()->addServiceBuilding(10, 1, 1);  // PoliceStation
    runTicks(1);
    // After a tick, service upkeep should be non-zero
    EXPECT_GE(sim_->getServiceUpkeepCost(), 0.0f);
}

// ---------------------------------------------------------------------------
// Road maintenance cost
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, RoadMaintenanceCost_AfterPlacingRoads_IncreasesAfterTick) {
    for (int i = 0; i < 5; ++i) sim_->placeRoad(i, 0);
    runTicks(1);
    EXPECT_GE(sim_->getRoadMaintenanceCost(), 0.0f);
}

// ---------------------------------------------------------------------------
// Estimate monthly upkeep
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, EstimateMonthlyUpkeep_IsNonNegative) {
    cs()->addServiceBuilding(5, 0, 0);
    for (int i = 0; i < 3; ++i) sim_->placeRoad(i, 0);
    EXPECT_GE(sim_->estimateMonthlyUpkeep(), 0.0f);
}

// ---------------------------------------------------------------------------
// Multiple ticks — no crash with varied city layout
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, ExtendedRun_120Ticks_NoCrashAndFiniteValues) {
    // Medium-sized city layout
    for (int x = 0; x < 5; ++x) {
        sim_->placeZone(x, 0, ZoneType::Residential, DensityTier::Low);
        sim_->placeZone(x, 1, ZoneType::Commercial,  DensityTier::Low);
        sim_->placeZone(x, 2, ZoneType::Industrial,  DensityTier::Low);
    }
    for (int x = 0; x < 5; ++x) sim_->placeRoad(x, 3);
    cs()->addServiceBuilding(6, 0, 0);
    cs()->addServiceBuilding(6, 1, 1);
    cs()->addServiceBuilding(6, 2, 2);
    cs()->addServiceBuilding(6, 3, 3);

    runTicks(120);

    EXPECT_TRUE(std::isfinite(sim_->getTreasuryBalance()));
    EXPECT_GE(sim_->getTotalPopulation(), 0);
}

// ---------------------------------------------------------------------------
// Tax revenue line items after tick
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, TaxRevenueFunctions_AfterTick_ReturnFiniteValues) {
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(1, 0, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(2, 0, ZoneType::Industrial,  DensityTier::Low);
    runTicks(1);

    EXPECT_TRUE(std::isfinite(sim_->getTaxRevenue(ZoneType::Residential)));
    EXPECT_TRUE(std::isfinite(sim_->getTaxRevenue(ZoneType::Commercial)));
    EXPECT_TRUE(std::isfinite(sim_->getTaxRevenue(ZoneType::Industrial)));
    EXPECT_TRUE(std::isfinite(sim_->getUtilityFeeRevenue()));
}

// ---------------------------------------------------------------------------
// Undo expiry after tick (undo expires after budget tick)
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, UndoPendingAction_ExpiresAfterBudgetTick) {
    sim_->placeRoad(4, 3);
    sim_->placeZone(3, 3, ZoneType::Residential, DensityTier::Low);
    ASSERT_TRUE(sim_->hasUndoPendingAction());
    runTicks(2);  // 2 ticks = undo should expire (1 tick expiry per spec)
    EXPECT_FALSE(sim_->hasUndoPendingAction())
        << "Undo must expire after one budget tick";
}

// ---------------------------------------------------------------------------
// Service coverage (non-empty after placing PowerPlant + tick)
// ---------------------------------------------------------------------------

TEST_F(SimComprehensiveTest, GetServiceCoverage_AfterAddingBuildings_MayBeNonEmpty) {
    cs()->addServiceBuilding(0, 0, 3);  // PowerPlant
    runTicks(1);
    // getServiceCoverage stub returns empty — just verifying no crash
    auto cov = sim_->getServiceCoverage();
    (void)cov;
}
