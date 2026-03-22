// tests/terrain/road_lane_test.cpp
//
// Phase 11h Deliverable 5b: Road Lane Geometry Constant Verification Tests
//
// Tests verify the named lane geometry constants in src/rendering/render_constants.h
// and document the expected world-space offset math for vehicle agents.
//
// These are pure constant-verification tests — no CitySimulation, no MockRenderer,
// no display required. All five tests are unit tests (no integration label).
//
// Added to terrain_tests via:
//   target_sources(terrain_tests PRIVATE tests/terrain/road_lane_test.cpp)
// Do NOT call add_executable(terrain_tests ...) or aitown_add_tests(terrain_tests ...) again.
//
// Spec reference: implementation/phase-11h.md §3a and §5b
//   kLaneCenterOffset      = 1.875f  (metres from road center to lane center)
//   kCarriagewayHalfWidth  = 3.75f   (half of 7.5 m carriageway)
//
// Lane assignment convention (keep-right, two-way):
//   Northbound (+Z direction): world X = tileCenter X + kLaneCenterOffset
//   Southbound (-Z direction): world X = tileCenter X - kLaneCenterOffset
//   East/West:  same offset applied to Z axis (90° rotation).
//   Intersection tiles (3+ road neighbours): lane offset = 0; snap to tile center.

#include "src/rendering/render_constants.h"
#include <gtest/gtest.h>
#include <cmath>

// kTileSize — tile world width in metres. Used in offset math tests.
static constexpr float kTileSize = 10.0f;

// ---------------------------------------------------------------------------
// RoadLane_CarriagewayHalfWidth_Is3_75m
//
// Verifies that kCarriagewayHalfWidth equals 3.75f (half of the 7.5 m carriageway
// that covers 3/4 of the 10 m tile width).
// Spec: implementation/phase-11h.md §3a, §4c, §5b.
// ---------------------------------------------------------------------------
TEST(RoadLaneConstantTest, RoadLane_CarriagewayHalfWidth_Is3_75m)
{
    EXPECT_FLOAT_EQ(RenderConstants::kCarriagewayHalfWidth, 3.75f)
        << "kCarriagewayHalfWidth must equal 3.75f (half of 7.5 m carriageway, "
           "covering 3/4 of the 10 m tile width).";
}

// ---------------------------------------------------------------------------
// RoadLane_LaneCenterOffset_Is1_875m
//
// Verifies that kLaneCenterOffset equals 1.875f (distance from road center to
// the center of one lane; half of kCarriagewayHalfWidth).
// Spec: implementation/phase-11h.md §3a, §4c, §5b.
// ---------------------------------------------------------------------------
TEST(RoadLaneConstantTest, RoadLane_LaneCenterOffset_Is1_875m)
{
    EXPECT_FLOAT_EQ(RenderConstants::kLaneCenterOffset, 1.875f)
        << "kLaneCenterOffset must equal 1.875f (half of the 7.5 m carriageway / 2 = 1.875 m).";
}

// ---------------------------------------------------------------------------
// RoadLane_NorthboundAgent_PositiveXOffset
//
// Documents and verifies the offset arithmetic for a northbound (+Z direction)
// vehicle agent. Per the keep-right convention, the northbound lane is the
// right lane (local X = +kLaneCenterOffset from road center).
//
// For a tile at tileX = 5 (world center at tileX * kTileSize + kTileSize/2):
//   tileCenterX = 5 * 10.0f + 5.0f = 55.0f
//   expectedWorldX = tileCenterX + kLaneCenterOffset = 55.0f + 1.875f = 56.875f
//
// Spec: implementation/phase-11h.md §3b ("right lane (local X = +1.875m)"),
//       §4e ("northbound (+Z) agents shift +X by kLaneCenterOffset").
// ---------------------------------------------------------------------------
TEST(RoadLaneOffsetMathTest, RoadLane_NorthboundAgent_PositiveXOffset)
{
    const int   tileX       = 5;
    const float tileCenterX = static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f;
    const float expectedWorldX = tileCenterX + RenderConstants::kLaneCenterOffset;

    // Value verification (documents the computed offset, not a mock call).
    EXPECT_FLOAT_EQ(expectedWorldX, 56.875f)
        << "Northbound agent at tile X=5 must have world X = 55.0 + 1.875 = 56.875";

    // Verify the offset is positive (right lane for northbound).
    EXPECT_GT(expectedWorldX, tileCenterX)
        << "Northbound (+Z direction) agent world X must be > tile center X "
           "(keep-right: right lane = +kLaneCenterOffset from road center)";
}

