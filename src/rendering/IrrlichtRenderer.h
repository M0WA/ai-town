#pragma once

#include "src/interfaces/IRenderer.h"  // IRenderer, TextureHandle, CameraParams, TerrainChunkRebuildParams, ScreenRect
#include <irrlicht.h>
#include <unordered_map>
#include <cstdint>

// Forward-declare ITerrainQuery — full include is in IrrlichtRenderer.cpp only,
// consistent with the "Irrlicht-free nature of IRenderer.h" principle.
// IrrlichtRenderer stores m_terrain as ITerrainQuery* (non-owning observer pointer)
// and calls only ITerrainQuery interface methods — no concrete TerrainSystem API
// is referenced inside IrrlichtRenderer.h.
class ITerrainQuery;

// Forward-declare UIManager — must NOT #include "UIManager.h" in this header.
// Violation breaks headless testability: any ui_tests binary that includes UIManager.h
// without linking aitown_render will fail to compile.
// Rule: IrrlichtRenderer.h forward-declares UIManager; IrrlichtRenderer.cpp #includes it.
// Per architecture/graphics-architecture/irrlicht-device-lifecycle.md Header Dependency Rule.
class UIManager;

// IrrlichtRenderer — concrete implementation of IRenderer backed by Irrlicht.
//
// Per-frame sequence enforced by drawScene():
//   1. sceneManager->drawAll()       (3D scene)
//   2. uiManager->draw()             (update panel element states: visibility, text, alpha)
//   3. guiEnvironment->drawAll()     (render all visible GUI elements)
//   Step 2 manages Z-order via visibility toggling (non-active panels hide their elements).
//   Step 3 then paints only what is visible.
//   All three calls happen inside the single beginScene/endScene pair.
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
    ~IrrlichtRenderer() override;

    // Late-bind UIManager (allows construction before UIManager exists).
    void setUIManager(UIManager* uiManager) { m_uiManager = uiManager; }

    // Phase 9b late-bind setters — called from main.cpp after terrain generation.
    // NOT on the IRenderer interface (same rationale as above: one-time initialization
    // setters, not general renderer capabilities).

    // setTerrainQuery — inject the ITerrainQuery* used by pickTerrainTile(),
    // setTileHoverHighlight(), and setZoneOverlay() for heightmap sampling.
    // Called from main.cpp step 2 of Phase 9b wiring order (Deliverable H).
    void setTerrainQuery(ITerrainQuery* terrain) { m_terrain = terrain; }

    // setCellSize — supply the world-space width of one tile in metres (e.g. 10.0f).
    // Obtained from TerrainSystem::getCellSize() (Deliverable E.1) and called from
    // main.cpp step 2a of Phase 9b wiring order.  Used by pickTerrainTile() for
    // the ray-march step size and tile-index conversion.
    void setCellSize(float cellSize) { m_cellSize = cellSize; }

    // setRendererMapDimensions — supply the map width and depth in tiles so that
    // pickTerrainTile() and setZoneOverlay() can clamp tile indices to valid bounds.
    // Called from main.cpp step (2b) of Phase 9b wiring order (Deliverable H), after
    // step (2a) setCellSize().
    // (ref: architecture/graphics-architecture/procedural-terrain.md — pickTerrainTile
    //  DDA Algorithm — IrrlichtRenderer members table)
    void setRendererMapDimensions(int mapTilesX, int mapTilesZ) {
        m_mapTilesX = mapTilesX;
        m_mapTilesZ = mapTilesZ;
    }

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

    // Phase 9b — IRenderer world-interaction methods.
    bool      pickTerrainTile(int screenX, int screenY,
                               int& tileX, int& tileZ) const override;
    void      setTileHoverHighlight(int tileX, int tileZ, uint32_t argb) override;
    void      setZoneOverlay(int mapTilesX, int mapTilesZ,
                             const std::unordered_map<uint64_t, uint32_t>& sparseOverlay) override;
    ScreenRect getTileScreenBounds(int tileX, int tileZ) const override;

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

    // --- Phase 9b: terrain query and map geometry ---
    // m_terrain: non-owning pointer to ITerrainQuery; set via setTerrainQuery().
    // m_cellSize: world-space width of one tile in metres; set via setCellSize().
    // m_mapTilesX/Z: map dimensions in tiles; set via setMapTileCount().
    // All three default to null/0/0 and are guarded at use sites.
    ITerrainQuery* m_terrain{nullptr};
    float          m_cellSize{1.0f};
    int            m_mapTilesX{0};
    int            m_mapTilesZ{0};

    // --- Phase 9b: hover tile highlight ---
    // m_hoveredTileMesh: allocated ONCE in constructor (never null during gameplay).
    // m_hoverBuffer:     the single SMeshBuffer inside m_hoveredTileMesh; pre-populated
    //                    in constructor with 4 vertices + 6 indices.  Updated in-place by
    //                    setTileHoverHighlight() — no per-event allocation/deallocation.
    // m_hoverVisible:    true when a valid tile is highlighted; false on clear request.
    //                    Checked in drawScene() before issuing the drawMeshBuffer() call.
    // Ownership: m_hoveredTileMesh is dropped (->drop()) only in the IrrlichtRenderer destructor.
    irr::scene::SMesh*       m_hoveredTileMesh{nullptr};
    irr::scene::SMeshBuffer* m_hoverBuffer{nullptr};  // non-owning after addMeshBuffer+drop
    bool                     m_hoverVisible{false};

    // --- Phase 9b: zone overlay ---
    // m_overlayNode: persistent scene node for the zone colour overlay.
    // Rebuilt by setZoneOverlay(); old node is removed before new mesh is attached.
    // null until the first non-empty setZoneOverlay() call.
    irr::scene::IMeshSceneNode* m_overlayNode{nullptr};
};
