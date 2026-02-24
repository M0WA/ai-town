#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend, Rect

// InspectorPanel (Query Panel) — stub for Phase 3. Full implementation in Phase 6.
// getBounds() is used for hit-testing in UIManager::onEvent() (Phase 6).
class InspectorPanel {
public:
    explicit InspectorPanel(IUIBackend* backend) : m_backend(backend) {}
    void show() { m_backend->setElementVisible(0xDEAD0104u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0104u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0104u, true);  }
    Rect getBounds() const { return {0, 0, 0, 0}; }  // Phase 6: real bounds

private:
    IUIBackend* m_backend{nullptr};
};
