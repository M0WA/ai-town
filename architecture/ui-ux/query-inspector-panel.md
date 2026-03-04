# Query / Inspector Panel

- Activated by Query tool; opens a floating panel **240×160 px** (virtual 1920×1080 space) anchored **40 px to the right and 40 px below** the cursor. **All cursor coordinates used in these formulas must be in virtual 1920×1080 space** (i.e., already un-projected from screen space via `UIScaler`). Using raw screen-space coordinates against virtual-space bounds will produce off-screen panels at non-1080p resolutions. Position computed as:

  ```text
  panel_x = clamp(cursor_x + 40, 0, 1920 − 240)
  panel_y = clamp(cursor_y + 40, 0, 1080 − 160)
  ```

  **Tile overlap prevention**: If the computed panel rect intersects the queried tile's screen bounding box (`tileBounds` is the queried tile's bounding box expressed in virtual 1920×1080 coordinate space, already un-projected via `UIScaler` before being passed to `computePanelPosition()`), try placing the panel in the opposite diagonal (above-left of cursor):

  ```text
  fallback_x = clamp(cursor_x − 40 − 240, 0, 1920 − 240)
  fallback_y = clamp(cursor_y − 40 − 160, 0, 1080 − 160)
  ```

  Both primary and fallback positions are clamped to screen bounds, ensuring the panel is always fully visible. This ensures the player can see the queried tile while reading the panel.
  **Third fallback (center-screen tiles where both primary and fallback overlap the tile)**:
  If both the primary and fallback panel positions still intersect the tile's screen bounding box after clamping, snap the panel to the nearest screen edge:

  ```text
  if primary overlaps tile AND fallback overlaps tile:
    edge_x = cursor_x <= 960 ? 1920 − 240 : 0   // right edge if cursor is in left half OR exactly at center (x=960); left edge if cursor is strictly in right half (x>960)
    edge_y = clamp(cursor_y − 80, 0, 1080 − 160)
    panel_position = (edge_x, edge_y)
  ```

  This three-step cascade (primary → fallback → edge-snap) guarantees the panel never overlaps the queried tile and is always fully on-screen.

## `computePanelPosition` Function Signature (Phase 9b)

`InspectorPanel::computePanelPosition` is a `static` pure-function that encapsulates the three-step
cascade described above. Its authoritative signature (as of Phase 9b) is:

```cpp
// src/ui/inspector_panel.h (public static method)
// Returns the panel's top-left position in virtual 1920×1080 space as a ScreenRect.
// cursorX, cursorY: cursor position in virtual 1920×1080 space (already un-projected via UIScaler).
// tileBounds: the queried tile's bounding box in virtual 1920×1080 space (already un-projected via UIScaler).
// The panel rect dimensions are fixed: w=240, h=160.
// ScreenRect is defined in IRenderer.h (struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; }).
static ScreenRect computePanelPosition(int cursorX, int cursorY, const ScreenRect& tileBounds);
```

**Usage by `UIManager`** (Phase 9b):

1. Convert physical cursor coordinates to virtual space via `UIScaler::unproject(physX, physY)`.
2. Obtain the tile's bounding box in physical pixels via `m_renderer->getTileScreenBounds(tileX, tileZ)`.
3. Un-project all four corners of the tile bounding box via `UIScaler::unproject()` to obtain
   `tileBounds_virtual` — a `ScreenRect` in virtual 1920×1080 space.
4. Call `InspectorPanel::computePanelPosition(cursorX_virtual, cursorY_virtual, tileBounds_virtual)`.
5. The returned `ScreenRect` gives the panel's top-left pixel position in virtual space; use
   destroy-and-recreate via `IUIBackend` to position the panel elements (no `setElementPosition`
   method exists on `IUIBackend`).

**Phase 8 note**: Phase 8 implemented a stub with signature
`static Rect computePanelPosition(int clickX, int clickY, int screenW, int screenH)`.
Phase 9b MUST update this signature to the above. The `screenW`/`screenH` parameters are no longer
needed — the edge-snap step derives virtual screen bounds from the fixed constants 1920 × 1080.
Existing Phase 8 pure-function tests must be updated to pass a `ScreenRect tileBounds` argument
instead of `screenW`/`screenH`; pass `ScreenRect{1000, 1000, 10, 10}` (off-screen — guaranteed
non-overlapping) to keep existing placement assertions valid.

