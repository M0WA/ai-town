## Phase 10b: Terrain Flattening & Sky Clouds

### Goal

Deliver two visual enhancements that make placement feedback and skybox believable: terrain
flattening writes the placed tile's height (and blended neighbour heights) back to the
persistent LOD0 heightmap and rebuilds affected chunks; a scrolling cloud plane adds a
lightweight animated cloud layer above the sky dome.

### Deliverables

#### Feature 1: Terrain Flattening on Placement

##### graphics-dev-irrlicht

- [ ] Add `setTileHeight(int tileX, int tileZ, float height)` as a pure-virtual method to
  `src/interfaces/ITerrainQuery.h`. Method sets the persistent LOD0 heightmap value at
  `(tileX, tileZ)` to `height`, enqueues `ChunkRebuildRequest`s for all affected chunks,
  then triggers neighbour blending (see below). Returns immediately; chunk rebuilds are
  processed by `TerrainSystem::update()` at the 2-per-frame cap, or synchronously during
  `flushPendingRebuilds()`. (ref: `architecture/game-design/terrain-interaction.md`,
  `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] Implement `TerrainSystem::setTileHeight(int tileX, int tileZ, float height)`:
  - Write `height` into the persistent LOD0 heightmap array at `(tileX, tileZ)`.
  - Apply neighbour blending: for each of the 8 surrounding tiles, lerp the neighbour's
    current height toward `height` using a falloff factor. Cardinal neighbours (N/S/E/W)
    use the factor confirmed by `gamedesign-lookandfeel` sign-off deliverable; diagonal
    neighbours (NE/NW/SE/SW) use the diagonal factor confirmed by the same sign-off (see
    Risks & Spikes — blending values are design-owner decisions, not engineering defaults).
    All neighbour coordinates are clamped to `[0, mapTilesX-1]` × `[0, mapTilesZ-1]`
    before any heightmap write. Out-of-bounds neighbours are silently skipped.
  - After writing the centre tile and all in-bounds neighbours, enqueue
    `ChunkRebuildRequest` for every chunk that contains at least one modified tile. Chunk
    deduplication is already handled by `TerrainSystem::update()`'s
    `processedThisFrame` set.
  - (ref: `architecture/graphics-architecture/procedural-terrain.md` — Deque
    deduplication section)
- [ ] Add `float getHeightAt(int tileX, int tileZ) const override { return 0.0f; }` and
  `void setTileHeight(int tileX, int tileZ, float height) override {}` no-op overrides to
  `ManualTerrainQuery` in `tests/simulation/manual_terrain_query.h` so that
  `ManualTerrainQuery` remains a concrete (non-abstract) class. (ref:
  `architecture/graphics-architecture/procedural-terrain.md` — `ITerrainQuery` interface
  promotion section)
- [ ] Update `IrrlichtRenderer::placeBuildingMesh()`, `placeRoadMesh()`, and
  `placeServiceBuildingMesh()` to call `m_terrain->setTileHeight(tileX, tileZ, flatY)`
  before placing the scene node, where `flatY` is derived from
  `m_terrain->getHeightAt(tileX, tileZ)` immediately prior to flattening (the tile's
  pre-placement height). After `setTileHeight()` returns, call
  `m_terrain->getHeightAt(tileX, tileZ)` again to obtain the now-flattened height and use
  that value as the scene node Y coordinate. This guarantees the placed node sits on the
  freshly flattened surface. (ref: `architecture/graphics-architecture/procedural-terrain.md`
  — MANDATORY building/road/service-building placement pattern)
- [ ] Confirm that `sfx_earthworks` continues to play on placement via the existing
  `CitySimulation` callback wired in Phase 10 — no new audio wiring required in this phase.
  (ref: `architecture/game-design/terrain-interaction.md` — Earthworks is treasury-only
  in V1, now extended with visual modification)

##### gamedesign-lookandfeel

- [x] **Sign-off: neighbour blending falloff factors.** Approved values (reviewed and
  signed off 2026-03-13): **cardinal neighbours (N/S/E/W) = 0.5**, **diagonal neighbours
  (NE/NW/SE/SW) = 0.25**. Blending formula:
  `new_height = lerp(neighbour_current_height, placed_tile_height, factor)`.
  Rationale: cardinal 0.5 produces a noticeable but not extreme slope, giving responsive
  SimCity-style feedback; diagonal 0.25 (half of cardinal) maintains a smooth spatial
  gradient — diagonal distance is √2 × cardinal distance, so a weaker influence is
  geometrically correct. Edge-tile placements produce asymmetric blending (out-of-bounds
  neighbours are skipped); the resulting edge-side cliff is expected V1 behaviour.
  **Design intent**: locking the placed tile to its own pre-flattened height gives
  immediate visual confirmation that the player's action has taken effect, while neighbour
  blending prevents hard seams. This is preferable to averaging all 9 tiles, which would
  shift the placed tile height unpredictably on sloped terrain. (ref:
  `architecture/game-design/terrain-interaction.md`)

##### test-dev-cpp

- [ ] `TerrainFlattening_SetTileHeight_EnqueuesRebuildForAffectedChunks`: construct a
  `TerrainSystem` with a `ManualClock`; call `setTileHeight()` on a tile at a known chunk
  boundary; assert that `m_rebuildDeque` contains at least the expected chunk IDs (using a
  friend-accessor or subclass seam). (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] `TerrainFlattening_NeighborBlend_ClampedToMapBounds`: call `setTileHeight()` on a
  corner tile (e.g. `(0, 0)`); assert no out-of-bounds heightmap write occurs (no crash,
  ASAN clean) and that all four in-bounds cardinal neighbours were written. (ref:
  `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight`: extend
  `MockRenderer` (in `tests/rendering/mock_renderer.h`) to record the Y position passed
  to `placeBuilding`/`placeRoad`/`placeServiceBuilding` calls. Inject a
  `ManualTerrainQuery` enhanced to be stateful: `setTileHeight()` marks it as flattened
  and subsequent `getHeightAt()` returns the post-flattened height. Verify `MockRenderer`
  received the post-flattened Y, not the pre-flattening height. This test runs without a
  real OpenGL context (`unit` label). Do NOT use a real `IrrlichtRenderer` — that
  requires `EDT_NULL` and the `requires-opengl` label. (ref:
  `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] Enhance `ManualTerrainQuery` in `tests/simulation/manual_terrain_query.h` to be
  stateful for Phase 10b tests: add `m_flattened` bool (default `false`),
  `m_heightBeforeFlat` (default `0.0f`), `m_heightAfterFlat` (default `0.0f`), and
  setter helpers `setHeightBeforeFlattening(float)` / `setHeightAfterFlattening(float)`.
  Override `getHeightAt()` to return `m_flattened ? m_heightAfterFlat : m_heightBeforeFlat`.
  Override `setTileHeight()` to set `m_flattened = true`. This replaces the Phase 9b
  no-op pattern for Phase 10b tests only; existing tests that rely on `return 0.0f` are
  unaffected because `m_heightBeforeFlat` defaults to `0.0f`.
