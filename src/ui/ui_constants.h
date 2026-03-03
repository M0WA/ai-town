#pragma once

// src/ui/ui_constants.h
// Canonical toolbar carve-out constants from architecture/ui-ux/ui-manager.md.
// All coordinates are in virtual 1920x1080 space.
// Phase 3 UIManager::onEvent() references these constants — creating ui_constants.h at Phase 1
// prevents hardcoded pixel literals in Phase 3.
constexpr int kToolbarLeft   = 8;    // virtual x-coordinate (1920x1080 space)
constexpr int kToolbarRight  = 72;   // virtual x-coordinate (1920x1080 space)
constexpr int kToolbarTop    = 64;   // virtual y-coordinate (1920x1080 space)
// kToolbarBottom = 784: the visual icon group ends at y:600, but the input
// carve-out extends to y:784 to cover the undo button, demand bar, and active
// tool indicator. See architecture/ui-ux/hud-layout.md for the full panel layout.
constexpr int kToolbarBottom = 784;  // virtual y-coordinate (1920x1080 space)

// Minimap widget top edge (virtual 1920x1080 space).
// kMinimapWidgetTopOverlayActive: top edge when an overlay panel (e.g. QueryPanel)
// is active above the minimap.
// kMinimapWidgetTop: default top edge with no overlay active.
constexpr int kMinimapWidgetTopOverlayActive = 732;
constexpr int kMinimapWidgetTop = 880;

// --- Phase 9b: Zone overlay and hover highlight ARGB colour constants ---
// Format: 0xAARRGGBB  (Irrlicht SColor format, alpha 0x60 ≈ 38% for overlays)
// Authoritative source: architecture/ui-ux/hud-layout.md — Zone Colour Overlay ARGB section.
// Used by UIManager to populate m_overlayMap entries and by IRenderer::setZoneOverlay().
// MUST NOT be hardcoded inline — always reference these constants.
constexpr unsigned int kOverlayArgbResidential = 0x6000FF00u;  // green
constexpr unsigned int kOverlayArgbCommercial  = 0x600000FFu;  // blue
constexpr unsigned int kOverlayArgbIndustrial  = 0x60FFFF00u;  // yellow

// Hover highlight ARGB colours per active tool (alpha 0x80 ≈ 50%).
// Used by UIManager::onEvent() MouseMove handler.
constexpr unsigned int kHoverArgbZone      = 0x80FF00FFu;  // semi-transparent magenta
constexpr unsigned int kHoverArgbRoad      = 0x8000FFFFu;  // semi-transparent cyan
constexpr unsigned int kHoverArgbUtilities = 0x80FF8000u;  // semi-transparent orange
constexpr unsigned int kHoverArgbDemolish  = 0x80FF0000u;  // semi-transparent red
constexpr unsigned int kHoverArgbQuery     = 0x80FFFFFFu;  // semi-transparent white
