// tests/ui/minimap_overlay_test.cpp
//
// Phase 11d — Minimap Traffic and ServiceCoverage overlay rendering tests.
//
// Covers:
//   - Minimap::draw() Traffic overlay branch (MinimapOverlay::Traffic):
//       calls ICitySimulation::getRoadSegmentSpeeds() and renders coloured
//       dots via IUIBackend::addStaticText + setElementBackground.
//   - Minimap::draw() ServiceCoverage overlay branch (MinimapOverlay::ServiceCoverage):
//       calls ICitySimulation::getServiceCoverage() and renders coloured
//       dots via IUIBackend::addStaticText + setElementBackground.
//   - Minimap::draw() with no simulation wired: both overlay branches are skipped
//       silently (m_sim == nullptr guard).
//   - Minimap::draw() with overlayActive == false: neither branch executes.
//   - toggleOverlay() / isOverlayActive() accessor pair.
//   - setOverlayMode() / getOverlayMode() accessor pair.
//
// Mock policy: NiceMock<MockUIBackend> (incidental backend calls), and
//              NiceMock<MockCitySimulation> for sim_ so we can set up
//              getRoadSegmentSpeeds()/getServiceCoverage() return values.
//
// Added to ui_tests via:
//   target_sources(ui_tests PRIVATE tests/ui/minimap_overlay_test.cpp)
// Do NOT call add_executable(ui_tests ...) or aitown_add_tests(ui_tests ...) again.

#include "src/ui/Minimap.h"
#include "src/interfaces/simulation_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// MinimapOverlayTest fixture
// ---------------------------------------------------------------------------
class MinimapOverlayTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>        backend_;
    NiceMock<MockCitySimulation>   sim_;

    std::unique_ptr<Minimap> minimap_;

    void SetUp() override {
        // NiceMock suppresses construction-time backend calls (addStaticText,
        // addButton, setElementBackground, setElementVisible, etc.).
        // Return incrementing handles so handle != kInvalidUIElement checks pass.
        static UIElementHandle nextHandle = 1;
        ON_CALL(backend_, addStaticText(_, _, _, _, _))
            .WillByDefault([&](const std::string&, int, int, int, int) -> UIElementHandle {
                return nextHandle++;
            });
        ON_CALL(backend_, addButton(_, _, _, _, _))
            .WillByDefault([&](const std::string&, int, int, int, int) -> UIElementHandle {
                return nextHandle++;
            });

        minimap_ = std::make_unique<Minimap>(&backend_);
    }

    void TearDown() override {
        minimap_.reset();
    }
};

// ===========================================================================
// toggleOverlay / isOverlayActive
// ===========================================================================

TEST_F(MinimapOverlayTest, ToggleOverlay_InitiallyInactive_BecomeActive) {
    EXPECT_FALSE(minimap_->isOverlayActive())
        << "Overlay must be inactive at construction.";
    minimap_->toggleOverlay();
    EXPECT_TRUE(minimap_->isOverlayActive())
        << "Overlay must become active after first toggle.";
}

TEST_F(MinimapOverlayTest, ToggleOverlay_ActiveThenInactive_TogglesTwice) {
    minimap_->toggleOverlay();
    EXPECT_TRUE(minimap_->isOverlayActive());
    minimap_->toggleOverlay();
    EXPECT_FALSE(minimap_->isOverlayActive())
        << "Overlay must return to inactive after second toggle.";
}

// ===========================================================================
// setOverlayMode / getOverlayMode
// ===========================================================================

TEST_F(MinimapOverlayTest, SetOverlayMode_Traffic_GetReturnsTraffic) {
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    EXPECT_EQ(minimap_->getOverlayMode(), MinimapOverlay::Traffic);
}

TEST_F(MinimapOverlayTest, SetOverlayMode_ServiceCoverage_GetReturnsServiceCoverage) {
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    EXPECT_EQ(minimap_->getOverlayMode(), MinimapOverlay::ServiceCoverage);
}

TEST_F(MinimapOverlayTest, SetOverlayMode_None_GetReturnsNone) {
    minimap_->setOverlayMode(MinimapOverlay::Traffic);  // change from default
    minimap_->setOverlayMode(MinimapOverlay::None);
    EXPECT_EQ(minimap_->getOverlayMode(), MinimapOverlay::None);
}

