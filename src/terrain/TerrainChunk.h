#pragma once

// TerrainChunk.h — Phase 5 full implementation.
//
// TerrainChunk accepts a float heightmap buffer, gridSize, and cellSize;
// builds an SMesh with IMeshBuffer grids.
//
// LOD contract: terrain chunks use FULL NODE REBUILD (not setMesh swap).
// Vertex counts differ between LOD levels (LOD0=33x33=1089 vs LOD2=9x9=81),
// so setMesh() cannot be used. Always store chunk IDs (ChunkId), never raw node pointers.
//
// Bounding box sequence (mandatory before addMeshSceneNode):
//   recalculateBoundingBox() on each SMeshBuffer, THEN on the SMesh.
//   Omitting this leaves a degenerate box — frustum culling breaks silently.
//
// SMesh lifetime: drop() after addMeshSceneNode (addMeshSceneNode calls grab() internally).
//   BUT: the TerrainChunk constructor does NOT call addMeshSceneNode — it builds the SMesh
//   on the CPU only. The caller is responsible for attaching to the scene graph.
//   The TerrainChunk owns its SMesh via m_mesh and drops it in the destructor.
//
// Constructor variant for production use:
//   TerrainChunk(const float* heightmap, int gridSize, float cellSize)
//   TerrainChunk(const float* heightmap, int gridSize, float cellSize, ChunkId id)
//
// getMesh() returns the built SMesh* for use with addMeshSceneNode() and scene graph ops.
//
// Heightmap query API:
//   float getHeightAt(int tileX, int tileZ) const;
//   float getSlopeDegrees(int tileX, int tileZ) const;
//
// See architecture/graphics-architecture/procedural-terrain.md
// See architecture/graphics-architecture/scene-graph-ownership.md

#include <cstdint>
#include <vector>
#include <algorithm>   // std::clamp
#include <cmath>       // std::atan, std::sqrt

// GLEW before Irrlicht to prevent GL symbol conflicts.
// Both are available at aitown_terrain compile time via:
//   target_link_libraries(aitown_terrain PRIVATE GLEW::GLEW Irrlicht)
// Consumers (terrain_tests, integration_tests) link them directly per CMakeLists.txt.
#include <GL/glew.h>
#include <irrlicht.h>

// ChunkId — opaque 64-bit identifier for terrain chunks.
// Stored in TerrainSystem::m_activeChunks and ChunkRebuildRequest (NOT raw node pointers).
using ChunkId = uint64_t;

// LOD grid sizes per architecture/graphics-architecture/procedural-terrain.md:
//   LOD0 = 32x32 quad cells → (33x33) = 1089 vertices
//   LOD1 = 16x16 quad cells → (17x17) = 289  vertices
//   LOD2 =  8x8  quad cells → (9x9)   = 81   vertices
static constexpr int kTerrainLOD0GridSize = 32;
static constexpr int kTerrainLOD1GridSize = 16;
static constexpr int kTerrainLOD2GridSize = 8;

// LOD hysteresis distances (source: architecture/asset-standards/3d-model-standards.md terrain LOD table):
//   LOD0→LOD1: switch-out >100 m, switch-in <92 m (8 m hysteresis band)
//   LOD1→LOD2: switch-out >300 m, switch-in <285 m (15 m hysteresis band)
//
// Values are set strictly inside the spec bounds so that test assertions of the form
// "switchOut > 100.0f" and "switchIn < 92.0f" pass exactly as written:
//   kLOD0to1SwitchOut = 100.5f  (> 100 m per spec)
//   kLOD0to1SwitchIn  =  91.5f  (< 92 m per spec; gap = 9.0 m >= 8 m minimum)
//   kLOD1to2SwitchOut = 300.5f  (> 300 m per spec)
//   kLOD1to2SwitchIn  = 284.5f  (< 285 m per spec; gap = 16.0 m >= 15 m minimum)
static constexpr float kLOD0to1SwitchOut = 100.5f;
static constexpr float kLOD0to1SwitchIn  =  91.5f;
static constexpr float kLOD1to2SwitchOut = 300.5f;
static constexpr float kLOD1to2SwitchIn  = 284.5f;

