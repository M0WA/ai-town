// font_tier_test.cpp — Phase 11g: FontTier boundary tests.
// Tests IrrlichtUIBackend::selectFontTier() — a pure static function.
// No Irrlicht device or display required; included in ui_tests (unit label).
#include "src/rendering/IrrlichtUIBackend.h"
#include <gtest/gtest.h>

// IrrlichtUIBackend.h forward-declares Irrlicht types only — safe to include
// in unit tests without linking Irrlicht.

using FT = IrrlichtUIBackend::FontTier;

TEST(FontTierSelectionTest, Below720p_Uses720pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(480), FT::k720p);
}

TEST(FontTierSelectionTest, At720p_Uses720pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(720), FT::k720p);
}

TEST(FontTierSelectionTest, At768p_Uses720pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(768), FT::k720p);
}

TEST(FontTierSelectionTest, At899p_Uses720pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(899), FT::k720p);
}

TEST(FontTierSelectionTest, At900p_Uses1080pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(900), FT::k1080p);
}

TEST(FontTierSelectionTest, At1080p_Uses1080pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(1080), FT::k1080p);
}

TEST(FontTierSelectionTest, At1259p_Uses1080pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(1259), FT::k1080p);
}

TEST(FontTierSelectionTest, At1260p_Uses1440pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(1260), FT::k1440p);
}

TEST(FontTierSelectionTest, At1440p_Uses1440pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(1440), FT::k1440p);
}

TEST(FontTierSelectionTest, At2160p_Uses1440pTier) {
    EXPECT_EQ(IrrlichtUIBackend::selectFontTier(2160), FT::k1440p);
}
