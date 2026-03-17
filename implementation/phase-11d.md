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

This commit must land BEFORE any test files for Deliverables 3a, 3d, 4c, 5c, or 5e are written.

- [x] Define in `src/interfaces/IRenderer.h`: the six new pure-virtual method signatures
  (exact signatures binding for Day-One Commit). `IRenderer.h` must `#include "simulation_types.h"`
  to access `AgentHandle`, `SignalPhase`, and `ServiceBuildingType` — do NOT re-define
  `AgentHandle` in `IRenderer.h`; it is defined exactly once in `simulation_types.h` (see next
  item) to avoid ODR violations when both headers appear in the same translation unit.
  `virtual void spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ, ZoneType zone) = 0;`,
  `virtual void moveVehicleAgent(AgentHandle handle, int tileX, int tileZ, float headingDeg) = 0;`,
  `virtual void despawnVehicleAgent(AgentHandle handle) = 0;`,
  `virtual void setIntersectionSignalState(int tileX, int tileZ, SignalPhase phase) = 0;`,
  `virtual void showServiceCoverageOverlay(int tileX, int tileZ, ServiceBuildingType type, bool degraded) = 0;`,
  `virtual void hideServiceCoverageOverlay() = 0;`.
  (Note: `getListenerPosition` was added in Phase 10; verify it is already present in
  `IRenderer.h` — do not re-add.)
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/game-design/traffic-system.md`, `architecture/game-design/service-coverage.md`)

- [x] Define in `src/interfaces/simulation_types.h` all 6 new types required for Phase 11d query
  methods: `using AgentHandle = uint32_t;` (type alias), `enum class SignalPhase { Green, Red };`,
  `struct AgentState { uint32_t agentId; int tileX; int tileZ; float headingDeg; ZoneType zone; };`,
  `struct IntersectionSignalState { int tileX; int tileZ; SignalPhase phase; };`,
  `struct RoadSegmentSpeed { int tileX; int tileZ; float speedFraction; };`,
  `struct ServiceCoverageTile { int tileX; int tileZ; ServiceBuildingType coveredBy; bool degraded; };`.
  All six must be present in the Day-One Commit.
  (ref: `architecture/game-design/traffic-system.md`,
  `architecture/game-design/service-coverage.md`)

- [x] **Extend `QueryResult` struct** — add `bool degraded{false};` to `src/interfaces/simulation_types.h`
  immediately after `ServiceBuildingType serviceType{ServiceBuildingType::None};`, as required by
  `architecture/ui-ux/query-inspector-panel.md` §Service building tile detection and by
  Deliverable 4a (which passes `QueryResult::degraded` to
  `IRenderer::showServiceCoverageOverlay(tileX, tileZ, serviceType, degraded)`). This must land
  in the Day-One Commit — Deliverable 4a integration code cannot compile without this field.
  (ref: `architecture/ui-ux/query-inspector-panel.md`,
  `architecture/game-design/service-coverage.md`)

- [x] Extend `IAudioSystem` in `src/interfaces/IAudioSystem.h` with three new pure-virtual methods
  required by Deliverable 3a's vehicle engine audio pair wiring:
  `virtual std::pair<int,int> acquireVehicleEnginePair(ZoneType zone) = 0;`
  (returns `{-1,-1}` if the vehicle pair pool is exhausted — callers MUST check before use;
  indices are opaque SFX pool source indices, not `ALuint` — `IAudioSystem` must not expose
  OpenAL types per `architecture/audio-architecture/audio-system.md`) and
  `virtual void releaseVehicleEnginePair(int idleIdx, int moveIdx) = 0;` and
  `virtual void updateVehicleAudio(int idleIdx, int moveIdx, float speedFraction, float worldX, float worldZ) = 0;`
  (called per active vehicle per render frame by `main.cpp` inside the per-frame agent sync
  loop, after `getAgentPositions()` and before `drawScene()`, to push speed fraction and world
  position so `AudioSystem` can update `AL_PITCH`, `AL_GAIN` crossblend, and `AL_POSITION` on
  the audio thread — see `architecture/audio-architecture/audio-system.md` §updateVehicleAudio).
  Update `MockAudioSystem` with `MOCK_METHOD` declarations for all three new methods.
  Also update the method-count comment in `tests/simulation/MockAudioSystem.h` from 15 to 18 to match the updated `audio-system.md` spec.
  Also update the method-count comment in `src/interfaces/IAudioSystem.h` from 15 to 18 to keep interface and mock documentation in sync.
  This must land in the Day-One Commit so that Deliverable 3a test authoring can begin.
  (ref: `architecture/audio-architecture/audio-system.md`,
  `architecture/audio-architecture/source-pool.md`,
  `architecture/testing/testability-architecture.md`)

- [x] Extend `ICitySimulation` with four new pure-virtual query methods: `getAgentPositions`,
  `getIntersectionSignalStates`, `getRoadSegmentSpeeds`, `getServiceCoverage`.
  (ref: `architecture/game-design/traffic-system.md`,
  `architecture/game-design/service-coverage.md`)

- [x] Update `MockCitySimulation` with `MOCK_METHOD` declarations for all four new query
  methods (`getAgentPositions`, `getIntersectionSignalStates`, `getRoadSegmentSpeeds`,
  `getServiceCoverage`). This is a prerequisite for all test authoring in Deliverables 3d
  and 4c.
  (ref: `architecture/testing/testability-architecture.md`)

- [x] Extend `MockRenderer` with `MOCK_METHOD` stubs for all six new `IRenderer` methods:
  `spawnVehicleAgent`, `moveVehicleAgent`, `despawnVehicleAgent`,
  `setIntersectionSignalState`, `showServiceCoverageOverlay`, `hideServiceCoverageOverlay`.
  This is a prerequisite for all test authoring in Deliverables 3d and 4c.
  Note: `spawnVehicleAgent`, `moveVehicleAgent`, and `despawnVehicleAgent` are NEW additive
  `IRenderer` methods; they coexist with Phase 10's `placeVehicle` / `moveVehicle` /
  `removeVehicle` and do NOT replace them. Both sets of methods must be present in
  `MockRenderer` after the Day-One Commit.
  (ref: `architecture/testing/testability-architecture.md`)

- [x] Extend the **existing** `IRenderer::setTilePlacementPreview` signature in
  `src/interfaces/IRenderer.h` from `(tiles, argb)` to
  `(freeTiles, freeArgb, blockedTiles = {})` — this change MUST land in the Day-One Commit
  atomically with the MockRenderer update below, because `MockRenderer` inherits from
  `IRenderer` and uses `override`; if the base signature does not match, compilation fails.
  (Full implementation of the two-list rendering logic is in Deliverable 5d — only the
  signature change lands here; the implementation body may simply ignore `blockedTiles` until
  Deliverable 5d.)

- [x] Update `MockRenderer` with the new two-list `setTilePlacementPreview` signature
  (`freeTiles`, `freeArgb`, `blockedTiles = {}`) — this must land in the Day-One Commit so
  that Deliverable 5e test files compile from the start.
  (ref: `architecture/testing/testability-architecture.md`)

---

#### 1. 3D Model Detail Rework

All building and vehicle models authored in Phase 9 are reworked to reach the upper end of
the polygon budgets defined in `architecture/asset-standards/3d-model-standards.md`. Phase 9
established valid-but-minimal geometry; this phase adds architectural detail that makes the
city feel lived-in at close and mid range. **Each zone type (Residential, Commercial,
Industrial) must have a visually distinct silhouette and geometry vocabulary — a player must
be able to identify the zone type from mesh shape alone, without colour or texture cues.**

##### 1a. Building Model Rework

**Floor count per tier (mandatory for all building sets)**:

- Small buildings (`height_floors <= 3`): Low tier variants must use `height_floors` 1 or
  2; Medium tier variants must use `height_floors` 2 or 3. The four variants within a tier
  may use different values within their tier range — this height difference is the primary
  silhouette-variation tool between same-tier variants (e.g. `res_low_01` at 1 floor,
  `res_low_02` at 2 floors, `res_low_03` at 1 floor with a different footprint,
  `res_low_04` at 2 floors with a different roof form). The `height_floors` value in each
  variant's `.meta` file must match the modelled geometry floor count exactly.
- Large buildings (`height_floors >= 4`): Residential High and Industrial High variants must
  use `height_floors` in the range 5–10. The four variants within each of these High-tier
  slots must span at least a 3-floor range (e.g. `res_high_01` at 5 floors, `res_high_02`
  at 7 floors, `res_high_03` at 8 floors, `res_high_04` at 10 floors) to produce readable
  skyline height variation between adjacent buildings. No two variants within the same slot
  may share the same `height_floors` value.
  **Exception — Commercial High** (`com_high_01–04`): these are tall glass skyscrapers;
  `height_floors` must be in the range 15–30 per the binding decision for this phase. The
  four variants must span at least a 10-floor range within that window (e.g. `com_high_01`
  at 15 floors, `com_high_02` at 20 floors, `com_high_03` at 25 floors, `com_high_04` at
  30 floors). No two `com_high` variants may share the same `height_floors` value.

- [x] **Large-building LOD0 geometry** — re-export all Large building variants
  (`res_high_01`, `res_high_02`, `res_high_03`, `res_high_04`,
  `com_high_01`, `com_high_02`, `com_high_03`, `com_high_04`,
  `ind_high_01`, `ind_high_02`, `ind_high_03`, `ind_high_04`)
  targeting **6,000–8,000 tris** for Residential High and Industrial High
  (spec upper: 4,000–8,000 tris per `architecture/asset-standards/3d-model-standards.md`
  §LOD Requirements; Phase 11d targets the upper 75% of the spec range).
  Zone-type geometry requirements are **mandatory** — each zone must have a distinct
  architectural vocabulary:
  - **Residential High** (`res_high_01/02/03/04`): projecting balcony slabs on every
    2–3 floors (20–40 cm slab overhang as inset geometry, not painted), recessed window
    bays with frame reveals, planted parapet geometry at rooftop, stairwell tower stub at
    rear elevation. Rooftop equipment silhouettes must include discrete AC condenser box
    geometry (boxy units, ~0.6 m tall, staggered placement) so the roofline reads as
    inhabited — not a bare slab.
  - **Commercial High** (`com_high_01/02/03/04`) — tall glass skyscrapers
    (`height_floors` 15–30), targeting **8,000–10,000 tris**: these are landmark
    buildings; budget and detail must reflect that. Four distinct form vocabularies are
    required — one per variant:
    - `com_high_01`: narrow glass tower with a spire crown (slender rectangular shaft
      tapering to a spire pinnacle at rooftop; floor plate consistent throughout height)
    - `com_high_02`: wide slab with setback upper floors and an antenna cluster crown
      (lower 60% is a broad rectangular slab; upper 40% steps back on at least two sides;
      antenna cluster of 3–5 vertical rods of varying heights at the roof centre)
    - `com_high_03`: tapered pyramid form with chamfered corners (floor plate reduces
      uniformly from base to crown, each floor stepping inward ~0.3–0.5 m; all four
      vertical corners are chamfered throughout the full height)
    - `com_high_04`: stepped ziggurat with floor-plate reductions every 3–4 floors
      (distinct horizontal ledge at every setback step, min 4 step levels visible from
      ground to crown)
    - All four `com_high` variants must have: a unique crown treatment (distinguishable
      by silhouette from skyline distance); ground floor grand entrance lobby canopy
      geometry (projecting flat canopy slab, min 4 m wide × 1.5 m deep), multi-bay
      revolving door recess (min 3 door bays, each min 1.2 m wide × 2.2 m tall,
      recessed min 0.4 m), podium base geometry (a wider base volume, min 1.5 m taller
      than street level, set back from the tower shaft above); facade floor-to-ceiling
      curtain-wall mullion grid throughout the full height (thin vertical and horizontal
      extrusions, not painted lines), expressed structural core visible on the exterior
      (a thickened central or corner volume carrying vertical columns proud of the
      curtain wall face by min 5 cm)
  - **Industrial High** (`ind_high_01/02/03/04`): flat-top roofline with rooftop
    plant-room volume (a distinct secondary box set back from the parapet), pipe/duct
    stub geometry on the upper facade, clerestory-band recess geometry near the roofline,
    stair-tower extrusion on at least one elevation.
  All three zones must also include: setback modelling at each floor band and rooftop
  equipment silhouettes (AC units, antennae stubs).
  **Inter-variant differentiation is mandatory for Large buildings**: the four variants
  within each zone type must each differ from every other variant in at least one of:
  footprint aspect ratio (narrow-tower vs. wider-slab massing), floor-band setback count,
  rooftop silhouette (e.g. for Residential: single stair-tower vs. dual stair-tower vs.
  planted parapet vs. recessed equipment deck; for Industrial: single plant-room box vs.
  two offset boxes vs. sawtooth monitor roof vs. perimeter parapet with duct cluster). A
  city block containing all four variants must not contain any two buildings that look alike
  when viewed from 60 m.
  **Building atlas note**: the atlas layout is UNCHANGED — all four variants of the same
  zone-tier share the same wall-module atlas cell (the binding decision from Phase 1).
  Only mesh geometry and UV placement within the shared cell differ between variants.
  This is consistent with `architecture/asset-standards/building-atlas-layout.md`.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [x] **Large-building LOD1 geometry** — re-export all Large building LOD1 meshes
  (all 12 variants: `res_high_01/02/03/04`, `com_high_01/02/03/04`, `ind_high_01/02/03/04`)
  targeting **1,000–1,500 tris** for Residential High and Industrial High, and
  **1,500–2,000 tris** for Commercial High skyscrapers (reflecting the higher LOD0 budgets).
  LOD1 must retain all zone-defining silhouette features at simplified fidelity: balcony slab
  extrusion profile (single flat slab per floor band, no railing geometry) for Residential,
  chamfered or recessed corner volume plus the variant-specific crown silhouette for
  Commercial (the spire, antenna cluster, tapered top, or ziggurat steps must still be
  readable at LOD1 polygon count), rooftop plant-room box for Industrial. Per-window insets
  and curtain-wall mullion grids are baked to texture, not modelled at LOD1. **Height
  variation among the four variants must be preserved**: the floor-count spread across all
  four variants within each zone type must still produce visible height variation at LOD1 —
  the tallest variant must produce a noticeably taller LOD1 box than the shortest variant;
  no two variants within the same zone-tier slot may reduce to the same-height box.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [x] **Small-building LOD0 geometry** — re-export all Small building variants
  (`res_low_01`, `res_low_02`, `res_low_03`, `res_low_04`,
  `com_low_01`, `com_low_02`, `com_low_03`, `com_low_04`,
  `ind_low_01`, `ind_low_02`, `ind_low_03`, `ind_low_04`,
  `res_med_01`, `res_med_02`, `res_med_03`, `res_med_04`,
  `com_med_01`, `com_med_02`, `com_med_03`, `com_med_04`,
  `ind_med_01`, `ind_med_02`, `ind_med_03`, `ind_med_04`)
  targeting the **upper** LOD0 tri range: **2,000–3,000 tris** (spec: 1,500–3,000 tris per
  `architecture/asset-standards/3d-model-standards.md` §LOD Requirements; Phase 11d targets
  the upper portion of the budget to reach production detail for small buildings).
  Zone-type geometry requirements are **mandatory**:
  - **Residential Low/Med** (`res_low_*`, `res_med_*`): pitched roof geometry (gabled or
    hipped ridge line — not a flat slab), chimney stub, bay window outcrop on the principal
    elevation, front door step and small porch canopy geometry. At least one window opening
    per principal elevation must include a narrow window-box ledge geometry (a 5–8 cm
    horizontal shelf proud of the wall face) to read as a flower-box or sill planter when
    the texture is applied — this is the primary "lived-in" cue at close range.
  - **Commercial Low/Med** (`com_low_*`, `com_med_*`): flat roof with parapet, wide
    display-window recess on the ground floor (storefront inset geometry, not painted),
    overhead awning frame geometry above the storefront, signage band fascia geometry
    between the awning and the first floor. The awning frame must be modelled as a
    projecting bracket-and-valance profile (not a flush plane) so that awning colour
    painted in the atlas reads as a distinct canopy element at street level.
  - **Industrial Low** (`ind_low_*`): mono-pitch single-span shed roof (one ridge running
    the length of the building — the lower end faces the street), loading dock recess on at
    least one elevation (recessed bay, min 0.5 m depth), corrugated wall panel rib geometry
    (min 8 parallel extrusions across the principal facade plane), clerestory-strip window
    geometry near the roofline (a continuous horizontal slot, not individual windows).
  - **Industrial Med** (`ind_med_*`): multi-bay shed with sawtooth profile (minimum two
    parallel ridges visible in silhouette — the stepped roofline is the primary identifier
    that distinguishes Med from Low), loading dock on at least one elevation, corrugated
    panel ribs, and a taller clerestory band between sawtooth bays. At least one elevation
    must also carry an external stair or access-ladder stub and ventilation cowl geometry on
    a roof ridge (min 2 cowl stubs).
  **Building atlas note**: the atlas layout is UNCHANGED — all four variants of the same
  zone-tier share the same wall-module atlas cell (the binding decision from Phase 1).
  Only mesh geometry and UV placement within the shared cell differ between variants.
  This is consistent with `architecture/asset-standards/building-atlas-layout.md`.
  **Inter-variant differentiation is mandatory for Small buildings**: the four variants
  within each zone/tier must each differ from every other variant in at least one of:
  footprint aspect ratio, roof form (gabled vs. hipped for Residential Low/Med;
  flat-parapet height vs. mansard step for Commercial Low/Med; mono-pitch roof direction
  for Industrial Low; 2-bay vs. 3-bay vs. 4-bay sawtooth for Industrial Med), principal-
  facade window count or arrangement, or secondary-elevation treatment (blank wall vs. side
  entrance detail). A player zoomed to street-level view must be able to distinguish any
  two variants within the same zone/tier without reading asset names.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [x] **Small-building LOD1 geometry** — re-export all Small building LOD1 meshes
  (all 24 variants: `res_low/med_01–04`, `com_low/med_01–04`, `ind_low/med_01–04`)
  targeting **300–400 tris** (spec: 200–400 tris; Phase 11d raises the floor to 300 tris
  minimum to preserve distinguishable silhouettes across all four variants at LOD1 switch-in
  distance). LOD1 must preserve the overall building envelope, the primary facade plane, and
  the zone-defining silhouette feature of each zone type: pitched roof ridge line for
  Residential (the ridge must remain as a distinct edge — not collapsed to a flat top),
  storefront parapet and single awning extrusion for Commercial, and mono-pitch or sawtooth
  roof ridge count for Industrial (a sawtooth Med variant must retain at least two visible
  ridges at LOD1). Per-window detail, corrugated panel ribs, and canopy bracket geometry are
  baked into the texture, not modelled at LOD1. **Variant silhouette must be preserved
  across all four variants**: within each zone-tier slot, no two of the four LOD1 meshes
  may reduce to the same-height box — the height or roof-form distinction established at
  LOD0 must still be readable at LOD1 polygon count.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [x] **Service building LOD0 geometry** — re-export all four service building models
  (one unique model per type, no variants:
  `svc_fire_station_lod0.b3d`, `svc_police_station_lod0.b3d`,
  `svc_power_plant_lod0.b3d`, `svc_water_tower_lod0.b3d`) targeting **2,500–4,000 tris**
  (spec: 2,000–4,000 tris; Phase 11d raises the minimum to 2,500 to ensure distinguishable
  landmark silhouettes — below 2,500 tris service buildings lose the proportional mass and
  architectural detail that lets players distinguish them from zone buildings at mid-range.
  Small-building LOD thresholds apply; see
  `architecture/asset-standards/3d-model-standards.md`; service buildings use small-building
  LOD distance thresholds: LOD0→LOD1 at >30 m / <25 m, LOD1→LOD2 at >100 m / <90 m).
  Each service building type must be **immediately identifiable by silhouette alone** at LOD0
  viewing distances — a player must be able to tell all four types apart without reading a
  label. Mandatory per-type detail requirements:
  - **Fire station** (`svc_fire_station`): two vehicle bay door openings (inset geometry,
    min 2.8 m wide × 3.2 m tall per bay, min 0.5 m depth), raised kerb or apron geometry
    in front of the bays, hose reel housing stub on an exterior side wall (a small box
    volume, approx 0.5 m × 0.3 m × 0.8 m), personnel entrance door recess distinct from
    the bay openings, antenna/radio mast on the roofline (min 2.5 m above parapet).
  - **Police station** (`svc_police_station`): solid masonry-style volume with recessed
    window openings (no large glazed curtain-wall spans), covered vehicle bay recess on
    one side elevation (single bay, 0.4 m depth), entrance canopy geometry over the main
    door (projecting flat slab, min 1.2 m × 0.8 m footprint), communications antenna
    cluster on the roofline (min 2 antenna stubs at different heights).
  - **Power plant** (`svc_power_plant`): transformer/switchgear secondary box volume
    adjacent to the main building (distinct footprint, min 2.5 m × 2.5 m × 1.8 m),
    exhaust stack geometry (min 4.5 m above roofline, cylindrical or rectangular),
    three intake/exhaust duct stubs on the principal facade (approx 20 cm square
    section, projecting min 0.4 m), recessed loading door opening on one elevation.
  - **Water tower** (`svc_water_tower`): elevated cylindrical tank volume (tank bottom
    min 4 m above ground, tank diameter min 3 m, tank height min 2 m), four support
    legs with visible diagonal cross-bracing geometry, inlet/outlet pipe stub at the
    tank base (one pipe, approx 15 cm diameter, descending from the tank floor),
    access ladder geometry on one support leg, dome or flat cap geometry on the tank top.
  UV channel 0 must remain mapped to the `service_buildings` atlas cell (row 3, col 2
  of the 2048×2048 building atlas) per `architecture/asset-standards/building-atlas-layout.md`.
  (ref: `architecture/asset-standards/3d-model-standards.md`,
  `architecture/asset-standards/building-atlas-layout.md`)

- [x] **Service building LOD1 geometry** — re-export all four service building LOD1 meshes
  (one unique model per type, no variants:
  `svc_fire_station_lod1.b3d`, `svc_police_station_lod1.b3d`,
  `svc_power_plant_lod1.b3d`, `svc_water_tower_lod1.b3d`) targeting **200–400 tris**
  (spec: 200–400 tris; targeting the full spec range — below 200 tris a two-storey service
  building collapses to a featureless box, and the water tower loses its elevated tank
  silhouette entirely, making it indistinguishable from the police station at LOD1
  switch-in distance). LOD1 must retain
  the single most-distinctive silhouette feature of each service type: garage bay volumes for
  the fire station (simplified to flat inset quads), elevated tank + simplified support leg
  profile for the water tower (four legs as flat fins, no cross-bracing), exhaust stack stub
  for the power plant (extruded box, no pipe detail), and solid masonry block outline for the
  police station. Hose reels, pipe stubs, cross-bracing detail, and canopy geometry are
  removed or baked to texture at LOD1.
  (ref: `architecture/asset-standards/3d-model-standards.md` §LOD Requirements)

- [x] **Full asset inventory verification** — confirm the following complete set of building
  `.b3d` files exists after all rework exports:
  - **Zone building LOD0**: 36 files (4 variants × 9 zone-tiers:
    `res_low_01–04`, `res_med_01–04`, `res_high_01–04`,
    `com_low_01–04`, `com_med_01–04`, `com_high_01–04`,
    `ind_low_01–04`, `ind_med_01–04`, `ind_high_01–04` — all suffixed `_lod0.b3d`)
  - **Zone building LOD1**: 36 files (same 36 variants, suffixed `_lod1.b3d`)
  - **Zone building LOD2** — split by `category` in `.meta` (per `architecture/asset-standards/3d-model-standards.md` §V1 Minimum Building Coverage and §BuildingAssetLoader LOD Loading Contract):
    - *Geometry shells (`_lod2.b3d`) — High-density only*: 12 files
      (`res_high_01–04_lod2.b3d`, `com_high_01–04_lod2.b3d`, `ind_high_01–04_lod2.b3d`)
      Reason: `category: large_building`, `height_floors >= 4` → geometry shell at LOD2.
    - *Billboard atlas DDS (`_billboard.dds`) — Low and Med density only*: 24 files
      (`res_low_01–04_billboard.dds`, `res_med_01–04_billboard.dds`,
      `com_low_01–04_billboard.dds`, `com_med_01–04_billboard.dds`,
      `ind_low_01–04_billboard.dds`, `ind_med_01–04_billboard.dds`)
      Reason: `category: small_building`, `height_floors <= 3` → billboard imposter at LOD2.
      High-density variants do NOT have `_billboard.dds`.
  - **Service building LOD0**: 4 files (one per type, no variants):
    `svc_fire_station_lod0.b3d`, `svc_police_station_lod0.b3d`,
    `svc_power_plant_lod0.b3d`, `svc_water_tower_lod0.b3d`
  - **Service building LOD1**: 4 files (one per type):
    `svc_fire_station_lod1.b3d`, `svc_police_station_lod1.b3d`,
    `svc_power_plant_lod1.b3d`, `svc_water_tower_lod1.b3d`
  - **Service building LOD2** (billboard imposter, `height_floors = 2 <= 3`): 4 files
    (`svc_fire_station_billboard.dds`, `svc_police_station_billboard.dds`,
    `svc_power_plant_billboard.dds`, `svc_water_tower_billboard.dds`)
  - **Total .b3d files (buildings)**: 36 (zone LOD0) + 36 (zone LOD1) + 12 (zone LOD2 High geometry shells) + 4 (svc LOD0) + 4 (svc LOD1) = **92 building .b3d files**
  - **Billboard atlas DDS files**: 24 (zone Low/Med) + 4 (service) = **28 billboard DDS files**
  The `validate_assets.py` expected-file-count check (established in Phase 5) must be
  updated to reflect 92 `.b3d` files and 28 billboard DDS files before the
  regression-clean step is run.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

- [x] **Collision mesh re-verification** — after reworking all building LOD0 geometry,
  re-verify that each asset's `_col.obj` (or `_col_0/1/2.obj` / `_col_circle.obj`)
  accurately covers the actual XZ footprint of the reworked mesh. If the rework added
  projecting geometry that extends beyond the Phase 9 footprint (e.g. a loading dock recess
  now protrudes further on the facade plane, a canopy now overhangs the tile boundary, or the
  water tower support legs now extend outside the original collision hull), update the
  collision mesh to match. Only collision meshes whose covered footprint changed require
  re-export. All updated `_col.obj` files must re-pass `validate_assets.py` collision mesh
  checks (max 24 tris, no top/bottom caps, flat at Y=0).
  (ref: `architecture/asset-standards/3d-model-standards.md` §Collision Meshes)

- [x] **LOD distance thresholds unchanged**: LOD swap distances (Large buildings: LOD0→LOD1
  at >50 m / <45 m; LOD1→LOD2 at >200 m / <185 m; Small: >30 m / <25 m; >100 m / <90 m)
  are defined in `architecture/asset-standards/3d-model-standards.md` and must NOT be
  altered. Only mesh detail within each LOD level changes in this phase.
  (ref: `architecture/asset-standards/3d-model-standards.md` §LOD Distance Thresholds)

- [x] **`validate_assets.py` regression-clean**: after re-export, confirm all reworked
  building models pass the existing `validate_assets.py` 24-check suite (established in
  Phase 5 and extended through Phase 10b) with zero errors. The `validate-assets` CI job
  must remain green.
  (ref: `architecture/ci-cd/github-actions-workflow.md`)

- [x] **`graphics-artist-3d-model` sign-off gate** (blocking): before committing any reworked
  `.b3d` file, `graphics-artist-3d-model` must verify: (a) tri counts are within the upper
  LOD0/LOD1 ranges stated above, (b) coordinate system is Y-up Z-forward left-handed
  (Irrlicht convention); Blender export axis confirmed as **-Z Forward, Y Up** (NOT Y Forward,
  Z Up — wrong setting produces Z-up assets rotated 90° in Irrlicht;
  ref: `architecture/asset-standards/3d-model-standards.md` §Coordinate System Export
  Convention), (c) pivot is at the building's ground-centre footprint at exactly
  Y=0 (tolerance: 5 mm per spec), (d) UV channel 0 maps into the correct building atlas cell
  with no UV islands crossing the 8 px border into adjacent cells, (e) UV channel 1 (lightmap)
  is present and non-overlapping on all LOD0 and LOD1 meshes, (f) minimum 5 mm gap between UV
  islands on UV channel 0 AND UV channel 1 (both channels must satisfy the gap requirement —
  per `architecture/asset-standards/3d-model-standards.md` §UV Authoring),
  **(g) zone-type silhouette distinction confirmed**: the three zone-type geometry vocabularies
  (pitched roof + porch for Residential small; storefront recess + awning for Commercial small;
  loading dock + corrugated ribs for Industrial small; balcony slabs for Residential high;
  curtain-wall mullions for Commercial high; rooftop plant box + duct stubs for Industrial high)
  are visually distinct in the exported mesh with no zone-type overlap — a viewer looking at
  the untextured grey mesh can unambiguously identify the zone type,
  **(h) inter-variant differentiation confirmed**: for every zone-type/tier pair, the four
  variants (`_01`, `_02`, `_03`, `_04`) each differ from every other variant in at least one
  of the structural dimensions listed in the Large-building and Small-building inter-variant
  differentiation requirements above — a side-by-side screenshot of all four untextured
  variants at 60 m camera distance must show clearly different massing with no two variants
  looking alike,
  **(i) service building inter-type silhouette distinction confirmed**: all four service
  building types are identifiable by silhouette alone in an untextured grey viewport — a
  viewer can unambiguously distinguish fire station (two bay openings), water tower (elevated
  tank on legs), power plant (exhaust stack + secondary transformer box), and police station
  (solid block with small windows and canopy),
  **(j) lived-in geometry present**: Residential Low/Med variants include window-box ledge
  geometry (min one horizontal shelf per principal elevation); Commercial Low/Med variants
  include bracket-and-valance awning profile (not a flush plane); Residential High includes
  discrete AC condenser box geometry on the rooftop (min two boxes).
  The sign-off is recorded as a commit-message annotation on the first reworked-model commit.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

##### 1b. Vehicle Model Rework

**Vehicle subtype silhouette distinction (mandatory)**: All five V1 vehicle types must be
immediately distinguishable from each other by silhouette alone at LOD0 viewing distances.
In particular: `car_sedan`, `car_hatchback`, and `car_suv` are different model classes and
must NOT share the same mesh — each must have a distinct body shape: sedan has a pronounced
boot/trunk step behind the C-pillar; hatchback has a near-vertical rear end with no boot
step; SUV has a raised ride-height body (a taller overall profile at the same length) with
a flat roof extending to the rear end. `bus_standard` must be visually taller and longer
than any car variant. `truck_cargo` must have a distinct cab-plus-cargo-box silhouette with
a flat vertical rear face. A test viewer must be unable to confuse any two of the five types
when viewed from above at 40 m.

- [x] **Car LOD0 geometry** — re-export all three car variants (`car_sedan`, `car_hatchback`,
  `car_suv`) each targeting **1,800–2,000 tris** (binding decision for Phase 11d; note the
  spec table in `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget
  lists ≤2,000 tris as the general limit — Phase 11d targets the spec ceiling for production
  detail). Each car
  subtype must have a distinct body silhouette (see subtype distinction requirement above).
  Mandatory added detail for all car variants: door panel crease lines (inset edge loops along
  door boundaries), wheel arch opening geometry (arched cut-outs, not rectangular), bumper
  corner rounding (chamfered front/rear bumper ends), side mirror body geometry (two mirrored
  stubs with backing plate, no glass detail), windscreen and rear glass as separate flat quad
  fills within the body opening (dark tinted — not modelled separately from the body but
  recessed min 3 cm from the outer body plane). Exhaust pipe stub on the sedan and hatchback
  (one round stub, approx 6 cm diameter, at the rear valance). Roof rack rails on the SUV
  (two longitudinal bars, approx 4 cm wide, running the length of the roof). Pivot at
  bottom-center of the vehicle footprint at Y=0 (Irrlicht convention, same as LOD0/LOD1).
  UV channel 0 must map into the vehicle's assigned atlas cell in `vehicles_diffuse_atlas_d`
  with no UV islands outside the cell bounds.
  (ref: `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget)

- [x] **Bus and truck LOD0 geometry** — re-export bus (`bus_standard`) and truck
  (`truck_cargo`) models targeting **2,500–3,000 tris** (binding decision for Phase 11d;
  note the spec table in `architecture/asset-standards/3d-model-standards.md` §Vehicle
  Polygon Budget lists ≤3,000 tris as the general limit — Phase 11d targets the spec
  ceiling for production detail).
  Mandatory added detail per type:
  - **Bus** (`bus_standard`): destination blind recess panel on the front face (an inset
    flat surface above the windscreen, min 80 cm wide × 25 cm tall), folding door frame
    geometry on the nearside (recessed door step geometry, min 0.6 m depth), wheel arch
    skirt geometry (lower side panel connecting wheel arches to the underside), roof-
    mounted air conditioning box (a flat box on the roof, approx 1.5 m × 0.6 m × 0.4 m),
    rear engine vent louvres (a recessed grille panel, min 6 horizontal louvre bars).
  - **Truck** (`truck_cargo`): cab front grille geometry (a recessed rectangular panel with
    min 4 horizontal bars), cab exhaust stack stub (one vertical stack on the cab roof,
    approx 12 cm diameter × 60 cm height), cargo box rear door panel inset (recessed door
    frame on the cargo box rear face, min 6 cm depth), side step geometry below the cab
    door (a small projecting step box), cab rear wall panel that separates cab from cargo
    box (the gap between cab back and box front as a distinct recessed volume).
  Pivot at bottom-center of each vehicle's footprint at Y=0. UV channel 0 must map into
  the vehicle's assigned atlas cell.
  (ref: `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget)

