#pragma once

#include "src/interfaces/ICitySimulation.h"
#include "gmock/gmock.h"

// MockCitySimulation — GMock implementation of ICitySimulation.
// Source location: tests/ui/mock_city_simulation.h
//
// SEMANTIC DISTINCTION: getDemandPressurePct vs getTrafficDemandFactor
//   getDemandPressurePct  — post-combination, post-floor aggregate; used by HUD demand bars.
//   getTrafficDemandFactor — raw traffic-only rolling-window multiplier; used for save/load only.
class MockCitySimulation : public ICitySimulation {
public:
    // --- ISimulationPauser ---
    MOCK_METHOD(void, setPaused, (bool paused), (override));

    // --- Speed control ---
    MOCK_METHOD(void, setSpeed,  (SpeedMultiplier speed), (override));
    MOCK_METHOD(bool, isPaused,  (), (const, override));
    MOCK_METHOD(SpeedMultiplier, getSpeedMultiplier, (), (const, override));

    // --- Economy/treasury queries ---
    MOCK_METHOD(float, getTreasuryBalance,       (), (const, override));
    MOCK_METHOD(float, getCurrentMonthlyRevenue, (), (const, override));
    MOCK_METHOD(float, getOutstandingDebt,       (), (const, override));
    MOCK_METHOD(float, estimateMonthlyUpkeep,    (), (const, override));
    MOCK_METHOD(float, getNextUnlockThreshold,   (Difficulty d), (const, override));

    // --- City rating ---
    MOCK_METHOD(CityRatingTier, getCityRating, (), (const, override));

    // --- Demand pressure (city-wide aggregate; HUD demand bars) ---
    MOCK_METHOD(float, getDemandPressurePct, (ZoneType zone), (const, override));

    // --- Population ---
    MOCK_METHOD(int, getTotalPopulation, (), (const, override));

    // --- Undo state ---
    MOCK_METHOD(bool,   hasUndoPendingAction,     (), (const, override));
    MOCK_METHOD(double, getUndoExpiryTimeSeconds, (), (const, override));

    // --- Game-over deficit streak ---
    MOCK_METHOD(int, getConsecutiveDeficitMonths, (), (const, override));

    // --- Traffic demand factor (save/load serialization only) ---
    MOCK_METHOD(float, getTrafficDemandFactor, (ZoneType zone), (const, override));

    // --- Density-unlock state ---
    MOCK_METHOD(DensityUnlockState, getDensityUnlockState, (), (const, override));

    // --- Simulation time (Phase 6 delivery) ---
    MOCK_METHOD(SimulationTime, getSimulationTime, (), (const, override));

    // --- Simulation event queue (Phase 6 delivery) ---
    MOCK_METHOD(bool, pollPendingNotification, (SimulationNotification& out), (override));
    MOCK_METHOD(int,  consumeBudgetTicks, (), (override));

    // --- Tax rate (Phase 6 delivery) ---
    MOCK_METHOD(void,  setTaxRate, (ZoneType zone, float rate), (override));
    MOCK_METHOD(float, getTaxRate, (ZoneType zone), (const, override));

    // --- Budget line-item accessors (Phase 6 delivery) ---
    MOCK_METHOD(float, getTaxRevenue,         (ZoneType zone), (const, override));
    MOCK_METHOD(float, getWagesCost,          (), (const, override));
    MOCK_METHOD(float, getRoadMaintenanceCost, (), (const, override));
    MOCK_METHOD(float, getServiceUpkeepCost,  (), (const, override));
    MOCK_METHOD(float, getUtilityFeeRevenue,  (), (const, override));

    // --- Zone/road action methods (Phase 6 delivery) ---
    MOCK_METHOD(void, placeZone,      (int tileX, int tileZ, ZoneType type, DensityTier tier,
                                       int earthworksCostOverride), (override));
    MOCK_METHOD(void, placeRoad,      (int tileX, int tileZ, int earthworksCostOverride), (override));
    MOCK_METHOD(void, demolishTile,   (int tileX, int tileZ), (override));
    MOCK_METHOD(void, undoLastAction, (), (override));
    MOCK_METHOD(void, placeServiceBuilding,
                (int tileX, int tileZ, ServiceBuildingType type,
                 int earthworksCostOverride),
                (override));

    // --- Per-tile query (Phase 6 delivery) ---
    MOCK_METHOD(QueryResult, queryTile, (int tileX, int tileZ), (const, override));

    // --- Road proximity check (Phase 11m bug fix) ---
    MOCK_METHOD(bool, isWithinRoadRange, (int x, int z, DensityTier tier), (const, override));

    // --- Bond use count (Phase 6 delivery) ---
    MOCK_METHOD(int, getOutstandingBondUses, (), (const, override));

    // --- Time of day (Phase 6 delivery) ---
    MOCK_METHOD(TimeOfDay, getTimeOfDay, (), (const, override));

    // -----------------------------------------------------------------------
    // Phase 11d — Per-frame simulation state query methods
    // -----------------------------------------------------------------------
    MOCK_METHOD((std::vector<AgentState>),              getAgentPositions,           (), (const, override));
    MOCK_METHOD((std::vector<IntersectionSignalState>), getIntersectionSignalStates,  (), (const, override));
    MOCK_METHOD((std::vector<RoadSegmentSpeed>),        getRoadSegmentSpeeds,         (), (const, override));
    MOCK_METHOD((std::vector<ServiceCoverageTile>),     getServiceCoverage,           (), (const, override));

    // Phase 11m: reset simulation state for a new game.
    MOCK_METHOD(void, reset, (int64_t startingFunds), (override));

    MOCK_METHOD(bool, applyLoadedJson, (const std::string& json), (override));

    // Map dimension getters (Bug 2 fix: load-game flow).
    MOCK_METHOD(int, getMapTilesX, (), (const, override));
    MOCK_METHOD(int, getMapTilesZ, (), (const, override));
};
