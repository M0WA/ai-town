// RenderSystem.cpp — Irrlicht device lifecycle + GLEW initialization.
// GLEW must be included BEFORE irrlicht.h to avoid symbol conflicts.
// GLEW::GLEW is linked BEFORE Irrlicht in CMakeLists.txt (GLEW symbol duplication mitigation-2).
#include <GL/glew.h>   // MUST come before any Irrlicht or OpenGL includes

#include <irrlicht.h>

#include "RenderSystem.h"

#include <cstdlib>   // std::abort

using namespace irr;
using namespace irr::video;

RenderSystem::RenderSystem() {
    // -------------------------------------------------------------------------
    // Step 1: Create the Irrlicht device with EDT_OPENGL
    // -------------------------------------------------------------------------
    SIrrlichtCreationParameters params;
    params.DriverType  = EDT_OPENGL;
    params.WindowSize  = core::dimension2d<u32>(1280, 720);
    params.Bits        = 32;
    params.ZBufferBits = 24;   // required for correct depth sorting
    params.AntiAlias   = 4;    // 4x MSAA per "realistic graphics" goal
    params.Vsync       = false;
    params.EventReceiver = nullptr;

    m_device = createDeviceEx(params);

    if (m_device)
        m_device->getLogger()->setLogLevel(irr::ELL_ERROR);

    if (!m_device) {
        // EDT_OPENGL unavailable — log and abort
        fprintf(stderr, "[RenderSystem] FATAL: Failed to create Irrlicht device with EDT_OPENGL.\n");
        std::abort();
    }

    // -------------------------------------------------------------------------
    // Step 2: EDT_NULL guard — skip glewInit() if no GL context (headless CI)
    // -------------------------------------------------------------------------
    // The EDT_NULL guard is an OUTER guard that fires BEFORE any glewResult check.
    // It is NOT one of the three glewResult tiers (SUCCESS / DEBUG-fatal / RELEASE-fatal).
    // Only call glewInit() when the device has a real GL context.
    if (m_device->getVideoDriver()->getDriverType() == EDT_NULL) {
        // Headless path (AITOWN_HEADLESS / CI): set all GL-dependent defaults without any GL call.
        m_maxTextureSize       = 2048;
        m_srgbTextureSupported = false;
        m_maxAnisotropy        = 1.0f;
        return; // constructor done
    }

    // -------------------------------------------------------------------------
    // Step 3: glewInit() — THREE-TIER response based on glewResult
    // -------------------------------------------------------------------------
    glewExperimental = GL_TRUE;
    const GLenum glewResult = glewInit();

    if (glewResult == GLEW_OK || glewResult == GLEW_ERROR_NO_GL_VERSION) {
        // SUCCESS PATH — GLEW_ERROR_NO_GL_VERSION is non-fatal: log and continue.
        if (glewResult == GLEW_ERROR_NO_GL_VERSION) {
            m_device->getLogger()->log(
                "[RenderSystem] WARNING: glewInit returned GLEW_ERROR_NO_GL_VERSION — "
                "continuing without version string (non-fatal).",
                irr::ELL_WARNING);
        }

        // Query GL_MAX_TEXTURE_SIZE and store (Phase 1 mandatory — same step as glewInit SUCCESS)
        // NOTE: query immediately after createDevice() per architecture/graphics-architecture/irrlicht-device-lifecycle.md
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

        // Query sRGB texture extension support (Phase 1 mandatory)
        // Must be present at Phase 1 to avoid adding a constructor side-effect mid-pipeline.
        m_srgbTextureSupported = (glewIsExtensionSupported("GL_EXT_texture_sRGB") == GL_TRUE);

        // Query anisotropy support (Phase 1 mandatory — same timing as m_maxTextureSize)
        if (glewIsExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m_maxAnisotropy);
        } else {
            m_device->getLogger()->log(
                "[RenderSystem] WARNING: GL_EXT_texture_filter_anisotropic not supported — "
                "anisotropy defaulting to 1.0f.",
                irr::ELL_WARNING);
            m_maxAnisotropy = 1.0f;
        }

    } else {
        // FAILURE PATH — any other glewResult value
#ifndef NDEBUG
        // DEBUG builds: abort immediately (hard assert)
        m_device->getLogger()->log(
            "[RenderSystem] FATAL (DEBUG): glewInit failed.",
            irr::ELL_ERROR);
        std::abort();
#else
        // RELEASE builds: fall back to EDT_NULL device
        // MANDATORY ORDER (ISSUE-P exit criterion checklist):
        // (1) Drop old device BEFORE creating EDT_NULL replacement
        //     (old OpenGL device holds GL state that conflicts with subsequent GL calls)
        m_device->drop();
        m_device = nullptr;

        // (2) Create EDT_NULL device
        m_device = createDevice(EDT_NULL, core::dimension2d<u32>(1, 1));

        // (3) Set ALL THREE GL-dependent member defaults WITHOUT any GL call
        //     MUST appear in this RELEASE fallback code path body (not only in member-initializer-list)
        //     so code inspection can unambiguously verify the fallback behaviour.
        m_maxTextureSize       = 2048;
        m_srgbTextureSupported = false;
        m_maxAnisotropy        = 1.0f;

        // (4) Display user-facing error notification via the EDT_NULL device's logger
        m_device->getLogger()->log(
            "[RenderSystem] ERROR: GLEW initialization failed in RELEASE build. "
            "Falling back to EDT_NULL (headless mode).",
            irr::ELL_ERROR);

        // (5) Return — SUCCESS-path GL query block is skipped entirely in RELEASE fallback
        return;
#endif
    }
}

