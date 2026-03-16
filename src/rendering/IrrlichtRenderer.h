#pragma once

#include "src/interfaces/IRenderer.h"  // IRenderer, TextureHandle, CameraParams, TerrainChunkRebuildParams, ScreenRect, ServiceBuildingType
#include <irrlicht.h>
#include <unordered_map>
#include <memory>    // std::unique_ptr
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

// Forward-declare BuildingAssetLoader, LODNode, TextureCache, RenderSystem — full includes in IrrlichtRenderer.cpp.
// IrrlichtRenderer owns a BuildingAssetLoader and TextureCache by unique_ptr (Phase 10).
class BuildingAssetLoader;
class LODNode;
class TextureCache;
class RenderSystem;

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

    // setRenderSystem — inject RenderSystem* for terrain shader capabilities queries.
    // Calls initTerrainShader() after storing the pointer.
    // Called from main.cpp after renderer construction (Phase 10c wiring order).
    void setRenderSystem(RenderSystem* rs);

    // terrainMaterialTypeForTest() — test-only accessor for the terrain shader material type.
    // Returns -1 when initTerrainShader() has not run or shader compile failed.
    // Production code must NOT call this method.
    int  terrainMaterialTypeForTest() const { return m_terrainMaterialType; }
    void setTerrainMaterialTypeForTest(int t) { m_terrainMaterialType = t; }

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
    vec3       getListenerPosition() const override;
    void       setTilePlacementPreview(const std::vector<std::pair<int,int>>& tiles,
                                       uint32_t argb) override;

    // Phase 10 — building mesh spawning and road mesh rendering API.
    // Six overrides corresponding to the pure-virtual methods added to IRenderer
    // in Phase 10. Implementations in IrrlichtRenderer.cpp load .b3d assets via
    // BuildingAssetLoader and create/remove scene nodes through SceneEntityManager.
    // All methods are no-op safe (log warning + return) when assetBaseName is empty
    // or the asset file is missing — callers must not assume the node was created.
    // main-thread-only.
    void placeBuildingMesh(int tileX, int tileZ,
                           const std::string& assetBaseName) override;
    void removeBuildingMesh(int tileX, int tileZ) override;
    // Road mesh: no assetBaseName — road tiles get per-tile terrain-conforming geometry.
    void placeRoadMesh(int tileX, int tileZ) override;
    void removeRoadMesh(int tileX, int tileZ) override;
    // Service building mesh: identified by ServiceBuildingType enum, not a string.
    void placeServiceBuildingMesh(int tileX, int tileZ,
                                  ServiceBuildingType type) override;
    void removeServiceBuildingMesh(int tileX, int tileZ) override;

    // Phase 10 — Vehicle rendering API.
    // Implementations are in IrrlichtRenderer.cpp.
    // placeVehicle removes any existing node for vehicleId before creating the new one.
    // moveVehicle delegates to placeVehicle when vehicleId is unknown.
    // removeVehicle runs the full eviction sequence and deletes the LODNode wrapper.
    void placeVehicle(uint32_t vehicleId,
                      const std::string& assetName,
                      float worldX, float worldY, float worldZ,
                      float yawDegrees) override;
    void moveVehicle(uint32_t vehicleId,
                     float worldX, float worldY, float worldZ,
                     float yawDegrees) override;
    void removeVehicle(uint32_t vehicleId) override;

    // initCloudPlane() — build the scrolling cloud plane mesh and scene node.
    // Called once from the constructor after other initialization.
    // Guarded by m_driverType == EDT_NULL: returns immediately in headless mode,
    // leaving m_cloudNode null. UV scrolling in update() guards with if(m_cloudNode).
    void initCloudPlane();

    // update() — per-frame animation update (UV scrolling for cloud plane).
    // Called from main.cpp frame loop before beginFrame().
    void update(float dt);

    // cloudNodeForTest() — test-only accessor for the cloud plane scene node.
    // Returns nullptr when constructed with EDT_NULL (headless guard triggered).
    // Intended for use by cloud_plane_test.cpp in opengl_tests.
    // Production code must NOT call this method.
    irr::scene::IMeshSceneNode* cloudNodeForTest() const { return m_cloudNode; }

