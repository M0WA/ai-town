#pragma once
// AudioStream.h — OGG Vorbis streaming helpers and per-stream state.
//
// Provides the AudioStream struct (per-stream state held by AudioSystem) and
// OGG open/close/read utilities used by AudioSystem's streaming loop.
// The OggVorbis_File struct is declared here via a forward declaration to avoid
// pulling <vorbis/vorbisfile.h> into any non-audio TU.
//
// All functions in this header are implemented in AudioStream.cpp which includes
// the real libvorbisfile headers.

#include <cstdint>
#include <string>

struct OggVorbis_File;

// ---------------------------------------------------------------------------
// AudioStream — per-stream state for one streaming OGG source.
// Moved here from AudioSystem.h (B-33) so AudioStream.h is the single
// definition point; AudioSystem.h #includes this header to get the type.
// ---------------------------------------------------------------------------
struct AudioStream {
    // AL source handle backing this stream slot (sourced from m_sources[]).
    unsigned int sourceHandle{0};  // ALuint

    // AL buffer pool for this stream (8 × 64 KB buffers).
    static constexpr int kNumBuffers = 8;
    unsigned int buffers[kNumBuffers]{};  // ALuint[]

    // Software sample counter — incremented by frame count decoded each update.
    // Used for bar-boundary crossfade calculation (NOT AL_SAMPLE_OFFSET).
    // "Samples" = PCM frames (per-channel samples at one point in time).
    uint64_t m_samplesQueued{0};

    // Absolute sample-frame index of the next bar boundary.
    // Initialized to 0; bootstrap branch fires once after first decode.
    // Guard: m_nextBarBoundary > 0 prevents false crossfade at stream start.
    uint64_t m_nextBarBoundary{0};

    // kSamplesPerBuffer: 64 KB / (2 channels × 2 bytes/sample) = 16384 frames.
    static constexpr uint32_t kSamplesPerBuffer = 64u * 1024u / (2u * 2u);

    // OggVorbis file handle — persistent member opened at stream start,
    // closed via ov_clear() in the destructor/stop path.
    // Raw pointer managed manually — libvorbisfile owns the struct memory.
    OggVorbis_File* vf{nullptr};

    // Sidecar metadata (music stems only — ambient beds have no sidecar).
    float bpm{90.0f};
    int   beatsPerBar{4};
    bool  isMusicStem{false};   // true = bar-boundary crossfade; false = ambient (real-time)
    bool  isOpen{false};        // true when vf is valid and ready to decode
    bool  m_intentionallyStopped{false};  // set+alSourceStop must be in same m_streamMutex scope

    // Gain computed by the crossfade curve [0..1]; applied by the audio thread.
    float crossfadeGain{1.0f};

    // Computed samples-played estimate helper (same formula as spec).
    [[nodiscard]] static uint64_t computeSamplesPlayed(uint64_t samplesQueued, int buffersQueued) noexcept {
        uint64_t queued = static_cast<uint64_t>(buffersQueued) * kSamplesPerBuffer;
        return (samplesQueued > queued) ? samplesQueued - queued : 0u;
    }

    AudioStream() = default;
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
};

namespace AudioStreamUtils {

// Open an OGG file at path.  The caller provides an already-heap-allocated
// OggVorbis_File struct.  Returns true on success.
[[nodiscard]] bool openOGG(const std::string& path, OggVorbis_File* vf) noexcept;

// Close an OGG file.  Calls ov_clear() and does NOT free the vf pointer.
void closeOGG(OggVorbis_File* vf) noexcept;

// Seek to sample-frame 0 (loop restart).
[[nodiscard]] bool seekToStart(OggVorbis_File* vf) noexcept;

// Decode up to maxFrames PCM frames from vf into pcmBuf (interleaved int16).
// channels: 1 = mono, 2 = stereo.
// Returns number of frames decoded, 0 at EOF, negative on error.
[[nodiscard]] int decodeFrames(OggVorbis_File* vf, int16_t* pcmBuf, int maxFrames, int channels) noexcept;

// Query vorbis_info: fills sampleRate and channels.  Returns false if vf is
// not open or the info struct is null.
[[nodiscard]] bool getInfo(OggVorbis_File* vf, int& sampleRate, int& channels) noexcept;

// Return total PCM sample frames in the stream (-1 on error or unseekable).
[[nodiscard]] int64_t getTotalFrames(OggVorbis_File* vf) noexcept;

} // namespace AudioStreamUtils
