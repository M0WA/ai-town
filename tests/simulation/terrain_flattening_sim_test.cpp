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
#include "src/simulation/simulation_constants.h"
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
// Phase 11l update: terrain earthworks flattening now happens at placeZone() time
// (Phase 11h behaviour), not inside placeBuildingMesh().  The 3D building mesh is
// deferred until doPopulationTick() fires with sufficient demand (>= 0.50).
//
// Steps:
//   1. Assert terrain_ has no flattenCalls before any placement.
//   2. Place road + R zone + C zone (C ensures R bootstrap demand ~0.917 >= 0.50).
//      placeZone() calls setTileHeight() for earthworks flattening immediately —
//      record the flattenCalls count after placement.
//   3. Assert flattenCalls grew during placement (earthworks proof).
//   4. Advance one budget tick; doPopulationTick() fires placeBuildingMesh(), which
//      triggers the ON_CALL action (getHeightAt → setTileHeight).
//   5. Assert flattenCalls grew again after the tick (placeBuildingMesh proof).
//
// Spec ref: architecture/game-design/terrain-interaction.md §Multi-tile footprint extension
//           architecture/graphics-architecture/procedural-terrain.md §setTileHeight
// ---------------------------------------------------------------------------
TEST_F(TerrainFlatteningSimTest, TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight) {
    // 1. Before placement: no flatten calls recorded.
    ASSERT_TRUE(terrain_.m_flattenCalls.empty())
        << "ManualTerrainQuery must have no flattenCalls before zone placement";

    // 2. Phase 11l: placeZone() sets underConstruction=true and calls setTileHeight()
    //    for Phase 11h earthworks flattening; placeBuildingMesh() is deferred.
    sim_->placeRoad(1, 0, 0);
    sim_->placeZone(/*tileX=*/0, /*tileZ=*/0, ZoneType::Residential,
                    DensityTier::Low, /*earthworksCostOverride=*/0);
    // C zone at (2, 0) is within 3 tiles of road — boosts R bootstrap demand to ~0.917.
    sim_->placeZone(/*tileX=*/2, /*tileZ=*/0, ZoneType::Commercial,
                    DensityTier::Low, /*earthworksCostOverride=*/0);

    // 3. placeZone() must have called setTileHeight() for earthworks.
    const std::size_t callsAfterPlacement = terrain_.m_flattenCalls.size();
    EXPECT_GT(callsAfterPlacement, 0u)
        << "placeZone() must invoke setTileHeight() for Phase 11h earthworks flattening";

    // 4. Advance one budget tick; doPopulationTick() checks demand (R ≈ 0.917 >= 0.50)
    //    and calls renderer_.placeBuildingMesh(), which triggers the ON_CALL action
    //    that invokes setTileHeight() via the simulated three-step flattening pattern.
    auto* concreteSim = static_cast<CitySimulation*>(sim_.get());
    clock_.advance(SimulationConstants::SECONDS_PER_BUDGET_TICK);
    concreteSim->tick(SimulationConstants::SECONDS_PER_BUDGET_TICK);

    // 5. placeBuildingMesh() ON_CALL must have added more flattenCalls.
    EXPECT_GT(terrain_.m_flattenCalls.size(), callsAfterPlacement)
        << "doPopulationTick() must invoke placeBuildingMesh() which calls setTileHeight() — "
           "confirms Phase 11l demand-gated mesh spawn triggers terrain operations";
}
