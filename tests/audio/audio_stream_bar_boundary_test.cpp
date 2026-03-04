// audio_stream_bar_boundary_test.cpp
// Phase 10: bar-boundary crossfade logic correctness tests.
//
// Spec references:
//   implementation/phase-10.md:
//     "AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake:
//       AL_BUFFERS_QUEUED read once per wake; same value passed to both
//       computeSamplesPlayed() and computeNextBarBoundary()."
//     "AudioStream_BarBoundary_StreamStart_NoFalseFire:
//       verify crossfade condition does not fire when
//       m_samplesQueued < buffersQueued * kSamplesPerBuffer
//       and m_nextBarBoundary = 0"
//
//   architecture/audio-architecture/dynamic-soundscape.md:
//     "computeSamplesPlayed formula:
//       samplesPlayed = (samplesQueued > buffersQueued * kSamplesPerBuffer)
//                       ? samplesQueued - buffersQueued * kSamplesPerBuffer : 0
//       (underflow guard: at stream start samplesPlayed returns 0, preventing
//        a false crossfade fire)"
//     "m_nextBarBoundary > 0 guard on crossfade condition
//       (prevents false fire at stream start)"
//     "CRITICAL: Read AL_BUFFERS_QUEUED exactly once per audio thread wake
//      AND read it BEFORE calling alSourceUnqueueBuffers."
//
// These tests are pure-math: they exercise the formula logic without
// requiring a real OpenAL device.