- [x] **Vehicle LOD1 geometry** — re-export all five vehicle LOD1 meshes targeting Phase
  11d quality-floor targets: car **300–400 tris**, bus **400–500 tris**, truck **400–500
  tris** (binding decision for Phase 11d; note the spec table in
  `architecture/asset-standards/3d-model-standards.md` lists car ≤400 tris, bus ≤500 tris,
  truck ≤500 tris — Phase 11d targets the spec ceilings). **Minimum tris floor (Phase 11d
  quality gate)**: car ≥300 tris, bus ≥400 tris, truck ≥400 tris — below these floors the
  vehicle body collapses to a rectangular block with no body curvature. LOD1 retains the body silhouette and subtype shape: wheels are
  simplified to flat disc quads (4 tris per wheel), mirrors are removed, panel crease detail
  is removed, but the body outline (sedan boot step, hatchback vertical rear, SUV raised
  profile, bus destination panel inset, truck cab/box separation gap) must still be
  distinguishable from the LOD1 mesh. Pivot at Y=0, same as LOD0.
  (ref: `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget)

- [x] **`validate_assets.py` regression-clean (vehicles)**: after re-export, confirm all
  reworked vehicle models pass the existing `validate_assets.py` check suite with zero
  errors. Specifically: check #10 (UV channel 0 within assigned atlas cell), check #12
  (normal atlas UV cell), check #15 (`.meta` sidecar present), and the per-class tri budget
  binding limits in the Vehicle Polygon Budget table. The `validate-assets` CI job must
  remain green after vehicle re-exports.
  (ref: `architecture/ci-cd/github-actions-workflow.md`,
  `architecture/asset-standards/3d-model-standards.md`)

- [x] **`graphics-artist-3d-model` sign-off gate — vehicles** (blocking): before committing
  any reworked vehicle `.b3d` file, `graphics-artist-3d-model` must verify: (a) tri counts
  are within the LOD0 upper range and above the LOD1 floor stated above for each vehicle
  class, (b) coordinate system is Y-up Z-forward left-handed (Irrlicht convention); Blender
  export axis confirmed as **-Z Forward, Y Up** (NOT Y Forward, Z Up — wrong setting produces
  Z-up assets rotated 90° in Irrlicht; ref: `architecture/asset-standards/3d-model-standards.md`
  §Coordinate System Export Convention), (c) pivot is at the vehicle's bottom-center footprint
  at exactly Y=0 (tolerance: 5 mm),
  (d) UV channel 0 maps into the correct vehicle atlas cell with no UV islands outside the
  cell boundary, (e) all five vehicle subtypes are visually distinct from each other in an
  untextured grey viewport at 40 m — the three car subtypes (sedan, hatchback, SUV) have
  distinct body outlines, and bus and truck are clearly larger and taller than the car
  variants, (f) mandatory per-type detail features are present in the exported mesh (door
  arches, mirrors, panel creases for cars; destination blind recess and roof AC box for bus;
  grille and exhaust stack for truck), (g) LOD1 body silhouette still distinguishes subtypes
  (sedan boot step vs hatchback vertical rear vs SUV raised profile visible at LOD1 polygon
  count).
  The sign-off is recorded as a commit-message annotation on the first reworked vehicle
  model commit.
  (ref: `architecture/asset-standards/3d-model-standards.md`)

---

#### 2. Texture Detail Rework

All building and vehicle textures authored in Phase 9 are reworked to production detail
quality. Phase 9 established correct format and UV mapping; this phase replaces the
placeholder paint-over content with hand-authored or baked detail.

##### 2a. Building Facade Atlas Rework

- [x] **`buildings_atlas_d.png` (source PNG, 2048×2048)** — re-author all 15 assigned atlas
  cells (per `architecture/asset-standards/building-atlas-layout.md` Cell Assignment Table)
  to production quality within the 496×496 px per-cell usable area (8 px border on each edge
  per spec). **Each zone-type wall cell must have a materially distinct surface that a player
  can recognise at mid-range without geometry cues** — the three zone columns must look like
  three different building materials, not three colour tints of the same texture.

  **Texel density**: each 496×496 px wall cell represents approximately 4 m × 4 m of facade
  at the standard wall-tile UV scale, giving ~124 px/m. All repeating surface features must
  be scaled to this density: a standard 75 mm brick course occupies 9–10 px; a curtain-wall
  glazing bay of ~1.5 m width occupies ~186 px; a corrugation rib pitch of ~50 mm occupies
  ~6–7 px. Features scaled significantly outside these ranges will appear either microscopic
  or poster-sized in-game and constitute a quality failure requiring rework before commit.

  **Density-tier grunge budget** (applies to all three zone columns):

  - Low-density row (row 0): 30–40 % of cell area covered by stain, rust, or wear marks
  - Med-density row (row 1): 15–25 % grunge coverage; one period detail feature per cell
  - High-density row (row 2): 5–10 % grunge coverage; contemporary material; crisp edges

  Required material, colour palette, and minimum feature counts per cell type:

  - **Residential wall cells** (rows 0–2, col 0 — `wall_residential_low/med/high`):
    **Warm colour temperature** — brick face hues sRGB (170–200, 80–110, 60–85)
    (terracotta-red to buff-orange); mortar joints 10–15 % lighter than brick face.
    Minimum features: mortar joints at 9–10 px vertical pitch tiling seamlessly across
    the full 496 px width with no visible repeat seam at the tile boundary; minimum 3
    distinct efflorescence or mortar-stain marks per 128×128 px sub-region in the low
    row. Window glass panels (where present): cool grey-blue sRGB (100–130, 130–155,
    160–185) with a specular hotspot (~12×6 px bright highlight) baked at the upper-right
    corner of each pane. **Material quality must progress across the three tier rows**:
    `wall_residential_low` — aged/worn brick with heavy mortar staining and paint flake
    at the base course; `wall_residential_med` — clean brick, lighter weathering, painted
    render band at the first floor; `wall_residential_high` — smooth rendered or stone-clad
    finish (brick pattern yields to flat render or pale limestone, signalling affluence).
    The three rows must be visually distinct enough that a player can identify the tier
    from the wall texture alone. **Lived-in surface cues** (baked into the diffuse, not
    requiring separate geometry): window-box sill planters (warm green/terracotta rectangle
    ~8×4 px beneath each window opening); on cells where the geometry includes a balcony
    slab, a faint laundry-line colour dash baked into the balcony underside region.

  - **Commercial wall cells** (rows 0–2, col 1 — `wall_commercial_low/med/high`):
    **Cool colour temperature** — glass panels sRGB (90–120, 120–155, 165–195) (blue-grey
    to aqua); aluminium/steel frames sRGB (160–180, 162–182, 165–185); spandrel panels
    sRGB (110–130, 112–132, 115–135). Minimum features: steel-frame grid with mullions
    ~8–12 px wide; minimum 2 complete glazing bays visible across the 496 px cell width;
    sky-gradient bake in each glass panel (top ~15 % brighter, bottom ~15 % darker over
    a ~30 px gradient). Low row: wider frame proportions, slightly warmer/yellowed
    aluminium tone; no mirrored tint. High row: add a subtle chevron or diagonal reflective
    streak (~4–6 px wide diagonal highlight band) across glass panels to suggest high-spec
    mirrored glazing. **Ground-floor accent**: `wall_commercial_low` must include a
    warm-amber or terracotta awning colour band baked into the storefront recess area;
    `wall_commercial_high` uses a cooler charcoal-grey or dark-bronze spandrel band.
    **Awning colour variety across all four variants**: low/med commercial variants
    MUST use different awning accent colours across all four variants (e.g. `_01` warm
    terracotta, `_02` deep forest-green, `_03` navy, `_04` burgundy or mustard) so that a
    commercial strip with mixed variants shows colour variety rather than a uniform repeated
    canopy. Each of the four variants must have a distinct awning accent colour. No brick or
    concrete.

  - **Industrial wall cells** (rows 0–2, col 2 — `wall_industrial_low/med/high`):
    **Neutral steel/concrete colour temperature** — corrugated steel base sRGB (155–175,
    150–170, 145–165); rust streaks sRGB (140–165, 70–95, 40–60); panel seam shadow
    sRGB (80–100, 80–100, 80–100). Commit to one material per row — do not mix
    corrugated steel and concrete within the same cell. Corrugated steel option:
    alternating highlight/shadow ribs at 6–8 px pitch (vertical orientation); minimum
    3 distinct rust streaks per 128 px horizontal span; panel seam groove every 128–160 px
    vertically; fastener bolt dots (~3 px diameter) at seam intersections, 4 bolts per
    seam segment. Concrete option: board-form grain (horizontal lines ~2 px apart);
    tie-hole dots (~4 px diameter) on a ~80 px grid; horizontal lift-joint groove every
    ~120 px; aggregate exposure patches ~10 % cell coverage. **Wear/grime progression**:
    `wall_industrial_low` — heavy rust/oil-stain 35–45 % grunge, paint-peel patches;
    `wall_industrial_med` — moderate rust, faded safety stripe; `wall_industrial_high`
    — clean coated panels <8 % rust, factory blue-grey or mid-green coat sRGB (90–110,
    110–130, 110–130). Grime must decrease visibly row 0 to row 2. No brick, no glass.

  - **Base shared cells** (row 0 col 3; row 1 col 3): stone ashlar or large-format poured
    concrete with a contact-shadow gradient (~20 px darker band) at the building foot.
    Stone option: coursed ashlar in neutral warm-grey sRGB (170–185, 165–180, 155–170)
    with visible joint lines. Concrete option: flat-grey sRGB (150–165, 150–165, 148–165)
    with subtle aggregate speckle. The chosen material must not visually conflict with
    any of the three wall types above.

  - **Roof shared cell** (row 2 col 3): modified bitumen/tar paper in neutral grey-brown
    sRGB (110–130, 105–125, 95–115); gravel-ballast specks ~8 % cell coverage; parapet
    coping cap: 1–2 px lighter-tone edge at the top row of the cell; at least two
    rectangular HVAC equipment shadow patches (~40×30 px each) placed asymmetrically
    to break visual repetition when the cell tiles across a rooftop.

  - **Facade detail cells** (row 3 cols 0–1): balcony railing — painted steel bar
    pattern, vertical balusters ~10 px spacing, warm off-white sRGB (235–245, 230–242,
    220–235) with shadow gap between bars; pilaster: limestone or cast-concrete tone
    sRGB (205–220, 200–215, 185–205) with recessed-face profile shadow; cornice:
    top-lit highlight strip (~4 px) and underside shadow strip (~6 px); window-bay reveal
    interior: warm off-white with contact-shadow at the reveal inner edge.

  - **Service buildings cell** (row 3 col 2): concrete base sRGB (145–165, 145–165,
    140–160) with craze-crack lines and staining ~15 % coverage; glass-block accent
    band sRGB (180–200, 195–215, 210–230); power plant: yellow/black hazard diagonal
    at 45°, 16 px stripe pitch; police station: blue identification band sRGB
    (30–55, 60–100, 160–200) spanning ~40 px horizontal strip; fire station: red
    identification band sRGB (180–210, 25–50, 25–50) spanning ~40 px horizontal strip;
    water tower tank body: galvanised steel sRGB (175–195, 178–198, 178–198) with
    horizontal weld-seam lines every ~48 px.

  (ref: `architecture/asset-standards/building-atlas-layout.md`,
  `architecture/asset-standards/2d-texture-standards.md`)

- [x] **`buildings_atlas_d.png` → DDS pipeline**: after re-authoring the source PNG, run
  `tools/export_textures.py` (Phase 9 deliverable) to regenerate `buildings_atlas_d.dds`
  (DXT1 sRGB, 4-mip) and `buildings_atlas_d_n.dds` (DXT5nm normal map, 4-mip). Validate
  the DDS headers per `architecture/asset-standards/2d-texture-standards.md`
  §Validating sRGB DDS Output. Both files must pass the
  `validate_assets.py` sRGB/mip-chain checks before commit.
  (ref: `architecture/asset-standards/2d-texture-standards.md` §DDS Authoring Pipeline)

- [x] **Normal map source re-author** — for each wall-cell type, produce a height-map
  source PNG in the DCC tool (Substance Painter bake, xNormal height-map bake, or
  Photoshop bump-to-normal conversion) and derive the normal-map PNG from it. **The
  height-map source PNG must be committed alongside the normal-map PNG** so it can be
  regenerated if the swizzle or DXT export pipeline changes. Per zone-type requirements:
  - **Residential** (`wall_residential_*`): mortar joint recesses — height depression of
    0.3–0.5 mm equivalent (at 124 px/m density = approximately 4–6 grey levels depression
    in an 8-bit height map on a 0–255 scale); window-sill shelf: 15–20 mm proud ledge
    (2–3 px wide highlight step at the sill top edge); header-course step above window
    reveals; low/med rows must also include brick-face surface micro-texture (±0.1 mm
    random variation across the brick face, implemented as perlin or Voronoi noise at
    ~3–4 px wavelength). The normal map must produce a visible brick-depth shadow when
    lit from a 45° side angle in a test render — a flat normal result indicates the
    height map was not applied.
  - **Commercial** (`wall_commercial_*`): curtain-wall frame step — frame face proud of
    glass surface by 15–25 mm equivalent; glass panel surface: essentially flat (height
    variation < 0.5 mm — do not apply brick or mortar depth to glass panels); spandrel
    panel: recessed 5–10 mm below the frame face to accentuate depth transition. High
    row: add a shallow chamfered edge (~2 px wide at 45°) along all mullion edges to
    catch glancing light. The normal map on a commercial cell must produce no significant
    mid-surface variation — frame edges should be the dominant relief feature.
  - **Industrial** (`wall_industrial_*`): corrugated-steel option — rib crown-to-valley
    depth 1.5–2 mm equivalent encoded in the height map; panel seam groove: 2–3 mm deep,
    4 px wide; fastener bolt dome: 1 mm proud, 3 px diameter; rust surface: micro-texture
    noise at ~2 px wavelength ±0.2 mm. Concrete option: board-form grain ±0.15 mm at
    2 px wavelength; tie-hole inset 3 mm deep, 4 px diameter; lift-joint groove 2 mm
    deep, 3 px wide.
  - **Base shared cells** and **roof cell**: stone ashlar joint recesses 2–3 mm deep
    (5–8 grey levels); gravel specks on roof: 0.5 mm proud dome per speck. Both are
    low-frequency detail; the normal map must not contain high-frequency noise.
  - **Facade detail cells** and **service buildings cell**: author relief matching the
    diffuse spec — balcony bar cross-section bevel, pilaster face projection, cornice
    undercut; hazard stripes are flat (no relief on painted stripes); glass-block grid:
    3 mm recessed joint between blocks.
  **Validation**: after exporting, perform a sphere-test render — a point light at 45°
  from upper-left must produce highlight ridges on Residential brick courses, on
  Commercial frame edges, and on Industrial corrugation crests. An inverted result
  (shadows where highlights are expected) indicates a Y-flip error; correct per the
  OpenGL-convention rule in `architecture/asset-standards/2d-texture-standards.md`
  §Normal map Y-channel convention before committing.
  Export using the DXT5nm swizzle procedure (X→alpha, Y→green, Z=0) per
  `architecture/asset-standards/2d-texture-standards.md` §DXT5nm packing.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [x] **Automated colour-variance quality gate** — add `validate_assets.py` check #28
  (building atlas diffuse minimum variance): for each of the 9 zone-type wall cells
  (rows 0–2, cols 0–2) within `buildings_atlas_d.png`, compute the standard deviation
  of pixel luminance within the 496×496 px usable area. A standard deviation below 8.0
  (0–255 scale) indicates a nearly flat placeholder fill. **Any wall cell with luminance
  stddev < 8.0 must be treated as a CI failure.** Add check #29 (normal map non-flat
  check): for each corresponding normal-map cell in the normal-map source PNG, compute
  the mean absolute deviation of the green channel. A value below 3.0 indicates a flat
  normal map with no authored surface relief. Both checks run on the source PNG (not
  the DDS) to avoid DXT1/DXT5 block-artefact noise in the measurement. Both checks
  must pass before Deliverable 2a is considered complete.
  (ref: `architecture/ci-cd/github-actions-workflow.md`)

- [x] **`graphics-artist-2d-texture` sign-off gate** (blocking): before committing any
  reworked atlas PNG or DDS, `graphics-artist-2d-texture` must verify: (a) sRGB ICC profile
  embedded in source PNG, (b) DX10 extended header present in DDS with correct DXGI_FORMAT
  (BC1_UNORM_SRGB = 72 for DXT1 atlas; BC3_UNORM_SRGB = 78 for DXT5 atlas), (c) 8 px
  border on every atlas cell respected, (d) all four mip levels present in DDS data (not
  truncated — check file size against reference table in
  `architecture/asset-standards/2d-texture-standards.md` §DDS Mip Chain Integrity), (e) no
  UV bleed across cell borders (visual inspection at LOD1 switch-in distance of 45 m for
  large buildings), **(f) zone-type texture distinction and colour temperature confirmed**:
  Residential wall cells (col 0) are identifiably brick with warm terracotta-orange palette;
  Commercial wall cells (col 1) are identifiably glass-curtain-wall with cool blue-grey
  palette; Industrial wall cells (col 2) are identifiably corrugated steel or concrete with
  neutral steel/concrete palette — no two zone columns use the same surface material
  category or the same dominant colour temperature, **(g) tier-quality progression
  confirmed**: within each zone column the low/med/high rows show a perceptible quality and
  wear change — Residential progresses from worn brick to rendered/clad finish; Industrial
  progresses from heavy rust/grime to clean cladding; rows 0 and 2 of the same column must
  not look like the same texture with a colour tint applied, **(h) lived-in surface cues
  present**: Residential cells include baked window-box sill planter colour patches;
  Commercial low/med cells include at least four distinct awning accent colours across the
  four variants — no two commercial variants share the same awning colour, **(i) checks #28 and
  #29 both pass** on the source PNG before the DDS is generated.
  Sign-off recorded as commit-message annotation.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

##### 2b. Vehicle Texture Rework

- [x] **Diffuse atlas** — `vehicles_diffuse_atlas_d.png` (2048×2048 source) →
  `vehicles_diffuse_atlas_d.dds` (DXT1 sRGB, 4-mip): rework vehicle liveries to production
  quality. Required per-vehicle surface detail: panel gaps and door seams (inset dark line
  ~1–2 px wide separating body panels); wheel disc markings (hub cap pattern or alloy spoke
  design appropriate to vehicle type); windscreen tinting (dark grey-blue, ~15 % lighter
  than the body colour); headlight and tail-light bezels (bezel in body-adjacent tone,
  lens fill in near-white for headlights and deep red for tail-lights); chassis underside
  darkening (darker band at the base course of each body cell, ~30 px height). **AO bake
  required for panel gaps**: panel gap lines must incorporate an ambient-occlusion-derived
  darkening (not just a flat-colour line) — the deepest point of each gap is darkest, with
  a 1–2 px gradient outward. Exported via `tools/export_textures.py`. DX10 sRGB header
  (`BC1_UNORM_SRGB`, DXGI_FORMAT = 72) must be validated before commit.
  **Vehicle livery colour variety is mandatory**: the car atlas cells must contain at least
  four distinct body colours (e.g. white, silver-grey, dark blue, warm red) so that when
  multiple car agents are spawned, the roads do not read as a uniform single-colour stream.
  The bus and truck liveries must each use a recognisably different primary hue from the
  car palette (e.g. bus: yellow-cream or city-green; truck: dark charcoal or olive) so
  that zone-type vehicle differentiation is legible without inspecting the mesh silhouette.
  The sprite atlas roof-swatch cells must mirror this variety — each body-colour variant
  needs a corresponding roof-colour swatch so LOD2 impostors match the LOD0 livery.
  (ref: `architecture/asset-standards/2d-texture-standards.md` §DDS Authoring Pipeline,
  `architecture/asset-standards/building-atlas-layout.md` §Vehicle Atlas)

- [x] **Sprite atlas** — `vehicles_sprite_atlas_d.png` (256×256 source) →
  `vehicles_sprite_atlas_d.dds` (DXT5, linear, 1 mip level (DDS dwMipMapCount=1,
  GL_TEXTURE_MAX_LEVEL=0, base level only)): author roof colour-swatch palette for
  LOD2 impostors per vehicle type. Each 16×16 px sprite cell must be a solid fill of the
  vehicle's dominant roof colour — no gradients or multi-colour fills within a single
  sprite cell, as these are illegible at the 16×16 rendering size. **Colour distance
  requirement**: the roof-colour swatch for the bus type and the truck type must have a
  perceptual colour distance ΔE ≥ 20 (CIE76) from each other and from the car sedan swatch,
  ensuring vehicle-type differentiation remains legible at LOD2. Exported via
  `tools/export_textures.py`.
  (ref: `architecture/asset-standards/building-atlas-layout.md` §Vehicle Sprite Atlas)

- [x] **Normal atlas** — `vehicles_normal_atlas_n.png` (2048×2048 source) →
  `vehicles_normal_atlas_n.dds` (DXT5nm, linear, 4-mip): author vehicle surface normal maps
  with per-vehicle-type surface detail requirements:
  - **Car variants** (sedan, hatchback, SUV): bonnet crease — a single longitudinal
    raised crease line running from the front grille area to the windscreen base (1–2 px
    wide highlight); door panel step — a 5–8 mm height step at each door boundary; roof
    curvature — gentle convex crown (peak-to-edge height difference ~15 mm) encoded as a
    low-frequency gradient in the height map.
  - **Bus** (`bus_standard`): side panel ribs — horizontal relief ribs at each floor
    level (~5 mm proud of the panel face, ~3 px wide crest); destination blind recess
    (~10 mm deep) at the front face above the windscreen; roof AC box perimeter step.
  - **Truck** (`truck_cargo`): cab front grille depth (~30 mm recess); exhaust stack base
    flange (~5 mm proud ring around the stack base); cargo box panel flat (no major
    surface variation) with only the door frame step at the rear face.
  Author each vehicle's normal map from a height-map bake of the LOD0 mesh where possible;
  hand-paint micro-surface features (panel step, crease) in the DCC tool on top of the
  baked base. Export using the DXT5nm swizzle procedure (X→alpha, Y→green, Z=0) per
  `architecture/asset-standards/2d-texture-standards.md` §DXT5nm packing. Exported via
  `tools/export_textures.py`.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [x] **`validate_assets.py` checks #25–27 — vehicle atlas DDS verification**: as part of
  completing this deliverable, the `cicd-dev-github` engineer adds three new checks to
  `validate_assets.py` for vehicle atlas DDS format and mip-level validation:
  check #25 (`vehicles_diffuse_atlas_d.dds` — BC1_UNORM_SRGB, 4-mip, 2048×2048),
  check #26 (`vehicles_sprite_atlas_d.dds` — BC3_UNORM linear, 1 mip level (DDS
  dwMipMapCount=1, GL_TEXTURE_MAX_LEVEL=0), 256×256),
  check #27 (`vehicles_normal_atlas_n.dds` — BC3_UNORM linear (DXT5nm), 4-mip, 2048×2048).
  All three checks must pass before Deliverable 2b is considered complete.
  (ref: `architecture/ci-cd/github-actions-workflow.md`,
  `architecture/asset-standards/2d-texture-standards.md`)

- [x] **Guard steps for checks #25–30 in `validate-assets` CI job** — add `Verify check_N present`
  guard steps to `.github/workflows/ci.yml` for checks #25 through #30, following the
  established pattern for checks #21–24. Each guard step runs
  `grep -q "check_N" tools/validate_assets.py || exit 1`. These six steps are additive —
  they do NOT replace or modify any existing guard steps. Placed in the `validate-assets`
  job only (not `build-linux`).
  (ref: `architecture/ci-cd/github-actions-workflow.md`)

- [x] **`graphics-artist-2d-texture` sign-off gate — vehicles** (blocking): before
  committing any reworked vehicle atlas PNG or DDS, `graphics-artist-2d-texture` must
  verify: (a) DX10 sRGB header present in `vehicles_diffuse_atlas_d.dds`
  (BC1_UNORM_SRGB, DXGI_FORMAT = 72), (b) at least four distinct car body colours present
  in diffuse atlas, (c) bus and truck use recognisably different primary hues from car
  palette, (d) each car variant's diffuse cell includes panel gap AO darkening (not flat
  lines), (e) sprite atlas has one solid-fill swatch per vehicle type with no multi-colour
  fills within a cell, (f) bus and truck sprite swatches are perceptually distinct from each
  other and from the car sedan swatch, (g) normal atlas has per-vehicle surface detail as
  specified above — verifiable by test render with a 45° point light showing bonnet crease
  highlight on cars, panel rib ridges on bus, and grille recess shadow on truck,
  (h) DX10 LINEAR header present in `vehicles_sprite_atlas_d.dds` (BC3_UNORM,
  DXGI_FORMAT = 77 — NOT sRGB; sprite atlas uses linear DXT5),
  (i) DX10 LINEAR header present in `vehicles_normal_atlas_n.dds` (BC3_UNORM,
  DXGI_FORMAT = 77 — NOT sRGB; normal atlas uses linear DXT5nm).
  Sign-off recorded as commit-message annotation on the first reworked vehicle atlas commit.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

##### 2c. Billboard Atlas Rework

- [x] **Billboard atlases** (`res_low_01_billboard.dds` through `res_low_04_billboard.dds`,
  `com_low_01_billboard.dds` through `com_low_04_billboard.dds`, etc. — one per Low/Med-density
  zone variant plus one per service building type; **28 files total**: 24 zone billboard files
  (Low and Med density across 3 zone types × 4 variants each) + 4 service billboard files.
  **High-density zones are excluded**: they use `_lod2.b3d` geometry shells at LOD2, not
  billboard imposters — per `architecture/asset-standards/3d-model-standards.md` §LOD Requirements)
  — re-bake
  all billboard imposters using the following mandatory bake parameters:
  - **Camera pitch**: exactly −45° below horizontal (the midpoint of the [−70°, −20°]
    player camera pitch range, per `architecture/asset-standards/3d-model-standards.md`
    §Camera Pitch Range). Do not bake at −30° or nadir — these produce distorted impostors
    that mismatch the player's typical viewing angle.
  - **8-direction horizontal rotation**: bake all 8 compass orientations (0°, 45°, 90°,
    135°, 180°, 225°, 270°, 315°) and pack them into the 8 × 128×128 frames of the
    1024×128 horizontal strip (left to right = 0° through 315°). Do not bake fewer than
    8 directions and stretch to fill — each frame must be a distinct bake.
  - **Lighting**: flat ambient-only illumination (uniform hemisphere light, no directional
    key light or sun). Baking with a directional light embeds a fixed shadow direction
    into the impostor that conflicts with the in-game sun angle at runtime. The ambient-only
    bake must use a sky/ground hemisphere of approximately equal brightness (~0.8 sky /
    ~0.6 ground on a 0–1 scale) to produce a balanced, shadow-free result.
  - **Source mesh and textures**: billboards MUST be baked from the reworked Phase 11d
    LOD0 mesh (Deliverable 1a) and the reworked Phase 11d atlas texture (Deliverable 2a).
    Re-baking from Phase 9 placeholder geometry or un-reworked textures is a quality
    failure. Verify the source mesh tri count and the diffuse atlas commit date before
    submitting the billboard bake.
  - **Content fill**: the building silhouette must occupy at least 60 % of the 112×112 px
    usable area within each 128×128 frame (after the 8 px border). Frames with the
    building occupying less than 60 % of the frame height indicate an incorrect camera
    distance or FOV in the bake setup.
  Format: DDS DXT5/BC3 sRGB, 1024×128 px, 4-mip chain capped at `GL_TEXTURE_MAX_LEVEL = 3`.
  Use Compressonator with `-miplevels 4` to produce exactly 4 mip levels in the DDS file
  (not a full mip chain) — see `architecture/asset-standards/2d-texture-standards.md`
  §DDS Authoring Pipeline for the billboard-atlas Compressonator command.
  **Billboard variant visual differentiation is mandatory**: all four billboard atlases
  for the same zone/tier must each be perceptibly different from the others at LOD2
  viewing distance. Because billboards are baked from the LOD0 mesh, this is automatically
  satisfied if the inter-variant differentiation requirements in Deliverable 1a are met —
  verify by side-by-side comparison of all four (e.g. `res_low_01_billboard.dds` through
  `res_low_04_billboard.dds` and equivalents for com/ind): clearly different silhouettes
  and/or awning colour are required so a city block at LOD2 does not look like a tiled
  repetition of one image. No two of the four billboard images within the same zone/tier
  may be visually indistinguishable.
  (ref: `architecture/asset-standards/2d-texture-standards.md` §Billboard Imposter Atlas)

- [x] **`validate_assets.py` check #30 — billboard atlas format and mip verification**:
  for each `*_billboard.dds` file, verify: (a) DDS dimensions are exactly 1024×128 px,
  (b) DX10 header present with DXGI_FORMAT BC3_UNORM_SRGB (value 78), (c) DDS
  `dwMipMapCount` field equals exactly 4, (d) total file size matches the reference size
  for DXT5/BC3 1024×128 at 4 mip levels (192,640 bytes per
  `architecture/asset-standards/2d-texture-standards.md` §DDS Mip Chain Integrity).
  Check #30 must pass for all billboard atlases before Deliverable 2c is considered
  complete.
  (ref: `architecture/ci-cd/github-actions-workflow.md`,
  `architecture/asset-standards/2d-texture-standards.md`)

---

#### 3. Traffic System Visual Wiring

Phase 6 delivered the full `CitySimulation` traffic simulation (A* pathfinding, agent
lifecycle, speed-function, intersection signal cycles, demand coupling). This deliverable
wires that simulation to visible in-world rendering: vehicle mesh nodes spawned and driven
along road paths, and intersection signal state shown at road nodes.

##### 3a. Vehicle Agent Rendering

- [ ] **`IRenderer::spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ, ZoneType zone)` → `void`**
  — new method on `IRenderer` interface (`src/interfaces/IRenderer.h`). Spawns a vehicle
  agent node as an `IMeshSceneNode* (CMeshSceneNode)` (not a `LODNode*` wrapper) at the
  world-space position derived from tile coordinates `(tileX, tileZ)` and stores it in
  `m_agentNodes` keyed by `AgentHandle`. Agent vehicle nodes MUST be created via
  `smgr->addMeshSceneNode(static_cast<IMesh*>(vehicleMesh))` — NOT
  `addAnimatedMeshSceneNode` — because B3D vehicle meshes contain a root bone transform
  that displaces the node position when the animated variant is used
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`
  §Why CMeshSceneNode is required).
  **Mesh loading source**: `vehicleMesh` is an `IAnimatedMesh*` loaded from the Irrlicht
  mesh cache via `smgr->getMesh("assets/3d/vehicles/<vehicle>_lod0.b3d")` and cast to `IMesh*`
  for `addMeshSceneNode`. The scene manager retains ownership of the cached mesh — do NOT
  call `->drop()` on it after node creation. Vehicle meshes are loaded once at renderer init
  and reused across all agent spawns for the same vehicle type (see `despawnVehicleAgent`
  below for the full ref-count contract — same B3D cached pattern as building meshes).
  (SceneEntityManager is NOT
  involved — agent nodes are stored in `m_agentNodes` and managed entirely by
  `IrrlichtRenderer`.) Zone type determines which vehicle atlas cell is sampled (Residential → car,
  sub-variant selected as `handle % 3`: 0=`car_sedan`, 1=`car_hatchback`, 2=`car_suv`;
  Commercial → `bus_standard`; Industrial → `truck_cargo`). The caller-supplied `AgentHandle` is the
  stable identifier used for subsequent `moveVehicleAgent` / `despawnVehicleAgent` calls. `AgentHandle` is defined as
  `using AgentHandle = uint32_t;` in `src/interfaces/simulation_types.h` (NOT in
  `IRenderer.h` — see Deliverable 0 ODR note) — it is a stable identifier for the
  agent's lifetime and is assigned by the traffic simulation. agentId
  (from traffic simulation) is mapped 1:1 to `AgentHandle`; the renderer maintains a
  parallel `m_agentNodes` registry alongside the existing `m_vehicleNodes` — agent methods
  coexist with Phase 10 vehicle methods without replacing them. The `m_agentNodes`
  handle→node registry is owned by `IrrlichtRenderer`; `main.cpp` calls the three agent
  methods but does not hold the map.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`,
  `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget)

