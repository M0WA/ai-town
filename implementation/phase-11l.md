## Phase 11l: Bug-Fix Batch — Terrain Stitching, Construction Delay, Ground Plates, Finances Panel, Notification Log Scrollbar, Load Button

**Status: OPEN**

### Goal

Six targeted bug fixes and UI improvements identified during visual QA:

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

6. **Load button does not work** — Clicking the Load Game button on the main menu does
   nothing. The button is correctly enabled by `main.cpp` when a save file exists, but
   the click is silently dropped because `MainMenuPanel` has no `m_loadGameRequested` flag
   and `UIManager::update()` never polls for or dispatches a load request.

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

- [ ] Verify that the **Boundary vertex four-chunk rule** paragraph exists in Step 3 of
  `setTileHeight()` in `architecture/graphics-architecture/procedural-terrain.md` and
  documents: for each modified tile coordinate `(mx, mz)`, chunks owning tiles
  `(mx, mz)`, `(mx-1, mz)`, `(mx, mz-1)`, `(mx-1, mz-1)` (clamped to map bounds)
  are each converted to a chunk ID via `chunkIdOf()`; the resulting IDs are
  deduplicated; and every unique chunk ID is marked `currentLOD = -1` and enqueued
  for rebuild.
- [ ] Verify that Step 1 of `setTileHeight()` documents that the height write must
  update both `m_generatedHeightmap` AND every `m_chunkHeightmaps` entry that covers
  `(tileX, tileZ)`, because that vertex is shared by up to four chunks.

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

**Test** (`tests/terrain/terrain_boundary_test.cpp`, label: `unit`):

- [ ] `TerrainSystem_SetTileHeight_AtChunkBoundary_BothChunksEnqueued`:
  construct a `TerrainSystem` with chunk size 4 and a 8×8 tile map; call
  `setTileHeight(4, 0, 5.0f)` (boundary between chunk 0 and chunk 1 in X);
  assert that the rebuild deque contains entries for both chunk IDs (chunk at
  `(0,0)` and chunk at `(1,0)`).
- [ ] `TerrainSystem_SetTileHeight_Interior_OnlyOwningChunkEnqueued`: call
  `setTileHeight(2, 2, 5.0f)` (interior tile, not on boundary); assert only one
  chunk ID in the deque.

**Implementation note**: Access the rebuild queue via the public `getPendingRebuildIds() const` accessor on `TerrainSystem` (documented in `architecture/graphics-architecture/procedural-terrain.md` Test API section). The accessor returns `std::vector<uint64_t>` — a deduplicated snapshot of chunk IDs scheduled for rebuild; order is unspecified.

- [ ] Create `tests/terrain/terrain_boundary_test.cpp` (new dedicated file, following the
  per-feature-per-file pattern of `terrain_chunk_test.cpp`, `terrain_flattening_test.cpp`, etc.).
  Register it in `CMakeLists.txt`:
  `target_sources(terrain_tests PRIVATE tests/terrain/terrain_boundary_test.cpp)`
  The `terrain_tests` target's `LABELS "unit"` property (set in Phase 1) propagates
  automatically — no additional label registration is required.

---

#### 2. Building construction delay

**Design**

When a zone tile is placed it enters `underConstruction = true` state. The building mesh
is not spawned immediately — it only appears once `populationTick()` evaluates demand as
sufficient (`effective_demand_factor >= construction_delay_demand_threshold = 0.50`).
This means buildings only appear when there is genuine need for them, not merely after
a fixed time delay.  The zone-colour overlay is visible immediately on placement; only
the 3D mesh is deferred.  While `underConstruction = true` the tile contributes
`population = 0` and no tax revenue.

**Spec update** (`architecture/game-design/zoning-system.md` — `## Construction Delay`
sub-section, already written):

- [ ] Verify `## Construction Delay` section exists and documents the demand gate
  (`effective_demand_factor >= construction_delay_demand_threshold` before
  `placeBuildingMesh()` is called and `underConstruction` is cleared).
