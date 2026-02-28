// src/ui/MainMenuPanel.cpp
//
// MainMenuPanel -- full-screen overlay with New Game / Load Game / Settings / Quit.
// New Game flow: mode, difficulty, seed, loading screen.
// All coordinates in virtual 1920x1080 space.

#include "src/ui/main_menu_panel.h"
#include "src/platform/input_event.h"

#include <string>

// Layout constants (centered in 1920x1080)
static constexpr int kMenuW = 400;
static constexpr int kMenuH = 600;
static constexpr int kMenuX = (1920 - kMenuW) / 2;
static constexpr int kMenuY = (1080 - kMenuH) / 2;
static constexpr int kBtnW  = 360;
static constexpr int kBtnH  = 48;
static constexpr int kBtnX  = kMenuX + 20;

// ---------------------------------------------------------------------------
// Constructor -- creates all elements, shows main menu screen
// ---------------------------------------------------------------------------
MainMenuPanel::MainMenuPanel(IUIBackend* backend)
    : m_backend(backend)
    , m_visible(false)
    , m_screen(Screen::MainMenu)
    , m_focusedButton(0)
    , m_selectedDifficulty(1) // Normal
{
    if (!m_backend) return;

    // --- Main Menu screen ---
    m_titleLabel = m_backend->addStaticText("AI Town", kMenuX + 20, kMenuY + 16, kMenuW - 40, 48);
    int y = kMenuY + 80;
    m_btnNewGame  = m_backend->addButton("New Game",  kBtnX, y, kBtnW, kBtnH); y += kBtnH + 12;
    m_btnLoadGame = m_backend->addButton("Load Game", kBtnX, y, kBtnW, kBtnH); y += kBtnH + 12;
    m_btnSettings = m_backend->addButton("Settings",  kBtnX, y, kBtnW, kBtnH); y += kBtnH + 12;
    m_btnQuit     = m_backend->addButton("Quit",      kBtnX, y, kBtnW, kBtnH);

    // Load Game grayed if no saves (default state in V1)
    m_backend->setElementEnabled(m_btnLoadGame, false);

    // --- New Game screen ---
    m_ngTitle       = m_backend->addStaticText("New Game", kMenuX + 20, kMenuY + 16, kMenuW - 40, 36);
    y = kMenuY + 60;
    m_ngModeLabel   = m_backend->addStaticText("Mode:", kBtnX, y, 80, 32);
    m_ngBtnSandbox  = m_backend->addButton("[Sandbox]",          kBtnX + 90,  y, 120, 32);
    m_ngBtnScenario = m_backend->addButton("Scenario",           kBtnX + 220, y, 120, 32);
    m_backend->setElementEnabled(m_ngBtnScenario, false); // Grayed in V1
    y += 44;

    m_ngDiffLabel = m_backend->addStaticText("Difficulty:", kBtnX, y, 120, 32);
    m_ngBtnEasy   = m_backend->addButton("Easy ($1M)",     kBtnX + 130, y, 100, 32);
    m_ngBtnNormal = m_backend->addButton("[Normal ($500K)]",kBtnX + 240, y, 120, 32);
    m_ngBtnHard   = m_backend->addButton("Hard ($200K)",   kBtnX, y + 36, 120, 32);
    y += 80;

    m_ngSeedLabel    = m_backend->addStaticText("Map Seed:", kBtnX, y, 120, 32);
    m_ngSeedInput    = m_backend->addStaticText("",          kBtnX + 130, y, 160, 32);
    m_ngBtnRandomize = m_backend->addButton("Randomize",    kBtnX + 300, y, 100, 32);
    y += 44;

    m_ngBtnStartCity = m_backend->addButton("Start City", kBtnX, y, kBtnW, kBtnH); y += kBtnH + 12;
    m_ngBtnBack      = m_backend->addButton("Back",       kBtnX, y, 120, 36);
    m_ngErrorLabel   = m_backend->addStaticText("",        kBtnX, y + 40, kBtnW, 24);

    // --- Loading screen ---
    m_loadingLabel     = m_backend->addStaticText("Generating terrain...",   kMenuX + 20, 480, kMenuW - 40, 36);
    m_loadingProgress  = m_backend->addStaticText("0%",                      kMenuX + 20, 520, kMenuW - 40, 36);
    m_loadingCancelBtn = m_backend->addButton("Cancel",                      kMenuX + 140, 570, 120, 36);

    // Show main menu immediately on construction (spec requirement)
    show();
}

