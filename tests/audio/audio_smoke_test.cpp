// audio_smoke_test.cpp — Phase 0 compile-check stub for audio_tests target.
// Three purposes:
//   (1) SUCCEED() so the test passes.
//   (2) (void)&ov_pcm_total forces linker to resolve a real libvorbisfile symbol,
//       catching misconfigured Vorbis::vorbisfile CMake targets.
//   (3) static_assert checks for all six audio_constants.h values — a bare #include
//       only verifies header existence, NOT that the constant values are correct.
//       A typo kSFXPoolSize=57 (instead of 58) compiles silently without these.
#include "src/interfaces/IAudioSystem.h"
#include "src/audio/audio_constants.h"
#include <vorbis/vorbisfile.h>
#include <gtest/gtest.h>

// Validate pool-size constant values at compile time.
static_assert(kEvictableSFXCount     == 55, "kEvictableSFXCount must be 55 (sources[0..54])");
static_assert(kStingerCount          ==  2, "kStingerCount must be 2 (V1: CRISIS + MILESTONE)");
static_assert(kSFXPoolSize           == 58, "kSFXPoolSize must be 58 (55+2+1 reserved post-V1)");
static_assert(kStreamSourceCount     ==  4, "kStreamSourceCount must be 4 (2 music + 2 ambient)");
static_assert(kTotalSources          == 62, "kTotalSources must be 62 (kSFXPoolSize + kStreamSourceCount)");
static_assert(kTransientReserveStart == 51, "kTransientReserveStart must be 51 (sources[51..54])");

// Cross-check StingerType enum values against pool-boundary constants.
// These enforce the structural stinger slot reservation in source-pool.md:
// CRISIS and MILESTONE must occupy the first two slots above the evictable pool.
static_assert(static_cast<int>(StingerType::CRISIS)    == kEvictableSFXCount,
              "StingerType::CRISIS must equal kEvictableSFXCount (55) — first stinger slot");
static_assert(static_cast<int>(StingerType::MILESTONE) == kEvictableSFXCount + 1,
              "StingerType::MILESTONE must equal kEvictableSFXCount + 1 (56)");
static_assert(static_cast<int>(StingerType::MILESTONE) <  kSFXPoolSize,
              "StingerType::MILESTONE must be within kSFXPoolSize bounds — OOB if pool shrinks");

TEST(AudioSmoke, VorbisfileIAudioSystemAndConstantsCompile) {
    (void)&ov_pcm_total;
    SUCCEED();
}
