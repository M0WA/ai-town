#pragma once
#include <string>
#include <cstdio>   // fopen/fclose/fread/fseek/ftell
#include <cstring>  // strlen

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
    // Reserved keys ("Q", "E"): silently ignored with a warning logged to stderr.
    // Unrecognised action names: logged as unknown and skipped.
    // The const fields `undo` and `save` are never modified.
    // Does not throw — all errors are handled as warnings + skip.
    void load(const std::string& path) {
        // --- 1. Open file ---
        FILE* f = fopen(path.c_str(), "r");
        if (!f) {
            return; // Absent file — caller already handled; no warning here.
        }

        // --- 2. Read entire file into a string ---
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string content;
        if (fileSize > 0) {
            content.resize(static_cast<std::string::size_type>(fileSize));
            fread(&content[0], 1, static_cast<std::size_t>(fileSize), f);
        }
        fclose(f);

        // --- 3. Hand-rolled flat JSON key-value parser ---
        // Expects a flat object: { "key": "value", ... }
        // Strategy: repeatedly find the next `"key"` : `"value"` pair.
        std::string::size_type pos = 0;
        while (pos < content.size()) {
            // Find the opening quote of the next key.
            std::string::size_type kStart = content.find('"', pos);
            if (kStart == std::string::npos) break;
            std::string::size_type kEnd = content.find('"', kStart + 1);
            if (kEnd == std::string::npos) break;
            std::string key = content.substr(kStart + 1, kEnd - kStart - 1);

            // Skip past the key's closing quote and find the colon.
            std::string::size_type colon = content.find(':', kEnd + 1);
            if (colon == std::string::npos) break;

            // Find the opening quote of the value.
            std::string::size_type vStart = content.find('"', colon + 1);
            if (vStart == std::string::npos) break;
            std::string::size_type vEnd = content.find('"', vStart + 1);
            if (vEnd == std::string::npos) break;
            std::string value = content.substr(vStart + 1, vEnd - vStart - 1);

            // Advance position past this pair for next iteration.
            pos = vEnd + 1;

            // --- 4. Reserved-key guard ---
            if (isReservedKey(value)) {
                fprintf(stderr,
                    "[KeyBindings::load] Rejected reserved key \"%s\" for action \"%s\" — default retained.\n",
                    value.c_str(), key.c_str());
                continue;
            }

            // --- 5. Map key name to struct field and assign ---
            if      (key == "camPanUp")        { camPanUp        = value; }
            else if (key == "camPanDown")       { camPanDown       = value; }
            else if (key == "camPanLeft")       { camPanLeft       = value; }
            else if (key == "camPanRight")      { camPanRight      = value; }
            else if (key == "toolZone")         { toolZone         = value; }
            else if (key == "toolRoad")         { toolRoad         = value; }
            else if (key == "toolUtilities")    { toolUtilities    = value; }
            else if (key == "toolDemolish")     { toolDemolish     = value; }
            else if (key == "toolInspector")    { toolInspector    = value; }
            else if (key == "toggleTaxPanel")   { toggleTaxPanel   = value; }
            else if (key == "toggleNotifLog")   { toggleNotifLog   = value; }
            else if (key == "togglePause")      { togglePause      = value; }
            else if (key == "speedIncrease")    { speedIncrease    = value; }
            else if (key == "speedDecrease")    { speedDecrease    = value; }
            else if (key == "openPauseMenu")    { openPauseMenu    = value; }
            // Non-rebindable chords ("undo", "save") are intentionally absent here —
            // the const fields must not be modified by load().
            else {
                fprintf(stderr,
                    "[KeyBindings::load] Unknown key \"%s\" — ignored.\n",
                    key.c_str());
            }
        }
    }

    // Returns true for keys that are reserved and cannot be assigned to any action.
    // Q and E are reserved for future camera controls.
    // Ctrl+Z and Ctrl+S are non-rebindable chords (separate from this guard).
    bool isReservedKey(const std::string& key) const {
        return key == "Q" || key == "E";
    }
};
