## Phase 11l: Bug-Fix Batch — Terrain Stitching, Construction Delay, Ground Plates, Finances Panel, Notification Log Scrollbar

**Status: OPEN**

### Goal

Five targeted bug fixes and UI improvements identified during visual QA:

1. **Terrain stitching holes** — After `setTileHeight()` is called (e.g. during zone/road
   placement), visible seams or "holes" appear at chunk boundaries. Root cause:
   `setTileHeight()` only enqueues the chunk that "owns" the modified tile, missing
   neighboring chunks that share the same boundary vertex.

2. **Immediate building construction** — Zone buildings spawn in the same frame the player
   places a zone tile. Buildings should only appear once the tile's zone-type demand is
   sufficient (`effective_demand_factor(zone, tick) >= 0.50`, using the per-zone-type demand
   factor for R, C, or I), evaluated each `populationTick()`. This applies to all three zone
   types. The empty-lot persists until demand is met — which may take many ticks if demand is
   low, or resolve after the first tick if demand is already high.

3. **Ground plate / terrain intersection** — Zone building ground plates intersect the
   terrain surface on multi-tile footprints (2×2 Medium, 3×3 High density). Root cause:
   `setTileHeight()` is called only for the 4 corners of the 1×1 origin tile, leaving the
   remaining footprint tiles un-flattened.

4. **Finances Panel** — The Tax Rate Panel (T key / resource-bar click) and the Budget
   Detail Panel (hover over treasury balance) are merged into a single **Finances Panel**.
   The Budget Detail Panel trigger is removed; the Tax Rate Panel is renamed "Finances" and
   extended with income/expense line items.

5. **Notification log scrollbar** — The notification log panel (B key / bell icon) has no
   visual scroll indicator. Mouse-wheel scrolling works internally but there is no thumb or
   track to communicate scroll position.

---

### Deliverables

#### 1. Terrain stitching fix

**Root cause analysis**

Each terrain chunk has its own heightmap copy (`m_chunkHeightmaps`).  Vertex
`(cx, cz)` is the *right/bottom edge* of the chunk to the left/above **and** the
*left/top edge* of the chunk that "owns" tile `(cx, cz)`.  Currently
`setTileHeight()`:

- Writes `m_generatedHeightmap[cx][cz]` ✅
- Writes only the `m_chunkHeightmaps` entry for the chunk that owns tile `(cx, cz)` ❌
- Enqueues only that one chunk for rebuild ❌

When `cx` or `cz` is a chunk-boundary coordinate, up to 3 additional adjacent chunks also
share that boundary vertex but are never updated (4 chunks total share the vertex), causing
a height discontinuity ("hole") in the rendered mesh.

**Spec update** (`architecture/graphics-architecture/procedural-terrain.md` —
`setTileHeight()` Step 3 — Chunk rebuild enqueue):

- [ ] Verify that the four-chunk boundary vertex rule is documented in Step 3 of
  `setTileHeight()` in `architecture/graphics-architecture/procedural-terrain.md`:
  "For each modified tile coordinate `(mx, mz)` (centre + up to 8 blended
  neighbours), determine the **set of all chunks** that include `(mx, mz)` as a
  vertex.  Vertex `(mx, mz)` is used by up to four chunks — the ones whose tile
  ranges include it as any corner: tiles `(mx, mz)`, `(mx-1, mz)`, `(mx, mz-1)`,
  `(mx-1, mz-1)` (clamp to map bounds).  Each of these tiles maps to a chunk ID via
  `chunkIdOf(tx, tz)`.  **All four chunk IDs** (deduplicated, valid, in-bounds) must
  be marked `currentLOD = -1` and enqueued for rebuild."
- [ ] Verify that the dual heightmap sync note in Step 1 documents:
  "The write MUST update `m_generatedHeightmap` AND every `m_chunkHeightmaps` entry
  that covers `(tileX, tileZ)`.  Because vertex `(tileX, tileZ)` is shared by up to
  four chunks, up to four `m_chunkHeightmaps` entries may require updating."

**Code fix** (`src/rendering/TerrainSystem.cpp` — `setTileHeight()`):

- [ ] In the chunk-enqueue loop, replace `chunkIdOf(mx, mz)` (single chunk) with a
  helper `affectedChunkIds(mx, mz)` that returns the deduplicated set of up to four
  chunk IDs derived from tile positions `{(mx,mz),(mx-1,mz),(mx,mz-1),(mx-1,mz-1)}`
  (each clamped to `[0, mapTilesX-1] × [0, mapTilesZ-1]` before ID conversion).
