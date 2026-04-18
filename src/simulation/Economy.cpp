// Economy.cpp — economy/treasury sub-system for CitySimulation.
// Extracted verbatim from CitySimulation.cpp (Phase 11q1 decomposition).

#include "Economy.h"
#include "Zoning.h"
#include "Traffic.h"
#include "Population.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/IClock.h"
#include "src/interfaces/sound_ids.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

float Economy::getTreasuryBalance() const {
    return static_cast<float>(m_treasury);
}

float Economy::getCurrentMonthlyRevenue() const {
    return m_currentMonthlyRevenue;
}

float Economy::getOutstandingDebt() const {
    int64_t total = 0;
    for (const LoanEntry& loan : m_loans) {
        total += loan.remainingPrincipal;
    }
    return static_cast<float>(total);
}

float Economy::estimateMonthlyUpkeep(const Zoning& zoning, double clockNow, double constructionTime) const {
    if ((clockNow - constructionTime) < SimulationConstants::grace_period_real_seconds) {
        return 0.0f;
    }
    return static_cast<float>(computeServiceUpkeepCost(zoning) + computeRoadMaintenanceCost(zoning));
}

int Economy::getOutstandingBondUses() const {
    return m_outstandingBondUses;
}

void Economy::setTaxRate(ZoneType zone, float rate) {
    rate = std::min(0.25f, std::max(0.01f, rate));
    m_taxRates[static_cast<int>(zone)] = rate;
}

float Economy::getTaxRate(ZoneType zone) const {
    return m_taxRates[static_cast<int>(zone)];
}

float Economy::getTaxRevenue(ZoneType zone) const {
    return m_lastMonthTaxRevenue[static_cast<int>(zone)];
}

float Economy::getWagesCost() const {
    return m_lastMonthWagesCost;
}

float Economy::getRoadMaintenanceCost() const {
    return m_lastMonthRoadMaintenanceCost;
}

float Economy::getServiceUpkeepCost() const {
    return m_lastMonthServiceUpkeepCost;
}

float Economy::getUtilityFeeRevenue() const {
    return m_lastMonthUtilityFeeRevenue;
}

float Economy::getBudgetSurplusPct() const {
    return m_budgetSurplusPct;
}

bool Economy::isFirstRevenueTicked() const {
    return m_firstRevenueTicked;
}

// ---------------------------------------------------------------------------
// computeBudgetSurplusPct
// ---------------------------------------------------------------------------

float Economy::computeBudgetSurplusPct(int64_t revenue, int64_t expenses) const {
    if (revenue == 0) {
        return (expenses > 0) ? -1.0f : 0.0f;
    }
    return static_cast<float>(revenue - expenses) / static_cast<float>(revenue);
}

// ---------------------------------------------------------------------------
// getDensityUnlockScale
// ---------------------------------------------------------------------------

float Economy::getDensityUnlockScale(const Population& /*population*/, Difficulty difficulty) const {
    switch (difficulty) {
        case Difficulty::Easy:   return SimulationConstants::density_unlock_scale_easy;
        case Difficulty::Normal: return SimulationConstants::density_unlock_scale_normal;
        case Difficulty::Hard:   return SimulationConstants::density_unlock_scale_hard;
    }
    return SimulationConstants::density_unlock_scale_normal;
}

// ---------------------------------------------------------------------------
// setInitialFunds
// ---------------------------------------------------------------------------

void Economy::setInitialFunds(int64_t funds, Difficulty difficulty) {
    m_treasury = funds;
    m_outstandingBondUses = [](Difficulty d) {
        switch (d) {
            case Difficulty::Easy:   return SimulationConstants::bond_max_uses_easy;
            case Difficulty::Normal: return SimulationConstants::bond_max_uses_normal;
            case Difficulty::Hard:   return SimulationConstants::bond_max_uses_hard;
        }
        return SimulationConstants::bond_max_uses_normal;
    }(difficulty);
}

// ---------------------------------------------------------------------------
// computeEconomySnapshot
// ---------------------------------------------------------------------------

