#pragma once

#include "vec3.h"
#include <string>
#include <cstdint>

// Opaque texture handle — uint32_t alias instead of ITexture* so that IRenderer.h
// severs the compile-time dependency on Irrlicht headers for all consumers.
// The concrete IrrlichtRenderer maintains std::unordered_map<TextureHandle, ITexture*> internally.
using TextureHandle = uint32_t;
static constexpr TextureHandle kInvalidTexture = 0;

// CameraParams — passed to IRenderer::setCamera() each frame.
// Defined in IRenderer.h (alongside IRenderer) since it is only used as a parameter to IRenderer.
// Not shared with IAudioSystem — that interface uses CameraState (position/forward/up vectors)
// for 3D spatial audio listener placement, which differs from the renderer's FOV/clip-plane needs.
// IRenderer.h must NOT include audio_types.h — doing so leaks CameraState, SoundPriority,
// StingerType, and other audio types into every render-interface consumer.
struct CameraParams {
    vec3  position{};          // world-space camera eye position
    vec3  target{};            // world-space look-at target (NOT a direction vector)
    float fovDegrees{45.0f};   // horizontal field of view in degrees
    float nearClip{0.1f};      // near clip plane distance in metres
    float farClip{3000.0f};    // far clip plane distance in metres (covers 1024x1024 map + sky)
};

// IRenderer — Phase 0 stub.
// main-thread-only: all methods must be called from the main/render thread.
// Uses opaque TextureHandle (uint32_t) instead of ITexture* — the same pattern as
// IUIBackend with UIElementHandle. MockRenderer::loadTexture() returns an incrementing
// non-zero integer.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void          beginFrame() = 0;              // main-thread-only
    virtual void          endFrame() = 0;                // main-thread-only
    virtual void          drawScene() = 0;               // main-thread-only
    virtual TextureHandle loadTexture(const std::string& path) = 0;  // main-thread-only; returns kInvalidTexture on failure
    virtual void          setCamera(const CameraParams& p) = 0;       // main-thread-only
};
