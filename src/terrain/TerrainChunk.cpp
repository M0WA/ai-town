// TerrainChunk.cpp — Phase 5 full implementation.
//
// Implements TerrainChunk construction, SMesh building, and heightmap query API.
// See architecture/graphics-architecture/procedural-terrain.md
// See architecture/graphics-architecture/scene-graph-ownership.md

// GLEW before any Irrlicht/OpenGL includes to prevent symbol conflicts.
// TerrainChunk.h uses a forward declaration for irr::scene::SMesh to avoid
// requiring GLEW/Irrlicht in the static library build flags. The .cpp includes
// them directly because terrain_tests and integration_tests link GLEW::GLEW and
// Irrlicht explicitly, making these headers available at test binary compile time.
// When aitown_terrain compiles TerrainChunk.cpp, it must receive GLEW/Irrlicht
// includes via its consumers (terrain_tests links them directly per CMakeLists.txt).
#include <GL/glew.h>
#include <irrlicht.h>

#include "TerrainChunk.h"

#include <stdexcept>
#include <algorithm>  // std::clamp
#include <cmath>      // std::atan, std::sqrt

// ---------------------------------------------------------------------------
// buildMesh — private helper: constructs the SMesh from the stored heightmap.
//
// Vertex layout: S3DVertex (position, normal, color, texCoord).
// One SMeshBuffer per chunk (entire terrain chunk in one draw call).
//
// Grid structure:
//   m_gridSize quad cells per side → (m_gridSize+1) vertices per side
//   Total vertices = (m_gridSize+1)^2
//   Total indices  = m_gridSize * m_gridSize * 6  (2 triangles per quad, 3 indices each)
//
// Winding: counter-clockwise (Irrlicht front-face default).
//   For quad at (col, row):
//     v0 = (col,   row)    top-left
//     v1 = (col+1, row)    top-right
//     v2 = (col+1, row+1)  bottom-right
//     v3 = (col,   row+1)  bottom-left
//   Triangle 1: v0, v1, v2  (CCW)
//   Triangle 2: v0, v2, v3  (CCW)
//
// Bounding box sequence (mandatory per procedural-terrain.md):
//   recalculateBoundingBox() on each SMeshBuffer, THEN on the SMesh itself.
//   The TerrainChunk constructor stores the mesh; callers must NOT call
//   addMeshSceneNode() before the chunk is built (constructor handles both).
//
// SMesh ownership: m_mesh is grabbed once by the TerrainChunk and dropped
// in the destructor. Callers that pass it to addMeshSceneNode() receive an
// additional grab() from Irrlicht — the chunk remains alive independently.
// ---------------------------------------------------------------------------
void TerrainChunk::buildMesh() {
    using namespace irr;
    using namespace irr::scene;
    using namespace irr::video;

    // Create the mesh and a single mesh buffer.
    m_mesh = new SMesh();  // ref_count = 1 (the TerrainChunk owns it)

    SMeshBuffer* buf = new SMeshBuffer();  // ref_count = 1

    const int verts    = m_gridSize + 1;          // vertices per side
    const int vertCount = verts * verts;           // total vertex count
    const int quadCount = m_gridSize * m_gridSize; // total quad count
    const int indexCount = quadCount * 6;          // 2 triangles * 3 indices

    // Reserve capacity to avoid repeated reallocations.
    buf->Vertices.reallocate(static_cast<u32>(vertCount));
    buf->Indices.reallocate(static_cast<u32>(indexCount));

    // ---------------------------------------------------------------------------
    // Build vertex array.
    // Row-major order: vertex[z * verts + x] at world position (x*cellSize, h, z*cellSize).
    // ---------------------------------------------------------------------------
    for (int z = 0; z < verts; ++z) {
        for (int x = 0; x < verts; ++x) {
            // Height from heightmap (row-major: z*(gridSize+1)+x).
            float h = m_heightmap[static_cast<size_t>(z * verts + x)];

            // World position: X and Z from grid, Y from heightmap.
            core::vector3df pos(
                static_cast<f32>(x) * m_cellSize,
                h,
                static_cast<f32>(z) * m_cellSize
            );

            // Normal: computed from finite differences between adjacent heightmap samples.
            // For interior vertices we use central differences; for boundary vertices we
            // clamp to valid range (getHeightAt handles clamping).
            float hR = m_heightmap[static_cast<size_t>(z * verts + std::min(x + 1, m_gridSize))];
            float hL = m_heightmap[static_cast<size_t>(z * verts + std::max(x - 1, 0))];
            float hU = m_heightmap[static_cast<size_t>(std::max(z - 1, 0) * verts + x)];
            float hD = m_heightmap[static_cast<size_t>(std::min(z + 1, m_gridSize) * verts + x)];

            // Tangent vectors along X and Z (world units).
            // dX = (hR-hL) / (2*cellSize), dZ = (hD-hU) / (2*cellSize)
            // Normal = cross(-dX, -dZ, 1) normalised = (-dX_unnorm, 1, -dZ_unnorm).
            float dX = (hR - hL) * 0.5f / m_cellSize;
            float dZ = (hD - hU) * 0.5f / m_cellSize;
            core::vector3df normal(-dX, 1.0f, -dZ);
            normal.normalize();

            // UV coordinates: (x, z) / gridSize — [0,1] across the chunk.
            core::vector2df uv(
                static_cast<f32>(x) / static_cast<f32>(m_gridSize),
                static_cast<f32>(z) / static_cast<f32>(m_gridSize)
            );

            S3DVertex vert(pos, normal, SColor(255, 255, 255, 255), uv);
            buf->Vertices.push_back(vert);
        }
    }

    // ---------------------------------------------------------------------------
    // Build index array.
    // For each quad cell (col=0..gridSize-1, row=0..gridSize-1):
    //   v0 = row*verts + col        (top-left)
    //   v1 = row*verts + col+1      (top-right)
    //   v2 = (row+1)*verts + col+1  (bottom-right)
    //   v3 = (row+1)*verts + col    (bottom-left)
    // Triangle 1: v0, v1, v2  (CCW)
    // Triangle 2: v0, v2, v3  (CCW)
    // ---------------------------------------------------------------------------
    for (int row = 0; row < m_gridSize; ++row) {
        for (int col = 0; col < m_gridSize; ++col) {
            u32 v0 = static_cast<u32>(row       * verts + col);
            u32 v1 = static_cast<u32>(row       * verts + col + 1);
            u32 v2 = static_cast<u32>((row + 1) * verts + col + 1);
            u32 v3 = static_cast<u32>((row + 1) * verts + col);

            // Triangle 1: v0, v1, v2
            buf->Indices.push_back(v0);
            buf->Indices.push_back(v1);
            buf->Indices.push_back(v2);

            // Triangle 2: v0, v2, v3
            buf->Indices.push_back(v0);
            buf->Indices.push_back(v2);
            buf->Indices.push_back(v3);
        }
    }

    // ---------------------------------------------------------------------------
    // Bounding box sequence (mandatory per procedural-terrain.md):
    //   1. recalculateBoundingBox() on each SMeshBuffer.
    //   2. recalculateBoundingBox() on the SMesh.
    // Omitting either step leaves a degenerate bounding box — frustum culling
    // breaks silently (objects disappear when they should be visible).
    // ---------------------------------------------------------------------------
    buf->recalculateBoundingBox();

    // Transfer buffer ownership to the mesh.
    // SMesh::addMeshBuffer() calls grab() on the buffer, incrementing its ref_count.
    // We then drop() the local reference — the mesh is the sole owner.
    m_mesh->addMeshBuffer(buf);
    buf->drop();  // ref_count 2→1; m_mesh is now the sole owner

    // Recalculate the SMesh bounding box from all its buffers.
    m_mesh->recalculateBoundingBox();
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

// Primary 3-parameter constructor — tests use this.
// heightmap: flat array of (gridSize+1)*(gridSize+1) height values.
// Row-major: index = z * (gridSize+1) + x.
TerrainChunk::TerrainChunk(const float* heightmap, int gridSize, float cellSize)
    : m_chunkId{0}
    , m_gridSize{gridSize}
    , m_cellSize{cellSize}
    , m_currentLOD{0}
{
    const int vertCount = (gridSize + 1) * (gridSize + 1);
    m_heightmap.assign(heightmap, heightmap + vertCount);
    buildMesh();
}

// 4-parameter constructor — production use with ChunkId.
TerrainChunk::TerrainChunk(const float* heightmap, int gridSize, float cellSize, ChunkId chunkId)
    : m_chunkId{chunkId}
    , m_gridSize{gridSize}
    , m_cellSize{cellSize}
    , m_currentLOD{0}
{
    const int vertCount = (gridSize + 1) * (gridSize + 1);
    m_heightmap.assign(heightmap, heightmap + vertCount);
    buildMesh();
}

// Convenience constructor from std::vector — for internal use.
TerrainChunk::TerrainChunk(const std::vector<float>& heightData, int gridSize, float cellSize,
                           ChunkId chunkId)
    : m_chunkId{chunkId}
    , m_gridSize{gridSize}
    , m_cellSize{cellSize}
    , m_currentLOD{0}
    , m_heightmap{heightData}
{
    // Validate that heightData has the correct size; pad with zeros if undersized.
    const int expectedSize = (gridSize + 1) * (gridSize + 1);
    if (static_cast<int>(m_heightmap.size()) < expectedSize) {
        m_heightmap.resize(static_cast<size_t>(expectedSize), 0.0f);
    }
    buildMesh();
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
TerrainChunk::~TerrainChunk() {
    if (m_mesh) {
        m_mesh->drop();
        m_mesh = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Move constructor
// ---------------------------------------------------------------------------
TerrainChunk::TerrainChunk(TerrainChunk&& other) noexcept
    : m_chunkId{other.m_chunkId}
    , m_gridSize{other.m_gridSize}
    , m_cellSize{other.m_cellSize}
    , m_currentLOD{other.m_currentLOD}
    , m_heightmap{std::move(other.m_heightmap)}
    , m_mesh{other.m_mesh}
{
    other.m_mesh = nullptr;  // Transfer ownership — other's destructor must NOT drop
}

// ---------------------------------------------------------------------------
// Move assignment operator
// ---------------------------------------------------------------------------
TerrainChunk& TerrainChunk::operator=(TerrainChunk&& other) noexcept {
    if (this != &other) {
        // Drop our current mesh before overwriting.
        if (m_mesh) {
            m_mesh->drop();
        }
        m_chunkId    = other.m_chunkId;
        m_gridSize   = other.m_gridSize;
        m_cellSize   = other.m_cellSize;
        m_currentLOD = other.m_currentLOD;
        m_heightmap  = std::move(other.m_heightmap);
        m_mesh       = other.m_mesh;
        other.m_mesh = nullptr;  // Transfer ownership
    }
    return *this;
}

// ---------------------------------------------------------------------------
// getHeightAt — returns the heightmap value at chunk-local vertex (tileX, tileZ).
// ---------------------------------------------------------------------------
float TerrainChunk::getHeightAt(int tileX, int tileZ) const {
    // Clamp to valid vertex range [0, gridSize].
    // The heightmap has (gridSize+1)*(gridSize+1) entries for vertex indices 0..gridSize.
    int x = std::clamp(tileX, 0, m_gridSize);
    int z = std::clamp(tileZ, 0, m_gridSize);
    // Row-major: index = z * (gridSize+1) + x
    int idx = z * (m_gridSize + 1) + x;
    return m_heightmap[static_cast<size_t>(idx)];
}

// ---------------------------------------------------------------------------
// getSlopeDegrees — returns the terrain slope in degrees at tile (tileX, tileZ).
// ---------------------------------------------------------------------------
float TerrainChunk::getSlopeDegrees(int tileX, int tileZ) const {
    // Compute slope from finite differences in X and Z directions.
    // h00 = height at (tileX, tileZ)
    // h10 = height at (tileX+1, tileZ) — one step in X
    // h01 = height at (tileX, tileZ+1) — one step in Z
    //
    // dx = (h10 - h00) / m_cellSize  → rise/run in X direction
    // dz = (h01 - h00) / m_cellSize  → rise/run in Z direction
    // slope = atan(sqrt(dx*dx + dz*dz)) in radians → convert to degrees
    //
    // Note: getHeightAt clamps out-of-bounds accesses, so tileX+1 / tileZ+1
    // at the chunk boundary will return the edge vertex height (slope = 0 at boundary edges).
    float h00 = getHeightAt(tileX,     tileZ);
    float h10 = getHeightAt(tileX + 1, tileZ);
    float h01 = getHeightAt(tileX,     tileZ + 1);

    float dx = (h10 - h00) / m_cellSize;  // rise/run in X
    float dz = (h01 - h00) / m_cellSize;  // rise/run in Z

    float slopeRad = std::atan(std::sqrt(dx * dx + dz * dz));

    // Convert radians to degrees.
    static constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    return slopeRad * kRadToDeg;
}
