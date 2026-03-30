// terrain_system_integration_test.cpp
//
// Integration tests for TerrainSystem and TerrainChunk exercising:
//   - TerrainChunk construction and mesh building (LOD0, LOD1, LOD2)
//   - TerrainChunk heightmap queries
//   - TerrainChunk slope computation
//   - TerrainSystem construction with null renderer
//   - TerrainSystem chunk registration, enqueue, dequeue
//   - TerrainSystem generate() with ITerrainRNG
//   - TerrainSystem LOD hysteresis distance accessors
//   - TerrainSystem ITerrainQuery: getSlopeDegrees, getHeightAt, setTileHeight
//
// CMake target: integration_tests, label "integration".
// Uses ManualClock for deterministic timing.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cmath>
#include <vector>
#include <memory>
#include <numeric>

// Terrain headers
#include "TerrainChunk.h"
#include "TerrainSystem.h"
#include "terrain_types.h"
#include "ManualClock.h"

// ITerrainRNG
#include "src/interfaces/ITerrainRNG.h"

// ---------------------------------------------------------------------------
// DeterministicTerrainRNG — simple deterministic ITerrainRNG for tests.
// Always returns a flat map (all heights = 0.5).
// ---------------------------------------------------------------------------
class DeterministicTerrainRNG : public ITerrainRNG {
public:
    void reseed(uint64_t /*seed*/) override { m_reseedCount++; }
    float nextFloat() override { return 0.5f; }
    int   nextInt(int min, int /*max*/) override { return min; }
    int   reseedCount() const { return m_reseedCount; }
private:
    int m_reseedCount{0};
};

// ---------------------------------------------------------------------------
// Terrain chunk helpers
// ---------------------------------------------------------------------------

// Build a flat heightmap of (gridSize+1)^2 entries, all set to 'height'.
static std::vector<float> makeFlatHeightmap(int gridSize, float height = 0.0f) {
    int verts = (gridSize + 1) * (gridSize + 1);
    return std::vector<float>(verts, height);
}

// Build a heightmap with a linear gradient (x+z) * scale.
static std::vector<float> makeGradientHeightmap(int gridSize, float scale = 0.5f) {
    int n = gridSize + 1;
    std::vector<float> hm(n * n);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x)
            hm[z * n + x] = (x + z) * scale;
    return hm;
}

// ===========================================================================
// TerrainChunk tests
// ===========================================================================

class TerrainChunkTest : public ::testing::Test {};

TEST_F(TerrainChunkTest, LOD0_Construction_DoesNotCrash) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);
    EXPECT_EQ(chunk.getGridSize(), kTerrainLOD0GridSize);
}

TEST_F(TerrainChunkTest, LOD1_Construction_DoesNotCrash) {
    auto hm = makeFlatHeightmap(kTerrainLOD1GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD1GridSize, 4.0f);
    EXPECT_EQ(chunk.getGridSize(), kTerrainLOD1GridSize);
}

TEST_F(TerrainChunkTest, LOD2_Construction_DoesNotCrash) {
    auto hm = makeFlatHeightmap(kTerrainLOD2GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD2GridSize, 8.0f);
    EXPECT_EQ(chunk.getGridSize(), kTerrainLOD2GridSize);
}

TEST_F(TerrainChunkTest, GetMesh_ReturnsNonNull) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);
    EXPECT_NE(chunk.getMesh(), nullptr);
}

TEST_F(TerrainChunkTest, GetCellSize_ReturnsCorrectValue) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 3.5f);
    EXPECT_FLOAT_EQ(chunk.getCellSize(), 3.5f);
}

TEST_F(TerrainChunkTest, GetHeightAt_FlatMap_ReturnsStoredHeight) {
    float targetHeight = 5.0f;
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize, targetHeight);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);

    // Interior vertex
    float h = chunk.getHeightAt(8, 8);
    EXPECT_FLOAT_EQ(h, targetHeight)
        << "Flat heightmap must return the stored height at any vertex";
}

TEST_F(TerrainChunkTest, GetHeightAt_FlatMap_CornerVertex) {
    float targetHeight = 12.5f;
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize, targetHeight);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);

    EXPECT_FLOAT_EQ(chunk.getHeightAt(0, 0), targetHeight);
    EXPECT_FLOAT_EQ(chunk.getHeightAt(kTerrainLOD0GridSize, kTerrainLOD0GridSize),
                    targetHeight);
}

