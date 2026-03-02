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
    maximum 4096 steps or until the ray exits map bounds). At each step, convert the current
    world-space ray position to tile indices (`stepTileX = static_cast<int>(rayX / cellSize)`,
    `stepTileZ = static_cast<int>(rayZ / cellSize)`), clamp to map bounds, then sample height
    via `m_terrain->getHeightAt(stepTileX, stepTileZ)` (the `ITerrainQuery` interface method
    that `TerrainSystem` implements, injected via `setTerrainQuery()`). `getHeightAt()` takes
    integer tile indices, not world-space floats — the conversion must happen at every march step.
  - Convert the world-space hit point to tile grid coordinates:
    `tileX = static_cast<int>(hitX / cellSize)`, `tileZ = static_cast<int>(hitZ / cellSize)`.
  - Clamp to map bounds `[0, mapTilesX-1] × [0, mapTilesZ-1]`; return false if no hit found.
  - `IrrlichtRenderer` must store a non-owning `ITerrainQuery* m_terrain{nullptr}` pointer
    (set via a new method `setTerrainQuery(ITerrainQuery* terrain)` called from `main.cpp`
    after terrain generation). `IrrlichtRenderer` does NOT hold a pointer to the concrete
    `TerrainSystem` class — only to the `ITerrainQuery` interface.
  - **NOTE**: `IrrlichtRenderer` forward-declares `ITerrainQuery`; the full include of
    `ITerrainQuery.h` lives in `.cpp` only to preserve the Irrlicht-free nature of `IRenderer.h`.
    The concrete `TerrainSystem` type is never mentioned in any `IrrlichtRenderer` header. (ref:
    `architecture/graphics-architecture/procedural-terrain.md` — Heightmap Query API)

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
  // sparseOverlay maps (tileZ * mapTilesX + tileX) -> RGBA color for tiles
  // with a non-zero zone overlay; tiles absent from the map have transparent
  // (no) overlay. Capped at 100K simultaneous entries for V1.
  // Called once per budget tick when zone layout changes.
  // main-thread-only.
  virtual void setZoneOverlay(int mapTilesX, int mapTilesZ,
                              const std::unordered_map<uint64_t, uint32_t>& sparseOverlay) = 0;
  ```

  (ref: `architecture/ui-ux/hud-layout.md` — active tool cursor shapes per tool mode;
  `architecture/game-design/zoning-system.md` — zone types R/C/I)

- [ ] Implement `IrrlichtRenderer::setTileHoverHighlight()`: build a single-quad `SMeshBuffer`
  with four vertices at the tile's world corners (Y = terrain height at tile centre, sampled
  from `TerrainSystem`). Set the material type to `EMT_TRANSPARENT_ALPHA_CHANNEL`. Apply the
  hover RGBA colour to every vertex via `SMeshBuffer::Vertices[i].Color` (one `SColor` per
  corner). After populating all vertices, call `buf->recalculateBoundingBox()` on the
  `SMeshBuffer`, then call `m->recalculateBoundingBox()` on the parent `SMesh*`, before passing
  the mesh to `IVideoDriver::drawMeshBuffer()` (required even when not attaching to the scene
  graph — the renderer may perform its own frustum check against the bounding box).

  **Memory lifecycle**: `IrrlichtRenderer` stores the hover highlight mesh as a private member
  `SMesh* m_hoveredTileMesh{nullptr}`. On each call to `setTileHoverHighlight()`:
  - If `m_hoveredTileMesh` is non-null, call `m_hoveredTileMesh->drop()` and set it to
    `nullptr` before creating a new mesh.
  - If `tileX == -1` (clear request): call `m_hoveredTileMesh->drop()` (if non-null), set
    `m_hoveredTileMesh = nullptr`, and return without creating a new mesh — no draw call is
    issued.
  - Otherwise: create the new `SMesh*`, store it in `m_hoveredTileMesh`, and draw it via
    `IVideoDriver::drawMeshBuffer()` directly in the render pass. The mesh is NOT added to the
    Irrlicht scene graph (avoids scene graph overhead for per-frame rebuild).

  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] Implement `IrrlichtRenderer::setZoneOverlay()`: maintain an internal `SMesh*` overlay
  plane (one quad per entry in `sparseOverlay`) rendered with `EMT_TRANSPARENT_ALPHA_CHANNEL`.
  Rebuild the overlay mesh when `setZoneOverlay()` is called. **Memory management order**:
  (1) Iterate `sparseOverlay` entries; for each, compute quad position from key
  `(tileZ * mapTilesX + tileX)` and generate a coloured quad. Out-of-bounds keys
  (key >= mapTilesX × mapTilesZ) are silently skipped.
  (2) Build the new `SMesh*` overlay from the valid entries. After populating all vertices
  across all `SMeshBuffer` quads, call `buf->recalculateBoundingBox()` on each `SMeshBuffer`,
  then call `m->recalculateBoundingBox()` on the parent `SMesh*`, before attaching the mesh to
  a scene node — omitting this causes silent frustum culling failures where overlay quads that
  should be visible are not rendered.
  (3) If the new mesh is valid: call `SceneEntityManager::destroy(m_overlayNode)` on the
  previous overlay scene node (if any) to destroy the old node, then attach the new mesh to a
  new `ISceneNode*` and store it as `m_overlayNode` via `SceneEntityManager`. (4) If mesh
  construction fails, log error and leave the previous overlay scene node in place. (ref:
  `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/graphics-architecture/texture-cache.md`)

  **Lifecycle distinction — overlay vs hover highlight**: the zone overlay mesh IS attached to
  the Irrlicht scene graph as a persistent `ISceneNode*` (stored as `m_overlayNode` via
  `SceneEntityManager`). On each `setZoneOverlay()` call the OLD scene node is destroyed via
  `SceneEntityManager::destroy(m_overlayNode)` before the new mesh is created and attached.
  This is the OPPOSITE of `setTileHoverHighlight()`, which keeps its mesh completely OUT of the
  scene graph and draws it every frame with `IVideoDriver::drawMeshBuffer()`. The zone overlay
  uses a scene node because it persists across many frames between placement events — attaching
  it once and letting the scene graph render it each frame is cheaper than issuing a raw draw
  call every frame for a mesh that rarely changes.

- [ ] Add no-op stubs for `setTileHoverHighlight` and `setZoneOverlay` (with the sparse-map
  signature) to `MockRenderer`.

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
    - Zone: `0x80FF00FF` (semi-transparent magenta)
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
  - **Utilities tool**: call
    `m_sim->placeServiceBuilding(tileX, tileZ, m_selectedServiceBuilding, earthworksCost)`.
    `m_selectedServiceBuilding` is the type currently selected in the Utilities sub-panel
    (default: `ServiceBuildingType::PowerPlant`). Same earthworks computation and
    insufficient-funds guard as the Zone and Road tools. `CitySimulation` enforces the one-
    building-per-tile invariant as a no-op at the API level; no UIManager pre-query is required.
    (ref: `architecture/game-design/service-coverage.md` — Utilities Tool Placement Design)
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
  of the toolbar showing the 9 zone-type + density combinations (R/C/I × Low/Medium/High) as a
  3×3 button grid. Active selection highlighted. `UIManager` tracks
  `ZoneType m_selectedZoneType{ZoneType::Residential}` and
  `DensityTier m_selectedDensityTier{DensityTier::Low}`. Sub-panel is hidden when Zone tool is
  not active.

  **Absolute bounds and layout**:
  - Top-left anchor: virtual (x:80, y:64). The left edge (x:80) begins 8 px to the right of the
    toolbar right edge (x:72), so the sub-panel does not overlap the toolbar.
  - Each button: 64×40 px, with 4 px gap between buttons.
  - Grid layout: 3 columns (R / C / I) × 3 rows (Low / Med / High), left-to-right, top-to-bottom.
  - Total width: (64 × 3) + (4 × 2) = 200 px. Total height: (40 × 3) + (4 × 2) = 128 px.
  - Sub-panel occupies virtual bounds: x:80–280, y:64–192.

  **Rendering**: The Zone sub-panel IS fully rendered in Phase 9b. All 9 buttons are created via
  `IUIBackend::addButton()` during HUD or UIManager `init()`. Visibility is toggled as a group
  via `IUIBackend::setElementVisible()` on each button handle: visible when `m_activeTool ==
  ActiveTool::Zone`, hidden otherwise.

  (ref: `architecture/game-design/zoning-system.md`, `architecture/ui-ux/hud-layout.md`)

- [ ] **Utilities sub-panel** (Utilities tool active): a compact sub-panel appears immediately to
  the right of the toolbar (aligned with the Utilities button row) showing the four service
  building types as a 2×2 button grid:

  | Column 1 | Column 2 |
  |---|---|
  | Power Plant | Water Tower |
  | Fire Station | Police Station |

  Active selection highlighted. `UIManager` tracks
  `ServiceBuildingType m_selectedServiceBuilding{ServiceBuildingType::PowerPlant}`. Each
  button displays the building name and its placement cost (e.g. "Power Plant $10,000"). Sub-panel
  is hidden when Utilities tool is not active.

  **Absolute bounds and layout**:
  - Top-left anchor: virtual (x:80, y:176). The left edge (x:80) begins 8 px to the right of
    the toolbar right edge (x:72), aligned horizontally with the Utilities button row (y:176).
  - Each button: 96×48 px, with 4 px gap between buttons.
  - Grid layout: 2 columns × 2 rows, left-to-right, top-to-bottom.
  - Total width: (96 × 2) + 4 = 196 px. Total height: (48 × 2) + 4 = 100 px.
  - Sub-panel occupies virtual bounds: x:80–276, y:176–276.

  **Rendering**: The Utilities sub-panel IS fully rendered in Phase 9b. All 4 buttons are created
  via `IUIBackend::addButton()` during HUD or UIManager `init()`. Visibility is toggled as a
  group via `IUIBackend::setElementVisible()` on each button handle: visible when `m_activeTool ==
  ActiveTool::Utilities`, hidden otherwise.

- [ ] Both sub-panels (Zone and Utilities) are fully rendered in Phase 9b — UIManager creates
  their buttons at `init()` time and shows/hides them based on active tool via
  `IUIBackend::setElementVisible()`.

  (ref: `architecture/game-design/service-coverage.md` — Utilities Tool Placement Design)

- [ ] **Zone overlay refresh**: after any `placeZone`, `placeRoad`, or `demolishTile` call that
  succeeds (returns without throwing — `ICitySimulation` mutations are fire-and-forget), update
  the zone overlay using a `std::unordered_map<uint64_t, uint32_t> m_overlayMap` sparse overlay
  map stored as a private member of `UIManager`, keyed by `(tileZ * mapW + tileX)`. At
  placement, insert or update the entry: `m_overlayMap[(tileZ * mapW + tileX)] = colour`. At
  demolish, erase the entry: `m_overlayMap.erase(tileZ * mapW + tileX)`. When calling
  `m_renderer->setZoneOverlay(mapW, mapH, m_overlayMap)`, UIManager passes `m_overlayMap`
  directly — the sparse map is the interface contract (tiles absent from the map are
  transparent). The renderer generates overlay quads only for entries present in the sparse map,
  capped at 100K simultaneous overlay quads for V1 (entries beyond this cap are silently
  dropped). On new-game load, UIManager clears `m_overlayMap` and calls
  `setZoneOverlay(mapW, mapH, {})` (empty sparse map) before the new map's zones are set.
  (ref: `architecture/game-design/zoning-system.md`)

#### E. `ITerrainQuery` — Tile Height Query (New Method)

- [ ] Add `virtual float getHeightAt(int tileX, int tileZ) const = 0;` to `ITerrainQuery`
  (`src/interfaces/ITerrainQuery.h`). Returns Y-axis terrain height in world-space metres for
  the tile centre. Returns 0.0f for out-of-bounds coordinates. `TerrainSystem` already exposes
  `getHeightAt` per Phase 5's `TerrainChunk` heightmap query API — Phase 9b promotes this to
  the `ITerrainQuery` interface so the game loop can use it for zone overlay Y-height without
  a direct dependency on `TerrainSystem`. **LOD Contract**: `getHeightAt(int tileX, int tileZ)`
  MUST query TerrainSystem's persistent LOD0 heightmap array, never the active scene-node mesh
  geometry (which may be at LOD1 or LOD2 for distant chunks). The returned value is the exact
  grid-centre height sample with no interpolation. (ref:
  `architecture/graphics-architecture/procedural-terrain.md` — `TerrainChunk` heightmap query
  API)

- [ ] Extend `ManualTerrainQuery` (`tests/simulation/manual_terrain_query.h`) to implement the
  new method:

  ```cpp
  float getHeightAt(int tileX, int tileZ) const override { return 0.0f; }
  ```

  The stub always returns 0.0f — unit tests that need specific heights inject `MockRenderer`
  for the renderer path; no Phase 9b test requires `ManualTerrainQuery` to return non-zero
  heights. This override is required because `getHeightAt()` is pure virtual on `ITerrainQuery`;
  without it `ManualTerrainQuery` fails to compile, blocking all 14 Phase 9b unit tests.

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

- [ ] `WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry` (unit test): construct
  `UIManager` with `NiceMock<MockCitySimulation>`, `NiceMock<MockRenderer>`,
  `ManualTerrainQuery` (slope = 0°), `NiceMock<MockUIBackend>`, `ManualClock`. Set active tool
  to `ActiveTool::Zone`. Stub `MockRenderer::pickTerrainTile` to return `true` at tileX=3,
  tileZ=4. Send `MouseButtonDown button=0`. After dispatch, capture the `sparseOverlay` argument
  passed to `setZoneOverlay`. Assert that the captured map contains exactly 1 entry whose key
  equals `(4 * mapW + 3)` (i.e. the placed tile) and whose value is non-zero.
  `EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).WillOnce(SaveArg<2>(&capturedMap));`

- [ ] `WorldInteraction_Demolish_SparseOverlay_ErasesEntry` (unit test): place a Zone tile at
  (3, 4) via a Zone-tool left-click (same fixture as above; tile is now in `m_overlayMap`).
  Switch active tool to `ActiveTool::Demolish`. Suppress the demolish confirmation modal
  (Settings "Confirm before demolish" = OFF). Send a second `MouseButtonDown button=0` at
  (3, 4). Capture the `sparseOverlay` passed to the second `setZoneOverlay` call. Assert that
  the captured map is empty (the entry was erased on demolish).

- [ ] `WorldInteraction_NewGameLoad_ClearsOverlay` (unit test): pre-populate `m_overlayMap` with
  at least 3 entries by performing 3 zone placements. Trigger new-game load via
  `UIManager::onNewGame()` (or equivalent reset call). Assert that `setZoneOverlay` is called
  with an empty sparse map: the captured `sparseOverlay` argument has `size() == 0`.

- [ ] `WorldInteraction_OverlayCap_100K_StillCalls` (unit test): drive `UIManager` to insert
  100,000 entries into `m_overlayMap` by calling the internal overlay-insert path directly (or
  via 100K simulated placements on distinct tiles). Confirm that when a 100,001st entry would be
  inserted, the cap prevents storage but `setZoneOverlay` is still called (the call is not
  suppressed). Use `EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(1));` and
  assert the captured map has `size() <= 100000`.

- [ ] All new tests are labelled unit (no `requires-opengl`); added to `ui_tests` CMake target
  via `target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp)`. Do NOT call
  `add_executable` or `aitown_add_tests` again for `ui_tests` (duplicate target error). (ref:
  `architecture/testing/framework.md`)

#### H. `main.cpp` Wiring

- [ ] After `TerrainSystem` is constructed and before entering the main game loop, call the
  following methods in this exact order to establish all pointers before the first frame:
  - **(1)** Call `terrainSystem.generate()` and `terrainSystem.buildAllChunks()` (called on
    the concrete `TerrainSystem*` instance — these methods are NOT part of the `ITerrainQuery`
    interface).
  - **(2)** Call `renderer.setTerrainQuery(&terrainSystem)` (passes `TerrainSystem*` as
    `ITerrainQuery*`; the renderer stores it as `m_terrain` and calls only `ITerrainQuery`
    interface methods — no concrete `TerrainSystem` API is used inside `IrrlichtRenderer`).
  - **(3)** Call `uiManager.setRenderer(&renderer)`.
  - **(4)** Call `uiManager.setTerrainQuery(&terrainSystem)`.

  This order guarantees all pointers are valid before the first game-loop frame calls
  `pickTerrainTile()`. These calls replace the Phase 8 stub wiring where the UIManager had no
  renderer or terrain reference. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)

- [ ] In the main game loop (after `uiManager.update(dt)`), call
  `renderer.setZoneOverlay(...)` if the overlay dirty flag is set (set by any successful
  placement or demolish in the current frame). The dirty flag is managed by `UIManager`.

#### I. Utilities Tool Spec — Resolved

<!-- RESOLVED: gamedesign-lookandfeel 2026-03-01 -->
<!-- Decision: service buildings are individually placed objects via placeServiceBuilding(), -->
<!-- NOT zone tiles. ZoneType::Utility does not exist and must not be added. -->

- [x] **SPEC GAP RESOLVED**: The Utilities toolbar button places **service infrastructure
  buildings** (Power Plant, Water Tower, Fire Station, Police Station) as discrete placed
  objects. This is NOT a zone type. `ZoneType::Utility` does not exist and must not be added to
  the codebase.

  **Authoritative decision** (`gamedesign-lookandfeel`, 2026-03-01):

  - A new `enum class ServiceBuildingType { PowerPlant, WaterTower, FireStation, PoliceStation }`
    has been added to `src/interfaces/simulation_types.h`.
  - A new `virtual void placeServiceBuilding(int tileX, int tileZ, ServiceBuildingType type,
    int earthworksCostOverride = 0) = 0;` has been added to `src/interfaces/ICitySimulation.h`.
  - Placement costs (deducted immediately from treasury):
    - Power Plant: $10,000 (`SimulationConstants::service_placement_cost_power_plant`)
    - Water Tower: $3,000 (`SimulationConstants::service_placement_cost_water_tower`)
    - Fire Station: $5,000 (`SimulationConstants::service_placement_cost_fire_station`)
    - Police Station: $4,000 (`SimulationConstants::service_placement_cost_police_station`)
  - Full spec in `architecture/game-design/service-coverage.md` — "Utilities Tool — Placement
    Design (V1)" section.
  - Phase 9b implementation may proceed. **BLOCK CLEARED.**

- [ ] **Phase 9b implementation tasks arising from this resolution**:
  - Add `ServiceBuildingType m_selectedServiceBuilding{ServiceBuildingType::PowerPlant}` to
    `UIManager` private members.
  - Add `placeServiceBuilding` to `MockCitySimulation` in `tests/ui/mock_city_simulation.h`.
  - Add placement-cost constants to `simulation_constants.h`:
    `service_placement_cost_power_plant = 10000`,
    `service_placement_cost_water_tower = 3000`,
    `service_placement_cost_fire_station = 5000`,
    `service_placement_cost_police_station = 4000`.
  - Add `placeServiceBuilding` stub to `CitySimulation` (Phase 9b implementation work —
    not part of this spec/interface clarification).
  - Add unit test `WorldInteraction_UtilitiesPlacement_CallsPlaceServiceBuilding`:
    construct `UIManager` with `NiceMock<MockCitySimulation>`, `NiceMock<MockRenderer>`,
    `ManualTerrainQuery` (slope = 0°). Set active tool to `ActiveTool::Utilities`, selected
    building to `ServiceBuildingType::FireStation`. Stub `MockRenderer::pickTerrainTile` to
    return true at (5, 7). Send `MouseButtonDown button=0`.
    `EXPECT_CALL(sim_, placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0)).Times(1);`

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
- All 14 unit tests in `WorldInteractionTest` pass under `ctest -LE "integration|requires-opengl"`
  (G.1–G.9 plus I—Utilities placement, plus 4 sparse-overlay tests: SparseOverlay_InsertsEntry,
  Demolish_SparseOverlay_ErasesEntry, NewGameLoad_ClearsOverlay, OverlayCap_100K_StillCalls)
- `UIManager::getActiveTool()` returns the correct `ActiveTool` after each toolbar button click
  and hotkey press (verified by `WorldInteraction_NoActiveTool_LeftClickIgnored` and related tests)
- Zone overlay is visually correct: newly placed tiles appear in the correct zone colour within
  one frame of placement; demolished tiles clear within one frame
- **Spec gap I resolved**: Utilities tool places service buildings via
  `ICitySimulation::placeServiceBuilding(tileX, tileZ, ServiceBuildingType, earthworksCost)`.
  `ServiceBuildingType` enum is in `simulation_types.h`. Full placement design (sub-panel layout,
  costs, placement rules) is in `architecture/game-design/service-coverage.md`. BLOCK CLEARED.
- `all-checks-pass` CI gate remains green after Phase 9b changes land

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `IRenderer::pickTerrainTile` + `setTileHoverHighlight` + `setZoneOverlay` implementation; `IrrlichtRenderer` terrain-system pointer wiring; `ITerrainQuery::getHeightAt` promotion; `main.cpp` wiring (`setRenderer`, `setTerrainQuery`, `setTerrainSystem`); `InspectorPanel::populate()` real implementation; `InspectorPanel` data refresh cadence wiring |
| `gamedesign-ux` | Zone sub-panel layout (3×3 button grid, virtual x:80 px); zone colour scheme (R/C/I); hover colour scheme per tool mode; confirm Utilities spec gap resolution with `gamedesign-lookandfeel` |
| `gamedesign-lookandfeel` | **COMPLETE**: Utilities placement spec gap resolved (Deliverable I, 2026-03-01); earthworks cost gate confirmed consistent with economy balance; zone/road/service placement costs confirmed for V1 difficulty tiers |
| `test-dev-cpp` | All 14 `WorldInteractionTest` unit tests in `tests/ui/world_interaction_test.cpp`; `MockRenderer` extension stubs (including sparse-map `setZoneOverlay`); `ManualTerrainQuery` extension for `getHeightAt` |

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

- **BLOCKING SPIKE** (required before Deliverable B implementation begins): Before implementing
  `IrrlichtRenderer::pickTerrainTile()`, execute a benchmarking spike using `aitown_benchmark`.
  Measure per-frame ray-march cost over a realistic camera path on a 1024×1024 terrain at 60 FPS.
  If sustained cost exceeds 1 ms, the implementation MUST switch to an O(1) grid-intersection +
  heightmap-refinement approach. Spike result must be documented in this plan before any PR for
  Deliverable B is opened. **This is a hard blocker; Deliverable B cannot start until the spike
  is complete and documented.**

- **RISK**: `IrrlichtRenderer::pickTerrainTile` ray-march may be too slow for real-time hover
  (60 FPS budget = 16 ms; a 4096-step march on a 1024-tile map with heightmap sampling could
  exceed 2 ms on weak hardware). See BLOCKING SPIKE above for resolution.

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

- **RESOLVED**: `ZoneType::Utility` spec gap (Deliverable I) was resolved by
  `gamedesign-lookandfeel` on 2026-03-01. Utilities placement uses `placeServiceBuilding()`.
  No interim stub required. Implementation may proceed immediately.
