// terrain_chunk_test.cpp — Phase 5 TerrainChunk unit tests.
//
// Tests the TerrainChunk mesh-building contract:
//   - Correct vertex count for each LOD grid size ((gridSize+1)^2)
//   - Non-degenerate bounding box after recalculateBoundingBox()
//   - Property: bounding box is never degenerate for any grid size in [4, 64]
//
// Label: "unit" (no display or GL context required)
// Spec ref: phase-5.md §TerrainChunk unit tests
//           architecture/graphics-architecture/procedural-terrain.md
//           architecture/asset-standards/3d-model-standards.md §LOD Requirements

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "src/terrain/TerrainChunk.h"

// ---------------------------------------------------------------------------
// Helpers: build a minimal flat heightmap buffer of size (gridSize+1)*(gridSize+1).
// All heights are 0.0f — the flat terrain case exercises bounding-box
// behaviour on the X/Z axes; the Y extent is guaranteed >= 0.
// ---------------------------------------------------------------------------
static std::vector<float> makeFlatHeightmap(int gridSize) {
    int verts = (gridSize + 1) * (gridSize + 1);
    return std::vector<float>(verts, 0.0f);
}

// ---------------------------------------------------------------------------
// TerrainChunk_BuildsMesh_WithCorrectVertexCount
//
// LOD grid sizes (quad cells, not vertices):
//   LOD0 = 32x32 -> vertex count = (32+1)^2 = 1089
//   LOD1 = 16x16 -> vertex count = (16+1)^2 = 289
//   LOD2 =  8x8  -> vertex count = ( 8+1)^2 = 81
//
// TerrainChunk constructor: TerrainChunk(const float* heightmap,
//                                        int gridSize, float cellSize)
// After construction, getMesh()->getMeshBuffer(0)->getVertexCount()
// must equal (gridSize+1)^2.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, BuildsMesh_WithCorrectVertexCount_LOD0) {
    const int gridSize = 32;
    auto heights = makeFlatHeightmap(gridSize);
    const float cellSize = 2.0f;  // 2 m per cell -> 64 m chunk

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    // (32+1)^2 = 33*33 = 1089
    const unsigned int expected = static_cast<unsigned int>((gridSize + 1) * (gridSize + 1));
    ASSERT_NE(chunk.getMesh(), nullptr) << "getMesh() must not return nullptr after construction";
    ASSERT_GT(chunk.getMesh()->getMeshBufferCount(), 0u)
        << "Mesh must have at least one MeshBuffer";
    EXPECT_EQ(chunk.getMesh()->getMeshBuffer(0)->getVertexCount(), expected)
        << "LOD0 32x32 grid must produce (33*33)=1089 vertices";
}

TEST(TerrainChunk, BuildsMesh_WithCorrectVertexCount_LOD1) {
    const int gridSize = 16;
    auto heights = makeFlatHeightmap(gridSize);
    const float cellSize = 4.0f;

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    const unsigned int expected = static_cast<unsigned int>((gridSize + 1) * (gridSize + 1));
    ASSERT_NE(chunk.getMesh(), nullptr);
    ASSERT_GT(chunk.getMesh()->getMeshBufferCount(), 0u);
    EXPECT_EQ(chunk.getMesh()->getMeshBuffer(0)->getVertexCount(), expected)
        << "LOD1 16x16 grid must produce (17*17)=289 vertices";
}

TEST(TerrainChunk, BuildsMesh_WithCorrectVertexCount_LOD2) {
    const int gridSize = 8;
    auto heights = makeFlatHeightmap(gridSize);
    const float cellSize = 8.0f;

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    const unsigned int expected = static_cast<unsigned int>((gridSize + 1) * (gridSize + 1));
    ASSERT_NE(chunk.getMesh(), nullptr);
    ASSERT_GT(chunk.getMesh()->getMeshBufferCount(), 0u);
    EXPECT_EQ(chunk.getMesh()->getMeshBuffer(0)->getVertexCount(), expected)
        << "LOD2 8x8 grid must produce (9*9)=81 vertices";
}

