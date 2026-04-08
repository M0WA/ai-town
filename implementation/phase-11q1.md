## Phase 11q1: Split `CitySimulation` into Sub-Systems and Introduce Sub-Interfaces

**Status: Planned**

**Prerequisite**: none. Independent of phase-11q2/11q3.

### Goal

Two violations addressed together:

1. `CitySimulation` (107 methods, 65 fields) violates `cpp:S1448` and `cpp:S1820`.
   The class is refactored into a thin coordinator that owns five focused sub-system
   objects. Every existing `ICitySimulation` method stays on `CitySimulation`; they
   delegate one level deeper.

2. `ICitySimulation` (44 methods) violates `cpp:S1448` (max 35). Rather than
   suppressing it, the interface is decomposed into three focused sub-interfaces —
   `IEconomyQuery`, `IZoningActions`, `ISimulationState` — which `ICitySimulation`
   extends. `ICitySimulation` then declares only 7 methods of its own (well under
   35). **All callers that hold `ICitySimulation*` are untouched** — the full method
   set is still reachable via inheritance.

---

### Sub-System Ownership

| Sub-system | File pair | Owns |
|---|---|---|
| `Economy` | `Economy.h/cpp` | Treasury, tax rates, loans, budget line items |
| `Traffic` | `Traffic.h/cpp` | Vehicles, signals, rolling windows, demand factors |
| `Zoning` | `Zoning.h/cpp` | Tile map, service buildings, power cache, building variants |
| `Population` | `Population.h/cpp` | Population, density unlock, city rating, music intensity |
| `SimTiming` | `SimTiming.h/cpp` | Simulation clock, speed/pause, time-of-day, budget tick accumulators |

All five files live under `src/simulation/`.

---

### Cross-System Dependencies

Sub-systems are **not** singletons and hold **no** raw pointers to each other.
`CitySimulation` passes the required sub-systems as `const` or mutable references
when calling tick methods that span boundaries:

```cpp
// CitySimulation::tick(float realDt) orchestration sketch
int budgetTicks = m_timing.tick(realDt, m_timing.getSpeedMultiplier());
if (m_timing.hasTimeOfDayChanged())               // fire setTimeOfDay only on transition
    m_audio->setTimeOfDay(m_timing.getTimeOfDay());
for (int i = 0; i < budgetTicks; ++i) {
    doBudgetTick();
}
// Per-frame updates (every frame, outside budget-tick loop)
m_traffic.doTrafficSignalTick(realDt, m_renderer, m_audio, m_clock);
m_traffic.doTrafficVehicleTick(realDt, m_zoning, m_renderer, m_audio);

// CitySimulation::doBudgetTick() orchestration sketch
bool inGracePeriod = (m_clock->nowSeconds() - m_timing.getConstructionTimeSeconds())
                     < SimulationConstants::grace_period_real_seconds;
m_zoning.buildPowerCoverageCache();
m_economy.computeEconomySnapshot(m_zoning, m_traffic, m_population, inGracePeriod);
m_traffic.computeTrafficDemand();
m_traffic.computeEffectiveDemand(m_zoning, m_timing.getTotalTicks());
m_zoning.doServiceDegradationTick(m_economy, *m_rng, m_audio, m_notifications);
m_zoning.doDesirabilityTick(m_economy, m_traffic, m_audio, m_notifications);
m_population.doPopulationTick(m_zoning, m_traffic, m_economy,
                              m_audio, m_renderer, m_notifications);
m_population.doDensityUnlockTick(m_zoning, m_traffic, m_economy, m_difficulty,
                                 m_renderer, m_audio, m_notifications);
m_zoning.doProximityTick(m_notifications);
m_economy.doEconomyTick(m_zoning, m_population, inGracePeriod,
                        m_audio, *m_clock, m_notifications);
m_population.doGameOverTick(m_economy, m_timing, *m_clock);
m_population.checkCityRatingTransition(m_notifications);
m_population.updateMusicIntensity(m_economy, m_audio);
```

---

### Deliverables

---

#### 0. Preparation

- [x] Read `CitySimulation.h` in full to confirm field-to-sub-system assignment
  matches the current source (phase authored from 2026-04-07 snapshot; verify no
  new fields were added since).
- [x] Check `CMakeLists.txt` for the `aitown_sim` (or equivalent) target to confirm
  the correct target name for `target_sources()` additions.

---

#### 1. `Economy` — new files

**`src/simulation/Economy.h`** — declare the struct and all its methods.

Fields to migrate from `CitySimulation.h`:

- `m_treasury` (`int64_t`)
- `m_taxRates` (`std::array<float, 3>`)
- `m_loans` (`std::vector<LoanEntry>`)
- `m_outstandingBondUses` (`int`)
- `m_loanCooldownTicks` (`int`)
- `m_firstRevenueTicked` (`bool`)
- `m_budgetSurplusPct` (`float`)
- `m_lastMonthTaxRevenue` (`std::array<float, 3>`)
- `m_lastMonthWagesCost` (`float`)
- `m_lastMonthRoadMaintenanceCost` (`float`)
- `m_lastMonthServiceUpkeepCost` (`float`)
- `m_lastMonthUtilityFeeRevenue` (`float`)
- `m_currentMonthlyRevenue` (`float`)
- `m_budgetWarnFired` (`bool`)