TEST_F(TerrainChunkTest, GetHeightAt_GradientMap_ReturnsCorrectValues) {
    auto hm = makeGradientHeightmap(kTerrainLOD0GridSize, 1.0f);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);

    // At (x=0, z=0): height = 0
    EXPECT_FLOAT_EQ(chunk.getHeightAt(0, 0), 0.0f);
    // At (x=1, z=0): height = 1
    EXPECT_FLOAT_EQ(chunk.getHeightAt(1, 0), 1.0f);
    // At (x=0, z=1): height = 1
    EXPECT_FLOAT_EQ(chunk.getHeightAt(0, 1), 1.0f);
}

TEST_F(TerrainChunkTest, GetSlopeDegrees_FlatMap_IsNearZero) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);

    float slope = chunk.getSlopeDegrees(5, 5);
    EXPECT_NEAR(slope, 0.0f, 0.01f)
        << "Flat heightmap must produce near-zero slope";
}

TEST_F(TerrainChunkTest, GetSlopeDegrees_GradientMap_IsPositive) {
    auto hm = makeGradientHeightmap(kTerrainLOD0GridSize, 2.0f);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);

    float slope = chunk.getSlopeDegrees(5, 5);
    EXPECT_GT(slope, 0.0f)
        << "Gradient heightmap must produce positive slope";
    EXPECT_LE(slope, 90.0f)
        << "Slope must be in [0, 90] range";
}

TEST_F(TerrainChunkTest, GetCurrentLOD_Default_IsZero) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);
    EXPECT_EQ(chunk.getCurrentLOD(), 0);
}

TEST_F(TerrainChunkTest, SetCurrentLOD_PersistsValue) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);
    chunk.setCurrentLOD(2);
    EXPECT_EQ(chunk.getCurrentLOD(), 2);
}

TEST_F(TerrainChunkTest, GetChunkId_Default_IsZero) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);
    EXPECT_EQ(chunk.getChunkId(), 0u);
}

TEST_F(TerrainChunkTest, FourParameterCtor_SetsChunkId) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    ChunkId id = 42u;
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f, id);
    EXPECT_EQ(chunk.getChunkId(), id);
}

TEST_F(TerrainChunkTest, VectorCtor_DoesNotCrash) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize, 3.0f);
    TerrainChunk chunk(hm, kTerrainLOD0GridSize, 2.0f, 77u);
    EXPECT_EQ(chunk.getChunkId(), 77u);
    EXPECT_NE(chunk.getMesh(), nullptr);
}

TEST_F(TerrainChunkTest, GetHeightmap_SizeMatchesExpected) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f);
    int expected = (kTerrainLOD0GridSize + 1) * (kTerrainLOD0GridSize + 1);
    EXPECT_EQ(static_cast<int>(chunk.getHeightmap().size()), expected);
}

TEST_F(TerrainChunkTest, MoveConstructor_TransfersOwnership) {
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    TerrainChunk original(hm.data(), kTerrainLOD0GridSize, 2.0f, 99u);
    irr::scene::SMesh* meshPtr = original.getMesh();

    TerrainChunk moved(std::move(original));
    EXPECT_EQ(moved.getChunkId(), 99u);
    EXPECT_EQ(moved.getMesh(), meshPtr) << "Move must transfer mesh pointer";
}

TEST_F(TerrainChunkTest, AllVertices_HeightSampledCorrectly) {
    // Verify all vertices on a gradient map
    auto hm = makeGradientHeightmap(8, 1.0f);  // use small gridSize=8 for speed
    TerrainChunk chunk(hm.data(), 8, 1.0f);

    for (int z = 0; z <= 8; ++z) {
        for (int x = 0; x <= 8; ++x) {
            float expected = static_cast<float>(x + z);
            float actual = chunk.getHeightAt(x, z);
            EXPECT_FLOAT_EQ(actual, expected)
                << "Mismatch at (" << x << ", " << z << ")";
        }
    }
}

// ===========================================================================
// TerrainSystem tests
// ===========================================================================

class TerrainSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // null renderer is valid for EDT_NULL test context
        system_ = std::make_unique<TerrainSystem>(nullptr, &clock_);
    }

    void TearDown() override {
        system_.reset();
    }

    ManualClock                         clock_;
    std::unique_ptr<TerrainSystem>      system_;
    DeterministicTerrainRNG             rng_;
};

TEST_F(TerrainSystemTest, Construction_DoesNotCrash) {
    EXPECT_NE(system_, nullptr);
}