- [ ] **`IRenderer::moveVehicleAgent(AgentHandle handle, int tileX, int tileZ, float headingDeg)`** — updates
  the agent's scene node to the world-space position derived from tile coordinates
  `(tileX, tileZ)` and sets Y-axis rotation each frame. Called from `main.cpp`
  per-frame render loop after `CitySimulation::getAgentPositions()` is polled. No physics;
  direct node position set (`setPosition` + `setRotation`).
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **`IRenderer::despawnVehicleAgent(AgentHandle)`** — look up the node pointer in `m_agentNodes[handle]` first, then apply the eviction sequence: iterate all material slots to clear texture pointers → `driver->setMaterial(SMaterial{})` → `node->remove()`, then erase the handle from `m_agentNodes`. Per `architecture/graphics-architecture/scene-graph-ownership.md`, agent nodes use `IAnimatedMesh` loaded via the scene manager — do NOT call `->drop()` on the mesh (scene manager retains ownership). `IrrlichtRenderer` owns agent nodes directly; `SceneEntityManager` is not involved. **TextureCache note**: vehicle atlas textures are loaded ONCE at renderer init and shared across all agent nodes; `spawnVehicleAgent` does NOT call `TextureCache::loadSRGB`, so `despawnVehicleAgent` does NOT call `TextureCache::releaseSRGB` — clearing the material slots is sufficient.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **`ICitySimulation::getAgentPositions()` query** — new method on `ICitySimulation`
  returning a `std::vector<AgentState>` (defined in `simulation_types.h`) where `AgentState`
  carries `{ agentId, tileX, tileZ, headingDeg, ZoneType }`. Used by the render loop to
  sync scene nodes to simulation state each frame. `AgentState` is a value type; no raw
  pointers.
  (ref: `architecture/game-design/traffic-system.md`)

