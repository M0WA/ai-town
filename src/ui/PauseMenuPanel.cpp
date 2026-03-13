// src/ui/PauseMenuPanel.cpp
//
// PauseMenuPanel -- pause overlay with Resume, Settings, Save,
// Quit to Main Menu, Quit to Desktop buttons.
// Centered vertically in 1920x1080 virtual space.
// Keyboard navigation: Up/Down arrows cycle focus, Enter activates, Escape = Resume.

#include "src/ui/PauseMenuPanel.h"
#include "src/ui/SettingsPanel.h"
#include "src/platform/input_event.h"

#include <string>

// Panel dimensions (centered in 1920x1080)
static constexpr int kPanelW = 300;
static constexpr int kPanelH = 400;
static constexpr int kPanelX = (1920 - kPanelW) / 2;
static constexpr int kPanelY = (1080 - kPanelH) / 2;
static constexpr int kBtnW   = 260;
static constexpr int kBtnH   = 48;
static constexpr int kBtnX   = kPanelX + 20;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
PauseMenuPanel::PauseMenuPanel(IUIBackend* backend)
    : m_backend(backend)
    , m_settings(nullptr)
    , m_visible(false)
    , m_focusedButton(0)
{
    if (!m_backend) return;

    m_panelBg    = m_backend->addStaticText("", kPanelX, kPanelY, kPanelW, kPanelH);
    m_titleLabel = m_backend->addStaticText("Paused", kPanelX + 20, kPanelY + 8, kPanelW - 40, 36);

    int y = kPanelY + 52;
    m_btnResume      = m_backend->addButton("Resume",           kBtnX, y, kBtnW, kBtnH); y += kBtnH + 8;
    m_btnSettings    = m_backend->addButton("Settings",         kBtnX, y, kBtnW, kBtnH); y += kBtnH + 8;
    m_btnSave        = m_backend->addButton("Save",             kBtnX, y, kBtnW, kBtnH); y += kBtnH + 8;
    m_btnQuitToMenu  = m_backend->addButton("Quit to Main Menu",kBtnX, y, kBtnW, kBtnH); y += kBtnH + 8;
    m_btnQuitDesktop = m_backend->addButton("Quit to Desktop",  kBtnX, y, kBtnW, kBtnH);

    hide();
}

// ---------------------------------------------------------------------------
// setSettingsPanel
// ---------------------------------------------------------------------------
void PauseMenuPanel::setSettingsPanel(SettingsPanel* settings) {
    m_settings = settings;
}

// ---------------------------------------------------------------------------
// consumeQuitDesktopRequest / consumeQuitToMenuRequest
// ---------------------------------------------------------------------------
bool PauseMenuPanel::consumeQuitDesktopRequest() {
    if (m_quitDesktopRequested) {
        m_quitDesktopRequested = false;
        return true;
    }
    return false;
}

bool PauseMenuPanel::consumeQuitToMenuRequest() {
    if (m_quitToMenuRequested) {
        m_quitToMenuRequested = false;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void PauseMenuPanel::show() {
    m_visible = true;
    m_focusedButton = 0; // Default focus on Resume
    if (!m_backend) return;

    m_backend->setElementVisible(m_panelBg,       true);
    m_backend->setElementVisible(m_titleLabel,    true);
    m_backend->setElementVisible(m_btnResume,     true);
    m_backend->setElementVisible(m_btnSettings,   true);
    m_backend->setElementVisible(m_btnSave,       true);
    m_backend->setElementVisible(m_btnQuitToMenu, true);
    m_backend->setElementVisible(m_btnQuitDesktop,true);
}

void PauseMenuPanel::hide() {
    m_visible = false;
    if (!m_backend) return;

    m_backend->setElementVisible(m_panelBg,       false);
    m_backend->setElementVisible(m_titleLabel,    false);
    m_backend->setElementVisible(m_btnResume,     false);
    m_backend->setElementVisible(m_btnSettings,   false);
    m_backend->setElementVisible(m_btnSave,       false);
    m_backend->setElementVisible(m_btnQuitToMenu, false);
    m_backend->setElementVisible(m_btnQuitDesktop,false);
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void PauseMenuPanel::draw() {
    if (!m_visible || !m_backend) return;

    // Update focus visual state
    UIElementHandle buttons[kNumButtons] = {
        m_btnResume, m_btnSettings, m_btnSave, m_btnQuitToMenu, m_btnQuitDesktop
    };
    const char* labels[kNumButtons] = {
        "Resume", "Settings", "Save", "Quit to Main Menu", "Quit to Desktop"
    };

    for (int i = 0; i < kNumButtons; ++i) {
        if (i == m_focusedButton) {
            std::string focused = std::string("> ") + labels[i] + " <";
            m_backend->setElementText(buttons[i], focused);
        } else {
            m_backend->setElementText(buttons[i], labels[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// onEvent
// ---------------------------------------------------------------------------
bool PauseMenuPanel::onEvent(const InputEvent& event) {
    if (!m_visible || !m_backend) return false;

    if (event.type == InputEvent::Type::KeyDown) {
        int key = event.keyCode;

        // Escape = Resume
        if (key == 27) {
            hide();
            return true;
        }

        // Up arrow (38): move focus up
        if (key == 38) {
            m_focusedButton = (m_focusedButton > 0) ? m_focusedButton - 1 : kNumButtons - 1;
            return true;
        }
        // Down arrow (40): move focus down
        if (key == 40) {
            m_focusedButton = (m_focusedButton < kNumButtons - 1) ? m_focusedButton + 1 : 0;
            return true;
        }
        // Tab: cycle focus forward
        if (key == 9) {
            m_focusedButton = (m_focusedButton + 1) % kNumButtons;
            return true;
        }

        // Enter: activate focused button
        if (key == 13) {
            switch (m_focusedButton) {
                case 0: // Resume
                    hide();
                    return true;
                case 1: // Settings
                    if (m_settings) {
                        hide();
                        m_settings->show();
                    }
                    return true;
                case 2: // Save
                    // Save slot dialog would show here (deferred to sub-dialog)
                    return true;
                case 3: // Quit to Main Menu
                    m_quitToMenuRequested = true;
                    hide();
                    return true;
                case 4: // Quit to Desktop
                    m_quitDesktopRequested = true;
                    hide();
                    return true;
                default:
                    break;
            }
            return true;
        }
    }

    // Mouse clicks
    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        // Outside panel -- consume (pause menu blocks all input)
        if (mx < kPanelX || mx > kPanelX + kPanelW ||
            my < kPanelY || my > kPanelY + kPanelH) {
            return true; // Blocked -- pause menu is active
        }

        UIElementHandle buttons[kNumButtons] = {
            m_btnResume, m_btnSettings, m_btnSave, m_btnQuitToMenu, m_btnQuitDesktop
        };

        for (int i = 0; i < kNumButtons; ++i) {
            Rect r = m_backend->getElementRect(buttons[i]);
            if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
                m_focusedButton = i;
                // Simulate Enter press
                InputEvent enterEvent;
                enterEvent.type = InputEvent::Type::KeyDown;
                enterEvent.keyCode = 13;
                return onEvent(enterEvent);
            }
        }

        return true; // Consume click inside panel
    }

    // Consume all events while pause menu is active
    return true;
}
