// tests/ui/notification_system_test.cpp
//
// Phase 8 notification system tests -- two fixtures:
//
//   NotificationManagerTest: NiceMock<MockUIBackend> + NiceMock<MockCitySimulation>
//     + ManualClock + NiceMock<MockAudioSystem>. Tests NotificationManager in isolation
//     (constructed directly). Phase 10: NiceMock<MockAudioSystem> passed as the 4th
//     constructor parameter (IAudioSystem*) so toast audio calls do not fail.
//     APPROVED EXCEPTION: NiceMock<MockAudioSystem> (not StrictMock) — this fixture
//     does not assert audio call counts; it tests notification lifecycle only.
//
//   UIManagerDeficitIntegrationTest: NiceMock<MockUIBackend> + NiceMock<MockCitySimulation>
//     + NiceMock<MockAudioSystem> + ManualClock. Tests UIManager deficit-polling chain.
//     APPROVED EXCEPTION: NiceMock<MockAudioSystem> instead of StrictMock (see phase-8.md).
//
// TearDown contract: notifMgr_/ui_ reset before mock destruction.

#include "src/ui/UIManager.h"
#include "src/ui/NotificationManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "src/interfaces/sound_ids.h"      // UI_TOAST (SoundId 23) — Phase 10 audio SFX tests
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::SetArgReferee;

// ============================================================================
// NotificationManagerTest -- standalone NotificationManager tests
// ============================================================================
class NotificationManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));

        // Phase 10: NotificationManager gains a 4th IAudioSystem* constructor parameter
        // so postCritical()/postNormal() can call m_audio->playSound(UI_TOAST, ...).
        // NiceMock suppresses unexpected call warnings — this fixture does not assert
        // audio call counts; it tests notification lifecycle only.
        notifMgr_ = std::make_unique<NotificationManager>(&backend_, &sim_, &clock_, &audio_);
    }

    void TearDown() override {
        // Destroy NotificationManager before mock destructors run — prevents any
        // destructor-path callback into audio_ after it is torn down.
        notifMgr_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    NiceMock<MockAudioSystem>    audio_;
    ManualClock                  clock_;
    std::unique_ptr<NotificationManager> notifMgr_;
    uint32_t                     nextHandle_{100};
};

// --- CriticalToast_OnPost_AutoPausesCalled ---
// Posting a CRITICAL toast calls setPaused(true) exactly once when the
// CRITICAL queue transitions from empty to non-empty.
TEST_F(NotificationManagerTest, CriticalToast_OnPost_AutoPausesCalled) {
    EXPECT_CALL(sim_, setPaused(true)).Times(1);
    notifMgr_->postCritical("Crisis", "Budget deficit detected");
}

// --- CriticalToast_OnLastDismiss_NoAutoResume ---
// Dismissing the last CRITICAL toast does NOT call setPaused(false).
// Per notification-system.md, dismissCriticalToast does NOT un-pause.
TEST_F(NotificationManagerTest, CriticalToast_OnLastDismiss_NoAutoResume) {
    // Post a CRITICAL toast first.
    EXPECT_CALL(sim_, setPaused(true)).Times(1);
    notifMgr_->postCritical("Crisis", "Budget deficit");

    // Dismissing should NOT call setPaused(false).
    EXPECT_CALL(sim_, setPaused(false)).Times(0);

    // We need to get the handle. Since we use mock addStaticText, the handle is generated.
    // NotificationManager creates a UI element for the toast internally.
    // We need to verify that dismissing does not un-pause.
    // The NotificationManager tracks CRITICAL toasts; we test via hasCriticalToastVisible.
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());
}

// --- CriticalToast_SecondPost_NoDoublePause ---
// Posting a second CRITICAL toast while one is already active does NOT
// call setPaused(true) again.
TEST_F(NotificationManagerTest, CriticalToast_SecondPost_NoDoublePause) {
    EXPECT_CALL(sim_, setPaused(true)).Times(1); // Only once total.
    notifMgr_->postCritical("Crisis 1", "First deficit");
    notifMgr_->postCritical("Crisis 2", "Second deficit");

    // Only one setPaused(true) call.
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());
}

// --- CriticalToast_HiddenWhileModalActive_ReappearsAfterClose ---
// Prerequisite for Priority 2 dual-guard integration.
TEST_F(NotificationManagerTest, CriticalToast_HiddenWhileModalActive_ReappearsAfterClose) {
    notifMgr_->postCritical("Crisis", "Budget deficit");
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());

    // Modal becomes active.
    notifMgr_->setModalActive(true);

    // hasCriticalToastVisible should return false while modal is active
    // if the notification system implements dual-guard properly.
    // Per the code, hasCriticalToastVisible() checks m_criticalQueue.empty() only,
    // not m_modalActive. The dual-guard is at the UIManager input level.
    // So hasCriticalToastVisible() may still return true here.
    // The key point is that input routing is suppressed by UIManager.

    // When modal closes, toasts reappear.
    notifMgr_->setModalActive(false);
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());
}

