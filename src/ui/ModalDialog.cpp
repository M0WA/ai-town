// src/ui/ModalDialog.cpp
//
// ModalDialog — handles all blocking modal dialogs.
// Background scrim at 50% opacity. Keyboard navigation with Tab/Enter/Escape.
// Auto-pause: calls m_sim->setPaused(true) on open if sim was not already paused.
// closeModal() ordering: setPaused(false) THEN clear m_active.

#include "src/ui/modal_dialog.h"
#include "src/interfaces/LoanTerms.h"
#include "src/platform/input_event.h"

#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
ModalDialog::ModalDialog(IUIBackend* backend, ICitySimulation* sim)
    : m_backend(backend)
    , m_sim(sim)
    , m_active(false)
    , m_didPauseSim(false)
{
    if (!m_backend) return;

    // Create scrim (full 1920x1080 overlay)
    m_scrim = m_backend->addStaticText("", 0, 0, 1920, 1080);
    m_backend->setElementAlpha(m_scrim, 0.5f);

    // Create dialog elements at default positions — repositioned per dialog type
    m_dialogBg    = m_backend->addStaticText("", 0, 0, 640, 400);
    m_titleLabel  = m_backend->addStaticText("", 0, 0, 600, 40);
    m_bodyLabel   = m_backend->addStaticText("", 0, 0, 600, 200);
    m_btnPrimary  = m_backend->addButton("", 0, 0, 140, 40);
    m_btnSecondary= m_backend->addButton("", 0, 0, 140, 40);
    m_btnTertiary = m_backend->addButton("", 0, 0, 140, 40);
    m_btnBack     = m_backend->addButton("", 0, 0, 200, 40);

    hideAllDialogElements();
}

// ---------------------------------------------------------------------------
// isActive
// ---------------------------------------------------------------------------
bool ModalDialog::isActive() const {
    return m_active;
}

// ---------------------------------------------------------------------------
// show / hide (generic — delegates to internal open/close)
// ---------------------------------------------------------------------------
void ModalDialog::show() {
    // Generic show is a no-op; use specific dialog launchers
}

void ModalDialog::hide() {
    closeModal();
}

// ---------------------------------------------------------------------------
// hideAllDialogElements
// ---------------------------------------------------------------------------
void ModalDialog::hideAllDialogElements() {
    if (!m_backend) return;
    m_backend->setElementVisible(m_scrim,        false);
    m_backend->setElementVisible(m_dialogBg,     false);
    m_backend->setElementVisible(m_titleLabel,   false);
    m_backend->setElementVisible(m_bodyLabel,    false);
    m_backend->setElementVisible(m_btnPrimary,   false);
    m_backend->setElementVisible(m_btnSecondary, false);
    m_backend->setElementVisible(m_btnTertiary,  false);
    m_backend->setElementVisible(m_btnBack,      false);
}

// ---------------------------------------------------------------------------
// openModal — common open sequence
// ---------------------------------------------------------------------------
void ModalDialog::openModal(DialogType type) {
    m_dialogType = type;
    m_active = true;
    m_lastResult = DialogResult::None;
    m_focusedButton = 0;

    // Auto-pause: pause sim if not already paused
    if (m_sim && !m_sim->isPaused()) {
        m_sim->setPaused(true);
        m_didPauseSim = true;
    } else {
        m_didPauseSim = false;
    }

    // Show scrim
    if (m_backend) {
        m_backend->setElementVisible(m_scrim, true);
        m_backend->setElementAlpha(m_scrim, 0.5f);
    }
}

// ---------------------------------------------------------------------------
// closeModal — common close sequence
// ---------------------------------------------------------------------------
void ModalDialog::closeModal() {
    // Resume sim if this modal initiated the pause
    if (m_didPauseSim && m_sim) {
        m_sim->setPaused(false);
    }
    m_didPauseSim = false;

    m_active = false;
    m_dialogType = DialogType::None;
    hideAllDialogElements();
}

// ---------------------------------------------------------------------------
// setDialogRect — helper to center dialog of given size
// ---------------------------------------------------------------------------
void ModalDialog::setDialogRect(int w, int h) {
    if (!m_backend) return;

    int x = (1920 - w) / 2;
    int y = (1080 - h) / 2;

    // Recreate dialog background at the correct size, centered in 1920x1080.
    // Only the background is recreated; button handles are unchanged to preserve
    // handle-based test expectations and hit-testing contracts.
    m_backend->removeElement(m_dialogBg);
    m_dialogBg = m_backend->addStaticText("", x, y, w, h);
}

