# HUD Layout

- Minimap (bottom-right corner, 200×200 px in virtual 1920×1080 space)
- Resource/budget bar (top bar, full width, virtual y: 0–56 px): treasury balance (`ICitySimulation::getTreasury()`), outstanding debt indicator (`getOutstandingDebt()` — hidden when debt is zero), City Rating label (`getCityRating()` → `CityRatingTier` display name), population count (`getPopulation()`), and current in-game date/time display. **Red flashing budget indicator**: when `ICitySimulation::getConsecutiveDeficitMonths() >= 2`, a red-tinted pulsing overlay (alpha oscillation 0.3–1.0 at ~1 Hz) activates on the treasury field, distinct from the standard deficit indicator. Removed immediately when `getConsecutiveDeficitMonths()` returns 0 (streak broken). Implementation: `UIManager::update()` polls `getConsecutiveDeficitMonths()` each frame and calls `setElementAlpha()` with a sine-wave value when active. See [Game Over Flow](../game-design/game-over-flow.md) for streak mechanics.
- Time controls and speed selector (top-right, virtual x: 1600–1796 px, y: 8–56 px): four mutually exclusive buttons — Pause (⏸), 1× (▶), 3× (▶▶), 10× (▶▶▶). Active button has accent-color border; inactive buttons have default border. Dispatches `ICitySimulation::setSpeed(SpeedMultiplier)` on click. **Polling contract**: `UIManager::update()` polls `ICitySimulation::getSpeedMultiplier()` every frame (not only on player input) and updates the active button highlight to match — this is required for auto-slow events from `CitySimulation::tick()` (deficit month-1 auto-slow to 1×) to be reflected without UI-layer polling gaps. **State matrix** (per [Notification System](notification-system.md)): speed buttons are disabled (grayed, non-interactive via `setElementEnabled(..., false)`) only when a blocking modal is active; the speed selector remains **enabled** during CRITICAL-toast-only auto-pause (no modal) so the player can pre-set speed for resume. See the authoritative state matrix in `notification-system.md` for all state combinations. See [Settings / Pause Menu](settings-pause-menu.md) for Escape → pause interaction.
- Notification/alert area (top-center for transient toasts); see Notification System for stacking rules
- Primary toolbar with tool modes: Zone, Road, Utilities, Demolish, Query — **left panel**, virtual bounds: x: **8–72 px** (8 px left margin so the toolbar does not clip at the window edge on non-16:9 resolutions and ultrawide aspect ratios), y: 64–600 px (below resource bar, above minimap area). Each tool mode is a 48×48 px icon button with 8 px padding. Active tool highlighted with accent-color border.

  **Toolbar y-range — visual bounds vs. input carve-out (MUST read before implementing):**

  > **WARNING: DO NOT use y:600 as an input gate threshold.** y:64–600 px is the visual icon group bounds only. All input arbitration MUST use `kToolbarBottom = 784` (defined in `ui_constants.h`). Using y:600 for the input gate leaves the undo button, demand bars, and active tool indicator exposed to accidental world-clicks. See `ui_constants.h` for the authoritative constant definition and `input-arbitration.md` Priority 3 for the enforcement point.

  - **y:64–600 px** is the **visual icon group bounds only** — the region where the five tool-mode icon buttons are drawn. This range describes geometry for *rendering* purposes only. It MUST NOT be used as an input gate boundary.
  - **`kToolbarBottom = 784`** (defined in `ui_constants.h`; see also `input-arbitration.md` Priority 3) is the **full input carve-out bottom edge**. Any mouse click with `virtual_y <= 784` **and** `virtual_x <= 72` is unconditionally consumed by the left-panel toolbar input handler, regardless of whether a visible toolbar element exists at that exact pixel. The carve-out intentionally extends below the visible icon group (from y:600 down to y:784) to cover the undo button (y:608–656), the demand bars (y:664–744), and the active tool indicator (y:752–784) — all of which are left-panel elements that would otherwise be missed if the gate used y:600.
  - **Phase 3 implementers MUST use `kToolbarBottom = 784` for the input gate.** Using y:600 is incorrect; it leaves the undo button, demand bars, and active tool indicator exposed to accidental world-clicks when the player's cursor is in the lower portion of the toolbar panel.
