// tests/ui/keybindings_load_test.cpp
//
// Unit tests for KeyBindings::load().
//
// Implementation under test: src/ui/key_bindings.h — load() parses a flat
// "key": "value" JSON file and assigns values to struct fields.
//
// Behaviour verified:
//   1. Valid JSON with all rebindable fields — every field updated correctly.
//   2. Reserved key "Q" rejected — default retained, no other field mutated.
//   3. Reserved key "E" rejected — default retained, no other field mutated.
//   4. Absent file — returns immediately, all fields keep defaults, no crash.
//   5. Unknown action name — logged and skipped, valid sibling field updated.
//   6. Partial JSON (2 of 15 rebindable fields) — only those 2 change.
//
// Temp files: written to /tmp/aitown_kb_test_<case>.json and removed in
// TearDown().  std::tmpnam is avoided — fixed names with per-fixture suffix
// are sufficient for single-process unit test execution.
//
// Framework: Google Test (no GMock needed — KeyBindings has no mock seam).
// Label: unit (applied by aitown_add_tests(ui_tests LABEL "unit")).

#include "src/ui/key_bindings.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <cstdio>   // std::remove

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Write text content to path, ASSERT-failing the test on I/O error.
void WriteTempFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "Could not open temp file for writing: " << path;
    out << content;
    ASSERT_TRUE(out.good()) << "Failed to write temp file: " << path;
}

// Returns a KeyBindings struct freshly default-constructed.
// Named helper for readability in EXPECT chains.
KeyBindings Defaults() {
    return KeyBindings{};
}

}  // namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class KeyBindingsLoadTest : public ::testing::Test {
protected:
    // Fixed temp paths — one per test case to allow parallel test registration,
    // though GTest runs them serially.
    static constexpr const char* kTmpAll      = "/tmp/aitown_kb_test_all.json";
    static constexpr const char* kTmpRsvQ     = "/tmp/aitown_kb_test_rsv_q.json";
    static constexpr const char* kTmpRsvE     = "/tmp/aitown_kb_test_rsv_e.json";
    static constexpr const char* kTmpUnknown  = "/tmp/aitown_kb_test_unknown.json";
    static constexpr const char* kTmpPartial  = "/tmp/aitown_kb_test_partial.json";

    void TearDown() override {
        // Remove any temp files that may have been created.  std::remove is a
        // no-op (returns non-zero) if the file does not exist, so it is safe to
        // call unconditionally.
        std::remove(kTmpAll);
        std::remove(kTmpRsvQ);
        std::remove(kTmpRsvE);
        std::remove(kTmpUnknown);
        std::remove(kTmpPartial);
        // Destructor-path contract: KeyBindings has no mock expectations and no
        // dynamic resources beyond its std::string members (including two const
        // fields `undo`/`save` that make the move-assignment operator deleted).
        // Google Test constructs a fresh fixture object (and therefore a fresh
        // kb_) for every TEST_F, so no explicit reset is needed here.
        // The const member constraint is the reason we do NOT call
        //   kb_ = KeyBindings{};
        // here — the assignment would not compile.
    }

    KeyBindings kb_;
};

// ---------------------------------------------------------------------------
// Test 1 — all rebindable fields applied
// ---------------------------------------------------------------------------

TEST_F(KeyBindingsLoadTest, Load_ValidJson_AllFieldsApplied) {
    // Use non-default values for every rebindable field.
    // Chosen values do not include "Q" or "E" (reserved) and are distinct from
    // each other and from the defaults to make assertion failures obvious.
    const std::string json = R"({
  "camPanUp":       "W",
  "camPanDown":     "S",
  "camPanLeft":     "A",
  "camPanRight":    "D",
  "toolZone":       "F",
  "toolRoad":       "G",
  "toolUtilities":  "H",
  "toolDemolish":   "J",
  "toolInspector":  "K",
  "toggleTaxPanel": "L",
  "toggleNotifLog": "M",
  "togglePause":    "N",
  "speedIncrease":  "O",
  "speedDecrease":  "P",
  "openPauseMenu":  "X"
})";
    WriteTempFile(kTmpAll, json);

    kb_.load(kTmpAll);

    EXPECT_EQ(kb_.camPanUp,       "W");
    EXPECT_EQ(kb_.camPanDown,     "S");
    EXPECT_EQ(kb_.camPanLeft,     "A");
    EXPECT_EQ(kb_.camPanRight,    "D");
    EXPECT_EQ(kb_.toolZone,       "F");
    EXPECT_EQ(kb_.toolRoad,       "G");
    EXPECT_EQ(kb_.toolUtilities,  "H");
    EXPECT_EQ(kb_.toolDemolish,   "J");
    EXPECT_EQ(kb_.toolInspector,  "K");
    EXPECT_EQ(kb_.toggleTaxPanel, "L");
    EXPECT_EQ(kb_.toggleNotifLog, "M");
    EXPECT_EQ(kb_.togglePause,    "N");
    EXPECT_EQ(kb_.speedIncrease,  "O");
    EXPECT_EQ(kb_.speedDecrease,  "P");
    EXPECT_EQ(kb_.openPauseMenu,  "X");

    // Non-rebindable const fields must not change.
    EXPECT_EQ(kb_.undo, "Ctrl+Z");
    EXPECT_EQ(kb_.save, "Ctrl+S");
}