- [ ] **`IRenderer::getListenerPosition()` → `irr::core::vector3df`** — method already added
  in Phase 10 (`src/interfaces/IRenderer.h`); returns the current camera/listener world-space
  position. Used by the traffic agent cull logic below to compute agent-to-camera distance.
  Verify it is present before use — do not re-add it in this phase.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **Distance cull for agent spawning** — only agents within **150 m** (Phase 11d agents use LOD0 only — no LOD swap; `m_agentNodes` stores plain `IMeshSceneNode*`, not `LODNode*`; the 150 m value is the spawn/despawn cull boundary) of the camera
  listener position (per `IRenderer::getListenerPosition()` defined in Phase 10) are
  spawned as scene nodes. Agents outside cull range are tracked in simulation but have no
  scene node. On spawn, `spawnVehicleAgent` is called; on despawn (timeout or exit cull
  range), `despawnVehicleAgent` is called. This matches the vehicle-engine-audio 150 m cull
  established in Phase 10. Before implementing, confirm this value matches the vehicle-audio
  cull distance in `architecture/audio-architecture/dynamic-soundscape.md`; if values differ,
  use the audio spec value as binding authority.
  (ref: `architecture/game-design/traffic-system.md`,
  `architecture/audio-architecture/dynamic-soundscape.md`)

