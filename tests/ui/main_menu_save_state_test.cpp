// tests/ui/main_menu_save_state_test.cpp
//
// MainMenuSaveStateTest — Phase 11c unit tests for the Main Menu "Load Game" button
// state reflecting save-file state at startup.
//
// Tests:
//   1. MainMenuPanel_NoSaves_LoadButtonGrayed
//      — When ISaveSystem::getSaveFileState() returns NoSaves, the Load Game button
//        is disabled (setElementEnabled(..., false)).
//   2. MainMenuPanel_CorruptSaves_LoadButtonGrayed_TooltipShowsPath
//      — When getSaveFileState() returns AllCorrupt, the Load Game button is disabled
//        and the save-status label text contains the save directory path.
//   3. MainMenuPanel_ValidSave_LoadButtonEnabled
//      — When getSaveFileState() returns Valid, the Load Game button is enabled.
//
// The UIManager mediates between ISaveSystem and MainMenuPanel via
// setSaveSystem() + setSaveAvailable() + setSaveStatusText().
//
// Uses NiceMock<MockUIBackend> (many incidental addStaticText/addButton calls) and
// StrictMock<MockSaveSystem> (save-state queries must be explicit).
//
// TearDown contract: ui_ reset before mock destruction.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/ISaveSystem.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/ui/MockSaveSystem.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::HasSubstr;
using ::testing::_;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class MainMenuSaveStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 140, 40}));
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
        ON_CALL(sim_, consumeBudgetTicks()).WillByDefault(Return(0));

        // UIManager starts in MainMenu state by default.
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        ui_.reset();
    }

    // Wire ISaveSystem and query the save state, then drive UIManager to update
    // the MainMenuPanel.  This mirrors what main.cpp does at startup:
    //   1. ui_->setSaveSystem(&save_)
    //   2. Inspect state via getSaveFileState()
    //   3. Call setSaveAvailable() / setSaveStatusText() accordingly.
    void wireSaveSystem() {
        ui_->setSaveSystem(&save_);

        SaveFileState state = save_.getSaveFileState();
        bool available = (state == SaveFileState::Valid);
        std::string statusText;
        if (state == SaveFileState::NoSaves) {
            statusText = "No saves found.";
        } else if (state == SaveFileState::AllCorrupt) {
            statusText = "Save data is corrupted — cannot load. Check " +
                         save_.getSaveDirectoryPath() + " for recovery.";
        }
        ui_->setSaveAvailable(available);
        ui_->setSaveStatusText(statusText);
    }

    NiceMock<MockUIBackend>         backend_;
    NiceMock<MockAudioSystem>       audio_;
    NiceMock<MockCitySimulation>    sim_;
    StrictMock<MockSaveSystem>      save_;
    ManualClock                     clock_;
    std::unique_ptr<UIManager>      ui_;
    uint32_t                        nextHandle_{100};
};

// ---------------------------------------------------------------------------
// Test 1: No saves → Load Game button grayed.
// ---------------------------------------------------------------------------
TEST_F(MainMenuSaveStateTest, MainMenuPanel_NoSaves_LoadButtonGrayed) {
    // setSaveAvailable(false) calls setElementEnabled(m_btnLoadGame, false) immediately.
    // Register the expectation BEFORE wireSaveSystem() so it fires when the call happens.
    EXPECT_CALL(backend_, setElementEnabled(_, false)).Times(AtLeast(1));

    EXPECT_CALL(save_, getSaveFileState())
        .WillOnce(Return(SaveFileState::NoSaves));
    EXPECT_CALL(save_, getSaveDirectoryPath()).Times(0);  // not needed for NoSaves

    wireSaveSystem();
}

// ---------------------------------------------------------------------------
// Test 2: All corrupt saves → Load Game button grayed; tooltip shows path.
// ---------------------------------------------------------------------------
TEST_F(MainMenuSaveStateTest, MainMenuPanel_CorruptSaves_LoadButtonGrayed_TooltipShowsPath) {
    const std::string savePath = "/home/player/.config/aitown/saves";

    // Both calls happen inside wireSaveSystem(); register expectations before it runs.
    EXPECT_CALL(backend_, setElementEnabled(_, false)).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementText(_, HasSubstr(savePath))).Times(AtLeast(1));

    EXPECT_CALL(save_, getSaveFileState())
        .WillOnce(Return(SaveFileState::AllCorrupt));
    EXPECT_CALL(save_, getSaveDirectoryPath())
        .WillOnce(Return(savePath));

    wireSaveSystem();
}

// ---------------------------------------------------------------------------
// Test 3: Valid save exists → Load Game button enabled.
// ---------------------------------------------------------------------------
TEST_F(MainMenuSaveStateTest, MainMenuPanel_ValidSave_LoadButtonEnabled) {
    // setSaveAvailable(true) calls setElementEnabled(m_btnLoadGame, true) immediately.
    EXPECT_CALL(backend_, setElementEnabled(_, true)).Times(AtLeast(1));

    EXPECT_CALL(save_, getSaveFileState())
        .WillOnce(Return(SaveFileState::Valid));
    EXPECT_CALL(save_, getSaveDirectoryPath()).Times(0);

    wireSaveSystem();
}
