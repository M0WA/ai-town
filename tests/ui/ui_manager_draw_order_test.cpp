// tests/ui/ui_manager_draw_order_test.cpp
//
// UIManagerDrawOrderTest — verifies that UIManager::draw() issues setElementVisible
// calls in the correct Z-order across all 10 named draw slots.
//
// Mock policy: NiceMock for ALL mocks (backend, audio, sim).
//   NiceMock suppresses "unexpected call" warnings for incidental calls that
//   occur during construction (e.g. MainMenuPanel::show() fires setElementVisible
//   from the UIManager constructor before the test body runs) and during draw()
//   for slots not under test in a given test case.
//
// Draw slot order verified against UIManager.cpp::draw():
//   slot 1:  m_mainMenu->draw()       -> setElementVisible(kMainMenuSentinel,     true)
//   slot 2:  m_minimap->draw()        -> setElementVisible(kMinimapSentinel,      true)
//   slot 3:  m_hud->draw()            -> setElementVisible(kHudSentinel,          true)
//   slot 4:  m_taxPanel->draw()       -> setElementVisible(kTaxPanelSentinel,     true)
//   slot 5:  m_inspector->draw()      -> setElementVisible(kInspectorSentinel,    true)
//   slot 6:  m_notifications->draw()  -> setElementVisible(kNotificationSentinel, true)
//   slot 7:  m_pauseMenu->draw()      -> setElementVisible(kPauseMenuSentinel,    true)
//   slot 8:  m_settings->draw()       -> setElementVisible(kSettingsSentinel,     true)
//   slot 9:  scrim                    -> setElementVisible(m_scrimHandle=0, true)
//                                        fires ONLY when m_modal->isActive() — not in Phase 3
//   slot 10: m_modal->draw()          -> setElementVisible(kModalSentinel,        false)
//                                        (m_active defaults false; modal stub is inactive)
//
// Note on kHudSentinel: its value is 0xDEAD0106u but it occupies draw SLOT 3.
// The sentinel value sequence is NOT the same as the draw slot sequence.

#include "src/ui/UIManager.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_audio_system.h"
#include "tests/ui/panel_sentinel_handles.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::_;

class UIManagerDrawOrderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // UIManager constructor: passes nullptr for IClock*.
        // Phase 3 stubs do not dereference IClock in draw() or in the panel
        // constructors, so nullptr is safe for this fixture.
        // MainMenuPanel calls show() from its own constructor, which fires
        // setElementVisible(kMainMenuSentinel, true) during SetUp().
        // NiceMock suppresses the warning for that incidental call.
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, nullptr);
    }

    void TearDown() override {
        // Explicit reset ensures UIManager is destroyed while all three mocks
        // are still alive. This satisfies the TearDown contract: mock destructors
        // run AFTER UIManager's destructor, preventing dangling-pointer use if
        // any panel destructor calls back into the backend.
        ui_.reset();
    }

    // MANDATORY member declaration order (C++ reverse-destruction):
    //   Mocks declared first -> destroyed last (outlive UIManager).
    //   ui_ declared last   -> destroyed first (UIManager destructed before mocks).
    NiceMock<MockUIBackend>         backend_;   // destroyed last
    NiceMock<MockAudioSystem>       audio_;
    NiceMock<MockCitySimulation>    sim_;
    std::unique_ptr<UIManager>      ui_;        // destroyed first
};

