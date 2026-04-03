// src/platform/PlatformUtils.h — platform-specific utility helpers.
#pragma once
#include <string>

// resolveAssetsDir()
//
// Returns the canonical assets directory path for the running process.
//
// Windows: returns the directory containing the executable + "/assets",
//          resolved via GetModuleFileNameW(). Works regardless of CWD.
//
// Linux:   returns AITOWN_ASSETS_DIR (set at compile time):
//          - dev builds:        <repo>/assets
//          - DEB installs:      /usr/share/aitown/assets
//
// Called once at startup; result stored in g_assetsDir.
std::string resolveAssetsDir();

// g_assetsDir — the resolved assets directory, set once at startup by main()
// and read-only thereafter. All subsystems use this instead of AITOWN_ASSETS_DIR.
extern std::string g_assetsDir;
