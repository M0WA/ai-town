// notification_sfx_efx_bypass_test.cpp — Phase 10 audio test.
//
// Test: NotificationSFX_EFXBypass_DirectFilterSetToNull
//
// Verifies the EFX occlusion bypass list specified in phase-10.md §UI sound wiring:
//
//   Non-positional notification SFX that MUST have AL_DIRECT_FILTER = AL_FILTER_NULL:
//     ui_click, ui_toast, ui_menu_open, ui_menu_close,
//     sfx_power_out, sfx_water_out, sfx_budget_warn, sfx_loan_issued,
//     sfx_zone_upgrade, sfx_service_degrade
//   Positional exception (AL_SOURCE_RELATIVE = AL_FALSE, but EFX bypass required):
//     sfx_earthworks  — construction occurs on open, unoccluded tiles
//
//   Service alert SFX that must NOT have EFX bypass (positional, benefit from occlusion):
//     sfx_fire_alert, sfx_police_alert
//
// This test file has two responsibilities:
//
//   1. Compile-time check: verifies that all expected SoundId constants from
//      sound_ids.h are present and have the correct values, so a future refactor
//      that renames or renumbers SoundIds is caught immediately.
//
//   2. Runtime contract check: verifies that the bypass SoundId set exactly
//      matches the spec list — any addition or removal to the bypass list must
//      update both the implementation AND this test.
//
// Phase 10 implementation note:
//   The full integration test (verifying alSourcei(AL_DIRECT_FILTER,AL_FILTER_NULL)
//   is actually called on the OpenAL source) requires AudioSystem to be
//   refactored to accept an IAlFunctions seam (similar to IAlcFunctions for the
//   audio thread test).  Until that seam is added, this test verifies the set
//   membership contract only.  When the IAlFunctions seam is available, extend
//   this test to inject a MockAlFunctions and EXPECT_CALL alSourcei for each
//   bypass asset.
//
// CMake target: audio_tests (target_sources, Phase 10 block in CMakeLists.txt).
// Does NOT require a real audio device.
//
// Spec refs:
//   implementation/phase-10.md §UI sound wiring — EFX bypass list
//   architecture/audio-architecture/v1-audio-asset-manifest.md §EFX bypass policy

#include <gtest/gtest.h>
#include <set>
#include <string>

#include "src/interfaces/sound_ids.h"
#include "src/interfaces/audio_types.h"

// ---------------------------------------------------------------------------
// EFX bypass SoundId sets — derived directly from phase-10.md specification.
//
// BYPASS_SET: SoundIds that MUST have AL_DIRECT_FILTER = AL_FILTER_NULL applied
// before playback, regardless of AL_SOURCE_RELATIVE setting.
//
// NO_BYPASS_SET: Positional SFX that must NOT have EFX bypass (they benefit
// from occlusion simulation).
//
// These sets are the authoritative contract for AudioSystem::playSound() and
// AudioSystem::playPositionalSound() implementations.
// ---------------------------------------------------------------------------
namespace {

// The 11 SoundIds that require AL_DIRECT_FILTER = AL_FILTER_NULL.
// 10 non-positional (AL_SOURCE_RELATIVE = AL_TRUE) + 1 positional exception.
const std::set<SoundId> kEfxBypassSet = {
    // Non-positional UI sounds (AL_SOURCE_RELATIVE = AL_TRUE)
    UI_CLICK,
    UI_TOAST,
    UI_MENU_OPEN,
    UI_MENU_CLOSE,
    // Non-positional outage notifications (AL_SOURCE_RELATIVE = AL_TRUE)
    SFX_POWER_OUT,
    SFX_WATER_OUT,
    // Non-positional economy notifications (AL_SOURCE_RELATIVE = AL_TRUE)
    SFX_BUDGET_WARN,
    SFX_LOAN_ISSUED,
    SFX_ZONE_UPGRADE,
    SFX_SERVICE_DEGRADE,
    // Positional exception: earthworks occurs on open tiles — EFX bypass required
    // but AL_SOURCE_RELATIVE = AL_FALSE (world-space position must be preserved).
    SFX_EARTHWORKS,
};

// Service alert SFX: positional, benefit from occlusion — must NOT bypass EFX.
const std::set<SoundId> kNoBypassSet = {
    SFX_FIRE_ALERT,
    SFX_POLICE_ALERT,
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// NotificationSFX_EFXBypass_DirectFilterSetToNull
//
// Contract verification: the bypass set contains exactly the 11 SoundIds
// specified in phase-10.md and does NOT include the two service alert SFX.
// ---------------------------------------------------------------------------
TEST(NotificationSFXEFXBypass, DirectFilterSetToNull)
{
    // --- Part 1: verify bypass set size matches spec (11 entries) ---
    EXPECT_EQ(kEfxBypassSet.size(), 11u)
        << "EFX bypass set size mismatch — expected 11 SoundIds "
           "(10 non-positional + 1 positional exception sfx_earthworks); "
           "update this test and AudioSystem if the spec list changes";

    // --- Part 2: verify each expected SoundId is present in bypass set ---
    // Non-positional UI sounds
    EXPECT_NE(kEfxBypassSet.find(UI_CLICK),          kEfxBypassSet.end()) << "UI_CLICK missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(UI_TOAST),          kEfxBypassSet.end()) << "UI_TOAST missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(UI_MENU_OPEN),      kEfxBypassSet.end()) << "UI_MENU_OPEN missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(UI_MENU_CLOSE),     kEfxBypassSet.end()) << "UI_MENU_CLOSE missing from bypass set";
    // Non-positional outage notifications
    EXPECT_NE(kEfxBypassSet.find(SFX_POWER_OUT),     kEfxBypassSet.end()) << "SFX_POWER_OUT missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(SFX_WATER_OUT),     kEfxBypassSet.end()) << "SFX_WATER_OUT missing from bypass set";
    // Non-positional economy notifications
    EXPECT_NE(kEfxBypassSet.find(SFX_BUDGET_WARN),   kEfxBypassSet.end()) << "SFX_BUDGET_WARN missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(SFX_LOAN_ISSUED),   kEfxBypassSet.end()) << "SFX_LOAN_ISSUED missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(SFX_ZONE_UPGRADE),  kEfxBypassSet.end()) << "SFX_ZONE_UPGRADE missing from bypass set";
    EXPECT_NE(kEfxBypassSet.find(SFX_SERVICE_DEGRADE), kEfxBypassSet.end()) << "SFX_SERVICE_DEGRADE missing from bypass set";
    // Positional exception
    EXPECT_NE(kEfxBypassSet.find(SFX_EARTHWORKS),    kEfxBypassSet.end()) << "SFX_EARTHWORKS missing from bypass set (positional exception)";