// ---------------------------------------------------------------------------
// showForcedLoan
// ---------------------------------------------------------------------------
void ModalDialog::showForcedLoan(const LoanTerms& terms) {
    m_loanAmount         = terms.amount;
    m_loanRepaymentTicks = terms.repaymentTicks;
    m_loanInterestRate   = terms.interestRate;

    openModal(DialogType::ForcedLoan);
    layoutForcedLoanScreen1();
}

void ModalDialog::layoutForcedLoanScreen1() {
    if (!m_backend) return;

    // Large dialog: 640x400, centered in 1920x1080
    setDialogRect(640, 400);

    m_backend->setElementVisible(m_dialogBg, true);
    m_backend->setElementVisible(m_titleLabel, true);
    m_backend->setElementVisible(m_bodyLabel, true);
    m_backend->setElementVisible(m_btnPrimary, true);
    m_backend->setElementVisible(m_btnSecondary, true);
    m_backend->setElementVisible(m_btnTertiary, false);
    m_backend->setElementVisible(m_btnBack, false);

    m_backend->setElementText(m_titleLabel, "Budget Crisis - Loan Required");

    char bodyBuf[512];
    float repayPerTick = m_loanAmount / static_cast<float>(m_loanRepaymentTicks);
    std::snprintf(bodyBuf, sizeof(bodyBuf),
        "Loan Amount: $%.0f\n"
        "Interest Rate: %.0f%%/year\n"
        "Repayment: $%.0f/month over %d months",
        m_loanAmount, m_loanInterestRate * 100.0f,
        repayPerTick, m_loanRepaymentTicks);
    m_backend->setElementText(m_bodyLabel, bodyBuf);

    m_backend->setElementText(m_btnPrimary, "Accept");
    m_backend->setElementText(m_btnSecondary, "Decline");

    // Default focus on Accept (least destructive — accepting loan avoids bankruptcy)
    m_focusedButton = 0;
}

void ModalDialog::layoutForcedLoanScreen2() {
    if (!m_backend) return;

    m_dialogType = DialogType::ForcedLoanScreen2;

    // Large dialog: 640x400 (same as screen 1)
    setDialogRect(640, 400);

    m_backend->setElementVisible(m_dialogBg, true);
    m_backend->setElementVisible(m_titleLabel, true);
    m_backend->setElementVisible(m_bodyLabel, true);
    m_backend->setElementVisible(m_btnPrimary, true);   // Raise tax
    m_backend->setElementVisible(m_btnSecondary, true);  // Demolish last
    m_backend->setElementVisible(m_btnTertiary, true);   // Emergency bond
    m_backend->setElementVisible(m_btnBack, true);       // Back to accept loan

    m_backend->setElementText(m_titleLabel, "Budget Action Required");
    m_backend->setElementText(m_bodyLabel, "Select an alternative action:");

    m_backend->setElementText(m_btnPrimary, "Raise Tax Rates (x1.10)");
    m_backend->setElementText(m_btnSecondary, "Demolish Last Building");

    // Emergency bond — gray if exhausted
    if (m_sim) {
        int bondsRemaining = m_sim->getOutstandingBondUses();
        char bondLabel[64];
        std::snprintf(bondLabel, sizeof(bondLabel), "Emergency Bond (%d remaining)", bondsRemaining);
        m_backend->setElementText(m_btnTertiary, bondLabel);
        m_backend->setElementEnabled(m_btnTertiary, bondsRemaining > 0);
    } else {
        m_backend->setElementText(m_btnTertiary, "Emergency Bond (0 remaining)");
        m_backend->setElementEnabled(m_btnTertiary, false);
    }

    m_backend->setElementText(m_btnBack, "Back - Accept original loan terms");

    // Default focus on Raise Tax (least destructive)
    m_focusedButton = 0;
}

// ---------------------------------------------------------------------------
// showDemolishConfirm
// ---------------------------------------------------------------------------
void ModalDialog::showDemolishConfirm(int tileCount) {
    openModal(DialogType::DemolishConfirm);
    layoutDemolishConfirm(tileCount);
}

void ModalDialog::layoutDemolishConfirm(int tileCount) {
    if (!m_backend) return;

    // Small dialog: 480x240, centered
    setDialogRect(480, 240);

    m_backend->setElementVisible(m_dialogBg, true);
    m_backend->setElementVisible(m_titleLabel, true);
    m_backend->setElementVisible(m_bodyLabel, true);
    m_backend->setElementVisible(m_btnPrimary, true);
    m_backend->setElementVisible(m_btnSecondary, true);
    m_backend->setElementVisible(m_btnTertiary, false);
    m_backend->setElementVisible(m_btnBack, false);

    m_backend->setElementText(m_titleLabel, "Demolish Confirmation");

    std::string body = "Demolish " + std::to_string(tileCount)
                       + " tile(s)? You can press Ctrl+Z to undo.";
    m_backend->setElementText(m_bodyLabel, body);

    m_backend->setElementText(m_btnPrimary, "Yes");
    m_backend->setElementText(m_btnSecondary, "Cancel");

    // Default focus on Cancel (least destructive)
    m_focusedButton = 1;
}

