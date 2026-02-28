#pragma once
#include "src/ui/IUIBackend.h"  // UIElementHandle, IUIBackend

struct InputEvent;

// MainMenuPanel -- full-screen overlay with New Game, Load Game, Settings, Quit buttons.
// New Game flow: difficulty selector, mode selector, seed entry, loading screen.
class MainMenuPanel {
public:
    explicit MainMenuPanel(IUIBackend* backend);

    void show();
    void hide();
    void draw();
    bool onEvent(const InputEvent& event);

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
    int m_selectedDifficulty{1};   // 0=Easy, 1=Normal, 2=Hard

    // Helpers
    void showMainMenuScreen();
    void showNewGameScreen();
    void showLoadingScreen();
    void hideAllElements();
};