#include <gtest/gtest.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// Mirror of the AudioStream constants and formulas from dynamic-soundscape.md.
// These will be replaced by real includes once AudioSystem.cpp is implemented.
// ---------------------------------------------------------------------------
namespace {

// kSamplesPerBuffer: 64 KB buffer, stereo int16 format.
// 64 * 1024 bytes / (2 channels * 2 bytes per sample) = 16384 frames/buffer.
static constexpr uint32_t kSamplesPerBuffer = 64u * 1024u / (2u * 2u); // = 16384

// Compute estimated samples played from samples queued and the current
// AL_BUFFERS_QUEUED value (read BEFORE alSourceUnqueueBuffers).
//
// Underflow guard: at stream start, m_samplesQueued may be less than
// buffersQueued * kSamplesPerBuffer (buffers queued but none played yet);
// in that case return 0, which prevents a false crossfade fire.
uint64_t computeSamplesPlayed(uint64_t samplesQueued, int32_t buffersQueued)
{
    const uint64_t queued = static_cast<uint64_t>(buffersQueued) * kSamplesPerBuffer;
    return (samplesQueued > queued) ? (samplesQueued - queued) : 0u;
}

// Compute the next bar boundary after the current playback position.
// sr         — sample rate (e.g. 44100 Hz)
// bpm        — beats per minute (e.g. 90)
// beatsPerBar — typically 4
// buffersQueued — current AL_BUFFERS_QUEUED value (read BEFORE unqueuing)
uint64_t computeNextBarBoundary(uint64_t samplesQueued,
                                int32_t  buffersQueued,
                                uint32_t sr,
                                float    bpm,
                                int      beatsPerBar)
{
    const uint64_t samplesPlayed = computeSamplesPlayed(samplesQueued, buffersQueued);
    const uint64_t spb = static_cast<uint64_t>(
        (static_cast<double>(sr) * 60.0 / static_cast<double>(bpm))
        * static_cast<double>(beatsPerBar));
    return ((samplesPlayed / spb) + 1u) * spb;
}

// Returns true if the crossfade condition passes (bar boundary reached AND
// m_nextBarBoundary has been initialised to a non-zero value).
bool crossfadeCondition(uint64_t samplesPlayed, uint64_t nextBarBoundary)
{
    return (samplesPlayed >= nextBarBoundary) && (nextBarBoundary > 0u);
}

// Simulates one audio thread wake.
// Enforces the mandated read order from the spec:
//   (1) read AL_BUFFERS_QUEUED    -> buffersQueued (LOCAL, read ONCE)
//   (2) read AL_BUFFERS_PROCESSED -> buffersProcessed
//   (3) alSourceUnqueueBuffers    -> decrements the runtime queue depth
//   (4) decode PCM                -> increments samplesQueued
//   (5) alSourceQueueBuffers
//
// The test verifies that computeSamplesPlayed() and computeNextBarBoundary()
// use the SAME buffersQueued value captured before the unqueue step (step 1).
struct WakeResult {
    uint64_t samplesPlayedBeforeUnqueue;  // calculated with pre-unqueue depth
    uint64_t samplesPlayedAfterUnqueue;   // calculated with post-unqueue depth (wrong order)
    bool     crossfireFired;
};

WakeResult simulateOneWake(
    uint64_t  samplesQueued,
    int32_t   buffersQueuedBeforeUnqueue,  // AL_BUFFERS_QUEUED read before alSourceUnqueueBuffers
    int32_t   buffersProcessed,            // AL_BUFFERS_PROCESSED (how many were just unqueued)
    uint64_t  nextBarBoundary)
{
    // Step 1: read AL_BUFFERS_QUEUED ONCE into a local (before unqueue).
    const int32_t buffersQueued = buffersQueuedBeforeUnqueue;  // captured ONCE

    // Step 3 (simulated): after unqueue, the queue depth decreases.
    const int32_t buffersQueuedAfterUnqueue = buffersQueued - buffersProcessed;

    // Correct: uses the pre-unqueue buffersQueued value for the formula.
    const uint64_t spPlayedCorrect = computeSamplesPlayed(samplesQueued, buffersQueued);
    // Wrong: uses the post-unqueue value (the bug this test guards against).
    const uint64_t spPlayedWrong   = computeSamplesPlayed(samplesQueued, buffersQueuedAfterUnqueue);

    WakeResult r{};
    r.samplesPlayedBeforeUnqueue = spPlayedCorrect;
    r.samplesPlayedAfterUnqueue  = spPlayedWrong;
    r.crossfireFired = crossfadeCondition(spPlayedCorrect, nextBarBoundary);
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// AudioStreamTest
// ---------------------------------------------------------------------------

// Test 1: AL_BUFFERS_QUEUED read order — consistent value used for both
// computeSamplesPlayed() and computeNextBarBoundary().
//
// Scenario: 8 buffers queued, 2 buffers processed in this wake.
//   - buffersQueued (before unqueue) = 8
//   - buffersProcessed               = 2
//   - buffersQueued (after unqueue)  = 6
//
// At steady state with m_samplesQueued advanced well past the queue depth,
// the pre-unqueue estimate gives a LOWER samplesPlayed than the post-unqueue
// estimate (because subtracting more buffered samples gives a smaller number
// of "played" samples).  The spec requires the PRE-unqueue value because
// reading after unqueue underestimates the buffer backlog and produces a
// higher samplesPlayed — which can fire crossfades EARLY.
//
// This test verifies:
//  (a) pre-unqueue and post-unqueue values differ when buffers are processed.
//  (b) the crossfade condition uses the pre-unqueue value.
//  (c) when the next bar boundary is within the post-unqueue (inflated) range
//      but not within the pre-unqueue (correct) range, the crossfade does NOT
//      fire — proving that reading order matters.
TEST(AudioStreamTest,
     AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake)
{
    // 90 BPM, 4/4 time, 44100 Hz
    // Samples per bar = (44100 * 60 / 90) * 4 = 117600 samples
    constexpr uint64_t kSamplesPerBar = 117600u;

    // Steady-state: 8 buffers are queued; 2 have been processed this wake.
    constexpr int32_t kBuffersQueuedBeforeUnqueue = 8;
    constexpr int32_t kBuffersProcessed           = 2;

    // m_samplesQueued at the start of this wake.
    // We've pushed 10 * kSamplesPerBuffer samples through the decoder.
    const uint64_t samplesQueued = 10u * kSamplesPerBuffer;  // = 163840

    // Correct samplesPlayed (pre-unqueue, 8 buffers in flight):
    //   samplesPlayed = samplesQueued - 8 * kSamplesPerBuffer
    //                 = 163840 - 131072 = 32768
    const uint64_t spCorrect = computeSamplesPlayed(samplesQueued, kBuffersQueuedBeforeUnqueue);
    EXPECT_EQ(spCorrect, samplesQueued - 8u * kSamplesPerBuffer);

    // Wrong samplesPlayed (post-unqueue, only 6 buffers counted):
    //   samplesPlayed = samplesQueued - 6 * kSamplesPerBuffer
    //                 = 163840 - 98304 = 65536
    const uint64_t spWrong = computeSamplesPlayed(
        samplesQueued, kBuffersQueuedBeforeUnqueue - kBuffersProcessed);
    EXPECT_EQ(spWrong, samplesQueued - 6u * kSamplesPerBuffer);

    // The two values must differ when buffers were processed.
    EXPECT_NE(spCorrect, spWrong)
        << "Pre- and post-unqueue samplesPlayed estimates must differ "
           "when buffersProcessed > 0";

    // Correct estimate is SMALLER (more conservative — less likely to fire early).
    EXPECT_LT(spCorrect, spWrong)
        << "Pre-unqueue estimate must be smaller (larger backlog subtracted) "
           "than post-unqueue estimate";

    // Set the next bar boundary to a value that the post-unqueue estimate
    // would exceed but the pre-unqueue estimate does NOT.
    // nextBarBoundary = midpoint of (spCorrect, spWrong).
    const uint64_t nextBarBoundary = (spCorrect + spWrong) / 2u;
    ASSERT_GT(nextBarBoundary, 0u) << "nextBarBoundary must be positive for this test";
    ASSERT_GT(spWrong,   nextBarBoundary)
        << "post-unqueue estimate must exceed nextBarBoundary (pre-condition)";
    ASSERT_LT(spCorrect, nextBarBoundary)
        << "pre-unqueue estimate must be below nextBarBoundary (pre-condition)";

    // Simulate the wake:
    WakeResult r = simulateOneWake(samplesQueued,
                                   kBuffersQueuedBeforeUnqueue,
                                   kBuffersProcessed,
                                   nextBarBoundary);

    // The crossfade must NOT fire because the CORRECT (pre-unqueue) estimate
    // does not yet exceed the bar boundary.
    EXPECT_FALSE(r.crossfireFired)
        << "Crossfade must NOT fire when the correct (pre-unqueue) samplesPlayed "
           "does not exceed nextBarBoundary — reading after unqueue would "
           "produce a false early fire";

    // Confirm: if we had used the wrong (post-unqueue) estimate, the crossfade
    // WOULD have fired, demonstrating why read order matters.
    const bool wouldFireWithWrongOrder =
        crossfadeCondition(r.samplesPlayedAfterUnqueue, nextBarBoundary);
    EXPECT_TRUE(wouldFireWithWrongOrder)
        << "Post-unqueue estimate would have fired the crossfade prematurely";

    // Additional: verify computeNextBarBoundary uses the same pre-unqueue value.
    // At 90 BPM/4 beats with spCorrect = 32768:
    //   barIndex = 32768 / 117600 = 0
    //   nextBarBoundary = (0 + 1) * 117600 = 117600
    const uint64_t nextBar = computeNextBarBoundary(
        samplesQueued,
        kBuffersQueuedBeforeUnqueue,  // same pre-unqueue value
        44100u, 90.0f, 4);

    // Computing with the wrong (post-unqueue) value gives a different result:
    const uint64_t nextBarWrong = computeNextBarBoundary(
        samplesQueued,
        kBuffersQueuedBeforeUnqueue - kBuffersProcessed,  // wrong order
        44100u, 90.0f, 4);

    // Both values should be valid bar boundaries, but at different positions.
    EXPECT_NE(nextBar, nextBarWrong)
        << "computeNextBarBoundary must produce a different result when the "
           "wrong (post-unqueue) buffersQueued value is used";
}

// Test 2: No false crossfade fire at stream start.
//
// At stream start:
//   m_samplesQueued < buffersQueued * kSamplesPerBuffer
//     => underflow guard returns samplesPlayed = 0
//   m_nextBarBoundary = 0 (zero-initialised, not yet computed)
//     => guard condition (m_nextBarBoundary > 0) prevents crossfade fire.
//
// This covers the spec requirement:
//   "m_nextBarBoundary > 0 guard on crossfade condition (prevents false fire
//    at stream start)"
//   AND
//   "at stream start samplesPlayed returns 0, preventing a false crossfade fire"
TEST(AudioStreamTest,
     AudioStream_BarBoundary_StreamStart_NoFalseFire)
{
    // Stream just started: 8 buffers have been queued to AL but none played yet.
    // The decoder has pushed 8 * kSamplesPerBuffer samples into the queue.
    const uint64_t samplesQueued  = 8u * kSamplesPerBuffer;
    const int32_t  buffersQueued  = 8;

    // At stream start, m_nextBarBoundary is zero-initialised.
    constexpr uint64_t kNextBarBoundaryAtStreamStart = 0u;

    // --- Sub-case A: underflow guard ------------------------------------------
    // samplesQueued == buffersQueued * kSamplesPerBuffer (no extra queued beyond
    // the current AL queue depth) → computeSamplesPlayed must return 0.
    const uint64_t spAtStreamStart = computeSamplesPlayed(samplesQueued, buffersQueued);
    EXPECT_EQ(spAtStreamStart, 0u)
        << "At stream start, samplesPlayed must be 0 (underflow guard active): "
           "m_samplesQueued == buffersQueued * kSamplesPerBuffer";

    // --- Sub-case B: nextBarBoundary guard ------------------------------------
    // Even if samplesPlayed were somehow non-zero, the m_nextBarBoundary == 0
    // guard must prevent crossfade from firing.
    EXPECT_FALSE(crossfadeCondition(spAtStreamStart, kNextBarBoundaryAtStreamStart))
        << "Crossfade must NOT fire when m_nextBarBoundary == 0 (stream start)";

    // Verify guard holds even when samplesPlayed > 0 (defensive test).
    constexpr uint64_t kNonZeroSamplesPlayed = 44100u;  // 1 second of samples
    EXPECT_FALSE(crossfadeCondition(kNonZeroSamplesPlayed, kNextBarBoundaryAtStreamStart))
        << "Crossfade must NOT fire when m_nextBarBoundary == 0 even if "
           "samplesPlayed > 0 — the boundary guard is the primary safety net";

    // --- Sub-case C: both guards active together at stream start -------------
    // This is the normal stream-start scenario: samplesPlayed == 0 AND
    // m_nextBarBoundary == 0 → crossfade must definitely not fire.
    EXPECT_FALSE(crossfadeCondition(0u, 0u))
        << "Crossfade must NOT fire when both samplesPlayed == 0 and "
           "m_nextBarBoundary == 0";

    // --- Sub-case D: initialization branch fires first boundary correctly -----
    // After the first batch of PCM is decoded (m_samplesQueued > 0) AND
    // m_nextBarBoundary is still 0, computeNextBarBoundary() is called once.
    // The result must be > 0 to arm the guard for subsequent wakes.
    //
    // Simulate: decoder has pushed kSamplesPerBuffer more frames than the AL queue.
    const uint64_t samplesQueuedAfterFirstDecode = samplesQueued + kSamplesPerBuffer;
    const uint64_t firstBarBoundary = computeNextBarBoundary(
        samplesQueuedAfterFirstDecode,
        buffersQueued,  // still 8 (we decoded one more buffer)
        44100u, 90.0f, 4);

    EXPECT_GT(firstBarBoundary, 0u)
        << "computeNextBarBoundary must return > 0 after stream start initialisation";

    // After initialisation, if samplesPlayed < firstBarBoundary, no crossfade.
    const uint64_t spAfterFirstDecode = computeSamplesPlayed(
        samplesQueuedAfterFirstDecode, buffersQueued);
    EXPECT_FALSE(crossfadeCondition(spAfterFirstDecode, firstBarBoundary))
        << "Crossfade must NOT fire on the same wake as first-boundary "
           "initialisation (samplesPlayed is still near zero)";
}