// loadShader() — null-checks getGPUProgrammingServices() before calling
// addHighLevelShaderMaterialFromFiles(). Required per architecture/graphics-architecture/shader-loading.md.
// EDT_NULL returns null from getGPUProgrammingServices(); non-null drivers returning null are fatal in DEBUG.
// Returns -1 if GPU programming services are unavailable (callers must check for -1).
int RenderSystem::loadShader(
        const char* vsFile, const char* fsFile,
        irr::video::IShaderConstantSetCallBack* cb)
{
    irr::video::IVideoDriver* driver = m_device ? m_device->getVideoDriver() : nullptr;
    if (!driver) {
        if (m_device) {
            m_device->getLogger()->log(
                "[RenderSystem] loadShader: no video driver available.",
                irr::ELL_WARNING);
        }
        return -1;
    }

    irr::video::IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();
    if (!gpu) {
        // EDT_NULL or software rasterizer: GPU programs not supported — return early.
        // Non-EDT_NULL returning null is a fatal configuration error in debug builds.
#ifndef NDEBUG
        if (driver->getDriverType() != irr::video::EDT_NULL) {
            m_device->getLogger()->log(
                "[RenderSystem] FATAL (DEBUG): getGPUProgrammingServices() returned null "
                "on a non-EDT_NULL driver — OpenGL GPU programming services missing.",
                irr::ELL_ERROR);
            std::abort();
        }
#endif
        m_device->getLogger()->log(
            "[RenderSystem] loadShader: getGPUProgrammingServices() returned null "
            "(EDT_NULL or unsupported driver) — shader loading skipped.",
            irr::ELL_WARNING);
        return -1;
    }

    // 8-param overload: no geometry shader stage (Irrlicht GLSL backend has none in V1).
    // EVST_VS_1_1 / EPST_PS_1_1 are placeholder enums — GLSL backend ignores them entirely.
    // The actual GLSL version is determined by the #version directive in the shader source.
    int matType = gpu->addHighLevelShaderMaterialFromFiles(
        vsFile, "main", irr::video::EVST_VS_1_1,
        fsFile, "main", irr::video::EPST_PS_1_1,
        cb, irr::video::EMT_SOLID);

    // cb->drop() is called by the CALLER after this function returns — not here.
    // This function does not own the callback; caller allocates and drops it.

    return matType;
}

RenderSystem::~RenderSystem() {
    if (m_device) {
        m_device->drop();
        m_device = nullptr;
    }
}