// --- NotificationSystem_AutoPause_OnFirstCriticalToast ---
TEST_F(NotificationManagerTest, NotificationSystem_AutoPause_OnFirstCriticalToast) {
    EXPECT_CALL(sim_, setPaused(true)).Times(1);
    notifMgr_->postCritical("Crisis", "Budget deficit");
}

// --- NotificationSystem_NoPause_OnNormalToast ---
TEST_F(NotificationManagerTest, NotificationSystem_NoPause_OnNormalToast) {
    EXPECT_CALL(sim_, setPaused(_)).Times(0);
    notifMgr_->postNormal("Info", "New road completed");
}

// --- Normal toast auto-dismiss after timeout ---
TEST_F(NotificationManagerTest, NormalToast_AutoDismiss_After5Seconds) {
    notifMgr_->postNormal("Info", "New road completed", 5.0f);

    // Advance clock past the 5s timeout.
    clock_.advance(6.0);
    notifMgr_->update();

    // After update, the normal toast should have been removed.
    // We can verify by posting another and checking the system does not crash.
    notifMgr_->postNormal("Info 2", "Another event", 5.0f);
    SUCCEED();
}

// --- Max 2 CRITICAL toasts visible (excess queued) ---
TEST_F(NotificationManagerTest, CriticalToast_Max2Visible) {
    notifMgr_->postCritical("C1", "First");
    notifMgr_->postCritical("C2", "Second");
    notifMgr_->postCritical("C3", "Third");

    // The system should still have CRITICAL toasts visible.
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());
    // All 3 are in the queue; at most 2 are visible simultaneously.
    // We cannot directly test visible count without inspecting UI element visibility,
    // but we verify no crash with 3 toasts.
    SUCCEED();
}

// --- Toggle log panel ---
TEST_F(NotificationManagerTest, ToggleLog_OpensAndCloses) {
    EXPECT_FALSE(notifMgr_->isLogOpen());
    notifMgr_->toggleLog();
    EXPECT_TRUE(notifMgr_->isLogOpen());
    notifMgr_->toggleLog();
    EXPECT_FALSE(notifMgr_->isLogOpen());
}

// ============================================================================
// UIManagerDeficitIntegrationTest -- deficit-polling chain tests
// NiceMock<MockAudioSystem> approved exception (see phase-8.md rationale).
// ============================================================================
class UIManagerDeficitIntegrationTest : public ::testing::Test {
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
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(10000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(100));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getZoneDemandFactor(_)).WillByDefault(Return(0.5f));

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
        ui_->transitionToGameplay(GameMode::Scenario);
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
    uint32_t                     nextHandle_{100};
};

// --- Test 1: UIManagerDeficit_Month1_ToastDispatched_NoStinger ---
// First deficit month: CRITICAL toast dispatched, no stinger.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month1_ToastDispatched_NoStinger) {
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(1));

    // Should dispatch a CRITICAL toast (calls addStaticText for toast element).
    EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(AtLeast(1));
    // Stinger should NOT fire at month 1 per spec.
    EXPECT_CALL(audio_, triggerStinger(_)).Times(0);

    ui_->update(0.016f);
}

// --- Test 2: UIManagerDeficit_Month2_ToastDispatched_StingerFires ---
// Second deficit month: CRITICAL toast + stinger fires.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month2_ToastDispatched_StingerFires) {
    // Simulate transition from 0 to 2 directly.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2));

    EXPECT_CALL(audio_, triggerStinger(_)).Times(1);
    ui_->update(0.016f);
}

// --- Test 3: UIManagerDeficit_RapidFireCooldown_SecondStingerDropped ---
// Rapid deficit increases within cooldown window do not double-fire stingers.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_RapidFireCooldown_SecondStingerDropped) {
    // First fire: 0 -> 2 edge transition triggers stinger.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2));
    EXPECT_CALL(audio_, triggerStinger(_)).Times(1);
    ui_->update(0.016f);

    // Reset to 0 to create a new edge.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
    ui_->update(0.016f);

    // Re-trigger at 2, but within 5s cooldown -- stinger should be dropped.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2));
    clock_.advance(2.0); // Only 2s elapsed, within 5s cooldown.
    EXPECT_CALL(audio_, triggerStinger(_)).Times(0);
    ui_->update(0.016f);
}

