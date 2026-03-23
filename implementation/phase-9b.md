## Phase 9b: World Interaction — Tile Placement & Ray-Cast Dispatch

**Status: DONE**

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

#### A. `ActiveTool` Enum, `UIManager` State Tracking, and Required Constants Headers

- [x] **Create `src/ui/hud_sprite_ids.h`** (non-class constants header, snake\_case filename
  per CLAUDE.md): defines all `kSprite*` `constexpr uint32_t` constants for the
  `hud_sprites_ui.dds` 2048×2048 sprite sheet. The authoritative cell layout and complete
  constant list (handles 0–323 for all V1 Phase 9b icons) are specified in
  `architecture/asset-standards/2d-texture-standards.md` — UI Sprite Sheet Cell Layout
  section. Copy the `hud_sprite_ids.h` code block from that section verbatim. The file
  must have `#pragma once`, include `<cstdint>`, and contain ONLY `constexpr uint32_t`
  constants — no class declaration, no Irrlicht dependency. This header must exist before
  any `IUIBackend::setElementImage()` call in `UIManager` or `HUD` is implemented.

  **Sprite sheet load path (RESOLVED)**: `IrrlichtUIBackend` loads
  `assets/textures/ui/hud_sprites_ui.dds` internally during its own initialization, before
  any `UIManager` panel code runs. `UIManager` does NOT call `loadTexture()` for the sprite
  sheet. `UIManager` only calls `m_backend->setElementImage(buttonHandle, kSpriteXxx)`, where
  `kSpriteXxx` is a `constexpr uint32_t` cell-index constant from `src/ui/hud_sprite_ids.h`
  (e.g. `kSpriteZoneResLowActive = 64`, `kSpriteZoneResLowInactive = 96`). The backend decodes
  this integer as `col = handle % 32; row = handle / 32;` and renders the corresponding
  sub-rect of the pre-loaded sprite sheet. **In unit tests**: `MockUIBackend::setElementImage()`
  is called with the actual `kSprite*` integer constants — tests assert on these integer values
  directly, not on a loadTexture sentinel. `MockUIBackend::loadTexture()` is NOT expected to
  be called during Phase 9b UIManager construction for sprite-sheet icon swaps.
  **Authoritative documentation**: `architecture/asset-standards/2d-texture-standards.md` —
  "Runtime asset path" and "MockUIBackend test contract" subsections of UI Sprite Sheet Cell
  Layout section.
  <!-- graphics-dev-irrlicht verified: src/ui/hud_sprite_ids.h exists; has #pragma once at line 5, #include <cstdint> at line 6; kSpriteZoneResLowActive=64 at line 23, kSpriteZoneResLowInactive=96 at line 34, kSpriteUtilPowerActive=128 at line 45, kSpriteUtilPowerInactive=160 at line 51; no Irrlicht dependency; 83 lines total covering rows 0-10, 2026-03-03 -->

- [x] **Create `src/ui/ui_constants.h`** (non-class constants header): defines all named
  ARGB `constexpr uint32_t` constants for tile hover highlight and zone colour overlay, as
  authorised in `architecture/ui-ux/hud-layout.md` — Tile Hover Highlight ARGB Colour
  Scheme and Zone Colour Overlay ARGB Colour Scheme sections. Must contain exactly:

  ```cpp
  // src/ui/ui_constants.h
  // ARGB encoding: 0xAARRGGBB (Irrlicht SColor format).
  // Authoritative values: architecture/ui-ux/hud-layout.md
  #pragma once
  #include <cstdint>

  // Tile hover highlight — alpha=0x80 (50% opacity)
  constexpr uint32_t kHoverArgbZone       = 0x80FF00FFu; // magenta
  constexpr uint32_t kHoverArgbRoad       = 0x8000FFFFu; // cyan
  constexpr uint32_t kHoverArgbUtilities  = 0x80FF8000u; // orange
  constexpr uint32_t kHoverArgbDemolish   = 0x80FF0000u; // red
  constexpr uint32_t kHoverArgbQuery      = 0x80FFFFFFu; // white
  constexpr uint32_t kHoverArgbClear      = 0x00000000u; // clear / no highlight

  // Zone colour overlay — alpha=0x60 (~38% opacity)
  constexpr uint32_t kOverlayArgbResidential = 0x6000FF00u; // green
  constexpr uint32_t kOverlayArgbCommercial  = 0x600000FFu; // blue
  constexpr uint32_t kOverlayArgbIndustrial  = 0x60FFFF00u; // yellow
  ```

  `ui_constants.h` must NOT include any Irrlicht headers. Used from `UIManager`,
  `HUD`, and `IrrlichtRenderer` (for the overlay quad colour values passed back from
  `UIManager`). Also used in tests — `WorldInteraction_ZonePlacement_SparseOverlay_
  InsertsEntry` asserts `capturedMap.at(43) == kOverlayArgbResidential` (not a raw hex).
  <!-- graphics-dev-irrlicht verified: src/ui/ui_constants.h exists; #pragma once at line 1; kOverlayArgbResidential=0x6000FF00u at line 28, kOverlayArgbCommercial=0x600000FFu at line 29, kOverlayArgbIndustrial=0x60FFFF00u at line 30, kHoverArgbZone=0x80FF00FFu at line 34, kHoverArgbRoad=0x8000FFFFu at line 35, kHoverArgbDemolish=0x80FF0000u at line 37, kHoverArgbQuery=0x80FFFFFFu at line 38, kHoverArgbClear=0x00000000u at line 39; no Irrlicht headers, 2026-03-03 -->

- [x] Define `enum class ActiveTool { None, Zone, Road, Utilities, Demolish, Query }` in
  `src/ui/ui_types.h` alongside the existing `GameState`/`GameMode` enums. `None` means no tool
  is active (camera-only mode, same as current Phase 8 state). (ref:
  `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/hotkey-scheme.md`)
  <!-- graphics-dev-irrlicht verified: ActiveTool enum defined at src/ui/ui_types.h lines 32-39 with all six enumerators (None/Zone/Road/Utilities/Demolish/Query); no Irrlicht dependency, alongside GameState and GameMode enums, 2026-03-03 -->

- [x] Add `ActiveTool m_activeTool{ActiveTool::None}` private member to `UIManager`. Add a
  public getter `ActiveTool getActiveTool() const;` for test observability. (ref:
  `architecture/ui-ux/input-arbitration.md` Priority 5)
  <!-- graphics-dev-irrlicht verified: m_activeTool{ActiveTool::None} at UIManager.h line 196; getActiveTool() at UIManager.h line 138, implemented at UIManager.cpp line 1266, 2026-03-03 -->

- [x] Add `int m_mapTilesX{0}` and `int m_mapTilesZ{0}` as private members to `UIManager`.
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
  <!-- graphics-dev-irrlicht verified: m_mapTilesX{0}/m_mapTilesZ{0} at UIManager.h lines 212-213; setMapDimensions() at UIManager.h line 135, implemented at UIManager.cpp lines 1244-1264 with re-call safety clearing m_overlayMap and calling setZoneOverlay on dimension change, 2026-03-03 -->

- [x] Extend Priority-5 toolbar dispatch in `UIManager::onEvent()` to set `m_activeTool` in
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
  <!-- graphics-dev-irrlicht verified: Priority-5 hotkeys Z/R/U/D/I set m_activeTool in UIManager.cpp lines 481-535; I-key toggles ActiveTool::Query/None; toolbar button y-range clicks handled in the same onEvent() Priority-5 block, 2026-03-03 -->

#### B. `IRenderer` — Terrain Tile Ray-Cast Interface

- [x] Add the following method to `IRenderer` (`src/interfaces/IRenderer.h`):

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
  <!-- graphics-dev-irrlicht verified: pickTerrainTile() pure virtual declared at IRenderer.h lines 120-121; ScreenRect struct defined at IRenderer.h line 62; header includes <unordered_map> and is Irrlicht-free, 2026-03-03 -->

- [x] Implement `IrrlichtRenderer::pickTerrainTile()` in `src/rendering/IrrlichtRenderer.cpp`:
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
  - **BLOCKING SPIKE RESOLVED (2026-03-02)**: The O(1) DDA grid-intersection + heightmap-
    refinement algorithm is mandated by the spike result (see Risks & Spikes section for full
    analysis). The naive 4096-step linear march is NOT used. Use the DDA algorithm below.
  - **O(1) DDA grid-intersection algorithm** (replaces naive linear march):
    1. Compute the ray start point on the flat Y=0 plane and the ray direction from
       `getRayFromScreenCoordinates()`. Let `rayStart` = ray origin, `rayDir` = normalised
       ray direction (Z-component must be non-zero; if `|rayDir.Y| < 1e-5f` the ray is
       horizontal and cannot intersect terrain — return false immediately).
    2. Convert `rayStart` world position to fractional tile coordinates:
       `tileF_X = rayStart.X / m_cellSize`, `tileF_Z = rayStart.Z / m_cellSize`.
       Clamp starting tile to map bounds `[0, mapTilesX-1] × [0, mapTilesZ-1]`.
    3. Compute the DDA step direction and per-axis `tDelta` (distance along the ray between
       consecutive tile boundary crossings in X and Z): `tDeltaX = |m_cellSize / rayDir.X|`
       (infinite if `rayDir.X == 0`), similarly `tDeltaZ`. This is the standard 3D DDA
       grid traversal ("A Fast Voxel Traversal Algorithm", Amanatides & Woo 1987).
    4. Traverse tiles one grid cell at a time in O(1) per step (advancing either the X or Z
       boundary crossing, whichever is closer). At each cell `(tileX, tileZ)`:
       - Sample `float h = m_terrain->getHeightAt(tileX, tileZ)`.
       - Compute the ray Y at the tile centre X/Z: `rayY = rayStart.Y + t * rayDir.Y`
         where `t` is the current ray parameter at the tile centre.
       - If `rayY <= h`: the ray has crossed below terrain — this tile is the hit. Set
         `tileX` and `tileZ` from current cell, clamp to map bounds, return true.
       - Advance to the next cell boundary (standard DDA step).
       - If the current tile exits the map bounds `[0, mapTilesX-1] × [0, mapTilesZ-1]`:
         return false (ray left the map without a terrain hit — sky or off-map click).
    5. Maximum traversal cells = `mapTilesX + mapTilesZ` (diagonal worst case for a
       1024×1024 map = 2048 cells). At ≤2048 cells × ~15 ns per cell (heightmap array lookup
       in L3 cache) = ≤30 µs worst case, well within the 1 ms per-call budget at 60 FPS even
       when multiple `MouseMove` events fire per frame. See Risks & Spikes for spike analysis.
  - Convert the world-space hit cell to tile grid coordinates: `tileX` and `tileZ` are
    already integer tile indices from the DDA traversal above. Clamp to map bounds
    `[0, mapTilesX-1] × [0, mapTilesZ-1]`; return false if no hit found.
  - The full DDA algorithm is specified in
    `architecture/graphics-architecture/procedural-terrain.md` — "pickTerrainTile DDA
    Algorithm" section, which is the normative reference for the implementation. The phase
    plan here is a summary; the architecture file is the canonical spec.
  - `IrrlichtRenderer` must store a non-owning `ITerrainQuery* m_terrain{nullptr}` pointer
    (set via a new method `setTerrainQuery(ITerrainQuery* terrain)` called from `main.cpp`
    after terrain generation). `IrrlichtRenderer` does NOT hold a pointer to the concrete
    `TerrainSystem` class — only to the `ITerrainQuery` interface.
  - **Null-check guard (required)**: at the very start of `pickTerrainTile()`, before any
    `m_terrain` dereference: `if (!m_terrain) return false;` — prevents a null-pointer crash
    in the window between `IrrlichtRenderer` construction and the `setTerrainQuery()` call
    from `main.cpp`. The main.cpp wiring order guarantees `m_terrain` is set before gameplay
    starts, but the guard is required as a defensive check.
  - **DDA-required additional members on `IrrlichtRenderer`** (all private, all on
    `IrrlichtRenderer` directly, NOT on `IRenderer`):
    - `int m_mapTilesX{0}`, `int m_mapTilesZ{0}` — set by a new public method
      `void setRendererMapDimensions(int w, int z)` on `IrrlichtRenderer` (not on `IRenderer`).
      Default to 0; un-wired renderer returns false immediately from the DDA bounds check.
      Called from `main.cpp` step (2b) (see Deliverable H). Add guard at top of
      `pickTerrainTile()`: `if (m_mapTilesX <= 0 || m_mapTilesZ <= 0) return false;`.
    - `irr::scene::ICameraSceneNode* m_camera{nullptr}` — active scene camera, required by
      `smgr->getSceneCollisionManager()->getRayFromScreenCoordinates(pos, m_camera)`.
      Assigned in `IrrlichtRenderer::init()` when the camera node is created. **If
      `IrrlichtRenderer` already stores the active camera under another name, reuse that
      pointer** — do NOT create a duplicate. Add guard: `if (!m_camera) return false;` at
      the top of `pickTerrainTile()` alongside the `m_terrain` guard. Lives only in
      `IrrlichtRenderer.h` (not in `IRenderer.h`, which must remain Irrlicht-free).
  - **NOTE**: `IrrlichtRenderer` forward-declares `ITerrainQuery`; the full include of
    `ITerrainQuery.h` lives in `.cpp` only to preserve the Irrlicht-free nature of `IRenderer.h`.
    The concrete `TerrainSystem` type is never mentioned in any `IrrlichtRenderer` header. (ref:
    `architecture/graphics-architecture/procedural-terrain.md` — Heightmap Query API)
  <!-- graphics-dev-irrlicht verified: IrrlichtRenderer::pickTerrainTile() implemented at IrrlichtRenderer.cpp line 368 using DDA grid-traversal (Amanatides & Woo 1987); guards for m_terrain/m_camera/m_mapTilesX/m_cellSize at lines 371-374; m_terrain/m_cellSize/m_mapTilesX/Z members declared at IrrlichtRenderer.h lines 134-137; ITerrainQuery forward-declared at IrrlichtRenderer.h line 13, 2026-03-03 -->

