// save_system_real_test.cpp — Coverage tests for the real SaveSystem class.
//
// The existing save_system_test.cpp uses stub implementations and never
// instantiates SaveSystem itself, leaving SaveSystem.cpp at 0% coverage.
// This file directly exercises the real SaveSystem for the lcov gate.
//
// Two fixtures:
//   SaveSystemNoSimTest  — paths that don't require a CitySimulation
//   SaveSystemWithSimTest — disk-I/O paths using a real CitySimulation
//
// CMake target: simulation_tests (added via target_sources), label "unit".
// Mock policy: NiceMock — this fixture tests SaveSystem, not CitySimulation.

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

namespace fs = std::filesystem;
using ::testing::NiceMock;

// ============================================================================
// SaveSystemNoSimTest — exercises paths that return early without a sim.
// ============================================================================

class SaveSystemNoSimTest : public ::testing::Test {
protected:
    ManualClock clock_;
};

TEST_F(SaveSystemNoSimTest, Constructor_CreatesInstance) {
    SaveSystem ss(&clock_);
    (void)ss;
}

TEST_F(SaveSystemNoSimTest, SetSimulation_Null_IsAccepted) {
    SaveSystem ss(&clock_);
    ss.setSimulation(nullptr);
}

TEST_F(SaveSystemNoSimTest, AutoSave_NoSim_ReturnsError) {
    SaveSystem ss(&clock_);
    SaveResult r = ss.autoSave();
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST_F(SaveSystemNoSimTest, SaveToSlot_SlotZero_ReturnsError) {
    SaveSystem ss(&clock_);
    SaveResult r = ss.saveToSlot(0);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST_F(SaveSystemNoSimTest, SaveToSlot_SlotFour_ReturnsError) {
    SaveSystem ss(&clock_);
    SaveResult r = ss.saveToSlot(4);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST_F(SaveSystemNoSimTest, SaveToSlot_ValidSlots_NoSim_ReturnsError) {
    SaveSystem ss(&clock_);
    EXPECT_FALSE(ss.saveToSlot(1).ok);
    EXPECT_FALSE(ss.saveToSlot(2).ok);
    EXPECT_FALSE(ss.saveToSlot(3).ok);
}

TEST_F(SaveSystemNoSimTest, LoadFromSlot_SlotZero_ReturnsError) {
    SaveSystem ss(&clock_);
    LoadResult r = ss.loadFromSlot(0);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST_F(SaveSystemNoSimTest, LoadFromSlot_SlotFour_ReturnsError) {
    SaveSystem ss(&clock_);
    LoadResult r = ss.loadFromSlot(4);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST_F(SaveSystemNoSimTest, GetSaveDirectoryPath_ReturnsNonEmpty) {
    SaveSystem ss(&clock_);
    EXPECT_FALSE(ss.getSaveDirectoryPath().empty());
}

TEST_F(SaveSystemNoSimTest, Update_Suspended_DoesNotCrash) {
    SaveSystem ss(&clock_);
    ss.suspendAutoSave(true);
    ss.update(200.0f);
}

TEST_F(SaveSystemNoSimTest, Update_NoSim_DoesNotCrash) {
    SaveSystem ss(&clock_);
    ss.update(200.0f);
}

TEST_F(SaveSystemNoSimTest, OnBudgetTick_NoSim_DoesNotCrash) {
    SaveSystem ss(&clock_);
    for (int i = 0; i < 6; ++i) ss.onBudgetTick();
}

TEST_F(SaveSystemNoSimTest, OnBudgetTick_Suspended_DoesNotFire) {
    SaveSystem ss(&clock_);
    ss.suspendAutoSave(true);
    for (int i = 0; i < 6; ++i) ss.onBudgetTick();
}

TEST_F(SaveSystemNoSimTest, OnForcedLoanDialogActive_NoSim_DoesNotCrash) {
    SaveSystem ss(&clock_);
    ss.onForcedLoanDialogActive();
}

TEST_F(SaveSystemNoSimTest, OnPauseMenuOpened_NoSim_DoesNotCrash) {
    SaveSystem ss(&clock_);
    ss.onPauseMenuOpened();
}

TEST_F(SaveSystemNoSimTest, SuspendAutoSave_Toggle_DoesNotCrash) {
    SaveSystem ss(&clock_);
    ss.suspendAutoSave(true);
    ss.suspendAutoSave(false);
}

// ============================================================================
// SaveSystemWithSimTest — disk-I/O paths using a real CitySimulation.
//
// Redirects HOME (Linux) / APPDATA (Windows) to a temporary directory so
// save files don't pollute the dev environment.  The directory is removed
// in TearDown.
// ============================================================================

class SaveSystemWithSimTest : public ::testing::Test {
protected:
    NiceMock<MockRenderer>    renderer_;
    NiceMock<MockAudioSystem> audio_;
    ManualRNG                 rng_;
    ManualClock               clock_;
    ManualTerrainQuery        terrain_;
    std::unique_ptr<CitySimulation> sim_;

    std::string savedEnvValue_;
    fs::path    tmpDir_;

    void SetUp() override {
        // Redirect the save directory to an isolated temp path.
        tmpDir_ = fs::temp_directory_path() / "aitown_ss_real_test";
        fs::create_directories(tmpDir_);

#if defined(_WIN32)
        const char* v = std::getenv("APPDATA");
        savedEnvValue_ = v ? v : "";
        _putenv_s("APPDATA", tmpDir_.string().c_str());
#else
        const char* v = std::getenv("HOME");
        savedEnvValue_ = v ? v : "";
        setenv("HOME", tmpDir_.string().c_str(), 1);
#endif

        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
    }

    void TearDown() override {
        sim_.reset();

        // Restore the original environment variable.
#if defined(_WIN32)
        if (!savedEnvValue_.empty()) _putenv_s("APPDATA", savedEnvValue_.c_str());
#else
        if (!savedEnvValue_.empty()) setenv("HOME", savedEnvValue_.c_str(), 1);
        else                         unsetenv("HOME");
#endif

        std::error_code ec;
        fs::remove_all(tmpDir_, ec);
    }

    // Helper: construct a SaveSystem with sim_ already registered.
    std::unique_ptr<SaveSystem> makeWithSim() {
        auto ss = std::make_unique<SaveSystem>(&clock_);
        ss->setSimulation(sim_.get());
        return ss;
    }
};

TEST_F(SaveSystemWithSimTest, AutoSave_WithSim_Succeeds) {
    auto ss = makeWithSim();
    SaveResult r = ss->autoSave();
    EXPECT_TRUE(r.ok) << "autoSave() error: " << r.error;
}

TEST_F(SaveSystemWithSimTest, SaveToSlot_AllSlots_Succeed) {
    auto ss = makeWithSim();
    EXPECT_TRUE(ss->saveToSlot(1).ok);
    EXPECT_TRUE(ss->saveToSlot(2).ok);
    EXPECT_TRUE(ss->saveToSlot(3).ok);
}

TEST_F(SaveSystemWithSimTest, LoadAutoSave_AfterSave_ReturnsValidJson) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->autoSave().ok);
    LoadResult r = ss->loadAutoSave();
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.jsonData.empty());
    EXPECT_NE(r.jsonData.find("schema_version"), std::string::npos);
}

TEST_F(SaveSystemWithSimTest, LoadFromSlot_AfterSave_ReturnsValidJson) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->saveToSlot(1).ok);
    LoadResult r = ss->loadFromSlot(1);
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.jsonData.empty());
}

