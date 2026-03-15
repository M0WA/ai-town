#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend

// Forward declaration — avoid pulling ICitySimulation.h into every consumer.
class ICitySimulation;

// BudgetDetailPanel — owned by HUD (NOT a top-level UIManager panel).
// HUD creates and destroys this panel; UIManager never holds a pointer to it directly.
// 320x200 px panel with 8 named line items plus density unlock preview.
class BudgetDetailPanel {
public:
    BudgetDetailPanel(IUIBackend* backend, ICitySimulation* sim);

    void show();
    void hide();
    void draw();
    void update();

    bool isVisible() const { return m_visible; }

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    bool             m_visible{false};

    // Panel background / container
    UIElementHandle m_panelBg{kInvalidUIElement};

    // 8 line items
    UIElementHandle m_taxRevenueR{kInvalidUIElement};
    UIElementHandle m_taxRevenueC{kInvalidUIElement};
    UIElementHandle m_taxRevenueI{kInvalidUIElement};
    UIElementHandle m_wages{kInvalidUIElement};
    UIElementHandle m_roadMaint{kInvalidUIElement};
    UIElementHandle m_serviceUpkeep{kInvalidUIElement};
    UIElementHandle m_utilityFees{kInvalidUIElement};
    UIElementHandle m_netBalance{kInvalidUIElement};

    // Density unlock preview (bottom of panel)
    UIElementHandle m_unlockPreview{kInvalidUIElement};
};
