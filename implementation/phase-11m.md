## Phase 11m: Bug-Fix Batch — Zone Overlay Colors, Road Proximity Distance, Terrain Border Stitching, Cost Waiver Reset, Save State Refresh, New-Game Reset, Finances Background

**Status: OPEN**

### Goal

Seven targeted bug fixes identified during gameplay QA:

1. **Zone overlay colors missing** — Zoned but unbuilt tiles show no color. They should
   display a demand-intensity gradient: light → dark green (Residential), light → dark
   blue (Commercial), light → dark yellow (Industrial).

2. **Road proximity blocks valid placements** — `"Placement blocked: road proximity"` fires
   for tiles that are visually within 3 tiles of a road. Root cause: `nearestRoadDistance()`
   uses Manhattan distance; city-block center tiles can be at Manhattan 4+ even when only
   2–3 Chebyshev tiles from the nearest road edge.

3. **Terrain flattening reintroduces road–terrain intersection** — Zone placement flattens
   the N×N footprint to origin height but leaves adjacent tiles unchanged. Road tiles
   bordering a freshly flattened zone end up at a different height, causing road geometry
   to intersect the terrain again after an otherwise successful flatten.

4. **Cost waiver label absent on second new game** — `HUD::m_gameStartTime` is set once
   at HUD construction and `m_gracePeriodExpired` is never reset. Starting a second game
   after the first game's 120 s grace period expires means the label never appears.

5. **Main menu shows "No saves found" after saving** — `transitionToMainMenu()` does not
   re-query save state; the status set at startup (before any game was played) persists
   forever.

6. **"Quit to Main Menu" does not reset the city** — `transitionToMainMenu()` only hides
   gameplay UI panels; the simulation, renderer city objects, and terrain remain from the
   previous session. Clicking "New Game" the second time resumes the old city instead of
   starting fresh.

7. **Finances panel has no background** — `m_panelBg` is an empty `addStaticText("")` with
   no background color. `setElementBackground()` is never called, so the panel appears as
   floating text with no visible container.

---

### Deliverables

---

#### 1. Zone overlay colors for unbuilt zoned tiles

**Root cause analysis**

`UIManager::m_overlayMap` is erased on zone placement (line 1211) and demolish
(lines 934, 1476) but entries are **never added**. The renderer's `setZoneOverlay()`
is fully implemented and ready; the overlay is simply never populated.

Additionally, `QueryResult` has no field to distinguish an unbuilt zone (building mesh not
yet spawned) from a zone with an active building. `TileData::underConstruction` exists in
`CitySimulation` but is not surfaced through `ICitySimulation::queryTile()`.

**Color spec**

Overlay color is a **fixed lookup keyed on zone type × density tier** — no demand
computation. Alpha = 180 (0xB4) for all entries. Zone type encodes hue; density tier
encodes brightness (Low = pale, Medium = mid, High = dark).

| Zone type   | Density | ARGB (`0xAARRGGBB`) | Appearance |
|-------------|---------|----------------------|------------|
| Residential | Low     | `0xB480CC80`         | Pale green |
| Residential | Medium  | `0xB400AA00`         | Medium green |
| Residential | High    | `0xB4005500`         | Dark green |
| Commercial  | Low     | `0xB48080CC`         | Pale blue |
| Commercial  | Medium  | `0xB40000AA`         | Medium blue |
| Commercial  | High    | `0xB4000055`         | Dark blue |
| Industrial  | Low     | `0xB4CCCC80`         | Pale yellow |
| Industrial  | Medium  | `0xB4AAAA00`         | Medium yellow |
| Industrial  | High    | `0xB4555500`         | Dark yellow |

**Spec update** (`architecture/game-design/zoning-system.md`):

- [x] Add a subsection "Unbuilt Zone Overlay Colors" documenting:
  - Unbuilt zone tiles (placed but building not yet spawned) display a fixed-color
    overlay rendered by `IrrlichtRenderer::setZoneOverlay()`, keyed on zone type and
    density tier.
  - 9-entry color table (ZoneType × DensityTier) as above.
  - Alpha = 180 (semi-transparent).
  - Overlay is removed when the building mesh spawns or when the tile is demolished.
  **Already applied.**

**Code changes — `QueryResult` (`src/interfaces/simulation_types.h`)**:

- [x] Add `bool underConstruction{false}` to `QueryResult`. Doc comment: "true when the
  zone tile has been placed but the building mesh has not yet spawned (demand below
  `SimulationConstants::construction_delay_demand_threshold`). False for road tiles,
  unzoned tiles, and tiles whose building has already spawned."

**Code changes — `CitySimulation::queryTile()` (`src/simulation/CitySimulation.cpp`)**:

- [x] Populate `result.underConstruction = tile.underConstruction;` in the zoned-tile
  branch of `queryTile()`.

**Code changes — `UIManager` (`src/ui/UIManager.cpp` / `UIManager.h`)**:

- [x] Add private helper `uint32_t computeZoneOverlayColor(ZoneType zone, DensityTier density)`
  implemented as a 3×3 constexpr lookup table (ZoneType index × DensityTier index):

  ```cpp
  static constexpr uint32_t kTable[3][3] = {
      // Low,         Medium,       High
      { 0xB480CC80u, 0xB400AA00u, 0xB4005500u }, // Residential (green)
      { 0xB48080CCu, 0xB40000AAu, 0xB4000055u }, // Commercial  (blue)
      { 0xB4CCCC80u, 0xB4AAAA00u, 0xB4555500u }, // Industrial  (yellow)
  };
  return kTable[static_cast<int>(zone)][static_cast<int>(density)];
  ```

- [x] Fix the stale comment at line ~1207: remove "placeZone() always places a building
  immediately" — this was invalidated by Phase 11l construction delay.
- [x] In `UIManager::doTerrainPlacement()`, zone-placement branch: replace the
  `m_overlayMap.erase(key)` call with
  `m_overlayMap[key] = computeZoneOverlayColor(zoneType, densityTier)`.
  Use the `densityTier` variable already available at the `placeZone()` call site — no
  demand query needed. Call `setZoneOverlay()` after inserting the new entry (a new entry
  was just added to `m_overlayMap`, so state has changed and the renderer must be notified).
  **`setZoneOverlay()` is called only when `m_overlayMap` state changes (new entry added
  on placement, expired entry removed on refresh), never unconditionally.**
- [x] In `UIManager::update()`, add a periodic overlay refresh block.
  `m_overlayRefreshCounter` increments each frame and resets to 0 after each refresh run
  (whether triggered by reaching 60 frames or by receiving a populationTick notification);
  whichever occurs first in an update cycle triggers the refresh and resets the counter.
  **When a populationTick notification triggers the refresh path, immediately process all
  entries AND reset `m_overlayRefreshCounter` to 0. This ensures the "60-frame timer OR
  populationTick, whichever comes first" rule is deterministic — the next 60-frame interval
  starts fresh after each populationTick-driven refresh.**
  For each entry in `m_overlayMap`:
  - Call `m_sim->queryTile(tileX, tileZ)`.
  - If `!result.isZoned || !result.underConstruction`: erase from map (building spawned
    or tile no longer zoned).
  - **Do not recompute color** — color is fixed by zone type and density tier and does
    not change while the tile remains under construction.
  - After all entries processed, call `setZoneOverlay()` only if any entries were removed.

  **Overlay removal on building spawn**: When `underConstruction` becomes `false`
  (building spawned), the overlay for that tile is automatically absent on the next
  refresh cycle — the periodic 60-frame refresh is the removal mechanism; no special
  fade or instant-clear transition is performed. The tile entry is erased from
  `m_overlayMap` during the refresh pass and `setZoneOverlay()` is called once
  immediately after to push the updated (now tile-absent) map to the renderer.
- [x] Add a 60-frame counter `m_overlayRefreshCounter` (type `int`, initialized to 0)
  to UIManager to drive the periodic refresh described above.

