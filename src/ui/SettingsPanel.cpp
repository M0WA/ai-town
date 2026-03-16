// src/ui/SettingsPanel.cpp
//
// SettingsPanel -- 4-tab settings screen.
// Tabs: Graphics, Controls, Audio, Gameplay.
// Graphics: resolution, Vsync, MSAA, colorblind toggle, Apply with 10s countdown auto-revert.
// Controls: edge scroll, mouse sensitivity, key rebindings, WASD preset button.
// Audio: master/music/SFX volume sliders wired to IAudioSystem.
// Gameplay: difficulty (read-only), demolish confirm toggle, disaster (grayed).

#include "src/ui/SettingsPanel.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/IClock.h"
#include "src/ui/KeyBindingsPanel.h"
#include "src/ui/ModalDialog.h"
#include "src/ui/PauseMenuPanel.h"
#include "src/platform/input_event.h"

#include <cstdio>
#include <string>
#include <algorithm>
#include <functional>

// Panel dimensions: roughly centered in 1920x1080
static constexpr int kSettingsX = 360;
static constexpr int kSettingsY = 140;
static constexpr int kSettingsW = 1200;
static constexpr int kSettingsH = 800;

// Tab strip Y and height
static constexpr int kTabStripY = kSettingsY + 4;
static constexpr int kTabW = 140;
static constexpr int kTabH = 36;

// Content area
static constexpr int kContentX = kSettingsX + 16;
static constexpr int kContentY = kSettingsY + 50;
static constexpr int kContentW = kSettingsW - 32;
static constexpr int kLineH = 32;

