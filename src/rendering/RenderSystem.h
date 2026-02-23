#pragma once

// RenderSystem — owns the IrrlichtDevice exclusively (RAII).
// Manages GL capability queries after device creation.
// Single instance per application lifetime.
//
// Creation parameters: EDT_OPENGL, 1280x720, Bits=32, ZBufferBits=24, Stencil=true,
// AntiAlias=4, Vsync=false — per architecture/graphics-architecture/irrlicht-device-lifecycle.md.
//
// glewInit() is called immediately after createDevice() returns non-null (EDT_NULL guard applied).
// GL capability members are populated in the SUCCESS path; defaulted in EDT_NULL and RELEASE fallback.
// GLEW available (vcpkg libGLEW.a, Q2 confirmed 2026-02-22) — using glewIsExtensionSupported().

// Forward-declare Irrlicht types to keep this header clean.
// Consumers that need the full Irrlicht API must include <irrlicht.h> themselves.
namespace irr {
    class IrrlichtDevice;
    namespace video { class IVideoDriver; }
    namespace scene { class ISceneManager; }
}

class RenderSystem {
public:
    RenderSystem();
    ~RenderSystem();

    // Non-copyable / non-movable — device ownership is exclusive
    RenderSystem(const RenderSystem&)            = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&)                 = delete;
    RenderSystem& operator=(RenderSystem&&)      = delete;

    // Returns the owned device pointer. May be nullptr if construction failed.
    // Consumers must null-check before use.
    irr::IrrlichtDevice* getDevice() const { return m_device; }

    // GL capability accessors (populated from glewInit() SUCCESS path)
    int   getMaxTextureSize()        const { return m_maxTextureSize; }
    bool  isSRGBTextureSupported()   const { return m_srgbTextureSupported; }
    float getMaxAnisotropy()         const { return m_maxAnisotropy; }

private:
    irr::IrrlichtDevice* m_device{nullptr};

    // GL-dependent members — initialized in glewInit() SUCCESS path.
    // Defaulted in EDT_NULL path and RELEASE fatal-failure path (without any GL call).
    // The three explicit assignments in the RELEASE fallback code path body are MANDATORY —
    // they must appear there (not only in member-initializer-list defaults) so code inspection
    // can unambiguously verify the fallback behaviour (ISSUE-P exit criterion).
    int   m_maxTextureSize{2048};
    bool  m_srgbTextureSupported{false};
    float m_maxAnisotropy{1.0f};
};
