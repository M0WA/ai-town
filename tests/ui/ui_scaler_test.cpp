// ui_scaler_test.cpp — Phase 1 UIScaler unit tests.
// All 6 named test cases registered under the ui_tests CMake target (label "unit").
// All 6 tests must PASS (not stubs) per phase-1.md §UIScaler Unit Tests.
//
// UIScaler translates between virtual 1920x1080 coordinate space and physical
// viewport coordinates (with letterbox/pillarbox offset support).
//
// Constructor signature (locked at Phase 0):
//   UIScaler(int virtualW, int virtualH, int viewportW, int viewportH,
//            int offsetX, int offsetY)
//
// unproject(physX, physY) returns UIScaler::VirtualPoint{int x, int y} in
// virtual space, clamped to [0, virtualW-1] x [0, virtualH-1].
// The maximum valid x is 1919 (not 1920); maximum valid y is 1079 (not 1080).
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler testability
//           architecture/ui-ux/resolution-ui-scaling.md §UIScaler Constructor
//           phase-1.md §UIScaler Unit Tests
//
// Include path: project-root-relative form required (${CMAKE_SOURCE_DIR} in
// ui_tests target_include_directories).
#include "src/ui/UIScaler.h"
#include <gtest/gtest.h>

// ===========================================================================
// Test 1: UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler tests
//           named test case 1.
//
// Scenario: 1920x1080 virtual space, 1280x720 physical viewport, 90px top
// letterbox offset (the physical window is taller than the viewport).
//
// Formula: virtual_x = (physX - offsetX) * virtualW / viewportW
//          virtual_y = (physY - offsetY) * virtualH / viewportH
//
// For physX=640, physY=450 (with offsetY=90):
//   virtual_x = (640 - 0) * 1920 / 1280 = 640 * 1.5 = 960
//   virtual_y = (450 - 90) * 1080 / 720 = 360 * 1.5 = 540
// Expected: VirtualPoint{960, 540} — the center of virtual space.
// ===========================================================================
TEST(UIScalerTest, UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly)
{
    // 1920x1080 virtual, 1280x720 viewport, 0px left offset, 90px top letterbox.
    UIScaler scaler(1920, 1080, 1280, 720, 0, 90);

    // Physical center of the viewport (relative to window origin):
    //   center_x_phys = 0 + 1280/2 = 640
    //   center_y_phys = 90 + 720/2 = 90 + 360 = 450
    UIScaler::VirtualPoint vp = scaler.unproject(640, 450);

    EXPECT_EQ(vp.x, 960) << "Letterbox center X must unproject to virtual 960";
    EXPECT_EQ(vp.y, 540) << "Letterbox center Y must unproject to virtual 540";
}

// ===========================================================================
// Test 2: UIScaler_FullNative_NoOffset_ProjectsIdentity
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler tests
//           named test case 2.
//
// Scenario: 1920x1080 virtual, 1920x1080 physical viewport, zero offset.
// The transform is identity — physical == virtual for coordinates in range.
//
// Verifications:
//   (a) unproject(960, 540) -> VirtualPoint{960, 540}  (center)
//   (b) unproject(0, 0)     -> VirtualPoint{0, 0}      (top-left corner)
// ===========================================================================
TEST(UIScalerTest, UIScaler_FullNative_NoOffset_ProjectsIdentity)
{
    // Full native: 1920x1080 virtual = 1920x1080 viewport, no offset.
    UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);

    // (a) Center maps to center.
    {
        UIScaler::VirtualPoint vp = scaler.unproject(960, 540);
        EXPECT_EQ(vp.x, 960) << "Native-res center X must be identity";
        EXPECT_EQ(vp.y, 540) << "Native-res center Y must be identity";
    }

    // (b) Top-left corner maps to (0, 0).
    {
        UIScaler::VirtualPoint vp = scaler.unproject(0, 0);
        EXPECT_EQ(vp.x, 0) << "Native-res top-left X must be 0";
        EXPECT_EQ(vp.y, 0) << "Native-res top-left Y must be 0";
    }
}

// ===========================================================================
// Test 3: UIScaler_PillarboxOffset_UnprojectsCenterCorrectly
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler tests
//           named test case 3.
//
// Scenario: 1920x1080 virtual, 1440x1080 physical viewport with 240px left
// pillarbox offset (e.g. a 1920x1080 window with a 1440-wide viewport centered).
//
// Formula for center:
//   physX_center = 240 + 1440/2 = 240 + 720 = 960
//   physY_center = 0   + 1080/2 = 540
//
//   virtual_x = (960 - 240) * 1920 / 1440 = 720 * 1.333... = 960
//   virtual_y = (540 - 0)   * 1080 / 1080 = 540
// Expected: VirtualPoint{960, 540}.
// ===========================================================================
TEST(UIScalerTest, UIScaler_PillarboxOffset_UnprojectsCenterCorrectly)
{
    // 1920x1080 virtual, 1440x1080 viewport, 240px left pillarbox offset.
    UIScaler scaler(1920, 1080, 1440, 1080, 240, 0);

    // Physical center of the active viewport (offset included):
    //   center_x_phys = 240 + 720 = 960
    //   center_y_phys = 0   + 540 = 540
    UIScaler::VirtualPoint vp = scaler.unproject(960, 540);

    EXPECT_EQ(vp.x, 960) << "Pillarbox center X must unproject to virtual 960";
    EXPECT_EQ(vp.y, 540) << "Pillarbox center Y must unproject to virtual 540";
}