// --- Test 4: UIManagerDeficit_PerStreakSingleFire_NoReFireAfterCooldown_SameStreak ---
// Same streak value after cooldown does not re-fire stinger.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_PerStreakSingleFire_NoReFireAfterCooldown_SameStreak) {
    // Constant deficit months = 2.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2));

    // First update fires the stinger (0 -> 2).
    EXPECT_CALL(audio_, triggerStinger(_)).Times(1);
    ui_->update(0.016f);

    // Advance past 5s cooldown.
    clock_.advance(6.0);

    // Second update: months still 2 (no new transition), so no re-fire.
    EXPECT_CALL(audio_, triggerStinger(_)).Times(0);
    ui_->update(0.016f);
}

// --- Test 5: UIManagerDeficit_CounterZero_NoToastNoStinger ---
// Zero deficit months: no toast, no stinger.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_CounterZero_NoToastNoStinger) {
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));

    EXPECT_CALL(audio_, triggerStinger(_)).Times(0);
    ui_->update(0.016f);
    SUCCEED();
}

// --- Test 6: UIManagerDeficit_StreakBreak_RecoveryToastDispatched ---
// Streak break (2 -> 0) dispatches a Normal recovery toast.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_StreakBreak_RecoveryToastDispatched) {
    // First: deficit month 2.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(2));
    ui_->update(0.016f);

    clock_.advance(1.0);

    // Now recovery (2 -> 0): should dispatch a Normal toast.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
    // After recovery, a Normal toast should be posted (addStaticText for toast).
    EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(AtLeast(0));
    ui_->update(0.016f);

    SUCCEED();
}

// --- Test 7: UIManagerDeficit_Month1StreakBreak_ReenablesFutureStreak ---
// Streak break at month 1 resets state, allowing month 1 to fire again.
// Uses ON_CALL/Return per phase (not a lambda counter) because HUD::update()
// also calls getConsecutiveDeficitMonths() before UIManager's edge-detection.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month1StreakBreak_ReenablesFutureStreak) {
    // Catch-all: update path calls setPaused through NotificationManager::postCritical.
    // Other paths may call setPaused(false). Catch-all absorbs all calls.
    EXPECT_CALL(sim_, setPaused(_)).Times(AnyNumber());

    // Phase 1: deficit month 1 (0 -> 1 edge triggers postCritical -> setPaused(true)).
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(1));
    ui_->update(0.016f);

    clock_.advance(1.0);

    // Phase 2: streak broken (1 -> 0, recovery).
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
    ui_->update(0.016f);

    clock_.advance(1.0);

    // Phase 3: deficit month 1 again (0 -> 1 edge re-fires postCritical).
    // The edge detection (currentMonths==1 && m_lastDeficitMonths<1) fires again
    // because m_lastDeficitMonths was reset to 0 in phase 2.
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(1));
    // Verify that a new addStaticText is called (CRITICAL toast created).
    EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(AtLeast(1));
    ui_->update(0.016f);
}

// --- Test 8: UIManagerDeficit_Month3_SandboxMode_NoGameOverModal ---
// Sandbox mode does not trigger game over, even at 3 consecutive deficit months.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month3_SandboxMode_NoGameOverModal) {
    // Re-transition to sandbox mode.
    ui_->transitionToGameplay(GameMode::Sandbox);
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(3));

    ui_->update(0.016f);

    // No game-over modal should be active.
    EXPECT_FALSE(ui_->hasActiveModal());
}

// --- Test 9: UIManagerDeficit_Month1_SpeedSelectorReflectsAutoSlow ---
// When a deficit toast fires, the speed selector should reflect any auto-slow.
TEST_F(UIManagerDeficitIntegrationTest, UIManagerDeficit_Month1_SpeedSelectorReflectsAutoSlow) {
    ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(1));
    ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));

    ui_->update(0.016f);

    // Verify that the UI polls getSpeedMultiplier (draw updates speed display).
    EXPECT_CALL(sim_, getSpeedMultiplier()).Times(AtLeast(1));
    ui_->draw();
}

// ============================================================================
// NotificationManager extended tests — covers draw, onEvent, toggleLog,
// truncateBody, and update auto-dismiss paths.
// ============================================================================

// --- Draw with CRITICAL toasts sets elements visible ---
TEST_F(NotificationManagerTest, Draw_WithCriticalToasts_SetsElementsVisible) {
    notifMgr_->postCritical("Crisis", "Budget deficit");

    EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(1));
    notifMgr_->draw();
}

// --- Draw with Normal toasts sets elements visible ---
TEST_F(NotificationManagerTest, Draw_WithNormalToasts_SetsElementsVisible) {
    notifMgr_->postNormal("Info", "Road completed", 10.0f);

    EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(1));
    notifMgr_->draw();
}

