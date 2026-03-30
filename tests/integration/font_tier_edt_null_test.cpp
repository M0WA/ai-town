// font_tier_edt_null_test.cpp — Phase 11g: IrrlichtUIBackend EDT_NULL constructor test.
// Verifies that constructing IrrlichtUIBackend with an EDT_NULL device skips font
// loading gracefully: m_hudFont and m_hudMonoFont remain nullptr, no crash.
// Label: "integration" (no display required — EDT_NULL only).
//
// Spec ref: phase-11g.md §IrrlichtUIBackend_EDT_NULL_ConstructorSkipsFontLoadingGracefully
//           architecture/ui-ux/resolution-ui-scaling.md §Bitmap Font Tier Selection
#include "src/rendering/IrrlichtUIBackend.h"
#include <irrlicht.h>
#include <gtest/gtest.h>

TEST(IrrlichtUIBackend_EDT_NULL, ConstructorSkipsFontLoadingGracefully) {
    irr::IrrlichtDevice* device = irr::createDevice(
        irr::video::EDT_NULL,
        irr::core::dimension2d<irr::u32>(1920, 1080));
    ASSERT_NE(device, nullptr) << "EDT_NULL device creation failed";

    {
        IrrlichtUIBackend backend(device);
        // EDT_NULL path skips font loading — both font pointers must be null.
        EXPECT_EQ(backend.getHudFont(), nullptr)
            << "getHudFont() must return nullptr on EDT_NULL (no font loading)";
        EXPECT_EQ(backend.getMonoFont(), nullptr)
            << "getMonoFont() must return nullptr on EDT_NULL (no font loading)";
        // Destructor must not crash (implicit on scope exit).
    }

    device->drop();
}
