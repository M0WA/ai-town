// tests/ui/query_panel_test.cpp
//
// Phase 8 QueryPanel tests -- pure static-function tests for
// InspectorPanel::computePanelPosition() PLUS QueryPanel integration tests.
//
// computePanelPosition is a static pure function. Panel dimensions: 240x160 px.
// Now calls the real InspectorPanel::computePanelPosition (4-parameter form).
//
// InspectorPanel text format (from QueryPanel.cpp):
//   Coordinates: "Tile: (X, Z)"
//   Zone type:   "Residential (Low)" or "Unzoned"
//   Population:  "Pop: 42"
//   Coverage:    "Fire:80% Pol:60% Pwr:100% Wtr:90%"
//   Desirability:"Desirability: 75"
//   Demand:      "Demand: 55%"

#include "src/ui/inspector_panel.h"
#include "src/ui/IUIBackend.h"  // Rect
#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_audio_system.h"
#include "tests/simulation/manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::AtLeast;
using ::testing::AnyNumber;

// ============================================================================
// QueryPanelPosition tests -- pure-function tests for computePanelPosition
// ============================================================================

// --- Test 1: Primary right-below placement ---
TEST(QueryPanelPosition, PrimaryRightBelow_UpperLeftQuadrant) {
    Rect r = InspectorPanel::computePanelPosition(200, 200, 1920, 1080);
    EXPECT_GE(r.x, 200);
    EXPECT_GE(r.y, 200);
    EXPECT_EQ(r.w, 240);
    EXPECT_EQ(r.h, 160);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 2: Fallback left placement ---
TEST(QueryPanelPosition, FallbackLeft_TileInRightHalf) {
    Rect r = InspectorPanel::computePanelPosition(1800, 200, 1920, 1080);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_EQ(r.w, 240);
    EXPECT_EQ(r.h, 160);
}

// --- Test 3: Edge clamping (4 sub-cases) ---
TEST(QueryPanelPosition, EdgeClamping_NearLeftEdge) {
    Rect r = InspectorPanel::computePanelPosition(5, 500, 1920, 1080);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

TEST(QueryPanelPosition, EdgeClamping_NearRightEdge) {
    Rect r = InspectorPanel::computePanelPosition(1915, 500, 1920, 1080);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
}

TEST(QueryPanelPosition, EdgeClamping_NearTopEdge) {
    Rect r = InspectorPanel::computePanelPosition(500, 5, 1920, 1080);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

TEST(QueryPanelPosition, EdgeClamping_NearBottomEdge) {
    Rect r = InspectorPanel::computePanelPosition(500, 1075, 1920, 1080);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 4: Third-fallback edge-snap ---
TEST(QueryPanelPosition, ThirdFallback_EdgeSnap_InsufficientSpaceAllQuadrants) {
    Rect r = InspectorPanel::computePanelPosition(1910, 1070, 1920, 1080);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 5: Zero coordinate click ---
TEST(QueryPanelPosition, ZeroCoordinate_PanelStaysOnScreen) {
    Rect r = InspectorPanel::computePanelPosition(0, 0, 1920, 1080);
    EXPECT_GE(r.x, 0);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 6: Center screen click ---
TEST(QueryPanelPosition, CenterScreen_PanelPlacedRightBelow) {
    Rect r = InspectorPanel::computePanelPosition(960, 540, 1920, 1080);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// ============================================================================
// QueryPanel integration tests with mock backend + mock simulation
// ============================================================================
class QueryPanelIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));

        // Query tile returns zoned data.
        QueryResult qr;
        qr.tileX = 5;
        qr.tileZ = 10;
        qr.isZoned = true;
        qr.zoneType = ZoneType::Residential;
        qr.densityTier = DensityTier::Low;
        qr.population = 42;
        qr.coverage.fire = 80.0f;
        qr.coverage.police = 60.0f;
        qr.coverage.power = 100.0f;
        qr.coverage.water = 90.0f;
        qr.desirability = 75.0f;
        qr.demandPressurePct = 55.0f;
        ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

        panel_ = std::make_unique<InspectorPanel>(&backend_, &sim_);
    }

    void TearDown() override {
        panel_.reset();
    }

    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockCitySimulation> sim_;
    std::unique_ptr<InspectorPanel> panel_;
    uint32_t                     nextHandle_{100};
};

// InspectorPanel shows tile coordinates after show() + draw().
// Format: "Tile: (5, 10)"
TEST_F(QueryPanelIntegrationTest, Show_DisplaysTileCoordinates) {
    panel_->show(5, 10, 200, 200);
    EXPECT_TRUE(panel_->isOpen());

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Tile: (5, 10)"))).Times(AtLeast(1));
    panel_->draw();
}

// InspectorPanel shows zone type.
// Format: "Residential (Low)"
TEST_F(QueryPanelIntegrationTest, Draw_DisplaysZoneType) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Residential"))).Times(AtLeast(1));
    panel_->draw();
}

// InspectorPanel shows population.
// Format: "Pop: 42"
TEST_F(QueryPanelIntegrationTest, Draw_DisplaysPopulation) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Pop: 42"))).Times(AtLeast(1));
    panel_->draw();
}

// InspectorPanel shows service coverage.
// Format: "Fire:80% Pol:60% Pwr:100% Wtr:90%"
TEST_F(QueryPanelIntegrationTest, Draw_DisplaysServiceCoverage) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Fire:80%"))).Times(AtLeast(1));
    panel_->draw();
}

// InspectorPanel shows desirability.
// Format: "Desirability: 75"
TEST_F(QueryPanelIntegrationTest, Draw_DisplaysDesirability) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Desirability: 75"))).Times(AtLeast(1));
    panel_->draw();
}