// ---------------------------------------------------------------------------
// TerrainChunk_BoundingBox_NotDegenerate
//
// After buildMesh() (performed in constructor), recalculateBoundingBox() is
// called on each mesh buffer and then on the SMesh itself.  For a non-zero
// cellSize the X and Z extents must be > 0.
//
// The Y extent is >= 0 (flat terrain has Y=0 everywhere; all heights equal
// means max == min, so Y extent == 0 is acceptable for a perfectly flat chunk).
// ---------------------------------------------------------------------------
TEST(TerrainChunk, BoundingBox_NotDegenerate_XZ) {
    const int gridSize = 32;
    auto heights = makeFlatHeightmap(gridSize);
    const float cellSize = 2.0f;

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    ASSERT_NE(chunk.getMesh(), nullptr);
    const auto& bbox = chunk.getMesh()->getBoundingBox();
    EXPECT_GT(bbox.MaxEdge.X - bbox.MinEdge.X, 0.0f)
        << "Bounding box X extent must be > 0 for non-zero cellSize";
    EXPECT_GE(bbox.MaxEdge.Y - bbox.MinEdge.Y, 0.0f)
        << "Bounding box Y extent must be >= 0 (0 is valid for flat terrain)";
    EXPECT_GT(bbox.MaxEdge.Z - bbox.MinEdge.Z, 0.0f)
        << "Bounding box Z extent must be > 0 for non-zero cellSize";
}

// Non-flat heightmap: varying heights ensure Y extent > 0 as well.
TEST(TerrainChunk, BoundingBox_NotDegenerate_WithVaryingHeights) {
    const int gridSize = 8;
    const int vertCount = (gridSize + 1) * (gridSize + 1);
    const float cellSize = 4.0f;

    // Create a simple slope: height increases linearly with vertex index.
    std::vector<float> heights(vertCount);
    for (int i = 0; i < vertCount; ++i) {
        heights[i] = static_cast<float>(i) * 0.5f;  // 0.0, 0.5, 1.0, ...
    }

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    ASSERT_NE(chunk.getMesh(), nullptr);
    const auto& bbox = chunk.getMesh()->getBoundingBox();
    EXPECT_GT(bbox.MaxEdge.X - bbox.MinEdge.X, 0.0f);
    EXPECT_GT(bbox.MaxEdge.Y - bbox.MinEdge.Y, 0.0f)
        << "Varying heights must produce non-zero Y extent";
    EXPECT_GT(bbox.MaxEdge.Z - bbox.MinEdge.Z, 0.0f);
}

// ---------------------------------------------------------------------------
// TerrainChunk_GetHeightAt_ReturnsCorrectValue
//
// Phase 5 deliverable: TerrainChunk exposes getHeightAt(int tileX, int tileZ).
// For a flat heightmap at a known constant, every tile must return that constant.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, GetHeightAt_ReturnsCorrectValue) {
    const int gridSize = 16;
    const float cellSize = 2.0f;
    const float kHeight = 42.0f;

    // All heights set to kHeight.
    std::vector<float> heights((gridSize + 1) * (gridSize + 1), kHeight);

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    // Sample a few tile positions.
    EXPECT_FLOAT_EQ(chunk.getHeightAt(0, 0), kHeight);
    EXPECT_FLOAT_EQ(chunk.getHeightAt(gridSize / 2, gridSize / 2), kHeight);
    EXPECT_FLOAT_EQ(chunk.getHeightAt(gridSize - 1, gridSize - 1), kHeight);
}