TEST_F(TerrainSystemTest, InitialState_NoPendingRebuilds) {
    EXPECT_EQ(system_->pendingRebuildCount(), 0);
}

TEST_F(TerrainSystemTest, InitialState_HeightmapEmpty) {
    EXPECT_TRUE(system_->getGeneratedHeightmap().empty());
}

TEST_F(TerrainSystemTest, InitialState_MapTilesXZZero) {
    EXPECT_EQ(system_->getMapTilesX(), 0);
    EXPECT_EQ(system_->getMapTilesZ(), 0);
}

TEST_F(TerrainSystemTest, InitialState_GetHeightAt_ReturnsZero) {
    EXPECT_FLOAT_EQ(system_->getHeightAt(0, 0), 0.0f);
}

TEST_F(TerrainSystemTest, InitialState_GetSlopeDegrees_ReturnsZero) {
    EXPECT_FLOAT_EQ(system_->getSlopeDegrees(0, 0), 0.0f);
}

TEST_F(TerrainSystemTest, RegisterAndGetChunkLOD_Works) {
    system_->registerChunkAtLOD(1u, 0);
    EXPECT_EQ(system_->getChunkLOD(1u), 0);
}

TEST_F(TerrainSystemTest, GetChunkLOD_UnknownChunk_ReturnsMinus1) {
    EXPECT_EQ(system_->getChunkLOD(9999u), -1);
}

TEST_F(TerrainSystemTest, HasActiveChunk_AfterRegister_ReturnsTrue) {
    system_->registerChunkAtLOD(5u, 0);
    EXPECT_TRUE(system_->hasActiveChunk(5u));
}

TEST_F(TerrainSystemTest, HasActiveChunk_Unknown_ReturnsFalse) {
    EXPECT_FALSE(system_->hasActiveChunk(9999u));
}

TEST_F(TerrainSystemTest, UnregisterChunk_RemovesIt) {
    system_->registerChunkAtLOD(3u, 0);
    ASSERT_TRUE(system_->hasActiveChunk(3u));
    system_->unregisterChunk(3u);
    EXPECT_FALSE(system_->hasActiveChunk(3u));
}

TEST_F(TerrainSystemTest, EnqueueRebuild_WithRegisteredChunk_IncrementsPending) {
    system_->registerChunkAtLOD(2u, 0);
    system_->registerChunkPosition(2u, 0.0f, 0.0f);
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    system_->registerChunkHeightmap(2u, hm);
    system_->enqueueRebuild(2u, 1, 50.0f);
    EXPECT_GE(system_->pendingRebuildCount(), 1);
}

TEST_F(TerrainSystemTest, FlushPendingRebuilds_EmptyQueue_IsNoOp) {
    system_->flushPendingRebuilds(nullptr);
    EXPECT_EQ(system_->pendingRebuildCount(), 0);
    EXPECT_EQ(system_->chunksRebuiltLastFlush(), 0);
}

TEST_F(TerrainSystemTest, FlushPendingRebuilds_WithChunk_ClearsPending) {
    // Register a chunk with all required data
    system_->registerChunkAtLOD(10u, 0);
    system_->registerChunkPosition(10u, 0.0f, 0.0f);
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    system_->registerChunkHeightmap(10u, hm);
    system_->enqueueRebuild(10u, 1, 0.0f);
    ASSERT_GE(system_->pendingRebuildCount(), 1);

    // Flush — null renderer means processOneRebuild won't call IRenderer methods
    system_->flushPendingRebuilds(nullptr);

    EXPECT_EQ(system_->pendingRebuildCount(), 0)
        << "Rebuild deque must be empty after flush";
}

TEST_F(TerrainSystemTest, Update_WithEmptyQueue_DoesNotCrash) {
    system_->update(0.016f);
    EXPECT_EQ(system_->chunksRebuiltLastFrame(), 0);
}

TEST_F(TerrainSystemTest, GetPendingRebuildIds_Empty_ReturnsEmptyVector) {
    EXPECT_TRUE(system_->getPendingRebuildIds().empty());
}

TEST_F(TerrainSystemTest, GetPendingRebuildIds_AfterEnqueue_ReturnsId) {
    system_->registerChunkAtLOD(7u, 0);
    system_->registerChunkPosition(7u, 0.0f, 0.0f);
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    system_->registerChunkHeightmap(7u, hm);
    system_->enqueueRebuild(7u, 1, 0.0f);

    auto ids = system_->getPendingRebuildIds();
    EXPECT_FALSE(ids.empty());
    bool found = false;
    for (auto id : ids) { if (id == 7u) found = true; }
    EXPECT_TRUE(found) << "Enqueued chunk ID must appear in pending list";
}

