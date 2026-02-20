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
- **Fields per entity type**:
  - Zone tile: demand score, desirability score, tax yield/month, zone type + density, demand pressure % (unmet demand percentage per zone type from the `demand_pressure_pct` field of `QueryResult`)
  - Road segment: current occupancy %, current speed, capacity, congestion status
  - Service building: coverage radius, current upkeep, service level %
- **Mutual exclusion with Tax Rate Panel**: QueryPanel and Tax Rate Panel must NOT be simultaneously open. Opening the QueryPanel closes the Tax Rate Panel if it is open. See `input-arbitration.md` Priority 3 for the authoritative mutual exclusion rule.
- Panel populated by a `QueryResult` data struct passed from the simulation layer to `UIManager`
- **Data refresh policy**: The QueryPanel refreshes its displayed data at different rates by data category to balance accuracy with performance:
  - **Budget/economy data** (tax yield, demand score, desirability): refreshed once per budget tick (same cadence as the simulation update).
  - **Traffic data** (road occupancy %, current speed, congestion status): refreshed every **10 simulation frames** (approximately every 167 ms at 60 FPS) — traffic state changes frequently enough that per-budget-tick updates would be stale, but per-frame updates are unnecessary.
  - **Service data** (coverage radius, upkeep, service level %): refreshed once per budget tick (coverage changes only on build/demolish or budget deficit events).
  - A small "Updated N seconds ago" line is displayed at the bottom of the panel showing the age of the most recently refreshed data category. If all categories are current within 1 s, this line is hidden.
- **Dismissed** by pressing I again, clicking elsewhere, or pressing **Escape**. The Escape key is **consumed by the QueryPanel** when the panel is open — it closes the QueryPanel and does NOT trigger the Pause Menu. This is enforced by the input arbitration priority order (see Input Arbitration spec). If the QueryPanel is closed, Escape passes through to `UIManager` which opens the Pause Menu. **Escape feedback** (prevents "why didn't the game pause?" confusion): When Escape closes the QueryPanel, display a brief Normal-queue toast (1.5 s, auto-dismiss, non-blocking): "Panel closed — press Escape again to open Pause Menu." This communicates to the player that their first Escape press was consumed by the panel and the simulation is still running. The toast must not appear if the player was already at speed=0 (paused) when they pressed Escape — in that case no "will not pause" feedback is needed since the simulation is already stopped. **The Escape-feedback toast is a passive display element — it must NOT intercept or consume any input events.** If the player presses Escape while the toast is still visible (within the 1.5 s display window), that Escape event passes through the toast to `UIManager` and opens the Pause Menu normally. The toast has no input priority in the arbitration chain; it is rendered on-screen but takes no ownership of events. Failing to enforce this produces a bug where the player tries to pause after the QueryPanel closes but the toast "absorbs" their Escape press and nothing happens.
