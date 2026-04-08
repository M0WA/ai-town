// subsystem_unit_test.cpp — Direct unit tests for the Phase 11q1 sub-system structs:
// SimTiming, Economy, Zoning, Traffic.  These tests instantiate the sub-system
// classes directly (without CitySimulation) to cover branches unreachable via the
// existing integration-style tests.
//
// Coverage targets (see Phase 11q1 coverage gate):
//   SimTiming  — tick() with Paused/x3/x10 speed paths
//   Economy    — isFirstRevenueTicked() default, getDensityUnlockScale() all
//                difficulty branches, bond issuance, final loan repayment
//   Zoning     — maxPopulationForTile() all zone/density combinations,
//                serviceBuildingsRef() getter, getServiceCoverage() with buildings
//   Traffic    — removeVehiclesForRoad(), getAgentPositions() with vehicles,
//                getIntersectionSignalStates() with signals,
//                getRoadSegmentSpeeds() with road tiles,
//                computeEffectiveDemand() with Commercial/Industrial Medium/High tiles,
//                computeTrafficDemand() path through smoothstep/travelTimeDemand

#include "src/simulation/SimTiming.h"
#include "src/simulation/Economy.h"
#include "src/simulation/Population.h"
#include "src/simulation/Zoning.h"
#include "src/simulation/Traffic.h"
#include "src/simulation/simulation_constants.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/IClock.h"

#include <gtest/gtest.h>
#include <queue>

// Minimal IClock implementation used only for Economy unit tests that accept IClock&.
// Economy::checkAndIssueForcedLoan marks the clock parameter as unused (/*clock*/),
// so nowSeconds() is never actually called in these tests.
struct FixedClock : IClock {
    double nowSeconds() const override { return 200.0; }
};

// ============================================================================
// SimTiming unit tests
// ============================================================================

// Tick with Paused speed must return 0 immediately (SimTiming.cpp line 31).
TEST(SimTimingUnit, Tick_Paused_Returns0) {
    SimTiming st;
    int ticks = st.tick(1.0f, SpeedMultiplier::Paused);
    EXPECT_EQ(ticks, 0);
    EXPECT_EQ(st.m_totalTicks, 0);
}

// Tick with x3 speed fires budget ticks proportionally.
TEST(SimTimingUnit, Tick_x3_FiresBudgetTicks) {
    SimTiming st;
    // 30s * 3x = 90 sim-seconds → 3 budget ticks
    int ticks = st.tick(30.0f, SpeedMultiplier::x3);
    EXPECT_EQ(ticks, 3);
    EXPECT_EQ(st.m_totalTicks, 3);
}

// Tick with x10 speed fires 10 budget ticks per real 30s.
TEST(SimTimingUnit, Tick_x10_FiresBudgetTicks) {
    SimTiming st;
    int ticks = st.tick(30.0f, SpeedMultiplier::x10);
    EXPECT_EQ(ticks, 10);
}

// setPaused(true) on an x1 sim puts it in Paused state; subsequent tick returns 0.
TEST(SimTimingUnit, SetPaused_StopsTicking) {
    SimTiming st;
    st.setSpeed(SpeedMultiplier::x1);
    st.setPaused(true);
    EXPECT_TRUE(st.isPaused());
    int ticks = st.tick(30.0f, st.m_speed);
    EXPECT_EQ(ticks, 0);
}

// ============================================================================
// Economy unit tests
// ============================================================================

// isFirstRevenueTicked() returns false on a default-constructed Economy (lines 79-80).
TEST(EconomyUnit, IsFirstRevenueTicked_DefaultFalse) {
    Economy e;
    EXPECT_FALSE(e.isFirstRevenueTicked());
}

// getDensityUnlockScale() for Easy difficulty (lines 98-100).
TEST(EconomyUnit, GetDensityUnlockScale_Easy) {
    Economy e;
    Population p;
    float scale = e.getDensityUnlockScale(p, Difficulty::Easy);
    EXPECT_FLOAT_EQ(scale, SimulationConstants::density_unlock_scale_easy);
}

// getDensityUnlockScale() for Hard difficulty (lines 102-104).
TEST(EconomyUnit, GetDensityUnlockScale_Hard) {
    Economy e;
    Population p;
    float scale = e.getDensityUnlockScale(p, Difficulty::Hard);
    EXPECT_FLOAT_EQ(scale, SimulationConstants::density_unlock_scale_hard);
}

// setInitialFunds() with Hard difficulty sets bond_max_uses_hard (line 119).
TEST(EconomyUnit, SetInitialFunds_Hard_SetsHardBondUses) {
    Economy e;
    e.setInitialFunds(50000, Difficulty::Hard);
    EXPECT_EQ(e.m_outstandingBondUses, SimulationConstants::bond_max_uses_hard);
    EXPECT_EQ(e.m_treasury, 50000LL);
}