// ---------------------------------------------------------------------------
// RoadLane_SouthboundAgent_NegativeXOffset
//
// Documents and verifies the offset arithmetic for a southbound (-Z direction)
// vehicle agent. Per the keep-right convention, the southbound lane is the
// left lane (local X = -kLaneCenterOffset from road center).
//
// For a tile at tileX = 5:
//   tileCenterX = 55.0f
//   expectedWorldX = tileCenterX - kLaneCenterOffset = 55.0f - 1.875f = 53.125f
//
// Spec: implementation/phase-11h.md §3b ("left lane (local X = -1.875m)"),
//       §4e ("southbound agents shift -X").
// ---------------------------------------------------------------------------
TEST(RoadLaneOffsetMathTest, RoadLane_SouthboundAgent_NegativeXOffset)
{
    const int   tileX       = 5;
    const float tileCenterX = static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f;
    const float expectedWorldX = tileCenterX - RenderConstants::kLaneCenterOffset;

    EXPECT_FLOAT_EQ(expectedWorldX, 53.125f)
        << "Southbound agent at tile X=5 must have world X = 55.0 - 1.875 = 53.125";

    // Verify the offset is negative (left lane for southbound).
    EXPECT_LT(expectedWorldX, tileCenterX)
        << "Southbound (-Z direction) agent world X must be < tile center X "
           "(keep-right: left lane = -kLaneCenterOffset from road center)";
}

// ---------------------------------------------------------------------------
// RoadLane_IntersectionTile_AgentSnapsToTileCenter
//
// Documents the intersection snap rule: at intersection tiles (3 or more road
// neighbours), lane offset = 0. The agent world X/Z equals the tile center.
//
// For a tile at tileX = 3, tileZ = 7:
//   tileCenterX = 3 * 10.0f + 5.0f = 35.0f
//   tileCenterZ = 7 * 10.0f + 5.0f = 75.0f
//   At intersection: worldX = tileCenterX + 0.0f = 35.0f (offset suppressed)
//   After exit:      worldX = tileCenterX + kLaneCenterOffset (offset resumes)
//
// Spec: implementation/phase-11h.md §4e ("at intersection tiles ... agents snap
// to the tile center X/Z (lane offset = 0)"), §3b lane assignment subsection.
// ---------------------------------------------------------------------------
TEST(RoadLaneOffsetMathTest, RoadLane_IntersectionTile_AgentSnapsToTileCenter)
{
    const int   tileX       = 3;
    const int   tileZ       = 7;
    const float tileCenterX = static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f;
    const float tileCenterZ = static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f;

    // At intersection: offset = 0.
    constexpr float kIntersectionOffset = 0.0f;
    const float intersectionWorldX = tileCenterX + kIntersectionOffset;
    const float intersectionWorldZ = tileCenterZ + kIntersectionOffset;

    EXPECT_FLOAT_EQ(intersectionWorldX, tileCenterX)
        << "Agent at intersection tile must snap to tile center X (offset = 0)";
    EXPECT_FLOAT_EQ(intersectionWorldZ, tileCenterZ)
        << "Agent at intersection tile must snap to tile center Z (offset = 0)";

    // After exiting intersection northbound (resuming +Z travel):
    const float exitWorldX = tileCenterX + RenderConstants::kLaneCenterOffset;
    EXPECT_GT(exitWorldX, tileCenterX)
        << "After exiting intersection northbound, lane offset resumes to +kLaneCenterOffset";

    // Consistency: offset at intersection is strictly less than offset on straight segment.
    EXPECT_LT(std::fabs(kIntersectionOffset),
              std::fabs(RenderConstants::kLaneCenterOffset))
        << "Intersection lane offset (0) must be smaller than straight-segment offset (1.875)";
}
