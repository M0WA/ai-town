#pragma once
#include <string>

// Default hotkey values per architecture/ui-ux/hotkey-scheme.md.
// Camera pan defaults to Arrow keys (not WASD); the WASD preset is a player-applied
// rebind via Settings > Controls and must NOT be the out-of-box default.
// Q and E are reserved for future camera controls and MUST NOT be bound here.
// Config file path: ~/.config/aitown/keybindings.json (Linux),
//                   %APPDATA%\aitown\keybindings.json  (Windows)
// Key names use SDL2-style string identifiers (e.g. "Space", "KeyZ").
struct KeyBindings {
    // Camera pan (default: arrow keys)
    std::string camPanUp    = "ArrowUp";
    std::string camPanDown  = "ArrowDown";
    std::string camPanLeft  = "ArrowLeft";
    std::string camPanRight = "ArrowRight";

    // Tool hotkeys
    std::string toolZone      = "Z";    // Zone tool
    std::string toolRoad      = "R";    // Road tool
    std::string toolUtilities = "U";    // Utilities tool
    std::string toolDemolish  = "D";    // Demolish tool
    std::string toolInspector = "I";    // Inspector / Query tool

    // UI panel toggles
    std::string toggleTaxPanel  = "T";      // Toggle Tax Rate Panel
    std::string toggleNotifLog  = "B";      // Toggle Notification Log
    std::string togglePause     = "Space";  // Pause / unpause
    std::string speedIncrease   = "+";      // Increase simulation speed (also "=")
    std::string speedDecrease   = "-";      // Decrease simulation speed
    std::string openPauseMenu   = "Escape"; // Open pause menu (gameplay); Back/Cancel elsewhere

    // Non-rebindable chords (presented as informational rows in the rebinding UI)
    // Ctrl+Z and Ctrl+S are processed by IEventReceiver before other handlers.
    // They are stored here for UI display only — rebinding infrastructure is post-V1.
    const std::string undo = "Ctrl+Z";  // Undo last destructive action (not rebindable in V1)
    const std::string save = "Ctrl+S";  // Manual save / save-slot dialog (not rebindable in V1)

    // Load key bindings from JSON config file.
    // Stub — Phase 8 implements the full JSON parse.
    // On load, any key whose value is "Q" or "E" is silently ignored and the default
    // is retained, with a warning logged (Q/E are reserved; see hotkey-scheme.md).
    void load(const std::string& /*path*/) {}

    // Returns true for keys that are reserved and cannot be assigned to any action.
    // Q and E are reserved for future camera controls.
    // Ctrl+Z and Ctrl+S are non-rebindable chords (separate from this guard).
    bool isReservedKey(const std::string& key) const {
        return key == "Q" || key == "E";
    }
};
