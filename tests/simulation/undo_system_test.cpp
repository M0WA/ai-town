// undo_system_test.cpp — Phase 6 simulation-side undo mechanics tests.
// UI-facing undo tests (countdown display, button grayout) belong in
// tests/ui/undo_button_test.cpp (Phase 8 deliverable).
//
// Tests:
//   UndoSystem_Refund_ClampedAtStartingCapital
//   UndoSystem_ExpiryTime_SpeedChangeAfterPlacement
//   UndoSystem_ModalBlocked_NoOpBeforeModalClose
//   UndoSystem_PausedSimulation_UndoWindowDoesNotExpireDuringPause
//   UndoSystem_ExpiryTime_ComputedCorrectly_AtHighSpeed
//
// Spec references:
//   architecture/game-design/undo-system.md
//   implementation/phase-6.md
//
// Undo expiry formula (tick-based):
//   Pending undo clears when the SECOND budget tick after the action fires.
//   Wall-clock display: getUndoExpiryTimeSeconds() returns the absolute clock
//   time at which the undo window expires.
//   At speed S: wall_expiry = clock.nowSeconds() + (2 × SECONDS_PER_BUDGET_TICK) / S
//   The wall expiry is recorded at placement time and does NOT change on speed change.
//
// Pause contract:
//   Budget ticks do NOT fire while paused; therefore the undo window cannot
//   expire during pause — its expiry is tick-based, not clock-based.
//
// Fixture: UndoTest (NiceMock) — placement SFX is irrelevant to undo logic.
//   TearDown() resets sim_ before mock destructors run.

#include "CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "src/simulation/simulation_constants.h"
#include "mock_audio_system.h"
#include "mock_renderer.h"
#include "manual_rng.h"
#include "manual_clock.h"
#include "manual_terrain_query.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::_;
using ::testing::AnyNumber;

// ---------------------------------------------------------------------------
// UndoTest fixture
// ---------------------------------------------------------------------------
// NiceMock for audio and renderer — undo logic tests do not care about SFX calls.
// ManualRNG with non-strict wrapping — int/float sequences are not the subject here.
// TearDown() resets sim_ before mocks are destroyed, preventing use-after-free
// in CitySimulation destructor if it calls back into audio_/renderer_.

class UndoTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    // Non-strict, wrapping RNG: undo tests don't depend on stochastic behaviour.
    ManualRNG    rng_;  // default: int={0}, float={0.9f}, non-strict
    ManualClock  clock_;
    ManualTerrainQuery terrain_;

    // sim_ declared LAST — destroyed first (reverse declaration order).
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        sim_->setSpeed(SpeedMultiplier::x1);
    }

    void TearDown() override {
        // Explicitly reset sim_ before mocks are destroyed.
        // CitySimulation destructor must not call back into mock objects
        // after they have been torn down.
        sim_.reset();
    }

    CitySimulation* cs() {
        auto* p = dynamic_cast<CitySimulation*>(sim_.get());
        EXPECT_NE(p, nullptr) << "Downcast to CitySimulation* failed";
        return p;
    }

    // Helper: run one budget tick at the current speed.
    // Advances ManualClock by SECONDS_PER_BUDGET_TICK real seconds.
    void runOneTick() {
        auto* c = cs();
        if (!c) return;
        const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
        clock_.advance(dt);
        c->tick(dt);
    }
};

// ---------------------------------------------------------------------------
// Test 1: UndoSystem_Refund_ClampedAtStartingCapital
//
// Spec (undo-system.md):
//   "If the refund would exceed the starting capital cap, the treasury is
//    clamped (no negative refunds)."
//
// Strategy:
//   Use Normal difficulty (starting funds = $500,000).
//   Place a road (costs road_placement_cost_per_tile = $500).
//   Treasury becomes $499,500.
//   Undo the road → refund $500 → treasury = $500,000 (back to cap).
//   Undo must not push treasury ABOVE starting_funds_normal.
//   Verify treasury == starting_funds_normal after undo (clamped, not $500,001).
//
// Note: road placement immediately deducts $500. After undo, treasury must equal
// starting_funds_normal exactly (clamp operates at the starting capital ceiling).
// ---------------------------------------------------------------------------
TEST_F(UndoTest, UndoSystem_Refund_ClampedAtStartingCapital) {
    const float startingFunds =
        static_cast<float>(SimulationConstants::starting_funds_normal);

    // Verify initial treasury.
    ASSERT_FLOAT_EQ(sim_->getTreasuryBalance(), startingFunds)
        << "Initial treasury should be starting_funds_normal";

    // Place a road: deducts road_placement_cost_per_tile ($500).
    sim_->placeRoad(0, 0);

    const float expectedAfterPlacement =
        startingFunds - static_cast<float>(SimulationConstants::road_placement_cost_per_tile);
    ASSERT_FLOAT_EQ(sim_->getTreasuryBalance(), expectedAfterPlacement)
        << "Treasury should be decremented by road_placement_cost_per_tile after placeRoad";

    // Undo must be pending.
    ASSERT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be true immediately after placeRoad";

    // Undo the road placement.
    sim_->undoLastAction();

    // Treasury must be exactly starting_funds_normal (clamped at cap).
    // Not more than starting_funds_normal even if arithmetic might allow it.
    EXPECT_LE(sim_->getTreasuryBalance(), startingFunds)
        << "Treasury must not exceed starting_funds_normal after undo (clamped)";
    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(), startingFunds)
        << "Treasury must be fully restored to starting_funds_normal after undo";

    // After undo, there should be no pending undo action.
    EXPECT_FALSE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be false after undoLastAction()";
}

