#pragma once
#include "simulation_types.h"
#include "ISimulationPauser.h"
#include <vector>

// ICitySimulation — interface enabling UIManager to call simulation control methods without
// depending on the concrete CitySimulation class. Phase 0 stub contained the minimum required
// methods; Phase 1 locked the full interface below.
//
// Source location: src/interfaces/ICitySimulation.h
// MockCitySimulation lives in: tests/ui/mock_city_simulation.h
//
// ICitySimulation extends ISimulationPauser so UIManager can pass m_sim to
// NotificationManager as ICitySimulation*. setPaused(bool) is inherited from
// ISimulationPauser and implemented by CitySimulation — do NOT redeclare it here.
//
// Return type note: getUndoExpiryTimeSeconds() must return double (not float) — it uses
// the IClock::nowSeconds() return type directly. All other economy query methods
// (getTreasuryBalance, getCurrentMonthlyRevenue, getOutstandingDebt, estimateMonthlyUpkeep)
// return float.
//
// SEMANTIC DISTINCTION — getDemandPressurePct vs getTrafficDemandFactor:
//   getDemandPressurePct(ZoneType)  → float [0.0, 1.0]
//     The post-combination, post-floor, post-bootstrap AGGREGATE effective demand for the
//     given zone type across all tiles. This is what the HUD demand bars display. It combines
//     the traffic smoothstep signal, bootstrap decay, capacity-ratio signals, and demand floors.
//   getTrafficDemandFactor(ZoneType) → float [0.0, 1.0]
//     The INTERNAL traffic-only smoothstep multiplier from the rolling travel-time window,
//     BEFORE combining with bootstrap decay, capacity-ratio signals, or demand floors.
//     Exposed solely for Phase 8 save/load round-trip serialization of rolling-window state.
//     The HUD never reads this value directly.
// These two methods MUST NOT be collapsed into one — they serve different consumers at
// different points in the demand pipeline. See architecture/game-design/zoning-system.md
// (effective_demand_factor combination rule) and implementation/phase-3.md (serialization).
class ICitySimulation : public ISimulationPauser {
public:
    virtual ~ICitySimulation() = default;
    // setPaused(bool paused) is inherited from ISimulationPauser — do not redeclare here.

    virtual void setSpeed(SpeedMultiplier speed) = 0;

    // State-query methods used by UIManager panels:
    virtual bool isPaused() const = 0;
    virtual SpeedMultiplier getSpeedMultiplier() const = 0;

    // Economy/treasury queries — called by HUD resource bar and Budget Detail Panel:
    virtual float getTreasuryBalance() const = 0;          // Called by HUD resource bar to display treasury balance
    virtual float getCurrentMonthlyRevenue() const = 0;    // Called by Budget Detail Panel for net monthly balance line
    virtual float getOutstandingDebt() const = 0;          // Called by HUD persistent debt indicator
    virtual float estimateMonthlyUpkeep() const = 0;       // Called by grace period tooltip and Budget Detail Panel upkeep lines
    virtual float getNextUnlockThreshold(Difficulty d) const = 0; // Called by Density Unlock Preview Tooltip

    // City rating — called by HUD to display star rating:
    virtual CityRatingTier getCityRating() const = 0;  // called by HUD city-rating display; tier transitions trigger stinger_milestone

    // Demand pressure — called by HUD demand pressure bar per budget tick.
    // Returns the city-wide EFFECTIVE demand for the given zone type as a float in [0.0, 1.0].
    // This is the post-floor, post-bootstrap, post-combination aggregate value — a weighted average
    // of effective_demand_factor across all tiles of that zone type. The HUD demand bars display this
    // value directly. This is NOT the raw traffic demand factor; see getTrafficDemandFactor below.
    // Required by architecture/game-design/traffic-system.md for Inspector panel demand readout.
    // Cross-reference: architecture/game-design/zoning-system.md (effective_demand_factor combination rule).
    virtual float getDemandPressurePct(ZoneType zone) const = 0;

    // Traffic demand factor — returns the INTERNAL traffic-only smoothstep multiplier in [0.0, 1.0]
    // for the given zone type, derived from the rolling travel-time window BEFORE applying bootstrap
    // decay, demand floors, or capacity-ratio signals. R/C zones use a 5-tick window; I zones use
    // a 3-tick window.
    // Used by: (1) Phase 11 save/load round-trip serialization (rolling-window state persisted);
    //          (2) Phase 6 unit tests as a direct observation point for traffic null-path behavior
    //              (CommercialDemand_NullPath test — the only way to observe the raw smoothstep
    //               factor without going through the post-combination getDemandPressurePct).
    // The HUD demand bars display getDemandPressurePct (the post-combination aggregate), not this value.
    virtual float getTrafficDemandFactor(ZoneType zone) const = 0;

    // Population — called by HUD population display and density-unlock checks:
    virtual int getTotalPopulation() const = 0;  // Called by HUD population counter and density-unlock preview