**Note — colorblind mode deferral**: The 9-color density-tier overlay system does not
specify colorblind-safe alternatives. The static `kOverlayArgb*_Colorblind` constants
(from the Phase 9b static overlay system) are superseded for overlay use. Colorblind-safe
density-tier ramps are deferred to a post-V1 phase; see
`architecture/ui-ux/resolution-ui-scaling.md` for the colorblind spec. V1 colorblind mode
uses the same density-tier colors as non-colorblind mode for zone overlays only (minimap
and button tints remain colorblind-safe per existing constants).

**Tests** (`tests/ui/uimanager_zone_overlay_test.cpp`, label: `unit`):

Declare all three tests as a fixture with `NiceMock<MockUIBackend> backend_`,
`NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`,
`NiceMock<MockRenderer> renderer_` (defined in `tests/simulation/MockRenderer.h`), and
`ManualClock clock_` as members. Construct UIManager as
`std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_)` (canonical 4-parameter
order). Call `uiManager_->setRenderer(&renderer_)` in `SetUp()`
after constructing UIManager. Include a `TearDown()` override that resets `uiManager_` to
`nullptr` before mock destruction, enforcing the destructor-path contract per
testability-architecture.md (UIManager holds raw pointers to backend, simulation, and
renderer; resetting first prevents UAF during mock teardown). `NiceMock` is required
because UIManager's constructor calls many `addStaticText`/`addButton` methods.

All three tests assert via `MockRenderer::setZoneOverlay()` — the observable side-effect
of m_overlayMap changes — rather than accessing the private member directly.

- [x] `UIManager_ZonePlacement_AddsOverlayEntry`: place a Residential/Low zone (no demand
  stub needed — color is a fixed lookup); assert `setZoneOverlay()` is called on the mock
  renderer with a non-empty overlay map containing an entry with ARGB value `0xB480CC80`
  (Residential Low = pale green, alpha 180).
- [x] `UIManager_OverlayRefresh_RemovesEntryWhenBuildingSpawned`: set up `setZoneOverlay`
  ON_CALL to capture the overlay map; call `update()` 60 times with `queryTile` stub
  returning `underConstruction=false`; assert the final `setZoneOverlay()` call passes an
  empty overlay map (the entry was removed).
- [x] `UIManager_Demolish_RemovesOverlayEntry`: trigger demolish for a zoned tile; assert
  `setZoneOverlay()` is called with an overlay map that no longer contains that tile's
  key.

---

#### 2. Road proximity: switch to Chebyshev distance

**Root cause analysis**

`nearestRoadDistance()` (`CitySimulation.cpp:1863`) computes Manhattan distance
`|rx| + |rz|` for each candidate road offset. In a typical city block where roads run
along the perimeter, a zone tile at the center is at equal Manhattan and Chebyshev distance
along cardinal axes, but diagonally the Manhattan distance can be up to 2× the Chebyshev
distance. A zone tile 2 tiles east and 2 tiles north of a road corner has Chebyshev
distance 2 but Manhattan distance 4 — the placement check `> 3` incorrectly blocks it.

The intuitive player understanding of "3 tiles" matches Chebyshev distance (max of
`|dx|`, `|dz|`), not Manhattan distance.

**Spec update** (`architecture/game-design/zoning-system.md`):

- [x] Verify `architecture/game-design/zoning-system.md` uses "Chebyshev distance
  (`max(|dx|, |dz|) ≤ 3`)" throughout — in zone placement requirements, the abandonment
  rule (`doProximityTick()`), and multi-tile road adjacency. **Already applied** — no
  "Manhattan distance" references remain in the spec.

**Code fix** (`src/simulation/CitySimulation.cpp` — `nearestRoadDistance()`):

- [x] Change the distance variable from Manhattan to Chebyshev.

  ```cpp
  // Before:
  int manhattan = std::abs(rx) + std::abs(rz);
  if (manhattan == 0 || manhattan >= minDist) continue;
  // After:
  int chebyshev = std::max(std::abs(rx), std::abs(rz));
  if (chebyshev == 0 || chebyshev >= minDist) continue;
  ```

  Replace `minDist = manhattan` with `minDist = chebyshev`.
- [x] The `±3` search bounds already correctly cover Chebyshev distance ≤ 3 (a 7×7 box);
  no search-range change is needed.
- [x] Rename the local variable from `manhattan` to `chebyshev` throughout the function body.
- [x] `doProximityTick()` calls `nearestRoadDistance()` — the function fix automatically
  corrects abandonment distance checks as well; no additional code change is needed there.

**Test** (`tests/simulation/city_simulation_road_proximity_test.cpp`, label: `unit`):

- [x] `CitySimulation_PlaceZone_DiagonalRoad_AtChebyshev3_Allowed`: place a road at
  `(3, 3)` and attempt to zone `(0, 0)` (Chebyshev 3, Manhattan 6); assert `placeZone()`
  succeeds (no `PlacementBlocked` notification, tile `isZoned == true`).
- [x] `CitySimulation_PlaceZone_DiagonalRoad_AtChebyshev4_Blocked`: place a road at
  `(4, 4)` and zone `(0, 0)` (Chebyshev 4); assert `PlacementBlocked` notification fires.
- [x] `CitySimulation_PlaceZone_CardinalRoad_AtDistance3_Allowed`: place a road at
  `(3, 0)` and zone `(0, 0)` (Chebyshev 3 = Manhattan 3); assert success (existing
  behavior must not regress).

---

#### 3. Terrain flattening: also update tiles bordering the zone footprint

**Root cause analysis**

`placeZone()` flattens the N×N footprint tiles to `flatHeight` (origin tile height).
Adjacent road tiles retain their pre-flatten height. After the flatten operation, the
terrain changes abruptly at the zone boundary, causing road geometry placed against that
boundary to intersect the terrain again.

**Fix strategy**

After flattening all N×N footprint tiles, extend the flatten to any immediately adjacent
(orthogonally and diagonally: a (N+2)×(N+2) border ring) tile that is a **road tile**
(`isRoad == true`). This synchronises bordering road tiles to `flatHeight` without
affecting non-road terrain tiles outside the footprint, which may belong to other zones or
un-touched terrain.

**Spec update** (`architecture/graphics-architecture/procedural-terrain.md`):

- [x] Add a note under the `setTileHeight()` usage in zone placement:
  "After flattening the N×N footprint, the caller must also call `setTileHeight()` for
  every road tile within the 1-tile orthogonal+diagonal border ring of the footprint
  (a (N+2)×(N+2) candidate set minus the N×N footprint itself), bringing adjacent roads
  to the same `flatHeight`. Non-road tiles in the border ring are NOT modified."
  *(Already applied — see `architecture/graphics-architecture/procedural-terrain.md`
  § "Zone placement — border-ring road-tile flatten (Phase 11m)")*

**Code fix** (`src/simulation/CitySimulation.cpp` — `placeZone()`):

- [x] After the existing footprint-flatten loop in `placeZone()` (pre-existing code that
  calls `setTileHeight()` for each tile in the N×N footprint), add a second loop that
  iterates offsets `dx ∈ [-1, N]`, `dz ∈ [-1, N]` (the (N+2)×(N+2) candidate set).
  Note: Phase 11l's flatten loop is in `IrrlichtRenderer::placeBuildingMesh()` (runs at
  building-spawn time); the footprint-flatten loop referenced here is the separate
  zone-placement flatten that already exists in `CitySimulation::placeZone()`.
  - Skip offsets where `0 ≤ dx < N && 0 ≤ dz < N` (those are the footprint tiles,
    already flattened).
  - Look up `m_tiles.find(tileKey(tileX + dx, tileZ + dz))`.
  - If the tile exists and `tile.isRoad`, call
    `m_terrain->setTileHeight(tileX + dx, tileZ + dz, flatHeight)`.