// setInitialFunds() with Easy difficulty sets bond_max_uses_easy.
TEST(EconomyUnit, SetInitialFunds_Easy_SetsEasyBondUses) {
    Economy e;
    e.setInitialFunds(20000, Difficulty::Easy);
    EXPECT_EQ(e.m_outstandingBondUses, SimulationConstants::bond_max_uses_easy);
}

// ============================================================================
// Zoning unit tests
// ============================================================================

// maxPopulationForTile for Commercial/Medium — covers line 91 branch (previously
// only Residential/Industrial/Low variants were exercised via integration tests).
TEST(ZoningUnit, MaxPop_Commercial_Medium) {
    int pop = Zoning::maxPopulationForTile(ZoneType::Commercial, DensityTier::Medium);
    EXPECT_EQ(pop, SimulationConstants::max_pop_commercial_medium);
}

// maxPopulationForTile for Industrial/Medium and Industrial/High.
TEST(ZoningUnit, MaxPop_Industrial_Medium) {
    int pop = Zoning::maxPopulationForTile(ZoneType::Industrial, DensityTier::Medium);
    EXPECT_EQ(pop, SimulationConstants::max_pop_industrial_medium);
}

TEST(ZoningUnit, MaxPop_Industrial_High) {
    int pop = Zoning::maxPopulationForTile(ZoneType::Industrial, DensityTier::High);
    EXPECT_EQ(pop, SimulationConstants::max_pop_industrial_high);
}

TEST(ZoningUnit, MaxPop_Residential_High) {
    int pop = Zoning::maxPopulationForTile(ZoneType::Residential, DensityTier::High);
    EXPECT_EQ(pop, SimulationConstants::max_pop_residential_high);
}

TEST(ZoningUnit, MaxPop_Commercial_High) {
    int pop = Zoning::maxPopulationForTile(ZoneType::Commercial, DensityTier::High);
    EXPECT_EQ(pop, SimulationConstants::max_pop_commercial_high);
}

// serviceBuildingsRef() returns a non-const reference to the internal vector (lines 135-136).
TEST(ZoningUnit, ServiceBuildingsRef_EmptyByDefault) {
    Zoning z;
    std::vector<ServiceBuilding>& ref = z.serviceBuildingsRef();
    EXPECT_TRUE(ref.empty());
    // Modifying via the ref is observable through serviceBuildings().
    ServiceBuilding sb;
    sb.x = 5; sb.z = 5;
    sb.type = ServiceBuildingType::FireStation;
    sb.degraded = false;
    ref.push_back(sb);
    EXPECT_EQ(z.serviceBuildings().size(), 1u);
}

// ============================================================================
// Traffic unit tests
// ============================================================================

// removeVehiclesForRoad() is a documented no-op that must compile and not crash
// (covers lines 293-296 — the function body is intentionally empty per spec).
TEST(TrafficUnit, RemoveVehiclesForRoad_NoOp) {
    Traffic t;
    // No vehicles, no signal — call must return without side effects.
    t.removeVehiclesForRoad(0, 0, nullptr);
    EXPECT_TRUE(t.m_trafficVehicles.empty());
}

// getTrafficDemandFactor with Residential/Commercial/Industrial zones.
// The Commercial and Industrial paths were exercised by integration tests, but
// this verifies the initial null-path-default value for all three branches.
TEST(TrafficUnit, GetTrafficDemandFactor_AllZones_DefaultValue) {
    Traffic t;
    // Initial value is null_path_demand_default for all zones.
    EXPECT_FLOAT_EQ(t.getTrafficDemandFactor(ZoneType::Residential),
                    SimulationConstants::null_path_demand_default);
    EXPECT_FLOAT_EQ(t.getTrafficDemandFactor(ZoneType::Commercial),
                    SimulationConstants::null_path_demand_default);
    EXPECT_FLOAT_EQ(t.getTrafficDemandFactor(ZoneType::Industrial),
                    SimulationConstants::null_path_demand_default);
}

// getAgentPositions() with one vehicle in m_trafficVehicles — covers the loop body
// that builds each AgentState (Traffic.cpp lines 46-56).
TEST(TrafficUnit, GetAgentPositions_WithOneVehicle) {
    Traffic t;
    TrafficVehicle v;
    v.id         = 42;
    v.srcX       = 3;
    v.srcZ       = 7;
    v.headingDeg = 90.0f;
    v.zone       = ZoneType::Commercial;
    v.worldX     = 35.0f;
    v.worldZ     = 75.0f;
    t.m_trafficVehicles.push_back(v);

    std::vector<AgentState> agents = t.getAgentPositions();
    ASSERT_EQ(agents.size(), 1u);
    EXPECT_EQ(agents[0].agentId,    42u);
    EXPECT_EQ(agents[0].tileX,      3);
    EXPECT_EQ(agents[0].tileZ,      7);
    EXPECT_FLOAT_EQ(agents[0].headingDeg, 90.0f);
    EXPECT_EQ(agents[0].zone,       ZoneType::Commercial);
}

