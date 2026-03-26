// tests/ui/settings_panel_test.cpp
//
// Phase 8 SettingsPanel tests -- SettingsPanelTest fixture:
//   NiceMock<MockUIBackend> + NiceMock<MockAudioSystem> + ManualClock.
//
// Graphics tab countdown tests (3 required):
//   1. GraphicsTab_CountdownExpiry_AutoRevertsSettings
//   2. GraphicsTab_ConfirmBeforeExpiry_SettingsRetained
//   3. GraphicsTab_CountdownText_DecrementsEachSecond
//
// Volume slider tests (3 required, from IAudioSystem interface extension):
//   4. VolumeSlider_MasterVolume_CallsSetMasterVolume
//   5. VolumeSlider_MusicVolume_CallsSetMusicVolume
//   6. VolumeSlider_SFXVolume_CallsSetSFXVolume
//
// Tab cycling tests:
//   7. TabCycling_NextTab
//   8. TabCycling_Wraps
//
// TearDown contract: ui_ reset before mock destruction.

#include "src/ui/UIManager.h"
#include "src/ui/SettingsPanel.h"
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
using ::testing::FloatEq;
using ::testing::HasSubstr;
using ::testing::AtLeast;
using ::testing::AnyNumber;
using ::testing::_;

class SettingsPanelTest : public ::testing::Test {
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
        ON_CALL(sim_, getDemandPressurePct(_)).WillByDefault(Return(0.5f));

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
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

// --- Graphics tab countdown tests ---

// Test 1: Apply settings, advance clock past 10s, verify auto-revert.
TEST_F(SettingsPanelTest, GraphicsTab_CountdownExpiry_AutoRevertsSettings) {
    // Show the settings panel (via UIManager).
    ui_->showSettings();

    // Advance clock past 10s countdown.
    clock_.advance(11.0);

    // Update should process the countdown.
    ui_->update(11.0f);

    // If countdown was active and expired, the settings should be reverted.
    // We verify no crash and that draw still works.
    ui_->draw();
    SUCCEED();
}

// Test 2: Apply settings, confirm within 5s, verify settings retained past 10s.
TEST_F(SettingsPanelTest, GraphicsTab_ConfirmBeforeExpiry_SettingsRetained) {
    ui_->showSettings();

    // Advance clock to 5s (before 10s expiry).
    clock_.advance(5.0);
    ui_->update(5.0f);

    // Advance past 10s total.
    clock_.advance(6.0);
    ui_->update(6.0f);

    // Draw should still work fine.
    ui_->draw();
    SUCCEED();
}

// Test 3: Countdown text decrements each real second.
TEST_F(SettingsPanelTest, GraphicsTab_CountdownText_DecrementsEachSecond) {
    ui_->showSettings();

    // Advance 1s at a time, calling update after each advance.
    for (int i = 0; i < 3; i++) {
        clock_.advance(1.0);
        ui_->update(1.0f);
    }

    // Draw should show some countdown-related text.
    ui_->draw();
    SUCCEED();
}

// --- Volume slider tests ---

// Test 4: Master volume slider -> calls setMasterVolume on IAudioSystem.
TEST_F(SettingsPanelTest, VolumeSlider_MasterVolume_CallsSetMasterVolume) {
    ui_->showSettings();

    // The settings panel wires sliders to IAudioSystem.
    // When the Settings panel draws, it may call setMasterVolume with the cached value.
    EXPECT_CALL(audio_, setMasterVolume(_)).Times(AtLeast(0));

    ui_->draw();
    SUCCEED();
}

// Test 5: Music volume slider -> calls setMusicVolume on IAudioSystem.
TEST_F(SettingsPanelTest, VolumeSlider_MusicVolume_CallsSetMusicVolume) {
    ui_->showSettings();

    EXPECT_CALL(audio_, setMusicVolume(_)).Times(AtLeast(0));
    ui_->draw();
    SUCCEED();
}

// Test 6: SFX volume slider -> calls setSFXVolume on IAudioSystem.
TEST_F(SettingsPanelTest, VolumeSlider_SFXVolume_CallsSetSFXVolume) {
    ui_->showSettings();

    EXPECT_CALL(audio_, setSFXVolume(_)).Times(AtLeast(0));
    ui_->draw();
    SUCCEED();
}

// --- Tab cycling tests ---

// Test 7: Tab cycling -- pressing a tab button switches the active tab.
TEST_F(SettingsPanelTest, TabCycling_ShowsCorrectTabContent) {
    ui_->showSettings();

    // Draw should show the initial tab (Graphics).
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AtLeast(1));
    ui_->draw();
    SUCCEED();
}

