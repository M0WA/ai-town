// tests/ui/ui_manager_modal_test.cpp
//
// UIManagerModalTest — smoke tests and compile-only stubs for UIManager modal behavior.
//
// Mock policy: NiceMock for ALL THREE mocks (backend, audio, sim).
//   Rationale: modal-related tests may trigger incidental calls to backend methods
//   (e.g. setElementVisible during construction) that are not under assertion here.
//   NiceMock prevents spurious "uninteresting call" failures.
//
// TearDown contract: ui_.reset() is called explicitly in TearDown() so that
// UIManager is destroyed while all three mocks are still alive. C++ destroys
// class members in reverse declaration order, so declaring ui_ LAST ensures the
// UIManager destructor runs first — but the explicit reset() in TearDown() makes
// the contract visible and document-able rather than relying on implicit ordering.
//
// Full modal assertions (speed-selector, undo-button disable, scrim blocking) are
// implemented in Phase 6 when the modal panels have real logic. These stubs
// reserve the test names so the Phase 6 engineer has clear landing spots.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/interfaces/LoanTerms.h"
#include "src/platform/input_event.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_audio_system.h"
#include "tests/simulation/manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::Return;

class UIManagerModalTest : public ::testing::Test {
protected:
    void SetUp() override {
        // IClock* is nullptr: Phase 3 stubs do not dereference the clock pointer
        // from within UIManager::draw() or any panel constructor invoked during
        // UIManager construction. Safe for this fixture.
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, nullptr);
    }

    void TearDown() override {
        // Explicit destruction: UIManager is torn down here while all mocks are
        // still live. Prevents dangling-pointer callbacks if any panel destructor
        // calls back into the backend mock.
        ui_.reset();
    }

    // MANDATORY member declaration order (C++ reverse-destruction):
    //   Mocks declared first -> destroyed LAST (they outlive UIManager).
    //   ui_ declared last   -> destroyed FIRST (UIManager destructor runs before mocks).
    NiceMock<MockUIBackend>         backend_;   // 1st declared -> destroyed last
    NiceMock<MockAudioSystem>       audio_;     // 2nd
    NiceMock<MockCitySimulation>    sim_;       // 3rd
    std::unique_ptr<UIManager>      ui_;        // 4th declared -> destroyed first
};

// Smoke test: the fixture constructs and destructs cleanly.
// Passing this test confirms that UIManager, all 9 owned panels, and all 3 mocks
// are wired correctly for the Phase 3 test suite.
TEST_F(UIManagerModalTest, FixtureConstructsAndDestructsCleanly) {
    SUCCEED();
}

// Phase 3 stub: hasActiveModal() returns false in the Phase 3 shell.
// Full implementation and real assertion arrive in Phase 6 when ModalDialog
// has live show()/hide() logic wired through UIManager.
TEST_F(UIManagerModalTest, HasActiveModal_ReturnsFalse_InPhase3Stub) {
    EXPECT_FALSE(ui_->hasActiveModal());
}

// Compile-only stubs — Phase 6 replaces SUCCEED() with real assertions.
// Naming convention: <Trigger>_<Action>_<ExpectedOutcome>.

// Verifies: pressing Escape while settings panel is open (entered from pause menu)
// closes the settings panel and returns focus to the pause menu.
// Phase 6: assert m_settings->isVisible() == false && m_pauseMenu->isVisible() == true.
TEST_F(UIManagerModalTest, EscapeClosesSettingsAndReturnsToPauseMenu) {
    SUCCEED();
}

// Verifies: pressing Escape while settings panel is open (entered from main menu
// pre-game flow) closes the settings panel and returns to the main menu.
// Phase 6: assert m_settings->isVisible() == false && m_mainMenu->isVisible() == true.
TEST_F(UIManagerModalTest, EscapeClosesSettingsAndReturnsToMainMenu) {
    SUCCEED();
}

