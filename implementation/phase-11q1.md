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
m_economy.computeEconomySnapshot(m_zoning, m_population, inGracePeriod);
m_traffic.computeTrafficDemand();
m_traffic.computeEffectiveDemand(m_zoning, m_timing.getTotalTicks());
m_zoning.doServiceDegradationTick(m_economy, *m_rng, m_audio, m_notifications);
m_zoning.doDesirabilityTick(m_economy, m_traffic);
m_population.doPopulationTick(m_zoning, m_traffic, m_economy,
                              m_audio, m_renderer, m_notifications);
m_population.doDensityUnlockTick(m_zoning, m_economy, m_difficulty,
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

- [ ] Read `CitySimulation.h` in full to confirm field-to-sub-system assignment
  matches the current source (phase authored from 2026-04-07 snapshot; verify no
  new fields were added since).
- [ ] Check `CMakeLists.txt` for the `aitown_sim` (or equivalent) target to confirm
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

Internal tick methods (called only from `CitySimulation::doBudgetTick()`):
`computeEconomySnapshot(const Zoning&, const Population&, bool inGracePeriod)`,
`doEconomyTick(Zoning&, const Population&, bool inGracePeriod,
               IAudioSystem*, IClock&,
               std::queue<SimulationNotification>&)`,
`checkAndIssueForcedLoan(bool inGracePeriod, IClock&,
                         std::queue<SimulationNotification>&)`,
`processLoanRepayments()`.

Private helpers (stay private to `Economy`):
`computeTaxRevenue(ZoneType, const Zoning&) const`,
`computeWagesCost(int64_t) const`,
`computeServiceUpkeepCost(const Zoning&) const`,
`computeRoadMaintenanceCost(const Zoning&) const`,
`computeUtilityFeeRevenue(const Zoning&) const`,
`getDensityUnlockScale(const Population&) const`,
`computeBudgetSurplusPct(int64_t, int64_t) const`.

`LoanEntry` definition moves into `Economy.h` (remove from `CitySimulation.h`).

- [ ] Create `src/simulation/Economy.h` with the struct definition, all field
  declarations (with their existing in-class initialisers), and all method
  declarations.
- [ ] Create `src/simulation/Economy.cpp` — move implementation bodies from
  `CitySimulation.cpp` verbatim; update references to `m_*` fields that now
  live in this struct (they drop the `m_` prefix or keep it — choose consistently).
- [ ] Add `Economy.cpp` to the simulation CMake target.

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
`resetTrafficWindows()`.

Private helpers:
`pickNextRoadTile(const Zoning&, int, int, int, int, int&, int&) const`,
`static smoothstep(float)`,
`static travelTimeDemand(float, float, float)`.

Called by `CitySimulation::placeRoad()` and `demolishTile()` to add/remove signals
and vehicles — these become `addSignalForTile()` / `removeSignalForTile()` /
`spawnVehiclesForRoad()` / `removeVehiclesForRoad()` methods on `Traffic`.

- [ ] Create `src/simulation/Traffic.h` and `Traffic.cpp`.
- [ ] Add to CMake target.

---

#### 3. `Zoning` — new files

Fields to migrate from `CitySimulation.h`:

- `m_tiles` (`std::unordered_map<int64_t, TileData>`)
- `m_serviceBuildings` (`std::vector<ServiceBuilding>`)
- `m_roadTileCount` (`int`)
- `m_powerCoverageCache` (`std::unordered_set<int64_t>`)
- `m_upgradeRetryCount` (`std::unordered_map<int64_t, int>`)
- `m_buildingVariantCounters` (`std::array<int, 9>`)

`TileData` and `ServiceBuilding` struct definitions move into `Zoning.h`.

Public accessors:
`findTile(int, int)` (const + mutable overloads),
`roadTileCount() const`,
`getBuildingVariantCounter(int, int) const`,
`getServiceCoverage() const`,
`isWithinRoadRange(int, int, DensityTier) const`,
`queryTile(int, int) const`,
`isBuildableTile(int, int) const`.

Internal tick methods:
`buildPowerCoverageCache()`,
`buildServiceCoverageMap(bool&, bool&, bool&, bool&)`,
`applyDesirabilityScores(bool, bool, bool, bool, const Economy&)`,
`doDesirabilityTick(const Economy&, const Traffic&)`,
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

- [ ] Create `src/simulation/Zoning.h` and `Zoning.cpp`.
- [ ] Add to CMake target.
- [ ] Update `architecture/asset-standards/3d-model-standards.md` — Variant Selection
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
      member of the `Zoning` sub-system per zone-tier slot."

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
`doDensityUnlockTick(Zoning&, const Economy&, Difficulty,
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

- [ ] Create `src/simulation/Population.h` and `Population.cpp`.
- [ ] Add to CMake target.

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

- [ ] Create `src/simulation/SimTiming.h` with the struct definition, all 9 field
  declarations (with their existing in-class initialisers), and all method
  declarations listed above.
- [ ] Create `src/simulation/SimTiming.cpp` — move implementation bodies from
  `CitySimulation.cpp` verbatim; update references to fields that now live in
  this struct.
- [ ] Add `SimTiming.cpp` to the simulation CMake target.

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

- [ ] Update `CitySimulation.h` — remove all migrated fields, remove private method
  declarations that moved to sub-systems, add five sub-system member declarations
  (`m_economy`, `m_traffic`, `m_zoning`, `m_population`, `m_timing`),
  add `#include` for each sub-system header.
- [ ] Add `// NOSONAR cpp:S1448` to the class declaration opening line in
  `CitySimulation.h` with inline comment: "// NOSONAR cpp:S1448 — thin coordinator;
  44 overrides are delegation boilerplate". This suppresses the remaining S1448
  warning that cannot be eliminated without a different architecture.
- [ ] Update `CitySimulation.cpp` — replace all migrated method bodies with
  one-line delegations. Update `doBudgetTick()`, `tick()`, `reset()`,
  `placeZone()`, `placeRoad()`, `demolishTile()`, `placeServiceBuilding()`,
  `serializeToJson()`, `deserializeFromJson()` to call sub-system methods in the
  correct order.
- [ ] Update serialization: `serializeToJson()` calls
  `m_economy.serializeTo(j)`, `m_traffic.serializeTo(j)`,
  `m_zoning.serializeTo(j)`, `m_population.serializeTo(j)`,
  `m_timing.serializeTo(j)`. Each sub-system owns its own JSON section.
  `deserializeFromJson()` mirrors this.
- [ ] Run `make build`. Fix all compiler errors before proceeding.
- [ ] Run `ctest -LE "integration|requires-opengl"` — zero regressions.
- [ ] Run `ctest -L "^integration$"` — zero regressions.
- [ ] Run `xvfb-run --auto-servernum ctest -L "^requires-opengl$"` — zero regressions.

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
has `MOCK_METHOD` entries for all 44 methods; the only change required is
renaming the `MOCK_METHOD` entry for `getDemandPressurePct` to
`getZoneDemandFactor` (method count stays at 44).

**Deliverable checkboxes:**

- [ ] Create `src/interfaces/IEconomyQuery.h` with the 12-method interface above.
- [ ] Create `src/interfaces/IZoningActions.h` with the 9-method interface above.
- [ ] Create `src/interfaces/ISimulationState.h` with the 15-method interface above.
- [ ] In `src/interfaces/ISimulationState.h`, add the semantic-distinction comment
  block alongside `getZoneDemandFactor` explaining its inverse relationship with
  `QueryResult::demandPressurePct` (this comment currently lives in
  `ICitySimulation.h`; migrate it to `ISimulationState.h` where the method is now
  declared so that the critical inverse-semantics contract remains documented in
  the header where implementers and callers will encounter it). Also update the
  `src/interfaces/simulation_types.h` cross-reference comments (lines ~175-180) to
  reference `ISimulationState::getZoneDemandFactor` instead of
  `ICitySimulation::getDemandPressurePct`.
- [ ] Update `src/interfaces/ICitySimulation.h` — change base class list to
  extend all three new interfaces, remove the 36 moved method declarations, add
  the three new `#include`s, retain the 7 own method declarations and the long
  explanatory comments that accompany them (semantic distinction notes, etc.).
- [ ] Verify the current symbol name in `src/interfaces/ICitySimulation.h` — the source
  may use `getDemandPressurePct` (pre-11o) or `getDemandPressureFraction` (if 11o D-16
  was applied). In either case rename it to `getZoneDemandFactor` (the canonical target
  per testability-architecture.md). Adjust the grep/rename commands accordingly.
- [ ] Rename `getDemandPressurePct` to `getZoneDemandFactor` across the codebase
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
- [ ] Verify `MockCitySimulation` still compiles after the rename above.
- [ ] Update `architecture/testing/testability-architecture.md` — (a) add
  `IEconomyQuery`, `IZoningActions`, `ISimulationState` sub-interface
  descriptions; (b) update the `ICitySimulation` class definition to show the
  new base class list and 7-method own declaration; (c) note that
  `MockCitySimulation` `MOCK_METHOD` entries are structurally unchanged since
  all methods are still inherited through the sub-interfaces.
- [ ] Run `make build` and fix any compiler errors.
- [ ] Verify `ICitySimulation.h` now declares exactly 7 own methods.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q1.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` passes with zero new warnings.
- [ ] All three ctest suites pass with zero regressions.
- [ ] `CitySimulation.h` has ≤ 20 fields (20 actual) (verify by counting).
  `CitySimulation` intentionally retains all 44 virtual override delegations plus
  private helpers — it will exceed 35 methods. Add `// NOSONAR cpp:S1448` to the
  class opening line in `CitySimulation.h` with the comment "thin coordinator —
  delegation overhead is expected".
- [ ] Each of `Economy.h`, `Traffic.h`, `Zoning.h`, `Population.h`, `SimTiming.h`
  has ≤ 35 methods and ≤ 20 fields.
- [ ] `ICitySimulation.h` declares exactly 7 own methods.
- [ ] `IEconomyQuery.h` declares 12 methods, `IZoningActions.h` 9,
  `ISimulationState.h` 15.
- [ ] SonarCloud re-scan shows `cpp:S1820` resolved on `src/simulation/CitySimulation.h`
  (field count drops from 65 to 20); `cpp:S1448` suppressed on
  `src/simulation/CitySimulation.h` via `// NOSONAR cpp:S1448` (delegation overhead
  is inherent to the coordinator pattern); `cpp:S1448` resolved on
  `src/interfaces/ICitySimulation.h` (own methods drop from 44 to 7, well under 35).
- [ ] Code coverage gate (≥ 95%) maintained — no production logic deleted, only
  moved.
