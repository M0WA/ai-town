#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "CitySimulation.h"
#include "mock_audio_system.h"
#include "mock_renderer.h"
#include "manual_rng.h"
#include "manual_clock.h"
#include "manual_terrain_query.h"

// SimulationTestBase — shared base fixture for all CitySimulation unit tests.
//
// Provides:
//   - StrictMock<MockRenderer>       renderer_
//   - StrictMock<MockAudioSystem>    audio_
//   - ManualRNG                      rng_      (strict mode; exhaustion throws)
//   - ManualClock                    clock_    (starts at 0.0; advance via clock_.advance())
//   - ManualTerrainQuery             terrain_  (flat by default; set slopes via terrain_.setSlope())
//   - std::unique_ptr<ICitySimulation> sim_
//
// Declaration order is significant: sim_ is declared LAST so it is destroyed FIRST
// (reverse declaration order), preventing use-after-free in CitySimulation destructor
// when it references renderer_ / audio_.
//
// SetUp(): creates sim_ with default Difficulty::Normal and speed already set to x1
//   to avoid accidental time-compression in tests that don't explicitly set speed.
//   Override makeSim() to change difficulty or initial state.
//
// TearDown(): resets sim_ explicitly before mock destructors run, ensuring
//   CitySimulation releases its references before StrictMock validates expectations.
//
// Usage:
//   class MyTest : public SimulationTestBase {
//   protected:
//       void SetUp() override {
//           SimulationTestBase::SetUp();
//           // additional setup…
//       }
//   };

class SimulationTestBase : public ::testing::Test {
protected:
    // Strict mocks — any unexpected call is a test failure.
    ::testing::StrictMock<MockRenderer>     renderer_;
    ::testing::StrictMock<MockAudioSystem>  audio_;

    // Deterministic test doubles
    ManualRNG          rng_;      // sequences set per test; strict=true by default
    ManualClock        clock_;    // starts at 0.0 s
    ManualTerrainQuery terrain_;  // flat (0° slope) by default

    // sim_ declared LAST — destroyed first (reverse declaration order)
    std::unique_ptr<ICitySimulation> sim_;

    // Override to customize difficulty or post-construction state.
    // Default creates Normal difficulty.
    virtual Difficulty difficulty() const { return Difficulty::Normal; }

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, difficulty());
        // Default to x1 speed so tests don't accidentally fire rapid ticks
        sim_->setSpeed(SpeedMultiplier::x1);
        // Phase 10: CitySimulation::tick() calls setMusicIntensity() each budget tick
        // to communicate adaptive music intensity tier to IAudioSystem.
        // Allow any number of calls from StrictMock — base fixture tests do not
        // assert music intensity; individual tests that do will override with their
        // own EXPECT_CALL after calling SimulationTestBase::SetUp().
        EXPECT_CALL(audio_, setMusicIntensity(::testing::_)).Times(::testing::AnyNumber());
        // Phase 10: CitySimulation::tick() also calls setTimeOfDay() whenever the
        // in-game clock crosses a DAY/DUSK/NIGHT/DAWN boundary.  This is incidental
        // to most test scenarios; allow any number of calls so StrictMock does not
        // fail on time-of-day transitions that occur during multi-tick test runs.
        EXPECT_CALL(audio_, setTimeOfDay(::testing::_)).Times(::testing::AnyNumber());
        // Phase 10: CitySimulation placement methods (placeZone, placeRoad,
        // placeServiceBuilding, demolishTile, doDensityUnlockTick) call the six new
        // IRenderer mesh placement/removal methods when they succeed.  These calls
        // are incidental to simulation-logic tests; suppress them so StrictMock does
        // not fail on unexpected renderer calls in tests that focus on treasury,
        // traffic, or other non-rendering behaviour.
        // Individual tests that verify specific renderer interactions must override
        // with their own EXPECT_CALL after calling SimulationTestBase::SetUp().
        // placeBuildingMesh(tileX, tileZ, assetBaseName) — 3 args
        EXPECT_CALL(renderer_, placeBuildingMesh(::testing::_, ::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
        // removeBuildingMesh(tileX, tileZ) — 2 args
        EXPECT_CALL(renderer_, removeBuildingMesh(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
        // placeRoadMesh(tileX, tileZ) — 2 args (road mesh is always the same asset)
        EXPECT_CALL(renderer_, placeRoadMesh(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
        // removeRoadMesh(tileX, tileZ) — 2 args
        EXPECT_CALL(renderer_, removeRoadMesh(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
        // placeServiceBuildingMesh(tileX, tileZ, ServiceBuildingType) — 3 args
        EXPECT_CALL(renderer_, placeServiceBuildingMesh(::testing::_, ::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
        // removeServiceBuildingMesh(tileX, tileZ) — 2 args
        EXPECT_CALL(renderer_, removeServiceBuildingMesh(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
    }

    void TearDown() override {
        // Destroy sim_ before mock destructors run — prevents use-after-free
        // when CitySimulation destructor logs or calls back into interfaces.
        sim_.reset();
    }

    // Helper: advance clock and call tick() with realDeltaSeconds to fire N budget ticks.
    // ticksToFire × SECONDS_PER_BUDGET_TICK seconds of real time are simulated at speed x1.
    void runTicks(int ticksToFire) {
        auto* cs = dynamic_cast<CitySimulation*>(sim_.get());
        ASSERT_NE(cs, nullptr);
        const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
        for (int i = 0; i < ticksToFire; ++i) {
            clock_.advance(dt);
            cs->tick(dt);
        }
    }
};