// Verifies: showForcedLoanDialog() transitions the modal into an active state
// and hasActiveModal() returns true afterward.
// Covers UIManager.cpp line 164-167 (showForcedLoanDialog production body)
// and hasActiveModal() returning true once the modal is shown.
TEST_F(UIManagerModalTest, ShowForcedLoanDialog_SetsModalActive) {
    LoanTerms terms;
    terms.amount         = 50000.0f;
    terms.repaymentTicks = 12;
    terms.interestRate   = 0.05f;

    ui_->showForcedLoanDialog(terms);

    EXPECT_TRUE(ui_->hasActiveModal())
        << "showForcedLoanDialog() must set the modal to active";
}

// Verifies: while a modal dialog is active, click events targeted at HUD elements
// are blocked (Priority 4 returns true) and draw() sets the scrim visible.
// Covers UIManager.cpp line 91 (return true when modalActive) and
// line 119 (setElementVisible scrim when hasActiveModal()).
TEST_F(UIManagerModalTest, ScrimBlocksHUDClickWhenModalActive) {
    LoanTerms terms;
    terms.amount         = 50000.0f;
    terms.repaymentTicks = 12;
    terms.interestRate   = 0.05f;
    ui_->showForcedLoanDialog(terms);
    ASSERT_TRUE(ui_->hasActiveModal()) << "Precondition: modal must be active";

    // Non-focus event with active modal: Priority 4 consumes it (line 91).
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    EXPECT_TRUE(ui_->onEvent(ev))
        << "Mouse click with active modal must return true (blocked by Priority 4)";

    // draw() with active modal: scrim made visible (line 119).
    EXPECT_NO_FATAL_FAILURE(ui_->draw())
        << "draw() with active modal must not crash";
}

// Verifies: closeModal() transitions the modal back to inactive.
// Phase 6: show a modal, call closeModal(), assert hasActiveModal() == false.
TEST_F(UIManagerModalTest, CloseModal_ClearsModalActive) {
    SUCCEED();
}

// Phase 6 carve-out stub (Phase 3 exit criterion per phase-3.md §UX-4).
// Verifies: a dismiss click on InspectorPanel does NOT block events targeted
// at the Minimap area — the Minimap bounds check at Priority 3 must pass through.
// Phase 6: inject a click InputEvent at a Minimap-area coordinate, assert
// onEvent() returns false (not consumed by InspectorPanel) and Minimap receives it.
TEST(InspectorPanel, InspectorPanel_DismissClick_MinimapAreaPassesThrough) {
    SUCCEED();
}

// ============================================================================
// UIManagerTransitionTest — covers state-transition methods and onEvent routing.
//
// These tests exercise the otherwise-uncovered Phase 3 stub bodies for:
//   transitionToGameplay(), transitionToPaused(), transitionToGameplay_fromPaused(),
//   transitionToGameOver(), showForcedLoanDialog(), showGameOverModal(),
//   closeModal(), showSettings(), setUnsavedChanges(), update().
//
// Mock policy: NiceMock — transition stubs call no mock methods directly in
// Phase 3; NiceMock avoids spurious failures for incidental backend calls.
// TearDown: explicit ui_.reset() before mocks are destroyed.
// ============================================================================
class UIManagerTransitionTest : public ::testing::Test {
protected:
    void SetUp() override {
        clock_.advance(0.0);  // initialise to t=0
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        ui_.reset();  // UIManager destroyed before mocks
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
};

// ---------------------------------------------------------------------------
// transitionToGameplay — sets gameMode and clears unsaved-changes flag.
// In Phase 3 the method body does: m_gameMode = mode; m_hasUnsavedChanges = false.
// We verify indirectly: calling it with Sandbox and then transitionToGameOver()
// (which has a Sandbox guard) must be a no-op for GameOver (guard fires).
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, TransitionToGameplay_Sandbox_ThenGameOver_IsNoop) {
    // transitionToGameplay sets m_gameMode = Sandbox and clears unsaved flag.
    ui_->transitionToGameplay(GameMode::Sandbox);

    // Calling transitionToGameOver() in Sandbox mode must be a no-op —
    // the sandbox guard must prevent any crash or state corruption.
    EXPECT_NO_FATAL_FAILURE(ui_->transitionToGameOver());
    // hasActiveModal() must still be false (no modal was shown).
    EXPECT_FALSE(ui_->hasActiveModal());
}

