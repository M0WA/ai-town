#pragma once
#include <cstdint>
#include "vec3.h"
#include "camera_state.h"

// Canonical definitions for all game-domain audio types used by IAudioSystem.
// Do NOT define SimSpeed or SpeedMultiplier here — those belong exclusively
// in simulation_types.h. IAudioSystem.h obtains SimSpeed by including simulation_types.h.

using SoundId      = uint32_t;
using SoundHandle  = uint32_t;
using MusicTrackId = uint32_t;

enum class SoundPriority {
    LOW      = 0,
    NORMAL   = 1,
    HIGH     = 2,
    CRITICAL = 3
};

// StingerType enum values encode the fixed AL source indices.
// CRISIS    = 55 — sources[55] (first stinger slot above the evictable pool)
// MILESTONE = 56 — sources[56] (second stinger slot)
// Using implicit values (CRISIS=0, MILESTONE=1) would index into the general
// evictable SFX pool (sources[0..54]) rather than the reserved stinger slots[55..56].
// Array sizing note: Any array indexed by static_cast<int>(StingerType) must be sized
// kSFXPoolSize (= 58), NOT kEvictableSFXCount (= 55). CRISIS=55 and MILESTONE=56 are
// valid indices into a 58-element SFX pool array but OOB in a 55-element array.
enum class StingerType {
    CRISIS    = 55,
    MILESTONE = 56
};

// Cycle order: DAY -> DUSK -> NIGHT -> DAWN -> DAY
// The cycle-transition formula (static_cast<int>(tod)+1)%4 belongs in
// AudioSystem.cpp's setTimeOfDay() implementation, NOT here. Locking a
// +1 mod 4 formula into the type definition creates a maintenance hazard
// if enumerators are reordered.
enum class TimeOfDay {
    DAY,
    DUSK,
    NIGHT,
    DAWN
};
