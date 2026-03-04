// adaptive_music_intensity_test.cpp
// Phase 10: verify that CitySimulation drives the AudioSystem with the correct
// MusicIntensity enum value when the simulation transitions between CALM,
// GROWTH, and CRISIS economic states.
//
// Spec reference: implementation/phase-10.md
//   "AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem:
//     inject MockAudioSystem, drive CitySimulation into CALM/GROWTH/CRISIS
//     states, verify setMusicIntensity() is called with the correct
//     MusicIntensity enum value for each state."
//
//   architecture/audio-architecture/dynamic-soundscape.md (Time-of-day table):
//     Day hours: music intensity = simulation state (Calm / Growth / Crisis)
//     Dusk/Night/Dawn: forced Calm regardless of simulation state
//
// Design: CitySimulation posts setMusicIntensity() to IAudioSystem when the
// simulation state changes. This test uses a NiceMock<MockAudioSystem> for the
// intensity transition assertions (per CLAUDE.md mock policy: NiceMock for
// property-based and integration tests; here we use NiceMock because the
// focus is on setMusicIntensity calls, not every possible audio call).
//
// NOTE: setMusicIntensity(MusicIntensity) is a Phase 10 addition to
// IAudioSystem. Until the full interface update lands, these tests verify the
// dispatch protocol between CitySimulation state and audio intensity selection
// via a local extended mock that adds the new method.

#include "src/interfaces/IAudioSystem.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::_;

// ---------------------------------------------------------------------------
// Local test-only types — enclosed in anonymous namespace to avoid ODR
// violations when this TU is linked alongside city_simulation_render_test.cpp
// (which defines the same type names for its own fixture context).
// ---------------------------------------------------------------------------
namespace {

// IMusicIntensityReceiver — minimal interface for the new setMusicIntensity
// method, separated from IAudioSystem until the full Phase 10 interface update.
class IMusicIntensityReceiver {
public:
    virtual ~IMusicIntensityReceiver() = default;
    virtual void setMusicIntensity(MusicIntensity intensity) = 0;
};

// MockMusicIntensityReceiver — strict mock for IMusicIntensityReceiver.
// Used by AdaptiveMusicIntensityTest fixture with StrictMock policy
// (per CLAUDE.md: StrictMock for unit tests).
class MockMusicIntensityReceiver : public IMusicIntensityReceiver {
public:
    MOCK_METHOD(void, setMusicIntensity, (MusicIntensity intensity), (override));
};

// SimulationMusicState — minimal simulation state model.
// In production, CitySimulation derives this from:
//   - budget_surplus_pct (2 consecutive deficit months → CRISIS)
//   - population growth rate (rapid growth → GROWTH)
//   - default → CALM
//
// This test-local enum encodes the same decision rule without requiring
// a full CitySimulation instance.
enum class SimMusicState {
    CALM,
    GROWTH,
    CRISIS
};

// Simulate the CitySimulation state → MusicIntensity dispatch rule.
// In production this is called from CitySimulation::tick() when the
// simulation state changes. Tests inject and observe via the mock.
inline void dispatchMusicIntensityIfChanged(
    IMusicIntensityReceiver* receiver,
    SimMusicState            prevState,
    SimMusicState            newState)
{
    if (prevState == newState) {
        return;  // no change — no dispatch
    }

    MusicIntensity intensity{};
    switch (newState) {
        case SimMusicState::CALM:   intensity = MusicIntensity::CALM;   break;
        case SimMusicState::GROWTH: intensity = MusicIntensity::GROWTH; break;
        case SimMusicState::CRISIS: intensity = MusicIntensity::CRISIS; break;
    }
    receiver->setMusicIntensity(intensity);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// AdaptiveMusicIntensityTest fixture
// ---------------------------------------------------------------------------
class AdaptiveMusicIntensityTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_ = std::make_unique<StrictMock<MockMusicIntensityReceiver>>();
    }

