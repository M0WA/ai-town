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

4. **Demolition fix** — demolition is currently non-functional and the left-mouse-down event
   fires the yes/no/cancel confirmation dialog immediately, which blocks zoning. The fix moves
   the confirmation to mouse-release, gates it behind the active tool, and wires
   `demolishTile()` correctly end-to-end.

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

- [ ] Document the **native-size authoring convention**: models are authored at real-world scale
  (1 Blender unit = 1 m). Each density tier has its own correctly-sized model; no runtime
  `setScale()` is applied — `placeBuildingMesh()` places nodes at scale 1.0:

  | Density tier | Local-space half-extent | World footprint |
  |---|---|---|
  | `low` (res/com/ind) | ±5 m | 10 m × 10 m |
  | `med` (res/com/ind) | ±10 m | 20 m × 20 m |
  | `high` (res/com/ind) | ±15 m | 30 m × 30 m |
  | Service (2×2) | ±10 m | 20 m × 20 m |

  Artists export each tier as a separate model at the correct size. The existing ±2 m
  authoring convention is retired. (`graphics-artist-3d-model`)

  > **Note**: This change requires all Phase 9 building assets (`res_low_*`, `res_med_*`,
  > `res_high_*`, `com_low_*`, etc.) to be re-exported from Blender at native world size.
  > Add this as a prerequisite in the Dependencies section.

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
  - Terrain flattening: during placement, `setTileHeight()` must be called for **all tiles in
    the N×N footprint** (not only the origin tile) — see §4a for the C++ implementation detail.
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

- [ ] Add a **Service Building Street Adjacency** rule: a service building may only be placed
  if at least one of the tiles in its 2×2 footprint is directly edge-adjacent (4-directional
  cardinal, distance = 1) to a road tile. If no adjacent road exists, placement is rejected
  and the player is shown a toast: "Service building must be next to a road". Service
  buildings are never subject to the 3-tile proximity rule below — they use the stricter
  direct-adjacency requirement. (`gamedesign-lookandfeel`)

- [ ] Add a **Zone Street Proximity** rule: a zone tile (any density, any type) requires a
  road tile within **3 tiles** (Manhattan distance from any tile in the footprint to the
  nearest road tile, measured as straight-line grid steps, not path cost). The rule has two
  enforcement modes:
  - **New placement**: if no road tile is within 3 tiles of the entire N×N footprint,
    placement is rejected and the player is shown a toast: "Must be within 3 tiles of a road".
  - **Abandonment** (existing building): on each simulation tick, if the nearest road tile
    to a building's footprint exceeds 3 tiles (e.g. the road was demolished), the building
    becomes **abandoned** — population is removed, tax revenue drops to $0, and a
    NORMAL-priority toast is shown: "Building abandoned — too far from road". An abandoned
    building can recover automatically if a road is built within 3 tiles before the next
    abandonment tick; otherwise it remains abandoned until demolished or road is restored.
  (`gamedesign-lookandfeel`)

- [ ] Document the **hover highlight** rule: when the player hovers over a zone tile with the
  Zone tool active, the highlight overlay covers the full footprint of the tier selected in
  the Zone sub-panel (1×1, 2×2, or 3×3), not just the hovered tile. (`gamedesign-ux`)

##### 1c. `architecture/graphics-architecture/scene-graph-ownership.md`

- [ ] Update `placeBuildingMesh()` contract: derive `DensityTier` from the `assetBaseName`
  prefix so the scene-graph layer can compute the correct world origin. Document that no
  `setScale()` is applied — models are natively sized. Document the per-tier local
  half-extent table from §1a. (`graphics-dev-irrlicht`)

- [ ] Update `placeServiceBuildingMesh()` contract: service buildings use the 2×2 footprint
  at scale 1.0 (model is natively sized at ±10 m). The world origin is the center of the 2×2
  footprint (see origin formula above). (`graphics-dev-irrlicht`)

#### 2. Spec Updates — Budget Screen Income/Expense Breakdown

##### 2a. `architecture/ui-ux/hud-layout.md` — Budget Detail Panel

*(Spec already updated during planning — these deliverables confirm the spec is correct and track the C++ implementation work.)*

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

- [ ] Tourism income line: the last line item within the Income section (after Utility fees),
  rendered as a grayed-out non-interactive row labelled "Tourism income: $0 (post-V1)" to
  reserve space for a post-V1 feature. (`gamedesign-ux`)

##### 2b. `architecture/game-design/economy-model.md`

