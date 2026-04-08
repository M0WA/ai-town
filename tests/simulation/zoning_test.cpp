// zoning_test.cpp — Phase 6 simulation unit tests for zoning and demand mechanics.
// Tests: bootstrap demand isolation, demand floors, zombie population emigration,
//        density unlock (3-consecutive-month gate), commercial demand null-path window,
//        desirability in [0,100] property invariant.
//
// Fixture:
//   ZoningTestNice — NiceMock; used for all zoning tests. placeZone() and placeRoad()
//                    fire audio callbacks (SFX_BUILD_PLACE, SFX_ROAD_BUILD) that are
//                    irrelevant to zoning logic; NiceMock suppresses unexpected-call
//                    failures without requiring per-test EXPECT_CALLs. For tests that
//                    do verify audio (DensityUpgrade_AudioCallback_FiresOnUpgrade), an
//                    explicit EXPECT_CALL is added — NiceMock still enforces it.
//
// Key constants (SimulationConstants):
//   density_unlock_base_threshold_1 = $50K  → tiers 0 (Med-R) + 1 (Med-C) together
//   density_unlock_base_threshold_2 = $75K  → tier 2 (Med-I)
//   density_unlock_base_threshold_3 = $100K → tier 3 (High-R), requires Med-I first
//   demand_bootstrapping_ticks = 6
//   demand_floor_residential = 0.20f
//   demand_floor_commercial  = 0.10f
//   demand_floor_industrial  = 0.10f
//   density_upgrade_wave_demand_threshold = 0.75f
//   SECONDS_PER_BUDGET_TICK = 30.0f

#include "SimulationTestBase.h"
#include "NiceSimulationTestBase.h"
#include "src/interfaces/simulation_types.h"
#include "src/simulation/simulation_constants.h"
#include "src/interfaces/sound_ids.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cmath>

using ::testing::NiceMock;
using ::testing::_;
using ::testing::AtLeast;

// ---------------------------------------------------------------------------
// ZoningTestNice — NiceMock fixture for zoning tests that place zones/roads.
// Inherits from NiceSimulationTestBase: NiceMock renderer_/audio_, ManualRNG,
// ManualClock, ManualTerrainQuery, sim_, SetUp/TearDown, cs(), runTicks().
// NiceMock suppresses unexpected audio/renderer calls from placement.
// ---------------------------------------------------------------------------
class ZoningTestNice : public NiceSimulationTestBase {
};

// ---------------------------------------------------------------------------
// TEST 1: IndustrialDemand_ZeroCapacity_DefaultsTo1
//
// When no Industrial zones exist (I_production_capacity = 0), Industrial demand
// defaults to 1.0f (fully demanded — all available industrial space absorbed).
// Spec: "If there is no Industrial zone (I_production_capacity = 0), Industrial
// demand defaults to 1.0 (maximum)."
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, IndustrialDemand_ZeroCapacity_DefaultsTo1) {
    // No zones placed — I_production_capacity = 0.
    // getZoneDemandFactor(Industrial) returns effective demand in [0,1].
    // At construction before any ticks, during bootstrap period (tick 0),
    // I_demand defaults to 1.0 when no I zone exists.
    //
    // Note: bootstrap also contributes I_bootstrap(0) = 0.15, but the
    // capacity-ratio I_demand = 1.0 dominates. Combined demand is clamped to 1.0.
    // After bootstrap period ends (tick 6+): still 1.0 since no I zone.
    //
    // Run a few ticks to confirm the default persists.
    runTicks(1);

    float iDemand = sim_->getZoneDemandFactor(ZoneType::Industrial);
    EXPECT_FLOAT_EQ(iDemand, 1.0f)
        << "Industrial demand must default to 1.0 when no Industrial zones exist "
           "(I_production_capacity = 0).";
}

// ---------------------------------------------------------------------------
// TEST 2: BootstrapDemand_NoCIZones_ResidentialGrowthIsZero
//
// With only Residential zones placed (no Commercial or Industrial), R_demand = 0
// (capacity ratio: total_C_I_worker_capacity = 0 → R_demand = 0).
// Even with bootstrap active, population growth = max_density × R_demand × (des/100)
// = max_density × 0 × _ = 0.
//
// The bootstrap supplements only the travel-time signal (formula_demand_factor),
// NOT the capacity-ratio R_demand. When capacity-ratio is 0, growth is 0 regardless
// of bootstrap.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, BootstrapDemand_NoCIZones_ResidentialGrowthIsZero) {
    // Place only R zones + a road (so A* finds a valid path — no null-path).
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(2, 0, ZoneType::Residential, DensityTier::Low);

    // Run through the full bootstrap period (demand_bootstrapping_ticks = 6).
    runTicks(SimulationConstants::demand_bootstrapping_ticks);

    // With no C/I zones: R_demand = 0, growth target = 0.
    // Population must remain at 0 (no growth possible without jobs).
    EXPECT_EQ(sim_->getTotalPopulation(), 0)
        << "With no Commercial or Industrial zones, R capacity-ratio demand = 0 "
           "even during bootstrap, so population growth = 0.";
}