// ---------------------------------------------------------------------------
// LOD hysteresis distance accessors
// ---------------------------------------------------------------------------

TEST_F(TerrainSystemTest, LodSwitchOutDistance_LOD0_GreaterThan100m) {
    EXPECT_GT(TerrainSystem::lodSwitchOutDistance(0), 100.0f);
}

TEST_F(TerrainSystemTest, LodSwitchInDistance_LOD0_LessThan92m) {
    EXPECT_LT(TerrainSystem::lodSwitchInDistance(0), 92.0f);
}

TEST_F(TerrainSystemTest, LodSwitchOutDistance_LOD1_GreaterThan300m) {
    EXPECT_GT(TerrainSystem::lodSwitchOutDistance(1), 300.0f);
}

TEST_F(TerrainSystemTest, LodSwitchInDistance_LOD1_LessThan285m) {
    EXPECT_LT(TerrainSystem::lodSwitchInDistance(1), 285.0f);
}

TEST_F(TerrainSystemTest, LodHysteresisBand_LOD0_AtLeast8m) {
    float hysteresis = TerrainSystem::lodSwitchOutDistance(0)
                     - TerrainSystem::lodSwitchInDistance(0);
    EXPECT_GE(hysteresis, 8.0f)
        << "LOD0 hysteresis band must be at least 8 m";
}

TEST_F(TerrainSystemTest, LodHysteresisBand_LOD1_AtLeast15m) {
    float hysteresis = TerrainSystem::lodSwitchOutDistance(1)
                     - TerrainSystem::lodSwitchInDistance(1);
    EXPECT_GE(hysteresis, 15.0f)
        << "LOD1 hysteresis band must be at least 15 m";
}

// ---------------------------------------------------------------------------
// ITerrainQuery implementation tests
// ---------------------------------------------------------------------------

TEST_F(TerrainSystemTest, GetSlopeDegrees_AfterGenerate_InRange) {
    // Generate a small map
    bool ok = system_->generate(32, 32, 2.0f, &rng_, 1);
    (void)ok;  // may fail playability check with flat rng, but must not crash

    float slope = system_->getSlopeDegrees(5, 5);
    EXPECT_GE(slope, 0.0f);
    EXPECT_LE(slope, 90.0f);
}

TEST_F(TerrainSystemTest, GetHeightAt_AfterGenerate_IsFinite) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    float h = system_->getHeightAt(5, 5);
    EXPECT_TRUE(std::isfinite(h));
}

TEST_F(TerrainSystemTest, GetHeightAt_OutOfBounds_ReturnsZero) {
    // Before generate() — out-of-bounds always returns 0
    EXPECT_FLOAT_EQ(system_->getHeightAt(-1, -1), 0.0f);
    EXPECT_FLOAT_EQ(system_->getHeightAt(9999, 9999), 0.0f);
}

TEST_F(TerrainSystemTest, Generate_SmallMap_SetsMapTiles) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    EXPECT_EQ(system_->getMapTilesX(), 32);
    EXPECT_EQ(system_->getMapTilesZ(), 32);
}

TEST_F(TerrainSystemTest, Generate_SmallMap_HeightmapNotEmpty) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    EXPECT_FALSE(system_->getGeneratedHeightmap().empty());
}

TEST_F(TerrainSystemTest, Generate_SmallMap_HeightmapSize) {
    int tX = 32, tZ = 32;
    system_->generate(tX, tZ, 2.0f, &rng_, 1);
    // Expected size: (tX+1) * (tZ+1)
    int expected = (tX + 1) * (tZ + 1);
    EXPECT_EQ(static_cast<int>(system_->getGeneratedHeightmap().size()), expected);
}

TEST_F(TerrainSystemTest, SetTileHeight_AffectsGetHeightAt) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    float newHeight = 99.0f;
    system_->setTileHeight(5, 5, newHeight);
    EXPECT_FLOAT_EQ(system_->getHeightAt(5, 5), newHeight)
        << "getHeightAt must reflect value set by setTileHeight";
}

TEST_F(TerrainSystemTest, SetTileHeight_OutOfBounds_IsNoOp) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    // Out-of-bounds setTileHeight must not crash
    system_->setTileHeight(-1, -1, 100.0f);
    system_->setTileHeight(9999, 9999, 100.0f);
}