- [ ] Apply the same four-chunk expansion when syncing `m_chunkHeightmaps`: for each
  of the up to four chunks, write `height` (or the blended value) into the
  corresponding local offset within that chunk's heightmap copy.
- [ ] When writing to `m_chunkHeightmaps` for an affected chunk, compute the local
  offset as: `localX = tileX - chunkMinTileX`, `localZ = tileZ - chunkMinTileZ`,
  where `chunkMinTileX` and `chunkMinTileZ` are the origin tile coordinates of the
  chunk.  Write to heightmap at index `localZ * (chunkSize + 1) + localX`.
- [ ] Mark each affected chunk `currentLOD = -1` before enqueueing (existing guard
  in `processOneRebuild` skips the rebuild if LOD is already at target).

**Test** (`tests/terrain/terrain_tests.cpp`):

- [ ] `TerrainSystem_SetTileHeight_AtChunkBoundary_BothChunksEnqueued`:
  construct a `TerrainSystem` with chunk size 4 and a 8×8 tile map; call
  `setTileHeight(4, 0, 5.0f)` (boundary between chunk 0 and chunk 1 in X);
  assert that the rebuild deque contains entries for both chunk IDs (chunk at
  `(0,0)` and chunk at `(1,0)`).
- [ ] `TerrainSystem_SetTileHeight_Interior_OnlyOwningChunkEnqueued`: call
  `setTileHeight(2, 2, 5.0f)` (interior tile, not on boundary); assert only one
  chunk ID in the deque.

**Implementation note**: Access the rebuild queue via a public `getPendingRebuildIds() const` accessor on `TerrainSystem` (or equivalent test-visible accessor/friend class per testability-architecture.md). The deque should contain the deduplicated set of chunk IDs scheduled for rebuild.

---

#### 2. Building construction delay

**Design**

When a zone tile is placed it enters `underConstruction = true` state. The building mesh
is not spawned immediately — it only appears once `populationTick()` evaluates demand as
sufficient (`effective_demand_factor >= density_upgrade_wave_demand_threshold = 0.50`).
This means buildings only appear when there is genuine need for them, not merely after
a fixed time delay.  The zone-colour overlay is visible immediately on placement; only
the 3D mesh is deferred.  While `underConstruction = true` the tile contributes
`population = 0` and no tax revenue.

**Spec update** (`architecture/game-design/zoning-system.md` — `## Construction Delay`
sub-section, already written):

- [ ] Verify `## Construction Delay` section exists and documents the demand gate
  (`effective_demand_factor >= density_upgrade_wave_demand_threshold` before
  `placeBuildingMesh()` is called and `underConstruction` is cleared).

**Code changes**:

- [ ] Add `bool underConstruction{false}` to the per-tile data struct
  (`TileData` or equivalent in `src/simulation/CitySimulation.cpp`/`.h`).
- [ ] In `CitySimulation::placeZone()`: set `tile.underConstruction = true`.
  Remove (or guard) the existing `m_renderer->placeBuildingMesh()` call so it is
  **not** executed at placement time.
- [ ] In `CitySimulation::populationTick()`, after computing `effective_demand_factor`
  for a tile, add the demand-gated spawn:

  ```cpp
  if (tile.isZoned && tile.underConstruction
      && effective_demand_factor >= SimulationConstants::density_upgrade_wave_demand_threshold) {
      tile.underConstruction = false;
      std::string baseName = zoneAssetBaseName(tile.zoneType, tile.density);
      if (baseName.size() >= 2) {
          baseName[baseName.size() - 2] = '0';
          baseName[baseName.size() - 1] = static_cast<char>('0' + tile.variantIndex);
      }
      m_renderer->placeBuildingMesh(tileX, tileZ, baseName);
  }
  ```

  Tiles below the demand threshold remain as empty lots and are re-evaluated every
  subsequent tick.
- [ ] Serialise `underConstruction` in the save-file tile struct (Phase 12 save
  system must include it; add a `TODO(phase-12)` comment at the serialisation site).

**Test** (`tests/simulation/simulation_tests.cpp`):