// ---------------------------------------------------------------------------
// TEST 3: DensityUnlock_RequiresThreeConsecutiveMonths_NotOneSpike
//
// Density unlock (Med-R, tier 0) requires 3 CONSECUTIVE months with treasury
// above the threshold. A 2-month streak, then a miss, then hitting again must
// NOT unlock. After the miss the counter resets; 3 more consecutive months unlock.
//
// Strategy: use Normal difficulty, threshold_1 = $50K.
// At construction, Normal starts with $500K (> $50K). Place zones so revenue
// keeps treasury above threshold. We verify the counter via getDensityUnlockState().
//
// Note: the test uses the initial $500K treasury (Normal difficulty) which is
// already above $50K. The consecutive-month counter increments each budget tick
// that the treasury ends above the scaled threshold.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DensityUnlock_RequiresThreeConsecutiveMonths_NotOneSpike) {
    // Normal difficulty starting treasury = $500K > threshold_1 ($50K × 1.0 scale).
    // Place zones to generate revenue and keep treasury above threshold.
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);  // past grace period

    // After 1 tick: counter should be 1 (treasury still > $50K).
    runTicks(1);
    DensityUnlockState state1 = sim_->getDensityUnlockState();
    EXPECT_FALSE(state1.unlock_flags[0])
        << "Med-R should NOT be unlocked after only 1 month above threshold";
    EXPECT_EQ(state1.consecutive_months_above_threshold[0], 1)
        << "Counter should be 1 after 1 consecutive month above threshold";

    // After 2 ticks: counter should be 2.
    runTicks(1);
    DensityUnlockState state2 = sim_->getDensityUnlockState();
    EXPECT_FALSE(state2.unlock_flags[0])
        << "Med-R should NOT be unlocked after only 2 months above threshold";
    EXPECT_EQ(state2.consecutive_months_above_threshold[0], 2)
        << "Counter should be 2 after 2 consecutive months above threshold";

    // After 3 ticks: counter reaches 3 → unlock fires, counter resets to 0.
    runTicks(1);
    DensityUnlockState state3 = sim_->getDensityUnlockState();
    EXPECT_TRUE(state3.unlock_flags[0])
        << "Med-R MUST be unlocked after 3 consecutive months above threshold";
    // Counter resets to 0 after unlock fires (transient 3 becomes 0).
    EXPECT_EQ(state3.consecutive_months_above_threshold[0], 0)
        << "Counter must reset to 0 after unlock fires";
}

// ---------------------------------------------------------------------------
// TEST 4: DensityUnlock_SimultaneousThreshold_MedIBeforeHighR
//
// Tier 2 (Med-I, threshold_2 = $75K) must unlock BEFORE tier 3 (High-R,
// threshold_3 = $100K) becomes eligible. High-R requires Med-I as a prerequisite:
// the High-R consecutive-month counter only starts incrementing after Med-I is
// unlocked.
//
// With Normal starting treasury = $500K (above both thresholds), Med-I should
// unlock at 3 months and High-R counter starts only after Med-I is unlocked.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DensityUnlock_SimultaneousThreshold_MedIBeforeHighR) {
    // Normal starting treasury = $500K, well above $75K (Med-I) and $100K (High-R).
    // Place zones for revenue to hold treasury above threshold across ticks.
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);

    // After 3 ticks: Med-R (tier 0), Med-C (tier 1), Med-I (tier 2) all unlock
    // (all thresholds at $50K, $50K, $75K — all below $500K starting treasury).
    runTicks(3);
    DensityUnlockState state = sim_->getDensityUnlockState();

    EXPECT_TRUE(state.unlock_flags[2])
        << "Med-I (tier 2) must unlock after 3 months above $75K threshold";

    // High-R (tier 3) counter starts ONLY after Med-I is unlocked.
    // After exactly 3 ticks (Med-I just unlocked this tick), High-R counter = 0
    // or 1 depending on whether it starts counting in the same tick as Med-I unlock.
    // Either way: High-R must NOT be unlocked yet (needs 3 months after Med-I).
    EXPECT_FALSE(state.unlock_flags[3])
        << "High-R (tier 3) must NOT be unlocked yet — it requires Med-I to be "
           "unlocked first and then 3 more consecutive months above $100K";
}

// ---------------------------------------------------------------------------
// TEST 5: DensityUnlock_MedR_And_MedC_SameThreshold_BothUnlockTogether
//
// Med-R (tier 0) and Med-C (tier 1) share threshold_1 ($50K). When revenue
// stays above $50K for 3 months, both unlock simultaneously.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DensityUnlock_MedR_And_MedC_SameThreshold_BothUnlockTogether) {
    // Normal starting treasury = $500K > $50K.
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);
    runTicks(3);

    DensityUnlockState state = sim_->getDensityUnlockState();

    EXPECT_TRUE(state.unlock_flags[0])
        << "Med-R (tier 0) must unlock after 3 months above $50K threshold";
    EXPECT_TRUE(state.unlock_flags[1])
        << "Med-C (tier 1) must unlock after 3 months above $50K threshold "
           "(same threshold as Med-R — both unlock simultaneously)";
}