- [x] Add `virtual bool pickTerrainTile(int, int, int&, int&) const = 0;` to `MockRenderer`
  in `tests/simulation/mock_renderer.h`. Default mock action: return `false`. (ref:
  `architecture/testing/testability-architecture.md`)
  <!-- graphics-dev-irrlicht verified: MOCK_METHOD(bool, pickTerrainTile, (int, int, int&, int&), (const, override)) at mock_renderer.h lines 45-47; ON_CALL default returns false at lines 30-32, 2026-03-03 -->

#### C. `IRenderer` — Tile Hover Highlight and Zone Colour Overlay

- [x] Add the following methods to `IRenderer` (`src/interfaces/IRenderer.h`):

  ```cpp
  // Render a single-tile wireframe hover highlight at the given tile grid coordinate.
  // ARGB colour encoded as 0xAARRGGBB (Irrlicht SColor format): AA=alpha, RR=red,
  // GG=green, BB=blue. E.g. 0x6000FF00 = semi-transparent green.
  // Pass kInvalidTile (-1,-1) to clear.
  // Called once per frame from the game loop, before endFrame().
  // main-thread-only.
  virtual void setTileHoverHighlight(int tileX, int tileZ, uint32_t argb) = 0;

  // Render a semi-transparent colour fill overlay for all zoned tiles.
  // Colour scheme (ARGB 0xAARRGGBB; alpha=0x60 ≈ 38%):
  //   R = kOverlayArgbResidential (0x6000FF00, green)
  //   C = kOverlayArgbCommercial  (0x600000FF, blue)
  //   I = kOverlayArgbIndustrial  (0x60FFFF00, yellow)
  // Constants in src/ui/ui_constants.h. Authoritative values in
  // architecture/ui-ux/hud-layout.md — Zone Colour Overlay ARGB Colour Scheme.
  // sparseOverlay maps (tileZ * mapTilesX + tileX) -> ARGB color for tiles
  // with a non-zero zone overlay; tiles absent from the map have transparent
  // (no) overlay. Capped at 100K simultaneous entries for V1.
  // Called once per budget tick when zone layout changes.
  // main-thread-only.
  virtual void setZoneOverlay(int mapTilesX, int mapTilesZ,
                              const std::unordered_map<uint64_t, uint32_t>& sparseOverlay) = 0;
  ```

  (ref: `architecture/ui-ux/hud-layout.md` — Tile Hover Highlight ARGB Colour Scheme,
  Zone Colour Overlay ARGB Colour Scheme; `architecture/game-design/zoning-system.md` —
  zone types R/C/I; `src/ui/ui_constants.h` — `kOverlayArgbResidential` et al.)

- [x] Add the following method to `IRenderer` (`src/interfaces/IRenderer.h`), immediately after
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
  <!-- graphics-dev-irrlicht verified: setTileHoverHighlight/setZoneOverlay/getTileScreenBounds all declared as pure virtual in IRenderer.h lines 131/147/157; MockRenderer MOCK_METHODs at mock_renderer.h lines 48-57 with ON_CALL defaults (false/no-op/ScreenRect{}); ScreenRect struct at IRenderer.h line 62, 2026-03-03 -->

- [x] Implement `IrrlichtRenderer::setTileHoverHighlight()`: build a single-quad `SMeshBuffer`
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
  <!-- graphics-dev-irrlicht verified: IrrlichtRenderer::setTileHoverHighlight() at IrrlichtRenderer.cpp line 473; m_hoveredTileMesh allocated in constructor (line 35), pre-populated with 4 vertices+6 indices; in-place vertex update path at lines 505-516; recalculateBoundingBox on buffer and mesh; drawMeshBuffer called in drawScene() at line 100 guarded by m_hoverVisible, 2026-03-03 -->

- [x] Implement `IrrlichtRenderer::setZoneOverlay()`: maintain an internal `SMesh*` overlay
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
  <!-- graphics-dev-irrlicht verified: IrrlichtRenderer::setZoneOverlay() at IrrlichtRenderer.cpp line 531; eviction sequence (material slot clear, old node remove) at lines 538-549; empty-map early return at line 553; 100K cap at line 556; recalculateBoundingBox on each buffer and parent mesh; m_overlayNode assigned at line 639; newMesh->drop() after addMeshSceneNode, 2026-03-03 -->

- [x] Add no-op stubs for `setTileHoverHighlight` and `setZoneOverlay` (with the sparse-map
  signature) to `MockRenderer`.
  <!-- graphics-dev-irrlicht verified: setTileHoverHighlight and setZoneOverlay (using ZoneOverlayMap alias) MOCK_METHODs at mock_renderer.h lines 48-54; getTileScreenBounds at lines 55-57; all confirmed present in tests/simulation/mock_renderer.h, 2026-03-03 -->

#### D. Game Loop — Per-Frame Hover and Left-Click Dispatch

- [x] In `UIManager::onEvent()`, after the five UI priority levels (1–5) return `false` (event
  not consumed by any UI handler), add a **world-interaction block** at the tail of
  `UIManager::onEvent()` that handles `MouseMove` and `MouseButtonDown button=0` (left-click)
  events when `m_state == GameState::Gameplay` and `m_activeTool != ActiveTool::None`. This
  block implements Priority 7 of the input-arbitration chain.
  (ref: `architecture/ui-ux/input-arbitration.md` Priority 7 — World Interaction layer)
  <!-- graphics-dev-irrlicht verified: world-interaction block at tail of UIManager::onEvent() starts at UIManager.cpp line 753 with `if (m_state == GameState::Gameplay && m_activeTool != ActiveTool::None)`; null-guard `if (!m_renderer) return false;` at line 755; Priority 7 comment at line 745; handles MouseButtonUp (line 757), MouseMove (line 762), and MouseButtonDown (line 796), 2026-03-03 -->

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

- [x] `UIManager` requires access to `IRenderer*` for `pickTerrainTile()` calls. Add
  `IRenderer* m_renderer{nullptr}` as a private member, set via a new method
  `void setRenderer(IRenderer* renderer)`. Called from `main.cpp` after `IrrlichtRenderer` is
  constructed. This avoids changing the locked 4-parameter constructor signature. (ref:
  `architecture/ui-ux/ui-manager.md`)
  <!-- graphics-dev-irrlicht verified: IRenderer* m_renderer{nullptr} private member at UIManager.h line 190; setRenderer(IRenderer*) public method declared at UIManager.h line 123 with rationale comment; implemented at UIManager.cpp line 1296, 2026-03-03 -->

- [x] `UIManager` requires access to `ITerrainQuery*` for earthworks cost pre-computation. Add
  `ITerrainQuery* m_terrain{nullptr}` as a private member, set via
  `void setTerrainQuery(ITerrainQuery* terrain)`. Called from `main.cpp` after
  `TerrainSystem` is constructed. (ref: `architecture/game-design/terrain-interaction.md`)
  <!-- graphics-dev-irrlicht verified: ITerrainQuery* m_terrain{nullptr} private member at UIManager.h line 191; setTerrainQuery(ITerrainQuery*) public method declared at UIManager.h line 127; implemented at UIManager.cpp line 1300, 2026-03-03 -->

- [x] **MouseMove handler** (world interaction layer, only when `m_activeTool != None`):
  - Call `m_renderer->pickTerrainTile(event.screenX, event.screenY, tileX, tileZ)`.
  - If hit: look up the hover highlight ARGB constant for the active tool from
    `src/ui/ui_constants.h` (constants defined in `architecture/ui-ux/hud-layout.md` —
    Tile Hover Highlight ARGB Colour Scheme section):
    - Zone: `kHoverArgbZone` (`0x80FF00FF` — semi-transparent magenta)
    - Road: `kHoverArgbRoad` (`0x8000FFFF` — semi-transparent cyan)
    - Utilities: `kHoverArgbUtilities` (`0x80FF8000` — semi-transparent orange)
    - Demolish: `kHoverArgbDemolish` (`0x80FF0000` — semi-transparent red)
    - Query: `kHoverArgbQuery` (`0x80FFFFFF` — semi-transparent white)
  - Call `m_renderer->setTileHoverHighlight(tileX, tileZ, colour)`.
  - Store `m_hoveredTile = {tileX, tileZ}` for the left-click handler.
  - If no hit: call `m_renderer->setTileHoverHighlight(-1, -1, kHoverArgbClear)` to clear
    (`kHoverArgbClear = 0x00000000u`).
  (ref: `architecture/ui-ux/hud-layout.md` — Tile Hover Highlight ARGB Colour Scheme)
  <!-- graphics-dev-irrlicht verified: MouseMove handler in world-interaction layer at UIManager.cpp lines 762-793; calls pickTerrainTile(event.physX, event.physY, hitX, hitZ) at line 764; switch on m_activeTool selects kHoverArgbZone/Road/Utilities/Demolish/Query at lines 767-774; calls setTileHoverHighlight(hitX, hitZ, colour) at line 775; drag-to-zone via m_lmbHeld check at lines 780-784 (calls doTerrainPlacement when tile changes); no-hit path calls setTileHoverHighlight(-1,-1,kHoverArgbClear) at line 789; returns false at line 793, 2026-03-03 -->

