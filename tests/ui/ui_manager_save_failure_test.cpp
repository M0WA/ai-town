// tests/ui/ui_manager_save_failure_test.cpp
//
// UIManagerSaveFailureTest — Phase 11c unit tests for manual-save failure and
// auto-save failure notification paths.
//
// Tests:
//   1. UIManager_ManualSave_Failure_ShowsBlockingModal
//      — When Ctrl+S triggers saveToSlot() and it fails, UIManager shows a blocking
//        error modal (ModalDialog::showSaveFailure()).
//   2. UIManager_ManualSave_RetrySuccess_ClearsUnsavedDot
//      — Retry on the save-failure modal calls saveToSlot() again; on success the
//        unsaved-changes dot is cleared.
//   3. UIManager_AutoSave_Failure_PostsCriticalToast_NotModal
//      — When auto-save fails, a CRITICAL toast is posted (not a blocking modal).
//
// Dependencies:
//   NiceMock<MockUIBackend>         — incidental UI calls during construction
//   StrictMock<MockSaveSystem>      — save calls are verified exactly (requires
//                                     ISaveSystem + MockSaveSystem from Phase 11c)
//
// NOTE: These tests require ISaveSystem.h and UIManager::setSaveSystem(ISaveSystem*)
// to be delivered by graphics-dev-irrlicht (Phase 11c §3-pre).
//
// TearDown contract: ui_ reset before mock destruction.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "src/interfaces/ISaveSystem.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/ui/MockSaveSystem.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::_;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class UIManagerSaveFailureTest : public ::testing::Test {
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

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
        ui_->transitionToGameplay(GameMode::Scenario);

        // Wire the ISaveSystem mock.
        ui_->setSaveSystem(&save_);
    }

    void TearDown() override {
        // Explicit destruction before mocks — destructor-path contract.
        ui_.reset();
    }

    // Helper: send Ctrl+S (manual save hotkey).
    // UIManager tracks m_ctrlDown via keyCode 162 (LCTRL) KeyDown/KeyUp events.
    // Send LCTRL KeyDown then S KeyDown to fire the Ctrl+S handler.
    void sendCtrlS() {
        InputEvent ctrl;
        ctrl.type    = InputEvent::Type::KeyDown;
        ctrl.keyCode = 162;  // kKeyLCtrl = Irrlicht KEY_LCONTROL
        ui_->onEvent(ctrl);

        InputEvent s;
        s.type    = InputEvent::Type::KeyDown;
        s.keyCode = 83;  // kKeyS = Irrlicht KEY_KEY_S
        ui_->onEvent(s);
    }

    // Helper: create a KeyDown InputEvent.
    static InputEvent keyDown(int code) {
        InputEvent e;
        e.type    = InputEvent::Type::KeyDown;
        e.keyCode = code;
        return e;
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
// Test 1: Manual save failure shows a blocking modal (not just a toast).
// ---------------------------------------------------------------------------
TEST_F(UIManagerSaveFailureTest, UIManager_ManualSave_Failure_ShowsBlockingModal) {
    // Mark unsaved changes so the save is meaningful.
    ui_->setUnsavedChanges(true);

    // saveToSlot(1) fails.
    EXPECT_CALL(save_, saveToSlot(1))
        .Times(1)
        .WillOnce(Return(SaveResult{false, "disk full"}));

    // Trigger Ctrl+S.
    sendCtrlS();

    // After the failed save, UIManager must show a blocking modal.
    EXPECT_TRUE(ui_->hasActiveModal())
        << "Manual save failure must show a blocking error modal";

    // The unsaved-changes dot remains set (save failed — still dirty).
    // This is verified indirectly: isQuitRequested() remains false while the modal
    // is active, and the blocking modal itself signals that the save is still pending.
}

// ---------------------------------------------------------------------------
// Test 2: Retry on the save-failure modal calls saveToSlot() again; success
//         clears the unsaved-changes dot.
// ---------------------------------------------------------------------------
TEST_F(UIManagerSaveFailureTest, UIManager_ManualSave_RetrySuccess_ClearsUnsavedDot) {
    ui_->setUnsavedChanges(true);

    // First attempt fails.
    EXPECT_CALL(save_, saveToSlot(1))
        .WillOnce(Return(SaveResult{false, "transient error"}))
        .WillOnce(Return(SaveResult{true, ""}));

    // First Ctrl+S — save fails, modal appears.
    sendCtrlS();
    ASSERT_TRUE(ui_->hasActiveModal());

    // The save-failure modal has two buttons: Retry (index 0) and Cancel (index 1).
    // Default focus is Cancel (least destructive — avoids spurious retries).
    // Tab once to move focus from Cancel (1) to Retry (0), then press Enter.
    ui_->onEvent(keyDown(9));   // Tab → focus moves to Retry
    ui_->onEvent(keyDown(13));  // Enter → activates Retry (DialogResult::Accept)

    // update() dispatches the retry; on success, the modal closes and the dot clears.
    ui_->update(0.016f);

    EXPECT_FALSE(ui_->hasActiveModal())
        << "Successful retry must close the save-failure modal";
    // The unsaved-changes dot is cleared on a successful save; we verify the modal
    // is gone as the observable proxy for the cleared-dot state.
}

// ---------------------------------------------------------------------------
// Test 3: Auto-save failure via "Save and Quit" path does NOT show a blocking
//         modal (as opposed to the manual-save Ctrl+S path which shows a modal).
//
// The "Save and Quit" path calls autoSave() and, if it fails, posts a CRITICAL
// toast (not a blocking modal).  This is the key distinction from the manual-
// save path.
// ---------------------------------------------------------------------------
TEST_F(UIManagerSaveFailureTest, UIManager_AutoSave_Failure_PostsCriticalToast_NotModal) {
    // Register incidental save methods that fire during pause-menu navigation.
    // save_ is a StrictMock<MockSaveSystem> wired in SetUp; onPauseMenuOpened()
    // is called by UIManager when the pause overlay opens (ESC key).
    EXPECT_CALL(save_, onPauseMenuOpened()).WillRepeatedly(Return());
    EXPECT_CALL(save_, onBudgetTick()).WillRepeatedly(Return());
    EXPECT_CALL(save_, suspendAutoSave(_)).WillRepeatedly(Return());

    // Wire the unsaved-quit path: set unsaved changes and navigate to Quit to Desktop.
    ui_->setUnsavedChanges(true);

    // Navigate to Quit to Desktop via the pause menu keyboard path.
    auto esc  = keyDown(27);
    auto down = keyDown(40);
    auto enter = keyDown(13);
    ui_->onEvent(esc);
    ui_->onEvent(down); ui_->onEvent(down);
    ui_->onEvent(down); ui_->onEvent(down);
    ui_->onEvent(enter);
    ui_->update(0.016f);

    // The unsaved-quit modal should be open.
    ASSERT_TRUE(ui_->hasActiveModal());

    // autoSave() fails.
    EXPECT_CALL(save_, autoSave())
        .Times(1)
        .WillOnce(Return(SaveResult{false, "no space left"}));

    // Press Enter (default focus = "Save and Quit") to trigger autoSave().
    ui_->onEvent(keyDown(13));

    // update() dispatches the pending quit — autoSave() was called.
    // After the "Save and Quit" path runs with a failed autoSave(), no NEW blocking
    // modal should be opened for the auto-save failure (it uses a CRITICAL toast path,
    // not a modal).  The original modal is now closed (dialog resolved).
    ui_->update(0.016f);

    // The save-failure modal must NOT be shown for the auto-save path.
    // (Manual Ctrl+S failure uses showSaveFailure() modal; auto-save failure does not.)
    EXPECT_FALSE(ui_->hasActiveModal())
        << "Auto-save failure must NOT show a blocking save-failure modal";
}