// getIntersectionSignalStates() with one green-phase signal — covers the loop body
// and the phase computation (Traffic.cpp lines 63-72).
TEST(TrafficUnit, GetIntersectionSignalStates_GreenPhase) {
    Traffic t;
    TrafficSignal sig;
    sig.tileX       = 5;
    sig.tileZ       = 5;
    sig.phaseSeconds = 30.0f;
    sig.phaseTimer  = 5.0f; // 5 < 15 (half of 30) → Green
    t.m_trafficSignals.push_back(sig);

    std::vector<IntersectionSignalState> states = t.getIntersectionSignalStates();
    ASSERT_EQ(states.size(), 1u);
    EXPECT_EQ(states[0].tileX, 5);
    EXPECT_EQ(states[0].tileZ, 5);
    EXPECT_EQ(states[0].phase, SignalPhase::Green);
}

// getIntersectionSignalStates() with a red-phase signal.
TEST(TrafficUnit, GetIntersectionSignalStates_RedPhase) {
    Traffic t;
    TrafficSignal sig;
    sig.tileX       = 2;
    sig.tileZ       = 3;
    sig.phaseSeconds = 30.0f;
    sig.phaseTimer  = 20.0f; // 20 >= 15 (half of 30) → Red
    t.m_trafficSignals.push_back(sig);

    std::vector<IntersectionSignalState> states = t.getIntersectionSignalStates();
    ASSERT_EQ(states.size(), 1u);
    EXPECT_EQ(states[0].phase, SignalPhase::Red);
}

// getRoadSegmentSpeeds() with one road tile and no signals — covers the loop body
// and the per-tile speed entry (Traffic.cpp lines 78-96).
TEST(TrafficUnit, GetRoadSegmentSpeeds_WithRoadTile) {
    Traffic t;
    Zoning  z;

    // Insert a road tile at (3, 4)
    TileData td;
    td.isRoad = true;
    z.tilesRef()[Zoning::tileKey(3, 4)] = td;

    std::vector<RoadSegmentSpeed> speeds = t.getRoadSegmentSpeeds(z);
    ASSERT_EQ(speeds.size(), 1u);
    EXPECT_EQ(speeds[0].tileX, 3);
    EXPECT_EQ(speeds[0].tileZ, 4);
    EXPECT_FLOAT_EQ(speeds[0].speedFraction, 1.0f); // no signals → full speed
}

// computeEffectiveDemand() with Commercial/Medium and Industrial/High tiles — forces
// Traffic::maxPopulationForTile() to return the Medium/High-density capacities
// (Traffic.cpp lines 117-139, called at lines 416 and 420).
TEST(TrafficUnit, ComputeEffectiveDemand_CommercialMediumIndustrialHigh) {
    Traffic t;
    Zoning  z;

    // Residential tile at (1, 0) for worker population
    TileData res;
    res.isZoned    = true;
    res.zone       = ZoneType::Residential;
    res.density    = DensityTier::Low;
    res.population = 50.0f;
    z.tilesRef()[Zoning::tileKey(1, 0)] = res;

    // Commercial tile at (2, 0) — Medium density
    TileData com;
    com.isZoned    = true;
    com.zone       = ZoneType::Commercial;
    com.density    = DensityTier::Medium;
    com.population = 30.0f;
    z.tilesRef()[Zoning::tileKey(2, 0)] = com;

    // Industrial tile at (3, 0) — High density
    TileData ind;
    ind.isZoned    = true;
    ind.zone       = ZoneType::Industrial;
    ind.density    = DensityTier::High;
    ind.population = 20.0f;
    z.tilesRef()[Zoning::tileKey(3, 0)] = ind;

    // computeEffectiveDemand calls maxPopulationForTile for Commercial and Industrial tiles.
    t.computeEffectiveDemand(z, 10);

    // After the call the demand pressure arrays are set; just verify it ran without crash.
    float cDemand = t.m_demandPressurePct[static_cast<int>(ZoneType::Commercial)];
    float iDemand = t.m_demandPressurePct[static_cast<int>(ZoneType::Industrial)];
    EXPECT_GE(cDemand, 0.0f);
    EXPECT_LE(cDemand, 1.0f);
    EXPECT_GE(iDemand, 0.0f);
    EXPECT_LE(iDemand, 1.0f);
}

