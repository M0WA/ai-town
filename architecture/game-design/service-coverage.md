# Service Coverage

- **Coverage model**: Each service building type (fire, police, power, water) provides coverage within a **radius** in world units. Coverage is boolean per tile (covered / not covered).

| Service type | Coverage radius |
|---|---|
| Fire station | 800 m |
| Police station | 600 m |
| Power plant | Entire connected power grid (graph traversal, not radius) |
| Water tower | 700 m |

- **Service level %**: For a service building, `service_level_pct = covered_tiles_in_range / total_buildable_tiles_in_range × 100`. Shown in Inspector panel. **Zero-tiles-in-range guard**: when `total_buildable_tiles_in_range == 0` (the building is placed where no buildable terrain tiles fall within the coverage radius), the Inspector panel displays **"N/A — no served zones in range"** rather than computing the formula (which would produce a meaningless 100% or a division-by-zero). **Definition of "buildable tiles"**: a terrain tile is buildable if its slope is ≤ 15° (i.e. slope ≤ 15.0°, consistent with the non-buildable threshold defined in [Terrain Interaction](terrain-interaction.md) as slope > 15.0°). This is a static map property determined at map generation time, not a runtime occupancy check — a tile occupied by a road or building is still counted as buildable for the purpose of this denominator. Therefore `total_buildable_tiles_in_range` is fixed for a given building placement and does not change as the city grows. The denominator is computed once at placement time and cached on the building entity.
- **Consequence curve** for uncovered residential zones: uncovered tile loses **`SimulationConstants::service_uncovered_desirability_penalty_per_tick` (= 5) desirability points per budget tick** (compounding; floor 0). This drives emigration (population decay) over several ticks if not addressed. The −5/tick penalty begins only at the budget tick **following** the first tick during which coverage was absent after having previously been present — a newly placed zone tile has no penalty during the first tick before the player can build service buildings. **Recovery rate**: when a tile transitions from uncovered to covered, desirability recovers at **`SimulationConstants::service_recovery_desirability_per_tick` (= 8) desirability points per budget tick** (60% faster than the penalty accumulation rate), making service infrastructure feel immediately rewarding. Recovery does not exceed the tile's adjacency-adjusted desirability ceiling.

  Phase 6 implementers must define both constants in the `SimulationConstants` struct:

  ```cpp
  // SimulationConstants (game-design/service-coverage.md)
  static constexpr int service_uncovered_desirability_penalty_per_tick = 5;  // −5/tick for uncovered residential zones
  static constexpr int service_recovery_desirability_per_tick          = 8;  // +8/tick recovery when coverage is restored
  ```

  These are the single authoritative values for service desirability adjustment. No magic numbers for these rates may appear elsewhere in the codebase — all code must reference `SimulationConstants::service_uncovered_desirability_penalty_per_tick` and `SimulationConstants::service_recovery_desirability_per_tick` directly.
