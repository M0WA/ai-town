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

// ---------------------------------------------------------------------------
// Source pool layout constants
// ---------------------------------------------------------------------------
// sources[0..54]  — evictable SFX pool (55 slots)
// sources[55..56] — stinger slots (CRISIS=55, MILESTONE=56)
// sources[57]     — reserved post-V1 GAME_OVER stinger slot
// sources[58..61] — streaming (2 music + 2 ambient bed)
// Total: 62 sources
constexpr int kEvictableSFXCount           = 55;
constexpr int kStingerCount                = 2;
constexpr int kSFXPoolSize                 = 58;   // kEvictableSFXCount + kStingerCount + 1
constexpr int kStreamSourceCount           = 4;
constexpr int kTotalSources                = 62;   // kSFXPoolSize + kStreamSourceCount
constexpr int kTransientReserveStart       = 51;
constexpr int kMaxVehiclePairs             = 12;
constexpr float kZoneLoopMaxPreloadDurationSeconds   = 18.0f;
constexpr float kVehicleEngineLoopMinDurationSeconds = 6.0f;

// WARNING: Any array indexed by static_cast<int>(StingerType) must be sized
// kSFXPoolSize (= 58), NOT kEvictableSFXCount (= 55). The stinger sources live
// at indices 55 and 56 — beyond the evictable pool.
enum class StingerType {
    CRISIS    = kEvictableSFXCount,      // = 55
    MILESTONE = kEvictableSFXCount + 1,  // = 56
};

static_assert(kEvictableSFXCount + kStingerCount + 1 + kStreamSourceCount == kTotalSources,
    "Source pool total mismatch");
static_assert(static_cast<int>(StingerType::CRISIS)    == kEvictableSFXCount,
    "StingerType::CRISIS pool index mismatch");
static_assert(static_cast<int>(StingerType::MILESTONE) == kEvictableSFXCount + 1,
    "StingerType::MILESTONE pool index mismatch");
static_assert(kTransientReserveStart < kEvictableSFXCount,
    "Transient reserve must be within evictable pool");
static_assert(kMaxVehiclePairs * 2 <= kEvictableSFXCount,
    "Vehicle pair pool exceeds evictable SFX pool");
static_assert(kMaxVehiclePairs == 12,
    "kMaxVehiclePairs changed — update audio asset manifest");

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

// Music intensity tier driven by live simulation state.
// Set by CitySimulation::update() via IAudioSystem::setMusicIntensity().
// AudioSystem maps each tier to the corresponding gameplay stem pair:
//   CALM   -> calm_01/02
//   GROWTH -> growth_01/02
//   CRISIS -> crisis_01/02
// Priority order (highest first): CRISIS > GROWTH > CALM.
// Threshold conditions are authoritative in
// architecture/game-design/economy-model.md §Music Intensity Tiers:
//   CALM:   budget_surplus_pct >= 0%  (default state)
//   GROWTH: net population change positive this tick, no deficit streak
//   CRISIS: consecutive_deficit_months >= 2  (highest priority)
// Time-of-day forced-Calm override (DUSK/NIGHT/DAWN) is applied
// internally by AudioSystem; CitySimulation does NOT suppress GROWTH/CRISIS
// calls during off-hours.
// Added in Phase 10 (see implementation/phase-10.md).
enum class MusicIntensity {
    CALM,
    GROWTH,
    CRISIS
};