Public accessors (called by `CitySimulation` to implement `ICitySimulation`):
`getTreasuryBalance()`, `getCurrentMonthlyRevenue()`, `getOutstandingDebt()`,
`estimateMonthlyUpkeep()`, `getOutstandingBondUses()`,
`getTaxRate()`, `setTaxRate()`, `getTaxRevenue()`,
`getWagesCost()`, `getRoadMaintenanceCost()`,
`getServiceUpkeepCost()`, `getUtilityFeeRevenue()`.

Inter-sub-system read-only accessors (not part of `IEconomyQuery`; used only by `Population`):
`getBudgetSurplusPct() const` — read by `Population::doGameOverTick()` for the −50% game-over
threshold check and by `Population::updateMusicIntensity()` for music intensity tier determination.
`isFirstRevenueTicked() const` — read by `Population::doGameOverTick()` to gate forced-loan
checks before first revenue tick fires.

Tick methods called directly from `CitySimulation::doBudgetTick()`:
`computeEconomySnapshot(const Zoning&, const Traffic&, const Population&, bool inGracePeriod)`,
`doEconomyTick(Zoning&, const Population&, bool inGracePeriod,
               IAudioSystem*, IClock&,
               std::queue<SimulationNotification>&)`.

Tick methods called internally from `Economy::doEconomyTick()`:
`checkAndIssueForcedLoan(bool inGracePeriod, IClock&, IAudioSystem*,
                         std::queue<SimulationNotification>&)`,
`processLoanRepayments()`.

Private helpers (stay private to `Economy`):
`computeTaxRevenue(ZoneType, const Zoning&, const Traffic&) const`,
`computeWagesCost(int64_t) const`,
`computeServiceUpkeepCost(const Zoning&) const`,
`computeRoadMaintenanceCost(const Zoning&) const`,
`computeUtilityFeeRevenue(const Zoning&) const`,
`getDensityUnlockScale(const Population&) const`,
`computeBudgetSurplusPct(int64_t, int64_t) const`.

`LoanEntry` definition moves into `Economy.h` (remove from `CitySimulation.h`).

- [x] Create `src/simulation/Economy.h` with the struct definition, all field
  declarations (with their existing in-class initialisers), and all method
  declarations.
- [x] Create `src/simulation/Economy.cpp` — move implementation bodies from
  `CitySimulation.cpp` verbatim; update references to `m_*` fields that now
  live in this struct (they drop the `m_` prefix or keep it — choose consistently).
- [x] Add `Economy.cpp` to the simulation CMake target.
- [x] Update `architecture/game-design/economy-model.md` — change the sentence on
  approximately line 27 that describes `outstanding_bond_uses` as "tracked as a field
  in `CitySimulation` state" to "tracked as a field in the `Economy` sub-system
  (`src/simulation/Economy.h`)".
- [x] Update `architecture/audio-architecture/production-briefs/wav-sfx-production-brief.md`
  — change the trigger fields for:
  (a) `sfx_budget_warn` (approximately line 287) from `CitySimulation::tick()` to
      `Economy::doEconomyTick()` called from `CitySimulation::doBudgetTick()`;
  (b) `sfx_loan_issued` (approximately line 312) from `CitySimulation::tick()` to
      `Economy::checkAndIssueForcedLoan()` called from `Economy::doEconomyTick()`.
- [x] Update `architecture/game-design/economy-model.md` — in the "Phase 10 Audio
  Callbacks for Economy Events" section, change the call-site references on
  approximately lines 84–86, 99, 103, 119, and 123 that say `CitySimulation::tick()`
  or "CitySimulation internal methods" to reference `Economy::doEconomyTick()` (for
  `sfx_budget_warn`, called directly from `CitySimulation::doBudgetTick()`) and
  `Economy::checkAndIssueForcedLoan()` (for `sfx_loan_issued`, called internally
  from `Economy::doEconomyTick()`, not directly from `doBudgetTick()`).

---

#### 2. `Traffic` — new files

Fields to migrate from `CitySimulation.h`:

- `m_trafficVehicles` (`std::vector<TrafficVehicle>`)
- `m_nextVehicleId` (`uint32_t`)
- `m_trafficSignals` (`std::vector<TrafficSignal>`)
- `m_trafficWindowR[…]`, `m_trafficWindowC[…]`, `m_trafficWindowI[…]` (C-arrays)
- `m_trafficWindowIdxRC`, `m_trafficWindowIdxI` (`int`)
- `m_trafficDemandFactorR/C/I` (`float`)
- `m_roadSpeedFraction` (`float`)
- `m_demandPressurePct` (`std::array<float, 3>`)

`TrafficVehicle` and `TrafficSignal` struct definitions move into `Traffic.h`.

Public accessors:
`getZoneDemandFactor(ZoneType) const`,
`getTrafficDemandFactor(ZoneType) const`,
`getRoadSegmentSpeeds() const`,
`getAgentPositions() const`,
`getIntersectionSignalStates() const`.