// ---------------------------------------------------------------------------
// hideAllElements
// ---------------------------------------------------------------------------
void MainMenuPanel::hideAllElements() {
    if (!m_backend) return;

    // Main menu
    m_backend->setElementVisible(m_titleLabel, false);
    m_backend->setElementVisible(m_btnNewGame, false);
    m_backend->setElementVisible(m_btnLoadGame, false);
    m_backend->setElementVisible(m_btnSettings, false);
    m_backend->setElementVisible(m_btnQuit, false);

    // New Game
    m_backend->setElementVisible(m_ngTitle, false);
    m_backend->setElementVisible(m_ngModeLabel, false);
    m_backend->setElementVisible(m_ngBtnSandbox, false);
    m_backend->setElementVisible(m_ngBtnScenario, false);
    m_backend->setElementVisible(m_ngDiffLabel, false);
    m_backend->setElementVisible(m_ngBtnEasy, false);
    m_backend->setElementVisible(m_ngBtnNormal, false);
    m_backend->setElementVisible(m_ngBtnHard, false);
    m_backend->setElementVisible(m_ngSeedLabel, false);
    m_backend->setElementVisible(m_ngSeedInput, false);
    m_backend->setElementVisible(m_ngBtnRandomize, false);
    m_backend->setElementVisible(m_ngBtnStartCity, false);
    m_backend->setElementVisible(m_ngBtnBack, false);
    m_backend->setElementVisible(m_ngErrorLabel, false);

    // Loading
    m_backend->setElementVisible(m_loadingLabel, false);
    m_backend->setElementVisible(m_loadingProgress, false);
    m_backend->setElementVisible(m_loadingCancelBtn, false);
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void MainMenuPanel::show() {
    m_visible = true;
    m_screen = Screen::MainMenu;
    showMainMenuScreen();
}

void MainMenuPanel::hide() {
    m_visible = false;
    hideAllElements();
}

// ---------------------------------------------------------------------------
// Screen transitions
// ---------------------------------------------------------------------------
void MainMenuPanel::showMainMenuScreen() {
    hideAllElements();
    m_screen = Screen::MainMenu;
    m_focusedButton = 0; // Default focus on New Game

    m_backend->setElementVisible(m_titleLabel, true);
    m_backend->setElementVisible(m_btnNewGame, true);
    m_backend->setElementVisible(m_btnLoadGame, true);
    m_backend->setElementVisible(m_btnSettings, true);
    m_backend->setElementVisible(m_btnQuit, true);
}

void MainMenuPanel::showNewGameScreen() {
    hideAllElements();
    m_screen = Screen::NewGame;
    m_focusedButton = 0;

    m_backend->setElementVisible(m_ngTitle, true);
    m_backend->setElementVisible(m_ngModeLabel, true);
    m_backend->setElementVisible(m_ngBtnSandbox, true);
    m_backend->setElementVisible(m_ngBtnScenario, true);
    m_backend->setElementVisible(m_ngDiffLabel, true);
    m_backend->setElementVisible(m_ngBtnEasy, true);
    m_backend->setElementVisible(m_ngBtnNormal, true);
    m_backend->setElementVisible(m_ngBtnHard, true);
    m_backend->setElementVisible(m_ngSeedLabel, true);
    m_backend->setElementVisible(m_ngSeedInput, true);
    m_backend->setElementVisible(m_ngBtnRandomize, true);
    m_backend->setElementVisible(m_ngBtnStartCity, true);
    m_backend->setElementVisible(m_ngBtnBack, true);
}

void MainMenuPanel::showLoadingScreen() {
    hideAllElements();
    m_screen = Screen::Loading;

    m_backend->setElementVisible(m_loadingLabel, true);
    m_backend->setElementVisible(m_loadingProgress, true);
    m_backend->setElementVisible(m_loadingCancelBtn, true);
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void MainMenuPanel::draw() {
    if (!m_visible || !m_backend) return;

    // Update difficulty radio button labels
    if (m_screen == Screen::NewGame) {
        m_backend->setElementText(m_ngBtnEasy,   m_selectedDifficulty == 0 ? "[Easy ($1M)]"     : "Easy ($1M)");
        m_backend->setElementText(m_ngBtnNormal,  m_selectedDifficulty == 1 ? "[Normal ($500K)]" : "Normal ($500K)");
        m_backend->setElementText(m_ngBtnHard,    m_selectedDifficulty == 2 ? "[Hard ($200K)]"   : "Hard ($200K)");
    }
}

// ---------------------------------------------------------------------------
// onEvent
// ---------------------------------------------------------------------------
bool MainMenuPanel::onEvent(const InputEvent& event) {
    if (!m_visible || !m_backend) return false;

    if (event.type == InputEvent::Type::KeyDown) {
        int key = event.keyCode;

        // Escape on New Game = Back to Main Menu
        if (key == 27) {
            if (m_screen == Screen::NewGame) {
                showMainMenuScreen();
                return true;
            }
            if (m_screen == Screen::Loading) {
                showNewGameScreen();
                return true;
            }
            // Main menu: Escape is no-op (already at top level)
            return true;
        }

        // Up/Down arrow navigation on main menu
        if (m_screen == Screen::MainMenu) {
            if (key == 38) { // Up
                m_focusedButton = (m_focusedButton > 0) ? m_focusedButton - 1 : 3;
                return true;
            }
            if (key == 40) { // Down
                m_focusedButton = (m_focusedButton < 3) ? m_focusedButton + 1 : 0;
                return true;
            }
            if (key == 9) { // Tab
                m_focusedButton = (m_focusedButton + 1) % 4;
                return true;
            }
            if (key == 13) { // Enter
                switch (m_focusedButton) {
                    case 0: // New Game
                        showNewGameScreen();
                        return true;
                    case 1: // Load Game (grayed)
                        return true;
                    case 2: // Settings
                        // UIManager handles showing SettingsPanel from main menu
                        return true;
                    case 3: // Quit
                        // UIManager handles shutdown
                        return true;
                }
                return true;
            }
        }
    }

    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        auto hitTest = [this](int mx, int my, UIElementHandle handle) {
            Rect r = m_backend->getElementRect(handle);
            return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
        };

        if (m_screen == Screen::MainMenu) {
            if (hitTest(mx, my, m_btnNewGame)) {
                showNewGameScreen();
                return true;
            }
            if (hitTest(mx, my, m_btnSettings)) {
                // UIManager handles showing SettingsPanel from main menu
                return true;
            }
            if (hitTest(mx, my, m_btnQuit)) {
                // UIManager handles shutdown
                return true;
            }
        }

        if (m_screen == Screen::NewGame) {
            // Difficulty buttons
            if (hitTest(mx, my, m_ngBtnEasy))   { m_selectedDifficulty = 0; return true; }
            if (hitTest(mx, my, m_ngBtnNormal))  { m_selectedDifficulty = 1; return true; }
            if (hitTest(mx, my, m_ngBtnHard))    { m_selectedDifficulty = 2; return true; }

            if (hitTest(mx, my, m_ngBtnBack)) {
                showMainMenuScreen();
                return true;
            }
            if (hitTest(mx, my, m_ngBtnStartCity)) {
                showLoadingScreen();
                return true;
            }
            if (hitTest(mx, my, m_ngBtnRandomize)) {
                m_backend->setElementText(m_ngSeedInput, "");
                return true;
            }
        }

        if (m_screen == Screen::Loading) {
            if (hitTest(mx, my, m_loadingCancelBtn)) {
                showNewGameScreen();
                return true;
            }
        }
    }

    // Consume all input while main menu is active
    return true;
}