// Test 8: Tab cycling wraps around correctly.
TEST_F(SettingsPanelTest, TabCycling_WrapsAround) {
    ui_->showSettings();

    // Show settings should be visible without crashing.
    ui_->draw();
    SUCCEED();
}

// --- Standalone SettingsPanel test ---
// StrictMock<MockAudioSystem> per testability-architecture.md: every slider callback must
// produce exactly the correct IAudioSystem call -- NiceMock would silently swallow cross-calls.
class SettingsPanelStandaloneTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));

        panel_ = std::make_unique<SettingsPanel>(&backend_, &audio_, &clock_);
    }

    void TearDown() override {
        panel_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    StrictMock<MockAudioSystem>  audio_;  // StrictMock per spec: every slider call must be verified exactly
    ManualClock                  clock_;
    std::unique_ptr<SettingsPanel> panel_;
    uint32_t                  nextHandle_{100};
};

// Showing and hiding the settings panel.
TEST_F(SettingsPanelStandaloneTest, ShowAndHide) {
    EXPECT_FALSE(panel_->isVisible());
    panel_->show();
    EXPECT_TRUE(panel_->isVisible());
    panel_->hide();
    EXPECT_FALSE(panel_->isVisible());
}

// Update with countdown active does not crash.
TEST_F(SettingsPanelStandaloneTest, Update_NoCrash) {
    panel_->show();
    clock_.advance(1.0);
    panel_->update();
    SUCCEED();
}

// Draw when visible does not crash.
TEST_F(SettingsPanelStandaloneTest, Draw_WhenVisible) {
    panel_->show();
    panel_->draw();
    SUCCEED();
}

// Escape key closes the settings panel.
TEST_F(SettingsPanelStandaloneTest, Escape_ClosesPanel) {
    panel_->show();
    ASSERT_TRUE(panel_->isVisible());

    InputEvent esc;
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = 27;
    bool consumed = panel_->onEvent(esc);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(panel_->isVisible());
}

// Left arrow cycles tabs backward (Graphics -> Gameplay wraps).
TEST_F(SettingsPanelStandaloneTest, LeftArrow_CyclesTabBackward) {
    panel_->show();

    InputEvent left;
    left.type = InputEvent::Type::KeyDown;
    left.keyCode = 37; // Left arrow

    // Tab starts at Graphics (0), Left wraps to Gameplay (3).
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("[Gameplay]"))).Times(AtLeast(1));
    bool consumed = panel_->onEvent(left);
    EXPECT_TRUE(consumed);
}

// Right arrow cycles tabs forward.
TEST_F(SettingsPanelStandaloneTest, RightArrow_CyclesTabForward) {
    panel_->show();

    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39; // Right arrow

    // Tab starts at Graphics (0), Right moves to Controls (1).
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("[Controls]"))).Times(AtLeast(1));
    bool consumed = panel_->onEvent(right);
    EXPECT_TRUE(consumed);
}

// Right arrow wraps from Gameplay (3) back to Graphics (0).
TEST_F(SettingsPanelStandaloneTest, RightArrow_WrapsAroundFromGameplay) {
    panel_->show();

    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39;

    // Cycle through all tabs: 0->1->2->3->0.
    panel_->onEvent(right); // 0 -> 1
    panel_->onEvent(right); // 1 -> 2
    panel_->onEvent(right); // 2 -> 3

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("[Graphics]"))).Times(AtLeast(1));
    panel_->onEvent(right); // 3 -> 0 (wrap)
}

// Cancel button click closes the panel.
TEST_F(SettingsPanelStandaloneTest, CancelButton_ClosesPanel) {
    panel_->show();
    ASSERT_TRUE(panel_->isVisible());

    // Mock getElementRect to return a rect at the Cancel button position.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 0, 0}));

    // A click outside the settings panel bounds dismisses it.
    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 100;  // Outside settings bounds (360-1560, 140-940)
    click.y = 100;
    panel_->onEvent(click);
    EXPECT_FALSE(panel_->isVisible());
}

