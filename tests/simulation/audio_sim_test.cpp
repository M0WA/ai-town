// audio_sim_test.cpp — CitySimulation audio callback tests (Phase 9b Deliverable J).
//
// Tests that CitySimulation placement actions fire the correct IAudioSystem callbacks.
// Located in tests/simulation/ alongside other simulation tests.
//
// CMake: added to simulation_tests target via:
//   target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp)
// Do NOT call add_executable(simulation_tests ...) or aitown_add_tests(simulation_tests ...)
// again — that would produce a duplicate target error.
//
// Test policy (ref: architecture/testing/testability-architecture.md):
//   - NiceMock<MockRenderer>:   placeRoad() may call renderer methods internally for world-space
//                               position computation; this test is focused solely on audio callback
//                               behaviour. Same pattern as economy_test.cpp using NiceMock for
//                               non-audio tests. NiceMock suppresses unrelated renderer calls.
//   - StrictMock<MockAudioSystem>: ensures only the expected audio call fires and nothing else.
//   - ManualTerrainQuery:       flat (0° slope) — earthworks SFX branch (SFX_EARTHWORKS) is
//                               not triggered when earthworksCostOverride is 0.
//   - ManualClock:              starts at 0.0 s; Easy difficulty treasury is positive at
//                               construction without advancing the clock.
//   - Difficulty::Easy:         initialises a positive starting treasury so placeRoad() does
//                               not fail due to insufficient funds before firing the audio callback.
//
// (ref: implementation/phase-9b.md Deliverable J)

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "CitySimulation.h"
#include "MockAudioSystem.h"
#include "MockRenderer.h"
#include "ManualRNG.h"
#include "ManualClock.h"
#include "ManualTerrainQuery.h"
#include "src/interfaces/sound_ids.h"
#include "src/interfaces/simulation_types.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::StrictMock;

// ---------------------------------------------------------------------------
// AudioSimTest — fixture for CitySimulation audio callback tests.
//
// Declaration order (destroyed in reverse — sim_ last to first, then mocks):
//   renderer_, audioSystem_, rng_, clock_, terrain_ declared BEFORE sim_
//   so that sim_ (declared LAST) is destroyed FIRST.
// This prevents use-after-free when CitySimulation destructor calls back into
// renderer_ / audioSystem_ (the standard SimulationTestBase pattern).
// ---------------------------------------------------------------------------
class AudioSimTest : public ::testing::Test {
protected:
    // NiceMock<MockRenderer>: renderer interactions inside placeRoad() are
    // incidental to this audio-focused test; NiceMock suppresses them.
    NiceMock<MockRenderer>      renderer_;

    // StrictMock<MockAudioSystem>: verifies that ONLY the expected audio call fires.
    StrictMock<MockAudioSystem> audioSystem_;

    // Deterministic test doubles
    ManualRNG          rng_;
    ManualClock        clock_;
    ManualTerrainQuery terrain_;   // flat (0° slope) by default

    // sim_ declared LAST — destroyed first (reverse declaration order)
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        // Difficulty::Easy provides a positive starting treasury so placeRoad()
        // does not fail due to insufficient funds before firing the audio callback.
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audioSystem_, &rng_, &clock_, &terrain_, Difficulty::Easy);
        // x1 speed — avoids accidental time-compression triggering budget ticks
        sim_->setSpeed(SpeedMultiplier::x1);
    }

    void TearDown() override {
        // Destroy sim_ before mock destructors run — prevents use-after-free
        // when CitySimulation destructor logs or calls back into interfaces.
        sim_.reset();
    }
};

// ---------------------------------------------------------------------------
// CitySimulation_PlaceRoad_FiresSFXRoadBuild
//
// Verifies that CitySimulation::placeRoad() fires exactly one
// playPositionalSound(SFX_ROAD_BUILD, ...) call — the dedicated road-construction
// feedback sound (SoundId = 3, ref: v1-audio-asset-manifest.md SoundId Assignment Table).
//
// SFX_BUILD_PLACE (SoundId = 1) is the generic zone/building placement sound and
// MUST NOT fire on placeRoad(). This test closes the Phase 10 precondition:
// sfx_road_build must fire from real road placement dispatch, not from the old
// SFX_BUILD_PLACE placeholder. (ref: implementation/phase-9b.md Deliverable J)
//
// Three wildcard matchers cover:
//   _  = vec3 world-space position (tile centroid)
//   _  = SoundPriority priority
//   _  = float gain
// matching IAudioSystem::playPositionalSound(SoundId, vec3, SoundPriority, float).
//
// EXPECT_CALL is set up BEFORE placeRoad() is invoked (standard GMock order).
//
// earthworksCostOverride = 0 (flat tile) — no SFX_EARTHWORKS call triggered.
// ---------------------------------------------------------------------------
TEST_F(AudioSimTest, CitySimulation_PlaceRoad_FiresSFXRoadBuild) {
    // Verify the treasury is positive so placeRoad() does not fail silently.
    ASSERT_GT(sim_->getTreasuryBalance(), 0.0f)
        << "Easy difficulty must initialise a positive treasury so placeRoad() "
           "can proceed without an insufficient-funds early-return";

    // Set up expectation BEFORE calling placeRoad().
    // SFX_ROAD_BUILD (SoundId = 3) must fire exactly once; no other audio call expected.
    EXPECT_CALL(audioSystem_, playPositionalSound(SFX_ROAD_BUILD, _, _, _)).Times(1);

    // Act: place a road at tile (5, 7) with zero earthworks override (flat terrain).
    // earthworksCostOverride = 0 suppresses the SFX_EARTHWORKS branch inside placeRoad().
    sim_->placeRoad(5, 7, 0);
}