// ---------------------------------------------------------------------------
// showWASDPreset
// ---------------------------------------------------------------------------
void ModalDialog::showWASDPreset() {
    openModal(DialogType::WASDPreset);
    layoutWASDPreset();
}

void ModalDialog::layoutWASDPreset() {
    if (!m_backend) return;

    // Small dialog: 480x240, centered
    setDialogRect(480, 240);

    m_backend->setElementVisible(m_dialogBg, true);
    m_backend->setElementVisible(m_titleLabel, true);
    m_backend->setElementVisible(m_bodyLabel, true);
    m_backend->setElementVisible(m_btnPrimary, true);
    m_backend->setElementVisible(m_btnSecondary, true);
    m_backend->setElementVisible(m_btnTertiary, false);
    m_backend->setElementVisible(m_btnBack, false);

    m_backend->setElementText(m_titleLabel, "Apply WASD Preset?");
    m_backend->setElementText(m_bodyLabel,
        "This will change:\n"
        "W=Pan Up, A=Pan Left, S=Pan Down, D=Pan Right\n"
        "(Demolish moved to X)\n"
        "Any custom bindings for W/A/S/D will be overwritten.");

    m_backend->setElementText(m_btnPrimary, "Apply");
    m_backend->setElementText(m_btnSecondary, "Cancel");

    // Default focus on Cancel (least destructive)
    m_focusedButton = 1;
}

// ---------------------------------------------------------------------------
// showGameOver
// ---------------------------------------------------------------------------
void ModalDialog::showGameOver(int64_t debt, int months) {
    m_gameOverDebt = debt;
    m_gameOverMonths = months;

    openModal(DialogType::GameOver);
    layoutGameOver(debt, months);
}

