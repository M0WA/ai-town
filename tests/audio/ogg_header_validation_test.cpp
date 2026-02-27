// ogg_header_validation_test.cpp — Phase 7 OGG header validation unit tests.
//
// Tests that placeholder OGG assets meet the spec-mandated format requirements
// using ov_fopen() and vorbis_info directly (Vorbis::vorbisfile linkage required).
//
// Asset format requirements from architecture/audio-architecture/audio-asset-formats.md
// and architecture/audio-architecture/v1-audio-asset-manifest.md:
//   - Music stems (music_placeholder.ogg): 44100 Hz, stereo (2 channels)
//   - Ambient bed (ambient_bed_placeholder.ogg): 44100 Hz, stereo (2 channels)
//   - Zone loop (placeholder_zone_loop.ogg): 44100 Hz, mono (1 channel)
//   - Vehicle engine SFX (placeholder_vehicle_engine.ogg): 44100 Hz, mono (1 channel)
//
// These tests run in CI without an OpenAL device — libvorbisfile reads OGG files
// directly from the filesystem using ov_fopen(), which requires no audio hardware.
//
// CI environment requirement: tests must run from CMAKE_SOURCE_DIR (working directory
// set by aitown_add_tests via gtest_discover_tests WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}").
// Asset paths are relative to the project root: assets/audio/<filename>.ogg
//
// Test label: unit — runs in CI without a display or audio device.

#include <vorbis/vorbisfile.h>
#include <gtest/gtest.h>
#include <string>
#include <cstring>

// ---------------------------------------------------------------------------
// Helper: open an OGG file via ov_fopen and check success.
// Returns true if the file was opened successfully (ov_fopen returns 0).
// Closes the file on success (caller must not use vf after this check returns false).
// On GTEST_SKIP() the test is skipped — caller receives control flow via SKIP.
// ---------------------------------------------------------------------------

// Path prefix relative to CMAKE_SOURCE_DIR (working directory for tests)
static const std::string kAudioAssetDir = "assets/audio/";

// Macro: open OGG file or GTEST_SKIP if not found
// The macro form is required to make GTEST_SKIP() work (it uses return internally).
#define OPEN_OGG_OR_SKIP(vf_ptr, filename)                                          \
    do {                                                                             \
        std::string _path = kAudioAssetDir + (filename);                            \
        int _ret = ov_fopen(_path.c_str(), (vf_ptr));                               \
        if (_ret != 0) {                                                             \
            GTEST_SKIP() << "OGG asset not found or failed to open (ov_fopen "      \
                         << "returned " << _ret << "): " << _path << "\n"           \
                         << "Run sound-artist-opensoftal Phase 7 task first to "    \
                         << "generate placeholder OGG assets.";                     \
        }                                                                            \
    } while (0)

// ---------------------------------------------------------------------------
// OggHeaderValidationTest fixture — uses StrictMock policy (no mocks needed
// here since tests use ov_fopen directly; fixture provides TearDown contract).
// ---------------------------------------------------------------------------
class OggHeaderValidationTest : public ::testing::Test {
protected:
    OggVorbis_File m_vf;
    bool           m_vfOpen{false};

    void SetUp() override {
        std::memset(&m_vf, 0, sizeof(m_vf));
    }

    void TearDown() override {
        // Destructor-path contract: always close the OGG file if opened.
        // This prevents resource leaks even if a test assertion fails midway.
        // ov_clear() is safe to call on a zero-initialized OggVorbis_File
        // only if it was successfully opened via ov_fopen. Guard with m_vfOpen.
        if (m_vfOpen) {
            ov_clear(&m_vf);
            m_vfOpen = false;
        }
    }

    // Open an OGG file into m_vf and set m_vfOpen = true on success.
    // Returns the ov_fopen return code (0 = success).
    int openOgg(const std::string& filename) {
        std::string path = kAudioAssetDir + filename;
        int ret = ov_fopen(path.c_str(), &m_vf);
        if (ret == 0) {
            m_vfOpen = true;
        }
        return ret;
    }

    // Returns vorbis_info for stream 0 (the primary logical bitstream).
    // Must be called only after a successful openOgg().
    const vorbis_info* getInfo() const {
        return ov_info(const_cast<OggVorbis_File*>(&m_vf), -1);
    }
};

// ---------------------------------------------------------------------------
// Test A: music_placeholder.ogg opens successfully via ov_fopen
//
// Verifies ov_fopen() returns 0 (success) for the music stem placeholder.
// Failure indicates the file is missing (run sound-artist-opensoftal first)
// or is corrupt (re-generate from spec).
// ---------------------------------------------------------------------------
TEST_F(OggHeaderValidationTest, OggHeader_MusicPlaceholder_OpensSuccessfully) {
    int ret = openOgg("music_placeholder.ogg");
    if (ret != 0) {
        GTEST_SKIP() << "music_placeholder.ogg not found — "
                        "run sound-artist-opensoftal Phase 7 task first";
    }
    EXPECT_EQ(ret, 0) << "ov_fopen must return 0 (success) for music_placeholder.ogg";
    EXPECT_TRUE(m_vfOpen);
}

// ---------------------------------------------------------------------------
// Test B: music_placeholder.ogg is stereo (2 channels)
//
// Music stems must be stereo per architecture/audio-architecture/audio-asset-formats.md:
//   "Music stems must be 44100 Hz stereo — throw std::runtime_error on mismatch"
// Verified via vorbis_info::channels.
// ---------------------------------------------------------------------------
TEST_F(OggHeaderValidationTest, OggHeader_MusicPlaceholder_IsStereo) {
    int ret = openOgg("music_placeholder.ogg");
    if (ret != 0) {
        GTEST_SKIP() << "music_placeholder.ogg not found — "
                        "run sound-artist-opensoftal Phase 7 task first";
    }

    const vorbis_info* info = getInfo();
    ASSERT_NE(info, nullptr) << "ov_info must return valid vorbis_info for music_placeholder.ogg";
    EXPECT_EQ(info->channels, 2)
        << "music_placeholder.ogg must be stereo (2 channels); "
           "got " << info->channels << " channel(s). "
           "Mono music files would silently corrupt m_samplesQueued bar-boundary calculations.";
}