// ---------------------------------------------------------------------------
// TerrainChunk_GetSlopeDegrees_FlatTerrain_ReturnsZero
//
// Phase 5 deliverable: getHeightAt()/getSlopeDegrees() API.
// On a perfectly flat heightmap, slope must be 0 degrees at every tile.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, GetSlopeDegrees_FlatTerrain_ReturnsZero) {
    const int gridSize = 16;
    const float cellSize = 2.0f;

    std::vector<float> heights((gridSize + 1) * (gridSize + 1), 0.0f);

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    // All tiles are flat; slope must be exactly 0.
    EXPECT_FLOAT_EQ(chunk.getSlopeDegrees(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(chunk.getSlopeDegrees(gridSize / 2, gridSize / 2), 0.0f);
    EXPECT_FLOAT_EQ(chunk.getSlopeDegrees(gridSize - 1, gridSize - 1), 0.0f);
}

// ---------------------------------------------------------------------------
// TerrainChunk_FourParamConstructor_StoresChunkId
//
// Verifies the 4-parameter constructor: TerrainChunk(float*, int, float, ChunkId).
// After construction the chunk must report the given ChunkId, the correct gridSize,
// and must have a valid non-null mesh.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, FourParamConstructor_StoresChunkId) {
    const int gridSize     = 8;
    const float cellSize   = 2.0f;
    const ChunkId chunkId  = 0xDEADBEEFu;
    auto heights = makeFlatHeightmap(gridSize);

    TerrainChunk chunk(heights.data(), gridSize, cellSize, chunkId);

    EXPECT_EQ(chunk.getChunkId(), chunkId)
        << "4-param constructor must store the given ChunkId";
    EXPECT_EQ(chunk.getGridSize(), gridSize)
        << "4-param constructor must store the given gridSize";
    EXPECT_FLOAT_EQ(chunk.getCellSize(), cellSize)
        << "4-param constructor must store the given cellSize";
    ASSERT_NE(chunk.getMesh(), nullptr)
        << "4-param constructor must build a valid mesh";
}

// ---------------------------------------------------------------------------
// TerrainChunk_VectorConstructor_CorrectVertexCount
//
// Verifies the std::vector<float> convenience constructor:
//   TerrainChunk(const std::vector<float>&, int, float, ChunkId)
// After construction the vertex count must equal (gridSize+1)^2.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, VectorConstructor_CorrectVertexCount) {
    const int gridSize  = 16;
    const float cellSize = 4.0f;
    const ChunkId id    = 42u;
    auto heights = makeFlatHeightmap(gridSize);

    TerrainChunk chunk(heights, gridSize, cellSize, id);

    ASSERT_NE(chunk.getMesh(), nullptr);
    ASSERT_GT(chunk.getMesh()->getMeshBufferCount(), 0u);
    const unsigned int expected =
        static_cast<unsigned int>((gridSize + 1) * (gridSize + 1));
    EXPECT_EQ(chunk.getMesh()->getMeshBuffer(0)->getVertexCount(), expected)
        << "Vector constructor must produce (gridSize+1)^2 vertices";
    EXPECT_EQ(chunk.getChunkId(), id)
        << "Vector constructor must store the given ChunkId";
}

// ---------------------------------------------------------------------------
// TerrainChunk_VectorConstructor_DefaultChunkId_IsZero
//
// Verifies the std::vector<float> convenience constructor with the default
// ChunkId (= 0 when omitted).
// ---------------------------------------------------------------------------
TEST(TerrainChunk, VectorConstructor_DefaultChunkId_IsZero) {
    const int gridSize  = 8;
    const float cellSize = 1.0f;
    auto heights = makeFlatHeightmap(gridSize);

    // Call with 3 args (ChunkId defaults to 0).
    TerrainChunk chunk(heights, gridSize, cellSize);

    EXPECT_EQ(chunk.getChunkId(), static_cast<ChunkId>(0))
        << "Vector constructor with default ChunkId must set chunkId to 0";
    ASSERT_NE(chunk.getMesh(), nullptr);
}