- [ ] Add `construction_delay_demand_threshold = 0.50f` to `simulation_constants.h` as a
  dedicated constant for the construction delay gate.  Do **not** reuse
  `density_upgrade_wave_demand_threshold` for this purpose — that constant governs density
  upgrades (a separate mechanic) and the two thresholds may diverge in future phases.

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
      && effective_demand_factor >= SimulationConstants::construction_delay_demand_threshold) {
      tile.underConstruction = false;
      int zoneIdx = static_cast<int>(tile.zoneType);   // 0=Res, 1=Com, 2=Ind
      int tierIdx = static_cast<int>(tile.density);    // 0=Low, 1=Med, 2=High
      int& counter = m_buildingVariantCounters[zoneIdx * 3 + tierIdx];
      std::string baseName = buildingAssetBaseName(tile.zoneType, tile.density, counter);
      counter = (counter + 1) % 4;  // round-robin through 4 variants (01..04)
      m_renderer->placeBuildingMesh(tileX, tileZ, baseName);
  }
  ```

  This uses the `buildingAssetBaseName()` helper and the `m_buildingVariantCounters` round-robin
  array already defined in `CitySimulation` per `architecture/asset-standards/3d-model-standards.md`
  Variant Selection Policy. Do **not** add a `variantIndex` field to per-tile data.

  Tiles below the demand threshold remain as empty lots and are re-evaluated every
  subsequent tick.
- [ ] Serialise `underConstruction` in the save-file tile struct (Phase 12 save
  system must include it; add a `TODO(phase-12)` comment at the serialisation site).

**Test** (`tests/simulation/zoning_test.cpp`):

**Mock injection**: Tests inject `StrictMock<MockRenderer>` as the `IRenderer*` parameter to `CitySimulation`. `StrictMock` is required here because `placeBuildingMesh` presence/absence is the primary assertion being made — any unexpected call to `placeBuildingMesh` must be a hard test failure, not silently swallowed. For `ZoningSystem_PlaceZone_NoBuildingMeshAtPlacement`: use `EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(0)`. For `ZoningSystem_PlaceZone_BuildingMeshSpawnsWhenDemandSufficient`: configure demand to return ≥ 0.50 and use `EXPECT_CALL(m_renderer, placeBuildingMesh(_, _, _)).Times(1)`. The three arguments are `(int tileX, int tileZ, const std::string& assetBaseName)` per `IRenderer::placeBuildingMesh`; use `testing::_` matchers to match any baseName.

**Fixture note**: Create a separate fixture class `ZoningConstructionDelayTest` in the same `zoning_test.cpp` file, using `StrictMock<MockRenderer>` as the renderer mock. The existing `ZoningTestNice` fixture (which uses `NiceMock<MockRenderer>`) must NOT be modified — it continues to serve demand/zoning logic tests. The four construction delay tests below all use `ZoningConstructionDelayTest`.

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

**Test** (`tests/integration/irrlicht_renderer_flatten_test.cpp`, CMake label `integration`):

Both renderer-level flatten tests exercise `IrrlichtRenderer` with a `ManualTerrainQuery`
stub for the terrain query interface. However, `IrrlichtRenderer::placeBuildingMesh()`
creates Irrlicht scene nodes and requires an EDT_NULL Irrlicht device — these tests go in
`tests/integration/irrlicht_renderer_flatten_test.cpp` with CMake label `integration`. No display or
GPU is required; the EDT_NULL device suffices.  (The Deliverable 1 terrain stitching tests
`TerrainSystem_SetTileHeight_AtChunkBoundary_*` remain in
`tests/terrain/terrain_boundary_test.cpp` with CMake label `unit` — no change needed there.)

**ManualTerrainQuery**: Defined in `tests/simulation/ManualTerrainQuery.h` per testability-architecture.md. Configure non-uniform heights via `setHeightAt(x, z, h)` (Phase 11l extension). Records all `setTileHeight()` calls in `m_flattenCalls` (vector of `{x, z, h}` tuples, Phase 11l extension — see testability-architecture.md). Assert that all expected vertices appear in `m_flattenCalls` with the same `targetH` value.

- [ ] Register in `CMakeLists.txt`:
  `target_sources(integration_tests PRIVATE tests/integration/irrlicht_renderer_flatten_test.cpp)`
  Do NOT call `add_executable` or `aitown_add_tests` again — the `integration_tests` target
  already exists (Phase 10c+); re-calling either would cause a duplicate-target CMake error.
  The `integration_tests` target's `LABELS "integration"` property propagates automatically.
- [ ] Extend `tests/simulation/ManualTerrainQuery.h` (Phase 11l per `testability-architecture.md`): add `std::map<int64_t, float> m_tileHeights`, `std::vector<std::tuple<int,int,float>> m_flattenCalls`, implement `void setHeightAt(int x, int z, float h)`, and update `setTileHeight()` override to append `{x, z, h}` to `m_flattenCalls`
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
  - Construct with all required parameters:
    `m_finances = new FinancesPanel(m_backend, m_sim, m_audio, m_clock);`
    (`IAudioSystem* m_audio` and `IClock* m_clock` must be accessible in HUD — inject both via HUD constructor if not already present,
    following the same pattern as `NotificationManager` in `UIManager.cpp`.)
- [ ] Update `src/ui/UIManager.cpp` input handler: T key and resource-bar click now
  call `m_hud->toggleFinancesPanel()` (replacing old `toggleTaxPanel()`).
- [ ] `UI_MENU_OPEN` / `UI_MENU_CLOSE` SFX wiring: `FinancesPanel::open()` must fire via `m_audio->playSound(UI_MENU_OPEN, SoundPriority::HIGH, 1.0f)` and `FinancesPanel::close()` must fire via `m_audio->playSound(UI_MENU_CLOSE, SoundPriority::HIGH, 1.0f)`. `SoundPriority::HIGH` is required for all UI sounds per `source-pool.md` to ensure access to the transient reserve and prevent audio starvation during heavy traffic. The `FinancesPanel` constructor receives `IAudioSystem*` as a parameter to enable this. This matches the existing floating-panel audio pattern per `hud-layout.md`.
- [ ] Implement key-repeat rate cap: track `int m_holdDelta{0}` (cumulative delta for the
  current hold event) in `FinancesPanel`. In the button hold/repeat handler, before applying
  a ±1% increment, check `std::abs(m_holdDelta) < 5`; if the cap is reached, skip the
  increment. Reset `m_holdDelta = 0` on button-release (`onButtonReleased` event). This
  enforces the ±5 pp cap per hold event specified in `finances-panel.md`.
- [ ] Implement pending rate change indicator: add `bool hasPendingRateChange() const` and
  `void clearPendingRateChange()` to `FinancesPanel` (flag set when the player changes a tax
  rate, cleared when the next budget tick commits the change). In `HUD::update(float dt)`,
  poll `m_finances->hasPendingRateChange()` and show/hide the amber "Tax rates updating next
  budget cycle" label on the resource/budget bar accordingly (per `finances-panel.md`
  Pending rate change HUD indicator section).
- [ ] Update `CMakeLists.txt` (or the relevant source list): replace
  `TaxRatePanel.cpp` and `BudgetDetailPanel.cpp` with `FinancesPanel.cpp`.
- [ ] Update `CMakeLists.txt` test source list for `ui_tests`:
  - Add: `target_sources(ui_tests PRIVATE tests/ui/finances_panel_test.cpp)`
  - Remove: the `tests/ui/budget_detail_panel_test.cpp` line from target_sources
    (the BudgetDetailPanel test fixture is superseded by FinancesPanel tests).
  - Remove: the `tests/ui/tax_rate_panel_test.cpp` line from target_sources if present
    (the TaxRatePanel test fixture is also superseded).

**Tests** (`tests/ui/finances_panel_test.cpp`):

**Fixture**: `UIManagerFinancesPanelTest` uses `NiceMock<MockUIBackend>`, `NiceMock<MockCitySimulation>`, `NiceMock<MockAudioSystem>`, and `ManualClock`. This is an intentional combined fixture: UIManager instantiates FinancesPanel internally, so both layers share the same mock setup. `UIManager_*` tests exercise UIManager integration behavior; `FinancesPanel_*` tests exercise FinancesPanel behavior visible through that integration. No separate fixture file is required. `NiceMock<MockAudioSystem>` is the correct choice for this mixed fixture: integration tests like `UIManager_TKey_OpensFinancesPanel` trigger audio calls (FinancesPanel::open() fires `playSound(UI_MENU_OPEN)`) without asserting them, while `FinancesPanel_Open_FiresUIMenuOpenSound` and `FinancesPanel_Close_FiresUIMenuCloseSound` use `EXPECT_CALL` for explicit audio verification. NiceMock silences unexpected calls in integration tests while still enforcing `EXPECT_CALL` assertions in the audio-focused tests.

- [ ] Implement `void TearDown() override { m_uiManager.reset(); }` in `UIManagerFinancesPanelTest` to reset the `UIManager` smart pointer to `nullptr` before mock destruction, consistent with the destructor-path contract in testability-architecture.md.
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
  element bounds via `m_backend->setElementRect(m_logScrollThumb, 1856, thumbY, 12, thumbH)`;
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
  open log. Use `EXPECT_CALL(backend_, setElementRect(_, _, _, _, _)).WillRepeatedly(SaveArg<2>(&capturedY))`
  (or equivalent GMock argument capture) to record the Y argument of the last
  `setElementRect` call on the thumb element — in `setElementRect(handle, x, y, w, h)`,
  `y` is at position 2 (0-indexed), so `SaveArg<2>` captures the correct parameter;
  scroll down 5 entries via the mouse-wheel handler; assert the newly captured Y is
  greater than the initial Y.
- [ ] `NotificationManager_LogScrollbar_ThumbAtBottomAfterScrollToEnd`: scroll to
  the last entry; use `SaveArg<2>` capture for `y` (thumbY) and `SaveArg<4>` capture
  for `h` (thumbH) on `setElementRect(handle, x, y, w, h)` calls; assert
  `thumbY + thumbH ≈ trackTop + trackH` (i.e. `trackBottom`) within 1 px.
  Track bounds constants (trackTop=56, trackH=500) may be used directly.

---

#### 6. Load button fix

**Root cause analysis**

`MainMenuPanel::onEvent()` handles mouse clicks for New Game, Settings, and Quit
(lines 352–363 in `MainMenuPanel.cpp`) but **omits the Load Game hit-test entirely**.
The keyboard Enter handler (case 1) hits the button slot but returns `true` without
setting any flag.  No `m_loadGameRequested` member or `consumeLoadGameRequest()` method
exists in `MainMenuPanel`.  `UIManager::update()` polls only `consumeStartGameRequest()`
and therefore never calls `m_saveSystem->loadMostRecentSave()`.

No spec change is required — `architecture/ui-ux/main-menu-new-game-flow.md` already
correctly documents that clicking Load Game when a save is available triggers the
loading-screen path and calls `UIManager::onGameLoaded()` after deserialization.

**Code changes** (`src/ui/MainMenuPanel.h`, `src/ui/MainMenuPanel.cpp`,
`src/ui/UIManager.cpp`):

- [ ] **`MainMenuPanel.h`** — add public declaration and private member:

  ```cpp
  bool consumeLoadGameRequest();   // returns true once, then resets

  // in private:
  bool m_loadGameRequested{false};
  ```

- [ ] **`MainMenuPanel.cpp` — `onEvent()` mouse click handler** — add the missing
  hit-test for `m_btnLoadGame` inside the `Screen::MainMenu` block, guarded by
  `isElementEnabled()` (mirrors the existing disabled-guard pattern used for
  keyboard Enter path):

  ```cpp
  if (hitTest(mx, my, m_btnLoadGame) &&
      m_backend->isElementEnabled(m_btnLoadGame)) {
      m_loadGameRequested = true;
      return true;
  }
  ```

- [ ] **`MainMenuPanel.cpp` — Enter key handler (case 1)** — set the flag when the
  button is enabled (replaces the bare `return true`):

  ```cpp
  case 1: // Load Game
      if (m_backend->isElementEnabled(m_btnLoadGame))
          m_loadGameRequested = true;
      return true;
  ```

- [ ] **`MainMenuPanel.cpp` — `consumeLoadGameRequest()`** — implement as consume-once
  (same pattern as `consumeStartGameRequest()`):

  ```cpp
  bool MainMenuPanel::consumeLoadGameRequest() {
      if (!m_loadGameRequested) return false;
      m_loadGameRequested = false;
      return true;
  }
  ```

- [ ] **`UIManager.cpp` — `update()`** — after the `consumeStartGameRequest()` poll,
  add a load-game poll:

  ```cpp
  if (m_mainMenu->consumeLoadGameRequest()) {
      auto result = m_saveSystem->loadMostRecentSave();
      if (result == LoadResult::Ok) {
          transitionToGameplay(GameMode::Loaded);
          onGameLoaded();
      }
      // LoadResult::NoSaveFound should not occur here because the button
      // is only enabled when a valid save exists (set by setSaveAvailable()).
  }
  ```

**Test** (`tests/ui/ui_manager_test.cpp` and `tests/ui/main_menu_panel_test.cpp`,
label: `unit`):

**Fixture note**: Use the existing `UIManagerTest` fixture (which already injects
`MockSaveSystem`, `MockUIBackend`, and `ManualClock` with the `TearDown()` null-reset
contract per `testability-architecture.md`).  Use the existing `MainMenuPanelTest`
fixture for the panel-level test.

- [ ] `MainMenuPanel_LoadGame_ClickSetsFlag`: construct `MainMenuPanel` with a
  `MockUIBackend`, call `setSaveAvailable(true)` to enable the button, simulate a
  `MouseButtonDown` event hitting `m_btnLoadGame`'s rect, assert
  `consumeLoadGameRequest()` returns `true`, assert a second call returns `false`.

- [ ] `MainMenuPanel_LoadGame_ClickIgnoredWhenDisabled`: do NOT call
  `setSaveAvailable(true)` (button remains disabled), simulate the same click,
  assert `consumeLoadGameRequest()` returns `false`.

- [ ] `UIManager_LoadGame_CallsLoadMostRecentSave`: configure
  `MockMainMenuPanel::consumeLoadGameRequest()` to return `true` once; configure
  `MockSaveSystem::loadMostRecentSave()` to return `LoadResult::Ok`;
  call `update()`; assert `loadMostRecentSave()` was called exactly once and that
  `onGameLoaded()` side-effects are observable (e.g. `m_previousCityRating` seeded).

---

#### 7. GitHub release on merge to main

**Design**

Every push to `main` triggers a two-phase release process:

1. **Version bump**: the pipeline reads the latest `v*.*.*` git tag, increments the patch
   component, writes the new version back to `CMakeLists.txt` (`project(aitown VERSION
   X.Y.Z ...)`), commits the change with message `chore: bump version to vX.Y.Z [skip ci]`
   (the `[skip ci]` token prevents a re-trigger loop), and pushes the commit + new tag to
   `main`.  This means `CMakeLists.txt` is always kept in sync with the latest release tag
   without requiring manual version bumps in PRs.
2. **Release creation**: after packaging jobs succeed, a `release` job downloads the
   installer and `.deb` artifacts and publishes a GitHub release tagged `vX.Y.Z` with all
   packages attached.

**Spec update** (`architecture/ci-cd/github-actions-workflow.md` — `## release Job`
section, already added):

