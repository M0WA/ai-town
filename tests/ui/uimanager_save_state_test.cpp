// tests/ui/uimanager_save_state_test.cpp
//
// Phase 11m D5: UIManager::transitionToMainMenu() refreshes load-button state
// by querying ISaveSystem::getSaveFileState().
//
// Mock policy: NiceMock for backend_, audio_, sim_, clock_; NiceMock<MockSaveSystem>
// for saveSystem_ (transition queries it; suppress any unrelated calls gracefully).
// TearDown contract: uiManager_.reset() before mock destructors.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/ISaveSystem.h"
#include "src/simulation/SaveSystem.h"   // SaveFileState definitions
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/ui/MockSaveSystem.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// UIManagerSaveStateTest fixture
// ---------------------------------------------------------------------------
class UIManagerSaveStateTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    NiceMock<MockSaveSystem>     saveSystem_;

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
        ON_CALL(sim_, getZoneDemandFactor(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));

        // Construct UIManager (canonical 4-param order).
        uiManager_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
        uiManager_->setSaveSystem(&saveSystem_);
    }

    void TearDown() override {
        uiManager_.reset();
    }

private:
    UIElementHandle nextHandle_{0};
};

// ---------------------------------------------------------------------------
// Test: UIManager_TransitionToMainMenu_RefreshesLoadButtonState
//
// Boot UIManager through a game session, then call transitionToMainMenu().
// Verify that getSaveFileState() is queried at least once during the transition.
// ---------------------------------------------------------------------------
TEST_F(UIManagerSaveStateTest, UIManager_TransitionToMainMenu_RefreshesLoadButtonState)
{
    // Initial state: no saves.
    ON_CALL(saveSystem_, getSaveFileState())
        .WillByDefault(Return(SaveFileState::NoSaves));

    // Transition to gameplay to simulate an active game session.
    uiManager_->transitionToGameplay(GameMode::Sandbox);

    // Switch to valid save so the query during transitionToMainMenu sees it.
    ON_CALL(saveSystem_, getSaveFileState())
        .WillByDefault(Return(SaveFileState::Valid));

    // Expect getSaveFileState() is queried at least once during/after transition.
    EXPECT_CALL(saveSystem_, getSaveFileState()).Times(AtLeast(1));

    // Trigger transition back to main menu.
    uiManager_->transitionToMainMenu();

    EXPECT_TRUE(::testing::Mock::VerifyAndClearExpectations(&saveSystem_));
}