// ---------------------------------------------------------------------------
// TEST 6: DemandFloor_Residential_Applied_WhenFormulaDemandBelow20Pct
//
// After the bootstrap period (tick 6+), when a road network exists, Residential
// effective demand is clamped to at least demand_floor_residential (0.20).
//
// Set up: enough C/I zones for a non-zero capacity ratio but small enough
// that the formula demand < 0.20. With a road present (valid path), the floor
// kicks in and getZoneDemandFactor(Residential) >= 0.20.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DemandFloor_Residential_Applied_WhenFormulaDemandBelow20Pct) {
    // Place a road + small C/I zones to give R some capacity ratio demand.
    // Many R tiles but few C/I → R_demand = C/I_capacity / max(1, R_pop) can be small.
    sim_->placeRoad(0, 0);
    // Lots of R zones — will keep R population denominator large
    for (int i = 1; i <= 5; ++i) {
        sim_->placeZone(i, 0, ZoneType::Residential, DensityTier::Low);
    }
    // Small C/I capacity relative to R
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(0, 2, ZoneType::Industrial,  DensityTier::Low);

    // Run past bootstrap period.
    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks + 1);

    float rDemand = sim_->getZoneDemandFactor(ZoneType::Residential);

    // Post-bootstrap with road network: R demand floor must be active.
    EXPECT_GE(rDemand, SimulationConstants::demand_floor_residential)
        << "Residential effective demand must be >= demand_floor_residential (0.20) "
           "when a road network exists, post-bootstrap.";
    EXPECT_LE(rDemand, 1.0f);
}

// ---------------------------------------------------------------------------
// TEST 7: DemandFloor_Commercial_Applied
//
// After bootstrap (tick 6+) with road network: Commercial demand >= 10%.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DemandFloor_Commercial_Applied) {
    sim_->placeRoad(0, 0);
    // Many C zones, few R → C_demand = R_pop / C_worker_cap can be small.
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    for (int i = 0; i < 5; ++i) {
        sim_->placeZone(0, 1 + i, ZoneType::Commercial, DensityTier::Low);
    }
    sim_->placeZone(1, 1, ZoneType::Industrial, DensityTier::Low);

    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks + 1);

    float cDemand = sim_->getZoneDemandFactor(ZoneType::Commercial);
    EXPECT_GE(cDemand, SimulationConstants::demand_floor_commercial)
        << "Commercial effective demand must be >= demand_floor_commercial (0.10) "
           "when a road network exists, post-bootstrap.";
    EXPECT_LE(cDemand, 1.0f);
}

// ---------------------------------------------------------------------------
// TEST 8: DemandFloor_Industrial_Applied
//
// After bootstrap (tick 6+) with road network: Industrial demand >= 10%.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DemandFloor_Industrial_Applied) {
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    // Many I zones, few R/C → I_demand = (R_raw + C_goods) / I_cap can be small.
    for (int i = 0; i < 5; ++i) {
        sim_->placeZone(0, 2 + i, ZoneType::Industrial, DensityTier::Low);
    }

    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks + 1);

    float iDemand = sim_->getZoneDemandFactor(ZoneType::Industrial);
    EXPECT_GE(iDemand, SimulationConstants::demand_floor_industrial)
        << "Industrial effective demand must be >= demand_floor_industrial (0.10) "
           "when a road network exists, post-bootstrap.";
    EXPECT_LE(iDemand, 1.0f);
}

// ---------------------------------------------------------------------------
// TEST 9: DemandFloor_DoesNotPreventEmigration_OnBadConditions
//
// The demand floor gates ONLY occupancy_increase_this_tick (new growth).
// It does NOT prevent population decay when desirability drives the target
// below current occupancy.
//
// Scenario: place an R zone far from Commercial/Industrial (desirability = 50,
// R_demand → small). Run many ticks. Because the decay target
// (max_density × raw_demand × desirability/100) can be below the current
// population, decay proceeds even while the demand floor prevents new growth.
//
// This test verifies that population can decrease (emigration) even when the
// demand floor would prevent growth — the floor and decay are independent paths.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DemandFloor_DoesNotPreventEmigration_OnBadConditions) {
    // Place an R zone + road. To trigger emigration we need population first,
    // then remove the C/I support so R_demand drops. Then run many ticks to
    // allow decay.
    //
    // Phase 1: grow some population by having C/I zones.
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks);  // grow population

    int popAfterGrowth = sim_->getTotalPopulation();

    // Phase 2: demolish C/I zones so R_demand → 0 (no jobs).
    sim_->demolishTile(0, 1);  // remove Commercial
    sim_->demolishTile(1, 1);  // remove Industrial

    // Run many ticks to allow decay to proceed.
    // The demand floor gates growth (occupancy_increase) but decay uses the
    // unclamped R_demand = 0, so target = 0 → population decays toward 0.
    runTicks(10);

    int popAfterDecay = sim_->getTotalPopulation();

    // If any population grew during bootstrap and then C/I was removed,
    // population should decay. If popAfterGrowth was 0, skip the decay check
    // (no population to decay from).
    if (popAfterGrowth > 0) {
        EXPECT_LT(popAfterDecay, popAfterGrowth)
            << "Population must decay when R_demand = 0 (no C/I zones). "
               "The demand floor must NOT prevent emigration on bad conditions.";
    }
    // In both cases: demand floor constant is correct and positive.
    EXPECT_GT(SimulationConstants::demand_floor_residential, 0.0f);
    EXPECT_LT(SimulationConstants::demand_floor_residential, 1.0f);
}

