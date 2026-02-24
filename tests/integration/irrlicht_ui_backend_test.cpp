// irrlicht_ui_backend_test.cpp — Phase 3 EDT_NULL runtime integration test.
//
// Verifies that IrrlichtUIBackend constructs cleanly against an EDT_NULL Irrlicht
// device and returns a non-zero UIElementHandle from addStaticText(). EDT_NULL
// requires no display server and is safe on headless CI runners.
//
// Label: "integration" (no display required — EDT_NULL only)
//
// Spec ref: phase-3.md §IrrlichtUIBackend EDT_NULL Integration Test
//           architecture/testing/testability-architecture.md §IUIBackend
#include "src/rendering/IrrlichtUIBackend.h"
// IrrlichtUIBackend.h already pulls in src/ui/IUIBackend.h (UIElementHandle,
// kInvalidUIElement, IUIBackend). No separate include needed.
#include <irrlicht.h>
#include <gtest/gtest.h>

// Phase 3: Runtime test using EDT_NULL device.
// Verifies that IrrlichtUIBackend constructs cleanly and returns non-zero handles
// for element creation methods. EDT_NULL requires no display server.
TEST(IrrlichtUIBackendTest, AddStaticText_ReturnsNonZeroHandle) {
    irr::IrrlichtDevice* device = irr::createDevice(
        irr::video::EDT_NULL,
        irr::core::dimension2d<irr::u32>(640, 480));
    ASSERT_NE(device, nullptr) << "EDT_NULL device creation failed";

    {
        // Scope backend so its destructor runs before device->drop().
        // Prevents use-after-free when Phase 8 implements real Irrlicht teardown.
        IrrlichtUIBackend backend(device);
        UIElementHandle handle = backend.addStaticText("Test", 0, 0, 100, 20);
        EXPECT_NE(handle, static_cast<UIElementHandle>(0))
            << "addStaticText must return a non-zero handle";
    }  // backend destructor fires here

    device->drop();
}
