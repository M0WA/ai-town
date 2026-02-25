#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend, Rect

// TaxRatePanel — stub for Phase 3. Full implementation in Phase 6.
// getBounds() is used for hit-testing in UIManager::onEvent() (Phase 6).
class TaxRatePanel {
public:
    explicit TaxRatePanel(IUIBackend* backend) : m_backend(backend) {}
    void show() { m_backend->setElementVisible(0xDEAD0103u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0103u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0103u, true);  }
    Rect getBounds() const { return {0, 0, 0, 0}; }  // Phase 6: real bounds

private:
    IUIBackend* m_backend{nullptr};
};
