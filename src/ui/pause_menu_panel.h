#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend

// Forward declaration — SettingsPanel is wired to PauseMenuPanel via setSettingsPanel()
// to avoid a circular include between pause_menu_panel.h and settings_panel.h.
class SettingsPanel;

// PauseMenuPanel — stub for Phase 3. Full implementation in Phase 6.
// Triggered by Escape during Gameplay state; routes to SettingsPanel on Settings button.
class PauseMenuPanel {
public:
    explicit PauseMenuPanel(IUIBackend* backend) : m_backend(backend) {}
    void show() { m_backend->setElementVisible(0xDEAD0107u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0107u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0107u, true);  }

    // Called by UIManager after construction to wire the settings panel pointer.
    // Phase 6 implementation uses this pointer to show SettingsPanel on button press.
    void setSettingsPanel(SettingsPanel* /*settings*/) {}

private:
    IUIBackend* m_backend{nullptr};
};
