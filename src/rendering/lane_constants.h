#pragma once

// lane_constants.h — road lane geometry constants.
//
// Split out from render_constants.h so that non-rendering targets (audio_tests)
// can include these float constants without pulling in <irrlicht.h>.
//
// kCarriagewayHalfWidth and kLaneCenterOffset are defined here; render_constants.h
// includes this header so all existing consumers remain unchanged.

namespace RenderConstants {

    // kCarriagewayHalfWidth — half-width of the 7.5 m asphalt carriageway.
    // Asphalt X range: [-kCarriagewayHalfWidth, +kCarriagewayHalfWidth].
    // Kerb strips occupy: [kCarriagewayHalfWidth, kTileSize*0.5f] on each side.
    static constexpr float kCarriagewayHalfWidth = 3.75f;  // metres

    // kLaneCenterOffset — offset from road tile center to lane center (for vehicle rendering).
    // Northbound (+Z) agents: worldX += kLaneCenterOffset
    // Southbound (-Z) agents: worldX -= kLaneCenterOffset
    // East (+X) agents: worldZ += kLaneCenterOffset
    // West (-X) agents: worldZ -= kLaneCenterOffset
    // At intersection tiles (3+ road neighbours): lane offset = 0 (agent snaps to tile center).
    static constexpr float kLaneCenterOffset = 1.875f;  // metres

} // namespace RenderConstants