**Mock injection**: Tests inject `NiceMock<MockRenderer>` as the `IRenderer*` parameter to `CitySimulation`. For `ZoningSystem_PlaceZone_NoBuildingMeshAtPlacement`: use `EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(0)`. For `ZoningSystem_PlaceZone_BuildingMeshSpawnsWhenDemandSufficient`: configure demand to return ≥ 0.50 and use `EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(1)`. The three arguments are `(int tileX, int tileZ, const std::string& assetBaseName)` per `IRenderer::placeBuildingMesh`; use `testing::_` matchers to match any baseName.

- [ ] `ZoningSystem_PlaceZone_NoBuildingMeshAtPlacement`: mock renderer,
  place zone, assert `placeBuildingMesh` **not** called in `placeZone`.
- [ ] `ZoningSystem_PlaceZone_BuildingMeshSpawnsWhenDemandSufficient`: place zone,
  configure simulation so `effective_demand_factor >= 0.50` on the first tick, call
  `tick()` once, assert `placeBuildingMesh` called exactly once.
- [ ] `ZoningSystem_PlaceZone_NoBuildingMeshWhenDemandInsufficient`: place zone,
  configure simulation so `effective_demand_factor < 0.50`, call `tick()` multiple
  times, assert `placeBuildingMesh` never called while demand stays below threshold.
- [ ] `ZoningSystem_PlaceZone_NoRevenueUntilMeshSpawned`: assert
  `getMonthlyRevenue()` reflects zero population for that tile while
  `underConstruction = true`.

---

#### 3. Ground plate terrain intersection — multi-tile footprint full flatten

**Root cause**

`placeBuildingMesh()` and `placeServiceBuildingMesh()` call `setTileHeight()` for the
4 corners of the **origin tile only** (a 1×1 vertex quad).  For a 2×2 Medium-density
building, the remaining 5 vertices of the 3×3 corner grid are left at their original
heights.  Terrain blending (`setTileHeight` neighbour propagation) partially levels
adjacent tiles but does not guarantee planarity across the full footprint, leaving
raised edges that intersect the building's ground plate.

**Spec update** (`architecture/game-design/terrain-interaction.md` — Phase 10b:
Buildings and service buildings — full flattening):

- [ ] Verify that the `### Multi-tile footprint extension` subsection exists after the 4-corner code block in `architecture/game-design/terrain-interaction.md` with the following content: "**Multi-tile footprint extension**: For
  buildings with an N×N footprint (N > 1), `setTileHeight()` must be called for
  **all `(N+1) × (N+1)` corner vertices** spanning the full footprint, not only the
  4 corners of the origin tile.  The target height `targetH` for the entire footprint
  is computed from the 4 outermost corners:

  ```text
  h_NW = getHeightAt(footX,   footZ)
  h_NE = getHeightAt(footX+N, footZ)
  h_SW = getHeightAt(footX,   footZ+N)
  h_SE = getHeightAt(footX+N, footZ+N)
  targetH = (h_NW + h_NE + h_SW + h_SE) * 0.25f
  ```

  Then call `setTileHeight(cx, cz, targetH)` for every `cx ∈ [footX, footX+N]` and
  `cz ∈ [footZ, footZ+N]`.  A single `flushTerrainRebuilds()` is called after all
  writes complete."

**Code fix** (`src/rendering/IrrlichtRenderer.cpp` —
`placeBuildingMesh()` / `placeServiceBuildingMesh()`):

- [ ] Replace the hardcoded 4-call flatten with a loop:

  ```cpp
  // footprintN = 1 (Low), 2 (Medium / service building), 3 (High)
  const float h_NW = m_terrain->getHeightAt(footX,       footZ);
  const float h_NE = m_terrain->getHeightAt(footX + footprintN, footZ);
  const float h_SW = m_terrain->getHeightAt(footX,       footZ + footprintN);
  const float h_SE = m_terrain->getHeightAt(footX + footprintN, footZ + footprintN);
  const float targetH = (h_NW + h_NE + h_SW + h_SE) * 0.25f;
  if (m_terrain) {
      for (int cz = footZ; cz <= footZ + footprintN; ++cz)
          for (int cx = footX; cx <= footX + footprintN; ++cx)
              m_terrain->setTileHeight(cx, cz, targetH);
      m_terrain->flushTerrainRebuilds();
  }
  ```