- [x] For each offset in the border ring, skip any tile where `tileX + dx < 0 ||
  tileX + dx >= m_mapWidth || tileZ + dz < 0 || tileZ + dz >= m_mapHeight` (map-edge
  guard — use `m_mapWidth` / `m_mapHeight` stored in `CitySimulation` from the
  `MapConfig` passed at construction). Do not rely on `ITerrainQuery::setTileHeight()`
  silently ignoring out-of-bounds calls — the out-of-bounds contract is only documented
  for `getHeightAt()` (returns 0.0f); `setTileHeight()` out-of-bounds behavior is
  unspecified and must not be relied upon.
- [x] After the border-ring loop completes, call `m_terrain->flushTerrainRebuilds()` to
  synchronously apply the border-ring height changes to the terrain geometry. Without this
  flush, the border-ring terrain geometry will remain at the pre-flatten heights until
  the next game-loop tick.

**Test** (`tests/simulation/city_simulation_terrain_flatten_test.cpp`, label: `unit`):

- [x] `CitySimulation_PlaceZone_RoadAdjacentTile_FlattenedToMatchZone`:
  - Construct a `CitySimulation` with a real `TerrainSystem` seeded with a 4×4 flat
    heightmap at height 0.0f (same small-map pattern used in phase-11l terrain boundary
    tests).
  - Place a road at `(1, 0)` via `placeRoad()` so `m_tiles[(1,0)].isRoad == true`.
  - Place a 1×1 Residential zone at `(0, 0)`; origin tile is at height 0.0f so
    `flatHeight = 0.0f`.
  - Before placement, call `terrain.setTileHeight(1, 0, 5.0f)` to give the adjacent road
    tile a different height.
  - After `placeZone()`, assert `terrain.getHeightAt(1, 0) == 0.0f` (road tile was
    flattened to match `flatHeight`).
- [x] `CitySimulation_PlaceZone_NonRoadAdjacentTile_NotFlattened`:
  - Same setup; tile at `(1, 0)` is NOT a road (leave it as plain terrain at height 5.0f).
  - After `placeZone()`, assert `terrain.getHeightAt(1, 0) == 5.0f` (non-road tile
    height unchanged).

---

#### 4. Cost waiver label: reset grace period on new game

**Root cause analysis**

`HUD::m_gameStartTime` is set to `clock->nowSeconds()` at HUD **construction** (line 54),
which occurs well before the first game starts. Once the 120 s grace period expires
(`m_gracePeriodExpired = true`), neither that flag nor `m_gameStartTime` is ever reset.
Starting a second new game leaves `m_gracePeriodExpired == true`, so
`m_gracePeriodLabel` is never shown.

**Code fix** (`src/ui/HUD.cpp` / `HUD.h`):

- [x] Add `void HUD::notifyGameStarted()` (public):

  ```cpp
  void HUD::notifyGameStarted() {
      if (!m_clock) return;
      m_gameStartTime     = m_clock->nowSeconds();
      m_gracePeriodExpired = false;
      m_graceFadeAlpha    = 1.0f;
      if (m_backend && m_gracePeriodLabel != kInvalidUIElement && m_visible) {
          m_backend->setElementAlpha(m_gracePeriodLabel, 1.0f);
          m_backend->setElementVisible(m_gracePeriodLabel, true);
      }
  }
  ```

- [x] Declare `notifyGameStarted()` in `HUD.h`.
- [x] In `UIManager::transitionToGameplay()`, add a call:

  ```cpp
  if (m_hud) m_hud->notifyGameStarted();
  ```

  This fires for both first-game and subsequent new games.

**Test** (`tests/ui/hud_grace_period_test.cpp`, label: `unit`):

- [x] `HUD_SecondNewGame_GracePeriodLabelVisible`:
  **Note**: this test isolates HUD state behavior only — it does not verify the audio
  precondition (`setTimeOfDay(DAY)` before `transitionToGameplay()`). Audio sequencing is
  verified by the UIManager integration flow in Deliverable 6 and by manual exit criterion
  MC-5.
  - Declare a test constant `kTestLabelHandle = UIElementHandle{42}`.
  - Declare the test as a fixture with `NiceMock<MockUIBackend> backend_`,
    `NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`, and
    `ManualClock clock_` as members. Include a `TearDown()` override that explicitly
    resets `hud_` to `nullptr` before the mocks are destroyed, enforcing the
    destructor-path contract per testability-architecture.md (HUD holds a raw pointer
    to the backend; resetting first prevents UAF during mock teardown).
  - `NiceMock` is required for all four parameters because the HUD constructor makes
    many `addStaticText`/`addButton` calls that are not under test; `StrictMock` would
    fail on every unexpected UI-element creation call.
  - Before constructing HUD, set up a capture on the grace period label handle:
    `ON_CALL(backend_, addStaticText(HasSubstr("Cost waiver"), _, _, _, _)).WillByDefault(Return(kTestLabelHandle))`.
  - Construct `HUD hud_(&backend_, &audio_, &sim_, &clock_)` (matching the canonical
    parameter order: backend, audio, sim, clock per HUD.h). Clock is at t=0.
  - **Phase 1 — simulate first game and expire the grace period** (this is what sets
    `m_gracePeriodExpired = true` internally, creating the bug precondition):
    - Call `hud_->show()` and `hud_->notifyGameStarted()` (first game starts at t=0).
    - Advance `ManualClock` to t=200 s (beyond the 120 s grace period + fade-out).
    - Call `hud_->update(0.016f)` so the HUD processes the expired state
      (`m_gracePeriodExpired` becomes `true` internally). Do NOT reference
      `m_gracePeriodExpired` directly.
  - **Phase 2 — verify second game resets the grace period**:
    - Now install expectations (after first-game state is established):
      `EXPECT_CALL(backend_, setElementAlpha(kTestLabelHandle, 1.0f)).Times(AtLeast(1))` and
      `EXPECT_CALL(backend_, setElementVisible(kTestLabelHandle, true)).Times(AtLeast(1))`.
    - Call `hud_->notifyGameStarted()` (second game starts — this must reset the
      grace period and make the label visible again).
    - Call `hud_->update(0.016f)` so the HUD renders the reset state.
    - Verify mock expectations are satisfied (GMock verifies on mock destruction or via
      `Mock::VerifyAndClearExpectations(&backend_)`).
  - **Rationale**: Installing expectations only after the first game's expiry ensures
    the test exclusively measures the second `notifyGameStarted()` call's effect on
    visibility — not incidental calls during the first-game lifecycle.

---

#### 5. Save state refresh on return to main menu

**Root cause analysis**

In `main.cpp` (lines 231–246), `getSaveFileState()` is called once at startup and the
result is pushed to `UIManager`. When the player saves a game and then quits to main menu,
`transitionToMainMenu()` does not re-query — so the label still shows "No saves found."

`UIManager` already holds `m_saveSystem` (set via `setSaveSystem()`), so the fix is
entirely within `transitionToMainMenu()`.

**Code fix** (`src/ui/UIManager.cpp` — `transitionToMainMenu()`):

- [x] At the end of `transitionToMainMenu()`, before `m_mainMenu->show()`, add:

  ```cpp
  // Re-query save state so the Load Game button reflects any saves made this session.
  if (m_saveSystem && m_mainMenu) {
      SaveFileState state = m_saveSystem->getSaveFileState();
      m_mainMenu->setSaveAvailable(state == SaveFileState::Valid);
      switch (state) {
          case SaveFileState::NoSaves:
              m_mainMenu->setSaveStatusText("No saves found.");
              break;
          case SaveFileState::AllCorrupt:
              // ISaveSystem::getSaveDirectoryPath() is already defined in the interface
              // (returns std::string) — use it in the diagnostic message to help players
              // locate corrupt saves.
              m_mainMenu->setSaveStatusText(
                  "Save data is corrupted — cannot load. Check "
                  + m_saveSystem->getSaveDirectoryPath() + " for recovery.");
              break;
          case SaveFileState::Valid:
              m_mainMenu->setSaveStatusText("");
              break;
      }
  }
  ```

