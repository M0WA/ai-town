// tests/audio/audio_system_transition_main_menu_test.cpp
//
// Phase 11m D6-audio: AudioSystem::transitionToMainMenu() test.
// Verifies that calling transitionToMainMenu() sets the main-menu music looping
// flag accessible via isMainMenuMusicLooping().
//
// Uses real AudioSystem with null audio driver (ALSOFT_DRIVERS=null in CI).
// Skips if AudioSystem construction fails (no audio device available).
//
// ZERO AL headers included — all verification via public AudioSystem API.
//
// Added to audio_tests via:
//   target_sources(audio_tests PRIVATE tests/audio/audio_system_transition_main_menu_test.cpp)
// Do NOT call add_executable(audio_tests ...) or aitown_add_tests(audio_tests ...) again.

#include "src/audio/AudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <stdexcept>

class AudioSystemTransitionMainMenuTest : public ::testing::Test {
protected:
    ManualClock clock_;
};

// ---------------------------------------------------------------------------
// AudioSystem_TransitionToMainMenu_LoopingFlagSet
//
// Construct AudioSystem with null IAlcFunctions injection (ALSOFT_DRIVERS=null).
// Call transitionToMainMenu(). Assert isMainMenuMusicLooping() == true.
// ---------------------------------------------------------------------------
TEST_F(AudioSystemTransitionMainMenuTest, AudioSystem_TransitionToMainMenu_LoopingFlagSet)
{
    try {
        AudioSystem audio(nullptr, &clock_);

        audio.transitionToMainMenu();

        EXPECT_TRUE(audio.isMainMenuMusicLooping())
            << "isMainMenuMusicLooping() should be true after transitionToMainMenu()";
    } catch (const std::runtime_error& e) {
        GTEST_SKIP() << "AudioSystem construction failed (no audio device?): " << e.what();
    }
}
