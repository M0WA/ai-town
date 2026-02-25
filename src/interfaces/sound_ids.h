#pragma once
#include "audio_types.h"

// ---------------------------------------------------------------------------
// SoundId Assignment Table — LOCKED. Do not reassign values.
// Source of truth: architecture/audio-architecture/v1-audio-asset-manifest.md
//
// SoundId 0 is reserved as the invalid/null identifier.
// All V1 SFX, UI, vehicle, zone loop, and stinger assets start at 1.
//
// NOTE: Zone loop constants use the SFX_ prefix (not ZONE_ alone) to distinguish
// them from ZoneType enum values in simulation_types.h. Zone loops are positional
// pre-loaded sounds, not streaming audio.
//
// NOTE: Stinger SoundIds (20–21) identify pre-loaded WAV buffers and are separate
// from stinger source pool indices (sources[55] and sources[56] as defined by
// StingerType enum in audio_types.h). Do NOT conflate these two identifier spaces.
// ---------------------------------------------------------------------------

// SoundId 0 — reserved (invalid/null)
constexpr SoundId SFX_INVALID                  = 0;

// --- Construction / zone placement SFX ---
constexpr SoundId SFX_BUILD_PLACE              = 1;   // sfx_build_place.wav
constexpr SoundId SFX_BUILD_DEMOLISH           = 2;   // sfx_build_demolish.wav
constexpr SoundId SFX_ROAD_BUILD               = 3;   // sfx_road_build.wav
constexpr SoundId SFX_EARTHWORKS               = 4;   // sfx_earthworks.wav
constexpr SoundId SFX_ZONE_UPGRADE             = 5;   // sfx_zone_upgrade.wav
constexpr SoundId SFX_SERVICE_DEGRADE          = 6;   // sfx_service_degrade.wav

// --- Budget / economy SFX ---
constexpr SoundId SFX_BUDGET_WARN              = 7;   // sfx_budget_warn.wav
constexpr SoundId SFX_LOAN_ISSUED              = 8;   // sfx_loan_issued.wav

// --- Utility / service alert SFX ---
constexpr SoundId SFX_POWER_OUT                = 9;   // sfx_power_out.wav
constexpr SoundId SFX_WATER_OUT                = 10;  // sfx_water_out.wav
constexpr SoundId SFX_FIRE_ALERT               = 11;  // sfx_fire_alert.wav   (CRITICAL priority, mono positional)
constexpr SoundId SFX_POLICE_ALERT             = 12;  // sfx_police_alert.wav (CRITICAL priority, mono positional)

// --- Vehicle SFX ---
constexpr SoundId SFX_VEHICLE_ENGINE_IDLE      = 13;  // sfx_vehicle_engine_idle.ogg (OGG, min 6 s, mono positional)
constexpr SoundId SFX_VEHICLE_ENGINE_MOVE      = 14;  // sfx_vehicle_engine_move.ogg (OGG, min 6 s, mono positional)
constexpr SoundId SFX_VEHICLE_HORN             = 15;  // sfx_vehicle_horn.wav        (HIGH priority, mono positional)

// --- Ambient / traffic SFX ---
constexpr SoundId SFX_INTERSECTION_TICK        = 16;  // sfx_intersection_tick.wav

// --- Zone loop SFX (pre-loaded OGG, mono positional, max 18 s) ---
constexpr SoundId SFX_ZONE_RESIDENTIAL         = 17;  // sfx_zone_residential.ogg
constexpr SoundId SFX_ZONE_COMMERCIAL          = 18;  // sfx_zone_commercial.ogg
constexpr SoundId SFX_ZONE_INDUSTRIAL          = 19;  // sfx_zone_industrial.ogg

// --- Music stingers (pre-loaded WAV PCM, mono, non-evictable reserved slots) ---
constexpr SoundId SFX_STINGER_CRISIS           = 20;  // stinger_crisis.wav    (pool source index 55)
constexpr SoundId SFX_STINGER_MILESTONE        = 21;  // stinger_milestone.wav (pool source index 56)

// --- UI sounds ---
constexpr SoundId UI_CLICK                     = 22;  // ui_click.wav
constexpr SoundId UI_TOAST                     = 23;  // ui_toast.wav
constexpr SoundId UI_MENU_OPEN                 = 24;  // ui_menu_open.wav
constexpr SoundId UI_MENU_CLOSE                = 25;  // ui_menu_close.wav

// Post-V1 additions (e.g., stinger_game_over, sfx_population_notification)
// MUST be assigned the next available integer (26, 27, ...) and appended here.
// Never reuse a retired ID.

// ---------------------------------------------------------------------------
// MusicTrackId Assignment Table — LOCKED. Do not reassign values.
// Source of truth: architecture/audio-architecture/v1-audio-asset-manifest.md
//
// MusicTrackId 0 is reserved as the invalid/null identifier.
// Only assets managed exclusively by the music crossfade state machine use
// MusicTrackId. Ambient bed streams are selected internally by AudioSystem
// using the TimeOfDay enum — they do NOT have MusicTrackId constants here.
// ---------------------------------------------------------------------------

// MusicTrackId 0 — reserved (invalid/null)
constexpr MusicTrackId MUSIC_INVALID           = 0;

// --- Main menu music (streamed, stereo, bar-aligned seamless loop) ---
constexpr MusicTrackId MUSIC_MAIN_MENU_01      = 1;   // music_main_menu_01.ogg
constexpr MusicTrackId MUSIC_MAIN_MENU_02      = 2;   // music_main_menu_02.ogg

// --- Gameplay music stems (streamed, stereo, bar-aligned seamless loop, 90 BPM) ---
constexpr MusicTrackId MUSIC_CALM_01           = 3;   // music_calm_01.ogg
constexpr MusicTrackId MUSIC_CALM_02           = 4;   // music_calm_02.ogg
constexpr MusicTrackId MUSIC_GROWTH_01         = 5;   // music_growth_01.ogg
constexpr MusicTrackId MUSIC_GROWTH_02         = 6;   // music_growth_02.ogg
constexpr MusicTrackId MUSIC_CRISIS_01         = 7;   // music_crisis_01.ogg
constexpr MusicTrackId MUSIC_CRISIS_02         = 8;   // music_crisis_02.ogg

// Post-V1 music stems MUST be assigned the next available integer (9, 10, ...)
// and appended here. Never reuse a retired ID.
