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
// Called once at startup; result stored via setAssetsDir().
std::string resolveAssetsDir();

// setAssetsDir / getAssetsDir — assets directory accessor.
// setAssetsDir() must be called exactly once at startup (in main()) before any
// subsystem threads are started. getAssetsDir() is read-only after that point.
void setAssetsDir(std::string dir);
const std::string& getAssetsDir();
