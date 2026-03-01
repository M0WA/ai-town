#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend

// Forward declarations
class IAudioSystem;
class IClock;
class PauseMenuPanel;
struct InputEvent;

// SettingsPanel — 4-tab settings screen accessible from pause menu and main menu.
// Tabs: Graphics, Controls, Audio, Gameplay.
// Graphics tab has 10s countdown auto-revert on Apply.
// Audio tab wires sliders to IAudioSystem::setMasterVolume/setMusicVolume/setSFXVolume.
class SettingsPanel {
public:
    SettingsPanel(IUIBackend* backend, IAudioSystem* audio, IClock* clock);

    void show();
    void hide();
    void draw();
    void update();
    bool onEvent(const InputEvent& event);

    void setPauseMenu(PauseMenuPanel* pauseMenu);
    bool isVisible() const { return m_visible; }

private:
    IUIBackend*     m_backend{nullptr};
    IAudioSystem*   m_audio{nullptr};
    IClock*         m_clock{nullptr};
    PauseMenuPanel* m_pauseMenu{nullptr};
    bool            m_visible{false};

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