**Test** (`tests/ui/uimanager_save_state_test.cpp`, label: `unit`):

- [x] `UIManager_TransitionToMainMenu_RefreshesLoadButtonState`:
  - Declare as a test fixture with `NiceMock<MockUIBackend> backend_`,
    `NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`,
    `ManualClock clock_`, and `NiceMock<MockSaveSystem> saveSystem_` as members.
    Include a `TearDown()` override that resets `uiManager_` to `nullptr` before mock
    destruction (destructor-path contract per testability-architecture.md).
    Construct UIManager as `std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_)`
    (matching the canonical 4-parameter constructor order).
  - After constructing UIManager, call `uiManager_->setSaveSystem(&saveSystem_)` to
    inject the mock save system.
  - Configure `ON_CALL(saveSystem_, getSaveFileState()).WillByDefault(Return(SaveFileState::NoSaves))`.
  - Boot UIManager through a game session (`transitionToGameplay`, then begin
    `transitionToMainMenu` call).
  - Before `transitionToMainMenu` fires, switch mock:
    `ON_CALL(saveSystem_, getSaveFileState()).WillByDefault(Return(SaveFileState::Valid))`.
  - Set `EXPECT_CALL(saveSystem_, getSaveFileState()).Times(AtLeast(1))` to verify
    the re-query happens during `transitionToMainMenu()`.
  - Call `uiManager_->transitionToMainMenu()`.
  - Verify mock expectations (GMock verifies on mock destruction or via
    `Mock::VerifyAndClearExpectations(&saveSystem_)`). Satisfaction of the
    `getSaveFileState()` expectation proves the re-query occurred.

---

#### 6. "Quit to Main Menu" must reset city for a new game

**Root cause analysis**

`transitionToMainMenu()` hides gameplay panels. `transitionToGameplay()` shows HUD/minimap.
Neither resets `CitySimulation` state, clears renderer city objects, nor re-generates
terrain. On the second "New Game", the old simulation data persists.

The first game's terrain is generated **before the main game loop** in `main.cpp`
(initialization section, line 160). There is currently no mechanism to trigger a second
terrain-generation pass from within the game loop.

**Design**

- Add `UIManager::consumeNewGameRequest()` → returns true (once) when the player clicks
  "Start City" and a previous game session has already been played.
- Add `UIManager::getNewGameParams()` → returns the selected `MapSize`, seed, and
  difficulty so `main.cpp` can regenerate terrain with the correct parameters.
- Add `CitySimulation::reset(int64_t startingFunds)` — clears all simulation state.
- Add `IRenderer::clearCity()` — removes all building, road, and traffic-agent scene
  nodes from the scene graph while preserving the terrain mesh.
- `main.cpp` game loop polls `consumeNewGameRequest()` and, when true, performs the full
  reset + regenerate + loading loop sequence before calling
  `uiManager.transitionToGameplay()`.

**Spec update** (`architecture/ui-ux/ui-manager.md`):

- [x] Document the `consumeNewGameRequest()` / `getNewGameParams()` contract: these are
  polling methods that return true/valid-params exactly once after each "Start City"
  click that follows a prior gameplay session. They reset automatically on consumption.
- [x] Document that `transitionToGameplay()` is called by `main.cpp` (not by UIManager
  itself) when the second-or-later new-game flow completes terrain generation.

**Spec updates**:

- [x] Update `architecture/graphics-architecture/scene-graph-ownership.md`: Add a
  subsection "City Reset — clearCity()" documenting the contract: iterates
  `m_buildingNodes`, `m_roadNodes`, and `m_agentNodes` (IrrlichtRenderer's internal
  registries), calls the appropriate eviction sequence on each entry, clears all three
  maps, and resets road mesh buffers. Terrain chunk nodes are NOT removed.
  **Already applied.**
- [x] Update `architecture/ui-ux/ui-manager.md`: Add a subsection "New Game Request
  Polling API" documenting `consumeNewGameRequest()`, `getNewGameParams()`, and the
  `NewGameParams` struct (`{MapSize mapSize; int seed; int difficulty;}`). The contract:
  `consumeNewGameRequest()` returns `true` exactly once after each "Start City" click
  that follows a prior gameplay session, then resets to `false`. `getNewGameParams()`
  returns the params captured at the moment of the request. V1 hardcodes `Sandbox` mode;
  `NewGameParams` intentionally has no `gameMode` field. **Already applied.**

**Code changes — `UIManager.h` / `UIManager.cpp`**:

- [x] Add `bool m_gameSessionActive{false}` member: set to `true` the first time
  `transitionToGameplay()` completes.
- [x] Add `bool m_newGamePending{false}` member.
- [x] In `UIManager::update()`, when `consumeStartGameRequest()` returns true:
  - If `m_gameSessionActive == false` (first game): behave as today — call
    `transitionToGameplay(GameMode::Sandbox)` immediately.
  - If `m_gameSessionActive == true` (subsequent game): set `m_newGamePending = true`;
    show the loading screen (`m_mainMenu->showLoadingScreen()`); do NOT call
    `transitionToGameplay()` yet.
- [x] Add `bool UIManager::consumeNewGameRequest()`: returns `m_newGamePending` and
  atomically resets it to `false`.
- [x] Add `struct NewGameParams { MapSize mapSize; int seed; int difficulty; };`
  and `NewGameParams UIManager::getNewGameParams()` returning the params captured at
  "Start City" click.
  **Phase 11m scope**: V1 hardcodes `GameMode::Sandbox`; Scenario mode is deferred.
  `NewGameParams` does not carry a `gameMode` field in this phase — the
  `transitionToGameplay(GameMode::Sandbox)` call in `main.cpp` supplies the mode
  directly, reflecting the deliberate V1 choice to support Sandbox only.
- [x] Add `NewGameParams m_newGameParams{}` member (stores params from the last "Start City"
  click; populated by `consumeStartGameRequest()` handling and by `handleNewGameRequest()`).
- [x] Add `void UIManager::handleNewGameRequest(const NewGameParams& params)` — a
  test-accessible entry point that simulates a "Start City" button press directly (bypassing
  the event system), gated on `#ifdef AITOWN_TESTING_ENABLED` (consistent with
  `setGameSessionActiveForTest`). Implementation: store `m_newGameParams = params`; if
  `m_gameSessionActive` is true, set `m_newGamePending = true` and call
  `m_mainMenu->showLoadingScreen()`; otherwise call `transitionToGameplay(GameMode::Sandbox)`
  (first-game path). Production code uses `consumeStartGameRequest()` via `UIManager::update()`.
- [x] In `transitionToGameplay()`, set `m_gameSessionActive = true` after the state change.
- [x] In `UIManager::transitionToGameplay()`, call `m_audio->setTimeOfDay(TimeOfDay::DAY)`
  **first**, then `m_audio->transitionToGameplay()` — matching the mandatory call sequence
  specified in `architecture/audio-architecture/audio-system.md` lines 160-162. New games
  always start at DAY; `setTimeOfDay()` must be called before `transitionToGameplay()` so
  that the ambient bed selection on sources[60..61] uses the correct time-of-day value.
- [x] Add `void UIManager::setGameSessionActiveForTest(bool value)` gated on
  `#ifdef AITOWN_TESTING_ENABLED`: sets `m_gameSessionActive = value` directly. Required by the
  `UIManager_SecondNewGame_SetsNewGamePendingFlag` test to force the "subsequent game" path
  without running a full gameplay session first.
