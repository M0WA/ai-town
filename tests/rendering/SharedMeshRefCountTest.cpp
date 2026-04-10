// tests/rendering/SharedMeshRefCountTest.cpp
// Phase 11q6: Regression test for shared mesh ref-count fix.
// Requires an OpenGL context (xvfb-run on Linux CI).
//
// One test case:
//   LastAgentDespawn_OtherNodesUnaffected — despawning one agent that shares a mesh
//   with another must not crash when drawAll renders the surviving node.
//
// Registered inline in add_executable(opengl_tests ...) in CMakeLists.txt.
// CTest label: "requires-opengl" (run under xvfb-run in CI).

#include <gtest/gtest.h>

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "src/rendering/IrrlichtRenderer.h"
#include "src/rendering/render_constants.h"
#include "ManualTerrainQuery.h"

#include <cstdlib>   // std::getenv

using namespace irr;
using namespace irr::video;

// ---------------------------------------------------------------------------
// Helper: create a minimal EDT_OPENGL device.
// ---------------------------------------------------------------------------
static IrrlichtDevice* createTestDevice() {
    SIrrlichtCreationParameters params;
    params.DriverType    = EDT_OPENGL;
    params.WindowSize    = core::dimension2d<u32>(1, 1);
    params.Bits          = 32;
    params.ZBufferBits   = 24;
    params.Fullscreen    = false;
    params.Stencilbuffer = false;
    params.AntiAlias     = 0;
    params.Vsync         = false;
    params.LoggingLevel  = ELL_NONE;
    return createDeviceEx(params);
}

// ---------------------------------------------------------------------------
// SharedMeshRefCountTest — fixture for shared mesh ref-count regression test.
// ---------------------------------------------------------------------------
class SharedMeshRefCountTest : public ::testing::Test {
protected:
    IrrlichtDevice*    device_{nullptr};
    IrrlichtRenderer*  renderer_{nullptr};
    scene::ISceneManager* smgr_{nullptr};
    IVideoDriver*      driver_{nullptr};
    ManualTerrainQuery terrain_;

    void SetUp() override {
        device_ = createTestDevice();
        if (!device_) {
            const char* display = std::getenv("DISPLAY");
            if (display && display[0] != '\0') {
                FAIL() << "createDevice(EDT_OPENGL) returned null with DISPLAY set — "
                          "OpenGL/Mesa is misconfigured in CI.";
            }
            GTEST_SKIP() << "No display available; shared mesh ref-count tests skipped.";
        }

        smgr_   = device_->getSceneManager();
        driver_ = device_->getVideoDriver();
        renderer_ = new IrrlichtRenderer(device_, /*uiManager=*/nullptr);
        renderer_->setTerrainQuery(&terrain_);

        // Set flat terrain for all relevant tiles.
        for (int x = 0; x <= 10; ++x) {
            for (int z = 0; z <= 10; ++z) {
                terrain_.setHeightAt(x, z, 0.0f);
            }
        }
    }

    void TearDown() override {
        delete renderer_;
        renderer_ = nullptr;
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// ===========================================================================
// TEST: LastAgentDespawn_OtherNodesUnaffected
//
// Handles 0 and 3 both have (handle % 3 == 0), same zone (Residential) ->
// same IAnimatedMesh* from cache. Despawning handle 0 must not invalidate
// the mesh for handle 3. drawAll must not crash.
// ===========================================================================
TEST_F(SharedMeshRefCountTest, LastAgentDespawn_OtherNodesUnaffected) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(0, 5, 5, ZoneType::Residential);
    renderer_->spawnVehicleAgent(3, 6, 5, ZoneType::Residential);

    auto* node0 = renderer_->agentNodeForTest(0);
    auto* node3 = renderer_->agentNodeForTest(3);
#pragma GCC diagnostic pop

    if (!node0 || !node3) {
        GTEST_SKIP() << "Mesh assets not found — spawn produced no scene node.";
    }

    // Precondition: both nodes share the same IMesh* from the scene manager cache.
    ASSERT_EQ(node0->getMesh(), node3->getMesh())
        << "Precondition: handles 0 and 3 (same variant index) must share the same IMesh*.";

    // Despawn handle 0 — node 3 must survive with its mesh intact.
    renderer_->despawnVehicleAgent(0);

    driver_->beginScene(true, true, SColor(255, 0, 0, 0));
    smgr_->drawAll();  // would crash before fix (node 3 dangling mesh)
    driver_->endScene();
    SUCCEED();  // ASAN confirms mesh still alive for node 3
}