- [ ] Verify the `## release Job` section in `architecture/ci-cd/github-actions-workflow.md`
  reflects CI-calculated versioning (not CMake VERSION read from source) and the
  version-bump commit step.

**Code changes** (`.github/workflows/ci.yml`):

- [ ] Add a `bump-version` job with:
  - `if: github.event_name == 'push' && github.ref == 'refs/heads/main'`
  - `permissions: contents: write`
  - `timeout-minutes: 5`
  - Step: checkout with `fetch-depth: 0` (full history required for `git describe`)
  - Step: calculate next version and bump:

    ```bash
    # Get latest tag (default to v0.0.0 if none)
    CURRENT=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || echo 'v0.0.0')
    # Increment patch
    IFS='.' read -r MAJOR MINOR PATCH <<< "${CURRENT#v}"
    PATCH=$((PATCH + 1))
    NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
    # Update CMakeLists.txt
    sed -i "s/project(aitown VERSION [0-9.]*)/project(aitown VERSION ${NEW_VERSION})/" CMakeLists.txt
    # Commit and tag
    git config user.name  "github-actions[bot]"
    git config user.email "github-actions[bot]@users.noreply.github.com"
    git add CMakeLists.txt
    git commit -m "chore: bump version to v${NEW_VERSION} [skip ci]"
    git tag "v${NEW_VERSION}"
    git push --follow-tags
    echo "AITOWN_VERSION=${NEW_VERSION}" >> "$GITHUB_ENV"
    ```

