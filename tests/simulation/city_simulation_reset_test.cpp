// tests/simulation/city_simulation_reset_test.cpp
//
// Phase 11l coverage tests — CitySimulation::reset(), applyLoadedJson(),
// isWithinRoadRange(), and SimulationConstants::startingFundsForDifficulty().
//
// These methods were added in Phase 11l and were previously uncovered,
// causing the 85% per-file floor gate to fail.
//
// Fixture: NiceMock for renderer_ + audio_ to suppress incidental callbacks
// from placeRoad() / placeZone() during state setup.
// TearDown() resets sim_ before mock destructors run.
//
// Added to simulation_tests via:
//   target_sources(simulation_tests PRIVATE tests/simulation/city_simulation_reset_test.cpp)

#include "src/simulation/CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "src/simulation/simulation_constants.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualRNG.h"
#include "tests/simulation/ManualClock.h"
#include "tests/simulation/ManualTerrainQuery.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>

using ::testing::_;
using ::testing::AtLeast;
using ::testing::AnyNumber;
using ::testing::NiceMock;

// ---------------------------------------------------------------------------
// ResetTest fixture
// ---------------------------------------------------------------------------
class ResetTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    ManualRNG                 rng_;
    ManualClock               clock_;
    ManualTerrainQuery        terrain_;

    // sim_ declared LAST — destroyed first.
    std::unique_ptr<CitySimulation> sim_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        sim_->setSpeed(SpeedMultiplier::x1);
    }

    void TearDown() override {
        sim_.reset();
    }

    // Helper: drain and discard all pending notifications.
    void drainNotifications() {
        SimulationNotification notif{};
        while (sim_->pollPendingNotification(notif)) {}
    }
};

// ---------------------------------------------------------------------------
// TEST R-1: CitySimulation_Reset_TreasurySetToStartingFunds
//
// After reset(N), getTreasuryBalance() reports N (as float).
// Verifies that m_treasury is overwritten regardless of prior state.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_TreasurySetToStartingFunds)
{
    // Advance state slightly so treasury is not at the constructor default.
    const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
    clock_.advance(dt);
    sim_->tick(dt);

    const int64_t newFunds = 750000;
    sim_->reset(newFunds);

    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(), static_cast<float>(newFunds))
        << "reset() must set treasury to startingFunds";
}

// ---------------------------------------------------------------------------
// TEST R-2: CitySimulation_Reset_ClearsZonedTilesAndPopulation
//
// After placing a zone + road and calling reset(), all tiles are cleared:
//   - getTotalPopulation() == 0
//   - queryTile() for the previously zoned tile returns isZoned == false
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_ClearsZonedTilesAndPopulation)
{
    sim_->placeRoad(0, 0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low, 0);
    drainNotifications();

    // Confirm zone is present before reset.
    ASSERT_TRUE(sim_->queryTile(1, 0).isZoned);

    sim_->reset(SimulationConstants::starting_funds_normal);

    EXPECT_EQ(sim_->getTotalPopulation(), 0)
        << "reset() must clear all population";
    EXPECT_FALSE(sim_->queryTile(1, 0).isZoned)
        << "reset() must clear m_tiles — previously zoned tile must be absent";
}

// ---------------------------------------------------------------------------
// TEST R-3: CitySimulation_Reset_SpeedRestoredToDefault
//
// After setPaused(true) and setSpeed(x10), reset() restores the speed to
// kDefaultSimSpeed (x3) and clears the pause state.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_SpeedRestoredToDefault)
{
    sim_->setPaused(true);
    sim_->setSpeed(SpeedMultiplier::x10);

    sim_->reset(SimulationConstants::starting_funds_normal);

    EXPECT_EQ(sim_->getSpeedMultiplier(), kDefaultSimSpeed)
        << "reset() must restore speed to kDefaultSimSpeed (x3)";
    EXPECT_FALSE(sim_->isPaused())
        << "reset() must clear the paused state";
}

// ---------------------------------------------------------------------------
// TEST R-4: CitySimulation_Reset_BondUsesResetByDifficulty
//
// reset() resets m_outstandingBondUses to the difficulty-appropriate constant.
// Hard difficulty has fewer bond uses than Normal.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_BondUsesResetByDifficulty)
{
    // Create Hard-difficulty sim to exercise the Hard branch of the switch.
    std::unique_ptr<CitySimulation> hard_sim = std::make_unique<CitySimulation>(
        &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Hard);
    hard_sim->setSpeed(SpeedMultiplier::x1);

    hard_sim->reset(SimulationConstants::starting_funds_hard);

    EXPECT_EQ(hard_sim->getOutstandingBondUses(),
              SimulationConstants::bond_max_uses_hard)
        << "reset() on Hard difficulty must restore bond_max_uses_hard";

    hard_sim.reset();
}

