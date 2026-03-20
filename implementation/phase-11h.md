## Phase 11h: Multi-Tile Building Footprints, Budget Screen Breakdown & Road Lane Design

**Status: Planned**

### Goal

Three independent but thematically related gameplay/visual improvements:

1. **Multi-tile building footprints** — low-density buildings occupy 1×1 tiles (unchanged);
   medium-density buildings occupy 2×2 tiles; high-density buildings occupy 3×3 tiles; service
   buildings occupy 2×2 tiles. This gives buildings physically meaningful size relative to the
   urban grid and makes density tier progression visually legible from the camera.

2. **Budget screen income/expense breakdown** — the Budget Detail Panel is reorganised into an
   explicit **Income** section, an **Expenses** section, and a **Total** (net balance) line at
   the bottom. Players can see at a glance whether a deficit comes from low income or high
   outgoings.

3. **Road lane geometry and center-line strip** — road tiles are narrowed to cover approximately
   ¾ of the 10 m tile width (~7.5 m carriageway), with a painted white center-line strip
   dividing the carriageway into two lanes. Vehicle agents drive on the left lane going in one
   direction and the right lane going the other direction (two-way traffic, keep-right convention).

---

### Deliverables

#### 1. Spec Updates — Building Footprints

##### 1a. `architecture/asset-standards/3d-model-standards.md`

- [ ] Add a **Multi-Tile Footprint** subsection immediately after the existing *Zone building
  footprint constraint* paragraph. Document the binding tile footprint by density tier:

  | Density tier | Tile footprint | World footprint |
  |---|---|---|
  | `low` (res/com/ind) | 1×1 tiles | 10 m × 10 m |
  | `med` (res/com/ind) | 2×2 tiles | 20 m × 20 m |
  | `high` (res/com/ind) | 3×3 tiles | 30 m × 30 m |
  | Service buildings | 2×2 tiles | 20 m × 20 m |

  (`graphics-artist-3d-model`)

- [ ] Document that **model-space authoring convention** remains ±2 m half-extent for all
  assets — the per-tier scale applied by `placeBuildingMesh()` changes:

  | Density tier | `setScale()` argument | Resulting world footprint |
  |---|---|---|
  | `low` | `kTileSize / 4.0f` = 2.5f | ±5 m = 10 m × 10 m |
  | `med` | `kTileSize * 2 / 4.0f` = 5.0f | ±10 m = 20 m × 20 m |
  | `high` | `kTileSize * 3 / 4.0f` = 7.5f | ±15 m = 30 m × 30 m |
  | Service (2×2) | `kTileSize * 2 / 4.0f` = 5.0f | ±10 m = 20 m × 20 m |

  Artists continue to author all building meshes in ±2 m local space. No re-export is required
  — only the C++ scale factor changes per density tier. (`graphics-artist-3d-model`)

- [ ] Clarify that **collision registration** and **simulation ownership** use the full tile
  footprint (2×2 or 3×3): all tiles in the footprint are marked as occupied and cannot
  receive an overlapping placement. The **origin tile** is the bottom-left corner
  (`tileX, tileZ`) of the footprint; the placed scene node's world origin is the center of the
  full footprint:

  ```text
  worldX = (tileX + (footprintW - 1) * 0.5f) * kTileSize
  worldZ = (tileZ + (footprintH - 1) * 0.5f) * kTileSize
  ```

  For a 1×1 building `worldX = tileX * kTileSize` (unchanged). (`graphics-dev-irrlicht`)

- [ ] Document that **road adjacency** for a multi-tile building requires at least one road tile
  edge-adjacent (4-directional cardinal) to any of the tiles in the footprint — not only the
  origin tile. (`gamedesign-lookandfeel`)

##### 1b. `architecture/game-design/zoning-system.md`

