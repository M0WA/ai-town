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
- **Unsaved changes indicator**: A dot (**16×16 px**, amber fill) placed immediately to the **left** of the notification bell icon at virtual bounds x: **1796–1812 px**, y: 8–24 px. **Must not overlap the bell icon** (bell occupies x: 1820–1868 px; the dot must end at x ≤ 1812 with at least an 8 px gap before the bell's x:1820 left edge). Appears whenever there are unsaved changes since the last manual save or auto-save. Hidden when game state matches last save. Tooltip on hover: "You have unsaved changes. Press Ctrl+S to save." 16×16 px minimum ensures the dot is visually distinct and hoverable without pixel-perfect accuracy. The dot and bell are mutually exclusive interactive targets — no shared coordinate range between them. **The dot must remain visible (not cleared) after a failed auto-save** — a failed auto-save does not constitute a successful save and must not clear the unsaved-changes state. The dot element has a fixed amber fill color authored at creation time via the backend — it does not change color dynamically. Visibility is controlled exclusively via `setElementVisible`. No `setElementColor` method is required on `IUIBackend` for V1; the `IUIBackend` 17-method interface is sufficient for the dot element.
- **Grace period indicator**: Virtual bounds: x: 8–1912 px, y: 60–92 px (32 px height, directly beneath the resource/budget bar which occupies approximately y: 0–56 px). Displayed while the wall-clock grace period is active. Label: "Cost waiver: Xs remaining" (green text with clock icon from UI sprite sheet), where X is `floor(120 − IClock::nowSeconds() elapsed since game start)`, updated every real second. When fewer than 20 real seconds remain, the label text turns amber as a last-chance visual cue. On expiry, the indicator fades to alpha 0 over 0.5 real seconds via `setElementAlpha`, then is hidden via `setElementVisible(false)` — the grace period has ended and no countdown is needed. The 120 s duration is a wall-clock measurement and is unaffected by simulation speed — at 10× speed the same 120 real seconds apply. **Interactive tooltip**: On hover or click, shows: "During the grace period, road maintenance ($10/tile/month) and building upkeep costs are waived. These costs will begin in approximately Xs" (where X is the real-seconds countdown already shown in the indicator label — not a month count, as the grace period is real-time not month-aligned) "Estimated monthly upkeep when active: $[calculated from current city]." The estimated figure updates each real second as the player builds. Note: road tile placement cost ($500/tile) is NOT waived during the grace period — only ongoing maintenance and upkeep costs are waived. See [Economy Model](../game-design/economy-model.md) for the authoritative 120 s definition.
- **Demand pressure bar** (compact, anchored below undo button): virtual bounds x: 8–72 px, y: **664–744 px** (8 px visual gap from the undo button bottom edge at y:656). Per-zone-type (R/C/I) unmet demand percentage indicator as three vertical bars. Updates each budget tick. (**Layout note**: the previous placement at y:608–688 px overlapped the undo button at y:608–656 px; moved down to y:664–744 px to eliminate the overlap.) **Low-resolution text legibility**: Each bar is labeled with a single-character zone-type indicator ('R', 'C', 'I') rendered above the bar column. At the virtual 64 px toolbar width, each of the 3 bars occupies ~20 px horizontally. The 'R', 'C', 'I' labels must use a **minimum 9-point font** (virtual space) at all supported resolutions to remain legible. If the UI scaling factor produces a rendered label below 9 virtual pixels high, the HUD must fall back based on the active accessibility mode:

  - **Standard mode** (colorblind mode OFF): Switch to a **numeric percentage tooltip** (shown on hover/tap only) rather than a persistent label — do not render sub-9-pixel text. This prevents illegible "blur spots" on low-resolution displays or when the demand bar is very compact. The tooltip remains supplemental information in this mode.
  - **Colorblind mode** (colorblind mode ON): The tooltip-only fallback is **NOT permitted**. Color is already an insufficient encoding in colorblind mode, so removing the persistent label would leave users with no reliable zone-type identification. Instead, the HUD MUST render the single-character zone-type symbol ('R', 'C', or 'I') at the hard physical floor of **11 px physical pixels** (per `resolution-ui-scaling.md` Typography hard physical floor — `UIScaler` clamps text scale to this minimum). Alongside the clamped label, the bar column MUST also display a **pattern or hatching overlay** (see `resolution-ui-scaling.md` Colorblind Accessibility section — "Demand pressure bar hatching patterns (colorblind mode)": Residential = diagonal hatching at 45°, Commercial = horizontal lines, Industrial = cross-hatch) so that zone type can be distinguished by pattern alone, independent of both color and the small label. The tooltip remains available as supplemental information but may not be the sole encoding in colorblind mode.

  **Data source**: `ICitySimulation::getDemandPressurePct(ZoneType)` returns the city-wide weighted-average `demand_factor` across all tiles of that zone type, updated each budget tick. **DISPLAY SCALING**: `getDemandPressurePct()` returns `float` in `[0.0, 1.0]` — NOT a percentage in `[0, 100]`. The HUD demand bars MUST multiply by `100.0f` before display (e.g., `barFillPct = getDemandPressurePct(zone) * 100.0f`). Omitting this multiplication produces a bar always showing ≤1% fill. **INVERSE SEMANTICS**: `QueryResult::demandPressurePct` (Inspector Panel) uses the complementary definition `(1.0f − effective_demand_factor) × 100` where 100 = zero demand — opposite direction. Do NOT use `QueryResult::demandPressurePct` directly to fill the HUD demand bar. This is distinct from per-tile demand pressure available via `QueryResult::demand_pressure_pct` in the Query/Inspector Panel.
- **Active tool indicator** (persistent badge below demand bar): virtual bounds x: 8–72 px, y: 752–784 px. Shows the current tool's 32×32 px icon with a small text label, visible from anywhere on screen without looking at the toolbar. Updates immediately when the active tool changes. Icon sprite handles are drawn from `hud_sprites_ui.dds` row 6 (`kSpriteIndicatorNone` through `kSpriteIndicatorQuery` — see `src/ui/hud_sprite_ids.h` and `architecture/asset-standards/2d-texture-standards.md` UI Sprite Sheet Cell Layout section). **Cursor shape**: each tool mode uses a distinct cursor shape from the UI sprite sheet (Zone: crosshair with zone-color tint; Road: road-segment icon; Utilities: wrench; Demolish: X marker; Query: magnifying glass). OS-level cursor shape changes require `IUIBackend::setMouseCursor()`, which does not exist in the Phase 9b interface; cursor icon rendering from the sprite sheet is **deferred to Phase 10+** (see row 7 reserved cells in the sprite sheet layout). The `m_activeTool` state set in Phase 9b is the prerequisite for Phase 10+ cursor shape selection.

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

- **Notification log bell icon**: positioned at the right end of the resource/budget bar, virtual bounds x: 1820–1868 px, y: 8–56 px (48×48 px icon). Displays an unread-count badge (small numeral overlay) that increments when new notifications arrive and resets to zero when the log is opened. Keyboard shortcut: **B** (toggles the log open/closed; rebindable in Settings > Controls with standard conflict detection).

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
