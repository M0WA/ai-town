// tests/simulation/city_simulation_render_test.cpp — Phase 10 simulation unit tests.
//
// Verifies that CitySimulation placement and demolish operations call the
// corresponding IRenderer mesh placement/removal methods on success.
//
// Fixture: CitySimulationRenderTest
//   - NiceMock<MockRenderer>:      focuses assertions on specific mesh-placement
//     calls; suppresses incidental renderer calls (getListenerPosition, etc.)
//     without requiring exhaustive EXPECT_CALLs for every audio side-effect.
//   - NiceMock<MockAudioSystem>:   every placement operation fires positional or
//     non-positional audio calls alongside mesh-placement calls; NiceMock suppresses
//     incidental audio calls so tests focus on the renderer assertion under test.
//     Approved mock policy deviation per testability-architecture.md §CitySimulationRenderTest.
//   - ManualRNG (non-strict, wrap 0.9f): service degradation calls safe.
//   - ManualClock (starts at 0.0 s): deterministic timing.
//   - ManualTerrainQuery (flat 0° slope): no earthworks SFX triggered
//     (earthworksCostOverride = 0 on all placement calls in this test file).
//   - sim_ declared LAST — destroyed first (reverse declaration order).
//   - TearDown(): resets sim_ before mock destructors run (destructor-path contract).
//
// Why NiceMock for both MockRenderer and MockAudioSystem (not StrictMock):
//   These tests verify renderer mesh-placement calls. Using StrictMock for
//   MockRenderer would require exhaustive EXPECT_CALLs for every incidental
//   renderer call made during the same operation (getListenerPosition in
//   doTrafficSignalTick, etc.). NiceMock suppresses those without sacrificing
//   assertion integrity on the specific placement methods under test.
//   Similarly, StrictMock<MockAudioSystem> would require declaring EXPECT_CALLs
//   for every audio side-effect (playPositionalSound, playSound, setMusicIntensity,
//   setTimeOfDay) that fire on the same code path as the mesh-placement assertion.
//   NiceMock<MockAudioSystem> suppresses those incidental audio calls.
//   (ref: architecture/testing/testability-architecture.md §CitySimulationRenderTest)
//
// Test coordinates: (5, 7) for single-tile operations; (5, 8) for
// second-tile operations in multi-step tests (density upgrade swap).
//
// Spec refs:
//   implementation/phase-10.md §City Rendering — Building Mesh Spawning and Road Mesh Rendering
//   architecture/testing/testability-architecture.md §Phase 10 simulation_tests canonical test names
//
// CMake: added to simulation_tests target via:
//   target_sources(simulation_tests PRIVATE tests/simulation/city_simulation_render_test.cpp)
// Do NOT call add_executable(simulation_tests ...) or aitown_add_tests(simulation_tests ...)
// again — that would produce a duplicate target error.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/audio_types.h"
#include "src/interfaces/sound_ids.h"
#include "src/simulation/simulation_constants.h"
#include "mock_audio_system.h"
#include "mock_renderer.h"
#include "manual_rng.h"
#include "manual_clock.h"
#include "manual_terrain_query.h"

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Exactly;
using ::testing::AtLeast;
using ::testing::InSequence;

