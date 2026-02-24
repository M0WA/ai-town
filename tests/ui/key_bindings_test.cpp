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

TEST(KeyBindingsTest, IsReservedKey_Q_ReturnsTrue) {
    KeyBindings kb;
    EXPECT_TRUE(kb.isReservedKey("Q"));
}

TEST(KeyBindingsTest, IsReservedKey_E_ReturnsTrue) {
    KeyBindings kb;
    EXPECT_TRUE(kb.isReservedKey("E"));
}

// --- Non-reserved keys (must return false) ---

// W is a common WASD key; must not be reserved so players can rebind camera pan.
TEST(KeyBindingsTest, IsReservedKey_W_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("W"));
}

TEST(KeyBindingsTest, IsReservedKey_A_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("A"));
}

TEST(KeyBindingsTest, IsReservedKey_S_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("S"));
}

TEST(KeyBindingsTest, IsReservedKey_D_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("D"));
}

// Default tool hotkeys must not be inadvertently reserved.
TEST(KeyBindingsTest, IsReservedKey_Z_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("Z"));  // toolZone default
}

TEST(KeyBindingsTest, IsReservedKey_R_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("R"));  // toolRoad default
}

TEST(KeyBindingsTest, IsReservedKey_T_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("T"));  // toggleTaxPanel default
}

TEST(KeyBindingsTest, IsReservedKey_Space_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("Space"));  // togglePause default
}

// Lowercase variants are distinct strings — isReservedKey() is case-sensitive
// (the implementation uses exact string comparison: key == "Q" || key == "E").
TEST(KeyBindingsTest, IsReservedKey_LowercaseQ_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("q"));
}

TEST(KeyBindingsTest, IsReservedKey_LowercaseE_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("e"));
}

// Empty string must not be considered reserved.
TEST(KeyBindingsTest, IsReservedKey_EmptyString_ReturnsFalse) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey(""));
}

// --- Default field values (sanity checks) ---

// Verifies the out-of-box defaults match the hotkey-scheme.md spec.
// Camera pan uses arrow keys, NOT WASD.
TEST(KeyBindingsTest, DefaultCamPanUp_IsArrowUp) {
    KeyBindings kb;
    EXPECT_EQ(kb.camPanUp, "ArrowUp");
}

TEST(KeyBindingsTest, DefaultCamPanDown_IsArrowDown) {
    KeyBindings kb;
    EXPECT_EQ(kb.camPanDown, "ArrowDown");
}

TEST(KeyBindingsTest, DefaultTogglePause_IsSpace) {
    KeyBindings kb;
    EXPECT_EQ(kb.togglePause, "Space");
}

TEST(KeyBindingsTest, DefaultOpenPauseMenu_IsEscape) {
    KeyBindings kb;
    EXPECT_EQ(kb.openPauseMenu, "Escape");
}
