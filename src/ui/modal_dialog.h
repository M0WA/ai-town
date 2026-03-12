#pragma once
#include "src/ui/IUIBackend.h"             // UIElementHandle, IUIBackend
#include "src/interfaces/ICitySimulation.h" // ICitySimulation

class IAudioSystem;
struct InputEvent;
struct LoanTerms;

// ModalDialog — handles all blocking modal dialogs:
//   - Forced-loan dialog (2-screen flow, 640x400 px virtual)
//   - Demolish confirmation (480x240 px virtual)
//   - WASD preset confirmation (480x240 px virtual)
//   - Game-over modal (560x320 px virtual, stub in V1)
//
// When active, isActive() returns true and UIManager::onEvent() routes all input
// to the modal at Priority 4, blocking the game world and HUD input.
class ModalDialog {
public:
    ModalDialog(IUIBackend* backend, ICitySimulation* sim);

    void show();
    void hide();
    void draw();
    bool isActive() const;
    bool onEvent(const InputEvent& event);

    // --- Dialog launchers ---
    void showForcedLoan(const LoanTerms& terms);
    void showDemolishConfirm(int tileCount);
    void showWASDPreset();
    void showGameOver(int64_t debt, int months);

    // Result accessors for UIManager to check after dialog closes
    enum class DialogResult { None, Accept, Decline, Cancel };
    DialogResult getLastResult() const { return m_lastResult; }

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    bool             m_active{false};
    bool             m_didPauseSim{false};

    DialogResult m_lastResult{DialogResult::None};

    // Current dialog type
    enum class DialogType {
        None,
        ForcedLoan,
        ForcedLoanScreen2,
        DemolishConfirm,
        WASDPreset,
        GameOver
    };
    DialogType m_dialogType{DialogType::None};

    // Scrim element (full-screen 50% opacity overlay)
    UIElementHandle m_scrim{kInvalidUIElement};

    // Dialog container elements
    UIElementHandle m_dialogBg{kInvalidUIElement};
    UIElementHandle m_titleLabel{kInvalidUIElement};
    UIElementHandle m_bodyLabel{kInvalidUIElement};
    UIElementHandle m_btnPrimary{kInvalidUIElement};
    UIElementHandle m_btnSecondary{kInvalidUIElement};
    UIElementHandle m_btnTertiary{kInvalidUIElement};
    UIElementHandle m_btnBack{kInvalidUIElement};

    // Stored loan terms for forced-loan dialog
    float m_loanAmount{0.0f};
    int   m_loanRepaymentTicks{0};
    float m_loanInterestRate{0.05f};

    // Stored context for game-over
    int64_t m_gameOverDebt{0};
    int     m_gameOverMonths{0};

    // Focus tracking for keyboard navigation
    int m_focusedButton{0};  // 0-based index into active buttons

    // Stored dialog rectangle in virtual 1920x1080 space.
    // Set by setDialogRect(); used by layout helpers to position content elements
    // relative to the centred dialog box without hardcoding screen coordinates.
    int m_dialogX{0};
    int m_dialogY{0};
    int m_dialogW{0};
    int m_dialogH{0};

    void openModal(DialogType type);
    void closeModal();
    void layoutForcedLoanScreen1();
    void layoutForcedLoanScreen2();
    void layoutDemolishConfirm(int tileCount);
    void layoutWASDPreset();
    void layoutGameOver(int64_t debt, int months);
    void hideAllDialogElements();
    void setDialogRect(int w, int h);
};