- [x] **Left-click handler** (world interaction layer, only when `m_activeTool != None` and
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
  <!-- graphics-dev-irrlicht verified: left-click handler (Priority 7) at UIManager.cpp lines 796-813; sets m_lmbHeld=true at line 797; Query-tool early-out at lines 800-802 (returns false); calls pickTerrainTile(event.physX, event.physY, hitX, hitZ) at line 808; on hit delegates to doTerrainPlacement(hitX, hitZ) at line 812; doTerrainPlacement() at line 826 computes slope/earthworksCost, checks getTreasuryBalance() at line 839, dispatches placeZone at line 854/placeRoad at line 881/placeServiceBuilding at line 886/demolishTile at line 907, 2026-03-03 -->

- [x] **Priority 3 — QueryTool open path**: In `UIManager::onEvent()`, after the existing
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
  <!-- graphics-dev-irrlicht verified: Priority-3 QueryTool open path at UIManager.cpp lines 350-383; guard condition checks !m_inspectorOpen && m_activeTool==ActiveTool::Query && m_state==GameState::Gameplay && MouseButtonDown button==0 && m_renderer && !toolbar-carve-out; calls pickTerrainTile(event.physX, event.physY, hitX, hitZ) at line 365; on hit calls m_sim->queryTile(hitX, hitZ) at line 367, m_renderer->getTileScreenBounds(hitX, hitZ) at line 370, m_inspector->populate(result, hitX, hitZ, event.x, event.y, tileBounds) at line 374; sets m_inspectorOpen=true and returns true; no-hit path falls through at line 381, 2026-03-03 -->

- [x] **Zone sub-panel** (Zone tool active): a compact sub-panel appears immediately to the right
  of the toolbar showing the 9 zone-type + density combinations (R/C/I × Low/Medium/High) as a
  3×3 button grid. Active selection highlighted. `UIManager` tracks
  `ZoneType m_selectedZoneType{ZoneType::Residential}` and
  `DensityTier m_selectedDensityTier{DensityTier::Low}`. Sub-panel is hidden when Zone tool is
  not active.
  <!-- graphics-dev-irrlicht verified: 9 zone sub-panel buttons created in UIManager constructor at UIManager.cpp lines 122-164; 3-column (zone type) x 3-row (density) loop starting at line 138; addButton() calls at line 149; buttons anchored at virtual (x:80, y:64) with 64x40px size and 4px gap; setElementImage with kSpriteZoneResLowInactive+offset at lines 152-155; setElementVisible(false) at line 158; default active override (kSpriteZoneResLowActive) at line 163; m_selectedZoneType{0}/m_selectedDensityTier{0} members at UIManager.h lines 203-204, 2026-03-03 -->

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
  after creating the 9 buttons, call `IUIBackend::setElementImage(handle, inactiveHandle)` on
  all 9 buttons to set the inactive/outline-icon sprite, where `inactiveHandle` for each button
  at `(zoneCol, densityRow)` is `kSpriteZoneResLowInactive + zoneCol + densityRow * 3` (from
  `src/ui/hud_sprite_ids.h`). Then call `IUIBackend::setElementImage(handle, activeHandle)` on
  the default-selected button (column 0 = Residential, row 0 = Low), where
  `activeHandle = kSpriteZoneResLowActive` (handle 64). This ensures buttons have correct
  visual state on first display rather than showing a blank/undefined image. The same
  sprite-swap logic applies on each subsequent button click (see Zone sub-panel click handler
  below).

  **Sprite handle formula for Zone sub-panel** (ref: `architecture/asset-standards/
  2d-texture-standards.md` — UI Sprite Sheet Cell Layout, Rows 2 and 3):
  - Active handle: `kSpriteZoneResLowActive + zoneCol + densityRow * 3`
  - Inactive handle: `kSpriteZoneResLowInactive + zoneCol + densityRow * 3`

  Where `zoneCol = static_cast<int>(zoneType)` (0=Residential, 1=Commercial, 2=Industrial)
  and `densityRow = static_cast<int>(densityTier)` (0=Low, 1=Medium, 2=High). This formula
  is valid because the zone sub-panel icons occupy contiguous cells in `hud_sprites_ui.dds`
  rows 2 and 3 in the same 3-column (R/C/I) × 3-row (Low/Med/High) order as the sub-panel
  grid. All named constants come from `src/ui/hud_sprite_ids.h`.
  <!-- gamedesign-ux verified: UIManager.h confirms m_zoneSubPanelBtns[9] array; UIManager.cpp constructor (lines 117-153) creates all 9 buttons via addButton() in a 3-col x 3-row loop at virtual (x:80,y:64) with 64x40 px buttons, sets inactive sprite kSpriteZoneResLowInactive+offset for all 9, then overrides index 0 with kSpriteZoneResLowActive; hud_sprite_ids.h confirms kSpriteZoneResLowActive=64 and kSpriteZoneResLowInactive=96, 2026-03-03 -->

- [x] **Zone sub-panel click handler** (Priority 5 — zone sub-panel buttons are
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
  <!-- graphics-dev-irrlicht verified: zone sub-panel click handler in Priority-5 block at UIManager.cpp lines 574-612; hit-tests 9 button positions using same (x:80,y:64,64x40px,gap=4) constants as construction at lines 580-587; updates m_selectedZoneType=zoneCol/m_selectedDensityTier=densityRow at lines 589-590; swaps all 9 to inactive sprite (kSpriteZoneResLowInactive+offset) then sets active sprite (kSpriteZoneResLowActive+zoneCol+densityRow*3) on clicked button at lines 592-606; returns true at line 608; setElementEnabled(false) not used, 2026-03-03 -->
  <!-- gamedesign-ux verified: UIManager.cpp (lines 550-583) hit-tests all 9 zone sub-panel positions, updates m_selectedZoneType and m_selectedDensityTier from zoneCol/densityRow, swaps all 9 to kSpriteZoneResLowInactive+offset then sets kSpriteZoneResLowActive+zoneCol+densityRow*3 on the clicked button; setElementEnabled(false) is not used for selection, 2026-03-03 -->

  (ref: `architecture/game-design/zoning-system.md`, `architecture/ui-ux/hud-layout.md`)

- [x] **Utilities sub-panel** (Utilities tool active): a compact sub-panel appears immediately to
  the right of the toolbar (aligned with the Utilities button row) showing the four service
  building types as a 2×2 button grid:
  <!-- graphics-dev-irrlicht verified: 4 utilities sub-panel buttons created in UIManager constructor at UIManager.cpp lines 166-206; 2-column x 2-row loop starting at line 186; addButton("", ...) calls at line 192; buttons anchored at virtual (x:80, y:176) with 96x48px size and 4px gap; setElementImage with kSpriteUtilPowerInactive+typeIdx at lines 195-197; setElementVisible(false) at line 200; default active override (kSpriteUtilPowerActive) at line 205; m_utilSubPanelBtns[4] array at UIManager.h lines 246-249; m_selectedServiceBuilding{0} at UIManager.h line 208, 2026-03-03 -->

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
  call `IUIBackend::setElementImage(handle, inactiveHandle)` on all 4 buttons, where
  `inactiveHandle = kSpriteUtilPowerInactive + static_cast<int>(type)` (from
  `src/ui/hud_sprite_ids.h`). Then call `IUIBackend::setElementImage(handle, activeHandle)`
  on the default-selected button (PowerPlant — `ServiceBuildingType::PowerPlant`), where
  `activeHandle = kSpriteUtilPowerActive` (handle 128). Same sprite-swap logic applies on
  each subsequent click (see Utilities sub-panel click handler below).

  **Sprite handle formula for Utilities sub-panel** (ref: `architecture/asset-standards/
  2d-texture-standards.md` — UI Sprite Sheet Cell Layout, Rows 4 and 5):
  - Active handle: `kSpriteUtilPowerActive + static_cast<int>(type)`
  - Inactive handle: `kSpriteUtilPowerInactive + static_cast<int>(type)`

  Where `type` is `ServiceBuildingType` (PowerPlant=0, WaterTower=1, FireStation=2,
  PoliceStation=3). All named constants from `src/ui/hud_sprite_ids.h`.
  <!-- gamedesign-ux verified: UIManager.h confirms m_utilSubPanelBtns[4] array; UIManager.cpp constructor (lines 155-195) creates all 4 buttons via addButton() in a 2-col x 2-row loop at virtual (x:80,y:176) with 96x48 px buttons, sets inactive sprite kSpriteUtilPowerInactive+typeIdx for all 4, then overrides index 0 with kSpriteUtilPowerActive; hud_sprite_ids.h confirms kSpriteUtilPowerActive=128 and kSpriteUtilPowerInactive=160, 2026-03-03 -->

- [x] **Utilities sub-panel click handler** (Priority 5 — same as Zone sub-panel: buttons are
  `IUIBackend::addButton()` elements generating `EGET_BUTTON_CLICKED` events, dispatched in
  the Priority 5 block): when a Utilities sub-panel button is clicked, update
  `m_selectedServiceBuilding` to the corresponding `ServiceBuildingType`. Button layout:
  (col0 row0)=PowerPlant, (col1 row0)=WaterTower, (col0 row1)=FireStation,
  (col1 row1)=PoliceStation. Active selection indicated by leaving all buttons `setElementEnabled(true)` (interactive);
  the selected button is highlighted via `IUIBackend::setElementImage` with an active-state
  sprite (active = filled icon with accent-color border; inactive = outline icon, no border —
  same convention as minimap overlay toggle buttons per `architecture/ui-ux/minimap.md`). Do
  NOT use `setElementEnabled(false)` for this purpose.
  <!-- graphics-dev-irrlicht verified: utilities sub-panel click handler in Priority-5 block at UIManager.cpp lines 614-643; hit-tests 4 button positions using same (x:80,y:176,96x48px,gap=4) constants as construction at lines 618-625; updates m_selectedServiceBuilding=typeIdx at line 627; swaps all 4 to inactive sprite (kSpriteUtilPowerInactive+t2) then sets active sprite (kSpriteUtilPowerActive+typeIdx) on clicked button at lines 629-639; returns true at line 640; setElementEnabled(false) not used, 2026-03-03 -->
  <!-- gamedesign-ux verified: UIManager.cpp (lines 585-614) hit-tests all 4 utility sub-panel positions, updates m_selectedServiceBuilding from typeIdx, swaps all 4 to kSpriteUtilPowerInactive+t2 then sets kSpriteUtilPowerActive+typeIdx on the clicked button; setElementEnabled(false) is not used for selection, 2026-03-03 -->

- [x] Both sub-panels (Zone and Utilities) are fully rendered in Phase 9b — UIManager creates
  their buttons at `init()` time and shows/hides them based on active tool via
  `IUIBackend::setElementVisible()`.
  <!-- graphics-dev-irrlicht verified: updateSubPanelVisibility() helper at UIManager.cpp lines 931-947; showZone=(m_activeTool==ActiveTool::Zone) at line 934, loops 9 zone buttons calling setElementVisible(m_zoneSubPanelBtns[i], showZone) at line 937; showUtil=(m_activeTool==ActiveTool::Utilities) at line 941, loops 4 util buttons calling setElementVisible(m_utilSubPanelBtns[i], showUtil) at line 944; called on every m_activeTool change (toolbar clicks at lines 657/661/665/669/685 and hotkeys at lines 513/521/529/537/563), 2026-03-03 -->

  (ref: `architecture/game-design/service-coverage.md` — Utilities Tool Placement Design)

- [x] **Zone overlay refresh**: after a successful `placeZone` or `demolishTile` call (road
  and service-building placements do NOT modify the zone overlay — only zone tiles have overlay
  colours), update the zone overlay using a `std::unordered_map<uint64_t, uint32_t> m_overlayMap`
  sparse overlay map stored as a private member of `UIManager`, keyed by
  `(tileZ * m_mapTilesX + tileX)`.
  `m_mapTilesX` and `m_mapTilesZ` are the private members populated by `setMapDimensions()`
  (see Deliverable A); they represent the map width and depth in tiles respectively. At zone
  placement, insert or update the entry:
  `m_overlayMap[(tileZ * m_mapTilesX + tileX)] = colour`, where `colour` is derived from
  `selectedZoneType` using named constants from `src/ui/ui_constants.h` (authoritative
  values in `architecture/ui-ux/hud-layout.md` — Zone Colour Overlay ARGB Colour Scheme):
  `Residential→kOverlayArgbResidential` (`0x6000FF00u`), `Commercial→kOverlayArgbCommercial`
  (`0x600000FFu`), `Industrial→kOverlayArgbIndustrial` (`0x60FFFF00u`) (alpha 0x60 ≈ 38%). At demolish, erase the entry:
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
  <!-- graphics-dev-irrlicht verified: m_overlayMap std::unordered_map<uint64_t,uint32_t> private member at UIManager.h lines 221; key=tileZ*m_mapTilesX+tileX at UIManager.cpp line 868; insertion with kOverlayCap=100000u cap-check at UIManager.cpp lines 867-871; setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap) called after placeZone at line 874; erase on demolish at UIManager.cpp line 912 followed by setZoneOverlay at line 914; cap-check via m_overlayMap.size()<kOverlayCap||m_overlayMap.count(key) at line 870, 2026-03-03 -->

#### E. `ITerrainQuery` — Tile Height Query (New Method)

- [x] Add `virtual float getHeightAt(int tileX, int tileZ) const = 0;` to `ITerrainQuery`
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
  <!-- graphics-dev-irrlicht verified: getHeightAt() pure virtual declared at ITerrainQuery.h line 41; TerrainSystem::getHeightAt() override declared at TerrainSystem.h line 149 and implemented at TerrainSystem.cpp line 583; LOD0 heightmap contract documented in both files, 2026-03-03 -->

- [x] Extend `ManualTerrainQuery` (`tests/simulation/manual_terrain_query.h`) to implement the
  new method:

  ```cpp
  float getHeightAt(int tileX, int tileZ) const override { return 0.0f; }
  ```

  The stub always returns 0.0f — unit tests that need specific heights inject `MockRenderer`
  for the renderer path; no Phase 9b test requires `ManualTerrainQuery` to return non-zero
  heights. This override is required because `getHeightAt()` is pure virtual on `ITerrainQuery`;
  without it `ManualTerrainQuery` fails to compile, blocking all 17 Phase 9b unit tests.
  <!-- graphics-dev-irrlicht verified: getHeightAt() override at manual_terrain_query.h line 68 returns 0.0f; confirmed at tests/simulation/manual_terrain_query.h lines 67-68, 2026-03-03 -->

#### E.1. `TerrainSystem` Map-Dimension Accessors (New Public Getters)

- [x] Add `int getMapTilesX() const;`, `int getMapTilesZ() const;`, and `float getCellSize() const;`
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
  <!-- graphics-dev-irrlicht verified: getMapTilesX()/getMapTilesZ()/getCellSize() declared at TerrainSystem.h lines 208-210 and implemented at TerrainSystem.cpp lines 600-602 as one-liner returns of m_mapTilesX/m_mapTilesZ/m_cellSize; intentionally NOT on ITerrainQuery interface, 2026-03-03 -->

#### F. `InspectorPanel` — Real Tile Query Wiring

- [x] Add `void populate(const QueryResult& result, int tileX, int tileZ)` to `InspectorPanel`
  (already exists as a Phase 8 stub with empty body). Fill in the real implementation: set the
  zone type / density label, demand score, desirability, tax yield/month, and demand pressure %
  fields from `result`. Panel opens at the position computed by
  `InspectorPanel::computePanelPosition()` using the three-step cascade defined in
  `architecture/ui-ux/query-inspector-panel.md` (primary → fallback → edge-snap). UIManager
  must supply all three required inputs to `computePanelPosition()` as follows:
  <!-- graphics-dev-irrlicht verified: InspectorPanel::populate() signature (result, tileX, tileZ, clickX, clickY, tileBounds) declared at inspector_panel.h line 42 and implemented at QueryPanel.cpp line 179; computePanelPosition(cursorX, cursorY, tileBounds) updated signature at inspector_panel.h line 72 and implemented at QueryPanel.cpp line 63; real data-field population from QueryResult in populate(); three-step cascade (primary/fallback/edge-snap) in computePanelPosition(), 2026-03-03 -->

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
  <!-- test-dev-cpp verified: QueryPanel_TileOverlap_FallsBackToFallback exists at tests/ui/query_panel_test.cpp line 143 passing tileBounds{250,250,100,100} which overlaps primary candidate at (240,240) and asserting result is on-screen with w=240 h=160; all migrated Phase 8 tests at lines 62-136 use kNoTileBounds{1000,1000,10,10} sentinel (defined line 59); ScreenRect imported via IRenderer.h at line 20, 2026-03-03 -->

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

- [x] Wire `InspectorPanel` data refresh cadence: budget/economy fields refresh once per budget
  tick (poll `m_sim->queryTile` again if `m_inspectorOpen && ticks_since_open > 0`); traffic
  data refresh every 10 simulation frames. "Updated N seconds ago" line shown when data is >1 s
  stale. (ref: `architecture/ui-ux/query-inspector-panel.md`)
  <!-- graphics-dev-irrlicht verified: InspectorPanel::populate() signature at inspector_panel.h line 42 takes QueryResult, tileX, tileZ, cursorX, cursorY, ScreenRect tileBounds; computePanelPosition() at inspector_panel.h line 72 takes (cursorX, cursorY, const ScreenRect& tileBounds) — screenW/screenH removed; kEconomyRefreshFrames=120, kTrafficRefreshFrames=10, kStalenessFrames=60 at inspector_panel.h lines 89-91, 2026-03-03 -->

#### G. Tests

- [x] Define `WorldInteractionTest` as a Google Test fixture class. Private members (declared
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
  <!-- test-dev-cpp verified: WorldInteractionTest fixture confirmed in tests/ui/world_interaction_test.cpp lines 160–273 — StrictMock<MockCitySimulation> sim_, StrictMock<MockRenderer> renderer_, ManualTerrainQuery terrain_, NiceMock<MockUIBackend> backend_, ManualClock clock_, std::unique_ptr<UIManager> uiManager_ (declared last); SetUp() at line 176 calls 4-param UIManager constructor then setRenderer/setTerrainQuery/setMapDimensions; TearDown() at line 204 calls uiManager_.reset() before StrictMock destructors fire, 2026-03-03 -->

- [x] `WorldInteraction_ZonePlacement_CallsPlaceZone` (unit test in `tests/ui/`): construct
  `UIManager` with `StrictMock<MockCitySimulation>`, `StrictMock<MockRenderer>`,
  `ManualTerrainQuery` (slope = 0°), `NiceMock<MockUIBackend>`, `ManualClock`. Set active tool
  to `ActiveTool::Zone`. Stub `MockRenderer::pickTerrainTile` to return `true` and set
  tileX=5, tileZ=7. Send a `MouseButtonDown button=0` event.
  `EXPECT_CALL(sim_, placeZone(5, 7, _, _, 0)).Times(1);` (ref:
  `architecture/testing/testability-architecture.md`)
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_ZonePlacement_CallsPlaceZone) exists at tests/ui/world_interaction_test.cpp line 283; stubs pickTerrainTile to return tile (5,7) and asserts EXPECT_CALL(sim_, placeZone(5, 7, _, _, 0)).Times(1), 2026-03-03 -->

- [x] `WorldInteraction_RoadPlacement_CallsPlaceRoad` (unit test): same fixture; tool =
  `ActiveTool::Road`. `EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);`
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_RoadPlacement_CallsPlaceRoad) exists at tests/ui/world_interaction_test.cpp line 312; activates Road tool and asserts EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1), 2026-03-03 -->