// ---------------------------------------------------------------------------
// CitySimulationRenderTest — fixture for Phase 10 mesh-placement unit tests.
//
// NiceMock<MockRenderer>:    renderer interactions are incidental to the
//   specific mesh-placement assertion in each test; NiceMock suppresses
//   unexpected calls (getListenerPosition, etc.) so tests focus only on the
//   method under verification.
//
// NiceMock<MockAudioSystem>: every placement operation (placeZone, placeRoad,
//   placeServiceBuilding, demolishTile, doDensityUnlockTick) fires positional
//   or non-positional audio calls alongside the mesh-placement calls being tested.
//   NiceMock suppresses those incidental audio calls without requiring exhaustive
//   EXPECT_CALLs for every audio side-effect in a test whose assertion target is
//   the renderer method under test.
//   Approved mock policy deviation per:
//     architecture/testing/testability-architecture.md §CitySimulationRenderTest
//     implementation/phase-10.md §Rendering method unit tests in simulation_tests
//
// ManualRNG (non-strict, float=0.9f): 0.9f > service_degradation_probability
//   (0.5f), so service buildings do NOT degrade during multi-tick runs.
//
// Declaration order: sim_ LAST — destroyed FIRST (reverse order) to prevent
//   use-after-free when CitySimulation destructor calls back into mocks.
// ---------------------------------------------------------------------------
class CitySimulationRenderTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>      renderer_;
    NiceMock<MockAudioSystem>   audio_;
    ManualRNG          rng_;      // non-strict, float={0.9f}
    ManualClock        clock_;    // starts at 0.0 s
    ManualTerrainQuery terrain_;  // flat (0° slope) — no earthworks SFX

    // sim_ declared LAST — destroyed first (reverse declaration order).
    std::unique_ptr<ICitySimulation> sim_;

    void SetUp() override {
        // Easy difficulty: $1,000,000 starting treasury ensures placement
        // calls succeed without insufficient-funds early-exit.
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Easy);
        sim_->setSpeed(SpeedMultiplier::x1);

        // NiceMock<MockAudioSystem> suppresses unexpected audio calls automatically.
        // The EXPECT_CALL stubs below are retained as documentation of which audio
        // calls fire on placement/tick paths, and to allow individual tests to
        // install EXPECT_CALL overrides (e.g. Times(Exactly(1))) that tighten the
        // constraint for the specific call under test in that test case.
        // setMusicIntensity fires every budget tick (Phase 10 wiring).
        EXPECT_CALL(audio_, setMusicIntensity(_)).Times(AnyNumber());
        // setTimeOfDay fires when the in-game clock crosses DAY/DUSK/NIGHT/DAWN.
        EXPECT_CALL(audio_, setTimeOfDay(_)).Times(AnyNumber());
        // playPositionalSound fires for SFX_BUILD_PLACE, SFX_ROAD_BUILD,
        // SFX_BUILD_DEMOLISH (Phase 10 audio wiring in CitySimulation).
        // Individual tests install a specific EXPECT_CALL after SetUp() to
        // tighten the count for the particular SFX under test.
        EXPECT_CALL(audio_, playPositionalSound(_, _, _, _)).Times(AnyNumber());
        // playSound fires for SFX_ZONE_UPGRADE during density upgrade waves.
        EXPECT_CALL(audio_, playSound(_, _, _)).Times(AnyNumber());
    }

    void TearDown() override {
        // Destroy sim_ before mock destructors run — prevents use-after-free
        // when CitySimulation destructor calls back into audio_/renderer_.
        sim_.reset();
    }

    // Downcast helper — required for test-only API (doDensityUnlockTick).
    CitySimulation* cs() {
        auto* p = dynamic_cast<CitySimulation*>(sim_.get());
        EXPECT_NE(p, nullptr) << "Downcast to CitySimulation* failed";
        return p;
    }

    // Advance clock and fire N budget ticks at x1 speed.
    void runTicks(int n) {
        const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
        for (int i = 0; i < n; ++i) {
            clock_.advance(dt);
            cs()->tick(dt);
        }
    }
};

// ---------------------------------------------------------------------------
// CitySimulation_PlaceZone_SpawnsBuilding
//
// After a successful placeZone(5, 7, Residential, Low, 0), IRenderer must
// receive placeBuildingMesh(5, 7, "res_low_01") exactly once.
//
// Zone type → asset name component mapping (locked for Phase 10, _01 suffix):
//   Residential / Low → "res_low_01"
//
// Audio side-effect (verified): playPositionalSound(SFX_BUILD_PLACE, _, _, _)
// fires once (the build-placement feedback).
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_PlaceZone_SpawnsBuilding)
{
    // Override the AnyNumber allowances with exact counts for this test.
    EXPECT_CALL(audio_, playPositionalSound(SFX_BUILD_PLACE, _, _, _))
        .Times(Exactly(1));
    EXPECT_CALL(renderer_, placeBuildingMesh(5, 7, std::string("res_low_01")))
        .Times(Exactly(1));

    sim_->placeZone(5, 7, ZoneType::Residential, DensityTier::Low, 0);
}

