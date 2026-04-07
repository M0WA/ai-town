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
//   RoadShaderCallback* cb = new RoadShaderCallback(srgbSupported, diffuseTexGLuint);
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
//   m_srgbSupported     — bool from construction; used to derive u_srgbLinear value.
//   m_diffuseTexGLuint  — raw GLuint of road_asphalt_tileable.dds (sRGB or linear upload).
//                         Bound to GL_TEXTURE0 inside OnSetConstants() before the
//                         sampler uniform is set. 0 = no texture (e.g., headless/EDT_NULL).
//
// u_srgbLinear semantics (road.frag):
//   u_srgbLinear == 0 (srgbSupported=true):
//     Texture uploaded as GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT.
//     GPU decoded sRGB→linear on sample. road.frag applies pow(x, 1/2.2) to
//     re-encode linear→sRGB for the non-sRGB framebuffer (recovering authored colors).
//   u_srgbLinear == 1 (srgbSupported=false):
//     Texture uploaded as GL_COMPRESSED_RGBA_S3TC_DXT5_EXT (linear, no GPU decode).
//     Raw authored bytes are the display values; road.frag outputs them directly.
//
// The callback stores the sRGB flag and the raw GL texture handle. Both are resolved
// once at construction time — no RenderSystem pointer is stored.
class RoadShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    // Constructor.
    // srgbSupported:    pass (glewIsExtensionSupported("GL_EXT_texture_sRGB") == GL_TRUE).
    //   true  → u_srgbLinear = 0; road.frag applies pow(x, 1/2.2) inverse re-encode.
    //   false → u_srgbLinear = 1; road.frag outputs raw texture sample directly.
    // diffuseTexGLuint: raw GLuint from TextureCache::loadSRGB().
    //   Bound to GL_TEXTURE0 in OnSetConstants().
    //   Pass 0 in tests or when no GL context is available.
    explicit RoadShaderCallback(bool srgbSupported, GLuint diffuseTexGLuint = 0);

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
    void OnSetMaterial(const irr::video::SMaterial& /*material*/) override { /* intentionally empty — all state set per-draw in OnSetConstants() */ }

    // srgbLinearValue() — test accessor.
    // Returns the int that will be passed to setPixelShaderConstant("u_srgbLinear", ...).
    // Allows unit-style assertions (EXPECT_EQ) without a live GL context.
    //
    // Contract (matches road.frag semantics):
    //   RoadShaderCallback(true).srgbLinearValue()  == 0
    //     → sRGB upload; GPU decoded to linear; road.frag applies pow(x, 1/2.2) re-encode.
    //   RoadShaderCallback(false).srgbLinearValue() == 1
    //     → linear upload; raw authored values; road.frag outputs directly.
    int srgbLinearValue() const { return m_srgbSupported ? 0 : 1; }

private:
    bool   m_srgbSupported;
    GLuint m_diffuseTexGLuint;
};
