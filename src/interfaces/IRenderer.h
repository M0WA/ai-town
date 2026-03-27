#pragma once

#include "vec3.h"
#include "simulation_types.h"  // ServiceBuildingType — for placeServiceBuildingMesh()
#include <string>
#include <cstdint>
#include <unordered_map>
#include <utility>   // std::pair — used by setTilePlacementPreview tile list
#include <vector>

// Opaque texture handle — uint32_t alias instead of ITexture* so that IRenderer.h
// severs the compile-time dependency on Irrlicht headers for all consumers.
// The concrete IrrlichtRenderer maintains std::unordered_map<TextureHandle, ITexture*> internally.
using TextureHandle = uint32_t;
static constexpr TextureHandle kInvalidTexture = 0;

// ToolMode — mirrors ActiveTool (ui_types.h) but defined here so IRenderer.h
// does not depend on src/ui/ headers. UIManager casts ActiveTool → ToolMode.
// Values MUST match the order of ActiveTool in ui_types.h exactly.
enum class ToolMode { None, Zone, Road, Utilities, Demolish, Query };

// CameraParams — passed to IRenderer::setCamera() each frame.
// Defined in IRenderer.h (alongside IRenderer) since it is only used as a parameter to IRenderer.
// Not shared with IAudioSystem — that interface uses CameraState (position/forward/up vectors)
// for 3D spatial audio listener placement, which differs from the renderer's FOV/clip-plane needs.
// IRenderer.h must NOT include audio_types.h — doing so leaks CameraState, SoundPriority,
// StingerType, and other audio types into every render-interface consumer.
struct CameraParams {
    vec3  position{};          // world-space camera eye position
    vec3  target{};            // world-space look-at target (NOT a direction vector)
    float fovDegrees{45.0f};   // horizontal field of view in degrees
    float nearClip{0.1f};      // near clip plane distance in metres
    float farClip{15000.0f};   // far clip plane distance in metres — must exceed cloud dome radius
                               // (kCloudDomeRadius=6000 m; dome vertices at ~6082 m from camera).
                               // With near=0.1 the depth ratio is 150000; roads use polygon offset
                               // (EPO_FRONT, factor=1) so z-fighting is handled. farClip=3000 caused
                               // OpenGL to hard-clip dome triangles beyond 3000 m, producing a visible
                               // circular arc ring at the frustum boundary.
};

// TerrainChunkRebuildParams — all data needed by IRenderer::rebuildTerrainChunk().
//
// IRenderer receives this struct so that TerrainSystem remains free of Irrlicht headers:
// TerrainSystem calls rebuildTerrainChunk() through the IRenderer interface without
// importing any irrlicht.h symbols.  The concrete IrrlichtRenderer translates these
// plain data fields into SMesh/IMeshSceneNode operations.
//
// heightmap   — vertex height values for the new LOD level.
//               Row-major: index = z * (gridSize+1) + x.
//               Must contain exactly (gridSize+1)*(gridSize+1) elements.
// gridSize    — quad-cell count per side for the target LOD
//               (kTerrainLOD0GridSize=32, LOD1=16, LOD2=8).
// cellSize    — world-space width/depth of each quad cell in metres.
// worldOriginX, worldOriginZ — world-space position of the chunk's (0,0) vertex corner.
//               Used to set the scene node's world translation so the rebuilt mesh
//               occupies the same footprint regardless of LOD grid size.
// chunkId     — opaque 64-bit identifier; IrrlichtRenderer uses it as the key into its
//               internal chunk node map to locate and remove the old scene node.
struct TerrainChunkRebuildParams {
    std::vector<float> heightmap;   // (gridSize+1)^2 vertex heights
    int                gridSize{0}; // quad cells per side (32, 16, or 8)
    float              cellSize{1.0f};
    float              worldOriginX{0.0f};
    float              worldOriginZ{0.0f};
    uint64_t           chunkId{0};
};

