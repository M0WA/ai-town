// cloud_plane_test.cpp — Phase 10b Feature 2 cloud plane init test.
// Registered under the requires-opengl label in the opengl_tests target.
//
// Tests: CloudPlane_Init_CreatesCloudNode
//   Construct an IrrlichtRenderer with a real EDT_OPENGL device (1×1 window).
//   Assert that m_cloudNode is non-null after construction (cloud init succeeded).
//
// Tests: CloudPlane_Init_NullUnderEDT_NULL
//   Construct an IrrlichtRenderer with an EDT_NULL device.
//   Assert that m_cloudNode is null (headless guard triggered, init skipped).
//
// Run under xvfb-run in CI (requires-opengl label).
// Spec ref: implementation/phase-10b.md §test-dev-cpp
//           architecture/graphics-architecture/sky-clouds.md

#include <gtest/gtest.h>

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "src/rendering/IrrlichtRenderer.h"

using namespace irr;
using namespace irr::video;

// ---------------------------------------------------------------------------
// Helper: create a minimal test device.
// driverType: EDT_OPENGL for the real-device test; EDT_NULL for headless guard.
// Returns nullptr on failure (caller must GTEST_SKIP or ASSERT_NE).
// ---------------------------------------------------------------------------
static IrrlichtDevice* makeDevice(E_DRIVER_TYPE driverType) {
    SIrrlichtCreationParameters params;
    params.DriverType    = driverType;
    params.WindowSize    = core::dimension2d<u32>(1, 1);
    params.Bits          = 32;
    params.ZBufferBits   = 24;
    params.Fullscreen    = false;
    params.Stencilbuffer = false;
    params.AntiAlias     = 0;
    params.Vsync         = false;
    params.LoggingLevel  = ELL_NONE;
    return createDeviceEx(params);
}

// ===========================================================================
// CloudPlaneTest::CloudPlane_Init_CreatesCloudNode
//
// With a real EDT_OPENGL device, IrrlichtRenderer constructor calls
// initCloudPlane() which builds the cloud quad mesh and adds a scene node.
// The resulting scene node pointer must be non-null.
//
// GTEST_SKIP() if device creation fails (headless environment without xvfb).
// ===========================================================================
TEST(CloudPlaneTest, CloudPlane_Init_CreatesCloudNode) {
    IrrlichtDevice* device = makeDevice(EDT_OPENGL);
    if (!device) {
        GTEST_SKIP() << "EDT_OPENGL device creation failed — skipping (no display?)";
    }

    IrrlichtRenderer renderer(device, /*uiManager=*/nullptr);

    EXPECT_NE(renderer.cloudNodeForTest(), nullptr)
        << "m_cloudNode must be non-null after IrrlichtRenderer construction "
           "with a real EDT_OPENGL device";

    device->drop();
}

// ===========================================================================
// CloudPlaneTest::CloudPlane_Init_NullUnderEDT_NULL
//
// With an EDT_NULL device, initCloudPlane() returns immediately (headless guard).
// The cloud node pointer must remain null — no scene node is created.
//
// EDT_NULL still requires an X server on Linux (Irrlicht opens a display
// connection even for null driver); this test runs under xvfb-run.
// GTEST_SKIP() if device creation fails.
// ===========================================================================
TEST(CloudPlaneTest, CloudPlane_Init_NullUnderEDT_NULL) {
    IrrlichtDevice* device = makeDevice(EDT_NULL);
    if (!device) {
        GTEST_SKIP() << "EDT_NULL device creation failed — skipping";
    }

    IrrlichtRenderer renderer(device, /*uiManager=*/nullptr);

    EXPECT_EQ(renderer.cloudNodeForTest(), nullptr)
        << "m_cloudNode must be null under EDT_NULL — headless guard must prevent "
           "cloud plane creation when no real driver is available";

    device->drop();
}
