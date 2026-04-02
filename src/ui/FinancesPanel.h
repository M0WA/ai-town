#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend, UIRect
#include <array>

// Forward declarations — avoid pulling full headers into every consumer.
class ICitySimulation;
class IAudioSystem;
class IClock;
struct InputEvent;

// FinancesPanel — 360x520 px combined Tax Rates + Budget breakdown panel.
// Merged from TaxRatePanel and BudgetDetailPanel (Phase 11l).
// Centered in virtual 1920x1080 space, anchored below the resource bar.
// Open/close fires UI_MENU_OPEN / UI_MENU_CLOSE via IAudioSystem.
// Sentinel: kSentinel = 0xDEAD0103u (same slot as old TaxRatePanel).
class FinancesPanel {
public:
    FinancesPanel(IUIBackend* backend, ICitySimulation* sim,
                  IAudioSystem* audio, IClock* clock);

    void open();
    void close();
    bool isOpen() const { return m_visible; }
    void update(float dt);
    void draw();
    bool onEvent(const InputEvent& event);

    // Returns true if a tax rate change has been made since the last budget tick.
    bool hasPendingRateChange() const;
    void clearPendingRateChange();

    // Sentinel for draw-order tests.
    static constexpr UIElementHandle kSentinel{0xDEAD0103u};

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    IAudioSystem*    m_audio{nullptr};
    IClock*          m_clock{nullptr};
    bool             m_visible{false};
    bool             m_pendingRateChange{false};

    // Panel dimensions (virtual 1920x1080 space)
    static constexpr int kPanelX = 780;   // centered: (1920-360)/2
    static constexpr int kPanelY = 60;    // below resource bar
    static constexpr int kPanelW = 360;
    static constexpr int kPanelH = 520;

    // --- Section 1: Tax Rates ---
    UIElementHandle m_panelBg{kInvalidUIElement};
    UIElementHandle m_titleLabel{kInvalidUIElement};

    // Per-zone row: label, decrement, readout, increment
    struct ZoneRow {
        UIElementHandle label{kInvalidUIElement};
        UIElementHandle btnDec{kInvalidUIElement};
        UIElementHandle readout{kInvalidUIElement};
        UIElementHandle btnInc{kInvalidUIElement};
    };
    // D-11 / UI-13: indexed by (int)ZoneType — [0]=Residential, [1]=Commercial, [2]=Industrial
    std::array<ZoneRow, 3> m_rows{};

    UIElementHandle m_noUndoLabel{kInvalidUIElement};

    // Key repeat state
    bool  m_holdingInc{false};
    bool  m_holdingDec{false};
    int   m_holdZone{-1};          // 0=R, 1=C, 2=I
    float m_holdTimer{0.0f};
    int   m_holdDelta{0};          // cumulative change during hold (±5 pp cap)
    bool  m_initialRepeatDone{false};

    // --- Section 2: Budget breakdown ---
    UIElementHandle m_budgetSeparator{kInvalidUIElement};  // section separator
    UIElementHandle m_incomeHeader{kInvalidUIElement};

    UIElementHandle m_taxRevenueR{kInvalidUIElement};
    UIElementHandle m_taxRevenueC{kInvalidUIElement};
    UIElementHandle m_taxRevenueI{kInvalidUIElement};
    UIElementHandle m_utilityFees{kInvalidUIElement};
    UIElementHandle m_tourismIncome{kInvalidUIElement};  // always $0, grayed

    UIElementHandle m_expenseSeparator{kInvalidUIElement};
    UIElementHandle m_expensesHeader{kInvalidUIElement};

    UIElementHandle m_roadMaint{kInvalidUIElement};
    UIElementHandle m_serviceUpkeep{kInvalidUIElement};
    UIElementHandle m_wages{kInvalidUIElement};

    UIElementHandle m_netSeparator{kInvalidUIElement};
    UIElementHandle m_netBalance{kInvalidUIElement};

    // Helpers
    ZoneRow createRow(const char* label, int y);
    void setRowEnabled(ZoneRow& row, float rate);
    void updateRowText(ZoneRow& row, float rate);
    void showAllElements();
    void hideAllElements();
};
