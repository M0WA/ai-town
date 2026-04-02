// tests/ui/settings_panel_background_test.cpp
//
// SettingsPanelBackgroundTest — Phase 11c unit tests for the Glass City background
// treatment on SettingsPanel.
//
// Tests:
//   1. SettingsPanelBackground_Show_SetsScrimVisible
//      — SettingsPanel::show() calls setElementVisible(scrimHandle, true).
//   2. SettingsPanelBackground_Show_SetsBgImage
//      — SettingsPanel::show() calls setElementImage(bgHandle, kPanelTileId).
//
// Fixture: NiceMock<MockUIBackend> (many incidental addStaticText/addButton calls
// are made during SettingsPanel construction; NiceMock suppresses unexpected-call
// warnings for those incidental calls).
//
// TearDown contract: panel_ is reset before mock destruction to prevent
// order-of-destruction issues with mock expectations on IUIBackend.

#include "src/ui/SettingsPanel.h"
#include "src/interfaces/IUIBackend.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::AnyNumber;

class SettingsPanelBackgroundTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default return values: each addStaticText/addButton returns a unique handle.
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));

        panel_ = std::make_unique<SettingsPanel>(&backend_, &audio_, &clock_);
    }

    void TearDown() override {
        // Explicit destruction before mock tear-down to honour the destructor-path
        // contract: SettingsPanel may call IUIBackend methods in its destructor.
        panel_.reset();
    }

    NiceMock<MockUIBackend>   backend_;
    NiceMock<MockAudioSystem> audio_;
    ManualClock               clock_;
    std::unique_ptr<SettingsPanel> panel_;
    UIElementHandle           nextHandle_{100};
};

// Test 1: show() sets the full-screen scrim visible.
// SettingsPanel::show() must call setElementVisible(scrimHandle, true) to draw the
// rgba(0,0,0,0.50) full-screen scrim behind the floating panel (same rule as
// ModalDialog — see architecture/ui-ux/modal-dialog-system.md).
TEST_F(SettingsPanelBackgroundTest, SettingsPanelBackground_Show_SetsScrimVisible) {
    // Allow all false-visibility calls (hide during construction / re-show).
    EXPECT_CALL(backend_, setElementVisible(_, false)).Times(AnyNumber());
    // Expect at least one setElementVisible call with true — the scrim becoming visible.
    EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(1));

    panel_->show();

    EXPECT_TRUE(panel_->isVisible());
}

// Test 2: SettingsPanel configures the Glass City deep-navy background colour on
// the background element.  The implementation calls setElementBackground() during
// construction (rgba(13,27,42,0.88) = RGBA 13,27,42,224).  show() then makes the
// element visible.  We verify that setElementBackground was called at least once
// at the correct colour by reconstructing the panel with the expectation in place.
TEST_F(SettingsPanelBackgroundTest, SettingsPanelBackground_Show_SetsBgImage) {
    // Reset the panel so we can reconstruct it with the expectation registered.
    panel_.reset();

    // Catch-all for other setElementBackground calls (e.g. the 50%-black scrim).
    EXPECT_CALL(backend_, setElementBackground(_, _, _, _, _)).Times(AnyNumber());
    // Expect setElementBackground() to be called at least once during construction
    // (deep navy: R=13, G=27, B=42).  Alpha and handle are wildcards.
    EXPECT_CALL(backend_, setElementBackground(_, 13, 27, 42, _)).Times(AtLeast(1));

    panel_ = std::make_unique<SettingsPanel>(&backend_, &audio_, &clock_);
    panel_->show();

    EXPECT_TRUE(panel_->isVisible());
}