- **Fields per entity type**:
  - Zone tile: demand score, desirability score, tax yield/month, zone type + density, demand pressure % (unmet demand percentage per zone type from the `demand_pressure_pct` field of `QueryResult`)
  - Road segment: current occupancy %, current speed, capacity, congestion status
  - Service building: coverage radius, current upkeep, service level %
- **Road tile detection via `QueryResult::isRoad`**: `ICitySimulation::queryTile()` sets
  `QueryResult::isRoad = true` for road tiles (`TileData::isRoad == true`). Road tiles have
  `isZoned = false`, so without the explicit `isRoad` check they would fall through to the
  "Unzoned" branch. The display priority is:

  ```text
  if result.isZoned  → show zone/density/population/coverage data
  else if result.isRoad → show "Road" label; traffic data fields populated in future phase
  else               → show "Unzoned"
  ```

  `QueryResult::isRoad` is declared in `src/interfaces/simulation_types.h` as `bool isRoad{false}`.
  `queryTile()` returns early after setting `isRoad = true` — no zone/population data is filled for
  road tiles. This is the V1 implementation; full road traffic fields (occupancy, speed, capacity,
  congestion) are deferred until the traffic system simulation phase.

- **Mutual exclusion with Tax Rate Panel**: QueryPanel and Tax Rate Panel must NOT be simultaneously open. Opening the QueryPanel closes the Tax Rate Panel if it is open. See `input-arbitration.md` Priority 3 for the authoritative mutual exclusion rule.
- Panel populated by a `QueryResult` data struct passed from the simulation layer to `UIManager`
- **Data refresh policy**: The QueryPanel refreshes its displayed data at different rates by data category to balance accuracy with performance:
  - **Budget/economy data** (tax yield, demand score, desirability): refreshed once per budget tick (same cadence as the simulation update). **Implementation note**: `ICitySimulation` does not expose a budget-tick counter, so the implementation uses a draw-frame count proxy — `kEconomyRefreshFrames = 120` draw frames (≈2 s at 60 FPS) — as an approximation of the budget tick cadence. The `m_lastEconomyFrame` member is initialised to `-kEconomyRefreshFrames` so the first `draw()` call always triggers an immediate refresh.
  - **Traffic data** (road occupancy %, current speed, congestion status): refreshed every **10 simulation frames** (approximately every 167 ms at 60 FPS) — traffic state changes frequently enough that per-budget-tick updates would be stale, but per-frame updates are unnecessary. Implemented as `kTrafficRefreshFrames = 10` draw frames.
  - **Service data** (coverage radius, upkeep, service level %): refreshed once per budget tick (coverage changes only on build/demolish or budget deficit events). Uses the same `kEconomyRefreshFrames = 120` frame proxy as economy data.
  - A small "Updated N seconds ago" line is displayed at the bottom of the panel showing the age of the most recently refreshed data category. If all categories are current within 1 s, this line is hidden. **Implementation note**: staleness is detected by comparing draw-frame counts using `kStalenessFrames = 60` (≈1 s at 60 FPS); the label is rendered via a `m_updatedLabel` `UIElementHandle`.
- **Dismissed** by pressing I again, clicking elsewhere, or pressing **Escape**. The Escape key is **consumed by the QueryPanel** when the panel is open — it closes the QueryPanel and does NOT trigger the Pause Menu. This is enforced by the input arbitration priority order (see Input Arbitration spec). If the QueryPanel is closed, Escape passes through to `UIManager` which opens the Pause Menu. **Escape feedback** (prevents "why didn't the game pause?" confusion): When Escape closes the QueryPanel, display a brief Normal-queue toast (1.5 s, auto-dismiss, non-blocking): "Panel closed — press Escape again to open Pause Menu." This communicates to the player that their first Escape press was consumed by the panel and the simulation is still running. The toast must not appear if the player was already at speed=0 (paused) when they pressed Escape — in that case no "will not pause" feedback is needed since the simulation is already stopped. **The Escape-feedback toast is a passive display element — it must NOT intercept or consume any input events.** If the player presses Escape while the toast is still visible (within the 1.5 s display window), that Escape event passes through the toast to `UIManager` and opens the Pause Menu normally. The toast has no input priority in the arbitration chain; it is rendered on-screen but takes no ownership of events. Failing to enforce this produces a bug where the player tries to pause after the QueryPanel closes but the toast "absorbs" their Escape press and nothing happens.