// ---------------------------------------------------------------------------
// Test 2: UndoSystem_ExpiryTime_SpeedChangeAfterPlacement
//
// Spec (phase-6.md):
//   "place an undo action at 3× speed; record getUndoExpiryTimeSeconds() as T1;
//    call setSpeed(SpeedMultiplier::x1) (slowing down);
//    verify getUndoExpiryTimeSeconds() == T1 (expiry is fixed at placement time
//    — speed change does NOT update stored expiry)."
//
// Expiry formula at placement time:
//   wall_expiry = clock.nowSeconds() + (2 × SECONDS_PER_BUDGET_TICK) / speed_value(x3)
//              = 0.0 + (2 × 30.0) / 3.0 = 20.0 seconds
//
// After speed change to x1, getUndoExpiryTimeSeconds() must still return T1.
// ---------------------------------------------------------------------------
TEST_F(UndoTest, UndoSystem_ExpiryTime_SpeedChangeAfterPlacement) {
    // Set speed to x3 before placement.
    sim_->setSpeed(SpeedMultiplier::x3);

    // ManualClock starts at 0.0; do not advance it so computation is exact.
    // Place a road to record the undo action.
    sim_->placeRoad(0, 0);

    ASSERT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be true after placeRoad";

    // Record T1 immediately after placement.
    const double T1 = sim_->getUndoExpiryTimeSeconds();
    ASSERT_GT(T1, 0.0)
        << "getUndoExpiryTimeSeconds() must be > 0 when a pending undo action exists";

    // Change speed to x1 (slowing down). Per spec, expiry is fixed at placement.
    sim_->setSpeed(SpeedMultiplier::x1);

    // Expiry must equal T1 — speed change must not alter the stored expiry.
    EXPECT_DOUBLE_EQ(sim_->getUndoExpiryTimeSeconds(), T1)
        << "getUndoExpiryTimeSeconds() must be unchanged by a subsequent speed change "
           "(expiry is fixed at placement time, not recomputed on speed change)";
}

// ---------------------------------------------------------------------------
// Test 3: UndoSystem_ModalBlocked_NoOpBeforeModalClose
//
// Spec (undo-system.md):
//   "Undo is unavailable while a blocking modal is active."
//
// Strategy:
//   1. Place a road → undo action is pending.
//   2. Open a modal (cs()->setModalOpen(true)).
//   3. Call undoLastAction() → must be a no-op (action still pending, road still there).
//   4. Close modal (cs()->setModalOpen(false)).
//   5. Call undoLastAction() again → now succeeds (pending action cleared).
// ---------------------------------------------------------------------------
TEST_F(UndoTest, UndoSystem_ModalBlocked_NoOpBeforeModalClose) {
    const float startingFunds =
        static_cast<float>(SimulationConstants::starting_funds_normal);

    // Place a road to create a pending undo.
    sim_->placeRoad(0, 0);
    ASSERT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be true after placeRoad";

    const float treasuryAfterPlacement = sim_->getTreasuryBalance();
    const float expectedAfterPlacement =
        startingFunds - static_cast<float>(SimulationConstants::road_placement_cost_per_tile);
    ASSERT_FLOAT_EQ(treasuryAfterPlacement, expectedAfterPlacement)
        << "Pre-condition: treasury decremented by road_placement_cost_per_tile";

    // Open a blocking modal.
    cs()->setModalOpen(true);

    // Attempt undo while modal is open: must be a no-op.
    sim_->undoLastAction();

    // Action must still be pending — modal blocked the undo.
    EXPECT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must still be true after blocked undoLastAction() "
           "during open modal";

    // Treasury must be unchanged — no refund happened.
    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(), treasuryAfterPlacement)
        << "Treasury must be unchanged when undoLastAction() is blocked by modal";

    // Close the modal.
    cs()->setModalOpen(false);

    // Undo should now succeed.
    sim_->undoLastAction();

    EXPECT_FALSE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be false after successful undoLastAction() "
           "following modal close";

    // Treasury restored.
    EXPECT_FLOAT_EQ(sim_->getTreasuryBalance(), startingFunds)
        << "Treasury must be restored to starting_funds_normal after successful undo";
}

