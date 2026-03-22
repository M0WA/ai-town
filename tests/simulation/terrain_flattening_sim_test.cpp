// terrain_flattening_sim_test.cpp — Phase 10b Feature 1 simulation test.
//
// Verifies that IrrlichtRenderer::placeBuildingMesh() calls
// ITerrainQuery::setTileHeight() and that getHeightAt() returns the
// post-flatten height.  Tested via a ManualTerrainQuery stub and a
// NiceMock<MockRenderer> whose ON_CALL for placeBuildingMesh simulates
// the three-step flattening pattern executed in IrrlichtRenderer.
//
// Three-step placement pattern (IrrlichtRenderer::placeBuildingMesh):
//   1. preY = getHeightAt(tileX, tileZ)
//   2. setTileHeight(tileX, tileZ, preY)
//   3. postY = getHeightAt(tileX, tileZ)  — used as scene-node Y
//
// IRenderer placement methods carry no Y parameter — height verification
// goes through ManualTerrainQuery, not MockRenderer.
//
// Label: "unit" (no display or GL context required)
// Target: simulation_tests (links aitown_sim; terrain_tests cannot link
//         aitown_sim without a circular dependency)
// Spec ref: implementation/phase-10b.md §test-dev-cpp
//           architecture/graphics-architecture/procedural-terrain.md §setTileHeight

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "MockAudioSystem.h"
#include "MockRenderer.h"
#include "ManualRNG.h"
#include "ManualClock.h"
#include "ManualTerrainQuery.h"

using ::testing::NiceMock;
using ::testing::_;
using ::testing::Invoke;

// ---------------------------------------------------------------------------
// TerrainFlatteningSimTest fixture
//
// Uses NiceMock for both renderer_ and audio_ — the assertion is on terrain
// state, not renderer or audio state.  NiceMock suppresses unexpected-call
// failures from incidental callbacks (setMusicIntensity, setTimeOfDay, etc.).
//
// The ON_CALL on placeBuildingMesh simulates the three-step IrrlichtRenderer
// flattening pattern so the test exercises the ManualTerrainQuery stateful form
// without requiring a real OpenGL context.
// ---------------------------------------------------------------------------
class TerrainFlatteningSimTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    ManualRNG    rng_;     // default: non-strict, float=0.9f, int=0
    ManualClock  clock_;
    ManualTerrainQuery terrain_;

    // sim_ declared LAST — destroyed first (reverse declaration order).
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        // Configure terrain: before flattening returns 5.0f, after returns 3.0f.
        terrain_.setHeightBeforeFlattening(5.0f);
        terrain_.setHeightAfterFlattening(3.0f);

        // Wire the three-step flattening pattern into MockRenderer::placeBuildingMesh.
        // This simulates what IrrlichtRenderer::placeBuildingMesh() does:
        //   1. preY  = getHeightAt(tileX, tileZ)      // reads 5.0f (before)
        //   2. setTileHeight(tileX, tileZ, preY)       // sets m_flattened = true
        //   3. postY = getHeightAt(tileX, tileZ)       // now reads 3.0f (after)
        // The ON_CALL captures &terrain_ by pointer so it operates on the live state.
        ManualTerrainQuery* terrain_ptr = &terrain_;
        ON_CALL(renderer_, placeBuildingMesh(_, _, _))
            .WillByDefault(Invoke([terrain_ptr](int tileX, int tileZ,
                                               const std::string& /*assetBaseName*/) {
                const float preY = terrain_ptr->getHeightAt(tileX, tileZ);
                terrain_ptr->setTileHeight(tileX, tileZ, preY);
                // postY = terrain_ptr->getHeightAt(tileX, tileZ);
                // (not used by mock — in IrrlichtRenderer this positions the node)
            }));

        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        sim_->setSpeed(SpeedMultiplier::x1);
    }

    void TearDown() override {
        sim_.reset();
    }
};

// ---------------------------------------------------------------------------
// TerrainFlatteningSimTest / TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight
//
// Steps:
//   1. Assert terrain_ is NOT yet flattened before any placement.
//   2. Place a residential zone tile via ICitySimulation::placeZone().
//      CitySimulation calls renderer_.placeBuildingMesh(), which triggers
//      the ON_CALL action that simulates the three-step flattening pattern.
//   3. Assert terrain_.m_flattened == true — confirms setTileHeight() was called.
//   4. Assert terrain_.getHeightAt(0, 0) returns 3.0f (m_heightAfterFlat).
//
// Spec ref: implementation/phase-10b.md §test-dev-cpp
//           architecture/graphics-architecture/procedural-terrain.md §setTileHeight
// ---------------------------------------------------------------------------
TEST_F(TerrainFlatteningSimTest, TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight) {
    // 1. Before placement: not yet flattened.
    ASSERT_FALSE(terrain_.m_flattened)
        << "ManualTerrainQuery must not be flattened before zone placement";
    ASSERT_FLOAT_EQ(terrain_.getHeightAt(0, 0), 5.0f)
        << "getHeightAt() must return m_heightBeforeFlat (5.0f) before setTileHeight()";

    // 2. Place a residential zone tile at (0, 0).
    //    Phase 11h: placeZone requires a road within 3 tiles.
    sim_->placeRoad(1, 0, 0);
    //    placeZone() calls renderer_.placeBuildingMesh(), which triggers the ON_CALL
    //    that executes the three-step flattening: getHeightAt → setTileHeight → getHeightAt.
    sim_->placeZone(/*tileX=*/0, /*tileZ=*/0, ZoneType::Residential,
                    DensityTier::Low, /*earthworksCostOverride=*/0);

    // 3. The ON_CALL must have invoked terrain_.setTileHeight(), setting m_flattened = true.
    EXPECT_TRUE(terrain_.m_flattened)
        << "ManualTerrainQuery::m_flattened must be true after zone placement — "
           "confirms that placeBuildingMesh invoked ITerrainQuery::setTileHeight()";

    // 4. getHeightAt() must now return the post-flatten value.
    EXPECT_FLOAT_EQ(terrain_.getHeightAt(0, 0), 3.0f)
        << "getHeightAt() must return m_heightAfterFlat (3.0f) after setTileHeight() "
           "was invoked by the placeBuildingMesh three-step flattening pattern";
}