// ---------------------------------------------------------------------------
// TerrainChunk_VectorConstructor_UndersizedVector_PadsWithZeros
//
// Verifies that the vector constructor pads a too-small vector with zeros
// rather than reading out-of-bounds memory.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, VectorConstructor_UndersizedVector_PadsWithZeros) {
    const int gridSize  = 8;
    const float cellSize = 2.0f;
    const int expectedSize = (gridSize + 1) * (gridSize + 1); // 81

    // Provide only 10 heights — much less than the required 81.
    std::vector<float> tinyHeights(10, 5.0f);

    // Must not crash; constructor pads with zeros.
    TerrainChunk chunk(tinyHeights, gridSize, cellSize);

    ASSERT_NE(chunk.getMesh(), nullptr)
        << "Vector constructor must build a mesh even with an undersized input";
    EXPECT_EQ(chunk.getHeightmap().size(), static_cast<size_t>(expectedSize))
        << "Heightmap must be padded to (gridSize+1)^2";
}

// ---------------------------------------------------------------------------
// TerrainChunk_MoveConstructor_TransfersOwnership
//
// Verifies TerrainChunk move constructor:
//   - The moved-to chunk has a valid mesh pointer (original mesh).
//   - The moved-from chunk has a null mesh pointer (ownership transferred).
//   - The moved-to chunk reports the correct gridSize and chunkId.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, MoveConstructor_TransfersOwnership) {
    const int gridSize    = 8;
    const float cellSize  = 2.0f;
    const ChunkId chunkId = 77u;
    auto heights = makeFlatHeightmap(gridSize);

    TerrainChunk src(heights.data(), gridSize, cellSize, chunkId);
    irr::scene::SMesh* originalMesh = src.getMesh();
    ASSERT_NE(originalMesh, nullptr);

    // Move-construct.
    TerrainChunk dst(std::move(src));

    // The destination now owns the mesh.
    EXPECT_EQ(dst.getMesh(), originalMesh)
        << "Move constructor must transfer mesh pointer to destination";
    EXPECT_EQ(dst.getChunkId(), chunkId)
        << "Move constructor must transfer chunkId";
    EXPECT_EQ(dst.getGridSize(), gridSize)
        << "Move constructor must transfer gridSize";

    // The source must no longer own the mesh (null after move).
    EXPECT_EQ(src.getMesh(), nullptr)
        << "Move constructor must null the source mesh pointer after transfer";
}

// ---------------------------------------------------------------------------
// TerrainChunk_MoveAssignment_TransfersOwnership
//
// Verifies TerrainChunk move assignment operator:
//   - The assigned-to chunk obtains the source mesh.
//   - The source chunk's mesh pointer is null after assignment.
//   - A prior mesh owned by the destination is dropped correctly (no crash).
// ---------------------------------------------------------------------------
TEST(TerrainChunk, MoveAssignment_TransfersOwnership) {
    const int gridSize   = 8;
    const float cellSize = 2.0f;
    auto heights = makeFlatHeightmap(gridSize);

    TerrainChunk src(heights.data(), gridSize, cellSize, /*chunkId=*/11u);
    irr::scene::SMesh* srcMesh = src.getMesh();
    ASSERT_NE(srcMesh, nullptr);

    // Construct a separate chunk to be overwritten by move assignment.
    TerrainChunk dst(heights.data(), gridSize, cellSize, /*chunkId=*/22u);
    ASSERT_NE(dst.getMesh(), nullptr);
    EXPECT_EQ(dst.getChunkId(), 22u);

    // Move-assign src into dst — dst's old mesh is dropped, src mesh transferred.
    dst = std::move(src);

    EXPECT_EQ(dst.getMesh(), srcMesh)
        << "Move assignment must transfer mesh pointer from source to destination";
    EXPECT_EQ(dst.getChunkId(), 11u)
        << "Move assignment must transfer chunkId from source";
    EXPECT_EQ(src.getMesh(), nullptr)
        << "Move assignment must null the source mesh pointer";
}