- [ ] `CloudPlane_EDTNull_InitSkipped`: construct an `IrrlichtRenderer` with
  `EDT_NULL`, call `init()`; assert `m_cloudNode == nullptr` (cloud initialisation
  was skipped under headless driver). Label `requires-opengl` (uses `IrrlichtRenderer`).
  This is the minimal compile-and-no-crash gate for cloud rendering code.
- [ ] Wire all new test cases into the appropriate test targets via `target_sources` in
  `CMakeLists.txt`: terrain tests → `terrain_tests` (label `unit`); cloud test →
  `opengl_tests` (label `requires-opengl`).

---

#### Feature 2: Sky Clouds

##### graphics-artist-2d-texture

- [ ] Author `assets/textures/sky/clouds.png`: seamless tileable cloud pattern,
  1024×1024 RGBA (R=G=B=grey-white luminance; A=cloud density mask 0–255), authored for
  UV tiling (no hard edges at boundaries). Deliver as PNG (not DDS) — see rationale in
  Risks & Spikes. (ref: `architecture/asset-standards/2d-texture-standards.md` — Runtime
  formats, PNG for linear-pool textures)

##### graphics-dev-irrlicht

- [ ] Implement `IrrlichtRenderer::initCloudPlane()` called once from
  `IrrlichtRenderer::init()` after sky dome creation:
  - Build a flat `SMesh*` plane mesh (single quad, 2 triangles, CW winding for Irrlicht
    left-handed +Y normal) spanning world coordinates `(−cloudHalfExtent, kCloudAltitude,
    −cloudHalfExtent)` to `(+cloudHalfExtent, kCloudAltitude, +cloudHalfExtent)`.
    `kCloudAltitude = 200.0f` (metres). `cloudHalfExtent` defaults to 1000.0f (2 km ×
    2 km plane).
  - UV coordinates: `(0,0)` at near-left, `(kCloudUVScale, 0)` at near-right,
    `(kCloudUVScale, kCloudUVScale)` at far-right, `(0, kCloudUVScale)` at far-left.
    `kCloudUVScale = 4.0f` (tiles the cloud texture 4× across the 2 km extent).
  - Mandatory: call `recalculateBoundingBox()` on every `SMeshBuffer` then on the `SMesh`
    before `addMeshSceneNode()`. Drop the `SMesh*` after `addMeshSceneNode()` has grabbed
    it. (ref: `architecture/graphics-architecture/procedural-terrain.md` — SMesh lifetime)
  - Material settings on the resulting `IMeshSceneNode*`:
    - `MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL`
    - `Lighting = false` (`EMF_LIGHTING = false`)
    - `BackfaceCulling = false` (`EMF_BACK_FACE_CULLING = false`) so the plane is visible
      from below (camera always looks down)
    - `Texture[0]` = `clouds.png` loaded via `IVideoDriver::getTexture()` (linear pool,
      PNG, not DDS — Irrlicht DDS loader is disabled; see `architecture/graphics-architecture/texture-cache.md`
      — "IVideoDriver::getTexture() cannot load DDS files")
  - Store the cloud plane node as `m_cloudNode` (`IMeshSceneNode*`). Store the initial UV
    offset as `m_cloudUVOffset` (`irr::core::vector2df`, initialised to `{0.f, 0.f}`).
  - Store scroll speeds as constants: `kCloudScrollX = 0.002f` UV units/second,
    `kCloudScrollZ = 0.0008f` UV units/second.
  - (ref: `architecture/graphics-architecture/sky-clouds.md`)
