// src/ui/FinancesPanel.cpp
//
// FinancesPanel — 360x520 px combined Tax Rates + Budget breakdown panel.
// Merged from TaxRatePanel (Phase 8) and BudgetDetailPanel (Phase 11h).
// Phase 11l: single panel opened via T key or resource-bar click.

#include "src/ui/FinancesPanel.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/sound_ids.h"
#include "src/interfaces/audio_types.h"
#include "src/platform/input_event.h"

#include <cstdio>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr float kMinTaxRate = 0.01f;   // 1%
static constexpr float kMaxTaxRate = 0.25f;   // 25%
static constexpr float kTaxStep    = 0.01f;   // 1% per click
static constexpr float kHoldInitialDelay = 0.4f;   // 400ms initial repeat delay
static constexpr float kHoldRepeatDelay  = 0.15f;  // 150ms repeat interval
static constexpr int   kHoldDeltaCap     = 5;      // ±5 pp cap per continuous hold

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
// Constructor
// ---------------------------------------------------------------------------
FinancesPanel::FinancesPanel(IUIBackend* backend, ICitySimulation* sim,
                             IAudioSystem* audio, IClock* clock)
    : m_backend(backend)
    , m_sim(sim)
    , m_audio(audio)
    , m_clock(clock)
    , m_visible(false)
    , m_pendingRateChange(false)
{
    if (!m_backend) return;

    // --- Panel background and title ---
    m_panelBg    = m_backend->addStaticText("", kPanelX, kPanelY, kPanelW, kPanelH);
    m_backend->setElementBackground(m_panelBg, 13, 27, 42, 217);
    // Glass City deep-navy: rgba(13, 27, 42, 0.85) per finances-panel.md.
    m_titleLabel = m_backend->addStaticText("Finances", kPanelX + 8, kPanelY + 4, kPanelW - 16, 24);

    // --- Section 1: Tax Rates (rows at y+32, y+76, y+120) ---
    m_rowR = createRow("Residential", kPanelY + 32);
    m_rowC = createRow("Commercial",  kPanelY + 76);
    m_rowI = createRow("Industrial",  kPanelY + 120);

    m_noUndoLabel = m_backend->addStaticText("Tax changes cannot be undone",
                                              kPanelX + 8, kPanelY + 168, kPanelW - 16, 20);

    // --- Budget section separator ---
    m_budgetSeparator = m_backend->addStaticText("", kPanelX + 8, kPanelY + 194, kPanelW - 16, 2);

    // --- Income header ---
    m_incomeHeader = m_backend->addStaticText("Income",
                                              kPanelX + 8, kPanelY + 200, kPanelW - 16, 20);

    // Income line items (all numeric readouts — monospace required)
    const int lineH = 20;
    int y = kPanelY + 224;
    m_taxRevenueR   = m_backend->addStaticText("Tax Residential: $0",   kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_taxRevenueR);   y += lineH;
    m_taxRevenueC   = m_backend->addStaticText("Tax Commercial: $0",    kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_taxRevenueC);   y += lineH;
    m_taxRevenueI   = m_backend->addStaticText("Tax Industrial: $0",    kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_taxRevenueI);   y += lineH;
    m_utilityFees   = m_backend->addStaticText("Utility Fees: $0",      kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_utilityFees);   y += lineH;
    m_tourismIncome = m_backend->addStaticText("Tourism: $0 (post-V1)", kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_tourismIncome); y += lineH;

    // Expense separator
    m_expenseSeparator = m_backend->addStaticText("", kPanelX + 8, y, kPanelW - 16, 2); y += 6;

    // Expenses header
    m_expensesHeader = m_backend->addStaticText("Expenses", kPanelX + 8, y, kPanelW - 16, 20); y += 24;

    // Expense line items (all numeric readouts — monospace required)
    m_roadMaint     = m_backend->addStaticText("Road Maintenance: $0",  kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_roadMaint);     y += lineH;
    m_serviceUpkeep = m_backend->addStaticText("Service Upkeep: $0",    kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_serviceUpkeep); y += lineH;
    m_wages         = m_backend->addStaticText("Wages: $0",             kPanelX + 8, y, kPanelW - 16, lineH); m_backend->setElementMonoFont(m_wages);         y += lineH;

    // Net balance separator
    m_netSeparator = m_backend->addStaticText("", kPanelX + 8, y, kPanelW - 16, 2); y += 6;

    // Net balance
    m_netBalance = m_backend->addStaticText("Net Monthly Balance: $0", kPanelX + 8, y, kPanelW - 16, lineH);
    m_backend->setElementMonoFont(m_netBalance);

    // Tourism is always grayed — post-V1 placeholder
    m_backend->setElementEnabled(m_tourismIncome, false);

    hideAllElements();
}