- [x] In `transitionToMainMenu()`, call `m_audio->transitionToMainMenu()` before
  `m_mainMenu->show()` to stop gameplay music stems and ambient beds and restart
  main menu music. **`m_audio->transitionToMainMenu()` MUST be the first operation
  called (before modal/overlay state resets and save-state refresh), to stop gameplay
  audio stems before any UI state transitions occur. See audio-system.md lines 177-189.**
  Call order within `transitionToMainMenu()`:
  1. `m_audio->transitionToMainMenu()` — **must be first; stops all gameplay audio before UI changes**
  2. `onNewGame()` (clear overlay + tool state)
  3. Save-state refresh (re-query `m_saveSystem->getSaveFileState()`)
  4. `m_mainMenu->show()`

  **Note**: `onNewGame()` clearing `m_overlayMap` in step (2) has no connection to audio
  source state — the overlay map is a pure UI rendering construct. The "audio first"
  requirement exists solely to ensure gameplay audio stems stop before UI transitions begin.

  **Note**: when `main.cpp`'s new-game-request loop subsequently calls
  `UIManager::transitionToGameplay()`, it will re-establish the mandatory audio
  precondition — `setTimeOfDay(TimeOfDay::DAY)` is called first, then
  `transitionToGameplay()` — per the requirement documented in this deliverable (and
  `audio-system.md` lines 160-162). The audio state is correctly maintained across
  multiple game sessions.

  `transitionToMainMenu()` does NOT reset `m_gameSessionActive`. The flag stays `true`
  after returning to the menu so that the next `handleNewGameRequest()` call correctly
  takes the subsequent-game path (sets `m_newGamePending = true`). If `m_gameSessionActive`
  were reset to `false` here, the next Start City click would take the first-game direct
  path and skip the full city reset.

**Code changes — `ICitySimulation.h` / `CitySimulation.h` / `CitySimulation.cpp`**:

- [x] Add `virtual void reset(int64_t startingFunds) = 0;` to `ICitySimulation`.
- [x] Add `MOCK_METHOD(void, reset, (int64_t startingFunds), (override));` to
  `tests/ui/MockCitySimulation.h` (under the "Zone/road action methods" section).
  **Required**: `MockCitySimulation` inherits `ICitySimulation`; adding a new pure-virtual
  method to the interface without updating the mock causes a compile error on every test
  that instantiates `MockCitySimulation`. The mock must have an entry for every pure-virtual
  method in `ICitySimulation`.
- [x] **Prerequisite — `TrafficVehicle` struct field additions** (`src/simulation/CitySimulation.h`):
  Before implementing `reset()`, add two new fields to the `TrafficVehicle` struct:

  ```cpp
  int idleIdx = -1;   // source-pool index for idle engine sound; -1 = not acquired
  int moveIdx = -1;   // source-pool index for moving engine sound; -1 = not acquired
  ```

  These fields are required by the `releaseVehicleEnginePair(agent.idleIdx, agent.moveIdx)`
  call inside `reset()` (see below). The default value `-1` signals "no audio pair acquired"
  and is treated as a no-op by `IAudioSystem::releaseVehicleEnginePair()`. Without these
  fields the `reset()` implementation will not compile.

- [x] Implement `CitySimulation::reset(int64_t startingFunds)`:
  - Clear `m_tiles`.
  - Reset `m_totalPopulation = 0`, `m_roadTileCount = 0`, `m_treasury = startingFunds`.
  - Clear `m_notifications` queue.
  - Before clearing `m_agents`, iterate all traffic agents and call
    `m_audio->releaseVehicleEnginePair(agent.idleIdx, agent.moveIdx)` for each agent that
    has an active audio pair (`agent.idleIdx != -1`). This returns all vehicle engine pool
    slots to the free state, preventing orphaned PLAYING sources on the second new game.
    Passing `{-1, -1}` (agent with no acquired pair) is a no-op per IAudioSystem contract.
  - Clear `m_agents` (traffic agents).
  - Clear `m_trafficSignals`.
  - Clear `m_serviceBuildings`.
  - Reset `m_constructionTimeSeconds = m_clock->nowSeconds()` (restart grace period).
  - Reset all tick counters, deficit counters, loan state to initial values.
  - Reset demand factors, desirability aggregate, population counters.
  - Reset `m_firstRevenueTicked = false`, `m_loanCooldownTicks = 0`,
    `m_outstandingDebt = 0`, `m_consecutiveDeficitMonths = 0`.
  - Reset all building variant counters in `m_buildingVariantCounters` to `0`
    (fill all 9 elements with 0). Per `save-system.md`, the default-constructed
    value for a new game is `[0, 0, 0, 0, 0, 0, 0, 0, 0]` (index mapping defined
    in `architecture/game-design/save-system.md`); failing to reset these counters
    causes skewed variant distribution on the second game.
  - Does NOT call `clearCity()` on the renderer internally — main.cpp calls
    `renderer.clearCity()` as a separate step after `citySimulation.reset()` returns
    (see main.cpp code changes below). This avoids double-calling and keeps
    `CitySimulation` free of renderer dependencies.
  - Explicitly does NOT reset tax rates (keep player's tax settings from last game — but
    this is a design choice; document it).

**Code changes — `IAudioSystem.h` / `MockAudioSystem.h`**:

- [x] Add `virtual void transitionToMainMenu() = 0;` to `src/interfaces/IAudioSystem.h`
  (pure-virtual, 19th method). The header comment and phase-history annotation already reflect
  19 methods with the Phase 11m annotation in `architecture/audio-architecture/audio-system.md`
  (lines 64–67) — **Already applied** to the spec; no spec change needed, only the C++ source
  declaration.
- [x] Add `MOCK_METHOD(void, transitionToMainMenu, (), (override))` to
  `tests/simulation/MockAudioSystem.h`.

**Code changes — `AudioSystem.h` / `AudioSystem.cpp`**:

- [x] Add `void transitionToMainMenu() override;` to `src/audio/AudioSystem.h`.
- [x] Add `int m_lastMainMenuVariant{-1};` as a private member to `AudioSystem`
  (declared in `src/audio/AudioSystem.h`). Initialized to `-1` in the constructor member
  initializer list. This tracks the last-played main menu variant to enforce the
  same random-excluding-repeat policy used for gameplay stems
  (ref: `architecture/audio-architecture/dynamic-soundscape.md` §Main Menu Audio).
- [x] **Variant selection in `transitionToMainMenu()`** — implement using the
  same random-excluding-repeat policy as gameplay stems:
  1. Build the candidate set `{1, 2}` (the two main menu music variants).
  2. Remove `m_lastMainMenuVariant` from the candidate set (exclude repeat).
     If `m_lastMainMenuVariant == -1` (first call), both variants are candidates.
  3. Select uniformly at random from the remaining candidates using
     `std::uniform_int_distribution` seeded from `m_rng` (the existing RNG member
     already present in `AudioSystem`; if `m_rng` is not yet present, add
     `std::mt19937 m_rng{std::random_device{}()};` as a private member).
  4. Store the selected variant in `m_lastMainMenuVariant` before calling
     `setMusicTrack()` with that variant ID, so the next `transitionToMainMenu()`
     call correctly excludes it.
- [x] In `AudioSystem::transitionToMainMenu()`, wrap the entire variant selection
  block — (1) build candidate set `{1, 2}`, (2) exclude `m_lastMainMenuVariant`,
  (3) select randomly with `std::uniform_int_distribution` from `m_rng`, (4) store
  result back to `m_lastMainMenuVariant` — in a single
  `std::lock_guard<std::mutex> lk(m_streamMutex)` scope. This prevents a data race
  with the audio thread's EOF variant-switching path, which also reads and writes
  `m_lastMainMenuVariant` under `m_streamMutex` (see thread-safety contract at
  line ~676). Release the lock before the subsequent `setMusicTrack()` /
  crossfade-setup calls — do NOT hold `m_streamMutex` across the full method body
  (that would cause audio-thread starvation on its 10 ms wake cycle per
  `streaming-architecture.md`).
