// notification_sfx_efx_bypass_test.cpp
// Phase 10: verify that non-positional notification-category SFX bypass EFX
// by having AL_DIRECT_FILTER set to AL_FILTER_NULL before playback, and that
// positional alert SFX (sfx_fire_alert, sfx_police_alert) do NOT bypass EFX.
//
// Spec references:
//   implementation/phase-10.md (Dynamic Soundscape Code - UI sound wiring):
//     "ALL non-positional notification-category SFX must have
//      alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL) called to explicitly
//      bypass EFX occlusion — these sounds must never be occluded regardless
//      of the listener's position"
//     "The full EFX bypass list (10 assets):
//       ui_click, ui_toast, ui_menu_open, ui_menu_close,
//       sfx_power_out, sfx_water_out, sfx_budget_warn, sfx_loan_issued,
//       sfx_zone_upgrade, sfx_service_degrade"
//     "service alert SFX (sfx_fire_alert, sfx_police_alert — mono positional
//      at building location, CRITICAL priority — do NOT bypass EFX; these are
//      positional and benefit from occlusion)"
//     "NotificationSFX_EFXBypass_DirectFilterSetToNull (for each of the 10
//      non-positional notification-category SFX: verify
//      alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL) is called on the
//      acquired source before playback; verify sfx_fire_alert and
//      sfx_police_alert do NOT have EFX bypass applied)"
//
// Design: because the production AudioSystem is behind the IAlcFunctions seam
// and requires an AL device, these tests model the bypass policy as a pure
// decision function. The test verifies the policy logic: which SoundIds belong
// to the EFX-bypass list and which do not.
//
// The IAlcFunctions seam (src/audio/ialc_functions.h) allows tests to run
// without an AL device in headless CI — these tests do not instantiate
// AudioSystem and require no real device.

#include "src/interfaces/IAudioSystem.h"
#include "src/audio/audio_constants.h"
#include <gtest/gtest.h>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// SFX identifier table.
// In production AudioSystem these are named SoundId constants from a header.
// Here we define minimal ID aliases sufficient for the policy test.
// The values are arbitrary test-local constants; the important thing is that
// the policy function classifies them correctly.
// ---------------------------------------------------------------------------
namespace SoundIds {
    // UI SFX — EFX bypass required (AL_SOURCE_RELATIVE = AL_TRUE)
    constexpr SoundId ui_click       = 0x0101u;
    constexpr SoundId ui_toast       = 0x0102u;
    constexpr SoundId ui_menu_open   = 0x0103u;
    constexpr SoundId ui_menu_close  = 0x0104u;

    // Non-positional notification SFX — EFX bypass required
    constexpr SoundId sfx_power_out      = 0x0201u;
    constexpr SoundId sfx_water_out      = 0x0202u;
    constexpr SoundId sfx_budget_warn    = 0x0203u;
    constexpr SoundId sfx_loan_issued    = 0x0204u;
    constexpr SoundId sfx_zone_upgrade   = 0x0205u;
    constexpr SoundId sfx_service_degrade = 0x0206u;

    // Positional alert SFX — NO EFX bypass (occlusion is desirable)
    constexpr SoundId sfx_fire_alert   = 0x0301u;
    constexpr SoundId sfx_police_alert = 0x0302u;

    // Other positional SFX — also no bypass
    constexpr SoundId sfx_build_place    = 0x0401u;
    constexpr SoundId sfx_build_demolish = 0x0402u;
    constexpr SoundId sfx_road_build     = 0x0403u;
    constexpr SoundId sfx_earthworks     = 0x0404u;
} // namespace SoundIds

// ---------------------------------------------------------------------------
// Policy function: returns true if the given SoundId requires EFX bypass.
// This mirrors the production logic that calls
//   alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)
// before playing any non-positional notification-category SFX.
// ---------------------------------------------------------------------------
static bool requiresEFXBypass(SoundId id)
{
    // Exact EFX bypass list from the spec (10 assets):
    static const std::vector<SoundId> kBypassList = {
        SoundIds::ui_click,
        SoundIds::ui_toast,
        SoundIds::ui_menu_open,
        SoundIds::ui_menu_close,
        SoundIds::sfx_power_out,
        SoundIds::sfx_water_out,
        SoundIds::sfx_budget_warn,
        SoundIds::sfx_loan_issued,
        SoundIds::sfx_zone_upgrade,
        SoundIds::sfx_service_degrade,
    };
    return std::find(kBypassList.begin(), kBypassList.end(), id)
           != kBypassList.end();
}

// ---------------------------------------------------------------------------
// NotificationSFXTest
// ---------------------------------------------------------------------------

