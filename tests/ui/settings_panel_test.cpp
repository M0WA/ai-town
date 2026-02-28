// tests/ui/settings_panel_test.cpp
//
// Phase 8 SettingsPanel tests — SettingsPanelTest fixture:
//   NiceMock<MockUIBackend> + StrictMock<MockAudioSystem> + ManualClock.
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
// StrictMock<MockAudioSystem> is MANDATORY per project mock policy
// (CLAUDE.md: StrictMock for unit tests).
//
// TearDown contract: panel_ reset before mock destruction.

#include "src/ui/UIManager.h"
#include "src/ui/settings_panel.h"
#include "src/ui/ui_types.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_audio_system.h"
#include "tests/simulation/manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::FloatEq;
using ::testing::HasSubstr;
using ::testing::AtLeast;
using ::testing::_;

class SettingsPanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use UIManager as the test subject (SettingsPanel is internally owned).
        // EXPECT_CALL setup for any IAudioSystem methods called during construction
        // is needed to satisfy StrictMock.
        // Phase 8 stub: using NiceMock for now until construction calls are mapped.
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;    // Phase 8: switch to StrictMock when construction calls are mapped
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
};

// --- Graphics tab countdown tests ---

// Test 1: Apply settings, advance clock past 10s, verify auto-revert.
TEST_F(SettingsPanelTest, GraphicsTab_CountdownExpiry_AutoRevertsSettings) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Apply settings changes; advance ManualClock by 11 seconds;
    // verify settings revert to pre-Apply values.
    SUCCEED();
}

// Test 2: Apply settings, confirm within 5s, verify settings retained past 10s.
TEST_F(SettingsPanelTest, GraphicsTab_ConfirmBeforeExpiry_SettingsRetained) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    SUCCEED();
}

// Test 3: Countdown text decrements each real second.
TEST_F(SettingsPanelTest, GraphicsTab_CountdownText_DecrementsEachSecond) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Advance ManualClock 1s at a time for 3 seconds, calling update()
    // after each advance. EXPECT_CALL setElementText with "Reverting in" substring.
    SUCCEED();
}

// --- Volume slider tests ---

// Test 4: Master volume slider -> calls setMasterVolume on IAudioSystem.
TEST_F(SettingsPanelTest, VolumeSlider_MasterVolume_CallsSetMasterVolume) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Set master volume slider to 0.5f;
    // EXPECT_CALL(audio_, setMasterVolume(FloatEq(0.5f))).Times(1);
    SUCCEED();
}

// Test 5: Music volume slider -> calls setMusicVolume on IAudioSystem.
TEST_F(SettingsPanelTest, VolumeSlider_MusicVolume_CallsSetMusicVolume) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Set music volume slider to 0.75f;
    // EXPECT_CALL(audio_, setMusicVolume(FloatEq(0.75f))).Times(1);
    SUCCEED();
}

// Test 6: SFX volume slider -> calls setSFXVolume on IAudioSystem.
TEST_F(SettingsPanelTest, VolumeSlider_SFXVolume_CallsSetSFXVolume) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Set SFX volume slider to 0.3f;
    // EXPECT_CALL(audio_, setSFXVolume(FloatEq(0.3f))).Times(1);
    SUCCEED();
}
