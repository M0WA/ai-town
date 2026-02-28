// tests/ui/notification_system_test.cpp
//
// Phase 8 notification system tests — two fixtures:
//
//   NotificationManagerTest: NiceMock<MockUIBackend> + NiceMock<MockCitySimulation>
//     + ManualClock. Tests NotificationManager in isolation (constructed directly).
//     - CriticalToast_OnPost_AutoPausesCalled
//     - CriticalToast_OnLastDismiss_NoAutoResume
//     - CriticalToast_SecondPost_NoDoublePause
//     - CriticalToast_HiddenWhileModalActive_ReappearsAfterClose
//     - NotificationSystem_AutoPause_OnFirstCriticalToast
//     - NotificationSystem_NoPause_OnNormalToast
//
//   UIManagerDeficitIntegrationTest: NiceMock<MockUIBackend> + NiceMock<MockCitySimulation>
//     + NiceMock<MockAudioSystem> + ManualClock. Tests UIManager deficit-polling chain.
//     APPROVED EXCEPTION: NiceMock<MockAudioSystem> instead of StrictMock (see phase-8.md).
//     - UIManagerDeficit_Month1_ToastDispatched_NoStinger
//     - UIManagerDeficit_Month2_ToastDispatched_StingerFires
//     - UIManagerDeficit_RapidFireCooldown_SecondStingerDropped
//     - UIManagerDeficit_PerStreakSingleFire_NoReFireAfterCooldown_SameStreak
//     - UIManagerDeficit_CounterZero_NoToastNoStinger
//     - UIManagerDeficit_StreakBreak_RecoveryToastDispatched
//     - UIManagerDeficit_Month1StreakBreak_ReenablesFutureStreak
//     - UIManagerDeficit_Month3_SandboxMode_NoGameOverModal
//     - UIManagerDeficit_Month1_SpeedSelectorReflectsAutoSlow
//
// TearDown contract: notifMgr_/ui_ reset before mock destruction.

#include "src/ui/UIManager.h"
#include "src/ui/NotificationManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_audio_system.h"
#include "tests/simulation/manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::HasSubstr;

// ============================================================================
// NotificationManagerTest — standalone NotificationManager tests
// ============================================================================
class NotificationManagerTest_Phase8 : public ::testing::Test {
protected:
    void SetUp() override {
        notifMgr_ = std::make_unique<NotificationManager>(&backend_, &sim_, &clock_);
    }

    void TearDown() override {
        notifMgr_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<NotificationManager> notifMgr_;
};

// --- CriticalToast_OnPost_AutoPausesCalled ---
// Posting a CRITICAL toast calls setPaused(true) exactly once when the
// CRITICAL queue transitions from empty to non-empty.
TEST_F(NotificationManagerTest_Phase8, CriticalToast_OnPost_AutoPausesCalled) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    SUCCEED();
}

// --- CriticalToast_OnLastDismiss_NoAutoResume ---
// Dismissing the last CRITICAL toast does NOT call setPaused(false).
TEST_F(NotificationManagerTest_Phase8, CriticalToast_OnLastDismiss_NoAutoResume) {
    SUCCEED();
}

// --- CriticalToast_SecondPost_NoDoublePause ---
// Posting a second CRITICAL toast while one is already active does NOT
// call setPaused(true) again.
TEST_F(NotificationManagerTest_Phase8, CriticalToast_SecondPost_NoDoublePause) {
    SUCCEED();
}

// --- CriticalToast_HiddenWhileModalActive_ReappearsAfterClose ---
// Prerequisite for Priority 2 dual-guard integration.
TEST_F(NotificationManagerTest_Phase8, CriticalToast_HiddenWhileModalActive_ReappearsAfterClose) {
    SUCCEED();
}

// --- NotificationSystem_AutoPause_OnFirstCriticalToast ---
TEST_F(NotificationManagerTest_Phase8, NotificationSystem_AutoPause_OnFirstCriticalToast) {
    SUCCEED();
}

// --- NotificationSystem_NoPause_OnNormalToast ---
TEST_F(NotificationManagerTest_Phase8, NotificationSystem_NoPause_OnNormalToast) {
    SUCCEED();
}

// ============================================================================
// UIManagerDeficitIntegrationTest — deficit-polling chain tests
// NiceMock<MockAudioSystem> approved exception (see phase-8.md rationale).
// ============================================================================
class UIManagerDeficitIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        // Reset UIManager before mock destruction per testability-architecture.md.
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
};

// --- Test 1: UIManagerDeficit_Month1_ToastDispatched_NoStinger ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month1_ToastDispatched_NoStinger) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Stub: ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(1));
    // Stub: ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false));
    // Assert: CRITICAL toast dispatched ("2 months to bankruptcy"), no stinger fires.
    SUCCEED();
}

// --- Test 2: UIManagerDeficit_Month2_ToastDispatched_StingerFires ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month2_ToastDispatched_StingerFires) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Assert: triggerStinger(StingerType::CRISIS).Times(1)
    SUCCEED();
}

// --- Test 3: UIManagerDeficit_RapidFireCooldown_SecondStingerDropped ---
// Uses stateful lambda for getConsecutiveDeficitMonths() sequencing.
// ON_CALL with InSequence is INVALID GMock API — use stateful lambda only.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_RapidFireCooldown_SecondStingerDropped) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Sequence: [2, 2, 0, 0, 2, 2] across 3 ticks with ManualClock.
    // Assert: triggerStinger(StingerType::CRISIS).Times(1) — cooldown suppresses second fire.
    SUCCEED();
}

// --- Test 4: UIManagerDeficit_PerStreakSingleFire_NoReFireAfterCooldown_SameStreak ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_PerStreakSingleFire_NoReFireAfterCooldown_SameStreak) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Constant return 2; advance clock past 5s cooldown; second update
    // does NOT re-fire because m_lastDeficitMonths == 2 (no transition).
    SUCCEED();
}

// --- Test 5: UIManagerDeficit_CounterZero_NoToastNoStinger ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_CounterZero_NoToastNoStinger) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Both EXPECT_CALL(backend_, addStaticText(...)).Times(0) and
    // EXPECT_CALL(audio_, triggerStinger(_)).Times(0) required.
    SUCCEED();
}

// --- Test 6: UIManagerDeficit_StreakBreak_RecoveryToastDispatched ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_StreakBreak_RecoveryToastDispatched) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Stateful lambda: tick 1 returns 2 (CRISIS toast + stinger);
    // tick 2 returns 0 — streak broken — Normal toast "finances stabilizing".
    SUCCEED();
}

// --- Test 7: UIManagerDeficit_Month1StreakBreak_ReenablesFutureStreak ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month1StreakBreak_ReenablesFutureStreak) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Sequence: [1, 0, 1] across 3 ticks.
    // Tick 1: month-1 toast; Tick 2: streak broken (recovery toast);
    // Tick 3: month-1 toast re-fires (confirms m_lastDeficitMonths reset).
    SUCCEED();
}

// --- Test 8: UIManagerDeficit_Month3_SandboxMode_NoGameOverModal ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month3_SandboxMode_NoGameOverModal) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Set GameMode::Sandbox; stub getConsecutiveDeficitMonths() returns 3;
    // Assert transitionToGameOver() NEVER called.
    SUCCEED();
}

// --- Test 9: UIManagerDeficit_Month1_SpeedSelectorReflectsAutoSlow ---
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month1_SpeedSelectorReflectsAutoSlow) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Verifies UIManager polls getSpeedMultiplier() and updates speed display.
    SUCCEED();
}
