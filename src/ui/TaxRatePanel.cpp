// src/ui/TaxRatePanel.cpp
//
// TaxRatePanel — 300x200 px floating panel for per-zone tax rate adjustment.
// Anchored below resource bar, horizontally centered in 1920x1080 virtual space.
// Rate bounds: 1%-25%, key repeat: 400ms initial, 150ms repeat, +/-5 cap per hold.

#include "src/ui/tax_rate_panel.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/platform/input_event.h"

#include <cstdio>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static constexpr float kMinTaxRate = 0.01f;   // 1%
static constexpr float kMaxTaxRate = 0.25f;   // 25%
static constexpr float kTaxStep   = 0.01f;    // 1% per click

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TaxRatePanel::TaxRatePanel(IUIBackend* backend, ICitySimulation* sim)
    : m_backend(backend)
    , m_sim(sim)
    , m_visible(false)
{
    if (!m_backend) return;

    m_panelBg   = m_backend->addStaticText("", kPanelX, kPanelY, kPanelW, kPanelH);
    m_titleLabel = m_backend->addStaticText("Tax Rates", kPanelX + 8, kPanelY + 4, kPanelW - 16, 24);

    m_rowR = createRow("Residential", kPanelY + 32);
    m_rowC = createRow("Commercial",  kPanelY + 76);
    m_rowI = createRow("Industrial",  kPanelY + 120);

    m_noUndoLabel = m_backend->addStaticText("Tax changes cannot be undone",
                                              kPanelX + 8, kPanelY + 170, kPanelW - 16, 24);

    hide();
}

TaxRatePanel::ZoneRow TaxRatePanel::createRow(const char* label, int y) {
    ZoneRow row;
    row.label   = m_backend->addStaticText(label,  kPanelX + 8,   y, 100, 36);
    // label is a zone name — proportional font, do NOT call setElementMonoFont
    row.btnDec  = m_backend->addButton("-",        kPanelX + 116, y, 32,  36);
    row.readout = m_backend->addStaticText("10%",  kPanelX + 152, y, 48,  36);
    m_backend->setElementMonoFont(row.readout);  // numeric percentage readout — monospace required (phase-10)
    row.btnInc  = m_backend->addButton("+",        kPanelX + 204, y, 32,  36);
    return row;
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void TaxRatePanel::show() {
    m_visible = true;
    if (!m_backend) return;
    m_backend->setElementVisible(m_panelBg,    true);
    m_backend->setElementVisible(m_titleLabel, true);

    auto showRow = [this](ZoneRow& row) {
        m_backend->setElementVisible(row.label,   true);
        m_backend->setElementVisible(row.btnDec,  true);
        m_backend->setElementVisible(row.readout, true);
        m_backend->setElementVisible(row.btnInc,  true);
    };
    showRow(m_rowR);
    showRow(m_rowC);
    showRow(m_rowI);
    m_backend->setElementVisible(m_noUndoLabel, true);
}

void TaxRatePanel::hide() {
    m_visible = false;
    if (!m_backend) return;
    m_backend->setElementVisible(m_panelBg,    false);
    m_backend->setElementVisible(m_titleLabel, false);

    auto hideRow = [this](ZoneRow& row) {
        m_backend->setElementVisible(row.label,   false);
        m_backend->setElementVisible(row.btnDec,  false);
        m_backend->setElementVisible(row.readout, false);
        m_backend->setElementVisible(row.btnInc,  false);
    };
    hideRow(m_rowR);
    hideRow(m_rowC);
    hideRow(m_rowI);
    m_backend->setElementVisible(m_noUndoLabel, false);

    // Reset hold state
    m_holdingInc = false;
    m_holdingDec = false;
    m_holdZone = -1;
    m_holdDelta = 0;
}

// ---------------------------------------------------------------------------
// draw — refresh rate readouts from simulation
// ---------------------------------------------------------------------------
void TaxRatePanel::draw() {
    if (!m_visible || !m_backend || !m_sim) return;

    float rateR = m_sim->getTaxRate(ZoneType::Residential);
    float rateC = m_sim->getTaxRate(ZoneType::Commercial);
    float rateI = m_sim->getTaxRate(ZoneType::Industrial);

    updateRowText(m_rowR, rateR);
    updateRowText(m_rowC, rateC);
    updateRowText(m_rowI, rateI);

    setRowEnabled(m_rowR, rateR);
    setRowEnabled(m_rowC, rateC);
    setRowEnabled(m_rowI, rateI);
}

void TaxRatePanel::updateRowText(ZoneRow& row, float rate) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f%%", rate * 100.0f);
    m_backend->setElementText(row.readout, buf);
}

void TaxRatePanel::setRowEnabled(ZoneRow& row, float rate) {
    m_backend->setElementEnabled(row.btnDec, rate > kMinTaxRate + 0.001f);
    m_backend->setElementEnabled(row.btnInc, rate < kMaxTaxRate - 0.001f);
}

// ---------------------------------------------------------------------------
// getBounds
// ---------------------------------------------------------------------------
Rect TaxRatePanel::getBounds() const {
    return {kPanelX, kPanelY, kPanelW, kPanelH};
}

// ---------------------------------------------------------------------------
// onEvent — handle button clicks for rate adjustment
// ---------------------------------------------------------------------------
bool TaxRatePanel::onEvent(const InputEvent& event) {
    if (!m_visible || !m_backend || !m_sim) return false;

    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        // Check if click is within panel bounds
        if (mx < kPanelX || mx > kPanelX + kPanelW ||
            my < kPanelY || my > kPanelY + kPanelH) {
            // Click outside panel — dismiss (but do NOT consume the click)
            hide();
            return false;
        }

        // Check each zone row's buttons
        struct ZoneCheck {
            ZoneRow* row;
            ZoneType zone;
        };
        ZoneCheck zones[] = {
            {&m_rowR, ZoneType::Residential},
            {&m_rowC, ZoneType::Commercial},
            {&m_rowI, ZoneType::Industrial}
        };

        for (auto& zc : zones) {
            Rect decRect = m_backend->getElementRect(zc.row->btnDec);
            Rect incRect = m_backend->getElementRect(zc.row->btnInc);

            if (mx >= decRect.x && mx <= decRect.x + decRect.w &&
                my >= decRect.y && my <= decRect.y + decRect.h) {
                float rate = m_sim->getTaxRate(zc.zone);
                float newRate = std::max(kMinTaxRate, rate - kTaxStep);
                m_sim->setTaxRate(zc.zone, newRate);
                return true;
            }
            if (mx >= incRect.x && mx <= incRect.x + incRect.w &&
                my >= incRect.y && my <= incRect.y + incRect.h) {
                float rate = m_sim->getTaxRate(zc.zone);
                float newRate = std::min(kMaxTaxRate, rate + kTaxStep);
                m_sim->setTaxRate(zc.zone, newRate);
                return true;
            }
        }

        // Click inside panel but not on a button — consumed
        return true;
    }

    // Escape closes the panel
    if (event.type == InputEvent::Type::KeyDown && event.keyCode == 27) { // Escape
        hide();
        return true;
    }

    return false;
}
