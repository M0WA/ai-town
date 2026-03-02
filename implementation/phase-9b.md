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

- [ ] Add `int m_mapTilesX{0}` and `int m_mapTilesZ{0}` as private members to `UIManager`.
  Add a public setter `void setMapDimensions(int mapTilesX, int mapTilesZ)` that assigns these
  members. `setMapDimensions()` is called from `main.cpp` after terrain generation completes —
  the same pattern as `IrrlichtRenderer::setTerrainQuery()` called after terrain init. These
  members supply the map width and depth values used by the zone overlay key computation
  (`tileZ * m_mapTilesX + tileX`) and the `setZoneOverlay` call. Until `setMapDimensions()` is
  called, both members default to 0 and zone overlay writes are skipped (guard:
  `if (m_mapTilesX == 0 || m_mapTilesZ == 0) return;` at the top of any overlay-update path).
  **Re-call safety**: if `setMapDimensions()` is called a second time (e.g., on a new-game load
  with a different map size), the implementation MUST clear `m_overlayMap` before updating the
  dimension members — stale overlay entries keyed to the old `m_mapTilesX` would otherwise
  produce wrong tile indices on the new map. Also call
  `m_renderer->setZoneOverlay(mapTilesX, mapTilesZ, {})` immediately after the clear if
  `m_renderer` is non-null.
  (ref: `architecture/ui-ux/ui-manager.md`)

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

  **Cursor shape deferral**: `architecture/ui-ux/hud-layout.md` (Active tool indicator section)
  specifies that each tool mode uses a distinct cursor shape from the UI sprite sheet (Zone:
  crosshair with zone-color tint; Road: road-segment icon; Utilities: wrench; Demolish: X marker;
  Query: magnifying glass). Implementing OS-level cursor shape changes requires a new
  `IUIBackend::setMouseCursor(cursor_id)` method that does not exist in the Phase 9b
  `IUIBackend` 17-method interface. **Cursor shape changes are explicitly deferred to a future
  phase** (post-Phase 10). Phase 9b delivers the `m_activeTool` state that a future phase will
  use to drive cursor-shape selection. The active tool indicator icon (`HUD::setActiveToolLabel`
  updating the y:752–784 px badge) is already implemented in Phase 8 and updated by this phase.

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
  - `IrrlichtRenderer` stores `float m_cellSize{1.0f}` as a private member. It is set once
    from `main.cpp` via a new method `void setCellSize(float cellSize)` (called at step 2a of
    Deliverable H, after terrain generation). This method is on `IrrlichtRenderer` directly —
    NOT on the `IRenderer` interface (same rationale as `setTerrainQuery`: it is a
    one-time initialization setter, not a general renderer capability). `m_cellSize` is the
    world-space width of one tile in metres (e.g. `10.0f` for 10 m tiles); it is obtained from
    `TerrainSystem::getCellSize()` (see Deliverable E.1). `MockRenderer` does NOT need a
    `setCellSize()` method — the mock's `pickTerrainTile()` stub returns hardcoded test values
    independent of cellSize.
  - Intersect the ray with the terrain heightmap using a linear march (step size = `m_cellSize / 2`;
    maximum 4096 steps or until the ray exits map bounds). At each step, convert the current
    world-space ray position to tile indices (`stepTileX = static_cast<int>(rayX / m_cellSize)`,
    `stepTileZ = static_cast<int>(rayZ / m_cellSize)`), clamp to map bounds, then sample height
    via `m_terrain->getHeightAt(stepTileX, stepTileZ)` (the `ITerrainQuery` interface method
    that `TerrainSystem` implements, injected via `setTerrainQuery()`). `getHeightAt()` takes
    integer tile indices, not world-space floats — the conversion must happen at every march step.
  - Convert the world-space hit point to tile grid coordinates:
    `tileX = static_cast<int>(hitX / m_cellSize)`, `tileZ = static_cast<int>(hitZ / m_cellSize)`.
  - Clamp to map bounds `[0, mapTilesX-1] × [0, mapTilesZ-1]`; return false if no hit found.
  - `IrrlichtRenderer` must store a non-owning `ITerrainQuery* m_terrain{nullptr}` pointer
    (set via a new method `setTerrainQuery(ITerrainQuery* terrain)` called from `main.cpp`
    after terrain generation). `IrrlichtRenderer` does NOT hold a pointer to the concrete
    `TerrainSystem` class — only to the `ITerrainQuery` interface.
  - **Null-check guard (required)**: at the very start of `pickTerrainTile()`, before any
    `m_terrain` dereference: `if (!m_terrain) return false;` — prevents a null-pointer crash
    in the window between `IrrlichtRenderer` construction and the `setTerrainQuery()` call
    from `main.cpp`. The main.cpp wiring order guarantees `m_terrain` is set before gameplay
    starts, but the guard is required as a defensive check.
  - **NOTE**: `IrrlichtRenderer` forward-declares `ITerrainQuery`; the full include of
    `ITerrainQuery.h` lives in `.cpp` only to preserve the Irrlicht-free nature of `IRenderer.h`.
    The concrete `TerrainSystem` type is never mentioned in any `IrrlichtRenderer` header. (ref:
    `architecture/graphics-architecture/procedural-terrain.md` — Heightmap Query API)

- [ ] Add `virtual bool pickTerrainTile(int, int, int&, int&) const = 0;` to `MockRenderer`
  in `tests/simulation/mock_renderer.h`. Default mock action: return `false`. (ref:
  `architecture/testing/testability-architecture.md`)

#### C. `IRenderer` — Tile Hover Highlight and Zone Colour Overlay

- [ ] Add the following methods to `IRenderer` (`src/interfaces/IRenderer.h`):

  ```cpp
  // Render a single-tile wireframe hover highlight at the given tile grid coordinate.
  // ARGB colour encoded as 0xAARRGGBB (Irrlicht SColor format): AA=alpha, RR=red,
  // GG=green, BB=blue. E.g. 0x6000FF00 = semi-transparent green.
  // Pass kInvalidTile (-1,-1) to clear.
  // Called once per frame from the game loop, before endFrame().
  // main-thread-only.
  virtual void setTileHoverHighlight(int tileX, int tileZ, uint32_t argb) = 0;

  // Render a semi-transparent colour fill overlay for all zoned tiles.
  // Colour scheme: R=0x6000FF00, C=0x600000FF, I=0x60FFFF00 (alpha=0x60 ≈ 38%).
  // (ARGB encoding: 0xAARRGGBB; 0x6000FF00 = green, 0x600000FF = blue, 0x60FFFF00 = yellow).
  // sparseOverlay maps (tileZ * mapTilesX + tileX) -> ARGB color for tiles
  // with a non-zero zone overlay; tiles absent from the map have transparent
  // (no) overlay. Capped at 100K simultaneous entries for V1.
  // Called once per budget tick when zone layout changes.
  // main-thread-only.
  virtual void setZoneOverlay(int mapTilesX, int mapTilesZ,
                              const std::unordered_map<uint64_t, uint32_t>& sparseOverlay) = 0;
  ```

  (ref: `architecture/ui-ux/hud-layout.md` — active tool cursor shapes per tool mode;
  `architecture/game-design/zoning-system.md` — zone types R/C/I)

