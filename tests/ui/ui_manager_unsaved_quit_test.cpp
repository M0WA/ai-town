// tests/ui/ui_manager_unsaved_quit_test.cpp
//
// UIManagerUnsavedQuitTest — Phase 11c unit tests for the unsaved-changes quit guard.
//
// Tests:
//   1. UIManager_QuitToDesktop_WithUnsavedChanges_ShowsModal
//   2. UIManager_QuitToDesktop_NoUnsavedChanges_ExitsImmediately
//   3. UIManager_QuitToMenu_WithUnsavedChanges_ShowsModal
//   4. UIManager_QuitToMenu_NoUnsavedChanges_TransitionsImmediately
//   5. UIManager_UnsavedQuit_SaveAndQuit_CallsAutoSave
//   6. UIManager_UnsavedQuit_Cancel_ClearsPendingQuit
//
// Dependencies:
//   StrictMock<MockCitySimulation>  — every sim call is checked
//   NiceMock<MockUIBackend>         — many incidental UI calls during construction
//   StrictMock<MockSaveSystem>      — save calls are verified exactly (requires
//                                     ISaveSystem + MockSaveSystem from Phase 11c)
//   ManualClock                     — deterministic timing
//
// NOTE: These tests require ISaveSystem.h and UIManager::setSaveSystem(ISaveSystem*)
// to be delivered by graphics-dev-irrlicht (Phase 11c §3-pre).  They will not
// compile until those deliverables land.
//
// TearDown contract: ui_ is reset before mock destruction to prevent
// order-of-destruction issues with StrictMock expectations.

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
class UIManagerUnsavedQuitTest : public ::testing::Test {
protected:
    void SetUp() override {
        // NiceMock<MockUIBackend>: many incidental addStaticText/addButton calls.
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));

        // StrictMock<MockCitySimulation>: register every sim call UIManager may make
        // during construction, transitionToGameplay, update, and modal open/close.
        // Using EXPECT_CALL with WillRepeatedly allows 0-or-more calls (AnyNumber).
        EXPECT_CALL(sim_, isPaused()).WillRepeatedly(Return(false));
        EXPECT_CALL(sim_, getConsecutiveDeficitMonths()).WillRepeatedly(Return(0));
        EXPECT_CALL(sim_, pollPendingNotification(_)).WillRepeatedly(Return(false));
        EXPECT_CALL(sim_, getSpeedMultiplier()).WillRepeatedly(Return(SpeedMultiplier::x1));
        EXPECT_CALL(sim_, getTreasuryBalance()).WillRepeatedly(Return(10000.0f));
        EXPECT_CALL(sim_, getOutstandingDebt()).WillRepeatedly(Return(0.0f));
        EXPECT_CALL(sim_, getCityRating()).WillRepeatedly(Return(CityRatingTier::Village));
        EXPECT_CALL(sim_, getTotalPopulation()).WillRepeatedly(Return(0));
        EXPECT_CALL(sim_, getSimulationTime()).WillRepeatedly(Return(SimulationTime{1, 1}));
        EXPECT_CALL(sim_, getDemandPressurePct(_)).WillRepeatedly(Return(0.0f));
        EXPECT_CALL(sim_, hasUndoPendingAction()).WillRepeatedly(Return(false));
        EXPECT_CALL(sim_, getUndoExpiryTimeSeconds()).WillRepeatedly(Return(0.0));
        EXPECT_CALL(sim_, consumeBudgetTicks()).WillRepeatedly(Return(0));
        // setPaused: called when the unsaved-quit modal opens (setPaused(true)) and
        // when it closes (setPaused(false)).  Allow any number of calls.
        EXPECT_CALL(sim_, setPaused(_)).WillRepeatedly(Return());
        // getOutstandingBondUses: called by ModalDialog on screen 2 of forced-loan.
        // Not triggered by unsaved-quit modal, but registered to prevent StrictMock failures
        // if the internal ModalDialog path is exercised by any test case.
        EXPECT_CALL(sim_, getOutstandingBondUses()).WillRepeatedly(Return(2));

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
        ui_->transitionToGameplay(GameMode::Scenario);
    }

    void TearDown() override {
        // MANDATORY: destroy UIManager before mocks so destructor-path calls to the
        // StrictMock<MockCitySimulation> do not fire unexpected-call failures.
        ui_.reset();
    }

    // Helper: navigate to Quit to Desktop via the pause menu keyboard path.
    void navigatePauseToQuitDesktop() {
        // Open pause menu with Escape.
        ui_->onEvent(keyDown(27));
        // Down four times to reach "Quit to Desktop".
        for (int i = 0; i < 4; ++i) ui_->onEvent(keyDown(40));
        ui_->onEvent(keyDown(13));  // Enter
    }

    // Helper: navigate to Quit to Main Menu.
    void navigatePauseToQuitToMenu() {
        ui_->onEvent(keyDown(27));
        for (int i = 0; i < 3; ++i) ui_->onEvent(keyDown(40));
        ui_->onEvent(keyDown(13));
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
    StrictMock<MockCitySimulation>  sim_;
    StrictMock<MockSaveSystem>      save_;
    ManualClock                     clock_;
    std::unique_ptr<UIManager>      ui_;
    uint32_t                        nextHandle_{100};
};