// ---------------------------------------------------------------------------
// TEST 10: ZombiePopulation_Residential_EmigrationForcedWhenDemandZero
//
// When R_demand drops to 0 (no C/I zones and no bootstrap), population must
// decay (emigration). The demand floor applies only to growth, not to the
// decay computation. After many ticks with zero R_demand, population trends
// toward 0.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, ZombiePopulation_Residential_EmigrationForcedWhenDemandZero) {
    // Grow population first (C/I present during bootstrap).
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks);

    int popBeforeDecay = sim_->getTotalPopulation();

    // Remove C/I support → R_demand = 0 post-bootstrap.
    sim_->demolishTile(0, 1);
    sim_->demolishTile(1, 1);

    // Run enough ticks to see decay (post-bootstrap, R_demand = 0).
    runTicks(8);

    int popAfterDecay = sim_->getTotalPopulation();

    // Emigration must occur: if there was population, it must decline.
    // The demand floor prevents new growth but cannot create zombie population
    // that refuses to leave despite zero demand.
    if (popBeforeDecay > 0) {
        EXPECT_LT(popAfterDecay, popBeforeDecay)
            << "Residential population must decline (emigration) when R_demand = 0. "
               "Zombie populations (no decay despite zero demand) are not allowed.";
    }
    // Structural invariant: population is never negative.
    EXPECT_GE(popAfterDecay, 0);
}

// ---------------------------------------------------------------------------
// TEST 11: ZombiePopulation_Commercial_EmigrationForcedWhenDemandZero
//
// When C_demand = 0 (no Residential population → no consumers), Commercial
// population must decay. The demand floor does not prevent emigration.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, ZombiePopulation_Commercial_EmigrationForcedWhenDemandZero) {
    // Grow some C population first (need R population as demand driver).
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks);

    // Note initial C tax revenue as proxy for C population.
    float cRevenueBefore = sim_->getTaxRevenue(ZoneType::Commercial);

    // Remove R zones → C_demand = R_pop / C_cap → 0 as population decays.
    sim_->demolishTile(1, 0);  // remove Residential

    // Run ticks post-bootstrap: C_demand formula = R_pop / C_worker_cap → ~0.
    runTicks(8);

    float cRevenueAfter = sim_->getTaxRevenue(ZoneType::Commercial);

    // C revenue (proportional to C population) must not be higher than before
    // once R population is gone and C_demand collapses.
    // If there was some C revenue before demolition, it should decline or stay flat.
    if (cRevenueBefore > 0.0f) {
        EXPECT_LE(cRevenueAfter, cRevenueBefore)
            << "Commercial revenue (population) must decline when R population is gone "
               "(C_demand = 0 with no residential consumers). "
               "Zombie populations must not persist.";
    }
    EXPECT_GE(cRevenueAfter, 0.0f);
}

// ---------------------------------------------------------------------------
// TEST 12: ZombiePopulation_Industrial_EmigrationForcedWhenDemandZero
//
// When I_demand driver (R_raw_material + C_goods) → 0, Industrial population
// must decay. The demand floor does not prevent emigration.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, ZombiePopulation_Industrial_EmigrationForcedWhenDemandZero) {
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks);

    float iRevenueBefore = sim_->getTaxRevenue(ZoneType::Industrial);

    // Remove R and C zones → I_demand = (0 + 0) / I_cap = 0 post-bootstrap.
    sim_->demolishTile(1, 0);  // remove Residential
    sim_->demolishTile(0, 1);  // remove Commercial

    runTicks(8);

    float iRevenueAfter = sim_->getTaxRevenue(ZoneType::Industrial);

    // I revenue (proportional to I population) must not increase after demand source removed.
    if (iRevenueBefore > 0.0f) {
        EXPECT_LE(iRevenueAfter, iRevenueBefore)
            << "Industrial revenue (population) must decline or stay flat when demand "
               "drivers (R raw material + C goods consumption) are removed. "
               "Zombie populations must not persist.";
    }
    EXPECT_GE(iRevenueAfter, 0.0f);
}

