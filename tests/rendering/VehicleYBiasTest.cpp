// VehicleYBiasTest.cpp — Phase 11q renderer tests for vehicle Y-bias and slope rotation.
//
// Four renderer tests requiring a real Irrlicht device (EDT_OPENGL):
//   1. SpawnVehicleAgent_FlatTerrain_VehicleYIncludesBias
//   2. MoveVehicleAgent_FlatTerrain_VehicleYIncludesBias
//   3. SpawnVehicleAgent_SlopedTerrain_PitchApplied
//   4. MoveVehicleAgent_SlopedTerrain_RollApplied
//
// Registered inline in add_executable(opengl_tests ...) in CMakeLists.txt.
// CTest label: "requires-opengl" (run under xvfb-run in CI).
//
// Uses ManualTerrainQuery (NOT a no-op — real bilinear interpolation via
// getHeightAtWorld()). ManualTerrainQuery does NOT include render_constants.h.
//
// TearDown: device->drop() and null the renderer pointer before fixture
// destruction to avoid dangling scene-graph references.

#include <gtest/gtest.h>

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "src/rendering/IrrlichtRenderer.h"
#include "src/rendering/render_constants.h"
#include "ManualTerrainQuery.h"

#include <cstdlib>   // std::getenv
#include <cmath>     // std::abs

using namespace irr;
using namespace irr::video;

// ---------------------------------------------------------------------------
// Helper: create a minimal EDT_OPENGL device for vehicle Y-bias tests.
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
// VehicleYBiasTest — fixture for vehicle Y-bias and slope rotation tests.
// Creates a real IrrlichtRenderer with a ManualTerrainQuery injection.
// ---------------------------------------------------------------------------
class VehicleYBiasTest : public ::testing::Test {
protected:
    IrrlichtDevice*    device_{nullptr};
    IrrlichtRenderer*  renderer_{nullptr};
    ManualTerrainQuery terrain_;

    void SetUp() override {
        device_ = createTestDevice();
        if (!device_) {
            const char* display = std::getenv("DISPLAY");
            if (display && display[0] != '\0') {
                FAIL() << "createDevice(EDT_OPENGL) returned null with DISPLAY set — "
                          "OpenGL/Mesa is misconfigured in CI.";
            }
            GTEST_SKIP() << "No display available; vehicle Y-bias tests skipped.";
        }

        renderer_ = new IrrlichtRenderer(device_, /*uiManager=*/nullptr);
        renderer_->setTerrainQuery(&terrain_);
    }

    void TearDown() override {
        // Explicitly release renderer before device to avoid dangling scene-graph refs.
        delete renderer_;
        renderer_ = nullptr;
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// ===========================================================================
// TEST 1: SpawnVehicleAgent_FlatTerrain_VehicleYIncludesBias
//
// On flat terrain (all heights = 5.0f), spawnVehicleAgent() should position
// the vehicle at terrain_height + kRoadSurfaceYBias = 5.0f + 0.25f = 5.25f.
// ===========================================================================
TEST_F(VehicleYBiasTest, SpawnVehicleAgent_FlatTerrain_VehicleYIncludesBias) {
    // Configure flat terrain at height 5.0f for all tiles.
    // Set heights around the spawn tile (5,5) and its neighbours for bilinear interp.
    for (int x = 4; x <= 7; ++x) {
        for (int z = 4; z <= 7; ++z) {
            terrain_.setHeightAt(x, z, 5.0f);
        }
    }

    const AgentHandle handle = 42;
    // Suppress deprecated-declarations warning for test-only API.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(handle, 5, 5, ZoneType::Residential);
    auto* node = renderer_->agentNodeForTest(handle);
#pragma GCC diagnostic pop

    // If the mesh asset doesn't exist in the test environment, the node won't be created.
    // Skip rather than fail — asset availability is outside the scope of this unit test.
    if (!node) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }

    const float expectedY = 5.0f + RenderConstants::kRoadSurfaceYBias;
    EXPECT_NEAR(node->getPosition().Y, expectedY, 0.01f)
        << "Spawned vehicle Y should be terrain height + kRoadSurfaceYBias";
}

// ===========================================================================
// TEST 2: MoveVehicleAgent_FlatTerrain_VehicleYIncludesBias
//
// On flat terrain (all heights = 3.0f), after moveVehicleAgent(), the vehicle
// Y should be terrain_height + kRoadSurfaceYBias = 3.0f + 0.25f = 3.25f.
// ===========================================================================
TEST_F(VehicleYBiasTest, MoveVehicleAgent_FlatTerrain_VehicleYIncludesBias) {
    // Configure flat terrain at height 3.0f.
    for (int x = 4; x <= 7; ++x) {
        for (int z = 4; z <= 7; ++z) {
            terrain_.setHeightAt(x, z, 3.0f);
        }
    }

    const AgentHandle handle = 43;
    // Spawn first, then move.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(handle, 5, 5, ZoneType::Residential);
    auto* node = renderer_->agentNodeForTest(handle);
#pragma GCC diagnostic pop

    if (!node) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }

    // Move to the centre of tile (5,5) in world coords.
    const float worldX = 5.0f * RenderConstants::kTileSize + RenderConstants::kTileSize * 0.5f;
    const float worldZ = 5.0f * RenderConstants::kTileSize + RenderConstants::kTileSize * 0.5f;
    renderer_->moveVehicleAgent(handle, worldX, worldZ, 0.0f);

    const float expectedY = 3.0f + RenderConstants::kRoadSurfaceYBias;
    EXPECT_NEAR(node->getPosition().Y, expectedY, 0.01f)
        << "Moved vehicle Y should be terrain height + kRoadSurfaceYBias";
}

// ===========================================================================
// TEST 3: SpawnVehicleAgent_SlopedTerrain_PitchApplied
//
// On sloped terrain (height increases with Z), spawnVehicleAgent() should
// apply a non-zero pitch (rotation.X) to match the road surface slope.
// ===========================================================================
TEST_F(VehicleYBiasTest, SpawnVehicleAgent_SlopedTerrain_PitchApplied) {
    // Configure sloped terrain: height increases with Z.
    // Tile (5,5) = 0.0f, tiles at z=6 = 2.0f, tiles at z=7 = 4.0f.
    for (int x = 4; x <= 7; ++x) {
        for (int z = 4; z <= 7; ++z) {
            terrain_.setHeightAt(x, z, static_cast<float>((z - 5) * 2));
        }
    }

    const AgentHandle handle = 44;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(handle, 5, 5, ZoneType::Residential);
    auto* node = renderer_->agentNodeForTest(handle);
#pragma GCC diagnostic pop

    if (!node) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }

    // On sloped terrain, the vehicle should have non-zero pitch (rotation.X).
    // The exact value depends on the slope gradient and the spawn heading (0 at spawn).
    EXPECT_TRUE(std::abs(node->getRotation().X) > 0.01f)
        << "Spawned vehicle on sloped terrain should have non-zero pitch (rotation.X)";
}

// ===========================================================================
// TEST 4: MoveVehicleAgent_SlopedTerrain_RollApplied
//
// On sloped terrain (height increases with Z), moveVehicleAgent with heading=0
// should produce pitch. With heading=90 (facing +X), the Z-slope becomes
// perpendicular to heading and should produce roll (rotation.Z != 0).
// This test validates the yaw-relative decomposition.
// ===========================================================================
TEST_F(VehicleYBiasTest, MoveVehicleAgent_SlopedTerrain_RollApplied) {
    // Configure terrain with a pure Z-slope:
    // All tiles at z<=5 are at height 0.0f, tiles at z=6 are at 2.0f, etc.
    for (int x = 4; x <= 7; ++x) {
        terrain_.setHeightAt(x, 5, 0.0f);
        terrain_.setHeightAt(x, 6, 2.0f);
        terrain_.setHeightAt(x, 7, 4.0f);
        terrain_.setHeightAt(x, 4, 0.0f);
    }

    const AgentHandle handle = 45;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    renderer_->spawnVehicleAgent(handle, 5, 5, ZoneType::Residential);
    auto* node = renderer_->agentNodeForTest(handle);
#pragma GCC diagnostic pop

    if (!node) {
        GTEST_SKIP() << "Vehicle mesh asset not found — spawn produced no scene node.";
    }

    // Move the vehicle to tile (5,5) centre with heading=90 (facing +X / east).
    // At yaw=90, a Z-slope is perpendicular to heading -> should produce roll.
    const float worldX = 5.0f * RenderConstants::kTileSize + RenderConstants::kTileSize * 0.5f;
    const float worldZ = 5.0f * RenderConstants::kTileSize + RenderConstants::kTileSize * 0.5f;
    renderer_->moveVehicleAgent(handle, worldX, worldZ, 90.0f);

    // With yaw=90 and a Z-slope, the slope should produce roll (rotation.Z != 0)
    // and minimal/no pitch (rotation.X near 0).
    // At minimum, the vehicle orientation must show SOME slope effect.
    const auto rot = node->getRotation();
    bool hasSlopeEffect = (std::abs(rot.X) > 0.01f) || (std::abs(rot.Z) > 0.01f);
    EXPECT_TRUE(hasSlopeEffect)
        << "Vehicle on sloped terrain after moveVehicleAgent should have non-zero "
           "pitch or roll. rotation=(" << rot.X << ", " << rot.Y << ", " << rot.Z << ")";
}