// ---------------------------------------------------------------------------
// TEST R-5: CitySimulation_Reset_EasyDifficultyBondUses
//
// Easy difficulty exercises the Easy branch of the reset() switch statement.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_EasyDifficultyBondUses)
{
    std::unique_ptr<CitySimulation> easy_sim = std::make_unique<CitySimulation>(
        &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Easy);
    easy_sim->setSpeed(SpeedMultiplier::x1);

    easy_sim->reset(SimulationConstants::starting_funds_easy);

    EXPECT_EQ(easy_sim->getOutstandingBondUses(),
              SimulationConstants::bond_max_uses_easy)
        << "reset() on Easy difficulty must restore bond_max_uses_easy";

    easy_sim.reset();
}

// ---------------------------------------------------------------------------
// TEST R-6: CitySimulation_Reset_ReleasesVehicleAudioSources
//
// When reset() is called and traffic vehicles exist, releaseVehicleEnginePair()
// must be called once per vehicle with the vehicle's idleIdx and moveIdx.
// Vehicles spawned by placeRoad() have idleIdx=-1 and moveIdx=-1 (defaults).
//
// Strategy: place 3 roads (kVehicleSpawnInterval=3) so exactly one vehicle is
// spawned. Then call reset() — NiceMock records the call; we verify it happened.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_ReleasesVehicleAudioSources)
{
    // Place roads in a line so vehicle spawning is possible (needs 2 connected
    // road tiles for a destination).
    sim_->placeRoad(0, 0, 0);
    sim_->placeRoad(1, 0, 0);
    sim_->placeRoad(2, 0, 0);  // 3rd road → vehicle spawns
    drainNotifications();

    // Verify at least one vehicle exists before reset.
    ASSERT_FALSE(sim_->getAgentPositions().empty())
        << "Test precondition: 3 roads must spawn at least one traffic vehicle";

    // releaseVehicleEnginePair must be called for each vehicle.
    // Vehicles from placeRoad() have idleIdx=-1, moveIdx=-1.
    EXPECT_CALL(audio_, releaseVehicleEnginePair(-1, -1)).Times(AtLeast(1));

    sim_->reset(SimulationConstants::starting_funds_normal);

    EXPECT_TRUE(sim_->getAgentPositions().empty())
        << "reset() must clear all traffic vehicles";
}

// ---------------------------------------------------------------------------
// TEST R-7: CitySimulation_Reset_ClearsServiceBuildings
//
// After placing a service building and calling reset(), getServiceCoverage()
// returns an empty vector (no tiles to cover in an empty city).
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_Reset_ClearsServiceBuildings)
{
    sim_->placeServiceBuilding(5, 5, ServiceBuildingType::FireStation);
    drainNotifications();

    sim_->reset(SimulationConstants::starting_funds_normal);

    // After reset, no service buildings and no zoned tiles exist.
    // getServiceCoverage() iterates zoned tiles, so coverage must be empty.
    auto coverage = sim_->getServiceCoverage();
    EXPECT_TRUE(coverage.empty())
        << "reset() must clear service buildings; coverage vector must be empty after reset";
}

// ---------------------------------------------------------------------------
// TEST A-0: CitySimulation_ApplyLoadedJson_WithServiceBuildings_RoundTrip
//
// Places a service building so serializeToJson() emits the service_buildings
// array, then round-trips through applyLoadedJson().  This covers the
// serialization loop body (lines ~3246-3252) and the deserialization loop
// (~3636-3673) which are otherwise unreachable from tests that use only
// empty simulations.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_ApplyLoadedJson_WithServiceBuildings_RoundTrip)
{
    // placeServiceBuilding requires at least one cardinal-adjacent road next to
    // its 2×2 footprint.  Road at (2,3) is adjacent to the west side of (3,3).
    sim_->placeRoad(2, 3, 0);
    sim_->placeServiceBuilding(3, 3, ServiceBuildingType::FireStation);
    drainNotifications();

    std::string json = sim_->serializeToJson();
    // "degraded" only appears when at least one service building was serialized.
    ASSERT_NE(json.find("\"degraded\""), std::string::npos)
        << "JSON must contain a serialized service building entry; "
           "check that placeServiceBuilding(3,3) succeeded (road at (2,3) should satisfy adjacency)";

    bool ok = sim_->applyLoadedJson(json);
    EXPECT_TRUE(ok)
        << "applyLoadedJson() must return true for JSON that includes service buildings";
}

