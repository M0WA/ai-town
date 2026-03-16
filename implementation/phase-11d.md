## Phase 11d: Detailed Asset Rework & Simulation Visual Wiring

**Status: Planned**

### Goal

Elevate the visual quality of all V1 city assets to production-detail level and wire the
already-complete traffic and service coverage simulation (Phase 6) to visible in-world
rendering: moving vehicle agents on roads, traffic signal state indicators, and interactive
service coverage radius overlays on the minimap and in the world.

---

### Deliverables

#### 0. Interface Seam — Day-One Commit

This commit must land BEFORE any test files for Deliverables 3d or 4c are written.

- [ ] Define in `src/interfaces/IRenderer.h`: `using AgentHandle = uint32_t;` and the seven new
  method signatures: `spawnVehicleAgent`, `moveVehicleAgent`, `despawnVehicleAgent`,
  `setIntersectionSignalState`, `showServiceCoverageOverlay`, `hideServiceCoverageOverlay`,
  `getListenerPosition`.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/game-design/traffic-system.md`, `architecture/game-design/service-coverage.md`)

- [ ] Define in `src/interfaces/simulation_types.h`: `enum class SignalPhase { Green, Red };`,
  `struct AgentState { ... };`, `struct IntersectionSignalState { ... };`,
  `struct RoadSegmentSpeed { ... };`, `struct ServiceCoverageTile { ... };`.
  (ref: `architecture/game-design/traffic-system.md`,
  `architecture/game-design/service-coverage.md`)

- [ ] Extend `ICitySimulation` with four new pure-virtual query methods: `getAgentPositions`,
  `getIntersectionSignalStates`, `getRoadSegmentSpeeds`, `getServiceCoverage`.
  (ref: `architecture/game-design/traffic-system.md`,
  `architecture/game-design/service-coverage.md`)

- [ ] Update `MockCitySimulation` with `MOCK_METHOD` declarations for all four new query
  methods (`getAgentPositions`, `getIntersectionSignalStates`, `getRoadSegmentSpeeds`,
  `getServiceCoverage`). This is a prerequisite for all test authoring in Deliverables 3d
  and 4c.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] Extend `MockRenderer` with `MOCK_METHOD` stubs for all six new `IRenderer` methods:
  `spawnVehicleAgent`, `moveVehicleAgent`, `despawnVehicleAgent`,
  `setIntersectionSignalState`, `showServiceCoverageOverlay`, `hideServiceCoverageOverlay`.
  This is a prerequisite for all test authoring in Deliverables 3d and 4c.
  (ref: `architecture/testing/testability-architecture.md`)

---

#### 1. 3D Model Detail Rework

All building and vehicle models authored in Phase 9 are reworked to reach the upper end of
the polygon budgets defined in `architecture/asset-standards/3d-model-standards.md`. Phase 9
established valid-but-minimal geometry; this phase adds architectural detail that makes the
city feel lived-in at close and mid range.

##### 1a. Building Model Rework

- [ ] **Large-building LOD0 geometry** — re-export all Large building variants
  (`res_high_01`, `res_high_02`, `com_high_01`, `com_high_02`, `ind_high_01`, `ind_high_02`)
  targeting the **upper** LOD0 tri range: **4,000–5,000 tris** (spec: 2,000–5,000 tris per
  `architecture/asset-standards/3d-model-standards.md` §LOD Requirements).
  Added detail must include: window reveals (inset geometry, not painted), setback modelling
  at each floor band, rooftop equipment silhouettes (AC units, parapet caps, antennae stubs),
  and entrance canopy geometry.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [ ] **Large-building LOD1 geometry** — re-export all Large building LOD1 meshes targeting
  **800–1,000 tris** (spec: 500–1,000 tris). LOD1 must retain the silhouette profile of LOD0
  setbacks without per-window insets.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [ ] **Small-building LOD0 geometry** — re-export all Small building variants
  (`res_low_01`, `res_low_02`, `com_low_01`, `com_low_02`, `ind_low_01`, `ind_low_02`,
  `res_med_01`, `res_med_02`, `com_med_01`, `com_med_02`, `ind_med_01`, `ind_med_02`)
  targeting the **upper** LOD0 tri range: **1,200–1,500 tris** (spec: 500–1,500 tris per
  `architecture/asset-standards/3d-model-standards.md` §LOD Requirements).
  Added detail must include: bay window outcrops, door step geometry, window sill shelves,
  and roof overhang profile geometry.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [ ] **Small-building LOD1 geometry** — re-export all Small building LOD1 meshes targeting
  **250–300 tris** (spec: 100–300 tris). LOD1 must preserve the overall building envelope and
  the primary facade plane; per-window detail is baked into the texture, not modelled.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [ ] **Service building LOD0 geometry** — re-export all four service building models
  (`svc_fire_station_lod0.b3d`, `svc_police_station_lod0.b3d`, `svc_power_plant_lod0.b3d`,
  `svc_water_tower_lod0.b3d`) targeting **3,000–5,000 tris** (treated as Large buildings per
  spec category). Added detail must include: antenna masts, intake/exhaust geometry on the
  power plant, hose reels and garage bay insets on the fire station, and service entrance
  steps. UV channel 0 must remain mapped to the `service_buildings` atlas cell (row 3, col 2
  of the 2048×2048 building atlas) per `architecture/asset-standards/building-atlas-layout.md`.
  (ref: `architecture/asset-standards/3d-model-standards.md`,
  `architecture/asset-standards/building-atlas-layout.md`)

- [ ] **LOD distance thresholds unchanged**: LOD swap distances (Large buildings: LOD0→LOD1
  at >50 m / <45 m; LOD1→LOD2 at >200 m / <185 m; Small: >30 m / <25 m; >100 m / <90 m)
  are defined in `architecture/asset-standards/3d-model-standards.md` and must NOT be
  altered. Only mesh detail within each LOD level changes in this phase.
  (ref: `architecture/asset-standards/3d-model-standards.md` §LOD Distance Thresholds)

- [ ] **`validate_assets.py` regression-clean**: after re-export, confirm all reworked
  building models pass the existing `validate_assets.py` 24-check suite (established in
  Phase 5 and extended through Phase 9) with zero errors. The `validate-assets` CI job
  must remain green.
  (ref: `architecture/ci-cd/github-actions-workflow.md`)

- [ ] **`graphics-artist-3d-model` sign-off gate** (blocking): before committing any reworked
  `.b3d` file, `graphics-artist-3d-model` must verify: (a) tri counts are within the upper
  LOD0/LOD1 ranges stated above, (b) coordinate system is Y-up Z-forward left-handed
  (Irrlicht convention), (c) pivot is at the building's ground-centre footprint, (d) UV
  channel 0 maps into the correct building atlas cell, (e) UV channel 1 (lightmap) is present
  and non-overlapping, (f) 5 mm minimum gap between UV islands (per
  `architecture/asset-standards/3d-model-standards.md` §UV Authoring). The sign-off is
  recorded as a commit-message annotation on the first reworked-model commit.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

##### 1b. Vehicle Model Rework

- [ ] **Car LOD0 geometry** — re-export the car model targeting **1,400–1,500 tris** (spec
  binding limit: ≤1,500 tris per `architecture/asset-standards/3d-model-standards.md`
  §Vehicle Polygon Budget). Added detail: door panel creases, wheel arch geometry, bumper
  corner rounding, side mirror bodies.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [ ] **Bus and truck LOD0 geometry** — re-export bus (`bus_standard`) and truck
  (`truck_cargo`) models targeting their respective upper binding limits (bus ≤2,500 tris,
  truck ≤2,500 tris — per the Vehicle Polygon Budget per-class table in
  `architecture/asset-standards/3d-model-standards.md`). Added detail proportional to each
  vehicle type's silhouette complexity.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [ ] **Vehicle LOD1 geometry** — re-export all vehicle LOD1 meshes at their per-class
  binding limits: car ≤300 tris, bus ≤450 tris, truck ≤450 tris (per the Vehicle Polygon
  Budget per-class table in `architecture/asset-standards/3d-model-standards.md`; the
  general Vehicles row indicative range of 200–500 tris is superseded by these binding
  limits). LOD1 retains the body silhouette; wheels are simplified to flat discs.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

---

#### 2. Texture Detail Rework

All building and vehicle textures authored in Phase 9 are reworked to production detail
quality. Phase 9 established correct format and UV mapping; this phase replaces the
placeholder paint-over content with hand-authored or baked detail.

##### 2a. Building Facade Atlas Rework

- [ ] **`buildings_atlas_d.png` (source PNG, 2048×2048)** — re-author all 14 assigned atlas
  cells (per `architecture/asset-standards/building-atlas-layout.md` Cell Assignment Table)
  to production quality within the 496×496 px per-cell usable area (8 px border on each edge
  per spec). Required detail per cell type:
  - **Wall cells** (rows 0–2, cols 0–2): genuine brick, glass-panel, concrete-panel, or
    corrugated-metal surface texture with subtle grunge, window glass reflections baked in,
    and normal-map-compatible edge highlights.
  - **Base shared cells** (row 0 col 3; row 1 col 3): ground-floor material breakup — tile,
    stone, or poured-concrete surface with contact-shadow gradient at the building foot.
  - **Roof shared cell** (row 2 col 3): rooftop material — tar paper, gravel, parapet coping,
    and HVAC equipment footprint colour.
  - **Facade detail cells** (row 3 cols 0–1): balcony railing texture, pilaster stone detail,
    cornice moulding, window-bay reveal colour.
  - **Service buildings cell** (row 3 col 2): concrete base, glass-block accent, warning
    stripe on power-plant intake, blue-band on police station, red-band on fire station,
    galvanised steel on water tower.
  (ref: `architecture/asset-standards/building-atlas-layout.md`,
  `architecture/asset-standards/2d-texture-standards.md`)

- [ ] **`buildings_atlas_d.png` → DDS pipeline**: after re-authoring the source PNG, run
  `tools/export_textures.py` (Phase 9 deliverable) to regenerate `buildings_atlas_d.dds`
  (DXT1 sRGB, 4-mip) and `buildings_atlas_d_nm.dds` (DXT5nm normal map, 4-mip). Validate
  the DDS headers per `architecture/asset-standards/2d-texture-standards.md`
  §Validating sRGB DDS Output. Both files must pass the
  `validate_assets.py` sRGB/mip-chain checks before commit.
  (ref: `architecture/asset-standards/2d-texture-standards.md` §DDS Authoring Pipeline)

- [ ] **Normal map source re-author** — for each wall-cell type produce or update the
  normal-map source PNG: realistic brick-mortar depth, window-sill relief, panel joint
  grooves. Export using the DXT5nm swizzle procedure (X→alpha, Y→green, Z=0) per
  `architecture/asset-standards/2d-texture-standards.md` §DXT5nm packing.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [ ] **`graphics-artist-2d-texture` sign-off gate** (blocking): before committing any
  reworked atlas PNG or DDS, `graphics-artist-2d-texture` must verify: (a) sRGB ICC profile
  embedded in source PNG, (b) DX10 extended header present in DDS with correct DXGI_FORMAT
  (BC1_UNORM_SRGB = 72 for DXT1 atlas; BC3_UNORM_SRGB = 78 for DXT5 atlas), (c) 8 px
  border on every atlas cell respected, (d) all four mip levels present in DDS data (not
  truncated — check file size against reference table in
  `architecture/asset-standards/2d-texture-standards.md` §DDS Mip Chain Integrity), (e) no
  UV bleed across cell borders (visual inspection at LOD1 switch-in distance of 45 m for
  large buildings). Sign-off recorded as commit-message annotation.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

##### 2b. Vehicle Texture Rework

- [ ] **Diffuse atlas** — `vehicles_diffuse_atlas_d.png` (2048×2048 source) →
  `vehicles_diffuse_atlas_d.dds` (DXT1 sRGB, 4-mip): rework vehicle liveries to production
  quality — panel gaps, door seams, wheel disc markings, windscreen tinting,
  headlight/tail-light bezels, and chassis underside darkening. Exported via
  `tools/export_textures.py`. DX10 sRGB header (`BC1_UNORM_SRGB`, DXGI_FORMAT = 72) must be
  validated before commit.
  (ref: `architecture/asset-standards/2d-texture-standards.md` §DDS Authoring Pipeline,
  `architecture/asset-standards/building-atlas-layout.md` §Vehicle Atlas)

- [ ] **Sprite atlas** — `vehicles_sprite_atlas_d.png` (256×256 source) →
  `vehicles_sprite_atlas_d.dds` (DXT5, linear, 1-mip): author roof colour-swatch palette for
  LOD2 impostors per vehicle type. Exported via `tools/export_textures.py`.
  (ref: `architecture/asset-standards/3d-model-standards.md` §Billboard Imposter Bake,
  `architecture/asset-standards/2d-texture-standards.md`)

- [ ] **Normal atlas** — `vehicles_normal_atlas_n.png` (2048×2048 source) →
  `vehicles_normal_atlas_n.dds` (DXT5nm, linear, 4-mip): author vehicle surface normal maps
  using the DXT5nm swizzle procedure (X→alpha, Y→green, Z=0) per
  `architecture/asset-standards/2d-texture-standards.md` §DXT5nm packing. Exported via
  `tools/export_textures.py`.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [ ] **`validate_assets.py` checks #25–27 — vehicle atlas DDS verification**: as part of
  completing this deliverable, the `cicd-dev-github` engineer adds three new checks to
  `validate_assets.py` for vehicle atlas DDS format and mip-level validation:
  check #25 (`vehicles_diffuse_atlas_d.dds` — BC1_UNORM_SRGB, 4-mip, 2048×2048),
  check #26 (`vehicles_sprite_atlas_d.dds` — BC3_UNORM linear, 1-mip, 256×256),
  check #27 (`vehicles_normal_atlas_n.dds` — BC3_UNORM linear (DXT5nm), 4-mip, 2048×2048).
  All three checks must pass before Deliverable 2b is considered complete.
  (ref: `architecture/ci-cd/github-actions-workflow.md`,
  `architecture/asset-standards/2d-texture-standards.md`)

##### 2c. Billboard Atlas Rework

- [ ] **Billboard atlases** (`res_low_01_billboard.dds`, `com_low_01_billboard.dds`, etc. —
  one per Small-building variant with `height_floors ≤ 3`) — re-bake all billboard imposters
  at the confirmed −45° camera pitch bake angle (per
  `architecture/asset-standards/3d-model-standards.md` §Camera Pitch Range). Billboards must
  reflect the reworked LOD0 mesh and atlas textures. Format: DDS DXT5/BC3 sRGB, 1024×128 px,
  4-mip chain capped at `GL_TEXTURE_MAX_LEVEL = 3`.
  (ref: `architecture/asset-standards/3d-model-standards.md` §Billboard Imposter Bake)

---

#### 3. Traffic System Visual Wiring

Phase 6 delivered the full `CitySimulation` traffic simulation (A* pathfinding, agent
lifecycle, speed-function, intersection signal cycles, demand coupling). This deliverable
wires that simulation to visible in-world rendering: vehicle mesh nodes spawned and driven
along road paths, and intersection signal state shown at road nodes.

##### 3a. Vehicle Agent Rendering

- [ ] **`IRenderer::spawnVehicleAgent(agentId, ZoneType, vec3 startPos)` → `AgentHandle`**
  — new method on `IRenderer` interface (`src/interfaces/IRenderer.h`). Spawns a vehicle
  `SceneNode` (using `IrrlichtRenderer::placeVehicle` vehicle-pool pattern from Phase 9)
  at `startPos` and registers the `AgentHandle` in `SceneEntityManager`. Zone type determines
  which vehicle atlas cell is sampled (Residential → car (sedan/hatchback/SUV);
  Commercial → bus_standard; Industrial → truck_cargo). Returns a handle the caller uses for
  subsequent `moveVehicleAgent` /
  `despawnVehicleAgent` calls. `AgentHandle` is defined as `using AgentHandle = uint32_t;` in
  `src/interfaces/IRenderer.h` — it is a stable identifier for the agent's lifetime and is
  assigned by the traffic simulation. agentId (from traffic simulation) is mapped 1:1 to
  `AgentHandle`; the renderer maintains a parallel `m_agentNodes` registry alongside the
  existing `m_vehicleNodes` — agent methods coexist with Phase 10 vehicle methods without
  replacing them.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget)

- [ ] **`IRenderer::moveVehicleAgent(AgentHandle, vec3 pos, float headingDeg)`** — updates
  the agent's scene node position and Y-axis rotation each frame. Called from `main.cpp`
  per-frame render loop after `CitySimulation::getAgentPositions()` is polled. No physics;
  direct node position set (`setPosition` + `setRotation`).
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **`IRenderer::despawnVehicleAgent(AgentHandle)`** — removes the vehicle scene node via
  `SceneEntityManager::destroy()`, following the destroy-nulls-before-remove contract.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **`ICitySimulation::getAgentPositions()` query** — new method on `ICitySimulation`
  returning a `std::vector<AgentState>` (defined in `simulation_types.h`) where `AgentState`
  carries `{ agentId, tileX, tileZ, headingDeg, ZoneType }`. Used by the render loop to
  sync scene nodes to simulation state each frame. `AgentState` is a value type; no raw
  pointers.
  (ref: `architecture/game-design/traffic-system.md`)

- [ ] **`IRenderer::getListenerPosition()` → `irr::core::vector3df`** — method defined in
  Deliverable 0 interface seam (`src/interfaces/IRenderer.h`); returns the current
  camera/listener world-space position. Used by the traffic agent cull logic below to compute
  agent-to-camera distance.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **Distance cull for agent spawning** — only agents within **150 m** of the camera
  listener position (per `IRenderer::getListenerPosition()` defined in this phase) are
  spawned as scene nodes. Agents outside cull range are tracked in simulation but have no
  scene node. On spawn, `spawnVehicleAgent` is called; on despawn (timeout or exit cull
  range), `despawnVehicleAgent` is called. This matches the vehicle-engine-audio 150 m cull
  established in Phase 10.
  (ref: `architecture/game-design/traffic-system.md`,
  `architecture/audio-architecture/dynamic-soundscape.md`)

- [ ] **Per-frame agent sync loop in `main.cpp`**: after `CitySimulation::tick()` and before
  `IRenderer::drawScene()`, call `CitySimulation::getAgentPositions()` and reconcile the
  returned list against the active `AgentHandle` map (spawn missing, despawn removed, move
  existing). This loop runs every render frame (not every simulation tick), giving agents
  smooth linear interpolation between tick-computed positions.
  (ref: `architecture/game-design/traffic-system.md`)

##### 3b. Traffic Signal Visual State

- [ ] **`IRenderer::setIntersectionSignalState(tileX, tileZ, SignalPhase phase)`** — sets
  the material emissive colour on the road intersection scene node at `(tileX, tileZ)` to
  green (`SignalPhase::Green`) or red (`SignalPhase::Red`), where `SignalPhase` is
  `enum class SignalPhase { Green, Red };` defined in `src/interfaces/simulation_types.h`
  (see Deliverable 0). Signal state is driven by
  `CitySimulation::getIntersectionSignalStates()` (new query method, see below) polled once
  per simulation tick. Visual implementation: a small billboard quad above the intersection
  node changes colour; uses `EMT_TRANSPARENT_ADD_COLOR` material for emissive glow.
  (ref: `architecture/game-design/traffic-system.md` §Intersections)

- [ ] **`ICitySimulation::getIntersectionSignalStates()` query** — returns
  `std::vector<IntersectionSignalState>` (new struct in `simulation_types.h`:
  `{ tileX, tileZ, SignalPhase phase }`). Polled once per simulation tick in the main loop
  and forwarded to `IRenderer::setIntersectionSignalState`.
  (ref: `architecture/game-design/traffic-system.md` §Intersections)

##### 3c. Traffic Overlay — Minimap

- [ ] **Minimap traffic congestion overlay** — add a `MinimapOverlay::Traffic` rendering
  mode to the existing minimap (Phase 8 deliverable). When the player toggles the overlay,
  road segments are tinted on the 200×200 px minimap by congestion level using the following
  thresholds (thresholds per `architecture/game-design/traffic-system.md` congestion penalty
  table):
  - Green: speed ≥ 40% of max (no penalty band)
  - Orange: speed 21–39% of max (covers the 31–40% and 21–30% penalty bands)
  - Red: speed ≤ 20% of max (≤20% penalty band)
  The congestion colour is fetched from `ICitySimulation::getRoadSegmentSpeeds()` (new query,
  see below) and applied as a coloured overlay pixel on the minimap texture.
  (ref: `architecture/game-design/traffic-system.md` §Congestion threshold,
  `architecture/ui-ux/minimap.md`)

- [ ] **`ICitySimulation::getRoadSegmentSpeeds()` query** — returns
  `std::vector<RoadSegmentSpeed>` (new struct: `{ tileX, tileZ, float speedFraction }`)
  where `speedFraction` is `currentSpeed / maxSpeed` in [0.0, 1.0]. Used exclusively by
  the minimap traffic overlay.
  (ref: `architecture/game-design/traffic-system.md`)

##### 3d. Traffic Tests

- [ ] **`AgentRenderSync_SpawnDespawn_MatchesSimulationOutput`** (label `unit`,
  CMake target `simulation_tests`): using `MockRenderer`, verify that the per-frame agent
  sync loop calls `spawnVehicleAgent` exactly once per new agent, `moveVehicleAgent` every
  frame for active agents, and `despawnVehicleAgent` exactly once when an agent is removed.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`AgentRenderSync_CullDistance_AgentsBeyond150m_NotSpawned`** (label `unit`,
  CMake target `simulation_tests`): verify that agents with tile distance > 150 m from the
  camera do not trigger `spawnVehicleAgent`.

- [ ] **CMakeLists extension** — add test source via `target_sources(simulation_tests PRIVATE
  tests/simulation/agent_render_sync_test.cpp)` following the Phase 4+ extension policy (do
  NOT call `add_executable` again — duplicate target error).

---

#### 4. Service Coverage Visual Wiring

Phase 6 delivered the full service coverage simulation (radius computation, deficit
degradation, desirability effects). This deliverable wires that simulation to visible
in-world indicators: coverage radius outlines rendered in world space when a service building
is selected, and a coverage overlay layer on the minimap.

##### 4a. Service Coverage Radius Overlay

- [ ] **`IRenderer::showServiceCoverageOverlay(tileX, tileZ, ServiceBuildingType,
  bool degraded)`** — renders a wireframe circle (or tile-step polygon) on the terrain at the
  specified building's coverage radius. Radius values come directly from the spec:
  Fire Station 800 m, Police Station 600 m, Water Tower 700 m, Power Plant uses BFS footprint
  (not a circle — highlight covered tiles individually). When `degraded == true`, the overlay
  radius is halved for radius-based buildings (per
  `architecture/game-design/service-coverage.md` §Budget deficit degradation).
  (ref: `architecture/game-design/service-coverage.md`)

- [ ] **`IRenderer::hideServiceCoverageOverlay()`** — removes the coverage overlay. Called
  when the Inspector panel is closed or a different tile is queried.
  (ref: `architecture/game-design/service-coverage.md`)

- [ ] **Inspector panel integration** — when `queryTile` returns a service building tile,
  `UIManager` calls `showServiceCoverageOverlay` with the building's type and degradation
  state. On Inspector close, `hideServiceCoverageOverlay` is called. Uses the
  `InspectorPanel::populate()` wiring established in Phase 9b.
  (ref: `architecture/ui-ux/query-inspector-panel.md`,
  `architecture/game-design/service-coverage.md`)

- [ ] **Power plant BFS tile highlight** — for `ServiceBuildingType::PowerPlant`, instead of
  a circle overlay, `showServiceCoverageOverlay` highlights all BFS-reachable tiles by
  calling `IRenderer::setTileHoverHighlight` (Phase 9b deliverable) in a distinct "coverage"
  colour (cyan `#00C9C8`) on all tiles within the plant's BFS coverage depth.
  `hideServiceCoverageOverlay` clears these highlights.
  (ref: `architecture/game-design/service-coverage.md` §Power plant coverage model)

