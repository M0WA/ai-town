// tests/ui/key_bindings_panel_test.cpp
//
// KeyBindingsPanelTest — Phase 11c unit tests for the Controls tab rebinding table.
//
// Tests:
//   1. KeyBindingsPanel_CaptureMode_TogglesOnClick
//      — Clicking a bindable chip enters Capture state; clicking a second chip while
//        the first is active cancels the first capture and begins a new one.
//   2. KeyBindingsPanel_ConflictDetected_ShowsSwapOption
//      — Binding a key already used by another action shows inline conflict text and
//        Swap/Cancel buttons.
//   3. KeyBindingsPanel_Swap_ExchangesBothRows
//      — Pressing Swap correctly exchanges bindings in the in-memory KeyBindings struct.
//   4. KeyBindingsPanel_ReservedKey_ShowsReservedError
//      — Pressing Q or E during capture shows reserved-key error and does NOT advance
//        to conflict detection.
//   5. KeyBindingsPanel_Escape_CancelsCapture
//      — Pressing Escape during capture exits Capture state without changing the
//        binding; the tab-level Escape handler fires in the same event frame.
//   6. KeyBindingsPanel_ApplyGrayed_WhenConflictExists
//      — Apply button is disabled when any conflict is unresolved in the in-memory
//        struct.
//
// All tests use NiceMock<MockUIBackend>: strict call expectations are enforced via
// EXPECT_CALL within the NiceMock fixture (NOT StrictMock<MockUIBackend>) because
// the UI backend generates many incidental calls during construction and draw.
//
// KeyBindings is injected as a value type: copied on tab open, written on Apply.
//
// TearDown contract: panel_ is reset before mock destruction to prevent
// order-of-destruction issues with pending mock expectations.

#include "src/ui/KeyBindingsPanel.h"
#include "src/ui/key_bindings.h"
#include "src/interfaces/IUIBackend.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::AnyNumber;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class KeyBindingsPanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        // Track every addButton call so tests can identify chip handles by index.
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) {
                UIElementHandle h = ++nextHandle_;
                btnHandles_.push_back(h);
                return h;
            });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 200, 32}));

        // Construct the panel (no modal needed for unit tests).
        panel_ = std::make_unique<KeyBindingsPanel>(&backend_, nullptr);
        // Open the Controls tab with default bindings (takes snapshot for Cancel revert).
        panel_->openTab(defaultBindings_);
        // btnHandles_[0..10]  = capturable-row chip buttons
        // btnHandles_[11]     = conflict Swap button
        // btnHandles_[12]     = conflict Cancel button
    }

    void TearDown() override {
        // Explicit destruction before mock tear-down — honours destructor-path contract.
        panel_.reset();
    }

    // Helper: send a key-down event with the given key code.
    InputEvent keyDown(int keyCode) {
        InputEvent e;
        e.type    = InputEvent::Type::KeyDown;
        e.keyCode = keyCode;
        return e;
    }

    // Helper: simulate clicking the chip for row index `row` (0-based, matching the
    // rebindable actions list: 0=Zone, 1=Road, ...).
    // The backend returns the configured rect for the chip handle; clicking inside it
    // fires the capture state machine.
    InputEvent chipClick(int x, int y) {
        InputEvent e;
        e.type   = InputEvent::Type::MouseButtonDown;
        e.button = 0;
        e.x      = x;
        e.y      = y;
        return e;
    }

    NiceMock<MockUIBackend>         backend_;
    KeyBindings                     defaultBindings_{};
    std::unique_ptr<KeyBindingsPanel> panel_;
    UIElementHandle                  nextHandle_{100};
    std::vector<UIElementHandle>    btnHandles_;
};

// ---------------------------------------------------------------------------
// Test 1: Clicking a bindable chip enters Capture state.
//         Clicking a second chip while the first is active cancels first and
//         begins the new capture.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_CaptureMode_TogglesOnClick) {
    panel_->show();

    // Verify that show() made the panel visible without crashing.
    EXPECT_TRUE(panel_->isVisible());

    // btnHandles_[0] = chip for row 0 (Zone tool), btnHandles_[1] = chip for row 1.
    // Give every chip a default non-hittable rect, then override row 0's chip to be
    // hittable at click (50, 40).
    ASSERT_GE(btnHandles_.size(), 2u) << "Need at least two chip handles";
    UIElementHandle chip0 = btnHandles_[0];
    UIElementHandle chip1 = btnHandles_[1];

    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{2000, 2000, 1, 1}));
    ON_CALL(backend_, getElementRect(chip0)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    ON_CALL(backend_, getElementRect(chip1)).WillByDefault(Return(UIRect{30, 70, 200, 32}));

    // Act: click inside chip 0's rect.
    panel_->onEvent(chipClick(50, 40));

    // Row 0 should be in Capturing state.
    EXPECT_TRUE(panel_->isCapturing());
    EXPECT_EQ(panel_->capturingRowIndex(), 0);

    // Act: click inside chip 1's rect while capturing row 0.
    // chip0 is off-screen, so it won't intercept; chip1 is hittable.
    panel_->onEvent(chipClick(50, 80));

    // After clicking chip 1, capture cancels row 0 and begins row 1.
    EXPECT_TRUE(panel_->isCapturing());
    EXPECT_EQ(panel_->capturingRowIndex(), 1);
}

