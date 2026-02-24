// tests/ui/key_bindings_test.cpp
//
// KeyBindingsTest — unit tests for KeyBindings::isReservedKey().
//
// Verified against src/ui/key_bindings.h:
//   isReservedKey() returns true for "Q" and "E" only.
//   All other key strings return false.
//   Rationale (from hotkey-scheme.md): Q and E are reserved for future
//   camera-rotation controls and must never be assigned to any tool or action.
//
// KeyBindings is a plain struct with no dependencies — no mocks needed.
// These are pure unit tests.

#include "src/ui/key_bindings.h"
#include <gtest/gtest.h>

// --- Reserved keys (must return true) ---

TEST(KeyBindings, IsReservedKey_Q_ReturnsTrue) {
    KeyBindings kb;
    EXPECT_TRUE(kb.isReservedKey("Q"));
}

TEST(KeyBindings, IsReservedKey_E_ReturnsTrue) {
    KeyBindings kb;
    EXPECT_TRUE(kb.isReservedKey("E"));
}

// --- Non-reserved keys (must return false) ---

// W is a common WASD key; must not be reserved so players can rebind camera pan.
TEST(KeyBindings, IsReservedKey_W_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("W"));
}

TEST(KeyBindings, IsReservedKey_A_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("A"));
}

TEST(KeyBindings, IsReservedKey_S_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("S"));
}

TEST(KeyBindings, IsReservedKey_D_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("D"));
}

// Default tool hotkeys must not be inadvertently reserved.
TEST(KeyBindings, IsReservedKey_Z_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("Z"));  // toolZone default
}

TEST(KeyBindings, IsReservedKey_R_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("R"));  // toolRoad default
}

TEST(KeyBindings, IsReservedKey_T_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("T"));  // toggleTaxPanel default
}

TEST(KeyBindings, IsReservedKey_Space_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("Space"));  // togglePause default
}

// Lowercase variants are distinct strings — isReservedKey() is case-sensitive
// (the implementation uses exact string comparison: key == "Q" || key == "E").
TEST(KeyBindings, IsReservedKey_LowercaseQ_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("q"));
}

TEST(KeyBindings, IsReservedKey_LowercaseE_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("e"));
}

// Empty string must not be considered reserved.
TEST(KeyBindings, IsReservedKey_EmptyString_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey(""));
}

// --- Default field values (sanity checks) ---

// Verifies the out-of-box defaults match the hotkey-scheme.md spec.
// Camera pan uses arrow keys, NOT WASD.
TEST(KeyBindings, DefaultCamPanUp_IsArrowUp) {
    KeyBindings kb;
    EXPECT_EQ(kb.camPanUp, "ArrowUp");
}

TEST(KeyBindings, DefaultCamPanDown_IsArrowDown) {
    KeyBindings kb;
    EXPECT_EQ(kb.camPanDown, "ArrowDown");
}

TEST(KeyBindings, DefaultTogglePause_IsSpace) {
    KeyBindings kb;
    EXPECT_EQ(kb.togglePause, "Space");
}

TEST(KeyBindings, DefaultOpenPauseMenu_IsEscape) {
    KeyBindings kb;
    EXPECT_EQ(kb.openPauseMenu, "Escape");
}
