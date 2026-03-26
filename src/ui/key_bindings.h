#pragma once
#include <string>
#include <cstdio>   // fopen/fclose/fread/fseek/ftell/fprintf

// Forward declaration — the load() method accepts an optional irr::ILogger* pointer
// but the full Irrlicht include is kept out of this header (testability rule: headers
// included by test targets must not pull in Irrlicht).  The implementation in
// key_bindings.cpp includes <irrlicht.h> for the full ILogger type.
namespace irr { class ILogger; }

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

    // Load key bindings from a JSON config file at `path`.
    // Parses the flat string-to-string JSON object at startup.
    // If the file cannot be opened, returns immediately (caller handles absent file).
    // Reserved keys ("Q", "E"): silently ignored with a warning logged via logger or stderr.
    // Unrecognised action names: logged as unknown and skipped.
    // The const fields `undo` and `save` are never modified.
    // Does not throw — all errors are handled as warnings + skip.
    // logger — optional irr::ILogger*; if nullptr, warnings fall back to stderr.
    // Implementation in key_bindings.cpp (keeps Irrlicht out of test include paths).
    void load(const std::string& path, irr::ILogger* logger = nullptr);

    // Returns true for keys that are reserved and cannot be assigned to any action.
    // Q and E are reserved for future camera controls.
    // Ctrl+Z and Ctrl+S are non-rebindable chords (separate from this guard).
    bool isReservedKey(const std::string& key) const {
        return key == "Q" || key == "E";
    }

    // Copy only the mutable (rebindable) fields from `src` into this struct.
    // Required because the const fields `undo` and `save` make the copy-assignment
    // operator implicitly deleted.  All sites that need to "assign" a KeyBindings
    // must call this helper instead of operator=.
    void copyMutableFrom(const KeyBindings& src) {
        camPanUp       = src.camPanUp;
        camPanDown     = src.camPanDown;
        camPanLeft     = src.camPanLeft;
        camPanRight    = src.camPanRight;
        toolZone       = src.toolZone;
        toolRoad       = src.toolRoad;
        toolUtilities  = src.toolUtilities;
        toolDemolish   = src.toolDemolish;
        toolInspector  = src.toolInspector;
        toggleTaxPanel = src.toggleTaxPanel;
        toggleNotifLog = src.toggleNotifLog;
        togglePause    = src.togglePause;
        speedIncrease  = src.speedIncrease;
        speedDecrease  = src.speedDecrease;
        openPauseMenu  = src.openPauseMenu;
    }

    // Write the 11 rebindable fields to a flat JSON file at `path`.
    // Uses fopen/fprintf — no external JSON library.
    // If `path` is empty or the file cannot be opened, silently returns.
    // Does not write the const fields `undo` and `save`.
    // Named writeToFile to avoid collision with the const field `save`.
    void writeToFile(const std::string& path) const {
        if (path.empty()) return;
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return;
        fprintf(f, "{\n");
        fprintf(f, "  \"camPanUp\": \"%s\",\n",       camPanUp.c_str());
        fprintf(f, "  \"camPanDown\": \"%s\",\n",     camPanDown.c_str());
        fprintf(f, "  \"camPanLeft\": \"%s\",\n",     camPanLeft.c_str());
        fprintf(f, "  \"camPanRight\": \"%s\",\n",    camPanRight.c_str());
        fprintf(f, "  \"toolZone\": \"%s\",\n",       toolZone.c_str());
        fprintf(f, "  \"toolRoad\": \"%s\",\n",       toolRoad.c_str());
        fprintf(f, "  \"toolUtilities\": \"%s\",\n",  toolUtilities.c_str());
        fprintf(f, "  \"toolDemolish\": \"%s\",\n",   toolDemolish.c_str());
        fprintf(f, "  \"toolInspector\": \"%s\",\n",  toolInspector.c_str());
        fprintf(f, "  \"toggleTaxPanel\": \"%s\",\n", toggleTaxPanel.c_str());
        fprintf(f, "  \"toggleNotifLog\": \"%s\"\n",  toggleNotifLog.c_str());
        fprintf(f, "}\n");
        fclose(f);
    }
};