// ---------------------------------------------------------------------------
// TEST A-1: CitySimulation_ApplyLoadedJson_ValidJson_ReturnsTrue
//
// applyLoadedJson() delegates to deserializeFromJson(). A JSON string produced
// by serializeToJson() is valid and must return true.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_ApplyLoadedJson_ValidJson_ReturnsTrue)
{
    std::string json = sim_->serializeToJson();
    ASSERT_FALSE(json.empty()) << "serializeToJson() must produce non-empty JSON";

    bool ok = sim_->applyLoadedJson(json);
    EXPECT_TRUE(ok) << "applyLoadedJson() must return true for valid JSON from serializeToJson()";
}

// ---------------------------------------------------------------------------
// TEST A-2: CitySimulation_ApplyLoadedJson_InvalidJson_ReturnsFalse
//
// applyLoadedJson() must return false when the input is not valid JSON.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_ApplyLoadedJson_InvalidJson_ReturnsFalse)
{
    bool ok = sim_->applyLoadedJson("this is not valid json");
    EXPECT_FALSE(ok) << "applyLoadedJson() must return false for invalid JSON";
}

// ---------------------------------------------------------------------------
// TEST I-1: CitySimulation_IsWithinRoadRange_RoadAdjacent_ReturnsTrue
//
// isWithinRoadRange() wraps nearestRoadDistance() <= 3.
// A road at (1,0) is Chebyshev 1 from (0,0) — within range.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_IsWithinRoadRange_RoadAdjacent_ReturnsTrue)
{
    sim_->placeRoad(1, 0, 0);
    drainNotifications();

    bool result = sim_->isWithinRoadRange(0, 0, DensityTier::Low);
    EXPECT_TRUE(result)
        << "isWithinRoadRange() must return true when road is within Chebyshev 3";
}

// ---------------------------------------------------------------------------
// TEST I-2: CitySimulation_IsWithinRoadRange_NoRoad_ReturnsFalse
//
// When no roads exist, isWithinRoadRange() must return false.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_IsWithinRoadRange_NoRoad_ReturnsFalse)
{
    // No roads placed.
    bool result = sim_->isWithinRoadRange(0, 0, DensityTier::Low);
    EXPECT_FALSE(result)
        << "isWithinRoadRange() must return false when no roads are within Chebyshev 3";
}

// ---------------------------------------------------------------------------
// TEST M-1: CitySimulation_GetMapTilesX_ReturnsSetWidth
//
// getMapTilesX() / getMapTilesZ() are inline in CitySimulation.h.
// They are not exercised by any other test and count against the
// per-file coverage floor for CitySimulation.h.
// ---------------------------------------------------------------------------
TEST_F(ResetTest, CitySimulation_GetMapTilesXZ_ReturnsSetDimensions)
{
    sim_->setMapDimensions(20, 30);

    EXPECT_EQ(sim_->getMapTilesX(), 20)
        << "getMapTilesX() must return the width set via setMapDimensions()";
    EXPECT_EQ(sim_->getMapTilesZ(), 30)
        << "getMapTilesZ() must return the depth set via setMapDimensions()";
}

// ---------------------------------------------------------------------------
// TEST S-1: SimulationConstants_StartingFundsForDifficulty_AllVariants
//
// startingFundsForDifficulty() maps Difficulty enum to expected starting fund constants.
// ---------------------------------------------------------------------------
TEST(SimulationConstantsTest, StartingFundsForDifficulty_AllVariants)
{
    EXPECT_EQ(SimulationConstants::startingFundsForDifficulty(Difficulty::Easy),
              SimulationConstants::starting_funds_easy)
        << "Easy must map to starting_funds_easy";
    EXPECT_EQ(SimulationConstants::startingFundsForDifficulty(Difficulty::Normal),
              SimulationConstants::starting_funds_normal)
        << "Normal must map to starting_funds_normal";
    EXPECT_EQ(SimulationConstants::startingFundsForDifficulty(Difficulty::Hard),
              SimulationConstants::starting_funds_hard)
        << "Hard must map to starting_funds_hard";
}