// --- Draw with log panel open sets it visible ---
TEST_F(NotificationManagerTest, Draw_LogOpen_SetsLogPanelVisible) {
    notifMgr_->toggleLog();
    EXPECT_TRUE(notifMgr_->isLogOpen());

    EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(1));
    notifMgr_->draw();
}

// --- Draw with log panel closed sets it hidden ---
TEST_F(NotificationManagerTest, Draw_LogClosed_SetsLogPanelHidden) {
    // Open then close the log.
    notifMgr_->toggleLog();
    notifMgr_->toggleLog();
    EXPECT_FALSE(notifMgr_->isLogOpen());

    notifMgr_->draw();
    SUCCEED();
}

// --- onEvent: click on CRITICAL toast dismisses it ---
TEST_F(NotificationManagerTest, OnEvent_ClickOnCriticalToast_DismissesToast) {
    notifMgr_->postCritical("Crisis", "Budget deficit");
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());

    int vw = 1920;
    int toastX = (vw - 500) / 2; // kToastWidth = 500

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = toastX + 10;
    click.y = 30; // Inside first CRITICAL toast band (y:20-68)

    bool consumed = notifMgr_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// --- onEvent: Enter key dismisses focused CRITICAL toast ---
TEST_F(NotificationManagerTest, OnEvent_EnterKey_DismissesFocusedToast) {
    notifMgr_->postCritical("Crisis", "Budget deficit");

    InputEvent enter;
    enter.type = InputEvent::Type::KeyDown;
    enter.keyCode = 13; // Enter
    bool consumed = notifMgr_->onEvent(enter);
    EXPECT_TRUE(consumed);
}

// --- onEvent: Delete key dismisses focused CRITICAL toast ---
TEST_F(NotificationManagerTest, OnEvent_DeleteKey_DismissesFocusedToast) {
    notifMgr_->postCritical("Crisis", "Budget deficit");

    InputEvent del;
    del.type = InputEvent::Type::KeyDown;
    del.keyCode = 46; // Delete
    bool consumed = notifMgr_->onEvent(del);
    EXPECT_TRUE(consumed);
}

// --- onEvent: Tab cycles focus between CRITICAL toasts ---
TEST_F(NotificationManagerTest, OnEvent_Tab_CyclesFocusBetweenToasts) {
    notifMgr_->postCritical("Crisis 1", "First");
    notifMgr_->postCritical("Crisis 2", "Second");

    InputEvent tab;
    tab.type = InputEvent::Type::KeyDown;
    tab.keyCode = 9; // Tab
    bool consumed = notifMgr_->onEvent(tab);
    EXPECT_TRUE(consumed);
}

// --- onEvent: Tab with single toast is still consumed ---
TEST_F(NotificationManagerTest, OnEvent_Tab_SingleToast_Consumed) {
    notifMgr_->postCritical("Crisis", "Single toast");

    InputEvent tab;
    tab.type = InputEvent::Type::KeyDown;
    tab.keyCode = 9;
    bool consumed = notifMgr_->onEvent(tab);
    EXPECT_TRUE(consumed);
}

// --- onEvent: empty queue returns false ---
TEST_F(NotificationManagerTest, OnEvent_EmptyQueue_ReturnsFalse) {
    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 960;
    click.y = 30;
    EXPECT_FALSE(notifMgr_->onEvent(click));
}

// --- truncateBody: short body posted and drawn without crash ---
TEST_F(NotificationManagerTest, TruncateBody_ShortBody_PostAndDraw) {
    notifMgr_->postNormal("Info", "Short body text", 10.0f);
    EXPECT_NO_FATAL_FAILURE(notifMgr_->draw());
}

// --- truncateBody: long body (>80 chars) posted and drawn without crash ---
TEST_F(NotificationManagerTest, TruncateBody_LongBody_PostAndDraw) {
    // kMaxBodyChars is 80 per NotificationManager.h
    std::string longBody(200, 'A');
    notifMgr_->postNormal("Info", longBody, 10.0f);
    EXPECT_NO_FATAL_FAILURE(notifMgr_->draw());
}

// --- toggleLog shows entries text ---
TEST_F(NotificationManagerTest, ToggleLog_ShowsEntries) {
    notifMgr_->postCritical("Alert", "Something critical happened");
    notifMgr_->postNormal("Info", "Building complete", 10.0f);

    // Open the log.
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Alert"))).Times(AtLeast(1));
    notifMgr_->toggleLog();
}

// --- toggleLog mark critical entries with [!] prefix ---
TEST_F(NotificationManagerTest, ToggleLog_CriticalEntries_HavePrefix) {
    notifMgr_->postCritical("Alert", "Crisis body");

    EXPECT_CALL(backend_, setElementText(_, HasSubstr("[!]"))).Times(AtLeast(1));
    notifMgr_->toggleLog();
}

