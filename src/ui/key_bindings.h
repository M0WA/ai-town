#pragma once
#include <string>

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

    // Non-rebindable chords (presented as informational rows in the rebinding UI).
    // D-2 / UI-4: `const` removed — was the source of the deleted copy-assignment operator
    // that forced all copy sites to use copyMutableFrom().  Non-rebindability is now
    // enforced at runtime in load(): any attempt to bind "Ctrl+Z" or "Ctrl+S" via the
    // config file is silently rejected.
    std::string undo = "Ctrl+Z";  // Undo last destructive action (not rebindable in V1)
    std::string save = "Ctrl+S";  // Manual save / save-slot dialog (not rebindable in V1)

    // Load key bindings from a JSON config file at `path`.
    // Parses the flat string-to-string JSON object at startup.
    // If the file cannot be opened, returns immediately (caller handles absent file).
    // Reserved keys ("Q", "E"): silently ignored with a warning logged via logger or stderr.
    // Unrecognised action names: logged as unknown and skipped.
    // "Ctrl+Z" and "Ctrl+S" values are silently rejected if they appear in the file
    //   (runtime guard replacing the removed const fields — D-2 / UI-4).
    // Does not throw — all errors are handled as warnings + skip.
    // logger — optional irr::ILogger*; if nullptr, warnings fall back to stderr.
    // Implementation in key_bindings.cpp (keeps Irrlicht out of test include paths).
    void load(const std::string& path, irr::ILogger* logger = nullptr);

    // Returns true for keys that are reserved and cannot be assigned to any action.
    // Q and E are reserved for future camera controls.
    // Ctrl+Z and Ctrl+S are non-rebindable chords; load() rejects them at parse time.
    bool isReservedKey(const std::string& key) const {
        return key == "Q" || key == "E";
    }

    // Write the 11 rebindable fields to a flat JSON file at `path`.
    // D-3 / UI-5: body moved from header to key_bindings.cpp; uses std::ofstream
    // with a single ostringstream to replace 11 individual fprintf calls.
    // Does not write the `undo` or `save` fields (non-rebindable display-only values).
    // Named writeToFile to avoid collision with the `save` field.
    void writeToFile(const std::string& path) const;

    // D-2 / UI-4: copyMutableFrom() deleted.
    // All sites that previously called copyMutableFrom(src) should now use operator=(src)
    // directly — default copy-assignment works correctly now that `undo` and `save` are
    // non-const.  The only difference is that operator= also copies `undo` and `save`,
    // which is harmless since their values are always the canonical defaults.
};