// ---------------------------------------------------------------------------
// TerrainChunk_MoveAssignment_SelfAssignment_IsNoop
//
// Verifies that move self-assignment (dst = std::move(dst)) does not crash
// and leaves the chunk in a valid state.
// The standard guarantees self-move is technically UB for some containers,
// but our implementation explicitly checks this == &other.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, MoveAssignment_SelfAssignment_IsNoop) {
    const int gridSize   = 8;
    const float cellSize = 2.0f;
    auto heights = makeFlatHeightmap(gridSize);

    TerrainChunk chunk(heights.data(), gridSize, cellSize, /*chunkId=*/33u);
    irr::scene::SMesh* originalMesh = chunk.getMesh();
    ASSERT_NE(originalMesh, nullptr);

    // Self-assignment: the implementation checks (this != &other) and skips.
    chunk = std::move(chunk);  // NOLINT(clang-analyzer-cplusplus.Move)

    // Mesh pointer must remain valid.
    EXPECT_EQ(chunk.getMesh(), originalMesh)
        << "Move self-assignment must leave the mesh pointer unchanged";
}

// ---------------------------------------------------------------------------
// TerrainChunk_GetHeightmap_MatchesInputData
//
// Verifies that getHeightmap() returns the exact heights passed to the
// 3-parameter constructor.
// ---------------------------------------------------------------------------
TEST(TerrainChunk, GetHeightmap_MatchesInputData) {
    const int gridSize  = 4;
    const float cellSize = 1.0f;
    const int vertCount  = (gridSize + 1) * (gridSize + 1);

    std::vector<float> heights(vertCount);
    for (int i = 0; i < vertCount; ++i) {
        heights[i] = static_cast<float>(i) * 0.1f;
    }

    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    const auto& stored = chunk.getHeightmap();
    ASSERT_EQ(stored.size(), static_cast<size_t>(vertCount));
    for (int i = 0; i < vertCount; ++i) {
        EXPECT_FLOAT_EQ(stored[static_cast<size_t>(i)], heights[static_cast<size_t>(i)])
            << "getHeightmap()[" << i << "] must match input height";
    }
}

// ---------------------------------------------------------------------------
// RC_GTEST_PROP: TerrainChunk_ArbitraryGrid_NeverDegenerateBoundingBox
//
// Property: for ANY gridSize in [4, 64] and ANY positive cellSize, the
// bounding box X and Z extents are strictly > 0, and Y extent >= 0.
//
// The shrinking generator for gridSize explores failure cases automatically.
// NiceMock is not needed here — no mock objects are involved.
// ---------------------------------------------------------------------------
RC_GTEST_PROP(TerrainChunk, ArbitraryGrid_NeverDegenerateBoundingBox, ()) {
    const int gridSize = *rc::gen::inRange(4, 65);  // [4, 64] inclusive
    // cellSize in (0, 20] — must be strictly positive to produce non-degenerate XZ extent.
    const float cellSize = static_cast<float>(*rc::gen::inRange(1, 21));

    auto heights = makeFlatHeightmap(gridSize);
    TerrainChunk chunk(heights.data(), gridSize, cellSize);

    RC_ASSERT(chunk.getMesh() != nullptr);
    RC_ASSERT(chunk.getMesh()->getMeshBufferCount() > 0u);

    const auto& bbox = chunk.getMesh()->getBoundingBox();
    RC_ASSERT(bbox.MaxEdge.X - bbox.MinEdge.X > 0.0f);
    // Y extent >= 0 is the correct bound for flat terrain (may be exactly 0).
    RC_ASSERT(bbox.MaxEdge.Y - bbox.MinEdge.Y >= 0.0f);
    RC_ASSERT(bbox.MaxEdge.Z - bbox.MinEdge.Z > 0.0f);

    // Vertex count must match (gridSize+1)^2.
    const unsigned int expected =
        static_cast<unsigned int>((gridSize + 1) * (gridSize + 1));
    RC_ASSERT(chunk.getMesh()->getMeshBuffer(0)->getVertexCount() == expected);
}
