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
//
// Phase 11l additions (Deliverable 6):
//   4. MainMenuPanel_LoadGame_ClickSetsFlag
//      — Clicking the enabled Load Game button sets consumeLoadGameRequest().
//   5. MainMenuPanel_LoadGame_ClickIgnoredWhenDisabled
//      — Clicking the disabled Load Game button leaves consumeLoadGameRequest() false.

#include "src/ui/UIManager.h"
#include "src/ui/MainMenuPanel.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/ISaveSystem.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/ui/MockSaveSystem.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <vector>

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
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));
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

// ===========================================================================
// Phase 11l Deliverable 6 — direct MainMenuPanel tests for Load Game click
//
// These tests instantiate MainMenuPanel directly (not via UIManager) to verify
// the consumeLoadGameRequest() polling flag is set correctly on mouse click.
//
// Fixture: MainMenuLoadGameClickTest
//   - NiceMock<MockUIBackend> so incidental calls during construction are silent.
//   - Load Game button handle is captured so getElementRect can return its rect.
//   - After construction, setSaveAvailable(true) enables the Load Game button.
// ===========================================================================

class MainMenuLoadGameClickTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) {
                uint32_t h = ++nextHandle_;
                // The Load Game button is the second button created (after New Game).
                // We capture it by tracking creation order.
                createdButtons_.push_back(h);
                return h;
            });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 0, 0}));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(false));

        panel_ = std::make_unique<MainMenuPanel>(&backend_);
        panel_->show();

        // createdButtons_[0] = New Game, [1] = Load Game, [2] = Settings, [3] = Quit.
        // Enable the Load Game button so hit-tests respect isElementEnabled().
        if (createdButtons_.size() >= 2) {
            loadGameHandle_ = createdButtons_[1];
        }

        // Return a hit-able rect only for the Load Game button.
        ON_CALL(backend_, getElementRect(loadGameHandle_)).WillByDefault(
            Return(UIRect{760, 188, 360, 48}));

        // Enable only the Load Game button for the enabled tests.
        ON_CALL(backend_, isElementEnabled(loadGameHandle_)).WillByDefault(Return(true));
    }

    void TearDown() override {
        panel_.reset();
    }

    static InputEvent makeMouseClick(int x, int y) {
        InputEvent ev{};
        ev.type   = InputEvent::Type::MouseButtonDown;
        ev.button = 0;
        ev.x      = x;
        ev.y      = y;
        ev.physX  = x;
        ev.physY  = y;
        return ev;
    }

    NiceMock<MockUIBackend>        backend_;
    std::unique_ptr<MainMenuPanel> panel_;
    std::vector<uint32_t>          createdButtons_;
    uint32_t                       loadGameHandle_{0};
    uint32_t                       nextHandle_{10};
};

// ---------------------------------------------------------------------------
// Test 4: Clicking enabled Load Game button sets consumeLoadGameRequest().
// ---------------------------------------------------------------------------
TEST_F(MainMenuLoadGameClickTest, MainMenuPanel_LoadGame_ClickSetsFlag)
{
    // Click inside Load Game button rect (760, 188) + size (360, 48).
    // Use centre of button: (760+180, 188+24) = (940, 212).
    bool consumed = panel_->onEvent(makeMouseClick(940, 212));
    EXPECT_TRUE(consumed);
    EXPECT_TRUE(panel_->consumeLoadGameRequest())
        << "consumeLoadGameRequest() must return true after clicking the enabled Load Game button";
    // Second call must return false (one-shot flag).
    EXPECT_FALSE(panel_->consumeLoadGameRequest())
        << "consumeLoadGameRequest() must return false on subsequent call (flag is one-shot)";
}

// ---------------------------------------------------------------------------
// Test 5: Clicking disabled Load Game button leaves consumeLoadGameRequest() false.
//
// When isElementEnabled(m_btnLoadGame) returns false, onEvent() must not set
// m_loadGameRequested — even if the click lands inside the button rect.
// ---------------------------------------------------------------------------
TEST_F(MainMenuLoadGameClickTest, MainMenuPanel_LoadGame_ClickIgnoredWhenDisabled)
{
    // Override: make Load Game button disabled for this test.
    ON_CALL(backend_, isElementEnabled(loadGameHandle_)).WillByDefault(Return(false));

    // Click inside the button rect.
    panel_->onEvent(makeMouseClick(940, 212));

    EXPECT_FALSE(panel_->consumeLoadGameRequest())
        << "consumeLoadGameRequest() must return false when the Load Game button is disabled";
}