- [ ] **Vehicle engine audio pair wiring** — in the per-frame agent sync loop in `main.cpp`,
  after each `spawnVehicleAgent` call, call `IAudioSystem::acquireVehicleEnginePair(ZoneType)`
  and store the returned pair indices in a `std::unordered_map<AgentHandle, std::pair<int,int>>`
  keyed by `AgentHandle`; before each `despawnVehicleAgent` call, call
  `IAudioSystem::releaseVehicleEnginePair(idleIdx, moveIdx)` using the stored indices.
  This main-thread map is solely for caller bookkeeping to associate the correct pair indices
  with each `AgentHandle` for the eventual release call — it is independent of
  `AudioSourcePool`'s internal `m_vehiclePairs` tracking table (which manages pool-internal
  state). The two maps do not synchronize; `releaseVehicleEnginePair` must be called exactly
  once per successful `acquireVehicleEnginePair`. Passing `{-1, -1}` (failed acquisition) to
  `releaseVehicleEnginePair` is a safe no-op. **AL_VELOCITY requirement**: the `AudioSystem` implementation of
  `acquireVehicleEnginePair()` must call `alSource3f(src, AL_VELOCITY, 0.f, 0.f, 0.f)` on
  both acquired sources, on the audio thread, before returning the indices to the caller —
  this disables Doppler pitch shift (speed-dependent pitch is handled entirely by `AL_PITCH`
  modulation; leaving `AL_VELOCITY` at the default would produce double-pitch artifacts per
  `architecture/audio-architecture/dynamic-soundscape.md §Vehicle Engine Audio`).
  The caller in `main.cpp` receives only opaque integer indices and must NOT call any `al*`
  function directly.
  Per-frame audio update: in the per-frame agent sync loop in `main.cpp`, for each active
  vehicle, call `IAudioSystem::updateVehicleAudio(idleIdx, moveIdx, speedFraction, worldX, worldZ)`
  with the speed fraction and world position from `getAgentPositions()`. `AudioSystem` uses
  these values on the audio thread to set `AL_PITCH` (base pitch × lerp(0.75, 1.35, speedFraction)),
  `AL_GAIN` crossblend (idle/move sources blended by speed threshold per
  `architecture/audio-architecture/dynamic-soundscape.md` §Vehicle Engine Audio), and
  `AL_POSITION` for 3D spatial rolloff. Base pitch is stored internally by `AudioSystem` from
  the zone type supplied at `acquireVehicleEnginePair()` time (Residential → 1.0 car,
  Commercial → 0.85 bus, Industrial → 0.85 truck).
  (ref: `architecture/audio-architecture/dynamic-soundscape.md`,
  `architecture/audio-architecture/source-pool.md`)

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
  - **Green** (`#27AE60`): speed ≥ 40% of free-flow (free-flowing; no economic penalty)
  - **Orange** (`#E67E22`): speed 31–39% of free-flow (mild congestion; −10% tax yield penalty)
  - **Red** (`#E74C3C`): speed ≤ 30% of free-flow (moderate–heavy congestion; ≥ −18% tax yield penalty)
  These thresholds and hex values are authoritative from `architecture/ui-ux/minimap.md` §Traffic Congestion overlay. Note: the traffic-system.md defines three economic penalty tiers (31–39%, 21–30%, ≤20%); the minimap collapses the bottom two tiers into a single Red band (≤30%) for visual simplicity. Speeds ≥ 40% are free-flow (no penalty) matching the Green band.
  The congestion colour is fetched from `ICitySimulation::getRoadSegmentSpeeds()` (new query,
  see below) and applied as a coloured overlay pixel on the minimap texture.
  (ref: `architecture/game-design/traffic-system.md` §Congestion threshold,
  `architecture/ui-ux/minimap.md`)