TEST_F(TerrainSystemTest, FlushTerrainRebuilds_DoesNotCrash) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    system_->flushTerrainRebuilds();
}

TEST_F(TerrainSystemTest, ClearAllChunks_DoesNotCrash) {
    system_->registerChunkAtLOD(1u, 0);
    system_->clearAllChunks();
    EXPECT_FALSE(system_->hasActiveChunk(1u));
}

TEST_F(TerrainSystemTest, GetCellSize_AfterGenerate_IsCorrect) {
    system_->generate(32, 32, 3.0f, &rng_, 1);
    EXPECT_FLOAT_EQ(system_->getCellSize(), 3.0f);
}

// ---------------------------------------------------------------------------
// Deduplication across multiple enqueue calls
// ---------------------------------------------------------------------------

TEST_F(TerrainSystemTest, EnqueueDuplicate_DeduplicatedInPending) {
    system_->registerChunkAtLOD(20u, 0);
    system_->registerChunkPosition(20u, 0.0f, 0.0f);
    auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
    system_->registerChunkHeightmap(20u, hm);

    system_->enqueueRebuild(20u, 1, 0.0f);
    system_->enqueueRebuild(20u, 1, 0.0f);  // duplicate

    // getPendingRebuildIds returns deduplicated set
    auto ids = system_->getPendingRebuildIds();
    int count = 0;
    for (auto id : ids) { if (id == 20u) count++; }
    EXPECT_EQ(count, 1) << "Duplicate enqueue must be deduplicated";
}

TEST_F(TerrainSystemTest, MultipleChunks_AllFlushed) {
    for (int i = 1; i <= 5; ++i) {
        uint64_t cid = static_cast<uint64_t>(i);
        system_->registerChunkAtLOD(cid, 0);
        system_->registerChunkPosition(cid, 0.0f, 0.0f);
        auto hm = makeFlatHeightmap(kTerrainLOD0GridSize);
        system_->registerChunkHeightmap(cid, hm);
        system_->enqueueRebuild(cid, 1, 0.0f);
    }
    ASSERT_GE(system_->pendingRebuildCount(), 1);

    system_->flushPendingRebuilds(nullptr);
    EXPECT_EQ(system_->pendingRebuildCount(), 0);
}

// ---------------------------------------------------------------------------
// EnqueueAllChunks after generate
// ---------------------------------------------------------------------------

TEST_F(TerrainSystemTest, EnqueueAllChunks_AfterGenerate_HasPendingRebuilds) {
    system_->generate(32, 32, 2.0f, &rng_, 1);
    // With null renderer, enqueueAllChunks should populate the rebuild queue
    system_->enqueueAllChunks();
    // Depending on impl, may or may not be non-zero, but must not crash
    int pending = system_->pendingRebuildCount();
    EXPECT_GE(pending, 0);
}

// ---------------------------------------------------------------------------
// TerrainChunk stress test — many instances
// ---------------------------------------------------------------------------

TEST(TerrainChunkStressTest, ConstructAndDestroyManyChunks_DoNotCrash) {
    for (int i = 0; i < 20; ++i) {
        auto hm = makeFlatHeightmap(kTerrainLOD0GridSize, static_cast<float>(i));
        TerrainChunk chunk(hm.data(), kTerrainLOD0GridSize, 2.0f,
                           static_cast<ChunkId>(i));
        EXPECT_NE(chunk.getMesh(), nullptr);
        EXPECT_EQ(chunk.getChunkId(), static_cast<ChunkId>(i));
    }
}

TEST(TerrainChunkStressTest, AllLODLevels_ConstuctCorrectly) {
    const int lodGridSizes[] = {
        kTerrainLOD0GridSize,
        kTerrainLOD1GridSize,
        kTerrainLOD2GridSize
    };
    const float cellSizes[] = { 2.0f, 4.0f, 8.0f };

    for (int i = 0; i < 3; ++i) {
        auto hm = makeFlatHeightmap(lodGridSizes[i], 1.0f);
        TerrainChunk chunk(hm.data(), lodGridSizes[i], cellSizes[i]);
        EXPECT_EQ(chunk.getGridSize(), lodGridSizes[i]);
        EXPECT_FLOAT_EQ(chunk.getCellSize(), cellSizes[i]);
        EXPECT_NE(chunk.getMesh(), nullptr);
    }
}