Internal tick methods:
`computeTrafficDemand()`,
`computeEffectiveDemand(const Zoning&, int totalTicks)`,
`doTrafficSignalTick(float realDt, IRenderer*, IAudioSystem*, IClock*)`,
`doTrafficVehicleTick(float realDt, Zoning&, IRenderer*, IAudioSystem*)`,
`resetTrafficWindows()`,
`reset(IAudioSystem*)` — releases all vehicle engine source pairs (calls
`IAudioSystem::releaseVehicleEnginePair()` for each vehicle's idle+move pair) and clears `m_trafficVehicles`,
`m_trafficSignals`, and rolling-window arrays; called from
`CitySimulation::reset()`.

Private helpers:
`pickNextRoadTile(const Zoning&, int, int, int, int, int&, int&) const`,
`static smoothstep(float)`,
`static travelTimeDemand(float, float, float)`.

Called by `CitySimulation::placeRoad()` and `demolishTile()` to add/remove signals
and vehicles — these become `addSignalForTile()` / `removeSignalForTile()` /
`spawnVehiclesForRoad()` / `removeVehiclesForRoad()` methods on `Traffic`.

- [x] Create `src/simulation/Traffic.h` and `Traffic.cpp`.
- [x] Add to CMake target.
- [x] Update `architecture/audio-architecture/production-briefs/vehicle-sfx-production-brief.md`
  — change the vehicle-distance-cull trigger reference on approximately line 229 from
  `CitySimulation::tick()` to `Traffic::doTrafficSignalTick()` called from
  `CitySimulation::tick()` to reflect the method migration in this phase.
- [x] Update `architecture/game-design/traffic-system.md` — change the `sfx_intersection_tick`
  call-site and distance-cull specification on approximately lines 167–199 that reference
  `CitySimulation::tick()` as the implementation location:
  (a) change the call-site annotation on lines 171 and 175 from "In `CitySimulation::tick()`"
      to "In `Traffic::doTrafficSignalTick()`";
  (b) change the distance-cull paragraph on lines 194–199 that says "in `CitySimulation`,
      NOT in `AudioSystem`" and "in `CitySimulation::tick()`" to reference
      `Traffic::doTrafficSignalTick()` called from `CitySimulation::tick()` instead.
- [x] Update `architecture/audio-architecture/audio-system.md` — change the comment on
  approximately lines 193–194 that says "Vehicle engine source pairs are released by
  `CitySimulation::reset()` iterating `m_agents`" to reference `Traffic::reset(IAudioSystem*)`
  iterating `m_trafficVehicles`, called from `CitySimulation::reset()`, to reflect the
  traffic-vehicle lifecycle migrating to the `Traffic` sub-system in this phase.

---

#### 3. `Zoning` — new files

Fields to migrate from `CitySimulation.h`:

- `m_tiles` (`std::unordered_map<int64_t, TileData>`)
- `m_serviceBuildings` (`std::vector<ServiceBuilding>`)
- `m_roadTileCount` (`int`)
- `m_powerCoverageCache` (`std::unordered_set<int64_t>`)
- `m_upgradeRetryCount` (`std::unordered_map<int64_t, int>`)
- `m_upgradeBlocked` (`std::unordered_map<int64_t, bool>`)
- `m_buildingVariantCounters` (`std::array<int, 9>`)

`TileData` and `ServiceBuilding` struct definitions move into `Zoning.h`.

Public accessors:
`findTile(int, int)` (const + mutable overloads),
`roadTileCount() const`,
`getBuildingVariantCounter(int, int) const`,
`getServiceCoverage() const`,
`isWithinRoadRange(int, int, DensityTier) const`,
`queryTile(int, int) const`,
`isBuildableTile(int, int) const`,
`tiles() const` — returns `const std::unordered_map<int64_t, TileData>&`; used by `Population` to iterate all tiles for population accumulation, density-unlock candidate scanning, and desirability-weighted growth.

Internal tick methods:
`buildPowerCoverageCache()`,
`buildServiceCoverageMap(bool&, bool&, bool&, bool&)`,
`applyDesirabilityScores(bool, bool, bool, bool, const Economy&, IAudioSystem*,
                        std::queue<SimulationNotification>&)`,
`doDesirabilityTick(const Economy&, const Traffic&, IAudioSystem*,
                   std::queue<SimulationNotification>&)`,
`doServiceDegradationTick(const Economy&, ISimulationRNG&, IAudioSystem*,
                          std::queue<SimulationNotification>&)`,
`doProximityTick(std::queue<SimulationNotification>&)`.
<!-- doDesirabilityTick is a convenience wrapper that calls buildServiceCoverageMap then applyDesirabilityScores in sequence. -->

Private helpers:
`static tileKey(int, int)`,
`computeServiceCoverageRadius(ServiceBuildingType, bool) const`,
`computeRadialCoverage(int, int, ServiceBuildingType) const`,
`computePowerCoverage(int, int) const`,
`static footprintSize(DensityTier)`,
`static serviceFootprintSize()`,
`static nearestRoadDistance(…)`,
`static zoneAssetBaseName(ZoneType, DensityTier, int variantCounter)`,
`effectiveDemandForTile(const TileData&, const Traffic&) const`.

`CitySimulation`'s `placeZone()`, `placeRoad()`, `demolishTile()`,
`placeServiceBuilding()` methods call `Zoning` mutation methods; extract these
as `Zoning::placeZone(…)`, `Zoning::placeRoad(…)` etc., with
`CitySimulation` calling them and handling the `IRenderer` side-effects.

- [x] Create `src/simulation/Zoning.h` and `Zoning.cpp`.
- [x] Add to CMake target.
- [x] Update `architecture/asset-standards/3d-model-standards.md` — Variant Selection
  Policy section: (a) change the "Counter storage location" bullet from `CitySimulation`
  to `Zoning` sub-system (`src/simulation/Zoning.h/cpp`); (b) in the
  `buildingAssetBaseName()` code block, update the annotation from "implement as a
  `static` free function in `CitySimulation.cpp`" to "`Zoning.cpp`" and rename the
  function to `zoneAssetBaseName()`; (c) change the sentence "This function is internal
  to `CitySimulation.cpp`" to "`Zoning.cpp`"; (d) update the density-upgrade bullet
  from `CitySimulation::doDensityUnlockTick()` to `Population::doDensityUnlockTick()`
  and note that the variant counter is read from the `Zoning` sub-system by `Population`
  during upgrade;
  (e) change the sentence on line 175 ("`CitySimulation` maintains one `int`
      counter per unique zone-tier combination") to "`Zoning` sub-system
      (`src/simulation/Zoning.h`) maintains one `int` counter per unique
      zone-tier combination";
  (f) change the sentence on line 179 ("The counter is a plain `int` member of
      `CitySimulation` per zone-tier slot") to "The counter is a plain `int`
      member of the `Zoning` sub-system per zone-tier slot.";
  (g) change the sentence on approximately line 173 ("When `CitySimulation` places a
      zone tile, it selects a visual building variant") to "When a zone tile is placed,
      the `Zoning` sub-system selects a visual building variant using
      `zoneAssetBaseName()`".
- [x] Update `architecture/game-design/save-system.md` — change any reference from
  `CitySimulation::m_buildingVariantCounters` to `Zoning::m_buildingVariantCounters`
  to reflect the field migration in this phase.
- [x] Update `architecture/game-design/zoning-system.md` — change the sentence on
  line 215 that describes `upgradeRetryCount` and `upgradeBlocked` as tracked "on
  `CitySimulation`" to reference the `Zoning` sub-system
  (`src/simulation/Zoning.h/cpp`) instead.
- [x] Update `architecture/game-design/service-coverage.md` — change the reference
  on approximately line 339 that says `TileData` is "defined in
  `src/simulation/CitySimulation.h`" to "defined in `src/simulation/Zoning.h`"
  to reflect the struct migration in this phase.
- [x] Update `architecture/game-design/service-coverage.md` — change the code-snippet
  comments in approximately lines 183–281 (covering `sfx_service_degrade`,
  `sfx_power_out`, and `sfx_water_out`) that say the audio calls happen
  "inside `CitySimulation::tick()`" to reference `Zoning::doServiceDegradationTick()`
  called from `CitySimulation::doBudgetTick()` instead.
- [x] Update `architecture/game-design/service-coverage.md` — change the code-snippet
  comments in approximately lines 282–296 (covering `sfx_fire_alert` and
  `sfx_police_alert`) that say the audio calls happen "inside `CitySimulation::tick()`"
  to reference `Zoning::doDesirabilityTick()` called from `CitySimulation::doBudgetTick()`
  instead, consistent with the wav-sfx-production-brief.md update in this deliverable.
- [x] Update `architecture/audio-architecture/production-briefs/wav-sfx-production-brief.md`
  — change the trigger fields for:
  (a) `sfx_fire_alert` (approximately line 158) from `CitySimulation::tick()` to
      `Zoning::doDesirabilityTick()` called from `CitySimulation::doBudgetTick()`;
  (b) `sfx_police_alert` (approximately line 185) from `CitySimulation::tick()` to
      `Zoning::doDesirabilityTick()` called from `CitySimulation::doBudgetTick()`;
  (c) `sfx_power_out` (approximately line 223) from `CitySimulation::tick()` to
      `Zoning::doServiceDegradationTick()` called from `CitySimulation::doBudgetTick()`;
  (d) `sfx_water_out` (approximately line 245) from `CitySimulation::tick()` to
      `Zoning::doServiceDegradationTick()` called from `CitySimulation::doBudgetTick()`;
  (e) `sfx_service_degrade` (approximately line 262) from `CitySimulation::tick()` to
      `Zoning::doServiceDegradationTick()` called from `CitySimulation::doBudgetTick()`.
- [x] Update `architecture/graphics-architecture/scene-graph-ownership.md` — change the
  footprint-collision responsibility annotation on approximately line 492 from
  "the exclusive responsibility of `CitySimulation::placeZone()`" to
  "the exclusive responsibility of `Zoning::placeZone()` (called from
  `CitySimulation::placeZone()`)" to reflect that the actual footprint validation logic
  moves into the `Zoning` sub-system in this phase.

---

#### 4. `Population` — new files

Fields to migrate from `CitySimulation.h`:

- `m_totalPopulation`, `m_prevPopulation` (`int`)
- `m_cityRating` (`CityRatingTier`)
- `m_milestoneFired[5]` (`bool`)
- `m_consecutiveDeficitMonths` (`int`)
- `m_month1AutoSlowed` (`bool`)
- `m_lastSentMusicIntensity` (`MusicIntensity`)
- `m_densityUnlockState` (`DensityUnlockState`)

`UpgradeCandidate` struct moves into `Population.h` (or `Population.cpp`
if used only internally).

Public accessors:
`getTotalPopulation() const`,
`getCityRating() const`,
`getConsecutiveDeficitMonths() const`,
`getDensityUnlockState() const`,
`getNextUnlockThreshold(Difficulty) const`.

Internal tick methods:
`doPopulationTick(Zoning&, const Traffic&, const Economy&,
                  IAudioSystem*, IRenderer*,
                  std::queue<SimulationNotification>&)`,
`doDensityUnlockTick(Zoning&, const Traffic&, const Economy&, Difficulty,
                     IRenderer*, IAudioSystem*,
                     std::queue<SimulationNotification>&)`,
`doGameOverTick(const Economy&, SimTiming&, IClock&)`,
`checkCityRatingTransition(std::queue<SimulationNotification>&)`,
`updateMusicIntensity(const Economy&, IAudioSystem*)`.

Private helpers:
`accumulateHouseDemand(const Zoning&)`,
`static computeZoneGrowthDelta(float, float, int)`,
`static maxPopulationForTile(ZoneType, DensityTier)`,
`static getDensityUnlockThreshold(int)`,
`scanUnlockCandidates(const Zoning&, ZoneType, DensityTier) const`,
`applyDensityUpgrade(Zoning&, int, int, int64_t, ZoneType, DensityTier,
                     DensityTier, int&, IRenderer*, IAudioSystem*)`.

- [x] Create `src/simulation/Population.h` and `Population.cpp`.
- [x] Add to CMake target.
- [x] Update `architecture/audio-architecture/production-briefs/wav-sfx-production-brief.md`
  — change the trigger field on approximately line 330 for `sfx_zone_upgrade` from
  `CitySimulation::doDensityUnlockTick()` to `Population::doDensityUnlockTick()` to
  reflect the method migration in this phase.
- [x] Update `architecture/game-design/zoning-system.md` — change the references on
  approximately lines 314, 316, and 329 that refer to `CitySimulation::doDensityUnlockTick()`
  to `Population::doDensityUnlockTick()` to reflect the method migration in this phase.
- [x] Update `architecture/game-design/game-over-flow.md` — change the auto-slow
  implementation references on approximately lines 10–11 and 13 that say
  `CitySimulation::setSpeed(SpeedMultiplier::x1)` and `CitySimulation::tick()` to
  `Population::doGameOverTick()` calling `SimTiming::setSpeed(SpeedMultiplier::x1)`,
  both called from `CitySimulation::doBudgetTick()`.

---

#### 4b. `SimTiming` — new files

Fields to migrate from `CitySimulation.h`:

- `m_accumulatedSimSeconds` (`float`)
- `m_constructionTimeSeconds` (`double`)
- `m_totalTicks` (`int`)
- `m_pendingBudgetTicks` (`int`) — also backs `consumeBudgetTicks()`
- `m_month` (`int`)
- `m_year` (`int`)
- `m_speed` (`SpeedMultiplier`)
- `m_hoursAccumulator` (`float`)
- `m_timeOfDay` (`TimeOfDay`)

Public interface:

- `tick(float realDt, SpeedMultiplier speed)` — advances accumulators and returns
  number of budget ticks fired.
- `consumeBudgetTicks()` — returns and clears `m_pendingBudgetTicks`.
- `getSimulationTime() const` — returns `SimulationTime`.
- `getTimeOfDay() const` — returns `TimeOfDay`.
- `setSpeed(SpeedMultiplier)` — sets `m_speed`.
- `getSpeedMultiplier() const` — returns `m_speed`.
- `isPaused() const` — returns whether speed is paused.
- `setPaused(bool)` — sets paused state.
- `getConstructionTimeSeconds() const` — returns `m_constructionTimeSeconds`.
- `getTotalTicks() const` — returns `m_totalTicks`.
- `hasTimeOfDayChanged() const` — returns `true` if the most recent `tick()` call advanced
  `m_timeOfDay` to a new value; resets to `false` after `tick()` returns.

`CitySimulation` delegates `setSpeed`, `isPaused`, `getSpeedMultiplier`,
`getSimulationTime`, `getTimeOfDay` to `m_timing`.

- [x] Create `src/simulation/SimTiming.h` with the struct definition, all 9 field
  declarations (with their existing in-class initialisers), and all method
  declarations listed above.
- [x] Create `src/simulation/SimTiming.cpp` — move implementation bodies from
  `CitySimulation.cpp` verbatim; update references to fields that now live in
  this struct.
- [x] Add `SimTiming.cpp` to the simulation CMake target.

---

#### 5. Update `CitySimulation.h` and `CitySimulation.cpp`

**`CitySimulation.h`** after migration retains only 20 fields:

- Injected dependencies (6): `m_renderer`, `m_audio`, `m_rng`, `m_clock`,
  `m_terrain`, `m_difficulty`.
- Notification queue (1): `m_notifications`.
- Undo (4): `m_pendingUndo`, `m_undoExpiryWallSeconds`,
  `m_undoExpiryTickTarget`, `m_modalOpen`.
- Audio debounce (1): `m_lastPlacementSoundTime` (`double`).
- Scenario stub (1): `m_scenarioState`.
- Map dimensions (2): `m_mapWidth`, `m_mapHeight`.
- Sub-system members (5): `Economy m_economy`, `Traffic m_traffic`,
  `Zoning m_zoning`, `Population m_population`, `SimTiming m_timing`.

Private helpers that remain on `CitySimulation` (not extracted to sub-systems):

- `recordUndoAction(const UndoAction&)` — called from placement methods
- `UndoAction` struct definition
- `static speedValue(SpeedMultiplier)` — used by `tick()`
- `static startingFundsForDifficulty(Difficulty)` — used by constructor and `reset()`
- `static bondMaxUsesForDifficulty(Difficulty)` — used by `Economy`-related logic

All `ICitySimulation` method implementations become one-line delegations, e.g.:

```cpp
float CitySimulation::getTreasuryBalance() const {
    return m_economy.getTreasuryBalance();
}
int CitySimulation::getTotalPopulation() const {
    return m_population.getTotalPopulation();
}
```

`doBudgetTick()` becomes the cross-system orchestrator (sketch shown in the
Cross-System Dependencies section above).

- [x] Update `CitySimulation.h` — remove all migrated fields, remove private method
  declarations that moved to sub-systems, add five sub-system member declarations
  (`m_economy`, `m_traffic`, `m_zoning`, `m_population`, `m_timing`),
  add `#include` for each sub-system header.
- [x] Add `// NOSONAR cpp:S1448` to the class declaration opening line in
  `CitySimulation.h` with inline comment: "// NOSONAR cpp:S1448 — thin coordinator;
  44 overrides are delegation boilerplate". This suppresses the remaining S1448
  warning that cannot be eliminated without a different architecture.
- [x] Update `CitySimulation.cpp` — replace all migrated method bodies with
  one-line delegations. Update `doBudgetTick()`, `tick()`, `reset()`,
  `placeZone()`, `placeRoad()`, `demolishTile()`, `placeServiceBuilding()`,
  `serializeToJson()`, `deserializeFromJson()` to call sub-system methods in the
  correct order.
- [x] Update serialization: `serializeToJson()` calls
  `m_economy.serializeTo(j)`, `m_traffic.serializeTo(j)`,
  `m_zoning.serializeTo(j)`, `m_population.serializeTo(j)`,
  `m_timing.serializeTo(j)`. Each sub-system owns its own JSON section.
  `deserializeFromJson()` mirrors this.
- [x] Run `make test` — this builds with coverage instrumentation, runs all three test
  tiers (unit, integration, OpenGL), generates `coverage_filtered.info` via lcov, and
  enforces the ≥ 95% total coverage gate. Fix any regressions before continuing.
- [x] Run the per-file 85% coverage awk check from `architecture/testing/coverage.md`
  against `coverage_filtered.info` for each new sub-system file
  (`Economy.cpp`, `Traffic.cpp`, `Zoning.cpp`, `Population.cpp`, `SimTiming.cpp`).
  If any file is below 85%, add targeted unit tests in the relevant
  `tests/simulation/*_test.cpp` before proceeding to Deliverable 6.

---

#### 6. Decompose `ICitySimulation` into sub-interfaces

**New files** under `src/interfaces/`:

---

**`src/interfaces/IEconomyQuery.h`** — 12 methods (economy state + tax controls):

```cpp
#pragma once
#include "simulation_types.h"

class IEconomyQuery {
public:
    virtual ~IEconomyQuery() = default;
    virtual float getTreasuryBalance()        const = 0;
    virtual float getCurrentMonthlyRevenue()  const = 0;
    virtual float getOutstandingDebt()        const = 0;
    virtual float estimateMonthlyUpkeep()     const = 0;
    virtual void  setTaxRate(ZoneType zone, float rate) = 0;
    virtual float getTaxRate(ZoneType zone)   const = 0;
    virtual float getTaxRevenue(ZoneType zone) const = 0;
    virtual float getWagesCost()              const = 0;
    virtual float getRoadMaintenanceCost()    const = 0;
    virtual float getServiceUpkeepCost()      const = 0;
    virtual float getUtilityFeeRevenue()      const = 0;
    virtual int   getOutstandingBondUses()    const = 0;
};
```

---

**`src/interfaces/IZoningActions.h`** — 9 methods (placement mutations, tile
queries, undo):

```cpp
#pragma once
#include "simulation_types.h"

class IZoningActions {
public:
    virtual ~IZoningActions() = default;
    virtual void        placeZone(int x, int z, ZoneType t, DensityTier tier,
                                  int earthworksCost = 0) = 0;
    virtual void        placeRoad(int x, int z,
                                  int earthworksCost = 0) = 0;
    virtual void        demolishTile(int x, int z) = 0;
    virtual void        undoLastAction() = 0;
    virtual void        placeServiceBuilding(int x, int z,
                                             ServiceBuildingType type,
                                             int earthworksCost = 0) = 0;
    virtual QueryResult queryTile(int x, int z) const = 0;
    virtual bool        isWithinRoadRange(int x, int z,
                                          DensityTier tier) const = 0;
    virtual bool        hasUndoPendingAction()       const = 0;
    virtual double      getUndoExpiryTimeSeconds()   const = 0;
};
```

---

**`src/interfaces/ISimulationState.h`** — 15 methods (per-frame state for
rendering, audio, and population/progression queries):

```cpp
#pragma once
#include "simulation_types.h"
#include <vector>

class ISimulationState {
public:
    virtual ~ISimulationState() = default;
    virtual std::vector<AgentState>
        getAgentPositions()           const = 0;
    virtual std::vector<IntersectionSignalState>
        getIntersectionSignalStates() const = 0;
    virtual std::vector<RoadSegmentSpeed>
        getRoadSegmentSpeeds()        const = 0;
    virtual std::vector<ServiceCoverageTile>
        getServiceCoverage()          const = 0;
    virtual int       getMapTilesX()  const = 0;
    virtual int       getMapTilesZ()  const = 0;
    virtual SimulationTime getSimulationTime() const = 0;
    virtual TimeOfDay getTimeOfDay()  const = 0;
    virtual float getNextUnlockThreshold(Difficulty d) const = 0;
    virtual float       getZoneDemandFactor(ZoneType zone) const = 0;
    virtual float       getTrafficDemandFactor(ZoneType zone) const = 0;
    virtual int         getTotalPopulation()         const = 0;
    virtual CityRatingTier getCityRating()           const = 0;
    virtual int         getConsecutiveDeficitMonths() const = 0;
    virtual DensityUnlockState getDensityUnlockState() const = 0;
};
```

---

**Updated `src/interfaces/ICitySimulation.h`** — change base class list and
remove the 36 methods now declared in sub-interfaces. `ICitySimulation` retains
only 7 own method declarations:

```cpp
class ICitySimulation
    : public ISimulationPauser   // setPaused(bool) inherited
    , public IEconomyQuery
    , public IZoningActions
    , public ISimulationState {
public:
    virtual ~ICitySimulation() = default;
    // Speed / pause control
    virtual void            setSpeed(SpeedMultiplier speed) = 0;
    virtual bool            isPaused()             const = 0;
    virtual SpeedMultiplier getSpeedMultiplier()   const = 0;
    // Lifecycle
    virtual void   reset(int64_t startingFunds)               = 0;
    virtual bool   applyLoadedJson(const std::string& json)   = 0;
    // Event queue
    virtual bool   pollPendingNotification(SimulationNotification& out) = 0;
    // Simulation timing
    virtual int    consumeBudgetTicks() = 0;
};
```

Add `#include "IEconomyQuery.h"`, `#include "IZoningActions.h"`,
`#include "ISimulationState.h"` to `ICitySimulation.h`. Remove the 36
individual method declarations that moved.

**`CitySimulation.h`** adds `override` annotations on the sub-interface methods
(no functional change — they were already marked `override` against
`ICitySimulation`; the base class chain now traces through the sub-interfaces).

**`MockCitySimulation`** in `tests/` implements `ICitySimulation` and therefore
still inherits all sub-interface pure-virtual declarations — the mock already
has `MOCK_METHOD` entries for all 44 methods.
The spec's `MockCitySimulation` listing already shows the target name
`getZoneDemandFactor` (the spec was updated proactively). However, the **actual
source file** `tests/ui/MockCitySimulation.h` still uses `getDemandPressurePct`
and MUST be renamed as part of the codebase-wide rename in the deliverable below.
The spec note "no MOCK_METHOD changes are needed" refers only to the
sub-interface decomposition itself (method count stays at 44 — no methods are
added or removed); the rename from `getDemandPressurePct` to `getZoneDemandFactor`
IS required in source files.

**Deliverable checkboxes:**

- [x] Create `src/interfaces/IEconomyQuery.h` with the 12-method interface above.
- [x] Create `src/interfaces/IZoningActions.h` with the 9-method interface above.
- [x] Create `src/interfaces/ISimulationState.h` with the 15-method interface above.
- [x] In `src/interfaces/ISimulationState.h`, add the semantic-distinction comment
  block alongside `getZoneDemandFactor` explaining its inverse relationship with
  `QueryResult::demandPressurePct` (this comment currently lives in
  `ICitySimulation.h`; migrate it to `ISimulationState.h` where the method is now
  declared so that the critical inverse-semantics contract remains documented in
  the header where implementers and callers will encounter it). Also update the
  `src/interfaces/simulation_types.h` cross-reference comments (lines ~175-180) to
  reference `ISimulationState::getZoneDemandFactor` instead of
  `ICitySimulation::getDemandPressurePct`.
- [x] Update `src/interfaces/ICitySimulation.h` — change base class list to
  extend all three new interfaces, remove the 36 moved method declarations, add
  the three new `#include`s, retain the 7 own method declarations and the long
  explanatory comments that accompany them (semantic distinction notes, etc.).
- [x] Verify the current symbol name in `src/interfaces/ICitySimulation.h` — the source
  may use `getDemandPressurePct` (pre-11o) or `getDemandPressureFraction` (if 11o D-16
  was applied). In either case rename it to `getZoneDemandFactor` (the canonical target
  per testability-architecture.md). Adjust the grep/rename commands accordingly.
- [x] Rename `getDemandPressurePct` to `getZoneDemandFactor` across the codebase
  as part of this phase (the spec already uses the new name): update
  `src/interfaces/ICitySimulation.h`, `src/simulation/CitySimulation.h`,
  `src/simulation/CitySimulation.cpp`, `src/simulation/Traffic.h`,
  `src/interfaces/simulation_types.h` (update 4 comment references on
  lines ~175-180 that document the inverse-semantics of the method),
  all call sites in `src/ui/` (HUD.cpp, UIManager.cpp, or equivalent files that
  call `getDemandPressurePct`), and all test files under `tests/` that reference
  `getDemandPressurePct` (locate with `grep -r getDemandPressurePct tests/`).
  Update `MockCitySimulation`'s `MOCK_METHOD` entry for this method to use the
  new name.
- [x] Verify `MockCitySimulation` still compiles after the rename above.
- [x] Update `architecture/testing/testability-architecture.md` — (a) verify
  `IEconomyQuery`, `IZoningActions`, `ISimulationState` sub-interface
  descriptions are present and accurate; (b) update the `ICitySimulation` class definition to show the
  new base class list and 7-method own declaration; (c) note that
  `MockCitySimulation` `MOCK_METHOD` entries are structurally unchanged since
  all methods are still inherited through the sub-interfaces; (d) update the
  `AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem` test contract
  description to state that `Population::updateMusicIntensity()` (called from
  `CitySimulation::doBudgetTick()`) dispatches `IAudioSystem::setMusicIntensity()`,
  replacing the description that says the call lives in `CitySimulation::update()`;
  (e) change the `IRenderer::getListenerPosition()` comment on approximately lines 668
  and 802 from "Used by `CitySimulation::tick()` to perform the 80 m pre-acquisition
  distance cull" to "Used by `Traffic::doTrafficSignalTick()` (called from
  `CitySimulation::tick()`) to perform the 80 m pre-acquisition distance cull".
- [x] Update `architecture/game-design/economy-model.md` — change the references on
  approximately lines 134 and 150 that describe `CitySimulation::update()` as the
  call site for `audioSystem->setMusicIntensity()` to reference
  `Population::updateMusicIntensity()` called from `CitySimulation::doBudgetTick()`
  instead (reflecting the sub-system extraction in this phase).
- [x] Update `architecture/audio-architecture/audio-system.md` — change the references
  on approximately lines 57 and 222 that describe `CitySimulation::update()` as the
  call site for `setMusicIntensity()` to reference `Population::updateMusicIntensity()`
  called from `CitySimulation::doBudgetTick()` instead.
- [x] Update `architecture/audio-architecture/dynamic-soundscape.md`:
  (a) change all stale `CitySimulation` callback references in the Stinger vs SFX
      decision matrix on approximately line 147:
      — the SFX_BUDGET_WARN row "(CitySimulation via audio callback)" → reference
        `Economy::doEconomyTick()` called from `CitySimulation::doBudgetTick()`;
      — item (3) "service degradation: SFX_SERVICE_DEGRADE fires (CitySimulation
        audio callback)" → reference `Zoning::doServiceDegradationTick()` called
        from `CitySimulation::doBudgetTick()`;
  (b) change the implementation-contract paragraph on approximately line 157 that says
      "`CitySimulation::update()` wiring" to reference `Population::updateMusicIntensity()`
      called from `CitySimulation::doBudgetTick()`; the no-op guard sentence
      (`AudioSystem::setMusicIntensity()` deduplicates) is unchanged.
- [x] Run `make build` and fix any compiler errors.
- [x] Verify `ICitySimulation.h` now declares exactly 7 own methods.

---

### Exit Criteria

- [x] `npx markdownlint-cli 'implementation/phase-11q1.md'` — no errors.
- [x] All deliverable checkboxes above are checked.
- [x] `make build` passes with zero new warnings.
- [x] All three ctest suites pass with zero regressions.
- [x] `CitySimulation.h` has ≤ 20 fields (20 actual) (verify by counting).
  `CitySimulation` intentionally retains all 44 virtual override delegations plus
  private helpers — it will exceed 35 methods. Add `// NOSONAR cpp:S1448` to the
  class opening line in `CitySimulation.h` with the comment "thin coordinator —
  delegation overhead is expected".
- [x] Each of `Economy.h`, `Traffic.h`, `Zoning.h`, `Population.h`, `SimTiming.h`
  has ≤ 35 methods and ≤ 20 fields.
- [x] `ICitySimulation.h` declares exactly 7 own methods.
- [x] `IEconomyQuery.h` declares 12 methods, `IZoningActions.h` 9,
  `ISimulationState.h` 15.
- [x] SonarCloud re-scan shows `cpp:S1820` resolved on `src/simulation/CitySimulation.h`
  (field count drops from 65 to 20); `cpp:S1448` suppressed on
  `src/simulation/CitySimulation.h` via `// NOSONAR cpp:S1448` (delegation overhead
  is inherent to the coordinator pattern); `cpp:S1448` resolved on
  `src/interfaces/ICitySimulation.h` (own methods drop from 44 to 7, well under 35).
- [x] Code coverage gate (≥ 95%) maintained — no production logic deleted, only
  moved.
- [x] Each new sub-system `.cpp` file (`Economy.cpp`, `Traffic.cpp`, `Zoning.cpp`,
  `Population.cpp`, `SimTiming.cpp`) individually meets the per-file 85% line-coverage
  floor required by the Phase 11 CI gate.