// ---------------------------------------------------------------------------
// createRow helper
// ---------------------------------------------------------------------------
FinancesPanel::ZoneRow FinancesPanel::createRow(const char* label, int y) {
    ZoneRow row;
    row.label   = m_backend->addStaticText(label,  kPanelX + 8,   y, 100, 36);
    row.btnDec  = m_backend->addButton("-",        kPanelX + 116, y, 32,  36);
    row.readout = m_backend->addStaticText("10%",  kPanelX + 152, y, 48,  36);
    m_backend->setElementMonoFont(row.readout);  // numeric percentage — monospace required
    row.btnInc  = m_backend->addButton("+",        kPanelX + 204, y, 32,  36);
    return row;
}

// ---------------------------------------------------------------------------
// showAllElements / hideAllElements
// ---------------------------------------------------------------------------
void FinancesPanel::showAllElements() {
    if (!m_backend) return;

    m_backend->setElementVisible(m_panelBg,       true);
    m_backend->setElementVisible(m_titleLabel,    true);

    auto showRow = [this](ZoneRow& row) {
        m_backend->setElementVisible(row.label,   true);
        m_backend->setElementVisible(row.btnDec,  true);
        m_backend->setElementVisible(row.readout, true);
        m_backend->setElementVisible(row.btnInc,  true);
    };
    showRow(m_rowR);
    showRow(m_rowC);
    showRow(m_rowI);

    m_backend->setElementVisible(m_noUndoLabel,      true);
    m_backend->setElementVisible(m_budgetSeparator,  true);
    m_backend->setElementVisible(m_incomeHeader,     true);
    m_backend->setElementVisible(m_taxRevenueR,      true);
    m_backend->setElementVisible(m_taxRevenueC,      true);
    m_backend->setElementVisible(m_taxRevenueI,      true);
    m_backend->setElementVisible(m_utilityFees,      true);
    m_backend->setElementVisible(m_tourismIncome,    true);
    m_backend->setElementVisible(m_expenseSeparator, true);
    m_backend->setElementVisible(m_expensesHeader,   true);
    m_backend->setElementVisible(m_roadMaint,        true);
    m_backend->setElementVisible(m_serviceUpkeep,    true);
    m_backend->setElementVisible(m_wages,            true);
    m_backend->setElementVisible(m_netSeparator,     true);
    m_backend->setElementVisible(m_netBalance,       true);
}

void FinancesPanel::hideAllElements() {
    if (!m_backend) return;

    m_backend->setElementVisible(m_panelBg,       false);
    m_backend->setElementVisible(m_titleLabel,    false);

    auto hideRow = [this](ZoneRow& row) {
        m_backend->setElementVisible(row.label,   false);
        m_backend->setElementVisible(row.btnDec,  false);
        m_backend->setElementVisible(row.readout, false);
        m_backend->setElementVisible(row.btnInc,  false);
    };
    hideRow(m_rowR);
    hideRow(m_rowC);
    hideRow(m_rowI);

    m_backend->setElementVisible(m_noUndoLabel,      false);
    m_backend->setElementVisible(m_budgetSeparator,  false);
    m_backend->setElementVisible(m_incomeHeader,     false);
    m_backend->setElementVisible(m_taxRevenueR,      false);
    m_backend->setElementVisible(m_taxRevenueC,      false);
    m_backend->setElementVisible(m_taxRevenueI,      false);
    m_backend->setElementVisible(m_utilityFees,      false);
    m_backend->setElementVisible(m_tourismIncome,    false);
    m_backend->setElementVisible(m_expenseSeparator, false);
    m_backend->setElementVisible(m_expensesHeader,   false);
    m_backend->setElementVisible(m_roadMaint,        false);
    m_backend->setElementVisible(m_serviceUpkeep,    false);
    m_backend->setElementVisible(m_wages,            false);
    m_backend->setElementVisible(m_netSeparator,     false);
    m_backend->setElementVisible(m_netBalance,       false);

    // Reset hold state
    m_holdingInc = false;
    m_holdingDec = false;
    m_holdZone   = -1;
    m_holdDelta  = 0;
    m_initialRepeatDone = false;
}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------
void FinancesPanel::open() {
    m_visible = true;
    showAllElements();
    if (m_audio) {
        m_audio->playSound(UI_MENU_OPEN, SoundPriority::HIGH, 1.0f);
    }
}

