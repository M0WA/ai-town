// simulation_60tick_test.cpp — Phase 6 exit-criterion integration test.
//
// Verifies that economy, traffic, and zoning simulations can run for 60 budget
// ticks without crashing, and that SimulationTime reflects the correct month
// and year after those ticks elapse.
//
// LABEL: integration  (ctest -L "^integration$"; no display required)
//
// Speed policy: simulation is set to x1 in SetUp() to neutralise the auto-slow
//   mechanism (CitySimulation reduces speed to x1 after the first consecutive
//   deficit month, which fires on tick 1 for a freshly placed city with upkeep
//   but no revenue).  Driving at x1 with tick(30.0f) guarantees exactly one
//   budget tick per call regardless of any subsequent auto-slow transitions.
//
// Time math (see simulation_constants.h + simulation_types.h for authoritative values):
//   SECONDS_PER_BUDGET_TICK = 30.0f simulation seconds per budget tick.
//   Speed x1 → speedValue = 1.0f.
//   Real delta per tick     = 30.0f / 1.0f = 30.0f real seconds at x1 speed.
//   60 budget ticks         = 60 calls to tick(30.0f) at x1 speed.
//
// SimulationTime after 60 budget ticks (starting at year=1, month=1):
//   12 ticks/year × 5 years = 60 ticks → year=6, month=1.
//
// Grace period: grace_period_real_seconds = 120.0 (real wall-clock seconds).
//   ManualClock is advanced by 120.0 s before the tick loop so the forced-loan
//   gate is cleared, preventing forced-loan notifications from being suppressed.
//   This does NOT affect budget-tick accumulation — only the checkAndIssueForcedLoan()
//   guard against issuing loans before the player has had time to respond.
//
// RNG policy: default ManualRNG (non-strict, floatSeq={0.9f}, intSeq={0}).
//   0.9f > service_degradation_probability_per_tick (0.5f) — no service buildings
//   will enter degraded state on any tick, keeping the 60-tick run deterministic.
//   Non-strict wrap-around avoids sequence exhaustion across 60 ticks.
//
// Mock policy: NiceMock — integration tests tolerate unexpected calls
//   (e.g. triggerStinger on rating transitions, setSpeed on auto-slow).

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "CitySimulation.h"
#include "MockAudioSystem.h"
#include "MockRenderer.h"
#include "ManualRNG.h"
#include "ManualClock.h"
#include "ManualTerrainQuery.h"

using ::testing::NiceMock;

// ---------------------------------------------------------------------------
// Simulation60TickTest fixture
// ---------------------------------------------------------------------------
//
// Declaration order is significant: sim_ is declared LAST so it is destroyed
// FIRST (reverse declaration order in C++), preventing use-after-free when
// CitySimulation destructor calls back into mock interfaces.
class Simulation60TickTest : public ::testing::Test {
protected:
    // NiceMock — integration test; unexpected calls are silently ignored.
    NiceMock<MockRenderer>     renderer_;
    NiceMock<MockAudioSystem>  audio_;

    // ManualRNG default ctor: non-strict, floatSeq={0.9f}, intSeq={0}.
    // 0.9f exceeds service_degradation_probability_per_tick (0.5f) so no
    // service building degrades across the 60-tick run.
    ManualRNG          rng_;

    // ManualClock starts at 0.0 s; advanced to clear the 120 s grace period.
    ManualClock        clock_;

    // ManualTerrainQuery: flat (0° slope) by default — no earthworks cost on
    // any tile placement, keeping the treasury balance deterministic.
    ManualTerrainQuery terrain_;

    // sim_ declared LAST — destroyed first, before mock destructors run.
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        // Construct CitySimulation with all six injected dependencies.
        // Difficulty::Normal → starting_funds = 500,000.
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);

        // Downcast once during setup — used throughout the test to reach the
        // test-only addServiceBuilding() seam and to call tick() directly.
        CitySimulation* cs = dynamic_cast<CitySimulation*>(sim_.get());
        ASSERT_NE(cs, nullptr) << "sim_ must be a CitySimulation instance";

        // ---------------------------------------------------------------
        // Place a minimal city layout.
        // ---------------------------------------------------------------
        // Three Residential tiles (column x=0, rows z=0,1,2).
        cs->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);
        cs->placeZone(0, 1, ZoneType::Residential, DensityTier::Low);
        cs->placeZone(0, 2, ZoneType::Residential, DensityTier::Low);

        // Two Commercial tiles (column x=1, rows z=0,1).
        cs->placeZone(1, 0, ZoneType::Commercial, DensityTier::Low);
        cs->placeZone(1, 1, ZoneType::Commercial, DensityTier::Low);

        // One Industrial tile (column x=2, row z=0).
        cs->placeZone(2, 0, ZoneType::Industrial, DensityTier::Low);

        // Roads connecting the zones (column x=3, rows z=0,1,2 + row z=0 connecting).
        // A minimal road network: one road tile adjacent to each zone column.
        cs->placeRoad(3, 0);
        cs->placeRoad(3, 1);
        cs->placeRoad(3, 2);

        // Service buildings — injected via the test-only seam.
        //   serviceTypeInt: 0=FireStation, 1=PoliceStation, 2=WaterTower, 3=PowerPlant
        // Place at x=5 to keep them away from the zone cluster; flat terrain so
        // ITerrainQuery::isBuildableTile() returns true for all tiles.
        cs->addServiceBuilding(5, 0, 0);  // FireStation
        cs->addServiceBuilding(5, 1, 1);  // PoliceStation
        cs->addServiceBuilding(5, 2, 2);  // WaterTower
        cs->addServiceBuilding(5, 3, 3);  // PowerPlant

        // Set speed to x1 so tick(30.0f) fires exactly one budget tick per call.
        // The auto-slow mechanism (CitySimulation.cpp) reduces speed to x1 after
        // the first consecutive deficit month — which fires on tick 1 for a fresh
        // city with service upkeep but no revenue.  Explicitly setting x1 here
        // means auto-slow is a no-op and the per-tick delta is always 30.0f real
        // seconds → 30.0f sim seconds → exactly one SECONDS_PER_BUDGET_TICK per call.
        cs->setSpeed(SpeedMultiplier::x1);

        // Advance ManualClock past the 120 s grace period.
        // This clears the checkAndIssueForcedLoan() real-time gate so that any
        // forced-loan checks during the tick loop operate normally (rather than
        // being suppressed for the first 120 s of wall time).
        clock_.advance(SimulationConstants::grace_period_real_seconds + 1.0);
    }

    void TearDown() override {
        // Destroy sim_ before mock destructors run — prevents use-after-free
        // when CitySimulation destructor references renderer_/audio_.
        // TearDown contract: CitySimulation is the sole owner of simulation
        // state; resetting here guarantees its destructor runs while all injected
        // dependencies (NiceMock instances) are still alive.
        sim_.reset();
    }
};