// ScreenRect — plain-old-data screen bounding rectangle, in physical pixels.
// Defined here (alongside IRenderer) to keep this header Irrlicht-free: callers of
// getTileScreenBounds() and computePanelPosition() use ScreenRect without importing
// any Irrlicht headers.  Consistent with the "Irrlicht-free nature of IRenderer.h"
// principle (Phase 9b Deliverable B).
// Do NOT use irr::core::rect<irr::s32> at any call site that crosses the IRenderer boundary.
struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; };

// IRenderer — render interface.
// main-thread-only: all methods must be called from the main/render thread.
// Uses opaque TextureHandle (uint32_t) instead of ITexture* — the same pattern as
// IUIBackend with UIElementHandle. MockRenderer::loadTexture() returns an incrementing
// non-zero integer.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void          beginFrame() = 0;              // main-thread-only
    virtual void          endFrame() = 0;                // main-thread-only
    virtual void          drawScene() = 0;               // main-thread-only
    virtual TextureHandle loadTexture(const std::string& path) = 0;  // main-thread-only; returns kInvalidTexture on failure
    virtual void          setCamera(const CameraParams& p) = 0;       // main-thread-only

    // rebuildTerrainChunk() — full LOD node rebuild for a terrain chunk.
    //
    // Implements the 5-step terrain LOD rebuild sequence per
    // architecture/graphics-architecture/procedural-terrain.md and
    // architecture/graphics-architecture/scene-graph-ownership.md:
    //
    //   Step 1: Destroy the old scene node (if any) via the eviction sequence:
    //           clear material texture slots, driver->setMaterial(SMaterial{}),
    //           evictUnreferenced(), then node->remove().
    //   Step 2: Build a new SMesh* at the target LOD grid size from params.heightmap.
    //   Step 3: Call recalculateBoundingBox() on every SMeshBuffer AND the SMesh.
    //           MANDATORY — omitting this leaves a degenerate bounding box that
    //           breaks frustum culling silently.
    //   Step 4: smgr->addMeshSceneNode(smesh), then smesh->drop() to transfer ownership.
    //   Step 5: Register the new node in the renderer's internal chunk node map (keyed by chunkId).
    //
    // Called by TerrainSystem::processOneRebuild() as the single render-layer call site.
    // The IRenderer interface severs the compile-time Irrlicht dependency from TerrainSystem.
    // Implementations in test doubles (MockRenderer) are no-ops or mock expectations.
    // main-thread-only.
    virtual void rebuildTerrainChunk(const TerrainChunkRebuildParams& params) = 0;

    // pickTerrainTile — screen-to-world ray-cast returning the terrain tile under the cursor.
    //
    // Uses the O(1) DDA (Digital Differential Analyzer) grid-traversal algorithm
    // (Amanatides & Woo 1987) to find the first terrain tile whose heightmap sample is at or
    // above the ray Y position.  Traverses at most (mapTilesX + mapTilesZ) cells regardless
    // of map size.  Worst-case cost on a 1024×1024 map: ~30 µs.
    //
    // Mandated by the Phase 9b blocking spike (2026-03-02): a naive 4096-step linear march
    // costs ~205 µs per call; at 10 MouseMove events/frame at 60 FPS this exceeds the 1 ms
    // world-interaction budget.  The DDA reduces sustained cost to ~300 µs (within budget).
    //
    // See architecture/graphics-architecture/procedural-terrain.md — "pickTerrainTile DDA
    // Algorithm" for the full normative algorithm with reference implementation.
    //
    // Returns true and sets tileX/tileZ if the ray intersects the terrain.
    // Returns false if the ray misses the terrain (sky, off-map) or m_terrain is null.
    //
    // screenX/screenY are physical screen-space pixels (not virtual 1920x1080 space).
    // Callers must un-project via UIScaler first if they have virtual coordinates.
    // main-thread-only.
    virtual bool pickTerrainTile(int screenX, int screenY,
                                 int& tileX, int& tileZ) const = 0;

    // setTileHoverHighlight — render a highlight quad over the hovered tile or footprint.
    //
    // footprintSize: 1 for 1×1 tile, 2 for 2×2, 3 for 3×3. Default=1 (backward compatible).
    // Highlight color is determined by the renderer based on the current active tool:
    //   Zone: semi-transparent green (0x6600FF00)
    //   Demolish pending: semi-transparent red (0x66FF0000)
    //   Other: semi-transparent white
    // Pass tileX = -1 to clear the highlight.
    // main-thread-only.
    virtual void setTileHoverHighlight(int tileX, int tileZ, int footprintSize = 1) = 0;

    // setActiveTool — inform the renderer which tool is currently active.
    // Used by IrrlichtRenderer to select the hover-highlight color in setTileHoverHighlight().
    // Called by UIManager whenever m_activeTool changes.
    // main-thread-only.
    virtual void setActiveTool(ToolMode mode) = 0;

    // setZoneHoverColour — set the ARGB colour used for zone-tool hover highlights.
    // Called by UIManager whenever the selected zone type changes or the Zone tool is activated.
    // Allows the hover preview to reflect the current zone type (green=Residential,
    // blue=Commercial, yellow=Industrial).  Default no-op for implementations that
    // do not support per-zone hover colours.
    // main-thread-only.
    virtual void setZoneHoverColour(unsigned int argb) {}

    // clearDemolishHighlight — clear any pending demolition highlight.
    // Called by UIManager when the demolition confirmation modal is cancelled or dismissed.
    // Equivalent to setTileHoverHighlight(-1, -1) but semantically explicit.
    // main-thread-only.
    virtual void clearDemolishHighlight() = 0;

    // setZoneOverlay — update the semi-transparent zone-colour overlay mesh.
    //
    // sparseOverlay maps (tileZ * mapTilesX + tileX) -> ARGB colour for every tile that
    // has a non-zero overlay.  Tiles absent from the map have no overlay rendered.
    // Colour convention (ARGB 0xAARRGGBB, alpha 0x60 ≈ 38%):
    //   Residential  0x6000FF00  (green)
    //   Commercial   0x600000FF  (blue)
    //   Industrial   0x60FFFF00  (yellow)
    // The overlay mesh is attached to the Irrlicht scene graph as a persistent
    // IMeshSceneNode* (m_overlayNode) and rendered automatically each frame during
    // sceneManager->drawAll() in the transparent-node pass.
    // Capped at 100K simultaneous overlay quads for V1.
    // Called once per budget tick when zone layout changes; NOT called every frame.
    // main-thread-only.
    virtual void setZoneOverlay(int mapTilesX, int mapTilesZ,
                                const std::unordered_map<uint64_t, uint32_t>& sparseOverlay) = 0;

    // getTileScreenBounds — return the screen bounding box of tile (tileX, tileZ).
    //
    // Returns a ScreenRect in physical pixels.  Returns ScreenRect{} (zero-initialised)
    // if the tile is off-screen, m_terrain is null, or the tile coordinates are out of bounds.
    // Used by UIManager to compute the InspectorPanel position via the three-step cascade
    // (see architecture/ui-ux/query-inspector-panel.md — Tile overlap prevention).
    // main-thread-only.
    virtual ScreenRect getTileScreenBounds(int tileX, int tileZ) const = 0;

    // getListenerPosition — return the current camera/listener world-space position.
    //
    // Returns the eye position most recently set via setCamera(). Used by
    // CitySimulation::tick() for the sfx_intersection_tick pre-acquisition distance cull
    // (> 80 m), preventing the SFX pool from being saturated by distant intersection ticks.
    // Returns vec3{} (zero) before the first setCamera() call.
    // main-thread-only.
    // (ref: implementation/phase-10.md sfx_intersection_tick wiring)
    virtual vec3 getListenerPosition() const = 0;

    // -----------------------------------------------------------------------
    // Phase 11d — Traffic agent rendering API
    //
    // Agents are distinct from Phase 10 placeVehicle/moveVehicle/removeVehicle.
    // Both sets of methods coexist. Vehicles are identified by AgentHandle (uint32_t
    // alias defined in simulation_types.h — do NOT redefine here to avoid ODR).
    // main-thread-only.
    // (ref: architecture/graphics-architecture/scene-graph-ownership.md §Agent Registry)
    // -----------------------------------------------------------------------

    // spawnVehicleAgent — create a traffic agent scene node at tile (tileX, tileZ).
    // zone determines the vehicle mesh asset (Residential→car, Commercial→van,
    // Industrial→truck).
    virtual void spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ,
                                   ZoneType zone) = 0;

    // moveVehicleAgent — update the agent's world-space position and heading.
    // worldX/worldZ are metres in world space (same coordinate system as terrain).
    virtual void moveVehicleAgent(AgentHandle handle, float worldX, float worldZ,
                                  float headingDeg) = 0;

    // despawnVehicleAgent — remove and destroy the agent scene node.
    // No-op if handle is not registered.
    virtual void despawnVehicleAgent(AgentHandle handle) = 0;

    // setIntersectionSignalState — update the signal billboard colour at tile.
    // Green → RGB(0,220,0); Red → RGB(220,0,0).
    // (ref: architecture/graphics-architecture/scene-graph-ownership.md §Intersection Signal Billboard Registry)
    virtual void setIntersectionSignalState(int tileX, int tileZ,
                                            SignalPhase phase) = 0;

    // -----------------------------------------------------------------------
    // Phase 11d — Service coverage overlay API
    // main-thread-only.
    // (ref: architecture/game-design/service-coverage.md)
    // -----------------------------------------------------------------------

    // showServiceCoverageOverlay — render service radius overlay for the building
    // at tile (tileX, tileZ). degraded=true renders in the degraded-service colour.
    virtual void showServiceCoverageOverlay(int tileX, int tileZ,
                                            ServiceBuildingType type,
                                            bool degraded) = 0;

    // hideServiceCoverageOverlay — remove the currently shown service overlay.
    // No-op if no overlay is visible.
    virtual void hideServiceCoverageOverlay() = 0;

    // -----------------------------------------------------------------------
    // Phase 10 — Building mesh spawning and road mesh rendering API
    //
    // These six methods wire CitySimulation placement/removal callbacks to
    // visible 3D scene geometry. All coordinates are in tile-space integers;
    // IrrlichtRenderer converts to world-space via (tileX * kTileSize, 0.0f,
    // tileZ * kTileSize). assetBaseName is the stem used to locate the .b3d
    // LOD files under assets/3d/buildings/ or assets/3d/roads/.
    //
    // Implementations must be no-ops (log warning, no crash) when the asset
    // file is absent or assetBaseName is empty.
    // main-thread-only.
    // (ref: implementation/phase-10.md City Rendering deliverables)
    // -----------------------------------------------------------------------

    // placeBuildingMesh — load LOD0/1/2 .b3d for assetBaseName and create a
    // scene node at tile (tileX, tileZ) registered in SceneEntityManager.
    virtual void placeBuildingMesh(int tileX, int tileZ,
                                   const std::string& assetBaseName) = 0;

    // removeBuildingMesh — destroy the building scene node at tile (tileX, tileZ).
    // No-op if no building is registered for that tile.
    virtual void removeBuildingMesh(int tileX, int tileZ) = 0;

    // placeRoadMesh — create a road tile scene node at tile (tileX, tileZ).
    //
    // All road tiles share the same mesh: flat LOD0 quad + kerb geometry (<=48 tris) with
    // the road custom shader and road_asphalt_tileable.dds on texture unit 0.
    // No assetBaseName parameter — road mesh asset is fixed, not per-tile variable.
    // LOD transitions use road tile thresholds from 3d-model-standards.md (30/25 m close,
    // 100/90 m far).
    // Called by CitySimulation after placeRoad() succeeds.
    // main-thread-only.
    virtual void placeRoadMesh(int tileX, int tileZ) = 0;

    // removeRoadMesh — destroy the road tile scene node registered at (tileX, tileZ).
    // No-op if no road is registered for that tile.
    // Called by CitySimulation after demolishTile() on a road tile.
    // main-thread-only.
    virtual void removeRoadMesh(int tileX, int tileZ) = 0;

    // placeServiceBuildingMesh — create a service building scene node at tile (tileX, tileZ).
    //
    // Asset path derived from type:
    //   PowerPlant    → "assets/3d/buildings/svc_power_plant_lod0.b3d"
    //   WaterTower    → "assets/3d/buildings/svc_water_tower_lod0.b3d"
    //   FireStation   → "assets/3d/buildings/svc_fire_station_lod0.b3d"
    //   PoliceStation → "assets/3d/buildings/svc_police_station_lod0.b3d"
    // LOD thresholds use the small building/props category (30/25 m close, 100/90 m far,
    // billboard LOD2) per 3d-model-standards.md Service Building Model Standards.
    // If the .b3d file is absent, logs a warning and returns — does not assert.
    // Called by CitySimulation after placeServiceBuilding() succeeds.
    // main-thread-only.
    virtual void placeServiceBuildingMesh(int tileX, int tileZ,
                                          ServiceBuildingType type) = 0;

    // removeServiceBuildingMesh — destroy the service building scene node at
    // tile (tileX, tileZ). No-op if no service building is registered there.
    // Called by CitySimulation after demolishTile() on a service building tile.
    // main-thread-only.
    virtual void removeServiceBuildingMesh(int tileX, int tileZ) = 0;

    // -----------------------------------------------------------------------
    // Phase 10 — Vehicle rendering API
    //
    // Called by the traffic simulation each frame to place, move, and remove
    // vehicle scene nodes.  Vehicles are identified by a stable uint32_t ID
    // assigned by the simulation for the vehicle's lifetime.
    //
    // assetName is the B3D asset stem, e.g. "car_sedan" or "bus_standard"
    // (without the _lodN.b3d suffix).  B3D files are expected under
    // assets/3d/vehicles/<assetName>_lod0.b3d etc.
    //
    // worldX/Y/Z: world-space position in metres.
    // yawDegrees: Y-axis rotation (0 = +Z forward, 90 = +X right).
    //
    // Vehicles are authored at world scale (no tile-based setScale needed).
    // Implementations must be no-ops (log warning, no crash) when the asset
    // file is absent.
    // main-thread-only.
    // (ref: implementation/phase-10.md Vehicle Rendering deliverables)
    // -----------------------------------------------------------------------

    // placeVehicle — load LOD0/1 .b3d for assetName and create a scene node
    // at the given world-space position.  If vehicleId is already registered,
    // the old node is removed first (replaces in place).
    virtual void placeVehicle(uint32_t vehicleId,
                              const std::string& assetName,
                              float worldX, float worldY, float worldZ,
                              float yawDegrees) = 0;

    // moveVehicle — update position/yaw of an existing vehicle node.
    // If vehicleId is unknown, delegates to placeVehicle and returns.
    virtual void moveVehicle(uint32_t vehicleId,
                             float worldX, float worldY, float worldZ,
                             float yawDegrees) = 0;

    // removeVehicle — destroy the vehicle scene node for vehicleId.
    // No-op if vehicleId is not registered.
    virtual void removeVehicle(uint32_t vehicleId) = 0;

    // clearCity — remove all building, road, and agent scene nodes from the scene graph.
    //
    // Called by main.cpp after CitySimulation::reset() in the new-game flow (Phase 11m).
    // Clears m_buildingNodes, m_roadNodes, and m_agentNodes, running the full eviction
    // sequence (clear textures → setMaterial(SMaterial{}) → node->remove()) on each node.
    // Does NOT remove terrain chunk nodes — terrain is rebuilt separately.
    // Resets any road-tile count and per-tile mesh state that accumulates across addRoadTile().
    // main-thread-only.
    virtual void clearCity() = 0;

    // setTilePlacementPreview — render a multi-tile placement preview highlight.
    //
    // freeTiles / freeArgb: tiles the player can freely place on (shown in tool colour).
    // blockedTiles:         tiles that are already occupied (shown in kHoverArgbBlocked red).
    //                       Defaults to {} for all callers that have not yet been updated
    //                       to the two-list API (Deliverable 5d completes those call sites).
    //
    // Each entry in freeTiles and blockedTiles is a (tileX, tileZ) pair.
    // Passing empty freeTiles AND empty blockedTiles clears the preview.
    // main-thread-only.
    virtual void setTilePlacementPreview(const std::vector<std::pair<int,int>>& freeTiles,
                                         uint32_t freeArgb,
                                         const std::vector<std::pair<int,int>>& blockedTiles = {}) = 0;
};
