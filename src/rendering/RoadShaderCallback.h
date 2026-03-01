#pragma once

// RoadShaderCallback.h — Phase 9 GLSL shader callback for the road tile shader.
//
// Implements IShaderConstantSetCallBack::OnSetConstants() for the road tile
// shader pair (road.vert / road.frag).
//
// Responsibilities in OnSetConstants():
//   1. Save GL_ACTIVE_TEXTURE (mandatory per shader-loading.md §CRITICAL).
//   2. Set u_diffuseMap sampler uniform to kTexUnitDiffuse (unit 0).
//   3. Set u_srgbLinear (int, not bool): 1 when sRGB is NOT supported (linear
//      upload + manual pow(color.rgb, vec3(2.2)) gamma correction in shader),
//      0 when sRGB IS supported (no gamma correction needed).
//   4. Restore GL_ACTIVE_TEXTURE.
//
// The sRGB fallback path (architecture/graphics-architecture/shader-loading.md
// § sRGB Gamma Fallback — Uniform Bool Approach):
//   When GL_EXT_texture_sRGB is absent, road_asphalt_tileable.dds is retrieved
//   via the linear pool path instead of the sRGB raw-GL path.
//   u_srgbLinear = 1 activates pow(color.rgb, vec3(2.2)) correction in the shader.
//   u_srgbLinear = 0 suppresses it (sRGB upload path is active, GPU handles decode).
//
// Construction:
//   bool srgbSupported — pass RenderSystem::isSRGBTextureSupported() in production.
//   In tests, pass false to force the fallback path, or true to suppress it.
//
// Allocation pattern (raw-heap + ->drop() per shader-loading.md):
//   RoadShaderCallback* cb = new RoadShaderCallback(srgbSupported);
//   gpu->addHighLevelShaderMaterialFromFiles(..., cb, ...);
//   cb->drop();  // unconditional — Irrlicht holds its own grab() reference.
//
// Do NOT use std::unique_ptr — causes double-free when Irrlicht drops the callback.
// See architecture/graphics-architecture/shader-loading.md for the full lifetime spec.
//
// GL_ACTIVE_TEXTURE save/restore is MANDATORY per shader-loading.md §CRITICAL to prevent
// Irrlicht's internal m_CurrentTexture tracking from being corrupted on subsequent draws.

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "shader_constants.h"

// RoadShaderCallback — IShaderConstantSetCallBack for the road tile shader.
//
// Holds:
//   m_srgbSupported — bool from construction; used to derive u_srgbLinear value.
//
// The callback is lightweight: it stores only the sRGB flag passed at construction.
// No RenderSystem or TextureCache pointer is stored — the value is resolved once at
// construction time.
class RoadShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    // Constructor.
    // srgbSupported: pass RenderSystem::isSRGBTextureSupported() in production.
    //   true  → u_srgbLinear = 0 (sRGB upload path; no gamma correction needed)
    //   false → u_srgbLinear = 1 (linear upload path; shader applies pow(c, 2.2))
    explicit RoadShaderCallback(bool srgbSupported);

    // OnSetConstants — called by Irrlicht immediately before each draw call that
    // uses this material. Saves/restores GL_ACTIVE_TEXTURE, sets u_diffuseMap and
    // u_srgbLinear.
    //
    // u_srgbLinear is passed as int (Irrlicht maps setPixelShaderConstant int
    // to GLSL bool: 1 → true, 0 → false). See shader-loading.md §sRGB Gamma Fallback.
    // Passing bool* to setPixelShaderConstant is UB — bool is 1 byte on most ABIs;
    // Irrlicht reads 4 bytes. Always use int.
    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override;

    // OnSetMaterial — optional callback; called when the material is set.
    // No action needed here — all state is set per-draw in OnSetConstants().
    void OnSetMaterial(const irr::video::SMaterial& /*material*/) override {}

    // srgbLinearValue() — test accessor.
    // Returns the int that will be passed to setPixelShaderConstant("u_srgbLinear", ...).
    // Allows unit-style assertions (EXPECT_EQ) without a live GL context.
    //
    // Contract:
    //   RoadShaderCallback(false).srgbLinearValue() == 1  (must activate correction)
    //   RoadShaderCallback(true).srgbLinearValue()  == 0  (must not activate correction)
    int srgbLinearValue() const { return m_srgbSupported ? 0 : 1; }

private:
    bool m_srgbSupported;
};
