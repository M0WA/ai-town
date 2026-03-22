# Traffic System

- **Model**: Lightweight agent-based simulation with A* / Dijkstra pathfinding on the road graph
- **Road capacity**: Each road segment has a capacity (vehicles per unit length) and speed function: `speed = max(max_speed × min_speed_fraction, max_speed × (1 − occupancy / capacity))` where `min_speed_fraction = 0.05` (5% of max speed hard floor). This prevents agents from stopping permanently on fully-saturated segments. **Minimum road capacity**: every road segment has a capacity of at least **1 vehicle** to prevent division by zero in the speed formula. A newly placed road with zero agents has `occupancy = 0`, giving `speed = max_speed`. **Agent timeout**: agents that have not reached their destination within **120 simulation seconds** (120 real seconds at 1× speed) are despawned; their trip is logged as unserved and counted as an **extreme-travel-time trip** in the rolling demand average (equivalent to the maximum travel time for demand calculation purposes). **Timeout trips must NOT be classified as null-path trips** — a null-path trip means A* found no valid path at all; a timed-out trip had a valid path but was unable to complete it due to congestion. Conflating the two masks congestion problems behind null-path demand defaults (0.5 neutral), preventing the player from diagnosing gridlock. This prevents permanent gridlock from accumulating stuck agents.
- **Intersections**: Traffic signal cycles with configurable green/red durations (see §Intersections below)
- **Road parameters**: Default road max_speed = 50 km/h (in simulation units, 1 unit = 1 m/s, so max_speed = 13.9 units/s). Road segment capacity = 8 vehicles per tile of road length. A 4-tile road block has capacity 32. These values are calibrated so that at 10,000 agents spread across a 1024×1024 map with standard grid road spacing, congestion emerges as a meaningful late-game challenge, not an early-game constant state.
- **Zone-to-road adjacency ("serves" definition)**: A zone tile is served by a road segment if any **edge-adjacent tile** (4-directional cardinal neighbors only — north, south, east, west; diagonal neighbors do not count) contains a road segment. If no edge-adjacent tile of a zone tile contains a road, that zone tile has no valid path and falls into the null-path behavior defined below. This definition is used both for traffic demand coupling and for congestion tax-yield penalties.
- **Congestion threshold**: Graduated penalty applied to all zones served by a congested road segment:
  - Average segment speed 31–39% of max → −10% tax yield (speeds ≥ 40% are free-flow; no penalty — matches `#27AE60` Green band in `architecture/ui-ux/minimap.md` §Traffic Congestion overlay)
  - Average segment speed 21–30% of max → −18% tax yield
  - Average segment speed ≤ 20% of max → **−25% tax yield** (cap; does not stack further)
  - Tax yield penalty applies only to the zone's current-tick tax collection; does not affect stored treasury or demand scores
