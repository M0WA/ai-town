#pragma once
#include "simulation_types.h"
#include "ISimulationPauser.h"
#include "IEconomyQuery.h"
#include "IZoningActions.h"
#include "ISimulationState.h"
#include <string>

// ICitySimulation — interface enabling UIManager to call simulation control methods without
// depending on the concrete CitySimulation class. Phase 0 stub contained the minimum required
// methods; Phase 1 locked the full interface below.
//
// Source location: src/interfaces/ICitySimulation.h
// MockCitySimulation lives in: tests/ui/mock_city_simulation.h
//
// ICitySimulation extends ISimulationPauser, IEconomyQuery, IZoningActions, and ISimulationState.
// setPaused(bool) is inherited from ISimulationPauser — do NOT redeclare it here.
//
// Return type note: getUndoExpiryTimeSeconds() must return double (not float) — it uses
// the IClock::nowSeconds() return type directly. All other economy query methods
// (getTreasuryBalance, getCurrentMonthlyRevenue, getOutstandingDebt, estimateMonthlyUpkeep)
// return float.
class ICitySimulation : public ISimulationPauser,
                        public IEconomyQuery,
                        public IZoningActions,
                        public ISimulationState {
public:
    virtual ~ICitySimulation() = default;
    // setPaused(bool paused) is inherited from ISimulationPauser — do not redeclare here.

    virtual void setSpeed(SpeedMultiplier speed) = 0;

    // State-query methods used by UIManager panels:
    virtual bool isPaused() const = 0;
    virtual SpeedMultiplier getSpeedMultiplier() const = 0;

    // reset — clear all city state and restart with the given starting funds.
    // Does NOT call clearCity() on the renderer — caller is responsible.
    // Does NOT reset tax rates.
    virtual void reset(int64_t startingFunds) = 0;

    // applyLoadedJson — restore city state from a JSON string previously produced by
    // serializeToJson(). Returns true on success; false on parse error.
    // Called by UIManager load game handler after loadMostRecentSave() returns JSON.
    virtual bool applyLoadedJson(const std::string& json) = 0;

    // --- Simulation event queue (Phase 6 delivery) ---
    // Drains one pending notification from the FIFO event queue per call; returns false when empty.
    // UIManager polls this each frame and posts the appropriate toast via NotificationManager.
    // CitySimulation does NOT call NotificationManager directly — this queue is the boundary.
    // Events queued: ForcedLoanIssued, BondIssued, ServiceDegraded, BudgetDeficitWarn.
    virtual bool pollPendingNotification(SimulationNotification& out) = 0;

    // consumeBudgetTicks — returns the number of budget ticks that fired since the last call
    // and resets the internal counter to zero.  Called once per frame by UIManager to forward
    // tick counts to SaveSystem::onBudgetTick() for the 5-tick auto-save gate.
    virtual int consumeBudgetTicks() = 0;
};