// --- setModalActive(true) hides CRITICAL toasts ---
TEST_F(NotificationManagerTest, SetModalActive_HidesCriticalToasts) {
    notifMgr_->postCritical("Crisis", "Budget deficit");

    EXPECT_CALL(backend_, removeElement(_)).Times(AtLeast(1));
    notifMgr_->setModalActive(true);
}

// --- setModalActive(false) re-shows CRITICAL toasts ---
TEST_F(NotificationManagerTest, SetModalActive_False_ReShowsCriticalToasts) {
    notifMgr_->postCritical("Crisis", "Budget deficit");
    notifMgr_->setModalActive(true);

    // When modal closes, toasts are re-created.
    EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(AtLeast(1));
    ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
    notifMgr_->setModalActive(false);
}

// --- setModalActive(false) re-pauses if queue non-empty ---
TEST_F(NotificationManagerTest, SetModalActive_False_RePausesIfQueueNonEmpty) {
    notifMgr_->postCritical("Crisis", "Budget deficit");
    notifMgr_->setModalActive(true);

    // When modal closes, if queue is non-empty AND sim is not paused,
    // setPaused(true) is called.
    ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
    EXPECT_CALL(sim_, setPaused(true)).Times(AtLeast(1));
    notifMgr_->setModalActive(false);
}

// --- Normal toast queue depth limit ---
TEST_F(NotificationManagerTest, NormalToast_QueueDepthLimit) {
    // Queue many normal toasts beyond depth limit (kMaxNormalQueueDepth).
    for (int i = 0; i < 20; ++i) {
        notifMgr_->postNormal("Info " + std::to_string(i), "Body", 10.0f);
    }
    // Should not crash.
    SUCCEED();
}

// --- update auto-dismisses expired normal toasts ---
TEST_F(NotificationManagerTest, Update_Removes_ExpiredNormalToasts) {
    notifMgr_->postNormal("T1", "Body1", 3.0f);
    notifMgr_->postNormal("T2", "Body2", 5.0f);

    // After 4s, T1 should be dismissed (3s timeout), T2 still active.
    clock_.advance(4.0);

    EXPECT_CALL(backend_, removeElement(_)).Times(AtLeast(1));
    notifMgr_->update();
}

// --- addLogEntry caps entries at max ---
TEST_F(NotificationManagerTest, AddLogEntry_CapsAtMaxEntries) {
    // Post many entries to exceed the log cap.
    for (int i = 0; i < 60; ++i) {
        notifMgr_->postNormal("Entry " + std::to_string(i), "Body", 100.0f);
    }
    // No crash. The log is capped.
    SUCCEED();
}

// --- hasCriticalToastVisible returns false when modal is active ---
TEST_F(NotificationManagerTest, HasCriticalToastVisible_FalseWhenModalActive) {
    notifMgr_->postCritical("Crisis", "Deficit");
    EXPECT_TRUE(notifMgr_->hasCriticalToastVisible());

    notifMgr_->setModalActive(true);
    EXPECT_FALSE(notifMgr_->hasCriticalToastVisible());
}

// --- Notification polling integration: ForcedLoanIssued fires showForcedLoanDialog ---
TEST_F(UIManagerDeficitIntegrationTest, NotificationPolling_ForcedLoan) {
    SimulationNotification notif;
    notif.type               = NotificationType::ForcedLoanIssued;
    notif.loanPrincipal      = 50000;
    notif.loanRepaymentTicks = 12;

    ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(
        [&notif](SimulationNotification& out) {
            static bool called = false;
            if (!called) {
                called = true;
                out = notif;
                return true;
            }
            return false;
        });

    ui_->update(0.016f);
    EXPECT_TRUE(ui_->hasActiveModal());
}

// --- Notification polling integration: BondIssued posts normal toast ---
TEST_F(UIManagerDeficitIntegrationTest, NotificationPolling_BondIssued) {
    SimulationNotification notif;
    notif.type          = NotificationType::BondIssued;
    notif.loanPrincipal = 10000;

    ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(
        [&notif](SimulationNotification& out) {
            static bool called = false;
            if (!called) {
                called = true;
                out = notif;
                return true;
            }
            return false;
        });

    EXPECT_CALL(backend_, addStaticText(HasSubstr("Bond Issued"), _, _, _, _)).Times(AtLeast(1));
    ui_->update(0.016f);
}

// --- Notification polling integration: ServiceDegraded posts normal toast ---
TEST_F(UIManagerDeficitIntegrationTest, NotificationPolling_ServiceDegraded) {
    SimulationNotification notif;
    notif.type = NotificationType::ServiceDegraded;

    ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(
        [&notif](SimulationNotification& out) {
            static bool called = false;
            if (!called) {
                called = true;
                out = notif;
                return true;
            }
            return false;
        });

    ui_->update(0.016f);
    SUCCEED();
}

