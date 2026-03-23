#pragma once
#include <cstdint>
// sound_ids.h — locked SoundId and MusicTrackId constants.
// Per v1-audio-asset-manifest.md: do NOT reassign. Values are frozen.

// SoundId 0 is reserved as invalid/null.
static constexpr uint32_t SFX_INVALID             = 0;
static constexpr uint32_t SFX_BUILD_PLACE         = 1;
static constexpr uint32_t SFX_BUILD_DEMOLISH      = 2;
static constexpr uint32_t SFX_ROAD_BUILD          = 3;
static constexpr uint32_t SFX_EARTHWORKS          = 4;
static constexpr uint32_t SFX_ZONE_UPGRADE        = 5;
static constexpr uint32_t SFX_SERVICE_DEGRADE     = 6;
static constexpr uint32_t SFX_BUDGET_WARN         = 7;
static constexpr uint32_t SFX_LOAN_ISSUED         = 8;
static constexpr uint32_t SFX_POWER_OUT           = 9;
static constexpr uint32_t SFX_WATER_OUT           = 10;
static constexpr uint32_t SFX_FIRE_ALERT          = 11;
static constexpr uint32_t SFX_POLICE_ALERT        = 12;
static constexpr uint32_t SFX_VEHICLE_ENGINE_IDLE = 13;
static constexpr uint32_t SFX_VEHICLE_ENGINE_MOVE = 14;
static constexpr uint32_t SFX_VEHICLE_HORN        = 15;
static constexpr uint32_t SFX_INTERSECTION_TICK   = 16;
static constexpr uint32_t SFX_ZONE_RESIDENTIAL    = 17;
static constexpr uint32_t SFX_ZONE_COMMERCIAL     = 18;
static constexpr uint32_t SFX_ZONE_INDUSTRIAL     = 19;
static constexpr uint32_t SFX_STINGER_CRISIS      = 20;
static constexpr uint32_t SFX_STINGER_MILESTONE   = 21;
static constexpr uint32_t SFX_UI_CLICK            = 22;
static constexpr uint32_t SFX_UI_TOAST            = 23;
static constexpr uint32_t SFX_UI_MENU_OPEN        = 24;
static constexpr uint32_t SFX_UI_MENU_CLOSE       = 25;

// MusicTrackId 0 is reserved as invalid/null.
static constexpr uint32_t MUSIC_INVALID           = 0;
static constexpr uint32_t MUSIC_MAIN_MENU_01      = 1;
static constexpr uint32_t MUSIC_MAIN_MENU_02      = 2;
static constexpr uint32_t MUSIC_CALM_01           = 3;
static constexpr uint32_t MUSIC_CALM_02           = 4;
static constexpr uint32_t MUSIC_GROWTH_01         = 5;
static constexpr uint32_t MUSIC_GROWTH_02         = 6;
static constexpr uint32_t MUSIC_CRISIS_01         = 7;
static constexpr uint32_t MUSIC_CRISIS_02         = 8;