    // Undo state — called by HUD undo button:
    virtual bool hasUndoPendingAction() const = 0;           // Called by HUD undo button to gray out when no action is pending
    virtual double getUndoExpiryTimeSeconds() const = 0;     // Returns IClock::nowSeconds() value when the pending undo action expires; 0.0 if no action pending

    // Game-over flow — deficit streak accessor. Returns the number of consecutive budget ticks
    // in which the deficit was >= -50%. Returns 0 during the grace period.
    // NotificationManager reads this to determine which progressive warning toast to fire:
    //   0 = no warning, 1 = "2 months to bankruptcy", 2 = "1 month to bankruptcy", 3+ = game-over trigger.
    // Cross-reference: architecture/game-design/game-over-flow.md.
    virtual int getConsecutiveDeficitMonths() const = 0;

    // Density-unlock state accessor — returns a snapshot of all density-unlock counters and flags.
    // Required for Phase 11 save round-trip test to verify counter persistence across save/load.
    // DensityUnlockState is defined in simulation_types.h (counter range 0-2 in saved state).
    // Phase 3 stub returns a default-constructed DensityUnlockState{}. Phase 6 fills in real impl.
    virtual DensityUnlockState getDensityUnlockState() const = 0;

    // --- Simulation time ---
    // Returns the current in-game date. Used by HUD resource bar to display month/year.
    // Phase 6 deliverable. Phase 8 HUD reads this via ICitySimulation::getSimulationTime().
    virtual SimulationTime getSimulationTime() const = 0;

    // --- Simulation event queue (Phase 6 delivery) ---
    // Drains one pending notification from the FIFO event queue per call; returns false when empty.
    // UIManager polls this each frame and posts the appropriate toast via NotificationManager.
    // CitySimulation does NOT call NotificationManager directly — this queue is the boundary.
    // Events queued: ForcedLoanIssued, BondIssued, ServiceDegraded, BudgetDeficitWarn.
    virtual bool pollPendingNotification(SimulationNotification& out) = 0;

    // --- Tax rate (Phase 6 delivery) ---
    // setTaxRate: bounds enforced at [0.01, 0.25] (1%–25%); invalid values clamped silently.
    // getTaxRate: returns current rate for the given zone type.
    // Both required by Phase 8 Tax Rate Panel.
    virtual void  setTaxRate(ZoneType zone, float rate) = 0;
    virtual float getTaxRate(ZoneType zone) const = 0;

    // --- Budget line-item accessors (Phase 6 delivery) ---
    // Required by Phase 8 BudgetDetailPanel to display 8 named line items.
    // All values are for the most-recently-completed budget tick (not projected).
    virtual float getTaxRevenue(ZoneType zone) const = 0;    // per-zone tax revenue
    virtual float getWagesCost() const = 0;                  // wages expense
    virtual float getRoadMaintenanceCost() const = 0;        // road maintenance expense
    virtual float getServiceUpkeepCost() const = 0;          // service upkeep expense
    virtual float getUtilityFeeRevenue() const = 0;          // utility fee revenue (power+water)

    // --- Zone/road action methods (Phase 6 delivery) ---
    // Called by UIManager input arbitration when the player places zones/roads or demolishes.
    // All placement actions record an undo entry (expires at second budget tick after action).
    // earthworksCostOverride: pre-computed earthworks cost from ITerrainQuery; 0 on flat tiles.
    //   The game loop (which owns TerrainSystem) computes this before calling placeZone/placeRoad.
    virtual void placeZone(int tileX, int tileZ, ZoneType type, DensityTier tier,
                           int earthworksCostOverride = 0) = 0;
    virtual void placeRoad(int tileX, int tileZ, int earthworksCostOverride = 0) = 0;
    virtual void demolishTile(int tileX, int tileZ) = 0;
    virtual void undoLastAction() = 0;

    // --- Per-tile query (Phase 6 delivery) ---
    // Returns tile data for the Query/Inspector Panel.
    // If the tile coordinates are out of bounds or unzoned, returns a default QueryResult
    // with isZoned=false. Never crashes on out-of-range coordinates.
    virtual QueryResult queryTile(int tileX, int tileZ) const = 0;

    // --- Bond use count (Phase 6 delivery) ---
    // Returns the number of Emergency Municipal Bond uses remaining for the current difficulty.
    // Required by Phase 8 forced loan modal Screen 2 to gray the Emergency Bond button.
    // Initialized at construction: Easy=3, Normal=2, Hard=1 (SimulationConstants::bond_max_uses_*).
    virtual int getOutstandingBondUses() const = 0;

    // --- Time of day (Phase 6 delivery) ---
    // Returns the current in-game time of day, derived from accumulated in-game hours.
    // Phase 6 implements internal hour tracking and exposes this accessor.
    // Phase 10 (Dynamic Soundscape) consumes this value to call IAudioSystem::setTimeOfDay().
    // IAudioSystem::setTimeOfDay() is NOT called in Phase 6 — only the accessor is delivered here.
    // TimeOfDay is defined in simulation_types.h (shared interface layer).
    virtual TimeOfDay getTimeOfDay() const = 0;
};