// ---------------------------------------------------------------------------
// TEST: Simulation60Ticks_NocrashAndCorrectTime
//
// Drives 60 budget ticks at x1 speed (30.0f real seconds per tick) and asserts:
//   1. No crash during any of the 60 ticks.
//   2. SimulationTime.year  == 6  (5 complete years elapsed; year starts at 1).
//   3. SimulationTime.month == 1  (60 mod 12 == 0 → wraps back to month 1).
// ---------------------------------------------------------------------------
TEST_F(Simulation60TickTest, Simulation60Ticks_NocrashAndCorrectTime) {
    CitySimulation* cs = dynamic_cast<CitySimulation*>(sim_.get());
    ASSERT_NE(cs, nullptr);

    // At x1 speed, one budget tick fires per tick(30.0f) call:
    //   sim_seconds accumulated = realDelta × speedValue = 30.0f × 1.0f = 30.0f
    //   budget tick threshold   = SECONDS_PER_BUDGET_TICK = 30.0f
    //   ticks fired per call    = floor(30.0f / 30.0f) = 1
    // x1 is set in SetUp() to neutralise the auto-slow mechanism.
    static constexpr float kRealDeltaPerBudgetTick =
        SimulationConstants::SECONDS_PER_BUDGET_TICK;  // = 30.0f at x1

    static constexpr int kTargetTicks = 60;

    for (int i = 0; i < kTargetTicks; ++i) {
        // Drain the notification queue each tick to prevent unbounded growth;
        // the integration test does not assert on notification content.
        SimulationNotification notif;
        while (sim_->pollPendingNotification(notif)) { /* discard */ }

        cs->tick(kRealDeltaPerBudgetTick);
    }

    // Drain any remaining notifications after the last tick.
    SimulationNotification notif;
    while (sim_->pollPendingNotification(notif)) { /* discard */ }

    // -----------------------------------------------------------------------
    // Assert SimulationTime after 60 budget ticks.
    //
    // Derivation:
    //   Starting state:    year=1, month=1
    //   ticks_per_year     = 12
    //   60 ticks elapsed   = 5 full years
    //   Expected year      = 1 + 5 = 6
    //   Expected month     = ((1 - 1 + 60) % 12) + 1
    //                      = (60 % 12) + 1
    //                      = 0 + 1 = 1
    //
    // SimulationTime::month is 1-based (range 1–12, defined in simulation_types.h).
    // SimulationTime::year  is 1-based (starts at 1).
    // -----------------------------------------------------------------------
    SimulationTime t = sim_->getSimulationTime();
    EXPECT_EQ(t.year,  6) << "After 60 budget ticks (5 years), year must be 6";
    EXPECT_EQ(t.month, 1) << "After 60 budget ticks (5 full years), month must be 1";
}

// ---------------------------------------------------------------------------
// TEST: Simulation60Ticks_TreasuryRemainsFinite
//
// Secondary guard: verifies the treasury value is a finite (non-NaN, non-Inf)
// number after 60 ticks. A NaN treasury indicates a floating-point bug in the
// economy sub-system (e.g. division by zero in revenue calculations).
// ---------------------------------------------------------------------------
TEST_F(Simulation60TickTest, Simulation60Ticks_TreasuryRemainsFinite) {
    CitySimulation* cs = dynamic_cast<CitySimulation*>(sim_.get());
    ASSERT_NE(cs, nullptr);

    static constexpr float kRealDeltaPerBudgetTick =
        SimulationConstants::SECONDS_PER_BUDGET_TICK;  // = 30.0f at x1

    for (int i = 0; i < 60; ++i) {
        SimulationNotification notif;
        while (sim_->pollPendingNotification(notif)) { /* discard */ }
        cs->tick(kRealDeltaPerBudgetTick);
    }

    const float balance = sim_->getTreasuryBalance();
    EXPECT_TRUE(std::isfinite(balance))
        << "Treasury balance must be finite after 60 ticks; got: " << balance;
}