- [ ] Add a `release` job with:
  - `if: github.event_name == 'push' && github.ref == 'refs/heads/main'`
  - `needs: [bump-version, package-windows, package-linux-deb]`
  - `permissions: contents: write`
  - `timeout-minutes: 10`
  - Step: checkout (to read the bumped `CMakeLists.txt` for the version)
  - Step: read `AITOWN_VERSION` from `CMakeLists.txt` (same grep/sed as above, but read-only)
  - Steps: `actions/download-artifact` (SHA-pinned) for each package artifact into
    `./release-assets/` (same 5 artifacts as before)
  - Step: `softprops/action-gh-release` (SHA-pinned at implementation time via
    `gh release view --repo softprops/action-gh-release --json tagName,targetCommitish`):

    ```yaml
    - name: Create GitHub release
      uses: softprops/action-gh-release@<40-CHAR-SHA>  # resolve at implementation time
      with:
        tag_name: v${{ env.AITOWN_VERSION }}
        name: "AI Town v${{ env.AITOWN_VERSION }}"
        fail_on_unmatched_files: true
        files: release-assets/**
    ```

- [ ] `bump-version` and `release` must NOT be in `all-checks-pass` `needs:` — consistent
  with packaging gate policy.

**No unit tests** — verified by a real push to `main`; unit tests are not applicable.