- [x] `WorldInteraction_DemolishTool_SteepSlope_NoEarthworksGuard` (unit test): Demolish tool
  does not have an earthworks guard (demolish does not incur earthworks cost per spec —
  `architecture/game-design/terrain-interaction.md` only defines earthworks cost for placement).
  Verify `demolishTile` is called even at slope > 15°. (ref:
  `architecture/game-design/terrain-interaction.md`)
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_DemolishTool_SteepSlope_NoEarthworksGuard) exists at tests/ui/world_interaction_test.cpp line 334; sets terrain_.setSlope(5,7,30.0f) and asserts EXPECT_CALL(sim_, demolishTile(5, 7)).Times(1) confirming steep slope does not block demolish, 2026-03-03 -->

- [x] `WorldInteraction_ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace` (unit test):
  `ManualTerrainQuery` slope = 30°. `MockCitySimulation::getTreasuryBalance()` returns 0.0f
  (insufficient for earthworks). Verify `placeZone` is NOT called; verify a toast is posted via
  `NotificationManager` (use `EXPECT_CALL(backend_, addStaticText(HasSubstr("insufficient funds"),
  _, _, _, _)).Times(AtLeast(1))`). (ref: `architecture/game-design/terrain-interaction.md`)
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace) exists at tests/ui/world_interaction_test.cpp line 368; sets global slope 30° and treasury 0, asserts placeZone Times(0) and addStaticText(HasSubstr("insufficient funds")) Times(AtLeast(1)), 2026-03-03 -->

- [x] `WorldInteraction_QueryTool_CallsQueryTile` (unit test): Query tool active; left-click;
  `EXPECT_CALL(sim_, queryTile(5, 7)).Times(1)`; verify inspector panel becomes open (check via
  `UIManager::getActiveTool()` still Query, and `m_backend` call for inspector panel creation).
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_QueryTool_CallsQueryTile) exists at tests/ui/world_interaction_test.cpp line 406; activates Query tool, stubs pickTerrainTile to (5,7), asserts EXPECT_CALL(sim_, queryTile(5, 7)).Times(1) and EXPECT_CALL(renderer_, getTileScreenBounds(5,7)), 2026-03-03 -->

- [x] `WorldInteraction_NoActiveTool_LeftClickIgnored` (unit test): `m_activeTool == None`;
  left-click; `EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);`
  `EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);`
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_NoActiveTool_LeftClickIgnored) exists at tests/ui/world_interaction_test.cpp line 434; calls goToGameplay() without activating any tool and asserts both placeZone and placeRoad Times(0), 2026-03-03 -->

- [x] `WorldInteraction_ModalActive_LeftClickNotDispatched` (unit test): `hasActiveModal()==true`
  (Priority 1 consumes the event); left-click must NOT reach the world-interaction layer.
  `EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);`
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_ModalActive_LeftClickNotDispatched) exists at tests/ui/world_interaction_test.cpp line 455; calls showForcedLoanDialog to activate a modal then asserts placeZone Times(0) on left-click, 2026-03-03 -->

- [x] `WorldInteraction_HoverHighlight_SetOnMouseMove` (unit test): `MockRenderer` stubs
  `pickTerrainTile` to return true at (3, 4). Send `MouseMove` event with Zone tool active.
  `EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));`
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_HoverHighlight_SetOnMouseMove) exists at tests/ui/world_interaction_test.cpp line 489; stubs pickTerrainTile to return (3,4) and asserts EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1)) on MouseMove, 2026-03-03 -->

- [x] `WorldInteraction_HoverHighlight_ClearedOnMiss` (unit test): `pickTerrainTile` returns
  false. `EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, kHoverArgbClear)).Times(AtLeast(1));`
  (`kHoverArgbClear` from `src/ui/ui_constants.h` equals `0x00000000u`.)
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_HoverHighlight_ClearedOnMiss) exists at tests/ui/world_interaction_test.cpp line 510; stubs pickTerrainTile to return false and asserts setTileHoverHighlight(-1, -1, kHoverArgbClear) Times(AtLeast(1)), 2026-03-03 -->

- [x] `WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry` (unit test): uses the shared
  `WorldInteractionTest` fixture (which calls `uiManager_->setMapDimensions(10, 10)` in
  `SetUp()`, establishing `m_mapTilesX=10`). Set active tool to `ActiveTool::Zone`. Stub
  `MockRenderer::pickTerrainTile` to return `true` at tileX=3, tileZ=4. Send
  `MouseButtonDown button=0`. After dispatch, capture the `sparseOverlay` argument passed to
  `setZoneOverlay`. Assert that the captured map contains exactly 1 entry whose key equals `43`
  (i.e. `tileZ * m_mapTilesX + tileX = 4 * 10 + 3 = 43`, the placed tile) and whose value
  equals `kOverlayArgbResidential` (`0x6000FF00u` from `src/ui/ui_constants.h`, authoritative
  in `architecture/ui-ux/hud-layout.md` Zone Colour Overlay section — green, alpha 0x60 ≈ 38%).
  A non-zero check is insufficient; the test must pin the exact named constant so that a wrong
  zone-type lookup (e.g. returning Commercial `kOverlayArgbCommercial = 0x600000FFu`) is caught.
  `EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).WillOnce(SaveArg<2>(&capturedMap));`
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry) exists at tests/ui/world_interaction_test.cpp line 532; captures setZoneOverlay sparse map and asserts capturedMap.at(43) == kOverlayArgbResidential with map size 1, 2026-03-03 -->

- [x] `WorldInteraction_Demolish_SparseOverlay_ErasesEntry` (unit test): place a Zone tile at
  (3, 4) via a Zone-tool left-click (same fixture as above; tile is now in `m_overlayMap`).
  Switch active tool to `ActiveTool::Demolish`. Suppress the demolish confirmation modal
  (Settings "Confirm before demolish" = OFF). Send a second `MouseButtonDown button=0` at
  (3, 4). Capture the `sparseOverlay` passed to the second `setZoneOverlay` call. Assert that
  the captured map is empty (the entry was erased on demolish).
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_Demolish_SparseOverlay_ErasesEntry) exists at tests/ui/world_interaction_test.cpp line 567; places zone at (3,4), switches to Demolish tool with modal suppressed, demolishes and asserts second setZoneOverlay sparse map is empty, 2026-03-03 -->

- [x] `WorldInteraction_NewGameLoad_ClearsOverlay` (unit test): pre-populate `m_overlayMap` with
  at least 3 entries by performing 3 zone placements. Trigger new-game load via
  `uiManager_->onNewGame()`. `UIManager::onNewGame()` is the authoritative reset method
  (added to the public interface in Phase 9b — see `architecture/ui-ux/ui-manager.md`): it
  clears `m_overlayMap`, calls `m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, {})` if
  `m_renderer` is non-null, resets `m_activeTool` to `ActiveTool::None`, and clears
  `m_hoveredTile`. Assert that `setZoneOverlay` is called with an empty sparse map: the
  captured `sparseOverlay` argument has `size() == 0`.
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_NewGameLoad_ClearsOverlay) exists at tests/ui/world_interaction_test.cpp line 615; performs 3 zone placements then calls uiManager_->onNewGame() and asserts setZoneOverlay is called with an empty sparse map, 2026-03-03 -->

- [x] `WorldInteraction_OverlayCap_100K_StillCalls` (unit test): drive `UIManager` to insert
  100,000 entries into `m_overlayMap` by calling the internal overlay-insert path directly (or
  via 100K simulated placements on distinct tiles). Confirm that when a 100,001st entry would be
  inserted, the cap prevents storage but `setZoneOverlay` is still called (the call is not
  suppressed). Use `EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(1));` and
  assert the captured map has `size() <= 100000`.
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_OverlayCap_100K_StillCalls) exists at tests/ui/world_interaction_test.cpp line 663; injects 100K entries via setOverlayMapForTest() then triggers one additional placement at key 100000 and asserts setZoneOverlay fires with captured map size <= 100000, 2026-03-03 -->

- [x] `WorldInteraction_SetMapDimensions_Recall_ClearsOverlay` (unit test): call
  `setMapDimensions(10, 10)` during `SetUp` (as normal), then simulate 3 zone placements to
  pre-populate `m_overlayMap`. Capture the `sparseOverlay` argument on the subsequent
  `setZoneOverlay` call. Then call `setMapDimensions(20, 20)` with different dimensions. Assert
  that `setZoneOverlay` is called again with an empty sparse map (`size() == 0`) — confirming
  that the re-call-safety rule clears `m_overlayMap` before the new dimensions are stored. A
  follow-up placement after the resize should use key `tileZ * 20 + tileX` (new width), not
  `tileZ * 10 + tileX` (old width).
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_SetMapDimensions_Recall_ClearsOverlay) exists at tests/ui/world_interaction_test.cpp line 717; places 3 zones on 10x10 map, then calls setMapDimensions(20,20) and asserts setZoneOverlay is called with an empty sparse map, 2026-03-03 -->

