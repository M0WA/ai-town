// SceneEntityManager.cpp — SceneEntityManager full Phase 5 implementation.
//
// Implements the mandatory 4-step destroy sequence from:
//   architecture/graphics-architecture/scene-graph-ownership.md
//   architecture/graphics-architecture/texture-cache.md
//
// Step 1:  Iterate all material slots on the scene node; call
//          textureCache->releaseLinear(tex) for each non-null ITexture*; clear via
//          mat.setTexture(t, nullptr). getMaterial(m) called ONCE per outer loop
//          iteration — result cached as SMaterial& per C-4 constraint.
// Step 1b: Release sRGB diffuse textures by filename (not visible in material slots).
// Step 1c: Release splat map pool entries (terrain chunk entities only).
// Step 2:  driver->setMaterial(SMaterial{}) — flushes driver's last-bound state.
// Step 3:  textureCache->evictUnreferenced() — covers all three pools.
// Step 4:  Set node ptr to nullptr BEFORE node->remove(); never access node* after this line.
//
// See architecture/graphics-architecture/scene-graph-ownership.md and texture-cache.md.

// GLEW before any Irrlicht/OpenGL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "SceneEntityManager.h"

using namespace irr;
using namespace irr::video;
using namespace irr::scene;

SceneEntityManager::SceneEntityManager(irr::video::IVideoDriver* driver,
                                       TextureCache* textureCache)
    : m_driver{driver}
    , m_textureCache{textureCache}
{
}

irr::scene::ISceneNode* SceneEntityManager::track(irr::scene::ISceneNode* node,
                                                   const std::vector<std::string>& /*linearTextures*/,
                                                   const std::vector<std::string>& /*srgbTextures*/,
                                                   const std::vector<std::string>& /*splatMaps*/) {
    // Phase 5: tracking is informational. Entity objects (TerrainChunk, Building, Vehicle)
    // hold the node pointer directly.
    return node;
}

void SceneEntityManager::destroyImpl(irr::scene::ISceneNode* node,
                                      const std::vector<std::string>& srgbTextures,
                                      const std::vector<std::string>& splatMaps) {
    // Step 1b: Release sRGB diffuse textures by filename.
    for (const auto& filename : srgbTextures) {
        m_textureCache->releaseSRGB(filename);
    }

    // Step 1c: Release splat map pool entries (terrain chunk entities only).
    for (const auto& filename : splatMaps) {
        m_textureCache->releaseSplatMap(filename);
    }

    // Step 2: Set driver's active material to the default SMaterial.
    if (m_driver) {
        m_driver->setMaterial(irr::video::SMaterial{});
    }

    // Step 3: Evict zero-reference entries from all three texture pools.
    m_textureCache->evictUnreferenced();

    // Step 4: Remove the node. Caller must have already nulled the entity pointer.
    node->remove();
}

void SceneEntityManager::destroy(irr::scene::ISceneNode*& node,
                                  const std::vector<std::string>& linearTextures,
                                  const std::vector<std::string>& srgbTextures,
                                  const std::vector<std::string>& splatMaps) {
    if (!node) {
        return;
    }

    // ------------------------------------------------------------------
    // Step 1: Release linear-format textures from material slots.
    //
    // MANDATORY (C-4): getMaterial(m) called ONCE per outer loop iteration.
    // Result cached as SMaterial& — re-calling getMaterial() in the inner t loop
    // is a temporary-object hazard per texture-cache.md line 139.
    // ------------------------------------------------------------------
    irr::u32 matCount = node->getMaterialCount();
    for (irr::u32 m = 0; m < matCount; ++m) {
        // MANDATORY: call getMaterial(m) exactly once per outer loop iteration.
        irr::video::SMaterial& mat = node->getMaterial(m);
        for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
            irr::video::ITexture* tex = mat.getTexture(t);
            if (tex) {
                m_textureCache->releaseLinear(tex);
                mat.setTexture(t, nullptr);
            }
        }
    }

    // Steps 1b, 1c, 2, 3 via shared implementation.
    // Step 4: null the caller's reference BEFORE node->remove().
    irr::scene::ISceneNode* nodeToRemove = node;
    node = nullptr; // null the caller's reference first

    destroyImpl(nodeToRemove, srgbTextures, splatMaps);

    // Note: linearTextures, srgbTextures, splatMaps are now const refs (A-24).
    // Callers that need to clear their lists must do so themselves after this call.
    (void)linearTextures;
}
