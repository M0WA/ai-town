#pragma once

#include "src/interfaces/ICitySimulation.h"
#include "gmock/gmock.h"

// MockCitySimulation — GMock implementation of ICitySimulation.
// 19 MOCK_METHOD entries total:
//   1 inherited from ISimulationPauser (setPaused)
//   18 declared in ICitySimulation
// Source location: tests/ui/mock_city_simulation.h
//
// SEMANTIC DISTINCTION: getDemandPressurePct vs getTrafficDemandFactor
//   getDemandPressurePct  — post-combination, post-floor aggregate; used by HUD demand bars.
//   getTrafficDemandFactor — raw traffic-only rolling-window multiplier; used for save/load only.
class MockCitySimulation : public ICitySimulation {
public:
    MOCK_METHOD(void, setPaused, (bool paused), (override));
    MOCK_METHOD(void, setSpeed,  (SpeedMultiplier speed), (override));
    MOCK_METHOD(bool, isPaused,  (), (const, override));
    MOCK_METHOD(SpeedMultiplier, getSpeedMultiplier, (), (const, override));

    // Economy/treasury queries:
    MOCK_METHOD(float, getTreasuryBalance,       (), (const, override));
    MOCK_METHOD(float, getCurrentMonthlyRevenue, (), (const, override));
    MOCK_METHOD(float, getOutstandingDebt,       (), (const, override));
    MOCK_METHOD(float, estimateMonthlyUpkeep,    (), (const, override));
    MOCK_METHOD(float, getNextUnlockThreshold,   (Difficulty d), (const, override));

    // City rating:
    MOCK_METHOD(CityRatingTier, getCityRating, (), (const, override));

    // Demand pressure — UI display aggregate (post-floor, post-bootstrap, post-combination).
    // Used by HUD demand bars. NOT the same as getTrafficDemandFactor (see below).
    MOCK_METHOD(float, getDemandPressurePct, (ZoneType zone), (const, override));

    // Population:
    MOCK_METHOD(int, getTotalPopulation, (), (const, override));

    // Undo state:
    MOCK_METHOD(bool,   hasUndoPendingAction,     (), (const, override));
    MOCK_METHOD(double, getUndoExpiryTimeSeconds, (), (const, override));

    // Game-over flow — deficit streak accessor. Returns 0 during grace period.
    // Cross-reference: architecture/game-design/game-over-flow.md.
    MOCK_METHOD(int, getConsecutiveDeficitMonths, (), (const, override));

    // Traffic demand factor — internal traffic-only multiplier from rolling travel-time window,
    // BEFORE bootstrap/floor combination. R/C = 5-tick window; I = 3-tick window.
    // Exposed for Phase 8 save/load round-trip serialization only; HUD uses getDemandPressurePct.
    // Cross-reference: implementation/phase-3.md (Traffic demand factor serialization).
    MOCK_METHOD(float, getTrafficDemandFactor, (ZoneType zone), (const, override));

    // Density-unlock state accessor. Returns consecutive-month counters and unlock flags for all
    // 6 density tiers. Cross-reference: implementation/phase-3.md (getDensityUnlockState deliverable).
    MOCK_METHOD(DensityUnlockState, getDensityUnlockState, (), (const, override));
};
