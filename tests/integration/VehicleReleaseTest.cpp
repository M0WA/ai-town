// tests/integration/VehicleReleaseTest.cpp
// Phase 11q6: Verifies AudioSystem releases AL sources on
// releaseVehicleEnginePair(); uses real AudioSystem + null-backend thread.
//
// ENVIRONMENT: ALSOFT_DRIVERS=null (set via CMake aitown_add_tests ENVIRONMENT).
// The null OpenAL Soft backend allows AudioSystem construction without real audio
// hardware, enabling these tests to run in headless CI.
//
// Label: "integration"

#include <gtest/gtest.h>

#include <AL/al.h>

#include "AudioSystem.h"
#include "ManualClock.h"
#include "simulation_types.h"

#include <thread>
#include <chrono>
#include <memory>
#include <utility>

// ---------------------------------------------------------------------------
// Helper: bounded poll for an AL source to reach the expected state.
// Returns true if the state was reached within maxMs milliseconds.
// ---------------------------------------------------------------------------
static bool waitForSourceState(unsigned int src, int expectedState, int maxMs = 200) {
    for (int i = 0; i < maxMs / 5; ++i) {
        ALint state{};
        alGetSourcei(static_cast<ALuint>(src), AL_SOURCE_STATE, &state);
        if (state == expectedState) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// ---------------------------------------------------------------------------
// AudioSystemVehicleReleaseTest fixture
//
// Constructs a real AudioSystem with the null OpenAL Soft backend.
// logger=nullptr (falls back to stderr), alcFunctions=nullptr (real ALC).
// ---------------------------------------------------------------------------
class AudioSystemVehicleReleaseTest : public ::testing::Test {
protected:
    ManualClock clock_;
    std::unique_ptr<AudioSystem> audio_;

    void SetUp() override {
        try {
            audio_ = std::make_unique<AudioSystem>(nullptr, &clock_, nullptr);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "AudioSystem construction failed (null backend not available): "
                         << e.what();
        }
    }

    void TearDown() override {
        audio_.reset();  // destructs AudioSystem, joins audio thread
    }
};

// ===========================================================================
// TEST 1: SourceStoppedAfterRelease
//
// Acquires a vehicle engine pair, waits for AL_PLAYING, releases the pair,
// then verifies both sources reach AL_STOPPED with buffer detached (buf == 0).
// ===========================================================================
TEST_F(AudioSystemVehicleReleaseTest, SourceStoppedAfterRelease) {
    auto [idleIdx, moveIdx] = audio_->acquireVehicleEnginePair(ZoneType::Residential);
    if (idleIdx < 0 || moveIdx < 0) {
        GTEST_SKIP() << "acquireVehicleEnginePair returned negative indices — "
                        "vehicle audio assets may not be available.";
    }

    unsigned int srcIdle = audio_->testGetSourceHandle(idleIdx);
    unsigned int srcMove = audio_->testGetSourceHandle(moveIdx);

    // Wait for both sources to reach AL_PLAYING (audio thread processes pendingInit).
    // If vehicle audio assets (OGG files) are missing, no buffer is bound and sources
    // never reach AL_PLAYING. Skip rather than fail — asset availability is outside
    // the scope of this integration test.
    if (!waitForSourceState(srcIdle, AL_PLAYING) ||
        !waitForSourceState(srcMove, AL_PLAYING)) {
        // Release before skipping to avoid leaking the slot.
        audio_->releaseVehicleEnginePair(idleIdx, moveIdx);
        GTEST_SKIP() << "Vehicle audio assets not available — sources never reached AL_PLAYING.";
    }

    // Release the pair.
    audio_->releaseVehicleEnginePair(idleIdx, moveIdx);

    // Wait for both sources to reach AL_STOPPED (audio thread processes pendingRelease).
    ASSERT_TRUE(waitForSourceState(srcIdle, AL_STOPPED, 500))
        << "Idle source did not reach AL_STOPPED within timeout after release.";
    ASSERT_TRUE(waitForSourceState(srcMove, AL_STOPPED, 500))
        << "Move source did not reach AL_STOPPED within timeout after release.";

    // Verify sources are stopped and buffers detached.
    ALint stateIdle{}, stateMove{};
    ALint bufIdle{}, bufMove{};

    alGetSourcei(static_cast<ALuint>(srcIdle), AL_SOURCE_STATE, &stateIdle);
    ASSERT_EQ(stateIdle, AL_STOPPED);
    alGetSourcei(static_cast<ALuint>(srcIdle), AL_BUFFER, &bufIdle);
    ASSERT_EQ(bufIdle, 0) << "Idle source buffer should be detached after release.";

    alGetSourcei(static_cast<ALuint>(srcMove), AL_SOURCE_STATE, &stateMove);
    ASSERT_EQ(stateMove, AL_STOPPED);
    alGetSourcei(static_cast<ALuint>(srcMove), AL_BUFFER, &bufMove);
    ASSERT_EQ(bufMove, 0) << "Move source buffer should be detached after release.";
}

// ===========================================================================
// TEST 2: SlotReacquirableAfterRelease
//
// After releasing a vehicle engine pair, a new pair can be acquired
// (verifies the slot is returned to the free pool).
// ===========================================================================
TEST_F(AudioSystemVehicleReleaseTest, SlotReacquirableAfterRelease) {
    auto [idleIdx, moveIdx] = audio_->acquireVehicleEnginePair(ZoneType::Residential);
    if (idleIdx < 0 || moveIdx < 0) {
        GTEST_SKIP() << "acquireVehicleEnginePair returned negative indices — "
                        "vehicle audio assets may not be available.";
    }

    unsigned int srcIdle = audio_->testGetSourceHandle(idleIdx);
    unsigned int srcMove = audio_->testGetSourceHandle(moveIdx);

    // Wait for AL_PLAYING. Skip if assets are missing.
    if (!waitForSourceState(srcIdle, AL_PLAYING) ||
        !waitForSourceState(srcMove, AL_PLAYING)) {
        audio_->releaseVehicleEnginePair(idleIdx, moveIdx);
        GTEST_SKIP() << "Vehicle audio assets not available — sources never reached AL_PLAYING.";
    }

    // Release.
    audio_->releaseVehicleEnginePair(idleIdx, moveIdx);

    // Wait for AL_STOPPED.
    ASSERT_TRUE(waitForSourceState(srcIdle, AL_STOPPED, 500))
        << "Idle source did not reach AL_STOPPED within timeout after release.";
    ASSERT_TRUE(waitForSourceState(srcMove, AL_STOPPED, 500))
        << "Move source did not reach AL_STOPPED within timeout after release.";

    // Re-acquire — must succeed (slot returned to free pool).
    auto [idleIdx2, moveIdx2] = audio_->acquireVehicleEnginePair(ZoneType::Residential);
    ASSERT_GE(idleIdx2, 0)
        << "Re-acquire idle slot failed — slot not returned to free pool.";
    ASSERT_GE(moveIdx2, 0)
        << "Re-acquire move slot failed — slot not returned to free pool.";
}
