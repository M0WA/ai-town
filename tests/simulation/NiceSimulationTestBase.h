#pragma once
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

// NiceSimulationTestBase — shared base fixture for CitySimulation behavioral tests
// that do not assert on specific call counts to renderer_ or audio_.
//
// Provides:
//   - NiceMock<MockRenderer>       renderer_
//   - NiceMock<MockAudioSystem>    audio_
//   - ManualRNG                    rng_      (non-strict; float=0.9f prevents service degradation)
//   - ManualClock                  clock_    (starts at 0.0 s; advance via clock_.advance())
//   - ManualTerrainQuery           terrain_  (flat by default; set slopes via terrain_.setSlope())
//   - std::unique_ptr<ICitySimulation> sim_
//
// Use NiceMock (not StrictMock) when:
//   - The test asserts on simulation state (treasury, population, coverage, etc.)
//   - Placement methods fire audio/renderer side effects that are irrelevant to the assertion
//   - The test is behavioral rather than call-count oriented
//
// Use SimulationTestBase (StrictMock) when:
//   - The test must assert that a specific mock method is called exactly N times
//   - Unexpected mock calls must fail the test
//
// Declaration order is significant: sim_ is declared LAST so it is destroyed FIRST
// (reverse declaration order), preventing use-after-free in CitySimulation destructor
// when it references renderer_ / audio_.
//
// SetUp(): creates sim_ with default Difficulty::Normal at x1 speed.
//   Override difficulty() to change difficulty.
//
// TearDown(): resets sim_ explicitly before mock destructors run, ensuring
//   CitySimulation releases its references before NiceMock validates expectations.

class NiceSimulationTestBase : public ::testing::Test {
protected:
    // NiceMock — unexpected calls are silently ignored. Appropriate for tests
    // that focus on simulation state, not renderer/audio interaction counts.
    ::testing::NiceMock<MockRenderer>     renderer_;
    ::testing::NiceMock<MockAudioSystem>  audio_;

    // Non-strict RNG: float sequence 0.9f > service_degradation_probability (0.5f)
    // so service buildings never degrade during tests that do not force degradation.
    // Wrap-around avoids sequence exhaustion across multi-tick test runs.
    ManualRNG          rng_;      // default: int={0}, float={0.9f}, non-strict
    ManualClock        clock_;    // starts at 0.0 s
    ManualTerrainQuery terrain_;  // flat (0° slope) by default

    // sim_ declared LAST — destroyed first (reverse declaration order)
    std::unique_ptr<ICitySimulation> sim_;

    // Override to change difficulty.
    virtual Difficulty difficulty() const { return Difficulty::Normal; }

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, difficulty());
        sim_->setSpeed(SpeedMultiplier::x1);
    }

    void TearDown() override {
        // Destroy sim_ before mock destructors run — prevents use-after-free
        // when CitySimulation destructor calls back into interfaces.
        sim_.reset();
    }

    // Downcast helper: returns concrete CitySimulation* for test-only API access.
    // EXPECT_NE fires immediately if the downcast fails; the explicit null guard
    // prevents null dereference in subsequent calls within the same test body.
    CitySimulation* cs() {
        auto* p = dynamic_cast<CitySimulation*>(sim_.get());
        EXPECT_NE(p, nullptr) << "Downcast to CitySimulation* failed";
        if (!p) return nullptr;
        return p;
    }

    // Helper: advance clock and call tick() N times to fire N budget ticks.
    // Each call advances clock_ by SECONDS_PER_BUDGET_TICK real seconds.
    void runTicks(int n) {
        auto* c = dynamic_cast<CitySimulation*>(sim_.get());
        ASSERT_NE(c, nullptr);
        const float dt = SimulationConstants::SECONDS_PER_BUDGET_TICK;
        for (int i = 0; i < n; ++i) {
            clock_.advance(dt);
            c->tick(dt);
        }
    }
};