- [ ] **`ICitySimulation::getRoadSegmentSpeeds()` query** — returns
  `std::vector<RoadSegmentSpeed>` (new struct: `{ tileX, tileZ, float speedFraction }`)
  where `speedFraction` is `currentSpeed / maxSpeed` in [0.0, 1.0]. **All road tiles are
  returned** (not just occupied ones); unoccupied road tiles report `speedFraction = 1.0`
  (free-flow, green). This ensures the minimap shows a complete road network baseline and
  does not misrepresent empty roads as missing data. Used exclusively by the minimap traffic
  overlay.
  (ref: `architecture/game-design/traffic-system.md`)

- [ ] **`kMinimapWidgetTop`, `kMinimapWidgetTopOverlayActive`, and `kMinimapWidgetLeft` constants** — add to
  `src/ui/ui_constants.h`:

  ```cpp
  constexpr int kMinimapWidgetTop = 848;               // top edge of minimap widget (no overlay)
  constexpr int kMinimapWidgetTopOverlayActive = 732;  // top edge when overlay chrome is visible
  constexpr int kMinimapWidgetLeft = 1576;             // left edge of full minimap widget footprint
  ```

  These constants are required by the QueryPanel dismiss-click minimap carve-out in
  `UIManager::onEvent` (per `architecture/ui-ux/input-arbitration.md` Priority 3) to
  correctly exclude minimap-region clicks from closing the Inspector panel when a traffic or
  service-coverage overlay is active (overlay chrome extends the minimap widget upward from
  848 px to 732 px; `kMinimapWidgetLeft` anchors the left boundary for the x-coordinate
  carve-out check). Responsibility: `gamedesign-ux` (these are UI-layer constants in
  `src/ui/ui_constants.h` consumed by `UIManager::onEvent` — not graphics renderer code).
  (ref: `architecture/ui-ux/input-arbitration.md` Priority 3,
  `architecture/ui-ux/minimap.md`)

##### 3d. Traffic Tests

> **Mock policy**: all mocks in this section use `StrictMock<>` per
> `architecture/testing/testability-architecture.md` (StrictMock for unit tests). Use
> `StrictMock<MockRenderer>` in all test fixtures below.

- [ ] **`AgentRenderSync_SpawnDespawn_MatchesSimulationOutput`** (label `unit`,
  CMake target `simulation_tests`): using `StrictMock<MockRenderer>`, verify that the per-frame agent
  sync loop calls `spawnVehicleAgent` exactly once per new agent, `moveVehicleAgent` every
  frame for active agents, and `despawnVehicleAgent` exactly once when an agent is removed.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`AgentRenderSync_CullDistance_AgentsBeyond150m_NotSpawned`** (label `unit`,
  CMake target `simulation_tests`): using `StrictMock<MockRenderer>`, verify that agents with tile distance > 150 m from the
  camera do not trigger `spawnVehicleAgent`.

- [ ] **`AgentEngineAudio_AcquireRelease_MatchesSpawnDespawn`** (label `unit`,
  CMake target `simulation_tests`): using `StrictMock<MockAudioSystem>`, verify that the
  per-frame agent sync loop calls `acquireVehicleEnginePair` exactly once per spawned agent
  (on spawn) and `releaseVehicleEnginePair` exactly once per despawned agent (on despawn) with
  the matching indices. Also verify that passing `{-1, -1}` (pool exhaustion) to
  `releaseVehicleEnginePair` is a safe no-op (i.e., `releaseVehicleEnginePair(-1, -1)` is
  called and does not crash or assert). This test guards against pool source handle leaks when
  agents despawn.
  (ref: `architecture/audio-architecture/source-pool.md`,
  `architecture/audio-architecture/audio-system.md` §acquireVehicleEnginePair)

- [ ] **CMakeLists extension** — add test source via `target_sources(simulation_tests PRIVATE
  tests/simulation/agent_render_sync_test.cpp)` following the Phase 4+ extension policy (do
  NOT call `add_executable` again — duplicate target error).

##### 3e. Intersection Tick SFX

- [ ] **`sfx_intersection_tick` audio trigger in `CitySimulation::tick()`** — for each active
  traffic signal, on each phase transition (green→red or red→green), call
  `m_audio->playPositionalSound(SFX_INTERSECTION_TICK, pos, SoundPriority::LOW, 1.0f)` gated
  by an 80 m distance cull from `m_renderer->getListenerPosition()`. This wires the
  intersection signal SFX specified in `architecture/game-design/traffic-system.md` and
  `architecture/audio-architecture/dynamic-soundscape.md`. The `getListenerPosition()` method
  was defined in Phase 10 and is already present in `IRenderer.h`.
  (ref: `architecture/game-design/traffic-system.md` §Intersections,
  `architecture/audio-architecture/v1-audio-asset-manifest.md`)

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
  state. `hideServiceCoverageOverlay` is called in three cases: (1) on Inspector close (player
  presses **I**, clicks elsewhere, or presses **Escape**), (2) when a different tile is
  queried and the result is not a service building (i.e., `queryTile` returns `isZoned`,
  `isRoad`, or neither), and (3) when `queryTile` is called on a tile that IS a service
  building but DIFFERENT from the currently-displayed building — `UIManager` calls
  `hideServiceCoverageOverlay()` first (to clear the previous overlay) then immediately calls
  `showServiceCoverageOverlay()` with the new building's data, ensuring only one overlay is
  visible at a time per `architecture/ui-ux/query-inspector-panel.md`. Uses the
  `InspectorPanel::populate()` wiring established in Phase 9b.
  (ref: `architecture/ui-ux/query-inspector-panel.md`,
  `architecture/game-design/service-coverage.md`)