- [ ] Add the following method to `IRenderer` (`src/interfaces/IRenderer.h`), immediately after
  `setZoneOverlay`:

  ```cpp
  // Returns the screen bounding box of the tile at (tileX, tileZ) in physical pixels.
  // Used by UIManager to compute InspectorPanel position via the three-step cascade
  // (query-inspector-panel.md — Tile overlap prevention).
  // Returns an empty/zero rect if the tile is off-screen or terrain query is unavailable.
  // main-thread-only.
  //
  // ScreenRect is a plain-old-data struct defined in IRenderer.h to keep this header
  // Irrlicht-free (consistent with the "Irrlicht-free nature of IRenderer.h" principle,
  // Phase 9b Deliverable B). Do NOT use irr::core::rect<irr::s32> here.
  struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; };
  virtual ScreenRect getTileScreenBounds(int tileX, int tileZ) const = 0;
  ```

  `ScreenRect` is defined in `IRenderer.h` immediately before the `IRenderer` class declaration.
  `IrrlichtRenderer::getTileScreenBounds()` converts its Irrlicht-internal rect to `ScreenRect`
  before returning.

  Also add a no-op stub for `getTileScreenBounds()` to `MockRenderer` in
  `tests/simulation/mock_renderer.h` — default action: return
  `ScreenRect{}` (zero-initialised). Note: MockRenderer includes `IRenderer.h` and therefore
  has access to `ScreenRect` without any additional Irrlicht dependency. (ref:
  `architecture/ui-ux/query-inspector-panel.md` — Tile overlap prevention;
  `architecture/testing/testability-architecture.md`)

