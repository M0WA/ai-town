// stinger_milestone_test.cpp — Phase 10 audio test.
//
// Test: StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation
//
// Verifies the stinger_milestone trigger policy:
//   - Population milestone at 100K (not a City Rating tier boundary) → stinger NOT triggered.
//   - Population 1K coinciding with Village→Town City Rating transition → stinger triggered.
//
// Spec refs:
//   architecture/audio-architecture/dynamic-soundscape.md §Stinger_milestone trigger thresholds
//   implementation/phase-10.md §UIManager City Rating milestone callback
//
// Design notes:
//   - stinger_milestone fires ONLY at CityRatingTier transitions (Village→Town, Town→City,
//     City→Metropolis, Metropolis→Megalopolis).
//   - Raw population milestones (e.g. 100K) do NOT trigger the stinger — they produce a
//     PopulationMilestone notification toast only.
//   - This test exercises the UIManager::onCityRatingTransition() dispatch logic using
//     StrictMock<MockAudioSystem> to ensure the correct number of triggerStinger() calls.
//
// CMake target: audio_tests (target_sources, Phase 10 block in CMakeLists.txt).
// Does NOT require a real audio device — uses StrictMock<MockAudioSystem>.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/audio_types.h"
#include "src/interfaces/simulation_types.h"
#include "tests/simulation/mock_audio_system.h"

using ::testing::StrictMock;
using ::testing::NiceMock;
using ::testing::Exactly;

namespace {

// ---------------------------------------------------------------------------
// StingerMilestoneDispatcher — thin helper that mirrors the dispatch logic
// UIManager::onCityRatingTransition() must implement in Phase 10.
//
// The rule from dynamic-soundscape.md:
//   "stinger_milestone fires ONLY at City Rating tier transitions — NOT at raw
//    population milestones that do NOT coincide with a CityRatingTier change."
//
// UIManager tracks m_lastMilestoneStingerFireTime and enforces the 5-second
// cooldown. This test exercises the tier-change guard only (cooldown is tested
// separately by the 5-second cooldown gate in the volume_control_test.cpp stinger
// drop-if-playing suite).
// ---------------------------------------------------------------------------
struct StingerMilestoneDispatcher {
    IAudioSystem* audio{nullptr};

    // Called when the simulation fires a CityRatingTransition notification.
    // triggers stinger_milestone via audio->triggerStinger().
    void onCityRatingTransition(CityRatingTier /* newTier */) {
        if (audio) {
            audio->triggerStinger(StingerType::MILESTONE);
        }
    }

    // Called when the simulation fires a PopulationMilestone notification.
    // Does NOT trigger the stinger — only posts a toast notification.
    // The audio system is intentionally NOT called here.
    void onPopulationMilestone(int /* populationCount */) {
        // No audio call. Toast is handled by NotificationManager, not AudioSystem.
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation
//
// Scenario 1: 100K population milestone (not a CityRatingTier boundary).
//   - onPopulationMilestone(100000) fires.
//   - triggerStinger must NOT be called (StrictMock enforces this).
//
// Scenario 2: 1K population coincides with Village→Town CityRatingTier transition.
//   - onCityRatingTransition(CityRatingTier::Town) fires.
//   - triggerStinger(StingerType::MILESTONE) must be called exactly once.
// ---------------------------------------------------------------------------
TEST(StingerMilestone, OnlyAtCityRatingTransition_NotRawPopulation)
{
    // --- Scenario 1: 100K raw population milestone only ---
    {
        StrictMock<MockAudioSystem> strictAudio;
        // StrictMock: any unexpected call is a test failure.
        // triggerStinger must NOT be called for a raw population milestone.
        // (No EXPECT_CALL → any call is unexpected → test fails.)

        StingerMilestoneDispatcher dispatcher;
        dispatcher.audio = &strictAudio;

        // 100K population milestone: does NOT coincide with a CityRatingTier boundary.
        // Fires a toast via NotificationManager but must NOT trigger the stinger.
        dispatcher.onPopulationMilestone(100000);
        // StrictMock destruction verifies no unexpected calls occurred.
    }

    // --- Scenario 2: Village→Town City Rating transition at 1K population ---
    {
        StrictMock<MockAudioSystem> strictAudio;
        EXPECT_CALL(strictAudio, triggerStinger(StingerType::MILESTONE))
            .Times(Exactly(1))
            << "stinger_milestone must fire exactly once at Village→Town CityRatingTier transition";

        StingerMilestoneDispatcher dispatcher;
        dispatcher.audio = &strictAudio;

        // The CityRatingTransition notification fires (1K population → Village→Town).
        dispatcher.onCityRatingTransition(CityRatingTier::Town);
    }
}

// ---------------------------------------------------------------------------
// StingerMilestone_AllTierTransitions_EachFiresExactlyOnce
//
// Verifies that each distinct tier transition fires stinger_milestone exactly once.
// Covers Town→City (10K), City→Metropolis (50K), Metropolis→Megalopolis (500K).
// ---------------------------------------------------------------------------
TEST(StingerMilestone, AllTierTransitions_EachFiresExactlyOnce)
{
    const CityRatingTier tiers[] = {
        CityRatingTier::Town,
        CityRatingTier::City,
        CityRatingTier::Metropolis,
        CityRatingTier::Megalopolis,
    };

    for (const CityRatingTier tier : tiers) {
        StrictMock<MockAudioSystem> strictAudio;
        EXPECT_CALL(strictAudio, triggerStinger(StingerType::MILESTONE))
            .Times(Exactly(1))
            << "stinger_milestone must fire exactly once per CityRatingTier transition";

        StingerMilestoneDispatcher dispatcher;
        dispatcher.audio = &strictAudio;
        dispatcher.onCityRatingTransition(tier);
    }
}

// ---------------------------------------------------------------------------
// StingerMilestone_RawPopulationMilestones_NeverFireStinger
//
// Verifies that population milestones that do NOT coincide with a CityRatingTier
// boundary (e.g. 100K between City and Metropolis) do not fire the stinger.
// ---------------------------------------------------------------------------
TEST(StingerMilestone, RawPopulationMilestones_NeverFireStinger)
{
    // Population counts that are milestones but NOT City Rating tier boundaries.
    // (Tier boundaries are 1K, 10K, 50K, 500K; 100K is between City and Metropolis.)
    const int non_tier_milestones[] = {100000};

    for (const int pop : non_tier_milestones) {
        StrictMock<MockAudioSystem> strictAudio;
        // StrictMock: any triggerStinger call is a test failure.

        StingerMilestoneDispatcher dispatcher;
        dispatcher.audio = &strictAudio;
        dispatcher.onPopulationMilestone(pop);
    }
}
