#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend
#include <string>

struct InputEvent;

// Map size presets for the New Game screen (tile dimensions for TerrainSystem::generate()).
// Defined here per architecture/graphics-architecture/procedural-terrain.md §Map Size Presets.
// MainMenuPanel owns these as UI-facing aliases — not internal TerrainSystem constants.
enum class MapSize { kSmall = 128, kMedium = 512, kLarge = 1024 };

// MainMenuPanel -- full-screen overlay with New Game, Load Game, Settings, Quit buttons.
// New Game flow: mode selector, difficulty selector, map size selector, seed entry, loading screen.
class MainMenuPanel {
public:
    explicit MainMenuPanel(IUIBackend* backend);

    void show();
    void hide();
    void draw();
    bool onEvent(const InputEvent& event);

    // Loading screen: called when terrain generation crosses the abort checkpoint.
    // Disables Cancel button, changes label to "Finalizing..."
    void setAbortCheckpointPassed();

    // Polling API for UIManager: returns true (once) when "Start City" was clicked.
    bool consumeStartGameRequest();
    int getSelectedDifficulty() const { return m_selectedDifficulty; }
    MapSize getSelectedMapSize() const { return m_selectedMapSize; }

    // Returns true when on the main menu screen (not New Game or Loading).
    bool isOnMainMenuScreen() const { return m_screen == Screen::MainMenu; }

    // Returns true when on the Settings button (UIManager opens SettingsPanel).
    bool consumeSettingsRequest();

    // Returns true (once) when "Quit" was clicked or Enter-activated.
    bool consumeQuitRequest();

    // Returns true (once) when "Load Game" was clicked while the button is enabled.
    bool consumeLoadGameRequest();

    // Phase 11: Update Load Game button enabled state.
    // When available=false (default): button grayed with tooltip "No saves found."
    // When available=true: button enabled and interactive.
    void setSaveAvailable(bool available);

    // Phase 11: Show save status text beneath the Load Game button.
    // text="" hides the label. Used for "No saves found." and
    // "Save data corrupted — check [path]" messages.
    void setSaveStatusText(const std::string& text);

private:
    IUIBackend* m_backend{nullptr};
    bool m_visible{false};

    // Main screen state
    enum class Screen {
        MainMenu,
        NewGame,
        Loading
    };
    Screen m_screen{Screen::MainMenu};

    // --- Main Menu screen elements ---
    UIElementHandle m_titleLabel{kInvalidUIElement};
    UIElementHandle m_btnNewGame{kInvalidUIElement};
    UIElementHandle m_btnLoadGame{kInvalidUIElement};
    UIElementHandle m_loadStatusLabel{kInvalidUIElement};  // "No saves found." / "Corrupted…"
    UIElementHandle m_btnSettings{kInvalidUIElement};
    UIElementHandle m_btnQuit{kInvalidUIElement};

    // --- New Game screen elements ---
    UIElementHandle m_ngTitle{kInvalidUIElement};
    UIElementHandle m_ngModeLabel{kInvalidUIElement};
    UIElementHandle m_ngBtnSandbox{kInvalidUIElement};
    UIElementHandle m_ngBtnScenario{kInvalidUIElement};
    UIElementHandle m_ngDiffLabel{kInvalidUIElement};
    UIElementHandle m_ngBtnEasy{kInvalidUIElement};
    UIElementHandle m_ngBtnNormal{kInvalidUIElement};
    UIElementHandle m_ngBtnHard{kInvalidUIElement};
    UIElementHandle m_ngMapSizeLabel{kInvalidUIElement};
    UIElementHandle m_ngBtnSizeSmall{kInvalidUIElement};
    UIElementHandle m_ngBtnSizeMedium{kInvalidUIElement};
    UIElementHandle m_ngBtnSizeLarge{kInvalidUIElement};
    UIElementHandle m_ngSeedLabel{kInvalidUIElement};
    UIElementHandle m_ngSeedInput{kInvalidUIElement};
    UIElementHandle m_ngBtnRandomize{kInvalidUIElement};
    UIElementHandle m_ngBtnStartCity{kInvalidUIElement};
    UIElementHandle m_ngBtnBack{kInvalidUIElement};
    UIElementHandle m_ngErrorLabel{kInvalidUIElement};

    // --- Loading screen elements ---
    UIElementHandle m_loadingLabel{kInvalidUIElement};
    UIElementHandle m_loadingProgress{kInvalidUIElement};
    UIElementHandle m_loadingCancelBtn{kInvalidUIElement};

    // Focus tracking
    int m_focusedButton{0};
    int m_selectedDifficulty{1};           // 0=Easy, 1=Normal, 2=Hard
    MapSize m_selectedMapSize{MapSize::kMedium};  // default: Medium
    bool m_abortCheckpointPassed{false}; // Loading: past point of no return
    bool m_startGameRequested{false};    // "Start City" was clicked
    bool m_settingsRequested{false};     // "Settings" was clicked
    bool m_quitRequested{false};         // "Quit" was clicked
    bool m_loadGameRequested{false};     // "Load Game" was clicked and enabled

    // Helpers
    void showMainMenuScreen();
    void showNewGameScreen();
    void showLoadingScreen();
    void hideAllElements();
};
