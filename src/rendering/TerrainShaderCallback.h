#pragma once

// TerrainShaderCallback.h — Phase 5 terrain splat-map shader callback.
//
// Implements IShaderConstantSetCallBack::OnSetConstants() for the terrain
// splat-map shader pair (terrain.vert / terrain.frag).
//
// Responsibilities in OnSetConstants():
//   1. Save GL_ACTIVE_TEXTURE to avoid corrupting Irrlicht's internal state.
//   2. Bind the splat map (raw GLuint, linear RGBA8) to unit 4 (kTexUnitSplatMap).
//   3. Bind four sRGB terrain detail layers (raw GLuint) to units 5–8 (kTexUnitTerrainLayer0+i).
//   4. Set sampler uniforms (u_splatMap, u_layer0–3) to the corresponding unit indices.
//   5. Set u_srgbLinear (int, not bool) from !m_renderer->isSRGBTextureSupported().
//   6. Restore GL_ACTIVE_TEXTURE.
//
// Irrlicht DOES call grab() on IShaderConstantSetCallBack (verified against
// COpenGLSLMaterialRenderer.cpp). Allocation pattern:
//   TerrainShaderCallback* cb = new TerrainShaderCallback(...);
//   gpu->addHighLevelShaderMaterialFromFiles(..., cb, ...);
//   cb->drop();  // Always unconditional — Irrlicht holds its own grab() reference.
//
// Do NOT use std::unique_ptr — causes double-free when Irrlicht drops the callback.
// See architecture/graphics-architecture/shader-loading.md for the full lifetime spec.
//
// GL_ACTIVE_TEXTURE save/restore is MANDATORY per shader-loading.md to prevent
// Irrlicht's internal m_CurrentTexture tracking from being corrupted.

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include <string>
#include <array>

#include "shader_constants.h"

// Forward declarations — avoid pulling in full headers where possible.
class TextureCache;
class RenderSystem;

// TerrainShaderCallback — IShaderConstantSetCallBack for the terrain splat-map shader.
//
// Holds:
//   m_renderSystem — for isSRGBTextureSupported() query (u_srgbLinear uniform)
//   m_textureCache — for getSplatMapGLuint() and getSRGBGLuint() lookups
//   m_splatPath    — filesystem path to the splat map texture
//   m_detailPaths  — 4 filesystem paths to the sRGB terrain detail layers (R/G/B/A)
//
// The callback does NOT own the TextureCache or RenderSystem — both must outlive it.
class TerrainShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    // Constructor.
    // renderSystem:  the production RenderSystem (for isSRGBTextureSupported()).
    //               Must outlive this callback.
    // textureCache:  the production TextureCache (for GLuint lookups).
    //               Must outlive this callback.
    // splatPath:     path key of the splat map in the TextureCache (loaded via loadSplatMap).
    // detailPaths:   4-element array of path keys for the sRGB terrain detail layers,
    //               ordered by splat channel:
    //                 [0] = R channel — base biome layer (grass/sand)
    //                 [1] = G channel — asphalt
    //                 [2] = B channel — soil
    //                 [3] = A channel — concrete
    TerrainShaderCallback(
        RenderSystem*    renderSystem,
        TextureCache*    textureCache,
        const std::string& splatPath,
        const std::array<std::string, 4>& detailPaths)
        : m_renderSystem{renderSystem}
        , m_textureCache{textureCache}
        , m_splatPath{splatPath}
        , m_detailPaths{detailPaths}
    {}

    // OnSetConstants — called by Irrlicht immediately before each draw call that
    // uses this material. Sets all 5 texture unit bindings and the u_srgbLinear uniform.
    //
    // Required binding sequence (per shader-loading.md §Terrain Splat Shader 5-Unit Binding):
    //   Unit 4:  splat map    (kTexUnitSplatMap = 4)
    //   Unit 5:  detail layer 0 / R-channel (kTexUnitTerrainLayer0 + 0)
    //   Unit 6:  detail layer 1 / G-channel (kTexUnitTerrainLayer0 + 1)
    //   Unit 7:  detail layer 2 / B-channel (kTexUnitTerrainLayer0 + 2)
    //   Unit 8:  detail layer 3 / A-channel (kTexUnitTerrainLayer0 + 3)
    //
    // CRITICAL: save and restore GL_ACTIVE_TEXTURE to prevent corrupting Irrlicht's
    //           internal m_CurrentTexture tracking (see shader-loading.md).
    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 /*userData*/) override;

    // OnSetMaterial — optional callback; called when the material is set.
    // No action needed here — all state is set per-draw in OnSetConstants().
    void OnSetMaterial(const irr::video::SMaterial& /*material*/) override {}

private:
    RenderSystem*  m_renderSystem;  // for isSRGBTextureSupported() — not owned
    TextureCache*  m_textureCache;  // for texture handle lookups — not owned
    std::string    m_splatPath;
    std::array<std::string, 4> m_detailPaths;
};
