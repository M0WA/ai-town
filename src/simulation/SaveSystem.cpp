// SaveSystem.cpp — Save/load system for AI Town V1.
//
// Auto-save: fires every 120 real seconds OR every 5 budget ticks (whichever first).
// Manual slots: slot1.json, slot2.json, slot3.json under the platform save directory.
// Auto-save slot: autosave.json.
//
// File write safety: write to a temp file first, then rename to the target path
// (atomic on most POSIX filesystems; best-effort on Windows via MoveFileExW fallback).
//
// Phase-11 scope: sandbox-only; does NOT create scenarios/ subdirectory.

#include "SaveSystem.h"
#include "CitySimulation.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#  include <cstdlib>   // getenv
#else
#  include <cstdlib>   // getenv
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SaveSystem::SaveSystem(IClock* clock)
    : m_clock(clock)
{
    assert(clock && "SaveSystem requires a non-null IClock*");
}

void SaveSystem::setSimulation(CitySimulation* sim) {
    m_sim = sim;
}

// ---------------------------------------------------------------------------
// Auto-save timer
// ---------------------------------------------------------------------------

void SaveSystem::update(float realDeltaSeconds) {
    if (m_suspended || !m_sim) return;

    m_realSecondsAccumulated += realDeltaSeconds;
    if (m_realSecondsAccumulated >= kAutoSaveIntervalSeconds) {
        m_realSecondsAccumulated = 0.0f;
        m_budgetTicksAccumulated = 0;
        autoSave();
    }
}

void SaveSystem::onBudgetTick() {
    if (m_suspended || !m_sim) return;

    ++m_budgetTicksAccumulated;
    if (m_budgetTicksAccumulated >= kAutoSaveBudgetTicks) {
        m_budgetTicksAccumulated = 0;
        m_realSecondsAccumulated = 0.0f;
        autoSave();
    }
}

void SaveSystem::onForcedLoanDialogActive() {
    if (!m_sim) return;
    // Bypass suspension — this is an important safety-save before a destructive dialog.
    autoSave();
}

void SaveSystem::onPauseMenuOpened() {
    if (!m_sim) return;
    // Bypass suspension — pause-menu save is expected even when sim is paused.
    autoSave();
}

void SaveSystem::suspendAutoSave(bool suspended) {
    m_suspended = suspended;
}

// ---------------------------------------------------------------------------
// Save operations
// ---------------------------------------------------------------------------

SaveResult SaveSystem::autoSave() {
    if (!m_sim) {
        return SaveResult{false, "SaveSystem: no simulation registered"};
    }
    std::string jsonData = m_sim->serializeToJson();
    return writeJsonToFile(autoSaveFilePath(), jsonData);
}

SaveResult SaveSystem::saveToSlot(int slot, const std::string& /*name*/) {
    if (slot < 1 || slot > 3) {
        return SaveResult{false, "SaveSystem: slot must be 1–3, got " + std::to_string(slot)};
    }
    if (!m_sim) {
        return SaveResult{false, "SaveSystem: no simulation registered"};
    }
    std::string jsonData = m_sim->serializeToJson();
    return writeJsonToFile(slotFilePath(slot), jsonData);
}

// ---------------------------------------------------------------------------
// Load operations
// ---------------------------------------------------------------------------

LoadResult SaveSystem::loadFromSlot(int slot) const {
    if (slot < 1 || slot > 3) {
        return LoadResult{false, "", "SaveSystem: slot must be 1–3, got " + std::to_string(slot)};
    }
    return readJsonFromFile(slotFilePath(slot));
}

LoadResult SaveSystem::loadAutoSave() const {
    return readJsonFromFile(autoSaveFilePath());
}

LoadResult SaveSystem::loadMostRecentSave() const {
    // Collect all save file paths (auto-save + 3 slots)
    std::vector<std::string> candidates = {
        autoSaveFilePath(),
        slotFilePath(1),
        slotFilePath(2),
        slotFilePath(3),
    };

    std::string bestPath;
    fs::file_time_type bestTime{};

    for (const auto& path : candidates) {
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) continue;
        auto mtime = fs::last_write_time(path, ec);
        if (ec) continue;
        if (bestPath.empty() || mtime > bestTime) {
            bestPath = path;
            bestTime = mtime;
        }
    }

    if (bestPath.empty()) {
        return LoadResult{false, "", "no save data found"};
    }
    return readJsonFromFile(bestPath);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool SaveSystem::hasSaveData() const {
    std::vector<std::string> paths = {
        autoSaveFilePath(),
        slotFilePath(1),
        slotFilePath(2),
        slotFilePath(3),
    };
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::exists(p, ec) && !ec) return true;
    }
    return false;
}

std::string SaveSystem::getSaveDirectoryPath() const {
    return ensureSaveDirectory();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string SaveSystem::ensureSaveDirectory() const {
    std::string base;

#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata && appdata[0] != '\0') {
        base = std::string(appdata) + "\\aitown\\saves";
    } else {
        // Fallback: use current directory
        base = "aitown_saves";
    }
#else
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        base = std::string(home) + "/.config/aitown/saves";
    } else {
        base = "aitown_saves";
    }
#endif

    // Create the directory tree if it doesn't exist.
    // Errors (e.g. permission denied) are silently tolerated here; the
    // subsequent file write will produce a meaningful error if it fails.
    std::error_code ec;
    fs::create_directories(base, ec);

    return base;
}

std::string SaveSystem::slotFilePath(int slot) const {
    std::string dir = ensureSaveDirectory();
#if defined(_WIN32)
    return dir + "\\slot" + std::to_string(slot) + ".json";
#else
    return dir + "/slot" + std::to_string(slot) + ".json";
#endif
}

std::string SaveSystem::autoSaveFilePath() const {
    std::string dir = ensureSaveDirectory();
#if defined(_WIN32)
    return dir + "\\autosave.json";
#else
    return dir + "/autosave.json";
#endif
}

SaveResult SaveSystem::writeJsonToFile(const std::string& filePath,
                                        const std::string& jsonData) const {
    // Atomic write: write to a temp file, then rename.
    // The temp file lives in the same directory so the rename is same-filesystem.
    std::string tempPath = filePath + ".tmp";

    {
        std::ofstream ofs(tempPath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return SaveResult{false, "SaveSystem: cannot open temp file for writing: " + tempPath};
        }
        ofs << jsonData;
        if (!ofs) {
            return SaveResult{false, "SaveSystem: write error to temp file: " + tempPath};
        }
    }  // ofs closed here — flush to OS buffers before rename

    std::error_code ec;
    fs::rename(tempPath, filePath, ec);
    if (ec) {
        // rename failed — try to clean up the temp file
        std::error_code cleanupEc;
        fs::remove(tempPath, cleanupEc);
        return SaveResult{false, "SaveSystem: rename failed: " + ec.message()};
    }

    return SaveResult{true, ""};
}

LoadResult SaveSystem::readJsonFromFile(const std::string& filePath) const {
    std::error_code ec;
    if (!fs::exists(filePath, ec) || ec) {
        return LoadResult{false, "", "SaveSystem: save file not found: " + filePath};
    }

    std::ifstream ifs(filePath, std::ios::in);
    if (!ifs.is_open()) {
        return LoadResult{false, "", "SaveSystem: cannot open save file: " + filePath};
    }

    std::string content(
        (std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>()
    );
    if (!ifs && !ifs.eof()) {
        return LoadResult{false, "", "SaveSystem: read error from: " + filePath};
    }

    return LoadResult{true, std::move(content), ""};
}
