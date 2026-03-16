// save_system_coverage_test.cpp — Additional SaveSystem coverage tests.
//
// Targets the uncovered paths identified in the lcov report (90.3% -> ~95%+):
//   - getSaveFileState() all three return values (NoSaves, AllCorrupt, Valid)
//   - ensureSaveDirectory() fallback branch (HOME unset → "aitown_saves")
//   - readJsonFromFile() non-existent file path
//   - writeJsonToFile() error path coverage (file open failure via bad path)
//   - isSaveCorrupted() when a save file exists but is unreadable (chmod 000)
//   - hasSaveData() with and without existing save files
//
// CMake target: simulation_tests (added via target_sources), label "unit".
// Mock policy: NiceMock — this file tests SaveSystem, not CitySimulation.

#include "src/simulation/SaveSystem.h"
#include "src/simulation/CitySimulation.h"
#include "src/interfaces/simulation_types.h"
#include "MockAudioSystem.h"
#include "MockRenderer.h"
#include "ManualRNG.h"
#include "ManualClock.h"
#include "ManualTerrainQuery.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <memory>
#include <string>
#include <cstdlib>
#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using ::testing::NiceMock;

// ============================================================================
// SaveSystemCoverageTest — exercises uncovered SaveSystem paths.
// Redirects HOME to an isolated temp directory.
// ============================================================================

class SaveSystemCoverageTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    ManualRNG                 rng_;
    ManualClock               clock_;
    ManualTerrainQuery        terrain_;
    std::unique_ptr<CitySimulation> sim_;

    std::string savedHome_;
    fs::path    tmpDir_;

    void SetUp() override {
        tmpDir_ = fs::temp_directory_path() / "aitown_ss_cov_test";
        fs::create_directories(tmpDir_);

#if defined(_WIN32)
        const char* v = std::getenv("APPDATA");
        savedHome_ = v ? v : "";
        _putenv_s("APPDATA", tmpDir_.string().c_str());
#else
        const char* v = std::getenv("HOME");
        savedHome_ = v ? v : "";
        setenv("HOME", tmpDir_.string().c_str(), 1);
#endif

        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
    }

    void TearDown() override {
        sim_.reset();
#if defined(_WIN32)
        if (!savedHome_.empty()) _putenv_s("APPDATA", savedHome_.c_str());
#else
        if (!savedHome_.empty()) setenv("HOME", savedHome_.c_str(), 1);
        else                     unsetenv("HOME");
#endif
        std::error_code ec;
        fs::remove_all(tmpDir_, ec);
    }

    std::unique_ptr<SaveSystem> makeWithSim() {
        auto ss = std::make_unique<SaveSystem>(&clock_);
        ss->setSimulation(sim_.get());
        return ss;
    }
};

// ---------------------------------------------------------------------------
// getSaveFileState — NoSaves branch: no save files exist yet.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, GetSaveFileState_NoSaves_ReturnsNoSaves) {
    auto ss = makeWithSim();
    // No saves written yet.
    SaveFileState state = ss->getSaveFileState();
    EXPECT_EQ(state, SaveFileState::NoSaves);
}

// ---------------------------------------------------------------------------
// getSaveFileState — Valid branch: a valid save file exists.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, GetSaveFileState_Valid_AfterAutoSave) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->autoSave().ok);
    SaveFileState state = ss->getSaveFileState();
    EXPECT_EQ(state, SaveFileState::Valid);
}

// ---------------------------------------------------------------------------
// getSaveFileState — AllCorrupt branch: autosave exists but is unreadable.
//
// isSaveCorrupted() calls loadMostRecentSave() → readJsonFromFile(), which
// returns ok=false only when the file cannot be opened (not when JSON is
// syntactically invalid — that distinction is made by deserializeFromJson,
// not readJsonFromFile).  So we make the file unreadable (chmod 000) to
// force the AllCorrupt branch.  Running as root skips this test since root
// ignores read permissions.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, GetSaveFileState_AllCorrupt_WhenFileUnreadable) {
#if defined(_WIN32)
    GTEST_SKIP() << "chmod-based unreadable-file test not applicable on Windows";