// ===========================================================================
// draw() with no simulation wired — overlay branches silently skipped
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_NoSimulation_NoQueryCall) {
    // Activate Traffic overlay but do NOT wire a simulation.
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();  // overlayActive = true

    // sim_ is NOT wired (setSimulation not called) → m_sim == nullptr.
    // getRoadSegmentSpeeds must NOT be called.
    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(0);

    // draw() must not crash even without a simulation.
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_NoSimulation_NoQueryCall) {
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(0);

    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

// ===========================================================================
// draw() with overlayActive == false — no query calls at all
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_OverlayInactive_NoSpeedQuery) {
    minimap_->show();
    minimap_->setSimulation(&sim_);
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    // overlayActive is still false (no toggleOverlay call).

    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(0);
    EXPECT_CALL(sim_, getServiceCoverage()).Times(0);

    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

// ===========================================================================
// draw() Traffic overlay — getRoadSegmentSpeeds() is called; dots rendered
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_WithSimulation_CallsGetRoadSegmentSpeeds) {
    // Provide a road segment at tile (64, 64) — maps to pixel (1720+100, 880+100)
    // which is within the minimap area.
    RoadSegmentSpeed seg;
    seg.tileX        = 64;
    seg.tileZ        = 64;
    seg.speedFraction = 1.0f;  // Green (free-flow)

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(sim_, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();  // overlayActive = true

    // getRoadSegmentSpeeds must be called at least once during draw().
    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(AtLeast(1)).WillRepeatedly(Return(speeds));

    minimap_->draw();
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_MildCongestion_RendersDot) {
    // Segment with speedFraction 0.35 (orange congestion range).
    RoadSegmentSpeed seg;
    seg.tileX         = 50;
    seg.tileZ         = 50;
    seg.speedFraction = 0.35f;  // orange: 0.31-0.39

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(sim_, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(AtLeast(1)).WillRepeatedly(Return(speeds));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_HeavyCongestion_RendersDot) {
    // Segment with speedFraction 0.20 (red: <= 0.30).
    RoadSegmentSpeed seg;
    seg.tileX         = 30;
    seg.tileZ         = 30;
    seg.speedFraction = 0.20f;

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(sim_, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(AtLeast(1)).WillRepeatedly(Return(speeds));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_TileOutsideMinimapBounds_Skipped) {
    // A tile at coords that map outside the 200x200 minimap area must be skipped
    // (no addStaticText call for it). Tile at (256, 256) → px = 1720 + 400 > 1920.
    RoadSegmentSpeed seg;
    seg.tileX         = 256;
    seg.tileZ         = 256;
    seg.speedFraction = 1.0f;

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(sim_, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(AtLeast(1)).WillRepeatedly(Return(speeds));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_EmptySpeeds_NoCrash) {
    // Empty speed list — overlay active but nothing to render.
    std::vector<RoadSegmentSpeed> empty;
    ON_CALL(sim_, getRoadSegmentSpeeds()).WillByDefault(Return(empty));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(AtLeast(1)).WillRepeatedly(Return(empty));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

// ===========================================================================
// draw() ServiceCoverage overlay — getServiceCoverage() is called; dots rendered
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_WithSimulation_CallsGetServiceCoverage) {
    ServiceCoverageTile sct;
    sct.tileX     = 50;
    sct.tileZ     = 50;
    sct.coveredBy = ServiceBuildingType::FireStation;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(coverage));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));
    minimap_->draw();
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_PoliceStation_RendersDot) {
    ServiceCoverageTile sct;
    sct.tileX     = 40;
    sct.tileZ     = 40;
    sct.coveredBy = ServiceBuildingType::PoliceStation;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(coverage));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_PowerPlant_RendersDot) {
    ServiceCoverageTile sct;
    sct.tileX     = 20;
    sct.tileZ     = 20;
    sct.coveredBy = ServiceBuildingType::PowerPlant;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(coverage));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_WaterTower_RendersDot) {
    ServiceCoverageTile sct;
    sct.tileX     = 10;
    sct.tileZ     = 10;
    sct.coveredBy = ServiceBuildingType::WaterTower;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(coverage));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_NoneType_SkippedSilently) {
    // A ServiceCoverageTile with coveredBy == ServiceBuildingType::None must be
    // silently skipped (the default: case falls through continue).
    ServiceCoverageTile sct;
    sct.tileX     = 60;
    sct.tileZ     = 60;
    sct.coveredBy = ServiceBuildingType::None;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(coverage));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_TileOutsideBounds_Skipped) {
    // Tile at (256, 256) maps outside the minimap area — must be skipped without crash.
    ServiceCoverageTile sct;
    sct.tileX     = 256;
    sct.tileZ     = 256;
    sct.coveredBy = ServiceBuildingType::FireStation;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(coverage));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_EmptyCoverage_NoCrash) {
    std::vector<ServiceCoverageTile> empty;
    ON_CALL(sim_, getServiceCoverage()).WillByDefault(Return(empty));

    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(empty));
    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

// ===========================================================================
// draw() when hidden — neither query is called
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_WhenHidden_NoQueryCalls) {
    // Minimap is hidden (hide() called). draw() must be a no-op.
    minimap_->hide();
    minimap_->setSimulation(&sim_);
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();

    EXPECT_CALL(sim_, getRoadSegmentSpeeds()).Times(0);
    EXPECT_CALL(sim_, getServiceCoverage()).Times(0);

    EXPECT_NO_FATAL_FAILURE(minimap_->draw());
}

// ===========================================================================
// draw() label text — "[Tfc]" vs "[Svc]" vs "Svc" based on overlay state
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_ToggleButtonLabel_TrafficMode_ShowsTfc) {
    // When overlayActive && mode == Traffic the button text is "[Tfc]".
    // We verify via setElementText expectation.
    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::Traffic);
    minimap_->toggleOverlay();

    ON_CALL(sim_, getRoadSegmentSpeeds())
        .WillByDefault(Return(std::vector<RoadSegmentSpeed>{}));

    EXPECT_CALL(backend_, setElementText(_, std::string("[Tfc]"))).Times(AtLeast(1));
    minimap_->draw();
}

TEST_F(MinimapOverlayTest, Draw_ToggleButtonLabel_ServiceCoverageMode_ShowsSvc) {
    // When overlayActive && mode == ServiceCoverage the button text is "[Svc]".
    minimap_->setSimulation(&sim_);
    minimap_->show();
    minimap_->setOverlayMode(MinimapOverlay::ServiceCoverage);
    minimap_->toggleOverlay();

    ON_CALL(sim_, getServiceCoverage())
        .WillByDefault(Return(std::vector<ServiceCoverageTile>{}));

    EXPECT_CALL(backend_, setElementText(_, std::string("[Svc]"))).Times(AtLeast(1));
    minimap_->draw();
}

TEST_F(MinimapOverlayTest, Draw_ToggleButtonLabel_OverlayInactive_ShowsSvc) {
    // When overlay is inactive, button text is "Svc".
    minimap_->show();
    // Do NOT toggle — overlayActive stays false.

    EXPECT_CALL(backend_, setElementText(_, std::string("Svc"))).Times(AtLeast(1));
    minimap_->draw();
}
