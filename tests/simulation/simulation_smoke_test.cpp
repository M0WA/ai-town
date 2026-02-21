// simulation_smoke_test.cpp — Phase 0 compile-check stub for simulation_tests target.
// Verifies that all shared simulation mock and interface headers are well-formed.
#include "src/interfaces/IClock.h"
#include "src/interfaces/ISimulationRNG.h"
#include "src/interfaces/ICitySimulation.h"
#include "mock_audio_system.h"
#include "mock_renderer.h"
#include "manual_rng.h"
#include "manual_clock.h"
#include <gtest/gtest.h>

TEST(SimulationSmoke, AllHeadersCompile) {
    SUCCEED();
}
