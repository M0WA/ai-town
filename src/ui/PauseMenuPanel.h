#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend

// Forward declaration -- SettingsPanel is wired to PauseMenuPanel via setSettingsPanel()
// to avoid a circular include between pause_menu_panel.h and settings_panel.h.
class SettingsPanel;
struct InputEvent;

// PauseMenuPanel -- pause overlay with: Resume, Settings, Save, Quit to Menu, Quit buttons.
// Shown when user presses Escape during gameplay.
class PauseMenuPanel {
public:
    explicit PauseMenuPanel(IUIBackend* backend);

    void show();
    void hide();
    void draw();
    bool onEvent(const InputEvent& event);

    // Called by UIManager after construction to wire the settings panel pointer.
    void setSettingsPanel(SettingsPanel* settings);
    bool isVisible() const { return m_visible; }

    // Polling API for UIManager: returns true (once) when Quit to Desktop was clicked.
    bool consumeQuitDesktopRequest();

    // Polling API for UIManager: returns true (once) when Quit to Main Menu was clicked.
    bool consumeQuitToMenuRequest();

private:
    IUIBackend*    m_backend{nullptr};
    SettingsPanel* m_settings{nullptr};
    bool           m_visible{false};

    // Panel elements (centered vertically, 300 px wide)
    UIElementHandle m_panelBg{kInvalidUIElement};
    UIElementHandle m_titleLabel{kInvalidUIElement};
    UIElementHandle m_btnResume{kInvalidUIElement};
    UIElementHandle m_btnSettings{kInvalidUIElement};
    UIElementHandle m_btnSave{kInvalidUIElement};
    UIElementHandle m_btnQuitToMenu{kInvalidUIElement};
    UIElementHandle m_btnQuitDesktop{kInvalidUIElement};

    // Focus tracking for keyboard navigation
    int m_focusedButton{0};
    static constexpr int kNumButtons = 5;

    // Polling flags
    bool m_quitDesktopRequested{false};
    bool m_quitToMenuRequested{false};
};