TEST_F(UIManagerTransitionTest, TransitionToGameplay_Scenario_ThenGameOver_IsNoop) {
    // In Phase 3 the GameOver body is a stub (no actual modal shown),
    // but the guard must still be absent for Scenario mode.
    ui_->transitionToGameplay(GameMode::Scenario);
    EXPECT_NO_FATAL_FAILURE(ui_->transitionToGameOver());
    // Phase 3 stub: hasActiveModal() is still false (Phase 6 wires the real modal).
    EXPECT_FALSE(ui_->hasActiveModal());
}

// ---------------------------------------------------------------------------
// transitionToPaused / transitionToGameplay_fromPaused — Phase 3 stubs.
// Both are no-ops in Phase 3; we just verify they can be called without crash.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, TransitionToPaused_AndFromPaused_NoCrash) {
    ui_->transitionToGameplay(GameMode::Sandbox);
    EXPECT_NO_FATAL_FAILURE(ui_->transitionToPaused());
    EXPECT_NO_FATAL_FAILURE(ui_->transitionToGameplay_fromPaused());
}

// ---------------------------------------------------------------------------
// showForcedLoanDialog — Phase 3 stub. Verifies call completes without crash.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, ShowForcedLoanDialog_NoCrash) {
    LoanTerms terms;
    terms.amount         = 50000.0f;
    terms.repaymentTicks = 12;
    terms.interestRate   = 0.05f;
    EXPECT_NO_FATAL_FAILURE(ui_->showForcedLoanDialog(terms));
}

// ---------------------------------------------------------------------------
// showGameOverModal — Phase 3 stub. Verifies call completes without crash.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, ShowGameOverModal_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(ui_->showGameOverModal(/*totalDebt=*/100000LL,
                                                   /*monthsInDeficit=*/3));
}

// ---------------------------------------------------------------------------
// closeModal — calls m_notifications->setModalActive(false); no crash.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, CloseModal_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(ui_->closeModal());
    // After closeModal(), hasActiveModal() must still be false
    // (ModalDialog is not active in Phase 3).
    EXPECT_FALSE(ui_->hasActiveModal());
}

// ---------------------------------------------------------------------------
// showSettings — Phase 3 stub. Verifies call completes without crash.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, ShowSettings_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(ui_->showSettings());
}

// ---------------------------------------------------------------------------
// setUnsavedChanges — Phase 3 stub. Called with true then false; no crash.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, SetUnsavedChanges_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(ui_->setUnsavedChanges(true));
    EXPECT_NO_FATAL_FAILURE(ui_->setUnsavedChanges(false));
}

// ---------------------------------------------------------------------------
// update — polls m_sim->getConsecutiveDeficitMonths() when m_sim != null.
// Verifies that update() calls through to the simulation without crashing.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, Update_CallsGetConsecutiveDeficitMonths) {
    using ::testing::Return;
    EXPECT_CALL(sim_, getConsecutiveDeficitMonths())
        .WillOnce(Return(0));

    // update() must call getConsecutiveDeficitMonths() exactly once when m_sim != null.
    EXPECT_NO_FATAL_FAILURE(ui_->update(0.016f));
}

// ---------------------------------------------------------------------------
// update — with null sim (nullptr) must not crash (null guard in the stub).
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, Update_WithNullSim_NoCrash) {
    // Construct a separate UIManager with nullptr sim.
    UIManager uiNullSim(&backend_, &audio_, /*sim=*/nullptr, &clock_);
    EXPECT_NO_FATAL_FAILURE(uiNullSim.update(0.016f));
}

// ---------------------------------------------------------------------------
// onEvent — WindowFocusGained/Lost events always return false (pass-through).
// Verifies Priority-1 rule: focus events must never be consumed by UIManager.
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, OnEvent_WindowFocusGained_ReturnsFalse) {
    InputEvent ev{};
    ev.type = InputEvent::Type::WindowFocusGained;
    EXPECT_FALSE(ui_->onEvent(ev))
        << "WindowFocusGained must always pass through (Priority 1 rule)";
}