- [ ] Implement `IrrlichtRenderer::setTileHoverHighlight()`: build a single-quad `SMeshBuffer`
  with four vertices at the tile's world corners (Y = terrain height at tile centre **+ 0.05f**,
  sampled from `TerrainSystem` — the +0.05f offset prevents depth-buffer Z-fighting against
  terrain geometry; `EMT_TRANSPARENT_ALPHA_CHANNEL` disables depth writes but still reads the
  depth buffer with GL_LEQUAL, and at exactly terrain height floating-point precision can cause
  some fragments to fail the depth test; the 0.05f lift guarantees the quad is above terrain
  depth in NDC, below the zone overlay +0.1f layer). Set the material type to `EMT_TRANSPARENT_ALPHA_CHANNEL`. Apply the
  hover ARGB colour to every vertex via `SMeshBuffer::Vertices[i].Color` (one `SColor` per
  corner). After populating all vertices, call `buf->recalculateBoundingBox()` on the
  `SMeshBuffer`, then call `m->recalculateBoundingBox()` on the parent `SMesh*` — required even
  though the mesh is not scene-graph-attached; `IVideoDriver::drawMeshBuffer()` may perform its
  own frustum check against the bounding box.

  **Memory lifecycle**: `IrrlichtRenderer` allocates a single `SMeshBuffer* m_hoverBuffer{nullptr}`
  and a companion `SMesh* m_hoveredTileMesh{nullptr}` ONCE in the `IrrlichtRenderer` constructor —
  never lazily on first call. **Constructor vertex pre-population (required)**: immediately after
  allocating `m_hoverBuffer`, add 4 placeholder vertices (position `{0.f, 0.f, 0.f}`,
  Color `SColor(0, 0, 0, 0)`, UV `{0.f, 0.f}`) and 6 indices (`{0, 1, 2, 0, 2, 3}`) into
  `m_hoverBuffer`, then call `m_hoverBuffer->recalculateBoundingBox()`. This pre-population is
  required so that `setTileHoverHighlight()` can safely write to `m_hoverBuffer->Vertices[0..3]`
  in-place on its first call — accessing `Vertices[i]` on an empty `SMeshBuffer` is undefined
  behavior. **Constructor ownership sequence (required)**: after pre-populating `m_hoverBuffer`,
  allocate `m_hoveredTileMesh = new SMesh()`, then call
  `m_hoveredTileMesh->addMeshBuffer(m_hoverBuffer)` — `SMesh::addMeshBuffer()` unconditionally
  calls `grab()` on the buffer (ref_count → 2). Immediately follow with `m_hoverBuffer->drop()`
  to release the caller's ownership reference (ref_count → 1); `m_hoveredTileMesh` is now the
  sole owner. (ref: `architecture/graphics-architecture/scene-graph-ownership.md` — same
  grab/drop pattern as zone overlay `newMesh->drop()` after `addMeshSceneNode()`.) The mesh is
  NOT added to the Irrlicht scene graph
  (avoids scene graph overhead for a per-frame draw-call mesh). On each subsequent call:
  - If `tileX == -1` (clear request): set `m_hoverVisible = false` and return immediately —
    the buffer is NOT dropped, and `drawScene()` suppresses the `drawMeshBuffer` call when
    `m_hoverVisible` is `false`.
  - Otherwise: update the 4 vertex positions (world-space tile corners, Y = terrain height at
    tile centre + 0.05f), colours (`SColor` from the provided `argb` value), and indices
    in the EXISTING `m_hoverBuffer` — no `drop()` + re-allocation per call. Call
    `m_hoverBuffer->recalculateBoundingBox()` after updating vertices. Call
    `m_hoveredTileMesh->recalculateBoundingBox()` on the parent `SMesh*`. Set
    `m_hoverVisible = true`.
  - `m_hoverBuffer` is dropped (via `m_hoveredTileMesh->drop()`, which releases the mesh and
    its contained buffer) only in the `IrrlichtRenderer` destructor. The drop/recreate pattern
    (`m_hoveredTileMesh->drop()` + `new SMesh` per call) is NOT used — it causes per-event
    GPU mesh allocation in a hot input path (`MouseMove` fires multiple times per frame).

  **Render-pass draw call**: `setTileHoverHighlight()` is called from `UIManager::onEvent()`
  during event processing — outside the render pass. The actual
  `IVideoDriver::drawMeshBuffer(m_hoveredTileMesh->getMeshBuffer(0))` call must live in
  `IrrlichtRenderer::drawScene()`, immediately after `sceneManager->drawAll()` and before
  `uiManager->draw()`, guarded by `m_hoveredTileMesh != nullptr && m_hoverVisible`. This
  ensures the highlight renders on top of 3D terrain but beneath 2D GUI elements (per the
  mandatory 10-step per-frame sequence in
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md`).

  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] Implement `IrrlichtRenderer::setZoneOverlay()`: maintain an internal `SMesh*` overlay
  plane (one quad per entry in `sparseOverlay`) rendered with `EMT_TRANSPARENT_ALPHA_CHANNEL`.
  Rebuild the overlay mesh when `setZoneOverlay()` is called. **Memory management order**:
  (0) If `sparseOverlay.empty()`: if `m_overlayNode` is non-null, call `m_overlayNode->remove()`
  and set `m_overlayNode = nullptr`; return immediately — no new mesh is needed. This handles
  new-game loads and city states with no zoned tiles (the old overlay is cleared, leaving no
  zone overlay rendered).
  (1) Iterate `sparseOverlay` entries; for each, compute quad position from key
  `(tileZ * mapTilesX + tileX)` and generate a coloured quad (Y = terrain height at tile
  centre **+ 0.1f**, where height is sampled via `m_terrain->getHeightAt(tileX, tileZ)` —
  the +0.1f offset lifts the overlay quads 0.1 world units above the exact terrain depth
  plane occupied by opaque terrain tile geometry, preventing depth-buffer Z-fighting that
  would otherwise cause the overlay to flicker randomly. **Note**: the hover highlight also
  samples terrain height and uses a **+0.05f** offset (see `setTileHoverHighlight()` above for
  the full depth-buffer rationale — `EMT_TRANSPARENT_ALPHA_CHANNEL` disables depth writes but
  still reads the depth buffer with GL_LEQUAL, so a small Y lift is required even when the mesh
  is drawn via `IVideoDriver::drawMeshBuffer()` after `sceneManager->drawAll()`).
  Out-of-bounds keys
  (key >= mapTilesX × mapTilesZ) are silently skipped.
  (2) Build the new `SMesh*` overlay from the valid entries. For each `SMeshBuffer`, set
  `buf->Material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL` **before**
  calling `buf->recalculateBoundingBox()` — if omitted, the buffer uses the default material
  type (`EMT_SOLID`) and the overlay renders as fully opaque, defeating the transparency pass
  entirely. After setting the material type and populating all vertices across all
  `SMeshBuffer` quads, call `buf->recalculateBoundingBox()` on each `SMeshBuffer`, then call
  `m->recalculateBoundingBox()` on the parent `SMesh*`, before attaching the mesh to a scene
  node — omitting this causes silent frustum culling failures where overlay quads that should
  be visible are not rendered.
  (3) If the new mesh is valid: if `m_overlayNode` is non-null, call `m_overlayNode->remove()`
  and set `m_overlayNode = nullptr` to destroy the old node; then call
  `m_smgr->addMeshSceneNode(newMesh)` directly to create the scene node (the zone overlay is a
  renderer-internal node, not a game entity tracked in `SceneEntityManager`'s entity list),
  store the returned pointer in `m_overlayNode`, and immediately call `newMesh->drop()` to
  release the caller's reference — `addMeshSceneNode` grabs the mesh internally (incrementing
  its ref_count to 2), so the caller must drop its own reference to avoid a leak; after
  `drop()`, the scene node holds the sole reference (ref_count = 1). (4) If mesh
  construction fails, log error and leave the previous overlay scene node in place. (ref:
  `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/graphics-architecture/texture-cache.md`)

  **Lifecycle distinction — overlay vs hover highlight**: the zone overlay mesh IS attached to
  the Irrlicht scene graph as a persistent `ISceneNode*` stored directly in `m_overlayNode`.
  On each `setZoneOverlay()` call the OLD scene node (if any) is removed via
  `m_overlayNode->remove()` followed by `m_overlayNode = nullptr` before the new mesh is
  created and attached.
  This is the OPPOSITE of `setTileHoverHighlight()`, which keeps its mesh completely OUT of the
  scene graph and draws it every frame with `IVideoDriver::drawMeshBuffer()`. The zone overlay
  uses a scene node because it persists across many frames between placement events — attaching
  it once and letting the scene graph render it each frame is cheaper than issuing a raw draw
  call every frame for a mesh that rarely changes.
  **Render-pass placement**: since the zone overlay is a transparent scene node
  (`EMT_TRANSPARENT_ALPHA_CHANNEL`), Irrlicht renders it automatically during
  `sceneManager->drawAll()` in the transparent-node pass — AFTER all opaque nodes (terrain
  tiles, buildings) have been drawn and their depth values committed. No explicit step is
  required in the 10-step per-frame sequence in `irrlicht-device-lifecycle.md`; the overlay
  renders at the correct Z-layer via Irrlicht's standard material-type sorting.

- [ ] Add no-op stubs for `setTileHoverHighlight` and `setZoneOverlay` (with the sparse-map
  signature) to `MockRenderer`.

#### D. Game Loop — Per-Frame Hover and Left-Click Dispatch

- [ ] In `UIManager::onEvent()`, after the five UI priority levels (1–5) return `false` (event
  not consumed by any UI handler), add a **world-interaction block** at the tail of
  `UIManager::onEvent()` that handles `MouseMove` and `MouseButtonDown button=0` (left-click)
  events when `m_state == GameState::Gameplay` and `m_activeTool != ActiveTool::None`. This
  block implements Priority 7 of the input-arbitration chain.
  (ref: `architecture/ui-ux/input-arbitration.md` Priority 7 — World Interaction layer)

  **Why this placement is correct**: Priority 6 (`CameraController`) only handles scroll-wheel
  zoom, middle-mouse-button drag, right-mouse-button drag, and edge-scroll `MouseMove`. It never
  consumes `MouseButtonDown button=0` (left-click). Priority 7 never consumes `MouseMove`
  (returns `false`, side-effect only). Therefore, placing the Priority 7 world-interaction block
  at the tail of `UIManager::onEvent()` — before `CameraController` is separately invoked by the
  platform adapter — is functionally equivalent to placing it after `CameraController`: the two
  priorities handle disjoint event types with no ordering conflict. The world-interaction block
  executes only when `m_renderer` is non-null and does NOT consume `MouseMove` (camera
  edge-scroll must still work via `CameraController`) but DOES consume left-click when a
  non-Query placement tool is active and the ray-cast hits terrain (returns `true`).

  **Null-check guard (required)**: At the start of the world-interaction block, before any
  `pickTerrainTile()` call: `if (!m_renderer) return false;` — prevents null-pointer
  dereference before `setRenderer()` has been called from `main.cpp`.

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
  - **Query tool**: left-click is NOT handled at Priority 7. Per
    `architecture/ui-ux/input-arbitration.md` rule 7(d), Priority 7 must return `false` for
    QueryTool clicks — the QueryPanel open path is dispatched at Priority 3 (see below).
  (ref: `architecture/game-design/terrain-interaction.md`, `architecture/ui-ux/input-arbitration.md`,
  `architecture/ui-ux/modal-dialog-system.md`)

- [ ] **Priority 3 — QueryTool open path**: In `UIManager::onEvent()`, after the existing
  Priority 3 QueryPanel dismiss/Escape/dismiss-click handling, add a second Priority 3 handler
  for the open-path: when `m_activeTool == ActiveTool::Query` AND the QueryPanel is NOT
  currently open AND the event is `MouseButtonDown button=0`, call
  `m_renderer->pickTerrainTile(event.screenX, event.screenY, tileX, tileZ)`. If the ray-cast
  returns `true` (valid terrain hit), call `m_sim->queryTile(tileX, tileZ)` to obtain a
  `QueryResult`, compute tile screen bounds for panel positioning (see Deliverable F), call
  `InspectorPanel::populate(result, tileX, tileZ)`, open the inspector panel, and return `true`
  (event consumed). If the ray-cast returns `false` (no terrain hit), return `false`
  (pass-through). This ensures QueryTool left-clicks are consumed at Priority 3 and never reach
  Priority 7. (ref: `architecture/ui-ux/input-arbitration.md` rule 7(d))

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
  **Initial button state (set at `init()` time, before any player interaction)**: immediately
  after creating the 9 buttons, call `IUIBackend::setElementImage(handle, outlineSprite)` on
  all 9 buttons to set the inactive/outline-icon sprite. Then call
  `IUIBackend::setElementImage(handle, activeSprite)` on the default-selected button (column 0
  = Residential, row 0 = Low) to set its active-state sprite. This ensures buttons have correct
  visual state on first display rather than showing a blank/undefined image. The same
  sprite-swap logic applies on each subsequent button click (see Zone sub-panel click handler
  below).

- [ ] **Zone sub-panel click handler** (Priority 5 — zone sub-panel buttons are
  `IUIBackend::addButton()` elements that generate `EGET_BUTTON_CLICKED` events, handled in
  the same Priority 5 block as toolbar buttons in `UIManager::onEvent()`): when a Zone
  sub-panel button is clicked, update `m_selectedZoneType` and `m_selectedDensityTier` based
  on the button's column (0=Residential, 1=Commercial, 2=Industrial) and row (0=Low, 1=Medium,
  2=High). The
  active-selection button is visually indicated: all buttons remain `setElementEnabled(true)`
  (interactive). The selected button is highlighted via `IUIBackend::setElementImage` with an
  active-state sprite; unselected buttons display their default sprite (active = filled icon
  with accent-color border; inactive = outline icon, no border — same convention as minimap
  overlay toggle buttons per `architecture/ui-ux/minimap.md`). Do NOT use
  `setElementEnabled(false)` for selection indication — that produces a disabled/grayed-out
  non-interactive appearance, which visually signals "unavailable" rather than "active choice".

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
  **Initial button state (set at `init()` time)**: immediately after creating the 4 buttons,
  call `IUIBackend::setElementImage(handle, outlineSprite)` on all 4 buttons, then call
  `IUIBackend::setElementImage(handle, activeSprite)` on the default-selected button
  (PowerPlant — `ServiceBuildingType::PowerPlant`). Same sprite-swap logic applies on each
  subsequent click (see Utilities sub-panel click handler below).

- [ ] **Utilities sub-panel click handler** (Priority 5 — same as Zone sub-panel: buttons are
  `IUIBackend::addButton()` elements generating `EGET_BUTTON_CLICKED` events, dispatched in
  the Priority 5 block): when a Utilities sub-panel button is clicked, update
  `m_selectedServiceBuilding` to the corresponding `ServiceBuildingType`. Button layout:
  (col0 row0)=PowerPlant, (col1 row0)=WaterTower, (col0 row1)=FireStation,
  (col1 row1)=PoliceStation. Active selection indicated by leaving all buttons `setElementEnabled(true)` (interactive);
  the selected button is highlighted via `IUIBackend::setElementImage` with an active-state
  sprite (active = filled icon with accent-color border; inactive = outline icon, no border —
  same convention as minimap overlay toggle buttons per `architecture/ui-ux/minimap.md`). Do
  NOT use `setElementEnabled(false)` for this purpose.

- [ ] Both sub-panels (Zone and Utilities) are fully rendered in Phase 9b — UIManager creates
  their buttons at `init()` time and shows/hides them based on active tool via
  `IUIBackend::setElementVisible()`.

  (ref: `architecture/game-design/service-coverage.md` — Utilities Tool Placement Design)

- [ ] **Zone overlay refresh**: after a successful `placeZone` or `demolishTile` call (road
  and service-building placements do NOT modify the zone overlay — only zone tiles have overlay
  colours), update the zone overlay using a `std::unordered_map<uint64_t, uint32_t> m_overlayMap`
  sparse overlay map stored as a private member of `UIManager`, keyed by
  `(tileZ * m_mapTilesX + tileX)`.
  `m_mapTilesX` and `m_mapTilesZ` are the private members populated by `setMapDimensions()`
  (see Deliverable A); they represent the map width and depth in tiles respectively. At zone
  placement, insert or update the entry:
  `m_overlayMap[(tileZ * m_mapTilesX + tileX)] = colour`, where `colour` is derived from
  `selectedZoneType` using the ARGB values defined in the `setZoneOverlay()` interface comment
  (Deliverable C): `Residential→0x6000FF00u`, `Commercial→0x600000FFu`,
  `Industrial→0x60FFFF00u` (alpha 0x60 ≈ 38%). At demolish, erase the entry:
  `m_overlayMap.erase(tileZ * m_mapTilesX + tileX)`. Before any overlay update, guard with
  `if (m_mapTilesX == 0 || m_mapTilesZ == 0) return;` to skip updates until
  `setMapDimensions()` has been called. When calling
  `m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap)`, UIManager passes
  `m_overlayMap` directly — the sparse map is the interface contract (tiles absent from the map
  are transparent). The renderer generates overlay quads only for entries present in the sparse
  map, capped at 100K simultaneous overlay quads for V1 (entries beyond this cap are silently
  dropped). On new-game load, UIManager clears `m_overlayMap` and calls
  `setZoneOverlay(m_mapTilesX, m_mapTilesZ, {})` (empty sparse map) before the new map's zones
  are set.
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
  without it `ManualTerrainQuery` fails to compile, blocking all 17 Phase 9b unit tests.

#### E.1. `TerrainSystem` Map-Dimension Accessors (New Public Getters)

- [ ] Add `int getMapTilesX() const;`, `int getMapTilesZ() const;`, and `float getCellSize() const;`
  public accessor methods to `TerrainSystem` (`src/terrain/TerrainSystem.h` /
  `src/terrain/TerrainSystem.cpp`). These methods return the corresponding private members —
  `m_mapTilesX`, `m_mapTilesZ`, and `m_cellSize` respectively — which are set by
  `TerrainSystem::generate()`. `getMapTilesX()`/`getMapTilesZ()` are required so `main.cpp`
  can call `uiManager.setMapDimensions(...)` (Deliverable H step (5)). `getCellSize()` is
  required so `main.cpp` can call `renderer.setCellSize(terrainSystem.getCellSize())`
  (Deliverable H step (2a)) to supply `IrrlichtRenderer` with the tile grid spacing for
  `pickTerrainTile()`. `TerrainSystem` implements `ITerrainQuery` (for slope and height queries
  consumed by `CitySimulation` and `IrrlichtRenderer`), but these dimension accessors are
  intentionally NOT added to the `ITerrainQuery` interface — that interface is minimal by design
  (slope + height only). These getters are added directly to the concrete `TerrainSystem` class
  and are consumed only from `main.cpp`. Implementations are trivial one-liners:

  ```cpp
  // TerrainSystem.h (public section)
  int   getMapTilesX() const;
  int   getMapTilesZ() const;
  float getCellSize()  const;

  // TerrainSystem.cpp
  int   TerrainSystem::getMapTilesX() const { return m_mapTilesX; }
  int   TerrainSystem::getMapTilesZ() const { return m_mapTilesZ; }
  float TerrainSystem::getCellSize()  const { return m_cellSize; }
  ```

  (ref: `architecture/graphics-architecture/procedural-terrain.md` — `TerrainSystem` public
  interface; Deliverable H step (5) for usage site)
  Assigned to: `graphics-dev-irrlicht`.

#### F. `InspectorPanel` — Real Tile Query Wiring

- [ ] Add `void populate(const QueryResult& result, int tileX, int tileZ)` to `InspectorPanel`
  (already exists as a Phase 8 stub with empty body). Fill in the real implementation: set the
  zone type / density label, demand score, desirability, tax yield/month, and demand pressure %
  fields from `result`. Panel opens at the position computed by
  `InspectorPanel::computePanelPosition()` using the three-step cascade defined in
  `architecture/ui-ux/query-inspector-panel.md` (primary → fallback → edge-snap). UIManager
  must supply all three required inputs to `computePanelPosition()` as follows:

  **Prerequisite — update `computePanelPosition` signature** (Phase 9b deliverable, not Phase 8):
  Phase 8 implemented `InspectorPanel::computePanelPosition` with signature
  `static Rect computePanelPosition(int clickX, int clickY, int screenW, int screenH)` in
  `src/ui/QueryPanel.cpp` / `src/ui/inspector_panel.h`. This signature lacks the `tileBounds`
  parameter required by `architecture/ui-ux/query-inspector-panel.md` tile-overlap prevention.
  Phase 9b MUST update this signature to
  `static ScreenRect computePanelPosition(int cursorX, int cursorY, const ScreenRect& tileBounds)`
  (`ScreenRect` is defined in `IRenderer.h` per Deliverable B above — `struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; }`;
  returns a `ScreenRect` in virtual 1920×1080 space; `tileBounds` is the queried tile's
  bounding box already un-projected to virtual space before the call). The Phase 8
  `screenW`/`screenH` parameters are no longer needed — the edge-snap step derives the virtual
  screen bounds from fixed constants `1920 × 1080`. Update `src/ui/inspector_panel.h` (signature),
  `src/ui/QueryPanel.cpp` (implementation — replace `screenW`/`screenH` with `tileBounds` and
  add tile-overlap detection), and update `tests/ui/query_panel_test.cpp` to pass a `ScreenRect`
  tileBounds argument instead of `screenW`/`screenH`. Phase 9b is the first phase that calls
  `computePanelPosition()` from `UIManager` with the tile bounds, so this is the correct phase
  to complete the signature. **Test update guidance**: the 4 existing Phase 8 pure-function tests
  (primary placement, fallback placement, edge-clamping, edge-snap) should pass a dummy
  `ScreenRect{1000, 1000, 10, 10}` (off-screen, guaranteed non-overlapping with all test cursor
  positions) so the tile-overlap step is never triggered — existing placement assertions remain
  valid. Additionally, add one new tile-overlap test case:
  `QueryPanel_TileOverlap_FallsBackToFallback`: pass a `tileBounds` that overlaps the primary
  placement position and verify the returned rect equals the fallback placement result.

  (a) Convert physical cursor coordinates to virtual space via
  `UIScaler::unproject(physX, physY)` → `(cursorX_virtual, cursorY_virtual)`.
  `UIScaler::unproject` was delivered in Phase 1 and is already available.

  (b) Call `m_renderer->getTileScreenBounds(tileX, tileZ)` to obtain the tile's bounding box
  in physical pixels, then un-project all four corners via `UIScaler::unproject()` to obtain
  `tileBounds_virtual` — a rect in virtual 1920×1080 space.

  (c) Pass `(cursorX_virtual, cursorY_virtual, tileBounds_virtual)` to
  `InspectorPanel::computePanelPosition()` for the three-step cascade (primary → fallback →
  edge-snap) defined in `architecture/ui-ux/query-inspector-panel.md`. The `tileBounds_virtual`
  parameter is required for the tile-overlap detection step; passing only cursor coordinates
  omits the overlap check entirely and violates the spec.

  (d) Apply the computed `ScreenRect` position via destroy-and-recreate: `IUIBackend` does not
  provide `setElementPosition` or `setElementRect`, so panel repositioning is achieved by
  destroying any previously-created inspector panel elements and recreating them at the new
  coordinates. `InspectorPanel::populate()` implements this as follows:
  1. If previously-created element handles are stored (from a prior `populate()` call), call
     `m_backend->removeElement(handle)` on each stored handle to destroy the old elements.
  2. Create all inspector panel elements at the computed position by calling
     `m_backend->addStaticText(text, panelRect.x + offsetX, panelRect.y + offsetY, fieldW, fieldH)`
     for each label/value row, where `panelRect` is the `ScreenRect` returned by step (c) and
     `offsetX`/`offsetY` are per-field offsets within the panel bounds.
  3. Store the new `UIElementHandle` values in `InspectorPanel`'s member variables for subsequent
     refresh cycles (data update without repositioning) and for cleanup when the panel closes.

  This destroy-and-recreate pattern uses only the existing 17-method `IUIBackend` interface. No
  `setElementPosition` or `setElementRect` method is added to `IUIBackend`. Per-frame data
  refreshes (see cadence note below) update element text via `m_backend->setElementText()` on
  the existing handles without repositioning — only a new `populate()` call (new tile query)
  triggers destroy-and-recreate.

  (ref: `architecture/ui-ux/query-inspector-panel.md` — Tile overlap prevention)

- [ ] Wire `InspectorPanel` data refresh cadence: budget/economy fields refresh once per budget
  tick (poll `m_sim->queryTile` again if `m_inspectorOpen && ticks_since_open > 0`); traffic
  data refresh every 10 simulation frames. "Updated N seconds ago" line shown when data is >1 s
  stale. (ref: `architecture/ui-ux/query-inspector-panel.md`)

#### G. Tests

- [ ] Define `WorldInteractionTest` as a Google Test fixture class. Private members (declared
  in this order): `StrictMock<MockCitySimulation> sim_`, `StrictMock<MockRenderer> renderer_`,
  `ManualTerrainQuery terrain_`, `NiceMock<MockUIBackend> backend_`, `ManualClock clock_`,
  `std::unique_ptr<UIManager> uiManager_`. `SetUp()` constructs UIManager and wires
  dependencies: `uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);`
  then calls `uiManager_->setRenderer(&renderer_);`, `uiManager_->setTerrainQuery(&terrain_);`,
  and `uiManager_->setMapDimensions(10, 10);` — these three setter calls are REQUIRED before any
  event is sent. `setRenderer` and `setTerrainQuery` must be called or `pickTerrainTile()` will
  null-dereference; `setMapDimensions(10, 10)` establishes `m_mapTilesX=10`, `m_mapTilesZ=10`
  so that zone overlay key computations use a concrete, test-predictable map width (e.g. the
  tile at `(tileX=3, tileZ=4)` has key `4 * 10 + 3 = 43`). `TearDown()` override calls `uiManager_.reset();`
  (destructor contract: explicitly destroys UIManager before `StrictMock<>` members are
  destroyed, releasing raw `m_renderer`/`m_terrain` pointers safely — per
  `architecture/testing/testability-architecture.md` canonical TearDown pattern). All G-tests
  are methods of this fixture class and access UIManager via `uiManager_->`.

- [ ] `WorldInteraction_ZonePlacement_CallsPlaceZone` (unit test in `tests/ui/`): construct
  `UIManager` with `StrictMock<MockCitySimulation>`, `StrictMock<MockRenderer>`,
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

- [ ] `WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry` (unit test): uses the shared
  `WorldInteractionTest` fixture (which calls `uiManager_->setMapDimensions(10, 10)` in
  `SetUp()`, establishing `m_mapTilesX=10`). Set active tool to `ActiveTool::Zone`. Stub
  `MockRenderer::pickTerrainTile` to return `true` at tileX=3, tileZ=4. Send
  `MouseButtonDown button=0`. After dispatch, capture the `sparseOverlay` argument passed to
  `setZoneOverlay`. Assert that the captured map contains exactly 1 entry whose key equals `43`
  (i.e. `tileZ * m_mapTilesX + tileX = 4 * 10 + 3 = 43`, the placed tile) and whose value
  equals `0x6000FF00u` (the Residential zone ARGB colour defined in Deliverable C — green,
  alpha 0x60 ≈ 38%). A non-zero check is insufficient; the test must pin the exact colour so
  that a wrong zone-type lookup (e.g. returning Commercial blue `0x600000FFu`) is caught.
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

- [ ] `WorldInteraction_SetMapDimensions_Recall_ClearsOverlay` (unit test): call
  `setMapDimensions(10, 10)` during `SetUp` (as normal), then simulate 3 zone placements to
  pre-populate `m_overlayMap`. Capture the `sparseOverlay` argument on the subsequent
  `setZoneOverlay` call. Then call `setMapDimensions(20, 20)` with different dimensions. Assert
  that `setZoneOverlay` is called again with an empty sparse map (`size() == 0`) — confirming
  that the re-call-safety rule clears `m_overlayMap` before the new dimensions are stored. A
  follow-up placement after the resize should use key `tileZ * 20 + tileX` (new width), not
  `tileZ * 10 + tileX` (old width).

- [ ] `WorldInteraction_ZoneSubPanel_ButtonsInitialized` (unit test): construct `UIManager`
  with `NiceMock<MockUIBackend>`. Before sending any events, inspect the `setElementImage`
  calls made during construction/`init()`. Assert that `setElementImage` was called with the
  outline-icon sprite handle on all 9 Zone sub-panel button handles, and with the active-state
  sprite handle on exactly one button handle (the default selection: Residential Low, column 0
  row 0). Use `EXPECT_CALL(backend_, setElementImage(zone_btn[i], outlineHandle)).Times(1)` for
  each of the 8 non-default buttons, and
  `EXPECT_CALL(backend_, setElementImage(zone_btn[0], activeHandle)).Times(1)` for the default.
  This test closes the testability gap introduced when the init()-time button image
  initialization requirement was added in Fix O.

- [ ] `WorldInteraction_UtilitiesSubPanel_ButtonsInitialized` (unit test): same pattern for the
  4 Utilities sub-panel buttons. Assert `setElementImage` called with outline-icon sprite on all
  4 handles during init(), then active-state sprite called on the PowerPlant button handle
  (default selection). Use `NiceMock<MockUIBackend>` to suppress noise from unrelated
  `setElementImage` calls on other HUD elements; use `EXPECT_CALL` with
  `InSequence`-or-matcher to verify the sprite-swap pattern specifically for the Utilities
  sub-panel button handles.

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
  - **(2a)** Call `renderer.setCellSize(terrainSystem.getCellSize())` — supplies `IrrlichtRenderer`
    with the tile grid spacing (world-space metres per tile) required by `pickTerrainTile()`
    for the ray-march step size and tile-index conversion. Must be called on the concrete
    `IrrlichtRenderer` (not `IRenderer*`) since `setCellSize` is not on the interface (same
    rationale as `setTerrainQuery`).
  - **(3)** Call `uiManager.setRenderer(&renderer)`.
  - **(4)** Call `uiManager.setTerrainQuery(&terrainSystem)`.
  - **(5)** Call `uiManager.setMapDimensions(terrainSystem.getMapTilesX(),
    terrainSystem.getMapTilesZ())` — supplies `m_mapTilesX` and `m_mapTilesZ` so the zone
    overlay key computation (`tileZ * m_mapTilesX + tileX`) and the `setZoneOverlay` dimension
    arguments are correct for this session's map. Must be called after terrain generation so the
    map dimensions are final. Same pattern as `IrrlichtRenderer::setTerrainQuery()`.

  This order guarantees all pointers and map dimensions are valid before the first game-loop
  frame calls `pickTerrainTile()` or triggers a zone overlay update. These calls replace the
  Phase 8 stub wiring where the UIManager had no renderer or terrain reference. (ref:
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)

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
    construct `UIManager` with `StrictMock<MockCitySimulation>`, `StrictMock<MockRenderer>`,
    `ManualTerrainQuery` (slope = 0°), `NiceMock<MockUIBackend>`, `ManualClock`. Set active tool to `ActiveTool::Utilities`, selected
    building to `ServiceBuildingType::FireStation`. Stub `MockRenderer::pickTerrainTile` to
    return true at (5, 7). Send `MouseButtonDown button=0`.
    `EXPECT_CALL(sim_, placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0)).Times(1);`

#### J. SFX Wiring — Road Build Sound ID

Phase 6 wired 7 SFX IDs (`SFX_BUILD_PLACE`, `SFX_BUILD_DEMOLISH`, `SFX_EARTHWORKS`,
`SFX_ZONE_UPGRADE`, `SFX_SERVICE_DEGRADE`, `SFX_BUDGET_WARN`, `SFX_LOAN_ISSUED`).
The V1 Audio Asset Manifest defines `SFX_ROAD_BUILD` (SoundId = 3) as a **separate,
dedicated road-construction feedback sound** distinct from `sfx_build_place` (the generic
zone/building placement sound). Phase 10 (Dynamic Soundscape) requires `sfx_road_build` to
fire from real road placement dispatch.

**Phase 9b SFX wiring task** (assigned to `sound-dev-opensoftal`):

- [x] `CitySimulation::placeRoad()` calls `SFX_ROAD_BUILD` (SoundId = 3) — already
  implemented in `src/simulation/CitySimulation.cpp` (line 1441):

  ```cpp
  m_audio->playPositionalSound(SFX_ROAD_BUILD,
      vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
      SoundPriority::NORMAL, 1.0f);
  ```

  `SFX_BUILD_PLACE` (SoundId = 1) remains correct for `placeZone()` and
  `placeServiceBuilding()` calls (also uses `vec3{...}, SoundPriority::NORMAL, 1.0f`).
  `IAudioSystem::playPositionalSound` signature: `(SoundId, vec3, SoundPriority, float)`.
  **This code-change deliverable is COMPLETE** — only the unit test below remains.
  (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md` — SoundId Assignment Table)