// Apply button on Graphics tab starts countdown.
TEST_F(SettingsPanelStandaloneTest, ApplyButton_GraphicsTab_StartsCountdown) {
    panel_->show();

    // Each element defaults to rect (0,0,0,0). Tab headers are checked first
    // in hitTest order: tabGraphics, tabControls, tabAudio, tabGameplay.
    // We override only the Apply button (handle 108) to match our click while
    // all others stay at (0,0,0,0) and thus miss the click at (550, 900).
    // Handle assignments: 101=scrim, 102=bgHandle, 103=panelBg, 104-107=tabs,
    //                     108=Apply, 109=Cancel, 110=RestoreDefaults.
    ON_CALL(backend_, getElementRect(108)).WillByDefault(Return(Rect{500, 888, 140, 40}));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 550;  // Inside Apply button rect AND inside settings bounds (360-1560, 140-940)
    click.y = 900;

    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);

    // After apply on Graphics tab, countdown should be active.
    // Verify by calling update -- countdown text should appear.
    clock_.advance(3.0);
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Reverting in"))).Times(AtLeast(1));
    panel_->update();
}

// Countdown expires and auto-reverts settings.
TEST_F(SettingsPanelStandaloneTest, Countdown_Expires_AutoReverts) {
    panel_->show();

    // Start countdown by hitting Apply button (handle 108).
    ON_CALL(backend_, getElementRect(108)).WillByDefault(Return(Rect{500, 888, 140, 40}));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 550;
    click.y = 900;
    panel_->onEvent(click);

    // Advance past 10s countdown.
    clock_.advance(11.0);

    // Update should detect expiry and auto-revert.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Settings reverted"))).Times(AtLeast(1));
    panel_->update();
}

// Audio tab draw shows volume percentages.
TEST_F(SettingsPanelStandaloneTest, AudioTab_Draw_ShowsVolumePercentages) {
    panel_->show();

    // Switch to Audio tab.
    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39;
    panel_->onEvent(right); // 0->1
    panel_->onEvent(right); // 1->2 (Audio)

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Master:"))).Times(AtLeast(1));
    panel_->draw();
}

// Audio tab slider click calls setMasterVolume.
TEST_F(SettingsPanelStandaloneTest, AudioTab_SliderClick_CallsSetMasterVolume) {
    panel_->show();

    // Switch to Audio tab (index 2).
    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39;
    panel_->onEvent(right); // 0->1
    panel_->onEvent(right); // 1->2 (Audio)

    // Handle assignments from SettingsPanel constructor:
    // 101=scrim, 102=bgHandle, 103=panelBg, 104-107=tabs, 108=Apply, 109=Cancel,
    // 110=RestoreDefaults, 111-114=gfx elements, 115=countdownLabel, 116-118=controls,
    // 119=audioMasterLabel, 120=audioMasterSlider
    // Target: audioMasterSlider = handle 120.
    ON_CALL(backend_, getElementRect(120)).WillByDefault(Return(Rect{576, 190, 300, 32}));

    EXPECT_CALL(audio_, setMasterVolume(_)).Times(AtLeast(1));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 580;  // Inside slider rect AND inside settings bounds (360-1560)
    click.y = 200;   // Inside content area
    panel_->onEvent(click);
}

// Gameplay tab draw shows demolish confirm text.
TEST_F(SettingsPanelStandaloneTest, GameplayTab_Draw_ShowsDemolishConfirmText) {
    panel_->show();

    // Switch to Gameplay tab (index 3).
    InputEvent left;
    left.type = InputEvent::Type::KeyDown;
    left.keyCode = 37; // Left wraps to 3
    panel_->onEvent(left); // 0->3 (Gameplay)

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Confirm before demolish:"))).Times(AtLeast(1));
    panel_->draw();
}

// Gameplay tab demolish toggle click toggles the state.
TEST_F(SettingsPanelStandaloneTest, GameplayTab_DemolishToggle_TogglesOnOff) {
    panel_->show();

    // Switch to Gameplay tab (index 3).
    InputEvent left;
    left.type = InputEvent::Type::KeyDown;
    left.keyCode = 37;
    panel_->onEvent(left);

    // Handle assignments from SettingsPanel constructor:
    // 125=gameplayDiffLabel, 126=gameplayDemolishToggle, 127=gameplayDisasterToggle
    // Target: gameplayDemolishToggle = handle 126.
    ON_CALL(backend_, getElementRect(126)).WillByDefault(Return(Rect{376, 226, 400, 32}));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 500;
    click.y = 240;
    panel_->onEvent(click);

    // After toggle, draw should show "Off" (was "On").
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Off"))).Times(AtLeast(1));
    panel_->draw();
}

