#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend, Rect

// Minimap — stub for Phase 3. Full implementation in Phase 6.
// getBounds() is used by UIManager::onEvent() for the Priority-3 minimap carve-out
// hit-test (Phase 6 implementation). Returns zero rect until Phase 6 places it.
class Minimap {
public:
    explicit Minimap(IUIBackend* backend) : m_backend(backend) {}
    void show() { m_backend->setElementVisible(0xDEAD0102u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0102u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0102u, true);  }
    Rect getBounds() const { return {0, 0, 0, 0}; }  // Phase 6: real bounds

private:
    IUIBackend* m_backend{nullptr};
};