---

#### 8. Configurable terrain size (Small / Medium / Large)

**Design**

The New Game screen gains a **Map Size** row with three mutually exclusive radio buttons:
Small (128×128 tiles), Medium (512×512 tiles — default), Large (1024×1024 tiles).  The selected
size is passed to `TerrainSystem::generate(mapTilesX, mapTilesZ, ...)` at game start.
The V1 default of 512×512 is preserved as the default selection.

**Spec updates**:

- [ ] `architecture/ui-ux/main-menu-new-game-flow.md` — Add a **Map Size** row immediately
  after the Difficulty row.  Three buttons: `( ) Small`, `(*) Medium`, `( ) Large`.  Medium
  is the default selection.  Behaviour matches Difficulty radio buttons (mutual exclusion,
  radio labels update with `(*)`/`( )` prefix on selection, grayed-out buttons use
  `setElementEnabled(handle, false)`). Large is enabled in V1 (not grayed).
- [ ] `architecture/graphics-architecture/procedural-terrain.md` — Add a `## Map Size
  Presets` section defining the three presets: `kSmall = 128`, `kMedium = 512`,
  `kLarge = 1024` (tile counts, square maps; `mapTilesX == mapTilesZ`).  Document that
  `TerrainSystem::generate()` accepts arbitrary `mapTilesX`/`mapTilesZ` already; the
  presets are UI-facing aliases for those values.