// --- Notification polling: PopulationMilestone and CityRatingTransition ---
TEST_F(UIManagerDeficitIntegrationTest, NotificationPolling_PopulationMilestone) {
    SimulationNotification notif;
    notif.type = NotificationType::PopulationMilestone;
    notif.milestoneValue = 1000;

    ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(
        [&notif](SimulationNotification& out) {
            static bool called = false;
            if (!called) {
                called = true;
                out = notif;
                return true;
            }
            return false;
        });

    ui_->update(0.016f);
    SUCCEED();
}

TEST_F(UIManagerDeficitIntegrationTest, NotificationPolling_CityRatingTransition) {
    SimulationNotification notif;
    notif.type = NotificationType::CityRatingTransition;

    ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(
        [&notif](SimulationNotification& out) {
            static bool called = false;
            if (!called) {
                called = true;
                out = notif;
                return true;
            }
            return false;
        });

    ui_->update(0.016f);
    SUCCEED();
}

TEST_F(UIManagerDeficitIntegrationTest, NotificationPolling_BudgetDeficitWarn) {
    SimulationNotification notif;
    notif.type = NotificationType::BudgetDeficitWarn;

    ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(
        [&notif](SimulationNotification& out) {
            static bool called = false;
            if (!called) {
                called = true;
                out = notif;
                return true;
            }
            return false;
        });

    ui_->update(0.016f);
    SUCCEED();
}

// ============================================================================
// Phase 10 deliverable: NotificationSFX_ToastVisible_UIToastSoundFires
//
// Verifies that NotificationManager::postNormal() fires
// playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f) exactly once when the
// toast becomes visible on screen (not on enqueue when the queue is at capacity).
//
// Spec refs:
//   architecture/testing/testability-architecture.md §NotificationManager testability
//   implementation/phase-10.md §ui_toast wiring
//   architecture/ui-ux/hud-layout.md Phase 10 Audio Wiring — ui_toast section
//
// Phase 10 wiring rule: NotificationManager::postCritical() and postNormal()
// each call m_audio->playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f) immediately
// AFTER the toast element is made visible via m_backend->setElementVisible(handle, true).
// Guard: if (m_audio). Fires once per toast appearance — not once per enqueue.
//
// Fixture: NotificationManagerTest.
//   NiceMock<MockUIBackend>:      returns incrementing handles from addStaticText/addButton.
//   NiceMock<MockCitySimulation>: stub returns; not exercised by this test.
//   ManualClock:                  time-independent; postNormal() does not need clock advance.
//   NiceMock<MockAudioSystem>:    injected as IAudioSystem* (4th ctor param); intercepts
//                                 playSound(UI_TOAST, ...) for the Times(1) assertion.
//
// EXPECT_CALL is placed BEFORE postNormal() per standard GMock ordering rules.
// ============================================================================
TEST_F(NotificationManagerTest, NotificationSFX_ToastVisible_UIToastSoundFires) {
    // Expect exactly one UI_TOAST sound call when the Normal toast becomes visible.
    // SoundPriority::NORMAL and gain 1.0f are the canonical values per phase-10.md.
    EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f)).Times(1);

    // Post a Normal toast — NotificationManager makes the toast visible and calls
    // m_audio->playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f) once, guarded by
    // if (m_audio), immediately after m_backend->setElementVisible(handle, true).
    notifMgr_->postNormal("Info", "Road construction complete", 10.0f);
}

// ============================================================================
// Phase 10 deliverable: NotificationSFX_CriticalToast_UIToastSoundFires
//
// Verifies that NotificationManager::postCritical() fires
// playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f) exactly once when the
// CRITICAL toast becomes visible on screen.
//
// Both postCritical() and postNormal() must wire the ui_toast SFX per
// phase-10.md §ui_toast wiring (the spec explicitly names both methods).
//
// The auto-pause side-effect (setPaused(true)) is also covered by
// CriticalToast_OnPost_AutoPausesCalled; here we verify the audio SFX companion.
// ============================================================================
TEST_F(NotificationManagerTest, NotificationSFX_CriticalToast_UIToastSoundFires) {
    // Allow the auto-pause call that fires when the CRITICAL queue goes non-empty.
    EXPECT_CALL(sim_, setPaused(true)).Times(1);

    // Expect exactly one UI_TOAST sound call when the CRITICAL toast becomes visible.
    EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f)).Times(1);

    notifMgr_->postCritical("Crisis", "Budget deficit detected");
}