##### 4b. Service Coverage Minimap Overlay

- [ ] **Minimap service coverage overlay** — add a `MinimapOverlay::ServiceCoverage` mode
  to the minimap. When active, each service building's covered tiles are tinted on the
  200×200 px minimap using distinct colours per service type (matching the service legend
  panel established in Phase 8):
  - Fire Station coverage: red tint `#C0392B`
  - Police Station coverage: blue tint `#2E4482`
  - Power Plant coverage: yellow tint `#F1C40F`
  - Water Tower coverage: cyan tint `#1ABC9C`
  Only covered tiles are coloured; uncovered tiles receive no special highlight (per
  `architecture/ui-ux/minimap.md` covered-tile encoding).
  (ref: `architecture/ui-ux/minimap.md`,
  `architecture/game-design/service-coverage.md`)

- [ ] **`ICitySimulation::getServiceCoverage()` query** — returns
  `std::vector<ServiceCoverageTile>` (new struct in `simulation_types.h`:
  `{ tileX, tileZ, ServiceBuildingType coveredBy, bool degraded }`) for all tiles currently
  covered by at least one service building. Used exclusively by the minimap service coverage
  overlay. An uncovered tile is absent from the returned vector.
  (ref: `architecture/game-design/service-coverage.md`)

##### 4c. Service Coverage Tests