- **Budget deficit degradation**: At −10% budget surplus (`budget_surplus_pct ≤ SimulationConstants::service_deficit_radius_halving_threshold`), service buildings randomly enter a **reduced coverage state** to represent workforce reduction; player is notified via toast. Each radius-based service building independently rolls `ISimulationRNG::nextFloat() < SimulationConstants::service_degradation_probability_per_tick` (= 0.5f) each budget tick while the deficit condition persists — 50% chance per building per tick. Phase 6 implementers must reference `SimulationConstants::service_degradation_probability_per_tick` at the degradation roll site; no magic number is permitted. **Audio on degradation**: only Fire, Police, and Water stations trigger `SFX_SERVICE_DEGRADE` on entering the reduced-coverage state. The Power plant uses a fully deterministic brownout model (described below) — it enters the degraded state silently with no `SFX_SERVICE_DEGRADE` audio event and no `ISimulationRNG` roll. For radius-based services (Fire, Police, Water), coverage radius is temporarily halved. For the Power plant (graph traversal model), degradation applies a **BFS-distance coverage reduction (brownout model)**: each power plant continues coverage only for nodes within BFS depth ≤ `floor(max_depth_i × 0.70)` from that plant, where `max_depth_i` is the longest BFS path from plant i to any reachable node in its connected subgraph. Equivalently, the farthest 30% of covered nodes by BFS depth become uncovered. **Definition of 'outermost'**: always means highest BFS depth from the power plant — not a random selection. **Multiple-plant behavior**: each plant independently applies its brownout; a tile remains covered if at least one plant's brownout depth still reaches it. **Disconnected-grid radial fallback**: if BFS from a Power plant cannot reach a candidate tile through placed tiles (disconnected grid — the tile and plant are on separate road/zone islands with no placed-tile path between them), coverage falls back to a radial-distance check using `computeServiceCoverageRadius(PowerPlant, degraded)` expressed in grid tiles. This ensures coverage is never silently dropped for isolated tiles due to a disconnected grid — the fallback treats them as radially reachable if within range. Upon deficit recovery (surplus returns above −10%), full coverage is restored. This model is deterministic and predictable: buildings farthest from the power plant lose power first, motivating strategic plant placement. Priority order of degradation: **Fire → Police → Water → Power** (Fire is degraded first — without active fire events, reduced fire coverage has no immediate population impact; Power is preserved longest because power outages affect all grid-connected zones simultaneously and trigger the fastest desirability collapse). **Do NOT degrade Water first** — Water degradation immediately applies −5 desirability/tick to all uncovered residential tiles, which triggers rapid emigration, lowers revenue, deepens the deficit, and accelerates further degradation in a positive-feedback death spiral. Fire → Police → Water → Power order breaks this cycle by protecting the highest-impact services longest. **No-service-building guard**: when zero service buildings of any type are placed, `anyUncovered` is set to `true` immediately (every residential tile is uncovered) — the grace-tick logic (`firstDesirabilityTick`) still suppresses the −5 desirability/tick penalty on the first tick, giving the player the standard one-tick window to respond.
- **Multiple building stacking**: Coverage radii from multiple service buildings of the same type do **not** stack — tiles are covered or not. Overlap is allowed for redundancy (counts once for desirability purposes).

## Utilities Tool — Placement Design (V1)

### Decision

Service buildings are **individually placed objects**, not zone tiles. The Utilities toolbar button
activates the Utilities tool mode, which presents a sub-panel listing the four service building
types. The player selects one type, then left-clicks a terrain tile to place that building at that
tile. This is a discrete placement action — not a zone designation that auto-generates buildings
from demand signals.

**Rationale**: Each service building type is a strategic, high-cost infrastructure anchor with a
fixed coverage radius. Placing them as zone tiles would imply auto-generation from demand, which
contradicts their design role: the player must deliberately decide where to put a power plant to
maximise the grid's BFS coverage, or where to position a fire station to cover a residential
district. A zone-based model would remove this strategic decision. The placement mechanic mirrors
SimCity's approach for infrastructure buildings.

### Utilities Sub-Panel

When `ActiveTool::Utilities` is selected, a compact sub-panel appears immediately to the right of
the toolbar (virtual x:80 px, y:176 px — aligned with the Utilities button row) showing the four
service building types as a 2×2 button grid:

| Column 1 | Column 2 |
|---|---|
| Power Plant | Water Tower |
| Fire Station | Police Station |

Active selection is highlighted. `UIManager` tracks `ServiceBuildingType m_selectedServiceBuilding`
(default: `ServiceBuildingType::PowerPlant`). Sub-panel is hidden when Utilities tool is not
active.

### `ICitySimulation` Method

```cpp
// Places a service building of the given type at the specified tile.
// Deducts placement cost immediately from the treasury.
// earthworksCostOverride: pre-computed earthworks cost (same convention as placeZone/placeRoad).
// Records an undo entry (expires at second budget tick after action).
// No-ops if the tile is already occupied by a service building (does not replace).
virtual void placeServiceBuilding(int tileX, int tileZ,
                                  ServiceBuildingType type,
                                  int earthworksCostOverride = 0) = 0;
```

### `ServiceBuildingType` Enum (in `simulation_types.h`)

```cpp
enum class ServiceBuildingType {
    PowerPlant,
    WaterTower,
    FireStation,
    PoliceStation
};
```

### Placement Costs

Placement cost is deducted immediately from the treasury at the moment the player places the
building, before any budget tick fires. These are `SimulationConstants` values and must not be
hardcoded inline.

| Building | Placement cost | `SimulationConstants` name |
|---|---|---|
| Power Plant | $10,000 | `service_placement_cost_power_plant` |
| Water Tower | $3,000 | `service_placement_cost_water_tower` |
| Fire Station | $5,000 | `service_placement_cost_fire_station` |
| Police Station | $4,000 | `service_placement_cost_police_station` |

