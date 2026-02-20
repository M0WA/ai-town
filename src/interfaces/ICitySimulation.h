#pragma once
#include "simulation_types.h"
#include "ISimulationPauser.h"

// ICitySimulation — Phase 0 stub.
// Full interface defined in Phase 1, but this stub with complete type definitions
// must exist at Phase 0 so UIManager and MockCitySimulation compile.
//
// ICitySimulation extends ISimulationPauser so UIManager can pass m_sim to
// NotificationManager as ISimulationPauser* without requiring a separate constructor
// parameter. setPaused(bool) is inherited from ISimulationPauser and implemented by
// CitySimulation — do NOT redeclare it here.
//
// Return type note: getUndoExpiryTimeSeconds() must return double (not float) — it uses
// the IClock::nowSeconds() return type directly. All other economy query methods
// (getTreasuryBalance, getCurrentMonthlyRevenue, getOutstandingDebt, estimateMonthlyUpkeep)
// return float.
class ICitySimulation : public ISimulationPauser {
public:
    virtual ~ICitySimulation() = default;
    // setPaused(bool paused) is inherited from ISimulationPauser — do not redeclare here.

    virtual void setSpeed(SpeedMultiplier speed) = 0;

    // State-query methods used by UIManager panels:
    virtual bool isPaused() const = 0;
    virtual SpeedMultiplier getSpeed() const = 0;

    // Economy/treasury queries — called by HUD resource bar and Budget Detail Panel:
    virtual float getTreasuryBalance() const = 0;          // Called by HUD resource bar to display treasury balance
    virtual float getCurrentMonthlyRevenue() const = 0;    // Called by Budget Detail Panel for net monthly balance line
    virtual float getOutstandingDebt() const = 0;          // Called by HUD persistent debt indicator
    virtual float estimateMonthlyUpkeep() const = 0;       // Called by grace period tooltip and Budget Detail Panel upkeep lines
    virtual float getNextUnlockThreshold(Difficulty d) const = 0; // Called by Density Unlock Preview Tooltip

    // City rating — called by HUD to display star rating:
    virtual int getCityRating() const = 0;  // 0-5 stars; called by HUD city-rating display

    // Demand pressure — called by HUD demand pressure bar per budget tick:
    // Required by architecture/game-design/traffic-system.md for Inspector panel demand readout.
    virtual float getDemandPressurePct(ZoneType zone) const = 0;  // Called by HUD demand pressure bar (R/C/I bars)

    // Undo state — called by HUD undo button:
    virtual bool hasUndoPendingAction() const = 0;           // Called by HUD undo button to gray out when no action is pending
    virtual double getUndoExpiryTimeSeconds() const = 0;     // Returns IClock::nowSeconds() value when the pending undo action expires; 0.0 if no action pending
};
