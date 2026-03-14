// ui_manager_stinger_test.cpp — Phase 11 UIManager stinger unit tests.
//
// Tests verify that triggerStinger(StingerType::MILESTONE) is called by UIManager
// exactly when a City Rating tier transition occurs, and NOT fired on game load
// or when no tier transition has occurred.
//
// CMake target: ui_tests, label "unit".
// Mock policy: StrictMock for mocks under assertion; NiceMock for background mocks.
//
// Per architecture/audio-architecture/dynamic-soundscape.md and phase-11.md:
//   - stinger_milestone fires ONLY at City Rating tier transitions.
//   - 100K population milestone does NOT trigger stinger_milestone.
//   - Calling onGameLoaded() seeds the tier cache; no stinger fires on load.
//   - A second update() in the same tier fires no additional stinger (cooldown/same-tier).
//
// Implementation note: UIManager::update() calls getCityRating() exactly once per frame
// (stinger edge-detect section). m_lastMilestoneStingerFireTime is initialized to -5.0
// so the 5 s cooldown is satisfied from t=0 on a ManualClock.

#include "src/interfaces/IClock.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"  // CityRatingTier, SpeedMultiplier, DensityUnlockState
#include "src/ui/UIManager.h"
#include "MockCitySimulation.h"
#include "MockAudioSystem.h"
#include "MockUIBackend.h"
#include "ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::Return;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::AnyNumber;
using ::testing::AtLeast;

// ---------------------------------------------------------------------------
// Helper: install default ON_CALL stubs on a MockCitySimulation for all methods
// that UIManager::update() may call in any Phase. Using ON_CALL (not EXPECT_CALL)
// so that StrictMock does not fail on un-asserted calls made by future phases.
// ---------------------------------------------------------------------------
static void setDefaultSimCalls(NiceMock<MockCitySimulation>& sim)
{
    ON_CALL(sim, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
    ON_CALL(sim, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
    ON_CALL(sim, isPaused()).WillByDefault(Return(false));
    ON_CALL(sim, getTreasuryBalance()).WillByDefault(Return(10000.0f));
    ON_CALL(sim, getCurrentMonthlyRevenue()).WillByDefault(Return(500.0f));
    ON_CALL(sim, getOutstandingDebt()).WillByDefault(Return(0.0f));
    ON_CALL(sim, estimateMonthlyUpkeep()).WillByDefault(Return(200.0f));
    ON_CALL(sim, getTotalPopulation()).WillByDefault(Return(1000));
    ON_CALL(sim, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
    ON_CALL(sim, hasUndoPendingAction()).WillByDefault(Return(false));
    ON_CALL(sim, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));
    ON_CALL(sim, getDemandPressurePct(::testing::_)).WillByDefault(Return(0.5f));
    ON_CALL(sim, getTrafficDemandFactor(::testing::_)).WillByDefault(Return(0.5f));
    ON_CALL(sim, getDensityUnlockState()).WillByDefault(Return(DensityUnlockState{}));
    ON_CALL(sim, getNextUnlockThreshold(::testing::_)).WillByDefault(Return(5000.0f));
}

// ---------------------------------------------------------------------------
// Test a: UIManager_StingerMilestone_FiresOnTierTransition
//
// When UIManager::update() detects a Village→Town tier transition,
// triggerStinger(StingerType::MILESTONE) must be called exactly once.
// A second update() in the same tier must NOT trigger another stinger.
// ---------------------------------------------------------------------------
TEST(UIManagerStingerTest, UIManager_StingerMilestone_FiresOnTierTransition)
{
    NiceMock<MockUIBackend>      backend;
    NiceMock<MockCitySimulation> sim;
    NiceMock<MockAudioSystem>    audio;
    ManualClock                  clock;

    setDefaultSimCalls(sim);

    // First update(): getCityRating() returns Village — no tier change, no stinger.
    // Second and subsequent: returns Town — triggers stinger on second call only
    // (m_previousCityRating becomes Town after 2nd, so 3rd sees no transition).
    EXPECT_CALL(sim, getCityRating())
        .WillOnce(Return(CityRatingTier::Village))
        .WillRepeatedly(Return(CityRatingTier::Town));

    EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE))
        .Times(1);

    UIManager uiManager(&backend, &audio, &sim, &clock);

    uiManager.update(0.016f);  // Village — no tier change yet, no stinger
    uiManager.update(0.016f);  // Town — tier transition detected, stinger fires
    uiManager.update(0.016f);  // Still Town — no transition, no stinger
    // NiceMock on audio enforces Times(1): no second triggerStinger allowed.
}

// ---------------------------------------------------------------------------
// Test b: UIManager_StingerMilestone_NoStingerOnLoad
//
// Calling onGameLoaded() seeds the tier cache to the current rating.
// A subsequent update() with the same rating must NOT trigger a stinger.
// ---------------------------------------------------------------------------
TEST(UIManagerStingerTest, UIManager_StingerMilestone_NoStingerOnLoad)
{
    NiceMock<MockUIBackend>      backend;
    NiceMock<MockCitySimulation> sim;
    NiceMock<MockAudioSystem>    audio;
    ManualClock                  clock;

    setDefaultSimCalls(sim);

    // Always return City rating.
    ON_CALL(sim, getCityRating()).WillByDefault(Return(CityRatingTier::City));

    // triggerStinger(MILESTONE) must NEVER be called.
    EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE))
        .Times(0);

    UIManager uiManager(&backend, &audio, &sim, &clock);

    // onGameLoaded() seeds m_previousCityRating to City — no stinger on next update.
    uiManager.onGameLoaded();
    uiManager.update(0.016f);  // Same tier as cached — no stinger.
}

// ---------------------------------------------------------------------------
// Test c: UIManager_StingerMilestone_No100KStinger
//
// 100K population does NOT correspond to a CityRatingTier boundary.
// Even while population is in the Metropolis range (> 50K), if the City Rating
// tier stays at Metropolis for all calls, no stinger fires.
// ---------------------------------------------------------------------------
TEST(UIManagerStingerTest, UIManager_StingerMilestone_No100KStinger)
{
    NiceMock<MockUIBackend>      backend;
    NiceMock<MockCitySimulation> sim;
    NiceMock<MockAudioSystem>    audio;
    ManualClock                  clock;

    setDefaultSimCalls(sim);

    // getCityRating() always returns Metropolis — no tier transition across all calls.
    ON_CALL(sim, getCityRating()).WillByDefault(Return(CityRatingTier::Metropolis));
    ON_CALL(sim, getTotalPopulation()).WillByDefault(Return(100000));

    // triggerStinger(MILESTONE) must NEVER be called (100K is not a tier transition).
    EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE))
        .Times(0);

    UIManager uiManager(&backend, &audio, &sim, &clock);

    // Seed m_previousCityRating to Metropolis — population crossing 100K
    // does not correspond to a tier boundary.
    uiManager.onGameLoaded();
    uiManager.update(0.016f);  // Metropolis — no transition from seeded state.
    uiManager.update(0.016f);  // Still Metropolis at 100K — no stinger.
}
