#pragma once

// SceneEntityManager.h — SceneEntityManager is the authoritative owner of all scene nodes.
//
// It is the sole caller of addXxxSceneNode() and node->remove(). No code outside
// SceneEntityManager calls grab() on scene nodes or calls node->remove() directly.
//
// destroy() implements the mandatory 4-step eviction sequence from:
//   architecture/graphics-architecture/scene-graph-ownership.md
//   architecture/graphics-architecture/texture-cache.md
//
// Step 1:  Iterate all material slots on the scene node; call textureCache->releaseLinear(tex)
//          for each non-null ITexture*; clear the slot via mat.setTexture(t, nullptr).
//          getMaterial(m) is called ONCE per outer m loop — result cached as SMaterial&.
// Step 1b: Release sRGB diffuse textures by filename (not reachable via material slot iteration).
// Step 1c: Release splat map pool entries (terrain chunk entities only).
// Step 2:  driver->setMaterial(SMaterial{}) — flushes the driver's last-bound state.
// Step 3:  textureCache->evictUnreferenced() — covers all three pools.
// Step 4:  Set node pointer to nullptr BEFORE node->remove(); do NOT access node* after this line.
//
// MANDATORY IMPLEMENTATION CONSTRAINT (C-4): getMaterial(m) MUST be called exactly once
// per outer m loop iteration and cached as SMaterial& mat. The inner t loop calls
// mat.setTexture(t, nullptr) WITHOUT re-calling getMaterial(). Re-calling getMaterial()
// inside the inner t loop introduces a temporary-object hazard per texture-cache.md line 139.
//
// The template destroy<TEntity>(entity) overload is provided so that test entity types
// (e.g., TestEntity structs with getNode()/setNode()/getSRGBTextureFilenames()/getSplatMapFilenames())
// can use the same destroy() sequence without requiring them to inherit from a base class.
//
// See architecture/graphics-architecture/scene-graph-ownership.md for full ownership policy.

// GLEW before any Irrlicht/OpenGL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>
#include <string>
#include <vector>
#include "TextureCache.h"

// NOTE: using namespace irr intentionally removed from header (A-21).
// All irr names in this header use fully-qualified names to avoid polluting
// every translation unit that includes SceneEntityManager.h.

// SceneEntityManager — authoritative owner of all scene nodes.
// Sole caller of addXxxSceneNode() and node->remove().
// See architecture/graphics-architecture/scene-graph-ownership.md
class SceneEntityManager {
public:
    // Constructor: driver and textureCache must outlive this object.
    // driver is used in Step 2 (setMaterial) of the destroy sequence.
    // textureCache is used in Steps 1, 1b, 1c, and 3.
    SceneEntityManager(irr::video::IVideoDriver* driver, TextureCache* textureCache);

    // Non-copyable / non-movable — manages non-copyable GL and scene graph resources.
    SceneEntityManager(const SceneEntityManager&)            = delete;
    SceneEntityManager& operator=(const SceneEntityManager&) = delete;
    SceneEntityManager(SceneEntityManager&&)                 = delete;
    SceneEntityManager& operator=(SceneEntityManager&&)      = delete;

    // track() — register a scene node after creation via addXxxSceneNode().
    // Returns the same node pointer for convenience (enables chained calls).
    irr::scene::ISceneNode* track(irr::scene::ISceneNode* node,
                                  const std::vector<std::string>& linearTextures,
                                  const std::vector<std::string>& srgbTextures,
                                  const std::vector<std::string>& splatMaps);

    // destroy() — execute the full 4-step eviction sequence on a scene node.
    // Raw-pointer overload: caller passes node ref + texture filename vectors directly.
    //
    // Parameters:
    //   node          — non-const reference; set to nullptr in Step 4 before remove().
    //   linearTextures — filenames for linear-pool textures to release in Step 1.
    //   srgbTextures  — filenames for sRGB pool textures to release in Step 1b.
    //   splatMaps     — filenames for splat map pool textures to release in Step 1c.
    //
    // After return: node == nullptr; all texture ref_counts decremented; evictUnreferenced() called.
    void destroy(irr::scene::ISceneNode*& node,
                 const std::vector<std::string>& linearTextures,
                 const std::vector<std::string>& srgbTextures,
                 const std::vector<std::string>& splatMaps);

    // destroy<TEntity>(entity) — template overload for entity objects.
    // TEntity must have:
    //   scene::ISceneNode* getNode() const
    //   void setNode(scene::ISceneNode*)
    //   const std::vector<std::string>& getSRGBTextureFilenames() const
    //   const std::vector<std::string>& getSplatMapFilenames() const
    //
    // This overload is used by test entity types (TestEntity structs) and production
    // entity classes (Building, Vehicle, TerrainChunk) without requiring inheritance.
    template<typename TEntity>
    void destroy(TEntity& entity) {
        irr::scene::ISceneNode* node = entity.getNode();
        if (!node) return;

        // ------------------------------------------------------------------
        // Step 1: Release linear-format textures from material slots.
        //
        // MANDATORY (C-4): getMaterial(m) called ONCE per outer loop iteration.
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

        // ------------------------------------------------------------------
        // Step 1b: Release sRGB diffuse textures by filename.
        // ------------------------------------------------------------------
        for (const auto& filename : entity.getSRGBTextureFilenames()) {
            m_textureCache->releaseSRGB(filename);
        }

        // ------------------------------------------------------------------
        // Step 1c: Release splat map pool entries (terrain chunk entities only).
        // ------------------------------------------------------------------
        for (const auto& filename : entity.getSplatMapFilenames()) {
            m_textureCache->releaseSplatMap(filename);
        }

        // ------------------------------------------------------------------
        // Step 2: Set driver's active material to the default SMaterial.
        // ------------------------------------------------------------------
        if (m_driver) {
            m_driver->setMaterial(irr::video::SMaterial{});
        }

        // ------------------------------------------------------------------
        // Step 3: Evict zero-reference entries from all three texture pools.
        // ------------------------------------------------------------------
        m_textureCache->evictUnreferenced();

        // ------------------------------------------------------------------
        // Step 4: Null the entity's node pointer BEFORE calling node->remove().
        // Dangling pointer prevention per scene-graph-ownership.md.
        // ------------------------------------------------------------------
        entity.setNode(nullptr);
        node->remove(); // scene node may be destroyed here; do NOT access node* after this
    }

private:
    irr::video::IVideoDriver* m_driver;
    TextureCache*             m_textureCache;

    // Internal implementation of the 4-step destroy sequence used by the raw-pointer overload.
    void destroyImpl(irr::scene::ISceneNode* node,
                     const std::vector<std::string>& srgbTextures,
                     const std::vector<std::string>& splatMaps);
};