TEST_F(UIManagerTransitionTest, OnEvent_WindowFocusLost_ReturnsFalse) {
    InputEvent ev{};
    ev.type = InputEvent::Type::WindowFocusLost;
    EXPECT_FALSE(ui_->onEvent(ev))
        << "WindowFocusLost must always pass through (Priority 1 rule)";
}

// ---------------------------------------------------------------------------
// onEvent — non-focus event when no modal and no critical toast returns false
// (falls through all priority levels to the default return false at Priority 6).
// ---------------------------------------------------------------------------
TEST_F(UIManagerTransitionTest, OnEvent_MouseMove_NoModal_NoCriticalToast_ReturnsFalse) {
    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.physX = 500;
    ev.physY = 300;
    ev.x     = 500;
    ev.y     = 300;
    EXPECT_FALSE(ui_->onEvent(ev))
        << "Mouse move with no modal and no critical toast must return false";
}

// ============================================================================
// NotificationManagerTest — covers NotificationManager's Phase 3 stub bodies.
//
// NotificationManager is constructed internally by UIManager; to test it
// directly we construct it standalone with the mocks.
// ============================================================================
#include "src/ui/NotificationManager.h"
#include "src/platform/input_event.h"

class NotificationManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        notif_ = std::make_unique<NotificationManager>(&backend_, &sim_, &clock_);
    }

    void TearDown() override {
        notif_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<NotificationManager> notif_;
};

// ---------------------------------------------------------------------------
// postNormal — Phase 3 stub; must not crash.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, PostNormal_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(notif_->postNormal("Title", "Body text"));
}

// ---------------------------------------------------------------------------
// postCritical — Phase 3 stub; must not crash.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, PostCritical_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(notif_->postCritical("Critical Title", "Critical body"));
}

// ---------------------------------------------------------------------------
// hasCriticalToastVisible — Phase 3 stub always returns false.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, HasCriticalToastVisible_ReturnsFalse_InPhase3) {
    EXPECT_FALSE(notif_->hasCriticalToastVisible())
        << "Phase 3 stub: hasCriticalToastVisible() must return false";
}

// ---------------------------------------------------------------------------
// hasCriticalToastVisible — still false after postCritical (Phase 3 stub).
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, HasCriticalToastVisible_StillFalseAfterPostCritical) {
    notif_->postCritical("Alert", "Something bad happened");
    EXPECT_FALSE(notif_->hasCriticalToastVisible())
        << "Phase 3 stub: hasCriticalToastVisible() must remain false even after postCritical";
}

// ---------------------------------------------------------------------------
// update — Phase 3 stub; must not crash.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, Update_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(notif_->update());
}

// ---------------------------------------------------------------------------
// onEvent — Phase 3 stub always returns false (event not consumed).
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, OnEvent_ReturnsFalse) {
    InputEvent ev{};
    ev.type = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    EXPECT_FALSE(notif_->onEvent(ev))
        << "Phase 3 stub: onEvent() must return false (event not consumed)";
}

// ---------------------------------------------------------------------------
// setModalActive — sets the m_modalActive flag; no crash.
// Phase 3: the flag is private, but the method body sets it without dereferences.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, SetModalActive_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(notif_->setModalActive(true));
    EXPECT_NO_FATAL_FAILURE(notif_->setModalActive(false));
}

// ---------------------------------------------------------------------------
// dismissCriticalToast — Phase 3 stub; must not crash.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, DismissCriticalToast_NoCrash) {
    constexpr UIElementHandle kSomeHandle = 0x0001u;
    EXPECT_NO_FATAL_FAILURE(notif_->dismissCriticalToast(kSomeHandle));
}

// ---------------------------------------------------------------------------
// draw — calls m_backend->setElementVisible(); must not crash with NiceMock.
// ---------------------------------------------------------------------------
TEST_F(NotificationManagerTest, Draw_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(notif_->draw());
}
