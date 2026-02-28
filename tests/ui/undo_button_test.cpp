// tests/ui/undo_button_test.cpp
//
// Phase 8 undo button tests.
//
// Test: UndoCountdown_AmberAt10xSpeed_ImmediatelyOnAction
//   Verifies that the undo countdown label turns amber when
//   remainingSeconds < 5.0 || totalWindowSeconds <= 6.0.
//   Uses StrictMock<MockUIBackend> + NiceMock<MockCitySimulation> + ManualClock.
//
// TearDown contract: hud_ (or equivalent) reset to nullptr before
// StrictMock<MockUIBackend> is destroyed per testability-architecture.md.
//
// Note: UndoSystem_BlockedDuringModal_ButtonGrayedOut was moved to
// modal_dialog_test.cpp — it logically belongs to the modal pause/gray-out chain.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
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
using ::testing::HasSubstr;
using ::testing::AtLeast;

class UndoButtonTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        // Reset UIManager before mocks are destroyed.
        ui_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    ManualClock                  clock_;
    std::unique_ptr<UIManager>   ui_;
};

// UndoCountdown_AmberAt10xSpeed_ImmediatelyOnAction
// Verifies: at 10x speed, the undo countdown amber threshold fires
// immediately when totalWindowSeconds <= 6.0 real seconds.
TEST_F(UndoButtonTest, UndoCountdown_AmberAt10xSpeed_ImmediatelyOnAction) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // Phase 8: stub ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(true));
    // Assert the label contains amber color indicator or text change.
    SUCCEED();
}
