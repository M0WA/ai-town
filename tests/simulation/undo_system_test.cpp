// undo_system_test.cpp — Phase 6 simulation-side undo mechanics tests.
// UI-facing undo tests (countdown display, button grayout) belong in
// tests/ui/undo_button_test.cpp (Phase 8 deliverable).
//
// Tests specified in implementation/phase-6.md (Phase 6 stubs):
//   UndoState_SingleLevel_ExpiresAfterSecondBudgetTick_SimulationOnly
//   UndoState_FundsRefunded_SimulationSide
//   UndoSystem_ExpiryTime_ComputedCorrectly_AtHighSpeed
//   UndoSystem_PausedSimulation_UndoWindowDoesNotExpireDuringPause
//
// TODO Phase 6: Replace SUCCEED() stubs with full test implementations.
// All test names, fixture requirements, and formula details are in implementation/phase-6.md.

#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "mock_audio_system.h"
#include "mock_renderer.h"
#include "manual_rng.h"
#include "manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::StrictMock;
using ::testing::NiceMock;

// ---------------------------------------------------------------------------
// Phase 6 stub — tests will be fully implemented in Phase 6 code delivery.
// ---------------------------------------------------------------------------

TEST(UndoSystemStub, Phase6_UndoSystemTests_Pending) {
    SUCCEED();  // TODO Phase 6: replace with full undo system test implementations
}