// ---------------------------------------------------------------------------
// Test 2 — reserved key "Q" rejected, default retained
// ---------------------------------------------------------------------------

TEST_F(KeyBindingsLoadTest, Load_ReservedKeyQ_IsRejected_DefaultRetained) {
    // Assign "Q" (reserved) to toolDemolish.
    // The default for toolDemolish is "D".
    // A valid sibling field is also present so we can confirm the rest of the
    // file is still processed normally.
    const std::string json = R"({
  "toolDemolish": "Q",
  "toolZone": "F"
})";
    WriteTempFile(kTmpRsvQ, json);

    kb_.load(kTmpRsvQ);

    // "Q" must be rejected — default "D" must be retained.
    EXPECT_EQ(kb_.toolDemolish, "D");
    // The valid sibling must be applied.
    EXPECT_EQ(kb_.toolZone, "F");
    // All other fields must remain at defaults.
    EXPECT_EQ(kb_.camPanUp,       Defaults().camPanUp);
    EXPECT_EQ(kb_.toolRoad,       Defaults().toolRoad);
    EXPECT_EQ(kb_.toolUtilities,  Defaults().toolUtilities);
    EXPECT_EQ(kb_.toolInspector,  Defaults().toolInspector);
}

// ---------------------------------------------------------------------------
// Test 3 — reserved key "E" rejected, default retained
// ---------------------------------------------------------------------------

TEST_F(KeyBindingsLoadTest, Load_ReservedKeyE_IsRejected_DefaultRetained) {
    // Assign "E" (reserved) to toolRoad.
    // The default for toolRoad is "R".
    const std::string json = R"({
  "toolRoad": "E",
  "toolUtilities": "H"
})";
    WriteTempFile(kTmpRsvE, json);

    kb_.load(kTmpRsvE);

    // "E" must be rejected — default "R" must be retained.
    EXPECT_EQ(kb_.toolRoad, "R");
    // The valid sibling must be applied.
    EXPECT_EQ(kb_.toolUtilities, "H");
    // Spot-check other fields remain at defaults.
    EXPECT_EQ(kb_.camPanUp,      Defaults().camPanUp);
    EXPECT_EQ(kb_.toolZone,      Defaults().toolZone);
    EXPECT_EQ(kb_.toolDemolish,  Defaults().toolDemolish);
    EXPECT_EQ(kb_.togglePause,   Defaults().togglePause);
}

// ---------------------------------------------------------------------------
// Test 4 — absent file keeps defaults, no crash
// ---------------------------------------------------------------------------