- [ ] Implement UV scrolling in `IrrlichtRenderer::update(float dt)`:
  - Increment `m_cloudUVOffset.X += kCloudScrollX * dt` and
    `m_cloudUVOffset.Y += kCloudScrollZ * dt`.
  - Wrap both components into `[0.0f, 1.0f)` via `fmod` to prevent float precision
    accumulation over long sessions.
  - Apply by updating the texture matrix on the cloud node's material:
    `m_cloudNode->getMaterial(0).getTextureMatrix(0).setTextureTranslate(m_cloudUVOffset.X,
    m_cloudUVOffset.Y)`. (ref: `architecture/graphics-architecture/sky-clouds.md`)
- [ ] Guard `initCloudPlane()` and `update()` cloud logic with a null check on `m_smgr`
  and on `m_cloudNode` for headless/EDT_NULL contexts. Under `EDT_NULL`, `getTexture()`
  returns null and `addMeshSceneNode()` may behave unexpectedly; skip cloud initialisation
  when the driver type is `EDT_NULL`.

##### cicd-dev-github

- [ ] Add `assets/textures/sky/clouds.png` to the `validate-assets` CI presence gate:
  hard-fail in `build-linux`, `build-windows`, and `coverage-linux` jobs if the file is
  absent, using the same pattern as the Phase 10 audio asset presence gates. (ref:
  `architecture/ci-cd/github-actions-workflow.md`)
- [ ] Add **Check #24 — Cloud texture format gate** to `tools/validate_assets.py`: verify
  `assets/textures/sky/clouds.png` is exactly 1024×1024 pixels and RGBA (4 channels).
  Uses Pillow (already installed as a CI dependency from Phase 10). No-op when file does
  not exist yet.

### Exit Criteria

- On tile placement (zone, road, service building), the terrain under the placed tile
  visibly flattens: neighbouring tiles blend smoothly with no hard seams
- `TerrainFlattening_SetTileHeight_EnqueuesRebuildForAffectedChunks` passes on Linux and
  Windows CI without a real GPU
