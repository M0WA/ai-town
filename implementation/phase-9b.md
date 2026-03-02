## Phase 9b: World Interaction — Tile Placement & Ray-Cast Dispatch

### Goal

Wire the player's mouse clicks to the simulation placement API: terrain tile ray-casting from
cursor position, active-tool state tracking in `UIManager`, tile hover highlight and zone colour
overlay, and left-click dispatch to `ICitySimulation::placeZone` / `placeRoad` / `demolishTile`
— making the city buildable for the first time.

### Background & Scope Rationale

Phase 6 delivered the full `ICitySimulation` placement API (`placeZone`, `placeRoad`,
`demolishTile`, `queryTile`). Phase 8 delivered the toolbar buttons, the 6-priority input
arbitration chain, and the demolish confirmation modal. Phase 9 delivers the 3D building and
road assets. None of those phases wired a mouse click to an actual simulation mutation.

The gap is entirely in three layers:

1. **Renderer** — screen-to-world ray-cast, hover highlight, zone colour overlay (new methods
   on `IRenderer` / `IrrlichtRenderer`).
2. **UIManager / game loop** — active-tool enum state, per-frame hover update, left-click
   dispatch with earthworks cost pre-computation, query tool wiring.
3. **Tests** — `MockRenderer` extensions, unit tests for dispatch logic with `ManualTerrainQuery`
   injected stubs.

**What is not re-delivered here**: the `ICitySimulation` interface methods (Phase 6),
`ITerrainQuery::getSlopeDegrees` (Phase 5), `UIScaler::unproject` (Phase 1), the demolish
confirmation modal (Phase 8), and the `HUD::setActiveToolLabel` indicator (Phase 8).

---

### Deliverables

#### A. `ActiveTool` Enum and `UIManager` State Tracking

- [ ] Define `enum class ActiveTool { None, Zone, Road, Utilities, Demolish, Query }` in
  `src/ui/ui_types.h` alongside the existing `GameState`/`GameMode` enums. `None` means no tool
  is active (camera-only mode, same as current Phase 8 state). (ref:
  `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/hotkey-scheme.md`)

- [ ] Add `ActiveTool m_activeTool{ActiveTool::None}` private member to `UIManager`. Add a
  public getter `ActiveTool getActiveTool() const;` for test observability. (ref:
  `architecture/ui-ux/input-arbitration.md` Priority 5)

- [ ] Extend Priority-5 toolbar dispatch in `UIManager::onEvent()` to set `m_activeTool` in
  addition to calling `HUD::setActiveToolLabel`. Hotkey bindings Z/R/U/D also set
  `m_activeTool` at this priority level. Query tool (I key / Query button): toggles between
  `ActiveTool::Query` and `ActiveTool::None`; existing inspector open/close logic is unchanged.
  (ref: `architecture/ui-ux/hotkey-scheme.md`, `architecture/ui-ux/input-arbitration.md`)

  Updated dispatch table (extends Phase 8 table — adds `m_activeTool` column):

  | Button / Key | y-range or key | `m_activeTool` set | `setActiveToolLabel` |
  |---|---|---|---|
  | Zone / Z | y:64–112 | `ActiveTool::Zone` | "Zone" |
  | Road / R | y:120–168 | `ActiveTool::Road` | "Road" |
  | Utilities / U | y:176–224 | `ActiveTool::Utilities` | "Utilities" |
  | Demolish / D | y:232–280 | `ActiveTool::Demolish` | "Demolish" |
  | Query / I | y:288–336 | toggle `Query`/`None` | "Query"/"No tool" |

#### B. `IRenderer` — Terrain Tile Ray-Cast Interface