// Verifies all 10 draw slots fire in the correct Z-order.
// Slots 1-8 and slot 10 produce setElementVisible calls on their respective
// sentinel handles. The scrim (slot 9) uses m_scrimHandle = kInvalidUIElement (0)
// and fires only when the modal is active — hasActiveModal() delegates to
// m_modal->isActive(), which returns false (m_active defaults to false), so the scrim call does not appear.
// The modal (slot 10) always calls setElementVisible(kModalSentinel, false) because
// m_active defaults to false in the Phase 3 ModalDialog stub.
TEST_F(UIManagerDrawOrderTest, DrawOrder_AllSlots_CorrectZOrder) {
    InSequence seq;
    // slot 1
    EXPECT_CALL(backend_, setElementVisible(kMainMenuSentinel,     true));
    // slot 2
    EXPECT_CALL(backend_, setElementVisible(kMinimapSentinel,      true));
    // slot 3 — kHudSentinel = 0xDEAD0106u (slot 3, NOT slot 6)
    EXPECT_CALL(backend_, setElementVisible(kHudSentinel,          true));
    // slot 4
    EXPECT_CALL(backend_, setElementVisible(kTaxPanelSentinel,     true));
    // slot 5
    EXPECT_CALL(backend_, setElementVisible(kInspectorSentinel,    true));
    // slot 6
    EXPECT_CALL(backend_, setElementVisible(kNotificationSentinel, true));
    // slot 7
    EXPECT_CALL(backend_, setElementVisible(kPauseMenuSentinel,    true));
    // slot 8
    EXPECT_CALL(backend_, setElementVisible(kSettingsSentinel,     true));
    // slot 9 (scrim): m_scrimHandle == 0 (kInvalidUIElement); fires only when
    // modal is active. Phase 3 hasActiveModal() returns false -> no scrim call here.
    // slot 10: modal always fires but m_active == false in Phase 3 stub.
    EXPECT_CALL(backend_, setElementVisible(kModalSentinel,        false));

    ui_->draw();
}

// Verifies that the modal sentinel (slot 10) fires AFTER the notification
// sentinel (slot 6), pause menu (slot 7), and settings (slot 8).
// This is a targeted ordering assertion that would catch any reordering of
// the trailing slots in UIManager::draw().
TEST_F(UIManagerDrawOrderTest, DrawOrder_ModalFiresAfterNotificationPauseSettings) {
    // Absorb calls to sentinels not under test (slots 1-5 and scrim).
    // Registered before InSequence so they are not ordered; specific
    // InSequence expectations below take priority (LIFO matching).
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    InSequence seq;
    EXPECT_CALL(backend_, setElementVisible(kNotificationSentinel, true));
    EXPECT_CALL(backend_, setElementVisible(kPauseMenuSentinel,    true));
    EXPECT_CALL(backend_, setElementVisible(kSettingsSentinel,     true));
    // Slot 10: modal draw() passes m_active (false) as visibility.
    EXPECT_CALL(backend_, setElementVisible(kModalSentinel,        _));

    ui_->draw();
}

// Verifies that HUD (slot 3) fires AFTER minimap (slot 2) and BEFORE
// TaxPanel (slot 4). This covers the non-obvious ordering where kHudSentinel
// (0xDEAD0106u) occupies draw slot 3 despite its sentinel value suggesting slot 6.
TEST_F(UIManagerDrawOrderTest, DrawOrder_HudBetweenMinimapAndTaxPanel) {
    // Absorb calls to sentinels not under test (slots 1, 5-10).
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    InSequence seq;
    EXPECT_CALL(backend_, setElementVisible(kMinimapSentinel,  true));
    EXPECT_CALL(backend_, setElementVisible(kHudSentinel,      true));
    EXPECT_CALL(backend_, setElementVisible(kTaxPanelSentinel, true));

    ui_->draw();
}

// Verifies that PauseMenu (slot 7) fires AFTER Notification (slot 6).
// transitionToPaused() is a no-op stub in Phase 3; calling it does not affect
// the draw order, which is unconditional in the Phase 3 UIManager::draw() body.
TEST_F(UIManagerDrawOrderTest, DrawOrder_PauseMenuVisible_SlotSevenFiresAfterNotification) {
    ui_->transitionToPaused();  // no-op stub in Phase 3
    // Absorb calls to sentinels not under test (slots 1-5, 8, 10).
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    InSequence seq;
    EXPECT_CALL(backend_, setElementVisible(kNotificationSentinel, true));
    EXPECT_CALL(backend_, setElementVisible(kPauseMenuSentinel,    true));

    ui_->draw();
}

// Phase 3 compile-only stub (Phase 6 exit criterion per phase-3.md §278-279).
// Verifies: when a modal is explicitly activated, the scrim (slot 9) and modal (slot 10)
// fire AFTER the notification panel (slot 6) in UIManager::draw().
// Phase 6 wires showForcedLoanDialog() to call m_modal->show() so this test can fire
// the scrim branch; for now the stub reserves the test name.
TEST_F(UIManagerDrawOrderTest, DrawOrder_ModalActive_ScrimAndModalFireAfterPanels) {
    SUCCEED();
}