void FinancesPanel::close() {
    m_visible = false;
    hideAllElements();
    if (m_audio) {
        m_audio->playSound(UI_MENU_CLOSE, SoundPriority::HIGH, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// hasPendingRateChange / clearPendingRateChange
// ---------------------------------------------------------------------------
bool FinancesPanel::hasPendingRateChange() const {
    return m_pendingRateChange;
}

void FinancesPanel::clearPendingRateChange() {
    m_pendingRateChange = false;
}

// ---------------------------------------------------------------------------
// update — per-frame key-repeat processing
// ---------------------------------------------------------------------------
void FinancesPanel::update(float dt) {
    if (!m_visible || !m_backend || !m_sim) return;

    // Key-repeat for held +/- buttons
    if ((m_holdingInc || m_holdingDec) && m_holdZone >= 0) {
        m_holdTimer += dt;

        float threshold = m_initialRepeatDone ? kHoldRepeatDelay : kHoldInitialDelay;
        if (m_holdTimer >= threshold) {
            m_holdTimer = 0.0f;
            m_initialRepeatDone = true;

            // Enforce ±5 pp cap per continuous hold
            if (std::abs(m_holdDelta) < kHoldDeltaCap) {
                ZoneType zones[] = {
                    ZoneType::Residential,
                    ZoneType::Commercial,
                    ZoneType::Industrial
                };
                ZoneType zone = zones[m_holdZone];
                float rate = m_sim->getTaxRate(zone);

                if (m_holdingInc) {
                    float newRate = std::min(kMaxTaxRate, rate + kTaxStep);
                    if (newRate != rate) {
                        m_sim->setTaxRate(zone, newRate);
                        m_pendingRateChange = true;
                        ++m_holdDelta;
                    }
                } else {
                    float newRate = std::max(kMinTaxRate, rate - kTaxStep);
                    if (newRate != rate) {
                        m_sim->setTaxRate(zone, newRate);
                        m_pendingRateChange = true;
                        --m_holdDelta;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// draw — refresh tax readouts and budget line items from simulation
// ---------------------------------------------------------------------------
void FinancesPanel::draw() {
    if (!m_visible || !m_backend) return;

    // Emit sentinel for draw-order tests.
    m_backend->setElementVisible(kSentinel, false);

    if (!m_sim) return;

    // --- Tax Rates section ---
    float rateR = m_sim->getTaxRate(ZoneType::Residential);
    float rateC = m_sim->getTaxRate(ZoneType::Commercial);
    float rateI = m_sim->getTaxRate(ZoneType::Industrial);

    updateRowText(m_rowR, rateR);
    updateRowText(m_rowC, rateC);
    updateRowText(m_rowI, rateI);

    setRowEnabled(m_rowR, rateR);
    setRowEnabled(m_rowC, rateC);
    setRowEnabled(m_rowI, rateI);

    // --- Budget section ---
    const float incomeR    = m_sim->getTaxRevenue(ZoneType::Residential);
    const float incomeC    = m_sim->getTaxRevenue(ZoneType::Commercial);
    const float incomeI    = m_sim->getTaxRevenue(ZoneType::Industrial);
    const float incomeUtil = m_sim->getUtilityFeeRevenue();
    const float incomeTourism = 0.0f;  // post-V1, always $0
    const float incomeTotal   = incomeR + incomeC + incomeI + incomeUtil + incomeTourism;

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

    m_backend->setElementText(m_incomeHeader,
        "Income  " + fmtDollar(incomeTotal) + "/month");

    const float expRoad    = m_sim->getRoadMaintenanceCost();
    const float expService = m_sim->getServiceUpkeepCost();
    const float expWages   = m_sim->getWagesCost();
    const float expTotal   = expRoad + expService + expWages;

    m_backend->setElementText(m_roadMaint,
        "Road Maintenance: " + fmtDollar(expRoad));
    m_backend->setElementText(m_serviceUpkeep,
        "Service Upkeep: " + fmtDollar(expService));
    m_backend->setElementText(m_wages,
        "Wages: " + fmtDollar(expWages));

    m_backend->setElementText(m_expensesHeader,
        "Expenses  " + fmtDollar(expTotal) + "/month");

    // Net monthly balance
    float net = incomeTotal - expTotal;
    m_backend->setElementText(m_netBalance,
        "Net Monthly Balance: " + fmtDollar(net));
}

void FinancesPanel::updateRowText(ZoneRow& row, float rate) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f%%", rate * 100.0f);
    m_backend->setElementText(row.readout, buf);
}

void FinancesPanel::setRowEnabled(ZoneRow& row, float rate) {
    m_backend->setElementEnabled(row.btnDec, rate > kMinTaxRate + 0.001f);
    m_backend->setElementEnabled(row.btnInc, rate < kMaxTaxRate - 0.001f);
}

// ---------------------------------------------------------------------------
// onEvent — handle button clicks, escape, and outside-click dismiss
// ---------------------------------------------------------------------------
bool FinancesPanel::onEvent(const InputEvent& event) {
    if (!m_visible || !m_backend || !m_sim) return false;

    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        // Click outside panel — dismiss, but do NOT consume the event
        if (mx < kPanelX || mx > kPanelX + kPanelW ||
            my < kPanelY || my > kPanelY + kPanelH) {
            close();
            return false;
        }

        // Check each zone row's +/- buttons
        struct ZoneCheck {
            ZoneRow* row;
            ZoneType zone;
            int      idx;
        };
        ZoneCheck zones[] = {
            {&m_rowR, ZoneType::Residential, 0},
            {&m_rowC, ZoneType::Commercial,  1},
            {&m_rowI, ZoneType::Industrial,  2}
        };

        for (auto& zc : zones) {
            Rect decRect = m_backend->getElementRect(zc.row->btnDec);
            Rect incRect = m_backend->getElementRect(zc.row->btnInc);

            if (mx >= decRect.x && mx <= decRect.x + decRect.w &&
                my >= decRect.y && my <= decRect.y + decRect.h &&
                m_backend->isElementEnabled(zc.row->btnDec)) {
                float rate = m_sim->getTaxRate(zc.zone);
                float newRate = std::max(kMinTaxRate, rate - kTaxStep);
                if (newRate != rate) {
                    m_sim->setTaxRate(zc.zone, newRate);
                    m_pendingRateChange = true;
                }
                m_holdingDec = true;
                m_holdingInc = false;
                m_holdZone   = zc.idx;
                m_holdTimer  = 0.0f;
                m_holdDelta  = 0;
                m_initialRepeatDone = false;
                return true;
            }
            if (mx >= incRect.x && mx <= incRect.x + incRect.w &&
                my >= incRect.y && my <= incRect.y + incRect.h &&
                m_backend->isElementEnabled(zc.row->btnInc)) {
                float rate = m_sim->getTaxRate(zc.zone);
                float newRate = std::min(kMaxTaxRate, rate + kTaxStep);
                if (newRate != rate) {
                    m_sim->setTaxRate(zc.zone, newRate);
                    m_pendingRateChange = true;
                }
                m_holdingInc = true;
                m_holdingDec = false;
                m_holdZone   = zc.idx;
                m_holdTimer  = 0.0f;
                m_holdDelta  = 0;
                m_initialRepeatDone = false;
                return true;
            }
        }

        // Click inside panel but not on a button — consumed
        return true;
    }

    if (event.type == InputEvent::Type::MouseButtonUp && event.button == 0) {
        // Release hold
        m_holdingInc = false;
        m_holdingDec = false;
        m_holdZone   = -1;
        m_holdDelta  = 0;
        return false;
    }

    // Escape or T closes the panel
    if (event.type == InputEvent::Type::KeyDown) {
        if (event.keyCode == 27) {  // Escape
            close();
            return true;
        }
    }

    return false;
}