- [x] Implement `AudioSystem::transitionToMainMenu()` in `src/audio/AudioSystem.cpp`
  per the contract in `architecture/audio-architecture/audio-system.md`:
  stop all active gameplay music stems and ambient beds on sources[58..61], then
  crossfade in main menu music on sources[58..59] over 1 s using the constant-power
  curve (wall-clock time ONLY — bar-boundary synchronization is NOT applied to this
  transition; bar-boundary tracking applies only to within-gameplay stem crossfades,
  not to gameplay↔main-menu transitions which are triggered by user action and must
  start immediately — per `audio-system.md` lines 171-174), and establish looping
  playback (main menu music loops indefinitely per `dynamic-soundscape.md` §Main Menu
  Audio). Safe to call if gameplay audio is not active — the implementation stops all
  sources[58..61] unconditionally via `alSourceStop` (a no-op for non-playing sources);
  no guard condition is required per `audio-system.md` lines 175-178. Use `m_clock`
  for timing the crossfade, consistent with existing crossfade patterns in AudioSystem.
- [x] Add `bool m_mainMenuMusicLooping{false}` private member to `AudioSystem`
  (declared in `src/audio/AudioSystem.h`). Set it to `true` inside
  `transitionToMainMenu()` once the main menu music streaming loop is
  configured for indefinite looping via the OGG seek mechanism (same
  streaming refill mechanism as gameplay stems — the decode loop calls
  `ov_pcm_seek(vf, 0)` at EOF and continues refilling; do NOT set
  `AL_LOOPING = AL_TRUE` on buffer-queue streaming sources (sources[58..61]
  — the entire streaming partition, not just main menu sources) — that flag
  applies only to pre-loaded single-buffer sources and produces undefined
  behavior on any buffer-queue source; all streaming looping uses the OGG
  EOF→seek pattern per `streaming-architecture.md`). The looping refill itself
  executes on the audio thread inside `updateStreams()` (called every audio
  thread wake — see `architecture/audio-architecture/streaming-architecture.md`
  §libvorbisfile EOF detection): when `ov_read()` returns `0` on sources[58..59]
  during main menu playback, the decode loop calls `ov_pcm_seek(vf, 0)` and
  continues refilling — exactly the same EOF→seek pattern as ambient beds.
  **Variant switching at loop restart**: per `dynamic-soundscape.md` §Main Menu
  Audio ("Variant selection: same random-excluding-repeat policy as gameplay
  stems"), each time EOF triggers `ov_pcm_seek(vf, 0)` on a main-menu source,
  the audio thread must apply the random-excluding-repeat variant selection.
  **Use random-excluding-repeat selection (NOT deterministic flip — do NOT use a
  deterministic flip) to match gameplay stem variant selection policy per
  dynamic-soundscape.md line 5.** Implementation: (1) build a candidate list of
  all variants (`{1, 2}`) excluding `m_lastMainMenuVariant`, (2) randomly select
  from that list using `std::uniform_int_distribution` seeded from `m_rng` (the
  same RNG member used for gameplay stem selection). Do NOT use a deterministic
  flip or index-toggle in place of this random selection. Update
  `m_lastMainMenuVariant` and open the new OGG file before refilling.
  **Observation (mathematical, NOT implementation guidance)**: with exactly 2
  variants the candidate list always contains one entry, so `std::uniform_int_distribution`
  will always return the alternate variant — this is a consequence of the math, not
  the intended mechanism. The implementation MUST use `std::uniform_int_distribution`
  regardless; do NOT replace it with a deterministic flip even if the observable
  result would currently be identical.
  **Thread-safety and mutex scope**: `m_lastMainMenuVariant` is written by the
  main thread in `transitionToMainMenu()` (under `m_streamMutex`) and by the
  audio thread at EOF. For the EOF path, the required operation sequence is:
  (1) acquire `m_streamMutex` → read `m_lastMainMenuVariant` → randomly select
  next variant via `std::uniform_int_distribution` (not a deterministic flip) →
  write `m_lastMainMenuVariant` → release `m_streamMutex`;
  (2) open the new OGG file OUTSIDE the mutex (variable-duration I/O per the
  minimal critical section principle in `streaming-architecture.md`);
  (3) acquire `m_streamMutex` → call `ov_pcm_seek(vf, 0)` and begin refilling
  → release. Do NOT hold `m_streamMutex` across the OGG file open — this would
  violate the streaming-architecture.md rule against variable-duration work
  inside the mutex and cause audio-thread starvation.
  **Note**: this variant switching uses the EOF event — NOT bar-boundary
  crossfades (the bar-boundary software counter applies only to within-gameplay
  calm/growth/crisis tier crossfades per `dynamic-soundscape.md` line 7).
  **Ordering requirement**: `m_mainMenuMusicLooping = true` MUST be set as the
  **final** operation in `transitionToMainMenu()`, after all stop-gameplay and
  crossfade-initiation operations have completed. This makes the flag a
  completion sentinel: `EXPECT_TRUE(audio.isMainMenuMusicLooping())` in the
  test confirms that the full method body executed without an early exit,
  implicitly covering the gameplay-stop path.
  Thread-safety contract for `m_mainMenuMusicLooping`: the flag is written on
  the calling thread (main thread) inside `transitionToMainMenu()` using a
  **minimal critical section** — acquire `m_streamMutex` solely for the flag
  assignment (`m_mainMenuMusicLooping = true`) and release it immediately after.
  Do NOT hold `m_streamMutex` across the entire method body: the AL stop/crossfade
  setup before the flag assignment must NOT hold the mutex (consistent with the
  streaming-architecture.md rule that OGG decode and AL-call sequences acquire
  the mutex in short scopes per Steps 1 and 3, not across full operations).
  Holding the mutex for the full method duration would cause audio-thread starvation
  on its 10 ms wake cycle. `isMainMenuMusicLooping()` reads
  it without a lock (test-only accessor — the test calls
  `transitionToMainMenu()` synchronously and reads the flag on the same thread
  immediately after, under the same single-threaded assertion pattern used for
  other `AudioSystem` state accessors).
- [x] Add `bool isMainMenuMusicLooping() const` test accessor to the concrete
  `AudioSystem` class (declared in `AudioSystem.h`, implemented inline or in
  `AudioSystem.cpp`). Returns `m_mainMenuMusicLooping`. This accessor is NOT part
  of `IAudioSystem` — it is on the concrete class only, for unit-test inspection
  without exposing AL types through any interface.
- [x] Add test file `tests/audio/audio_system_transition_main_menu_test.cpp`
  (label: `unit`) with one test:
  - `AudioSystem_TransitionToMainMenu_LoopingFlagSet`: construct
    `AudioSystem audio(nullptr, &clock_)` (passing `nullptr` for `IAlcFunctions`
    activates `DefaultAlcFunctions`; CI sets `ALSOFT_DRIVERS=null` globally so the
    null audio backend is used — no `putenv` call needed in test code). Wrap
    construction in `try/catch(std::runtime_error)` and call `GTEST_SKIP()` if
    construction fails (mirrors the pattern in `volume_control_test.cpp`). After
    successful construction call `audio.transitionToMainMenu()`. Assert
    `audio.isMainMenuMusicLooping() == true`. Test file must NOT include any AL
    headers (`<AL/al.h>`, `<AL/alc.h>`) — all verification is through the
    `AudioSystem` public/test accessor API. (ref:
    `architecture/audio-architecture/audio-system.md`,
    `architecture/audio-architecture/dynamic-soundscape.md`)
  - **Note**: `isMainMenuMusicLooping()` is the designed completion sentinel for
    `transitionToMainMenu()` — verifying this flag is the correct and complete
    automated test contract per `testability-architecture.md`. The flag is set as
    the **final** operation in `transitionToMainMenu()` (after all stop-gameplay
    and crossfade-initiation operations), so its truth value implicitly confirms
    the full method body executed without an early exit. Actual audio streaming
    quality (source state transitions, crossfade onset, looping behavior) is
    verified by manual exit criterion MC-3 (see exit criteria: "start game, build
    city for ≥30 s, then quit to main menu → main menu music begins playing within
    1 s, loops continuously without audible clicks, silence gaps, or abrupt cuts").

**Code changes — `IRenderer.h` / `IrrlichtRenderer.h` / `IrrlichtRenderer.cpp`**:

- [x] Add `virtual void clearCity() = 0;` to `IRenderer`.
- [x] Implement `IrrlichtRenderer::clearCity()`:
  - For `m_buildingNodes` (stored as `LODNode*` wrappers): iterate the map and
    perform the full 4-step eviction sequence on each entry — (1) clear all material
    texture slots (iterate `mat.setTexture(t, nullptr)` for each texture unit), (2) call
    `m_driver->setMaterial(SMaterial{})` to flush the driver state, (3) call
    `node->remove()` to release the scene node from the scene graph, (4) call
    `delete kv.second` to free the heap-allocated `LODNode*` C++ wrapper (the wrapper
    is not reference-counted and is not freed by `node->remove()` — it must be deleted
    explicitly). Call `m_buildingNodes.clear()` once after the loop. Do NOT call
    `destroyTileNode()` during the iteration — `destroyTileNode()` erases from the map
    internally, causing iterator invalidation on `std::unordered_map`. Authoritative
    contract: `architecture/graphics-architecture/scene-graph-ownership.md §City Reset
    — clearCity() (Phase 11m)`, step 1.
  - For `m_roadNodes` (stored as raw `ISceneNode*`, no `LODNode` wrapper): iterate the
    map and perform the 3-step eviction sequence on each entry — (1) clear all material
    texture slots (iterate `mat.setTexture(t, nullptr)` for each texture unit), (2) call
    `m_driver->setMaterial(SMaterial{})` to flush the driver state, (3) call
    `node->remove()`. Road nodes are reference-counted `ISceneNode*` managed by
    Irrlicht — there is no heap-allocated C++ wrapper to delete. Call
    `m_roadNodes.clear()` once after the loop. Authoritative contract:
    `architecture/graphics-architecture/scene-graph-ownership.md §City Reset —
    clearCity() (Phase 11m)`, step 2.
  - For `m_agentNodes`: same full eviction sequence — texture clear, driver-state flush
    (`m_driver->setMaterial(SMaterial{})`), then `node->remove()` — then
    `m_agentNodes.clear()` after the loop. Traffic vehicle scene nodes must not persist
    from game 1 into game 2.
  - If `IrrlichtRenderer` maintains a persistent shared `SMesh*` for batched road
    geometry, empty it using the following pattern — `SMesh::addMeshBuffer()` calls
    `grab()` on each buffer, so the inverse `drop()` is required before clearing:

    ```cpp
    for (u32 i = 0; i < roadMesh->getMeshBufferCount(); ++i)
        roadMesh->getMeshBuffer(i)->drop();   // reverse the grab() from addMeshBuffer()
    roadMesh->MeshBuffers.clear();            // remove all buffer entries (public member of SMesh)
    roadMesh->recalculateBoundingBox();       // reset bounding box to empty/degenerate
    ```

    Do NOT `->drop()` the `SMesh*` itself — it is reused in the next game session. After
    `clearCity()` returns the mesh object is present but empty (zero buffers); subsequent
    `addRoadTile()` calls in the next game session re-append mesh buffers and rebuild
    the batched geometry from scratch. If roads use only per-tile nodes (all already
    evicted above), this step is a no-op.
  - Do NOT remove terrain chunk nodes (terrain is regenerated separately via
    `TerrainSystem::generate()`).
  - Reset any renderer-side road-tile count (e.g., `m_roadTileCount = 0`) and clear
    any per-tile mesh state that accumulates across `addRoadTile()` calls.

**Code changes — `main.cpp`** (game loop):

- [x] After `uiManager.update(realDeltaSeconds)`, add:

  ```cpp
  if (uiManager.consumeNewGameRequest()) {
      UIManager::NewGameParams ngp = uiManager.getNewGameParams();
      int64_t startingFunds = SimulationConstants::startingFundsForDifficulty(ngp.difficulty);
      citySimulation.reset(startingFunds);
      renderer.clearCity();
      StdTerrainRNG freshRng;
      freshRng.reseed(ngp.seed);
      terrainSystem.generate(
          static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize),
          10.0f, &freshRng);
      terrainSystem.buildAllChunks(); // subdivides heightmap into chunks, queues all LOD0 rebuilds
      // Run loading loop identical to the startup loading loop.
      double loadPrev2 = wallClock.nowSeconds();
      while (device->run() && terrainSystem.pendingRebuildCount() > 0) {
          const double loadNow2  = wallClock.nowSeconds();
          const float  loadDt2   = static_cast<float>(loadNow2 - loadPrev2);
          loadPrev2 = loadNow2;
          uiManager.update(loadDt2);
          terrainSystem.update(loadDt2);
          terrainSystem.flushPendingRebuilds();
          CameraState loadCam = cameraController.getCameraState();
          try {
              audioSystem.syncListenerToCamera(loadCam);
              audioSystem.update(loadDt2);
          } catch (const std::exception& e) {
              fprintf(stderr, "[main] Audio error during new-game loading: %s\n", e.what());
          }
          renderer.beginFrame();
          renderer.drawScene();
          renderer.endFrame();
      }
      uiManager.setMapDimensions(
          static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize));
      uiManager.transitionToGameplay(GameMode::Sandbox);
      uiManager.onGameLoaded(); // Re-initializes per-game view state: resets
                                // city-rating tracking state, clears notification
                                // history, and re-arms the cost-waiver label —
                                // see UIManager.h for full declaration.
                                // Specifically: sets m_previousCityRating to the
                                // current simulation city-rating tier so that the
                                // first update() tick after load does not fire a
                                // spurious stinger_milestone event. Does NOT
                                // transition GameState, clear the loading screen,
                                // or show HUD/minimap — those are performed by
                                // transitionToGameplay() on the line above.
      continue;  // restart frame loop from the top after transition
  }
  ```

  After step (5) (`transitionToGameplay()`), main.cpp calls `uiManager.onGameLoaded()`
  (pre-existing Phase 11 method) to reinitialize per-game view state: resets city-rating
  tier tracking, clears notification history, and re-arms the cost-waiver label. This call
  must occur before the first `update()` tick of the new game.

- [x] Add `SimulationConstants::startingFundsForDifficulty(int difficulty)` helper
  returning the per-difficulty starting fund (Easy/Normal/Hard as per economy-model.md):
  Easy = $1,000,000; Normal = $500,000; Hard = $200,000.

**Tests** (`tests/ui/uimanager_new_game_reset_test.cpp`, label: `unit`):

Declare both tests as a fixture with `NiceMock<MockUIBackend> backend_`,
`NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`, and
`ManualClock clock_` as members. Construct UIManager as
`std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_)` (canonical 4-parameter
order). Include a `TearDown()` override that resets `uiManager_` to `nullptr` before
mock destruction, enforcing the destructor-path contract per testability-architecture.md
(UIManager holds a raw pointer to the backend; resetting first prevents UAF during mock
teardown). `NiceMock` is required because UIManager's constructor makes many
`addStaticText`/`addButton` calls that are not under test.

- [x] `UIManager_SecondNewGame_SetsNewGamePendingFlag`:
  - Force `m_gameSessionActive = true` via test setter (add a `setGameSessionActiveForTest(bool)`
    method to UIManager gated on `#ifdef AITOWN_TESTING_ENABLED`).
  - Simulate the "Start City" button press by calling `uiManager_->handleNewGameRequest(params)`
    (a test-accessible entry point on UIManager that processes a NewGameParams struct as if
    the "Start City" button was clicked; NOT a mock call — calls the real UIManager logic).
  - Assert `consumeNewGameRequest()` returns `true`.
  - Assert a second call to `consumeNewGameRequest()` returns `false` (consumed — resets
    `m_newGamePending`).
- [x] `UIManager_FirstGame_NoNewGamePendingFlag`:
  - `m_gameSessionActive` is `false` (default — first game has not yet been played).
  - Simulate the "Start City" button press via `uiManager_->handleNewGameRequest(params)`.
  - Assert `consumeNewGameRequest()` returns `false` (first game takes the direct
    `transitionToGameplay()` path; no pending flag is set).

---

#### 7. Finances panel background

**Root cause analysis**

`FinancesPanel::m_panelBg` is created with `addStaticText("")`. Without calling
`setElementBackground()`, an `IGUIStaticText` with empty text and `fillBackground=false`
(Irrlicht default) is invisible. The panel appears as floating labels with no backing
rectangle.

**Code fix** (`src/ui/FinancesPanel.cpp` — constructor, after `m_panelBg` creation):

- [x] Add, immediately after line 56 (the `m_panelBg = m_backend->addStaticText(...)` line):

  ```cpp
  m_backend->setElementBackground(m_panelBg, 13, 27, 42, 217);
  // Glass City deep-navy: rgba(13, 27, 42, 0.85) per finances-panel.md.
  ```

  The RGBA values `(13, 27, 42, 217)` match the Glass City deep-navy style specified in
  `architecture/ui-ux/finances-panel.md` (alpha 217 = 0.85 × 255).

**Note — corner radius deferral**: `finances-panel.md` specifies 8 px corner radius on all
edges. Irrlicht's `IGUIStaticText` does not support corner radius natively; rounded-corner
panel tiles require a sprite-sheet UI layer (post-V1). Phase 11m implements solid-color
background only, matching the V1 limitation documented for the Settings/Pause Menu panel.
The 8 px corner radius remains in the spec as the authoritative post-V1 target.

**Test** (`tests/ui/finances_panel_background_test.cpp`, label: `unit`):

- [x] `FinancesPanel_Constructor_CallsSetElementBackground`:
  - Declare the test as a fixture with `NiceMock<MockUIBackend> backend_`,
    `NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`, and
    `ManualClock clock_` as members. Include an explicit `TearDown()` override that
    calls `panel_ = nullptr;` before the mocks are destroyed, enforcing the
    destructor-path contract (FinancesPanel holds a raw pointer to the backend;
    resetting first prevents UAF during mock teardown). This matches the TearDown()
    pattern documented in testability-architecture.md and used in D1, D4, and D5
    test specs.
    (`NiceMock` is required because the constructor calls `addStaticText` and
    `addButton` many times; `StrictMock` would fail on every unexpected UI-element
    creation call.)
  - The test body must follow this exact sequence (GMock requires expectations to be
    installed **before** the action that triggers them):
    1. **Set expectation first**: `EXPECT_CALL(backend_, setElementBackground(_, 13, 27, 42, 217)).Times(1)`.
    2. **Then construct FinancesPanel**: `auto panel_ = std::make_unique<FinancesPanel>(&backend_, &audio_, &sim_, &clock_)` —
       constructor parameter order is `(IUIBackend*, IAudioSystem*, ICitySimulation*, IClock*)`,
       i.e., `(&backend_, &audio_, &sim_, &clock_)` — the constructor fires
       `setElementBackground()`, satisfying the expectation installed in step 1.
    3. **Then verify**: `Mock::VerifyAndClearExpectations(&backend_)` (or rely on mock
       destruction at end of test scope to trigger GMock's automatic verification).

---

### CMakeLists.txt Registration

Register each new test file via `target_sources()` in the appropriate test target:

```cmake
# In CMakeLists.txt (root) — append to the existing target_sources(ui_tests ...) block:
target_sources(ui_tests PRIVATE
    tests/ui/uimanager_zone_overlay_test.cpp          # D1 — 3 tests
    tests/ui/hud_grace_period_test.cpp               # D4 — 1 test
    tests/ui/uimanager_save_state_test.cpp           # D5 — 1 test
    tests/ui/uimanager_new_game_reset_test.cpp       # D6-UI — 2 tests
    tests/ui/finances_panel_background_test.cpp     # D7 — 1 test
)

# REQUIRED: enable AITOWN_TESTING_ENABLED so that UIManager test-only methods
# (handleNewGameRequest, setGameSessionActiveForTest — gated on
# #ifdef AITOWN_TESTING_ENABLED) are compiled into both aitown_ui AND ui_tests.
#
# aitown_ui requires AITOWN_TESTING_ENABLED because the gated methods
# (handleNewGameRequest, setGameSessionActiveForTest) are non-inline — their
# definitions live in UIManager.cpp, which is compiled as part of aitown_ui.
# Without this flag on aitown_ui, the gated .cpp definitions are elided and
# ui_tests will fail to link (undefined symbol errors).
#
# ui_tests also requires it so that the #ifdef-guarded declarations in
# UIManager.h are visible when test translation units include the header.
#
# aitown_ui PRIVATE keeps the flag out of any downstream consumer's compile
# environment — it does not propagate to the production aitown binary.
#
# NOTE: target_compile_definitions(aitown_ui PRIVATE AITOWN_TESTING_ENABLED=1) may be
# placed anywhere in the CMakeLists.txt that defines aitown_ui — CMake compile definitions
# apply to the target globally and are not position-sensitive relative to
# target_sources() calls. The flag must simply be present before cmake's configure step
# completes.
target_compile_definitions(aitown_ui PRIVATE AITOWN_TESTING_ENABLED=1)
target_compile_definitions(ui_tests PRIVATE AITOWN_TESTING_ENABLED=1)

# In CMakeLists.txt (root) — append to the existing target_sources(simulation_tests ...) block:
target_sources(simulation_tests PRIVATE
    tests/simulation/city_simulation_road_proximity_test.cpp   # D2 — 3 tests
    tests/simulation/city_simulation_terrain_flatten_test.cpp  # D3 — 2 tests
)

# CRITICAL: audio_tests was already registered at Phase 0 with aitown_add_tests().
# DO NOT call aitown_add_tests(audio_tests) again — duplicate target registration
# causes CMake FATAL_ERROR. Use ONLY target_sources(audio_tests PRIVATE ...) for
# all Phase 11m additions, matching the Phase 7 and Phase 10 extension patterns.
#
# In CMakeLists.txt (root) — append to the existing target_sources(audio_tests ...) block:
target_sources(audio_tests PRIVATE
    tests/audio/audio_system_transition_main_menu_test.cpp  # D6-audio — 1 test
)
```

Do NOT call `add_executable` or `aitown_add_tests` again — extend the existing targets only.

### Exit Criteria

- [x] All 7 deliverable code fixes implemented.
- [x] All 14 tests above pass (green) with `ctest --test-dir build -LE "integration|requires-opengl"`.
- [x] Manual audio acceptance: start game, build city for ≥30 s, then quit to main menu →
  main menu music begins playing within 1 s, loops continuously without audible clicks,
  silence gaps, or abrupt cuts.
- [x] All 8 new test files registered in CMakeLists.txt via `target_sources()` (see above).
- [x] `make build` succeeds with zero warnings.
- [x] Zone overlay: place a Residential/High-density zone → tile shows dark green;
  place a Residential/Low-density zone → tile shows light green; building spawns → tile clears.
- [x] Road proximity: zone a tile 2 steps diagonally from a road (Chebyshev 2, Manhattan 4)
  → placement succeeds; 4 steps diagonal → blocked.
- [x] Terrain border: place a zone adjacent to a road on sloped terrain → road no longer
  intersects terrain after placement.
- [x] Cost waiver: quit a game after grace period, start a second game → "Cost waiver: 120s
  remaining" label appears immediately in HUD.
- [x] Save state: start a game, save, quit to menu → Load Game button is enabled, status
  label is hidden.
- [x] New-game reset: start game, build city, quit to menu, start new game → loading screen
  appears, new terrain generated, city is empty.
- [x] Finances panel: press T → panel has visible dark-background rectangle behind all
  labels and buttons.
- [x] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` passes
  with zero errors.