- [ ] **Power plant BFS tile highlight** — for `ServiceBuildingType::PowerPlant`, instead of
  a circle overlay, `showServiceCoverageOverlay` highlights all BFS-reachable tiles in a
  distinct "coverage" colour (yellow `#F1C40F`). Implementation MUST use a multi-tile mesh
  approach (following the placement-preview pattern from Phase 10:
  `architecture/graphics-architecture/scene-graph-ownership.md` §Placement Preview), NOT
  `setTileHoverHighlight()` (which is a single pre-allocated 4-vertex quad — calling it per
  tile overwrites and shows only the last tile). Allocate a dynamic `SMesh*` with one quad
  per covered tile, rebuild on `showServiceCoverageOverlay` call, and release on
  `hideServiceCoverageOverlay`. The dynamic `SMesh*` is owned by `IrrlichtRenderer` (not
  `SceneEntityManager`); it is allocated in `showServiceCoverageOverlay` and released via
  `->drop()` in `hideServiceCoverageOverlay`, following the `m_previewMesh` lifetime pattern.
  (ref: `architecture/game-design/service-coverage.md` §Power plant coverage model,
  `architecture/graphics-architecture/scene-graph-ownership.md` §Placement Preview)

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
  `architecture/ui-ux/minimap.md` §Service Coverage Overlay).
  **Colorblind mode** (required per `architecture/ui-ux/resolution-ui-scaling.md`
  §Colorblind Accessibility): when colorblind mode is active, each covered-tile colour
  must also include a distinct geometric pattern overlay so the service type is
  distinguishable by pattern alone — diagonal hatching for Fire Station, horizontal
  lines for Police Station, dotted overlay for Power Plant, cross-hatch for Water Tower.
  (ref: `architecture/ui-ux/minimap.md`,
  `architecture/game-design/service-coverage.md`,
  `architecture/ui-ux/resolution-ui-scaling.md` §Colorblind Accessibility)

- [ ] **`ICitySimulation::getServiceCoverage()` query** — returns
  `std::vector<ServiceCoverageTile>` (new struct in `simulation_types.h`:
  `{ tileX, tileZ, ServiceBuildingType coveredBy, bool degraded }`) for all tiles currently
  covered by at least one service building. Used exclusively by the minimap service coverage
  overlay. An uncovered tile is absent from the returned vector.
  (ref: `architecture/game-design/service-coverage.md`)

##### 4c. Service Coverage Tests

> **Mock policy**: all mocks in this section use `StrictMock<>` per
> `architecture/testing/testability-architecture.md` (StrictMock for unit tests). Use
> `StrictMock<MockCitySimulation>` and `StrictMock<MockRenderer>` in all test fixtures below.

- [ ] **`ServiceCoverageOverlay_QueryServiceTile_ShowsOverlay`** (label `unit`,
  CMake target `ui_tests`): using `StrictMock<MockCitySimulation>` and `StrictMock<MockRenderer>`,
  verify that querying a service building tile calls `showServiceCoverageOverlay` with correct
  type and degradation state.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`ServiceCoverageOverlay_InspectorClose_HidesOverlay`** (label `unit`,
  CMake target `ui_tests`): using `StrictMock<MockCitySimulation>` and `StrictMock<MockRenderer>`,
  verify that closing the Inspector panel calls `hideServiceCoverageOverlay`.

- [ ] **CMakeLists extension** — add via `target_sources(ui_tests PRIVATE
  tests/ui/service_coverage_overlay_test.cpp)`.

---

#### 5. Placement Conflict Prevention

Zones and roads must not silently overwrite existing tiles. Currently `placeZone` replaces
road tiles and `placeRoad` replaces zone tiles; this deliverable adds early-return guards
so that any attempt to place on an occupied tile is a no-op (no cost deducted, no undo
recorded, no renderer call made). Players must explicitly demolish (`demolishTile`) before
re-zoning or re-roading a tile.

##### 5a. Simulation Guard — `placeZone`

- [ ] **Guard in `CitySimulation::placeZone`**: at the top of the function, before any
  cost deduction or undo-state capture, check whether the target tile is already occupied:

  ```cpp
  auto it = m_tiles.find(key);
  if (it != m_tiles.end() && (it->second.isRoad || it->second.isZoned)) {
      return; // tile occupied — demolish first
  }
  ```

  If the tile is already a road (`isRoad == true`) or already zoned (`isZoned == true`),
  `placeZone` must return immediately with no side effects: no treasury deduction, no
  `recordUndoAction`, no renderer call, no audio event.
  **Service building occupancy**: this guard intentionally checks only `isRoad || isZoned`.
  Service buildings are NOT checked here — `placeServiceBuilding` is responsible for its own
  occupancy guard (see `architecture/game-design/service-coverage.md`). `placeZone` does not
  need to query service building state and must not be extended to do so.
  (ref: `architecture/game-design/zoning-system.md`,
  `architecture/game-design/undo-system.md`)

- [ ] **Remove the now-dead `isRoad` branch** inside `placeZone` that decremented
  `m_roadTileCount` when replacing a road tile — this code path can no longer be reached
  after the guard is in place. Remove it to eliminate dead code.
  (ref: `src/simulation/CitySimulation.cpp`)

##### 5b. Simulation Guard — `placeRoad`

- [ ] **Guard in `CitySimulation::placeRoad`**: at the top of the function, before any
  cost deduction or undo-state capture, add:

  ```cpp
  auto it = m_tiles.find(key);
  if (it != m_tiles.end() && (it->second.isRoad || it->second.isZoned)) {
      return; // tile occupied — demolish first
  }
  ```

  If the tile is already a road (`isRoad == true`) or already zoned (`isZoned == true`),
  `placeRoad` must return immediately with no side effects: no treasury deduction, no
  `recordUndoAction`, no renderer call, no audio event.
  **Service building occupancy**: this guard intentionally checks only `isRoad || isZoned`.
  Service buildings are NOT checked here — `placeServiceBuilding` handles its own occupancy
  guard independently. `placeRoad` does not need to query service building state.
  (ref: `architecture/game-design/zoning-system.md`,
  `architecture/game-design/undo-system.md`)

- [ ] **Remove the now-dead `isZoned` branch** inside `placeRoad` that handled replacing a
  zone tile with a road — this code path can no longer be reached after the guard is in
  place. Remove it to eliminate dead code.
  (ref: `src/simulation/CitySimulation.cpp`)