TEST_F(SaveSystemWithSimTest, LoadFromSlot_NoFile_ReturnsError) {
    auto ss = makeWithSim();
    // Slot 2 has no file yet.
    LoadResult r = ss->loadFromSlot(2);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST_F(SaveSystemWithSimTest, LoadAutoSave_NoFile_ReturnsError) {
    auto ss = makeWithSim();
    LoadResult r = ss->loadAutoSave();
    EXPECT_FALSE(r.ok);
}

TEST_F(SaveSystemWithSimTest, LoadMostRecentSave_NoFiles_ReturnsError) {
    auto ss = makeWithSim();
    LoadResult r = ss->loadMostRecentSave();
    EXPECT_FALSE(r.ok);
}

TEST_F(SaveSystemWithSimTest, LoadMostRecentSave_AfterSaves_ReturnsData) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->autoSave().ok);
    ASSERT_TRUE(ss->saveToSlot(1).ok);
    LoadResult r = ss->loadMostRecentSave();
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.jsonData.empty());
}

TEST_F(SaveSystemWithSimTest, HasSaveData_FalseBeforeSave) {
    auto ss = makeWithSim();
    EXPECT_FALSE(ss->hasSaveData());
}

TEST_F(SaveSystemWithSimTest, HasSaveData_TrueAfterSave) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->autoSave().ok);
    EXPECT_TRUE(ss->hasSaveData());
}

TEST_F(SaveSystemWithSimTest, IsSaveCorrupted_FalseWhenNoSaveData) {
    auto ss = makeWithSim();
    EXPECT_FALSE(ss->isSaveCorrupted());
}

TEST_F(SaveSystemWithSimTest, IsSaveCorrupted_FalseAfterValidSave) {
    auto ss = makeWithSim();
    ASSERT_TRUE(ss->autoSave().ok);
    EXPECT_FALSE(ss->isSaveCorrupted());
}

TEST_F(SaveSystemWithSimTest, Update_AccumulatesAndFiresAt120s) {
    auto ss = makeWithSim();
    // Just under 120 s — must NOT fire.
    ss->update(119.0f);
    EXPECT_FALSE(ss->hasSaveData());
    // Push past the threshold — must fire autoSave.
    ss->update(1.0f);
    EXPECT_TRUE(ss->hasSaveData()) << "autoSave must fire at 120 s accumulation";
}

TEST_F(SaveSystemWithSimTest, OnBudgetTick_FiveTicks_FiresAutoSave) {
    auto ss = makeWithSim();
    for (int i = 0; i < 5; ++i) ss->onBudgetTick();
    EXPECT_TRUE(ss->hasSaveData()) << "autoSave must fire after 5 budget ticks";
}

TEST_F(SaveSystemWithSimTest, OnForcedLoanDialogActive_FiresAutoSave) {
    auto ss = makeWithSim();
    ss->onForcedLoanDialogActive();
    EXPECT_TRUE(ss->hasSaveData());
}

TEST_F(SaveSystemWithSimTest, OnPauseMenuOpened_FiresAutoSave) {
    auto ss = makeWithSim();
    ss->onPauseMenuOpened();
    EXPECT_TRUE(ss->hasSaveData());
}
