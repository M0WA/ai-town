#pragma once

#include "src/interfaces/ICitySimulation.h"
#include "gmock/gmock.h"

// MockCitySimulation — GMock implementation of ICitySimulation.
// 13 MOCK_METHOD entries total:
//   1 inherited from ISimulationPauser (setPaused)
//   12 declared in ICitySimulation
// Source location: tests/ui/mock_city_simulation.h
class MockCitySimulation : public ICitySimulation {
public:
    MOCK_METHOD(void, setPaused, (bool paused), (override));
    MOCK_METHOD(void, setSpeed,  (SpeedMultiplier speed), (override));
    MOCK_METHOD(bool, isPaused,  (), (const, override));
    MOCK_METHOD(SpeedMultiplier, getSpeed, (), (const, override));

    // Economy/treasury queries:
    MOCK_METHOD(float, getTreasuryBalance,       (), (const, override));
    MOCK_METHOD(float, getCurrentMonthlyRevenue, (), (const, override));
    MOCK_METHOD(float, getOutstandingDebt,       (), (const, override));
    MOCK_METHOD(float, estimateMonthlyUpkeep,    (), (const, override));
    MOCK_METHOD(float, getNextUnlockThreshold,   (Difficulty d), (const, override));

    // City rating:
    MOCK_METHOD(int, getCityRating, (), (const, override));

    // Demand pressure:
    MOCK_METHOD(float, getDemandPressurePct, (ZoneType zone), (const, override));

    // Undo state:
    MOCK_METHOD(bool,   hasUndoPendingAction,     (), (const, override));
    MOCK_METHOD(double, getUndoExpiryTimeSeconds, (), (const, override));
};
