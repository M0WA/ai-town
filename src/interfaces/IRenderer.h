#pragma once

#include "vec3.h"
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Opaque texture handle — uint32_t alias instead of ITexture* so that IRenderer.h
// severs the compile-time dependency on Irrlicht headers for all consumers.
// The concrete IrrlichtRenderer maintains std::unordered_map<TextureHandle, ITexture*> internally.
using TextureHandle = uint32_t;
static constexpr TextureHandle kInvalidTexture = 0;

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
    float farClip{3000.0f};    // far clip plane distance in metres (covers 1024x1024 map + sky)
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

    // setTileHoverHighlight — render a wireframe/filled quad over the hovered tile.
    //
    // ARGB colour encoded as 0xAARRGGBB (Irrlicht SColor format).
    // Pass tileX = -1 to clear the highlight (sets m_hoverVisible = false without
    // dropping or reallocating the internal mesh buffer).
    // Called once per MouseMove event from UIManager; actual drawMeshBuffer() is issued
    // inside IrrlichtRenderer::drawScene() after sceneManager->drawAll().
    // main-thread-only.
    virtual void setTileHoverHighlight(int tileX, int tileZ, uint32_t argb) = 0;

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
};
