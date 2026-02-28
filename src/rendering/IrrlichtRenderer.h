#pragma once

#include "src/interfaces/IRenderer.h"  // IRenderer, TextureHandle, CameraParams, TerrainChunkRebuildParams
#include <irrlicht.h>
#include <unordered_map>
#include <cstdint>

// Forward-declare UIManager — must NOT #include "UIManager.h" in this header.
// Violation breaks headless testability: any ui_tests binary that includes UIManager.h
// without linking aitown_render will fail to compile.
// Rule: IrrlichtRenderer.h forward-declares UIManager; IrrlichtRenderer.cpp #includes it.
// Per architecture/graphics-architecture/irrlicht-device-lifecycle.md Header Dependency Rule.
class UIManager;

// IrrlichtRenderer — concrete implementation of IRenderer backed by Irrlicht.
//
// Per-frame sequence enforced by drawScene():
//   1. sceneManager->drawAll()     (3D scene)
//   2. uiManager->draw()           (2D HUD, explicit Z-order per ui-manager.md)
//   NOTE: m_gui->drawAll() is NOT called — calling it would bypass the explicit Z-order
//   layering required for the background scrim and modal overlay.
//   Both calls happen inside the single beginScene/endScene pair.
//
// Constructor signature LOCKED at Phase 1:
//   IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
// Phase 3 wires the real UIManager to this already-existing member — does NOT restructure.
//
// Terrain chunk node tracking (m_chunkNodes):
//   rebuildTerrainChunk() maintains a map from chunkId to the live IMeshSceneNode*.
//   The old node is removed before the new node is created (full node rebuild — never setMesh
//   for terrain LOD transitions, because vertex counts differ between LOD levels).
//   On node removal the scene node is dropped by Irrlicht's scene graph; the map entry is erased.
class IrrlichtRenderer : public IRenderer {
public:
    // LOCKED constructor signature (Phase 1).
    // device must be non-null. uiManager may be null (draws nothing in that case).
    IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager);
    ~IrrlichtRenderer() override = default;

    // Late-bind UIManager (allows construction before UIManager exists).
    void setUIManager(UIManager* uiManager) { m_uiManager = uiManager; }

    // IRenderer interface — main-thread-only
    void          beginFrame() override;  // driver->beginScene(true, true, SColor(255,0,0,0))
    void          drawScene()  override;  // smgr->drawAll() + uiManager->draw() inside begin/end pair
    void          endFrame()   override;  // driver->endScene()
    TextureHandle loadTexture(const std::string& path) override;
    void          setCamera(const CameraParams& p) override;

    // rebuildTerrainChunk() — full LOD node rebuild for a terrain chunk.
    //
    // Step 1: Remove the old scene node for this chunk (if any) — clear all material texture
    //         slots, call driver->setMaterial(SMaterial{}), then node->remove().
    //         In Phase 5 terrain chunks carry no textures (textured terrain is Phase 6+),
    //         so the texture-slot loop is a defensive no-op but is included for correctness.
    //         The node pointer is nulled BEFORE remove() per the dangling-pointer rule in
    //         scene-graph-ownership.md.
    // Step 2: Build a new SMesh* from params.heightmap at params.gridSize resolution
    //         using the same vertex/index layout as TerrainChunk::buildMesh().
    // Step 3: Call recalculateBoundingBox() on every SMeshBuffer, THEN on the SMesh.
    //         MANDATORY — omitting causes silent frustum-culling failure.
    // Step 4: smgr->addMeshSceneNode(smesh) — Irrlicht calls grab() internally.
    //         smesh->drop() — releases the caller's reference; scene node is now sole owner.
    //         Set world position to (params.worldOriginX, 0, params.worldOriginZ).
    // Step 5: Store the new node in m_chunkNodes[params.chunkId].
    //
    // main-thread-only.
    void rebuildTerrainChunk(const TerrainChunkRebuildParams& params) override;

private:
    irr::IrrlichtDevice*        m_device;
    UIManager*                  m_uiManager;
    irr::video::IVideoDriver*   m_driver;
    irr::scene::ISceneManager*  m_smgr;
    irr::scene::ICameraSceneNode* m_camera{nullptr};

    // Texture handle map: TextureHandle → ITexture*
    TextureHandle                                      m_nextHandle{1};
    std::unordered_map<TextureHandle, irr::video::ITexture*> m_textures;

    // Terrain chunk node map: chunkId → live IMeshSceneNode*.
    // Populated by rebuildTerrainChunk() (Step 5) and consumed (Step 1) on subsequent rebuilds.
    // Entries are erased when the old node is removed. The scene node is managed by Irrlicht's
    // scene graph; this map holds a non-owning observing pointer only.
    std::unordered_map<uint64_t, irr::scene::IMeshSceneNode*> m_chunkNodes;
};