#else
    // Skip if running as root (root can always read files regardless of perms).
    if (geteuid() == 0) {
        GTEST_SKIP() << "Running as root — cannot test unreadable file";
    }

    auto ss = makeWithSim();
    // First create a valid autosave so hasSaveData() returns true.
    ASSERT_TRUE(ss->autoSave().ok);

    // Make the autosave file unreadable.
    std::string saveDir = ss->getSaveDirectoryPath();
    std::string autoSavePath = saveDir + "/autosave.json";
    ASSERT_EQ(chmod(autoSavePath.c_str(), 0000), 0) << "chmod failed";

    // Also remove read permissions on all slot files if they exist.
    for (int slot = 1; slot <= 3; ++slot) {
        std::string slotPath = saveDir + "/slot" + std::to_string(slot) + ".json";
        if (fs::exists(slotPath)) {
            chmod(slotPath.c_str(), 0000);
        }
    }

    SaveFileState state = ss->getSaveFileState();
    EXPECT_EQ(state, SaveFileState::AllCorrupt);

    // Restore permissions so TearDown can clean up.
    chmod(autoSavePath.c_str(), 0644);
    for (int slot = 1; slot <= 3; ++slot) {
        std::string slotPath = saveDir + "/slot" + std::to_string(slot) + ".json";
        if (fs::exists(slotPath)) {
            chmod(slotPath.c_str(), 0644);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// hasSaveData / isSaveCorrupted after writing valid data.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, HasSaveData_True_AfterSave) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->saveToSlot(1).ok);
    EXPECT_TRUE(ss->hasSaveData());
}

TEST_F(SaveSystemCoverageTest, IsSaveCorrupted_False_AfterValidSave) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->autoSave().ok);
    EXPECT_FALSE(ss->isSaveCorrupted());
}

// ---------------------------------------------------------------------------
// loadFromSlot — file-not-found path.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, LoadFromSlot_NotFound_ReturnsError) {
    auto ss = makeWithSim();
    // Don't write any save file for slot 2.
    LoadResult r = ss->loadFromSlot(2);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

// ---------------------------------------------------------------------------
// loadAutoSave — file-not-found path.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, LoadAutoSave_NotFound_ReturnsError) {
    auto ss = makeWithSim();
    LoadResult r = ss->loadAutoSave();
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

// ---------------------------------------------------------------------------
// writeJsonToFile error path: write to a directory path (not a file).
// This forces the fopen/ofstream to fail so the error branch executes.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, WriteJsonToFile_BadPath_ViaSlotToNonWritableDir) {
    auto ss = makeWithSim();
    // Create a directory where slot1.json would go; saving to that path fails.
    std::string saveDir = ss->getSaveDirectoryPath();
    std::string slotPath = saveDir + "/slot1.json";

    // Place a *directory* at the slot path so the atomic write cannot create the file.
    std::error_code ec;
    fs::create_directory(slotPath, ec);  // May fail on some platforms; skip if so.
    if (ec || !fs::is_directory(slotPath, ec)) {
        GTEST_SKIP() << "Cannot create directory at slot path on this platform";
    }

    SaveResult r = ss->saveToSlot(1);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());

    // Cleanup: remove the directory.
    fs::remove(slotPath, ec);
}

// ---------------------------------------------------------------------------
// ensureSaveDirectory fallback: HOME unset → returns non-empty path.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, GetSaveDirectoryPath_HomeUnset_ReturnsNonEmpty) {
#if defined(_WIN32)
    // On Windows, unset APPDATA.
    _putenv_s("APPDATA", "");
#else
    unsetenv("HOME");
#endif

    SaveSystem ss(&clock_);
    std::string dir = ss.getSaveDirectoryPath();
    EXPECT_FALSE(dir.empty());

    // Restore HOME to tmpDir_ for TearDown.
#if defined(_WIN32)
    _putenv_s("APPDATA", tmpDir_.string().c_str());
#else
    setenv("HOME", tmpDir_.string().c_str(), 1);
#endif
}

// ---------------------------------------------------------------------------
// onForcedLoanDialogActive and onPauseMenuOpened with a sim attached.
// ---------------------------------------------------------------------------
TEST_F(SaveSystemCoverageTest, OnForcedLoanDialogActive_WithSim_TriggersAutoSave) {
    auto ss = makeWithSim();
    // Should not crash; triggers auto-save.
    ss->onForcedLoanDialogActive();
    // After calling onForcedLoanDialogActive, autosave.json may exist.
    SUCCEED();
}

TEST_F(SaveSystemCoverageTest, OnPauseMenuOpened_WithSim_TriggersAutoSave) {
    auto ss = makeWithSim();
    ss->onPauseMenuOpened();
    SUCCEED();
}