**Code changes** (`src/ui/MainMenuPanel.h` / `.cpp`):

- [ ] Add `MapSize` enum to `MainMenuPanel.h`: `kSmall=128, kMedium=512, kLarge=1024`.
- [ ] Add three buttons in the New Game screen constructor after the Difficulty row (new
  `y += 44` row): `m_ngBtnSizeSmall`, `m_ngBtnSizeMedium`, `m_ngBtnSizeLarge`.
  Default text: `"( ) Small"`, `"(*) Medium"`, `"( ) Large"`. Add
  `m_selectedMapSize{MapSize::kMedium}` private member.
- [ ] Add these buttons to `hideAllElements()` and `showNewGameScreen()`.
- [ ] In `draw()` (New Game branch), update the three size button labels with `(*)`/`( )`
  based on `m_selectedMapSize` (same pattern as Difficulty).
- [ ] In `onEvent()` mouse-click branch for New Game: handle hits on the three size buttons,
  set `m_selectedMapSize` accordingly.
- [ ] Add `MapSize getSelectedMapSize() const { return m_selectedMapSize; }` accessor.

**Code changes** (`src/ui/UIManager.cpp` / `src/main.cpp`):

- [ ] When `consumeStartGameRequest()` returns `true`, read
  `m_mainMenu->getSelectedMapSize()` (cast to `int`) and pass it as both `mapTilesX` and
  `mapTilesZ` to `TerrainSystem::generate()`.

**Test** (`tests/ui/main_menu_panel_test.cpp`, label: `unit`):

- [ ] `MainMenuPanel_MapSize_DefaultIsMedium`: construct panel, assert
  `getSelectedMapSize() == MapSize::kMedium`.
- [ ] `MainMenuPanel_MapSize_ClickSmall_SetsSmall`: simulate click on `m_ngBtnSizeSmall`,
  assert `getSelectedMapSize() == MapSize::kSmall`.
- [ ] `MainMenuPanel_MapSize_ClickLarge_SetsLarge`: simulate click on `m_ngBtnSizeLarge`,
  assert `getSelectedMapSize() == MapSize::kLarge`.

---

#### 9. Migrate all diagnostic logging to Irrlicht logger

**Design**

All diagnostic output that currently writes to `stderr` or `stdout` via `fprintf`, `printf`,
or `std::cerr` must be routed through `irr::ILogger*` (obtained from `m_device->getLogger()`
and passed to each subsystem as a constructor or function parameter).

**Exemptions** (do NOT change these):

- `src/benchmark/benchmark_main.cpp` — intentional CLI stdout result output; exempt as a
  standalone tool.
- `std::snprintf`/`fprintf(file, ...)` calls used for string formatting or writing to named
  files (not stderr/stdout) — these are not terminal output.
- Pre-device fatal errors in `main.cpp` that fire before the Irrlicht device exists may
  still use `fprintf(stderr, ...)`.

**Irrlicht logger API**:

```cpp
irr::ILogger* logger = device->getLogger();
logger->log("message text", irr::ELL_INFORMATION);
logger->log("message text", irr::ELL_WARNING);
logger->log("message text", irr::ELL_ERROR);
```

**Spec update** (`architecture/graphics-architecture/irrlicht-device-lifecycle.md` —
Warning Log section):

- [ ] Replace the `std::fprintf(stderr, "WARNING: GL_MAX_TEXTURE_SIZE...")` example with:

  ```cpp
  device->getLogger()->log(
      "GL_MAX_TEXTURE_SIZE < 4096; loading fallback atlas buildings_atlas_d_2k.dds",
      irr::ELL_WARNING);
  ```