- [ ] **Unit test: `CitySimulation_PlaceRoad_FiresSFXRoadBuild`**
  - **Location**: `tests/simulation/` — append to
    `tests/simulation/audio_sim_test.cpp` (the file that already contains audio-related
    simulation tests; if `audio_sim_test.cpp` does not exist, create it and add it to the
    `simulation_tests` CMake target via `target_sources(simulation_tests PRIVATE
    tests/simulation/audio_sim_test.cpp)`).
  - **Setup**: construct `CitySimulation` with all six constructor parameters supplied in
    the exact order below (matching `CitySimulation(IRenderer*, IAudioSystem*, ISimulationRNG*,
    IClock*, ITerrainQuery*, Difficulty)` from `src/simulation/CitySimulation.h`):
    1. `IRenderer*` — use `NiceMock<MockRenderer>` (from `tests/simulation/mock_renderer.h`).
       **`NiceMock` is intentional here** (explicit exception to the `StrictMock`-for-unit-tests
       policy): `CitySimulation::placeRoad()` may call renderer methods internally for
       world-space position computation; this test is focused solely on audio callback
       behaviour and renderer interactions are incidental. Same pattern as `economy_test.cpp`
       using `NiceMock<MockAudioSystem>` in non-audio economy tests.
    2. `IAudioSystem*` — use `StrictMock<MockAudioSystem>` (from
       `tests/simulation/mock_audio_system.h`, introduced in Phase 7).
    3. `ISimulationRNG*` — use `ManualRNG` (from `tests/simulation/manual_rng.h`)
    4. `IClock*` — use `ManualClock` (from `tests/simulation/manual_clock.h`)
    5. `ITerrainQuery*` — use `ManualTerrainQuery` (from `tests/simulation/manual_terrain_query.h`)
    6. `Difficulty` — use `Difficulty::Easy` (from `src/interfaces/simulation_types.h`);
       Easy difficulty initialises a positive starting treasury, satisfying the non-zero
       treasury requirement stated below.

    Concrete construction (parameter order reference):

    ```cpp
    NiceMock<MockRenderer>      renderer;
    StrictMock<MockAudioSystem> audioSystem;
    ManualRNG rng; ManualClock clock; ManualTerrainQuery terrain;
    CitySimulation citySimulation(&renderer, &audioSystem, &rng, &clock,
                                   &terrain, Difficulty::Easy);
    ```

    This is the same pattern used by `SimulationTestBase` in
    `tests/simulation/simulation_test_base.h`. If `audio_sim_test.cpp` does not exist,
    create it with a fixture that mirrors `SimulationTestBase`; if it already exists,
    extend its existing fixture rather than defining a new one.

    Ensure the `CitySimulation` under test has a non-zero starting treasury before calling
    `placeRoad(5, 7, 0)`. Use the existing test helper or default-construct with Easy
    difficulty so that the placement call does not silently fail due to insufficient funds
    before firing the audio callback. Call `citySimulation.placeRoad(5, 7, 0)` only after
    the `EXPECT_CALL` is set up and the treasury is confirmed non-zero.
  - **Assertion**:
    `EXPECT_CALL(audioSystem, playPositionalSound(SFX_ROAD_BUILD, _, _, _)).Times(1);`
    (the three `_` wildcards match: `vec3` world-space position, `SoundPriority` priority,
    and `float` gain — matching `IAudioSystem::playPositionalSound(SoundId, vec3, SoundPriority, float)`).
    The call must be
    set up via `EXPECT_CALL` **before** `placeRoad()` is invoked (standard GMock order).
  - **Label**: unit test — no `requires-opengl` label.
  - **CMake**: add to the `simulation_tests` target via
    `target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp)` in
    `CMakeLists.txt`. Do NOT call `add_executable` or `aitown_add_tests` again for
    `simulation_tests` (duplicate target error).
  - Assigned to: `test-dev-cpp`.

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
- All 17 unit tests in `WorldInteractionTest` pass under `ctest -LE "integration|requires-opengl"`
  (9 named base tests: ZonePlacement, RoadPlacement, DemolishTool_SteepSlope,
  ZoneTool_SteepSlope_InsufficientFunds, QueryTool, NoActiveTool, ModalActive,
  HoverHighlight_SetOnMouseMove, HoverHighlight_ClearedOnMiss;
  plus `WorldInteraction_UtilitiesPlacement_CallsPlaceServiceBuilding` (Deliverable I);
  plus 5 sparse-overlay tests: SparseOverlay_InsertsEntry, Demolish_SparseOverlay_ErasesEntry,
  NewGameLoad_ClearsOverlay, OverlayCap_100K_StillCalls, SetMapDimensions_Recall_ClearsOverlay;
  plus 2 button-init tests: ZoneSubPanel_ButtonsInitialized, UtilitiesSubPanel_ButtonsInitialized)