- **Undo button**: Positioned directly below the tool icon group within the left toolbar panel. Virtual bounds: x: 8–72 px, y: 608–656 px (immediately below the last tool icon, 8 px gap from y:600). A 48×48 px icon button labeled "↩ Undo" (or Ctrl+Z hotkey hint). Grayed out (via `IUIBackend::setElementEnabled(..., false)`) when no undo action is available or while a blocking modal is active. Disabled state must use `setElementEnabled` (non-interactive, grayed), not `setElementVisible` (hidden) — the button remains visible at all times. **Undo countdown**: When an undoable action is pending, the button label changes to "↩ Undo (expires in Xs)" where X is the real-time seconds remaining until the second budget tick expires the undo window, updated every real second. Text turns amber when `remainingSeconds < 5.0` or when the total undo window is ≤ 6 s (i.e., at 10× simulation speed where the second tick fires in ~6 real seconds — the countdown is amber from the moment the action is taken). Implementation: compute `remainingSeconds = secondBudgetTickTimeReal − IClock::nowSeconds()`; set amber if `remainingSeconds < 5.0 || totalWindowSeconds <= 6.0`. The countdown is absent when the button is grayed out (no pending undo). Requires `IClock` injection into the component responsible for rendering the undo button. See [Undo System](../game-design/undo-system.md) for the full undo expiry specification.
- **Unsaved changes indicator**: A dot (**16×16 px**, amber fill) placed immediately to the **left** of the notification bell icon at virtual bounds x: **1796–1812 px**, y: 8–24 px. **Must not overlap the bell icon** (bell occupies x: 1820–1868 px; the dot must end at x ≤ 1812 with at least an 8 px gap before the bell's x:1820 left edge). Appears whenever there are unsaved changes since the last manual save or auto-save. Hidden when game state matches last save. Tooltip on hover: "You have unsaved changes. Press Ctrl+S to save." 16×16 px minimum ensures the dot is visually distinct and hoverable without pixel-perfect accuracy. The dot and bell are mutually exclusive interactive targets — no shared coordinate range between them. **The dot must remain visible (not cleared) after a failed auto-save** — a failed auto-save does not constitute a successful save and must not clear the unsaved-changes state. The dot element has a fixed amber fill color authored at creation time via the backend — it does not change color dynamically. Visibility is controlled exclusively via `setElementVisible`. No `setElementColor` method is required on `IUIBackend` for V1; the 19-method `IUIBackend` interface (see `architecture/ui-ux/ui-manager.md` §IUIBackend Method Contract) is sufficient for the dot element.
- **Grace period indicator**: Virtual bounds: x: 8–1912 px, y: 60–92 px (32 px height, directly beneath the resource/budget bar which occupies approximately y: 0–56 px). Displayed while the wall-clock grace period is active. Label: "Cost waiver: Xs remaining" (green text with clock icon from UI sprite sheet), where X is `floor(120 − IClock::nowSeconds() elapsed since game start)`, updated every real second. When fewer than 20 real seconds remain, the label text turns amber as a last-chance visual cue. On expiry, the indicator fades to alpha 0 over 0.5 real seconds via `setElementAlpha`, then is hidden via `setElementVisible(false)` — the grace period has ended and no countdown is needed. The 120 s duration is a wall-clock measurement and is unaffected by simulation speed — at 10× speed the same 120 real seconds apply. **Interactive tooltip**: On hover or click, shows: "During the grace period, road maintenance ($10/tile/month) and building upkeep costs are waived. These costs will begin in approximately Xs" (where X is the real-seconds countdown already shown in the indicator label — not a month count, as the grace period is real-time not month-aligned) "Estimated monthly upkeep when active: $[calculated from current city]." The estimated figure updates each real second as the player builds. Note: road tile placement cost ($500/tile) is NOT waived during the grace period — only ongoing maintenance and upkeep costs are waived. See [Economy Model](../game-design/economy-model.md) for the authoritative 120 s definition.
- **Demand pressure bar** (compact, anchored below undo button): virtual bounds x: 8–72 px, y: **664–744 px** (8 px visual gap from the undo button bottom edge at y:656). Per-zone-type (R/C/I) unmet demand percentage indicator as three vertical bars. Updates each budget tick. (**Layout note**: the previous placement at y:608–688 px overlapped the undo button at y:608–656 px; moved down to y:664–744 px to eliminate the overlap.) **Low-resolution text legibility**: Each bar is labeled with a single-character zone-type indicator ('R', 'C', 'I') rendered above the bar column. At the virtual 64 px toolbar width, each of the 3 bars occupies ~20 px horizontally. The 'R', 'C', 'I' labels must use a **minimum 9-point font** (virtual space) at all supported resolutions to remain legible. If the UI scaling factor produces a rendered label below 9 virtual pixels high, the HUD must fall back based on the active accessibility mode:

  - **Standard mode** (colorblind mode OFF): Switch to a **numeric percentage tooltip** (shown on hover/tap only) rather than a persistent label — do not render sub-9-pixel text. This prevents illegible "blur spots" on low-resolution displays or when the demand bar is very compact. The tooltip remains supplemental information in this mode.
  - **Colorblind mode** (colorblind mode ON): The tooltip-only fallback is **NOT permitted**. Color is already an insufficient encoding in colorblind mode, so removing the persistent label would leave users with no reliable zone-type identification. Instead, the HUD MUST render the single-character zone-type symbol ('R', 'C', or 'I') at the hard physical floor of **11 px physical pixels** (per `resolution-ui-scaling.md` Typography hard physical floor — `UIScaler` clamps text scale to this minimum). Alongside the clamped label, the bar column MUST also display a **pattern or hatching overlay** (see `resolution-ui-scaling.md` Colorblind Accessibility section — "Demand pressure bar hatching patterns (colorblind mode)": Residential = diagonal hatching at 45°, Commercial = horizontal lines, Industrial = cross-hatch) so that zone type can be distinguished by pattern alone, independent of both color and the small label. The tooltip remains available as supplemental information but may not be the sole encoding in colorblind mode.

  **Data source**: `ICitySimulation::getDemandPressurePct(ZoneType)` returns the city-wide weighted-average `demand_factor` across all tiles of that zone type, updated each budget tick. **DISPLAY SCALING**: `getDemandPressurePct()` returns `float` in `[0.0, 1.0]` — NOT a percentage in `[0, 100]`. The HUD demand bars MUST multiply by `100.0f` before display (e.g., `barFillPct = getDemandPressurePct(zone) * 100.0f`). Omitting this multiplication produces a bar always showing ≤1% fill. **INVERSE SEMANTICS**: `QueryResult::demandPressurePct` (Inspector Panel) uses the complementary definition `(1.0f − effective_demand_factor) × 100` where 100 = zero demand — opposite direction. Do NOT use `QueryResult::demandPressurePct` directly to fill the HUD demand bar. This is distinct from per-tile demand pressure available via `QueryResult::demand_pressure_pct` in the Query/Inspector Panel.
- **Active tool indicator** (persistent badge below demand bar): virtual bounds x: 8–72 px, y: 752–784 px. Shows the current tool's 32×32 px icon with a small text label, visible from anywhere on screen without looking at the toolbar. Updates immediately when the active tool changes. Icon sprite handles are drawn from `hud_sprites_ui.dds` row 6 (`kSpriteIndicatorNone` through `kSpriteIndicatorQuery` — see `src/ui/hud_sprite_ids.h` and `architecture/asset-standards/2d-texture-standards.md` UI Sprite Sheet Cell Layout section). **Cursor shape**: each tool mode uses a distinct cursor shape from the UI sprite sheet (Zone: crosshair with zone-color tint; Road: road-segment icon; Utilities: wrench; Demolish: X marker; Query: magnifying glass). OS-level cursor shape changes require `IUIBackend::setMouseCursor()`, which is not part of the V1 19-method `IUIBackend` interface. Cursor icon rendering from the sprite sheet is **deferred to Phase 12** (the colorblind QA pass — consistent with the hover-highlight colorblind glyph deferral in `architecture/ui-ux/resolution-ui-scaling.md` Colorblind Accessibility section). The `m_activeTool` state set in Phase 9b is the prerequisite for Phase 12 cursor shape selection. Row 7 cells in the sprite sheet (`kSpriteCursorDefault` through `kSpriteCursorQuery`) are reserved for this purpose and already authored in `assets/textures/ui/hud_sprites_ui.dds` — no further artwork is needed in Phase 12, only the `setMouseCursor()` implementation.

## Zone Sub-Panel

The Zone sub-panel is a 3×3 grid of buttons that appears immediately to the right of the
left toolbar (x:80, y:64) whenever the Zone tool is active. It is hidden for all other tool
modes. Virtual bounds: x:80–280 px, y:64–192 px (width = (64×3)+(4×2) = 200 px,
height = (40×3)+(4×2) = 128 px). Grid layout: 3 columns (zone type: Residential / Commercial
/ Industrial, left-to-right) × 3 rows (density tier: Low / Medium / High, top-to-bottom).
Each button is 64×40 px with a 4 px gap between buttons. Constants defined in
`src/ui/ui_constants.h`: `kZoneSubPanelLeft`, `kZoneSubPanelTop`, `kZoneSubBtnW`,
`kZoneSubBtnH`, `kZoneSubBtnGap`.

### Zone Sub-Panel Button Content

Each button displays a sprite icon from `hud_sprites_ui.dds` via
`IUIBackend::setElementImage()` using the `kSpriteZone*` constants from
`src/ui/hud_sprite_ids.h` (rows 2 and 3 of the sprite sheet — see
`architecture/asset-standards/2d-texture-standards.md` Cell Assignment Tables). The button
is created with an empty text label (`addButton("", ...)`); the sprite is the sole visual
encoding. Do NOT pass a non-empty text string — Irrlicht renders button text on top of the
sprite image, which would overlay the icon with a text label.

**Tooltip**: On hover, each zone sub-panel button displays a tooltip with the full zone type
and density name, e.g. "Residential — Low density", "Commercial — Medium density",
"Industrial — High density". Tooltips are provided by the Irrlicht GUI environment's
built-in `setToolTipText()` on each button element.

### Zone Sub-Panel Selection State

At construction, the default selection is Residential Low (row 0, col 0 — `idx = 0`). The
selected button displays the active-state sprite (`kSpriteZoneResLowActive`, etc.). All
other buttons display the inactive-state sprite. When the player clicks a different button,
all buttons are set to their inactive sprite, then the clicked button is set to its
active-state sprite. The selected zone type and density tier are stored in
`m_selectedZoneType` and `m_selectedDensityTier` in `UIManager`.

### Zone Sub-Panel Visibility Rules

- The zone sub-panel is shown (`setElementVisible(btn, true)`) whenever `m_activeTool`
  becomes `ActiveTool::Zone` — this includes: clicking the Zone toolbar button, pressing the
  Z hotkey, and returning to Zone from any other tool mode (including from Query mode).
- The zone sub-panel is hidden (`setElementVisible(btn, false)`) whenever `m_activeTool`
  changes to any value other than `ActiveTool::Zone`.
- **Hiding must occur before `m_activeTool` is set** in `updateSubPanelVisibility()` — or
  equivalently, `updateSubPanelVisibility()` must be called immediately after every change
  to `m_activeTool`.
- The zone sub-panel is NOT shown when Query mode is active. It reappears the moment the
  Zone tool is next selected.

### Zone Rectangular Selection (SimCity-style)

The Zone tool uses a deferred rectangular fill pattern for placement. This is the sole
placement tool that does NOT fill tiles as the cursor drags.

**Interaction sequence:**

1. **LMB press on terrain** — sets the anchor tile (`m_zoneAnchorX`, `m_zoneAnchorZ`) to
   the tile under the cursor. The hover highlight updates to the anchor tile. No zone tiles
   are placed on press. The event is consumed (`return true`).

2. **Mouse drag (LMB held)** — the hover highlight updates to the current tile under the
   cursor as the mouse moves. No zone tiles are placed during drag. The rectangle corner
   moves with the cursor but no overlay or preview rectangle is drawn in V1 (the hover
   highlight shows the current corner tile only — V1 Option B). The drag-to-place path
   (`doTerrainPlacement` on `MouseMove`) is excluded for the Zone tool.

3. **LMB release** — fills all tiles in the axis-aligned rectangle
   `[min(anchorX, hoverX) .. max(anchorX, hoverX)] × [min(anchorZ, hoverZ) .. max(anchorZ, hoverZ)]`
   by calling `doTerrainPlacement(tx, tz)` for each tile. Each tile's earthworks cost is
   computed independently (slope checked per tile). After the fill, the anchor is reset to
   `{-1, -1}` and the hover highlight reverts to the release tile. The release event is NOT
   consumed (`return false`) per Priority 7 rules.

**Invariants:**

- A 1×1 selection (press and release on the same tile) calls `doTerrainPlacement` exactly
  once, matching the semantics of a single click.
- If the LMB is released while the hover tile is invalid (`m_hoveredTileX == -1`, e.g., ray
  missed all terrain), no tiles are filled and the anchor is cleared silently.
- When the active tool changes away from Zone (toolbar click, hotkey, or any other path),
  `m_zoneAnchorX` and `m_zoneAnchorZ` are reset to `-1` and `m_lmbHeld` is reset to `false`
  as part of `onNewGame()`. The tool-switch does not trigger a partial fill.

**Road, Utilities, and Demolish** tools retain their original tile-by-tile drag behavior:
`MouseButtonDown` calls `doTerrainPlacement()` immediately on press, and `MouseMove` with
LMB held calls `doTerrainPlacement()` for each new tile the cursor enters. See
`input-arbitration.md` Priority 7 for the authoritative per-tool behavior table.

## Utilities Sub-Panel

The Utilities sub-panel is a 4×1 single-row grid of buttons that appears immediately to the
right of the left toolbar (x:80, y:64) whenever the Utilities tool is active. It is hidden
for all other tool modes. It shares the same top anchor (y:64) as the Zone sub-panel — the
two sub-panels are mutually exclusive, so only one is ever visible at a time. Virtual bounds:
x:80–348 px, y:64–104 px (width = (64×4)+(4×3) = 268 px, height = 40 px). Grid layout: all
4 buttons in a single horizontal strip — column 0 = Power Plant, column 1 = Water Tower,
column 2 = Fire Station, column 3 = Police Station. Constants defined in
`src/ui/ui_constants.h`: `kUtilSubPanelLeft`, `kUtilSubPanelTop`, `kUtilSubBtnW`,
`kUtilSubBtnH`, `kUtilSubBtnGap`.

### Utilities Sub-Panel Button Content

Each button displays a sprite icon from `hud_sprites_ui.dds` via
`IUIBackend::setElementImage()` using the `kSpriteUtil*` constants from
`src/ui/hud_sprite_ids.h` (rows 4 and 5 of the sprite sheet — see
`architecture/asset-standards/2d-texture-standards.md` Cell Assignment Tables). The button
is created with an empty text label (`addButton("", ...)`); the sprite is the sole visual
encoding. Do NOT pass a non-empty text string — Irrlicht renders button text on top of the
sprite image, which would overlay the icon with a text label.

Button index maps to `ServiceBuildingType` enum value: `{PowerPlant=0, WaterTower=1,
FireStation=2, PoliceStation=3}` — sequential from 0 as defined in
`src/interfaces/simulation_types.h`.

**Tooltip**: On hover, each Utilities sub-panel button displays a tooltip with the full
service building name: "Power Plant", "Water Tower", "Fire Station", "Police Station".
Tooltips are provided by Irrlicht's built-in `setToolTipText()` on each button element.

### Utilities Sub-Panel Selection State

At construction, the default selection is Power Plant (index 0,
`ServiceBuildingType::PowerPlant`). The selected button displays the active-state sprite
(`kSpriteUtilPowerActive`, etc.). All other buttons display the inactive-state sprite.
When the player clicks a different button, all buttons are set to their inactive sprite,
then the clicked button is set to its active-state sprite. The selected building type is
stored in `m_selectedServiceBuilding` in `UIManager`.

### Utilities Sub-Panel Visibility Rules

- The Utilities sub-panel is shown whenever `m_activeTool` becomes `ActiveTool::Utilities`.
- The Utilities sub-panel is hidden whenever `m_activeTool` changes to any other value.
- Hiding must occur before `m_activeTool` is set, or equivalently,
  `updateSubPanelVisibility()` must be called immediately after every change to
  `m_activeTool`.
- The Utilities sub-panel is NOT shown when Query mode is active.

## Tool Mode State Machine

The active tool is tracked in `UIManager::m_activeTool` (type `ActiveTool`, defined in
`src/interfaces/simulation_types.h`). The valid states are: `None`, `Zone`, `Road`,
`Utilities`, `Demolish`, `Query`. The state machine has the following transition rules:

### Permitted Transitions

Any tool mode may transition to any other tool mode in a single user action. There are no
multi-step transition requirements. Specifically:

- **Query to Zone**: User clicks the Zone toolbar button (y:64–112) while Query mode is
  active. This closes the inspector panel (if open), sets `m_activeTool = Zone`, shows the
  zone sub-panel, and updates the active tool indicator — all in one action.
- **Query to Road / Utilities / Demolish**: Identical single-action transition from Query.
- **Zone / Road / Utilities / Demolish to Query**: User clicks the Query toolbar button
  (y:288–336) or presses I. This hides the zone or utilities sub-panel and activates Query
  mode.
- **Any tool to None**: Pressing I while Query is active toggles back to `None` (not back
  to the previous non-Query tool). Clicking the Query toolbar button while Query is active
  also returns to `None`.

### Inspector Panel Lifecycle Within the State Machine

The inspector panel (`m_inspectorOpen`) is tied to `ActiveTool::Query`:

- The inspector may only be opened when `m_activeTool == ActiveTool::Query`.
- **Leaving Query mode always closes the inspector**: whenever `m_activeTool` transitions
  away from `ActiveTool::Query` (to Zone, Road, Utilities, Demolish, or None), the
  inspector panel MUST be hidden (`m_inspector->hide()`) and `m_inspectorOpen` MUST be set
  to `false` before the new tool sub-panel is shown. This applies regardless of the
  transition trigger (toolbar button click, hotkey, or any other input path).
- This rule is enforced in `UIManager::onEvent()` at Priority 5 (toolbar dispatch) and at
  Priority 3 (toolbar carve-out within the inspector-open dismiss path). See
  `input-arbitration.md` Priority 3 for the authoritative enforcement rule.

### Sub-Panel Visibility Per Tool Mode

| Active tool | Zone sub-panel | Utilities sub-panel | Inspector panel |
|---|---|---|---|
| None | Hidden | Hidden | Hidden |
| Zone | Visible | Hidden | Hidden |
| Road | Hidden | Hidden | Hidden |
| Utilities | Hidden | Visible | Hidden |
| Demolish | Hidden | Hidden | Hidden |
| Query | Hidden | Hidden | Open (on tile click) or Hidden |

`UIManager::updateSubPanelVisibility()` MUST enforce this table whenever `m_activeTool`
changes. **Important**: the inspector panel is NOT controlled by `updateSubPanelVisibility()`
for the Query→other transition; it is explicitly closed at the transition site before
`updateSubPanelVisibility()` is called.

## Tile Hover Highlight — ARGB Colour Scheme

The tile hover highlight is a semi-transparent wireframe quad rendered over the hovered
terrain tile. ARGB values are encoded as `0xAARRGGBB` (Irrlicht `SColor` format;
AA=alpha, RR=red, GG=green, BB=blue).

**These values are authoritative and must be used by `UIManager::onEvent()` MouseMove
handler when calling `IRenderer::setTileHoverHighlight()`. Do not use inline literals —
define named constants in `src/ui/ui_constants.h`.**

| Active tool | ARGB value | Colour description | Named constant |
|---|---|---|---|
| Zone | `0x80FF00FF` | Semi-transparent magenta (alpha=128) | `kHoverArgbZone` |
| Road | `0x8000FFFF` | Semi-transparent cyan (alpha=128) | `kHoverArgbRoad` |
| Utilities | `0x80FF8000` | Semi-transparent orange (alpha=128) | `kHoverArgbUtilities` |
| Demolish | `0x80FF0000` | Semi-transparent red (alpha=128) | `kHoverArgbDemolish` |
| Query | `0x80FFFFFF` | Semi-transparent white (alpha=128) | `kHoverArgbQuery` |

Alpha value `0x80` = 128 = 50% opacity. This provides enough transparency to see terrain
geometry beneath the highlight, while remaining clearly visible against both light and
dark terrain surfaces. The highlight quad is rendered via
`IVideoDriver::drawMeshBuffer()` using `EMT_TRANSPARENT_ALPHA_CHANNEL` material type,
placed at terrain height +0.05 world units above the terrain surface (Z-fighting prevention).

**Clear sentinel**: pass `tileX = -1`, `tileZ = -1`, `argb = 0` to
`IRenderer::setTileHoverHighlight()` to remove the highlight entirely (no active tool or
ray-cast miss). The value `kHoverArgbClear = 0x00000000u` is the canonical constant for
this case.

## Zone Colour Overlay — ARGB Colour Scheme

The zone overlay is a persistent semi-transparent fill rendered over all zoned tiles.
ARGB values are encoded as `0xAARRGGBB`. These are the **canonical zone identification
colours** used consistently across all UI systems: zone overlay quads, minimap zone coding,
and zone sub-panel button active-state icon tints.

**These values are authoritative and must be used by `UIManager` when constructing the
`m_overlayMap` entries passed to `IRenderer::setZoneOverlay()`. Define as named constants
in `src/ui/ui_constants.h`.**

| Zone type | ARGB value | Colour description | Named constant |
|---|---|---|---|
| Residential | `0x6000FF00` | Semi-transparent green (alpha=96, ~38%) | `kOverlayArgbResidential` |
| Commercial | `0x600000FF` | Semi-transparent blue (alpha=96, ~38%) | `kOverlayArgbCommercial` |
| Industrial | `0x60FFFF00` | Semi-transparent yellow (alpha=96, ~38%) | `kOverlayArgbIndustrial` |

Alpha value `0x60` = 96 = approximately 38% opacity. This is deliberately lower than the
hover highlight alpha (0x80 = 50%) so that the always-on zone overlay is visually recessive
and the temporary hover highlight reads as a distinct, foreground interaction cue on top.

**Overlay depth ordering**: Zone overlay quads use Y offset +0.1 world units above terrain
surface. Hover highlight quads use Y offset +0.05 world units. This ensures the hover
highlight always renders above the zone overlay when both are present on the same tile.

**Minimap consistency**: These same three ARGB values are used for zone coding in the
minimap top-down render (R=green, C=blue, I=yellow — consistent with the minimap spec in
`architecture/ui-ux/minimap.md`). Do not define separate colour constants for the minimap
zone coding; reuse `kOverlayArgbResidential`, `kOverlayArgbCommercial`, and
`kOverlayArgbIndustrial` from `ui_constants.h`.

## Visual Design — Glass City

### Panel Backgrounds

All HUD panels use the Glass City deep-navy palette defined in
`architecture/ui-ux/resolution-ui-scaling.md` Visual Design — Glass City section.

| Panel | Background |
|---|---|
| Resource/budget bar | `rgba(13, 27, 42, 0.88)` — no corner radius |
| Left toolbar panel | `rgba(13, 27, 42, 0.82)` — 8 px radius on right/bottom inner edges |
| Zone sub-panel | `rgba(13, 27, 42, 0.80)` — 8 px radius on all inner edges |
| Utilities sub-panel | `rgba(13, 27, 42, 0.80)` — 8 px radius on all inner edges |
| Budget detail panel | `rgba(13, 27, 42, 0.85)` — 8 px radius |
| Grace period indicator | `rgba(13, 27, 42, 0.78)` — 8 px radius |

### Toolbar Icon States

Toolbar tool buttons (Zone, Road, Utilities, Demolish, Query) and all sub-panel buttons
use the icon state spec below. Both the inactive and active cells are separate sprites in
`hud_sprites_ui.png`; the backend call `IUIBackend::setElementImage()` switches between them.

| State | Icon style | Opacity | Border |
|---|---|---|---|
| Inactive | **Outlined — 2 px stroke, no fill** | 65% | None |
| Active | **Filled solid icon** | 100% | 2 px teal `rgba(0, 201, 200, 0.75)` + baked glow |

The teal border and glow are pre-baked into the active sprite cell. No runtime blur is
applied. The `kSpriteZone*Active`, `kSpriteUtil*Active`, etc. constants reference the
filled-icon cells; the inactive constants reference the stroke-only cells.

### Button Tile

All buttons in toolbar and sub-panels use the three-state tile:

- **Inactive**: `rgba(255, 255, 255, 0.08)` background, 1 px `rgba(255, 255, 255, 0.18)` border
- **Hover**: `rgba(255, 255, 255, 0.15)` background, 1 px `rgba(255, 255, 255, 0.35)` border
- **Active**: `rgba(0, 201, 200, 0.22)` teal wash, 2 px `rgba(0, 201, 200, 0.75)` border + 4 px baked glow

This replaces the earlier "accent-color border only" description for the active button state.
The teal wash + 2 px border is the single authoritative active-state signal.

### Text Colours in HUD

All HUD text follows the Glass City palette (see `resolution-ui-scaling.md` canonical table):

| Content type | Colour |
|---|---|
| Numeric values (treasury, population, date, demand %) | `#F0B429` warm amber |
| Primary labels | `#EBF4F6` near-white |
| Secondary / sub-labels | `#4A7FA5` mid-blue |
| Deficit / error indicator | `#F04E37` red |
| Warning (undo countdown amber, grace period near-expiry) | `#E8960C` warning amber |
| Undo countdown text turns amber when `remainingSeconds < 5.0` | `#F0B429` (value amber, same token) |

**Red flashing budget indicator** (consecutive deficit): the pulsing overlay on the treasury
field remains red. The ARGB for the overlay pulse is `0x80F04E37` (alpha 128, red `#F04E37`)
in Irrlicht `SColor` format, replacing any previously unspecified red tint. The alpha
oscillates 0.3–1.0 via `setElementAlpha()` as described in the layout spec above; the colour
component itself is fixed at `#F04E37`.

**Unsaved-changes dot**: fixed amber fill `#F0B429` (value amber token). Authored at
creation time; does not change colour dynamically.

- **Notification log bell icon**: positioned at the right end of the resource/budget bar, virtual bounds x: 1820–1868 px, y: 8–56 px (48×48 px icon). Displays an unread-count badge (small numeral overlay) that increments when new notifications arrive and resets to zero when the log is opened. Keyboard shortcut: **B** (toggles the log open/closed; rebindable in Settings > Controls with standard conflict detection).

## Phase 10 Audio Wiring for UI Events

All UI SFX calls below use `m_audio->playSound(soundId, SoundPriority::NORMAL, 1.0f)`.
`m_audio` is the `IAudioSystem*` stored as a private member on `UIManager` (see
`architecture/ui-ux/ui-manager.md` Class Structure private members). The `HUD` class also
holds its own `IAudioSystem* m_audio` (see HUD Class Structure — Constructor Signature) but
the call sites in this section (`onEvent()`, `updateSubPanelVisibility()`) are `UIManager`
methods and therefore use `UIManager::m_audio`, not `HUD::m_audio`. Every call is guarded by
`if (m_audio)`. All UI SFX are non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`) with EFX
bypass (`AL_DIRECT_FILTER = AL_FILTER_NULL`).

### `ui_click` — Toolbar button pressed

**Call site**: `UIManager::onEvent()`, at Priority 5 (toolbar dispatch), immediately after
the active tool state is updated. Fires once per toolbar button click (Zone, Road, Utilities,
Demolish, Query) regardless of whether the tool changed.

```cpp
// In UIManager::onEvent(), Priority 5 toolbar dispatch, after setActiveTool():
if (m_hud && m_audio) {
    m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
}
```

`UI_CLICK` = SoundId 22 (`ui_click.wav`). Also fires on zone sub-panel button clicks and
Utilities sub-panel button clicks — any button element backed by `addButton()` whose click
is processed in the toolbar/sub-panel dispatch path triggers this SFX.

### `ui_menu_open` — Sub-panel or overlay panel opened

**Call site**: `UIManager::updateSubPanelVisibility()`, immediately after
`setElementVisible(panel, true)` is called for the zone sub-panel or utilities sub-panel.

```cpp
// In UIManager::updateSubPanelVisibility(), after showing a sub-panel:
if (m_audio) {
    m_audio->playSound(UI_MENU_OPEN, SoundPriority::NORMAL, 1.0f);
}
```

Also fires when the Budget Detail Panel is opened (hover → click on treasury balance field)
and when the Tax Rate Panel opens. Call site: the panel's `show()` method or the UIManager
handler that sets the panel visible.

`UI_MENU_OPEN` = SoundId 24 (`ui_menu_open.wav`).

### `ui_menu_close` — Sub-panel or overlay panel closed

**Call site**: `UIManager::updateSubPanelVisibility()`, immediately after
`setElementVisible(panel, false)` is called when a sub-panel is hidden due to a tool change.
Also fires on Budget Detail Panel close and Tax Rate Panel close.

```cpp
// In UIManager::updateSubPanelVisibility(), after hiding a sub-panel:
if (m_audio) {
    m_audio->playSound(UI_MENU_CLOSE, SoundPriority::NORMAL, 1.0f);
}
```

`UI_MENU_CLOSE` = SoundId 25 (`ui_menu_close.wav`).

**Guard**: Do NOT fire `ui_menu_open` and `ui_menu_close` on the same frame (i.e. when
`updateSubPanelVisibility()` hides one panel and shows another in the same tool-change event).
Implementation: call `ui_menu_close` first for any newly-hidden panels, then call
`ui_menu_open` once for any newly-shown panel. If no panel changes visibility state, no
sound fires.

### `ui_toast` — Notification toast displayed

**Call site**: `NotificationManager::postCritical()` and `NotificationManager::postNormal()`,
immediately after the toast element is created and made visible via
`m_backend->setElementVisible(handle, true)`.

```cpp
// In NotificationManager::postCritical() / postNormal(), after toast element creation:
if (m_audio) {
    m_audio->playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f);
}
```

`UI_TOAST` = SoundId 23 (`ui_toast.wav`). Fires once per toast display (not once per queue
enqueue — only when the toast becomes visible on screen). If a toast is queued but not yet
visible (because the max simultaneous limit is reached), `ui_toast` does NOT fire until the
toast actually appears. `NotificationManager` holds `IAudioSystem* m_audio{nullptr}` injected
at construction (Phase 10 adds this parameter to the constructor alongside the existing
`IUIBackend*`, `ICitySimulation*`, and `IClock*`).

## HUD Class Structure

### Constructor Signature

```cpp
HUD(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)
```

All four parameters are stored as non-owning pointers. The `IAudioSystem*` parameter is stored as:

```cpp
IAudioSystem* m_audio{nullptr};
```

**Rationale**: Phase 9 HUD calls `m_audio->playSound(SoundId::UI_CLICK, ...)` for toolbar clicks and `m_audio->playSound(SoundId::UI_MENU_OPEN, ...)` for panel opens. Adding this dependency now prevents a Phase 9 header change to HUD that would force recompilation of UIManager and all tests that construct HUD directly.

**Phase 8 stub contract**: The Phase 8 stub body stores `m_audio` but never calls it. No audio calls are made in Phase 8. Phase 9 fills in the audio call sites. Tests that construct HUD in Phase 8 must supply a mock or null-safe stub for `IAudioSystem*`.

Required public methods of the `HUD` class:

```cpp
void setActiveToolLabel(const std::string& text);
```

Updates the active tool indicator text (virtual bounds x:8–72 px, y:752–784 px). Called by `UIManager::onEvent()` at Priority 5 when a toolbar tool button (Zone / Road / Utilities / Demolish / Query) is clicked. Sets the `m_activeToolLabel` element text via `m_backend->setElementText()`. Also called with `"No tool"` when Query mode is toggled off.

Required private members of the `HUD` class relevant to the budget detail overlay:

```cpp
BudgetDetailPanel* m_budgetDetail{nullptr}; // owned by HUD; shown on treasury balance hover; Phase 8 full implementation
```

> **Phase 8 stub requirement**: A companion stub class `src/ui/budget_detail_panel.h` MUST be created in
> Phase 8 with an empty constructor accepting `IUIBackend*` and a no-op `draw()` method. This prevents
> Phase 9 from requiring a header change to HUD (which would force recompilation of UIManager and all HUD
> consumers).

## Budget Detail Panel

BudgetDetailPanel is owned and drawn by HUD (not UIManager). UIManager does not hold a pointer to BudgetDetailPanel.

A floating detail panel that appears when the player hovers over or clicks the treasury balance display in the resource/budget bar.

- **Trigger**: hover or click on the treasury balance field in the resource bar
- **Dimensions**: approximately 320×200 px (virtual/scaled)
- **Anchor**: below the resource bar, left-aligned to the left edge of the resource bar
- **Z-order**: above all HUD elements; below the modal scrim (when a blocking modal is active, the budget detail panel is covered by the scrim)
- **Fields displayed** (named line items):
  - Tax revenue — Residential
  - Tax revenue — Commercial
  - Tax revenue — Industrial
  - Wages (city employee salaries)
  - Road maintenance
  - Service upkeep (fire, police, utilities)
  - Utility fees
  - Net monthly balance
- **Data refresh**: updates once per budget tick (not real-time between ticks)
- **Cross-reference**: see [Economy Model](../game-design/economy-model.md) for authoritative field definitions and calculation formulas

### Density Unlock Preview Tooltip

A supplemental line displayed within the budget detail panel (or as an additional line in the resource bar hover tooltip area) when the city is approaching a density tier unlock threshold.

- **Trigger**: appears when the current monthly revenue is within 10% of any locked density tier threshold (evaluated at the difficulty-adjusted threshold value). Not shown when all density tiers are already unlocked.
- **Display format**: "After Unlock: ~+$X/month expenses" where X is the estimated monthly upkeep increase resulting from the next density tier unlock
- **Placement**: shown as a supplemental line at the bottom of the budget detail panel, or as an additional line in the resource bar tooltip if the budget detail panel is not open
- **Update cadence**: updates once per budget tick (same as the rest of the budget detail panel)
- **Sentinel handling — all tiers unlocked**: The HUD calls `ICitySimulation::getNextUnlockThreshold(difficulty)` each budget tick to determine whether a threshold exists. When the return value equals `SimulationConstants::kNoUnlockThreshold` (`-1.0f`), the HUD MUST:
  1. Hide the density unlock progress indicator in the resource bar via `IUIBackend::setElementVisible(handle, false)` — the element is hidden (not merely disabled) because there is no actionable information to display.
  2. Suppress the Density Unlock Preview Tooltip entirely — the 10% proximity check MUST NOT execute when the sentinel is returned (guard the check with `if (threshold >= 0.0f)` before comparing against `getCurrentMonthlyRevenue()`).
  3. Never display the literal value `−1` or `−1.0` in any label — the sentinel MUST be intercepted before any formatting occurs.
  The suppression state is permanent for the session once all tiers are unlocked: there is no mechanism to re-lock a density tier, so once `kNoUnlockThreshold` is returned it will be returned for every subsequent tick. The HUD does not need to re-check after first suppression, but doing so is harmless.
- **Cross-reference**: See [Economy Model](../game-design/economy-model.md) (`getNextUnlockThreshold()` return semantics section) for the authoritative sentinel definition and the `SimulationConstants::kNoUnlockThreshold` named constant.

See also: [Tax Rate Panel](tax-rate-panel.md) — the floating panel for adjusting zone tax rates, accessible by clicking the resource/budget bar.

## Font Loading

Irrlicht's built-in default GUI font renders at approximately 8 physical pixels tall — unreadably
small at 1920×1080 and non-compliant with the 14 px virtual / 11 px physical minimums defined in
`resolution-ui-scaling.md` Typography section. `IrrlichtUIBackend` MUST load a custom font during
its constructor and set it as the default GUI environment font before any `addStaticText` or
`addButton` call creates an element.

**Required font asset**: `assets/fonts/hud_font.xml` — a bitmap font in Irrlicht's XML font
format (generated by Irrlicht's `makeFont` tool or equivalent). The font must be sized so that its
rendered glyph height at the minimum supported physical resolution (1280×720) is at least 11
physical pixels (the hard floor from `resolution-ui-scaling.md`). A font sized at 16 virtual pixels
maps to approximately 11 physical pixels at 1280×720 (scale factor ≈ 0.667), satisfying the
floor.

**Font loading implementation contract** (in `IrrlichtUIBackend` constructor, before element
creation begins):

```cpp
irr::gui::IGUIFont* hudFont = m_guiEnv->getFont("assets/fonts/hud_font.xml");
if (hudFont) {
    m_guiEnv->getSkin()->setFont(hudFont);
}
// If the font file is absent, Irrlicht silently falls back to its built-in default.
// Log a warning but do not assert — headless CI runs without assets.
```

**Phase assignment**: The font-loading constructor call (`getFont`/`setFont` code block above) was
delivered in **Phase 8** alongside the full `IrrlichtUIBackend` implementation, with graceful
fallback when the font file is absent (Irrlicht silently falls back to its built-in default; a
warning is logged but no assert fires). Phase 8 builds therefore already contain the loading code
but lack the font file, which is why Phase 9b builds exhibit the unreadable font symptom.
**Phase 10** delivers only the actual `assets/fonts/hud_font.xml` font file that makes the
graceful fallback succeed — no constructor code changes are required in Phase 10.

**Monospace requirement**: As specified in `resolution-ui-scaling.md` Typography, numeric readouts
(treasury balance, population, tax rates, percentages) must use a monospace typeface. **Decision
(Phase 10): Path 2 — two-font approach is selected.** `hud_font.xml` is a proportional sans-serif
face for labels, button text, tooltips, and panel titles. A second font file
`assets/fonts/hud_mono_font.xml` is a monospace face applied selectively to `IGUIStaticText`
elements that display numeric values. A single monospace face applied globally degrades legibility
in compact panels (Query/Inspector, Tax Rate Panel) where proportional glyphs are narrower and
allow more characters per line. The two-face approach allows each font to be optimised for its
role. Both font files must be delivered as Phase 10 assets.

**Implementation**: `IrrlichtUIBackend` loads `hud_mono_font.xml` in its constructor and stores
the result as `irr::gui::IGUIFont* m_monoFont{nullptr}`. Panel code applies the monospace font
by calling `m_backend->setElementMonoFont(handle)` immediately after each `addStaticText()` call
that creates a numeric element. `setElementMonoFont()` is method 19 on `IUIBackend`
(see `architecture/ui-ux/ui-manager.md` §IUIBackend Method Contract). `IrrlichtUIBackend`
implements it by calling `IGUIStaticText::setOverrideFont(m_monoFont)` on the looked-up element;
when `m_monoFont` is null (font file absent — graceful fallback), the call is a no-op and the
element keeps the environment default font. **Panel code MUST NOT cast `m_backend` to
`IrrlichtUIBackend*` to access `m_monoFont` directly** — the `static_cast` pattern breaks
unit tests that inject `MockUIBackend` (which is not an `IrrlichtUIBackend`) and produces
undefined behavior. Use `m_backend->setElementMonoFont(handle)` exclusively.

Numeric elements that MUST call `m_backend->setElementMonoFont(handle)` after `addStaticText()`:

- Treasury balance display in resource bar
- Population count display in resource bar
- All numeric fields in Tax Rate Panel (current rate, projected rate)
- All numeric fields in Budget Detail Panel (revenue, expense, and net balance line items)
- Density unlock progress threshold value in resource bar
- In-game date/time display in resource bar

Label text that uses `hud_font.xml` (do NOT call `setElementMonoFont()`):

- Zone type names, panel titles, button text, toolbar labels
- Notification toast body text
- Tooltip text
- Non-numeric status labels (e.g. "City Rating: Town", "Cost waiver: active")

## Toolbar Button Text Fallback — Known Rendering Artefact

**Symptom**: Players see small letter characters (e.g. "Zone", "Road", "Utils", "Demol",
"Query") in the upper-left area of the screen, overlapping the toolbar icon buttons. This is NOT a
misplaced text label — it is the **Irrlicht `IGUIButton` label text rendering as a fallback** when
the sprite sheet asset `assets/textures/ui/hud_sprites_ui.png` is not present on disk.

**Cause**: The five toolbar buttons (`m_btnZone`, `m_btnRoad`, `m_btnUtilities`, `m_btnDemolish`,
`m_btnQuery`) are created via `m_backend->addButton("Zone", ...)` etc. with text labels so they
have readable content during development before `hud_sprites_ui.png` is authored. Once the sprite
sheet is loaded by `IrrlichtUIBackend`, `setElementImage()` replaces the text with an icon.
However, until the sprite sheet file exists, Irrlicht renders the button label text in the
default (tiny) font at the button's physical position — which is the upper-left toolbar area at
approximately physical pixels (5–46, 43–250) on a 1280×720 window.

Note: Zone and Utilities **sub-panel** buttons are created with empty string labels
(`addButton("", ...)`). Only the five primary **toolbar** buttons carry text labels.

**Fix — two parts, both required**:

1. **Font size** (Phase 10): deliver the `assets/fonts/hud_font.xml` font file so the constructor
   call already present in `IrrlichtUIBackend` (Phase 8 deliverable) succeeds and the graceful
   fallback is no longer triggered. This makes the fallback text readable even before the sprite
   sheet is present.

2. **Sprite sheet asset** (Phase 10): author and place `assets/textures/ui/hud_sprites_ui.png`
   (the `hud_sprites_ui.dds` converted to PNG for Irrlicht's built-in texture loader, per
   `architecture/asset-standards/2d-texture-standards.md`). Once this file is present,
   `IrrlichtUIBackend::m_spriteTextureReady` becomes `true` and `setElementImage()` calls replace
   the fallback text with icons, eliminating the letter artefact entirely.

## Toolbar Button Label Abbreviations

The five primary toolbar buttons use **abbreviated labels** chosen to fit within the physical button
width at the minimum supported resolution (1280×720).

At 1280×720, toolbar buttons are 56×48 px virtual = approximately 37×32 px physical (scale factor
= 1280/1920 ≈ 0.667). The HUD font is 11 px physical (see `resolution-ui-scaling.md` — Bitmap Font
Physical Size), so the maximum characters per button width is approximately 37 ÷ 11 ≈ 3 characters
with full-width glyphs, or 5 characters with the narrower glyphs typical of DejaVu Sans 11 px.

**Authoritative toolbar button labels** (passed to `addButton()` as the text fallback):

| Tool | Label | Rationale |
|---|---|---|
| Zone | `"Zone"` | 4 chars — fits at 11 px physical |
| Road | `"Road"` | 4 chars — fits at 11 px physical |
| Utilities | `"Utils"` | Abbreviated from "Utilities" (9 chars too wide); 5 chars fits |
| Demolish | `"Demol"` | Abbreviated from "Demolish" (8 chars too wide); 5 chars fits |
| Query | `"Query"` | 5 chars — fits at 11 px physical |

**Do NOT use `"Utilities"` or `"Demolish"`** as button labels — these are too wide to render within
the physical button width at 1280×720. The abbreviated forms `"Utils"` and `"Demol"` are the
correct fallback labels and must be used in all code that calls `addButton()` for these two buttons.
The Utilities and Demolish tool names remain unabbreviated in tooltips, sub-panel headings, and all
other UI text.