private:
    irr::IrrlichtDevice*        m_device;
    UIManager*                  m_uiManager;
    irr::video::IVideoDriver*   m_driver;
    irr::scene::ISceneManager*  m_smgr;
    irr::scene::ICameraSceneNode* m_camera{nullptr};

    // --- Phase 10b: sky cloud plane ---
    // m_driverType: captured from the live device in the constructor body.
    //   Used to guard initCloudPlane() against headless (EDT_NULL) contexts.
    // m_cloudNode: scene node for the scrolling cloud quad; null under EDT_NULL.
    // m_cloudUVOffset: accumulated UV translation, wrapped to [0,1) via fmod.
    // m_cloudShaderCbRaw: caller's reference to the CloudDomeShaderCallback (defined
    //   in IrrlichtRenderer.cpp only).  Stored as void* to avoid pulling the class
    //   definition into the header.  Cast to CloudDomeShaderCallback* at all use sites
    //   inside the .cpp.  Null when the shader compile failed or under EDT_NULL.
    //   Dropped in the destructor via ->drop().
    irr::video::E_DRIVER_TYPE       m_driverType{irr::video::EDT_NULL};
    irr::scene::IMeshSceneNode*     m_cloudNode{nullptr};
    irr::core::vector2df            m_cloudUVOffset{0.f, 0.f};
    void*                           m_cloudShaderCbRaw{nullptr};

    // Cached camera eye position — updated every setCamera() call.
    // Returned by getListenerPosition() for use by CitySimulation's
    // sfx_intersection_tick 80 m pre-acquisition distance cull.
    vec3 m_lastCameraPosition{};

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

    // --- Phase 10: multi-tile placement preview (Zone rect / Road line) ---
    // m_previewMesh:    rebuilt on every setTilePlacementPreview() call with N quads.
    //                   null until the first non-empty call; dropped in destructor.
    // m_previewVisible: true when preview tiles are set; false after clear (empty tiles).
    //                   Checked in drawScene() before issuing drawMeshBuffer() calls.
    // Ownership: m_previewMesh is dropped (->drop()) only when replaced or in the destructor.
    irr::scene::SMesh* m_previewMesh{nullptr};
    bool               m_previewVisible{false};

    // --- Phase 9b: zone overlay ---
    // m_overlayNode: persistent scene node for the zone colour overlay.
    // Rebuilt by setZoneOverlay(); old node is removed before new mesh is attached.
    // null until the first non-empty setZoneOverlay() call.
    irr::scene::IMeshSceneNode* m_overlayNode{nullptr};

    // --- Phase 10: building and road scene node registries ---
    //
    // Key: tileZ * kMaxMapTiles + tileX (same encoding used for the zone overlay sparse map).
    // Value: owning LODNode* — IrrlichtRenderer is the sole owner; each LODNode wraps an
    //        IMeshSceneNode* that is owned by the Irrlicht scene graph.
    //
    // Invariant: every entry in these maps corresponds to a live scene node.
    // On removal the LODNode is deleted (which does NOT remove the scene node — the caller
    // must invoke the eviction sequence on the scene node separately before deleting the LODNode).
    //
    // kMaxMapTiles: upper bound on tile index per axis used as key multiplier.
    // Using 4096 accommodates any V1 map dimension (max 1024) with safe headroom.
    static constexpr int kMaxMapTiles = 4096;

    // kTileSize: world-space side length of one tile in metres.
    // Must match the tile dimensions defined in TerrainSystem / SimulationConstants.
    // Used to convert (tileX, tileZ) → world position (tileX * kTileSize, 0, tileZ * kTileSize).
    // Value: 10.0f metres per tile (matches physics tile grid in architecture specs).
    static constexpr float kTileSize = 10.0f;

    // Tile key helper — produces a stable uint64_t key from (tileX, tileZ).
    static uint64_t tileKey(int tileX, int tileZ) noexcept {
        return static_cast<uint64_t>(tileZ) * kMaxMapTiles +
               static_cast<uint64_t>(tileX);
    }

    // Building scene node registry (zone buildings + service buildings share one map).
    // All placed buildings (zone and service) are keyed by tile. A tile can hold at most
    // one building at a time — the simulation enforces the one-building-per-tile invariant.
    std::unordered_map<uint64_t, LODNode*> m_buildingNodes;

    // Road tile scene node registry.
    std::unordered_map<uint64_t, LODNode*> m_roadNodes;

    // BuildingAssetLoader — created lazily (on first placeBuildingMesh call) to avoid
    // constructing when m_smgr is null (e.g. unit tests with null device).
    // Owned exclusively by IrrlichtRenderer; destroyed in the destructor.
    std::unique_ptr<BuildingAssetLoader> m_buildingAssetLoader;

    // --- Phase 10: vehicle scene node registry ---
    //
    // Key: vehicleId (uint32_t cast to uint64_t) — stable for vehicle lifetime.
    // Value: owning LODNode* — IrrlichtRenderer is the sole owner.
    //
    // Invariant: every entry corresponds to a live scene node.
    // On removal the eviction sequence is run (clear textures → setMaterial →
    // node->remove()) before the LODNode wrapper is deleted.
    std::unordered_map<uint32_t, LODNode*> m_vehicleNodes;

    // m_vehicleAssetLoader — separate BuildingAssetLoader instance for vehicle
    // assets (different atlas path: vehicles_diffuse_atlas_d.dds).
    // Created lazily on first placeVehicle() call.
    std::unique_ptr<BuildingAssetLoader> m_vehicleAssetLoader;

    // ensureVehicleLoader — lazily construct m_vehicleAssetLoader.
    // Returns true if the loader is ready; false if m_smgr is null.
    bool ensureVehicleLoader();

    // destroyVehicleNode — eviction sequence + LODNode deletion for one vehicle
    // registry entry.  No-op if vehicleId is not in m_vehicleNodes.
    void destroyVehicleNode(uint32_t vehicleId);

    // --- Placeholder zone textures ---
    // Solid-color 4×4 textures created programmatically so buildings render with a
    // recognisable zone colour even when placeholder B3D assets have no UV data.
    // Key: zone prefix string ("res_", "com_", "ind_", "svc_", "default_").
    // Value: ITexture* owned by the Irrlicht driver texture cache (no drop needed here).
    std::unordered_map<std::string, irr::video::ITexture*> m_zoneTextures;

    // getOrCreateZoneTexture — returns (creating on demand) the solid-colour placeholder
    // texture for the given zone prefix.  Returns nullptr if m_driver is null.
    irr::video::ITexture* getOrCreateZoneTexture(const std::string& prefix);

    // --- Phase 10: procedural road mesh ---
    //
    // Road tiles are generated in C++ at runtime — no .b3d files on disk.
    // LOD0 mesh is now built per-tile (terrain-conforming geometry).
    // LOD1/LOD2 meshes remain shared (flat quads used at distance).
    //
    // m_roadTextureCache: lazily created on first placeRoadMesh() call.
    //   Owns the sRGB pool entry for road_asphalt_tileable.dds.
    std::unique_ptr<TextureCache> m_roadTextureCache;

    // m_roadMaterialType: s32 material type index returned by
    //   addHighLevelShaderMaterialFromFiles(road.vert, road.frag).
    //   -1 = not yet loaded or shader compile failed (falls back to EMT_SOLID).
    irr::s32 m_roadMaterialType{-1};

    // m_roadDiffuseTexGLuint: raw GL handle for road_asphalt_tileable.dds.
    //   0 = not loaded (e.g., EDT_NULL headless context).
    //   Bound to GL_TEXTURE0 inside RoadShaderCallback::OnSetConstants().
    unsigned int m_roadDiffuseTexGLuint{0};

    // Shared procedural road tile meshes — built once, reused for LOD1/LOD2 only.
    //   m_sharedRoadMeshLOD0: retained for possible future use (set nullptr; unused).
    //   m_sharedRoadMeshLOD1: flat quad only (2 tris, road shader) — LOD1 fallback.
    //   m_sharedRoadMeshLOD2: flat colored quad (2 tris, EMT_SOLID, road_lod2_color).
    // LOD0 is now per-tile (buildTileRoadMesh); m_sharedRoadMeshLOD0 is always nullptr.
    // Ownership: IrrlichtRenderer holds ref_count == 1; dropped in destructor.
    // Scene nodes from addMeshSceneNode() grab an additional ref (ref_count == 2)
    // and release it when the node is removed (back to 1).
    irr::scene::SMesh* m_sharedRoadMeshLOD0{nullptr};
    irr::scene::SMesh* m_sharedRoadMeshLOD1{nullptr};
    irr::scene::SMesh* m_sharedRoadMeshLOD2{nullptr};

    // --- Phase 10c: terrain texture wiring ---
    //
    // m_renderSystem: non-owning pointer to RenderSystem; set via setRenderSystem().
    //   Used by TerrainShaderCallback for isSRGBTextureSupported() query.
    // m_terrainTextureCache: lazily created in initTerrainShader(); owns sRGB pool
    //   entries for the 4 diffuse terrain layers and the splat map.
    // m_terrainMaterialType: s32 material type index returned by
    //   addHighLevelShaderMaterialFromFiles(terrain.vert, terrain.frag).
    //   -1 = not yet loaded or shader compile failed (falls back to default Irrlicht material).
    RenderSystem*                       m_renderSystem{nullptr};
    std::unique_ptr<TextureCache>       m_terrainTextureCache;
    int                                 m_terrainMaterialType{-1};

    // initTerrainShader() — load terrain splat shader + diffuse textures + splat map.
    // Called from setRenderSystem() after m_renderSystem is assigned.
    // EDT_NULL guard: returns immediately in headless mode.
    void initTerrainShader();

    // initRoadShader() — load road shader + diffuse texture (idempotent).
    // Returns true when ready; false if m_driver is null or shader load fails
    // (road tiles still render via EMT_SOLID fallback).
    bool initRoadShader();

    // ensureRoadMeshes() — build shared LOD1/LOD2 meshes (idempotent).
    // Must be called after initRoadShader() so material type is available.
    // LOD0 is not built here — it is built per-tile via buildTileRoadMesh().
    void ensureRoadMeshes();

    // buildTileRoadMesh — build a per-tile terrain-conforming LOD0 road mesh.
    //
    // Constructs a new SMesh* with the road quad and kerb strips shaped to the
    // actual terrain corner heights (h00, h10, h01, h11) at the 4 tile corners.
    // Vertices are in world-space Y (h00/h10/h01/h11 are absolute world heights).
    // The node must be placed at world position (worldX, 0, worldZ) with no Y offset;
    // all Y displacement is baked directly into the vertex positions.
    // The caller is responsible for calling drop() after addMeshSceneNode().
    // Returns nullptr if m_driver is null (headless context).
    irr::scene::SMesh* buildTileRoadMesh(float h00, float h10,
                                          float h01, float h11) const;

    // placeRoadMesh (internal extended) — core implementation called by both the
    // public IRenderer override and recursive neighbor rebuild calls.
    //
    // flattenTerrain: if true, run the conditional slope-clamping setTileHeight()
    //   sequence and flush terrain rebuilds before building the mesh.
    //   Pass false on recursive neighbor calls (terrain is already correct).
    // rebuildNeighbors: if true, after placing this tile rebuild all cardinal road
    //   neighbors (mesh only, no re-flattening).
    //   Pass false on recursive calls to prevent infinite recursion.
    void placeRoadMesh(int tileX, int tileZ,
                       bool flattenTerrain, bool rebuildNeighbors);

    // Helper: destroy a LODNode entry in a tile-keyed registry.
    // Executes the full eviction sequence on the wrapped scene node:
    //   clear material texture slots → driver->setMaterial(SMaterial{}) → node->remove()
    // then deletes the LODNode wrapper. Erases the map entry.
    // No-op if the key is not in the map.
    void destroyTileNode(std::unordered_map<uint64_t, LODNode*>& registry,
                         int tileX, int tileZ);

    // Helper: ensure m_buildingAssetLoader is created (idempotent).
    // Returns false and logs a warning if m_smgr is null (test/headless context).
    bool ensureAssetLoader();

    // drawScene sub-step profiling accumulators (µs)
    double m_prof3D{0}, m_profHover{0}, m_profUIDraw{0}, m_profGUI{0};
    int    m_profFrames{0};
};