- [ ] Low-density buildings (footprintN = 1) still produce the same 4 calls as
  before (loop collapses to 4 iterations) — no behavioural change for Low density.
- [ ] `flushTerrainRebuilds()` is called **once** after the full loop, not once per
  iteration.

**Test** (`tests/integration/integration_tests.cpp`, CMake label `integration`):

Both renderer-level flatten tests exercise `IrrlichtRenderer` with a `ManualTerrainQuery`
stub for the terrain query interface. However, `IrrlichtRenderer::placeBuildingMesh()`
creates Irrlicht scene nodes and requires an EDT_NULL Irrlicht device — these tests go in
`tests/integration/integration_tests.cpp` with CMake label `integration`. No display or
GPU is required; the EDT_NULL device suffices.  (The Deliverable 1 terrain stitching tests
`TerrainSystem_SetTileHeight_AtChunkBoundary_*` remain in
`tests/terrain/terrain_tests.cpp` with CMake label `unit` — no change needed there.)

**ManualTerrainQuery**: Defined in `tests/simulation/ManualTerrainQuery.h` per testability-architecture.md. Configure non-uniform heights via `setHeightAt(x, z, h)` (Phase 11l extension). Records all `setTileHeight()` calls in `m_flattenCalls` (vector of `{x, z, h}` tuples, Phase 11l extension — see testability-architecture.md). Assert that all expected vertices appear in `m_flattenCalls` with the same `targetH` value.

- [ ] `IrrlichtRenderer_PlaceMediumBuilding_AllCornerVerticesFlattened`:
  Using `ManualTerrainQuery` with non-uniform heights, place a 2×2 building at
  `(2, 2)`.  Assert `setTileHeight` was called for all 9 vertices
  `{(2,2),(3,2),(4,2),(2,3),(3,3),(4,3),(2,4),(3,4),(4,4)}` with the same
  `targetH`.
- [ ] `IrrlichtRenderer_PlaceLowBuilding_FourCornersOnly`: place a 1×1 building at
  `(5, 5)`.  Assert `setTileHeight` called exactly 4 times (vertices `(5,5)`,
  `(6,5)`, `(5,6)`, `(6,6)`).

---

#### 4. Finances Panel (merge Budget Detail + Tax Rate Panel)

**Overview**

The Budget Detail Panel (triggered by hover/click on the treasury balance field) and the
Tax Rate Panel (T key / resource-bar click) are merged into a single **Finances Panel**.
The treasury-hover trigger is removed entirely.  The panel is accessible exactly as the
Tax Rate Panel was (T key or resource-bar click).

**Spec changes**

- [ ] **Rename** `architecture/ui-ux/tax-rate-panel.md` to
  `architecture/ui-ux/finances-panel.md`.  Update the title from "Tax Rate Panel" to
  "Finances Panel".
- [ ] In `architecture/ui-ux/finances-panel.md`:
  - Rename panel title label from "Tax Rates" to "Finances".
  - Expand panel dimensions from 300×200 px to **360×520 px** (virtual/scaled) to
    accommodate the budget breakdown section below the tax rows.
  - **Section 1 — Tax Rates** (top portion, unchanged from old Tax Rate Panel):
    three rows (R/C/I), `+1%`/`−1%` buttons, numeric readout, projected revenue
    change, pending-change indicator, "Tax changes cannot be undone" label.
  - **1 px horizontal separator rule** between Section 1 and Section 2.
  - **Section 2 — Budget** (bottom portion, equivalent to old Budget Detail Panel):
    - **Income** [$X,XXX/month] — bold header with subtotal.  Line items: Tax revenue
      (R/C/I), Utility fees, Tourism income: $0 (post-V1, grayed-out placeholder).
    - **1 px separator**.
    - **Expenses** [$X,XXX/month] — bold header with subtotal.  Line items: Road
      maintenance, Service upkeep, Wages.
    - **1 px separator**.
    - **Net monthly balance** — surplus in `#80C850` green, deficit in `#F04E37` red.
  - Dismiss rules unchanged from old Tax Rate Panel (T again, Escape, outside click).
  - Glass City visual style (deep-navy `rgba(13, 27, 42, 0.85)`, 8 px corner radius)
    applies to the full combined panel.