TEST_F(KeyBindingsLoadTest, Load_AbsentFile_KeepsDefaults) {
    // load() must return silently if the file cannot be opened.
    // No assertion other than EXPECT_NO_FATAL_FAILURE and defaults retained.
    EXPECT_NO_FATAL_FAILURE(kb_.load("/nonexistent/path/keybindings.json"));

    // Every rebindable field must equal the corresponding default.
    KeyBindings d;
    EXPECT_EQ(kb_.camPanUp,       d.camPanUp);
    EXPECT_EQ(kb_.camPanDown,     d.camPanDown);
    EXPECT_EQ(kb_.camPanLeft,     d.camPanLeft);
    EXPECT_EQ(kb_.camPanRight,    d.camPanRight);
    EXPECT_EQ(kb_.toolZone,       d.toolZone);
    EXPECT_EQ(kb_.toolRoad,       d.toolRoad);
    EXPECT_EQ(kb_.toolUtilities,  d.toolUtilities);
    EXPECT_EQ(kb_.toolDemolish,   d.toolDemolish);
    EXPECT_EQ(kb_.toolInspector,  d.toolInspector);
    EXPECT_EQ(kb_.toggleTaxPanel, d.toggleTaxPanel);
    EXPECT_EQ(kb_.toggleNotifLog, d.toggleNotifLog);
    EXPECT_EQ(kb_.togglePause,    d.togglePause);
    EXPECT_EQ(kb_.speedIncrease,  d.speedIncrease);
    EXPECT_EQ(kb_.speedDecrease,  d.speedDecrease);
    EXPECT_EQ(kb_.openPauseMenu,  d.openPauseMenu);
    EXPECT_EQ(kb_.undo,           d.undo);
    EXPECT_EQ(kb_.save,           d.save);
}

// ---------------------------------------------------------------------------
// Test 5 — unknown action name ignored, valid sibling updated
// ---------------------------------------------------------------------------

TEST_F(KeyBindingsLoadTest, Load_UnknownKey_IsIgnored_OtherFieldsUnaffected) {
    // "futureAction" is not a known field name.  The parser should log a warning
    // and skip it without disrupting processing of subsequent valid pairs.
    const std::string json = R"({
  "futureAction": "X",
  "toolZone": "F"
})";
    WriteTempFile(kTmpUnknown, json);

    // Must not crash (unknown key logs a warning, does not throw).
    EXPECT_NO_FATAL_FAILURE(kb_.load(kTmpUnknown));

    // toolZone must be updated.
    EXPECT_EQ(kb_.toolZone, "F");
    // All other fields must remain at defaults.
    EXPECT_EQ(kb_.camPanUp,      Defaults().camPanUp);
    EXPECT_EQ(kb_.toolRoad,      Defaults().toolRoad);
    EXPECT_EQ(kb_.toolUtilities, Defaults().toolUtilities);
    EXPECT_EQ(kb_.toolDemolish,  Defaults().toolDemolish);
    EXPECT_EQ(kb_.toolInspector, Defaults().toolInspector);
    EXPECT_EQ(kb_.togglePause,   Defaults().togglePause);
    EXPECT_EQ(kb_.openPauseMenu, Defaults().openPauseMenu);
}

// ---------------------------------------------------------------------------
// Test 6 — partial JSON (2 of 15 rebindable fields) — only those 2 change
// ---------------------------------------------------------------------------

TEST_F(KeyBindingsLoadTest, Load_PartialJson_OnlySpecifiedFieldsChanged) {
    // Only camPanUp and toolDemolish are in the file.
    const std::string json = R"({
  "camPanUp":      "W",
  "toolDemolish":  "J"
})";
    WriteTempFile(kTmpPartial, json);

    kb_.load(kTmpPartial);

    // The two specified fields must be updated.
    EXPECT_EQ(kb_.camPanUp,      "W");
    EXPECT_EQ(kb_.toolDemolish,  "J");

    // All 13 remaining rebindable fields must keep their defaults.
    KeyBindings d;
    EXPECT_EQ(kb_.camPanDown,     d.camPanDown);
    EXPECT_EQ(kb_.camPanLeft,     d.camPanLeft);
    EXPECT_EQ(kb_.camPanRight,    d.camPanRight);
    EXPECT_EQ(kb_.toolZone,       d.toolZone);
    EXPECT_EQ(kb_.toolRoad,       d.toolRoad);
    EXPECT_EQ(kb_.toolUtilities,  d.toolUtilities);
    EXPECT_EQ(kb_.toolInspector,  d.toolInspector);
    EXPECT_EQ(kb_.toggleTaxPanel, d.toggleTaxPanel);
    EXPECT_EQ(kb_.toggleNotifLog, d.toggleNotifLog);
    EXPECT_EQ(kb_.togglePause,    d.togglePause);
    EXPECT_EQ(kb_.speedIncrease,  d.speedIncrease);
    EXPECT_EQ(kb_.speedDecrease,  d.speedDecrease);
    EXPECT_EQ(kb_.openPauseMenu,  d.openPauseMenu);
    // Non-rebindable const fields must not change.
    EXPECT_EQ(kb_.undo, d.undo);
    EXPECT_EQ(kb_.save, d.save);
}
