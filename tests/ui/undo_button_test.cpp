// tests/ui/undo_button_test.cpp
//
// Phase 8 undo button tests.
//
// Tests the undo countdown text formatting and grey-out when no pending action.
// Uses NiceMock<MockUIBackend> + NiceMock<MockCitySimulation> + ManualClock.
//
// TearDown contract: ui_ reset to nullptr before mocks are destroyed.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::AtLeast;
using ::testing::AnyNumber;

class UndoButtonTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(true));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));
        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
        ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
        ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false));
        ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(10000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(100));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getDemandPressurePct(_)).WillByDefault(Return(0.5f));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        // Reset UIManager before mocks are destroyed.
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
    uint32_t                     nextHandle_{100};
};

// Undo button shows "Undo" (no countdown) when there is no pending action.
TEST_F(UndoButtonTest, UndoButton_NoPendingAction_ShowsUndoText) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));

    // GMock requires catch-all for other setElementText calls, otherwise they are "unexpected".
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, "Undo")).Times(AtLeast(1));
    ui_->draw();
}

// Undo button is disabled (grayed) when no pending action.
TEST_F(UndoButtonTest, UndoButton_NoPendingAction_IsDisabled) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));

    EXPECT_CALL(backend_, setElementEnabled(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementEnabled(_, false)).Times(AtLeast(1));
    ui_->draw();
}

// Undo button is enabled (interactive) when there IS a pending action.
TEST_F(UndoButtonTest, UndoButton_HasPendingAction_IsEnabled) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(true));
    ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(15.0));
    clock_.advance(5.0);

    EXPECT_CALL(backend_, setElementEnabled(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementEnabled(_, true)).Times(AtLeast(1));
    ui_->draw();
}

// Undo button shows countdown text "Undo (Xs)" when there is a pending action.
// HUD.cpp format: "Undo (%ds)" via snprintf -> "Undo (10s)".
TEST_F(UndoButtonTest, UndoButton_HasPendingAction_ShowsCountdownText) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(true));
    // Expiry at 15.0 seconds, current time at 5.0 seconds -> 10s remaining.
    ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(15.0));
    clock_.advance(5.0);

    // Catch-all for unrelated setElementText calls.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    // Expect text like "Undo (10s)".
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Undo (10s)"))).Times(AtLeast(1));
    ui_->draw();
}

// Countdown text shows 0 (floored) when expiry has passed.
TEST_F(UndoButtonTest, UndoButton_ExpiryPassed_ShowsZeroSeconds) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(true));
    // Expiry at 5.0 seconds, current time at 10.0 seconds -> -5s -> clamped to 0s.
    ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(5.0));
    clock_.advance(10.0);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Undo (0s)"))).Times(AtLeast(1));
    ui_->draw();
}

// UndoCountdown_AmberAt10xSpeed
// At 10x speed, the undo countdown total < 6s triggers amber immediately.
TEST_F(UndoButtonTest, UndoCountdown_AmberAt10xSpeed_ImmediatelyOnAction) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(true));
    ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(3.0));
    clock_.advance(0.0); // Time is 0; 3s remaining.

    // Draw should set the countdown text.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Undo (3s)"))).Times(AtLeast(1));
    ui_->draw();
}

// Ctrl+Z triggers undo when there is a pending action during gameplay.
TEST_F(UndoButtonTest, CtrlZ_TrigersUndo_DuringGameplay) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(true));

    // Press LCTRL down.
    InputEvent ctrlDown;
    ctrlDown.type = InputEvent::Type::KeyDown;
    ctrlDown.keyCode = 162; // LCTRL
    ui_->onEvent(ctrlDown);

    // Press Z.
    EXPECT_CALL(sim_, undoLastAction()).Times(1);
    InputEvent zDown;
    zDown.type = InputEvent::Type::KeyDown;
    zDown.keyCode = 90; // Z
    bool consumed = ui_->onEvent(zDown);
    EXPECT_TRUE(consumed);
}

// Ctrl+Z does NOT trigger undo when no pending action.
TEST_F(UndoButtonTest, CtrlZ_NoEffect_WhenNoPendingAction) {
    ui_->transitionToGameplay(GameMode::Scenario);
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));

    InputEvent ctrlDown;
    ctrlDown.type = InputEvent::Type::KeyDown;
    ctrlDown.keyCode = 162;
    ui_->onEvent(ctrlDown);

    EXPECT_CALL(sim_, undoLastAction()).Times(0);
    InputEvent zDown;
    zDown.type = InputEvent::Type::KeyDown;
    zDown.keyCode = 90;
    ui_->onEvent(zDown);
}