// ---------------------------------------------------------------------------
// Test 1: Quit to Desktop with unsaved changes → unsaved-changes modal shown.
// ---------------------------------------------------------------------------
TEST_F(UIManagerUnsavedQuitTest, UIManager_QuitToDesktop_WithUnsavedChanges_ShowsModal) {
    ui_->setUnsavedChanges(true);
    navigatePauseToQuitDesktop();

    // update() detects the pending quit + unsaved flag and shows the modal.
    ui_->update(0.016f);

    EXPECT_TRUE(ui_->hasActiveModal())
        << "Unsaved-changes quit guard must open the unsaved-quit modal";
    EXPECT_FALSE(ui_->isQuitRequested())
        << "Quit must not be set while modal is still open";
}

// ---------------------------------------------------------------------------
// Test 2: Quit to Desktop with no unsaved changes → exits immediately (no modal).
// ---------------------------------------------------------------------------
TEST_F(UIManagerUnsavedQuitTest, UIManager_QuitToDesktop_NoUnsavedChanges_ExitsImmediately) {
    ui_->setUnsavedChanges(false);
    navigatePauseToQuitDesktop();

    ui_->update(0.016f);

    EXPECT_TRUE(ui_->isQuitRequested())
        << "Quit to Desktop with no unsaved changes must set isQuitRequested immediately";
    EXPECT_FALSE(ui_->hasActiveModal())
        << "No modal must be shown when there are no unsaved changes";
}

// ---------------------------------------------------------------------------
// Test 3: Quit to Main Menu with unsaved changes → unsaved-changes modal shown.
// ---------------------------------------------------------------------------
TEST_F(UIManagerUnsavedQuitTest, UIManager_QuitToMenu_WithUnsavedChanges_ShowsModal) {
    ui_->setUnsavedChanges(true);
    navigatePauseToQuitToMenu();

    ui_->update(0.016f);

    EXPECT_TRUE(ui_->hasActiveModal())
        << "Unsaved-changes quit guard must open the unsaved-quit modal";
    EXPECT_FALSE(ui_->isQuitRequested());
}

// ---------------------------------------------------------------------------
// Test 4: Quit to Main Menu with no unsaved changes → transitions immediately.
// ---------------------------------------------------------------------------
TEST_F(UIManagerUnsavedQuitTest, UIManager_QuitToMenu_NoUnsavedChanges_TransitionsImmediately) {
    ui_->setUnsavedChanges(false);
    navigatePauseToQuitToMenu();

    ui_->update(0.016f);

    EXPECT_FALSE(ui_->isQuitRequested())
        << "Quit to Main Menu must not set isQuitRequested";
    EXPECT_FALSE(ui_->hasActiveModal())
        << "No modal must be shown when there are no unsaved changes";
}

// ---------------------------------------------------------------------------
// Test 5: Save and Quit → autoSave() called on the ISaveSystem.
// ---------------------------------------------------------------------------
TEST_F(UIManagerUnsavedQuitTest, UIManager_UnsavedQuit_SaveAndQuit_CallsAutoSave) {
    // Register incidental save methods that fire during pause-menu navigation and
    // update() calls — required for StrictMock<MockSaveSystem>.
    EXPECT_CALL(save_, onPauseMenuOpened()).WillRepeatedly(Return());
    EXPECT_CALL(save_, onBudgetTick()).WillRepeatedly(Return());
    EXPECT_CALL(save_, suspendAutoSave(_)).WillRepeatedly(Return());
    EXPECT_CALL(save_, onForcedLoanDialogActive()).WillRepeatedly(Return());

    // Wire the ISaveSystem mock.
    ui_->setSaveSystem(&save_);

    ui_->setUnsavedChanges(true);
    navigatePauseToQuitDesktop();
    ui_->update(0.016f);
    ASSERT_TRUE(ui_->hasActiveModal());

    // autoSave() must be called exactly once when "Save and Quit" is chosen.
    EXPECT_CALL(save_, autoSave())
        .Times(1)
        .WillOnce(Return(SaveResult{true, ""}));

    // The unsaved-quit modal default focus is index 0 = "Save and Quit" (Accept).
    // Press Enter immediately to activate "Save and Quit".
    ui_->onEvent(keyDown(13));

    // update() detects the closed modal (DialogResult::Accept) and calls autoSave(),
    // then dispatches the pending quit.
    ui_->update(0.016f);
}

// ---------------------------------------------------------------------------
// Test 6: Cancel on the unsaved-quit modal clears the pending quit.
// ---------------------------------------------------------------------------
TEST_F(UIManagerUnsavedQuitTest, UIManager_UnsavedQuit_Cancel_ClearsPendingQuit) {
    ui_->setUnsavedChanges(true);
    navigatePauseToQuitDesktop();
    ui_->update(0.016f);
    ASSERT_TRUE(ui_->hasActiveModal());

    // Close the modal via the public API (simulates pressing Cancel / Escape).
    // autoSave() must NOT be called.
    EXPECT_CALL(save_, autoSave()).Times(0);

    ui_->closeModal();
    ui_->update(0.016f);

    EXPECT_FALSE(ui_->isQuitRequested())
        << "Cancel on the unsaved-quit modal must not quit the game";
    EXPECT_FALSE(ui_->hasActiveModal());
}
