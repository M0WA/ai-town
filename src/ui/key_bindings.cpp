#include "src/ui/key_bindings.h"
#include <irrlicht.h>  // irr::ILogger, irr::ELL_WARNING

void KeyBindings::load(const std::string& path, irr::ILogger* logger) {
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
            std::string warnMsg = std::string("[KeyBindings::load] Rejected reserved key \"") +
                                 value + "\" for action \"" + key + "\" — default retained.";
            if (logger) {
                logger->log(warnMsg.c_str(), irr::ELL_WARNING);
            } else {
                std::fprintf(stderr, "[KeyBindings WARNING] (no ILogger) %s\n", warnMsg.c_str());
            }
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
            std::string warnMsg = std::string("[KeyBindings::load] Unknown key \"") + key + "\" — ignored.";
            if (logger) {
                logger->log(warnMsg.c_str(), irr::ELL_WARNING);
            } else {
                std::fprintf(stderr, "[KeyBindings WARNING] (no ILogger) %s\n", warnMsg.c_str());
            }
        }
    }
}
