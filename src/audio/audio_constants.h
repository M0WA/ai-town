#pragma once
#include "src/interfaces/audio_types.h"

// Pool-size constants (kEvictableSFXCount, kStingerCount, kSFXPoolSize,
// kStreamSourceCount, kTotalSources, kTransientReserveStart, kMaxVehiclePairs,
// kZoneLoopMaxPreloadDurationSeconds, kVehicleEngineLoopMinDurationSeconds) are
// defined in src/interfaces/audio_types.h and available transitively via this header.
// Do NOT redeclare them here — that would produce redefinition errors under C++17.

// ---------------------------------------------------------------------------
// ALC_EXT_thread_local_context guard (for Phase 7 implementers):
// ---------------------------------------------------------------------------
// alcSetThreadContext must NEVER be called directly — it is NOT a standard
// OpenAL function on all platforms. At AudioSystem construction, check presence
// by calling `alcGetProcAddress(m_device, "alcSetThreadContext")` ONLY. If the
// return value is null, throw immediately. Do NOT call `alcIsExtensionPresent`
// for this extension — the dual-call pattern is explicitly prohibited.
// Store the loaded function pointer in m_fnSetThreadCtx
// (per architecture/audio-architecture/audio-system.md member declarations).
// The audio thread must call m_fnSetThreadCtx(m_context) at startup before
// any AL calls.

// ---------------------------------------------------------------------------
// Music crossfade constants
// ---------------------------------------------------------------------------
// BPM used for bar-boundary calculation (all V1 music stems authored at 90 BPM).
inline constexpr float kMusicBPM             = 90.0f;
inline constexpr int   kBeatsPerBar          = 4;

// Crossfade duration in seconds (constant-power curve, real-time delta).
// Minimum hold = 1 crossfade duration before a new crossfade may begin.
inline constexpr float kMusicCrossfadeDurationSeconds = 4.0f;

// Short crossfade used when transitioning from main menu music to the first
// gameplay stem.  The spec mandates 1 s (constant-power, real-time delta).
// Shorter than kMusicCrossfadeDurationSeconds to keep the UI transition snappy
// while still avoiding a jarring hard cut.
inline constexpr float kMenuToGameplayCrossfadeDurationSeconds = 1.0f;

// Music duck gain applied to music stems while a stinger is playing.
// Ambient beds are NOT affected — this applies to music stems only.
inline constexpr float kMusicDuckGain        = 0.4f;

// Minimum seconds between successive triggers of the same stinger type.
inline constexpr float kStingerCooldownSeconds = 5.0f;

// Maximum distance (metres) beyond which vehicle engine sources are culled.
inline constexpr float kVehicleEngineCullDistance = 150.0f;

// Maximum distance (metres) beyond which vehicle horn sources are culled.
inline constexpr float kVehicleHornCullDistance = 100.0f;

// Maximum simultaneous horn sources across all vehicles.
inline constexpr int kMaxSimultaneousHornSources = 3;

// Per-vehicle horn re-trigger cooldown in seconds.
inline constexpr float kVehicleHornCooldownSeconds = 2.0f;

// Maximum distance (metres) beyond which sfx_intersection_tick is culled.
inline constexpr float kIntersectionTickCullDistance = 80.0f;

// Forced-loan IClock gate duration: AudioSystem waits at least this many
// seconds (real-time, via IClock) before allowing a second forced loan event.
inline constexpr double kForcedLoanGateSeconds = 120.0;