- [ ] Add a **Logging Policy** subsection to `irrlicht-device-lifecycle.md`:
  "All diagnostic output MUST use `irr::ILogger*` obtained from `m_device->getLogger()`.
  Pass the logger pointer to subsystems that need it (non-owning). Use `ELL_ERROR` for
  failures, `ELL_WARNING` for recoverable issues, `ELL_INFORMATION` for progress/status.
  `fprintf(stderr,...)`, `printf(...)`, and `std::cerr`/`std::cout` are prohibited in
  game runtime code (exemptions: benchmark CLI, pre-device fatal errors in `main.cpp`,
  string-formatting `snprintf`, and file I/O `fprintf(file,...)`). Tests that construct
  subsystems may pass `nullptr` as the logger; all logger calls MUST be guarded by
  `if (m_logger)` null checks."

**Code changes** (`src/audio/AudioSystem.h` / `.cpp`):

- [ ] Add `std::mutex m_logMutex` private member to `AudioSystem` to serialize audio-thread logger calls.
- [ ] Add `irr::ILogger* m_logger{nullptr}` private member to `AudioSystem`.
- [ ] Add `irr::ILogger* logger` parameter to the `AudioSystem` constructor as the **first**
  parameter, before `IClock* clock`. The parameter is **required** — it has no default. Pass
  `nullptr` in tests (silences log output without crashing) and `device->getLogger()` in production.
  The `IAlcFunctions* alcFunctions = nullptr` third parameter is **retained** — do NOT remove it.
  The complete new signature is:

  ```cpp
  explicit AudioSystem(irr::ILogger* logger, IClock* clock, IAlcFunctions* alcFunctions = nullptr);
  ```

  All existing test call sites MUST update to pass logger explicitly: `AudioSystem(nullptr, clock_ptr)`.
  The call in `tests/audio/audio_thread_test.cpp` that passes `(&m_clock, &m_mockAlc)` MUST update to
  `(nullptr, &m_clock, &m_mockAlc)`.
  Store logger as `m_logger = logger`.
- [ ] In `logWarning()`, `logError()`, `logInfo()`: replace `std::cerr` with:

  ```cpp
  void AudioSystem::logWarning(const std::string& msg) {
      if (m_logger) {
          std::lock_guard<std::mutex> lk(m_logMutex);
          m_logger->log(msg.c_str(), irr::ELL_WARNING);
      }
  }
  void AudioSystem::logError(const std::string& msg) {
      if (m_logger) {
          std::lock_guard<std::mutex> lk(m_logMutex);
          m_logger->log(msg.c_str(), irr::ELL_ERROR);
      }
  }
  void AudioSystem::logInfo(const std::string& msg) {
      if (m_logger) {
          std::lock_guard<std::mutex> lk(m_logMutex);
          m_logger->log(msg.c_str(), irr::ELL_INFORMATION);
      }
  }
  ```

- [ ] **Disjoint-use constraint**: `logWarning()`, `logError()`, and `logInfo()` MUST NOT be called from within any scope that holds `m_streamMutex` or `m_occlusionMutex`. If an error must be logged after detecting a problem inside a locked section, copy the error description to a local `std::string`, release the lock, then call the logging helper. This eliminates the circular-wait deadlock risk between `m_logMutex` and the two operational mutexes (per `audio-system.md` §Thread-Safety Design — m_logMutex subsection).
- [ ] **Audit existing call sites**: search `AudioSystem.cpp` for all `logWarning()`/`logError()`/`logInfo()` calls that appear inside a `lock_guard` or `unique_lock` scope (pattern: `lock_guard` followed by a log call in the same block). Refactor each violation using the copy-then-release pattern before the changes in this deliverable are considered complete.
- [ ] Update `main.cpp` / wherever `AudioSystem` is constructed: pass
  `device->getLogger()` as the **first** argument (before `IClock*`).
- [ ] Update all existing `AudioSystem` test instantiations: replace `AudioSystem(clock_ptr)`
  with `AudioSystem(nullptr, clock_ptr)` in every test file under `tests/` that constructs
  `AudioSystem` directly (search for `AudioSystem(` and audit each call site).
- [ ] `tests/audio/audio_thread_test.cpp`: update `AudioSystem(&m_clock, &m_mockAlc)`
  → `AudioSystem(nullptr, &m_clock, &m_mockAlc)` (IAlcFunctions injection call, NOT a clock-only
  call).

**Code changes** (`src/ui/key_bindings.h`):

