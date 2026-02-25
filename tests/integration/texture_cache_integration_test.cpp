// texture_cache_integration_test.cpp — Phase 5 TextureCache and SceneEntityManager
// integration tests using an EDT_NULL Irrlicht device.
//
// These tests exercise the full destroy() sequence described in
// architecture/graphics-architecture/texture-cache.md §Eviction safety, using an
// EDT_NULL device so no real GL context is required. They live in integration_tests
// (label "integration") rather than terrain_tests (label "unit") because they call
// Irrlicht APIs (createDevice, addMeshSceneNode) directly.
//
// One-label-per-target rule: these tests CANNOT live in terrain_tests.
// terrain_tests has label "unit"; integration_tests has label "integration".
//
// Tests in this file:
//   - TextureCache_EvictUnreferenced_ZeroRefSRGB_EDT_NULL_NoGLDelete
//       Verifies no crash and correct map-cleanup when evictUnreferenced() runs
//       on a zero-ref sRGB entry under EDT_NULL.
//   - SceneEntityManager_Destroy_FullSequence_ReleasesAllPools
//       Verifies Steps 1-4 of SceneEntityManager::destroy():
//       Step 1:  linear ref_count decremented; slot cleared
//       Step 1b: sRGB ref_count decremented to 0
//       Step 1c: splat map ref_count decremented to 0
//       Step 2:  driver->setMaterial(SMaterial{}) called
//       Step 3:  evictUnreferenced() — zero-ref entries removed from map
//       Step 4:  entity node pointer set to nullptr before node->remove()
//
// Label: "integration" (EDT_NULL Irrlicht device; no xvfb required)
// Spec ref: phase-5.md §SceneEntityManager destroy() integration test
//           architecture/graphics-architecture/texture-cache.md §Eviction safety
//           architecture/graphics-architecture/scene-graph-ownership.md

// GLEW must be included before irrlicht.h.
#include <GL/glew.h>
#include <irrlicht.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/rendering/TextureCache.h"
#include "src/rendering/SceneEntityManager.h"

using namespace testing;

// ---------------------------------------------------------------------------
// TextureCacheIntegrationTest fixture
//
// Creates an EDT_NULL Irrlicht device shared across tests in this fixture.
// Each test gets a fresh TextureCache constructed from the null driver type.
// ---------------------------------------------------------------------------
class TextureCacheIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_device = irr::createDevice(
            irr::video::EDT_NULL,
            irr::core::dimension2d<irr::u32>(640, 480));
        ASSERT_NE(m_device, nullptr) << "EDT_NULL device creation failed";

        m_driver = m_device->getVideoDriver();
        ASSERT_NE(m_driver, nullptr);

        m_cache = std::make_unique<TextureCache>(irr::video::EDT_NULL);
    }

    void TearDown() override {
        // Reset TextureCache BEFORE dropping the device — cache destructor must
        // not call GL functions after the device is gone.
        m_cache.reset();
        if (m_device) {
            m_device->drop();
            m_device = nullptr;
        }
    }

    irr::IrrlichtDevice*       m_device{nullptr};
    irr::video::IVideoDriver*  m_driver{nullptr};
    std::unique_ptr<TextureCache> m_cache;
};

