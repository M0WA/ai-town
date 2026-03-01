// BuildingShaderCallback.cpp — Phase 9 building facade shader callback implementation.
//
// Implements OnSetConstants() for the building facade shader pair.
//
// Binding sequence per architecture/graphics-architecture/shader-loading.md
// §CRITICAL — save and restore GL_ACTIVE_TEXTURE inside OnSetConstants():
//   1. Save GL_ACTIVE_TEXTURE.
//   2. Set u_diffuseMap sampler to kTexUnitDiffuse (unit 0).
//   3. Set u_srgbLinear (int): 1 = apply manual pow(x,2.2), 0 = GPU handles sRGB.
//   4. Restore GL_ACTIVE_TEXTURE.
//
// NOTE: The building atlas raw GLuint is bound by the calling render path BEFORE
// the draw call that triggers OnSetConstants(). This callback only sets the sampler
// uniform index and the gamma-correction flag; it does NOT call glBindTexture itself.
// See architecture/graphics-architecture/texture-cache.md §sRGB texture binding.

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "BuildingShaderCallback.h"
#include "shader_constants.h"

using namespace irr;
using namespace irr::video;

void BuildingShaderCallback::OnSetConstants(IMaterialRendererServices* services,
                                             s32 /*userData*/)
{
    if (!services) return;

    // ------------------------------------------------------------------
    // Step 1: Save GL_ACTIVE_TEXTURE.
    // MANDATORY per shader-loading.md §CRITICAL: Irrlicht tracks active texture unit
    // in m_CurrentTexture. Calling glActiveTexture() without restoring corrupts
    // subsequent Irrlicht draw calls.
    //
    // glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit) returns the GL enum
    // (e.g., 0x84C0 for unit 0) — pass directly to glActiveTexture() to restore.
    // ------------------------------------------------------------------
    GLint savedUnit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit);

    // ------------------------------------------------------------------
    // Step 2: Set u_diffuseMap sampler uniform to kTexUnitDiffuse (unit 0).
    // The building facade atlas is bound to unit 0 by the calling render path.
    // ------------------------------------------------------------------
    int diffuseUnit = kTexUnitDiffuse;
    services->setPixelShaderConstant("u_diffuseMap", &diffuseUnit, 1);

    // ------------------------------------------------------------------
    // Step 3: Set u_srgbLinear uniform.
    // 0 = GPU handles sRGB decode (GL_COMPRESSED_SRGB_S3TC_DXT1_EXT upload path).
    // 1 = apply manual pow(color.rgb, vec3(2.2)) gamma correction in the fragment
    //     shader (GL_COMPRESSED_RGB_S3TC_DXT1_EXT linear fallback path).
    //
    // Passed as int (not bool) — Irrlicht's setPixelShaderConstant reads 4 bytes;
    // bool is 1 byte on most ABIs (UB if passed as bool*).
    // ------------------------------------------------------------------
    int v = m_srgbSupported ? 0 : 1;
    services->setPixelShaderConstant("u_srgbLinear", &v, 1);

    // ------------------------------------------------------------------
    // Step 4: Restore GL_ACTIVE_TEXTURE.
    // savedUnit is the GLenum (e.g., GL_TEXTURE0 = 0x84C0) returned by
    // glGetIntegerv(GL_ACTIVE_TEXTURE). Pass directly to glActiveTexture().
    //
    // Per shader-loading.md: Do NOT unbind individual texture units before
    // restoring — Irrlicht's driver state machine tracks active texture
    // units internally. Restoring GL_ACTIVE_TEXTURE is sufficient.
    // ------------------------------------------------------------------
    glActiveTexture(static_cast<GLenum>(savedUnit));
}
