#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend, Rect

// Forward declaration — avoid pulling ICitySimulation.h into every consumer.
class ICitySimulation;
struct InputEvent;

// TaxRatePanel — 300x200 px floating panel for per-zone tax rate adjustment.
// Anchored below resource bar, horizontally centered.
// Rate bounds: 1%-25%, key repeat 400ms initial / 150ms repeat, +/-5 cap per hold.
class TaxRatePanel {
public:
    TaxRatePanel(IUIBackend* backend, ICitySimulation* sim);

    void show();
    void hide();
    void draw();
    bool onEvent(const InputEvent& event);
    bool isOpen() const { return m_visible; }
    Rect getBounds() const;

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    bool             m_visible{false};

    // Panel position in virtual 1920x1080 space
    static constexpr int kPanelX = 810;  // centered: (1920-300)/2
    static constexpr int kPanelY = 60;   // below resource bar
    static constexpr int kPanelW = 300;
    static constexpr int kPanelH = 200;

    // Panel elements
    UIElementHandle m_panelBg{kInvalidUIElement};
    UIElementHandle m_titleLabel{kInvalidUIElement};

    // Per-zone row: label, decrement, readout, increment
    struct ZoneRow {
        UIElementHandle label{kInvalidUIElement};
        UIElementHandle btnDec{kInvalidUIElement};
        UIElementHandle readout{kInvalidUIElement};
        UIElementHandle btnInc{kInvalidUIElement};
    };
    ZoneRow m_rowR;  // Residential
    ZoneRow m_rowC;  // Commercial
    ZoneRow m_rowI;  // Industrial

    // "Tax changes cannot be undone" label
    UIElementHandle m_noUndoLabel{kInvalidUIElement};

    // Key repeat state
    bool   m_holdingInc{false};
    bool   m_holdingDec{false};
    int    m_holdZone{-1};        // 0=R, 1=C, 2=I
    float  m_holdTimer{0.0f};
    int    m_holdDelta{0};        // cumulative change during hold
    bool   m_initialRepeatDone{false};

    // Helper to create a zone row
    ZoneRow createRow(const char* label, int y);
    void setRowEnabled(ZoneRow& row, float rate);
    void updateRowText(ZoneRow& row, float rate);
};