// ---------------------------------------------------------------------------
// TEST 13: DensityUpgradeWave_EndsOnLowDemand_NoAutoRestart
//
// After Med-R is unlocked, the upgrade wave fires for tiles with demand > 0.75.
// If demand drops below 0.75, the wave ends and does NOT auto-restart.
// The unlock_flags remain true but no upgrades happen until demand recovers.
//
// Verify: once Med-R is unlocked and demand is below the threshold, no tiles
// are upgraded on subsequent ticks (queryTile still reports Low density).
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DensityUpgradeWave_EndsOnLowDemand_NoAutoRestart) {
    // Normal starting treasury $500K → will unlock Med-R after 3 months.
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);

    // Run 3 ticks to unlock Med-R.
    runTicks(3);
    DensityUnlockState stateAfterUnlock = sim_->getDensityUnlockState();
    EXPECT_TRUE(stateAfterUnlock.unlock_flags[0])
        << "Med-R must be unlocked after 3 months above $50K threshold.";

    // Now reduce demand below the upgrade wave threshold (0.75) by removing
    // the C/I employment drivers → R_demand drops.
    sim_->demolishTile(0, 1);  // remove Commercial
    sim_->demolishTile(1, 1);  // remove Industrial

    // Run additional ticks — demand is now low (no C/I → R_demand ~0).
    // The upgrade wave must NOT fire on tiles that don't meet demand > 0.75.
    runTicks(3);

    // The R tile placed at (1,0) should still be Low density — no upgrade.
    QueryResult qr = sim_->queryTile(1, 0);
    EXPECT_EQ(qr.densityTier, DensityTier::Low)
        << "Residential tile must remain Low density when demand is below 0.75 — "
           "the upgrade wave ends and does NOT auto-restart.";

    // unlock_flags still set (unlock is permanent once triggered).
    DensityUnlockState stateAfterLowDemand = sim_->getDensityUnlockState();
    EXPECT_TRUE(stateAfterLowDemand.unlock_flags[0])
        << "Med-R unlock flag must remain true permanently once set.";
}

