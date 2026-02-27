#pragma once

#include "vec3.h"
#include <string>
#include <cstdint>
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
};
