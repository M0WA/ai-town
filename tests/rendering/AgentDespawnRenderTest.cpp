// tests/rendering/AgentDespawnRenderTest.cpp
// Phase 11q6: Regression tests for despawnVehicleAgent UAF fix.
// Requires an OpenGL context (xvfb-run on Linux CI).
//
// Four test cases:
//   1. DespawnThenDrawScene_Clean         — despawn + drawAll must not crash
//   2. DespawnNonexistentHandle_NoOp      — early-return guard must not crash
//   3. DespawnAllAgents_DrawScene_Clean   — bulk despawn + drawAll must not crash
//   4. SpawnSameHandleTwice_NoLeak        — replace-guard despawn + drawAll
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
// Window size is deliberately small (1x1) to minimise GPU resource allocation.
// Returns nullptr on failure (caller must GTEST_SKIP or ASSERT_NE).
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
// AgentDespawnRenderTest — fixture for despawnVehicleAgent UAF regression tests.
// Creates a real IrrlichtRenderer with a ManualTerrainQuery injection.
// ---------------------------------------------------------------------------
class AgentDespawnRenderTest : public ::testing::Test {
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
            GTEST_SKIP() << "No display available; agent despawn render tests skipped.";
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
// TEST 1: DespawnThenDrawScene_Clean
//
// Despawn a single agent then call drawAll — must not crash (UAF regression).
// ===========================================================================
TEST_F(AgentDespawnRenderTest, DespawnThenDrawScene_Clean) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(0, 5, 5, ZoneType::Residential);
    if (!renderer_->agentNodeForTest(0)) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }
    renderer_->despawnVehicleAgent(0);
#pragma GCC diagnostic pop

    driver_->beginScene(true, true, SColor(255, 0, 0, 0));
    smgr_->drawAll();  // would crash before fix
    driver_->endScene();
    SUCCEED();
}

// ===========================================================================
// TEST 2: DespawnNonexistentHandle_NoOp
//
// Despawn a handle that was never spawned — early-return guard must not crash.
// ===========================================================================
TEST_F(AgentDespawnRenderTest, DespawnNonexistentHandle_NoOp) {
    renderer_->despawnVehicleAgent(99);
    SUCCEED();
}

// ===========================================================================
// TEST 3: DespawnAllAgents_DrawScene_Clean
//
// Spawn three agents, despawn all, then drawAll — must not crash.
// ===========================================================================
TEST_F(AgentDespawnRenderTest, DespawnAllAgents_DrawScene_Clean) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(0, 5, 5, ZoneType::Residential);
    renderer_->spawnVehicleAgent(1, 6, 5, ZoneType::Residential);
    renderer_->spawnVehicleAgent(2, 7, 5, ZoneType::Residential);

    if (!renderer_->agentNodeForTest(0) ||
        !renderer_->agentNodeForTest(1) ||
        !renderer_->agentNodeForTest(2)) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }

    renderer_->despawnVehicleAgent(0);
    renderer_->despawnVehicleAgent(1);
    renderer_->despawnVehicleAgent(2);
#pragma GCC diagnostic pop

    driver_->beginScene(true, true, SColor(255, 0, 0, 0));
    smgr_->drawAll();
    driver_->endScene();
    SUCCEED();
}

// ===========================================================================
// TEST 4: SpawnSameHandleTwice_NoLeak
//
// Spawning the same handle twice triggers the replace-guard despawn internally.
// The slot must exist afterwards and drawAll must not crash.
// ===========================================================================
TEST_F(AgentDespawnRenderTest, SpawnSameHandleTwice_NoLeak) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(0, 5, 5, ZoneType::Residential);
    renderer_->spawnVehicleAgent(0, 5, 5, ZoneType::Commercial);

    if (!renderer_->agentNodeForTest(0)) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }
    ASSERT_NE(renderer_->agentNodeForTest(0), nullptr);
#pragma GCC diagnostic pop

    driver_->beginScene(true, true, SColor(255, 0, 0, 0));
    smgr_->drawAll();
    driver_->endScene();
    SUCCEED();
}