// computeTrafficDemand() with a road-adjacent Commercial zone — drives
// adjacentCount > 0, which triggers the travelTimeDemand/smoothstep code path
// (Traffic.cpp lines 102-112).
TEST(TrafficUnit, ComputeTrafficDemand_RoadAdjacentZone_TriggersTravelTimeDemand) {
    Traffic t;
    Zoning  z;

    // Road tile at (0, 0)
    TileData road;
    road.isRoad = true;
    z.tilesRef()[Zoning::tileKey(0, 0)] = road;

    // Commercial zone at (1, 0) — adjacent to the road tile
    TileData com;
    com.isZoned    = true;
    com.zone       = ZoneType::Commercial;
    com.density    = DensityTier::Low;
    com.population = 10.0f;
    z.tilesRef()[Zoning::tileKey(1, 0)] = com;

    // This tick will have adjacentCount > 0 for Commercial, so travelTimeDemand
    // and smoothstep are called.
    t.computeTrafficDemand(z, 1);

    // The demand factor should now differ from the null-path default for Commercial.
    // We only verify it is in [0, 1] — the exact value depends on travel-time constants.
    float cDemand = t.m_trafficDemandFactorC;
    EXPECT_GE(cDemand, 0.0f);
    EXPECT_LE(cDemand, 1.0f);
}

// ============================================================================
// Economy unit tests — continued (bond issuance, loan final repayment)
// ============================================================================

// getDensityUnlockScale() for Normal difficulty — covers the Normal case
// (Economy.cpp line 101); the existing Easy/Hard tests covered lines 100 and 102.
TEST(EconomyUnit, GetDensityUnlockScale_Normal) {
    Economy e;
    Population p;
    float scale = e.getDensityUnlockScale(p, Difficulty::Normal);
    EXPECT_FLOAT_EQ(scale, SimulationConstants::density_unlock_scale_normal);
}

// checkAndIssueForcedLoan() — bond path: outstandingDebt >= debtCap AND
// m_outstandingBondUses > 0 triggers bond issuance (Economy.cpp lines 229-252).
TEST(EconomyUnit, CheckAndIssueForcedLoan_BondIssuedWhenDebtExceedsCap) {
    Economy e;
    FixedClock clock;
    std::queue<SimulationNotification> notifications;

    // Deficit condition required by the early-return guards.
    e.m_budgetSurplusPct   = -0.50f;
    e.m_firstRevenueTicked = true;
    e.m_loanCooldownTicks  = 0;
    e.m_outstandingBondUses = 2;

    // Revenue cap = max(currentMonthlyRevenue, 1000) = 1000; debtCap = 3000.
    e.m_currentMonthlyRevenue = 0.0f; // forces revenueCap to 1000

    // Add a loan whose principal exceeds debtCap (3000).
    LoanEntry loan;
    loan.principal          = 5000;
    loan.remainingPrincipal = 5000; // outstandingDebt = 5000 > debtCap 3000
    loan.ticksRemaining     = 10;
    loan.isBond             = false;
    e.m_loans.push_back(loan);
    e.m_treasury = 1000;

    e.checkAndIssueForcedLoan(/*inGracePeriod=*/false, clock, /*audio=*/nullptr, notifications);

    // Bond must have been issued.
    EXPECT_EQ(e.m_outstandingBondUses, 1);
    // Treasury increased by bondPrincipal = 2 * 5000 = 10000.
    EXPECT_EQ(e.m_treasury, 1000LL + 10000LL);
    // A new bond loan entry must be in the loans list.
    ASSERT_EQ(e.m_loans.size(), 2u);
    EXPECT_TRUE(e.m_loans.back().isBond);
    // Notification must be BondIssued.
    ASSERT_FALSE(notifications.empty());
    EXPECT_EQ(notifications.front().type, NotificationType::BondIssued);
}

// processLoanRepayments() — final repayment branch: ticksRemaining == 1 takes the
// else path, paying off remainingPrincipal in one shot (Economy.cpp lines 309-310).
TEST(EconomyUnit, ProcessLoanRepayments_FinalTickPaysOffLoan) {
    Economy e;
    e.m_treasury = 100000;

    LoanEntry loan;
    loan.principal          = 1200;
    loan.remainingPrincipal = 100; // last instalment
    loan.ticksRemaining     = 1;   // final tick → else branch
    loan.isBond             = false;
    e.m_loans.push_back(loan);

    e.processLoanRepayments();

    // The final principal (100) is deducted and the loan is removed.
    EXPECT_EQ(e.m_treasury, 100000LL - 100LL);
    EXPECT_TRUE(e.m_loans.empty());
}
