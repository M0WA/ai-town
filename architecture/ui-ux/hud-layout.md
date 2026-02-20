# HUD Layout

- Minimap (bottom-right corner, 200×200 px in virtual 1920×1080 space)
- Resource/budget bar + persistent debt indicator (top bar)
- Time controls and speed selector (top-right)
- Notification/alert area (top-center for transient toasts); see Notification System for stacking rules
- Primary toolbar with tool modes: Zone, Road, Utilities, Demolish, Query — **left panel**, virtual bounds: x: **8–72 px** (8 px left margin so the toolbar does not clip at the window edge on non-16:9 resolutions and ultrawide aspect ratios), y: 64–600 px (below resource bar, above minimap area). Each tool mode is a 48×48 px icon button with 8 px padding. Active tool highlighted with accent-color border.
- **Undo button**: Positioned directly below the tool icon group within the left toolbar panel. Virtual bounds: x: 8–72 px, y: 608–656 px (immediately below the last tool icon, 8 px gap from y:600). A 48×48 px icon button labeled "↩ Undo" (or Ctrl+Z hotkey hint). Grayed out (via `IUIBackend::setElementEnabled(..., false)`) when no undo action is available or while a blocking modal is active. Disabled state must use `setElementEnabled` (non-interactive, grayed), not `setElementVisible` (hidden) — the button remains visible at all times. **Undo countdown**: When an undoable action is pending, the button label changes to "↩ Undo (expires in Xs)" where X is the real-time seconds remaining until the second budget tick expires the undo window, updated every real second. Text turns amber when `remainingSeconds < 5.0` or when the total undo window is ≤ 6 s (i.e., at 10× simulation speed where the second tick fires in ~6 real seconds — the countdown is amber from the moment the action is taken). Implementation: compute `remainingSeconds = secondBudgetTickTimeReal − IClock::nowSeconds()`; set amber if `remainingSeconds < 5.0 || totalWindowSeconds <= 6.0`. The countdown is absent when the button is grayed out (no pending undo). Requires `IClock` injection into the component responsible for rendering the undo button. See [Undo System](../game-design/undo-system.md) for the full undo expiry specification.
- **Unsaved changes indicator**: A dot (**16×16 px**, amber fill) placed immediately to the **left** of the notification bell icon at virtual bounds x: **1796–1812 px**, y: 8–24 px. **Must not overlap the bell icon** (bell occupies x: 1820–1868 px; the dot must end at x ≤ 1812 with at least an 8 px gap before the bell's x:1820 left edge). Appears whenever there are unsaved changes since the last manual save or auto-save. Hidden when game state matches last save. Tooltip on hover: "You have unsaved changes. Press Ctrl+S to save." 16×16 px minimum ensures the dot is visually distinct and hoverable without pixel-perfect accuracy. The dot and bell are mutually exclusive interactive targets — no shared coordinate range between them. **The dot must remain visible (not cleared) after a failed auto-save** — a failed auto-save does not constitute a successful save and must not clear the unsaved-changes state. The dot element has a fixed amber fill color authored at creation time via the backend — it does not change color dynamically. Visibility is controlled exclusively via `setElementVisible`. No `setElementColor` method is required on `IUIBackend` for V1; the `IUIBackend` 14-method list is sufficient for the dot element.
- **Grace period indicator**: Displayed directly beneath the resource/budget bar while the wall-clock grace period is active. Label: "Cost waiver: Xs remaining" (green text with clock icon from UI sprite sheet), where X is `floor(120 − IClock::nowSeconds() elapsed since game start)`, updated every real second. When fewer than 20 real seconds remain, the label text turns amber as a last-chance visual cue. On expiry, the indicator fades to alpha 0 over 0.5 real seconds via `setElementAlpha`, then is hidden via `setElementVisible(false)` — the grace period has ended and no countdown is needed. The 120 s duration is a wall-clock measurement and is unaffected by simulation speed — at 10× speed the same 120 real seconds apply. **Interactive tooltip**: On hover or click, shows: "During the grace period, road maintenance ($10/tile/month) and building upkeep costs are waived. These costs will begin in approximately Xs" (where X is the real-seconds countdown already shown in the indicator label — not a month count, as the grace period is real-time not month-aligned) "Estimated monthly upkeep when active: $[calculated from current city]." The estimated figure updates each real second as the player builds. Note: road tile placement cost ($500/tile) is NOT waived during the grace period — only ongoing maintenance and upkeep costs are waived. See [Economy Model](../game-design/economy-model.md) for the authoritative 120 s definition.
- **Demand pressure bar** (compact, anchored below undo button): virtual bounds x: 8–72 px, y: **664–744 px** (8 px visual gap from the undo button bottom edge at y:656). Per-zone-type (R/C/I) unmet demand percentage indicator as three vertical bars. Updates each budget tick. (**Layout note**: the previous placement at y:608–688 px overlapped the undo button at y:608–656 px; moved down to y:664–744 px to eliminate the overlap.) **Low-resolution text legibility**: Each bar is labeled with a single-character zone-type indicator ('R', 'C', 'I') rendered above the bar column. At the virtual 64 px toolbar width, each of the 3 bars occupies ~20 px horizontally. The 'R', 'C', 'I' labels must use a **minimum 9-point font** (virtual space) at all supported resolutions to remain legible. If the UI scaling factor produces a rendered label below 9 virtual pixels high, switch to a **numeric percentage tooltip** (shown on hover/tap only) rather than a persistent label — do not render sub-9-pixel text. This prevents illegible "blur spots" on low-resolution displays or when the demand bar is very compact.
- **Active tool indicator** (persistent badge below demand bar): virtual bounds x: 8–72 px, y: 752–784 px. Shows the current tool's 32×32 px icon with a small text label, visible from anywhere on screen without looking at the toolbar. Updates immediately when the active tool changes. **Cursor shape**: each tool mode uses a distinct cursor shape from the UI sprite sheet (Zone: crosshair with zone-color tint; Road: road-segment icon; Utilities: wrench; Demolish: X marker; Query: magnifying glass).
- **Notification log bell icon**: positioned at the right end of the resource/budget bar, virtual bounds x: 1820–1868 px, y: 8–56 px (48×48 px icon). Displays an unread-count badge (small numeral overlay) that increments when new notifications arrive and resets to zero when the log is opened. Keyboard shortcut: **B** (toggles the log open/closed; rebindable in Settings > Controls with standard conflict detection).

## HUD Class Structure

### Constructor Signature

```cpp
HUD(IUIBackend* backend, IAudioSystem* audio, IClock* clock, ICitySimulation* sim)
```

All four parameters are stored as non-owning pointers. The `IAudioSystem*` parameter is stored as:

```cpp
IAudioSystem* m_audio{nullptr};
```

**Rationale**: Phase 5 HUD calls `m_audio->playSound(SoundId::UI_CLICK, ...)` for toolbar clicks and `m_audio->playSound(SoundId::UI_MENU_OPEN, ...)` for panel opens. Adding this dependency now prevents a Phase 5 header change to HUD that would force recompilation of UIManager and all tests that construct HUD directly.

**Phase 1 stub contract**: The Phase 1 stub body stores `m_audio` but never calls it. No audio calls are made in Phase 1. Phase 5 fills in the audio call sites. Tests that construct HUD in Phase 1 must supply a mock or null-safe stub for `IAudioSystem*`.

Required private members of the `HUD` class relevant to the budget detail overlay:

```cpp
BudgetDetailPanel* m_budgetDetail{nullptr}; // owned by HUD; shown on treasury balance hover; Phase 5 implementation
```

> **Phase 1 stub requirement**: A companion stub class `src/ui/budget_detail_panel.h` MUST be created in
> Phase 1 with an empty constructor accepting `IUIBackend*` and a no-op `draw()` method. This prevents
> Phase 5 from requiring a header change to HUD (which would force recompilation of UIManager and all HUD
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
