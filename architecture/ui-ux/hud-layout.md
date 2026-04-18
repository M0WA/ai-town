# HUD Layout

- Minimap (bottom-right corner, 200×200 px in virtual 1920×1080 space)
- Resource/budget bar (top bar, full width, virtual y: 0–56 px): treasury balance (`ICitySimulation::getTreasury()`), outstanding debt indicator (`getOutstandingDebt()` — hidden when debt is zero), City Rating label (`getCityRating()` → `CityRatingTier` display name), population count (`getPopulation()`), and current in-game date/time display. **Red flashing budget indicator**: when `ICitySimulation::getConsecutiveDeficitMonths() >= 2`, a red-tinted pulsing overlay (alpha oscillation 0.3–1.0 at ~1 Hz) activates on the treasury field, distinct from the standard deficit indicator. Removed immediately when `getConsecutiveDeficitMonths()` returns 0 (streak broken). Implementation: `UIManager::update()` polls `getConsecutiveDeficitMonths()` each frame and calls `setElementAlpha()` with a sine-wave value when active. See [Game Over Flow](../game-design/game-over-flow.md) for streak mechanics.
- Time controls and speed selector (top-right, virtual x: 1600–1796 px, y: 8–56 px): four mutually exclusive buttons — Pause (⏸), 1× (▶), 3× (▶▶), 10× (▶▶▶). Active button has accent-color border; inactive buttons have default border. Dispatches `ICitySimulation::setSpeed(SpeedMultiplier)` on click. **Polling contract**: `UIManager::update()` polls `ICitySimulation::getSpeedMultiplier()` every frame (not only on player input) and updates the active button highlight to match — this is required for auto-slow events from `CitySimulation::tick()` (deficit month-1 auto-slow to 1×) to be reflected without UI-layer polling gaps. **State matrix** (per [Notification System](notification-system.md)): speed buttons are disabled (grayed, non-interactive via `setElementEnabled(..., false)`) only when a blocking modal is active; the speed selector remains **enabled** during CRITICAL-toast-only auto-pause (no modal) so the player can pre-set speed for resume. See the authoritative state matrix in `notification-system.md` for all state combinations. See [Settings / Pause Menu](settings-pause-menu.md) for Escape → pause interaction.
- Notification/alert area (permanent right-side rail (x:1616–1908, top:64)); see Notification System for stacking rules
- Primary toolbar with tool modes: Zone, Road, Utilities, Demolish, Query — **left panel**, virtual bounds: x: **8–72 px** (8 px left margin so the toolbar does not clip at the window edge on non-16:9 resolutions and ultrawide aspect ratios), y: 64–600 px (below resource bar, above minimap area). Each tool mode is a 48×48 px icon button with 8 px padding. Active tool highlighted with accent-color border.

  **Toolbar y-range — visual bounds vs. input carve-out (MUST read before implementing):**

  > **WARNING: DO NOT use y:600 as an input gate threshold.** y:64–600 px is the visual icon group bounds only. All input arbitration MUST use `kToolbarBottom = 784` (defined in `ui_constants.h`). Using y:600 for the input gate leaves the undo button, demand bars, and active tool indicator exposed to accidental world-clicks. See `ui_constants.h` for the authoritative constant definition and `input-arbitration.md` Priority 5 for the enforcement point.

  - **y:64–600 px** is the **visual icon group bounds only** — the region where the five tool-mode icon buttons are drawn. This range describes geometry for *rendering* purposes only. It MUST NOT be used as an input gate boundary.
  - **`kToolbarBottom = 784`** (defined in `ui_constants.h`; see also `input-arbitration.md` Priority 5) is the **full input carve-out bottom edge**. Any mouse click with `virtual_y <= 784` **and** `virtual_x <= 72` is unconditionally consumed by the left-panel toolbar input handler, regardless of whether a visible toolbar element exists at that exact pixel. The carve-out intentionally extends below the visible icon group (from y:600 down to y:784) to cover the undo button (y:608–656), the demand bars (y:664–748), and the active tool indicator (y:752–784) — all of which are left-panel elements that would otherwise be missed if the gate used y:600.
  - **Phase 3 implementers MUST use `kToolbarBottom = 784` for the input gate.** Using y:600 is incorrect; it leaves the undo button, demand bars, and active tool indicator exposed to accidental world-clicks when the player's cursor is in the lower portion of the toolbar panel.
