// tests/ui/uimanager_new_game_reset_test.cpp
//
// Phase 11m D6-UI: UIManager new-game pending flag tests.
// Verifies the new-game pending-flag mechanics:
//   - Second game (m_gameSessionActive=true) sets pending flag.
//   - First game (m_gameSessionActive=false) does NOT set pending flag.
//
// Requires AITOWN_TESTING_ENABLED=1 for handleNewGameRequest() and
// setGameSessionActiveForTest() test seams.
//
// Mock policy: NiceMock for all (UIManager constructor calls many backend methods).
// TearDown contract: uiManager_.reset() before mock destructors.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/simulation_types.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// UIManagerNewGameResetTest fixture
// ---------------------------------------------------------------------------
class UIManagerNewGameResetTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;

    // uiManager_ declared LAST — destroyed first.
    std::unique_ptr<UIManager> uiManager_;

    void SetUp() override {
        // Backend stubs.
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));

        // Sim stubs.
        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
        ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
        ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false));
        ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(10000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(0));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getDemandPressurePct(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));

        // Construct UIManager (canonical 4-param order).
        uiManager_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        uiManager_.reset();
    }

private:
    UIElementHandle nextHandle_{0};
};

// ---------------------------------------------------------------------------
// Test 1: UIManager_SecondNewGame_SetsNewGamePendingFlag
//
// Force m_gameSessionActive=true via test seam. Call handleNewGameRequest().
// consumeNewGameRequest() must return true (flag set), then false on second call.
// ---------------------------------------------------------------------------
TEST_F(UIManagerNewGameResetTest, UIManager_SecondNewGame_SetsNewGamePendingFlag)
{
#ifndef AITOWN_TESTING_ENABLED
    GTEST_SKIP() << "Requires AITOWN_TESTING_ENABLED=1";
#endif

    // Force session-active state (simulate: a game is already running).
    uiManager_->setGameSessionActiveForTest(true);

    NewGameParams params{};  // default-constructed

    // Request a new game while a session is active — should set pending flag.
    uiManager_->handleNewGameRequest(params);

    // First consume call returns true.
    EXPECT_TRUE(uiManager_->consumeNewGameRequest());

    // Second consume call returns false (flag was reset atomically).
    EXPECT_FALSE(uiManager_->consumeNewGameRequest());
}

// ---------------------------------------------------------------------------
// Test 2: UIManager_FirstGame_NoNewGamePendingFlag
//
// m_gameSessionActive is false (default). Call handleNewGameRequest().
// consumeNewGameRequest() must return false (first game takes direct path).
// ---------------------------------------------------------------------------
TEST_F(UIManagerNewGameResetTest, UIManager_FirstGame_NoNewGamePendingFlag)
{
#ifndef AITOWN_TESTING_ENABLED
    GTEST_SKIP() << "Requires AITOWN_TESTING_ENABLED=1";
#endif

    // m_gameSessionActive defaults to false — first game path.
    NewGameParams params{};

    uiManager_->handleNewGameRequest(params);

    // First game should have taken the direct transitionToGameplay() path,
    // NOT the pending-flag path.
    EXPECT_FALSE(uiManager_->consumeNewGameRequest());
}
