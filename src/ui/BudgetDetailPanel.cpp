// src/ui/BudgetDetailPanel.cpp
//
// BudgetDetailPanel — 320x200 px floating panel owned by HUD.
// Shows 8 named budget line items and an optional density unlock preview.

#include "src/ui/budget_detail_panel.h"
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

    m_panelBg = m_backend->addStaticText("Budget Detail", px, py, pw, 200);

    int y = py + 4;
    // All budget line items are numeric readouts — apply monospace font (phase-10).
    m_taxRevenueR   = m_backend->addStaticText("Tax R: $0",       px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_taxRevenueR);   y += lineH;
    m_taxRevenueC   = m_backend->addStaticText("Tax C: $0",       px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_taxRevenueC);   y += lineH;
    m_taxRevenueI   = m_backend->addStaticText("Tax I: $0",       px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_taxRevenueI);   y += lineH;
    m_wages         = m_backend->addStaticText("Wages: $0",       px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_wages);         y += lineH;
    m_roadMaint     = m_backend->addStaticText("Road Maint: $0",  px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_roadMaint);     y += lineH;
    m_serviceUpkeep = m_backend->addStaticText("Service: $0",     px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_serviceUpkeep); y += lineH;
    m_utilityFees   = m_backend->addStaticText("Utility Fees: $0",px + 4, y,             pw - 8, lineH); m_backend->setElementMonoFont(m_utilityFees);   y += lineH;
    m_netBalance    = m_backend->addStaticText("Net Balance: $0",  px + 4, y,            pw - 8, lineH); m_backend->setElementMonoFont(m_netBalance);    y += lineH;
    m_unlockPreview = m_backend->addStaticText("",                 px + 4, y,            pw - 8, lineH); m_backend->setElementMonoFont(m_unlockPreview);

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
    m_backend->setElementVisible(m_wages,         true);
    m_backend->setElementVisible(m_roadMaint,     true);
    m_backend->setElementVisible(m_serviceUpkeep, true);
    m_backend->setElementVisible(m_utilityFees,   true);
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
    m_backend->setElementVisible(m_wages,         false);
    m_backend->setElementVisible(m_roadMaint,     false);
    m_backend->setElementVisible(m_serviceUpkeep, false);
    m_backend->setElementVisible(m_utilityFees,   false);
    m_backend->setElementVisible(m_netBalance,    false);
    m_backend->setElementVisible(m_unlockPreview, false);
}

// ---------------------------------------------------------------------------
// draw — refresh line item text from simulation state
// ---------------------------------------------------------------------------
void BudgetDetailPanel::draw() {
    if (!m_visible || !m_backend || !m_sim) return;

    m_backend->setElementText(m_taxRevenueR,
        "Tax Residential: " + fmtDollar(m_sim->getTaxRevenue(ZoneType::Residential)));
    m_backend->setElementText(m_taxRevenueC,
        "Tax Commercial: " + fmtDollar(m_sim->getTaxRevenue(ZoneType::Commercial)));
    m_backend->setElementText(m_taxRevenueI,
        "Tax Industrial: " + fmtDollar(m_sim->getTaxRevenue(ZoneType::Industrial)));
    m_backend->setElementText(m_wages,
        "Wages: " + fmtDollar(m_sim->getWagesCost()));
    m_backend->setElementText(m_roadMaint,
        "Road Maintenance: " + fmtDollar(m_sim->getRoadMaintenanceCost()));
    m_backend->setElementText(m_serviceUpkeep,
        "Service Upkeep: " + fmtDollar(m_sim->getServiceUpkeepCost()));
    m_backend->setElementText(m_utilityFees,
        "Utility Fees: " + fmtDollar(m_sim->getUtilityFeeRevenue()));
    m_backend->setElementText(m_netBalance,
        "Net Monthly Balance: " + fmtDollar(m_sim->getCurrentMonthlyRevenue()));

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