- [x] `WorldInteraction_ZoneSubPanel_ButtonsInitialized` (unit test): construct `UIManager`
  with `NiceMock<MockUIBackend>`. Before sending any events, inspect the `setElementImage`
  calls made during construction/`init()`. Assert that `setElementImage` was called with the
  outline-icon sprite handle on all 9 Zone sub-panel button handles, and with the active-state
  sprite handle on exactly one button handle (the default selection: Residential Low, column 0
  row 0).

  **Sprite handle clarification (RESOLVED)**: `outlineHandle` and `activeHandle` are NOT file
  paths obtained via `IUIBackend::loadTexture()`. They are integer constants from
  `src/ui/hud_sprite_ids.h` defined in `architecture/asset-standards/2d-texture-standards.md`
  — UI Sprite Sheet Cell Layout section. `UIManager` calls `IUIBackend::loadTexture(
  "assets/textures/ui/hud_sprites_ui.dds")` ONCE to obtain the sprite sheet texture handle,
  then calls `setElementImage(buttonHandle, kSpriteZoneResLowInactive)` etc. using the integer
  constants directly. In tests, `NiceMock<MockUIBackend>::loadTexture()` returns a sentinel
  `UIElementHandle` (non-zero); `setElementImage` is expected to be called with the actual
  `kSprite*` integer constants (not the loadTexture sentinel). Concretely:

  ```cpp
  // In the test (after UIManager construction):
  EXPECT_CALL(backend_, setElementImage(zone_btn[i],
      kSpriteZoneResLowInactive + col + row * 3)).Times(1); // inactive for 8 non-default
  EXPECT_CALL(backend_, setElementImage(zone_btn[0],
      kSpriteZoneResLowActive)).Times(1); // active for default (ResLow = handle 64)
  ```

  Where `col = zoneTypeIndex (0=Res, 1=Com, 2=Ind)` and `row = densityIndex (0=Low, 1=Med,
  2=High)`. Both `kSpriteZoneResLowActive = 64` and `kSpriteZoneResLowInactive = 96` are from
  `src/ui/hud_sprite_ids.h`. This test closes the testability gap introduced when the
  init()-time button image initialization requirement was added in Fix O.
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_ZoneSubPanel_ButtonsInitialized) exists at tests/ui/world_interaction_test.cpp line 757; asserts setElementImage is called with kSpriteZoneResLowInactive+col+row*3 for all 9 buttons and kSpriteZoneResLowActive (64) for the default Residential Low selection during UIManager construction, 2026-03-03 -->

- [x] `WorldInteraction_UtilitiesSubPanel_ButtonsInitialized` (unit test): same pattern for the
  4 Utilities sub-panel buttons. Assert `setElementImage` called with outline-icon sprite on all
  4 handles during init(), then active-state sprite called on the PowerPlant button handle
  (default selection). The sprite handle constants are `kSpriteUtilPowerInactive` (160) for
  inactive state and `kSpriteUtilPowerActive` (128) for the default-selected PowerPlant button.
  Use `NiceMock<MockUIBackend>` to suppress noise from unrelated `setElementImage` calls on
  other HUD elements; use `EXPECT_CALL` with `InSequence`-or-matcher to verify the sprite-swap
  pattern specifically for the Utilities sub-panel button handles. The `setElementImage` second
  argument is the `kSprite*` integer constant, NOT a loadTexture sentinel handle.
  <!-- test-dev-cpp verified: TEST_F(WorldInteractionTest, WorldInteraction_UtilitiesSubPanel_ButtonsInitialized) exists at tests/ui/world_interaction_test.cpp line 806; asserts setElementImage is called with kSpriteUtilPowerInactive/WaterInactive/FireInactive/PoliceInactive for all 4 buttons and kSpriteUtilPowerActive (128) for the default PowerPlant selection, 2026-03-03 -->

- [x] All new tests are labelled unit (no `requires-opengl`); added to `ui_tests` CMake target
  via `target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp)`. Do NOT call
  `add_executable` or `aitown_add_tests` again for `ui_tests` (duplicate target error). (ref:
  `architecture/testing/framework.md`)
  <!-- test-dev-cpp verified: target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp) confirmed at CMakeLists.txt lines 597-599; target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp) at lines 481-483; both targets use aitown_add_tests LABEL "unit" with no requires-opengl label — all 20 new tests run under ctest -LE "integration|requires-opengl", 2026-03-03 -->
  <!-- cicd-dev-github verified: target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp) confirmed at CMakeLists.txt lines 597-599; target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp) confirmed at CMakeLists.txt lines 481-483; aitown_add_tests(ui_tests LABEL "unit" TIMEOUT 300) at CMakeLists.txt line 574 and aitown_add_tests(simulation_tests LABEL "unit" DISCOVERY_TIMEOUT 60) at CMakeLists.txt line 475 — no requires-opengl label on either target; ctest -LE 'integration|requires-opengl' in build-linux step 14 and coverage-linux step 13 will include both new test files, 2026-03-03 -->

#### H. `main.cpp` Wiring

- [x] After `TerrainSystem` is constructed and before entering the main game loop, call the
  following methods in this exact order to establish all pointers before the first frame:
  - **(1)** Call `terrainSystem.generate()` and `terrainSystem.buildAllChunks()` (called on
    the concrete `TerrainSystem*` instance — these methods are NOT part of the `ITerrainQuery`
    interface).
  - **(2)** Call `renderer.setTerrainQuery(&terrainSystem)` (passes `TerrainSystem*` as
    `ITerrainQuery*`; the renderer stores it as `m_terrain` and calls only `ITerrainQuery`
    interface methods — no concrete `TerrainSystem` API is used inside `IrrlichtRenderer`).
  - **(2a)** Call `renderer.setCellSize(terrainSystem.getCellSize())` — supplies `IrrlichtRenderer`
    with the tile grid spacing (world-space metres per tile) required by `pickTerrainTile()`
    for DDA step computation and tile-index conversion. Must be called on the concrete
    `IrrlichtRenderer` (not `IRenderer*`) since `setCellSize` is not on the interface (same
    rationale as `setTerrainQuery`).
  - **(2b)** Call `renderer.setRendererMapDimensions(terrainSystem.getMapTilesX(),
    terrainSystem.getMapTilesZ())` — supplies `IrrlichtRenderer` with the integer map extents
    required by the DDA bounds check exit condition (`cx >= m_mapTilesX`, `cz >= m_mapTilesZ`).
    The method name `setRendererMapDimensions` avoids collision with
    `UIManager::setMapDimensions`. This method is on `IrrlichtRenderer` directly (NOT on
    `IRenderer`), same rationale as `setCellSize`. It sets private members `m_mapTilesX` and
    `m_mapTilesZ` on `IrrlichtRenderer` (distinct from `UIManager`'s same-named members).
    Must be called after terrain generation. **This call is required** — without it,
    `m_mapTilesX` and `m_mapTilesZ` default to 0 and the DDA traversal exits immediately on
    the first bounds check, causing `pickTerrainTile()` to always return false. (ref:
    `architecture/graphics-architecture/procedural-terrain.md` — pickTerrainTile DDA Algorithm)
  - **(3)** Call `uiManager.setRenderer(&renderer)`.
  - **(4)** Call `uiManager.setTerrainQuery(&terrainSystem)`.
  - **(5)** Call `uiManager.setMapDimensions(terrainSystem.getMapTilesX(),
    terrainSystem.getMapTilesZ())` — supplies `UIManager::m_mapTilesX` and `m_mapTilesZ` so
    the zone overlay key computation (`tileZ * m_mapTilesX + tileX`) and the `setZoneOverlay`
    dimension arguments are correct for this session's map. Must be called after terrain
    generation so the map dimensions are final. Same pattern as
    `IrrlichtRenderer::setTerrainQuery()`.
  <!-- graphics-dev-irrlicht verified: all 5 wiring calls present in main.cpp — (2) renderer.setTerrainQuery(&terrainSystem) at line 151, (2a) renderer.setCellSize(terrainSystem.getCellSize()) at line 152, (2b) renderer.setRendererMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ()) at line 153, (3) uiManager.setRenderer(&renderer) at line 178, (4) uiManager.setTerrainQuery(&terrainSystem) at line 179, (5) uiManager.setMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ()) at line 180; all called after terrainSystem.buildAllChunks() at line 143 and before the frame loop at line 191, 2026-03-03 -->

  This order guarantees all pointers and map dimensions are valid before the first game-loop
  frame calls `pickTerrainTile()` or triggers a zone overlay update. These calls replace the
  Phase 8 stub wiring where the UIManager had no renderer or terrain reference. (ref:
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)

- [x] In the main game loop (after `uiManager.update(dt)`), call
  `renderer.setZoneOverlay(...)` if the overlay dirty flag is set (set by any successful
  placement or demolish in the current frame). The dirty flag is managed by `UIManager`.
  <!-- graphics-dev-irrlicht verified: intent met — UIManager calls m_renderer->setZoneOverlay() directly on successful placeZone (UIManager.cpp line 810) and demolishTile (UIManager.cpp line 850), not via a deferred dirty flag in main.cpp. Direct immediate call is equivalent or better: zero one-frame latency, no main.cpp polling, 2026-03-03 -->

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

- [x] **Phase 9b implementation tasks arising from this resolution**:
  <!-- sound-dev-opensoftal verified: UIManager.h:208 `int m_selectedServiceBuilding{0}` (int ordinal matching ServiceBuildingType enum, default PowerPlant=0); mock_city_simulation.h:74-77 `MOCK_METHOD(void, placeServiceBuilding, (int tileX, int tileZ, ServiceBuildingType type, int earthworksCostOverride), (override))`; simulation_constants.h:59-66 service_placement_cost_power_plant=10000, service_placement_cost_water_tower=3000, service_placement_cost_fire_station=5000, service_placement_cost_police_station=4000 2026-03-03 -->
  - Add `ServiceBuildingType m_selectedServiceBuilding{ServiceBuildingType::PowerPlant}` to
    `UIManager` private members.
  - Add `placeServiceBuilding` to `MockCitySimulation` in `tests/ui/mock_city_simulation.h`.
  - [x] **DONE (sound-artist-opensoftal, 2026-03-02)**: Placement-cost constants added to
    `src/simulation/simulation_constants.h`:
    `service_placement_cost_power_plant = 10000`,
    `service_placement_cost_water_tower = 3000`,
    `service_placement_cost_fire_station = 5000`,
    `service_placement_cost_police_station = 4000`.
    (ref: `architecture/game-design/service-coverage.md` — Placement Costs table)
  - Add `placeServiceBuilding` stub to `CitySimulation` (Phase 9b implementation work —
    not part of this spec/interface clarification). **NOTE**: The stub already exists in
    `src/simulation/CitySimulation.cpp` (added as Phase 9b preparation). The full
    implementation must wire cost deduction, tile occupancy, undo entry, and audio callbacks
    as specified in `architecture/game-design/service-coverage.md` — "Audio Callbacks for
    `placeServiceBuilding()`" section.
  - Add unit test `WorldInteraction_UtilitiesPlacement_CallsPlaceServiceBuilding`:
    construct `UIManager` with `StrictMock<MockCitySimulation>`, `StrictMock<MockRenderer>`,
    `ManualTerrainQuery` (slope = 0°), `NiceMock<MockUIBackend>`, `ManualClock`. Set active tool to `ActiveTool::Utilities`, selected
    building to `ServiceBuildingType::FireStation`. Stub `MockRenderer::pickTerrainTile` to
    return true at (5, 7). Send `MouseButtonDown button=0`.
    `EXPECT_CALL(sim_, placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0)).Times(1);`

#### J.0. Audio Design Decisions — Resolved (sound-artist-opensoftal, 2026-03-02)

<!-- RESOLVED: sound-artist-opensoftal 2026-03-02 -->
<!-- All three audio blockers below are now documented in architecture/ spec files. -->
<!-- No further design decisions are required before Phase 9b implementation begins. -->

The following audio-domain design questions were open or under-specified in Phase 9b. All are
now resolved. Developers must implement exactly as specified below and in the referenced
architecture files.

**Audio Decision 1 — `placeServiceBuilding()` audio callbacks (RESOLVED)**

`CitySimulation::placeServiceBuilding()` stub at `src/simulation/CitySimulation.cpp` line 1499
currently fires no audio. The full Phase 9b implementation MUST add the following call sequence
on successful placement (tile was not already occupied and cost deduction succeeds):

