#pragma once
#include "simulation_constants.h"
#include "src/interfaces/simulation_types.h"

#include <array>
#include <queue>
#include <vector>

// Forward declarations for cross-system references
class Zoning;
class Traffic;
class Population;
class IAudioSystem;
class IClock;

// LoanEntry — individual loan or bond record.
// Moved from CitySimulation.h (Phase 11q1 decomposition).
struct LoanEntry {
    int64_t principal{0};
    int64_t remainingPrincipal{0};
    int     ticksRemaining{0};
    bool    isBond{false};
};

// Economy — sub-system owning all economy/treasury state for CitySimulation.
// Extracted from CitySimulation as part of Phase 11q1 decomposition.
class Economy {
public:
    // ---- Fields ----
    int64_t              m_treasury{0};
    std::array<float, 3> m_taxRates{0.05f, 0.05f, 0.05f};
    std::vector<LoanEntry> m_loans;
    int                  m_outstandingBondUses{0};
    int                  m_loanCooldownTicks{0};
    bool                 m_firstRevenueTicked{false};
    float                m_budgetSurplusPct{0.0f};
    std::array<float, 3> m_lastMonthTaxRevenue{};
    float                m_lastMonthWagesCost{0.0f};
    float                m_lastMonthRoadMaintenanceCost{0.0f};
    float                m_lastMonthServiceUpkeepCost{0.0f};
    float                m_lastMonthUtilityFeeRevenue{0.0f};
    float                m_currentMonthlyRevenue{0.0f};
    bool                 m_budgetWarnFired{false};

    // ---- Public accessors ----
    float   getTreasuryBalance()       const;
    float   getCurrentMonthlyRevenue() const;
    float   getOutstandingDebt()       const;
    float   estimateMonthlyUpkeep(const Zoning& zoning, double clockNow, double constructionTime) const;
    int     getOutstandingBondUses()   const;

    void    setTaxRate(ZoneType zone, float rate);
    float   getTaxRate(ZoneType zone) const;

    float   getTaxRevenue(ZoneType zone) const;
    float   getWagesCost()              const;
    float   getRoadMaintenanceCost()    const;
    float   getServiceUpkeepCost()      const;
    float   getUtilityFeeRevenue()      const;

    // Cross-system read-only accessors (not on any interface)
    float   getBudgetSurplusPct()  const;
    bool    isFirstRevenueTicked() const;

    // ---- Tick methods ----
    int64_t computeEconomySnapshot(const Zoning& zoning, const Traffic& traffic,
                                   const Population& population, bool inGracePeriod);
    void    doEconomyTick(Zoning& zoning, const Population& population, bool inGracePeriod,
                          IAudioSystem* audio, IClock& clock,
                          std::queue<SimulationNotification>& notifications);
    void    checkAndIssueForcedLoan(bool inGracePeriod, IClock& clock,
                                   IAudioSystem* audio,
                                   std::queue<SimulationNotification>& notifications);
    void    processLoanRepayments();

    // ---- Helpers ----
    float   computeBudgetSurplusPct(int64_t revenue, int64_t expenses) const;
    float   getDensityUnlockScale(const Population& population, Difficulty difficulty) const;

    // Initial funds setup
    void    setInitialFunds(int64_t funds, Difficulty difficulty);

private:
    int64_t computeTaxRevenue(ZoneType zone, const Zoning& zoning, const Traffic& traffic) const;
    int64_t computeWagesCost(int64_t totalCIRevenue) const;
    int64_t computeServiceUpkeepCost(const Zoning& zoning) const;
    int64_t computeRoadMaintenanceCost(const Zoning& zoning) const;
    int64_t computeUtilityFeeRevenue(const Zoning& zoning) const;
};