// Bottom buttons
static constexpr int kBtnY = kSettingsY + kSettingsH - 52;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SettingsPanel::SettingsPanel(IUIBackend* backend, IAudioSystem* audio, IClock* clock,
                             ModalDialog* modal)
    : m_backend(backend)
    , m_audio(audio)
    , m_clock(clock)
    , m_modal(modal)
    , m_pauseMenu(nullptr)
    , m_visible(false)
    , m_activeTab(0)
{
    if (!m_backend) return;

    // --- Glass City full-screen scrim (rgba(0,0,0,0.50)) ---
    // Created at full virtual canvas size; shown/hidden with the panel.
    m_scrimHandle = m_backend->addStaticText("",
        0, 0, m_backend->getVirtualWidth(), m_backend->getVirtualHeight());
    m_backend->setElementBackground(m_scrimHandle, 0, 0, 0, 128); // 50% opacity black
    m_backend->setElementVisible(m_scrimHandle, false);

    // --- Glass City deep-navy panel background (rgba(13,27,42,0.88)) ---
    // 8 px corner radius is baked visually via the panel tile or draw2DRectangle fallback.
    // Use the sprite panel tile (hud_sprites_ui rows 16+) when available;
    // fall back to setElementBackground with the nearest solid equivalent.
    m_bgHandle = m_backend->addStaticText("", kSettingsX, kSettingsY, kSettingsW, kSettingsH);
    m_backend->setElementBackground(m_bgHandle, 13, 27, 42, 224); // rgba(13,27,42,0.88)
    m_backend->setElementVisible(m_bgHandle, false);

    // Panel background (legacy handle, kept for backward compatibility with tests
    // that reference m_panelBg via the show/hide element-visible pattern).
    m_panelBg = m_backend->addStaticText("", kSettingsX, kSettingsY, kSettingsW, kSettingsH);

    // Tab header buttons
    int tabX = kSettingsX + 16;
    m_tabGraphics = m_backend->addButton("Graphics",  tabX, kTabStripY, kTabW, kTabH); tabX += kTabW + 4;
    m_tabControls = m_backend->addButton("Controls",  tabX, kTabStripY, kTabW, kTabH); tabX += kTabW + 4;
    m_tabAudio    = m_backend->addButton("Audio",     tabX, kTabStripY, kTabW, kTabH); tabX += kTabW + 4;
    m_tabGameplay = m_backend->addButton("Gameplay",  tabX, kTabStripY, kTabW, kTabH);

    // Bottom buttons (shared)
    m_btnApply           = m_backend->addButton("Apply",            kSettingsX + 500, kBtnY, 140, 40);
    m_btnCancel          = m_backend->addButton("Cancel",           kSettingsX + 650, kBtnY, 140, 40);
    m_btnRestoreDefaults = m_backend->addButton("Restore Defaults", kSettingsX + 800, kBtnY, 180, 40);

    // --- Graphics tab elements ---
    int y = kContentY;
    m_gfxResolutionLabel = m_backend->addStaticText("Resolution: 1920x1080", kContentX, y, kContentW, kLineH); y += kLineH + 4;
    m_gfxVsyncLabel      = m_backend->addStaticText("VSync: On",             kContentX, y, kContentW, kLineH); y += kLineH + 4;
    m_gfxMsaaLabel       = m_backend->addStaticText("MSAA: 4x",              kContentX, y, kContentW, kLineH); y += kLineH + 4;
    m_gfxColorblindBtn   = m_backend->addButton("Colorblind Mode: Off",      kContentX, y, 300, kLineH);

    // Countdown label for graphics auto-revert
    m_countdownLabel = m_backend->addStaticText("", kContentX, y + kLineH + 8, 400, kLineH);

    // --- Controls tab elements ---
    y = kContentY;
    m_ctrlEdgeScrollLabel   = m_backend->addStaticText("Edge Scroll: On",         kContentX, y, kContentW, kLineH); y += kLineH + 4;
    m_ctrlSensitivityLabel  = m_backend->addStaticText("Mouse Sensitivity: 1.0",  kContentX, y, kContentW, kLineH); y += kLineH + 4;
    m_ctrlWasdPresetBtn     = m_backend->addButton("WASD Preset",                 kContentX, y, 200, kLineH);

    // --- Audio tab elements ---
    y = kContentY;
    m_audioMasterLabel  = m_backend->addStaticText("Master Volume:",  kContentX, y, 200, kLineH);
    m_audioMasterSlider = m_backend->addStaticText("[==========] 100%", kContentX + 200, y, 300, kLineH); y += kLineH + 4;
    m_audioMusicLabel   = m_backend->addStaticText("Music Volume:",   kContentX, y, 200, kLineH);
    m_audioMusicSlider  = m_backend->addStaticText("[========  ]  80%", kContentX + 200, y, 300, kLineH); y += kLineH + 4;
    m_audioSfxLabel     = m_backend->addStaticText("SFX Volume:",     kContentX, y, 200, kLineH);
    m_audioSfxSlider    = m_backend->addStaticText("[========  ]  80%", kContentX + 200, y, 300, kLineH);

    // --- Gameplay tab elements ---
    y = kContentY;
    m_gameplayDiffLabel      = m_backend->addStaticText("Difficulty: Normal (read-only)", kContentX, y, kContentW, kLineH); y += kLineH + 4;
    m_gameplayDemolishToggle = m_backend->addButton("Confirm before demolish: On",        kContentX, y, 400, kLineH); y += kLineH + 4;
    m_gameplayDisasterToggle = m_backend->addButton("Disasters: Post-launch",             kContentX, y, 400, kLineH);
    m_backend->setElementEnabled(m_gameplayDisasterToggle, false); // Grayed in V1

    // --- Glass City text colour pass ---
    // Tab buttons: near-white #EBF4F6
    m_backend->setElementTextColor(m_tabGraphics, 0xEB, 0xF4, 0xF6);
    m_backend->setElementTextColor(m_tabControls, 0xEB, 0xF4, 0xF6);
    m_backend->setElementTextColor(m_tabAudio,    0xEB, 0xF4, 0xF6);
    m_backend->setElementTextColor(m_tabGameplay, 0xEB, 0xF4, 0xF6);
    // Bottom buttons: near-white #EBF4F6
    m_backend->setElementTextColor(m_btnApply,           0xEB, 0xF4, 0xF6);
    m_backend->setElementTextColor(m_btnCancel,          0xEB, 0xF4, 0xF6);
    m_backend->setElementTextColor(m_btnRestoreDefaults, 0xEB, 0xF4, 0xF6);
    // Graphics tab field labels: mid-blue #4A7FA5
    m_backend->setElementTextColor(m_gfxResolutionLabel, 0x4A, 0x7F, 0xA5);
    m_backend->setElementTextColor(m_gfxVsyncLabel,      0x4A, 0x7F, 0xA5);
    m_backend->setElementTextColor(m_gfxMsaaLabel,       0x4A, 0x7F, 0xA5);
    // Audio tab field labels: mid-blue #4A7FA5
    m_backend->setElementTextColor(m_audioMasterLabel, 0x4A, 0x7F, 0xA5);
    m_backend->setElementTextColor(m_audioMusicLabel,  0x4A, 0x7F, 0xA5);
    m_backend->setElementTextColor(m_audioSfxLabel,    0x4A, 0x7F, 0xA5);
    // Gameplay tab: difficulty label mid-blue; disaster toggle mid-blue at 50% alpha
    m_backend->setElementTextColor(m_gameplayDiffLabel, 0x4A, 0x7F, 0xA5);
    m_backend->setElementTextColor(m_gameplayDisasterToggle, 0x4A, 0x7F, 0xA5);
    m_backend->setElementAlpha(m_gameplayDisasterToggle, 0.5f);

    // --- KeyBindingsPanel (Controls tab) ---
    m_keyBindings = new KeyBindingsPanel(m_backend, m_modal);

    hide();
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
SettingsPanel::~SettingsPanel() {
    delete m_keyBindings;
}

// ---------------------------------------------------------------------------
// setPauseMenu / setModal
// ---------------------------------------------------------------------------
void SettingsPanel::setPauseMenu(PauseMenuPanel* pauseMenu) {
    m_pauseMenu = pauseMenu;
}

void SettingsPanel::setModal(ModalDialog* modal) {
    m_modal = modal;
    // Just update the modal pointer on the existing KeyBindingsPanel.
    // Do NOT delete/recreate — that would leave orphaned Irrlicht GUI elements
    // in guiEnvironment, doubling element count and slowing drawAll() every frame.
    if (m_keyBindings) {
        m_keyBindings->setModal(modal);
    }
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void SettingsPanel::show() {
    m_visible = true;
    if (!m_backend) return;

    // Glass City scrim and panel background.
    m_backend->setElementVisible(m_scrimHandle, true);
    m_backend->setElementVisible(m_bgHandle,    true);

    m_backend->setElementVisible(m_panelBg, true);
    m_backend->setElementVisible(m_tabGraphics, true);
    m_backend->setElementVisible(m_tabControls, true);
    m_backend->setElementVisible(m_tabAudio, true);
    m_backend->setElementVisible(m_tabGameplay, true);
    m_backend->setElementVisible(m_btnApply, true);
    m_backend->setElementVisible(m_btnCancel, true);
    m_backend->setElementVisible(m_btnRestoreDefaults, true);

    switchTab(m_activeTab);
}

void SettingsPanel::hide() {
    m_visible = false;
    m_countdownActive = false;
    if (!m_backend) return;

    // Glass City scrim and panel background.
    m_backend->setElementVisible(m_scrimHandle, false);
    m_backend->setElementVisible(m_bgHandle,    false);

    m_backend->setElementVisible(m_panelBg, false);
    m_backend->setElementVisible(m_tabGraphics, false);
    m_backend->setElementVisible(m_tabControls, false);
    m_backend->setElementVisible(m_tabAudio, false);
    m_backend->setElementVisible(m_tabGameplay, false);
    m_backend->setElementVisible(m_btnApply, false);
    m_backend->setElementVisible(m_btnCancel, false);
    m_backend->setElementVisible(m_btnRestoreDefaults, false);

    hideAllTabElements();
}

// ---------------------------------------------------------------------------
// hideAllTabElements
// ---------------------------------------------------------------------------
void SettingsPanel::hideAllTabElements() {
    if (!m_backend) return;

    // Graphics
    m_backend->setElementVisible(m_gfxResolutionLabel, false);
    m_backend->setElementVisible(m_gfxVsyncLabel, false);
    m_backend->setElementVisible(m_gfxMsaaLabel, false);
    m_backend->setElementVisible(m_gfxColorblindBtn, false);
    m_backend->setElementVisible(m_countdownLabel, false);

    // Controls
    m_backend->setElementVisible(m_ctrlEdgeScrollLabel, false);
    m_backend->setElementVisible(m_ctrlSensitivityLabel, false);
    m_backend->setElementVisible(m_ctrlWasdPresetBtn, false);
    if (m_keyBindings) m_keyBindings->hide();

    // Audio
    m_backend->setElementVisible(m_audioMasterLabel, false);
    m_backend->setElementVisible(m_audioMasterSlider, false);
    m_backend->setElementVisible(m_audioMusicLabel, false);
    m_backend->setElementVisible(m_audioMusicSlider, false);
    m_backend->setElementVisible(m_audioSfxLabel, false);
    m_backend->setElementVisible(m_audioSfxSlider, false);

    // Gameplay
    m_backend->setElementVisible(m_gameplayDiffLabel, false);
    m_backend->setElementVisible(m_gameplayDemolishToggle, false);
    m_backend->setElementVisible(m_gameplayDisasterToggle, false);
}

// ---------------------------------------------------------------------------
// switchTab
// ---------------------------------------------------------------------------
void SettingsPanel::switchTab(int tabIndex) {
    m_activeTab = tabIndex;
    m_focusedElement = 0; // Reset intra-tab focus on tab switch
    hideAllTabElements();

    // Update tab header visual state
    m_backend->setElementText(m_tabGraphics, tabIndex == 0 ? "[Graphics]" : "Graphics");
    m_backend->setElementText(m_tabControls, tabIndex == 1 ? "[Controls]" : "Controls");
    m_backend->setElementText(m_tabAudio,    tabIndex == 2 ? "[Audio]"    : "Audio");
    m_backend->setElementText(m_tabGameplay, tabIndex == 3 ? "[Gameplay]" : "Gameplay");

    // Tab strip visual states:
    //   Active:   teal wash rgba(0,201,200,56)
    //   Inactive: subtle glass rgba(255,255,255,20)
    m_backend->setElementBackground(m_tabGraphics, tabIndex == 0 ? 0   : 255,
                                                   tabIndex == 0 ? 201 : 255,
                                                   tabIndex == 0 ? 200 : 255,
                                                   tabIndex == 0 ? 56  : 20);
    m_backend->setElementBackground(m_tabControls, tabIndex == 1 ? 0   : 255,
                                                   tabIndex == 1 ? 201 : 255,
                                                   tabIndex == 1 ? 200 : 255,
                                                   tabIndex == 1 ? 56  : 20);
    m_backend->setElementBackground(m_tabAudio,    tabIndex == 2 ? 0   : 255,
                                                   tabIndex == 2 ? 201 : 255,
                                                   tabIndex == 2 ? 200 : 255,
                                                   tabIndex == 2 ? 56  : 20);
    m_backend->setElementBackground(m_tabGameplay, tabIndex == 3 ? 0   : 255,
                                                   tabIndex == 3 ? 201 : 255,
                                                   tabIndex == 3 ? 200 : 255,
                                                   tabIndex == 3 ? 56  : 20);

    switch (tabIndex) {
        case 0: showGraphicsTab(); break;
        case 1: showControlsTab(); break;
        case 2: showAudioTab(); break;
        case 3: showGameplayTab(); break;
        default: break;
    }
}

void SettingsPanel::showGraphicsTab() {
    m_backend->setElementVisible(m_gfxResolutionLabel, true);
    m_backend->setElementVisible(m_gfxVsyncLabel, true);
    m_backend->setElementVisible(m_gfxMsaaLabel, true);
    m_backend->setElementVisible(m_gfxColorblindBtn, true);
    if (m_countdownActive) {
        m_backend->setElementVisible(m_countdownLabel, true);
    }
}

void SettingsPanel::showControlsTab() {
    m_backend->setElementVisible(m_ctrlEdgeScrollLabel, true);
    m_backend->setElementVisible(m_ctrlSensitivityLabel, true);
    m_backend->setElementVisible(m_ctrlWasdPresetBtn, true);
    // Open the keybinding table with the currently-applied bindings (seeded by
    // UIManager via setCurrentBindings() after loadKeybindings()).
    if (m_keyBindings) {
        m_keyBindings->openTab(m_appliedBindings);
    }
}

void SettingsPanel::showAudioTab() {
    m_backend->setElementVisible(m_audioMasterLabel, true);
    m_backend->setElementVisible(m_audioMasterSlider, true);
    m_backend->setElementVisible(m_audioMusicLabel, true);
    m_backend->setElementVisible(m_audioMusicSlider, true);
    m_backend->setElementVisible(m_audioSfxLabel, true);
    m_backend->setElementVisible(m_audioSfxSlider, true);
}

void SettingsPanel::showGameplayTab() {
    m_backend->setElementVisible(m_gameplayDiffLabel, true);
    m_backend->setElementVisible(m_gameplayDemolishToggle, true);
    m_backend->setElementVisible(m_gameplayDisasterToggle, true);
}

// ---------------------------------------------------------------------------
// draw -- refresh slider readouts and countdown timer
// ---------------------------------------------------------------------------
void SettingsPanel::draw() {
    if (!m_visible || !m_backend) return;

    // Audio slider readouts
    if (m_activeTab == 2) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Master: %.0f%%", m_masterVolume * 100.0f);
        m_backend->setElementText(m_audioMasterSlider, buf);
        std::snprintf(buf, sizeof(buf), "Music: %.0f%%", m_musicVolume * 100.0f);
        m_backend->setElementText(m_audioMusicSlider, buf);
        std::snprintf(buf, sizeof(buf), "SFX: %.0f%%", m_sfxVolume * 100.0f);
        m_backend->setElementText(m_audioSfxSlider, buf);
    }

    // Demolish confirm toggle text
    if (m_activeTab == 3) {
        m_backend->setElementText(m_gameplayDemolishToggle,
            m_demolishConfirm ? "Confirm before demolish: On" : "Confirm before demolish: Off");
    }
}

// ---------------------------------------------------------------------------
// update -- countdown timer for graphics auto-revert
// ---------------------------------------------------------------------------
void SettingsPanel::update() {
    if (!m_visible || !m_backend) return;

    if (m_countdownActive && m_clock) {
        double elapsed = m_clock->nowSeconds() - m_countdownStartTime;
        double remaining = 10.0 - elapsed;

        if (remaining <= 0.0) {
            // Auto-revert: revert graphics settings (no change was applied)
            m_countdownActive = false;
            m_backend->setElementVisible(m_countdownLabel, false);
            m_backend->setElementText(m_countdownLabel, "Settings reverted.");
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Reverting in %ds...", static_cast<int>(remaining));
            m_backend->setElementText(m_countdownLabel, buf);
            m_backend->setElementVisible(m_countdownLabel, true);
        }
    }

    // Poll WASD preset modal result
    if (m_wasdPresetPending && m_modal) {
        ModalDialog::DialogResult res = m_modal->pollResult();
        if (res == ModalDialog::DialogResult::Accept) {
            // Apply WASD preset
            if (m_keyBindings) {
                KeyBindings preset = m_keyBindings->currentBindings();
                preset.camPanUp    = "W";
                preset.camPanLeft  = "A";
                preset.camPanDown  = "S";
                preset.camPanRight = "D";
                preset.toolDemolish = "X";
                m_keyBindings->openTab(preset);
            }
            m_wasdPresetPending = false;
        } else if (res == ModalDialog::DialogResult::Cancel ||
                   res == ModalDialog::DialogResult::Decline) {
            m_wasdPresetPending = false;
        }
        // DialogResult::None: modal still open, keep polling.
    }

    // Poll Restore Defaults modal result
    if (m_restoreDefaultsPending && m_modal) {
        ModalDialog::DialogResult res = m_modal->pollResult();
        if (res == ModalDialog::DialogResult::Accept) {
            if (m_keyBindings) {
                m_keyBindings->resetToDefaults();
            }
            m_restoreDefaultsPending = false;
        } else if (res == ModalDialog::DialogResult::Cancel ||
                   res == ModalDialog::DialogResult::Decline) {
            m_restoreDefaultsPending = false;
        }
        // DialogResult::None: modal still open, keep polling.
    }
}

// ---------------------------------------------------------------------------
// onEvent
// ---------------------------------------------------------------------------
bool SettingsPanel::onEvent(const InputEvent& event) {
    if (!m_visible || !m_backend) return false;

    // Route key events to KeyBindingsPanel FIRST when Controls tab is active.
    // This implements the capture-mode input interception (Priority 5 per
    // input-arbitration.md): KeyBindingsPanel consumes all keys during capture
    // EXCEPT Escape, which falls through to the Escape handler below so that
    // Settings closes in the same event frame.
    if (m_activeTab == 1 && m_keyBindings) {
        bool consumed = m_keyBindings->onEvent(event);
        if (consumed) return true;
        // If not consumed (including Escape from capture): fall through.
    }

    if (event.type == InputEvent::Type::KeyDown) {
        int key = event.keyCode;

        // Escape: cancel and return to pause menu
        if (key == 27) {
            hide();
            return true;
        }

        // Left/Right arrows: cycle tabs
        if (key == 37) { // Left arrow
            int newTab = (m_activeTab > 0) ? m_activeTab - 1 : 3;
            switchTab(newTab);
            return true;
        }
        if (key == 39) { // Right arrow
            int newTab = (m_activeTab < 3) ? m_activeTab + 1 : 0;
            switchTab(newTab);
            return true;
        }

        // Tab / Shift+Tab: navigate within active tab
        if (key == 9) { // Tab
            int count = getInteractiveElementCount();
            if (count > 0) {
                if (event.shiftDown) {
                    m_focusedElement = (m_focusedElement > 0) ? m_focusedElement - 1 : count - 1;
                } else {
                    m_focusedElement = (m_focusedElement + 1) % count;
                }
            }
            return true;
        }
    }

    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        // Check if outside panel bounds
        if (mx < kSettingsX || mx > kSettingsX + kSettingsW ||
            my < kSettingsY || my > kSettingsY + kSettingsH) {
            hide();
            return false;
        }

        // Tab header clicks
        auto hitTest = [this](int mx, int my, UIElementHandle handle) {
            Rect r = m_backend->getElementRect(handle);
            return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
        };

        if (hitTest(mx, my, m_tabGraphics)) { switchTab(0); return true; }
        if (hitTest(mx, my, m_tabControls)) { switchTab(1); return true; }
        if (hitTest(mx, my, m_tabAudio))    { switchTab(2); return true; }
        if (hitTest(mx, my, m_tabGameplay)) { switchTab(3); return true; }

        // Bottom button clicks
        if (hitTest(mx, my, m_btnCancel)) {
            if (m_activeTab == 1 && m_keyBindings) {
                m_keyBindings->revertToSnapshot();
            }
            hide();
            return true;
        }

        if (hitTest(mx, my, m_btnApply)) {
            if (m_activeTab == 0 && m_clock) {
                // Graphics: start 10s countdown for auto-revert
                m_countdownActive = true;
                m_countdownStartTime = m_clock->nowSeconds();
                m_backend->setElementVisible(m_countdownLabel, true);
            } else if (m_activeTab == 1 && m_keyBindings) {
                // Controls: write keybindings only if no conflict
                if (!m_keyBindings->hasConflict()) {
                    if (m_keybindingsApplyFn) m_keybindingsApplyFn(m_keyBindings->currentBindings());
                    hide();
                }
                // If conflict exists: Apply is a no-op (button is effectively grayed).
            }
            // Audio: already applied immediately, so Apply is a no-op for audio
            return true;
        }

        if (hitTest(mx, my, m_btnRestoreDefaults)) {
            if (m_activeTab == 1) {
                if (m_modal) m_modal->showRestoreDefaultsConfirm();
                m_restoreDefaultsPending = true;
            }
            return true;
        }

        // Controls tab: WASD preset button
        if (m_activeTab == 1 && hitTest(mx, my, m_ctrlWasdPresetBtn)) {
            if (m_modal) m_modal->showWASDPreset();
            m_wasdPresetPending = true;
            return true;
        }

        // Audio slider clicks (simplified: click increments/decrements by 0.1)
        if (m_activeTab == 2) {
            if (hitTest(mx, my, m_audioMasterSlider)) {
                m_masterVolume = std::min(1.0f, m_masterVolume + 0.1f);
                if (m_audio) m_audio->setMasterVolume(m_masterVolume);
                return true;
            }
            if (hitTest(mx, my, m_audioMusicSlider)) {
                m_musicVolume = std::min(1.0f, m_musicVolume + 0.1f);
                if (m_audio) m_audio->setMusicVolume(m_musicVolume);
                return true;
            }
            if (hitTest(mx, my, m_audioSfxSlider)) {
                m_sfxVolume = std::min(1.0f, m_sfxVolume + 0.1f);
                if (m_audio) m_audio->setSFXVolume(m_sfxVolume);
                return true;
            }
        }

        // Gameplay demolish toggle
        if (m_activeTab == 3 && hitTest(mx, my, m_gameplayDemolishToggle)) {
            m_demolishConfirm = !m_demolishConfirm;
            return true;
        }

        // Consume all clicks inside panel
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// setKeybindingsApplyFn — wire the UIManager callback for Controls Apply.
// ---------------------------------------------------------------------------
void SettingsPanel::setKeybindingsApplyFn(std::function<void(const KeyBindings&)> fn) {
    m_keybindingsApplyFn = std::move(fn);
}

// ---------------------------------------------------------------------------
// setCurrentBindings — seed m_appliedBindings (called after load / apply).
// ---------------------------------------------------------------------------
void SettingsPanel::setCurrentBindings(const KeyBindings& b) {
    m_appliedBindings.copyMutableFrom(b);
}

// ---------------------------------------------------------------------------
// applyKeybindings — update applied bindings and call the callback.
// ---------------------------------------------------------------------------
void SettingsPanel::applyKeybindings(const KeyBindings& b) {
    m_appliedBindings.copyMutableFrom(b);
    if (m_keybindingsApplyFn) m_keybindingsApplyFn(b);
}

// ---------------------------------------------------------------------------
// getInteractiveElementCount — number of Tab-navigable controls in active tab
// ---------------------------------------------------------------------------
int SettingsPanel::getInteractiveElementCount() const {
    switch (m_activeTab) {
        case 0: return 4; // Graphics: resolution, vsync, msaa, colorblind toggle
        case 1: return 3; // Controls: edge scroll, sensitivity, WASD preset
        case 2: return 3; // Audio: master, music, SFX sliders
        case 3: return 2; // Gameplay: demolish confirm, disaster toggle (grayed)
        default: return 0;
    }
}
