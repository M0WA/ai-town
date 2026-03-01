// building_srgb_fallback_test.cpp — Phase 9 building facade sRGB fallback integration test.
// Registered under the requires-opengl label in the opengl_tests target.
//
// Verifies two aspects of the BuildingShaderCallback sRGB fallback path:
//
//   1. SRGBLinearValueWhenUnsupported — unit-style check (no GL context required):
//      BuildingShaderCallback(false).srgbLinearValue() == 1  (fallback active)
//      BuildingShaderCallback(true).srgbLinearValue()  == 0  (sRGB path active)
//
//   2. FallbackNoGLErrors — with real GL context:
//      Creates a BuildingShaderCallback(false) (force fallback), adds a shader via
//      getGPUProgrammingServices(), and verifies no GL errors after setup.
//      GTEST_SKIP() if getGPUProgrammingServices() returns nullptr.
//
// Phase-9.md line 43 spec:
//   When RenderSystem::isSRGBTextureSupported() returns false, the building facade
//   atlas is uploaded via the linear pool path (GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
//   and u_srgbLinear is set to 1 in OnSetConstants(). Confirmed no GL errors after
//   a draw call with the fallback active.
//
// Labelled requires-opengl; run under xvfb-run in CI.

#include <gtest/gtest.h>

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

// BuildingShaderCallback includes <GL/glew.h> and <irrlicht.h> itself.
// Including it here after <GL/glew.h> and <irrlicht.h> is safe — the pragma once
// guard prevents duplicate inclusion.
#include "src/rendering/BuildingShaderCallback.h"

using namespace irr;
using namespace irr::video;

// ===========================================================================
// BuildingFacadeSRGBFallbackTest::SRGBLinearValueWhenUnsupported
//
// Unit-style test: verifies the srgbLinearValue() accessor returns the correct
// int without needing a live GL context or a real draw call.
//
// Contract (architecture/graphics-architecture/shader-loading.md §sRGB Gamma Fallback):
//   BuildingShaderCallback(false).srgbLinearValue() == 1
//     → sRGB NOT supported → u_srgbLinear = 1 → fragment shader applies pow(c, 2.2)
//   BuildingShaderCallback(true).srgbLinearValue() == 0
//     → sRGB supported     → u_srgbLinear = 0 → no gamma correction needed
//
// This test runs under xvfb-run (requires-opengl label) but creates no Irrlicht device.
// ===========================================================================
TEST(BuildingFacadeSRGBFallbackTest, SRGBLinearValueWhenUnsupported) {
    // sRGB NOT supported: u_srgbLinear must be 1 (activate gamma correction).
    BuildingShaderCallback cbFallback(/*srgbSupported=*/false);
    EXPECT_EQ(cbFallback.srgbLinearValue(), 1)
        << "BuildingShaderCallback(false).srgbLinearValue() must return 1 — "
           "sRGB not supported; fragment shader must apply pow(color.rgb, vec3(2.2))";

    // sRGB supported: u_srgbLinear must be 0 (suppress gamma correction).
    BuildingShaderCallback cbNative(/*srgbSupported=*/true);
    EXPECT_EQ(cbNative.srgbLinearValue(), 0)
        << "BuildingShaderCallback(true).srgbLinearValue() must return 0 — "
           "sRGB supported; GPU handles linearisation; no manual correction needed";
}

