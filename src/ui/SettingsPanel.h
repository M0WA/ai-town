#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend
#include "src/ui/key_bindings.h"        // KeyBindings
#include <functional>

// Forward declarations
class IAudioSystem;
class IClock;
class KeyBindingsPanel;
class ModalDialog;
class PauseMenuPanel;
struct InputEvent;

// SettingsPanel — 4-tab settings screen accessible from pause menu and main menu.
// Tabs: Graphics, Controls, Audio, Gameplay.
// Graphics tab has 10s countdown auto-revert on Apply.
// Audio tab wires sliders to IAudioSystem::setMasterVolume/setMusicVolume/setSFXVolume.
class SettingsPanel {
public:
    SettingsPanel(IUIBackend* backend, IAudioSystem* audio, IClock* clock,
                  ModalDialog* modal = nullptr);
    ~SettingsPanel();

    void show();
    void hide();
    void draw();
    void update();
    bool onEvent(const InputEvent& event);

    void setPauseMenu(PauseMenuPanel* pauseMenu);
    // Late-bind the ModalDialog pointer (called from UIManager after modal construction).
    void setModal(ModalDialog* modal);
    bool isVisible() const { return m_visible; }

    // Set the callback invoked when the player clicks Apply on the Controls tab.
    // Called from UIManager constructor to wire keybindings persistence.
    void setKeybindingsApplyFn(std::function<void(const KeyBindings&)> fn);

    // Set the currently-applied keybindings (used to seed Controls tab on open).
    // Called from UIManager after loadKeybindings() and after each successful apply.
    void setCurrentBindings(const KeyBindings& b);

    // Apply keybindings — public entry point called by UIManager for external updates.
    // Calls applyKeybindings internally; exposed for test observability.
    void applyKeybindings(const KeyBindings& b);

private:
    IUIBackend*       m_backend{nullptr};
    IAudioSystem*     m_audio{nullptr};
    IClock*           m_clock{nullptr};
    ModalDialog*      m_modal{nullptr};
    PauseMenuPanel*   m_pauseMenu{nullptr};
    bool              m_visible{false};

    // Glass City background elements (created once, repositioned on show())
    // scrimHandle: full-screen 50% opacity overlay beneath the panel.
    // bgHandle:    deep-navy panel background (rgba(13,27,42,0.88)).
    UIElementHandle m_scrimHandle{kInvalidUIElement};
    UIElementHandle m_bgHandle{kInvalidUIElement};

    // KeyBindingsPanel — owns the Controls tab keybinding table.
    KeyBindingsPanel* m_keyBindings{nullptr};

    // Active tab index (0=Graphics, 1=Controls, 2=Audio, 3=Gameplay)
    int m_activeTab{0};

    // Tab header buttons
    UIElementHandle m_tabGraphics{kInvalidUIElement};
    UIElementHandle m_tabControls{kInvalidUIElement};
    UIElementHandle m_tabAudio{kInvalidUIElement};
    UIElementHandle m_tabGameplay{kInvalidUIElement};

    // Bottom buttons (shared across all tabs)
    UIElementHandle m_btnApply{kInvalidUIElement};
    UIElementHandle m_btnCancel{kInvalidUIElement};
    UIElementHandle m_btnRestoreDefaults{kInvalidUIElement};

    // --- Graphics tab elements ---
    UIElementHandle m_gfxResolutionLabel{kInvalidUIElement};
    UIElementHandle m_gfxVsyncLabel{kInvalidUIElement};
    UIElementHandle m_gfxMsaaLabel{kInvalidUIElement};
    UIElementHandle m_gfxColorblindBtn{kInvalidUIElement};

    // --- Controls tab elements ---
    UIElementHandle m_ctrlEdgeScrollLabel{kInvalidUIElement};
    UIElementHandle m_ctrlSensitivityLabel{kInvalidUIElement};
    UIElementHandle m_ctrlWasdPresetBtn{kInvalidUIElement};

    // --- Audio tab elements ---
    UIElementHandle m_audioMasterLabel{kInvalidUIElement};
    UIElementHandle m_audioMasterSlider{kInvalidUIElement};
    UIElementHandle m_audioMusicLabel{kInvalidUIElement};
    UIElementHandle m_audioMusicSlider{kInvalidUIElement};
    UIElementHandle m_audioSfxLabel{kInvalidUIElement};
    UIElementHandle m_audioSfxSlider{kInvalidUIElement};

    // --- Gameplay tab elements ---
    UIElementHandle m_gameplayDiffLabel{kInvalidUIElement};
    UIElementHandle m_gameplayDemolishToggle{kInvalidUIElement};
    UIElementHandle m_gameplayDisasterToggle{kInvalidUIElement};

    // Panel background
    UIElementHandle m_panelBg{kInvalidUIElement};

    // Graphics auto-revert state
    bool   m_countdownActive{false};
    double m_countdownStartTime{0.0};
    UIElementHandle m_countdownLabel{kInvalidUIElement};

    // Audio slider values (cached for immediate-apply)
    float m_masterVolume{1.0f};
    float m_musicVolume{0.8f};
    float m_sfxVolume{0.8f};

    // Demolish confirm toggle state
    bool m_demolishConfirm{true};

    // --- Keybindings callback and state ---
    // Callback set by UIManager to persist keybindings on Controls Apply.
    std::function<void(const KeyBindings&)> m_keybindingsApplyFn;
    // The last applied (persisted) bindings — used to seed the Controls tab on open.
    KeyBindings m_appliedBindings{};

    // --- WASD preset modal polling state ---
    bool m_wasdPresetPending{false};
    // --- Restore Defaults modal polling state ---
    bool m_restoreDefaultsPending{false};

    void switchTab(int tabIndex);
    void hideAllTabElements();
    void showGraphicsTab();
    void showControlsTab();
    void showAudioTab();
    void showGameplayTab();

    // Intra-tab focus tracking
    int m_focusedElement{0};
    int getInteractiveElementCount() const;
};