int64_t Economy::computeEconomySnapshot(const Zoning& zoning, const Traffic& traffic,
                                        const Population& /*population*/, bool inGracePeriod) {
    int64_t taxRevR = computeTaxRevenue(ZoneType::Residential, zoning, traffic);
    int64_t taxRevC = computeTaxRevenue(ZoneType::Commercial, zoning, traffic);
    int64_t taxRevI = computeTaxRevenue(ZoneType::Industrial, zoning, traffic);

    int64_t totalCIRevenue = taxRevC + taxRevI;
    int64_t wages      = computeWagesCost(totalCIRevenue);
    int64_t svcUpkeep  = inGracePeriod ? 0LL : computeServiceUpkeepCost(zoning);
    int64_t roadMaint  = inGracePeriod ? 0LL : computeRoadMaintenanceCost(zoning);
    int64_t utilFees   = computeUtilityFeeRevenue(zoning);

    int64_t totalRevenue  = taxRevR + taxRevC + taxRevI + utilFees;
    int64_t totalExpenses = wages + svcUpkeep + roadMaint;

    m_budgetSurplusPct = computeBudgetSurplusPct(totalRevenue, totalExpenses);

    m_lastMonthTaxRevenue[0]       = static_cast<float>(taxRevR);
    m_lastMonthTaxRevenue[1]       = static_cast<float>(taxRevC);
    m_lastMonthTaxRevenue[2]       = static_cast<float>(taxRevI);
    m_lastMonthWagesCost           = static_cast<float>(wages);
    m_lastMonthRoadMaintenanceCost = static_cast<float>(roadMaint);
    m_lastMonthServiceUpkeepCost   = static_cast<float>(svcUpkeep);
    m_lastMonthUtilityFeeRevenue   = static_cast<float>(utilFees);
    m_currentMonthlyRevenue        = static_cast<float>(totalRevenue);

    return totalRevenue - totalExpenses;
}

// ---------------------------------------------------------------------------
// doEconomyTick
// ---------------------------------------------------------------------------

void Economy::doEconomyTick(Zoning& zoning, const Population& population, bool inGracePeriod,
                            IAudioSystem* audio, IClock& clock,
                            std::queue<SimulationNotification>& notifications) {
    // computeEconomySnapshot() was already called in doBudgetTick() before this
    // method runs.  It wrote m_budgetSurplusPct and all m_lastMonth* members.
    // Re-call it here to get the net for treasury update — the snapshot values
    // may have changed if called multiple times per tick (not in V1, but safe).
    // The caller passes the Zoning and Traffic refs; we use the overload that
    // takes them.  However, the original CitySimulation called computeEconomySnapshot()
    // again in doEconomyTick().  To keep behavior identical, the caller
    // (CitySimulation::doBudgetTick) will call computeEconomySnapshot a second time
    // and pass the net to applyEconomyNet() or we just recompute from cache.
    //
    // For verbatim fidelity: the original doEconomyTick re-called computeEconomySnapshot
    // and used its return value.  Since the caller already called it, the cached
    // m_lastMonth* members are identical, so recomputing net from cache is equivalent.
    int64_t totalRevenue  = static_cast<int64_t>(m_lastMonthTaxRevenue[0]) +
                            static_cast<int64_t>(m_lastMonthTaxRevenue[1]) +
                            static_cast<int64_t>(m_lastMonthTaxRevenue[2]) +
                            static_cast<int64_t>(m_lastMonthUtilityFeeRevenue);
    int64_t totalExpenses = static_cast<int64_t>(m_lastMonthWagesCost) +
                            static_cast<int64_t>(m_lastMonthRoadMaintenanceCost) +
                            static_cast<int64_t>(m_lastMonthServiceUpkeepCost);
    int64_t net = totalRevenue - totalExpenses;

    m_treasury += net;

    processLoanRepayments();

    if (m_currentMonthlyRevenue > 0.0f) {
        m_firstRevenueTicked = true;
    }

    if (m_firstRevenueTicked && m_budgetSurplusPct <= -0.25f) {
        if (!m_budgetWarnFired) {
            m_budgetWarnFired = true;
            notifications.push({NotificationType::BudgetDeficitWarn, 0, 0, 0});
            if (audio) {
                audio->playSound(SFX_BUDGET_WARN, SoundPriority::NORMAL, 1.0f);
            }
        }
    } else {
        m_budgetWarnFired = false;
    }

    checkAndIssueForcedLoan(inGracePeriod, clock, audio, notifications);
}

// ---------------------------------------------------------------------------
// checkAndIssueForcedLoan
// ---------------------------------------------------------------------------

