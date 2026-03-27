// tests/simulation/city_simulation_terrain_flatten_test.cpp
//
// Phase 11m D3: CitySimulation border-ring terrain flattening tests.
// Verifies Phase 11m behaviour: after placeZone(), adjacent road tiles in the
// border-ring (N+2)×(N+2) zone are flattened to match the zone's flat height.
// Non-road adjacent tiles are NOT flattened.
//
// Phase 11m logic (CitySimulation.cpp §border-ring):
//   flatHeight = terrain.getHeightAt(tileX, tileZ)  // origin tile height before modifications
//   For each border-ring tile (dx=-1..N, dz=-1..N, skip inner NxN):
//     if tile is a road: terrain.setTileHeight(bx, bz, flatHeight)
//
// IMPORTANT: m_mapWidth/m_mapHeight default to 0 which prevents border-ring
// processing (map-edge guard: bx >= m_mapWidth skips all tiles). Tests must
// call setMapDimensions() on the concrete CitySimulation to allow processing.
//
// Mock policy: NiceMock for renderer_ + audio_ (incidental callbacks suppressed).
// TearDown contract: sim_.reset() before mock destructors.
//
// Added to simulation_tests via:
//   target_sources(simulation_tests PRIVATE tests/simulation/city_simulation_terrain_flatten_test.cpp)

#include "src/simulation/CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualRNG.h"
#include "tests/simulation/ManualClock.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;

// ---------------------------------------------------------------------------
// TerrainFlattenBorderRingTest fixture
// ---------------------------------------------------------------------------
class TerrainFlattenBorderRingTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    ManualRNG                 rng_;     // default: float=0.9f, no service degradation
    ManualClock               clock_;
    ManualTerrainQuery        terrain_;

    // sim_ declared LAST — destroyed first.
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        sim_->setSpeed(SpeedMultiplier::x1);

        // Must set map dimensions so border-ring map-edge guard passes.
        // (m_mapWidth / m_mapHeight default to 0 → all border-ring tiles are
        // out-of-bounds and setTileHeight is skipped without this call.)
        auto* cs = static_cast<CitySimulation*>(sim_.get());
        cs->setMapDimensions(10, 10);
    }

    void TearDown() override {
        sim_.reset();
    }

    // Helper: drain all notifications.
    void drainNotifications() {
        SimulationNotification notif{};
        while (sim_->pollPendingNotification(notif)) {}
    }
};

// ---------------------------------------------------------------------------
// Test 1: CitySimulation_PlaceZone_RoadAdjacentTile_FlattenedToMatchZone
//
// Setup:
//   - Place road at (1,0) so zone at (0,0) has road adjacent (Chebyshev 1).
//   - Set terrain height at (0,0) to 0.0f (default) and at (1,0) to 5.0f.
//   - Place 1×1 Residential zone at (0,0).
//
// Expected:
//   - flatHeight = getHeightAt(0,0) = 0.0f (origin tile height before modifications).
//   - Road tile (1,0) is in the border ring (dx=1, dz=0) and is a road.
//   - setTileHeight(1, 0, 0.0f) must be called → terrain height at (1,0) == 0.0f.
// ---------------------------------------------------------------------------
TEST_F(TerrainFlattenBorderRingTest,
       CitySimulation_PlaceZone_RoadAdjacentTile_FlattenedToMatchZone)
{
    // Set terrain heights before placement.
    terrain_.setHeightAt(0, 0, 0.0f);  // zone origin — flat
    terrain_.setHeightAt(1, 0, 5.0f);  // adjacent road tile — elevated

    // Place road at (1,0) (adjacent to future zone at (0,0)).
    sim_->placeRoad(1, 0, 0);
    drainNotifications();

    // Place 1×1 Residential zone at (0,0).
    // flatHeight = getHeightAt(0,0) = 0.0f.
    // Border ring: tile (1,0) is a road → setTileHeight(1, 0, 0.0f).
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low, 0);
    drainNotifications();

    // Road tile (1,0) must have been flattened to match zone flatHeight (0.0f).
    EXPECT_FLOAT_EQ(terrain_.getHeightAt(1, 0), 0.0f)
        << "Adjacent road tile (1,0) should be flattened to zone flatHeight=0.0f";
}

// ---------------------------------------------------------------------------
// Test 2: CitySimulation_PlaceZone_NonRoadAdjacentTile_NotFlattened
//
// Setup:
//   - No road at (1,0) — tile is plain terrain at 5.0f.
//   - Place a different road far enough away that zone placement is allowed.
//     Road at (0,1) is Chebyshev 1 from (0,0) — use that.
//   - Place 1×1 Residential zone at (0,0).
//
// Expected:
//   - Tile (1,0) is NOT a road → border-ring logic skips it.
//   - terrain height at (1,0) remains 5.0f (unchanged).
// ---------------------------------------------------------------------------
TEST_F(TerrainFlattenBorderRingTest,
       CitySimulation_PlaceZone_NonRoadAdjacentTile_NotFlattened)
{
    // Set terrain heights before placement.
    terrain_.setHeightAt(0, 0, 0.0f);  // zone origin — flat
    terrain_.setHeightAt(1, 0, 5.0f);  // plain terrain tile — NOT a road

    // Place enabling road at (0,1) — Chebyshev 1 from (0,0), within proximity.
    sim_->placeRoad(0, 1, 0);
    drainNotifications();

    // Place 1×1 Residential zone at (0,0).
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low, 0);
    drainNotifications();

    // Tile (1,0) is plain terrain (not a road) — must NOT be flattened.
    EXPECT_FLOAT_EQ(terrain_.getHeightAt(1, 0), 5.0f)
        << "Non-road adjacent tile (1,0) should NOT be flattened by border-ring logic";
}