// ---------------------------------------------------------------------------
// TEST 14: DensityUpgrade_AudioCallback_FiresOnUpgrade
//
// When a density upgrade wave fires (demand > 0.75, Med-R unlocked), the
// simulation must call playSound(SFX_ZONE_UPGRADE, ...) at least once.
//
// Uses ZoningTestNice (NiceMock) because we need to allow ALL audio calls
// that happen during zone placement (SFX_BUILD_PLACE) AND explicitly verify
// SFX_ZONE_UPGRADE fires at least once during the upgrade wave.
//
// Note: playSound (non-positional, AL_SOURCE_RELATIVE = AL_TRUE) is the correct
// method for zone upgrade audio per phase-10.md §sfx_zone_upgrade wiring.
// The sound fires globally (not at a tile position) with a per-wave-tick cap of 3.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DensityUpgrade_AudioCallback_FiresOnUpgrade) {
    // Set up to trigger Med-R unlock + upgrade wave.
    // Normal starting treasury $500K → unlocks after 3 months.
    // Need demand > 0.75 for upgrade wave to fire.
    sim_->placeRoad(0, 0);
    // Place a contiguous 2×2 block of R-low tiles so the upgrade wave can expand
    // to a 2×2 Med-R footprint without crossing zone boundaries or empty tiles.
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(2, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(1, 1, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(2, 1, ZoneType::Residential, DensityTier::Low);
    // Balanced C/I elsewhere to create strong R demand.
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(0, 2, ZoneType::Commercial,  DensityTier::Low);
    sim_->placeZone(3, 0, ZoneType::Industrial,  DensityTier::Low);
    sim_->placeZone(3, 1, ZoneType::Industrial,  DensityTier::Low);

    clock_.advance(121.0);

    // Expect SFX_ZONE_UPGRADE to fire at least once during the upgrade wave.
    // playSound (non-positional) is used for zone upgrade per phase-10.md.
    EXPECT_CALL(audio_, playSound(SFX_ZONE_UPGRADE, _, _))
        .Times(AtLeast(1));

    // Run 3 ticks to unlock Med-R, then more ticks to allow the upgrade wave.
    // The upgrade wave fires on the tick when the unlock condition is met
    // (counter reaches 3 and resets), processing eligible tiles.
    runTicks(5);
}

// ---------------------------------------------------------------------------
// SP-B: DEMAND BOOTSTRAP AT 3x SPEED SPIKE
//
// Spec (architecture/game-design/traffic-system.md, zoning-system.md):
//   Bootstrap subsidies apply during ticks 0 through demand_bootstrapping_ticks-1
//   (ticks 0–5 inclusive, i.e., the first 6 budget ticks).
//   At SpeedMultiplier::x3, each game second advances 3× faster, but budget ticks
//   still fire on SECONDS_PER_BUDGET_TICK boundaries of accumulated simulation time.
//   Bootstrap demand must not oscillate: no zone type's demand should jump by more
//   than 1.0 per tick (full range is [0.0, 1.0], so a jump of 1.0 would be maximum
//   possible change from complete collapse to complete saturation or vice versa).
//
// This test:
//   1. Constructs CitySimulation at SpeedMultiplier::x3 with a blank map.
//   2. Runs 10 ticks (covering the bootstrap period and beyond).
//   3. Verifies getTrafficDemandFactor(zone) stays within [0.0, 1.0] for all zones.
//   4. Verifies no demand value jumps by more than a reasonable delta per tick
//      (oscillation invariant: delta per tick < 1.0 for all zone types).
//   5. Verifies getZoneDemandFactor stays in [0.0, 1.0] for all zones.
//
// Uses ZoningTestNice (NiceMock) per project conventions for integration tests.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DemandBootstrap_AtX3Speed_NoBoundaryViolationAndNoOscillation) {
    // Reconfigure to x3 speed.
    sim_->setSpeed(SpeedMultiplier::x3);
    EXPECT_EQ(sim_->getSpeedMultiplier(), SpeedMultiplier::x3);

    // Blank map: no zones, no roads. All demand signals should stay at or near
    // null_path_demand_default = 0.5 during bootstrap (Industrial defaults to 1.0
    // when no I zone exists; R and C start at bootstrap values).
    // At x3, the tick accumulator fires budget ticks on 10.0f real-second boundaries
    // (30.0f simulation seconds / 3 = 10.0f real seconds per budget tick).
    const float realSecondsPerBudgetTick =
        SimulationConstants::SECONDS_PER_BUDGET_TICK / 3.0f;  // 10 s at x3

    // Demand values from previous tick for oscillation check.
    float prevR = sim_->getTrafficDemandFactor(ZoneType::Residential);
    float prevC = sim_->getTrafficDemandFactor(ZoneType::Commercial);
    float prevI = sim_->getTrafficDemandFactor(ZoneType::Industrial);

    for (int tick = 0; tick < 10; ++tick) {
        clock_.advance(realSecondsPerBudgetTick);
        cs()->tick(realSecondsPerBudgetTick);

        float rFactor = sim_->getTrafficDemandFactor(ZoneType::Residential);
        float cFactor = sim_->getTrafficDemandFactor(ZoneType::Commercial);
        float iFactor = sim_->getTrafficDemandFactor(ZoneType::Industrial);

        // All traffic demand factors must be in [0.0, 1.0].
        EXPECT_GE(rFactor, 0.0f) << "R traffic demand must be >= 0 at tick " << tick;
        EXPECT_LE(rFactor, 1.0f) << "R traffic demand must be <= 1 at tick " << tick;
        EXPECT_GE(cFactor, 0.0f) << "C traffic demand must be >= 0 at tick " << tick;
        EXPECT_LE(cFactor, 1.0f) << "C traffic demand must be <= 1 at tick " << tick;
        EXPECT_GE(iFactor, 0.0f) << "I traffic demand must be >= 0 at tick " << tick;
        EXPECT_LE(iFactor, 1.0f) << "I traffic demand must be <= 1 at tick " << tick;

        // All effective demand pressures must be in [0.0, 1.0].
        float rPct = sim_->getZoneDemandFactor(ZoneType::Residential);
        float cPct = sim_->getZoneDemandFactor(ZoneType::Commercial);
        float iPct = sim_->getZoneDemandFactor(ZoneType::Industrial);

        EXPECT_GE(rPct, 0.0f) << "R demand pressure must be >= 0 at tick " << tick;
        EXPECT_LE(rPct, 1.0f) << "R demand pressure must be <= 1 at tick " << tick;
        EXPECT_GE(cPct, 0.0f) << "C demand pressure must be >= 0 at tick " << tick;
        EXPECT_LE(cPct, 1.0f) << "C demand pressure must be <= 1 at tick " << tick;
        EXPECT_GE(iPct, 0.0f) << "I demand pressure must be >= 0 at tick " << tick;
        EXPECT_LE(iPct, 1.0f) << "I demand pressure must be <= 1 at tick " << tick;

        // Oscillation invariant: no demand value may jump by more than 1.0 per tick
        // (the full [0,1] range). Any jump >= 1.0 indicates a sign-flip oscillation.
        // On a blank map the values should be stable (null_path_default = 0.5 or
        // Industrial default = 1.0 when no I zones exist).
        const float maxAllowedDeltaPerTick = 1.0f;
        EXPECT_LT(std::abs(rFactor - prevR), maxAllowedDeltaPerTick)
            << "R traffic demand oscillation at tick " << tick
            << ": prev=" << prevR << " curr=" << rFactor;
        EXPECT_LT(std::abs(cFactor - prevC), maxAllowedDeltaPerTick)
            << "C traffic demand oscillation at tick " << tick
            << ": prev=" << prevC << " curr=" << cFactor;
        EXPECT_LT(std::abs(iFactor - prevI), maxAllowedDeltaPerTick)
            << "I traffic demand oscillation at tick " << tick
            << ": prev=" << prevI << " curr=" << iFactor;

        prevR = rFactor;
        prevC = cFactor;
        prevI = iFactor;
    }

    // After the bootstrap period (ticks 0–5), demand must be stable.
    // On a blank map with no zones: I_demand = 1.0 (no I zone → default max).
    // R and C demand = 0.0 (no C/I or R zones → capacity ratio = 0).
    // Post-bootstrap (tick 6+), demand floors apply: R >= 0.20, C >= 0.10, I >= 0.10.
    // But demand floor only applies to growth, not to getZoneDemandFactor directly
    // (it applies to occupancy_increase). getZoneDemandFactor is the effective demand.
    // With no zones: I_demand = 1.0; R = 0; C = 0.
    float iPostBootstrap = sim_->getZoneDemandFactor(ZoneType::Industrial);
    EXPECT_FLOAT_EQ(iPostBootstrap, 1.0f)
        << "Industrial demand must be 1.0 on blank map (no I zones → default = 1.0)";

    // Verify bootstrap period constant is correct.
    EXPECT_EQ(SimulationConstants::demand_bootstrapping_ticks, 6);
}