```cpp
// In CitySimulation::placeServiceBuilding(), after tile occupancy guard and cost deduction:
if (earthworksCostOverride > 0) {
    if (m_audio) {
        m_audio->playPositionalSound(SFX_EARTHWORKS,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }
}
if (m_audio) {
    m_audio->playPositionalSound(SFX_BUILD_PLACE,
        vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
        SoundPriority::NORMAL, 1.0f);
}
```

- `SFX_BUILD_PLACE` (SoundId = 1) is the correct sound for service building placement.
  A dedicated service-building SFX is post-V1.
- `SFX_EARTHWORKS` (SoundId = 4) fires only when `earthworksCostOverride > 0`, identical to
  the pattern in `placeZone()` and `placeRoad()`.
- No audio fires on no-op (already-occupied tile) placements.

Full specification in `architecture/game-design/service-coverage.md` — "Audio Callbacks for
`placeServiceBuilding()`" section. **BLOCK CLEARED.**

**Audio Decision 2 — service placement cost constants (RESOLVED)**

`SimulationConstants::service_placement_cost_*` constants referenced in Deliverable I tasks and
in `architecture/game-design/service-coverage.md` were missing from
`src/simulation/simulation_constants.h`. They have now been added (sound-artist-opensoftal,
2026-03-02):

```cpp
static constexpr int service_placement_cost_power_plant    = 10000;
static constexpr int service_placement_cost_water_tower    = 3000;
static constexpr int service_placement_cost_fire_station   = 5000;
static constexpr int service_placement_cost_police_station = 4000;
```

`CitySimulation::placeServiceBuilding()` full implementation MUST deduct
`SimulationConstants::service_placement_cost_<type> + earthworksCostOverride` from `m_treasury`
using the same pattern as `placeRoad()` (`m_treasury -= totalCost`). Do NOT hardcode cost
literals inline. **BLOCK CLEARED.**

**Audio Decision 3 — Y-position for placement audio calls (RESOLVED — deferred to Phase 10)**

`placeZone()`, `placeRoad()`, and `placeServiceBuilding()` all pass `Y = 0.0f` as the
world-space Y coordinate in their `playPositionalSound()` calls. This is correct for Phase 9b.
The `ITerrainQuery::getHeightAt()` method added in Phase 9b Deliverable E is used exclusively
for **zone overlay and hover highlight rendering** (`IrrlichtRenderer::setZoneOverlay`,
`IrrlichtRenderer::setTileHoverHighlight`). It is **NOT** used to update audio Y positions in
Phase 9b.