// ---------------------------------------------------------------------------
// CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace
//
// Verifies that CitySimulation::placeServiceBuilding() fires exactly one
// playPositionalSound(SFX_BUILD_PLACE, ...) call on successful placement.
//
// SFX_BUILD_PLACE (SoundId = 1) is the correct sound for service building placement
// (Audio Decision 1, sound-artist-opensoftal 2026-03-02). A dedicated service-building
// SFX is post-V1.
//
// earthworksCostOverride = 0 (flat tile, ManualTerrainQuery default) — SFX_EARTHWORKS
// branch must NOT fire. StrictMock<MockAudioSystem> enforces this.
//
// (ref: implementation/phase-9b.md Deliverable J, Audio Decision 1)
// ---------------------------------------------------------------------------
TEST_F(AudioSimTest, CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace) {
    // Verify the treasury is positive so placeServiceBuilding() does not fail.
    ASSERT_GT(sim_->getTreasuryBalance(), 0.0f)
        << "Easy difficulty must initialise a positive treasury so placeServiceBuilding() "
           "can proceed without an insufficient-funds early-return";

    // Phase 11h: placeServiceBuilding requires an adjacent road to its 2×2 footprint.
    // Place road at (4,7) — adjacent to footprint tile (5,7). Fires SFX_ROAD_BUILD.
    EXPECT_CALL(audioSystem_, playPositionalSound(SFX_ROAD_BUILD, _, _, _)).Times(1);
    sim_->placeRoad(4, 7, 0);

    // Set up expectation BEFORE calling placeServiceBuilding().
    // SFX_BUILD_PLACE (SoundId = 1) must fire exactly once.
    // No SFX_EARTHWORKS expected: earthworksCostOverride = 0.
    EXPECT_CALL(audioSystem_, playPositionalSound(SFX_BUILD_PLACE, _, _, _)).Times(1);

    // Act: place a Power Plant at tile (5, 7) with zero earthworks override (flat terrain).
    sim_->placeServiceBuilding(5, 7, ServiceBuildingType::PowerPlant, 0);
}

// ---------------------------------------------------------------------------
// CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace
//
// Verifies that CitySimulation::placeServiceBuilding() fires SFX_EARTHWORKS BEFORE
// SFX_BUILD_PLACE when earthworksCostOverride > 0 (steep-slope placement).
//
// Using InSequence to enforce call order: SFX_EARTHWORKS must fire first,
// then SFX_BUILD_PLACE (Audio Decision 1 call sequence in phase-9b.md Deliverable J.0).
//
// ManualTerrainQuery::setSlope(5, 7, 30.0f) triggers the earthworks branch in
// CitySimulation::placeServiceBuilding() when earthworksCostOverride = 250 > 0.
// Note: earthworksCostOverride is the override passed to placeServiceBuilding(),
// NOT re-computed from terrain — the simulation uses the passed override directly.
//
// (ref: implementation/phase-9b.md Deliverable J, Audio Decision 1)
// ---------------------------------------------------------------------------
TEST_F(AudioSimTest, CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace) {
    // Set per-tile slope — informs the earthworks calculation if the simulation
    // queries terrain slope internally. earthworksCostOverride = 250 drives the
    // SFX_EARTHWORKS branch regardless of slope query.
    terrain_.setSlope(5, 7, 30.0f);

    // Verify the treasury is positive.
    ASSERT_GT(sim_->getTreasuryBalance(), 0.0f)
        << "Easy difficulty must initialise a positive treasury";

    // Phase 11h: placeServiceBuilding requires an adjacent road to its 2×2 footprint.
    // Place road at (4,7) — adjacent to footprint tile (5,7). Fires SFX_ROAD_BUILD.
    EXPECT_CALL(audioSystem_, playPositionalSound(SFX_ROAD_BUILD, _, _, _)).Times(1);
    sim_->placeRoad(4, 7, 0);

    // InSequence: SFX_EARTHWORKS must fire BEFORE SFX_BUILD_PLACE.
    // (ref: implementation/phase-9b.md Deliverable J.0 Audio Decision 1 call sequence)
    {
        ::testing::InSequence seq;
        EXPECT_CALL(audioSystem_, playPositionalSound(SFX_EARTHWORKS, _, _, _)).Times(1);
        EXPECT_CALL(audioSystem_, playPositionalSound(SFX_BUILD_PLACE, _, _, _)).Times(1);
    }

    // Act: place a Power Plant at (5, 7) with earthworksCostOverride = 250.
    // earthworksCostOverride > 0 triggers the SFX_EARTHWORKS branch.
    sim_->placeServiceBuilding(5, 7, ServiceBuildingType::PowerPlant, 250);
}