- [ ] Add the following method to `IRenderer` (`src/interfaces/IRenderer.h`):

  ```cpp
  // Pick the terrain tile grid coordinate under screen pixel (screenX, screenY).
  // Returns true and sets tileX/tileZ if the ray intersects the terrain heightmap.
  // Returns false if the ray misses the terrain (sky, off-map).
  // screenX/screenY are physical screen-space pixels (not virtual 1920x1080 space).
  // Callers must un-project via UIScaler first if they have virtual coordinates.
  // main-thread-only.
  virtual bool pickTerrainTile(int screenX, int screenY,
                               int& tileX, int& tileZ) const = 0;
  ```

  (ref: `architecture/graphics-architecture/procedural-terrain.md` — `TerrainChunk`
  heightmap query API; `architecture/ui-ux/input-arbitration.md` — Priority 6 world layer)

- [ ] Implement `IrrlichtRenderer::pickTerrainTile()` in `src/rendering/IrrlichtRenderer.cpp`:
  - Build a screen-to-world ray using Irrlicht's scene manager collision system:
    `smgr->getSceneCollisionManager()->getRayFromScreenCoordinates(pos, camera)`.
  - Intersect the ray with the terrain heightmap using a linear march (step size = `cellSize / 2`;
    maximum 4096 steps or until the ray exits map bounds). At each step, sample height via
    `TerrainChunk::getHeightAt(worldX, worldZ)` (the existing heightmap query API from Phase 5).
  - Convert the world-space hit point to tile grid coordinates:
    `tileX = static_cast<int>(hitX / cellSize)`, `tileZ = static_cast<int>(hitZ / cellSize)`.
  - Clamp to map bounds `[0, mapTilesX-1] × [0, mapTilesZ-1]`; return false if no hit found.
  - `IrrlichtRenderer` must store a non-owning pointer to `TerrainSystem` (set via a new method
    `setTerrainSystem(TerrainSystem* ts)` called from `main.cpp` after terrain generation).
  - **NOTE**: `IrrlichtRenderer` forward-declares `TerrainSystem`; the full include lives in
    `.cpp` only to preserve the Irrlicht-free nature of `IRenderer.h`. (ref:
    `architecture/graphics-architecture/procedural-terrain.md`)

- [ ] Add `virtual bool pickTerrainTile(int, int, int&, int&) const = 0;` to `MockRenderer`
  in `tests/rendering/mock_renderer.h`. Default mock action: return `false`. (ref:
  `architecture/testing/testability-architecture.md`)

#### C. `IRenderer` — Tile Hover Highlight and Zone Colour Overlay

- [ ] Add the following methods to `IRenderer` (`src/interfaces/IRenderer.h`):

  ```cpp
  // Render a single-tile wireframe hover highlight at the given tile grid coordinate.
  // RGBA colour is encoded as 0xAARRGGBB. Pass kInvalidTile (-1,-1) to clear.
  // Called once per frame from the game loop, before endFrame().
  // main-thread-only.
  virtual void setTileHoverHighlight(int tileX, int tileZ, uint32_t rgba) = 0;

  // Render a semi-transparent colour fill overlay for all zoned tiles.
  // Colour scheme: R=0x6000FF00, C=0x600000FF, I=0x60FFFF00 (alpha=0x60 ≈ 38%).
  // overlay is a flat array of (mapTilesX * mapTilesZ) uint32_t RGBA values;
  // index = tileZ * mapTilesX + tileX. Value 0 = no overlay (transparent).
  // Called once per budget tick when zone layout changes.
  // main-thread-only.
  virtual void setZoneOverlay(int mapTilesX, int mapTilesZ,
                              const std::vector<uint32_t>& overlay) = 0;
  ```

  (ref: `architecture/ui-ux/hud-layout.md` — active tool cursor shapes per tool mode;
  `architecture/game-design/zoning-system.md` — zone types R/C/I)