- [ ] In `architecture/ui-ux/hud-layout.md` — `## Budget Detail Panel` section (search for `BudgetDetailPanel` or `## Budget Detail Panel`):
  - Replace the current "Trigger: hover or click on treasury balance" section with a
    note: "**Budget Detail Panel removed**: The separate Budget Detail Panel has been
    merged into the Finances Panel (see `architecture/ui-ux/finances-panel.md`).
    Hovering over the treasury balance field no longer opens any panel.  The Finances
    Panel is opened exclusively via the T key or a click on the resource/budget bar."
  - Remove the `BudgetDetailPanel* m_budgetDetail` HUD private member note;
    replace with `FinancesPanel* m_finances`.
- [ ] Update `architecture/DOCUMENT_INDEX.md`: replace `tax-rate-panel.md` entry
  with `finances-panel.md` and description "Finances Panel (combined tax rates and
  budget breakdown)".
- [ ] Update `CLAUDE.md` Architecture File Links table: Tax Rate Panel row →
  `[Finances Panel](architecture/ui-ux/finances-panel.md)`.

**Code changes**:

- [ ] Create `src/ui/FinancesPanel.h` / `src/ui/FinancesPanel.cpp`:
  - Merges `TaxRatePanel` and `BudgetDetailPanel` into one class.
  - Constructor: `FinancesPanel(IUIBackend*, ICitySimulation*, IAudioSystem*, IClock*)`.
  - `open()` / `close()` / `isOpen() const` / `update(float dt)` methods.
  - `draw()` renders both sections in one pass.
  - `onEvent(const irr::SEvent&)` handles +/− clicks on tax rows and dismiss events.
- [ ] Remove `src/ui/TaxRatePanel.h` / `src/ui/TaxRatePanel.cpp` and
  `src/ui/BudgetDetailPanel.h` / `src/ui/BudgetDetailPanel.cpp` (their
  functionality is fully subsumed by `FinancesPanel`).
- [ ] Update `src/ui/HUD.h` / `src/ui/HUD.cpp`:
  - Replace `BudgetDetailPanel* m_budgetDetail` with `FinancesPanel* m_finances`.
  - Remove treasury-hover logic that was opening `m_budgetDetail`.
  - Delegate T key and resource-bar click to `m_finances->open()` /
    `m_finances->close()`.
- [ ] Update `src/ui/UIManager.cpp` input handler: T key and resource-bar click now
  call `m_hud->toggleFinancesPanel()` (replacing old `toggleTaxPanel()`).
- [ ] `ui_menu_open` / `ui_menu_close` SFX wiring: `FinancesPanel::open()` must fire `ui_menu_open` via `m_audio->playSound(UI_MENU_OPEN, SoundPriority::HIGH, 1.0f)` and `FinancesPanel::close()` must fire `ui_menu_close` via `m_audio->playSound(UI_MENU_CLOSE, SoundPriority::HIGH, 1.0f)`. `SoundPriority::HIGH` is required for all UI sounds per `source-pool.md` to ensure access to the transient reserve and prevent audio starvation during heavy traffic. The `FinancesPanel` constructor receives `IAudioSystem*` as a parameter to enable this. This matches the existing floating-panel audio pattern per `hud-layout.md`.
- [ ] Update `CMakeLists.txt` (or the relevant source list): replace
  `TaxRatePanel.cpp` and `BudgetDetailPanel.cpp` with `FinancesPanel.cpp`.

**Tests** (`tests/ui/finances_panel_test.cpp`):

**Fixture**: `UIManagerFinancesPanelTest` uses `NiceMock<MockUIBackend>`, `NiceMock<MockCitySimulation>`, `NiceMock<MockAudioSystem>`, and `ManualClock`. `TearDown()` must reset the `UIManager` smart pointer (`m_uiManager.reset()` if using `std::unique_ptr<UIManager> m_uiManager` per testability-architecture.md convention, or equivalent) to `nullptr` before mock destruction, consistent with the destructor-path contract in testability-architecture.md.

- [ ] `UIManager_TKey_OpensFinancesPanel`: press T, assert
  `FinancesPanel::isOpen() == true`.
- [ ] `UIManager_TKey_Twice_ClosesFinancesPanel`: open then press T again,
  assert `isOpen() == false`.
- [ ] `UIManager_ResourceBarClick_OpensFinancesPanel`: simulate resource-bar
  click, assert Finances Panel opens.
- [ ] `UIManager_TreasuryHover_NoPanel`: simulate hover over treasury balance
  field (old Budget Detail Panel trigger), assert no panel opens.
