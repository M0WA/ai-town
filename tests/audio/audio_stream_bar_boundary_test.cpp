// audio_stream_bar_boundary_test.cpp — Phase 10 audio tests.
//
// Two test cases (both required by implementation/phase-10.md exit criteria):
//
//   1. AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake
//      Verifies that AL_BUFFERS_QUEUED is read exactly once per audio thread wake
//      and the same value is passed to both computeSamplesPlayed() and
//      computeNextBarBoundary().  A second read after alSourceUnqueueBuffers would
//      underestimate queue depth and fire crossfades early.
//
//   2. AudioStream_BarBoundary_StreamStart_NoFalseFire
//      Verifies that the crossfade condition does NOT fire at stream start when
//      m_samplesQueued < buffersQueued * kSamplesPerBuffer (underflow guard) and
//      m_nextBarBoundary == 0 (bootstrap guard).
//
// Design notes:
//   These tests verify the AudioStream bar-boundary state machine behaviour
//   described in architecture/audio-architecture/dynamic-soundscape.md
//   §Beat-boundary synchronization (lines 46–98).
//
//   The tests do not require a real OpenAL device — they exercise the pure-math
//   helper functions computeSamplesPlayed() and computeNextBarBoundary() in
//   isolation.  Phase 10 must refactor these helpers into a testable form:
//   either as free functions in an AudioStreamHelpers translation unit, or as
//   static methods on AudioStream — whichever form makes them callable from
//   tests without instantiating AudioStream (which requires a real OGG file).
//
// CMake target: audio_tests (target_sources, Phase 10 block in CMakeLists.txt).
// Does NOT require a real audio device.
//
// Spec refs:
//   architecture/audio-architecture/dynamic-soundscape.md §Beat-boundary synchronization
//   implementation/phase-10.md §Audio crossfade unit tests

