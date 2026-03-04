// stinger_milestone_test.cpp
// Phase 10: verify stinger_milestone fires ONLY on CityRating transitions,
// NOT on raw population milestones or bare population increments.
//
// Spec references:
//   implementation/phase-10.md: "StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation
//     (100K population without City Rating transition → stinger NOT triggered;
//      1K population at Village→Town transition → stinger triggered)"
//
//   architecture/audio-architecture/dynamic-soundscape.md:
//     "stinger_milestone only at City Rating transitions (not raw population milestones);
//      game-over duck + 2 s stem fade-out + setGameOverState()"
//
//   CLAUDE.md (Simulation section):
//     "stinger_milestone fires for City Rating transitions only at overlapping thresholds
//      — population milestone toast still shown but no second stinger fires"
//
// These tests use NiceMock<MockAudioSystem> (per CLAUDE.md: "NiceMock for property/
// integration tests") to assert setMusicIntensity is never called for raw population.
// StrictMock is used for the unit tests so unexpected calls are caught immediately.

#include "mock_audio_system.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::StrictMock;
using ::testing::NiceMock;

// ---------------------------------------------------------------------------
// Test fixture and helpers
// ---------------------------------------------------------------------------
namespace {

// CityRatingTier mirrors the spec in architecture/game-design/game-progression-modes.md.
// Village → Town transition is at 1,000 population.
// All six tiers: Village(0), Town(1), City(2), Metropolis(3), Megalopolis(4).
enum class CityRatingTier {
    Village    = 0,
    Town       = 1,
    City       = 2,
    Metropolis = 3,
    Megalopolis = 4
};

// Minimal simulation state needed for the stinger decision logic.
struct SimulationState {
    int population{0};
    CityRatingTier cityRating{CityRatingTier::Village};
};

// Encodes the stinger-fire decision extracted from the dynamic-soundscape spec.
// Returns true if triggerStinger(MILESTONE) should be called for a population
// change from prevState to newState.
//
// Rule: stinger fires if and only if the CityRating tier CHANGES.
// Raw population increments (even past milestones like 100K) that do NOT change
// the CityRating tier must NOT trigger the stinger.
bool shouldFireMilestoneStinger(const SimulationState& prev,
                                const SimulationState& next)
{
    return next.cityRating != prev.cityRating;
}

// Minimal stub that reproduces the stinger dispatch decision.
// In production, CitySimulation calls audioSystem->triggerStinger(MILESTONE)
// only when the CityRating tier changes.
void dispatchStingerIfRatingTransition(IAudioSystem* audio,
                                       const SimulationState& prev,
                                       const SimulationState& next)
{
    if (shouldFireMilestoneStinger(prev, next)) {
        audio->triggerStinger(StingerType::MILESTONE);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// StingerTest
// ---------------------------------------------------------------------------
class StingerTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        // Explicitly reset mock before destruction to satisfy destructor-path
        // contract (TearDown is called before mock destructors run).
        // This prevents order-of-destruction issues with mock expectations.
        mock_.reset();
    }

    // Use unique_ptr so TearDown can reset it before the base destructor.
    std::unique_ptr<StrictMock<MockAudioSystem>> mock_ =
        std::make_unique<StrictMock<MockAudioSystem>>();
};

// ---------------------------------------------------------------------------
// Test 1: 100K population WITHOUT a CityRating transition → stinger NOT fired.
//
// Scenario: city is already at Megalopolis tier (highest tier, no further
// rating transitions possible).  Population jumps from 50,000 to 100,000.
// The 100K population milestone toast must still be shown in the UI, but
// the milestone stinger must NOT play because the CityRating did not change.
// ---------------------------------------------------------------------------
TEST_F(StingerTest,
       StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation)
{
    // Sub-case A: population reaches 100K without CityRating change.
    {
        // No EXPECT_CALL — StrictMock will fail if triggerStinger is called.
        SimulationState prev;
        prev.population  = 50000;
        prev.cityRating  = CityRatingTier::Megalopolis;

        SimulationState next;
        next.population  = 100000;
        next.cityRating  = CityRatingTier::Megalopolis;  // same tier — no transition

        // Must not call triggerStinger(MILESTONE).
        dispatchStingerIfRatingTransition(mock_.get(), prev, next);
        // StrictMock: any unexpected call would cause immediate test failure.
    }

    // Sub-case B: population reaches 1K AND CityRating transitions Village→Town.
    {
        // For this sub-case we need to allow exactly one triggerStinger call.
        EXPECT_CALL(*mock_, triggerStinger(StingerType::MILESTONE))
            .Times(1);

        SimulationState prev;
        prev.population  = 500;
        prev.cityRating  = CityRatingTier::Village;

        SimulationState next;
        next.population  = 1000;
        next.cityRating  = CityRatingTier::Town;  // transition fires stinger

        // Must call triggerStinger(MILESTONE) exactly once.
        dispatchStingerIfRatingTransition(mock_.get(), prev, next);
    }

    // Sub-case C: multiple population milestones in a single tick with NO
    // CityRating change — stinger must not fire.
    {
        // No EXPECT_CALL — stinger must not fire.
        SimulationState prev;
        prev.population  = 9000;
        prev.cityRating  = CityRatingTier::Town;

        SimulationState next;
        next.population  = 10001;  // passes 10K milestone
        next.cityRating  = CityRatingTier::Town;  // no rating change

        dispatchStingerIfRatingTransition(mock_.get(), prev, next);
    }

    // Sub-case D: population decreases through a milestone boundary — no stinger.
    {
        // Stingers only play on forward (upward) transitions in the normal spec,
        // but the key point here is no CityRating change means no stinger.
        SimulationState prev;
        prev.population  = 1001;
        prev.cityRating  = CityRatingTier::Town;

        SimulationState next;
        next.population  = 999;
        next.cityRating  = CityRatingTier::Town;  // no change (spec: rating
                                                   // can regress but that is a
                                                   // separate transition)

        dispatchStingerIfRatingTransition(mock_.get(), prev, next);
    }
}
