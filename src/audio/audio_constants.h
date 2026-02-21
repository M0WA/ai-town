#pragma once

// AL source pool-size constants referenced by AudioSystem.h member declarations
// and EFX allocation loops in Phase 4.
//
// IMPORTANT — ALC_EXT_thread_local_context guard note (for Phase 4 implementers):
// alcSetThreadContext must NEVER be called directly — it is NOT a standard OpenAL
// function on all platforms. At AudioSystem construction, check presence via
// alcIsExtensionPresent(device, "ALC_EXT_thread_local_context") and load the function
// pointer via alcGetProcAddress(device, "alcSetThreadContext") before calling it.
// A direct call without this guard is a null function pointer dereference on any
// OpenAL implementation that does not expose this extension.
// Store the guard result in m_useThreadLocalCtx and the pointer in m_fnSetThreadCtx
// (per architecture/audio-architecture/audio-system.md member declarations).

// sources[0..54] — evictable SFX pool (55 slots)
inline constexpr int kEvictableSFXCount = 55;

// V1 stingers: CRISIS(55) + MILESTONE(56)
inline constexpr int kStingerCount = 2;

// evictable(55) + stingers(2) + reserved_post_V1(1)
// sources[57] reserved for post-V1 GAME_OVER stinger.
// IMPORTANT: kSFXPoolSize = 58, NOT 57.
// Do NOT derive kSFXPoolSize = kEvictableSFXCount + kStingerCount = 55+2 = 57
// — that formula omits the reserved slot.
inline constexpr int kSFXPoolSize = 58;

// 2 music streams + 2 ambient bed streams
inline constexpr int kStreamSourceCount = 4;

// Total AL sources: kSFXPoolSize(58) + kStreamSourceCount(4) = 62
// Defined as kSFXPoolSize + kStreamSourceCount (NOT a subtraction).
inline constexpr int kTotalSources = kSFXPoolSize + kStreamSourceCount;

// sources[51..54] reserved for HIGH/CRITICAL priority transient SFX
inline constexpr int kTransientReserveStart = 51;
