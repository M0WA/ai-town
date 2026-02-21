// RenderSystem.cpp — Irrlicht device lifecycle + GLEW initialization.
// GLEW must be included BEFORE irrlicht.h to avoid symbol conflicts.
// GLEW::GLEW is linked BEFORE Irrlicht in CMakeLists.txt (GLEW symbol duplication mitigation-2).
#include <GL/glew.h>   // MUST come before any Irrlicht or OpenGL includes

#include <irrlicht.h>

#include "RenderSystem.h"

#include <cstdlib>   // std::abort
#include <cstdio>    // fprintf

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
            fprintf(stderr, "[RenderSystem] WARNING: glewInit returned GLEW_ERROR_NO_GL_VERSION — "
                    "continuing without version string (non-fatal).\n");
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
            fprintf(stderr, "[RenderSystem] WARNING: GL_EXT_texture_filter_anisotropic not supported — "
                    "anisotropy defaulting to 1.0f.\n");
            m_maxAnisotropy = 1.0f;
        }

    } else {
        // FAILURE PATH — any other glewResult value
#ifndef NDEBUG
        // DEBUG builds: abort immediately (hard assert)
        fprintf(stderr, "[RenderSystem] FATAL (DEBUG): glewInit failed: %s\n",
                glewGetErrorString(glewResult));
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

        // (4) Display user-facing error notification
        fprintf(stderr, "[RenderSystem] ERROR: GLEW initialization failed in RELEASE build. "
                "Falling back to EDT_NULL (headless mode). glewInit error: %s\n",
                glewGetErrorString(glewResult));

        // (5) Return — SUCCESS-path GL query block is skipped entirely in RELEASE fallback
        return;
#endif
    }
}

RenderSystem::~RenderSystem() {
    if (m_device) {
        m_device->drop();
        m_device = nullptr;
    }
}