- [ ] Implement `IrrlichtRenderer::setTileHoverHighlight()`: draw a single quad (four vertices at
  the tile's world corners, Y = terrain height at tile centre, height sampled from
  `TerrainSystem`) using `IVideoDriver::draw3DLine()` in wireframe style, or a flat overlay mesh
  using `SMeshBuffer` with `EMT_TRANSPARENT_ALPHA_CHANNEL`. The overlay is re-drawn every frame
  while a tile is hovered. When `tileX == -1`, clear the highlight (no draw call). (ref:
  `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] Implement `IrrlichtRenderer::setZoneOverlay()`: maintain an internal `SMesh*` overlay
  plane (one quad per non-zero cell) rendered with `EMT_TRANSPARENT_ALPHA_CHANNEL`. Rebuild the
  overlay mesh when `setZoneOverlay()` is called. Drop the old mesh via the standard
  `SceneEntityManager::destroy()` sequence before rebuilding. (ref:
  `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/graphics-architecture/texture-cache.md`)

- [ ] Add no-op stubs for `setTileHoverHighlight` and `setZoneOverlay` to `MockRenderer`.

#### D. Game Loop — Per-Frame Hover and Left-Click Dispatch

- [ ] In `UIManager::onEvent()`, after all six priority levels return `false` (unconsumed events
  reach the world layer), handle `MouseMove` and `MouseButtonDown button=0` (left-click) events
  when `m_state == GameState::Gameplay` and `m_activeTool != ActiveTool::None`. This is the
  **world-interaction layer** that sits logically after Priority 6 in the arbitration chain.
  (ref: `architecture/ui-ux/input-arbitration.md` Priority 6 comment: "simulation tool actions
  (zone/road/demolish placement) fall through after Priority 6 to the game world")

  **Implementation note**: `UIManager::onEvent()` returns `false` at Priority 6 for camera
  events. The world-interaction layer is an additional post-Priority-6 block executed only when
  `m_renderer` is non-null and the event type is `MouseMove` or `MouseButtonDown button=0`. It
  does NOT consume `MouseMove` (camera edge-scroll must still work) but DOES consume left-click
  when a non-Query placement tool is active and the ray-cast hits terrain (returns `true`).

- [ ] `UIManager` requires access to `IRenderer*` for `pickTerrainTile()` calls. Add
  `IRenderer* m_renderer{nullptr}` as a private member, set via a new method
  `void setRenderer(IRenderer* renderer)`. Called from `main.cpp` after `IrrlichtRenderer` is
  constructed. This avoids changing the locked 4-parameter constructor signature. (ref:
  `architecture/ui-ux/ui-manager.md`)

- [ ] `UIManager` requires access to `ITerrainQuery*` for earthworks cost pre-computation. Add
  `ITerrainQuery* m_terrain{nullptr}` as a private member, set via
  `void setTerrainQuery(ITerrainQuery* terrain)`. Called from `main.cpp` after
  `TerrainSystem` is constructed. (ref: `architecture/game-design/terrain-interaction.md`)

- [ ] **MouseMove handler** (world interaction layer, only when `m_activeTool != None`):
  - Call `m_renderer->pickTerrainTile(event.screenX, event.screenY, tileX, tileZ)`.
  - If hit: compute hover highlight colour per active tool:
    - Zone: `0x80FFFF00` (semi-transparent yellow)
    - Road: `0x8000FFFF` (semi-transparent cyan)
    - Utilities: `0x80FF8000` (semi-transparent orange)
    - Demolish: `0x80FF0000` (semi-transparent red)
    - Query: `0x80FFFFFF` (semi-transparent white)
  - Call `m_renderer->setTileHoverHighlight(tileX, tileZ, colour)`.
  - Store `m_hoveredTile = {tileX, tileZ}` for the left-click handler.
  - If no hit: call `m_renderer->setTileHoverHighlight(-1, -1, 0)` to clear.
  (ref: `architecture/ui-ux/hud-layout.md` — cursor shape per tool mode)

- [ ] **Left-click handler** (world interaction layer, only when `m_activeTool != None` and
  `m_hoveredTile` is valid):
  - **Zone tool**: call `m_sim->placeZone(tileX, tileZ, selectedZoneType, selectedDensityTier,
    earthworksCost)` where `earthworksCost` is computed as:
    `slope = m_terrain->getSlopeDegrees(tileX, tileZ);
     factor = std::clamp((slope - 15.0f) / 30.0f, 0.0f, 2.0f);
     earthworksCost = (slope > 15.0f) ? static_cast<int>(500.0f * factor) : 0;`
    This matches the formula in `architecture/game-design/terrain-interaction.md` exactly.
    Slope guard: if `slope > 15.0f` and earthworks cost would exceed treasury balance (queried
    via `m_sim->getTreasuryBalance()`), show a Normal toast: "Earthworks required — insufficient
    funds (cost: $X)." Do not call `placeZone`. Mark unsaved changes via
    `setUnsavedChanges(true)` on success. Refresh zone overlay via `m_renderer->setZoneOverlay`.
  - **Road tool**: call `m_sim->placeRoad(tileX, tileZ, earthworksCost)` (same earthworks
    computation). Same slope guard and unsaved-changes marking.
  - **Utilities tool**: call `m_sim->placeZone(tileX, tileZ, ZoneType::Utility,
    DensityTier::Low, earthworksCost)` — Utilities are zoned tiles in V1; the zone type
    `ZoneType::Utility` must already be present on `ICitySimulation` per Phase 6 delivery. If
    `ZoneType::Utility` is absent (spec gap), flag it explicitly in the Phase 9b kick-off review
    before implementation begins. (ref: `architecture/game-design/zoning-system.md`)
  - **Demolish tool**: if the demolish confirmation modal has NOT been suppressed (Settings >
    Gameplay "Confirm before demolish" is ON, the default), show the Phase 8 demolish
    confirmation modal with tile count = 1. On confirmation (or if suppressed), call
    `m_sim->demolishTile(tileX, tileZ)`. Mark unsaved changes. Refresh zone overlay.
  - **Query tool**: call `m_sim->queryTile(tileX, tileZ)` and pass the resulting `QueryResult`
    to `InspectorPanel::populate(result, tileX, tileZ)`. Open the inspector panel.
    Left-click in Query mode DOES NOT consume the event if the inspector panel is already
    handling its own dismiss-click logic at Priority 3 — the query dispatch only fires when
    the click has passed through Priority 3 (i.e., the inspector is not already open, or
    the click is outside the panel bounds per the Priority 3 pass-through logic).
  (ref: `architecture/game-design/terrain-interaction.md`, `architecture/ui-ux/input-arbitration.md`,
  `architecture/ui-ux/modal-dialog-system.md`)

- [ ] **Zone sub-panel** (Zone tool active): a compact sub-panel appears immediately to the right
  of the toolbar (virtual x:80 px, y:64 px) showing the 9 zone-type + density combinations
  (R/C/I × Low/Medium/High) as a 3×3 button grid. Active selection highlighted. `UIManager`
  tracks `ZoneType m_selectedZoneType{ZoneType::Residential}` and
  `DensityTier m_selectedDensityTier{DensityTier::Low}`. Sub-panel is hidden when Zone tool is
  not active. (ref: `architecture/game-design/zoning-system.md`,
  `architecture/ui-ux/hud-layout.md`)

- [ ] **Zone overlay refresh**: after any `placeZone`, `placeRoad`, or `demolishTile` call that
  succeeds (returns without throwing — `ICitySimulation` mutations are fire-and-forget), rebuild
  the zone overlay by iterating all tiles via `m_sim->queryTile` for each tile within the
  viewport, or by maintaining a `std::vector<uint32_t>` overlay array updated incrementally.
  Incremental update is preferred: at placement, set `overlay[tileZ * mapW + tileX] = colour`;
  at demolish, clear to 0. Pass to `m_renderer->setZoneOverlay()`. (ref:
  `architecture/game-design/zoning-system.md`)

#### E. `ITerrainQuery` — Tile Height Query (New Method)

- [ ] Add `virtual float getHeightAt(int tileX, int tileZ) const = 0;` to `ITerrainQuery`
  (`src/interfaces/ITerrainQuery.h`). Returns Y-axis terrain height in world-space metres for
  the tile centre. Returns 0.0f for out-of-bounds coordinates. `TerrainSystem` already exposes
  `getHeightAt` per Phase 5's `TerrainChunk` heightmap query API — Phase 9b promotes this to
  the `ITerrainQuery` interface so the game loop can use it for zone overlay Y-height without
  a direct dependency on `TerrainSystem`. (ref:
  `architecture/graphics-architecture/procedural-terrain.md` — `TerrainChunk` heightmap query
  API)

#### F. `InspectorPanel` — Real Tile Query Wiring

- [ ] Add `void populate(const QueryResult& result, int tileX, int tileZ)` to `InspectorPanel`
  (already exists as a Phase 8 stub with empty body). Fill in the real implementation: set the
  zone type / density label, demand score, desirability, tax yield/month, and demand pressure %
  fields from `result`. Panel opens at cursor + 40 px offset in virtual space per the
  `query-inspector-panel.md` three-step cascade position rule. (ref:
  `architecture/ui-ux/query-inspector-panel.md`)

- [ ] Wire `InspectorPanel` data refresh cadence: budget/economy fields refresh once per budget
  tick (poll `m_sim->queryTile` again if `m_inspectorOpen && ticks_since_open > 0`); traffic
  data refresh every 10 simulation frames. "Updated N seconds ago" line shown when data is >1 s
  stale. (ref: `architecture/ui-ux/query-inspector-panel.md`)

#### G. Tests

- [ ] `WorldInteraction_ZonePlacement_CallsPlaceZone` (unit test in `tests/ui/`): construct
  `UIManager` with `NiceMock<MockCitySimulation>`, `NiceMock<MockRenderer>`,
  `ManualTerrainQuery` (slope = 0°), `NiceMock<MockUIBackend>`, `ManualClock`. Set active tool
  to `ActiveTool::Zone`. Stub `MockRenderer::pickTerrainTile` to return `true` and set
  tileX=5, tileZ=7. Send a `MouseButtonDown button=0` event.
  `EXPECT_CALL(sim_, placeZone(5, 7, _, _, 0)).Times(1);` (ref:
  `architecture/testing/testability-architecture.md`)

- [ ] `WorldInteraction_RoadPlacement_CallsPlaceRoad` (unit test): same fixture; tool =
  `ActiveTool::Road`. `EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);`

- [ ] `WorldInteraction_DemolishTool_SteepSlope_NoEarthworksGuard` (unit test): Demolish tool
  does not have an earthworks guard (demolish does not incur earthworks cost per spec —
  `architecture/game-design/terrain-interaction.md` only defines earthworks cost for placement).
  Verify `demolishTile` is called even at slope > 15°. (ref:
  `architecture/game-design/terrain-interaction.md`)

- [ ] `WorldInteraction_ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace` (unit test):
  `ManualTerrainQuery` slope = 30°. `MockCitySimulation::getTreasuryBalance()` returns 0.0f
  (insufficient for earthworks). Verify `placeZone` is NOT called; verify a toast is posted via
  `NotificationManager` (use `EXPECT_CALL(backend_, addStaticText(HasSubstr("insufficient funds"),
  _, _, _, _)).Times(AtLeast(1))`). (ref: `architecture/game-design/terrain-interaction.md`)

- [ ] `WorldInteraction_QueryTool_CallsQueryTile` (unit test): Query tool active; left-click;
  `EXPECT_CALL(sim_, queryTile(5, 7)).Times(1)`; verify inspector panel becomes open (check via
  `UIManager::getActiveTool()` still Query, and `m_backend` call for inspector panel creation).

- [ ] `WorldInteraction_NoActiveTool_LeftClickIgnored` (unit test): `m_activeTool == None`;
  left-click; `EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);`
  `EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);`

- [ ] `WorldInteraction_ModalActive_LeftClickNotDispatched` (unit test): `hasActiveModal()==true`
  (Priority 1 consumes the event); left-click must NOT reach the world-interaction layer.
  `EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);`

- [ ] `WorldInteraction_HoverHighlight_SetOnMouseMove` (unit test): `MockRenderer` stubs
  `pickTerrainTile` to return true at (3, 4). Send `MouseMove` event with Zone tool active.
  `EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));`

- [ ] `WorldInteraction_HoverHighlight_ClearedOnMiss` (unit test): `pickTerrainTile` returns
  false. `EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, 0)).Times(AtLeast(1));`

- [ ] All new tests are labelled unit (no `requires-opengl`); added to `ui_tests` CMake target
  via `target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp)`. Do NOT call
  `add_executable` or `aitown_add_tests` again for `ui_tests` (duplicate target error). (ref:
  `architecture/testing/framework.md`)

#### H. `main.cpp` Wiring

- [ ] After `TerrainSystem` is constructed in `main.cpp`, call:
  - `renderer.setTerrainSystem(&terrainSystem);`
  - `uiManager.setRenderer(&renderer);`
  - `uiManager.setTerrainQuery(&terrainSystem);`
  These calls replace the Phase 8 stub wiring where the UIManager had no renderer or terrain
  reference. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)

- [ ] In the main game loop (after `uiManager.update(dt)`), call
  `renderer.setZoneOverlay(...)` if the overlay dirty flag is set (set by any successful
  placement or demolish in the current frame). The dirty flag is managed by `UIManager`.

#### I. `ZoneType::Utility` Spec Gap Investigation

- [ ] **SPEC GAP — FLAG FOR RESOLUTION**: The Utilities toolbar button (Phase 8) maps to a
  placement action in Phase 9b, but `architecture/game-design/zoning-system.md` defines only
  `Residential (R)`, `Commercial (C)`, and `Industrial (I)` zone types. Power plants and water
  towers are referenced in `architecture/game-design/service-coverage.md` as service buildings
  placed by the player, not as zone tiles. **Resolution required before Phase 9b
  implementation**: `gamedesign-lookandfeel` must clarify whether Utilities placement maps to
  (a) a `ZoneType::Utility` on `ICitySimulation`, (b) a separate `placeServiceBuilding(type,
  tile)` API not yet on the interface, or (c) a different dispatch path. This is a genuine
  spec contradiction between the HUD toolbar (which has a Utilities button implying a placement
  action) and the zoning spec (which does not define a Utilities zone type). **The Phase 9b
  kick-off is BLOCKED on this clarification.** Utilities tool click must NOT silently no-op.

---

### Exit Criteria

- Left-clicking on a terrain tile with Zone tool active calls `ICitySimulation::placeZone`
  with the correct `ZoneType`, `DensityTier`, and earthworks cost (0 on flat tiles;
  `500 × clamp((slope - 15) / 30, 0, 2)` on slopes > 15°)
- Left-clicking with Road tool calls `ICitySimulation::placeRoad` with correct earthworks cost
- Left-clicking with Demolish tool opens the Phase 8 confirmation modal (or calls
  `demolishTile` directly if the modal has been suppressed in Settings)
- Left-clicking with Query tool opens the `InspectorPanel` populated with live `QueryResult`
  data from `ICitySimulation::queryTile`
- Hovering over a terrain tile with any non-None tool active renders a semi-transparent
  colour highlight on that tile within the same frame
- Zoned tiles display a semi-transparent colour overlay (R=green, C=blue, I=yellow) at all
  times during gameplay
- Slope guard: when earthworks cost exceeds treasury balance, a Normal toast is shown and
  `placeZone` / `placeRoad` is NOT called
- All 9 unit tests in `WorldInteractionTest` pass under `ctest -LE "integration|requires-opengl"`
- `UIManager::getActiveTool()` returns the correct `ActiveTool` after each toolbar button click
  and hotkey press (verified by `WorldInteraction_NoActiveTool_LeftClickIgnored` and related tests)
- Zone overlay is visually correct: newly placed tiles appear in the correct zone colour within
  one frame of placement; demolished tiles clear within one frame
- **Spec gap I resolved**: `gamedesign-lookandfeel` clarification on Utilities placement is on
  record in `architecture/game-design/service-coverage.md` before implementation begins
- `all-checks-pass` CI gate remains green after Phase 9b changes land

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `IRenderer::pickTerrainTile` + `setTileHoverHighlight` + `setZoneOverlay` implementation; `IrrlichtRenderer` terrain-system pointer wiring; `ITerrainQuery::getHeightAt` promotion; `main.cpp` wiring (`setRenderer`, `setTerrainQuery`, `setTerrainSystem`) |
| `gamedesign-ux` | Zone sub-panel layout (3×3 button grid, virtual x:80 px); zone colour scheme (R/C/I); hover colour scheme per tool mode; confirm Utilities spec gap resolution with `gamedesign-lookandfeel` |
| `gamedesign-lookandfeel` | **BLOCKING**: resolve Utilities placement spec gap (Deliverable I); confirm earthworks cost gate behaviour is consistent with economy balance; confirm zone/road placement costs are correct for V1 difficulty tiers |
| `test-dev-cpp` | All 9 `WorldInteractionTest` unit tests in `tests/ui/world_interaction_test.cpp`; `MockRenderer` extension stubs; `ManualTerrainQuery` extension for `getHeightAt` |
| `graphics-dev-irrlicht` | `InspectorPanel::populate()` real implementation; `InspectorPanel` data refresh cadence wiring |

---

### Dependencies

- Requires Phase 6 complete (`ICitySimulation::placeZone`, `placeRoad`, `demolishTile`,
  `queryTile`, `getTreasuryBalance` — all on the interface and implemented in `CitySimulation`)
- Requires Phase 8 complete (6-priority input arbitration chain, `UIManager::onEvent()`, demolish
  confirmation modal, `HUD::setActiveToolLabel`, zone sub-panel placeholder bounds in toolbar)
- Requires Phase 9 complete or parallel (zone colour overlay requires building asset presence on
  terrain tiles for visual correctness; Phase 9b overlay rendering works with or without Phase 9
  3D assets — the overlay is a 2D mesh layer independent of building geometry)
- Phase 10 (Dynamic Soundscape) depends on Phase 9b: `sfx_build_place`, `sfx_build_demolish`,
  and `sfx_road_build` SFX callbacks must fire from real placement dispatch, not stubs
- Phase 11 (Save System) is unaffected: zone layout serialisation reads from `CitySimulation`
  internal state (already complete in Phase 6); Phase 9b does not change save format

---

### Risks & Spikes

- **RISK**: `IrrlichtRenderer::pickTerrainTile` ray-march may be too slow for real-time hover
  (60 FPS budget = 16 ms; a 4096-step march on a 1024-tile map with heightmap sampling could
  exceed 2 ms on weak hardware). **Spike**: prototype the ray-march on the benchmark tool
  (`aitown_benchmark`) and measure per-frame cost. If > 1 ms: switch to a grid-intersection
  approach (project camera ray onto the Y=0 plane, then use the heightmap to refine the
  intersection — O(1) for flat terrain). Resolution required before `IrrlichtRenderer` pick
  implementation begins.

- **RISK**: Zone overlay `SMesh*` rebuild on every placement call may cause a visible 1-frame
  hitch on large maps (1024×1024 = 1M tiles, most zero). **Mitigation**: store only non-zero
  tiles in a `std::unordered_map<uint64_t, uint32_t>` sparse map; `setZoneOverlay` only
  generates quads for non-zero entries. Cap at 100K simultaneous overlay quads for V1.

- **RISK**: `UIManager` currently has a locked 4-parameter constructor; adding `m_renderer` and
  `m_terrain` via setter methods rather than constructor parameters avoids breaking all existing
  UIManager unit tests. **Verify**: confirm that `NiceMock<MockRenderer>` can be passed via
  `setRenderer()` in test fixtures without introducing `UIManager` constructor changes that
  cascade to Phase 8 test fixtures. If the Phase 8 tests use bare `UIManager` construction,
  the setters are safe — they are no-ops if not called (nullptr guard in dispatch).

- **RISK**: `ZoneType::Utility` spec gap (Deliverable I) could block the entire phase if not
  resolved promptly. **Spike**: `gamedesign-lookandfeel` to resolve within 3 days of Phase 9b
  kick-off; escalate to product owner if not resolved. Interim mitigation: stub the Utilities
  left-click to show a Normal toast "Utilities placement not yet configured" so the game loop
  does not crash.