> **Note — `placeServiceBuilding` zone/road conflict exemption**: service buildings are
> infrastructure and may be placed on any buildable tile regardless of zoning or road status,
> per `architecture/game-design/service-coverage.md` §Service Building Placement — no
> zone/road occupancy guard is added to `placeServiceBuilding`. **However**, the
> service-building-on-service-building occupancy guard is still required: if the target tile
> is already occupied by a service building (any type), `placeServiceBuilding` must no-op
> (no treasury change, no undo entry, no renderer call) per
> `architecture/game-design/service-coverage.md` line 66 ("No-ops if the tile is already
> occupied by a service building (does not replace)"). This guard is distinct from the
> zone/road conflict guards in Deliverables 5a/5b and is NOT deferred.

##### 5c. Placement Conflict Tests

> **Mock policy**: all mocks in this section use `StrictMock<>` per
> `architecture/testing/testability-architecture.md` (StrictMock for unit tests). Use
> `StrictMock<MockRenderer>` in all test fixtures below — this is critical for the
> `Times(0)` negative assertions below (bare mocks silently swallow unexpected calls,
> defeating the no-op checks).

- [ ] **`PlaceZone_OnRoadTile_IsNoOp`** (label `unit`, CMake target `simulation_tests`):
  place a road tile at `(3, 3)`, then call `placeZone` on the same tile. Assert: the tile
  remains `isRoad == true` and `isZoned == false`; `m_roadTileCount` is unchanged; treasury
  is unchanged; `StrictMock<MockRenderer>` receives no `placeBuildingMesh` call
  (`EXPECT_CALL(...).Times(0)`); undo stack depth is unchanged.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`PlaceZone_OnZonedTile_IsNoOp`** (label `unit`, CMake target `simulation_tests`):
  place a Residential zone at `(4, 4)`, then call `placeZone` with Commercial on the same
  tile. Assert: tile zone remains `Residential`; treasury unchanged after the second call;
  `StrictMock<MockRenderer>` receives no second `placeBuildingMesh` call; undo stack depth
  is unchanged after the second call.

- [ ] **`PlaceRoad_OnZonedTile_IsNoOp`** (label `unit`, CMake target `simulation_tests`):
  place a Residential zone at `(5, 5)`, then call `placeRoad` on the same tile. Assert:
  tile remains `isZoned == true` and `isRoad == false`; `m_roadTileCount` is zero; treasury
  is unchanged after the `placeRoad` call; `StrictMock<MockRenderer>` receives no
  `placeRoadMesh` call; undo stack depth is unchanged after the `placeRoad` call.

- [ ] **`PlaceRoad_OnRoadTile_IsNoOp`** (label `unit`, CMake target `simulation_tests`):
  place a road at `(6, 6)`, then call `placeRoad` again on the same tile. Assert:
  `m_roadTileCount == 1` (not 2); treasury deduction happens only once; `StrictMock<MockRenderer>`
  receives exactly one `placeRoadMesh` call; undo stack depth is 1 after both calls.

- [ ] **`CMakeLists extension`** — add via `target_sources(simulation_tests PRIVATE
  tests/simulation/placement_conflict_test.cpp)`. Do NOT call `add_executable` again.

##### 5d. UI-Level Placement Guard with Red Preview Feedback

The simulation guards in 5a/5b are silent no-ops. This sub-deliverable adds visible feedback
so the player knows why a placement is being rejected, and makes it impossible to even
initiate a placement click on an occupied tile.

- [ ] **`kHoverArgbBlocked` constant** — add `constexpr uint32_t kHoverArgbBlocked =
  0xBBFF2222u;` (semi-opaque red, alpha=0xBB≈73%) to `src/ui/ui_constants.h`, alongside
  `kHoverArgbZone`, `kHoverArgbRoad`, and the other hover ARGB constants. **Do NOT add it
  to `UIManager.cpp`** — `hud-layout.md` §Tile Hover Highlight — ARGB Colour Scheme mandates all hover
  ARGB constants are defined in `src/ui/ui_constants.h` for header visibility (tests
  reference `kHoverArgbBlocked` directly, requiring it to be accessible from test TUs).
  (ref: `architecture/ui-ux/hud-layout.md` §Tile Hover Highlight — ARGB Colour Scheme)

- [ ] **`IRenderer::setTilePlacementPreview` signature extension** — change the existing
  single-list overload to accept a second tile list for blocked tiles:

  ```cpp
  void setTilePlacementPreview(
      const std::vector<std::pair<int,int>>& freeTiles,  uint32_t freeArgb,
      const std::vector<std::pair<int,int>>& blockedTiles = {}) override;
  ```

  `blockedTiles` are always rendered with `kHoverArgbBlocked`. The renderer draws both
  lists in a single `drawMeshBuffer` pass using the existing `m_previewMesh` (dynamically
  rebuilt each call to include both free and blocked quads; blocked quads use the hardcoded
  red ARGB rather than `freeArgb`). Update `IRenderer.h`, `IrrlichtRenderer.h/cpp`, and
  `MockRenderer` (`MOCK_METHOD` signature) to match the new signature. All existing
  call sites that pass only `freeTiles` remain valid (default `blockedTiles = {}`).
  Before committing the signature change, run `grep -r 'setTilePlacementPreview'` to confirm
  all existing call sites use only two arguments; none should pass a third argument.
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md` §Placement Preview,
  `architecture/testing/testability-architecture.md`)

- [ ] **Single-tile hover color override** — in the `UIManager::onEvent` MouseMove handler,
  after `colour` is determined from the active tool switch, add an occupancy override for
  Zone and Road tools:

  ```cpp
  if ((m_activeTool == ActiveTool::Zone || m_activeTool == ActiveTool::Road) && m_sim) {
      QueryResult tileInfo = m_sim->queryTile(hitX, hitZ);
      if (tileInfo.isRoad || tileInfo.isZoned) {
          colour = kHoverArgbBlocked;
      }
  }
  ```

  This ensures `setTileHoverHighlight` (single-tile path) receives red when the hovered
  tile is occupied.
  (ref: `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/input-arbitration.md`)

- [ ] **Drag preview partitioning — Zone rect** — in the MouseMove branch that builds the
  Zone rect preview (`m_lmbHeld && m_activeTool == ActiveTool::Zone && m_zoneAnchorX != -1`),
  after assembling the full `previewTiles` vector, partition it into `freeTiles` and
  `blockedTiles` by calling `m_sim->queryTile(tx, tz)` on each tile and checking
  `isRoad || isZoned`. Pass both lists to the extended `setTilePlacementPreview`.
  The `colour` passed as `freeArgb` is the tool-normal `kHoverArgbZone` (not overridden
  to red even if some tiles are blocked — only blocked tiles show red; free tiles remain
  the zone colour).
  (ref: `architecture/ui-ux/input-arbitration.md` §Priority 7)

- [ ] **Drag preview partitioning — Road line** — same partitioning as Zone rect, applied
  to the Road line preview branch (`m_lmbHeld && m_activeTool == ActiveTool::Road &&
  m_zoneAnchorX != -1`). Pass `freeTiles` and `blockedTiles` to the extended
  `setTilePlacementPreview` with `freeArgb = kHoverArgbRoad`.
  (ref: `architecture/ui-ux/input-arbitration.md` §Priority 7)

- [ ] **Commit loop — skip occupied tiles (Zone)** — in the `MouseButtonUp` Zone commit
  block that iterates `[x0..x1] × [z0..z1]` and calls `doTerrainPlacement(tx, tz)`, wrap
  each call in an occupancy guard:

  ```cpp
  if (m_sim) {
      QueryResult ti = m_sim->queryTile(tx, tz);
      if (ti.isRoad || ti.isZoned) continue;
  }
  doTerrainPlacement(tx, tz);
  ```

  (ref: `architecture/ui-ux/input-arbitration.md` §Priority 7)

- [ ] **Commit loop — skip occupied tiles (Road)** — same guard applied to the Road line
  commit loop (both the X-axis and Z-axis segments).
  (ref: `architecture/ui-ux/input-arbitration.md` §Priority 7)

##### 5e. Placement Conflict UI Tests

> **Mock policy**: all mocks in this section use `StrictMock<>` per
> `architecture/testing/testability-architecture.md` (StrictMock for unit tests). Use
> `StrictMock<MockCitySimulation>` and `StrictMock<MockRenderer>` in all test fixtures below.

- [ ] **`PlacementPreview_ZoneTool_OccupiedTile_ShowsRedHighlight`** (label `unit`,
  CMake target `ui_tests`): set up `StrictMock<MockCitySimulation>::queryTile` to return
  `QueryResult{.isRoad = true}` for tile `(2, 2)`. Inject into a `UIManager` with
  `StrictMock<MockRenderer>`. Synthesise a `MouseMove` event that ray-casts to tile `(2, 2)`
  with Zone tool active. Assert `MockRenderer::setTileHoverHighlight` was called with
  `argb == kHoverArgbBlocked`.
  (ref: `architecture/testing/testability-architecture.md`)

- [ ] **`PlacementPreview_ZoneDrag_PartiallyOccupied_BlockedTilesRed`** (label `unit`,
  CMake target `ui_tests`): `StrictMock<MockCitySimulation>::queryTile` returns `isRoad=true`
  for tile `(1,1)` and unoccupied for `(2,1)`. Simulate LMB-held Zone drag from `(1,1)` to
  `(2,1)`. Assert `StrictMock<MockRenderer>::setTilePlacementPreview` received
  `blockedTiles == {(1,1)}` and `freeTiles == {(2,1)}`.

- [ ] **`PlacementCommit_ZoneTool_OccupiedTileSkipped`** (label `unit`,
  CMake target `ui_tests`): `StrictMock<MockCitySimulation>::queryTile` returns `isRoad=true`
  for `(3,3)` and unoccupied for `(4,3)`. Simulate LMB drag-release over both tiles with
  Zone tool. Assert `StrictMock<MockCitySimulation>::placeZone` was called exactly once — for
  `(4,3)` only, never for `(3,3)`.

- [ ] **`CMakeLists extension`** — add via `target_sources(ui_tests PRIVATE
  tests/ui/placement_conflict_ui_test.cpp)`. Do NOT call `add_executable` again.

---

### Exit Criteria

- All reworked building LOD0 meshes confirmed within the upper polygon ranges:
  Residential High and Industrial High: 6,000–8,000 tris;
  Commercial High (skyscrapers): 8,000–10,000 tris;
  Small buildings (Low and Med tiers): 2,000–3,000 tris;
  Service buildings: 2,500–4,000 tris (one unique model per type, no variants).
- Service building LOD1: 200–400 tris.
- Large building LOD1: Residential/Industrial High: 1,000–1,500 tris; Commercial High: 1,500–2,000 tris.
- Small building LOD1: 300–400 tris.
- Vehicle LOD1 (by class): car 300–400 tris, bus 400–500 tris, truck 400–500 tris.
- All reworked vehicle LOD0 meshes confirmed at or below their per-class binding limits from
  `architecture/asset-standards/3d-model-standards.md` §Vehicle Polygon Budget
- `validate_assets.py` 30-check suite passes with zero errors on all reworked `.b3d` and
  `.dds` files (checks #25–27 added by Deliverable 2b for vehicle atlas DDS format
  verification; checks #28–29 added by Deliverable 2a for building atlas; check #30 added
  by Deliverable 2c for billboard atlas); `validate-assets` CI job remains green
- `buildings_atlas_d.dds` DX10 header confirmed BC1_UNORM_SRGB (DXGI_FORMAT = 72); all four
  mip levels present; no UV bleed across atlas cell borders
- `vehicles_diffuse_atlas_d.dds` passes DX10 header validation: BC1_UNORM_SRGB
  (DXGI_FORMAT = 72), 4-mip chain, 2048×2048.
- `vehicles_sprite_atlas_d.dds` passes DX10 header validation: BC3_UNORM (DXT5 linear),
  1 mip level (DDS dwMipMapCount=1, GL_TEXTURE_MAX_LEVEL=0), 256×256.
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
- All new unit tests pass: `AgentRenderSync_SpawnDespawn_MatchesSimulationOutput`,
  `AgentRenderSync_CullDistance_AgentsBeyond150m_NotSpawned`,
  `ServiceCoverageOverlay_QueryServiceTile_ShowsOverlay`,
  `ServiceCoverageOverlay_InspectorClose_HidesOverlay`,
  `PlaceZone_OnRoadTile_IsNoOp`,
  `PlaceZone_OnZonedTile_IsNoOp`,
  `PlaceRoad_OnZonedTile_IsNoOp`,
  `PlaceRoad_OnRoadTile_IsNoOp`,
  `PlacementPreview_ZoneTool_OccupiedTile_ShowsRedHighlight`,
  `PlacementPreview_ZoneDrag_PartiallyOccupied_BlockedTilesRed`,
  `PlacementCommit_ZoneTool_OccupiedTileSkipped`
- `placeZone` and `placeRoad` are confirmed no-ops when the target tile is occupied:
  no cost deducted, no undo entry recorded, no renderer call issued
- Single-tile hover shows red (`kHoverArgbBlocked`) when Zone or Road tool is active and
  the hovered tile is occupied (road or zone)
- Zone rect drag preview and Road line drag preview show free tiles in tool colour and
  blocked tiles in red simultaneously via the extended `setTilePlacementPreview` two-list API
- Zone and Road commit loops on LMB release skip all occupied tiles (no `doTerrainPlacement`
  call, no cost, no undo entry)
- `all-checks-pass` gate green

### Team

| Role | Responsibility |
|---|---|
| `graphics-artist-3d-model` | Rework all building and vehicle LOD0/LOD1 `.b3d` meshes to upper polygon-budget detail; re-bake all billboard atlases at −45° pitch; sign off per deliverable 1a/1b sign-off gate |
| `graphics-artist-2d-texture` | Rework `buildings_atlas_d.png` all 15 assigned cells to production detail; author all three vehicle atlases (`vehicles_diffuse_atlas_d.dds` DXT1 sRGB 4-mip, `vehicles_sprite_atlas_d.dds` DXT5 linear 1 mip level (DDS dwMipMapCount=1, GL_TEXTURE_MAX_LEVEL=0, base level only), `vehicles_normal_atlas_n.dds` DXT5nm linear 4-mip); rework billboard atlases; re-export all DDS files via `tools/export_textures.py`; implement checks #28–29 in `validate_assets.py` for building atlas validation (Deliverable 2a) and check #30 for billboard atlas validation (Deliverable 2c); sign off per deliverable 2a sign-off gate adapted for each vehicle atlas format (DXGI_FORMAT per checks #25–27: BC1_UNORM_SRGB=72 for diffuse, BC3_UNORM=77 for sprite and normal atlases) |
| `graphics-dev-irrlicht` | Implement `IRenderer` methods for vehicle agent spawn/move/despawn, intersection signal state, service coverage overlays; add minimap traffic and service coverage overlay modes; add `ICitySimulation` query methods (`getAgentPositions`, `getIntersectionSignalStates`, `getRoadSegmentSpeeds`, `getServiceCoverage`); per-frame agent sync loop in `main.cpp`; add placement conflict guards in `placeZone` and `placeRoad` (Deliverable 5a/5b); extend `IRenderer::setTilePlacementPreview` to accept `blockedTiles` second list (Deliverable 5d) |
| `sound-dev-opensoftal` | Implement the three new `IAudioSystem` concrete methods in `AudioSystem`: `acquireVehicleEnginePair` (callable from main thread — like all IAudioSystem methods; internally dispatches AL_VELOCITY zeroing to the audio thread per `AudioSystem`'s two-mutex design; returns opaque `{idleIdx, moveIdx}` or `{-1,-1}` if pool exhausted); `releaseVehicleEnginePair` (callable from main thread; internally stops and returns both sources to the pool on the audio thread); `updateVehicleAudio` (callable from main thread; internally executes AL_PITCH, AL_GAIN crossblend, and AL_POSITION updates on the audio thread). All AL calls within these implementations must execute on the audio thread — the methods themselves are main-thread entry points per the IAudioSystem contract. See `architecture/audio-architecture/audio-system.md` §Two-Mutex Design and `architecture/audio-architecture/dynamic-soundscape.md` §Vehicle Engine Audio. |
| `gamedesign-lookandfeel` | Confirm coverage radius colours, minimap tint palette, and signal visual behaviour match simulation intent; verify no V1 scope creep in visual wiring |
| `gamedesign-ux` | Add `kMinimapWidgetTop = 848`, `kMinimapWidgetTopOverlayActive = 732`, and `kMinimapWidgetLeft = 1576` to `src/ui/ui_constants.h` (Deliverable 3c); verify Inspector panel coverage-overlay UX (show on open, hide on close); confirm minimap overlay toggle interaction matches `architecture/ui-ux/minimap.md`; sign off on red preview feedback colour and blocked-tile drag-preview split (Deliverable 5d) |
| `test-dev-cpp` | Deliverable 0 (Day-One Commit): extend `MockRenderer` with updated `MOCK_METHOD` for `setTilePlacementPreview` (new two-list signature) and stubs for all six new `IRenderer` methods before any test files for Deliverables 3d, 4c, 5c, or 5e are written; author all eleven new unit tests (four traffic/coverage tests + four sim-level placement conflict tests + three UI-level placement feedback tests); extend `simulation_tests` and `ui_tests` CMake targets |
| `cicd-dev-github` | add checks #25–27 to `validate_assets.py` for vehicle atlas DDS format validation (Deliverable 2b); add guard steps 'Verify check_25 present' through 'Verify check_30 present' to the `validate-assets` job in `.github/workflows/ci.yml` for all six new checks (#25–30), following the established pattern for checks #21–24 (guard steps are added to the `validate-assets` job only — NOT to the `build-linux` preflight; the cicd role's responsibility for checks #28–30 is the guard steps only — the checks themselves are implemented in `validate_assets.py` by `graphics-artist-2d-texture` per Deliverables 2a/2c); Verify `validate-assets` CI job remains green after DDS rework; confirm `all-checks-pass` gate green |

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
  cause frame-time spikes. **Resolution**: `AgentState` is 20 bytes
  (`uint32_t agentId` + `int tileX` + `int tileZ` + `float headingDeg` + `ZoneType zone`);
  10,000 × 20 B × 60 fps ≈ 11.4 MB/s — well within DDR4 bandwidth (30–50 GB/s). Use simple
  `std::vector<AgentState>` value-copy; no dirty-flag or double-buffer strategy needed. Add a
  `DCHECK(copy_us < 500)` timing guard in DEBUG builds during Deliverable 3a integration to
  confirm the copy remains under 0.5 ms at runtime.
- **RISK**: Service coverage wireframe circle for a 800 m Fire Station radius will span many
  terrain chunks at varying heights. **Resolution**: terrain heights range 0–26 m on a default
  map, making screenspace projection unreliable across chunk boundaries. Go directly to the
  world-space tile-step polygon approach with `PolygonOffsetFactor=−1` and `EPO_FRONT` (same
  as intersection signal billboards — see
  `architecture/graphics-architecture/scene-graph-ownership.md` §Intersection Signal Billboard
  Registry). No flat-terrain screenspace prototype step is needed.
