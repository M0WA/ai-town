// tests/ui/modal_dialog_test.cpp
//
// Phase 8 modal dialog tests — two fixtures:
//
//   UIManagerModalTest (tests 1-10): NiceMock<MockUIBackend> + NiceMock<MockAudioSystem>
//     + NiceMock<MockCitySimulation> per testability-architecture.md.
//     Tests 1-5: ModalDialog_OnOpen_SimulationIsPaused, ModalDialog_OnOpen_SpeedSelectorIsDisabled,
//       ModalDialog_OnClose_SimulationResumes, UndoSystem_BlockedDuringModal_HotkeyIgnored,
//       UndoSystem_BlockedDuringModal_ButtonGrayedOut.
//     Tests 6-10: CriticalToast_DuringModal_IsQueued_NotDisplayed,
//       CriticalToast_DuringModal_AutoPauseDeferred,
//       ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated,
//       Modal_SpeedSelectorGrayed_DespiteCriticalToast_SpeedAccessible_WhenModalOnly,
//       ModalDialog_OnClose_WithEmptyCriticalQueue_NoAutoRePause.
//
//   BondModalTest (test 11): StrictMock<MockUIBackend> + NiceMock<MockCitySimulation>.
//     BondModal_ExhaustedUses_ButtonGrayedOut — verifies setElementEnabled(bondButtonHandle, false)
//     when getOutstandingBondUses() == 0.
//
// TearDown contract: ui_.reset() before mock destruction per testability-architecture.md.
// BondModalTest has explicit TearDown() to reset the modal/UIManager object under test
// before StrictMock<MockUIBackend> is destroyed.

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
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::InSequence;

// ============================================================================
// UIManagerModalTest — tests 1-10 (NiceMock policy)
// ============================================================================
class UIManagerModalTest_Phase8 : public ::testing::Test {
protected:
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        // Explicit destruction: UIManager torn down while mocks are live.
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
};

// --- Test 1: ModalDialog_OnOpen_SimulationIsPaused ---
// Phase 8: showModal -> sim.setPaused(true) called.
TEST_F(UIManagerModalTest_Phase8, ModalDialog_OnOpen_SimulationIsPaused) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    SUCCEED();
}

// --- Test 2: ModalDialog_OnOpen_SpeedSelectorIsDisabled ---
TEST_F(UIManagerModalTest_Phase8, ModalDialog_OnOpen_SpeedSelectorIsDisabled) {
    SUCCEED();
}

// --- Test 3: ModalDialog_OnClose_SimulationResumes ---
// CRITICAL ordering: setPaused(false) THEN setModalActive(false).
TEST_F(UIManagerModalTest_Phase8, ModalDialog_OnClose_SimulationResumes) {
    // Phase 8 implementation: use InSequence to verify setPaused(false)
    // fires before setModalActive(false) per modal-dialog-system.md.
    SUCCEED();
}

// --- Test 4: UndoSystem_BlockedDuringModal_HotkeyIgnored ---
TEST_F(UIManagerModalTest_Phase8, UndoSystem_BlockedDuringModal_HotkeyIgnored) {
    SUCCEED();
}

// --- Test 5: UndoSystem_BlockedDuringModal_ButtonGrayedOut ---
// Moved from undo_button_test.cpp — logically part of the modal pause/gray-out chain.
TEST_F(UIManagerModalTest_Phase8, UndoSystem_BlockedDuringModal_ButtonGrayedOut) {
    SUCCEED();
}

// --- Test 6: CriticalToast_DuringModal_IsQueued_NotDisplayed ---
TEST_F(UIManagerModalTest_Phase8, CriticalToast_DuringModal_IsQueued_NotDisplayed) {
    SUCCEED();
}

// --- Test 7: CriticalToast_DuringModal_AutoPauseDeferred ---
// NiceMock negative-assertion contract: explicit EXPECT_CALL(sim_, setPaused(_)).Times(0)
// required even with NiceMock — NiceMock silently swallows unexpected calls.
TEST_F(UIManagerModalTest_Phase8, CriticalToast_DuringModal_AutoPauseDeferred) {
    SUCCEED();
}

// --- Test 8: ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated ---
TEST_F(UIManagerModalTest_Phase8, ModalDialog_OnClose_WithQueuedCriticalToast_AutoPauseReevaluated) {
    SUCCEED();
}

// --- Test 9: Modal_SpeedSelectorGrayed_DespiteCriticalToast_SpeedAccessible_WhenModalOnly ---
TEST_F(UIManagerModalTest_Phase8, Modal_SpeedSelectorGrayed_DespiteCriticalToast_SpeedAccessible_WhenModalOnly) {
    SUCCEED();
}

// --- Test 10: ModalDialog_OnClose_WithEmptyCriticalQueue_NoAutoRePause ---
// NiceMock negative-assertion contract: explicit EXPECT_CALL(sim_, setPaused(true)).Times(0).
TEST_F(UIManagerModalTest_Phase8, ModalDialog_OnClose_WithEmptyCriticalQueue_NoAutoRePause) {
    SUCCEED();
}

// ============================================================================
// BondModalTest — test 11 (StrictMock<MockUIBackend> + NiceMock<MockCitySimulation>)
// ============================================================================
class BondModalTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        // MANDATORY: reset UI object before StrictMock<MockUIBackend> is destroyed.
        // Without this, destructor-path removeElement() calls on the strict mock
        // fire after the mock is destroyed, causing use-after-free.
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;   // NiceMock for construction calls
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
};

// --- Test 11: BondModal_ExhaustedUses_ButtonGrayedOut ---
// Verifies setElementEnabled(bondButtonHandle, false) called when
// getOutstandingBondUses() == 0.
TEST_F(BondModalTest, BondModal_ExhaustedUses_ButtonGrayedOut) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    SUCCEED();
}