- **Undo button**: Positioned directly below the tool icon group within the left toolbar panel. Virtual bounds: x: 8–72 px, y: 608–656 px (immediately below the last tool icon, 8 px gap from y:600). A 48×48 px icon button labeled "↩ Undo" (or Ctrl+Z hotkey hint). Grayed out (via `IUIBackend::setElementEnabled(..., false)`) when no undo action is available or while a blocking modal is active. Disabled state must use `setElementEnabled` (non-interactive, grayed), not `setElementVisible` (hidden) — the button remains visible at all times. **Undo countdown**: When an undoable action is pending, the button label changes to "↩ Undo (expires in Xs)" where X is the real-time seconds remaining until the second budget tick expires the undo window, updated every real second. Text turns amber when `remainingSeconds < 5.0` or when the total undo window is ≤ 6 s (i.e., at 10× simulation speed where the second tick fires in ~6 real seconds — the countdown is amber from the moment the action is taken). Implementation: compute `remainingSeconds = secondBudgetTickTimeReal − IClock::nowSeconds()`; set amber if `remainingSeconds < 5.0 || totalWindowSeconds <= 6.0`. The countdown is absent when the button is grayed out (no pending undo). Requires `IClock` injection into the component responsible for rendering the undo button. See [Undo System](../game-design/undo-system.md) for the full undo expiry specification.
- **Unsaved changes indicator**: A dot (**16×16 px**, amber fill) placed immediately to the **left** of the notification bell icon at virtual bounds x: **1796–1812 px**, y: 8–24 px. **Must not overlap the bell icon** (bell occupies x: 1820–1868 px; the dot must end at x ≤ 1812 with at least an 8 px gap before the bell's x:1820 left edge). Appears whenever there are unsaved changes since the last manual save or auto-save. Hidden when game state matches last save. Tooltip on hover: "You have unsaved changes. Press Ctrl+S to save." 16×16 px minimum ensures the dot is visually distinct and hoverable without pixel-perfect accuracy. The dot and bell are mutually exclusive interactive targets — no shared coordinate range between them. **The dot must remain visible (not cleared) after a failed auto-save** — a failed auto-save does not constitute a successful save and must not clear the unsaved-changes state. The dot element has a fixed amber fill color authored at creation time via the backend — it does not change color dynamically. Visibility is controlled exclusively via `setElementVisible`. No `setElementColor` method is required on `IUIBackend` for V1; the 19-method `IUIBackend` interface (see `architecture/ui-ux/ui-manager.md` §IUIBackend Method Contract) is sufficient for the dot element. **Shared edge with time controls**: The unsaved changes dot begins at x:1796 (immediately adjacent to the time controls right edge at x:1796). This zero-gap edge-share is intentional — the dot and time controls share the boundary pixel. If this causes visual crowding at any supported DPI, shift the dot right by 8 px (x:1804) and the bell accordingly, updating both `hud-layout.md` and `ui-manager.md`.
- **Grace period indicator**: Virtual bounds: x: **80–1912 px**, y: 60–92 px (32 px height, directly beneath the resource/budget bar which occupies approximately y: 0–56 px). **Starts at x=80** (after the 64 px-wide toolbar column) so it does not visually overlap the left-side tool buttons. Displayed while the wall-clock grace period is active. Label: "Cost waiver: Xs remaining" (green text with clock icon from UI sprite sheet), where X is `floor(120 − IClock::nowSeconds() elapsed since game start)`, updated every real second. When fewer than 20 real seconds remain, the label text turns amber as a last-chance visual cue. On expiry, the indicator fades to alpha 0 over 0.5 real seconds via `setElementAlpha`, then is hidden via `setElementVisible(false)` — the grace period has ended and no countdown is needed. The 120 s duration is a wall-clock measurement and is unaffected by simulation speed — at 10× speed the same 120 real seconds apply. **Interactive tooltip**: On hover or click, shows: "During the grace period, road maintenance ($10/tile/month) and building upkeep costs are waived. These costs will begin in approximately Xs" (where X is the real-seconds countdown already shown in the indicator label — not a month count, as the grace period is real-time not month-aligned) "Estimated monthly upkeep when active: $[calculated from current city]." The estimated figure updates each real second as the player builds. Note: road tile placement cost ($500/tile) is NOT waived during the grace period — only ongoing maintenance and upkeep costs are waived. See [Economy Model](../game-design/economy-model.md) for the authoritative 120 s definition.
- **Demand pressure bar** (compact, anchored below undo button): virtual bounds x: 8–72 px, y: **664–748 px** (8 px visual gap from the undo button bottom edge at y:656). Zone-type labels ("R", "C", "I") occupy y: 664–692 px (h=28); colored bar columns occupy y: 692–748 px (h=56). Per-zone-type (R/C/I) unmet demand percentage indicator as three vertical bars. Updates each budget tick. (**Layout note**: the previous placement at y:608–688 px overlapped the undo button at y:608–656 px; moved down to y:664–748 px to eliminate the overlap.) **Low-resolution text legibility**: Each bar is labeled with a single-character zone-type indicator ('R', 'C', 'I') rendered above the bar column. At the virtual 64 px toolbar width, each of the 3 bars occupies ~20 px horizontally. The 'R', 'C', 'I' labels must use a **minimum 9-point font** (virtual space) at all supported resolutions to remain legible. If the UI scaling factor produces a rendered label below 9 virtual pixels high, the HUD must fall back based on the active accessibility mode:

  - **Standard mode** (colorblind mode OFF): Switch to a **numeric percentage tooltip** (shown on hover/tap only) rather than a persistent label — do not render sub-9-pixel text. This prevents illegible "blur spots" on low-resolution displays or when the demand bar is very compact. The tooltip remains supplemental information in this mode.
  - **Colorblind mode** (colorblind mode ON): The tooltip-only fallback is **NOT permitted**. Color is already an insufficient encoding in colorblind mode, so removing the persistent label would leave users with no reliable zone-type identification. Instead, the HUD MUST render the single-character zone-type symbol ('R', 'C', or 'I') at the hard physical floor of **11 px physical pixels** (per `resolution-ui-scaling.md` Typography hard physical floor — `UIScaler` clamps text scale to this minimum). Alongside the clamped label, the bar column MUST also display a **pattern or hatching overlay** (see `resolution-ui-scaling.md` Colorblind Accessibility section — "Demand pressure bar hatching patterns (colorblind mode)": Residential = diagonal hatching at 45°, Commercial = horizontal lines, Industrial = cross-hatch) so that zone type can be distinguished by pattern alone, independent of both color and the small label. The tooltip remains available as supplemental information but may not be the sole encoding in colorblind mode.

  **Data source**: `ICitySimulation::getZoneDemandFactor(ZoneType)` returns the city-wide weighted-average `demand_factor` across all tiles of that zone type, updated each budget tick. **DISPLAY SCALING**: `getZoneDemandFactor()` returns `float` in `[0.0, 1.0]` — NOT a percentage in `[0, 100]`. The HUD demand bars MUST multiply by `100.0f` before display (e.g., `barFillPct = getZoneDemandFactor(zone) * 100.0f`). Omitting this multiplication produces a bar always showing ≤1% fill. **INVERSE SEMANTICS**: `QueryResult::demandPressurePct` (Inspector Panel) uses the complementary definition `(1.0f − effective_demand_factor) × 100` where 100 = zero demand — opposite direction. Do NOT use `QueryResult::demandPressurePct` directly to fill the HUD demand bar. This is distinct from per-tile demand pressure available via `QueryResult::demand_pressure_pct` in the Query/Inspector Panel.
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

At construction, **all 9 buttons display the active-state sprite**
(`kSpriteZoneResLowActive + col + row*3`), so icons are visible as soon as the panel opens
before any player interaction. The default selection is Residential Low (row 0, col 0 —
`idx = 0`). When the player clicks a button, **all 9 buttons keep their active-state sprite** —
icons remain visible for every zone type at all times. The selected zone type and density tier are stored in
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

The Utilities sub-panel is a 2×2 grid of buttons that appears immediately to the right of the
left toolbar (x:80, y:176) whenever the Utilities tool is active. It is hidden for all other
tool modes. The top anchor is y:176 (aligned with the Utilities toolbar button row) — this
differs from the Zone sub-panel top anchor of y:64. Virtual bounds: x:80–276 px, y:176–276 px
(width = (96×2)+4 = 196 px, height = (48×2)+4 = 100 px). Grid layout: 2 columns × 2 rows —
**Row 0**: column 0 = Power Plant, column 1 = Water Tower; **Row 1**: column 0 = Fire Station,
column 1 = Police Station. Constants defined in `src/ui/ui_constants.h`: `kUtilSubPanelLeft`,
`kUtilSubPanelTop`, `kUtilSubBtnW`, `kUtilSubBtnH`, `kUtilSubBtnGap`.

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

At construction, **all 4 buttons display the active-state sprite**
(`kSpriteUtilPowerActive + typeIdx`), so icons are visible as soon as the panel opens.
The default selection is Power Plant (index 0, `ServiceBuildingType::PowerPlant`). When the
player clicks a different button, **all 4 buttons keep their active-state sprite** — icons
remain visible for every utility type at all times. The selected building type is
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

Highlight colours are hardcoded in `IrrlichtRenderer` based on the active tool mode.
`UIManager` does not pass colour values at the call site — the renderer determines colour
internally. The named constants below serve as the authoritative reference for what
colours the renderer uses for each tool; implementors must match these values inside
`IrrlichtRenderer`. Do not use inline literals — define named constants in
`src/ui/ui_constants.h` for use as implementation references.

| Active tool | ARGB value | Colour description | Named constant |
|---|---|---|---|
| Zone | `0x80FF00FF` | Semi-transparent magenta (alpha=128) | `kHoverArgbZone` |
| Road | `0x8000FFFF` | Semi-transparent cyan (alpha=128) | `kHoverArgbRoad` |
| Utilities | `0x80FF8000` | Semi-transparent orange (alpha=128) | `kHoverArgbUtilities` |
| Demolish | `0x80FF0000` | Semi-transparent red (alpha=128) | `kHoverArgbDemolish` |
| Query | `0x80FFFFFF` | Semi-transparent white (alpha=128) | `kHoverArgbQuery` |
| Blocked tile (any placement tool) | `0xBBFF2222` | Semi-opaque red (alpha=187, ≈73%) — shown when the hovered tile cannot receive the current placement (occupied by existing zone or road) | `kHoverArgbBlocked` |

Alpha value `0x80` = 128 = 50% opacity. This provides enough transparency to see terrain
geometry beneath the highlight, while remaining clearly visible against both light and
dark terrain surfaces. The highlight quad is rendered via
`IVideoDriver::drawMeshBuffer()` using `EMT_TRANSPARENT_ALPHA_CHANNEL` material type,
placed at terrain height +0.05 world units above the terrain surface (Z-fighting prevention).

**`kHoverArgbBlocked` alpha rationale**: `0xBB` = 187 ≈ 73% opacity — intentionally more
opaque than the standard `0x80` (50%) hover alpha used for the per-tool colours above.
The higher opacity makes the blocking intent visually distinct: the player immediately reads
the tile as "unavailable" rather than as a normal hover. The pure-red hue (`0xFF2222`)
reinforces the "cannot place here" meaning.

**`kHoverArgbBlocked` vs. Demolish tool red**: `kHoverArgbDemolish` uses `0x80FF0000`
(fully-saturated red, 50% alpha). `kHoverArgbBlocked` uses `0xBBFF2222` (slightly
desaturated warm red, 73% alpha). These two are visually distinct: the Demolish hover
signals "will destroy this tile", whereas the blocked hover signals "cannot place here —
tile is already occupied". The alpha difference (73% vs. 50%) is the primary differentiator
because the colours are close in hue; implementors must not reduce `kHoverArgbBlocked`'s
alpha to `0x80` as that would make the two states indistinguishable.

**Clear sentinel**: pass `tileX = -1`, `tileZ = -1` to
`IRenderer::setTileHoverHighlight()` to remove the highlight entirely (no active tool or
ray-cast miss). The `argb` parameter is not used in the new interface and must not be
passed. The sentinel value is `setTileHoverHighlight(-1, -1)` (no colour argument).
The legacy constant `kHoverArgbClear = 0x00000000u` is retained as a reference only.

**Interface change (Phase 11h)**: The `uint32_t argb` parameter has been removed from
`IRenderer::setTileHoverHighlight()`. The new signature is:

```cpp
virtual void setTileHoverHighlight(int tileX, int tileZ, int footprintSize = 1) = 0;
```

UIManager passes only `(tileX, tileZ, footprintSize)`. The renderer determines the
highlight colour internally from the active tool mode — specifically by reading
`m_activeTool` (or equivalent tool-mode state member) inside `IrrlichtRenderer`. The named
constants (`kHoverArgbZone`, `kHoverArgbDemolish`, `kHoverArgbBlocked`, etc.) are used as
implementation references inside `IrrlichtRenderer` and must not appear in UIManager code.

## Zone Colour Overlay — ARGB Colour Scheme

**Phase 11m supersession**: Phase 11m replaces the static zone overlay with a
demand-gradient overlay for **unbuilt** tiles only (zone placed, building not yet spawned).
The static constants below remain as canonical zone identification colours for minimap
coding and zone sub-panel button tints, but are **no longer used** to populate
`m_overlayMap` entries after Phase 11m. From Phase 11m onward:

- **Unbuilt zone tiles**: `UIManager::computeZoneOverlayColor()` generates demand-gradient
  ARGB with alpha=180 (lerp between light and dark zone tones; see
  `architecture/game-design/zoning-system.md §Unbuilt Zone Overlay Colors`).
- **Built zone tiles** (building spawned): overlay entry is **removed** — no overlay is
  rendered on tiles that have an active building.

---

ARGB values are encoded as `0xAARRGGBB`. These are the **canonical zone identification
colours** for minimap zone coding and zone sub-panel button active-state icon tints.

| Zone type | ARGB value | Colour description | Named constant |
|---|---|---|---|
| Residential | `0x6000FF00` | Semi-transparent green (alpha=96, ~38%) | `kOverlayArgbResidential` |
| Commercial | `0x600000FF` | Semi-transparent blue (alpha=96, ~38%) | `kOverlayArgbCommercial` |
| Industrial | `0x60FFFF00` | Semi-transparent yellow (alpha=96, ~38%) | `kOverlayArgbIndustrial` |

Alpha value `0x60` = 96 = approximately 38% opacity. This is deliberately lower than the
hover highlight alpha (0x80 = 50%) so that zone identification colours remain visually
recessive relative to active interaction cues.

**Overlay depth ordering**: Zone overlay quads use Y offset +0.1 world units above terrain
surface. Hover highlight quads use Y offset +0.05 world units. This ensures the hover
highlight always renders above the zone overlay when both are present on the same tile.

**Minimap consistency**: These same three ARGB values are used for zone coding in the
minimap top-down render (R=green, C=blue, I=yellow — consistent with the minimap spec in
`architecture/ui-ux/minimap.md`). Do not define separate colour constants for the minimap
zone coding; reuse `kOverlayArgbResidential`, `kOverlayArgbCommercial`, and
`kOverlayArgbIndustrial` from `ui_constants.h`.

## Minimap Overlay Toggles

Three toggle buttons below the minimap render area, added in Phase 11q13:

| Button  | Left edge (virtual) | Y     | Width | Height |
|---------|---------------------|-------|-------|--------|
| Map     | 1720                | 838   | 64    | 22     |
| Traffic | 1788                | 838   | 64    | 22     |
| Service | 1856                | 838   | 64    | 22     |

Total row width: 200 px (matches minimap width). Gap between top of row and minimap render
area: 4 px.

**Toggle button labels**: Uppercase strings `"MAP"`, `"TRAFFIC"`, `"SERVICE"` are passed to
`addButton()`. Irrlicht has no CSS `text-transform` equivalent — only the literal string
passed to `addButton()` is rendered. Title-case (`"Map"`, `"Traffic"`, `"Service"`) is
incorrect and must not be used.

**Active state**: background fill `rgba(4,20,56,92)` drawn via `fillColoredRect`. Border
`kMinimapToggleActiveBorder` = `0xA500C8F0` (`SColor(165,0,200,240)` = `rgba(0,200,240,0.65)`).
Text colour `#60C8E8` via `setElementTextColor`.
**Inactive state**: background fill `rgba(4,12,28,71)`. Text colour `rgba(140,180,220,0.55)`
via `setElementTextColorA`.
**Dot indicator**: 5×5 px filled circle drawn left of text label via `fillColoredRect`.

- Map dot: `#A8C8F0`
- Traffic dot: `#FF7050`
- Service dot: `#50D890`

**Chrome top rim**: 2 px `fillColoredRect` at y=838, using `kChromeRimColor` constants.
**Draw path**: All fills and rims are drawn in `UIManager::drawOverlays()` (post-drawAll step),
NOT in `HUD::draw()`.
**`setElementBackground` is NOT called on toggle buttons** (it is a no-op on IGUIButton
elements per §IUIBackend Method Contract method 18).

### Phase 11q13 V1 Appearance Subset

The following CSS effect from `hud-option-a-mercury.html` is deferred to **Phase 11q14**:

- `backdrop-filter: blur(...)` — requires RTT + GLSL Kawase blur shader (High effort);
  keeping current semi-transparent solid fill via `setElementBackground` for Phase 11q13

The following CSS effect is a permanent engine constraint (not implemented, not deferred):

- `box-shadow` — not supported by Irrlicht; no practical custom-rendering workaround
  at acceptable cost

The following ARE implemented in Phase 11q13:

- (a) Toggle button background fills via `fillColoredRect` (active/inactive)
- (b) Dot indicator via `fillColoredRect`
- (c) Chrome top rim via `fillColoredRect`
- (d) Active/inactive text colour via `setElementTextColor` / `setElementTextColorA`
  (button dispatch)
- (e) `kMinimapToggleActiveBorder` border render via `fillColoredRect` outline for active
  button
- (f) **`border-radius`** on panels and buttons — 9-slice sprite textures with pre-rendered
  rounded corners; `draw2DImage` composites 9 slices per panel; `drawNineSlice` helper in
  `IrrlichtUIBackend` (see Deliverable 13)
- (g) **CSS `letter-spacing`** — widened glyph `rect` values in bitmap font `.xml` files;
  data-only change, zero runtime cost (see Deliverable 14)
- (h) **Animated hover transitions** — alpha-lerp overlay elements per button; `update(dt)`
  interpolates overlay alpha via `setElementAlpha()`; smooth fade-in/out replaces discrete
  sprite-swap (see Deliverable 15)

## Visual Design — Glacier Glass + Silver Chrome

### Panel Backgrounds

All HUD panels use the Glacier Glass + Silver Chrome palette defined in
`architecture/ui-ux/resolution-ui-scaling.md` Visual Design — Glacier Glass + Silver Chrome section.

| Panel | Background |
|---|---|
| Resource/budget bar | `SColor(66, 4, 12, 28)` = `kGlacierPanelBg` = `0x42040C1C` — no corner radius |
| Left toolbar panel | `SColor(66, 4, 12, 28)` = `kGlacierPanelBg` — 10 px radius on right/bottom inner edges |
| Zone sub-panel | `SColor(66, 4, 12, 28)` = `kGlacierPanelBg` — 10 px radius on all inner edges |
| Utilities sub-panel | `SColor(66, 4, 12, 28)` = `kGlacierPanelBg` — 10 px radius on all inner edges |
| Budget detail panel | `SColor(66, 4, 12, 28)` = `kGlacierPanelBg` — 10 px radius |
| Grace period indicator | `SColor(66, 4, 12, 28)` = `kGlacierPanelBg` — 10 px radius |
| Notification card bg | `SColor(71, 4, 12, 30)` = `kNotifCardBg` = `0x47040C1E` — 10 px radius |

### Toolbar Icon States

Toolbar tool buttons (Zone, Road, Utilities, Demolish, Query) and all sub-panel buttons
use the icon state spec below. Both the inactive and active cells are separate sprites in
`hud_sprites_ui.png`; the backend call `IUIBackend::setElementImage()` switches between them.

| State | Icon style | Opacity | Border |
|---|---|---|---|
| Inactive | **Outlined — 2 px stroke, no fill** | 65% | None |
| Active | **Filled solid icon** | 100% | 2 px `rgba(0,200,255,0.76)` = `kActiveButtonBorderColor` + baked glow |

The teal border and glow are pre-baked into the active sprite cell. No runtime blur is
applied. The `kSpriteZone*Active`, `kSpriteUtil*Active`, etc. constants reference the
filled-icon cells; the inactive constants reference the stroke-only cells.

### Button Tile

All buttons in toolbar and sub-panels use the three-state tile:

- **Inactive**: `rgba(255, 255, 255, 0.08)` background, 1 px `rgba(255, 255, 255, 0.18)` border
- **Hover**: `rgba(255, 255, 255, 0.15)` background, 1 px `rgba(255, 255, 255, 0.35)` border
- **Active**: `kActiveButtonWashColor` = `rgba(0, 200, 255, 0.16)` cyan wash, 2 px `rgba(0,200,255,0.76)` = `kActiveButtonBorderColor` + 4 px baked glow

This replaces the earlier "accent-color border only" description for the active button state.
The teal wash + 2 px border is the single authoritative active-state signal.

### Text Colours in HUD

All HUD text follows the Glacier Glass + Silver Chrome palette (see `resolution-ui-scaling.md` canonical table):

| Content type | Colour |
|---|---|
| Numeric values (treasury, population, date, demand %) | `#F0B429` warm amber |
| Primary labels | `#D0E8F8` near-white (Glacier Glass) |
| Secondary / sub-labels | `rgba(180,210,240,0.58)` dim blue |
| Positive value (budget surplus, growth) | `#70E898` green |
| Deficit value | `#FF7870` red |
| Rating pill | `#8ECAFF` blue |
| Warning (undo countdown amber, grace period near-expiry) | `#E8960C` warning amber |
| Undo countdown text turns amber when `remainingSeconds < 5.0` | `#F0B429` (value amber, same token) |
| Chrome strip | `#E6F2FC` alpha ≈ 95% |

**Red flashing budget indicator** (consecutive deficit): the pulsing overlay on the treasury
field remains red. The ARGB for the overlay pulse is `0x80FF7870` (alpha 128, deficit red `#FF7870`)
in Irrlicht `SColor` format, replacing any previously unspecified red tint. The alpha
oscillates 0.3–1.0 via `setElementAlpha()` as described in the layout spec above; the colour
component itself is fixed at `#FF7870`.

**Unsaved-changes dot**: fixed amber fill `#F0B429` (value amber token). Authored at
creation time; does not change colour dynamically.

- **Pending rate change indicator**: displayed inside the resource/budget bar when `HUD::m_finances->hasPendingRateChange()` returns `true`. A small amber text label — color `#E8960C` (warning amber per `finances-panel.md`), HUD font size, text: "Tax rates updating next budget cycle". Positioned to the left of the notification bell, virtual bounds approximately x: 1360–1812 px, y: 22–42 px (centered vertically in the bar). Hidden when no pending rate change. `HUD::update(float dt)` polls `hasPendingRateChange()` each frame and calls `setElementVisible()` accordingly. Cross-reference: `architecture/ui-ux/finances-panel.md` — Pending rate change HUD indicator section for the full functional spec.

- **Notification log bell icon**: positioned at the right end of the resource/budget bar, virtual bounds x: 1820–1868 px, y: 8–56 px (48×48 px icon). Displays an unread-count badge (small numeral overlay) that increments when new notifications arrive and resets to zero when the log is opened. Keyboard shortcut: **B** (toggles the log open/closed; rebindable in Settings > Controls with standard conflict detection).

## Phase 10 Audio Wiring for UI Events

All UI SFX calls below use `m_audio->playSound(soundId, SoundPriority::HIGH, 1.0f)`.
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
    m_audio->playSound(UI_CLICK, SoundPriority::HIGH, 1.0f);
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
    m_audio->playSound(UI_MENU_OPEN, SoundPriority::HIGH, 1.0f);
}
```

Also fires when the Finances Panel opens (T key or resource-bar click). Call site: `FinancesPanel::open()` or the UIManager handler that calls it.

`UI_MENU_OPEN` = SoundId 24 (`ui_menu_open.wav`).

### `ui_menu_close` — Sub-panel or overlay panel closed

**Call site**: `UIManager::updateSubPanelVisibility()`, immediately after
`setElementVisible(panel, false)` is called when a sub-panel is hidden due to a tool change.
Also fires when the Finances Panel closes.

```cpp
// In UIManager::updateSubPanelVisibility(), after hiding a sub-panel:
if (m_audio) {
    m_audio->playSound(UI_MENU_CLOSE, SoundPriority::HIGH, 1.0f);
}
```

`UI_MENU_CLOSE` = SoundId 25 (`ui_menu_close.wav`).

**Guard**: Do NOT fire `ui_menu_open` and `ui_menu_close` on the same frame (i.e. when
`updateSubPanelVisibility()` hides one panel and shows another in the same tool-change event).
Implementation: call `ui_menu_close` first for any newly-hidden panels, then call
`ui_menu_open` once for any newly-shown panel. If no panel changes visibility state, no
sound fires.

### `ui_toast` — Notification toast displayed

**Call site**: `NotificationManager::refreshVisibleSlots()`, on the not-visible → visible
slot transition for each card (CRITICAL or Normal), rate-limited 150 ms via
`m_lastToastSoundTime`. Neither `postCritical()` nor `postNormal()` calls `playSound`
directly.

```cpp
// In NotificationManager::refreshVisibleSlots(), on slot not-visible → visible transition:
if (!slot.soundFired) {
    slot.soundFired = true;  // set UNCONDITIONALLY before rate-limit gate
    if (m_clock->nowSeconds() - m_lastToastSoundTime >= 0.150) {
        if (m_audio) m_audio->playSound(UI_TOAST, SoundPriority::HIGH, 1.0f);
        m_lastToastSoundTime = m_clock->nowSeconds();
    }
}
```

`UI_TOAST` = SoundId 23 (`ui_toast.wav`). Fires once per toast display (not once per queue
enqueue — only when the toast becomes visible on screen). If a toast is queued but not yet
visible (because the max simultaneous limit is reached), `ui_toast` does NOT fire until the
toast actually appears. `NotificationManager` holds `IAudioSystem* m_audio{nullptr}` injected
at construction. See `architecture/ui-ux/notification-system.md §Phase 10 audio call sites`
for the authoritative unified fire-on-display contract.

## HUD Class Structure

### Constructor Signature

```cpp
HUD(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)
```

All four parameters are stored as non-owning pointers:

```cpp
IAudioSystem* m_audio{nullptr};
IClock*        m_clock{nullptr};
```

`m_audio` is forwarded to `FinancesPanel` in Phase 11l and used directly for toolbar/panel audio calls from Phase 9 onward. `m_clock` is forwarded to `FinancesPanel` in Phase 11l (for key-repeat timing) and used directly by HUD for the undo-button countdown and grace-period indicator timing introduced in Phase 9.

**Rationale**: Phase 9 HUD calls `m_audio->playSound(UI_CLICK, ...)` for toolbar clicks and `m_audio->playSound(UI_MENU_OPEN, ...)` for panel opens. Adding this dependency now prevents a Phase 9 header change to HUD that would force recompilation of UIManager and all tests that construct HUD directly.

**Phase 8 stub contract**: The Phase 8 stub body stores `m_audio` but never calls it. No audio calls are made in Phase 8. Phase 9 fills in the audio call sites. Tests that construct HUD in Phase 8 must supply a mock or null-safe stub for `IAudioSystem*`.

Required public methods of the `HUD` class:

```cpp
void setActiveToolLabel(const std::string& text);
```

Updates the active tool indicator text (virtual bounds x:8–72 px, y:752–784 px). Called by `UIManager::onEvent()` at Priority 5 when a toolbar tool button (Zone / Road / Utilities / Demolish / Query) is clicked. Sets the `m_activeToolLabel` element text via `m_backend->setElementText()`. Also called with `"No tool"` when Query mode is toggled off.

```cpp
void notifyGameStarted();
```

Resets the grace-period state so the cost-waiver label appears correctly for both the first game and all subsequent new games. Must be called by `UIManager::transitionToGameplay()` each time a new game session begins. Internally, resets `m_gameStartTime` (stores current `IClock` time) and sets `m_gracePeriodExpired = false`. Phase 11m: fixes the bug where `m_gracePeriodExpired` was never reset for second games.

```cpp
void incrementNotificationBadge();
```

Increments the bell icon's unread-count badge overlay by 1. Called by `UIManager::incrementNotificationBadge()` (null-guarded via `if (m_hud)`) whenever `NotificationManager` collapses a Normal card to the notification log due to slot-overflow displacement. Updates the badge counter element text and ensures the badge overlay is visible. Phase 11q13 addition.

Required private members of the `HUD` class relevant to the budget detail overlay:

```cpp
FinancesPanel* m_finances{nullptr}; // owned by HUD; opened via T key or resource-bar click; Phase 8 full implementation
```

> **Phase 8 stub requirement**: A companion stub class `src/ui/FinancesPanel.h` MUST be created in
> Phase 8 with a constructor accepting `IUIBackend*` and a no-op `draw()` method. The full constructor
> signature `FinancesPanel(IUIBackend*, ICitySimulation*, IAudioSystem*, IClock*)` is introduced in
> Phase 11l when the panel implementation is completed. This prevents Phase 9 from requiring a header
> change to HUD (which would force recompilation of UIManager and all HUD consumers).

## Budget Detail Panel

**Budget Detail Panel removed**: The separate Budget Detail Panel has been merged into the Finances Panel (see `architecture/ui-ux/finances-panel.md`). Hovering over the treasury balance field no longer opens any panel. The Finances Panel is opened exclusively via the T key or a click on the resource/budget bar.

## Font Loading

AI Town HUD uses two typefaces baked into Irrlicht-compatible bitmap fonts:

- **Proportional sans-serif**: **Barlow Condensed** (weights 300, 400, 500, 600, 700)
  Source: `assets/fonts/src/BarlowCondensed-Light.ttf` through `BarlowCondensed-Bold.ttf`
  Baked XML: `assets/fonts/hud_font_<tier>.xml` (one per resolution tier: 720, 1080, 1440)
- **Monospace**: **Share Tech Mono** (Regular)
  Source: `assets/fonts/src/ShareTechMono-Regular.ttf`
  Baked XML: `assets/fonts/hud_mono_font_<tier>.xml` (one per resolution tier)

Fonts are baked via `python3 tools/generate_bitmap_fonts.py` from the TTF sources.
The proportional font is applied via `IGUIElement::setOverrideFont()` to all standard HUD labels.
The monospace font is applied to treasury/population/date values, undo countdown, and notification
timestamps.

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
in compact panels (Query/Inspector, Finances Panel) where proportional glyphs are narrower and
allow more characters per line. The two-face approach allows each font to be optimised for its
role. Both font files must be delivered as Phase 10 assets.

### Font Tier Assets

Three resolution tiers each have a proportional and a monospace font pair (six pairs total):

| File | Physical cell height | Tier |
|---|---|---|
| `assets/fonts/hud_font_720.xml` + `hud_font_720.png` | 22 px | 720p |
| `assets/fonts/hud_font_1080.xml` + `hud_font_1080.png` | 33 px | 1080p |
| `assets/fonts/hud_font_1440.xml` + `hud_font_1440.png` | 44 px | 1440p |
| `assets/fonts/hud_mono_font_720.xml` + `hud_mono_font_720.png` | 22 px | 720p |
| `assets/fonts/hud_mono_font_1080.xml` + `hud_mono_font_1080.png` | 33 px | 1080p |
| `assets/fonts/hud_mono_font_1440.xml` + `hud_mono_font_1440.png` | 44 px | 1440p |

The existing `assets/fonts/hud_font.xml` and `assets/fonts/hud_mono_font.xml` are retained as
the 720p-tier copies (symlinks or duplicates) for backwards compatibility with any hard-coded
references in Phase 8 code. All font atlas PNGs are exempt from the DDS-only runtime texture
rule — see `architecture/asset-standards/2d-texture-standards.md` § Font Atlas Exception.

**Implementation**: `IrrlichtUIBackend` loads both tier-specific fonts in its constructor:
`m_hudFont` from `hud_font_<tier>.xml` (e.g. `hud_font_720.xml` / `hud_font_1080.xml` /
`hud_font_1440.xml`) is applied immediately as the environment default via
`m_env->getSkin()->setFont(m_hudFont)`, so all subsequently created `IGUIStaticText` elements
inherit the correct proportional tier font without per-element font assignment.
`m_hudMonoFont` is loaded from `hud_mono_font_<tier>.xml` (e.g. `hud_mono_font_720.xml` /
`hud_mono_font_1080.xml` / `hud_mono_font_1440.xml`) and stored as
`irr::gui::IGUIFont* m_hudMonoFont{nullptr}` (renamed from `m_monoFont` in Phase 11g). Panel code applies the monospace font by calling
`m_backend->setElementMonoFont(handle)` immediately after each `addStaticText()` call
that creates a numeric element. `setElementMonoFont()` is method 19 on `IUIBackend`
(see `architecture/ui-ux/ui-manager.md` §IUIBackend Method Contract). `IrrlichtUIBackend`
implements it by calling `IGUIStaticText::setOverrideFont(m_hudMonoFont)` on the looked-up
element; when `m_hudMonoFont` is null (font file absent — graceful fallback), the call is a
no-op and the element keeps the environment default font. **Panel code MUST NOT cast
`m_backend` to `IrrlichtUIBackend*` to access `m_hudMonoFont` directly** — the `static_cast`
pattern breaks unit tests that inject `MockUIBackend` (which is not an `IrrlichtUIBackend`)
and produces undefined behavior. Use `m_backend->setElementMonoFont(handle)` exclusively.

Numeric elements that MUST call `m_backend->setElementMonoFont(handle)` after `addStaticText()`:

- Treasury balance display in resource bar
- Population count display in resource bar
- All numeric fields in Finances Panel (current rate, projected rate, budget line items)
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