- `TerrainFlattening_NeighborBlend_ClampedToMapBounds` passes — ASAN clean on corner-tile
  flattening with no out-of-bounds write
- `TerrainFlattening_PlaceBuildingMesh_NodeYAtFlattenedHeight` passes — placed mesh Y
  matches the post-flattened height, not the pre-flattening height
- `gamedesign-lookandfeel` blending falloff factors signed off in writing before
  `setTileHeight()` is merged
- Cloud plane renders above terrain with no Z-fighting against the sky dome
- Clouds scroll continuously in the X and Z UV axes; UV offset wraps cleanly with no
  visible seam after extended play sessions
- Cloud plane invisible under `EDT_NULL` (headless CI runs clean; no crash or GL error);
  `CloudPlane_EDTNull_InitSkipped` passes in `opengl_tests`
- `clouds.png` present and 1024×1024 RGBA (Check #24 green)
- `all-checks-pass` CI gate remains green

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `ITerrainQuery::setTileHeight()`, `TerrainSystem` write path, neighbour blending, chunk rebuild enqueue, placement method updates, cloud plane mesh and UV scrolling |
| `gamedesign-lookandfeel` | Blending falloff factor sign-off |
| `graphics-artist-2d-texture` | `clouds.png` tileable cloud texture |
| `test-dev-cpp` | Three terrain flattening unit tests, CMake wiring |
| `cicd-dev-github` | Check #24 cloud texture gate, cloud asset presence gate in CI jobs |

### Dependencies

- Requires Phase 5 complete (`TerrainSystem`, `ITerrainQuery`, `ChunkRebuildRequest`
  deque, `ManualTerrainQuery` stub in test suite)
- Requires Phase 9 complete (sky dome `addSkyDomeSceneNode` already placed; building,
  road, and service-building placement helpers exist on `IrrlichtRenderer`)
- Requires Phase 9b complete (`ITerrainQuery::getHeightAt` promoted to interface;
  `ManualTerrainQuery` with `getHeightAt()` override; placement Y position already reads
  from `m_terrain`)
- Can run in parallel with Phase 10 (Dynamic Soundscape) — no shared interfaces; terrain
  flattening is a pure graphics + simulation concern; `sfx_earthworks` wiring is already
  complete from Phase 10

### Risks & Spikes

- **RISK**: Neighbour blending falloff factors are placeholder estimates (cardinal 0.5,
  diagonal 0.25). Wrong values produce jarring plateaus or excessive terrain distortion
  around placements. **Spike**: `gamedesign-lookandfeel` reviews in-editor with test
  cities before sign-off; blocking gate on merge.
- **RISK**: Enqueueing up to 9 chunk rebuilds per placement (centre + 8 neighbours) may
  exceed two chunks per frame, deferring full visual update across multiple frames. At
  high placement rates (bulk zone painting) the rebuild deque may grow unboundedly.
  **Spike**: measure deque depth during rapid rectangular zone placement on a 1024×1024
  map; if deque exceeds ~50 entries, consider raising the per-frame rebuild cap to 4 for
  placement-triggered rebuilds only (guard with a `PlacementRebuildRequest` priority flag
  in `ChunkRebuildRequest`).
- **RESOLVED**: Cloud plane altitude raised from 150 m to `kCloudAltitude = 200.0f` m,
  providing 120 m clearance above the V1 building max height (~80 m). Clipping on
  elevated terrain peaks is no longer a risk at this altitude.
- **RISK**: `EMT_TRANSPARENT_ALPHA_CHANNEL` cloud plane may sort incorrectly relative to
  transparent zone overlay quads, producing rendering artefacts when both are visible.
  **Spike**: verify depth-sort order on a city with active zone overlays; if artefacts
  appear, set `m_cloudNode->setMaterialFlag(EMF_ZBUFFER, true)` and verify the cloud
  plane passes the depth test behind opaque terrain but above the sky dome.
- **RISK**: Irrlicht texture matrix (`getTextureMatrix(0).setTextureTranslate()`) UV
  scrolling may not be supported for all material types or may be silently ignored on some
  OpenGL drivers. **Spike**: verify UV scrolling works in a minimal `requires-opengl`
  integration test or manual smoke test before committing the scrolling implementation.