- [ ] `FinancesPanel_TaxRate_PlusButton_IncreasesRate`: click +1% on R row,
  assert `ICitySimulation::setTaxRate(ZoneType::Residential, oldRate + 1)` called.
- [ ] `FinancesPanel_Open_FiresUIMenuOpenSound`: open the Finances Panel; assert
  `MockAudioSystem::playSound(UI_MENU_OPEN, SoundPriority::HIGH, 1.0f)` called
  exactly once.  Use `EXPECT_CALL(audio_, playSound(UI_MENU_OPEN,
  SoundPriority::HIGH, 1.0f)).Times(1)` before triggering the open action.
- [ ] `FinancesPanel_Close_FiresUIMenuCloseSound`: open then close the Finances
  Panel; assert `playSound(UI_MENU_CLOSE, SoundPriority::HIGH, 1.0f)` called
  exactly once.  Use `EXPECT_CALL(audio_, playSound(UI_MENU_CLOSE,
  SoundPriority::HIGH, 1.0f)).Times(1)` before triggering the close action.
- [ ] Existing `TaxRatePanel` and `BudgetDetailPanel` test fixtures removed or
  replaced with `FinancesPanel` equivalents.

---

#### 5. Notification log scrollbar

**Design**

The notification log panel (400×500 px, last 50 entries) gains a 12 px wide vertical
scrollbar track on its right edge.  The content area shrinks by 12 px to 388 px wide.
The scroll thumb height and Y position are proportional to the fraction of entries
currently visible.

**Spec update** (`architecture/ui-ux/notification-system.md` — Notification Log Panel
section, after the "Scroll: mouse wheel scrolls the list" bullet):

- [ ] Verify that the **Scroll** bullet in the Notification Log Panel section of `architecture/ui-ux/notification-system.md` contains the full scrollbar specification (12 px track, thumb sizing formula, colour tokens, hide condition). The expected content is:
  "**Scroll**: Mouse wheel scrolls the list.  A 12 px vertical scrollbar is rendered
  on the right inner edge of the log panel (right of content, left of panel boundary).
  Virtual bounds of scrollbar track: x:1856–1868 px, y:56–556 px (500 px height).
  Content area width is reduced from 400 px to **388 px** to accommodate the
  scrollbar.  **Thumb dimensions**: `thumbH = max(20 px, floor(visibleRows /
  totalRows × trackH))`; `thumbY = trackTop + floor(scrollOffset / max(1,
  totalRows − visibleRows) × (trackH − thumbH))`.  **Track colour**: `rgba(255, 255,
  255, 0.08)` (same as inactive button fill).  **Thumb colour**: `rgba(255, 255, 255,
  0.25)` at rest; `rgba(255, 255, 255, 0.40)` on thumb hover (detected via
  `IGUIElement::isPointInside()`).  The scrollbar is hidden (`setElementVisible(false)`)
  when `totalRows ≤ visibleRows` (all entries fit without scrolling)."

**Code changes** (`src/ui/NotificationManager.cpp`, `NotificationManager.h`):

- [ ] Add private members to `NotificationManager`:

  ```cpp
  UIElementHandle m_logScrollTrack{UIElementHandle::invalid()};
  UIElementHandle m_logScrollThumb{UIElementHandle::invalid()};
  int m_logVisibleRows{0};   // computed at panel open from panel height / row height
  ```

- [ ] In `toggleLog()` (panel creation path):
  - Reduce content area width from 400 px to 388 px.
  - Create track element at x:1856–1868, y:56–556 via `m_backend->addStaticText`
    (or equivalent), colour `rgba(255,255,255,0.08)`.
  - Create thumb element sized `(12, thumbH)` and positioned at `(1856, thumbY)`.
  - Compute `m_logVisibleRows = floor(500 / kLogRowHeightPx)` (use the existing
    row-height constant).
- [ ] Extract `updateScrollThumb()` helper: recomputes `thumbH` and `thumbY` from
  `m_logScrollOffset`, `m_logEntries.size()`, and `m_logVisibleRows`; updates
  element bounds via `m_backend->setElementPosition()` / `setElementSize()`;
  calls `setElementVisible(m_logScrollTrack, totalRows > visibleRows)` and same
  for thumb.