void ModalDialog::layoutGameOver(int64_t debt, int months) {
    if (!m_backend) return;

    // Medium dialog: 560x320, centered
    setDialogRect(560, 320);

    m_backend->setElementVisible(m_dialogBg, true);
    m_backend->setElementVisible(m_titleLabel, true);
    m_backend->setElementVisible(m_bodyLabel, true);
    m_backend->setElementVisible(m_btnPrimary, true);
    m_backend->setElementVisible(m_btnSecondary, true);
    m_backend->setElementVisible(m_btnTertiary, false);
    m_backend->setElementVisible(m_btnBack, false);

    m_backend->setElementText(m_titleLabel, "City Bankrupt");

    char bodyBuf[256];
    std::snprintf(bodyBuf, sizeof(bodyBuf),
        "Total Debt: $%lld\n"
        "Consecutive deficit months: %d",
        static_cast<long long>(debt), months);
    m_backend->setElementText(m_bodyLabel, bodyBuf);

    m_backend->setElementText(m_btnPrimary, "Load Last Save");
    m_backend->setElementText(m_btnSecondary, "Return to Main Menu");

    // Default focus on Load Last Save (least destructive)
    m_focusedButton = 0;
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void ModalDialog::draw() {
    if (!m_active || !m_backend) return;

    // Focus ring: set focused button to full opacity, unfocused to dimmed.
    // IrrlichtUIBackend interprets alpha 1.0 as the focused element and renders
    // a 2px accent-color border around it (Phase 8 focus ring spec).
    UIElementHandle buttons[] = {m_btnPrimary, m_btnSecondary, m_btnTertiary, m_btnBack};
    for (int i = 0; i < 4; ++i) {
        if (m_backend->isElementVisible(buttons[i])) {
            m_backend->setElementAlpha(buttons[i], (i == m_focusedButton) ? 1.0f : 0.85f);
        }
    }
}

// ---------------------------------------------------------------------------
// onEvent — keyboard navigation for modal dialogs
// ---------------------------------------------------------------------------
bool ModalDialog::onEvent(const InputEvent& event) {
    if (!m_active) return false;

    // All input is consumed while modal is active (blocking)
    if (event.type == InputEvent::Type::KeyDown) {
        int key = event.keyCode;

        // Tab: cycle focus between buttons
        if (key == 9) { // Tab
            int maxButtons = 2;
            if (m_dialogType == DialogType::ForcedLoanScreen2) maxButtons = 4;
            m_focusedButton = (m_focusedButton + 1) % maxButtons;
            return true;
        }

        // Enter: activate focused button
        if (key == 13) { // Enter
            switch (m_dialogType) {
                case DialogType::ForcedLoan:
                    if (m_focusedButton == 0) {
                        // Accept loan
                        m_lastResult = DialogResult::Accept;
                        closeModal();
                    } else {
                        // Decline -> show screen 2
                        layoutForcedLoanScreen2();
                    }
                    return true;

                case DialogType::ForcedLoanScreen2:
                    if (m_focusedButton == 0) {
                        // Raise tax rates (x1.10)
                        if (m_sim) {
                            float rateR = m_sim->getTaxRate(ZoneType::Residential);
                            float rateC = m_sim->getTaxRate(ZoneType::Commercial);
                            float rateI = m_sim->getTaxRate(ZoneType::Industrial);
                            m_sim->setTaxRate(ZoneType::Residential, std::min(0.25f, rateR * 1.10f));
                            m_sim->setTaxRate(ZoneType::Commercial,  std::min(0.25f, rateC * 1.10f));
                            m_sim->setTaxRate(ZoneType::Industrial,  std::min(0.25f, rateI * 1.10f));
                        }
                        m_lastResult = DialogResult::Accept;
                        closeModal();
                    } else if (m_focusedButton == 1) {
                        // Demolish last building
                        m_lastResult = DialogResult::Accept;
                        closeModal();
                    } else if (m_focusedButton == 2) {
                        // Emergency bond
                        m_lastResult = DialogResult::Accept;
                        closeModal();
                    } else {
                        // Back -> accept original loan
                        m_lastResult = DialogResult::Accept;
                        closeModal();
                    }
                    return true;

                case DialogType::DemolishConfirm:
                    if (m_focusedButton == 0) {
                        m_lastResult = DialogResult::Accept;
                    } else {
                        m_lastResult = DialogResult::Cancel;
                    }
                    closeModal();
                    return true;

                case DialogType::WASDPreset:
                    if (m_focusedButton == 0) {
                        m_lastResult = DialogResult::Accept;
                    } else {
                        m_lastResult = DialogResult::Cancel;
                    }
                    closeModal();
                    return true;

                case DialogType::GameOver:
                    if (m_focusedButton == 0) {
                        m_lastResult = DialogResult::Accept;  // Load Last Save
                    } else {
                        m_lastResult = DialogResult::Decline; // Return to Main Menu
                    }
                    closeModal();
                    return true;

                default:
                    break;
            }
            return true;
        }

        // Escape: activate cancel/safe-exit (consumed by modal focus layer)
        if (key == 27) { // Escape
            switch (m_dialogType) {
                case DialogType::ForcedLoan:
                case DialogType::ForcedLoanScreen2:
                    // Forced loan is non-dismissible — Escape consumed but no action
                    return true;
                case DialogType::GameOver:
                    // Game-over is non-dismissible — Escape consumed but no action
                    return true;
                case DialogType::DemolishConfirm:
                case DialogType::WASDPreset:
                    m_lastResult = DialogResult::Cancel;
                    closeModal();
                    return true;
                default:
                    return true;
            }
        }
    }

    // Mouse clicks on buttons
    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        Rect primary   = m_backend->getElementRect(m_btnPrimary);
        Rect secondary = m_backend->getElementRect(m_btnSecondary);

        int mx = event.x;
        int my = event.y;

        auto hitTest = [](int mx, int my, Rect r) {
            return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
        };

        if (m_backend->isElementVisible(m_btnPrimary) && hitTest(mx, my, primary)) {
            m_focusedButton = 0;
            // Simulate Enter press
            InputEvent enterEvent;
            enterEvent.type = InputEvent::Type::KeyDown;
            enterEvent.keyCode = 13;
            return onEvent(enterEvent);
        }
        if (m_backend->isElementVisible(m_btnSecondary) && hitTest(mx, my, secondary)) {
            m_focusedButton = 1;
            InputEvent enterEvent;
            enterEvent.type = InputEvent::Type::KeyDown;
            enterEvent.keyCode = 13;
            return onEvent(enterEvent);
        }
        if (m_backend->isElementVisible(m_btnTertiary)) {
            Rect tertiary = m_backend->getElementRect(m_btnTertiary);
            if (hitTest(mx, my, tertiary)) {
                m_focusedButton = 2;
                InputEvent enterEvent;
                enterEvent.type = InputEvent::Type::KeyDown;
                enterEvent.keyCode = 13;
                return onEvent(enterEvent);
            }
        }
        if (m_backend->isElementVisible(m_btnBack)) {
            Rect back = m_backend->getElementRect(m_btnBack);
            if (hitTest(mx, my, back)) {
                m_focusedButton = 3;
                InputEvent enterEvent;
                enterEvent.type = InputEvent::Type::KeyDown;
                enterEvent.keyCode = 13;
                return onEvent(enterEvent);
            }
        }
    }

    // All other events consumed while modal active
    return true;
}