- [ ] **`ServiceCoverageOverlay_QueryServiceTile_ShowsOverlay`** (label `unit`,
  CMake target `simulation_tests`): using `MockRenderer`, verify that querying a service
  building tile calls `showServiceCoverageOverlay` with correct type and degradation state.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`ServiceCoverageOverlay_InspectorClose_HidesOverlay`** (label `unit`,
  CMake target `simulation_tests`): verify that closing the Inspector panel calls
  `hideServiceCoverageOverlay`.

- [ ] **CMakeLists extension** — add via `target_sources(simulation_tests PRIVATE
  tests/simulation/service_coverage_overlay_test.cpp)`.

---

### Exit Criteria

- All reworked building LOD0 meshes confirmed within the upper polygon ranges (Large:
  4,000–5,000 tris; Small: 1,200–1,500 tris; Service: 3,000–5,000 tris)
- All reworked vehicle LOD0 meshes confirmed at or below their per-class binding limits from
  `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget
- `validate_assets.py` 27-check suite passes with zero errors on all reworked `.b3d` and
  `.dds` files (checks #25–27 added by Deliverable 2b for vehicle atlas DDS format
  verification); `validate-assets` CI job remains green
- `buildings_atlas_d.dds` DX10 header confirmed BC1_UNORM_SRGB (DXGI_FORMAT = 72); all four
  mip levels present; no UV bleed across atlas cell borders
- `vehicles_diffuse_atlas_d.dds` passes DX10 header validation: BC1_UNORM_SRGB
  (DXGI_FORMAT = 72), 4-mip chain, 2048×2048.
- `vehicles_sprite_atlas_d.dds` passes DX10 header validation: BC3_UNORM (DXT5 linear),
  1-mip, 256×256.
- `vehicles_normal_atlas_n.dds` passes DX10 header validation: BC3_UNORM (DXT5nm linear),
  4-mip chain, 2048×2048.
- Vehicle agents visibly move along roads in gameplay; despawn correctly on timeout (120
  simulation seconds per `architecture/game-design/traffic-system.md`)
- Intersection signal billboard quads toggle green/red in sync with `CitySimulation` tick
- Minimap traffic overlay colours road segments correctly by congestion level per the
  graduated thresholds in `architecture/game-design/traffic-system.md` §Congestion threshold
- Service coverage radius overlay appears on Inspector open for each service building type;
  disappears on Inspector close
- Power plant BFS tile highlights correctly reflect BFS coverage depth (including brownout
  depth reduction at budget deficit)
- Minimap service coverage overlay correctly shows per-type coloured tiles
- All new unit tests pass (`AgentRenderSync_SpawnDespawn_MatchesSimulationOutput`,
  `AgentRenderSync_CullDistance_AgentsBeyond150m_NotSpawned`,
  `ServiceCoverageOverlay_QueryServiceTile_ShowsOverlay`,
  `ServiceCoverageOverlay_InspectorClose_HidesOverlay`)
- `all-checks-pass` gate green

### Team

| Role | Responsibility |
|---|---|
| `graphics-artist-3d-model` | Rework all building and vehicle LOD0/LOD1 `.b3d` meshes to upper polygon-budget detail; re-bake all billboard atlases at −45° pitch; sign off per deliverable 1a/1b sign-off gate |
| `graphics-artist-2d-texture` | Rework `buildings_atlas_d.png` all 14 assigned cells to production detail; author all three vehicle atlases (`vehicles_diffuse_atlas_d.dds` DXT1 sRGB 4-mip, `vehicles_sprite_atlas_d.dds` DXT5 linear 1-mip, `vehicles_normal_atlas_n.dds` DXT5nm linear 4-mip); rework billboard atlases; re-export all DDS files via `tools/export_textures.py`; sign off per deliverable 2a sign-off gate |
| `graphics-dev-irrlicht` | Implement `IRenderer` methods for vehicle agent spawn/move/despawn, intersection signal state, service coverage overlays; add minimap traffic and service coverage overlay modes; add `ICitySimulation` query methods (`getAgentPositions`, `getIntersectionSignalStates`, `getRoadSegmentSpeeds`, `getServiceCoverage`); per-frame agent sync loop in `main.cpp` |
| `gamedesign-lookandfeel` | Confirm coverage radius colours, minimap tint palette, and signal visual behaviour match simulation intent; verify no V1 scope creep in visual wiring |
| `gamedesign-ux` | Verify Inspector panel coverage-overlay UX (show on open, hide on close); confirm minimap overlay toggle interaction matches `architecture/ui-ux/minimap.md` |
| `test-dev-cpp` | Deliverable 0 (Day-One Commit): extend `MockRenderer` with `MOCK_METHOD` stubs for all six new `IRenderer` methods (`spawnVehicleAgent`, `moveVehicleAgent`, `despawnVehicleAgent`, `setIntersectionSignalState`, `showServiceCoverageOverlay`, `hideServiceCoverageOverlay`) before any test files for Deliverables 3d or 4c are written; author all four new unit tests; extend `simulation_tests` CMake target |
| `cicd-dev-github` | Verify `validate-assets` CI job remains green after DDS rework; confirm `all-checks-pass` gate green |

### Dependencies

- Requires Phase 9 complete (building/vehicle `.b3d` assets and atlas DDS files exist as
  the baseline being reworked; `placeVehicle` scene-node infrastructure is in place)
- Requires Phase 9b complete (`IRenderer::setTileHoverHighlight`, `InspectorPanel::populate`,
  `ActiveTool` dispatch — used by service coverage overlay wiring)
- Requires Phase 6 complete (`CitySimulation` traffic simulation and service coverage
  simulation fully implemented; `ICitySimulation` interface with `placeServiceBuilding` and
  all placement APIs present)
- Requires Phase 10 complete (`placeVehicle`/`moveVehicle`/`removeVehicle` vehicle lifecycle
  in place; vehicle-engine audio 12-pair pool for agent audio sync)
- Requires Phase 11 complete (save/load serialises `wasPowered`/`wasWaterCovered`/`alertFired`
  TileData fields per `architecture/game-design/service-coverage.md` §Per-Tile Audio
  Transition Fields)
- **Interface Seaming prerequisite**: `ICitySimulation` must be extended with four new query
  methods (`getAgentPositions`, `getIntersectionSignalStates`, `getRoadSegmentSpeeds`,
  `getServiceCoverage`) and all six return-type structs must be defined in
  `simulation_types.h` as the very first commit of Phase 11d — prior to any mock update or
  test authoring (see Deliverable 0).

### Risks & Spikes

- **RISK**: Per-frame `getAgentPositions()` copy of up to 10,000 `AgentState` structs may
  cause frame-time spikes. **Spike**: profile the copy at 10,000 agents on a 1024×1024 map
  before committing to the value-type vector approach; if copy cost exceeds 1 ms at 60 FPS,
  investigate a double-buffer or dirty-flag strategy that only returns moved agents.
- **RISK**: Service coverage wireframe circle for a 800 m Fire Station radius will span many
  terrain chunks at varying heights. **Spike**: prototype the circle overlay as a flat-terrain
  screenspace outline first; if terrain height variation causes visible z-fighting, switch to
  a world-space tile-step polygon offset with `PolyOffset_Factor=−1`.
- **RISK**: Billboard re-bake at −45° pitch with reworked LOD0 meshes may introduce imposter
  mismatch visible at the LOD1→LOD2 transition distance (100 m / 90 m). **Spike**: render a
  comparison pair (reworked LOD1 vs. re-baked billboard) at the 90 m switch-in distance under
  `xvfb-run` integration test; confirm no obvious silhouette discontinuity before artist
  signs off.
- **RISK**: Adding four new `ICitySimulation` query methods (`getAgentPositions`,
  `getIntersectionSignalStates`, `getRoadSegmentSpeeds`, `getServiceCoverage`) requires
  updating `MockCitySimulation` in `tests/simulation/MockCitySimulation.h`. Failure to
  update the mock before writing tests causes link errors. **Spike**: add all four
  `MOCK_METHOD` declarations to `MockCitySimulation` as the very first commit of this phase,
  before any other deliverable.