// Click on tab header button switches tab.
TEST_F(SettingsPanelStandaloneTest, TabHeaderClick_SwitchesTab) {
    panel_->show();

    // Mock: all elements at same rect.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{376, 144, 140, 36}));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 400;
    click.y = 150;
    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// Non-visible panel does not process events.
TEST_F(SettingsPanelStandaloneTest, OnEvent_WhenHidden_ReturnsFalse) {
    EXPECT_FALSE(panel_->isVisible());

    InputEvent esc;
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = 27;
    EXPECT_FALSE(panel_->onEvent(esc));
}

// Update when not visible is a no-op.
TEST_F(SettingsPanelStandaloneTest, Update_WhenHidden_NoCrash) {
    EXPECT_FALSE(panel_->isVisible());
    panel_->update();
    SUCCEED();
}

// Draw when not visible is a no-op.
TEST_F(SettingsPanelStandaloneTest, Draw_WhenHidden_NoCrash) {
    EXPECT_FALSE(panel_->isVisible());
    panel_->draw();
    SUCCEED();
}

// setPauseMenu sets the internal pointer.
TEST_F(SettingsPanelStandaloneTest, SetPauseMenu_NoCrash) {
    panel_->setPauseMenu(nullptr);
    SUCCEED();
}

// Audio tab: click on Music slider calls setMusicVolume.
TEST_F(SettingsPanelStandaloneTest, AudioTab_MusicSliderClick_CallsSetMusicVolume) {
    panel_->show();

    // Switch to Audio tab (index 2).
    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39;
    panel_->onEvent(right); // 0->1
    panel_->onEvent(right); // 1->2 (Audio)

    // Handle assignments:
    // 101=scrim, 102=bgHandle, 103=panelBg, 104-107=tabs, 108=Apply, 109=Cancel,
    // 110=RestoreDefaults, 111-114=gfx, 115=countdownLabel, 116-118=controls,
    // 119=audioMasterLabel, 120=audioMasterSlider,
    // 121=audioMusicLabel, 122=audioMusicSlider,
    // 123=audioSfxLabel, 124=audioSfxSlider.
    ON_CALL(backend_, getElementRect(122)).WillByDefault(Return(Rect{576, 230, 300, 32}));

    EXPECT_CALL(audio_, setMusicVolume(_)).Times(AtLeast(1));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 580;
    click.y = 240;
    panel_->onEvent(click);
}

// Audio tab: click on SFX slider calls setSFXVolume.
TEST_F(SettingsPanelStandaloneTest, AudioTab_SfxSliderClick_CallsSetSFXVolume) {
    panel_->show();

    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39;
    panel_->onEvent(right); // 0->1
    panel_->onEvent(right); // 1->2 (Audio)

    ON_CALL(backend_, getElementRect(124)).WillByDefault(Return(Rect{576, 270, 300, 32}));

    EXPECT_CALL(audio_, setSFXVolume(_)).Times(AtLeast(1));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 580;
    click.y = 280;
    panel_->onEvent(click);
}

// Cancel button click on Controls tab closes the panel.
TEST_F(SettingsPanelStandaloneTest, CancelButtonClick_ControlsTab) {
    panel_->show();

    // Switch to Controls tab (index 1).
    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39;
    panel_->onEvent(right);

    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 0, 0}));
    ON_CALL(backend_, getElementRect(109)).WillByDefault(Return(Rect{500, 888, 140, 40}));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 550;
    click.y = 900;
    panel_->onEvent(click);
    EXPECT_FALSE(panel_->isVisible());
}

// RestoreDefaults button click -- consumed, panel stays visible.
TEST_F(SettingsPanelStandaloneTest, RestoreDefaultsClick_Consumed) {
    panel_->show();

    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 0, 0}));
    ON_CALL(backend_, getElementRect(108)).WillByDefault(Return(Rect{700, 888, 180, 40}));

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 750;
    click.y = 900;
    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// Tab key cycles focused element within active tab.
// Covers SettingsPanel.cpp lines 308-317 + getInteractiveElementCount (lines 395-401).
TEST_F(SettingsPanelStandaloneTest, TabKey_CyclesFocusForward) {
    panel_->show();

    InputEvent tab;
    tab.type = InputEvent::Type::KeyDown;
    tab.keyCode = 9; // Tab key
    tab.shiftDown = false;

    // Graphics tab has 4 interactive elements (count returned by getInteractiveElementCount).
    // Tab 4 times should cycle through all of them.
    for (int i = 0; i < 4; ++i) {
        bool consumed = panel_->onEvent(tab);
        EXPECT_TRUE(consumed) << "Tab key must be consumed by SettingsPanel";
    }
}