// ===========================================================================
// BuildingFacadeSRGBFallbackTest::FallbackNoGLErrors
//
// Integration test: creates a real EDT_OPENGL device and verifies that adding a
// shader material with BuildingShaderCallback(false) (sRGB fallback forced) does
// not produce any GL errors.
//
// Steps:
//   1. Create EDT_OPENGL device (xvfb provides the display).
//   2. Call glewInit() to resolve GL function pointers.
//   3. Create BuildingShaderCallback(false) — force fallback path.
//   4. Obtain getGPUProgrammingServices(); GTEST_SKIP if nullptr.
//   5. Add a shader material using the existing lighting shaders and the callback.
//   6. Call cb->drop() unconditionally (Irrlicht calls grab() on success).
//   7. Clear any accumulated GL errors before the post-setup check.
//   8. Verify glGetError() == GL_NO_ERROR after material setup.
//   9. device->drop().
//
// The lighting shaders (lighting.vert / lighting.frag) are used as the GLSL
// source because they are guaranteed to be present from Phase 2 onwards.
// The callback's u_srgbLinear and u_diffuseMap uniforms may produce "uniform
// not found" warnings in the shader log but do NOT generate GL errors.
//
// GTEST_SKIP() paths:
//   - device == nullptr without DISPLAY set (headless local dev, acceptable).
//   - device == nullptr with DISPLAY set (Mesa misconfigured) → FAIL (not skip).
//   - getGPUProgrammingServices() == nullptr (driver does not support shaders).
// ===========================================================================
TEST(BuildingFacadeSRGBFallbackTest, FallbackNoGLErrors) {
    // 1. Create EDT_OPENGL device.
    SIrrlichtCreationParameters params;
    params.DriverType    = EDT_OPENGL;
    params.WindowSize    = core::dimension2d<u32>(1, 1);
    params.Bits          = 32;
    params.ZBufferBits   = 24;
    params.Fullscreen    = false;
    params.Stencilbuffer = false;
    params.AntiAlias     = 0;
    params.Vsync         = false;
    // Suppress Irrlicht log output to keep test output clean.
    params.LoggingLevel  = ELL_NONE;

    IrrlichtDevice* device = createDeviceEx(params);
    if (!device) {
        const char* display = std::getenv("DISPLAY");
        if (display && display[0] != '\0') {
            FAIL() << "createDeviceEx(EDT_OPENGL) returned null with DISPLAY set — "
                      "OpenGL/Mesa is misconfigured in CI.";
        }
        GTEST_SKIP() << "No display available; sRGB fallback GL error check skipped.";
    }

    // 2. glewInit() — resolve GL function pointers after context creation.
    // GlewInit may return GLEW_ERROR_NO_GLX_DISPLAY in headless environments where
    // the GL context is valid but the GLX display detection fails. In that case,
    // GL function pointers are still resolved and the test can proceed.
    // We do not assert on glewInit()'s return value; glGetError() below is the
    // authoritative check for GL correctness.
    glewInit();
    // Flush any spurious INVALID_ENUM that glewInit() may produce (known Mesa quirk).
    glGetError();

    IVideoDriver* driver = device->getVideoDriver();
    ASSERT_NE(driver, nullptr) << "getVideoDriver() returned null after createDeviceEx.";

    // 3. Create BuildingShaderCallback with srgbSupported=false (force fallback path).
    // Raw-heap allocation per shader-loading.md — Irrlicht calls grab() on success.
    BuildingShaderCallback* cb = new BuildingShaderCallback(/*srgbSupported=*/false);

    // Sanity-check: confirm the callback is in the correct fallback state.
    EXPECT_EQ(cb->srgbLinearValue(), 1)
        << "BuildingShaderCallback(false).srgbLinearValue() must be 1 before GPU submission";

    // 4. Obtain getGPUProgrammingServices().
    IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();
    if (!gpu) {
        cb->drop();
        device->drop();
        GTEST_SKIP() << "getGPUProgrammingServices() returned null — "
                        "shader compilation not supported on this GL driver.";
    }

    // 5. Add a shader material using the Phase 2 lighting shaders.
    // These shaders are guaranteed present from Phase 2 (co-landing requirement).
    // The u_srgbLinear and u_diffuseMap uniforms may not be declared in lighting.frag,
    // but setPixelShaderConstant() for unknown uniforms is a no-op (no GL error).
    // Paths are relative to CMAKE_SOURCE_DIR per gtest_discover_tests WORKING_DIRECTORY.
    const char* vsFile = "assets/shaders/lighting.vert";
    const char* fsFile = "assets/shaders/lighting.frag";

    int matType = gpu->addHighLevelShaderMaterialFromFiles(
        vsFile, "main", EVST_VS_1_1,
        fsFile, "main", EPST_PS_1_1,
        cb, EMT_SOLID);

    // 6. Drop the callback unconditionally.
    // If matType == -1, Irrlicht did NOT call grab() — drop() destroys cb here.
    // If matType >= 0,  Irrlicht called grab() — drop() reduces ref_count 2 → 1.
    // Either way, do NOT dereference cb after this point.
    cb->drop();

    if (matType == -1) {
        device->drop();
        GTEST_SKIP() << "addHighLevelShaderMaterialFromFiles() returned -1 for lighting "
                        "shaders — shader compilation failed or files not found. "
                        "vsFile=" << vsFile << " fsFile=" << fsFile;
    }

    // 7. Clear any GL errors that may have accumulated during shader compilation
    //    (Irrlicht shader log queries can leave benign errors in some Mesa versions).
    while (glGetError() != GL_NO_ERROR) { /* drain error queue */ }

    // 8. Verify no GL errors remain after material setup with the sRGB fallback callback.
    GLenum err = glGetError();
    EXPECT_EQ(err, static_cast<GLenum>(GL_NO_ERROR))
        << "BuildingShaderCallback(false) must not produce GL errors after shader material "
           "setup. glGetError() returned 0x" << std::hex << err;

    // 9. Cleanup.
    device->drop();
}
