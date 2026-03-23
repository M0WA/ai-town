#pragma once

#include "src/interfaces/ISaveSystem.h"
#include "src/simulation/SaveSystem.h"  // full SaveResult / LoadResult definitions
#include "gmock/gmock.h"

// MockSaveSystem — GMock implementation of ISaveSystem.
// Header-only, NiceMock/StrictMock-compatible per testability-architecture.md.
// Used by UIManagerUnsavedQuitTest, UIManagerSaveFailureTest, MainMenuSaveStateTest.
//
// Source location: tests/ui/MockSaveSystem.h
// Must NOT be placed under src/ — mock files belong under tests/ only.
class MockSaveSystem : public ISaveSystem {
public:
    MOCK_METHOD(SaveResult,    saveToSlot,             (int slot),               (override));
    MOCK_METHOD(SaveResult,    autoSave,               (),                       (override));
    MOCK_METHOD(void,          update,                 (float realDeltaSeconds), (override));
    MOCK_METHOD(void,          onBudgetTick,           (),                       (override));
    MOCK_METHOD(void,          onForcedLoanDialogActive, (),                     (override));
    MOCK_METHOD(void,          onPauseMenuOpened,      (),                       (override));
    MOCK_METHOD(void,          suspendAutoSave,        (bool suspended),         (override));
    MOCK_METHOD(LoadResult,    loadMostRecentSave,     (),                       (const, override));
    MOCK_METHOD(bool,          hasSaveData,            (),                       (const, override));
    MOCK_METHOD(SaveFileState, getSaveFileState,       (),                       (const, override));
    MOCK_METHOD(std::string,   getSaveDirectoryPath,   (),                       (const, override));
};
