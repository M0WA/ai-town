#pragma once
#include <string>

// ---------------------------------------------------------------------------
// SaveResult -- result of a save operation.
// Defined here (rather than in SaveSystem.h) so that ISaveSystem consumers
// and MockSaveSystem do not need to include the concrete SaveSystem header.
// ---------------------------------------------------------------------------
struct SaveResult {
    bool        ok{false};   // true if the save succeeded
    std::string error;       // non-empty on failure
};

// ---------------------------------------------------------------------------
// LoadResult -- result of a load operation.
// ---------------------------------------------------------------------------
struct LoadResult {
    bool        ok{false};     // true if the load succeeded
    std::string jsonData;      // the raw JSON string read from disk (empty on failure)
    std::string error;         // non-empty on failure
};

// ---------------------------------------------------------------------------
// SaveFileState -- three-state result for the Main Menu Load Game button.
// Combines hasSaveData() and isSaveCorrupted() into a single tri-state query.
// ---------------------------------------------------------------------------
enum class SaveFileState {
    NoSaves,    // No save files exist on disk.
    AllCorrupt, // Save files exist but the most recent cannot be loaded.
    Valid       // At least one valid, loadable save exists.
};

// ---------------------------------------------------------------------------
// ISaveSystem -- pure-virtual interface for save/load operations.
// Implemented by SaveSystem (production) and MockSaveSystem (tests).
// UIManager holds an ISaveSystem* (not SaveSystem*) for testability.
// ---------------------------------------------------------------------------
class ISaveSystem {
public:
    virtual ~ISaveSystem() = default;

    // Write current simulation state to the numbered manual save slot (1-3).
    virtual SaveResult saveToSlot(int slot) = 0;

    // Write current simulation state to the auto-save slot.
    virtual SaveResult autoSave() = 0;

    // Advance the real-time auto-save timer.  Call once per frame.
    // realDeltaSeconds: elapsed real time since the last frame.
    // Fires an auto-save when the 120 s timer expires (unless suspended).
    virtual void update(float realDeltaSeconds) = 0;

    // Increment the budget-tick counter for the 5-tick auto-save gate.
    // Call once per budget tick.
    // Fires an auto-save when the 5-tick counter reaches the threshold.
    virtual void onBudgetTick() = 0;

    // Trigger an immediate auto-save just before the forced-loan modal opens.
    virtual void onForcedLoanDialogActive() = 0;

    // Trigger an immediate auto-save when the pause menu is opened.
    virtual void onPauseMenuOpened() = 0;

    // Suspend or resume the periodic auto-save timer.
    // Pass true to suspend (e.g., while a blocking modal is open),
    // false to resume.  Immediate-trigger methods bypass this flag.
    virtual void suspendAutoSave(bool suspended) = 0;

    // Load the save file with the most recent modification time across all slots.
    // Returns LoadResult{ok=false} if no save files exist.
    virtual LoadResult loadMostRecentSave() const = 0;

    // Returns true if at least one save file exists (any slot).
    virtual bool hasSaveData() const = 0;

    // Returns a three-state summary of save file availability and integrity.
    // NoSaves    -- no save files on disk.
    // AllCorrupt -- files exist but most recent is unreadable.
    // Valid      -- at least one valid, loadable save.
    virtual SaveFileState getSaveFileState() const = 0;

    // Returns the platform-specific save directory path used by this instance.
    // Linux:   ~/.config/aitown/saves/
    // Windows: %APPDATA%/aitown/saves/
    virtual std::string getSaveDirectoryPath() const = 0;
};
