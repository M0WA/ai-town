// src/ui/BudgetDetailPanel.cpp
//
// BudgetDetailPanel — 320x200 px floating panel owned by HUD.
// Shows 8 named budget line items and an optional density unlock preview.

#include "src/ui/BudgetDetailPanel.h"
#include "src/interfaces/ICitySimulation.h"

#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Helper: format a float as a dollar string
// ---------------------------------------------------------------------------
static std::string fmtDollar(float value) {
    char buf[64];
    if (value < 0.0f) {
        std::snprintf(buf, sizeof(buf), "-$%.0f", -value);
    } else {
        std::snprintf(buf, sizeof(buf), "$%.0f", value);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Constructor — creates all panel elements, initially hidden
// ---------------------------------------------------------------------------
BudgetDetailPanel::BudgetDetailPanel(IUIBackend* backend, ICitySimulation* sim)
    : m_backend(backend)
    , m_sim(sim)
    , m_visible(false)
{
    if (!m_backend) return;

    // Panel anchored below the resource bar, left-aligned: x:8, y:60, 320x200 px
    constexpr int px = 8;
    constexpr int py = 60;
    constexpr int pw = 320;
    constexpr int lineH = 20;

    m_panelBg = m_backend->addStaticText("Budget Detail", px, py, pw, 280);

    int y = py + 4;
    // Income section — all budget line items are numeric readouts (monospace font).
    m_taxRevenueR   = m_backend->addStaticText("Tax Res: $0",        px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_taxRevenueR);   y += lineH;
    m_taxRevenueC   = m_backend->addStaticText("Tax Com: $0",        px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_taxRevenueC);   y += lineH;
    m_taxRevenueI   = m_backend->addStaticText("Tax Ind: $0",        px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_taxRevenueI);   y += lineH;
    m_utilityFees   = m_backend->addStaticText("Utility Fees: $0",   px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_utilityFees);   y += lineH;
    m_tourismIncome = m_backend->addStaticText("Tourism: $0 (post-V1)", px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_tourismIncome); y += lineH;
    m_incomeTotal   = m_backend->addStaticText("Income Total: $0",   px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_incomeTotal);   y += lineH;
    // Expense section
    m_wages         = m_backend->addStaticText("Wages: $0",          px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_wages);         y += lineH;
    m_roadMaint     = m_backend->addStaticText("Road Maint: $0",     px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_roadMaint);     y += lineH;
    m_serviceUpkeep = m_backend->addStaticText("Service: $0",        px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_serviceUpkeep); y += lineH;
    m_expenseTotal  = m_backend->addStaticText("Expense Total: $0",  px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_expenseTotal);  y += lineH;
    // Net balance
    m_netBalance    = m_backend->addStaticText("Net Balance: $0",    px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_netBalance);    y += lineH;
    m_unlockPreview = m_backend->addStaticText("",                   px + 4, y, pw - 8, lineH); m_backend->setElementMonoFont(m_unlockPreview);

    hide();
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void BudgetDetailPanel::show() {
    m_visible = true;
    if (!m_backend) return;
    m_backend->setElementVisible(m_panelBg,       true);
    m_backend->setElementVisible(m_taxRevenueR,   true);
    m_backend->setElementVisible(m_taxRevenueC,   true);
    m_backend->setElementVisible(m_taxRevenueI,   true);
    m_backend->setElementVisible(m_utilityFees,   true);
    m_backend->setElementVisible(m_tourismIncome, true);
    m_backend->setElementVisible(m_incomeTotal,   true);
    m_backend->setElementVisible(m_wages,         true);
    m_backend->setElementVisible(m_roadMaint,     true);
    m_backend->setElementVisible(m_serviceUpkeep, true);
    m_backend->setElementVisible(m_expenseTotal,  true);
    m_backend->setElementVisible(m_netBalance,    true);
    // unlock preview visibility is set in draw() based on threshold
}

void BudgetDetailPanel::hide() {
    m_visible = false;
    if (!m_backend) return;
    m_backend->setElementVisible(m_panelBg,       false);
    m_backend->setElementVisible(m_taxRevenueR,   false);
    m_backend->setElementVisible(m_taxRevenueC,   false);
    m_backend->setElementVisible(m_taxRevenueI,   false);
    m_backend->setElementVisible(m_utilityFees,   false);
    m_backend->setElementVisible(m_tourismIncome, false);
    m_backend->setElementVisible(m_incomeTotal,   false);
    m_backend->setElementVisible(m_wages,         false);
    m_backend->setElementVisible(m_roadMaint,     false);
    m_backend->setElementVisible(m_serviceUpkeep, false);
    m_backend->setElementVisible(m_expenseTotal,  false);
    m_backend->setElementVisible(m_netBalance,    false);
    m_backend->setElementVisible(m_unlockPreview, false);
}

// ---------------------------------------------------------------------------
// draw — refresh line item text from simulation state
// ---------------------------------------------------------------------------
void BudgetDetailPanel::draw() {
    if (!m_visible || !m_backend || !m_sim) return;

    // Income section: R tax + C tax + I tax + utility fees + tourism ($0 in V1)
    const float incomeR    = m_sim->getTaxRevenue(ZoneType::Residential);
    const float incomeC    = m_sim->getTaxRevenue(ZoneType::Commercial);
    const float incomeI    = m_sim->getTaxRevenue(ZoneType::Industrial);
    const float incomeUtil = m_sim->getUtilityFeeRevenue();
    const float incomeTourism = 0.0f;  // Phase 11h: tourism is post-V1, always $0
    const float incomeTotal = incomeR + incomeC + incomeI + incomeUtil + incomeTourism;

    m_backend->setElementText(m_taxRevenueR,
        "Tax Residential: " + fmtDollar(incomeR));
    m_backend->setElementText(m_taxRevenueC,
        "Tax Commercial: " + fmtDollar(incomeC));
    m_backend->setElementText(m_taxRevenueI,
        "Tax Industrial: " + fmtDollar(incomeI));
    m_backend->setElementText(m_utilityFees,
        "Utility Fees: " + fmtDollar(incomeUtil));
    m_backend->setElementText(m_tourismIncome,
        "Tourism: " + fmtDollar(incomeTourism) + " (post-V1)");
    m_backend->setElementText(m_incomeTotal,
        "Income Total: " + fmtDollar(incomeTotal));

    // Expense section: wages + road maintenance + service upkeep
    const float expWages   = m_sim->getWagesCost();
    const float expRoad    = m_sim->getRoadMaintenanceCost();
    const float expService = m_sim->getServiceUpkeepCost();
    const float expenseTotal = expWages + expRoad + expService;

    m_backend->setElementText(m_wages,
        "Wages: " + fmtDollar(expWages));
    m_backend->setElementText(m_roadMaint,
        "Road Maintenance: " + fmtDollar(expRoad));
    m_backend->setElementText(m_serviceUpkeep,
        "Service Upkeep: " + fmtDollar(expService));
    m_backend->setElementText(m_expenseTotal,
        "Expense Total: " + fmtDollar(expenseTotal));

    // Net monthly balance
    m_backend->setElementText(m_netBalance,
        "Net Monthly Balance: " + fmtDollar(incomeTotal - expenseTotal));

    // Density Unlock Preview — hidden when kNoUnlockThreshold sentinel (-1.0f)
    float threshold = m_sim->getNextUnlockThreshold(Difficulty::Normal);
    if (threshold >= 0.0f) {
        float revenue = m_sim->getCurrentMonthlyRevenue();
        // Show when within 10% of threshold
        if (revenue >= threshold * 0.9f) {
            float upkeepEstimate = m_sim->estimateMonthlyUpkeep();
            m_backend->setElementText(m_unlockPreview,
                "After Unlock: ~+" + fmtDollar(upkeepEstimate) + "/month expenses");
            m_backend->setElementVisible(m_unlockPreview, true);
        } else {
            m_backend->setElementVisible(m_unlockPreview, false);
        }
    } else {
        // Sentinel: all tiers unlocked — hide preview entirely
        m_backend->setElementVisible(m_unlockPreview, false);
    }
}

// ---------------------------------------------------------------------------
// update — deferred per-frame work (currently same as draw refresh)
// ---------------------------------------------------------------------------
void BudgetDetailPanel::update() {
    // Data refresh happens in draw() per budget tick cadence.
    // No additional per-frame state to update.
}