// ---------------------------------------------------------------------------
// Test 4: UndoSystem_PausedSimulation_UndoWindowDoesNotExpireDuringPause
//
// Spec (undo-system.md):
//   "Budget ticks do NOT fire while the simulation is paused, so the undo window
//    cannot expire during pause."
//   "UndoSystem_PausedSimulation_UndoWindowDoesNotExpireDuringPause test contract:
//    advance ManualClock past the original predicted secondBudgetTickTimeReal without
//    firing any budget ticks (i.e., while paused); assert that the pending undo is
//    still valid."
//
// Strategy:
//   1. Place a road at x1 speed (clock=0).
//      Wall expiry = 0 + (2×30)/1 = 60 s.
//   2. Pause the simulation.
//   3. Advance ManualClock by 70 s (past the predicted expiry of 60 s).
//   4. Call tick() with the 70 s delta (paused, so no budget ticks fire).
//   5. Assert hasUndoPendingAction() == true (tick-based, not clock-based).
//
// Note: The undo window is budget-tick-driven. No ticks fired → window not expired.
// ---------------------------------------------------------------------------
TEST_F(UndoTest, UndoSystem_PausedSimulation_UndoWindowDoesNotExpireDuringPause) {
    // Place a road at x1 speed. Clock starts at 0.0.
    // Expected wall expiry = 0.0 + (2 × 30.0) / 1.0 = 60.0 s.
    sim_->placeRoad(0, 0);
    ASSERT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be true after placeRoad";

    // Pause the simulation — budget ticks must NOT fire while paused.
    sim_->setPaused(true);
    ASSERT_TRUE(sim_->isPaused()) << "Simulation must be paused";

    // Advance ManualClock by 70 s — past the original predicted 60 s expiry.
    const double pastExpiry = 70.0;
    clock_.advance(pastExpiry);

    // Call tick() with 70 s while paused: no budget ticks should fire.
    cs()->tick(static_cast<float>(pastExpiry));

    // Undo must still be pending — expiry is tick-based, not clock-based.
    EXPECT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must still be true after advancing clock past predicted "
           "expiry while paused (undo expiry is tick-based, not wall-clock-based)";

    // Unpause and fire two budget ticks — now the undo SHOULD expire.
    sim_->setPaused(false);
    runOneTick();
    runOneTick();

    EXPECT_FALSE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be false after two budget ticks fired post-unpause";
}

// ---------------------------------------------------------------------------
// Test 5: UndoSystem_ExpiryTime_ComputedCorrectly_AtHighSpeed
//
// Spec (phase-6.md exit criterion):
//   "getUndoExpiryTimeSeconds() returns the correct wall-clock value per the
//    documented formula"
//
// Formula: wall_expiry = clock.nowSeconds() + (2 × SECONDS_PER_BUDGET_TICK) / speed_value
// At x10 speed: speed_value = 10
//   wall_expiry = 0.0 + (2 × 30.0) / 10.0 = 6.0 seconds
//
// Strategy:
//   1. Set speed to x10. Clock = 0.0.
//   2. Place a zone (records undo with wall expiry = 6.0 s).
//   3. Verify getUndoExpiryTimeSeconds() ≈ 6.0.
// ---------------------------------------------------------------------------
TEST_F(UndoTest, UndoSystem_ExpiryTime_ComputedCorrectly_AtHighSpeed) {
    // Set speed to x10 at clock = 0.0.
    sim_->setSpeed(SpeedMultiplier::x10);

    // Place a zone to record the undo action.
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low);

    ASSERT_TRUE(sim_->hasUndoPendingAction())
        << "hasUndoPendingAction() must be true after placeZone";

    // Expected expiry formula:
    //   wall_expiry = clock.nowSeconds() + (2 × SECONDS_PER_BUDGET_TICK) / 10.0
    //              = 0.0 + (2 × 30.0) / 10.0 = 6.0
    const double expectedExpiry =
        clock_.nowSeconds() +
        (2.0 * static_cast<double>(SimulationConstants::SECONDS_PER_BUDGET_TICK)) / 10.0;

    const double actualExpiry = sim_->getUndoExpiryTimeSeconds();

    EXPECT_NEAR(actualExpiry, expectedExpiry, 1e-6)
        << "getUndoExpiryTimeSeconds() must equal "
           "clock.nowSeconds() + (2 × SECONDS_PER_BUDGET_TICK) / speed_value(x10) "
           "= " << expectedExpiry << " s at x10 speed";
}