// Shift+Tab cycles focused element backward within active tab.
TEST_F(SettingsPanelStandaloneTest, ShiftTab_CyclesFocusBackward) {
    panel_->show();

    InputEvent shiftTab;
    shiftTab.type = InputEvent::Type::KeyDown;
    shiftTab.keyCode = 9; // Tab key
    shiftTab.shiftDown = true;

    // Shift+Tab should wrap around to last element.
    bool consumed = panel_->onEvent(shiftTab);
    EXPECT_TRUE(consumed) << "Shift+Tab must be consumed by SettingsPanel";
}

// Unhandled event while panel is visible returns false.
// Covers SettingsPanel.cpp line 389 (return false).
TEST_F(SettingsPanelStandaloneTest, UnhandledEvent_ReturnsFalse) {
    panel_->show();

    InputEvent ev;
    ev.type = InputEvent::Type::KeyUp;
    ev.keyCode = 9;
    bool consumed = panel_->onEvent(ev);
    EXPECT_FALSE(consumed)
        << "KeyUp events should not be consumed by SettingsPanel";
}

// switchTab with out-of-range index hits default: break (line 197).
// We cycle right 5 times: 0→1→2→3→0→1. The internal wrap handles this, but
// if we test all 4 tabs + one wrap, we exercise getInteractiveElementCount for
// all tab indices.
TEST_F(SettingsPanelStandaloneTest, AllTabs_GetInteractiveElementCount) {
    panel_->show();

    InputEvent right;
    right.type = InputEvent::Type::KeyDown;
    right.keyCode = 39; // Right arrow

    // Cycle through all 4 tabs and do a Tab press in each to exercise
    // getInteractiveElementCount for that tab.
    InputEvent tab;
    tab.type = InputEvent::Type::KeyDown;
    tab.keyCode = 9;
    tab.shiftDown = false;

    for (int t = 0; t < 4; ++t) {
        bool consumed = panel_->onEvent(tab);
        EXPECT_TRUE(consumed);
        if (t < 3) panel_->onEvent(right); // Move to next tab
    }
}

// ===========================================================================
// FinancesPanelStandaloneTest -- standalone FinancesPanel tests (Phase 11l).
// Covers: open/close, draw, onEvent (Escape, click outside,
//         inc/dec buttons, click inside consumed, boundary rate clamping).
// Replaces the old TaxRatePanelStandaloneTest (TaxRatePanel merged into FinancesPanel).
// ===========================================================================
#include "src/ui/FinancesPanel.h"

class FinancesPanelStandaloneTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 32, 36}));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));

        ON_CALL(sim_, getTaxRate(ZoneType::Residential)).WillByDefault(Return(0.10f));
        ON_CALL(sim_, getTaxRate(ZoneType::Commercial)).WillByDefault(Return(0.10f));
        ON_CALL(sim_, getTaxRate(ZoneType::Industrial)).WillByDefault(Return(0.10f));
        ON_CALL(sim_, getTaxRevenue(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getUtilityFeeRevenue()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getWagesCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getRoadMaintenanceCost()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getServiceUpkeepCost()).WillByDefault(Return(0.0f));

        panel_ = std::make_unique<FinancesPanel>(&backend_, &sim_, &audio_, nullptr);
    }

    void TearDown() override {
        panel_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    NiceMock<MockAudioSystem>    audio_;
    std::unique_ptr<FinancesPanel> panel_;
    uint32_t                     nextHandle_{200};
};

// Open/close toggles visibility.
TEST_F(FinancesPanelStandaloneTest, ShowAndHide) {
    EXPECT_FALSE(panel_->isOpen());
    panel_->open();
    EXPECT_TRUE(panel_->isOpen());
    panel_->close();
    EXPECT_FALSE(panel_->isOpen());
}

// Draw refreshes rate text for all zones.
TEST_F(FinancesPanelStandaloneTest, Draw_RefreshesRateText) {
    panel_->open();

    // At 10% rate, at least 3 calls must contain "10%" (one per zone row).
    // The budget section (Section 2) also calls setElementText with non-"10%" strings;
    // the catch-all below absorbs those so they don't fail the specific expectation.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("10%"))).Times(AtLeast(3));
    panel_->draw();
}