**Cost rationale**: Power Plant is the most expensive ($10,000) because it provides city-wide
coverage through a BFS graph model and is the single largest infrastructure investment in the
early game. Fire Station ($5,000) is next — fire coverage has the largest radius (800 m) and the
highest upkeep ($500/tick), making it a significant commitment. Police Station ($4,000) is
slightly cheaper — smaller radius (600 m), lower upkeep ($400/tick). Water Tower ($3,000) is
the cheapest placement cost — comparable coverage radius to fire (700 m) but the lowest upkeep
($300/tick), making it accessible early to prevent the fast-feedback water-coverage desirability
penalty.

All four placement costs are calibrated to Normal difficulty starting funds ($500,000): placing
one of each costs $22,000 — 4.4% of starting capital — which is affordable alongside a
20-road-tile opening layout ($10,000 placement) without threatening the treasury before tax
revenue arrives.

Earthworks costs apply to service building placement under the same formula as `placeZone` and
`placeRoad`: `earthworksCost = (slope > 15.0°) ? static_cast<int>(500.0f * clamp((slope - 15.0f) / 30.0f, 0.0f, 2.0f)) : 0`.
The same insufficient-funds toast fires if earthworks cost exceeds treasury balance.

### V1 Scope

All four service building types are **mandatory for V1**. They are listed explicitly as core V1
systems in `architecture/game-design/minimum-viable-simulation.md` ("basic service coverage:
fire, police, power, water"). No service building type is deferred to post-V1.

### Placement Rules

- One service building per tile. Attempting to place a second building on an occupied tile is a
  no-op at the `CitySimulation` level (no cost deducted, no undo entry recorded). `CitySimulation`
  enforces this invariant — UIManager does not need a pre-placement `queryTile` call to check
  occupancy. `placeServiceBuilding` returning without effect (no treasury change, no undo entry)
  is the signal that the tile was already occupied; UIManager shows a Normal toast "Tile already
  occupied" by checking whether the treasury balance changed after the call (or by a returned
  bool — Phase 9b implementers may extend the return type of `placeServiceBuilding` to `bool` if
  a pre-placement occupancy signal is needed for the toast; the default `void` signature is
  sufficient for V1 if the toast is omitted).
- Service buildings can be placed on any buildable tile (slope ≤ 15.0° without earthworks; any
  slope with earthworks cost). They do **not** require a zoned tile — infrastructure can be
  placed on unzoned terrain.
- Service buildings are demolished via the Demolish tool (same as zones and roads). The
  demolish confirmation modal applies (same setting as zone/road demolish). Demolishing a
  service building removes coverage for all previously covered tiles on the next budget tick.

### Audio Callbacks for `placeServiceBuilding()` (Phase 9b)

`CitySimulation::placeServiceBuilding()` fires the following audio calls on successful placement
(i.e. the tile was not already occupied and cost deduction succeeds). No audio is emitted on
no-op (occupied tile) placements.

**Call sequence** (matches the pattern used by `placeZone()` — see `CitySimulation.cpp`):

1. If `earthworksCostOverride > 0` and `m_audio` is non-null:

   ```cpp
   m_audio->playPositionalSound(SFX_EARTHWORKS,
       vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
       SoundPriority::NORMAL, 1.0f);
   ```

2. If `m_audio` is non-null (unconditional on successful placement):

   ```cpp
   m_audio->playPositionalSound(SFX_BUILD_PLACE,
       vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
       SoundPriority::NORMAL, 1.0f);
   ```

**SoundId rationale**: `SFX_BUILD_PLACE` (SoundId = 1, `sfx_build_place.wav`) is shared by zone
placement and service building placement — both represent a "something was built here" feedback
event. A dedicated service-building SFX is post-V1. `SFX_ROAD_BUILD` (SoundId = 3) is reserved
for road placement only (distinct mechanic, distinct feedback tone).

**Y-position**: All three placement calls (`placeZone`, `placeRoad`, `placeServiceBuilding`) pass
`Y = 0.0f` for the audio world-space position. Real terrain height sampling for audio positioning
is **deferred to Phase 10**. The `ITerrainQuery::getHeightAt()` method added in Phase 9b
Deliverable E is used exclusively for zone overlay Y-height rendering (`IrrlichtRenderer::
setZoneOverlay`) and hover highlight rendering (`IrrlichtRenderer::setTileHoverHighlight`) —
it is NOT used to update audio call-site Y positions in Phase 9b. Phase 10 may refine
audio positioning by substituting `m_terrain->getHeightAt(tileX, tileZ)` for the `0.0f`
Y component if perceptible mispositioning is observed with real terrain heights.