- [ ] Add a **Multi-Tile Footprint Placement Rules** section:
  - Before placing a zone tile at `(tileX, tileZ)` with a 2×2 or 3×3 footprint, the
    simulation checks that **all tiles in the footprint** are empty (not road, not another
    building, not out-of-bounds). If any tile is occupied or out-of-bounds, placement is
    rejected and the player is shown a toast: "Not enough space for [tier] zone".
  - On demolish: all tiles in the footprint are freed simultaneously.
  - **Density upgrade (Low→Med or Med→High)**: the upgraded building's footprint expands
    into tiles that were previously empty or occupied by neighbouring low-tier buildings of
    the **same zone type**. The upgrade resolution order is:
    1. Compute the new N×N footprint centred on the upgrading building's origin tile.
    2. For each tile in the expanded footprint that is currently occupied by a
       **same-zone-type, lower-density neighbour** (e.g. a `res_low_*` building when the
       upgrading tile is upgrading to `res_med`), that neighbour is **automatically
       demolished** to make room. The demolish is free (no refund, no undo window) and a
       NORMAL-priority toast is shown: "Neighbouring [zone] building cleared for upgrade".
    3. If any tile in the expanded footprint is occupied by a **road**, a **different
       zone type**, a **service building**, or is **out-of-bounds**, the upgrade is
       **deferred** — same-type neighbours are NOT demolished preemptively for a blocked
       upgrade. A deferred upgrade retries on subsequent ticks until clear space is
       available or **12 ticks** pass (then the upgrade is cancelled for that tile only
       and a CRITICAL toast is shown: "Upgrade blocked — clear surrounding tiles").
    4. After demolishing same-type neighbours and confirming the expanded footprint is
       clear, all N×N tiles are marked occupied by the upgraded building.
  (`gamedesign-lookandfeel`)

- [ ] Document the **hover highlight** rule: when the player hovers over a zone tile with the
  Zone tool active, the highlight overlay covers the full footprint of the tier selected in
  the Zone sub-panel (1×1, 2×2, or 3×3), not just the hovered tile. (`gamedesign-ux`)

##### 1c. `architecture/graphics-architecture/scene-graph-ownership.md`

- [ ] Update `placeBuildingMesh()` contract: add a `DensityTier` parameter (or derive tier from
  `assetBaseName` prefix) so the scene-graph layer can compute the correct `setScale()`
  factor. Document the per-tier scale table. (`graphics-dev-irrlicht`)

- [ ] Update `placeServiceBuildingMesh()` contract: service buildings now use the 2×2
  footprint with `setScale(5.0f)`. The world origin is the center of the 2×2 footprint
  (see origin formula above). (`graphics-dev-irrlicht`)

#### 2. Spec Updates — Budget Screen Income/Expense Breakdown

##### 2a. `architecture/ui-ux/hud-layout.md` — Budget Detail Panel

- [ ] Redesign the **Fields displayed** list in the Budget Detail Panel section. Replace the
  flat 8-item list with a three-section layout:

  **Income**
  - Tax revenue — Residential
  - Tax revenue — Commercial
  - Tax revenue — Industrial
  - Utility fees

  **Expenses**
  - Road maintenance
  - Service upkeep (fire/police/utilities)
  - Wages

  **Total**
  - Net monthly balance = Income total − Expenses total

  Each section header ("Income", "Expenses", "Total") is rendered as a bold label row using
  the HUD font with a 1 px rule separating sections. (`gamedesign-ux`)

- [ ] Update the panel height: the 3-section layout requires more vertical space than the
  original 320×200 px. New dimensions: **320×260 px** (adds 60 px for section headers and
  separator rules). (`gamedesign-ux`)

- [ ] Document the **subtotals** shown on each section header row:
  - "Income [$X,XXX/month]" — sum of all income line items
  - "Expenses [$X,XXX/month]" — sum of all expense line items
  - "Total" row shows the net with sign: "+$X,XXX" (green) or "−$X,XXX" (red).

  The net balance sign uses `SColor` green `(255, 80, 200, 80)` for surplus and
  `(255, 220, 80, 80)` for deficit, consistent with the existing deficit-pulsing color
  palette. (`gamedesign-ux`)