// ===========================================================================
// Test 4: UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler tests
//           named test case 4.
//           architecture/ui-ux/resolution-ui-scaling.md §UIScaler Constructor
//           (OUTPUT CLAMPING paragraph).
//
// Scenario: 1920x1080 virtual, 1280x720 viewport, 90px top letterbox.
// A physical Y of 80 is inside the top black bar (offsetY=90).
//
// Pre-clamp virtual_y = (80 - 90) * 1080 / 720 = -10 * 1.5 = -15
// After clamp: virtual_y = 0  (clamped to [0, virtualH-1]).
//
// unproject() NEVER returns negative virtual coordinates — values below the
// viewport clamp to 0.
// ===========================================================================
TEST(UIScalerTest, UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero)
{
    // 1920x1080 virtual, 1280x720 viewport, 0px left offset, 90px top letterbox.
    UIScaler scaler(1920, 1080, 1280, 720, 0, 90);

    // physY=80 is in the top black bar (80 < offsetY=90).
    // Pre-clamp virtual_y = (80 - 90) * 1080 / 720 = -15 (negative — invalid).
    // After clamp: virtual_y must be 0.
    UIScaler::VirtualPoint vp = scaler.unproject(640, 80);

    EXPECT_EQ(vp.y, 0)
        << "Mouse in top black bar must clamp virtual Y to 0 (not negative)";
    // X coordinate: (640 - 0) * 1920 / 1280 = 960.
    EXPECT_EQ(vp.x, 960)
        << "Mouse in top black bar must still produce correct virtual X";
}

// ===========================================================================
// Test 5: UIScaler_GetViewportRect_ReturnsCorrectOffsets
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler tests
//           named test case 5.
//           architecture/ui-ux/resolution-ui-scaling.md §Mouse un-projection.
//
// getViewportRect() must return the active viewport rectangle in physical pixels:
//   {x: offsetX, y: offsetY, w: viewportW, h: viewportH}
//
// This rect is used by input handlers before calling unproject().
// ===========================================================================
TEST(UIScalerTest, UIScaler_GetViewportRect_ReturnsCorrectOffsets)
{
    // 1920x1080 virtual, 1280x720 viewport, 0px left offset, 90px top letterbox.
    UIScaler scaler(1920, 1080, 1280, 720, 0, 90);

    Rect rect = scaler.getViewportRect();

    EXPECT_EQ(rect.x, 0)    << "Viewport rect x must equal offsetX (0)";
    EXPECT_EQ(rect.y, 90)   << "Viewport rect y must equal offsetY (90)";
    EXPECT_EQ(rect.w, 1280) << "Viewport rect w must equal viewportW (1280)";
    EXPECT_EQ(rect.h, 720)  << "Viewport rect h must equal viewportH (720)";
}

// ===========================================================================
// Test 6: UIScaler_MouseBeyondVirtualWidth_VirtualX_ClampedToMax
//
// Spec ref: architecture/testing/testability-architecture.md §UIScaler tests
//           named test case (additional — upper-bound clamp).
//           architecture/ui-ux/resolution-ui-scaling.md §UIScaler Constructor
//           (OUTPUT CLAMPING paragraph) and
//           phase-1.md §UIScaler Unit Tests item 6.
//
// Scenario: 1920x1080 virtual, 1920x1080 viewport, no offset.
// A physical X of 2000 maps to virtual_x = 2000 * 1920 / 1920 = 2000, which
// exceeds virtualW-1 = 1919.
//
// After clamp: virtual_x must be 1919 (clamped to virtualW-1).
// The maximum valid virtual X is 1919 — not 1920. Returning 1920 is an
// off-by-one error that produces out-of-bounds atlas indices.
//
// UIScaler clamp boundary requirement (phase-1.md §UIScaler clamp):
//   (a) Lower-bound: below (0,0) clamps to (0,0)       [covered by Test 4]
//   (b) Upper-bound: above (1919,1079) clamps to (1919,1079) [this test]
// Both directions must be verified in Phase 1.
// ===========================================================================
TEST(UIScalerTest, UIScaler_MouseBeyondVirtualWidth_VirtualX_ClampedToMax)
{
    // Full native resolution, no offset — identity transform before clamping.
    UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);

    // physX=2000 maps to virtual_x = 2000 * 1920/1920 = 2000 (out of range).
    // Must be clamped to 1919 (virtualW - 1).
    UIScaler::VirtualPoint vp = scaler.unproject(2000, 540);

    EXPECT_EQ(vp.x, 1919)
        << "Physical X beyond viewport must clamp to virtualW-1 (1919), not 1920";
    EXPECT_EQ(vp.y, 540)
        << "Physical Y within range must be unchanged (540)";
}