// ---------------------------------------------------------------------------
// CitySimulation_PlaceRoad_SpawnsRoadMesh
//
// After a successful placeRoad(5, 7, 0), IRenderer must receive
// placeRoadMesh(5, 7) exactly once.
//
// Audio side-effect: playPositionalSound(SFX_ROAD_BUILD, _, _, _) × 1.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_PlaceRoad_SpawnsRoadMesh)
{
    EXPECT_CALL(audio_, playPositionalSound(SFX_ROAD_BUILD, _, _, _))
        .Times(Exactly(1));
    EXPECT_CALL(renderer_, placeRoadMesh(5, 7))
        .Times(Exactly(1));

    sim_->placeRoad(5, 7, 0);
}

// ---------------------------------------------------------------------------
// CitySimulation_PlaceServiceBuilding_SpawnsMesh
//
// After a successful placeServiceBuilding(5, 7, FireStation, 0), IRenderer
// must receive placeServiceBuildingMesh(5, 7, ServiceBuildingType::FireStation)
// exactly once.
//
// Audio side-effect: playPositionalSound(SFX_BUILD_PLACE, _, _, _) × 1.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_PlaceServiceBuilding_SpawnsMesh)
{
    EXPECT_CALL(audio_, playPositionalSound(SFX_BUILD_PLACE, _, _, _))
        .Times(Exactly(1));
    EXPECT_CALL(renderer_, placeServiceBuildingMesh(
                    5, 7, ServiceBuildingType::FireStation))
        .Times(Exactly(1));

    sim_->placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0);
}

// ---------------------------------------------------------------------------
// CitySimulation_DemolishZoneTile_RemovesBuilding
//
// After placing a zone tile at (5, 7) then demolishing it, IRenderer must
// receive removeBuildingMesh(5, 7) exactly once on demolish.
//
// Setup: placeZone first (placeBuildingMesh will fire — not the assertion);
//        then demolishTile must fire removeBuildingMesh.
//
// Audio side-effect on demolish: playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _) × 1.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_DemolishZoneTile_RemovesBuilding)
{
    // Step 1: place the zone (incidental — not the assertion focus).
    sim_->placeZone(5, 7, ZoneType::Residential, DensityTier::Low, 0);

    // Step 2: verify demolish fires removeBuildingMesh.
    EXPECT_CALL(audio_, playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _))
        .Times(Exactly(1));
    EXPECT_CALL(renderer_, removeBuildingMesh(5, 7))
        .Times(Exactly(1));

    sim_->demolishTile(5, 7);
}

// ---------------------------------------------------------------------------
// CitySimulation_DemolishRoadTile_RemovesRoadMesh
//
// After placing a road tile at (5, 7) then demolishing it, IRenderer must
// receive removeRoadMesh(5, 7) exactly once on demolish.
//
// Audio side-effect on demolish: playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _) × 1.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_DemolishRoadTile_RemovesRoadMesh)
{
    // Step 1: place the road (incidental — NiceMock<MockRenderer> suppresses
    //         the placeRoadMesh call from the SetUp allowance).
    sim_->placeRoad(5, 7, 0);

    // Step 2: verify demolish fires removeRoadMesh.
    EXPECT_CALL(audio_, playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _))
        .Times(Exactly(1));
    EXPECT_CALL(renderer_, removeRoadMesh(5, 7))
        .Times(Exactly(1));

    sim_->demolishTile(5, 7);
}

// ---------------------------------------------------------------------------
// CitySimulation_DemolishServiceBuilding_RemovesMesh
//
// After placing a FireStation at (5, 7) then demolishing it, IRenderer must
// receive removeServiceBuildingMesh(5, 7) exactly once on demolish.
//
// Audio side-effect on demolish: playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _) × 1.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_DemolishServiceBuilding_RemovesMesh)
{
    // Step 1: place the service building (incidental).
    sim_->placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0);

    // Step 2: verify demolish fires removeServiceBuildingMesh.
    EXPECT_CALL(audio_, playPositionalSound(SFX_BUILD_DEMOLISH, _, _, _))
        .Times(Exactly(1));
    EXPECT_CALL(renderer_, removeServiceBuildingMesh(5, 7))
        .Times(Exactly(1));

    sim_->demolishTile(5, 7);
}

