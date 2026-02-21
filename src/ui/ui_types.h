#pragma once

// UI-layer type aliases and enums needed by UIManager, ModalDialog, and their tests.

// GameState — the current state of the game UI state machine.
// GameOver is Scenario-mode only — MUST never be set when GameMode==Sandbox.
// PostWinFreePlaying is post-V1 and is NOT in the V1 GameState enum
// (it will be added to architecture/ui-ux/ui-manager.md when Scenario mode is
// implemented post-V1).
// Note: GameOver covers BOTH bankruptcy (primary Scenario failure) AND scenario
// objective-timeout loss — UIManager must use an auxiliary flag or GameOverReason
// enum (to be defined in Phase 5) to select the correct modal content.
enum class GameState {
    MainMenu,
    Gameplay,
    Paused,
    GameOver   // Scenario-mode only — MUST never be set when GameMode==Sandbox
};

// GameMode — the current game mode.
enum class GameMode {
    Sandbox,
    Scenario
};
