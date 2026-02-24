#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend

// MainMenuPanel — stub for Phase 3. Full implementation in Phase 6.
// The constructor calls show() so that UIManager::UIManager() shows the main menu
// immediately on construction (as required by the startup flow spec).
class MainMenuPanel {
public:
    explicit MainMenuPanel(IUIBackend* backend) : m_backend(backend) {
        show();  // spec: main menu is visible immediately on construction
    }
    void show() { m_backend->setElementVisible(0xDEAD0101u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0101u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0101u, true);  }

private:
    IUIBackend* m_backend{nullptr};
};
