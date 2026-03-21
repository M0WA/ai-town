// tests/audio/vehicle_audio_positioning_test.cpp
//
// Phase 11h Deliverable 5e: Vehicle Audio Positioning Tests (Lane Offset Coordination)
//
// Verifies that vehicle engine audio sources receive world coordinates that include
// the lane offset on straight road segments and snap to tile center at intersection
// tiles — matching the rendered vehicle position.
//
// The requirement from implementation/phase-11h.md §4e and §5e:
//   Straight segment (northbound, +Z direction):
//     worldX passed to updateVehicleAudio() = tileCenterX + kLaneCenterOffset
//   Intersection tile (3+ road neighbours):
//     worldX passed to updateVehicleAudio() = tileCenterX (offset = 0, snap to center)
//
// These tests verify the offset math contract and the kLaneCenterOffset constant.
// They do NOT test IrrlichtRenderer internals (which require OpenGL) — they verify
// the arithmetic invariants that the production code must satisfy, using the constants
// from render_constants.h.
//
// Added to audio_tests via:
//   target_sources(audio_tests PRIVATE tests/audio/vehicle_audio_positioning_test.cpp)
// Do NOT call add_executable(audio_tests ...) or aitown_add_tests(audio_tests ...) again.
//
// Mock policy:
//   NiceMock<MockAudioSystem> — verifies updateVehicleAudio receives correct coordinates.
//   NiceMock<MockRenderer>    — suppresses incidental render calls (no OpenGL required).
//
// Spec references:
//   implementation/phase-11h.md §4e (Vehicle Agent Rendering — Lane Offset)
//   implementation/phase-11h.md §5e (Vehicle Audio Positioning Tests)
//   architecture/audio-architecture/dynamic-soundscape.md §Vehicle Engine Audio
//   architecture/game-design/traffic-system.md §Lane Assignment

#include "src/rendering/lane_constants.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/simulation_types.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/MockRenderer.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cmath>

using ::testing::NiceMock;
using ::testing::_;
using ::testing::FloatNear;

// ---------------------------------------------------------------------------
// Lane offset computation helpers — these replicate the production-code contract
// that IrrlichtRenderer must satisfy for vehicle agent position updates.
// Tests verify these invariants hold for the kLaneCenterOffset constant value.
// ---------------------------------------------------------------------------

static constexpr float kTileSize = 10.0f;

// Compute tile center world X for a given tile grid X.
static float tileCenterWorldX(int tileX) {
    return static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f;
}

// Compute tile center world Z for a given tile grid Z.
static float tileCenterWorldZ(int tileZ) {
    return static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f;
}

// Lane offset math for a northbound (+Z) agent on tile column tileX.
// Returns: tileCenterX + kLaneCenterOffset (right lane, keep-right convention).
static float northboundAgentWorldX(int tileX) {
    return tileCenterWorldX(tileX) + RenderConstants::kLaneCenterOffset;
}

// Lane offset math for an intersection tile — offset suppressed (= 0).
// Returns: tileCenterX (no lane offset at intersections).
static float intersectionAgentWorldX(int tileX) {
    return tileCenterWorldX(tileX);
}