void Economy::checkAndIssueForcedLoan(bool inGracePeriod, IClock& /*clock*/,
                                      IAudioSystem* audio,
                                      std::queue<SimulationNotification>& notifications) {
    if (m_budgetSurplusPct > -0.25f) return;
    if (inGracePeriod) return;
    if (!m_firstRevenueTicked) return;
    if (m_loanCooldownTicks > 0) return;

    int64_t outstandingDebt = 0;
    for (const LoanEntry& loan : m_loans) {
        outstandingDebt += loan.remainingPrincipal;
    }

    float revenueCap = std::max(m_currentMonthlyRevenue, 1000.0f);
    auto debtCap = static_cast<int64_t>(3.0f * revenueCap);

    if (outstandingDebt >= debtCap) {
        if (m_outstandingBondUses > 0) {
            int64_t bondPrincipal = 2LL * outstandingDebt;
            m_treasury += bondPrincipal;
            m_outstandingBondUses--;
            m_loanCooldownTicks = SimulationConstants::loan_cooldown_ticks;

            LoanEntry bondLoan;
            bondLoan.principal         = bondPrincipal;
            bondLoan.remainingPrincipal = bondPrincipal;
            bondLoan.ticksRemaining    = SimulationConstants::bond_repayment_ticks;
            bondLoan.isBond            = true;
            m_loans.push_back(bondLoan);

            notifications.push({
                NotificationType::BondIssued,
                static_cast<int>(bondPrincipal),
                SimulationConstants::bond_repayment_ticks,
                0
            });
            if (audio) {
                audio->playSound(SFX_LOAN_ISSUED, SoundPriority::NORMAL, 1.0f);
            }
        }
        return;
    }

    float totalExpenses = m_lastMonthWagesCost + m_lastMonthRoadMaintenanceCost +
                          m_lastMonthServiceUpkeepCost;
    float totalRevenue  = m_currentMonthlyRevenue;
    float monthlyShortfall = std::max(0.0f, totalExpenses - totalRevenue);

    float principalF = std::max({monthlyShortfall * 3.0f,
                                  m_currentMonthlyRevenue * 0.5f,
                                  10000.0f});

    int64_t remainingDebtRoom = debtCap - outstandingDebt;
    if (remainingDebtRoom <= 0) return;

    int64_t principal = std::min(static_cast<int64_t>(principalF), remainingDebtRoom);
    if (principal <= 0) return;

    m_treasury += principal;
    m_loanCooldownTicks = SimulationConstants::loan_cooldown_ticks;

    LoanEntry newLoan;
    newLoan.principal          = principal;
    newLoan.remainingPrincipal = principal;
    newLoan.ticksRemaining     = SimulationConstants::loan_repayment_ticks;
    newLoan.isBond             = false;
    m_loans.push_back(newLoan);

    notifications.push({
        NotificationType::ForcedLoanIssued,
        static_cast<int>(principal),
        SimulationConstants::loan_repayment_ticks,
        0
    });
    if (audio) {
        audio->playSound(SFX_LOAN_ISSUED, SoundPriority::NORMAL, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// processLoanRepayments
// ---------------------------------------------------------------------------

void Economy::processLoanRepayments() {
    for (LoanEntry& loan : m_loans) {
        if (loan.ticksRemaining > 1) {
            int64_t repayment = loan.remainingPrincipal / static_cast<int64_t>(loan.ticksRemaining);
            m_treasury -= repayment;
            loan.remainingPrincipal -= repayment;

            float interest = static_cast<float>(loan.remainingPrincipal) *
                             (SimulationConstants::loan_interest_rate /
                              static_cast<float>(SimulationConstants::ticks_per_year));
            m_treasury -= static_cast<int64_t>(interest);

            loan.ticksRemaining--;
        } else {
            m_treasury -= loan.remainingPrincipal;
            loan.remainingPrincipal = 0;
        }
    }

    m_loans.erase(
        std::remove_if(m_loans.begin(), m_loans.end(),
            [](const LoanEntry& loan) {
                return loan.remainingPrincipal == 0 && loan.ticksRemaining <= 1;
            }),
        m_loans.end());
}

// ---------------------------------------------------------------------------
// Private: computeTaxRevenue
// ---------------------------------------------------------------------------

int64_t Economy::computeTaxRevenue(ZoneType zone, const Zoning& zoning, const Traffic& traffic) const {
    auto zoneIdx = static_cast<int>(zone);
    float taxRate = m_taxRates[zoneIdx];

    auto incomeForDensity = [](DensityTier d) -> int {
        switch (d) {
            case DensityTier::Low:    return SimulationConstants::base_income_per_resident_low;
            case DensityTier::Medium: return SimulationConstants::base_income_per_resident_medium;
            case DensityTier::High:   return SimulationConstants::base_income_per_resident_high;
        }
        return SimulationConstants::base_income_per_resident_low;
    };

    int64_t total = 0;
    for (auto& [key, tile] : zoning.tiles()) {
        if (!tile.isZoned || tile.zone != zone) continue;
        auto pop = static_cast<int>(tile.population);
        int income = incomeForDensity(tile.density);
        total += static_cast<int64_t>(static_cast<float>(income * pop) * taxRate);
    }

    // Apply congestion penalty
    float roadSpeedFraction = traffic.getRoadSpeedFraction();
    float penalty = 0.0f;
    if (roadSpeedFraction <= SimulationConstants::congestion_high_threshold) {
        penalty = SimulationConstants::congestion_penalty_high;
    } else if (roadSpeedFraction <= SimulationConstants::congestion_low_threshold) {
        penalty = SimulationConstants::congestion_penalty_medium;
    } else if (roadSpeedFraction <= SimulationConstants::congestion_none_threshold) {
        penalty = SimulationConstants::congestion_penalty_low;
    }

    total = static_cast<int64_t>(static_cast<float>(total) * (1.0f - penalty));
    return total;
}

// ---------------------------------------------------------------------------
// Private: computeWagesCost
// ---------------------------------------------------------------------------

int64_t Economy::computeWagesCost(int64_t totalCIRevenue) const {
    return static_cast<int64_t>(static_cast<float>(totalCIRevenue) *
                                 SimulationConstants::wage_fraction_of_revenue);
}

// ---------------------------------------------------------------------------
// Private: computeServiceUpkeepCost
// ---------------------------------------------------------------------------

int64_t Economy::computeServiceUpkeepCost(const Zoning& zoning) const {
    int64_t total = 0;
    for (const auto& sb : zoning.serviceBuildings()) {
        switch (sb.type) {
            case ServiceBuildingType::FireStation:
                total += SimulationConstants::service_upkeep_fire_station_per_tick; break;
            case ServiceBuildingType::PoliceStation:
                total += SimulationConstants::service_upkeep_police_station_per_tick; break;
            case ServiceBuildingType::WaterTower:
                total += SimulationConstants::service_upkeep_water_tower_per_tick; break;
            case ServiceBuildingType::PowerPlant:
                total += SimulationConstants::service_upkeep_power_plant_per_tick; break;
            default: break;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// Private: computeRoadMaintenanceCost
// ---------------------------------------------------------------------------

int64_t Economy::computeRoadMaintenanceCost(const Zoning& zoning) const {
    return static_cast<int64_t>(zoning.roadTileCount()) *
           static_cast<int64_t>(SimulationConstants::road_maintenance_cost_per_tile);
}

// ---------------------------------------------------------------------------
// Private: computeUtilityFeeRevenue
// ---------------------------------------------------------------------------

int64_t Economy::computeUtilityFeeRevenue(const Zoning& zoning) const {
    bool hasPower = false, hasWater = false;
    for (const auto& sb : zoning.serviceBuildings()) {
        if (sb.type == ServiceBuildingType::PowerPlant) hasPower = true;
        if (sb.type == ServiceBuildingType::WaterTower) hasWater = true;
    }

    int64_t total = 0;
    for (auto& [key, tile] : zoning.tiles()) {
        if (!tile.isZoned || tile.zone != ZoneType::Residential) continue;

        auto x = static_cast<int>(key >> 32);
        auto z = static_cast<int>(static_cast<uint32_t>(key));

        if (hasPower) {
            if (zoning.isPowerCovered(x, z)) {
                total += SimulationConstants::utility_fee_power_per_tile;
            }
        }
        if (hasWater) {
            float waterCov = zoning.computeRadialCoverage(x, z, ServiceBuildingType::WaterTower);
            if (waterCov > 0.0f) {
                total += SimulationConstants::utility_fee_water_per_tile;
            }
        }
    }
    return total;
}
