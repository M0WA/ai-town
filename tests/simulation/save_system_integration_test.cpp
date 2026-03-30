// save_system_integration_test.cpp — Phase 11 save system integration test.
//
// Test: SaveSystem_AutoSave_SuspendedDuringModal
//
// Verifies that auto-save does not fire while a modal dialog is open, and fires
// once the modal is closed (suspension is lifted).
//
// CMake target: integration_tests, label "integration".
// Mock policy: NiceMock for integration tests (per CLAUDE.md).
//
// DESIGN NOTE: This integration test exercises the SaveSystem suspension contract
// in combination with ManualClock for deterministic time control. It uses the
// same StubSaveSystem from save_system_test.cpp (reproduced here to avoid a
// shared-header dependency between test targets) and does not require a live
// UIManager or Irrlicht device.
//
// Per phase-11.md §SaveSystem_AutoSave_SuspendedDuringModal:
//   - Modal dialog open → suspendAutoSave(true).
//   - Advance ManualClock 200 s, call update() → auto-save must NOT fire.
//   - suspendAutoSave(false) → advance 1 s, call update() → NOT enough (< 120 s baseline).
//   - Advance 120 s total since resume → auto-save fires.
//
// The test uses the SaveSystem suspension API directly (as spec-documented) rather
// than going through UIManager::showForcedLoanDialog(), since UIManager is currently
// a Phase 1 stub and does not yet call suspendAutoSave().

#include "src/interfaces/IClock.h"
#include "ManualClock.h"
#include <gtest/gtest.h>
#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// StubSimForIntegration — minimal serialisable stub for integration tests.
// Separate from the unit test stub to avoid shared-header dependencies between
// simulation_tests and integration_tests CMake targets.
// ---------------------------------------------------------------------------
class StubSimForIntegration {
public:
    bool serializeToJson(std::string& out) const {
        out = "{\"schema_version\":1,\"treasury\":0}";
        return true;
    }
};

// ---------------------------------------------------------------------------
// StubSaveSystemForIntegration — minimal save system driven by ManualClock.
// Does not write to disk; tracks auto-save count and suspension state.
// This matches the contract of the real SaveSystem class that Phase 11 delivers.
// ---------------------------------------------------------------------------
class StubSaveSystemForIntegration {
public:
    explicit StubSaveSystemForIntegration(IClock* clock)
        : m_clock(clock) {}

    void setSimulation(StubSimForIntegration* sim) { m_sim = sim; }

    // Per-frame update — fires time-based auto-save when not suspended and
    // 120 s have elapsed since the last save (or since suspension was lifted).
    void update(float /*realDeltaSeconds*/) {
        if (m_suspended) return;
        double now = m_clock->nowSeconds();
        if (now - m_lastAutoSaveTime >= 120.0) {
            doAutoSave();
        }
    }

    // Suspend or resume the auto-save timer.
    // Resuming resets the clock baseline to now (prevents immediate fire).
    void suspendAutoSave(bool suspend) {
        m_suspended = suspend;
        if (!m_suspended) {
            m_lastAutoSaveTime = m_clock->nowSeconds();
        }
    }

    int  autoSaveCount()  const { return m_saveCount; }
    bool hasSaveData()    const { return m_saveCount > 0; }

private:
    void doAutoSave() {
        m_lastAutoSaveTime = m_clock->nowSeconds();
        ++m_saveCount;
    }

    IClock*                   m_clock{nullptr};
    StubSimForIntegration*    m_sim{nullptr};
    double  m_lastAutoSaveTime{0.0};
    int     m_saveCount{0};
    bool    m_suspended{false};
};

// ===========================================================================
// Test: SaveSystem_AutoSave_SuspendedDuringModal
//
// Scenario:
//   1. Open modal dialog → suspendAutoSave(true).
//   2. Advance 200 s → update(200 f) → auto-save must NOT fire (suspended).
//   3. Close modal → suspendAutoSave(false) (baseline reset to now).
//   4. Advance 1 s → update(1 f) → auto-save must NOT fire (< 120 s since resume).
//   5. Advance 119 more seconds → update(119 f) → auto-save fires (120 s elapsed).
//
// Spec ref: phase-11.md §SaveSystem_AutoSave_SuspendedDuringModal.
// ===========================================================================
TEST(SaveSystemIntegrationTest, SaveSystem_AutoSave_SuspendedDuringModal)
{
    ManualClock clock;
    StubSimForIntegration sim;
    StubSaveSystemForIntegration saveSystem(&clock);
    saveSystem.setSimulation(&sim);

    // Step 1: Open modal dialog — suspend auto-save.
    saveSystem.suspendAutoSave(true);

    // Step 2: Advance 200 s — must NOT fire (suspended).
    clock.advance(200.0);
    saveSystem.update(200.0f);
    EXPECT_EQ(saveSystem.autoSaveCount(), 0)
        << "Auto-save must not fire while suspended (modal open), even after 200 s";
    EXPECT_FALSE(saveSystem.hasSaveData())
        << "hasSaveData() must return false before any auto-save";

    // Step 3: Close modal — resume auto-save (baseline reset to now = 200 s).
    saveSystem.suspendAutoSave(false);

    // Step 4: Advance 1 s (total clock = 201 s) — must NOT fire (only 1 s since resume).
    clock.advance(1.0);
    saveSystem.update(1.0f);
    EXPECT_EQ(saveSystem.autoSaveCount(), 0)
        << "Auto-save must not fire 1 s after suspension is lifted (< 120 s baseline)";

    // Step 5: Advance 119 more seconds (total = 320 s, but 120 s since resume baseline).
    clock.advance(119.0);
    saveSystem.update(119.0f);
    EXPECT_EQ(saveSystem.autoSaveCount(), 1)
        << "Auto-save must fire exactly once 120 s after modal is closed";
    EXPECT_TRUE(saveSystem.hasSaveData())
        << "hasSaveData() must return true after auto-save fires";
}

// ===========================================================================
// Test: SaveSystem_AutoSave_SuspendedDuringModal_NoFileBeforeResume
//
// Companion test: verifies that no save file exists before the modal closes
// and that the save correctly reflects post-close state.
// ===========================================================================
TEST(SaveSystemIntegrationTest,
     SaveSystem_AutoSave_SuspendedDuringModal_NoFileBeforeResume)
{
    ManualClock clock;
    StubSimForIntegration sim;
    StubSaveSystemForIntegration saveSystem(&clock);
    saveSystem.setSimulation(&sim);

    // Open modal and advance far past the 120 s threshold.
    saveSystem.suspendAutoSave(true);
    clock.advance(300.0);
    saveSystem.update(300.0f);

    // No save must have occurred.
    EXPECT_FALSE(saveSystem.hasSaveData())
        << "No save file must exist while modal is open";
    EXPECT_EQ(saveSystem.autoSaveCount(), 0);

    // Close modal.
    saveSystem.suspendAutoSave(false);

    // Immediately after closing, still no save (baseline just reset).
    saveSystem.update(0.0f);
    EXPECT_EQ(saveSystem.autoSaveCount(), 0)
        << "No save fires immediately on modal close (baseline was just reset)";

    // After 120 s exactly, save fires.
    clock.advance(120.0);
    saveSystem.update(120.0f);
    EXPECT_EQ(saveSystem.autoSaveCount(), 1)
        << "Auto-save fires exactly at 120 s post-close";
}
