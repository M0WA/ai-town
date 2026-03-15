// ui_manager_stinger_test.cpp — Phase 11 UIManager stinger unit tests.
//
// Tests verify that triggerStinger(StingerType::MILESTONE) is called by UIManager
// exactly when a City Rating tier transition occurs, and NOT fired on game load
// or when no tier transition has occurred.
//
// CMake target: ui_tests, label "unit".
// Mock policy (per phase-11.md): StrictMock<MockCitySimulation> and
//   StrictMock<MockAudioSystem> for direct assertions; NiceMock<MockUIBackend>
//   for background backend calls (element visibility, text, alpha) not under
//   assertion — consistent with the UIManagerModalTest fixture pattern.
//
// StrictMock requires EXPECT_CALL coverage for every method UIManager::update()
// invokes. Background sim methods use Times(AnyNumber()).WillRepeatedly(Return(...))
// so that future-phase additions to update() do not silently pass.
//
// Per architecture/audio-architecture/dynamic-soundscape.md and phase-11.md:
//   - stinger_milestone fires ONLY at City Rating tier transitions.
//   - 100K population milestone does NOT trigger stinger_milestone.
//   - Calling onGameLoaded() seeds the tier cache; no stinger fires on load.
//   - A second update() in the same tier fires no additional stinger.
//
// Implementation note: UIManager::update() calls getCityRating() exactly once per
// frame (stinger edge-detect section). m_lastMilestoneStingerFireTime is initialised
// to -5.0 so the 5 s cooldown is satisfied from t=0 on a ManualClock.

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

// ---------------------------------------------------------------------------
// Helper: install EXPECT_CALL stubs on a StrictMock<MockCitySimulation> for
// all background methods UIManager::update() and onGameLoaded() call.
// AnyNumber() permits zero-or-more calls so future phases can add queries
// without breaking these tests; WillRepeatedly sets safe return values.
// getCityRating() is NOT set here — individual tests set it via EXPECT_CALL.
// ---------------------------------------------------------------------------
static void setDefaultSimCalls(StrictMock<MockCitySimulation>& sim)
{
    EXPECT_CALL(sim, getConsecutiveDeficitMonths())
        .Times(AnyNumber()).WillRepeatedly(Return(0));
    EXPECT_CALL(sim, getSpeedMultiplier())
        .Times(AnyNumber()).WillRepeatedly(Return(SpeedMultiplier::x1));
    EXPECT_CALL(sim, isPaused())
        .Times(AnyNumber()).WillRepeatedly(Return(false));
    EXPECT_CALL(sim, getTreasuryBalance())
        .Times(AnyNumber()).WillRepeatedly(Return(10000.0f));
    EXPECT_CALL(sim, getCurrentMonthlyRevenue())
        .Times(AnyNumber()).WillRepeatedly(Return(500.0f));
    EXPECT_CALL(sim, getOutstandingDebt())
        .Times(AnyNumber()).WillRepeatedly(Return(0.0f));
    EXPECT_CALL(sim, estimateMonthlyUpkeep())
        .Times(AnyNumber()).WillRepeatedly(Return(200.0f));
    EXPECT_CALL(sim, getTotalPopulation())
        .Times(AnyNumber()).WillRepeatedly(Return(1000));
    EXPECT_CALL(sim, hasUndoPendingAction())
        .Times(AnyNumber()).WillRepeatedly(Return(false));
    EXPECT_CALL(sim, getUndoExpiryTimeSeconds())
        .Times(AnyNumber()).WillRepeatedly(Return(0.0));
    EXPECT_CALL(sim, getDemandPressurePct(::testing::_))
        .Times(AnyNumber()).WillRepeatedly(Return(0.5f));
    EXPECT_CALL(sim, getTrafficDemandFactor(::testing::_))
        .Times(AnyNumber()).WillRepeatedly(Return(0.5f));
    EXPECT_CALL(sim, getDensityUnlockState())
        .Times(AnyNumber()).WillRepeatedly(Return(DensityUnlockState{}));
    EXPECT_CALL(sim, getNextUnlockThreshold(::testing::_))
        .Times(AnyNumber()).WillRepeatedly(Return(5000.0f));
    EXPECT_CALL(sim, pollPendingNotification(::testing::_))
        .Times(AnyNumber()).WillRepeatedly(Return(false));
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
    StrictMock<MockCitySimulation> sim;
    StrictMock<MockAudioSystem>    audio;
    ManualClock                    clock;

    setDefaultSimCalls(sim);

    // First update(): getCityRating() returns Village — no tier change, no stinger.
    // Second and subsequent: returns Town — triggers stinger on second call only
    // (m_previousCityRating becomes Town after 2nd, so 3rd sees no transition).
    EXPECT_CALL(sim, getCityRating())
        .WillOnce(Return(CityRatingTier::Village))
        .WillRepeatedly(Return(CityRatingTier::Town));

    // StrictMock: only triggerStinger(MILESTONE) once; any other audio call fails.
    EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE))
        .Times(1);

    UIManager uiManager(&backend, &audio, &sim, &clock);

    uiManager.update(0.016f);  // Village — no tier change yet, no stinger
    uiManager.update(0.016f);  // Town — tier transition detected, stinger fires
    uiManager.update(0.016f);  // Still Town — no transition, no stinger
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
    StrictMock<MockCitySimulation> sim;
    StrictMock<MockAudioSystem>    audio;
    ManualClock                    clock;

    setDefaultSimCalls(sim);

    // Always return City rating (for both onGameLoaded() and update()).
    EXPECT_CALL(sim, getCityRating())
        .Times(AnyNumber()).WillRepeatedly(Return(CityRatingTier::City));

    // StrictMock: triggerStinger(MILESTONE) must NEVER be called.
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
    StrictMock<MockCitySimulation> sim;
    StrictMock<MockAudioSystem>    audio;
    ManualClock                    clock;

    setDefaultSimCalls(sim);

    // Override population to 100K; getCityRating always returns Metropolis.
    EXPECT_CALL(sim, getTotalPopulation())
        .Times(AnyNumber()).WillRepeatedly(Return(100000));
    EXPECT_CALL(sim, getCityRating())
        .Times(AnyNumber()).WillRepeatedly(Return(CityRatingTier::Metropolis));

    // StrictMock: triggerStinger(MILESTONE) must NEVER be called.
    EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE))
        .Times(0);

    UIManager uiManager(&backend, &audio, &sim, &clock);

    // Seed m_previousCityRating to Metropolis — 100K does not cross a tier boundary.
    uiManager.onGameLoaded();
    uiManager.update(0.016f);  // Metropolis — no transition from seeded state.
    uiManager.update(0.016f);  // Still Metropolis at 100K — no stinger.
}
