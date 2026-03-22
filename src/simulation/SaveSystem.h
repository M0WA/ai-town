#pragma once
// SaveSystem.h — auto-save and manual-slot save/load for AI Town V1.
//
// Manages three manual save slots (slot1.json, slot2.json, slot3.json) and one
// auto-save slot (autosave.json) stored under the platform-specific save directory:
//   Linux:   ~/.config/aitown/saves/
//   Windows: %APPDATA%\aitown\saves\
//
// Auto-save fires every 120 real seconds OR every 5 budget ticks, whichever comes
// first.  Immediate auto-saves are also triggered by:
//   onForcedLoanDialogActive() — saves just before the forced-loan modal opens
//   onPauseMenuOpened()        — saves when the pause menu is opened
//
// The caller (main loop / UIManager) must call update(realDeltaSeconds) once per
// frame and onBudgetTick() once per budget tick.
//
// Phase-11 scope: does NOT create saves/scenarios/ subdirectory (post-V1).

#include "IClock.h"
#include "src/interfaces/ISaveSystem.h"

#include <string>

// Forward-declare to avoid pulling CitySimulation.h into the interface.
class CitySimulation;

// SaveResult and LoadResult are defined in src/interfaces/ISaveSystem.h.
// They are available here via the #include above.

// ---------------------------------------------------------------------------
// SaveSystem
// ---------------------------------------------------------------------------
class SaveSystem : public ISaveSystem {
public:
    // clock: injected IClock* for real-time auto-save timer (must not be null).
    explicit SaveSystem(IClock* clock);
    ~SaveSystem() override = default;

    // Non-copyable, non-movable (owns timer state and file paths).
    SaveSystem(const SaveSystem&)            = delete;
    SaveSystem& operator=(const SaveSystem&) = delete;

    // setSimulation — register the CitySimulation instance to save/load.
    // Must be called before any save/load methods.  The pointer is non-owning.
    void setSimulation(CitySimulation* sim);

    // ---- Auto-save triggers ----

    // update — advance the real-time auto-save timer.  Call once per frame.
    // realDeltaSeconds: time since last frame (real time, not sim time).
    // Fires an auto-save when the 120 s timer expires (unless suspended).
    void update(float realDeltaSeconds) override;

    // onBudgetTick — increment the budget-tick counter for the 5-tick auto-save gate.
    // Call once per budget tick from the main loop or CitySimulation::tick().
    // Fires an auto-save when the 5-tick counter reaches the threshold (unless suspended).
    void onBudgetTick() override;

    // onForcedLoanDialogActive — trigger an immediate auto-save.
    // Called by UIManager just before opening the forced-loan modal dialog.
    void onForcedLoanDialogActive() override;

    // onPauseMenuOpened — trigger an immediate auto-save.
    // Called by UIManager when the pause/settings menu is opened.
    void onPauseMenuOpened() override;

    // suspendAutoSave — pause or resume the auto-save timer.
    // Pass true to suspend (e.g., while a blocking modal is open or game is fully paused),
    // false to resume.  Immediate-trigger methods bypass this flag.
    void suspendAutoSave(bool suspended) override;

    // ---- Manual saves and loads ----

    // autoSave — write current simulation state to the auto-save slot.
    // Returns SaveResult{ok=false} if setSimulation() has not been called.
    SaveResult autoSave() override;

    // saveToSlot — write current simulation state to the numbered slot (1–3).
    // The ISaveSystem interface requires saveToSlot(int slot); this implementation
    // also accepts an optional name label (not used in V1 filename, reserved for
    // future UI). The override satisfies the interface's saveToSlot(int) signature.
    SaveResult saveToSlot(int slot) override;

    // saveToSlot with optional name label — non-virtual convenience overload.
    // Delegates to saveToSlot(slot) so that production callers that previously
    // passed a name continue to compile.
    SaveResult saveToSlot(int slot, const std::string& name);

    // loadFromSlot — read the save file for the numbered slot (1–3).
    // Returns LoadResult with the raw JSON; caller applies it via
    //   sim->deserializeFromJson(result.jsonData, err).
    LoadResult loadFromSlot(int slot) const;

    // loadAutoSave — read the auto-save slot.
    LoadResult loadAutoSave() const;

    // loadMostRecentSave — return the save file (across all slots including autosave)
    // with the most recent filesystem modification time.
    // Returns LoadResult{ok=false, error="no save data"} if no save files exist.
    LoadResult loadMostRecentSave() const override;

    // ---- Queries ----

    // hasSaveData — returns true if at least one save file exists (any slot).
    bool hasSaveData() const override;

    // isSaveCorrupted — returns true if save files exist but the most recent one
    // cannot be loaded (schema mismatch, malformed JSON, truncated file).
    // Returns false when hasSaveData() is false (absent file ≠ corrupted).
    bool isSaveCorrupted() const;

    // getSaveFileState — returns a three-state summary for the Load Game button.
    // NoSaves    — no save files on disk.
    // AllCorrupt — files exist but most recent is unreadable.
    // Valid      — at least one valid, loadable save.
    SaveFileState getSaveFileState() const override;

    // getSaveDirectoryPath — return the platform-specific save directory path.
    // The directory is created on first access (by autoSave/saveToSlot).
    std::string getSaveDirectoryPath() const override;

private:
    // ---- Members ----
    IClock*         m_clock{nullptr};
    CitySimulation* m_sim{nullptr};

    // Auto-save state
    float   m_realSecondsAccumulated{0.0f};   // real-time accumulator
    int     m_budgetTicksAccumulated{0};       // budget-tick counter since last auto-save
    bool    m_suspended{false};               // true while auto-save is paused

    static constexpr float kAutoSaveIntervalSeconds = 120.0f;
    static constexpr int   kAutoSaveBudgetTicks     = 5;

    // ---- Helpers ----
    std::string slotFilePath(int slot) const;        // saves/slot{N}.json
    std::string autoSaveFilePath() const;             // saves/autosave.json
    std::string ensureSaveDirectory() const;          // create dirs, return path

    SaveResult writeJsonToFile(const std::string& filePath,
                               const std::string& jsonData) const;
    LoadResult readJsonFromFile(const std::string& filePath) const;
};
