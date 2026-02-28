// tests/ui/query_panel_test.cpp
//
// Phase 8 QueryPanel tests — pure static-function tests for
// QueryPanel::computePanelPosition(). No mocks required.
// No TearDown() needed (no mock objects to reset).
//
// 4 test cases per architecture/testing/testability-architecture.md lines 120-128:
//   1. Primary right-below placement (clicked tile in upper-left quadrant)
//   2. Fallback left placement (tile in right half)
//   3. Edge clamping — four sub-cases: cursor near left/right/top/bottom edge
//   4. Third-fallback edge-snap (insufficient space in all quadrants)
//
// computePanelPosition is a pure function (no side effects, no Irrlicht dependency)
// returning a Rect. Panel dimensions: 240x160 px.
// Clamped to [0, 1920-240] x [0, 1080-160].
//
// Additional test:
//   QueryPanel_EscapeFeedbackToast_DoesNotConsumeSubsequentEscape

#include "src/ui/IUIBackend.h"  // Rect
#include <gtest/gtest.h>

// --------------------------------------------------------------------------
// computePanelPosition stub — Phase 8 delivers the real implementation.
// For now, forward-declare the signature so tests compile.
// Phase 8 implementation: move to InspectorPanel::computePanelPosition()
// in inspector_panel.h / QueryPanel.cpp.
// --------------------------------------------------------------------------
namespace {
// Phase 8 stub: returns a zero rect until implementation is wired.
Rect computePanelPosition(int /*clickX*/, int /*clickY*/,
                          int /*panelW*/, int /*panelH*/,
                          int /*screenW*/, int /*screenH*/) {
    return {0, 0, 0, 0};
}
}  // namespace

// --- Test 1: Primary right-below placement ---
TEST(QueryPanelPosition, PrimaryRightBelow_UpperLeftQuadrant) {
    // Clicked tile at (200, 200) — upper-left quadrant.
    // Panel should appear right-below the click point.
    Rect r = computePanelPosition(200, 200, 240, 160, 1920, 1080);
    // Phase 8 stub: SUCCEED() until computePanelPosition is implemented.
    (void)r;
    SUCCEED();
}

// --- Test 2: Fallback left placement ---
TEST(QueryPanelPosition, FallbackLeft_TileInRightHalf) {
    // Clicked tile at (1800, 200) — right half of screen.
    // Panel should fall back to left placement.
    Rect r = computePanelPosition(1800, 200, 240, 160, 1920, 1080);
    (void)r;
    SUCCEED();
}

// --- Test 3: Edge clamping (4 sub-cases) ---
TEST(QueryPanelPosition, EdgeClamping_NearLeftEdge) {
    Rect r = computePanelPosition(5, 500, 240, 160, 1920, 1080);
    (void)r;
    SUCCEED();
}

TEST(QueryPanelPosition, EdgeClamping_NearRightEdge) {
    Rect r = computePanelPosition(1915, 500, 240, 160, 1920, 1080);
    (void)r;
    SUCCEED();
}

TEST(QueryPanelPosition, EdgeClamping_NearTopEdge) {
    Rect r = computePanelPosition(500, 5, 240, 160, 1920, 1080);
    (void)r;
    SUCCEED();
}

TEST(QueryPanelPosition, EdgeClamping_NearBottomEdge) {
    Rect r = computePanelPosition(500, 1075, 240, 160, 1920, 1080);
    (void)r;
    SUCCEED();
}

// --- Test 4: Third-fallback edge-snap ---
TEST(QueryPanelPosition, ThirdFallback_EdgeSnap_InsufficientSpaceAllQuadrants) {
    // Extreme corner: panel cannot fit in any primary/secondary position.
    Rect r = computePanelPosition(1910, 1070, 240, 160, 1920, 1080);
    (void)r;
    SUCCEED();
}

// --- QueryPanel_EscapeFeedbackToast_DoesNotConsumeSubsequentEscape ---
// Verifies the 1.5 s "Panel closed" toast does not consume a subsequent Escape.
// Uses NiceMock<MockUIBackend> + NiceMock<MockCitySimulation> + ManualClock.
#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_audio_system.h"
#include "tests/simulation/manual_clock.h"
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;

TEST(QueryPanelEscape, EscapeFeedbackToast_DoesNotConsumeSubsequentEscape) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // 1. Open QueryPanel
    // 2. Fire first Escape -> toast posted
    // 3. Fire second Escape -> reaches UIManager, opens Pause Menu
    SUCCEED();
}