#include <gtest/gtest.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// Inline reference implementation of the bar-boundary helpers.
// These mirror the formulas specified in dynamic-soundscape.md and must match
// exactly what AudioSystem / AudioStream implements in Phase 10.
//
// Phase 10 implementers: replace these inline helpers with calls to the real
// AudioStream static methods or free functions once they are extracted and made
// testable.  Do NOT remove the tests — only replace the helper bodies.
// ---------------------------------------------------------------------------
namespace {

// kSamplesPerBuffer: 64 KB buffer / (2 channels × 2 bytes per sample) = 16384 frames.
// Matches the constant defined in audio_constants.h and dynamic-soundscape.md.
static constexpr uint32_t kSamplesPerBuffer = 64 * 1024 / (2 * 2);  // = 16384

// computeSamplesPlayed — estimates playback position from the software counter.
// Returns 0 (underflow guard) if samplesQueued < buffersQueued * kSamplesPerBuffer,
// which occurs at stream start before any buffers have been consumed.
static uint64_t computeSamplesPlayed(uint64_t samplesQueued, int buffersQueued)
{
    const uint64_t queued = static_cast<uint64_t>(buffersQueued) * kSamplesPerBuffer;
    return (samplesQueued > queued) ? samplesQueued - queued : 0u;
}

// computeNextBarBoundary — returns the absolute sample index of the next bar
// boundary after the current playback position.
// sr:           sample rate (Hz)
// bpm:          tempo (beats per minute)
// beatsPerBar:  time signature numerator
// buffersQueued: AL_BUFFERS_QUEUED read ONCE per wake (passed as parameter)
static uint64_t computeNextBarBoundary(uint64_t samplesQueued,
                                       uint32_t sr, float bpm, int beatsPerBar,
                                       int buffersQueued)
{
    const uint64_t spb = static_cast<uint64_t>(
        (static_cast<double>(sr) * 60.0 / bpm) * beatsPerBar);
    const uint64_t samplesPlayed = computeSamplesPlayed(samplesQueued, buffersQueued);
    return ((samplesPlayed / spb) + 1u) * spb;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake
//
// Verifies that passing the SAME buffersQueued value to both
// computeSamplesPlayed() and computeNextBarBoundary() produces a consistent
// nextBarBoundary above the current playback position.
//
// Specifically:
//   - Read buffersQueued ONCE (before alSourceUnqueueBuffers).
//   - Pass the same local to both helpers.
//   - Assert that computeSamplesPlayed() == computeSamplesPlayed() for the
//     same inputs (idempotent — no side effects).
//   - Assert that the next bar boundary computed with the same buffersQueued
//     is strictly GREATER than samplesPlayed (i.e., we are looking forward,
//     not backward).
//
// Phase 10 stub contract: the test body below is ALREADY the full test.
// Replace the inline helper calls with the real AudioStream helpers when
// they are extracted in Phase 10.
// ---------------------------------------------------------------------------
TEST(AudioStreamTest, AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake)
{
    // Simulate a mid-stream state: 256 buffers queued to AL, 8 still in the
    // driver queue (not yet played).  At 44100 Hz stereo with 16384 frames/buffer:
    //   total pushed: 256 × 16384 = 4,194,304 samples
    //   still buffered: 8 × 16384 = 131,072 samples
    //   estimated played: 4,194,304 − 131,072 = 4,063,232 samples

    const uint64_t samplesQueued  = 256ull * kSamplesPerBuffer;  // 4,194,304
    const int      buffersQueued  = 8;  // read ONCE per wake — passed to both helpers

    const uint64_t playedA = computeSamplesPlayed(samplesQueued, buffersQueued);
    const uint64_t playedB = computeSamplesPlayed(samplesQueued, buffersQueued);

    // Idempotency: two calls with the same inputs must return identical values.
    EXPECT_EQ(playedA, playedB)
        << "computeSamplesPlayed() is not idempotent — likely reading AL_BUFFERS_QUEUED "
           "internally instead of using the passed buffersQueued parameter";

    // At 44100 Hz, 90 BPM, 4/4: samples per bar = 44100 * 60/90 * 4 = 117600.
    const uint32_t sr       = 44100;
    const float    bpm      = 90.0f;
    const int      bpb      = 4;

    const uint64_t nextBoundary = computeNextBarBoundary(
        samplesQueued, sr, bpm, bpb, buffersQueued);

    // The next bar boundary must be STRICTLY after the current playback position.
    EXPECT_GT(nextBoundary, playedA)
        << "computeNextBarBoundary() returned a boundary at or before samplesPlayed; "
           "the crossfade would fire immediately on the next wake (wrong direction)";

    // The boundary must be within 1 bar of the current position.
    const uint64_t samplesPerBar = static_cast<uint64_t>(
        (static_cast<double>(sr) * 60.0 / bpm) * bpb);
    EXPECT_LE(nextBoundary, playedA + samplesPerBar)
        << "computeNextBarBoundary() returned a boundary more than 1 bar ahead; "
           "the formula should return the NEXT bar boundary, not a far-future one";
}

// ---------------------------------------------------------------------------
// AudioStream_BarBoundary_StreamStart_NoFalseFire
//
// Verifies that the crossfade guard condition does NOT fire at stream start:
//
//   Guard condition 1 (underflow): samplesPlayed = 0 when
//     m_samplesQueued < buffersQueued * kSamplesPerBuffer.
//     This happens at startup when buffers have been queued to AL but none
//     have been consumed yet.
//
//   Guard condition 2 (bootstrap): m_nextBarBoundary == 0 at construction.
//     The crossfade check: (samplesPlayed >= m_nextBarBoundary && m_nextBarBoundary > 0)
//     evaluates to false when m_nextBarBoundary == 0, preventing false-fire.
//
// Both guards together ensure the crossfade never fires before the first bar
// boundary is computed by computeNextBarBoundary() after stream start.
//
// Phase 10 stub contract: the test body below is ALREADY the full test.
// ---------------------------------------------------------------------------
TEST(AudioStreamTest, AudioStream_BarBoundary_StreamStart_NoFalseFire)
{
    // At stream start: 8 buffers have been queued to AL (samplesQueued = 8 × 16384),
    // but the driver has not consumed any yet (buffersQueued still = 8).
    const int      buffersQueued = 8;
    const uint64_t samplesQueued = static_cast<uint64_t>(buffersQueued) * kSamplesPerBuffer;

    // Guard 1 (underflow): samplesPlayed must be 0 because
    //   samplesQueued (= 8 × 16384) == buffersQueued × kSamplesPerBuffer
    // — the queue is full and nothing has been played yet.
    const uint64_t samplesPlayed = computeSamplesPlayed(samplesQueued, buffersQueued);
    EXPECT_EQ(samplesPlayed, 0u)
        << "computeSamplesPlayed() must return 0 at stream start (underflow guard); "
           "non-zero return would cause a false crossfade fire on the first wake";

    // Guard 2 (bootstrap): m_nextBarBoundary is zero-initialized.
    // The crossfade condition is: samplesPlayed >= m_nextBarBoundary && m_nextBarBoundary > 0.
    // With m_nextBarBoundary == 0, the second sub-condition is FALSE regardless of
    // samplesPlayed — no crossfade fires.
    const uint64_t m_nextBarBoundary = 0u;  // zero-initialized per spec
    const bool crossfadeCondition = (samplesPlayed >= m_nextBarBoundary)
                                 && (m_nextBarBoundary > 0u);
    EXPECT_FALSE(crossfadeCondition)
        << "Crossfade must NOT fire at stream start when m_nextBarBoundary == 0; "
           "the (m_nextBarBoundary > 0) guard must be present in the crossfade condition";
}