- [ ] Tourism income line: rendered as a grayed-out row "Tourism income: $0 (post-V1)" to
  occupy the reserved slot without being interactive. (`gamedesign-ux`)

##### 2b. `architecture/game-design/economy-model.md`

- [ ] Add a **Budget Screen Section Mapping** note that cross-references the canonical income
  and expense categories defined in this spec to their Budget Detail Panel display sections.
  This prevents future additions from being placed in the wrong column. (`gamedesign-lookandfeel`)

#### 3. Spec Updates — Road Lane Geometry & Center-Line Strip

##### 3a. `architecture/asset-standards/3d-model-standards.md` — Road Tile LOD

- [ ] Update the road tile mesh authoring note. Replace the existing carriageway geometry
  description with the following:

  **Carriageway width**: The asphalt surface covers **7.5 m** of the 10 m tile width
  (¾ of the tile). The remaining 1.25 m on each side is rendered as a kerb/verge strip using
  the existing kerb geometry. The carriageway is centered within the tile.

  **Center-line strip**: A 0.3 m wide white painted strip runs along the Z-axis center of the
  carriageway (at local X = 0). The strip is part of the LOD0 road mesh, implemented as a
  thin raised quad (+0.005 m above the asphalt surface to avoid Z-fighting) with a white
  vertex color (`SColor(255, 255, 255, 255)`) and `EMT_SOLID` material (no texture binding
  required — vertex color suffices). The strip does not appear at LOD1 or LOD2.

  **Lane layout** (two-way, keep-right):
  - Left lane (local X = −1.875 m center, 3.6 m wide): vehicle agents traveling in the
    **−Z direction** (southbound).
  - Right lane (local X = +1.875 m center, 3.6 m wide): vehicle agents traveling in the
    **+Z direction** (northbound).
  - Intersecting tiles rotate these conventions 90° about Y so the same lane rules hold in
    all cardinal directions. The lane center offsets are stored as named constants:

    ```cpp
    static constexpr float kLaneCenterOffset = 1.875f; // metres from road center
    static constexpr float kCarriagewayHalfWidth = 3.75f; // half of 7.5 m carriageway
    ```

  (`graphics-artist-3d-model`, `graphics-dev-irrlicht`)

- [ ] Update the road tile triangle budget note: the center-line strip quad adds 2 triangles
  to LOD0, bringing the new LOD0 budget to ≤50 tris (raise the cap from ≤48 to ≤50 to
  accommodate the strip). (`graphics-artist-3d-model`)

##### 3b. `architecture/game-design/traffic-system.md`

- [ ] Add a **Lane Assignment** subsection documenting the two-way lane convention:
  - Each road edge in the traffic graph is directional. Each physical road tile hosts two
    directed edges (one per lane direction).
  - An agent assigned to the northbound (+Z) lane is rendered at world X offset
    `+kLaneCenterOffset` from the tile center. An agent on the southbound (−Z) lane is
    rendered at `−kLaneCenterOffset`. East/west travel uses the same offset applied to the Z
    axis.
  - For turns and intersections: agents snap to the tile center X/Z at intersection tiles
    (no lane offset) and resume lane offset on the exit segment.
  - The `kLaneCenterOffset` constant is referenced from `src/rendering/render_constants.h`
    (the same file that holds `road_lod2_color`). Do NOT hardcode `1.875f` at call sites.
  (`gamedesign-lookandfeel`)

#### 4. C++ Implementation — Multi-Tile Footprints

##### 4a. `ICitySimulation` / `CitySimulation` — Footprint-Aware Placement

- [ ] Add `static int footprintSize(DensityTier tier)` to `CitySimulation` (returns 1, 2, or
  3). Service buildings return 2 (new `static int serviceFootprintSize()` = 2).
  (`graphics-dev-irrlicht`)

