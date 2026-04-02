#pragma once
// ui_format.h — shared UI formatting helpers for AI Town.
//
// D-1 / UI-1: formatDollar() merges the identical formatDollar() in HUD.cpp and
//   fmtDollar() in FinancesPanel.cpp into a single canonical helper.
// D-5 / UI-2: ratingName() promotes the file-local static from HUD.cpp to a
//   declared free function so other panels can reuse it without duplication.
//
// Include this header from any UI file that needs dollar formatting or city rating names.

#include "src/interfaces/simulation_types.h"

#include <cstdio>
#include <string>

namespace UIFormat {

// formatDollar — format a float value as a signed dollar string (no decimal places).
// Positive: "$12345"   Negative: "-$12345"
inline std::string formatDollar(float value) {
    char buf[64];
    if (value < 0.0f) {
        std::snprintf(buf, sizeof(buf), "-$%.0f", -value);
    } else {
        std::snprintf(buf, sizeof(buf), "$%.0f", value);
    }
    return buf;
}

// ratingName — map CityRatingTier enum to a display string.
// D-5 / UI-2: promoted from a file-local static in HUD.cpp.
inline const char* ratingName(CityRatingTier tier) {
    switch (tier) {
        case CityRatingTier::Village:     return "Village";
        case CityRatingTier::Town:        return "Town";
        case CityRatingTier::City:        return "City";
        case CityRatingTier::Metropolis:  return "Metropolis";
        case CityRatingTier::Megalopolis: return "Megalopolis";
    }
    return "Unknown";
}

} // namespace UIFormat
