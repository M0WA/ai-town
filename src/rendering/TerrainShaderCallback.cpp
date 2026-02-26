// TerrainShaderCallback.cpp — Phase 5 terrain splat-map shader callback implementation.
//
// Implements OnSetConstants() for the terrain.vert / terrain.frag shader pair.
//
// Binding sequence per architecture/graphics-architecture/shader-loading.md
// §Terrain Splat Shader — 5-Unit Binding Sequence in OnSetConstants():
//   1. Save GL_ACTIVE_TEXTURE.
//   2. Bind splat map to unit 4 (kTexUnitSplatMap).
//   3. Bind sRGB detail layers to units 5–8 (kTexUnitTerrainLayer0 + 0..3).
//   4. Set sampler uniforms (integer unit indices, not bool — Irrlicht reads 4 bytes).
//   5. Set u_srgbLinear (int: 1 = apply manual pow(x,2.2), 0 = GPU handles sRGB).
//   6. Restore GL_ACTIVE_TEXTURE.
//
// See also: architecture/graphics-architecture/texture-cache.md for the raw GLuint pools.

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "TerrainShaderCallback.h"
#include "TextureCache.h"
#include "RenderSystem.h"
#include "shader_constants.h"

using namespace irr;
using namespace irr::video;

void TerrainShaderCallback::OnSetConstants(IMaterialRendererServices* services,
                                            s32 /*userData*/) {
    if (!services) return;
    if (!m_textureCache) return;

    // ------------------------------------------------------------------
    // Step 1: Save GL_ACTIVE_TEXTURE.
    // MANDATORY per shader-loading.md: Irrlicht tracks active texture unit
    // in m_CurrentTexture. Calling glActiveTexture() without restoring
    // corrupts subsequent Irrlicht draw calls.
    //
    // glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit) returns the GL enum
    // (e.g., 0x84C0 for unit 0) — pass directly to glActiveTexture() to restore.
    // ------------------------------------------------------------------
    GLint savedUnit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit);

    // ------------------------------------------------------------------
    // Step 2: Bind splat map to unit 4 (kTexUnitSplatMap = 4).
    // Splat map is uploaded as RGBA8 (linear) via loadSplatMap().
    // ------------------------------------------------------------------
    GLuint splatHandle = m_textureCache->getSplatMapGLuint(m_splatPath);
    glActiveTexture(GL_TEXTURE0 + kTexUnitSplatMap);
    glBindTexture(GL_TEXTURE_2D, splatHandle);

    // ------------------------------------------------------------------
    // Step 3: Bind sRGB terrain detail layers to units 5–8.
    // Each layer is uploaded via loadSRGB() as GL_COMPRESSED_SRGB_S3TC_DXT1_EXT.
    // ------------------------------------------------------------------
    for (int i = 0; i < 4; ++i) {
        GLuint layerHandle = m_textureCache->getSRGBGLuint(m_detailPaths[i]);
        glActiveTexture(GL_TEXTURE0 + kTexUnitTerrainLayer0 + i);
        glBindTexture(GL_TEXTURE_2D, layerHandle);
    }

    // ------------------------------------------------------------------
    // Step 4: Set sampler uniforms.
    // Pass integer unit indices. Irrlicht's setPixelShaderConstant expects
    // a pointer to int (4 bytes); passing a bool* is UB (1 byte on most ABIs).
    // ------------------------------------------------------------------
    int unitSplat = kTexUnitSplatMap;
    services->setPixelShaderConstant("u_splatMap", &unitSplat, 1);

    static const char* kLayerNames[4] = { "u_layer0", "u_layer1", "u_layer2", "u_layer3" };
    for (int i = 0; i < 4; ++i) {
        int unit = kTexUnitTerrainLayer0 + i;
        services->setPixelShaderConstant(kLayerNames[i], &unit, 1);
    }

    // ------------------------------------------------------------------
    // Step 5: Set u_srgbLinear uniform.
    // 1 = apply manual pow(color.rgb, vec3(2.2)) in the fragment shader
    //     (GL_EXT_texture_sRGB absent — textures uploaded as linear).
    // 0 = GPU handles sRGB decode automatically.
    //
    // Passed as int (not bool) — Irrlicht's setPixelShaderConstant reads
    // 4 bytes; bool is 1 byte on most ABIs (UB if passed as bool*).
    // ------------------------------------------------------------------
    bool srgbLinear = m_renderSystem ? !m_renderSystem->isSRGBTextureSupported() : false;
    int srgbLinearInt = srgbLinear ? 1 : 0;
    services->setPixelShaderConstant("u_srgbLinear", &srgbLinearInt, 1);

    // ------------------------------------------------------------------
    // Step 6: Restore GL_ACTIVE_TEXTURE.
    // savedUnit is the GLenum (e.g., GL_TEXTURE0 = 0x84C0) returned by
    // glGetIntegerv(GL_ACTIVE_TEXTURE). Pass directly to glActiveTexture().
    //
    // Per shader-loading.md: Do NOT unbind individual texture units before
    // restoring — Irrlicht's driver state machine tracks active texture
    // units internally. Restoring GL_ACTIVE_TEXTURE is sufficient.
    // ------------------------------------------------------------------
    glActiveTexture(static_cast<GLenum>(savedUnit));
}
