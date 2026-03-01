#pragma once

// render_constants.h — rendering constants with no simulation significance.
//
// MUST NOT be placed in src/simulation/simulation_constants.h.
// See implementation/phase-9.md line 40.
//
// This file contains constants that are specific to the rendering pipeline and have
// no relevance to the city simulation logic (economy, zoning, traffic, etc.).

#include <irrlicht.h>

namespace RenderConstants {

    // road_lod2_color — flat shading color used for road tiles at LOD2.
    //
    // At LOD2 distance, road tiles are rendered as flat-shaded quads using this color
    // (no texture sampling, no road marking decals per spec).
    //
    // Value is the average linear RGB of road_asphalt_tileable.dds (DXT5 decode).
    // DXT5 block decode → linear-space average → packed as SColor(alpha, red, green, blue).
    // The asphalt gray average yields approximately R=80, G=80, B=85.
    //
    // CI check #20 in tools/validate_assets.py verifies this constant matches the
    // computed DDS average within ±3/255 per channel. Mismatch fails the build.
    // See implementation/phase-9.md line 37 (Check #20) and line 40 (placement rule).
    //
    // SColor(alpha, red, green, blue) — asphalt gray.
    static const irr::video::SColor road_lod2_color(255, 80, 80, 85);

} // namespace RenderConstants