- [ ] Add `irr::ILogger* logger = nullptr` parameter to `KeyBindings::load()`.
- [ ] Replace the two `fprintf(stderr, ...)` calls with:

  ```cpp
  if (logger) logger->log("...", irr::ELL_WARNING);
  ```

- [ ] Update all call sites of `KeyBindings::load()` to pass `device->getLogger()`.

**No new unit tests** — the behaviour of `logWarning`/`logError`/`logInfo` and
`KeyBindings::load()` is unchanged; only the output channel changes. Existing test call sites
that construct `AudioSystem(clock_ptr)` MUST be updated to `AudioSystem(nullptr, clock_ptr)` —
`irr::ILogger*` is the new required first parameter. Pass `nullptr` in tests. The null guard on
`m_logger` silences log calls (rather than crashing) when `nullptr` is passed.

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
| `architecture/game-design/save-system.md` | Add `LoadResult` enum (`Ok`, `NoSaveFound`, `Corrupted`) and `loadMostRecentSave()` API documentation for `ISaveSystem` (Deliverable 6) |
| `architecture/ci-cd/github-actions-workflow.md` | Add `## release Job` section; update versioning to CI-calculated patch-increment |
| `architecture/ci-cd/caching.md` | Add `softprops/action-gh-release` to SHA-pinning requirement list |
| `architecture/graphics-architecture/irrlicht-device-lifecycle.md` | Update Warning Log to use `device->getLogger()`; add Logging Policy subsection |
| `architecture/audio-architecture/audio-system.md` | Add constructor documentation: `irr::ILogger* logger` as first required parameter (no default; pass `nullptr` in tests, `device->getLogger()` in production; null-guard contract, test-nullptr safe); see Logging Policy in `irrlicht-device-lifecycle.md` |
| `architecture/ui-ux/main-menu-new-game-flow.md` | Add Map Size row (Small/Medium/Large) to New Game screen |
| `architecture/graphics-architecture/procedural-terrain.md` | Add `## Map Size Presets` section (kSmall=128, kMedium=512, kLarge=1024) |
| `architecture/game-design/minimum-viable-simulation.md` | Update map dimensions to the three configurable presets (128/512/1024) |

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
- [ ] Clicking the Load Game button on the main menu when a save file exists triggers the
  load sequence: `loadMostRecentSave()` is called, the game transitions to gameplay, and
  `onGameLoaded()` is called.
- [ ] Clicking the Load Game button when it is disabled (no save file) does nothing.
- [ ] Merging a PR to `main` triggers `bump-version` (auto-increments patch, commits updated
  `CMakeLists.txt`, pushes tag) and then `release` (creates a GitHub release tagged
  `vX.Y.Z` with the Windows `.exe` installer and one `.deb` per distro attached).
- [ ] The New Game screen shows a Map Size row; selecting Small/Medium/Large passes
  128/512/1024 as `mapTilesX`/`mapTilesZ` to `TerrainSystem::generate()`.
- [ ] No game runtime code calls `fprintf(stderr,...)`, `printf(...)`, `std::cerr`, or
  `std::cout` for diagnostic output; all such calls route through `irr::ILogger*`.
- [ ] No calls to `logWarning()`, `logError()`, or `logInfo()` appear inside a `lock_guard` or
  `unique_lock` scope protecting `m_streamMutex` or `m_occlusionMutex` in `AudioSystem.cpp`;
  the copy-then-release pattern is correctly applied at all such sites (verified by code review).
- [ ] Full unit test suite passes: 0 failures, no regressions.

---

### Sign-offs

| Role | Area | Status |
|---|---|---|
| `graphics-dev-irrlicht` | Terrain stitching chunk-boundary enqueue fix; multi-tile footprint flatten | ⬜ |
| `gamedesign-lookandfeel` | Construction delay (1-tick empty lot before mesh spawns) | ⬜ |
| `gamedesign-ux` | Finances Panel layout (360×520 px, two-section, dismiss rules); scrollbar visual spec; Load Game button UX | ⬜ |
| `test-dev-cpp` | New unit tests: terrain stitching, construction delay, multi-tile flatten, Finances Panel, scrollbar, load button | ⬜ |
| `cicd-dev-github` | GitHub release job: bump-version + release on push to main; attach Windows installer and Linux .deb packages | ⬜ |
| `gamedesign-ux` (Deliverable 8) | Map Size row in New Game screen (Small/Medium/Large) | ⬜ |
| `graphics-dev-irrlicht` + `sound-dev-opensoftal` (Deliverable 9) | Migrate `AudioSystem` and `KeyBindings` logging to `irr::ILogger*`; update spec | ⬜ |