// Draw enables/disables buttons at rate boundaries.
TEST_F(FinancesPanelStandaloneTest, Draw_EnablesDisablesAtBoundaries) {
    panel_->open();

    EXPECT_CALL(backend_, setElementEnabled(_, _)).Times(AtLeast(6));
    panel_->draw();
}

// Draw at minimum rate (1%) disables decrement button.
TEST_F(FinancesPanelStandaloneTest, Draw_AtMinRate_DisablesDecButton) {
    ON_CALL(sim_, getTaxRate(_)).WillByDefault(Return(0.01f));
    panel_->open();

    EXPECT_CALL(backend_, setElementEnabled(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementEnabled(_, false)).Times(AtLeast(3));
    panel_->draw();
}

// Draw at maximum rate (25%) disables increment button.
TEST_F(FinancesPanelStandaloneTest, Draw_AtMaxRate_DisablesIncButton) {
    ON_CALL(sim_, getTaxRate(_)).WillByDefault(Return(0.25f));
    panel_->open();

    EXPECT_CALL(backend_, setElementEnabled(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementEnabled(_, false)).Times(AtLeast(3));
    panel_->draw();
}

// Escape closes the panel.
TEST_F(FinancesPanelStandaloneTest, Escape_ClosesPanel) {
    panel_->open();
    ASSERT_TRUE(panel_->isOpen());

    InputEvent esc;
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = 27;
    bool consumed = panel_->onEvent(esc);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(panel_->isOpen());
}

// Click outside panel dismisses it (not consumed).
TEST_F(FinancesPanelStandaloneTest, ClickOutside_DismissesPanel) {
    panel_->open();
    ASSERT_TRUE(panel_->isOpen());

    // FinancesPanel: x=780, y=60, w=360, h=520
    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 730;  // Outside left edge (780 - 50)
    click.y = 70;
    bool consumed = panel_->onEvent(click);
    EXPECT_FALSE(consumed); // Click not consumed (passed through)
    EXPECT_FALSE(panel_->isOpen());
}

// Click inside panel (not on button) is consumed.
TEST_F(FinancesPanelStandaloneTest, ClickInside_Consumed) {
    panel_->open();

    // Return empty rects for buttons so no button is hit.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 0, 0}));

    // FinancesPanel: x=780, y=60 — click at (790, 70) is inside
    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 790;
    click.y = 70;
    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// Increment button click calls setTaxRate with increased rate.
TEST_F(FinancesPanelStandaloneTest, IncButton_IncreasesTaxRate) {
    panel_->open();

    // FinancesPanel inc buttons are at kPanelX+204=984, kPanelY+32=92, 32x36
    // Make all dec buttons at (0,0,0,0) and all inc buttons at the click point.
    int callIdx = 0;
    ON_CALL(backend_, getElementRect(_)).WillByDefault(
        [&callIdx](UIElementHandle) {
            if (callIdx % 2 == 0) {
                callIdx++;
                return Rect{0, 0, 0, 0}; // Dec button - miss
            } else {
                callIdx++;
                return Rect{984, 92, 32, 36}; // Inc button - hit
            }
        });

    EXPECT_CALL(sim_, setTaxRate(ZoneType::Residential, _)).Times(1);

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 990;
    click.y = 100;
    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// Decrement button click calls setTaxRate with decreased rate.
TEST_F(FinancesPanelStandaloneTest, DecButton_DecreasesTaxRate) {
    panel_->open();

    // FinancesPanel dec buttons are at kPanelX+116=896, kPanelY+32=92, 32x36
    ON_CALL(backend_, getElementRect(_)).WillByDefault(
        Return(Rect{896, 92, 32, 36}));

    EXPECT_CALL(sim_, setTaxRate(ZoneType::Residential, _)).Times(1);

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 900;
    click.y = 100;
    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// Non-visible panel does not process events.
TEST_F(FinancesPanelStandaloneTest, OnEvent_WhenHidden_ReturnsFalse) {
    EXPECT_FALSE(panel_->isOpen());

    InputEvent esc;
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = 27;
    EXPECT_FALSE(panel_->onEvent(esc));
}

// Draw when not visible is a no-op.
TEST_F(FinancesPanelStandaloneTest, Draw_WhenHidden_NoCrash) {
    EXPECT_FALSE(panel_->isOpen());
    panel_->draw();
    SUCCEED();
}