- [ ] Add a **Budget Screen Section Mapping** note that cross-references the canonical income
  and expense categories defined in this spec to their Budget Detail Panel display sections.
  This prevents future additions from being placed in the wrong column. The note MUST also
  explicitly document the V1 treatment of Tourism income:

  - **Tourism income is scoped as post-V1** (consistent with the existing "Tourism income:
    Post-V1 scope" line in the Revenue sources bullet of this spec). In the Budget Detail
    Panel Income section it renders as a grayed-out, non-interactive placeholder row labelled
    "Tourism income: $0 (post-V1)". A developer reading the economy spec must be able to
    understand from this note why Tourism appears in the panel at all (it reserves display
    space for the future feature) without hunting for the answer in the UI spec.
  - The cross-reference should point to `architecture/ui-ux/hud-layout.md` §Budget Detail
    Panel, Tourism income line item, for the authoritative rendering rule.

  (`gamedesign-lookandfeel`)

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

#### 3c. `architecture/ui-ux/input-arbitration.md` — Demolition Tool Input Fix

- [ ] Add a **Demolition Tool** subsection documenting the corrected input flow:
  - The demolish tool is activated via the Demolish button in the toolbar (or hotkey `X`).
    While demolish is **not** the active tool, left-mouse events must **never** trigger
    demolition logic.
  - **Mouse-down** (LMB press) while Demolish is active: highlights the hovered tile as a
    demolition target (visual feedback only — no dialog shown yet).
  - **Mouse-up** (LMB release) on the same tile that was highlighted on mouse-down: opens
    the confirmation modal "Demolish [tile type]? [Yes] [No]". If the cursor has moved to a
    different tile between down and up, cancel the highlight and do nothing.
  - **Yes**: call `ICitySimulation::demolishTile(tileX, tileZ)` and dismiss the modal.
  - **No** / modal dismissed (Esc): cancel the demolition, clear the highlight, remain in
    Demolish tool mode.
  - The Zone tool's LMB-down must not enter the demolition code path under any
    circumstances. Input arbitration: tool-mode is checked before any tile-interaction
    handler fires. (`gamedesign-ux`)

- [ ] Update the **Input Arbitration** section to document that the Demolish tool takes
  exclusive input ownership only while active; switching to the Zone or Road tool
  immediately clears any pending demolition highlight. (`gamedesign-ux`)

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

- [ ] `CitySimulation::placeServiceBuilding(int tileX, int tileZ, ServiceType)`: before
  placing, verify that at least one tile in the 2×2 footprint is directly adjacent (cardinal,
  distance = 1) to a road tile. If not, emit `SimEvent::PLACEMENT_BLOCKED` with toast
  "Service building must be next to a road" and return without placing. (`graphics-dev-irrlicht`)

- [ ] `CitySimulation::placeZone()`: extend the pre-placement check to also verify that
  the Manhattan distance from the nearest road tile to any tile in the N×N footprint is ≤ 3.
  If the nearest road is > 3 tiles away, emit `SimEvent::PLACEMENT_BLOCKED` with toast
  "Must be within 3 tiles of a road". This check runs after the footprint-collision check.
  Helper: `static int nearestRoadDistance(int tileX, int tileZ, int footprintN)` —
  returns the minimum Manhattan distance from any footprint tile to any road tile, or
  `INT_MAX` if no road exists in range. (`graphics-dev-irrlicht`)

- [ ] `CitySimulation::doProximityTick()`: new per-tick method that iterates all placed
  zone buildings (not service buildings) and for each checks `nearestRoadDistance() <= 3`.
  - If distance > 3 and building is **not** already abandoned: mark as abandoned, zero out
    population and tax contribution for this building, emit NORMAL toast "Building abandoned
    — too far from road".
  - If distance ≤ 3 and building **is** abandoned: recover automatically (restore
    population/tax), emit NORMAL toast "Building recovered — road reconnected".
  The abandoned flag is stored per-building in the footprint registry.
  `doProximityTick()` is called once per simulation tick, after `doDensityUnlockTick()`.
  (`gamedesign-lookandfeel`, `graphics-dev-irrlicht`)

- [ ] `ITerrainQuery::setTileHeight()` (from Phase 10b) must be called for **all tiles in the
  footprint** during terrain flattening, not only the origin tile. Iterate the full N×N grid:
  for a 1×1 footprint — 1 call; for a 2×2 footprint — 4 calls at `(tileX+dx, tileZ+dz)` for
  dx,dz ∈ {0,1}; for a 3×3 footprint — 9 calls at `(tileX+dx, tileZ+dz)` for
  dx,dz ∈ {0,1,2}. All tiles are set to the same height (the height of the origin tile
  sampled from the current heightmap **before** any `setTileHeight()` modifications occur)
  to ensure a flat, level building footprint. (`graphics-dev-irrlicht`)

##### 4b. `IrrlichtRenderer` — Per-Tier Scaling

- [ ] `IrrlichtRenderer::placeBuildingMesh(assetBaseName, tileX, tileZ)`: parse `DensityTier`
  from the second `_`-delimited segment of `assetBaseName` (e.g., `"res_low_01"` splits to
  ["res", "low", "01"] → tier = `low` → `DensityTier::LOW`), then compute world origin and
  confirm scale 1.0 per native-size convention. No IRenderer interface change needed;
  `MockRenderer` unchanged. (`graphics-dev-irrlicht`)

- [ ] `IrrlichtRenderer::placeServiceBuildingMesh(type, tileX, tileZ)`: place at scale 1.0
  (model is natively sized at ±10 m = 20 m × 20 m) and center the node over the 2×2 block.
  (`graphics-dev-irrlicht`)

- [ ] **Multi-tile hover highlight interface**: Update `IRenderer::setTileHoverHighlight()` in
  `src/interfaces/IRenderer.h` to accept a `footprintSize` parameter (default value `1` for
  backward compatibility with all existing 1×1 call sites):

  ```cpp
  virtual void setTileHoverHighlight(int tileX, int tileZ, int footprintSize = 1) = 0;
  ```

  The `uint32_t argb` parameter from the old signature is removed. Highlight colour is
  hardcoded per tool mode in `IrrlichtRenderer`: Zone-tool hover uses a semi-transparent
  green (`0x6600FF00`); Demolish-tool pending hover uses a semi-transparent red
  (`0x66FF0000`). No colour is passed at the call site — the renderer determines colour
  by reading an internal `m_activeTool` (or equivalent tool-mode state member) — the
  named colour constants (`kHoverArgbZone`, `kHoverArgbDemolish`, etc.) are defined in
  `src/ui/ui_constants.h` and used only inside `IrrlichtRenderer`. Cross-reference:
  `architecture/ui-ux/hud-layout.md` §Tile Hover Highlight. Update `MockRenderer` to match.

  The implementation in `IrrlichtRenderer` draws an N×N highlight quad (where N =
  `footprintSize`) centered on the footprint, covering all tiles from `(tileX, tileZ)` to
  `(tileX + footprintSize − 1, tileZ + footprintSize − 1)`. UIManager calls this single
  overload with the tier-appropriate footprint size (1, 2, or 3) when the player hovers with
  the Zone tool active. Update `MockRenderer` to add the new parameter with the same default.
  Update all call sites to compile cleanly — existing call sites with two arguments are
  unaffected by the default. Cross-reference: §1b hover highlight rule ("the highlight
  overlay covers the full footprint of the tier selected in the Zone sub-panel").
  (`graphics-dev-irrlicht`)

- [ ] **Active-tool synchronisation**: Declare `virtual void setActiveTool(ToolMode mode) = 0;`
  in `src/interfaces/IRenderer.h`. `ToolMode` is the existing enum (or equivalent) tracking
  the active placement tool (Zone, Road, Utilities, Demolish, Query, None). UIManager calls
  `m_renderer->setActiveTool(m_activeTool)` in its toolbar-button click handler whenever
  `m_activeTool` changes. `IrrlichtRenderer` stores the value in its own `m_activeTool` member
  and uses it inside `setTileHoverHighlight()` to select the highlight colour. Update
  `MockRenderer` to add a matching override.  (`graphics-dev-irrlicht`, `test-dev-cpp`)

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

##### 4d. UIManager — Demolition Tool Input Fix

- [ ] In `UIManager` (or the tool-dispatch layer), move the demolition confirmation trigger
  from `onMouseButtonDown()` to `onMouseButtonUp()`. The down event sets a
  `m_demolishPendingTile` member; the up event checks that the tile under the cursor matches
  `m_demolishPendingTile` before opening the modal. If they differ, clear
  `m_demolishPendingTile` and do nothing. (`graphics-dev-irrlicht`)

- [ ] Guard all demolition input handling behind an `activeTool == ToolMode::DEMOLISH` check.
  The Zone tool's `onMouseButtonDown()` must assert / short-circuit before reaching any
  demolition code path. (`graphics-dev-irrlicht`)

- [ ] Wire the confirmation modal **Yes** branch to call
  `m_simulation->demolishTile(tileX, tileZ)`. Verify the call reaches
  `CitySimulation::demolishTile()` and that the tile is actually removed from the world (mesh
  removed via `IRenderer::removeBuildingMesh()`, footprint registry cleared, population
  updated). This is the "currently not functional" wiring fix. (`graphics-dev-irrlicht`)

- [ ] On confirmation modal **No** / Esc: call `m_renderer->clearDemolishHighlight()` and
  reset `m_demolishPendingTile`. Do not call `demolishTile()`. (`graphics-dev-irrlicht`)

- [ ] Add `clearDemolishHighlight()` to `IRenderer` / `IrrlichtRenderer` if not already
  present. Update `MockRenderer`. (`graphics-dev-irrlicht`, `test-dev-cpp`)

##### 4e. Vehicle Agent Rendering — Lane Offset

- [ ] In `IrrlichtRenderer` vehicle agent position update (Phase 11d vehicle rendering),
  apply the `kLaneCenterOffset` world-space offset perpendicular to the agent's direction of
  travel. Northbound (+Z) agents shift +X by `kLaneCenterOffset`; southbound shift −X.
  East (+X) agents shift +Z; west shift −Z.

  **Intersection tile snap rule**: Lane offset is applied only on straight road segments.
  At intersection tiles — defined as tiles with 3 or more road neighbours, or tiles recorded
  in the intersection registry — agents snap to the tile center X/Z (lane offset = 0) and do
  NOT apply `kLaneCenterOffset`. The lane offset resumes on the exit segment once the agent
  leaves the intersection tile. Cross-reference: `architecture/game-design/traffic-system.md`
  §Lane Assignment ("agents snap to the tile center X/Z at intersection tiles (no lane
  offset) and resume lane offset on the exit segment"). (`graphics-dev-irrlicht`)

  **Audio positioning**: vehicle engine audio sources (`updateVehicleAudio(idleIdx, moveIdx, speedFraction, worldX, worldZ)`)
  must receive the same world coordinates as the rendered position — including the applied
  lane offset on straight segments. At intersection tiles (lane offset = 0, agent snapped to
  tile center), pass tile-center worldX/worldZ. This keeps audio spatially synchronised with
  the visual position. Cross-reference: `architecture/audio-architecture/dynamic-soundscape.md`
  §Vehicle Engine Audio. The vehicle position (including lane offset) is computed once per
  frame in the vehicle update pass; the resulting `(worldX, worldZ)` is passed to both the
  renderer and `updateVehicleAudio()` — no separate recomputation occurs in the audio path.
  (`sound-dev-opensoftal`)

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
- [ ] `FootprintTest_ServiceBuilding_PlacementBlocked_NoAdjacentRoad`: service building
  placement fails when no road tile is cardinal-adjacent to any tile in its 2×2 footprint.
- [ ] `FootprintTest_ServiceBuilding_PlacementSucceeds_OneAdjacentRoad`: placement succeeds
  when exactly one tile in the 2×2 footprint has a cardinal-adjacent road.
- [ ] `FootprintTest_ZonePlacement_Blocked_RoadTooFar`: `placeZone()` rejects placement when
  nearest road tile is > 3 tiles from the footprint; emits PLACEMENT_BLOCKED toast.
- [ ] `FootprintTest_ZonePlacement_Succeeds_RoadWithin3Tiles`: `placeZone()` succeeds when
  nearest road tile is exactly 3 tiles away.
- [ ] `FootprintTest_ZoneAbandonment_WhenRoadDemolished`: after placing a zone within 3 tiles
  of a road, demolishing that road causes `doProximityTick()` to mark the building abandoned
  and zero out its population contribution.
- [ ] `FootprintTest_ZoneRecovery_WhenRoadRestored`: an abandoned building recovers
  automatically on the next `doProximityTick()` after a road is placed within 3 tiles.

  All tests use `ManualRNG` and `ManualClock`. (`test-dev-cpp`)

- [ ] Add `footprint_test.cpp` to the `simulation_tests` CMake target. (`test-dev-cpp`)

##### 5b. Road Lane Tests — `tests/terrain/road_lane_test.cpp`

- [ ] `RoadLane_CarriagewayHalfWidth_Is3_75m`: verifies `kCarriagewayHalfWidth == 3.75f`.
- [ ] `RoadLane_LaneCenterOffset_Is1_875m`: verifies `kLaneCenterOffset == 1.875f`.
- [ ] `RoadLane_NorthboundAgent_PositiveXOffset`: confirms agent world-X = tile center X +
  `kLaneCenterOffset` for a +Z-direction agent.
- [ ] `RoadLane_SouthboundAgent_NegativeXOffset`: confirms agent world-X = tile center X −
  `kLaneCenterOffset` for a −Z-direction agent.
- [ ] `RoadLane_IntersectionTile_AgentSnapsToTileCenter`: confirms that an agent traversing
  an intersection tile (3+ road neighbours) has lane offset = 0 (world X/Z equals tile
  center X/Z), and that the lane offset resumes to `±kLaneCenterOffset` on the exit segment.

  (`test-dev-cpp`)

- [ ] Add `road_lane_test.cpp` to the `terrain_tests` CMake target.
  (`test-dev-cpp`)

##### 5c. Budget Panel Tests — `tests/ui/budget_breakdown_test.cpp`

- [ ] `BudgetBreakdown_IncomeSectionTotal_MatchesSumOfLineItems`
- [ ] `BudgetBreakdown_ExpenseSectionTotal_MatchesSumOfLineItems`
- [ ] `BudgetBreakdown_NetBalance_EqualsIncomeTotalMinusExpenseTotal`
- [ ] `BudgetBreakdown_TourismIncome_AlwaysZeroInV1`

  Use `MockCitySimulation`. (`test-dev-cpp`)

- [ ] Add `budget_breakdown_test.cpp` to `ui_tests` CMake target. (`test-dev-cpp`)

##### 5d. Demolition Input Tests — `tests/ui/demolition_input_test.cpp`

- [ ] `DemolitionInput_MouseUp_SameTile_ConfirmModalOpened`: mouse-down then mouse-up on
  same tile while Demolish tool active → confirmation modal opens; `demolishTile()` NOT
  yet called.
- [ ] `DemolitionInput_MouseUp_DifferentTile_NoModal`: mouse-down on tile A, mouse-up on
  tile B → no modal opened, no demolition triggered.
- [ ] `DemolitionInput_ConfirmYes_CallsDemolishTile`: after modal opens, confirming Yes →
  `demolishTile()` called with correct (tileX, tileZ); tile removed from renderer.
- [ ] `DemolitionInput_ConfirmNo_NoDemolition`: confirming No → `demolishTile()` NOT called;
  highlight cleared.
- [ ] `DemolitionInput_ZoneTool_MouseDown_DoesNotTriggerDemolish`: while Zone tool is active,
  LMB down on any tile must not set `m_demolishPendingTile` or open a confirmation modal.

  Use `MockCitySimulation` and `MockRenderer`. (`test-dev-cpp`)

- [ ] Add `demolition_input_test.cpp` to `ui_tests` CMake target. (`test-dev-cpp`)

##### 5e. Vehicle Audio Positioning Tests — `tests/audio/vehicle_audio_positioning_test.cpp`

- [ ] `VehicleAudio_StraightSegment_AudioReceivesLaneOffsetCoords`: verifies that when a
  vehicle agent travels a straight road segment in the northbound (+Z) direction, the worldX
  coordinate passed to `updateVehicleAudio(idleIdx, moveIdx, speedFraction, worldX, worldZ)`
  equals tile center X + `kLaneCenterOffset`. Cross-reference: §4e audio positioning
  requirement and `architecture/audio-architecture/dynamic-soundscape.md` §Vehicle Engine
  Audio. (`test-dev-cpp`)

- [ ] `VehicleAudio_IntersectionTile_AudioReceivesTileCenterCoords`: verifies that when a
  vehicle agent traverses an intersection tile (3+ road neighbours), the worldX/worldZ passed
  to `updateVehicleAudio()` equals the tile center (no lane offset). This confirms that audio
  remains spatially synchronized with the visual position when the agent snaps to the
  intersection tile center per §4e lane offset suppression rule. (`test-dev-cpp`)

  Use `MockAudioSystem` and `MockRenderer`. (`test-dev-cpp`)

- [ ] Add `vehicle_audio_positioning_test.cpp` to `audio_tests` CMake target. (`test-dev-cpp`)

---

### Exit Criteria

- [ ] `architecture/asset-standards/3d-model-standards.md` documents the multi-tile footprint
  table, native-size authoring convention (no `setScale()`; per-tier local half-extent table),
  multi-tile world-origin formula, and road carriageway / center-line strip geometry
  (including updated ≤50 tri LOD0 budget).
- [ ] `architecture/game-design/zoning-system.md` documents multi-tile placement rules,
  demolish behavior, density-upgrade deferral, hover highlight footprint, service building
  street-adjacency rule, and zone 3-tile street-proximity rule with abandonment/recovery.
- [ ] `architecture/graphics-architecture/scene-graph-ownership.md` documents
  `placeBuildingMesh()` and `placeServiceBuildingMesh()` updated contracts with per-tier scale.
- [ ] `architecture/ui-ux/hud-layout.md` Budget Detail Panel redesigned into Income /
  Expenses / Total sections, panel height updated to 320×260 px, subtotals and sign colors
  documented.
- [ ] `architecture/game-design/economy-model.md` Budget Screen Section Mapping note present,
  including explicit documentation that Tourism income is post-V1 and renders as a grayed-out
  placeholder in the Budget Detail Panel Income section, with cross-reference to
  `architecture/ui-ux/hud-layout.md`.
- [ ] `architecture/game-design/traffic-system.md` Lane Assignment subsection present with
  `kLaneCenterOffset` reference and intersection snap rule.
- [ ] `architecture/asset-standards/3d-model-standards.md` road tile lane-layout and
  center-line strip documented with named constants.
- [ ] `IrrlichtRenderer::placeBuildingMesh()` parses `DensityTier` from `assetBaseName`
  prefix; places at scale 1.0 (no `setScale()`); `IRenderer` interface unchanged;
  `MockRenderer` unchanged; all call sites compile cleanly.
- [ ] `CitySimulation::placeServiceBuilding()` enforces direct street adjacency (distance = 1).
- [ ] `CitySimulation::placeZone()` enforces zone street-proximity check (nearest road ≤ 3 tiles).
- [ ] `CitySimulation::doProximityTick()` marks buildings abandoned when road moves > 3 tiles
  away; recovers them automatically when road returns within 3 tiles.
- [ ] `CitySimulation::placeZone()` enforces multi-tile footprint collision check.
- [ ] `CitySimulation::demolishTile()` frees all footprint tiles.
- [ ] `CitySimulation::doDensityUnlockTick()` auto-demolishes same-zone lower-density
  neighbours in the expanded footprint; defers (not demolishes) on road/different-zone/OOB
  conflicts; cancels after 12 retries with CRITICAL toast.
- [ ] `placeRoadMesh()` renders 7.5 m carriageway with white center-line strip at LOD0.
- [ ] Vehicle agent positions offset by `kLaneCenterOffset` perpendicular to travel direction;
  lane offset suppressed (snapped to tile center X/Z) at intersection tiles.
- [ ] `IRenderer::setTileHoverHighlight(tileX, tileZ, footprintSize = 1)` declared in
  `src/interfaces/IRenderer.h`; `MockRenderer` updated; all existing call sites compile
  cleanly with two-argument form; UIManager passes tier footprint size for Zone tool hover.
- [ ] `kLaneCenterOffset` and `kCarriagewayHalfWidth` declared in `render_constants.h`.
- [ ] `architecture/ui-ux/input-arbitration.md` documents demolition tool input flow
  (mouse-down highlights, mouse-up triggers modal) and tool-mode gating.
- [ ] Demolition confirmation modal fires on mouse-up (not mouse-down); Zone tool LMB-down
  is fully isolated from demolition code path.
- [ ] Confirmation **Yes** calls `demolishTile()` and removes tile from world end-to-end.
- [ ] Vehicle audio positioning tests pass: `VehicleAudio_StraightSegment_AudioReceivesLaneOffsetCoords`
  and `VehicleAudio_IntersectionTile_AudioReceivesTileCenterCoords` confirm that audio
  coordinates match visual position with lane offset on straight segments and tile-center
  snap at intersections.
- [ ] All 18 footprint tests, 5 road lane tests, 4 budget breakdown tests, 5 demolition
  input tests, and 2 vehicle audio positioning tests pass.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` exits
  zero.

---

### Team

| Role | Responsibility |
|---|---|
| `gamedesign-lookandfeel` | Author multi-tile placement rules in zoning-system.md; service building street-adjacency rule; zone 3-tile proximity rule and abandonment/recovery spec; road adjacency rule; density-upgrade deferral design; lane assignment and kLaneCenterOffset spec in traffic-system.md; budget section mapping note in economy-model.md |
| `gamedesign-ux` | Redesign Budget Detail Panel layout in hud-layout.md (Income/Expenses/Total sections, 320×260 px, subtotals, colors, tourism placeholder); hover highlight footprint rule in zoning-system.md; demolition tool input flow in input-arbitration.md (mouse-up confirmation, tool-mode gating) |
| `graphics-artist-3d-model` | Author multi-tile footprint and native-size authoring convention (no setScale) in 3d-model-standards.md; road carriageway width, center-line strip geometry, and updated ≤50 tri budget in 3d-model-standards.md |
| `graphics-dev-irrlicht` | Implement `placeBuildingMesh()` (DensityTier parsed from assetBaseName prefix, scale 1.0, no IRenderer interface change, MockRenderer unchanged) and `placeServiceBuildingMesh()` at scale 1.0; add `footprintSize` parameter to `setTileHoverHighlight()` and update MockRenderer for that interface change; implement carriageway width + center-line strip in `placeRoadMesh()`; declare constants in `render_constants.h`; implement lane offset with intersection snap in vehicle agent rendering; update `placeZone()`/`demolishTile()`/`doDensityUnlockTick()` footprint logic; implement `placeServiceBuilding()` street-adjacency check; implement `placeZone()` 3-tile proximity check; implement `doProximityTick()` abandonment/recovery; update `setTileHeight()` calls to cover full footprint; fix demolition input (mouse-up trigger, tool-mode guard, Yes→demolishTile() wiring, `clearDemolishHighlight()`) |
| `test-dev-cpp` | Author footprint, road lane, and budget breakdown test files; wire all into CMake targets |

---

### Dependencies

- Requires Phase 11d complete: multi-tile vehicle agent rendering (lane offset) depends on the
  Phase 11d vehicle spawn/move/despawn infrastructure.
- Requires Phase 10b complete: terrain flattening for multi-tile footprints.
- Requires Phase 9 building assets to be re-exported at native world size (1 Blender unit = 1 m)
  per the §1a authoring convention. No `setScale()` is applied at runtime.
- No dependency on Phase 11f, 11g, or 12 — can run in parallel with those phases.
- Post-V1 extension: angled / diagonal road tiles with lane curvature are deferred to a
  post-V1 traffic pass.

---

### Bug Fixes & Sign-off Gaps (added during implementation sign-off)

The following items were discovered during the Phase 11h sign-off run and must be resolved
before the phase is marked DONE.

#### Bug 1 — E/W Road Center-Line Strip Rotated 90°

**Symptom**: Roads placed in the East/West direction show the white center-line strip running
along Z (North/South) instead of along the road direction.

**Root cause**: `buildTileRoadMesh()` always built the carriageway and center-line along the
Z-axis. Rotating the scene node 90° Y cannot fix this because vertex Y heights are baked as
absolute world-space coordinates; rotating the node maps the wrong corner heights to the wrong
world positions, producing twisted geometry.

**Fix** (`src/rendering/IrrlichtRenderer.cpp`): Add `bool isEW` parameter to
`buildTileRoadMesh()`. When `isEW=true`, build the carriageway along X (Z=±cH), south/north
kerbs on the Z sides, and interpolate the center-line heights at Z=0:
`yCL_W = (y00+y01)*0.5f`, `yCL_E = (y10+y11)*0.5f`. `placeRoadMesh()` detects E/W-only
tiles (`hasEW_dir && !hasNS_dir`) and passes `isEW=true` to the builder.

- [x] `buildTileRoadMesh()` accepts `isEW` parameter and builds correct E/W geometry. (`graphics-dev-irrlicht`)

#### Bug 2 — Houses Too Small (Phase 9 Assets at ±2 m Instead of Native Scale)

**Symptom**: Residential buildings appear as tiny dots — roughly the size of a shrub. Phase 9
assets were authored at ±2 m half-extent (placeholder) but `placeBuildingMesh()` places nodes
at scale 1.0 (no runtime `setScale()` per spec).

**Root cause**: Phase 9 assets must be re-exported at native world scale: Low = ±5 m, Med =
±10 m, High = ±15 m, Service = ±10 m (1 Blender unit = 1 m).

**Fix**: `graphics-artist-3d-model` re-exports all Phase 9 building assets
(`res_low_*`, `res_med_*`, `res_high_*`, `com_low_*`, `com_med_*`, `com_high_*`,
`ind_low_*`, `ind_med_*`, `ind_high_*`, `svc_fire_station`, `svc_police_station`,
`svc_power_plant`, `svc_water_tower`) at native world scale. Spec in
`architecture/asset-standards/3d-model-standards.md` §1a confirmed authoritative.

- [x] `architecture/asset-standards/3d-model-standards.md` confirms ±2 m convention is retired
  and native-size table (Low ±5 m / Med ±10 m / High ±15 m / Service ±10 m) is authoritative.
  (`graphics-artist-3d-model`)
- [ ] All Phase 9 `.b3d` building assets re-exported at native world scale; resident in
  `assets/models/buildings/`. (`graphics-artist-3d-model`)

#### Bug 3 — Terrain Flattening Sets All Footprint Tiles to Height 0

**Symptom**: Placing a zone on elevated terrain produces a visible crater — all tiles in the
N×N footprint are flattened to world height 0 instead of the origin tile's terrain height.

**Root cause**: `placeZone()` called `m_terrain->setTileHeight(..., 0.0f)` rather than
sampling the origin tile's height before flattening.

**Fix** (`src/simulation/CitySimulation.cpp` — `placeZone()`): sample origin tile height via
`m_terrain->getHeightAt(tileX, tileZ)` into `flatHeight` before the footprint loop; use
`flatHeight` in all `setTileHeight()` calls inside that loop.

- [x] `placeZone()` samples origin tile height before footprint loop; all footprint tiles are
  flattened to that height. (`graphics-dev-irrlicht`)

#### Bug 4 — Budget Detail Panel Not Visible (No Toggle Wired)

**Symptom**: `BudgetDetailPanel` is constructed and owned by `HUD`, but no UI event triggers
`show()` or `hide()`. The panel is never visible during gameplay.

**Root cause**: `UIManager` had no click handler for the treasury balance label area.

**Fix** (`src/ui/UIManager.h` + `src/ui/UIManager.cpp`): add `m_budgetPanelOpen{false}`
state; wire a click on the treasury label region (x=8, y=8, w=200, h=48 → region 8–208, 8–56)
to toggle `m_hud->getBudgetDetail()->show()/hide()`; add ESC handling (Priority 4a); call
`hide()` + reset flag in `transitionToMainMenu()`.

- [x] Click on treasury balance area toggles `BudgetDetailPanel`. (`graphics-dev-irrlicht`)
- [x] ESC closes the budget panel when open. (`graphics-dev-irrlicht`)
- [x] `transitionToMainMenu()` hides and resets the budget panel. (`graphics-dev-irrlicht`)

#### Sign-off Gap — `doDensityUnlockTick()` Auto-Demolish / Retry / Cancel Logic Missing (FAIL)

**Symptom**: Sign-off `doDensityUnlockTick` test FAIL — the density upgrade tick had no
expanded-footprint check, no same-zone-type neighbour auto-demolition, no retry counter, and
no 12-retry CRITICAL toast.

**Root cause**: Phase 11h spec requires that when a Low→Med or Med→High upgrade is triggered,
the code checks the expanded N×N footprint, silently demolishes same-zone-type lower-density
neighbours, defers with retry counter if blockers (road/different-zone/OOB) found, and cancels
after 12 retries with a `UpgradeBlocked` CRITICAL toast.

**Fix** (`src/interfaces/simulation_types.h`, `src/simulation/CitySimulation.h`,
`src/simulation/CitySimulation.cpp`):

- Added `NeighbourCleared` and `UpgradeBlocked` to `NotificationType` enum.
- `m_upgradeRetryCount` (`std::unordered_map<int64_t, int>`) tracks retries per origin tile
  (keyed by `tileKey(tileX, tileZ)`); reset on successful upgrade or manual demolish.
- `doDensityUnlockTick()` collects upgrade candidates first (avoid iterator invalidation),
  then for each: scans expanded footprint, auto-demolishes same-zone lower-density neighbours,
  defers (increments retry counter) if road/different-zone/OOB blockers present, cancels after
  12 retries with `UpgradeBlocked` toast.

- [x] `NotificationType::NeighbourCleared` and `NotificationType::UpgradeBlocked` added to
  `simulation_types.h`. (`graphics-dev-irrlicht`)
- [x] `doDensityUnlockTick()` implements expanded-footprint check, same-zone-type
  auto-demolish, retry counter (12-retry cancel + `UpgradeBlocked` CRITICAL toast), and
  iterator-safe candidate collection. (`graphics-dev-irrlicht`)
- [x] `demolishTile()` already erases `m_upgradeRetryCount[key]` on manual demolish — verified
  no additional change needed. (`graphics-dev-irrlicht`)

#### Bug 5 — Building World Position Off by Half-Tile (±5 m in X and Z)

**Symptom**: All placed buildings are offset approximately one half-tile (5 m) to the
north-east — they overlap the street on two sides and leave a gap on the other two.

**Root cause**: `placeBuildingMesh()` computed the world centre of the N×N footprint as:

```cpp
worldCentreX = (tileX + (footprintN - 1) * 0.5f) * kTileSize;
```

For Low (N=1) this gives `tileX * 10` — the tile *corner*, not the tile *centre*
(`(tileX + 0.5) * 10`). The correct formula is `(tileX + footprintN * 0.5f) * kTileSize`
so that:

- Low (N=1): `(tileX + 0.5) * 10` — tile centre ✓
- Med (N=2): `(tileX + 1.0) * 10` — centre of 2×2 block ✓
- High (N=3): `(tileX + 1.5) * 10` — centre of 3×3 block ✓

**Fix** (`src/rendering/IrrlichtRenderer.cpp` — `placeBuildingMesh()`): replace
`(footprintN - 1) * 0.5f` with `footprintN * 0.5f` in both `worldCentreX` and
`worldCentreZ`.

- [x] `placeBuildingMesh()` centres buildings at `(tileX + N*0.5) * kTileSize`. (`graphics-dev-irrlicht`)

#### Bug 6 — E/W Center-Line Strip Partially Invisible (PolygonOffsetFactor Overwritten)

**Symptom**: The white center-line on E/W road tiles is faint or intermittently invisible —
z-fighting against the asphalt carriageway surface.

**Root cause**: After building the 4-buffer road mesh (carriageway=buf0, south-kerb=buf1,
north-kerb=buf2, center-line=buf3), `placeRoadMesh()` iterates *all* node materials and
unconditionally sets `PolygonOffsetFactor = 4`. This overwrites the `factor = 5` set on
the center-line buffer (buf3) during mesh creation, collapsing the differential that kept
the center-line above the carriageway surface.

**Fix** (`src/rendering/IrrlichtRenderer.cpp` — `placeRoadMesh()` post-bind loop): skip
the factor assignment for material index 3 (center-line):

```cpp
if (m != 3) mat.PolygonOffsetFactor = 4;
```

Buffer 3 retains `PolygonOffsetFactor = 5` from mesh creation, ensuring the center-line
consistently wins the depth test against the carriageway (factor 4) regardless of camera
angle or terrain slope.

- [x] Center-line `PolygonOffsetFactor` (5) is not overwritten by the post-bind loop. (`graphics-dev-irrlicht`)
