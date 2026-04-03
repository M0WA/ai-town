// src/platform/PlatformUtils.cpp

#include "PlatformUtils.h"

#include <cstdio>
#include <string>

std::string g_assetsDir;

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

std::string resolveAssetsDir()
{
    // GetModuleFileNameW returns the full path to the running .exe.
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        // Fallback: compiled-in relative path (CWD-dependent, same as before).
        std::fprintf(stderr, "[PlatformUtils] WARNING: GetModuleFileNameW failed "
                             "(error %lu) — falling back to compiled-in assets path.\n",
                     GetLastError());
        return AITOWN_ASSETS_DIR;
    }

    // Convert wide path to UTF-8.
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(len),
                                      nullptr, 0, nullptr, nullptr);
    std::string exePath(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(len),
                        exePath.data(), utf8Len, nullptr, nullptr);

    // Strip the executable filename — keep only the directory.
    const size_t lastSlash = exePath.find_last_of("\\/");
    std::string exeDir = (lastSlash != std::string::npos)
                         ? exePath.substr(0, lastSlash)
                         : ".";

    return exeDir + "\\assets";
}

#else  // Linux / macOS

std::string resolveAssetsDir()
{
    // Linux DEB installs: AITOWN_ASSETS_DIR is /usr/share/aitown/assets (absolute).
    // Dev builds: AITOWN_ASSETS_DIR is <repo>/assets (absolute, set by CMake).
    // Either way the compiled-in path is correct — no CWD dependency on Linux
    // because installed paths are absolute and dev builds run from the repo root.
    return AITOWN_ASSETS_DIR;
}

#endif