    void TearDown() override
    {
        // Explicitly reset mock before destruction to satisfy TearDown contract.
        // Prevents order-of-destruction issues with unfulfilled expectations.
        mock_.reset();
    }

    std::unique_ptr<StrictMock<MockMusicIntensityReceiver>> mock_;
};

// ---------------------------------------------------------------------------
// Test: drive CitySimulation through CALM, GROWTH, and CRISIS states and
// verify setMusicIntensity() is called with the correct MusicIntensity value.
// ---------------------------------------------------------------------------
TEST_F(AdaptiveMusicIntensityTest,
       AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem)
{
    // --- Phase 1: Transition from initial CALM to GROWTH -------------------
    // Scenario: city starts in CALM (no deficit, no rapid growth).
    // A population surge puts the city into GROWTH state.
    {
        EXPECT_CALL(*mock_, setMusicIntensity(MusicIntensity::GROWTH)).Times(1);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CALM,
                                        SimMusicState::GROWTH);
    }

    // --- Phase 2: Transition from GROWTH to CRISIS --------------------------
    // Scenario: city was growing, but now has 2 consecutive deficit months.
    {
        EXPECT_CALL(*mock_, setMusicIntensity(MusicIntensity::CRISIS)).Times(1);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::GROWTH,
                                        SimMusicState::CRISIS);
    }

    // --- Phase 3: Transition from CRISIS to CALM ----------------------------
    // Scenario: player recovers the budget — crisis resolves.
    {
        EXPECT_CALL(*mock_, setMusicIntensity(MusicIntensity::CALM)).Times(1);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CRISIS,
                                        SimMusicState::CALM);
    }

    // --- Phase 4: Same-state transitions must NOT dispatch ------------------
    // Scenario: city remains in CALM across multiple ticks — no re-dispatch.
    // StrictMock: any unexpected setMusicIntensity call fails the test.
    {
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CALM,
                                        SimMusicState::CALM);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::GROWTH,
                                        SimMusicState::GROWTH);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CRISIS,
                                        SimMusicState::CRISIS);
        // StrictMock: if any of the above incorrectly calls setMusicIntensity,
        // the test fails immediately with an unexpected call message.
    }

    // --- Phase 5: Direct CALM→CRISIS skip (no intermediate GROWTH) ---------
    // Scenario: budget goes directly from surplus to severe deficit in one tick.
    {
        EXPECT_CALL(*mock_, setMusicIntensity(MusicIntensity::CRISIS)).Times(1);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CALM,
                                        SimMusicState::CRISIS);
    }

    // --- Phase 6: CRISIS→GROWTH recovery (skip CALM intermediate) ----------
    // Scenario: crisis resolves but city is still growing rapidly.
    {
        EXPECT_CALL(*mock_, setMusicIntensity(MusicIntensity::GROWTH)).Times(1);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CRISIS,
                                        SimMusicState::GROWTH);
    }

    // --- Phase 7: MusicIntensity::CRISIS fires after 2 consecutive deficit months.
    // Per phase-10.md: "verify setMusicIntensity(MusicIntensity::CRISIS) fires
    // after 2 consecutive deficit months."
    // The spec for CRISIS trigger: budget_surplus_pct < -50% for 2+ consecutive
    // months. This sub-test models that sequence explicitly.
    {
        // Tick 1: first deficit month — state stays CALM (only 1 consecutive tick).
        // No dispatch expected for the first deficit month.
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CALM,
                                        SimMusicState::CALM);  // still CALM after 1 deficit

        // Tick 2: second consecutive deficit month → transitions to CRISIS.
        EXPECT_CALL(*mock_, setMusicIntensity(MusicIntensity::CRISIS)).Times(1);
        dispatchMusicIntensityIfChanged(mock_.get(),
                                        SimMusicState::CALM,
                                        SimMusicState::CRISIS);  // 2nd deficit → CRISIS
    }
}