- [ ] Call `updateScrollThumb()` from:
  - `toggleLog()` after creating the elements.
  - The mouse-wheel handler that adjusts `m_logScrollOffset`.
  - `postCritical()` / `postNormal()` when a new entry is added while the log is open.

**Test** (`tests/ui/notification_system_test.cpp`, label: `unit`):

**Fixture**: Extend the existing `NotificationManagerTest` fixture in `tests/ui/notification_system_test.cpp` (which already uses `NiceMock<MockUIBackend>`, `NiceMock<MockCitySimulation>`, `NiceMock<MockAudioSystem>`, and `ManualClock` with the correct `TearDown()` null-reset contract per testability-architecture.md). Add the four scrollbar test methods to this existing fixture. No new fixture class is needed.

- [ ] `NotificationManager_LogScrollbar_HiddenWhenAllFit`: post 3 entries (fewer
  than `m_logVisibleRows`), open log, assert scrollbar track element is invisible
  (`isElementVisible() == false`).
- [ ] `NotificationManager_LogScrollbar_VisibleWhenOverflow`: post 30 entries
  (more than visible rows), open log, assert scrollbar track visible.
- [ ] `NotificationManager_LogScrollbar_ThumbMovesOnScroll`: post 30 entries,
  open log, record initial `thumbY`, scroll down 5 entries, assert `thumbY`
  increased.
- [ ] `NotificationManager_LogScrollbar_ThumbAtBottomAfterScrollToEnd`: scroll
  to last entry, assert `thumbY + thumbH ≈ trackBottom` (within 1 px).

---

### Architecture File Changes Summary

| File | Change |
|---|---|
| `architecture/graphics-architecture/procedural-terrain.md` | Step 3 chunk-enqueue: expand to all 4 chunks sharing a boundary vertex; Step 1 dual-sync note expanded |
| `architecture/game-design/terrain-interaction.md` | Multi-tile footprint full-flatten rule; `(N+1)×(N+1)` corner loop |
| `architecture/game-design/zoning-system.md` | Add "Construction delay" sub-section |
| `architecture/ui-ux/tax-rate-panel.md` | **Rename** to `finances-panel.md`; title → "Finances"; expand to 360×520 px; add Section 2 Budget |
| `architecture/ui-ux/hud-layout.md` | Remove Budget Detail Panel hover trigger; update `m_budgetDetail` → `m_finances` |
| `architecture/ui-ux/notification-system.md` | Add scrollbar spec (12 px track, thumb sizing formula, colour tokens) |
| `architecture/DOCUMENT_INDEX.md` | `tax-rate-panel.md` → `finances-panel.md` |
| `CLAUDE.md` | Architecture File Links table: Tax Rate Panel → Finances Panel |

---

### Exit Criteria

- [ ] No visible terrain seams or holes appear at chunk boundaries after placing a zone
  or road tile on non-flat terrain; flattening propagates correctly across chunk edges.
- [ ] Placing a zone tile shows zone-colour overlay with no 3D building mesh for at least
  one budget tick; the building mesh appears only once `populationTick()` evaluates
  `effective_demand_factor >= 0.50` (the demand gate), meaning the empty-lot state may
  persist for multiple ticks if demand is below the threshold.
- [ ] Building ground plates do not visually intersect the terrain for Low, Medium, or
  High density buildings or service buildings, including at footprint edges.
- [ ] T key / resource-bar click opens the Finances Panel showing both tax rate controls
  (Section 1) and the full income/expense budget breakdown (Section 2).
- [ ] Hovering over or clicking the treasury balance field opens no panel
  (old Budget Detail Panel trigger is removed).
- [ ] The notification log panel displays a visible scrollbar track and thumb when the
  entry count exceeds the visible row count; the thumb position updates on scroll.
- [ ] Full unit test suite passes: 0 failures, no regressions.

---

### Sign-offs

| Role | Area | Status |
|---|---|---|
| `graphics-dev-irrlicht` | Terrain stitching chunk-boundary enqueue fix; multi-tile footprint flatten | ⬜ |
| `gamedesign-lookandfeel` | Construction delay (1-tick empty lot before mesh spawns) | ⬜ |
| `gamedesign-ux` | Finances Panel layout (360×520 px, two-section, dismiss rules); scrollbar visual spec | ⬜ |
| `test-dev-cpp` | New unit tests: terrain stitching, construction delay, multi-tile flatten, Finances Panel, scrollbar | ⬜ |