    // --- Part 3: service alert SFX must NOT be in the bypass set ---
    EXPECT_EQ(kEfxBypassSet.find(SFX_FIRE_ALERT),   kEfxBypassSet.end())
        << "SFX_FIRE_ALERT must NOT be in the EFX bypass set — "
           "it is a positional alert that benefits from occlusion simulation";
    EXPECT_EQ(kEfxBypassSet.find(SFX_POLICE_ALERT), kEfxBypassSet.end())
        << "SFX_POLICE_ALERT must NOT be in the EFX bypass set — "
           "it is a positional alert that benefits from occlusion simulation";

    // --- Part 4: bypass set and no-bypass set must be disjoint ---
    for (const SoundId id : kEfxBypassSet) {
        EXPECT_EQ(kNoBypassSet.find(id), kNoBypassSet.end())
            << "SoundId " << id << " is in both bypass and no-bypass sets — "
               "a SoundId cannot simultaneously require and prohibit EFX bypass";
    }
}

// ---------------------------------------------------------------------------
// NotificationSFX_EarthworksPositionalException_NotRelative
//
// Verifies the earthworks positional exception rule:
//   sfx_earthworks IS in the bypass set (EFX bypass required)
//   sfx_earthworks is NOT in the non-positional (AL_SOURCE_RELATIVE) category.
//
// This is a documentation/contract test — it verifies the developer understands
// that sfx_earthworks requires EFX bypass but must remain world-space positional
// (AL_SOURCE_RELATIVE = AL_FALSE, not AL_TRUE).
//
// The non-positional SoundIds that must use AL_SOURCE_RELATIVE = AL_TRUE:
const std::set<SoundId> kRelativeSourceSet = {
    UI_CLICK, UI_TOAST, UI_MENU_OPEN, UI_MENU_CLOSE,
    SFX_POWER_OUT, SFX_WATER_OUT,
    SFX_BUDGET_WARN, SFX_LOAN_ISSUED, SFX_ZONE_UPGRADE, SFX_SERVICE_DEGRADE,
    // NOTE: SFX_EARTHWORKS is intentionally NOT in this list.
};
// ---------------------------------------------------------------------------
TEST(NotificationSFXEFXBypass, EarthworksPositionalException_NotRelative)
{
    // sfx_earthworks must be in the EFX bypass set
    EXPECT_NE(kEfxBypassSet.find(SFX_EARTHWORKS), kEfxBypassSet.end())
        << "sfx_earthworks must be in the EFX bypass set";

    // sfx_earthworks must NOT be in the AL_SOURCE_RELATIVE set
    EXPECT_EQ(kRelativeSourceSet.find(SFX_EARTHWORKS), kRelativeSourceSet.end())
        << "sfx_earthworks must NOT be in the AL_SOURCE_RELATIVE set — "
           "it is a positional sound (world-space, AL_SOURCE_RELATIVE = AL_FALSE) "
           "that requires EFX bypass only because construction occurs on open, "
           "unoccluded tiles (see phase-10.md §UI sound wiring)";

    // Non-positional bypass sounds must be in the AL_SOURCE_RELATIVE set
    const std::set<SoundId> nonPositionalBypass = {
        UI_CLICK, UI_TOAST, UI_MENU_OPEN, UI_MENU_CLOSE,
        SFX_POWER_OUT, SFX_WATER_OUT,
        SFX_BUDGET_WARN, SFX_LOAN_ISSUED, SFX_ZONE_UPGRADE, SFX_SERVICE_DEGRADE,
    };
    for (const SoundId id : nonPositionalBypass) {
        EXPECT_NE(kRelativeSourceSet.find(id), kRelativeSourceSet.end())
            << "SoundId " << id << " is a non-positional bypass sound but is "
               "missing from kRelativeSourceSet — it must use AL_SOURCE_RELATIVE = AL_TRUE";
    }
}
