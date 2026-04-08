#pragma once

// render_constants.h — rendering constants with no simulation significance.

#include <cstdint>
//
// MUST NOT be placed in src/simulation/simulation_constants.h.
// See implementation/phase-9.md line 40.
//
// This file contains constants that are specific to the rendering pipeline and have
// no relevance to the city simulation logic (economy, zoning, traffic, etc.).

#include <irrlicht.h>
#include "lane_constants.h"  // kCarriagewayHalfWidth, kLaneCenterOffset (Irrlicht-free)

namespace RenderConstants {

    // Overlay Y-offsets (A-17 / REN-24): world-space Y displacement above terrain
    // to prevent Z-fighting between overlay quads and terrain mesh.
    //
    // kHoverYOffset:        used for multi-tile placement preview quads
    //                       (5 cm — minimal, matches tile scale; close camera rarely Z-fights).
    // kHoverHighlightYOffset: used for the single-tile hover highlight quad
    //                       (10 cm — slightly more than preview to ensure it wins Z-test
    //                        when both preview and hover are drawn simultaneously).
    // kOverlayYOffset:      used for zone overlay mesh (25 cm — larger because the overlay
    //                       is rebuilt once per budget tick, not per frame; needs more clearance
    //                       at far zoom where terrain triangles have larger projected areas).
    static constexpr float kHoverYOffset            = 0.05f;
    static constexpr float kHoverHighlightYOffset   = 0.10f;
    static constexpr float kOverlayYOffset          = 0.25f;
    // kZoneOverlayYOffset: Y displacement for zone colour overlay quads (same as kOverlayYOffset;
    //   explicit alias for clarity at call sites in setZoneOverlay()).
    static constexpr float kZoneOverlayYOffset      = 0.25f;
    // kCoverageOverlayYOffset: Y displacement for service coverage overlay quads.
    //   8 cm — sits above the placement preview (5 cm) but below the hover highlight (10 cm).
    static constexpr float kCoverageOverlayYOffset  = 0.08f;
    // kCenterlineYBias: tiny Y nudge applied to road center-line strip vertices so the
    //   white dashes sit fractionally above the asphalt carriageway surface.
    //   0.5 cm — enough to eliminate Z-fighting without visible floating.
    static constexpr float kCenterlineYBias         = 0.005f;

    /// Side length of one map tile in world-space metres.
    /// Shared between IrrlichtRenderer and TerrainSystem (getHeightAtWorld).
    static constexpr float kTileSize = 10.0f;

    /// Vertical offset (metres) baked into road surface vertices by buildTileRoadMesh().
    /// Must be added to the terrain height when positioning vehicles on the road surface.
    static constexpr float kRoadSurfaceYBias = 0.25f;

    // kBlockedArgb — ARGB colour for blocked tiles in placement preview.
    // Semi-opaque red (alpha=0xBB≈73%) per Phase 11d Deliverable 5d spec.
    static constexpr uint32_t kBlockedArgb = 0xBBFF2222u;

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
    // inline prevents ODR violations when included in multiple translation units.
    static inline const irr::video::SColor road_lod2_color(255, 80, 80, 85);

} // namespace RenderConstants

namespace CloudDome {

    // Cloud dome geometry constants (A-12 / REN-17).
    // All measurements in world metres; angles in degrees where noted.
    // See architecture/graphics-architecture/sky-clouds.md for design rationale.

    // kRings: latitude bands — keep horizon fade smooth.
    static constexpr int   kRings         = 32;
    // kSectors: longitude segments around the dome circumference.
    static constexpr int   kSectors       = 32;
    // kAltitude: world-Y of the dome base ring relative to the camera.
    //   -1000 m → atan(-1000/6000) ≈ -9.5° below horizon, fully transparent at base.
    static constexpr float kAltitude      = -1000.0f;
    // kRadius: horizontal radius of the dome in metres.
    //   Must be less than the far clip plane distance (15000 m).
    static constexpr float kRadius        = 6000.0f;
    // kHeight: vertical extent of the dome from base ring to apex.
    //   Apex is at kAltitude + kHeight = -1000 + 2000 = 1000 m above camera.
    static constexpr float kHeight        = 2000.0f;
    // kUVScale: cloud texture tiling factor (repeats per full circumference).
    static constexpr float kUVScale       = 4.0f;

} // namespace CloudDome

namespace RoadLOD {

    // Road LOD distance thresholds (A-14 / REN-7).
    // kLOD0to1: squared camera distance at which road tiles switch from terrain-conforming
    //   LOD0 mesh to the flat LOD1 quad (50 m).
    // kLOD1to2: squared camera distance at which road tiles switch from LOD1 to flat
    //   LOD2 solid-colour quad (150 m).
    // kCullDist: squared camera distance at which road tiles are culled entirely (300 m).
    // LOD swap and cull are a future per-frame update path; constants are declared here
    // so they are discoverable and out of the placeRoadMesh() function scope.
    static constexpr float kLOD0to1  =  50.0f;
    static constexpr float kLOD1to2  = 150.0f;
    static constexpr float kCullDist = 300.0f;

} // namespace RoadLOD
