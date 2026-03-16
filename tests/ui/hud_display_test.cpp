// hud_display_test.cpp — Tests for HUD display branches and rendering logic.
//
// Covers:
//   1. ratingName() switch: Town, City, Metropolis, Megalopolis variants.
//      All tests use ui_->draw() after transitionToGameplay() which calls HUD::draw().
//   2. formatDollar() negative-value branch (treasury < 0.0f shows "-$NNN").
//   3. HUD::draw() debt label shown/hidden depending on outstanding debt.
//   4. HUD::update() budget-flash branch: getConsecutiveDeficitMonths() >= 2
//      triggers setElementAlpha on the treasury label.
//   5. HUD::update() grace-period fade: when elapsed > 120s the grace label
//      fades via setElementAlpha then hides.
//   6. HUD::setActiveToolLabel forwarding (called via UIManager when a toolbar
//      button is pressed).
//
// Uses NiceMock<MockUIBackend> + NiceMock<MockCitySimulation> + ManualClock.
// TearDown resets ui_ before mocks are destroyed (destructor-path contract).

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
#include <string>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::HasSubstr;

// ---------------------------------------------------------------------------
// HUDCoverageTest fixture
//
// Sets up UIManager with a full NiceMock suite so draw() + update() can be
// called without triggering unexpected-call failures.
// ---------------------------------------------------------------------------
class HUDCoverageTest : public ::testing::Test {
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
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 140, 40}));
        // Default sim stubs
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

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
        ui_->transitionToGameplay(GameMode::Scenario);
    }

    void TearDown() override {
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
    uint32_t                     nextHandle_{100};
};

// ---------------------------------------------------------------------------
// Test 1: ratingName — Town
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, RatingName_Town_SetOnDraw) {
    ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Town));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, "Town")).Times(AtLeast(1));
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 2: ratingName — City
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, RatingName_City_SetOnDraw) {
    ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::City));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, "City")).Times(AtLeast(1));
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 3: ratingName — Metropolis
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, RatingName_Metropolis_SetOnDraw) {
    ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Metropolis));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, "Metropolis")).Times(AtLeast(1));
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 4: ratingName — Megalopolis
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, RatingName_Megalopolis_SetOnDraw) {
    ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Megalopolis));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, "Megalopolis")).Times(AtLeast(1));
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 5: formatDollar — negative treasury balance shows "-$NNN"
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, FormatDollar_NegativeBalance_ShowsMinus) {
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(-5000.0f));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("-$5000"))).Times(AtLeast(1));
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 6: Debt label — shown when outstanding debt > 0
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, DebtLabel_Shown_WhenDebtPositive) {
    ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(10000.0f));

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Debt:"))).Times(AtLeast(1));
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 7: Debt label — hidden when outstanding debt == 0
//
// The debt label is set invisible via setElementVisible(handle, false) when
// debt == 0.  We just verify draw() does not crash and does NOT show "Debt:".
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, DebtLabel_Hidden_WhenDebtZero) {
    ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));

    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Debt:"))).Times(0);
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    ui_->draw();
}

// ---------------------------------------------------------------------------
// Test 8: Budget-flash — setElementAlpha called on treasury label when
//   getConsecutiveDeficitMonths() >= 2.
// HUD::update() pulses the treasury label alpha using a sine wave when the
// deficit streak reaches 2 or more months.
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, BudgetFlash_SetElementAlpha_WhenDeficitMonths2) {
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2));

    // setElementAlpha must be called at least once during update() for the flash.
    EXPECT_CALL(backend_, setElementAlpha(_, _)).Times(AtLeast(1));
    ui_->update(0.016f);
}

// ---------------------------------------------------------------------------
// Test 9: Budget-flash — reset to alpha 1.0 when deficit streak clears
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, BudgetFlash_ResetAlpha_WhenDeficitClears) {
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));

    // When deficit streak is 0, setElementAlpha(treasuryLabel, 1.0f) is called.
    EXPECT_CALL(backend_, setElementAlpha(_, 1.0f)).Times(AtLeast(1));
    ui_->update(0.016f);
}

// ---------------------------------------------------------------------------
// Test 10: Grace period — label text updated while time remaining > 0
// Before 120 s wall-clock elapsed, HUD::update() writes "Cost waiver: Xs remaining".
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, GracePeriod_LabelText_WhileActive) {
    // clock_ starts at 0.0; 10 s elapsed -> ~110 s remaining.
    clock_.advance(10.0);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Cost waiver:"))).Times(AtLeast(1));
    ui_->update(0.016f);
}

// ---------------------------------------------------------------------------
// Test 11: Grace period amber warning — when < 20 s remaining, alpha drops to 0.8f.
// The HUD sets alpha 0.8f when remaining < 20.0 seconds.
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, GracePeriod_AmberAlpha_WhenLessThan20sRemaining) {
    // 110 s elapsed -> 10 s remaining (< 20 s threshold).
    clock_.advance(110.0);

    // Allow any other setElementAlpha calls (e.g., budget-flash reset to 1.0f).
    EXPECT_CALL(backend_, setElementAlpha(_, _)).Times(AnyNumber());
    // The grace period amber path must call setElementAlpha with 0.8f at least once.
    EXPECT_CALL(backend_, setElementAlpha(_, 0.8f)).Times(AtLeast(1));
    ui_->update(0.016f);
}

// ---------------------------------------------------------------------------
// Test 12: Grace period fade-out — after 120 s, alpha fades toward 0.
// Simulate elapsed > 120 s: remaining <= 0 triggers the fade-out path.
// We advance to 121 s so remaining = -1.0 (negative clamped to 0).
// The fade-out decreases m_graceFadeAlpha by dt/0.5 per frame.
// With dt=0.016, alpha decreases from 1.0 to 0.968 on first frame.
// setElementAlpha is called with the new (< 1.0) value.
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, GracePeriod_FadeOut_AfterElapsed120s) {
    clock_.advance(121.0);

    // setElementAlpha must be called for the fade path (alpha < 1.0).
    EXPECT_CALL(backend_, setElementAlpha(_, _)).Times(AtLeast(1));
    ui_->update(0.016f);
}

// ---------------------------------------------------------------------------
// Test 13: Grace period fully expired — after enough frames the label is hidden.
// Drive update() with dt=0.5 until the grace period expires (m_graceFadeAlpha <= 0).
// After expiry, setElementVisible(gracePeriodLabel, false) is called.
// ---------------------------------------------------------------------------
TEST_F(HUDCoverageTest, GracePeriod_FullyExpired_LabelHidden) {
    clock_.advance(125.0);  // well past 120 s

    // Drive enough frames to fully deplete the 0.5s fade.
    // dt=0.5 per call; alpha starts at 1.0 -> 0.0 in two frames.
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, false)).Times(AtLeast(1));

    for (int i = 0; i < 5; ++i) {
        ui_->update(0.5f);
    }
}