- [ ] `CitySimulation::placeZone(int tileX, int tileZ, ZoneType, DensityTier)`: before placing,
  iterate the full `N×N` footprint. If any tile is occupied or out-of-bounds, emit a
  `SimEvent::PLACEMENT_BLOCKED` and return without modifying state. On success, mark all
  `N×N` tiles as occupied and record `(tileX, tileZ)` as the origin tile.
  (`graphics-dev-irrlicht`)

- [ ] `CitySimulation::demolishTile(int tileX, int tileZ)`: if the target tile is a non-origin
  footprint member, look up the origin tile from the footprint registry and demolish the full
  footprint. (`graphics-dev-irrlicht`)

- [ ] `CitySimulation::doDensityUnlockTick()`: after determining a tile upgrades (Low→Med or
  Med→High), resolve the expanded footprint using the four-step upgrade resolution order from
  §1b:
  1. Compute new N×N footprint.
  2. For each tile in the expanded footprint occupied by a **same-zone-type, lower-density
     neighbour**: call `demolishTile()` silently (no undo window, no treasury refund) and
     post a NORMAL toast "Neighbouring [zone] building cleared for upgrade".
  3. If any remaining tile in the footprint is a road, different zone type, service building,
     or out-of-bounds: skip step 2 entirely for this tick, increment the per-tile
     `upgradeRetryCount`, and return without upgrading.
  4. After 12 failed retries: cancel the pending upgrade for this tile and emit a CRITICAL
     toast "Upgrade blocked — clear surrounding tiles".

  The `upgradeRetryCount` is a `std::unordered_map<TileKey, int>` member on `CitySimulation`,
  reset to 0 whenever a tile successfully upgrades or is manually demolished.
  (`gamedesign-lookandfeel`, `graphics-dev-irrlicht`)

- [ ] `ITerrainQuery::setTileHeight()` (from Phase 10b) must be called for **all tiles in the
  footprint** during terrain flattening, not only the origin tile. (`graphics-dev-irrlicht`)

##### 4b. `IrrlichtRenderer` — Per-Tier Scaling

- [ ] `IrrlichtRenderer::placeBuildingMesh(assetBaseName, tileX, tileZ, DensityTier)`: derive
  `setScale()` and world origin from the density tier per the table in §1a. The
  `assetBaseName` naming convention is unchanged (`res_low_01`, etc.). (`graphics-dev-irrlicht`)

- [ ] `IrrlichtRenderer::placeServiceBuildingMesh(type, tileX, tileZ)`: apply `setScale(5.0f)`
  (2×2 footprint) and center the node over the 2×2 block. (`graphics-dev-irrlicht`)

- [ ] Update `IRenderer::placeBuildingMesh()` signature in `src/interfaces/IRenderer.h` to
  accept `DensityTier` as the third parameter (after `tileZ`). Update `MockRenderer` and all
  call sites in `CitySimulation` and tests. (`graphics-dev-irrlicht`, `test-dev-cpp`)

##### 4c. Road Mesh — Carriageway Width & Center-Line Strip

- [ ] In `IrrlichtRenderer::placeRoadMesh()`, update the asphalt quad half-width from 5.0 m to
  3.75 m (`kCarriagewayHalfWidth`). Kerb strips occupy the remaining 1.25 m on each side
  (kerb outer edge = ±5 m from tile center, carriageway edge = ±3.75 m, kerb width = 1.25 m).
  (`graphics-dev-irrlicht`)

- [ ] Add a center-line strip mesh to `placeRoadMesh()`: a 0.3 m wide × 10 m long quad at
  local X = 0, Y = +0.005 m, using white vertex color and `EMT_SOLID` material. Strip is
  LOD0 only — not added to LOD1 or LOD2 meshes. (`graphics-dev-irrlicht`)

- [ ] Declare `kLaneCenterOffset` and `kCarriagewayHalfWidth` as `static constexpr float`
  in `src/rendering/render_constants.h`. (`graphics-dev-irrlicht`)

##### 4d. Vehicle Agent Rendering — Lane Offset

