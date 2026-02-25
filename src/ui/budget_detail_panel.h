#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend

// BudgetDetailPanel — owned by HUD (NOT a top-level UIManager panel).
// HUD creates and destroys this panel; UIManager never holds a pointer to it directly.
// Stub for Phase 3. Full implementation in Phase 6.
class BudgetDetailPanel {
public:
    explicit BudgetDetailPanel(IUIBackend* backend) : m_backend(backend) {}
    void show() { m_backend->setElementVisible(0xDEAD0110u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0110u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0110u, true);  }

private:
    IUIBackend* m_backend{nullptr};
};
