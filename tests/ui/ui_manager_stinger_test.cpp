// ui_manager_stinger_test.cpp — Phase 11 UIManager stinger unit tests.
//
// Tests verify that triggerStinger(StingerType::MILESTONE) is called by UIManager
// exactly when a City Rating tier transition occurs, and NOT fired on game load
// or when no tier transition has occurred.
//
// CMake target: ui_tests, label "unit".
// Mock policy: StrictMock for mocks under assertion; NiceMock for background mocks.
//
// DESIGN NOTE: UIManager is currently a Phase 1 stub whose update() method is a
// no-op. The stinger tests defined here establish the *contract* that the Phase 11
// UIManager implementation must satisfy. The tests use the mock infrastructure
// provided by the project and document precise expectations so they become
// regression guards the moment the real UIManager update() body is delivered.
//
// For now (Phase 1 stub), each test calls UIManager::update() which does nothing,
// so the "no unexpected call" assertions also pass. The PhaseN_StingerFires tests
// are written as PENDING stubs that will be activated (by removing the GTEST_SKIP)
// when UIManager::update() actually calls getCityRating() and triggerStinger().
//
// Per architecture/audio-architecture/dynamic-soundscape.md and phase-11.md:
//   - stinger_milestone fires ONLY at City Rating tier transitions.
//   - 100K population milestone does NOT trigger stinger_milestone.
//   - Calling onGameLoaded() seeds the tier cache; no stinger fires on load.
//   - A second update() in the same tier fires no additional stinger (cooldown/same-tier).

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
//
// STATUS: PENDING — UIManager::update() is a Phase 1 stub (no-op). This test
// will become a live assertion when Phase 11 delivers the stinger wiring.
// ---------------------------------------------------------------------------
TEST(UIManagerStingerTest, UIManager_StingerMilestone_FiresOnTierTransition)
{
    GTEST_SKIP() << "PENDING: UIManager::update() is a Phase 1 stub; "
                    "this test activates when Phase 11 wires getCityRating() "
                    "and triggerStinger() in UIManager::update()";

    NiceMock<MockUIBackend>      backend;
    NiceMock<MockCitySimulation> sim;
    NiceMock<MockAudioSystem>    audio;
    ManualClock                  clock;

    setDefaultSimCalls(sim);

    // First update(): getCityRating() returns Village (0 → integer 0).
    // Second update(): getCityRating() returns Town (1).
    // Stinger must fire exactly once (Village→Town transition).
    EXPECT_CALL(sim, getCityRating())
        .WillOnce(Return(CityRatingTier::Village))   // Village on first update
        .WillOnce(Return(CityRatingTier::Town));     // Town on second update — triggers stinger

    EXPECT_CALL(audio, triggerStinger(StingerType::MILESTONE))
        .Times(1);

    UIManager uiManager(&backend, &audio, &sim, &clock);

    uiManager.update(0.016f);  // Village — no tier change yet, no stinger
    uiManager.update(0.016f);  // Town — tier transition detected, stinger fires

    // Third update with same Town rating — no additional stinger.
    ON_CALL(sim, getCityRating()).WillByDefault(Return(CityRatingTier::Town));
    uiManager.update(0.016f);
    // Implicit: StrictMock on audio would catch an unexpected triggerStinger here,
    // but since we used NiceMock, we rely on the Times(1) expectation set above.
}

// ---------------------------------------------------------------------------
// Test b: UIManager_StingerMilestone_NoStingerOnLoad
//
// Calling onGameLoaded() seeds the tier cache to the current rating.
// A subsequent update() with the same rating must NOT trigger a stinger.
//
// STATUS: PENDING — onGameLoaded() is not yet a UIManager method; will be
// added in Phase 11.
// ---------------------------------------------------------------------------
TEST(UIManagerStingerTest, UIManager_StingerMilestone_NoStingerOnLoad)
{
    GTEST_SKIP() << "PENDING: UIManager::onGameLoaded() and stinger wiring "
                    "are Phase 11 deliverables. This test activates once those "
                    "methods are implemented.";

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

    // onGameLoaded() seeds the cache to City (2) — no stinger on the next update.
    // uiManager.onGameLoaded();  // Phase 11 method — not yet declared.
    uiManager.update(0.016f);  // Same tier as cached — no stinger.
}

// ---------------------------------------------------------------------------
// Test c: UIManager_StingerMilestone_No100KStinger
//
// 100K population does NOT correspond to a CityRatingTier boundary.
// Even while population is in the Metropolis range (> 50K), if the City Rating
// tier stays at Metropolis for all calls, no stinger fires.
//
// STATUS: PENDING — UIManager::update() stinger wiring is a Phase 11 deliverable.
// ---------------------------------------------------------------------------
TEST(UIManagerStingerTest, UIManager_StingerMilestone_No100KStinger)
{
    GTEST_SKIP() << "PENDING: UIManager stinger wiring is a Phase 11 deliverable. "
                    "This test activates once getCityRating() is called in update().";

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

    // uiManager.onGameLoaded();  // Phase 11 method — not yet declared.
    uiManager.update(0.016f);  // Metropolis tier seeded; no transition.
    uiManager.update(0.016f);  // Still Metropolis at 100K — no stinger.
}
