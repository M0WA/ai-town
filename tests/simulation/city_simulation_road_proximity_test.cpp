// tests/simulation/city_simulation_road_proximity_test.cpp
//
// Phase 11m D2: CitySimulation road proximity tests.
// Verifies placeZone() road-proximity guard (Chebyshev distance <= 3 required).
//
// nearestRoadDistance() uses Chebyshev distance (max of |dx|, |dz|).
// The search radius is ±3 tiles from each footprint tile.
//
// Test scenarios:
//   Diagonal road at (3,3) from zone origin (0,0):
//     Chebyshev = max(3,3) = 3 → allowed (within ±3 search window).
//   Diagonal road at (4,4) from zone origin (0,0):
//     Chebyshev = max(4,4) = 4 → outside ±3 search window → blocked.
//   Cardinal road at (3,0) from zone origin (0,0):
//     Chebyshev = max(3,0) = 3 → allowed.
//
// Mock policy: NiceMock<MockRenderer> + NiceMock<MockAudioSystem> to suppress
// incidental callbacks. StrictMock is NOT used here because placement methods
// fire audio + renderer callbacks.
//
// Added to simulation_tests via:
//   target_sources(simulation_tests PRIVATE tests/simulation/city_simulation_road_proximity_test.cpp)

#include "NiceSimulationTestBase.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;

// ---------------------------------------------------------------------------
// RoadProximityTest fixture
// ---------------------------------------------------------------------------
class RoadProximityTest : public NiceSimulationTestBase {
protected:
    // Helper: drain all pending notifications and return true if PlacementBlocked was found.
    bool hasPlacementBlocked() {
        SimulationNotification notif{};
        while (sim_->pollPendingNotification(notif)) {
            if (notif.type == NotificationType::PlacementBlocked) {
                return true;
            }
        }
        return false;
    }

    // Helper: drain all notifications (discard).
    void drainNotifications() {
        SimulationNotification notif{};
        while (sim_->pollPendingNotification(notif)) {}
    }
};

// ---------------------------------------------------------------------------
// Test 1: CitySimulation_PlaceZone_DiagonalRoad_AtChebyshev3_Allowed
//
// Road at (3,3): Chebyshev distance from (0,0) = max(3,3) = 3.
// This is within the ±3 search window (exactly at boundary). placeZone(0,0)
// must succeed — no PlacementBlocked notification, tile isZoned==true.
// ---------------------------------------------------------------------------
TEST_F(RoadProximityTest, CitySimulation_PlaceZone_DiagonalRoad_AtChebyshev3_Allowed)
{
    // Place road at (3,3) — exactly Chebyshev 3 from (0,0).
    sim_->placeRoad(3, 3, 0);
    drainNotifications();

    // Attempt zone at (0,0).
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low, 0);

    // Must NOT produce a PlacementBlocked notification.
    EXPECT_FALSE(hasPlacementBlocked())
        << "placeZone at (0,0) with road at (3,3) [Chebyshev=3] should succeed";

    // Tile must be zoned.
    QueryResult qr = sim_->queryTile(0, 0);
    EXPECT_TRUE(qr.isZoned)
        << "Tile (0,0) should be zoned when road at (3,3) [Chebyshev=3]";
}

// ---------------------------------------------------------------------------
// Test 2: CitySimulation_PlaceZone_DiagonalRoad_AtChebyshev4_Blocked
//
// Road at (4,4): Chebyshev distance from (0,0) = max(4,4) = 4.
// This is outside the ±3 search window; nearestRoadDistance returns INT_MAX.
// placeZone(0,0) must fire PlacementBlocked.
// ---------------------------------------------------------------------------
TEST_F(RoadProximityTest, CitySimulation_PlaceZone_DiagonalRoad_AtChebyshev4_Blocked)
{
    // Place road at (4,4) — Chebyshev 4 from (0,0), outside the ±3 search window.
    sim_->placeRoad(4, 4, 0);
    drainNotifications();

    // Attempt zone at (0,0).
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low, 0);

    // Must produce a PlacementBlocked notification.
    EXPECT_TRUE(hasPlacementBlocked())
        << "placeZone at (0,0) with road at (4,4) [Chebyshev=4] should be blocked";

    // Tile must NOT be zoned.
    QueryResult qr = sim_->queryTile(0, 0);
    EXPECT_FALSE(qr.isZoned)
        << "Tile (0,0) should NOT be zoned when only road is at (4,4) [Chebyshev=4]";
}

// ---------------------------------------------------------------------------
// Test 3: CitySimulation_PlaceZone_CardinalRoad_AtDistance3_Allowed
//
// Road at (3,0): Chebyshev = max(3,0) = 3 (same as Manhattan 3 for cardinal direction).
// Regression test to verify cardinal-direction roads at exactly 3 tiles are allowed.
// ---------------------------------------------------------------------------
TEST_F(RoadProximityTest, CitySimulation_PlaceZone_CardinalRoad_AtDistance3_Allowed)
{
    // Place road at (3,0) — Chebyshev 3 (cardinal, directly to the right).
    sim_->placeRoad(3, 0, 0);
    drainNotifications();

    // Attempt zone at (0,0).
    sim_->placeZone(0, 0, ZoneType::Residential, DensityTier::Low, 0);

    // Must NOT produce a PlacementBlocked notification.
    EXPECT_FALSE(hasPlacementBlocked())
        << "placeZone at (0,0) with road at (3,0) [Chebyshev=3] should succeed";

    // Tile must be zoned.
    QueryResult qr = sim_->queryTile(0, 0);
    EXPECT_TRUE(qr.isZoned)
        << "Tile (0,0) should be zoned when road at (3,0) [Chebyshev=3]";
}