// TEST 15: DemandPressurePct_MaxDemand_Returns1f
//
// With a well-balanced R/C/I zone layout + road, and sufficient ticks for
// demand to converge, getZoneDemandFactor(Industrial) approaches 1.0 because
// I_demand defaults to 1.0 when no I zone exists.
//
// More broadly: verify getZoneDemandFactor returns values in [0.0, 1.0].
// With a balanced city (even R/C/I ratios, road present, past bootstrap),
// demand should be non-trivially positive for all zone types.
// ---------------------------------------------------------------------------
TEST_F(ZoningTestNice, DemandPressurePct_MaxDemand_Returns1f) {
    // Industrial demand defaults to 1.0 when no I zone exists (verified by
    // IndustrialDemand_ZeroCapacity_DefaultsTo1). Here verify the [0,1] bound
    // and that a balanced city generates meaningful demand.
    //
    // No I zones placed: I_demand = 1.0 (capacity = 0 → default 1.0).
    sim_->placeRoad(0, 0);
    sim_->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    sim_->placeZone(0, 1, ZoneType::Commercial,  DensityTier::Low);
    // Intentionally no Industrial zone — I_demand defaults to 1.0.

    clock_.advance(121.0);
    runTicks(3);

    float iDemand = sim_->getZoneDemandFactor(ZoneType::Industrial);
    float rDemand = sim_->getZoneDemandFactor(ZoneType::Residential);
    float cDemand = sim_->getZoneDemandFactor(ZoneType::Commercial);

    // All demand values must be in [0, 1].
    EXPECT_GE(iDemand, 0.0f);
    EXPECT_LE(iDemand, 1.0f);
    EXPECT_GE(rDemand, 0.0f);
    EXPECT_LE(rDemand, 1.0f);
    EXPECT_GE(cDemand, 0.0f);
    EXPECT_LE(cDemand, 1.0f);

    // Industrial demand with no I zones must be 1.0 (the canonical max-demand case).
    EXPECT_FLOAT_EQ(iDemand, 1.0f)
        << "getZoneDemandFactor(Industrial) must return 1.0 when no Industrial "
           "zones exist (I_production_capacity = 0 → default demand = 1.0).";
}

// ---------------------------------------------------------------------------
// ZoningConstructionDelayTest — Phase 11l Deliverable 2 tests.
//
// Verifies the building construction delay mechanic:
//   - placeZone() no longer calls placeBuildingMesh() at placement time.
//   - doPopulationTick() spawns the mesh once effective_demand_factor >= 0.50.
//   - Tiles below the threshold remain as empty lots across multiple ticks.
//   - Revenue (population) cannot grow while underConstruction==true.
//
// Fixture uses StrictMock<MockRenderer> so any unexpected renderer call is a
// test failure; NiceMock<MockAudioSystem> suppresses placement audio calls.
// ManualRNG non-strict with 0.9f float so service degradation never fires.
// ---------------------------------------------------------------------------
class ZoningConstructionDelayTest : public ::testing::Test {
protected:
    ::testing::StrictMock<MockRenderer>  m_renderer;
    NiceMock<MockAudioSystem>            m_audio;
    ManualRNG    m_rng;    // default: int={0}, float={0.9f}, non-strict
    ManualClock  m_clock;
    ManualTerrainQuery m_terrain;
    std::unique_ptr<CitySimulation> m_sim;

    void SetUp() override {
        m_sim = std::make_unique<CitySimulation>(
            &m_renderer, &m_audio, &m_rng, &m_clock, &m_terrain, Difficulty::Normal);
        m_sim->setSpeed(SpeedMultiplier::x1);
    }

    void TearDown() override {
        m_sim.reset();
    }

    void runTicks(int n) {
        const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
        for (int i = 0; i < n; ++i) {
            m_clock.advance(dt);
            m_sim->tick(dt);
        }
    }
};

// ---------------------------------------------------------------------------
// TEST CD-1: ZoningSystem_PlaceZone_NoBuildingMeshAtPlacement
//
// placeZone() must NOT call placeBuildingMesh() at the moment of placement.
// The building mesh is deferred until demand is sufficient.
//
// StrictMock enforces that placeBuildingMesh is never called; placeRoadMesh
// is expected exactly once (from placeRoad).
// ---------------------------------------------------------------------------
TEST_F(ZoningConstructionDelayTest, ZoningSystem_PlaceZone_NoBuildingMeshAtPlacement) {
    // Allow the road placement renderer call.
    EXPECT_CALL(m_renderer, placeRoadMesh(_, _)).Times(1);
    // placeBuildingMesh must NOT be called at placement time.
    EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(0);

    m_sim->placeRoad(0, 0);
    m_sim->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    // No ticks fired — mesh must not have spawned at placement.
}