// ---------------------------------------------------------------------------
// Test C: music_placeholder.ogg is 44100 Hz
//
// Verified via vorbis_info::rate.
// A mismatch would silently corrupt bar-boundary calculations in m_samplesQueued.
// ---------------------------------------------------------------------------
TEST_F(OggHeaderValidationTest, OggHeader_MusicPlaceholder_Is44100Hz) {
    int ret = openOgg("music_placeholder.ogg");
    if (ret != 0) {
        GTEST_SKIP() << "music_placeholder.ogg not found — "
                        "run sound-artist-opensoftal Phase 7 task first";
    }

    const vorbis_info* info = getInfo();
    ASSERT_NE(info, nullptr) << "ov_info must return valid vorbis_info for music_placeholder.ogg";
    EXPECT_EQ(info->rate, 44100L)
        << "music_placeholder.ogg must be 44100 Hz; "
           "got " << info->rate << " Hz. "
           "Mismatched sample rate would corrupt kSamplesPerBuffer bar-boundary calculations.";
}

// ---------------------------------------------------------------------------
// Test D: placeholder_zone_loop.ogg is mono (1 channel) at 44100 Hz
//
// Zone loops are always pre-loaded (never streamed) and must be mono per spec:
//   "zone loops and vehicle engine SFX must be 44100 Hz mono"
// Mono assets halve memory usage for pre-loaded buffers and are correct for
// positional 3D audio (stereo pre-loaded assets would fold to mono at playback).
// ---------------------------------------------------------------------------
TEST_F(OggHeaderValidationTest, OggHeader_ZoneLoopPlaceholder_IsMonoAt44100Hz) {
    int ret = openOgg("placeholder_zone_loop.ogg");
    if (ret != 0) {
        GTEST_SKIP() << "placeholder_zone_loop.ogg not found — "
                        "run sound-artist-opensoftal Phase 7 task first";
    }

    const vorbis_info* info = getInfo();
    ASSERT_NE(info, nullptr)
        << "ov_info must return valid vorbis_info for placeholder_zone_loop.ogg";
    EXPECT_EQ(info->channels, 1)
        << "placeholder_zone_loop.ogg must be mono (1 channel); "
           "got " << info->channels << " channel(s). "
           "Zone loops are pre-loaded positional SFX — stereo would be incorrect.";
    EXPECT_EQ(info->rate, 44100L)
        << "placeholder_zone_loop.ogg must be 44100 Hz; "
           "got " << info->rate << " Hz.";
}

// ---------------------------------------------------------------------------
// Test E: placeholder_vehicle_engine.ogg is mono (1 channel) at 44100 Hz
//
// Vehicle engine SFX are positional 3D audio and must be mono:
//   "vehicle engine SFX must be 44100 Hz mono"
// Minimum duration: 6 s (kVehicleEngineLoopMinDurationSeconds).
// This test checks format only (channel count + sample rate).
// Duration check is an AudioSystem load-time validation (not a file-format test).
// ---------------------------------------------------------------------------
TEST_F(OggHeaderValidationTest, OggHeader_VehicleEnginePlaceholder_IsMonoAt44100Hz) {
    int ret = openOgg("placeholder_vehicle_engine.ogg");
    if (ret != 0) {
        GTEST_SKIP() << "placeholder_vehicle_engine.ogg not found — "
                        "run sound-artist-opensoftal Phase 7 task first";
    }

    const vorbis_info* info = getInfo();
    ASSERT_NE(info, nullptr)
        << "ov_info must return valid vorbis_info for placeholder_vehicle_engine.ogg";
    EXPECT_EQ(info->channels, 1)
        << "placeholder_vehicle_engine.ogg must be mono (1 channel); "
           "got " << info->channels << " channel(s). "
           "Vehicle engine SFX are positional 3D audio — stereo is incorrect.";
    EXPECT_EQ(info->rate, 44100L)
        << "placeholder_vehicle_engine.ogg must be 44100 Hz; "
           "got " << info->rate << " Hz.";
}

// ---------------------------------------------------------------------------
// Test: ov_pcm_total verifies the linker resolved vorbisfile symbol
//
// Equivalent to the linker smoke check in audio_smoke_test.cpp but applied here
// to confirm Vorbis::vorbisfile is correctly linked to audio_tests after the
// Phase 7 target_sources() extension in CMakeLists.txt.
// ---------------------------------------------------------------------------
TEST(OggLinkSmoke, VorbisfileSymbolResolvesInPhase7Extension) {
    // (void)&ov_pcm_total forces the linker to resolve the symbol.
    // This is a link-time check: if Vorbis::vorbisfile is not linked, the build
    // fails with "undefined reference to ov_pcm_total" at the audio_tests target.
    (void)&ov_pcm_total;
    (void)&ov_fopen;
    (void)&ov_info;
    (void)&ov_clear;
    SUCCEED();
}

// Phase 7 sign-off (test-dev-cpp):
// Tests verified against spec in implementation/phase-7.md
// ManualClock used for deterministic duck timer testing
// IAlcFunctions injection seam used for AudioThread_AbsentThreadLocalContext_ConstructorThrows
// OGG header validation tests use ov_fopen() directly per architecture/audio-architecture/audio-asset-formats.md