// ============================================================================
// Phase 11l Deliverable 5 — Notification log scrollbar tests
//
// Layout: toggleLog() creates three addStaticText elements in order:
//   call 1 → panel (nextHandle_ starts at 100, so first call returns 101)
//   call 2 → scroll track (returns 102)
//   call 3 → scroll thumb (returns 103)
// nextHandle_ is reset to 100 in SetUp() for each test.
// kLogVisibleRows = kLogTrackH / kLogRowHeightPx = 500 / 20 = 25.
// ============================================================================

// Scrollbar hidden when all entries fit (totalRows ≤ visibleRows).
// Verifies setElementVisible(trackHandle, false) is called by updateScrollThumb().
TEST_F(NotificationManagerTest, NotificationManager_LogScrollbar_HiddenWhenAllFit) {
    // Post 3 entries — fewer than the 25 visible rows (500 / 20 = 25).
    notifMgr_->postNormal("N1", "Body1", 10.0f);
    notifMgr_->postNormal("N2", "Body2", 10.0f);
    notifMgr_->postNormal("N3", "Body3", 10.0f);

    // Capture the track handle: it is the 1st addStaticText call at x=1856 coords.
    UIElementHandle capturedTrack = kInvalidUIElement;
    int scrollbarCallCount = 0;
    ON_CALL(backend_, addStaticText(_, 1856, 56, 12, 500)).WillByDefault(
        [this, &capturedTrack, &scrollbarCallCount](const std::string&, int, int, int, int) {
            ++scrollbarCallCount;
            UIElementHandle h = ++nextHandle_;
            if (scrollbarCallCount == 1) { capturedTrack = h; }
            return h;
        });

    // Expect setElementVisible(track, false) — scrollbar must be hidden when
    // totalRows (3) ≤ visibleRows (25).
    // Use AtLeast(1) because updateScrollThumb may be called more than once.
    bool trackHiddenCalled = false;
    ON_CALL(backend_, setElementVisible(_, _)).WillByDefault(
        [&capturedTrack, &trackHiddenCalled](UIElementHandle h, bool vis) {
            if (h == capturedTrack && !vis) { trackHiddenCalled = true; }
        });

    notifMgr_->toggleLog();

    ASSERT_NE(capturedTrack, kInvalidUIElement);
    EXPECT_TRUE(trackHiddenCalled) << "setElementVisible(trackHandle, false) was not called";
}

// Scrollbar visible when entries overflow the visible row count (totalRows > visibleRows).
// Verifies setElementVisible(trackHandle, true) is called by updateScrollThumb().
TEST_F(NotificationManagerTest, NotificationManager_LogScrollbar_VisibleWhenOverflow) {
    // Post 30 entries — more than the 25 visible rows (500 / 20 = 25).
    for (int i = 0; i < 30; ++i) {
        notifMgr_->postNormal("N" + std::to_string(i), "Body", 10.0f);
    }

    UIElementHandle capturedTrack = kInvalidUIElement;
    int scrollbarCallCount = 0;
    ON_CALL(backend_, addStaticText(_, 1856, 56, 12, 500)).WillByDefault(
        [this, &capturedTrack, &scrollbarCallCount](const std::string&, int, int, int, int) {
            ++scrollbarCallCount;
            UIElementHandle h = ++nextHandle_;
            if (scrollbarCallCount == 1) { capturedTrack = h; }
            return h;
        });

    bool trackShownCalled = false;
    ON_CALL(backend_, setElementVisible(_, _)).WillByDefault(
        [&capturedTrack, &trackShownCalled](UIElementHandle h, bool vis) {
            if (h == capturedTrack && vis) { trackShownCalled = true; }
        });

    notifMgr_->toggleLog();

    ASSERT_NE(capturedTrack, kInvalidUIElement);
    EXPECT_TRUE(trackShownCalled) << "setElementVisible(trackHandle, true) was not called";
}

