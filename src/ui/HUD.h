#pragma once
#include "src/interfaces/IUIBackend.h"            // UIElementHandle, IUIBackend
#include "src/interfaces/ICitySimulation.h"// ICitySimulation
#include "src/interfaces/IClock.h"         // IClock

// Forward declarations — full includes in HUD.cpp only.
class IAudioSystem;
class FinancesPanel;

// HUD — the in-gameplay heads-up display.
// Owns FinancesPanel as a sub-panel.
// Parameter order (backend, audio, sim, clock) matches the UIManager canonical order.
//
// All coordinates are in virtual 1920x1080 space.
// Creates UI elements in constructor; updates state in draw()/update().
class HUD {
public:
    HUD(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock);
    ~HUD();

    void show();
    void hide();
    void draw();
    void update(float dt);

    // Mark or clear the unsaved-changes indicator dot.
    void setUnsavedChanges(bool unsaved);

    // Update the active-tool indicator text (e.g. "Zone", "Road", "Query").
    void setActiveToolLabel(const std::string& text);

    // Access the owned finances sub-panel.
    FinancesPanel* getFinances() const { return m_finances; }

    // Toggle the Finances panel open/closed.
    void toggleFinancesPanel();

    // Phase 11m: Reset grace period for a new game session.
    // Resets m_gameStartTime, unhides and fully-opaque the grace period label.
    // Called by UIManager::transitionToGameplay() for both first-game and subsequent new games.
    void notifyGameStarted();

private:
    IUIBackend*        m_backend{nullptr};
    IAudioSystem*      m_audio{nullptr};
    ICitySimulation*   m_sim{nullptr};
    IClock*            m_clock{nullptr};
    FinancesPanel*     m_finances{nullptr};

    bool m_visible{false};

    // --- Resource / budget bar (top, y:0-56) ---
    UIElementHandle m_treasuryLabel{kInvalidUIElement};
    UIElementHandle m_debtLabel{kInvalidUIElement};
    UIElementHandle m_ratingLabel{kInvalidUIElement};
    UIElementHandle m_populationLabel{kInvalidUIElement};
    UIElementHandle m_dateLabel{kInvalidUIElement};

    // --- Primary toolbar (left, x:8-72, y:64-600) ---
    UIElementHandle m_btnZone{kInvalidUIElement};
    UIElementHandle m_btnRoad{kInvalidUIElement};
    UIElementHandle m_btnUtilities{kInvalidUIElement};
    UIElementHandle m_btnDemolish{kInvalidUIElement};
    UIElementHandle m_btnQuery{kInvalidUIElement};

    // --- Undo button (x:8-72, y:608-656) ---
    UIElementHandle m_btnUndo{kInvalidUIElement};

    // --- Grace period indicator (x:8-1912, y:60-92) ---
    UIElementHandle m_gracePeriodLabel{kInvalidUIElement};
    double          m_graceElapsedSeconds{0.0};  // accumulated only when sim is not paused
    bool            m_gracePeriodExpired{false};
    float           m_graceFadeAlpha{1.0f};

    // --- Demand pressure bars (x:8-72, y:664-744) ---
    UIElementHandle m_demandBarR{kInvalidUIElement};
    UIElementHandle m_demandBarC{kInvalidUIElement};
    UIElementHandle m_demandBarI{kInvalidUIElement};
    UIElementHandle m_demandLabelR{kInvalidUIElement};
    UIElementHandle m_demandLabelC{kInvalidUIElement};
    UIElementHandle m_demandLabelI{kInvalidUIElement};

    // --- Active tool indicator (x:8-72, y:752-784) ---
    UIElementHandle m_activeToolLabel{kInvalidUIElement};

    // --- Notification bell (x:1820-1868, y:8-56) ---
    UIElementHandle m_notifBell{kInvalidUIElement};

    // --- Unsaved changes dot (x:1796-1812, y:8-24) ---
    UIElementHandle m_unsavedDotHandle{kInvalidUIElement};

    // --- Speed selector (top-right, x:1600-1796, y:8-56) ---
    UIElementHandle m_btnPause{kInvalidUIElement};
    UIElementHandle m_btnSpeed1{kInvalidUIElement};
    UIElementHandle m_btnSpeed3{kInvalidUIElement};
    UIElementHandle m_btnSpeed10{kInvalidUIElement};

    // --- Red flashing budget state ---
    float m_budgetFlashTimer{0.0f};

    // --- Tax rate pending indicator ---
    UIElementHandle m_taxPendingLabel{kInvalidUIElement};
};