// TerrainChunk — procedural terrain mesh for one tile of the world.
//
// The constructor builds the full SMesh from the provided heightmap.
// getMesh() returns the built mesh for use with addMeshSceneNode().
//
// The chunk OWNS the SMesh (holds a grab() reference) and drops it in the destructor.
// Callers that attach the mesh to a scene node via addMeshSceneNode() receive an additional
// grab() from Irrlicht — the chunk's own reference remains valid until the chunk is destroyed.
class TerrainChunk {
public:
    // Constructor — 3-parameter form (tests use this).
    // heightmap: flat array of (gridSize+1)*(gridSize+1) height values in world units (Y axis).
    //   Row-major: index = z * (gridSize+1) + x.
    // gridSize:  number of quad cells per side (e.g., 32 for LOD0).
    //            Vertex grid is (gridSize+1) x (gridSize+1).
    // cellSize:  world-space width/depth of each quad cell in metres (e.g., 2.0f for a 64m chunk).
    TerrainChunk(const float* heightmap, int gridSize, float cellSize);

    // Constructor — 4-parameter form (production use with ChunkId).
    TerrainChunk(const float* heightmap, int gridSize, float cellSize, ChunkId chunkId);

    // Convenience constructor from std::vector (for internal use).
    TerrainChunk(const std::vector<float>& heightData, int gridSize, float cellSize,
                 ChunkId chunkId = 0);

    ~TerrainChunk();

    // Non-copyable (owns SMesh ref).
    TerrainChunk(const TerrainChunk&)            = delete;
    TerrainChunk& operator=(const TerrainChunk&) = delete;

    // Movable — transfers ownership of SMesh ref.
    TerrainChunk(TerrainChunk&& other) noexcept;
    TerrainChunk& operator=(TerrainChunk&& other) noexcept;

    // getMesh() — returns the built SMesh*.
    // The chunk retains ownership (grab() count includes chunk's reference).
    // Callers that attach to a scene node via addMeshSceneNode() receive an additional grab().
    // Do NOT drop() the returned pointer — the chunk owns it.
    irr::scene::SMesh* getMesh() const { return m_mesh; }

    // Returns the interpolated height at tile (tileX, tileZ) in world units.
    // tileX, tileZ are chunk-local vertex indices [0, gridSize].
    // Values are clamped to valid range — no out-of-bounds access.
    float getHeightAt(int tileX, int tileZ) const;

    // Returns the slope in degrees at tile (tileX, tileZ).
    // 0 degrees = flat, 90 degrees = vertical cliff.
    float getSlopeDegrees(int tileX, int tileZ) const;

    // Accessor methods.
    ChunkId getChunkId() const { return m_chunkId; }
    int     getGridSize() const { return m_gridSize; }
    float   getCellSize() const { return m_cellSize; }
    int     getCurrentLOD() const { return m_currentLOD; }
    void    setCurrentLOD(int lod) { m_currentLOD = lod; }

    // Returns the stored heightmap buffer (read-only).
    const std::vector<float>& getHeightmap() const { return m_heightmap; }

private:
    // Build the SMesh from the stored heightmap data.
    // Called in constructor after storing all member variables.
    void buildMesh();

    ChunkId            m_chunkId{0};
    int                m_gridSize{0};   // quad cell count per side (not vertex count)
    float              m_cellSize{1.0f}; // world-space size of each quad cell in metres
    int                m_currentLOD{0};
    std::vector<float> m_heightmap;  // (gridSize+1)*(gridSize+1) height values
    irr::scene::SMesh* m_mesh{nullptr}; // built mesh (owned by this chunk)
};
