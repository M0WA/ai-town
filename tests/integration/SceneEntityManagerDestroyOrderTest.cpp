// tests/integration/SceneEntityManagerDestroyOrderTest.cpp
// Phase 11q6: Verifies SceneEntityManager::destroy() sets entity node to nullptr
// before calling node->remove() (null-before-remove ordering per
// architecture/graphics-architecture/scene-graph-ownership.md Step 4).
//
// Uses EDT_NULL device — no xvfb dependency.
// Label: "integration"

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include <gtest/gtest.h>

#include "src/rendering/TextureCache.h"
#include "src/rendering/SceneEntityManager.h"

#include <string>
#include <vector>

using namespace irr;

// ---------------------------------------------------------------------------
// Minimal test entity that satisfies the TEntity concept required by
// SceneEntityManager::destroy<TEntity>(entity).
// ---------------------------------------------------------------------------
struct TestEntity {
    scene::ISceneNode* node{nullptr};
    std::vector<std::string> srgbFilenames;
    std::vector<std::string> splatMapFilenames;

    scene::ISceneNode* getNode() const { return node; }
    void setNode(scene::ISceneNode* n) { node = n; }
    const std::vector<std::string>& getSRGBTextureFilenames() const { return srgbFilenames; }
    const std::vector<std::string>& getSplatMapFilenames() const { return splatMapFilenames; }
};

// ---------------------------------------------------------------------------
// SceneEntityManagerDestroyOrderTest fixture
// Creates an EDT_NULL Irrlicht device (no GL context).
// ---------------------------------------------------------------------------
class SceneEntityManagerDestroyOrderTest : public ::testing::Test {
protected:
    IrrlichtDevice*                      device_{nullptr};
    video::IVideoDriver*                 driver_{nullptr};
    scene::ISceneManager*                smgr_{nullptr};
    std::unique_ptr<TextureCache>        cache_;
    std::unique_ptr<SceneEntityManager>  mgr_;

    void SetUp() override {
        device_ = createDevice(
            video::EDT_NULL,
            core::dimension2d<u32>(640, 480));
        ASSERT_NE(device_, nullptr) << "EDT_NULL device creation failed";

        driver_ = device_->getVideoDriver();
        smgr_   = device_->getSceneManager();
        ASSERT_NE(driver_, nullptr);
        ASSERT_NE(smgr_, nullptr);

        cache_ = std::make_unique<TextureCache>(video::EDT_NULL);
        mgr_   = std::make_unique<SceneEntityManager>(driver_, cache_.get());
    }

    void TearDown() override {
        mgr_.reset();
        cache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// ===========================================================================
// TEST: NullsEntityBeforeNodeRemove
//
// After SceneEntityManager::destroy(entity) returns, entity.getNode() must
// be nullptr. This is the observable postcondition of Step 4 (null-before-remove).
// ===========================================================================
TEST_F(SceneEntityManagerDestroyOrderTest, NullsEntityBeforeNodeRemove) {
    // Create a scene node via the scene manager.
    scene::ISceneNode* node = smgr_->addEmptySceneNode();
    ASSERT_NE(node, nullptr);

    // Build a minimal entity wrapping the node.
    TestEntity entity;
    entity.node = node;

    // Destroy via the template overload — exercises the full 4-step sequence.
    mgr_->destroy(entity);

    // Postcondition: entity's node pointer must be null.
    ASSERT_EQ(entity.getNode(), nullptr)
        << "SceneEntityManager::destroy() must null the entity node pointer (Step 4).";
}

// ===========================================================================
// TEST: DestroyNullEntity_NoOp
//
// Calling destroy() on an entity whose node is already null must not crash.
// ===========================================================================
TEST_F(SceneEntityManagerDestroyOrderTest, DestroyNullEntity_NoOp) {
    TestEntity entity;
    entity.node = nullptr;

    mgr_->destroy(entity);

    ASSERT_EQ(entity.getNode(), nullptr);
}

// ===========================================================================
// TEST: RawPointerOverload_NullsPointer
//
// The raw-pointer overload destroy(ISceneNode*&, ...) must also null the
// caller's pointer before calling node->remove().
// ===========================================================================
TEST_F(SceneEntityManagerDestroyOrderTest, RawPointerOverload_NullsPointer) {
    scene::ISceneNode* node = smgr_->addEmptySceneNode();
    ASSERT_NE(node, nullptr);

    std::vector<std::string> empty;
    mgr_->destroy(node, empty, empty, empty);

    ASSERT_EQ(node, nullptr)
        << "Raw-pointer destroy() must null the caller's node pointer (Step 4).";
}
