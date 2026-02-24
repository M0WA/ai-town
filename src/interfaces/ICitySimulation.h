#pragma once
#include "simulation_types.h"
#include "ISimulationPauser.h"

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
    virtual int getCityRating() const = 0;  // 0-5 stars; called by HUD city-rating display

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
    // a 3-tick window. Exposed for Phase 8 save/load round-trip serialization only; the HUD demand
    // bars display getDemandPressurePct (the post-combination aggregate), not this value.
    // Cross-reference: implementation/phase-3.md (Traffic demand factor serialization).
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
    // Required for Phase 8 save round-trip test to verify counter persistence across save/load.
    // DensityUnlockState is defined in simulation_types.h:
    //   struct DensityUnlockState {
    //       int  consecutive_months_above_threshold[6];  // 0-2 range; one counter per density tier
    //       bool unlock_flags[6];                        // true if the corresponding tier is unlocked
    //   };
    // Phase 1 stub returns a default-constructed DensityUnlockState{}. Phase 3 fills in real impl.
    virtual DensityUnlockState getDensityUnlockState() const = 0;
};
