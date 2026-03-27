// tests/ui/hud_grace_period_test.cpp
//
// Phase 11m D4: HUD grace period reset test.
// Verifies that calling HUD::notifyGameStarted() on a second game resets the
// grace period — the grace-period label becomes visible with full alpha.
//
// Mock policy: NiceMock for all (many incidental backend calls during HUD construction).
// TearDown contract: hud_.reset() before mock destructors.

#include "src/ui/HUD.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/IUIBackend.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// HUDGracePeriodTest fixture
// ---------------------------------------------------------------------------
class HUDGracePeriodTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;

    // hud_ declared after mocks — destroyed first.
    std::unique_ptr<HUD> hud_;

    void TearDown() override {
        hud_.reset();
    }
};

// ---------------------------------------------------------------------------
// Test: HUD_SecondNewGame_GracePeriodLabelVisible
//
// Phase 1 — simulate first game + expire grace period.
// Phase 2 — call notifyGameStarted() for second game; expect label shown with
//             full alpha immediately on the next update().
//
// Strategy: capture the handle assigned to the "Cost waiver" addStaticText call
// during HUD construction, then assert that setElementAlpha(handle, 1.0f) and
// setElementVisible(handle, true) are called after notifyGameStarted().
// ---------------------------------------------------------------------------
TEST_F(HUDGracePeriodTest, HUD_SecondNewGame_GracePeriodLabelVisible)
{
    // Stub sim queries so HUD::update() / HUD::draw() do not crash.
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(10000.0f));
    ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
    ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
    ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(0));
    ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
    ON_CALL(sim_, getDemandPressurePct(_)).WillByDefault(Return(0.0f));
    ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
    ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));
    ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
    ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));

    // Shared counter for handle assignment: both addStaticText and addButton
    // use the same counter so handles are globally unique.
    UIElementHandle handleCounter{0};
    UIElementHandle gracePeriodHandle{kInvalidUIElement};

    // Capture the handle assigned to the "Cost waiver" grace period label.
    ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
        [&handleCounter, &gracePeriodHandle](
            const std::string& text, int, int, int, int) -> UIElementHandle {
            ++handleCounter;
            if (text.find("Cost waiver") != std::string::npos) {
                gracePeriodHandle = handleCounter;
            }
            return handleCounter;
        });
    ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
        [&handleCounter](const std::string&, int, int, int, int) {
            return ++handleCounter;
        });
    ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
    ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
    ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(true));
    ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 140, 40}));

    // Construct HUD — this will assign gracePeriodHandle.
    hud_ = std::make_unique<HUD>(&backend_, &audio_, &sim_, &clock_);

    // Verify we captured the grace period label handle.
    ASSERT_NE(gracePeriodHandle, kInvalidUIElement)
        << "HUD constructor must call addStaticText with 'Cost waiver' text";

    // Phase 1: simulate first game starting and grace period expiring.
    hud_->show();
    hud_->notifyGameStarted();  // first game starts at t=0

    // Advance clock past grace period (>120 s).
    clock_.advance(200.0);

    // Update so HUD processes the expired state.
    hud_->update(0.016f);

    // Phase 2: verify second game resets grace period.
    // Allow all setElementAlpha calls (update() also calls setElementAlpha for
    // treasury label pulse logic). Track whether our specific handle was called.
    bool graceLabelAlphaReset = false;
    bool graceLabelMadeVisible = false;

    ON_CALL(backend_, setElementAlpha(_, _)).WillByDefault(
        [gracePeriodHandle, &graceLabelAlphaReset](UIElementHandle h, float alpha) {
            if (h == gracePeriodHandle && alpha == 1.0f) {
                graceLabelAlphaReset = true;
            }
        });
    ON_CALL(backend_, setElementVisible(_, _)).WillByDefault(
        [gracePeriodHandle, &graceLabelMadeVisible](UIElementHandle h, bool visible) {
            if (h == gracePeriodHandle && visible) {
                graceLabelMadeVisible = true;
            }
        });

    hud_->notifyGameStarted();  // second game
    hud_->update(0.016f);

    EXPECT_TRUE(graceLabelAlphaReset)
        << "Grace period label (handle=" << gracePeriodHandle
        << ") should have setElementAlpha(h, 1.0f) called after second notifyGameStarted()";
    EXPECT_TRUE(graceLabelMadeVisible)
        << "Grace period label (handle=" << gracePeriodHandle
        << ") should have setElementVisible(h, true) called after second notifyGameStarted()";
}