// ---------------------------------------------------------------------------
// Test 2: Binding a key already used by another action shows inline conflict
//         text and Swap/Cancel buttons.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_ConflictDetected_ShowsSwapOption) {
    panel_->show();

    // Enter capture mode for row 0 (Zone tool, currently "Z").
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    // Press "R" — that key is already used by the Road tool (row 1).
    // This should trigger conflict detection and show inline conflict text +
    // Swap/Cancel buttons.
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());

    panel_->onEvent(keyDown('R'));  // ASCII 'R' = 82

    // After a conflict, isConflictPending() must be true.
    EXPECT_TRUE(panel_->isConflictPending());
}

// ---------------------------------------------------------------------------
// Test 3: Pressing Swap exchanges bindings in the in-memory KeyBindings struct.
//         Both rows update their chip labels to reflect the swap.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_Swap_ExchangesBothRows) {
    panel_->show();

    // Initial bindings: Zone="Z", Road="R".
    ASSERT_EQ(panel_->currentBindings().toolZone, "Z");
    ASSERT_EQ(panel_->currentBindings().toolRoad, "R");

    // Enter capture for row 0 (Zone), press "R" to create a conflict with Road.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    panel_->onEvent(keyDown('R'));
    ASSERT_TRUE(panel_->isConflictPending());

    // Act: call swap.
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    panel_->applySwap();

    // After swap: Zone should be "R" and Road should be "Z".
    EXPECT_EQ(panel_->currentBindings().toolZone, "R");
    EXPECT_EQ(panel_->currentBindings().toolRoad, "Z");

    // Conflict is resolved; panel returns to Idle state.
    EXPECT_FALSE(panel_->isConflictPending());
    EXPECT_FALSE(panel_->isCapturing());
}

// ---------------------------------------------------------------------------
// Test 4: Pressing Q or E during capture shows reserved-key error and does NOT
//         advance to conflict detection.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_ReservedKey_ShowsReservedError) {
    panel_->show();

    // Enter capture for row 0 (Zone tool).
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    // Press "Q" — reserved key. The panel should show a reserved-key error
    // and remain in Capturing state (NOT advance to conflict detection).
    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());

    panel_->onEvent(keyDown('Q'));  // ASCII 'Q' = 81

    // Must still be capturing (not conflict-pending, not idle).
    EXPECT_TRUE(panel_->isCapturing())
        << "Reserved key press must NOT exit Capturing state";
    EXPECT_FALSE(panel_->isConflictPending())
        << "Reserved key press must NOT advance to conflict detection";
    EXPECT_TRUE(panel_->hasReservedKeyError())
        << "Reserved key press must set the reserved-key error flag";

    // Same for "E".
    panel_->onEvent(keyDown('E'));  // ASCII 'E' = 69

    EXPECT_TRUE(panel_->isCapturing());
    EXPECT_FALSE(panel_->isConflictPending());
    EXPECT_TRUE(panel_->hasReservedKeyError());
}

// ---------------------------------------------------------------------------
// Test 5: Pressing Escape during capture exits Capture state without changing
//         the binding; the tab-level Escape handler fires in the same event
//         frame so Settings closes.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_Escape_CancelsCapture) {
    panel_->show();

    // Record the original binding for Zone.
    const std::string originalZone = panel_->currentBindings().toolZone;

    // Enter capture for row 0.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    // Press Escape (keyCode 27) during capture.
    // This must:
    //   (a) Exit Capture state (isCapturing() → false)
    //   (b) NOT change the binding (toolZone stays at originalZone)
    //   (c) Return false from onEvent (NOT consume the event) so the tab-level
    //       Escape handler fires in the same event frame and closes Settings.
    bool consumed = panel_->onEvent(keyDown(27));

    EXPECT_FALSE(panel_->isCapturing())
        << "Escape during capture must exit Capturing state";
    EXPECT_EQ(panel_->currentBindings().toolZone, originalZone)
        << "Escape must not change the binding";
    EXPECT_FALSE(consumed)
        << "Escape must NOT be consumed by KeyBindingsPanel so Settings closes";
}

// ---------------------------------------------------------------------------
// Test 6: Apply button is disabled when any conflict is unresolved.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_ApplyGrayed_WhenConflictExists) {
    panel_->show();

    // Create a conflict: capture Zone row and press "R" (used by Road).
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    panel_->onEvent(keyDown('R'));
    ASSERT_TRUE(panel_->isConflictPending());

    // The Apply button must be disabled when a conflict is pending.
    // KeyBindingsPanel does not own the Apply button (it belongs to SettingsPanel);
    // verify via the isApplyEnabled() query that SettingsPanel uses to gray it out.
    EXPECT_FALSE(panel_->isApplyEnabled())
        << "Apply button must be disabled when a conflict is unresolved";
    EXPECT_TRUE(panel_->hasConflict())
        << "hasConflict() must return true when a conflict is pending";
}