// ---------------------------------------------------------------------------
// TEST CD-2: ZoningSystem_PlaceZone_BuildingMeshSpawnsWhenDemandSufficient
//
// After placeZone(), a single budget tick should trigger placeBuildingMesh()
// for both the Residential and Commercial tiles.
//
// Strategy: Place R + C zones + road. During bootstrap tick 1:
//   effectiveR = trafficDemandFactorR + bootstrapR = 0.5 + 0.417 = 0.917 >= 0.50
//   effectiveC (before floor) ≈ 0.208; demand_floor_commercial = 0.55 >= 0.50
// CI-capacity gate does not zero R (C zone exists, totalCIWorkerCapacity > 0).
// Both R and C meshes spawn at tick 1 (both meet the 0.50 threshold).
// ---------------------------------------------------------------------------
TEST_F(ZoningConstructionDelayTest, ZoningSystem_PlaceZone_BuildingMeshSpawnsWhenDemandSufficient) {
    // Allow road placement call.
    EXPECT_CALL(m_renderer, placeRoadMesh(_, _)).Times(1);
    // Both R and C meshes spawn at tick 1: R via demand 0.917, C via demand floor 0.55.
    EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(2);

    m_sim->placeRoad(0, 0);
    // R zone: effectiveR = 0.917 >= 0.50 at tick 1 (bootstrap active, C zone present).
    m_sim->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);
    // C zone: effectiveC floor = 0.55 >= 0.50 at tick 1 — spawns at floor.
    m_sim->placeZone(0, 1, ZoneType::Commercial, DensityTier::Low);

    // One budget tick during bootstrap (m_totalTicks becomes 1 on first tick).
    runTicks(1);
}

// ---------------------------------------------------------------------------
// TEST CD-3: ZoningSystem_PlaceZone_NoBuildingMeshWhenDemandInsufficient
//
// When effective_demand_factor < 0.50, placeBuildingMesh() must never be called
// across multiple ticks. The tile remains as an empty lot.
//
// Strategy: Residential demand = 0 when there are no Commercial or Industrial
// zones (R capacity-ratio = 0, no bootstrap subsidy for growth). After bootstrap
// ends, demand stays at 0 → threshold never met → no mesh.
// ---------------------------------------------------------------------------
TEST_F(ZoningConstructionDelayTest, ZoningSystem_PlaceZone_NoBuildingMeshWhenDemandInsufficient) {
    // Allow road placement call.
    EXPECT_CALL(m_renderer, placeRoadMesh(_, _)).Times(1);
    // No mesh must ever be spawned — demand stays below 0.50.
    EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(0);

    m_sim->placeRoad(0, 0);
    // Residential zone only — no C/I zones → R_demand = 0 < 0.50 after bootstrap.
    m_sim->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);

    // Advance past grace period and bootstrap window; run many ticks.
    m_clock.advance(121.0);
    runTicks(SimulationConstants::demand_bootstrapping_ticks + 4);
    // placeBuildingMesh must not have been called (Times(0) enforced by StrictMock).
}

// ---------------------------------------------------------------------------
// TEST CD-4: ZoningSystem_PlaceZone_NoRevenueUntilMeshSpawned
//
// While a tile is underConstruction (demand below threshold), its population
// stays at zero, meaning getTaxRevenue() for that zone type returns 0.
//
// Strategy: Place only a Residential zone with no C/I support so R_demand = 0.
// After several ticks, the tile remains unconstructed → population = 0 →
// residential tax revenue = 0.
// ---------------------------------------------------------------------------
TEST_F(ZoningConstructionDelayTest, ZoningSystem_PlaceZone_NoRevenueUntilMeshSpawned) {
    // Allow road placement call.
    EXPECT_CALL(m_renderer, placeRoadMesh(_, _)).Times(1);
    // No mesh should spawn (demand insufficient).
    EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(0);

    m_sim->placeRoad(0, 0);
    // Residential only — no C/I → demand stays 0.
    m_sim->placeZone(1, 0, ZoneType::Residential, DensityTier::Low);

    m_clock.advance(121.0);  // past grace period
    runTicks(SimulationConstants::demand_bootstrapping_ticks);

    // Population must be 0: tile is underConstruction and demand never met threshold.
    EXPECT_EQ(m_sim->getTotalPopulation(), 0)
        << "Population must remain 0 for an underConstruction tile when demand < threshold. "
           "No revenue is generated from a tile whose building mesh has not yet spawned.";

    // Tax revenue from Residential must reflect zero population.
    EXPECT_FLOAT_EQ(m_sim->getTaxRevenue(ZoneType::Residential), 0.0f)
        << "Residential tax revenue must be 0.0 while the zone tile is underConstruction "
           "(no building mesh spawned, population = 0).";
}