- `CitySimulation_PlaceRoad_FiresSFXRoadBuild` simulation unit test passes under
  `ctest -LE "integration|requires-opengl"` (Deliverable J — located in
  `tests/simulation/audio_sim_test.cpp`, added to `simulation_tests` target)
- `UIManager::getActiveTool()` returns the correct `ActiveTool` after each toolbar button click
  and hotkey press (verified by `WorldInteraction_NoActiveTool_LeftClickIgnored` and related tests)
- Zone overlay is visually correct: newly placed tiles appear in the correct zone colour within
  one frame of placement; demolished tiles clear within one frame
- **Spec gap I resolved**: Utilities tool places service buildings via
  `ICitySimulation::placeServiceBuilding(tileX, tileZ, ServiceBuildingType, earthworksCost)`.
  `ServiceBuildingType` enum is in `simulation_types.h`. Full placement design (sub-panel layout,
  costs, placement rules) is in `architecture/game-design/service-coverage.md`. BLOCK CLEARED.
- `CitySimulation::placeRoad()` calls
  `m_audio->playPositionalSound(SFX_ROAD_BUILD, ...)` (Deliverable J — `SFX_BUILD_PLACE`
  placeholder replaced; Phase 10 `sfx_road_build` precondition satisfied)
- `all-checks-pass` CI gate remains green after Phase 9b changes land

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | Deliverable A (ActiveTool enum in `src/ui/ui_types.h`, `UIManager::m_activeTool` member + `getActiveTool()` getter, Priority-5 toolbar/hotkey dispatch extension); Deliverable D (UIManager world-interaction layer: MouseMove hover-highlight handler, left-click dispatch for all tool modes, overlay dirty-flag management, sub-panel visibility toggling via setElementVisible); `IRenderer::pickTerrainTile` + `setTileHoverHighlight` + `setZoneOverlay` implementation; `IrrlichtRenderer` terrain-system pointer wiring; `ITerrainQuery::getHeightAt` promotion; **Deliverable E.1** (`TerrainSystem::getMapTilesX()` / `getMapTilesZ()` public accessors in `src/terrain/TerrainSystem.h` and `TerrainSystem.cpp`); `main.cpp` wiring (`setRenderer`, `setTerrainQuery`, `setMapDimensions` via E.1 getters); `InspectorPanel::populate()` real implementation; `InspectorPanel` data refresh cadence wiring |
| `gamedesign-ux` | Zone sub-panel layout (3×3 button grid, virtual x:80 px); Utilities sub-panel layout (2×2 button grid, virtual x:80–276 y:176–276, 96×48 px buttons with placement cost labels); zone colour scheme (R/C/I); hover colour scheme per tool mode; confirm Utilities spec gap resolution with `gamedesign-lookandfeel` |
| `gamedesign-lookandfeel` | **COMPLETE**: Utilities placement spec gap resolved (Deliverable I, 2026-03-01); earthworks cost gate confirmed consistent with economy balance; zone/road/service placement costs confirmed for V1 difficulty tiers |
| `test-dev-cpp` | All 17 `WorldInteractionTest` unit tests in `tests/ui/world_interaction_test.cpp`; `MockRenderer` extension stubs (including sparse-map `setZoneOverlay`); `ManualTerrainQuery` extension for `getHeightAt`; **Deliverable J test**: `CitySimulation_PlaceRoad_FiresSFXRoadBuild` in `tests/simulation/audio_sim_test.cpp` (added to `simulation_tests` target via `target_sources`) |
| `sound-dev-opensoftal` | **Deliverable J**: replace `SFX_BUILD_PLACE` with `SFX_ROAD_BUILD` in `CitySimulation::placeRoad()` (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`) |

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
  and `sfx_road_build` SFX callbacks must fire from real placement dispatch, not stubs.
  Deliverable J (this phase) replaces the Phase 6 `SFX_BUILD_PLACE` placeholder in
  `CitySimulation::placeRoad()` with `SFX_ROAD_BUILD` (SoundId = 3), satisfying this
  Phase 10 precondition.
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
