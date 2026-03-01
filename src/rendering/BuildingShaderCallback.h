#pragma once

// BuildingShaderCallback.h — Phase 9 building facade shader callback.
//
// Implements IShaderConstantSetCallBack::OnSetConstants() for the building
// facade shader pair (building.vert / building.frag).
//
// Responsibilities in OnSetConstants():
//   1. Save GL_ACTIVE_TEXTURE (mandatory per shader-loading.md).
//   2. Set u_diffuseMap sampler uniform to kTexUnitDiffuse (unit 0).
//   3. Set u_srgbLinear (int, not bool): 1 when sRGB is NOT supported (linear
//      upload + manual pow(color.rgb, vec3(2.2)) gamma correction in shader),
//      0 when sRGB IS supported (no gamma correction needed).
//   4. Restore GL_ACTIVE_TEXTURE.
//
// The sRGB fallback path (architecture/graphics-architecture/shader-loading.md
// § sRGB Gamma Fallback — Uniform Bool Approach):
//   When GL_EXT_texture_sRGB is absent, the building facade atlas is uploaded
//   as GL_COMPRESSED_RGB_S3TC_DXT1_EXT (linear). The fragment shader must apply
//   a manual pow(color.rgb, vec3(2.2)) correction. u_srgbLinear = 1 activates
//   this correction; u_srgbLinear = 0 suppresses it (sRGB upload path is active).
//
// Construction:
//   bool srgbSupported — pass RenderSystem::isSRGBTextureSupported() in production.
//   In tests, pass false to force the fallback path, or true to suppress it.
//
// Allocation pattern (raw-heap + ->drop() per shader-loading.md):
//   BuildingShaderCallback* cb = new BuildingShaderCallback(srgbSupported);
//   gpu->addHighLevelShaderMaterialFromFiles(..., cb, ...);
//   cb->drop();  // unconditional — Irrlicht holds its own grab() reference.
//
// Do NOT use std::unique_ptr — causes double-free when Irrlicht drops the callback.
// See architecture/graphics-architecture/shader-loading.md for the full lifetime spec.
//
// GL_ACTIVE_TEXTURE save/restore is MANDATORY per shader-loading.md §CRITICAL to prevent
// Irrlicht's internal m_CurrentTexture tracking from being corrupted on subsequent draws.
//
// Test accessor:
//   srgbLinearValue() returns the precomputed int value that will be passed to
//   setPixelShaderConstant("u_srgbLinear", ...) in OnSetConstants(). This allows
//   unit-style assertions without requiring a live GL context or a real draw call.

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "shader_constants.h"

// BuildingShaderCallback — IShaderConstantSetCallBack for the building facade shader.
//
// Holds:
//   m_srgbSupported — precomputed bool from construction; used to derive u_srgbLinear.
//
// The callback is lightweight: it stores only the sRGB flag passed at construction.
// No RenderSystem or TextureCache pointer is stored — the value is resolved once at
// construction time.
class BuildingShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    // Constructor.
    // srgbSupported: pass RenderSystem::isSRGBTextureSupported() in production.
    //   true  → u_srgbLinear = 0 (sRGB upload path; no gamma correction needed)
    //   false → u_srgbLinear = 1 (linear upload path; shader applies pow(c, 2.2))
    explicit BuildingShaderCallback(bool srgbSupported)
        : m_srgbSupported{srgbSupported}
    {}

    // OnSetConstants — called by Irrlicht immediately before each draw call that
    // uses this material. Saves/restores GL_ACTIVE_TEXTURE, sets u_diffuseMap and
    // u_srgbLinear.
    //
    // u_srgbLinear is passed as int (Irrlicht maps setPixelShaderConstant int
    // to GLSL bool: 1 → true, 0 → false). See shader-loading.md §sRGB Gamma Fallback.
    // Passing bool* to setPixelShaderConstant is UB — bool is 1 byte on most ABIs;
    // Irrlicht reads 4 bytes. Always use int.
    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 /*userData*/) override;

    // OnSetMaterial — optional callback; called when the material is set.
    // No action needed here — all state is set per-draw in OnSetConstants().
    void OnSetMaterial(const irr::video::SMaterial& /*material*/) override {}

    // srgbLinearValue() — test accessor.
    // Returns the int that will be passed to setPixelShaderConstant("u_srgbLinear", ...).
    // Allows unit-style assertions (EXPECT_EQ) without a live GL context.
    //
    // Contract:
    //   BuildingShaderCallback(false).srgbLinearValue() == 1  (must activate correction)
    //   BuildingShaderCallback(true).srgbLinearValue()  == 0  (must not activate correction)
    int srgbLinearValue() const { return m_srgbSupported ? 0 : 1; }

private:
    bool m_srgbSupported;
};
