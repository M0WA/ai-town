// shader_stub_compile_test.cpp — Phase 1 + Phase 2 shader compile tests for opengl_tests target.
//
// Phase 1: ShaderStubCompileTest::Placeholder — trivial SUCCEED() to confirm CI routing.
// Phase 2: ShaderLoadingTest::LightingShaderCompilesWithoutError — loads lighting.vert/frag
//          via addHighLevelShaderMaterialFromFiles() and asserts matType != -1.
//
// Co-landing requirement (architecture/graphics-architecture/shader-loading.md):
//   This file MUST be committed in the same commit as all 6 GLSL stub files:
//   assets/shaders/lighting.vert, lighting.frag, terrain.vert, terrain.frag,
//   billboard.vert, billboard.frag. Committing this test without the GLSL stubs
//   causes an immediate CI failure (Irrlicht returns -1 for missing files).
//
// Shader paths are relative to CMAKE_SOURCE_DIR (project root) — the WORKING_DIRECTORY
// set in aitown_add_tests() via gtest_discover_tests(WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}").

#include <gtest/gtest.h>

#include <GL/glew.h>   // must come before irrlicht.h
#include <irrlicht.h>

#include <cstdlib>     // std::getenv

using namespace irr;
using namespace irr::video;

// ============================================================================
// Phase 1 stub — preserved from Phase 1 (do not remove)
// ============================================================================

TEST(ShaderStubCompileTest, Placeholder) {
    SUCCEED();
}

// ============================================================================
// Phase 2: LightingShaderCompilesWithoutError
// ============================================================================

// Minimal no-op shader callback — required by addHighLevelShaderMaterialFromFiles().
// Irrlicht calls grab() on this callback internally; we call drop() after the call
// to transfer ownership (raw-heap + ->drop() pattern per shader-loading.md).
class StubShaderCallback : public IShaderConstantSetCallBack {
public:
    void OnSetConstants(IMaterialRendererServices* /*services*/, s32 /*userData*/) override {}
};

TEST(ShaderLoadingTest, LightingShaderCompilesWithoutError) {
    // Create an EDT_OPENGL device under xvfb for shader compilation.
    // WindowSize (1x1) minimises GPU resource allocation — we only need a GL context.
    SIrrlichtCreationParameters params;
    params.DriverType  = EDT_OPENGL;
    params.WindowSize  = core::dimension2d<u32>(1, 1);
    params.Bits        = 32;
    params.ZBufferBits = 24;
    params.AntiAlias   = 0;
    params.Vsync       = false;
    params.EventReceiver = nullptr;

    IrrlichtDevice* device = createDeviceEx(params);

    // Two-condition null-device guard (per phase-2.md and irrlicht-device-lifecycle.md):
    //   - No display AND null device → SKIP (local dev without X server, acceptable).
    //   - DISPLAY set AND null device → FAIL (Mesa/OpenGL misconfigured in CI).
    if (!device) {
        const char* display = std::getenv("DISPLAY");
        if (display && display[0] != '\0') {
            FAIL() << "createDevice(EDT_OPENGL) returned null with DISPLAY set — "
                      "OpenGL/Mesa is misconfigured in CI.";
        }
        GTEST_SKIP() << "No display available; shader compilation skipped.";
    }

    IVideoDriver* driver = device->getVideoDriver();
    ASSERT_NE(driver, nullptr) << "getVideoDriver() returned null after createDeviceEx.";

    IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();
    if (!gpu) {
        device->drop();
        GTEST_SKIP() << "getGPUProgrammingServices() returned null — shader compilation not supported.";
    }

    // Shader paths relative to CMAKE_SOURCE_DIR (project root).
    // gtest_discover_tests WORKING_DIRECTORY is CMAKE_SOURCE_DIR per AitownTestHelpers.cmake.
    const char* vsFile = "assets/shaders/lighting.vert";
    const char* fsFile = "assets/shaders/lighting.frag";

    // Raw-heap allocation — Irrlicht calls grab() on success; we always drop() unconditionally.
    // FAILURE PATH: if matType == -1, Irrlicht did NOT call grab(); drop() destroys cb here.
    // Do NOT dereference cb after drop().
    StubShaderCallback* cb = new StubShaderCallback();

    // 8-param overload: no geometry shader stage (Irrlicht GLSL backend has none in V1).
    // EVST_VS_1_1 / EPST_PS_1_1 are placeholder enums — GLSL backend ignores them.
    // The GLSL version is determined by the #version directive in the shader source.
    int matType = gpu->addHighLevelShaderMaterialFromFiles(
        vsFile, "main", EVST_VS_1_1,
        fsFile, "main", EPST_PS_1_1,
        cb, EMT_SOLID);

    cb->drop();  // Irrlicht calls grab() on cb; we must drop() to transfer ownership.
    // FAILURE PATH WARNING: if matType == -1, Irrlicht did NOT call grab().
    // drop() reduces ref_count 1→0 and destroys cb NOW.
    // Do NOT dereference cb below this line.

    device->drop();

    ASSERT_NE(matType, -1)
        << "addHighLevelShaderMaterialFromFiles() returned -1 for lighting shaders. "
           "Check that assets/shaders/lighting.vert and assets/shaders/lighting.frag "
           "exist and begin with '#version 130'. "
           "vsFile=" << vsFile << " fsFile=" << fsFile;
}