// ---------------------------------------------------------------------------
// CitySimulation_DensityUpgrade_SwapsBuildingMesh
//
// When a density upgrade wave fires, CitySimulation must:
//   1. Call removeBuildingMesh(tileX, tileZ)           — remove old LOD mesh.
//   2. Call placeBuildingMesh(tileX, tileZ, newAsset)  — place upgraded mesh.
//   Both calls must occur within the same doDensityUnlockTick() pass.
//
// Setup:
//   - Easy difficulty ($1M starting treasury) ensures the treasury threshold
//     for Med-R unlock ($50K × 0.70 scale = $35K) is met immediately at
//     construction. No revenue needed.
//   - Place one Residential/Low tile at (5, 7) + balanced C/I zones + a road
//     to create strong R demand (> density_upgrade_wave_demand_threshold = 0.75).
//   - Advance ManualClock past grace_period_real_seconds (120 s) so budget
//     ticks can fire the density unlock counter increment.
//   - Run 4 budget ticks: ticks 1–3 increment consecutive_months_above_threshold[0]
//     to 3 and fire the unlock; tick 4 runs the upgrade wave (same-tick guard
//     in doDensityUnlockTick() prevents the wave from firing on the unlock tick).
//
// Asset name after upgrade (Residential Low → Medium):
//   "res_med_01" (ZoneType::Residential, DensityTier::Medium, _01 suffix).
//
// The removeBuildingMesh + placeBuildingMesh calls must occur in that order
// (remove first, then place) — enforced via InSequence.
//
// Audio side-effect: playSound(SFX_ZONE_UPGRADE, _, _) at least once
// (cap = 3 per doDensityUnlockTick() invocation; only 1 tile upgraded here).
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest, CitySimulation_DensityUpgrade_SwapsBuildingMesh)
{
    // Place a balanced map so R demand exceeds 0.75 (the upgrade wave threshold).
    // Tile (5, 7) is the Residential/Low tile that will be upgraded to Medium.
    sim_->placeZone(5, 7, ZoneType::Residential, DensityTier::Low, 0);
    // Balanced C/I zones to create strong R demand.
    sim_->placeZone(5, 8, ZoneType::Commercial,  DensityTier::Low, 0);
    sim_->placeZone(5, 9, ZoneType::Commercial,  DensityTier::Low, 0);
    sim_->placeZone(6, 7, ZoneType::Industrial,  DensityTier::Low, 0);
    sim_->placeZone(6, 8, ZoneType::Industrial,  DensityTier::Low, 0);
    // Road to enable A* pathfinding (non-null-path demand).
    sim_->placeRoad(4, 7, 0);

    // Advance past the grace period real-time gate so budget ticks are live.
    clock_.advance(
        static_cast<float>(SimulationConstants::grace_period_real_seconds) + 1.0f);

    // Install EXPECT_CALLs BEFORE running ticks so they capture calls that
    // fire inside doDensityUnlockTick().
    //
    // Audio: SFX_ZONE_UPGRADE fires at least once when a tile is upgraded.
    // playSound (non-positional, AL_SOURCE_RELATIVE = AL_TRUE) — per phase-10.md.
    EXPECT_CALL(audio_, playSound(SFX_ZONE_UPGRADE, _, _))
        .Times(AtLeast(1));

    // Renderer: remove then place, in that order — InSequence enforces ordering.
    {
        InSequence seq;
        EXPECT_CALL(renderer_, removeBuildingMesh(5, 7))
            .Times(Exactly(1));
        EXPECT_CALL(renderer_, placeBuildingMesh(5, 7, std::string("res_med_01")))
            .Times(Exactly(1));
    }

    // Run 4 budget ticks:
    //   Ticks 1–3: increment consecutive_months_above_threshold[0] → 1, 2, 3.
    //             On tick 3 the unlock flag fires (unlock_flags[0] = true),
    //             but wasAlreadyUnlocked[0] = false so the upgrade wave is
    //             suppressed that tick (spec §Same-tick unlock guard).
    //   Tick 4:   wasAlreadyUnlocked[0] = true → upgrade wave fires, swapping
    //             the mesh from "res_low_01" to "res_med_01".
    // (Easy difficulty: treasury $1M >> Med-R threshold $50K × 0.70 = $35K.)
    runTicks(4);
}