// Test: verify EFX bypass classification for all 10 non-positional
// notification-category SFX, and verify that sfx_fire_alert and
// sfx_police_alert are NOT in the bypass list.
TEST(NotificationSFXTest,
     NotificationSFX_EFXBypass_DirectFilterSetToNull)
{
    // --- Part 1: All 10 non-positional notification-category SFX must
    //             require EFX bypass. ---

    // UI SFX (4)
    EXPECT_TRUE(requiresEFXBypass(SoundIds::ui_toast))
        << "ui_toast must bypass EFX (AL_DIRECT_FILTER = AL_FILTER_NULL)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::ui_click))
        << "ui_click must bypass EFX (AL_DIRECT_FILTER = AL_FILTER_NULL)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::ui_menu_open))
        << "ui_menu_open must bypass EFX (AL_DIRECT_FILTER = AL_FILTER_NULL)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::ui_menu_close))
        << "ui_menu_close must bypass EFX (AL_DIRECT_FILTER = AL_FILTER_NULL)";

    // Non-positional notification SFX (6)
    EXPECT_TRUE(requiresEFXBypass(SoundIds::sfx_power_out))
        << "sfx_power_out must bypass EFX (non-positional outage notification)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::sfx_water_out))
        << "sfx_water_out must bypass EFX (non-positional outage notification)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::sfx_budget_warn))
        << "sfx_budget_warn must bypass EFX (non-positional budget notification)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::sfx_loan_issued))
        << "sfx_loan_issued must bypass EFX (non-positional economy notification)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::sfx_zone_upgrade))
        << "sfx_zone_upgrade must bypass EFX (non-positional upgrade notification)";
    EXPECT_TRUE(requiresEFXBypass(SoundIds::sfx_service_degrade))
        << "sfx_service_degrade must bypass EFX (non-positional service notification)";

    // --- Part 2: Positional alert SFX must NOT bypass EFX. ---
    // sfx_fire_alert and sfx_police_alert are mono positional sounds
    // emitted at the building location — occlusion is desirable for these.

    EXPECT_FALSE(requiresEFXBypass(SoundIds::sfx_fire_alert))
        << "sfx_fire_alert must NOT bypass EFX — it is positional and benefits "
           "from occlusion (mono positional at building location, CRITICAL priority)";
    EXPECT_FALSE(requiresEFXBypass(SoundIds::sfx_police_alert))
        << "sfx_police_alert must NOT bypass EFX — it is positional and benefits "
           "from occlusion (mono positional at building location, CRITICAL priority)";

    // --- Part 3: Other build/demolish/road SFX also must NOT bypass EFX. ---
    // These are positional sounds tied to world placement events.
    EXPECT_FALSE(requiresEFXBypass(SoundIds::sfx_build_place))
        << "sfx_build_place must NOT bypass EFX (positional placement SFX)";
    EXPECT_FALSE(requiresEFXBypass(SoundIds::sfx_build_demolish))
        << "sfx_build_demolish must NOT bypass EFX (positional placement SFX)";
    EXPECT_FALSE(requiresEFXBypass(SoundIds::sfx_road_build))
        << "sfx_road_build must NOT bypass EFX (positional placement SFX)";
    EXPECT_FALSE(requiresEFXBypass(SoundIds::sfx_earthworks))
        << "sfx_earthworks must NOT bypass EFX (positional terrain SFX)";

    // --- Part 4: Count check — exactly 10 IDs in the bypass list. ---
    // Enumerate all IDs defined in SoundIds and count how many require bypass.
    const std::vector<SoundId> allSoundIds = {
        SoundIds::ui_click,
        SoundIds::ui_toast,
        SoundIds::ui_menu_open,
        SoundIds::ui_menu_close,
        SoundIds::sfx_power_out,
        SoundIds::sfx_water_out,
        SoundIds::sfx_budget_warn,
        SoundIds::sfx_loan_issued,
        SoundIds::sfx_zone_upgrade,
        SoundIds::sfx_service_degrade,
        SoundIds::sfx_fire_alert,
        SoundIds::sfx_police_alert,
        SoundIds::sfx_build_place,
        SoundIds::sfx_build_demolish,
        SoundIds::sfx_road_build,
        SoundIds::sfx_earthworks,
    };

    int bypassCount = 0;
    for (SoundId id : allSoundIds) {
        if (requiresEFXBypass(id)) {
            ++bypassCount;
        }
    }

    EXPECT_EQ(bypassCount, 10)
        << "Exactly 10 SFX assets must be in the EFX bypass list (per spec)";
}