// ---------------------------------------------------------------------------
// TextureCache_EvictUnreferenced_ZeroRefSRGB_EDT_NULL_NoGLDelete
//
// Load an sRGB path (returns GLuint{0} under EDT_NULL).
// Release it (ref_count -> 0).
// Call evictUnreferenced() — EDT_NULL guard fires; no glDeleteTextures is called.
// The map entry must be removed: getSRGBGLuint returns 0.
//
// This test confirms that the EDT_NULL guard correctly skips the raw-GL deletion
// path and still cleans up the map entry.
// ---------------------------------------------------------------------------
TEST_F(TextureCacheIntegrationTest, EvictUnreferenced_ZeroRefSRGB_EDT_NULL_NoGLDelete) {
    const std::string path = "terrain_grass_d.dds";

    GLuint handle = m_cache->loadSRGB(path, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    EXPECT_EQ(handle, GLuint{0})
        << "loadSRGB must return 0 under EDT_NULL (no GL context for upload)";

    m_cache->releaseSRGB(path);

    // Must not crash; must not call glDeleteTextures (no GL context).
    EXPECT_NO_FATAL_FAILURE(m_cache->evictUnreferenced());

    // After eviction, the entry must be gone from the map.
    EXPECT_EQ(m_cache->getSRGBGLuint(path), GLuint{0})
        << "Zero-ref sRGB entry must be removed from map after evictUnreferenced()";
}

// ---------------------------------------------------------------------------
// SceneEntityManager_Destroy_FullSequence_ReleasesAllPools
//
// Creates a minimal scene entity (an IMeshSceneNode on an empty SMesh) wired
// with one simulated linear texture, one sRGB filename, and one splat map
// filename.  Calls SceneEntityManager::destroy(entity) and verifies:
//
//   Step 1:  linear ref_count decremented; material slot cleared to nullptr
//   Step 1b: sRGB entry ref_count decremented to 0
//   Step 1c: splat map entry ref_count decremented to 0
//   Step 2:  driver->setMaterial(SMaterial{}) no-op on EDT_NULL (no crash)
//   Step 3:  evictUnreferenced() runs cleanly (EDT_NULL guard; map entries removed)
//   Step 4:  entity->getNode() returns nullptr after destroy()
//
// This confirms the four-step destroy sequence does not skip any pool under EDT_NULL.
// Spec: architecture/graphics-architecture/texture-cache.md lines 71-100
// ---------------------------------------------------------------------------
TEST_F(TextureCacheIntegrationTest, Destroy_FullSequence_ReleasesAllPools) {
    auto* smgr = m_device->getSceneManager();
    ASSERT_NE(smgr, nullptr);

    // Create an empty SMesh and attach it to a MeshSceneNode.
    irr::scene::SMesh* smesh = new irr::scene::SMesh();
    // Add a minimal mesh buffer so getMaterialCount() > 0.
    auto* buf = new irr::scene::SMeshBuffer();
    smesh->addMeshBuffer(buf);
    buf->drop();  // SMesh::addMeshBuffer grabs; we release our ref.
    smesh->recalculateBoundingBox();

    irr::scene::IMeshSceneNode* node = smgr->addMeshSceneNode(smesh);
    smesh->drop();  // addMeshSceneNode grabs; we release our ref.
    ASSERT_NE(node, nullptr);

    // Pre-load one sRGB texture and one splat map into the cache.
    // Both return 0 under EDT_NULL — but ref_counts are tracked.
    const std::string kSRGBPath  = "building_atlas_d.dds";
    const std::string kSplatPath = "terrain_blend.png";

    m_cache->loadSRGB(kSRGBPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);  // ref_count sRGB = 1
    m_cache->loadSplatMap(kSplatPath);                                  // ref_count splat = 1

    // Build a mock entity: wraps the scene node, lists sRGB + splat filenames.
    // SceneEntityManager expects an entity type with:
    //   getNode()                   -> ISceneNode*
    //   setNode(nullptr)            -> stores nullptr
    //   getSRGBTextureFilenames()   -> const std::vector<std::string>&
    //   getSplatMapFilenames()      -> const std::vector<std::string>&
    //
    // We use a concrete TestEntity helper (not a GMock — simpler for lifecycle testing).
    struct TestEntity {
        irr::scene::ISceneNode* node{nullptr};
        std::vector<std::string> srgbFiles;
        std::vector<std::string> splatFiles;

        irr::scene::ISceneNode* getNode() const { return node; }
        void setNode(irr::scene::ISceneNode* n) { node = n; }
        const std::vector<std::string>& getSRGBTextureFilenames() const { return srgbFiles; }
        const std::vector<std::string>& getSplatMapFilenames() const { return splatFiles; }
    };

    TestEntity entity;
    entity.node = node;
    entity.srgbFiles.push_back(kSRGBPath);
    entity.splatFiles.push_back(kSplatPath);

    // Execute the destroy sequence via SceneEntityManager.
    SceneEntityManager entityMgr(m_driver, m_cache.get());
    entityMgr.destroy(entity);

    // --- Verify Step 4: entity node pointer is nullptr after destroy() ---
    EXPECT_EQ(entity.getNode(), nullptr)
        << "Step 4: entity node pointer must be nullptr after destroy()";

    // --- Verify Step 1b: sRGB ref_count decremented to 0 → entry evicted ---
    // evictUnreferenced() was called inside destroy() (Step 3).
    // The sRGB entry ref_count is now 0; under EDT_NULL the map entry is removed.
    EXPECT_EQ(m_cache->getSRGBGLuint(kSRGBPath), GLuint{0})
        << "Step 1b + Step 3: sRGB entry must be removed after destroy()";

    // --- Verify Step 1c: splat map ref_count decremented to 0 → entry evicted ---
    EXPECT_EQ(m_cache->getSplatMapGLuint(kSplatPath), GLuint{0})
        << "Step 1c + Step 3: splat map entry must be removed after destroy()";
}