- **Demand coupling**: Residential demand uses a **5-tick rolling average** travel time to prevent cliff-edge demand collapse from transient congestion spikes. Demand decays with a smooth ease-out curve from 100% at T=25 simulation seconds to 0% at T=60 simulation seconds (driving distance at free-flow speed, where 1 simulation second = 1 real second at 1× speed; see [Simulation Time System](simulation-time.md)); zones where 5-tick-average travel time exceeds 60 simulation seconds receive no residential demand. Formula: `demand_factor = clamp(1 − smoothstep(25, 60, avg_travel_time), 0, 1)` where `smoothstep(edge0, edge1, t) = 3t²−2t³` (t normalized from edge0 to edge1). **Calibration note**: at default road speed of 13.9 m/s, T=25 s corresponds to a 350 m commute and T=60 s to an 835 m commute — these are calibrated for a city map where inter-zone distances typically range from 200–800 m.
- **Commercial and Industrial demand coupling**: Commercial demand uses a 5-tick rolling average travel time with the same smoothstep formula as Residential, but with a slightly wider tolerance window: decay from 100% at T=30 simulation seconds to 0% at T=65 simulation seconds. `demand_factor_commercial = clamp(1 − smoothstep(30, 65, avg_travel_time), 0, 1)`. **Rationale**: T=65 s is intentionally narrower than the prior T=90 s to prevent C demand from outliving the residential consumer base (R demand collapses at T=60 s). If C demand tolerance significantly exceeds R demand tolerance, Commercial zones remain highly demanded even after Residential zones have lost their consumers — decoupling the supply chain and masking congestion problems. At T=65 s, C demand falls to 0% only slightly after R demand (≈8% wider window), maintaining economic coupling without hiding gridlock. Industrial demand uses a 3-tick rolling average (supply chains respond faster to *changes* in congestion, but tolerate longer hauls): decay from T=40 to T=80 simulation seconds. `demand_factor_industrial = clamp(1 − smoothstep(40, 80, avg_travel_time), 0, 1)`. **Calibration rationale**: Industrial onset T=40 s (556 m at 13.9 m/s) is intentionally later than Residential (T=25) and Commercial (T=30), making Industrial the *most traffic-tolerant* zone type across the full travel-time range. This ensures congestion always suppresses Residential and Commercial demand before Industrial — consistent with the design intent that supply chains tolerate longer freight hauls. A prior T=20 onset inverted this relationship: Industrial demand collapsed first at low congestion (T=20 s onset < R onset T=25 s) while remaining resilient at high congestion (T=75 s cutoff > R cutoff T=60 s), creating an unintuitive double inversion that made congestion consequences undiagnosable. The 3-tick rolling average models fast reaction to *changes* (sharp transitions) but activates only after congestion has already exceeded R and C tolerance. Both C and I null-path behavior mirrors Residential: tiles with no valid path are excluded from the rolling average; if all ticks in the window have no valid path, `demand_factor` defaults to 0.5 (neutral).
- **Null-path behavior**: When A\* finds no valid path for a zone tile (empty road graph or disconnected tile), that tick contributes `null_path_demand_default = 0.5f` as its individual `traffic_demand_factor` value in the rolling average calculation. The fallback of 0.5 when all ticks in the window are null-path is a mathematical consequence of averaging N values each equal to 0.5 — it is NOT a separate post-average override applied after computing the rolling average. Null-path ticks are included in the rolling average computation, not excluded with a fallback applied afterward. For example: a rolling window with 3 valid-path ticks and 2 null-path ticks has average = (sum of 3 valid `traffic_demand_factor` values + 2 × 0.5) / 5. If all ticks in the rolling window for that zone type have no valid path (5 ticks for Residential and Commercial; 3 ticks for Industrial — matching each zone type's rolling average window size), `demand_factor` is 0.5 (neutral) rather than 0.0, giving the player time to build road connections before demand collapses. This prevents the city from deadlocking with zero demand on a blank map. **Partial window averaging (early ticks)**: During the first few budget ticks before the rolling window has filled (i.e., `totalTicks < window_size`), divide the sum by `min(totalTicks, window_size)` — not by the constant `window_size` — to avoid diluting valid samples with zero-initialized slots. For example: at tick 2 with a 5-tick window, divide by 2, not 5. Initializing the window slots to `null_path_demand_default = 0.5f` and always dividing by the constant window size would over-weight the null-path default in early ticks and under-state demand for cities that build roads immediately. The oscillation sign-off tables in the zoning-system.md (e.g., "T+1=3 | [1.0, 1.0, 0.5, 0.5] (4 samples) | 3.0/4 = 0.750") already reflect partial-window averaging — the denominator equals the number of samples recorded so far, not the window capacity.
- **Bootstrap oscillation invariant (testable constraint)**: During the first `demand_bootstrapping_ticks` budget ticks (ticks 0–5), demand values must not oscillate. Specifically, for any two consecutive ticks, `|demand_factor[tick+1] − demand_factor[tick]| < 1.0` for all zone types. A jump of 1.0 (the full [0.0, 1.0] range) indicates a sign-flip error in the bootstrap subsidy calculation. On a blank map at any simulation speed (including `SpeedMultiplier::x3`), `getTrafficDemandFactor()` and `getDemandPressurePct()` must remain bounded in [0.0, 1.0] with no tick-to-tick delta exceeding 1.0. Verified by `DemandBootstrap_AtX3Speed_NoBoundaryViolationAndNoOscillation` in `tests/simulation/zoning_test.cpp`.
- **Demand pressure readouts** (C/I feedback): When Commercial or Industrial demand prerequisites are unmet, the unmet demand must be surfaced to the player. The `QueryResult` struct exposes a `demand_pressure_pct` field per tile = `(1.0f − effective_demand_factor) × 100`, where `effective_demand_factor` is the post-combination demand in [0.0, 1.0]. **Inverse semantics**: 0 means fully satisfied demand (high traffic flow, maximum demand); 100 means zero effective demand (demand collapsed). **CRITICAL — Do NOT confuse with `ICitySimulation::getDemandPressurePct(ZoneType)`**, which returns the city-wide EFFECTIVE demand in [0.0, 1.0] (1.0 = maximum demand) — the opposite direction. `QueryResult::demandPressurePct` = `(1.0f − tileEffectiveDemandFactor) × 100.0f`; it is NOT `getDemandPressurePct(zone) × 100`. See `simulation_types.h QueryResult::demandPressurePct` for the canonical definition and cross-reference. This is shown in the Inspector panel for zone tiles and as a compact HUD indicator bar per zone type (R/C/I). Without this readout, an economy flatline cannot be distinguished from a design error by the player.

## Lane Assignment

Each road edge in the traffic graph is **directional**. Each physical road tile hosts **two directed edges** — one per lane direction — allowing vehicles to travel in both directions on the same road.

### Lane Offset and World Positioning

Vehicle agents are rendered at a **lane center offset** perpendicular to their direction of travel. This visual offset indicates which lane they are occupying:

- **Northbound (+Z) agent**: rendered at world X offset `+kLaneCenterOffset` from the tile center.
- **Southbound (−Z) agent**: rendered at world X offset `−kLaneCenterOffset` from the tile center.
- **Eastbound (+X) agent**: rendered at world Z offset `+kLaneCenterOffset` from the tile center.
- **Westbound (−X) agent**: rendered at world Z offset `−kLaneCenterOffset` from the tile center.

The `kLaneCenterOffset` constant is **1.875 metres** and must be defined in `src/rendering/render_constants.h` alongside other rendering constants (e.g., `road_lod2_color`). **Do NOT hardcode the literal `1.875f` at call sites** — always reference the named constant. The declaration is: `static constexpr float kLaneCenterOffset = 1.875f;` in `src/rendering/render_constants.h`.

### Intersection Tile Snap Rule

Lane offset is **applied only on straight road segments**. At intersection tiles — defined as tiles with 3 or more road neighbours, or tiles recorded in the intersection signal registry — agents **snap to the tile center X/Z** (lane offset = 0). Lane offset resumes on the **exit segment** once the agent leaves the intersection tile.

This snap rule prevents agents from appearing offset when navigating tight intersections, and ensures smooth visual turns through the intersection space.

## Intersections

Each road intersection is tracked by tile coordinate (`tileX`, `tileZ`). The simulation emits
`IntersectionSignalState` structs (see `simulation_types.h`) on each signal phase transition.

**Visual representation** (owned by `IrrlichtRenderer`):

- A billboard quad (2D signal indicator) is placed as a child scene node of the road mesh node
  at the intersection tile.
- Material: `EMT_TRANSPARENT_ADD_COLOR`; emissive colour is set per phase:
  - `SignalPhase::Green` → RGB (0, 220, 0) — bright green
  - `SignalPhase::Red`   → RGB (220, 0, 0)   — bright red
- Y-offset: +0.15 m above road surface to layer above terrain without Z-fighting.
- Polygon offset: `PolygonOffsetFactor = -1, PolygonOffsetDirection = EPO_FRONT` to prevent
  depth-buffer Z-fighting against the road quad at the same elevation.
- Scene node lifetime: nodes are created by `IrrlichtRenderer::setIntersectionSignalState()`
  on first call for a tile and reused on subsequent calls; nodes are destroyed when the road
  tile is removed.
- Node registry: intersection billboard nodes are stored in `m_intersectionNodes`
  (`std::unordered_map<TileKey, ISceneNode*>`), owned by `IrrlichtRenderer`, following the
  same ownership pattern as `m_agentNodes` (see Phase 11d scene-graph-ownership spec).

See also: `implementation/phase-11d.md` Deliverable 3b.

## Phase 10 Audio Callbacks for Traffic Events

### `sfx_road_build` — Road tile placed

**Call site**: `CitySimulation::placeRoad(int tileX, int tileZ, int earthworksCostOverride)`,
immediately after successful road placement and treasury deduction.

```cpp
// In CitySimulation::placeRoad(), after tile assignment:
if (m_audio) {
    if (earthworksCostOverride > 0) {
        m_audio->playPositionalSound(
            SFX_EARTHWORKS,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }
    m_audio->playPositionalSound(
        SFX_ROAD_BUILD,
        vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
        SoundPriority::NORMAL, 1.0f);
}
```

`SFX_ROAD_BUILD` = SoundId 3 (`sfx_road_build.wav`) — positional
(`AL_SOURCE_RELATIVE = AL_FALSE`), no EFX bypass. `SFX_EARTHWORKS` = SoundId 4 — positional,
EFX bypass, fired only when `earthworksCostOverride > 0`. Y = 0.0f (same convention as zone
placement calls — see service-coverage.md Audio Callbacks section).

### `sfx_build_demolish` — Road tile demolished

**Call site**: `CitySimulation::demolishTile(int tileX, int tileZ)` for road tiles, same
method as zone demolish. Reuses `SFX_BUILD_DEMOLISH` (SoundId 2).

```cpp
// In CitySimulation::demolishTile(), road branch:
if (m_audio) {
    m_audio->playPositionalSound(
        SFX_BUILD_DEMOLISH,
        vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
        SoundPriority::NORMAL, 1.0f);
}
```

### `sfx_intersection_tick` — Traffic signal phase change

**Trigger**: Fires each time a traffic intersection's signal cycle completes a full phase
transition (green→red or red→green on the controlling road segment). The intersection is
identified by its tile coordinates `(tileX, tileZ)`.

**Call site**: `CitySimulation::tick()`, inside the traffic signal update loop, at the moment
a signal phase changes.

```cpp
// In CitySimulation::tick(), traffic signal update loop:
if (signal.phaseChanged) {
    if (m_audio) {
        // Pre-acquisition distance cull: skip entirely if beyond audible range.
        // CitySimulation obtains the listener position from IRenderer::getListenerPosition()
        // (the Phase 11d addition — stores the camera position from the last setCamera() call).
        // Culling here prevents even the playPositionalSound() call for out-of-range signals,
        // reducing audio system overhead at large city scales (hundreds of intersections).
        const vec3 signalPos{static_cast<float>(signal.tileX), 0.0f,
                              static_cast<float>(signal.tileZ)};
        const vec3 listenerPos = m_renderer->getListenerPosition();
        if (distance(listenerPos, signalPos) > 80.0f) continue;  // skip — beyond cull range
        m_audio->playPositionalSound(
            SFX_INTERSECTION_TICK,
            signalPos,
            SoundPriority::LOW, 1.0f);
    }
}
```

`SFX_INTERSECTION_TICK` = SoundId 16 (`sfx_intersection_tick.wav`) — positional
(`AL_SOURCE_RELATIVE = AL_FALSE`), no EFX bypass (subtle ambient detail — occlusion is
acceptable). `SoundPriority::LOW` ensures this sound is the first evicted under pool
pressure. **Distance cull (Phase 11d — in `CitySimulation`, NOT in `AudioSystem`)**: The
80 m pre-acquisition cull is implemented in `CitySimulation::tick()` using
`m_renderer->getListenerPosition()` (the `IRenderer` method added in Phase 11d — see
`implementation/phase-11d.md`). `CitySimulation` skips the `playPositionalSound()` call
entirely when `distance(listenerPos, signalPos) > 80.0f`. This is the only SFX with an
explicit pre-acquisition distance cull at the `CitySimulation` call site. `AudioSystem`
does NOT implement a separate cull for this sound — the `SoundPriority::LOW` eviction
policy handles any residual pool pressure from signals that pass the 80 m cull.
