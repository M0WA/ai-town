#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend

// SettingsPanel — stub for Phase 3. Full implementation in Phase 6.
// Accessible from PauseMenuPanel and from the New Game flow pre-game.
// Phase 8 adds the Colorblind mode toggle under Graphics tab > Accessibility subsection.
class SettingsPanel {
public:
    explicit SettingsPanel(IUIBackend* backend) : m_backend(backend) {}
    void show() { m_backend->setElementVisible(0xDEAD0108u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0108u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0108u, true);  }

private:
    IUIBackend* m_backend{nullptr};
};
