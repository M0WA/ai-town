// tests/audio/volume_control_test.cpp
//
// Phase 8 audio tests — two test areas registered via target_sources(audio_tests PRIVATE ...):
//
// 1. Headless setMasterVolume + alCheckError unit test
//    Verifies AudioSystem::setMasterVolume() calls alListenerf(AL_GAIN, gain) and
//    subsequent alCheckError() passes without error in headless CI (ALSOFT_DRIVERS=null).
//    Uses IAlcFunctions injection seam for headless operation.
//
// 2. AudioSystem_StingerDropIfAlreadyPlaying
//    Verifies that calling triggerStinger(StingerType::CRISIS) while a CRISIS stinger
//    is already playing results in the new trigger being silently dropped (not queued).
//    Uses MockAlcFunctions stub (manual stub, not GMock mock) per MockTerrainRNG pattern.
//
// Test label: unit (no display, no real audio device required).
// CI environment: ALSOFT_DRIVERS=null, AITOWN_HEADLESS=1.

#include "src/audio/ialc_functions.h"
#include "src/audio/audio_system.h"
#include "src/audio/audio_constants.h"
#include "tests/simulation/manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

// ---------------------------------------------------------------------------
// VolumeControlMockAlc — a MockAlcFunctions variant that allows the AudioSystem
// to construct successfully by providing stub getProcAddress results.
// Phase 8: this stub may need to return non-null for alcSetThreadContext to
// allow construction to succeed, depending on Phase 7 implementation.
// For now, stub returns nullptr; tests use GTEST_SKIP if construction throws.
// ---------------------------------------------------------------------------
struct VolumeControlMockAlc : IAlcFunctions {
    bool isExtensionPresent(const char* /*extName*/) override {
        return true;
    }
    void* getProcAddress(const char* /*funcName*/) override {
        // Phase 8 implementation: return appropriate stubs to allow construction.
        return nullptr;
    }
};

class VolumeControlTest : public ::testing::Test {
protected:
    ManualClock          clock_;
    VolumeControlMockAlc mockAlc_;
};

// ---------------------------------------------------------------------------
// Headless setMasterVolume test
// Phase 8 stub: GTEST_SKIP until AudioSystem constructor can succeed with
// the VolumeControlMockAlc seam (depends on Phase 7/8 impl status).
// ---------------------------------------------------------------------------
TEST_F(VolumeControlTest, SetMasterVolume_HeadlessCI_NoAlError) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // try {
    //     AudioSystem audio(&clock_, &mockAlc_);
    //     audio.setMasterVolume(0.5f);
    //     // alCheckError() should not have fired any error.
    //     SUCCEED();
    // } catch (const std::runtime_error&) {
    //     GTEST_SKIP() << "AudioSystem construction failed with mock ALC; awaiting Phase 8 impl";
    // }
    GTEST_SKIP() << "Awaiting Phase 8 AudioSystem volume control implementation";
}

// ---------------------------------------------------------------------------
// Stinger drop-if-already-playing test
// Verifies: triggerStinger(CRISIS) while CRISIS stinger is already playing
// silently drops the request (no queuing, no crash).
// Phase 8 stub: GTEST_SKIP until AudioSystem constructor can succeed.
// ---------------------------------------------------------------------------
TEST_F(VolumeControlTest, AudioSystem_StingerDropIfAlreadyPlaying) {
    // Phase 8 stub: full assertion in Phase 8 implementation.
    // try {
    //     AudioSystem audio(&clock_, &mockAlc_);
    //     audio.triggerStinger(StingerType::CRISIS);
    //     // Second trigger while first is still playing: should be silently dropped.
    //     audio.triggerStinger(StingerType::CRISIS);
    //     SUCCEED();
    // } catch (const std::runtime_error&) {
    //     GTEST_SKIP() << "AudioSystem construction failed with mock ALC; awaiting Phase 8 impl";
    // }
    GTEST_SKIP() << "Awaiting Phase 8 AudioSystem stinger drop-if-playing implementation";
}