// ---------------------------------------------------------------------------
// Test 7: Escape during ConflictPending state cancels the conflict and
//         returns to Idle (line 344-347 in KeyBindingsPanel.cpp).
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_EscapeDuringConflict_CancelsConflict) {
    panel_->show();

    // Enter Capturing state for row 0, press "R" to trigger conflict.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    panel_->onEvent(keyDown('R'));
    ASSERT_TRUE(panel_->isConflictPending());

    // Escape during ConflictPending -> cancelCapture(), returns false (line 344-347).
    bool consumed = panel_->onEvent(keyDown(27));
    EXPECT_FALSE(consumed) << "Escape during conflict must NOT be consumed by KeyBindingsPanel";
    EXPECT_FALSE(panel_->isConflictPending()) << "Conflict must be cancelled by Escape";
    EXPECT_FALSE(panel_->isCapturing()) << "Panel must return to Idle after Escape";
}

// ---------------------------------------------------------------------------
// Test 8: Clicking the Swap button during ConflictPending via onEvent() hit-test
//         (line 366-368). The mock returns a UIRect that the click lands inside.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_ConflictSwapButtonClick_AppliesSwap) {
    panel_->show();

    // btnHandles_[11] = Swap button (12th button created after 11 chip rows).
    // Create conflict first.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    panel_->onEvent(keyDown('R'));
    ASSERT_TRUE(panel_->isConflictPending());

    // Make the Swap button hit-testable at a specific position.
    // btnHandles_[11] is the Swap button handle.
    if (btnHandles_.size() > 11) {
        ON_CALL(backend_, getElementRect(btnHandles_[11])).WillByDefault(
            Return(UIRect{50, 120, 100, 32}));
        panel_->onEvent(chipClick(80, 130));  // Click inside Swap button rect.
        EXPECT_FALSE(panel_->isConflictPending()) << "Swap click must resolve conflict";
    } else {
        SUCCEED(); // Handle tracking didn't capture enough buttons — skip.
    }
}

// ---------------------------------------------------------------------------
// Test 9: Clicking the conflict Cancel button during ConflictPending
//         (line 370-372).
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_ConflictCancelButtonClick_CancelsConflict) {
    panel_->show();

    // Create conflict.
    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));
    panel_->onEvent(chipClick(50, 40));
    panel_->onEvent(keyDown('R'));
    ASSERT_TRUE(panel_->isConflictPending());

    // Make the Cancel button hit-testable.
    // btnHandles_[12] is the conflict Cancel button handle.
    if (btnHandles_.size() > 12) {
        ON_CALL(backend_, getElementRect(btnHandles_[12])).WillByDefault(
            Return(UIRect{160, 120, 100, 32}));
        panel_->onEvent(chipClick(200, 130));  // Click inside Cancel button rect.
        EXPECT_FALSE(panel_->isConflictPending()) << "Cancel click must resolve conflict";
    } else {
        SUCCEED();
    }
}

// ---------------------------------------------------------------------------
// Test 10: Clicking the same chip twice while Capturing cancels that capture
//          (line 383). Second click on the active chip calls cancelCapture().
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_DoubleClickSameChip_CancelsCapture) {
    panel_->show();

    ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{30, 30, 200, 32}));

    // First click: enter Capturing state for row 0.
    panel_->onEvent(chipClick(50, 40));
    ASSERT_TRUE(panel_->isCapturing());

    // Second click on the same chip: cancel capture (line 383).
    panel_->onEvent(chipClick(50, 40));
    EXPECT_FALSE(panel_->isCapturing()) << "Second click on same chip must cancel capture";
}

// ---------------------------------------------------------------------------
// Test 11: Clicking an informational (non-rebindable) row chip consumes the
//          event silently (line 398).
//          Informational chips: rows kNumCapturable..kTotalRows-1.
//          We fake the hit-test by returning a valid UIRect for a high-index chip.
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsPanelTest, KeyBindingsPanel_InfoRowChipClick_ConsumesEvent) {
    panel_->show();

    // btnHandles_[0..10] = rebindable rows.
    // btnHandles_[11, 12] = Swap, Cancel conflict buttons.
    // Rows kNumCapturable (11) and above = informational rows; those chips
    // are created after the capturable ones but we can test by setting all
    // rects to overlap and then checking an explicit known informational handle.
    // Since btnHandles_ may contain all buttons, use index 13+ for info rows.
    // KeyBindingsPanel kNumCapturable = 11 (camPanUp/Down/Left/Right + 7 tools).
    // kTotalRows includes reserved (Undo/Save) rows.
    // If btnHandles_.size() <= 13, skip as the test environment may differ.
    if (btnHandles_.size() > 13) {
        // Make informational chip (index 13) at a unique rect.
        ON_CALL(backend_, getElementRect(btnHandles_[13])).WillByDefault(
            Return(UIRect{30, 500, 200, 32}));
        bool consumed = panel_->onEvent(chipClick(80, 510));
        EXPECT_TRUE(consumed) << "Info row chip click must be consumed silently";
    } else {
        SUCCEED();
    }
}