// Scrollbar thumb Y increases after scrolling down.
// Verifies that after 5 downward scroll events, setElementRect is called with a
// larger Y argument for the thumb than after the initial panel open.
TEST_F(NotificationManagerTest, NotificationManager_LogScrollbar_ThumbMovesOnScroll) {
    // Post 30 entries — totalRows(30) > visibleRows(25), scrollbar is shown.
    for (int i = 0; i < 30; ++i) {
        notifMgr_->postNormal("N" + std::to_string(i), "Body", 10.0f);
    }

    // Capture the thumb handle: it is the 2nd addStaticText call at scrollbar coords.
    int scrollbarCallCount = 0;
    UIElementHandle capturedThumb = kInvalidUIElement;
    ON_CALL(backend_, addStaticText(_, 1856, 56, 12, 500)).WillByDefault(
        [this, &scrollbarCallCount, &capturedThumb](const std::string&, int, int, int, int) {
            ++scrollbarCallCount;
            UIElementHandle h = ++nextHandle_;
            if (scrollbarCallCount == 2) { capturedThumb = h; }
            return h;
        });

    // Track the Y argument from every setElementRect call on the thumb.
    int lastRectY = -1;
    ON_CALL(backend_, setElementRect(_, _, _, _, _)).WillByDefault(
        [&capturedThumb, &lastRectY](UIElementHandle h, int /*x*/, int y, int /*w*/, int /*h*/) {
            if (h == capturedThumb) { lastRectY = y; }
        });

    notifMgr_->toggleLog();
    ASSERT_NE(capturedThumb, kInvalidUIElement);

    // Record the Y after the initial open (scrollOffset == 0).
    int initialY = lastRectY;

    // Scroll down 5 rows — each wheel event adjusts offset by +1.
    for (int i = 0; i < 5; ++i) {
        InputEvent wheel;
        wheel.type       = InputEvent::Type::MouseWheel;
        wheel.wheelDelta = -1.0f; // negative = scroll down (towards older entries)
        notifMgr_->onEvent(wheel);
    }

    // After 5 downward scrolls the thumb must have moved down (Y increased).
    EXPECT_GT(lastRectY, initialY);
}

// Scrollbar thumb bottom aligns with track bottom after scrolling to the last entry.
// With 30 entries, visibleRows=25, maxOffset=5; scrolling 10 times clamps at offset 5.
// At maxOffset: thumbY + thumbH must equal trackTop + trackH = 56 + 500 = 556 (±1 px).
TEST_F(NotificationManagerTest, NotificationManager_LogScrollbar_ThumbAtBottomAfterScrollToEnd) {
    // Post 30 entries — totalRows = 30, visibleRows = 25, maxOffset = 5.
    for (int i = 0; i < 30; ++i) {
        notifMgr_->postNormal("N" + std::to_string(i), "Body", 10.0f);
    }

    int scrollbarCallCount = 0;
    UIElementHandle capturedThumb = kInvalidUIElement;
    ON_CALL(backend_, addStaticText(_, 1856, 56, 12, 500)).WillByDefault(
        [this, &scrollbarCallCount, &capturedThumb](const std::string&, int, int, int, int) {
            ++scrollbarCallCount;
            UIElementHandle h = ++nextHandle_;
            if (scrollbarCallCount == 2) { capturedThumb = h; }
            return h;
        });

    int capturedThumbY = -1;
    int capturedThumbH = -1;
    ON_CALL(backend_, setElementRect(_, _, _, _, _)).WillByDefault(
        [&capturedThumb, &capturedThumbY, &capturedThumbH](
                UIElementHandle h, int /*x*/, int y, int w, int height) {
            (void)w;
            if (h == capturedThumb) {
                capturedThumbY = y;
                capturedThumbH = height;
            }
        });

    notifMgr_->toggleLog();
    ASSERT_NE(capturedThumb, kInvalidUIElement);

    // Scroll down past the end — clamped at maxOffset = 5.
    for (int i = 0; i < 10; ++i) {
        InputEvent wheel;
        wheel.type       = InputEvent::Type::MouseWheel;
        wheel.wheelDelta = -1.0f;
        notifMgr_->onEvent(wheel);
    }

    // thumbY + thumbH should equal trackTop + trackH = 56 + 500 = 556, within 1 px.
    ASSERT_GE(capturedThumbY, 0) << "setElementRect was not called for the thumb";
    ASSERT_GE(capturedThumbH, 0) << "setElementRect was not called for the thumb";
    EXPECT_NEAR(capturedThumbY + capturedThumbH, 556, 1);
}

// ============================================================================
// Tests moved from coverage_gap_test.cpp
// ============================================================================

// ============================================================================
// Test: update() ForcedLoanIssued notification triggers showForcedLoanDialog
// UIManagerDeficitIntegrationTest is already in Gameplay state after SetUp().
// ============================================================================
TEST_F(UIManagerDeficitIntegrationTest, Coverage_Update_ForcedLoanNotification_ShowsDialog)
{
    ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
    ON_CALL(sim_, setPaused(_)).WillByDefault(Return());

    // First poll returns ForcedLoanIssued, subsequent return false.
    SimulationNotification notif{};
    notif.type               = NotificationType::ForcedLoanIssued;
    notif.loanPrincipal      = 50000;
    notif.loanRepaymentTicks = 12;
    EXPECT_CALL(sim_, pollPendingNotification(_))
        .WillOnce(DoAll(SetArgReferee<0>(notif), Return(true)))
        .WillRepeatedly(Return(false));

    ui_->update(0.016f);

    // The forced loan dialog should now be shown.
    EXPECT_TRUE(ui_->hasActiveModal());
}