// InspectorPanel shows demand pressure.
// Format: "Demand: 55%"
TEST_F(QueryPanelIntegrationTest, Draw_DisplaysDemandPressure) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Demand: 55%"))).Times(AtLeast(1));
    panel_->draw();
}

// InspectorPanel shows "Unzoned" for unzoned tiles.
TEST_F(QueryPanelIntegrationTest, Draw_DisplaysUnzoned) {
    QueryResult qr;
    qr.tileX = 5;
    qr.tileZ = 10;
    qr.isZoned = false;
    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, "Unzoned")).Times(AtLeast(1));
    panel_->draw();
}

// Escape closes the panel.
TEST_F(QueryPanelIntegrationTest, Escape_ClosesPanel) {
    panel_->show(5, 10, 200, 200);
    EXPECT_TRUE(panel_->isOpen());

    InputEvent esc;
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = 27;
    bool consumed = panel_->onEvent(esc);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(panel_->isOpen());
}

// Click outside panel dismisses it (not consumed).
TEST_F(QueryPanelIntegrationTest, ClickOutside_DismissesPanel) {
    panel_->show(5, 10, 200, 200);
    EXPECT_TRUE(panel_->isOpen());

    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 1800;
    click.y = 900;
    bool consumed = panel_->onEvent(click);
    EXPECT_FALSE(consumed);
    EXPECT_FALSE(panel_->isOpen());
}

// --- QueryPanel_EscapeFeedbackToast_DoesNotConsumeSubsequentEscape ---
TEST(QueryPanelEscape, EscapeFeedbackToast_DoesNotConsumeSubsequentEscape) {
    NiceMock<MockUIBackend>      backend;
    NiceMock<MockCitySimulation> sim;
    uint32_t nextHandle = 100;

    ON_CALL(backend, addStaticText(_, _, _, _, _)).WillByDefault(
        [&nextHandle](const std::string&, int, int, int, int) { return ++nextHandle; });
    ON_CALL(backend, addButton(_, _, _, _, _)).WillByDefault(
        [&nextHandle](const std::string&, int, int, int, int) { return ++nextHandle; });
    ON_CALL(backend, getVirtualWidth()).WillByDefault(Return(1920));
    ON_CALL(backend, getVirtualHeight()).WillByDefault(Return(1080));

    InspectorPanel panel(&backend, &sim);
    panel.show(5, 10, 200, 200);
    EXPECT_TRUE(panel.isOpen());

    InputEvent esc;
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = 27;

    // First Escape closes the panel (consumed).
    bool consumed1 = panel.onEvent(esc);
    EXPECT_TRUE(consumed1);
    EXPECT_FALSE(panel.isOpen());

    // Second Escape: panel is closed, so not consumed.
    bool consumed2 = panel.onEvent(esc);
    EXPECT_FALSE(consumed2);
}

// Click inside panel is consumed (not dismissed).
TEST_F(QueryPanelIntegrationTest, ClickInside_Consumed) {
    panel_->show(5, 10, 200, 200);
    EXPECT_TRUE(panel_->isOpen());

    // Panel position near (240, 240) for click at 200,200.
    // kPanelW=240, kPanelH=160. Primary: px=200+40=240, py=200+40=240.
    InputEvent click;
    click.type = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x = 260;
    click.y = 260;
    bool consumed = panel_->onEvent(click);
    EXPECT_TRUE(consumed);
    EXPECT_TRUE(panel_->isOpen());
}

// Non-click, non-escape event returns false when panel is open.
TEST_F(QueryPanelIntegrationTest, NonEscapeKey_ReturnsFalse) {
    panel_->show(5, 10, 200, 200);

    InputEvent key;
    key.type = InputEvent::Type::KeyDown;
    key.keyCode = 65; // A key
    bool consumed = panel_->onEvent(key);
    EXPECT_FALSE(consumed);
}

// Commercial zone type coverage.
TEST_F(QueryPanelIntegrationTest, Draw_CommercialZone) {
    QueryResult qr;
    qr.tileX = 3;
    qr.tileZ = 7;
    qr.isZoned = true;
    qr.zoneType = ZoneType::Commercial;
    qr.densityTier = DensityTier::Medium;
    qr.population = 100;
    qr.coverage = {50.0f, 50.0f, 50.0f, 50.0f};
    qr.desirability = 50.0f;
    qr.demandPressurePct = 40.0f;
    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

    panel_->show(3, 7, 500, 500);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Commercial"))).Times(AtLeast(1));
    panel_->draw();
}

// Industrial zone type coverage.
TEST_F(QueryPanelIntegrationTest, Draw_IndustrialZone) {
    QueryResult qr;
    qr.tileX = 8;
    qr.tileZ = 2;
    qr.isZoned = true;
    qr.zoneType = ZoneType::Industrial;
    qr.densityTier = DensityTier::High;
    qr.population = 200;
    qr.coverage = {90.0f, 70.0f, 100.0f, 80.0f};
    qr.desirability = 30.0f;
    qr.demandPressurePct = 70.0f;
    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

    panel_->show(8, 2, 400, 300);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Industrial"))).Times(AtLeast(1));
    panel_->draw();
}

// Third-fallback edge-snap: both primary and fallback overlap.
// Click near bottom-right corner of a very small screen.
TEST(QueryPanelPosition, ThirdFallback_SmallScreen_EdgeSnap) {
    // 320x240 screen with click near lower-right: both primary and fallback overlap.
    Rect r = InspectorPanel::computePanelPosition(150, 120, 320, 240);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 320);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 240);
}

// Edge-snap with click in left half of screen.
TEST(QueryPanelPosition, ThirdFallback_LeftHalf_EdgeSnap) {
    // 480x320 screen, click at center -- forces all quadrants to overlap.
    Rect r = InspectorPanel::computePanelPosition(120, 80, 480, 320);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 480);
}