- [ ] In `IrrlichtRenderer` vehicle agent position update (Phase 11d vehicle rendering),
  apply the `kLaneCenterOffset` world-space offset perpendicular to the agent's direction of
  travel. Northbound (+Z) agents shift +X by `kLaneCenterOffset`; southbound shift −X.
  East (+X) agents shift +Z; west shift −Z. (`graphics-dev-irrlicht`)

#### 5. Unit Tests

##### 5a. Multi-Tile Footprint Tests — `tests/simulation/footprint_test.cpp`

- [ ] `FootprintTest_LowZone_Occupies1x1Tiles`
- [ ] `FootprintTest_MedZone_Occupies2x2Tiles`
- [ ] `FootprintTest_HighZone_Occupies3x3Tiles`
- [ ] `FootprintTest_ServiceBuilding_Occupies2x2Tiles`
- [ ] `FootprintTest_MedZone_BlockedIfAnyTileOccupied`
- [ ] `FootprintTest_Demolish_FreesAllFootprintTiles`
- [ ] `FootprintTest_DensityUpgrade_DeferredIfSpaceUnavailable`
- [ ] `FootprintTest_RoadAdjacency_NonOriginTileCountsAsServed`
- [ ] `FootprintTest_UpgradeToMed_DemolishesSameZoneNeighbour`: a 3×3 grid of `res_low`
  buildings upgrades the center tile to `res_med`; the two overlapping neighbour tiles are
  auto-demolished, their space absorbed into the 2×2 footprint.
- [ ] `FootprintTest_UpgradeToMed_BlockedByRoad_DoesNotDemolishNeighbour`: when the 2×2
  expanded footprint overlaps a road tile, no neighbour demolition occurs and
  `upgradeRetryCount` is incremented.
- [ ] `FootprintTest_UpgradeToMed_BlockedByDifferentZone_DoesNotDemolishNeighbour`: expanded
  footprint overlaps a `com_low` tile — upgrade is deferred, `com_low` tile untouched.
- [ ] `FootprintTest_UpgradeRetryCancel_After12Ticks_EmitsCriticalToast`

  All tests use `ManualRNG` and `ManualClock`. (`test-dev-cpp`)

- [ ] Add `footprint_test.cpp` to the `simulation_tests` CMake target. (`test-dev-cpp`)

##### 5b. Road Lane Tests — `tests/rendering/road_lane_test.cpp`

- [ ] `RoadLane_CarriagewayHalfWidth_Is3_75m`: verifies `kCarriagewayHalfWidth == 3.75f`.
- [ ] `RoadLane_LaneCenterOffset_Is1_875m`: verifies `kLaneCenterOffset == 1.875f`.
- [ ] `RoadLane_NorthboundAgent_PositiveXOffset`: confirms agent world-X = tile center X +
  `kLaneCenterOffset` for a +Z-direction agent.
- [ ] `RoadLane_SouthboundAgent_NegativeXOffset`: confirms agent world-X = tile center X −
  `kLaneCenterOffset` for a −Z-direction agent.

  (`test-dev-cpp`)

- [ ] Add `road_lane_test.cpp` to the `terrain_tests` (or `rendering_tests`) CMake target.
  (`test-dev-cpp`)

##### 5c. Budget Panel Tests — `tests/ui/budget_breakdown_test.cpp`

- [ ] `BudgetBreakdown_IncomeSectionTotal_MatchesSumOfLineItems`
- [ ] `BudgetBreakdown_ExpenseSectionTotal_MatchesSumOfLineItems`
- [ ] `BudgetBreakdown_NetBalance_EqualsIncomeTotalMinusExpenseTotal`
- [ ] `BudgetBreakdown_TourismIncome_AlwaysZeroInV1`

  Use `MockCitySimulation`. (`test-dev-cpp`)

- [ ] Add `budget_breakdown_test.cpp` to `ui_tests` CMake target. (`test-dev-cpp`)

---

### Exit Criteria

