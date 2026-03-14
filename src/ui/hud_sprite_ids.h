// src/ui/hud_sprite_ids.h
// Sprite handle constants for hud_sprites_ui.dds (2048×2048, 32×32 cell grid, 64×64 px/cell).
// Handle encoding: col + row * 32. Authoritative table: architecture/asset-standards/2d-texture-standards.md
// DO NOT use raw integer literals in IUIBackend::setElementImage() calls.
#pragma once
#include <cstdint>

// Row 0 — Toolbar tool-mode icons (active state)
constexpr uint32_t kSpriteToolZoneActive       =  0;
constexpr uint32_t kSpriteToolRoadActive        =  1;
constexpr uint32_t kSpriteToolUtilitiesActive   =  2;
constexpr uint32_t kSpriteToolDemolishActive    =  3;
constexpr uint32_t kSpriteToolQueryActive       =  4;

// Row 1 — Toolbar tool-mode icons (inactive state)
constexpr uint32_t kSpriteToolZoneInactive      = 32;
constexpr uint32_t kSpriteToolRoadInactive      = 33;
constexpr uint32_t kSpriteToolUtilitiesInactive = 34;
constexpr uint32_t kSpriteToolDemolishInactive  = 35;
constexpr uint32_t kSpriteToolQueryInactive     = 36;

// Row 1 — Toolbar tool-mode icons (hover state; cols 5-9, adjacent to inactive cols 0-4)
constexpr uint32_t kSpriteToolZoneHover         = 37;
constexpr uint32_t kSpriteToolRoadHover         = 38;
constexpr uint32_t kSpriteToolUtilitiesHover    = 39;
constexpr uint32_t kSpriteToolDemolishHover     = 40;
constexpr uint32_t kSpriteToolQueryHover        = 41;

// Row 2 — Zone sub-panel button icons (active/selected state; order: col=zone R/C/I, row=density Low/Med/High)
constexpr uint32_t kSpriteZoneResLowActive      = 64;
constexpr uint32_t kSpriteZoneComLowActive      = 65;
constexpr uint32_t kSpriteZoneIndLowActive      = 66;
constexpr uint32_t kSpriteZoneResMedActive      = 67;
constexpr uint32_t kSpriteZoneComMedActive      = 68;
constexpr uint32_t kSpriteZoneIndMedActive      = 69;
constexpr uint32_t kSpriteZoneResHighActive     = 70;
constexpr uint32_t kSpriteZoneComHighActive     = 71;
constexpr uint32_t kSpriteZoneIndHighActive     = 72;

// Row 3 — Zone sub-panel button icons (inactive/outline state)
constexpr uint32_t kSpriteZoneResLowInactive    = 96;
constexpr uint32_t kSpriteZoneComLowInactive    = 97;
constexpr uint32_t kSpriteZoneIndLowInactive    = 98;
constexpr uint32_t kSpriteZoneResMedInactive    = 99;
constexpr uint32_t kSpriteZoneComMedInactive    = 100;
constexpr uint32_t kSpriteZoneIndMedInactive    = 101;
constexpr uint32_t kSpriteZoneResHighInactive   = 102;
constexpr uint32_t kSpriteZoneComHighInactive   = 103;
constexpr uint32_t kSpriteZoneIndHighInactive   = 104;

// Row 3 — Zone sub-panel button icons (hover state; cols 9-17, adjacent to inactive cols 0-8)
constexpr uint32_t kSpriteZoneResLowHover       = 105;
constexpr uint32_t kSpriteZoneComLowHover       = 106;
constexpr uint32_t kSpriteZoneIndLowHover       = 107;
constexpr uint32_t kSpriteZoneResMedHover       = 108;
constexpr uint32_t kSpriteZoneComMedHover       = 109;
constexpr uint32_t kSpriteZoneIndMedHover       = 110;
constexpr uint32_t kSpriteZoneResHighHover      = 111;
constexpr uint32_t kSpriteZoneComHighHover      = 112;
constexpr uint32_t kSpriteZoneIndHighHover      = 113;

// Row 4 — Utilities sub-panel button icons (active/selected state)
constexpr uint32_t kSpriteUtilPowerActive       = 128;
constexpr uint32_t kSpriteUtilWaterActive       = 129;
constexpr uint32_t kSpriteUtilFireActive        = 130;
constexpr uint32_t kSpriteUtilPoliceActive      = 131;

// Row 5 — Utilities sub-panel button icons (inactive/outline state)
constexpr uint32_t kSpriteUtilPowerInactive     = 160;
constexpr uint32_t kSpriteUtilWaterInactive     = 161;
constexpr uint32_t kSpriteUtilFireInactive      = 162;
constexpr uint32_t kSpriteUtilPoliceInactive    = 163;

// Row 5 — Utilities sub-panel button icons (hover state; cols 4-7, adjacent to inactive cols 0-3)
constexpr uint32_t kSpriteUtilPowerHover        = 164;
constexpr uint32_t kSpriteUtilWaterHover        = 165;
constexpr uint32_t kSpriteUtilFireHover         = 166;
constexpr uint32_t kSpriteUtilPoliceHover       = 167;

// Row 6 — Active tool indicator badge icons (32×32 px, centered in 64×64 px cell)
constexpr uint32_t kSpriteIndicatorNone         = 192;
constexpr uint32_t kSpriteIndicatorZone         = 193;
constexpr uint32_t kSpriteIndicatorRoad         = 194;
constexpr uint32_t kSpriteIndicatorUtilities    = 195;
constexpr uint32_t kSpriteIndicatorDemolish     = 196;
constexpr uint32_t kSpriteIndicatorQuery        = 197;

// Row 7 — Cursor-shape icons (reserved; Phase 10+ only — IUIBackend::setMouseCursor() not yet added)
constexpr uint32_t kSpriteCursorDefault         = 224;
constexpr uint32_t kSpriteCursorZone            = 225;
constexpr uint32_t kSpriteCursorRoad            = 226;
constexpr uint32_t kSpriteCursorUtilities       = 227;
constexpr uint32_t kSpriteCursorDemolish        = 228;
constexpr uint32_t kSpriteCursorQuery           = 229;

// Row 8 — Minimap overlay toggle icons (active state)
constexpr uint32_t kSpriteOverlayServiceCoverageActive   = 256;

// Row 9 — Minimap overlay toggle icons (inactive state + hover state in adjacent cols)
constexpr uint32_t kSpriteOverlayServiceCoverageInactive = 288;
constexpr uint32_t kSpriteOverlayServiceCoverageHover    = 289;

// Row 10 — Notification / HUD miscellaneous
constexpr uint32_t kSpriteNotificationBell      = 320;
constexpr uint32_t kSpriteClockIcon             = 321;
constexpr uint32_t kSpriteUnsavedDot            = 322;
constexpr uint32_t kSpriteUndoIcon              = 323;

// Row 16 — Panel background cells (dark navy tiles; cols 0-4; rows 11-15 reserved)
// Fill: rgb(13,27,42); opacity/alpha per tier; corner radius: 8 px (except resource bar = 0 px).
constexpr uint32_t kSpritePanelGracePeriod      = 512; // 78% opacity (alpha 199)
constexpr uint32_t kSpritePanelSubPanel         = 513; // 80% opacity (alpha 204)
constexpr uint32_t kSpritePanelToolbar          = 514; // 82% opacity (alpha 209)
constexpr uint32_t kSpritePanelDetail           = 515; // 85% opacity (alpha 217)
constexpr uint32_t kSpritePanelResourceBar      = 516; // 88% opacity (alpha 224)