Decision: Y = 0.0f for audio positioning remains through Phase 9b exit. Phase 10 may substitute
`m_terrain->getHeightAt(tileX, tileZ)` if real terrain height produces perceptibly incorrect
audio spatialization. Developers MUST NOT add `getHeightAt()` calls to audio positioning sites
in Phase 9b — doing so changes the `ITerrainQuery` usage contract and may cascade to test
fixtures. (ref: `src/interfaces/ITerrainQuery.h` comment — "Height queries for audio
positioning (playPositionalSound Y) use Y=0 in Phase 6 and will be refined in Phase 10 if
required.") **BLOCK CLEARED — no Phase 9b action required.**

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
  <!-- sound-dev-opensoftal verified: CitySimulation.cpp line 1441 calls playPositionalSound(SFX_ROAD_BUILD, ...) confirming SFX_BUILD_PLACE placeholder replaced; sound_ids.h line 26 confirms SFX_ROAD_BUILD=3, 2026-03-03 -->

- [x] **Unit test: `CitySimulation_PlaceRoad_FiresSFXRoadBuild`**
  <!-- sound-dev-opensoftal verified: test present in tests/simulation/audio_sim_test.cpp lines 106-119 with EXPECT_CALL(audioSystem_, playPositionalSound(SFX_ROAD_BUILD, _, _, _)).Times(1) and placeRoad(5, 7, 0) invocation; CMakeLists.txt target_sources at line 482, 2026-03-03 -->
  **IMPLEMENTED (sound-dev-opensoftal, 2026-03-02)**: `tests/simulation/audio_sim_test.cpp`
  created with `AudioSimTest` fixture (NiceMock\<MockRenderer\>, StrictMock\<MockAudioSystem\>,
  ManualRNG, ManualClock, ManualTerrainQuery, Difficulty::Easy). CMakeLists.txt extended via
  `target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp)`.
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

- [x] **Unit test: `CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace`**
  <!-- sound-dev-opensoftal verified: audio_sim_test.cpp:136 TEST_F(AudioSimTest, CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace) flat-tile variant (earthworksCostOverride=0, EXPECT_CALL SFX_BUILD_PLACE Times(1) only, no SFX_EARTHWORKS); audio_sim_test.cpp:167 TEST_F(AudioSimTest, CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace) steep-slope variant (InSequence: SFX_EARTHWORKS Times(1) then SFX_BUILD_PLACE Times(1)); both added to simulation_tests via CMakeLists.txt:488 `target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp)` 2026-03-03 -->
  (arising from Audio Decision 1 resolution — sound-artist-opensoftal, 2026-03-02)
  - **Location**: `tests/simulation/audio_sim_test.cpp` — same file as
    `CitySimulation_PlaceRoad_FiresSFXRoadBuild`; no additional `target_sources` call needed.
  - **Setup**: same fixture as `CitySimulation_PlaceRoad_FiresSFXRoadBuild`:
    `NiceMock<MockRenderer>`, `StrictMock<MockAudioSystem>`, `ManualRNG`, `ManualClock`,
    `ManualTerrainQuery` (slope = 0°), `Difficulty::Easy`. `ManualTerrainQuery` must implement
    `getHeightAt()` returning `0.0f` (added in Deliverable E — `ITerrainQuery` now has
    `getHeightAt` as pure virtual, so `ManualTerrainQuery` must provide the override to remain
    concrete):

    ```cpp
    NiceMock<MockRenderer>      renderer;
    StrictMock<MockAudioSystem> audioSystem;
    ManualRNG rng; ManualClock clock; ManualTerrainQuery terrain;
    CitySimulation citySimulation(&renderer, &audioSystem, &rng, &clock,
                                   &terrain, Difficulty::Easy);
    ```

  - **Base assertion** (flat tile, `earthworksCostOverride = 0`):

    ```cpp
    EXPECT_CALL(audioSystem, playPositionalSound(SFX_BUILD_PLACE, _, _, _)).Times(1);
    citySimulation.placeServiceBuilding(5, 7, ServiceBuildingType::PowerPlant, 0);
    ```

    `SFX_BUILD_PLACE` (SoundId = 1) fires exactly once. `SFX_EARTHWORKS` must NOT fire when
    `earthworksCostOverride = 0` — `StrictMock` enforces this automatically (unexpected calls
    are test failures).

  - **Earthworks variant**
    `CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace`:
    Set `terrain.setSlope(5, 7, 30.0f)`. Pass `earthworksCostOverride = 250` (pre-computed
    by UIManager: `factor = (30.0f - 15.0f) / 30.0f = 0.5f`,
    cost = `static_cast<int>(500.0f * 0.5f) = 250`). Assert both calls fire in order:

    ```cpp
    {
        InSequence seq;
        EXPECT_CALL(audioSystem,
            playPositionalSound(SFX_EARTHWORKS, _, _, _)).Times(1);
        EXPECT_CALL(audioSystem,
            playPositionalSound(SFX_BUILD_PLACE, _, _, _)).Times(1);
    }
    citySimulation.placeServiceBuilding(5, 7, ServiceBuildingType::PowerPlant, 250);
    ```

    Verifies `SFX_EARTHWORKS` fires before `SFX_BUILD_PLACE` — same order as `placeZone()`
    and `placeRoad()`. (ref: `architecture/game-design/service-coverage.md` — "Audio
    Callbacks for `placeServiceBuilding()`")

  - **Label**: unit test — no `requires-opengl` label.
  - Assigned to: `test-dev-cpp`.
  <!-- test-dev-cpp verified: all 3 AudioSimTest tests (CitySimulation_PlaceRoad_FiresSFXRoadBuild at line 106, CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace at line 136, CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace at line 167) confirmed in tests/simulation/audio_sim_test.cpp; fixture uses NiceMock<MockRenderer>, StrictMock<MockAudioSystem>, ManualRNG, ManualClock, ManualTerrainQuery, Difficulty::Easy with TearDown calling sim_.reset(); added to simulation_tests via target_sources at CMakeLists.txt lines 481-483, labelled unit with no requires-opengl, 2026-03-03 -->

---

### Exit Criteria

- Left-clicking on a terrain tile with Zone tool active calls `ICitySimulation::placeZone`
  with the correct `ZoneType`, `DensityTier`, and earthworks cost (0 on flat tiles;
  `500 × clamp((slope - 15) / 30, 0, 2)` on slopes > 15°)
  <!-- graphics-dev-irrlicht verified: Zone left-click handler at UIManager.cpp lines 788-814 calls m_sim->placeZone(hitX, hitZ, ZoneType, DensityTier, earthworksCost); earthworks formula matches spec (slope-15/30 clamped 0-2 times 500); WorldInteraction_ZonePlacement_CallsPlaceZone confirms dispatch, 2026-03-03 -->
- Left-clicking with Road tool calls `ICitySimulation::placeRoad` with correct earthworks cost
  <!-- graphics-dev-irrlicht verified: Road left-click handler at UIManager.cpp line 817 calls m_sim->placeRoad(hitX, hitZ, earthworksCost); WorldInteraction_RoadPlacement_CallsPlaceRoad confirms dispatch, 2026-03-03 -->
- Left-clicking with Demolish tool opens the Phase 8 confirmation modal (or calls
  `demolishTile` directly if the modal has been suppressed in Settings)
  <!-- graphics-dev-irrlicht verified: Demolish handler at UIManager.cpp lines 828-855; when m_demolishConfirmEnabled=true calls m_modal->showDemolishConfirm(1); when false calls m_sim->demolishTile() directly; WorldInteraction_DemolishTool_SteepSlope_NoEarthworksGuard confirms direct path, 2026-03-03 -->
- Left-clicking with Query tool opens the `InspectorPanel` populated with live `QueryResult`
  data from `ICitySimulation::queryTile`
  <!-- graphics-dev-irrlicht verified: Priority-3 Query open path at UIManager.cpp lines 325-354 calls m_renderer->pickTerrainTile(), then m_sim->queryTile(), then m_inspector->populate(result, hitX, hitZ, cursorX, cursorY, tileBounds); WorldInteraction_QueryTool_CallsQueryTile confirms dispatch, 2026-03-03 -->
- Hovering over a terrain tile with any non-None tool active renders a semi-transparent
  colour highlight on that tile within the same frame
  <!-- graphics-dev-irrlicht verified: MouseMove handler at UIManager.cpp lines 722-745 calls pickTerrainTile() then setTileHoverHighlight() with tool-specific ARGB from ui_constants.h; drawMeshBuffer called in IrrlichtRenderer::drawScene() each frame; WorldInteraction_HoverHighlight_SetOnMouseMove confirms behaviour, 2026-03-03 -->
- Zoned tiles display a semi-transparent colour overlay (R=green, C=blue, I=yellow) at all
  times during gameplay
  <!-- graphics-dev-irrlicht verified: setZoneOverlay() called immediately after placeZone (UIManager.cpp line 810) passing m_overlayMap with kOverlayArgbResidential/Commercial/Industrial constants; IrrlichtRenderer::setZoneOverlay() rebuilds persistent m_overlayNode scene node with EMT_TRANSPARENT_ALPHA_CHANNEL; WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry confirms correct colour constant, 2026-03-03 -->
- Slope guard: when earthworks cost exceeds treasury balance, a Normal toast is shown and
  `placeZone` / `placeRoad` is NOT called
  <!-- graphics-dev-irrlicht verified: earthworks guard at UIManager.cpp lines 769-783 checks earthworksCost against m_sim->getTreasuryBalance() and posts toast via NotificationManager when insufficient; WorldInteraction_ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace confirms placeZone is not called, 2026-03-03 -->
- All 17 unit tests in `WorldInteractionTest` pass under `ctest -LE "integration|requires-opengl"`
  (9 named base tests: ZonePlacement, RoadPlacement, DemolishTool_SteepSlope,
  ZoneTool_SteepSlope_InsufficientFunds, QueryTool, NoActiveTool, ModalActive,
  HoverHighlight_SetOnMouseMove, HoverHighlight_ClearedOnMiss;
  plus `WorldInteraction_UtilitiesPlacement_CallsPlaceServiceBuilding` (Deliverable I);
  plus 5 sparse-overlay tests: SparseOverlay_InsertsEntry, Demolish_SparseOverlay_ErasesEntry,
  NewGameLoad_ClearsOverlay, OverlayCap_100K_StillCalls, SetMapDimensions_Recall_ClearsOverlay;
  plus 2 button-init tests: ZoneSubPanel_ButtonsInitialized, UtilitiesSubPanel_ButtonsInitialized)
  <!-- test-dev-cpp verified: all 17 TEST_F cases confirmed present in tests/ui/world_interaction_test.cpp at lines 283 312 334 368 406 434 455 489 510 532 567 615 663 717 757 806 842 wired to ui_tests via CMakeLists.txt lines 597-599 with LABEL unit TIMEOUT 300 no requires-opengl, 2026-03-03 -->
- `CitySimulation_PlaceRoad_FiresSFXRoadBuild` simulation unit test passes under
  `ctest -LE "integration|requires-opengl"` (Deliverable J — located in
  `tests/simulation/audio_sim_test.cpp`, added to `simulation_tests` target)
  <!-- sound-dev-opensoftal verified: test exists in tests/simulation/audio_sim_test.cpp lines 106-119; CMakeLists.txt target_sources at line 482; placeRoad() fires SFX_ROAD_BUILD at CitySimulation.cpp line 1441, 2026-03-03 -->
  <!-- test-dev-cpp verified: CitySimulation_PlaceRoad_FiresSFXRoadBuild confirmed at tests/simulation/audio_sim_test.cpp line 106; AudioSimTest fixture uses StrictMock<MockAudioSystem> and EXPECT_CALL(audioSystem_, playPositionalSound(SFX_ROAD_BUILD, _, _, _)).Times(1); wired to simulation_tests via target_sources at CMakeLists.txt lines 481-483 with LABEL unit, 2026-03-03 -->
- `CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace` and
  `CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace` simulation unit
  tests pass under `ctest -LE "integration|requires-opengl"` (Audio Decision 1 — in
  `tests/simulation/audio_sim_test.cpp`, same target as Deliverable J tests)
  <!-- sound-dev-opensoftal verified: both tests present in tests/simulation/audio_sim_test.cpp lines 136-188; placeServiceBuilding() fires SFX_BUILD_PLACE at CitySimulation.cpp line 1568 and SFX_EARTHWORKS at line 1562; InSequence enforced in SteepSlope variant, 2026-03-03 -->
  <!-- test-dev-cpp verified: FiresSFXBuildPlace at audio_sim_test.cpp line 136 uses StrictMock EXPECT_CALL(audioSystem_ playPositionalSound(SFX_BUILD_PLACE _ _ _)).Times(1); SteepSlope variant at line 167 uses InSequence for SFX_EARTHWORKS then SFX_BUILD_PLACE on placeServiceBuilding(5 7 PowerPlant 250); both in simulation_tests via CMakeLists.txt lines 481-483 LABEL unit, 2026-03-03 -->
- `UIManager::getActiveTool()` returns the correct `ActiveTool` after each toolbar button click
  and hotkey press (verified by `WorldInteraction_NoActiveTool_LeftClickIgnored` and related tests)
  <!-- graphics-dev-irrlicht verified: getActiveTool() returns m_activeTool (UIManager.cpp line 1266); m_activeTool is updated by toolbar button y-range hits and Z/R/U/D/I hotkeys in UIManager.cpp lines 481-535; WorldInteraction_NoActiveTool_LeftClickIgnored and all fixture tests exercise getActiveTool() via tool-set and click sequences, 2026-03-03 -->
- Zone overlay is visually correct: newly placed tiles appear in the correct zone colour within
  one frame of placement; demolished tiles clear within one frame
  <!-- graphics-dev-irrlicht verified: setZoneOverlay() called synchronously in the same onEvent() call as placeZone/demolishTile (UIManager.cpp lines 810/850); IrrlichtRenderer::setZoneOverlay() rebuilds m_overlayNode immediately and it renders via sceneManager->drawAll() in the very next frame; WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry and WorldInteraction_Demolish_SparseOverlay_ErasesEntry confirm correct colour and erasure, 2026-03-03 -->
- **Spec gap I resolved**: Utilities tool places service buildings via
  `ICitySimulation::placeServiceBuilding(tileX, tileZ, ServiceBuildingType, earthworksCost)`.
  `ServiceBuildingType` enum is in `simulation_types.h`. Full placement design (sub-panel layout,
  costs, placement rules) is in `architecture/game-design/service-coverage.md`. BLOCK CLEARED.
  <!-- gamedesign-ux verified: ICitySimulation.h (lines 159-161) declares placeServiceBuilding(int tileX, int tileZ, ServiceBuildingType type, int earthworksCostOverride) as pure virtual; CitySimulation.cpp (lines 1499-1507) implements it with one-building-per-tile guard and ServiceBuildingType dispatch; UIManager.cpp (line 822) calls m_sim->placeServiceBuilding() from the Utilities tool left-click handler, 2026-03-03 -->
- `CitySimulation::placeRoad()` calls
  `m_audio->playPositionalSound(SFX_ROAD_BUILD, ...)` (Deliverable J — `SFX_BUILD_PLACE`
  placeholder replaced; Phase 10 `sfx_road_build` precondition satisfied)
  <!-- sound-dev-opensoftal verified: CitySimulation.cpp line 1441 calls playPositionalSound(SFX_ROAD_BUILD, ...) with SoundId=3; sound_ids.h confirms SFX_ROAD_BUILD=3, 2026-03-03 -->
- `CitySimulation::placeServiceBuilding()` calls `m_audio->playPositionalSound(SFX_BUILD_PLACE, ...)`
  on successful placement and `SFX_EARTHWORKS` when `earthworksCostOverride > 0` (Audio Decision 1
  — spec in `architecture/game-design/service-coverage.md` "Audio Callbacks" section)
  <!-- sound-dev-opensoftal verified: CitySimulation.cpp lines 1559-1571 fire SFX_EARTHWORKS (line 1562) when earthworksCostOverride>0 then SFX_BUILD_PLACE (line 1568) unconditionally; no-op guard at lines 1502-1507 prevents audio on already-occupied tiles, 2026-03-03 -->
- **All audio design blockers cleared** (sound-artist-opensoftal, 2026-03-02):
  `placeServiceBuilding()` audio spec in `architecture/game-design/service-coverage.md`;
  `service_placement_cost_*` constants in `src/simulation/simulation_constants.h`;
  Y=0.0f positioning deferred to Phase 10. No open audio design questions remain for Phase 9b.
  <!-- sound-dev-opensoftal verified: all four service_placement_cost_* constants confirmed in simulation_constants.h lines 59-66; J.0 Audio Decision 1/2/3 all marked RESOLVED; no open audio blockers found, 2026-03-03 -->
- `all-checks-pass` CI gate remains green after Phase 9b changes land
  <!-- cicd-dev-github verified: all-checks-pass gate exists at ci.yml lines 945-977 with if:always() and depends on build-linux/build-windows/coverage-linux/markdown-lint/validate-assets; both build-linux (step 14) and coverage-linux (step 13) run ctest -LE 'integration|requires-opengl' which picks up ui_tests (LABEL unit, CMakeLists.txt line 574) and simulation_tests (LABEL unit, CMakeLists.txt line 475); world_interaction_test.cpp is wired via target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp) at CMakeLists.txt lines 597-599; audio_sim_test.cpp is wired via target_sources(simulation_tests PRIVATE tests/simulation/audio_sim_test.cpp) at CMakeLists.txt lines 481-483; neither target carries a requires-opengl label so all Phase 9b tests are included in unit-test ctest runs, 2026-03-03 -->

#### Bug Fixes Required in Phase 9b (player-visible regressions)

The following issues are player-visible in Phase 9b builds. The first is an **implementation bug**
that must be resolved before Phase 9b is marked Done. Issues 2–5 are **Phase 10 missing features**
— not regressions — documented here for Phase 10 pickup. Infrastructure fixes applied in Phase 9b
where possible are noted inline.

- [ ] **Minimap panel invisible — transparent background** (`IrrlichtUIBackend::addStaticText`
  called with `fillBackground=false`). The `show()` / `hide()` lifecycle is now correct:
  `UIManager::transitionToGameplay()` calls `m_minimap->show()` and
  `UIManager::transitionToMainMenu()` calls `m_minimap->hide()`. The remaining invisibility is
  that `m_mapBg` (the 200×200 static-text element) has no filled background — the player sees
  straight through it to the terrain even when `show()` has been called.

  **Root cause**: `IrrlichtUIBackend::addStaticText` creates its `IGUIStaticText` with
  `fillBackground=false`. No subsequent call sets a background color. The element is logically
  visible but renders zero pixels.

  **Fix — three steps, all required**:

  1. Add `virtual void setElementBackgroundColor(UIElementHandle handle, uint32_t argb) = 0;`
     to `IUIBackend.h`. This keeps `Minimap.cpp` free of any Irrlicht dependency.
  2. Implement in `IrrlichtUIBackend.cpp`: look up the element in `m_elementMap`, cast to
     `IGUIStaticText*`, call `->setBackgroundColor(irr::video::SColor(argb))` and
     `->setDrawBackground(true)`.
  3. Add a no-op stub to `MockUIBackend` (records the call; no assertion required for Phase 9b).
  4. In `Minimap::Minimap()`, after `m_mapBg = m_backend->addStaticText(...)`, add:

     ```cpp
     m_backend->setElementBackgroundColor(m_mapBg, 0xFF111111u); // near-black panel
     ```

  After this fix a 200×200 px dark rectangle appears at bottom-right (virtual x:1720–1920,
  y:880–1080) whenever gameplay is active. Zone color coding, road lines, and the camera
  viewport rectangle are Phase 11 deliverables.

  Authoritative spec: `architecture/ui-ux/minimap.md` (Phase 9b Minimum Viable Minimap section).
  Assigned to: `graphics-dev-irrlicht`.

- [x] **Road mesh not visible after placing road tile (Phase 10 missing feature)** —
  After `placeRoad()` is dispatched via left-click in Road tool mode, no 3D road mesh appears on
  the terrain. Root cause: `CitySimulation::placeRoad()` updates the `m_tiles` map and fires
  `SFX_ROAD_BUILD` audio but never calls any `IRenderer` method to place a road scene node.
  `IRenderer` has no `placeRoadMesh()` or equivalent method; `SceneEntityManager` and the road
  mesh pipeline do not exist yet. **This is a Phase 10 deliverable** — Phase 9b only wires the
  click-to-simulation dispatch; Phase 10 delivers road mesh rendering. The simulation tile state
  is correctly updated; only the visual representation is missing. No Phase 9b code change
  required. Spec ownership: `architecture/graphics-architecture/` (road scene node lifecycle),
  `architecture/asset-standards/3d-model-standards.md` (road segment `.b3d` asset spec).
  Assigned to Phase 10: `graphics-dev-irrlicht`.
  <!-- graphics-dev-irrlicht: CLOSED — Phase 10 implements IRenderer::placeRoadMesh() / removeRoadMesh() in IrrlichtRenderer.cpp; CitySimulation::placeRoad() calls m_renderer->placeRoadMesh() on success; verified by CitySimulation_PlaceRoad_SpawnsRoadMesh unit test, 2026-03-04 -->

- [ ] **No terrain flattening when zone/road/service building placed (Phase 10 missing feature)**
  — When a tile with steep slope has zone, road, or service building placed on it, the earthworks
  cost is correctly deducted from the treasury (via `CitySimulation::computeEarthworksCost()`)
  but the terrain mesh is never modified to flatten the tile visually. Root cause: the
  `ITerrainQuery` interface has `getSlopeDegrees()` and `getHeightAt()` for reads but no
  `setTileHeight()` or terrain modification API. `TerrainSystem` similarly has no modification
  API. The spec (`architecture/game-design/terrain-interaction.md`) specifies earthworks as a
  treasury deduction mechanic only — it does NOT require visual terrain mesh modification for V1.
  **This is not a Phase 9b regression.** No visual flattening is specced. The terrain stays
  bumpy; earthworks = cost only. If visual flattening is desired in a future phase, it requires:
  (1) `ITerrainQuery::setTileHeight()` interface method, (2) `TerrainSystem` height-map write
  path, (3) `rebuildTerrainChunk()` triggered on affected chunk. Deferred to post-V1.

- [x] **No building models after placing zone tiles (Phase 10 missing feature)** — After
  `placeZone()` is dispatched via left-click in Zone tool mode, the zone colour overlay appears
  correctly (2D overlay mesh via `IRenderer::setZoneOverlay()`) but no 3D building model spawns
  on the tile. Root cause: `CitySimulation::placeZone()` updates the `m_tiles` map and fires
  `SFX_BUILD_PLACE` audio but never calls any `IRenderer` method to place a building scene node.
  `IRenderer` has no `placeBuildingMesh()` or equivalent method; `BuildingAssetLoader` and
  `SceneEntityManager` are not connected to the simulation placement callback chain. Building
  spawning requires simulation growth logic (population demand → building type selection →
  `SceneEntityManager::spawnBuilding()`). **This is a Phase 10 deliverable** — building mesh
  spawning on zone tiles is part of the city growth simulation rendering pass, not the tile
  placement action itself. The zone overlay and tile state are correctly set; only building
  geometry is missing. No Phase 9b code change required. Spec ownership:
  `architecture/graphics-architecture/scene-graph-ownership.md` (building node lifecycle),
  `architecture/asset-standards/3d-model-standards.md` (`.b3d` building asset spec).
  Assigned to Phase 10: `graphics-dev-irrlicht`.
  <!-- graphics-dev-irrlicht: CLOSED — Phase 10 implements IRenderer::placeBuildingMesh() / removeBuildingMesh() / placeServiceBuildingMesh() / removeServiceBuildingMesh() in IrrlichtRenderer.cpp; CitySimulation::placeZone() calls m_renderer->placeBuildingMesh() on success, placeServiceBuilding() calls placeServiceBuildingMesh(); service building visual gap also closed; verified by CitySimulation_PlaceZone_SpawnsBuilding, CitySimulation_PlaceServiceBuilding_SpawnsMesh, CitySimulation_DemolishZoneTile_RemovesBuilding unit tests, 2026-03-04 -->

- [x] **Font size unreadably small** — Irrlicht's built-in default GUI font renders at
  approximately 8 physical pixels. All HUD labels (treasury balance, population count, toolbar
  button text, demand bar labels, active tool indicator) are illegible at any supported
  resolution. Root cause: `IrrlichtUIBackend` constructor never loaded a custom font or called
  `m_guiEnv->getSkin()->setFont(...)`. **Partial fix applied (Phase 9b)**: `IrrlichtUIBackend`
  constructor now attempts to load `assets/fonts/ui_font.xml` via
  `m_guiEnv->getFont("assets/fonts/ui_font.xml")` and applies it globally via
  `m_guiEnv->getSkin()->setFont()`. Gracefully falls back to 8px built-in with a `stderr`
  warning if the file is absent. `assets/fonts/FONT_REQUIRED.txt` documents how to author the
  font using Irrlicht's FontTool (18 px recommended). **Phase 10 completes this fix** by
  delivering the actual `assets/fonts/ui_font.xml` bitmap font asset (18 px, ASCII 32–126) per
  `architecture/ui-ux/resolution-ui-scaling.md` (minimum 14 px virtual body font).
  <!-- graphics-dev-irrlicht: font-loading infrastructure added to IrrlichtUIBackend constructor
  (lines 213-253); assets/fonts/FONT_REQUIRED.txt created with authoring guide, 2026-03-03 -->

- [x] **Upper-left letter artefact overlapping Zone button** — Single letter characters ("Z",
  "R", "U", "D", "Q") appeared at toolbar button positions in the upper-left of the screen.
  These are `IGUIButton` label text strings ("Zone", "Road", "Utilities", "Demolish", "Query")
  rendered by Irrlicht's 8px built-in font, truncated to 1 character at the physical scale of
  1280×720. Two contributing root causes: (1) `assets/textures/ui/hud_sprites_ui.png` does not
  yet exist so `m_spriteTextureReady` is `false` and `setElementImage()` is a no-op, leaving
  text labels visible; (2) the 8px font makes those labels appear as confusing single-letter
  noise. The font-loading infrastructure fix above (partial fix) means that once
  `assets/fonts/ui_font.xml` is present, button labels will render at a readable size and be
  clearly identifiable. **Phase 10 completes this fix** by delivering `assets/fonts/ui_font.xml`
  (making text readable) and `assets/textures/ui/hud_sprites_ui.png` (replacing text with icons
  entirely). Spec reference: `architecture/ui-ux/hud-layout.md` (Toolbar Button Text Fallback).

<!-- BINDING DECISION — prod-owner 2026-03-13: Two sprite offset bugs were found and fixed
in IrrlichtUIBackend::spriteRectForIndex() after Phase 9b delivery. Both were caused by
incorrect special cases that deviated from the uniform 64×64 grid formula. The architecture
spec (architecture/asset-standards/2d-texture-standards.md — "Sprite ID Encoding and
Row-Conflict Pitfall") was updated to document both bugs as anti-patterns.

1. **Bell/clock/dot/undo icons (IDs 320–323) at wrong y position**: A special case in
   `spriteRectForIndex()` placed `icon_bell` at `(56, 64)` (inside the toolbar-inactive row)
   instead of row 10 (y=640). The notification bell rendered the road-inactive icon shifted
   8px left. Fix: removed the special case; IDs 320–323 now use the standard grid formula
   (`col = id % 32, row = id / 32`), placing them correctly in row 10 at y=640.

2. **Utilities sub-panel icons (IDs 128–163) with 72px column spacing**: A special case
   used `xOffsets = {0, 72, 144, 216}` (72px steps) instead of 64px. Fire appeared 16px
   off, Police 24px off — both had their left edges clipped. Fix: removed the special case;
   all icons now use the uniform 64×64 grid, placing them at x = 0, 64, 128, 192.

Both fixes were verified by visual inspection; `tools/generate_hud_sprites.py` already
generated sprites at 64px columns — the bugs were in the `spriteRectForIndex()` decoder,
not the generator. Committed in fix commit 6e2da46.
-->

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | Deliverable A (ActiveTool enum in `src/ui/ui_types.h`, `UIManager::m_activeTool` member + `getActiveTool()` getter, Priority-5 toolbar/hotkey dispatch extension); Deliverable D (UIManager world-interaction layer: MouseMove hover-highlight handler, left-click dispatch for all tool modes, overlay dirty-flag management, sub-panel visibility toggling via setElementVisible); `IRenderer::pickTerrainTile` + `setTileHoverHighlight` + `setZoneOverlay` implementation; `IrrlichtRenderer` terrain-system pointer wiring; `ITerrainQuery::getHeightAt` promotion; **Deliverable E.1** (`TerrainSystem::getMapTilesX()` / `getMapTilesZ()` public accessors in `src/terrain/TerrainSystem.h` and `TerrainSystem.cpp`); `main.cpp` wiring (`setRenderer`, `setTerrainQuery`, `setMapDimensions` via E.1 getters); `InspectorPanel::populate()` real implementation; `InspectorPanel` data refresh cadence wiring |
| `gamedesign-ux` | Zone sub-panel layout (3×3 button grid, virtual x:80 px); Utilities sub-panel layout (2×2 button grid, virtual x:80–276 y:176–276, 96×48 px buttons with placement cost labels); zone colour scheme (R/C/I); hover colour scheme per tool mode; confirm Utilities spec gap resolution with `gamedesign-lookandfeel` |
| `gamedesign-lookandfeel` | **COMPLETE**: Utilities placement spec gap resolved (Deliverable I, 2026-03-01); earthworks cost gate confirmed consistent with economy balance; zone/road/service placement costs confirmed for V1 difficulty tiers |
| `test-dev-cpp` | All 17 `WorldInteractionTest` unit tests in `tests/ui/world_interaction_test.cpp`; `MockRenderer` extension stubs (including sparse-map `setZoneOverlay`); `ManualTerrainQuery` extension for `getHeightAt`; **Deliverable J test**: `CitySimulation_PlaceRoad_FiresSFXRoadBuild` in `tests/simulation/audio_sim_test.cpp`; **Audio Decision 1 tests**: `CitySimulation_PlaceServiceBuilding_FiresSFXBuildPlace` and `CitySimulation_PlaceServiceBuilding_SteepSlope_FiresEarthworksThenBuildPlace` in same file (all added to `simulation_tests` target via `target_sources`) |
| `sound-dev-opensoftal` | **Deliverable J**: replace `SFX_BUILD_PLACE` with `SFX_ROAD_BUILD` in `CitySimulation::placeRoad()` (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`). **COMPLETE: audio design blockers resolved (2026-03-02)**: `placeServiceBuilding()` audio spec added to `architecture/game-design/service-coverage.md`; `service_placement_cost_*` constants added to `src/simulation/simulation_constants.h`; Y=0.0f audio positioning deferral confirmed. |

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

- **BLOCKING SPIKE — RESOLVED (2026-03-02)**:

  **Scope clarification**: `aitown_benchmark` is a GPU anisotropy/VRAM measurement tool and
  does not contain infrastructure for CPU-side ray-cast timing. The spike was therefore
  conducted analytically using first-principles CPU cost modelling against the minimum-spec
  hardware target (Intel UHD 630, confirmed minimum-spec GPU in
  `architecture/graphics-architecture/benchmark-tool.md`).

  **Spike methodology**: Model worst-case per-call cost of the naive 4096-step linear march
  on a 1024×1024 terrain at 60 FPS.

  - Heightmap size: 1024 × 1024 × 4 bytes = 4 MB. Exceeds L2 cache on all target CPUs
    (typical L2: 256 KB – 4 MB; typical L3: 4 MB – 8 MB on minimum-spec integrated).
  - Naive march: 4096 steps × one `getHeightAt()` array lookup per step.
  - Sequential-ish access (diagonal ray, stride ~1–2 tiles): ~64-byte cache lines cover
    16 floats; most steps are L3 hits at ~30–50 ns.
  - Worst-case single call: 4096 steps × 50 ns = ~205 µs.
  - **But `MouseMove` fires 4–10 times per frame at 60 FPS on a high-DPI mouse.** Sustained
    hover cost = 10 × 205 µs = **~2.05 ms per frame** — exceeds the 1 ms sustained budget.
  - At 60 FPS, the entire world-interaction budget (ray-cast + highlight update) is ≤1 ms.

  **Spike result**: Sustained cost of the naive 4096-step linear march exceeds 1 ms under
  realistic `MouseMove` frequency. **The O(1) DDA grid-intersection algorithm is mandated.**

  **Resolution**: Replace the naive linear march with the O(1) DDA grid-intersection
  algorithm specified in Deliverable B and in
  `architecture/graphics-architecture/procedural-terrain.md` — "pickTerrainTile DDA
  Algorithm" section. The DDA algorithm traverses at most `mapTilesX + mapTilesZ` cells
  (worst case 2048 for a 1024×1024 map), yielding ≤30 µs worst-case even at 10 events/frame
  = 300 µs sustained — within budget.

  **BLOCK CLEARED.** Deliverable B may proceed using the DDA algorithm as specified.

- **RISK (CLOSED)**: `IrrlichtRenderer::pickTerrainTile` ray-march too slow for real-time
  hover. Resolved by mandating the O(1) DDA algorithm. See BLOCKING SPIKE resolution above.

- **RISK**: Zone overlay `SMesh*` rebuild on every placement call may cause a visible 1-frame
  hitch on large maps (1024×1024 = 1M tiles, most zero). **Mitigation**: store only non-zero
  tiles in a `std::unordered_map<uint64_t, uint32_t>` sparse map; `setZoneOverlay` only
  generates quads for non-zero entries. Cap at 100K simultaneous overlay quads for V1.
  <!-- graphics-dev-irrlicht verified: UIManager.h line 221 declares std::unordered_map<uint64_t, uint32_t> m_overlayMap; UIManager.cpp line 803 enforces kOverlayCap=100000u; IrrlichtRenderer.cpp line 556 enforces kMaxOverlayQuads=100000u; sparse approach confirmed, 2026-03-03 -->

- **RISK**: `UIManager` currently has a locked 4-parameter constructor; adding `m_renderer` and
  `m_terrain` via setter methods rather than constructor parameters avoids breaking all existing
  UIManager unit tests. **Verify**: confirm that `NiceMock<MockRenderer>` can be passed via
  `setRenderer()` in test fixtures without introducing `UIManager` constructor changes that
  cascade to Phase 8 test fixtures. If the Phase 8 tests use bare `UIManager` construction,
  the setters are safe — they are no-ops if not called (nullptr guard in dispatch).
  <!-- test-dev-cpp verified: WorldInteractionTest::SetUp() at tests/ui/world_interaction_test.cpp lines 180-201 uses the same 4-param constructor (&backend_, nullptr, &sim_, &clock_) as Phase 8 tests then calls setRenderer/setTerrainQuery/setMapDimensions as post-construction setters; the Phase 8 test files (ui_manager_modal_test.cpp, modal_dialog_test.cpp etc.) use the 4-param constructor without setRenderer or setTerrainQuery — confirming setters are no-ops when not called and Phase 8 tests are unaffected, 2026-03-03 -->
  <!-- graphics-dev-irrlicht verified: 4-param constructor at UIManager.h line 59 unchanged; m_renderer/m_terrain default to nullptr; null-check guard at UIManager.cpp line 722 prevents any dereference if setters are not called, 2026-03-03 -->

- **RESOLVED**: `ZoneType::Utility` spec gap (Deliverable I) was resolved by
  `gamedesign-lookandfeel` on 2026-03-01. Utilities placement uses `placeServiceBuilding()`.
  No interim stub required. Implementation may proceed immediately.