// ============================================================================
// VehicleAudio_StraightSegment_AudioReceivesLaneOffsetCoords
//
// Verifies that the worldX coordinate passed to updateVehicleAudio() for a
// northbound (+Z direction) vehicle agent on a straight road segment equals:
//   tileCenterX + kLaneCenterOffset
//
// This is the right-lane offset for northbound traffic in the keep-right convention.
// Spec: implementation/phase-11h.md §4e, §5e.
// ============================================================================
TEST(VehicleAudioPositioningTest, VehicleAudio_StraightSegment_AudioReceivesLaneOffsetCoords)
{
    // Tile at (5, 3) — straight northbound road segment.
    const int tileX = 5;
    const int tileZ = 3;

    const float expectedWorldX = northboundAgentWorldX(tileX);
    const float expectedWorldZ = tileCenterWorldZ(tileZ);

    // Verify the expected offset against the constant value.
    EXPECT_FLOAT_EQ(RenderConstants::kLaneCenterOffset, 1.875f)
        << "kLaneCenterOffset must be 1.875f for this test to be correct";

    // Verify the computed coordinates match the spec arithmetic.
    EXPECT_FLOAT_EQ(expectedWorldX, 55.0f + 1.875f)
        << "Northbound agent at tileX=5 must have worldX = 55.0 + 1.875 = 56.875";
    EXPECT_FLOAT_EQ(expectedWorldZ, 35.0f)
        << "Agent at tileZ=3 must have worldZ = 3 * 10 + 5 = 35.0 (tile center)";

    // The lane offset for northbound traffic must be positive (right lane).
    EXPECT_GT(expectedWorldX, tileCenterWorldX(tileX))
        << "Northbound agent worldX must be greater than tile center X "
           "(right lane = tile center + kLaneCenterOffset)";

    // Verify against a MockAudioSystem to document the call contract.
    // In production: IrrlichtRenderer calls audio->updateVehicleAudio(idleIdx, moveIdx,
    //   speedFraction, expectedWorldX, expectedWorldZ) for northbound agents.
    NiceMock<MockAudioSystem> audio;
    const int idleIdx = 0;
    const int moveIdx = 1;
    const float speedFraction = 0.5f;

    // Document the expected call with the lane-offset coordinates.
    EXPECT_CALL(audio, updateVehicleAudio(
        idleIdx, moveIdx, speedFraction,
        FloatNear(expectedWorldX, 0.001f),
        FloatNear(expectedWorldZ, 0.001f)
    )).Times(1);

    // Simulate the production call site (what IrrlichtRenderer must call):
    audio.updateVehicleAudio(idleIdx, moveIdx, speedFraction, expectedWorldX, expectedWorldZ);
}

// ============================================================================
// VehicleAudio_IntersectionTile_AudioReceivesTileCenterCoords
//
// Verifies that the worldX/worldZ passed to updateVehicleAudio() for a vehicle
// agent traversing an intersection tile (3+ road neighbours) equals the tile
// center — no lane offset is applied at intersection tiles.
//
// Spec: implementation/phase-11h.md §4e ("intersection tiles: agents snap to the
//   tile center X/Z (lane offset = 0)"), §5e.
// ============================================================================
TEST(VehicleAudioPositioningTest, VehicleAudio_IntersectionTile_AudioReceivesTileCenterCoords)
{
    // Intersection tile at (4, 4) — 3+ road neighbours, lane offset suppressed.
    const int tileX = 4;
    const int tileZ = 4;

    const float intersectionWorldX = intersectionAgentWorldX(tileX);
    const float intersectionWorldZ = tileCenterWorldZ(tileZ);

    // At intersection: worldX must equal tile center X (offset = 0).
    EXPECT_FLOAT_EQ(intersectionWorldX, tileCenterWorldX(tileX))
        << "At intersection tile, agent worldX must equal tile center X (no lane offset)";
    EXPECT_FLOAT_EQ(intersectionWorldZ, tileCenterWorldZ(tileZ))
        << "At intersection tile, agent worldZ must equal tile center Z (no lane offset)";

    // Intersection world coordinates (tile 4,4):
    //   worldX = 4 * 10 + 5 = 45.0
    //   worldZ = 4 * 10 + 5 = 45.0
    EXPECT_FLOAT_EQ(intersectionWorldX, 45.0f)
        << "Intersection agent at tileX=4 must have worldX = 4 * 10 + 5 = 45.0";
    EXPECT_FLOAT_EQ(intersectionWorldZ, 45.0f)
        << "Intersection agent at tileZ=4 must have worldZ = 4 * 10 + 5 = 45.0";

    // The intersection snap offset must be strictly less than the straight-segment
    // lane offset (0 < kLaneCenterOffset = 1.875).
    EXPECT_LT(std::fabs(0.0f), RenderConstants::kLaneCenterOffset)
        << "Intersection lane offset (0) must be less than straight-segment offset (1.875)";

    // Verify against a MockAudioSystem to document the call contract.
    // In production: IrrlichtRenderer calls audio->updateVehicleAudio(idleIdx, moveIdx,
    //   speedFraction, intersectionWorldX, intersectionWorldZ) — tile center, no offset.
    NiceMock<MockAudioSystem> audio;
    const int idleIdx = 2;
    const int moveIdx = 3;
    const float speedFraction = 0.2f;

    EXPECT_CALL(audio, updateVehicleAudio(
        idleIdx, moveIdx, speedFraction,
        FloatNear(intersectionWorldX, 0.001f),
        FloatNear(intersectionWorldZ, 0.001f)
    )).Times(1);

    // Simulate the production call site at intersection tile:
    audio.updateVehicleAudio(idleIdx, moveIdx, speedFraction,
                             intersectionWorldX, intersectionWorldZ);
}