- [ ] `architecture/asset-standards/3d-model-standards.md` documents the multi-tile footprint
  table, per-tier `setScale()` values, multi-tile world-origin formula, and road carriageway /
  center-line strip geometry (including updated ≤50 tri LOD0 budget).
- [ ] `architecture/game-design/zoning-system.md` documents multi-tile placement rules,
  demolish behavior, density-upgrade deferral, and hover highlight footprint.
- [ ] `architecture/graphics-architecture/scene-graph-ownership.md` documents
  `placeBuildingMesh()` and `placeServiceBuildingMesh()` updated contracts with per-tier scale.
- [ ] `architecture/ui-ux/hud-layout.md` Budget Detail Panel redesigned into Income /
  Expenses / Total sections, panel height updated to 320×260 px, subtotals and sign colors
  documented.
- [ ] `architecture/game-design/economy-model.md` Budget Screen Section Mapping note present.
- [ ] `architecture/game-design/traffic-system.md` Lane Assignment subsection present with
  `kLaneCenterOffset` reference and intersection snap rule.
- [ ] `architecture/asset-standards/3d-model-standards.md` road tile lane-layout and
  center-line strip documented with named constants.
- [ ] `IRenderer::placeBuildingMesh()` accepts `DensityTier`; `MockRenderer` updated; all call
  sites compile cleanly.
- [ ] `CitySimulation::placeZone()` enforces multi-tile footprint collision check.
- [ ] `CitySimulation::demolishTile()` frees all footprint tiles.
- [ ] `CitySimulation::doDensityUnlockTick()` auto-demolishes same-zone lower-density
  neighbours in the expanded footprint; defers (not demolishes) on road/different-zone/OOB
  conflicts; cancels after 12 retries with CRITICAL toast.
- [ ] `placeRoadMesh()` renders 7.5 m carriageway with white center-line strip at LOD0.
- [ ] Vehicle agent positions offset by `kLaneCenterOffset` perpendicular to travel direction.
- [ ] `kLaneCenterOffset` and `kCarriagewayHalfWidth` declared in `render_constants.h`.
- [ ] All 13 footprint tests, 4 road lane tests, and 4 budget breakdown tests pass.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` exits
  zero.

---

### Team

| Role | Responsibility |
|---|---|
| `gamedesign-lookandfeel` | Author multi-tile placement rules in zoning-system.md; road adjacency rule; density-upgrade deferral design; lane assignment and kLaneCenterOffset spec in traffic-system.md; budget section mapping note in economy-model.md |
| `gamedesign-ux` | Redesign Budget Detail Panel layout in hud-layout.md (Income/Expenses/Total sections, 320×260 px, subtotals, colors, tourism placeholder); hover highlight footprint rule in zoning-system.md |
| `graphics-artist-3d-model` | Author multi-tile footprint and scale table in 3d-model-standards.md; road carriageway width, center-line strip geometry, and updated ≤50 tri budget in 3d-model-standards.md |
| `graphics-dev-irrlicht` | Implement per-tier scale in `placeBuildingMesh()`/`placeServiceBuildingMesh()`; update `IRenderer` signature and `MockRenderer`; implement carriageway width + center-line strip in `placeRoadMesh()`; declare constants in `render_constants.h`; implement lane offset in vehicle agent rendering; update `placeZone()`/`demolishTile()`/`doDensityUnlockTick()` footprint logic; update `setTileHeight()` calls to cover full footprint |
| `test-dev-cpp` | Author footprint, road lane, and budget breakdown test files; wire all into CMake targets |

---

### Dependencies

- Requires Phase 11d complete: multi-tile vehicle agent rendering (lane offset) depends on the
  Phase 11d vehicle spawn/move/despawn infrastructure.
- Requires Phase 10b complete: terrain flattening for multi-tile footprints.
- No dependency on Phase 11f, 11g, or 12 — can run in parallel with those phases.
- Post-V1 extension: angled / diagonal road tiles with lane curvature are deferred to a
  post-V1 traffic pass.
