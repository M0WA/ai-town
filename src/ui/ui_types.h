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

// ActiveTool — the currently active placement/interaction tool in the HUD toolbar.
// None means no tool is active (camera-only mode, same as the Phase 8 default state).
// Used by UIManager::m_activeTool and by the world-interaction input block (Priority 7)
// in UIManager::onEvent() to route mouse events to the correct placement handler.
// (ref: architecture/ui-ux/hud-layout.md, architecture/ui-ux/hotkey-scheme.md,
//  architecture/ui-ux/input-arbitration.md — Priority 5 and Priority 7)
enum class ActiveTool {
    None,        // no tool active — camera drag/pan only
    Zone,        // place zone tile (R/C/I, three density tiers)
    Road,        // place road tile
    Utilities,   // place service building (Power Plant / Water Tower / Fire Station / Police Station)
    Demolish,    // demolish any tile
    Query        // inspect tile data via InspectorPanel
};
