// tests/ui/minimap_overlay_test.cpp
//
// Phase 11d/11p -- Minimap overlay rendering tests.
//
// Covers:
//   MM-13c: Fixture migration (renamed members, 4-param constructor, ManualClock,
//           MinimapNoSimTest for null-sim tests)
//   MM-33:  Zone color coding (9 tests)
//   MM-34:  Road network rendering (4 tests)
//   MM-35:  Camera viewport rectangle (5 tests)
//   MM-36:  Click-to-pan (6 tests)
//   MM-37:  Overlay toggle radio behavior (7 tests)
//   MM-38:  Label strip (7 tests)
//   MM-39:  Legend panel (13 tests)
//   MM-40:  Budget-tick cadence (4 tests)
//   MM-41:  Input footprint (8 tests)
//   MM-42:  Element leak regression (5 tests)
//   MM-43:  Overlay tile rendering (11 tests)
//   MM-44:  Coordinate mapping direction verification (4 tests)
//           (East tile appears LEFT of centre — Irrlicht LH: camera right = -X)
//
// Mock policy: NiceMock<MockUIBackend> (incidental backend calls), and
//              NiceMock<MockCitySimulation> for m_sim.

#include "src/ui/Minimap.h"
#include "src/interfaces/simulation_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// MinimapOverlayTest fixture -- m_sim wired in constructor (4-param)
// ---------------------------------------------------------------------------
class MinimapOverlayTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>        m_backend;
    NiceMock<MockCitySimulation>   m_sim;
    ManualClock                    m_clock;

    std::unique_ptr<Minimap> m_minimap;

    // Element handles assigned by the constructor, tracked for EXPECT_CALL on specific handles.
    UIElementHandle m_handle_mapBg{0};
    UIElementHandle m_handle_toggleBtnSvc{0};
    UIElementHandle m_handle_toggleBtnTfc{0};
    UIElementHandle m_handle_labelStrip{0};
    UIElementHandle m_handle_legendPanel{0};
    UIElementHandle m_handle_legendLabel{0};

    // Counters must be members so lambda captures stay valid after SetUp() returns.
    int m_staticTextCount{0};
    int m_buttonCount{0};

    void SetUp() override {
        m_staticTextCount = 0;
        m_buttonCount = 0;

        // Track element creation order to map handles to member variables.
        // Constructor order: addStaticText(mapBg), addButton(Svc), addButton(Tfc),
        //                    addStaticText(labelStrip), addStaticText(legendPanel),
        //                    addStaticText(legendLabel).
        ON_CALL(m_backend, addStaticText(_, _, _, _, _))
            .WillByDefault([this](const std::string&, int, int, int, int) -> UIElementHandle {
                m_staticTextCount++;
                // Use a deterministic handle based on type+count so it is stable per test.
                UIElementHandle h = static_cast<UIElementHandle>(100 + m_staticTextCount);
                switch (m_staticTextCount) {
                    case 1: m_handle_mapBg = h; break;
                    case 2: m_handle_labelStrip = h; break;
                    case 3: m_handle_legendPanel = h; break;
                    case 4: m_handle_legendLabel = h; break;
                    default: break;
                }
                return h;
            });
        ON_CALL(m_backend, addButton(_, _, _, _, _))
            .WillByDefault([this](const std::string&, int, int, int, int) -> UIElementHandle {
                m_buttonCount++;
                UIElementHandle h = static_cast<UIElementHandle>(200 + m_buttonCount);
                switch (m_buttonCount) {
                    case 1: m_handle_toggleBtnSvc = h; break;
                    case 2: m_handle_toggleBtnTfc = h; break;
                    default: break;
                }
                return h;
            });

        ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(64));
        ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(64));
        ON_CALL(m_sim, consumeBudgetTicks()).WillByDefault(Return(0));

        m_minimap = std::make_unique<Minimap>(&m_backend, nullptr, &m_sim, &m_clock);
    }

    void TearDown() override {
        // Reset before mock destructors run to prevent order-of-destruction issues.
        m_minimap.reset();
    }

    // Convenience: stub all tiles to return a specific QueryResult.
    void stubAllTiles(const QueryResult& tile) {
        ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(tile));
    }

    // Convenience: prepare cache and draw.
    void tickAndDraw() {
        m_minimap->onBudgetTicks(1);
        m_minimap->draw();
    }

    // Convenience: simulate a left-click at (x, y).
    void clickAt(int x, int y) {
        InputEvent ev;
        ev.type = InputEvent::Type::MouseButtonDown;
        ev.button = 0;
        ev.x = x;
        ev.y = y;
        m_minimap->onEvent(ev);
    }

    // Convenience: allow all fillColoredRect calls (catch-all) so that
    // specific EXPECT_CALL matchers don't cause unexpected-call failures
    // for viewport-outline or other-color calls.
    // Must be called BEFORE setting specific EXPECT_CALL matchers (GMock
    // matches in reverse order: last-added wins for matching, first-added
    // is fallback).
    void allowAllFillColoredRect() {
        EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(AnyNumber());
    }
};

// ---------------------------------------------------------------------------
// MinimapNoSimTest fixture -- null sim (3rd param = nullptr)
// ---------------------------------------------------------------------------
class MinimapNoSimTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      m_backend;
    ManualClock                  m_clock;
    std::unique_ptr<Minimap>     m_minimap;

    void SetUp() override {
        ON_CALL(m_backend, addStaticText(_, _, _, _, _))
            .WillByDefault([](const std::string&, int, int, int, int) -> UIElementHandle {
                static UIElementHandle h = 500;
                return h++;
            });
        ON_CALL(m_backend, addButton(_, _, _, _, _))
            .WillByDefault([](const std::string&, int, int, int, int) -> UIElementHandle {
                static UIElementHandle h = 600;
                return h++;
            });

        m_minimap = std::make_unique<Minimap>(&m_backend, nullptr, nullptr, &m_clock);
    }
    void TearDown() override { m_minimap.reset(); }
};

// ===========================================================================
// Phase 11d migrated tests: toggleOverlay / isOverlayActive (2 tests)
// ===========================================================================

TEST_F(MinimapOverlayTest, ToggleOverlay_InitiallyInactive_BecomeActive) {
    EXPECT_FALSE(m_minimap->isOverlayActive())
        << "Overlay must be inactive at construction.";
    m_minimap->toggleOverlay();
    EXPECT_TRUE(m_minimap->isOverlayActive())
        << "Overlay must become active after first toggle.";
}

TEST_F(MinimapOverlayTest, ToggleOverlay_ActiveThenInactive_TogglesTwice) {
    m_minimap->toggleOverlay();
    EXPECT_TRUE(m_minimap->isOverlayActive());
    m_minimap->toggleOverlay();
    EXPECT_FALSE(m_minimap->isOverlayActive())
        << "Overlay must return to inactive after second toggle.";
}

// ===========================================================================
// Phase 11d migrated: setOverlayMode / getOverlayMode (3 tests)
// Null-sim test moved to MinimapNoSimTest below.
// ===========================================================================

TEST_F(MinimapOverlayTest, SetOverlayMode_Traffic_WithSim_GetReturnsTraffic) {
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::Traffic);
}

TEST_F(MinimapOverlayTest, SetOverlayMode_ServiceCoverage_GetReturnsServiceCoverage) {
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::ServiceCoverage);
}

TEST_F(MinimapOverlayTest, SetOverlayMode_None_GetReturnsNone) {
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->setOverlayMode(MinimapOverlay::None);
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::None);
}

// ===========================================================================
// Phase 11d migrated: draw() with overlayActive == false
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_OverlayInactive_NoSpeedQuery) {
    // show() sets m_pendingTicks=1 for an immediate first-frame cache refresh.
    // That first draw() queries all sim caches (tiles, service coverage, road speeds)
    // once.  After the initial refresh pendingTicks resets to 0, so a second draw()
    // without another budget tick must make no further sim queries.
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    // overlayActive is still false (no toggleOverlay call).

    // First draw: consume the show()-injected tick (one-time initial refresh).
    EXPECT_NO_FATAL_FAILURE(m_minimap->draw());

    // Second draw without budget tick: no further sim queries expected.
    EXPECT_CALL(m_sim, getRoadSegmentSpeeds()).Times(0);
    EXPECT_CALL(m_sim, getServiceCoverage()).Times(0);
    EXPECT_NO_FATAL_FAILURE(m_minimap->draw());
}

// ===========================================================================
// Phase 11d migrated: draw() + drawOverlay() Traffic overlay (5 tests)
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_WithSimulation_CallsGetRoadSegmentSpeeds) {
    RoadSegmentSpeed seg;
    seg.tileX         = 32;
    seg.tileZ         = 32;
    seg.speedFraction = 1.0f;

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    EXPECT_CALL(m_sim, getRoadSegmentSpeeds()).Times(AtLeast(1)).WillRepeatedly(Return(speeds));

    tickAndDraw();
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_MildCongestion_RendersDot) {
    RoadSegmentSpeed seg;
    seg.tileX         = 25;
    seg.tileZ         = 25;
    seg.speedFraction = 0.35f;

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_HeavyCongestion_RendersDot) {
    RoadSegmentSpeed seg;
    seg.tileX         = 15;
    seg.tileZ         = 15;
    seg.speedFraction = 0.20f;

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_TileOutsideMinimapBounds_Skipped) {
    RoadSegmentSpeed seg;
    seg.tileX         = 256;
    seg.tileZ         = 256;
    seg.speedFraction = 1.0f;

    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_TrafficOverlay_EmptySpeeds_NoCrash) {
    std::vector<RoadSegmentSpeed> empty;
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(empty));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

// ===========================================================================
// Phase 11d migrated: draw() + drawOverlay() ServiceCoverage overlay (7 tests)
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_WithSimulation_CallsGetServiceCoverage) {
    ServiceCoverageTile sct;
    sct.tileX     = 25;
    sct.tileZ     = 25;
    sct.coveredBy = ServiceBuildingType::FireStation;
    sct.degraded  = false;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    EXPECT_CALL(m_sim, getServiceCoverage()).Times(AtLeast(1)).WillRepeatedly(Return(coverage));

    tickAndDraw();
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_PoliceStation_RendersDot) {
    ServiceCoverageTile sct;
    sct.tileX     = 20;
    sct.tileZ     = 20;
    sct.coveredBy = ServiceBuildingType::PoliceStation;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_PowerPlant_RendersDot) {
    ServiceCoverageTile sct;
    sct.tileX     = 10;
    sct.tileZ     = 10;
    sct.coveredBy = ServiceBuildingType::PowerPlant;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_WaterTower_RendersDot) {
    ServiceCoverageTile sct;
    sct.tileX     = 5;
    sct.tileZ     = 5;
    sct.coveredBy = ServiceBuildingType::WaterTower;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_NoneType_SkippedSilently) {
    ServiceCoverageTile sct;
    sct.tileX     = 30;
    sct.tileZ     = 30;
    sct.coveredBy = ServiceBuildingType::None;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_TileOutsideBounds_Skipped) {
    ServiceCoverageTile sct;
    sct.tileX     = 256;
    sct.tileZ     = 256;
    sct.coveredBy = ServiceBuildingType::FireStation;

    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

TEST_F(MinimapOverlayTest, Draw_ServiceCoverageOverlay_EmptyCoverage_NoCrash) {
    std::vector<ServiceCoverageTile> empty;
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(empty));
    stubAllTiles(QueryResult{});

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    tickAndDraw();
    EXPECT_NO_FATAL_FAILURE(m_minimap->drawOverlay());
}

// ===========================================================================
// Phase 11d migrated: draw() when hidden
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_WhenHidden_NoQueryCalls) {
    m_minimap->hide();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    EXPECT_CALL(m_sim, getRoadSegmentSpeeds()).Times(0);
    EXPECT_CALL(m_sim, getServiceCoverage()).Times(0);

    m_minimap->onBudgetTicks(1);
    EXPECT_NO_FATAL_FAILURE(m_minimap->draw());
}

// ===========================================================================
// Phase 11d migrated: draw() legend text (3 tests)
// ===========================================================================

TEST_F(MinimapOverlayTest, Draw_ToggleButtonLabel_TrafficMode_ShowsTfc) {
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();

    bool foundTrafficLabel = false;
    ON_CALL(m_backend, setElementText(_, _))
        .WillByDefault([&](UIElementHandle, const std::string& text) {
            if (text == "Traffic Congestion") foundTrafficLabel = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(foundTrafficLabel) << "Expected label strip text 'Traffic Congestion'";
}

TEST_F(MinimapOverlayTest, Draw_ToggleButtonLabel_ServiceCoverageMode_ShowsSvc) {
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    bool foundSvcLabel = false;
    ON_CALL(m_backend, setElementText(_, _))
        .WillByDefault([&](UIElementHandle, const std::string& text) {
            if (text == "Service Coverage") foundSvcLabel = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(foundSvcLabel) << "Expected label strip text 'Service Coverage'";
}

TEST_F(MinimapOverlayTest, Draw_ToggleButtonLabel_OverlayInactive_ShowsSvc) {
    m_minimap->show();
    // Overlay is inactive (default) -- legend should be hidden, no label text set.
    EXPECT_NO_FATAL_FAILURE(m_minimap->draw());
}

// ===========================================================================
// Phase 11d migrated: 3 null-sim tests -> MinimapNoSimTest
// ===========================================================================

TEST_F(MinimapNoSimTest, SetOverlayMode_Traffic_NoSim_FallsBackToNone) {
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::None)
        << "Traffic mode must fall back to None when m_sim == nullptr (D-17 guard).";
}

TEST_F(MinimapNoSimTest, Draw_TrafficOverlay_NoSimulation_NoQueryCall) {
    m_minimap->show();
    // Traffic falls back to None since sim is null, but we test the draw path is safe.
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    m_minimap->onBudgetTicks(1);
    EXPECT_NO_FATAL_FAILURE(m_minimap->draw());
}

TEST_F(MinimapNoSimTest, Draw_ServiceCoverageOverlay_NoSimulation_NoQueryCall) {
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    m_minimap->onBudgetTicks(1);
    EXPECT_NO_FATAL_FAILURE(m_minimap->draw());
}

// ###########################################################################
// MM-33: Group 1 -- Zone color coding (9 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_Residential_EmitsGreenRect) {
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Residential;
    stubAllTiles(tile);

    m_minimap->show();
    tickAndDraw();

    // Catch-all first, then specific expectation (GMock matches in reverse order).
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_Commercial_EmitsBlueRect) {
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Commercial;
    stubAllTiles(tile);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x29, 0x80, 0xB9, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_Industrial_EmitsYellowRect) {
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Industrial;
    stubAllTiles(tile);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xF3, 0x9C, 0x12, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_Unzoned_NoZoneColorCalls) {
    QueryResult unzoned;
    unzoned.isZoned = false;
    stubAllTiles(unzoned);

    m_minimap->show();
    tickAndDraw();

    // Allow viewport white strips and any other non-zone colors.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(0);
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x29, 0x80, 0xB9, _)).Times(0);
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xF3, 0x9C, 0x12, _)).Times(0);
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_MixedZones_BothColorsEmitted) {
    ON_CALL(m_sim, queryTile(_, _))
        .WillByDefault([](int x, int z) -> QueryResult {
            QueryResult tile;
            tile.isZoned = true;
            tile.zoneType = ((x + z) % 2 == 0) ? ZoneType::Residential : ZoneType::Commercial;
            return tile;
        });

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x29, 0x80, 0xB9, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_RoadTile_NoZoneColor) {
    QueryResult road;
    road.isRoad  = true;
    road.isZoned = true;
    road.zoneType = ZoneType::Residential;
    stubAllTiles(road);

    m_minimap->show();
    tickAndDraw();

    // Road takes priority over zone color.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(0);
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x29, 0x80, 0xB9, _)).Times(0);
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xF3, 0x9C, 0x12, _)).Times(0);
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_ZoneAndRoadMixed) {
    ON_CALL(m_sim, queryTile(_, _))
        .WillByDefault([](int x, int z) -> QueryResult {
            QueryResult tile;
            if ((x + z) % 2 == 0) {
                tile.isRoad  = true;
                tile.isZoned = false;
            } else {
                tile.isRoad  = false;
                tile.isZoned = true;
                tile.zoneType = ZoneType::Residential;
            }
            return tile;
        });

    m_minimap->show();
    tickAndDraw();

    // Both road grey and residential green should appear.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x7F, 0x8C, 0x8D, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_Hidden_ZeroFillCalls) {
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Residential;
    stubAllTiles(tile);

    m_minimap->hide();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(0);
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_ZoneColor_AllThreeZoneTypes) {
    ON_CALL(m_sim, queryTile(_, _))
        .WillByDefault([](int x, int z) -> QueryResult {
            QueryResult tile;
            tile.isZoned = true;
            int mod = (x + z * 64) % 3;
            if (mod == 0) tile.zoneType = ZoneType::Residential;
            else if (mod == 1) tile.zoneType = ZoneType::Commercial;
            else tile.zoneType = ZoneType::Industrial;
            return tile;
        });

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x29, 0x80, 0xB9, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xF3, 0x9C, 0x12, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// ###########################################################################
// MM-34: Group 2 -- Road network rendering (4 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, DrawOverlay_RoadTile_EmitsGreyRect) {
    QueryResult road;
    road.isRoad = true;
    stubAllTiles(road);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x7F, 0x8C, 0x8D, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_NonRoad_NoGreyRect) {
    QueryResult tile;
    tile.isRoad  = false;
    tile.isZoned = false;
    stubAllTiles(tile);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x7F, 0x8C, 0x8D, _)).Times(0);
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_MapNoRoads_ZeroRoadColorCalls) {
    QueryResult unzoned;
    unzoned.isRoad  = false;
    unzoned.isZoned = false;
    stubAllTiles(unzoned);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x7F, 0x8C, 0x8D, _)).Times(0);
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_MapOnlyRoads_NoZoneColorCalls) {
    QueryResult road;
    road.isRoad  = true;
    road.isZoned = false;
    stubAllTiles(road);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x7F, 0x8C, 0x8D, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(0);
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x29, 0x80, 0xB9, _)).Times(0);
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xF3, 0x9C, 0x12, _)).Times(0);
    m_minimap->drawOverlay();
}

// ###########################################################################
// MM-35: Group 3 -- Camera viewport rectangle (5 tests)
// kMaxZoomDistance = 800.0f (from CameraController.h)
// Viewport formula: side = 200 * (zoomDistance / 800), clamped [8, 190]
// ###########################################################################

TEST_F(MinimapOverlayTest, DrawOverlay_CameraViewport_DefaultState_EmitsWhiteStrips) {
    stubAllTiles(QueryResult{});

    CameraState cs;
    cs.targetX = 320.f;
    cs.targetZ = 320.f;
    cs.zoomDistance = 200.f;
    m_minimap->setCameraState(cs);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 255, 255, 255, 200)).Times(AtLeast(4));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_CameraViewport_SmallZoom_Clamped8px) {
    stubAllTiles(QueryResult{});

    CameraState cs;
    cs.targetX = 320.f;
    cs.targetZ = 320.f;
    cs.zoomDistance = 1.f;  // side = 200 * (1/800) = 0.25 -> clamped to 8
    m_minimap->setCameraState(cs);

    m_minimap->show();
    tickAndDraw();

    // Verify at least 4 white viewport strips emitted (top, bottom, left, right).
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 255, 255, 255, 200)).Times(AtLeast(4));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_CameraViewport_LargeZoom_Clamped190px) {
    stubAllTiles(QueryResult{});

    CameraState cs;
    cs.targetX = 320.f;
    cs.targetZ = 320.f;
    cs.zoomDistance = 5000.f;  // side = 200 * (5000/800) = 1250 -> clamped to 190
    m_minimap->setCameraState(cs);

    m_minimap->show();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 255, 255, 255, 200)).Times(AtLeast(4));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_CameraViewport_NeverSet_NoCrash) {
    stubAllTiles(QueryResult{});

    // setCameraState() never called; default CameraState{} used.
    m_minimap->show();
    tickAndDraw();

    // Default zoomDistance=200 => side=50, should still emit viewport.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 255, 255, 255, 200)).Times(AtLeast(4));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, DrawOverlay_CameraViewport_CenterTarget_ViewportCentered) {
    stubAllTiles(QueryResult{});

    // Map center: 64 tiles * 10.0 kTileSize = 640; center = 320
    CameraState cs;
    cs.targetX = 320.f;
    cs.targetZ = 320.f;
    cs.zoomDistance = 400.f;  // side = 200 * (400/800) = 100
    m_minimap->setCameraState(cs);

    m_minimap->show();
    tickAndDraw();

    // cx = (320/640)*200 = 100, cz = 100
    // rectX = 1720 + (100 - 50) = 1770
    // rectY = 880  + (100 - 50) = 930
    // Verify top strip at those coords.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1770, 930, 100, 2, 255, 255, 255, 200)).Times(1);
    m_minimap->drawOverlay();
}

// ###########################################################################
// MM-36: Group 4 -- Click-to-pan (6 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, ClickToPan_InsideRenderArea_FiresCallback) {
    stubAllTiles(QueryResult{});

    bool callbackFired = false;
    m_minimap->setPanCallback([&](float, float) { callbackFired = true; });
    m_minimap->show();

    clickAt(1820, 980);  // inside 1720-1920, 880-1080
    EXPECT_TRUE(callbackFired);
}

TEST_F(MinimapOverlayTest, ClickToPan_OutsideRenderArea_NoCallback) {
    stubAllTiles(QueryResult{});

    bool callbackFired = false;
    m_minimap->setPanCallback([&](float, float) { callbackFired = true; });
    m_minimap->show();

    clickAt(500, 500);  // outside minimap
    EXPECT_FALSE(callbackFired);
}

TEST_F(MinimapOverlayTest, ClickToPan_MinimapCornerBottomRight_FiresCallback) {
    stubAllTiles(QueryResult{});

    bool callbackFired = false;
    m_minimap->setPanCallback([&](float, float) { callbackFired = true; });
    m_minimap->show();

    // Bottom-right corner is within the inclusive range check (mx <= kMapX + kMapW)
    clickAt(1920, 1080);
    EXPECT_TRUE(callbackFired);
}

TEST_F(MinimapOverlayTest, ClickToPan_NoCallbackRegistered_NoCrash) {
    stubAllTiles(QueryResult{});
    m_minimap->show();

    // No setPanCallback() called.
    EXPECT_NO_FATAL_FAILURE(clickAt(1820, 980));
}

TEST_F(MinimapOverlayTest, ClickToPan_WorldCoordinatesProportional) {
    stubAllTiles(QueryResult{});

    float receivedWx = -1.f, receivedWz = -1.f;
    m_minimap->setPanCallback([&](float wx, float wz) {
        receivedWx = wx;
        receivedWz = wz;
    });

    // Phase 11q6: camera-centred minimap — set camera target to world centre.
    CameraState cs;
    cs.targetX = 320.f;
    cs.targetZ = 320.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();

    // Click at center of minimap (1820 = 1720 + 100, 980 = 880 + 100)
    // offX = 0, offZ = 0 => pan to camera target (320, 320)
    clickAt(1820, 980);

    EXPECT_FLOAT_EQ(receivedWx, 320.f);
    EXPECT_FLOAT_EQ(receivedWz, 320.f);
}

TEST_F(MinimapOverlayTest, ClickToPan_TopLeftCorner_FiresCallback) {
    stubAllTiles(QueryResult{});

    float receivedWx = -1.f, receivedWz = -1.f;
    m_minimap->setPanCallback([&](float wx, float wz) {
        receivedWx = wx;
        receivedWz = wz;
    });

    // Phase 11q6: camera-centred minimap — set camera target to world centre.
    CameraState cs;
    cs.targetX = 320.f;
    cs.targetZ = 320.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();

    // y=881 avoids the Svc button hit area (y:848-880 is consumed by button handler).
    // offX = (1720 - 1820) / (200/640) = -320
    // offZ = (881 - 980) / (200/640) = -316.8
    // Irrlicht LH X-flip: worldOffX = -offX*cosYaw + offZ*sinYaw = 320
    // panTo(320 + 320, 320 + (-316.8)) = (640, 3.2)
    clickAt(1720, 881);
    EXPECT_NEAR(receivedWx, 640.f, 0.1f);
    EXPECT_NEAR(receivedWz, 3.2f, 0.1f);
}

// ###########################################################################
// MM-37: Group 5 -- Overlay toggle radio behavior (7 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, OverlayToggle_InitialState_NoOverlayActive) {
    EXPECT_FALSE(m_minimap->isOverlayActive());
}

TEST_F(MinimapOverlayTest, OverlayToggle_ClickSvcButton_SvcOverlayActivates) {
    m_minimap->show();

    clickAt(1736, 864);  // center of Svc button (1720-1752, 848-880)
    EXPECT_TRUE(m_minimap->isOverlayActive());
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::ServiceCoverage);
}

TEST_F(MinimapOverlayTest, OverlayToggle_ClickActiveSvcButton_Deactivates) {
    m_minimap->show();

    clickAt(1736, 864);  // activate Svc
    EXPECT_TRUE(m_minimap->isOverlayActive());

    clickAt(1736, 864);  // click again to deactivate
    EXPECT_FALSE(m_minimap->isOverlayActive());
}

TEST_F(MinimapOverlayTest, OverlayToggle_ClickTfcButton_TfcOverlayActivates) {
    m_minimap->show();

    clickAt(1700, 864);  // center of Tfc button (1684-1716, 848-880)
    EXPECT_TRUE(m_minimap->isOverlayActive());
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::Traffic);
}

TEST_F(MinimapOverlayTest, OverlayToggle_SvcThenTfc_OnlyOneActive) {
    m_minimap->show();

    clickAt(1736, 864);  // activate Svc
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::ServiceCoverage);

    clickAt(1700, 864);  // activate Tfc (should switch from Svc)
    EXPECT_TRUE(m_minimap->isOverlayActive());
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::Traffic);
}

TEST_F(MinimapOverlayTest, OverlayToggle_SvcActivated_SetElementAlpha_1f) {
    m_minimap->show();
    clickAt(1736, 864);  // activate Svc

    EXPECT_CALL(m_backend, setElementAlpha(m_handle_toggleBtnSvc, 1.0f)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, setElementAlpha(m_handle_toggleBtnTfc, 0.65f)).Times(AtLeast(1));
    m_minimap->draw();
}

TEST_F(MinimapOverlayTest, OverlayToggle_SvcDeactivated_SetElementAlpha_065f) {
    m_minimap->show();
    clickAt(1736, 864);  // activate
    clickAt(1736, 864);  // deactivate

    EXPECT_CALL(m_backend, setElementAlpha(m_handle_toggleBtnSvc, 0.65f)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, setElementAlpha(m_handle_toggleBtnTfc, 0.65f)).Times(AtLeast(1));
    m_minimap->draw();
}

// ###########################################################################
// MM-38: Group 6 -- Label strip (7 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, LabelStrip_NoOverlay_Hidden) {
    m_minimap->show();

    bool labelStripHidden = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_labelStrip && !visible) labelStripHidden = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(labelStripHidden) << "Label strip should be hidden when no overlay active.";
}

TEST_F(MinimapOverlayTest, LabelStrip_SvcOverlayActive_Visible) {
    m_minimap->show();
    clickAt(1736, 864);  // activate Svc

    bool labelStripVisible = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_labelStrip && visible) labelStripVisible = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(labelStripVisible) << "Label strip should be visible when Svc overlay active.";
}

TEST_F(MinimapOverlayTest, LabelStrip_TfcOverlayActive_Visible) {
    m_minimap->show();
    clickAt(1700, 864);  // activate Tfc

    bool labelStripVisible = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_labelStrip && visible) labelStripVisible = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(labelStripVisible) << "Label strip should be visible when Tfc overlay active.";
}

TEST_F(MinimapOverlayTest, LabelStrip_SvcOverlay_TextServiceCoverage) {
    m_minimap->show();
    clickAt(1736, 864);  // activate Svc

    bool foundLabel = false;
    ON_CALL(m_backend, setElementText(_, _))
        .WillByDefault([&](UIElementHandle h, const std::string& text) {
            if (h == m_handle_labelStrip && text == "Service Coverage")
                foundLabel = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(foundLabel) << "Label strip text should be 'Service Coverage'";
}

TEST_F(MinimapOverlayTest, LabelStrip_TfcOverlay_TextTrafficCongestion) {
    m_minimap->show();
    clickAt(1700, 864);  // activate Tfc

    bool foundLabel = false;
    ON_CALL(m_backend, setElementText(_, _))
        .WillByDefault([&](UIElementHandle h, const std::string& text) {
            if (h == m_handle_labelStrip && text == "Traffic Congestion")
                foundLabel = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(foundLabel) << "Label strip text should be 'Traffic Congestion'";
}

TEST_F(MinimapOverlayTest, LabelStrip_OverlayDeactivated_Hidden) {
    m_minimap->show();
    clickAt(1736, 864);  // activate Svc
    clickAt(1736, 864);  // deactivate

    bool labelStripHidden = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_labelStrip && !visible) labelStripHidden = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(labelStripHidden) << "Label strip should be hidden after overlay deactivated.";
}

TEST_F(MinimapOverlayTest, LabelStrip_Position_y832) {
    // The label strip is created at y:832 during construction.
    // Verify it was created (handle is non-zero).
    EXPECT_NE(m_handle_labelStrip, static_cast<UIElementHandle>(0));
}

// ###########################################################################
// MM-39: Group 7 -- Legend panel (13 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, LegendPanel_NoOverlay_Hidden) {
    m_minimap->show();

    bool legendHidden = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_legendPanel && !visible) legendHidden = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(legendHidden) << "Legend panel should be hidden when no overlay active.";
}

TEST_F(MinimapOverlayTest, LegendPanel_SvcOverlayActive_Visible) {
    m_minimap->show();
    clickAt(1736, 864);

    bool legendVisible = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_legendPanel && visible) legendVisible = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(legendVisible) << "Legend panel should be visible when Svc overlay active.";
}

TEST_F(MinimapOverlayTest, LegendPanel_TfcOverlayActive_Visible) {
    m_minimap->show();
    clickAt(1700, 864);

    bool legendVisible = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_legendPanel && visible) legendVisible = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(legendVisible) << "Legend panel should be visible when Tfc overlay active.";
}

TEST_F(MinimapOverlayTest, LegendPanel_SvcOverlay_FireSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0xC0, 0x39, 0x2B, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_SvcOverlay_PoliceSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0x2E, 0x44, 0x82, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_SvcOverlay_PowerSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0xF1, 0xC4, 0x0F, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_SvcOverlay_WaterSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0x1A, 0xBC, 0x9C, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_TfcOverlay_FreeFlowSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0x27, 0xAE, 0x60, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_TfcOverlay_MildCongestionSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0xE6, 0x7E, 0x22, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_TfcOverlay_HeavyCongestionSwatch) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0xE7, 0x4C, 0x3C, 255)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_SwitchFromSvcToTfc_SwatchesChange) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();
    m_minimap->drawOverlay();  // first overlay with Svc swatches

    // Switch to Traffic
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->draw();

    // Expect Tfc swatches, NOT Svc swatches for the legend swatch area.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0x27, 0xAE, 0x60, 255)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(1724, _, 8, 8, 0xC0, 0x39, 0x2B, 255)).Times(0);
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, LegendPanel_TextColor_EBF4F6) {
    // Legend label text color is set during construction to (235, 244, 246).
    EXPECT_NE(m_handle_legendLabel, static_cast<UIElementHandle>(0));
    // Verify that draw() calls setElementText on the legend label when overlay active.
    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();

    bool textSet = false;
    ON_CALL(m_backend, setElementText(_, _))
        .WillByDefault([&](UIElementHandle h, const std::string&) {
            if (h == m_handle_legendLabel) textSet = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(textSet) << "Legend label text should be updated when overlay active.";
}

TEST_F(MinimapOverlayTest, LegendPanel_HiddenWhenNoOverlay) {
    m_minimap->show();
    // Overlay is inactive.
    bool legendPanelHidden = false;
    bool legendLabelHidden = false;
    ON_CALL(m_backend, setElementVisible(_, _))
        .WillByDefault([&](UIElementHandle h, bool visible) {
            if (h == m_handle_legendPanel && !visible) legendPanelHidden = true;
            if (h == m_handle_legendLabel && !visible) legendLabelHidden = true;
        });
    m_minimap->draw();
    EXPECT_TRUE(legendPanelHidden) << "Legend panel should be hidden when no overlay.";
    EXPECT_TRUE(legendLabelHidden) << "Legend label should be hidden when no overlay.";
}

// ###########################################################################
// MM-40: Group 8 -- Budget-tick cadence (4 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, BudgetTick_QueryOncePerTick) {
    stubAllTiles(QueryResult{});
    m_minimap->show();

    // One budget tick: full grid queried (64*64 = 4096 tiles).
    EXPECT_CALL(m_sim, queryTile(_, _)).Times(64 * 64).WillRepeatedly(Return(QueryResult{}));
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();
}

TEST_F(MinimapOverlayTest, BudgetTick_MultipleDrawsNoBudgetTick_NoRequery) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    tickAndDraw();  // first tick + draw

    // Second draw without budget tick: no new queryTile calls.
    EXPECT_CALL(m_sim, queryTile(_, _)).Times(0);
    m_minimap->draw();
}

TEST_F(MinimapOverlayTest, BudgetTick_TickBetweenDraws_RequeriesOnSecondDraw) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    tickAndDraw();  // first tick + draw

    // Budget tick between draws.
    m_minimap->onBudgetTicks(1);
    EXPECT_CALL(m_sim, queryTile(_, _)).Times(64 * 64).WillRepeatedly(Return(QueryResult{}));
    m_minimap->draw();
}

TEST_F(MinimapOverlayTest, BudgetTick_ZeroTicks_NoRequery) {
    stubAllTiles(QueryResult{});
    m_minimap->show();
    tickAndDraw();  // prime cache

    m_minimap->onBudgetTicks(0);
    EXPECT_CALL(m_sim, queryTile(_, _)).Times(0);
    m_minimap->draw();
}

// ###########################################################################
// MM-41: Group 9 -- Input footprint (8 tests)
// ###########################################################################

TEST_F(MinimapOverlayTest, GetBounds_ReturnsRenderArea) {
    UIRect bounds = m_minimap->getBounds();
    EXPECT_EQ(bounds.x, 1720);
    EXPECT_EQ(bounds.y, 880);
    EXPECT_EQ(bounds.w, 200);
    EXPECT_EQ(bounds.h, 200);
}

TEST_F(MinimapOverlayTest, GetWidgetFootprint_NoOverlay) {
    UIRect fp = m_minimap->getWidgetFootprint();
    EXPECT_EQ(fp.x, 1576);
    EXPECT_EQ(fp.y, 848);
    EXPECT_EQ(fp.w, 344);
    EXPECT_EQ(fp.h, 232);
}

TEST_F(MinimapOverlayTest, GetWidgetFootprint_SvcOverlayActive) {
    m_minimap->show();
    clickAt(1736, 864);  // activate Svc

    UIRect fp = m_minimap->getWidgetFootprint();
    EXPECT_EQ(fp.x, 1576);
    EXPECT_EQ(fp.y, 732);
    EXPECT_EQ(fp.w, 344);
    EXPECT_EQ(fp.h, 348);
}

TEST_F(MinimapOverlayTest, GetWidgetFootprint_TfcOverlayActive) {
    m_minimap->show();
    clickAt(1700, 864);  // activate Tfc

    UIRect fp = m_minimap->getWidgetFootprint();
    EXPECT_EQ(fp.x, 1576);
    EXPECT_EQ(fp.y, 732);
    EXPECT_EQ(fp.w, 344);
    EXPECT_EQ(fp.h, 348);
}

TEST_F(MinimapOverlayTest, GetBoundsAndFootprint_DistinctWhenNoOverlay) {
    UIRect bounds = m_minimap->getBounds();
    UIRect fp     = m_minimap->getWidgetFootprint();
    EXPECT_NE(bounds.x, fp.x);
    EXPECT_NE(bounds.y, fp.y);
}

TEST_F(MinimapOverlayTest, GetBoundsAndFootprint_DistinctWhenOverlayActive) {
    m_minimap->show();
    clickAt(1736, 864);  // activate Svc

    UIRect bounds = m_minimap->getBounds();
    UIRect fp     = m_minimap->getWidgetFootprint();
    EXPECT_NE(bounds.x, fp.x);
    EXPECT_NE(bounds.y, fp.y);
    EXPECT_NE(bounds.h, fp.h);
}

TEST_F(MinimapOverlayTest, ToggleSvcButtonRegion_WithinNoOverlayFootprint) {
    UIRect fp = m_minimap->getWidgetFootprint();
    // Svc button region: x:1720-1752, y:848-880
    // Must lie within footprint (x:1576, y:848, w:344, h:232).
    EXPECT_GE(1720, fp.x);
    EXPECT_LE(1752, fp.x + fp.w);
    EXPECT_GE(848, fp.y);
    EXPECT_LE(880, fp.y + fp.h);
}

TEST_F(MinimapOverlayTest, LegendPanel_y732_WithinOverlayActiveFootprint) {
    m_minimap->show();
    clickAt(1736, 864);  // activate overlay

    UIRect fp = m_minimap->getWidgetFootprint();
    // Legend panel at y:732 must be within overlay-active footprint (y:732, h:348).
    EXPECT_LE(fp.y, 732);
    EXPECT_GE(fp.y + fp.h, 732 + 100);  // legend panel is 100px tall
}

// ###########################################################################
// MM-42: Group 10 -- Element leak regression (5 tests)
// ###########################################################################

class MinimapElementLeakTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      m_backend;
    NiceMock<MockCitySimulation> m_sim;
    std::unique_ptr<Minimap>     m_minimap;

    void SetUp() override {
        UIElementHandle nextHandle = 2000;
        ON_CALL(m_backend, addStaticText(_, _, _, _, _))
            .WillByDefault([nextHandle](const std::string&, int, int, int, int) mutable -> UIElementHandle {
                return nextHandle++;
            });
        ON_CALL(m_backend, addButton(_, _, _, _, _))
            .WillByDefault([nextHandle](const std::string&, int, int, int, int) mutable -> UIElementHandle {
                return nextHandle++;
            });
        ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(64));
        ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(64));
        ON_CALL(m_sim, consumeBudgetTicks()).WillByDefault(Return(0));

        // Pre-populate with zoned tile for fillColoredRect to fire.
        QueryResult zonedTile;
        zonedTile.isZoned  = true;
        zonedTile.zoneType = ZoneType::Residential;
        ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(zonedTile));

        m_minimap = std::make_unique<Minimap>(&m_backend, nullptr, &m_sim, nullptr);
        m_minimap->show();
        m_minimap->onBudgetTicks(1);
        m_minimap->draw();
    }
    void TearDown() override { m_minimap.reset(); }
};

TEST_F(MinimapElementLeakTest, SingleDrawOverlay_NoElementCreation_FillCalled) {
    EXPECT_CALL(m_backend, addStaticText(_, _, _, _, _)).Times(0);
    EXPECT_CALL(m_backend, addButton(_, _, _, _, _)).Times(0);
    m_minimap->draw();

    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapElementLeakTest, TenConsecutiveDrawOverlay_NoElementCreation) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_CALL(m_backend, addStaticText(_, _, _, _, _)).Times(0);
        EXPECT_CALL(m_backend, addButton(_, _, _, _, _)).Times(0);
        m_minimap->draw();

        EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(AtLeast(1));
        m_minimap->drawOverlay();

        ::testing::Mock::VerifyAndClearExpectations(&m_backend);
        // Re-apply NiceMock default stubs after clearing.
        ON_CALL(m_backend, addStaticText(_, _, _, _, _))
            .WillByDefault(Return(static_cast<UIElementHandle>(9999)));
        ON_CALL(m_backend, addButton(_, _, _, _, _))
            .WillByDefault(Return(static_cast<UIElementHandle>(9999)));
    }
}

TEST_F(MinimapElementLeakTest, DrawOverlayAfterSecondBudgetTick_NoElementCreation) {
    m_minimap->onBudgetTicks(1);
    EXPECT_CALL(m_backend, addStaticText(_, _, _, _, _)).Times(0);
    EXPECT_CALL(m_backend, addButton(_, _, _, _, _)).Times(0);
    m_minimap->draw();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapElementLeakTest, NoAddButtonDuringDraw) {
    EXPECT_CALL(m_backend, addButton(_, _, _, _, _)).Times(0);
    m_minimap->draw();
    m_minimap->drawOverlay();
}

TEST_F(MinimapElementLeakTest, FillColoredRectConfirmsCacheNotEmpty) {
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// ###########################################################################
// MM-43: Group 11 -- Overlay tile rendering (8 tests)
// ###########################################################################

// Service Coverage overlay tile tints (4 tests)

TEST_F(MinimapOverlayTest, Overlay_SvcActive_FireStation_EmitsFireColor) {
    stubAllTiles(QueryResult{});

    ServiceCoverageTile sct;
    sct.tileX = 10; sct.tileZ = 10;
    sct.coveredBy = ServiceBuildingType::FireStation;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xC0, 0x39, 0x2B, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Overlay_SvcActive_PoliceStation_EmitsPoliceColor) {
    stubAllTiles(QueryResult{});

    ServiceCoverageTile sct;
    sct.tileX = 20; sct.tileZ = 20;
    sct.coveredBy = ServiceBuildingType::PoliceStation;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x2E, 0x44, 0x82, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Overlay_SvcActive_PowerPlant_EmitsPowerColor) {
    stubAllTiles(QueryResult{});

    ServiceCoverageTile sct;
    sct.tileX = 30; sct.tileZ = 30;
    sct.coveredBy = ServiceBuildingType::PowerPlant;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xF1, 0xC4, 0x0F, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Overlay_SvcActive_WaterTower_EmitsWaterColor) {
    stubAllTiles(QueryResult{});

    ServiceCoverageTile sct;
    sct.tileX = 40; sct.tileZ = 40;
    sct.coveredBy = ServiceBuildingType::WaterTower;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x1A, 0xBC, 0x9C, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// Uncovered tile test

TEST_F(MinimapOverlayTest, Overlay_SvcActive_FireOverResidential_FireColorOnTop) {
    // Tile is zoned Residential AND covered by FireStation.
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Residential;
    stubAllTiles(tile);

    ServiceCoverageTile sct;
    sct.tileX = 10; sct.tileZ = 10;
    sct.coveredBy = ServiceBuildingType::FireStation;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    // Both green (zone) and red (fire) should be emitted.
    // Fire color is painted ON TOP via painter's algorithm.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xC0, 0x39, 0x2B, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// Traffic Congestion overlay (3 tests)

TEST_F(MinimapOverlayTest, Overlay_TfcActive_FreeFlow_EmitsGreen) {
    stubAllTiles(QueryResult{});

    RoadSegmentSpeed seg;
    seg.tileX = 10; seg.tileZ = 10;
    seg.speedFraction = 0.8f;  // >= 0.4 => free-flow green
    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Overlay_TfcActive_MildCongestion_EmitsOrange) {
    stubAllTiles(QueryResult{});

    RoadSegmentSpeed seg;
    seg.tileX = 10; seg.tileZ = 10;
    seg.speedFraction = 0.35f;  // > 0.3 and < 0.4 => mild orange
    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xE6, 0x7E, 0x22, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

TEST_F(MinimapOverlayTest, Overlay_TfcActive_HeavyCongestion_EmitsRed) {
    stubAllTiles(QueryResult{});

    RoadSegmentSpeed seg;
    seg.tileX = 10; seg.tileZ = 10;
    seg.speedFraction = 0.20f;  // <= 0.3 => heavy red
    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xE7, 0x4C, 0x3C, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// (b) Uncovered tile NOT overridden with service colours — shows zone color only.
TEST_F(MinimapOverlayTest, Overlay_SvcActive_UncoveredTile_ShowsZoneColor) {
    // All tiles are zoned Residential.
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Residential;
    tile.isRoad   = false;
    stubAllTiles(tile);

    // Only tile (0,0) is covered by FireStation — all others are uncovered.
    ServiceCoverageTile sct;
    sct.tileX = 0; sct.tileZ = 0;
    sct.coveredBy = ServiceBuildingType::FireStation;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    // Uncovered tiles should receive zone colour (Residential green), not service colour.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// (d) Speed-band colour emitted for road tiles when Tfc overlay active.
// drawOverlay() uses painter's algorithm: road grey drawn first, speed-band colour
// drawn on top (same pattern as service tints overwriting zone colours).
// This test verifies speed-band green IS emitted for a road tile with speed data.
TEST_F(MinimapOverlayTest, Overlay_TfcActive_RoadTile_SpeedBandColorEmitted) {
    QueryResult tile;
    tile.isZoned = false;
    tile.isRoad  = true;
    stubAllTiles(tile);

    // Road tile at (10,10) with free-flowing speed => green.
    RoadSegmentSpeed seg;
    seg.tileX = 10; seg.tileZ = 10;
    seg.speedFraction = 0.8f;  // >= 0.4 => free-flow green
    std::vector<RoadSegmentSpeed> speeds = {seg};
    ON_CALL(m_sim, getRoadSegmentSpeeds()).WillByDefault(Return(speeds));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::Traffic);
    m_minimap->toggleOverlay();
    tickAndDraw();

    // Speed-band green is emitted for the road tile with speed data (overdraws road grey).
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// (e) Deactivating overlay reverts to base zone/road rendering.
TEST_F(MinimapOverlayTest, Overlay_DeactivateSvc_RevertsToZoneColor) {
    // All tiles are zoned Residential.
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Residential;
    tile.isRoad   = false;
    stubAllTiles(tile);

    // Fire Station coverage at (0,0).
    ServiceCoverageTile sct;
    sct.tileX = 0; sct.tileZ = 0;
    sct.coveredBy = ServiceBuildingType::FireStation;
    std::vector<ServiceCoverageTile> coverage = {sct};
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();   // activate
    tickAndDraw();

    // First draw with overlay active — just consume expectations.
    allowAllFillColoredRect();
    m_minimap->drawOverlay();
    ::testing::Mock::VerifyAndClearExpectations(&m_backend);

    // Deactivate the overlay.
    m_minimap->toggleOverlay();   // deactivate

    // After deactivation: zone colour (Residential green) should appear,
    // Fire Station colour should NOT appear.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xC0, 0x39, 0x2B, _)).Times(0);
    m_minimap->drawOverlay();
}

// ###########################################################################
// Coverage gap fill: sort lambda, setSimulation(), Tfc deactivation
// ###########################################################################

// Exercises the service-coverage sort comparator (lines 111-119, 122 in Minimap.cpp)
// by providing two tiles of different service types — the sort is only called
// when there are 2+ elements, causing the lambda body to execute.
TEST_F(MinimapOverlayTest, Draw_ServiceCoverage_TwoTiles_SortLambdaExecuted) {
    QueryResult tile;
    tile.isZoned  = true;
    tile.zoneType = ZoneType::Residential;
    tile.isRoad   = false;
    stubAllTiles(tile);

    // Provide WaterTower then FireStation — comparator must be called to sort them.
    std::vector<ServiceCoverageTile> coverage;
    ServiceCoverageTile sct1;
    sct1.tileX = 5; sct1.tileZ = 5; sct1.coveredBy = ServiceBuildingType::WaterTower;
    ServiceCoverageTile sct2;
    sct2.tileX = 10; sct2.tileZ = 10; sct2.coveredBy = ServiceBuildingType::FireStation;
    coverage.push_back(sct1);
    coverage.push_back(sct2);
    ON_CALL(m_sim, getServiceCoverage()).WillByDefault(Return(coverage));

    m_minimap->show();
    m_minimap->setOverlayMode(MinimapOverlay::ServiceCoverage);
    m_minimap->toggleOverlay();
    tickAndDraw();

    // Both service colours are emitted after correct sort order.
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0xC0, 0x39, 0x2B, _)).Times(AtLeast(1));
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x1A, 0xBC, 0x9C, _)).Times(AtLeast(1));
    m_minimap->drawOverlay();
}

// Exercises Minimap::setSimulation() (lines 302-304 in Minimap.cpp).
// Temporarily sets sim to nullptr (triggering the visibility guard in drawOverlay)
// then restores it.
TEST_F(MinimapOverlayTest, SetSimulation_NullSim_GuardSuppressesDrawOverlay) {
    m_minimap->show();
    m_minimap->setSimulation(nullptr);
    // With no sim, drawOverlay must emit zero fillColoredRect calls.
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, _, _, _, _)).Times(0);
    m_minimap->drawOverlay();
    // Restore to avoid TearDown mock violations.
    m_minimap->setSimulation(&m_sim);
}

// Exercises the Tfc-deactivation branch (line 370 in Minimap.cpp):
// clicking the Tfc button when Tfc is already the active overlay deactivates it.
TEST_F(MinimapOverlayTest, OverlayToggle_ClickActiveTfcButton_Deactivates) {
    m_minimap->show();

    clickAt(1700, 864);  // activate Tfc (center of Tfc button: x:1684-1716, y:848-880)
    EXPECT_TRUE(m_minimap->isOverlayActive());
    EXPECT_EQ(m_minimap->getOverlayMode(), MinimapOverlay::Traffic);

    clickAt(1700, 864);  // click active Tfc button again → deactivates
    EXPECT_FALSE(m_minimap->isOverlayActive());
}

// ###########################################################################
// MM-44: Coordinate mapping direction verification
// ###########################################################################

// A residential tile at grid position (50,49) is one tile north (-Z direction)
// of camera target (500,500).  relZ = 49*10 - 500 = -10 < 0.  The camera at
// yaw=0 looks toward -Z (eye is at larger Z than target), so -Z = forward =
// "north" in this game.  With py = centreZ + rotZ*scaleZ:
// py = 980 + (-10)*(200/1000) = 978 < 980.  The tile must appear above centre.
TEST_F(MinimapOverlayTest, MM44_NorthTileAppearsAboveCentre_Yaw0) {
    QueryResult northTile;
    northTile.isRoad  = false;
    northTile.isZoned = true;
    northTile.zoneType = ZoneType::Residential;

    QueryResult unzoned;
    unzoned.isRoad = false;
    unzoned.isZoned = false;

    // 100x100 map; tile (50,49): relZ = 49*10 - 500 = -10 (in camera look direction).
    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));
    ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(unzoned));
    ON_CALL(m_sim, queryTile(50, 49)).WillByDefault(Return(northTile));

    CameraState cs;
    cs.targetX     = 500.f;
    cs.targetZ     = 500.f;
    cs.yaw         = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    int northPy = -1;
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, 255))
        .Times(AtLeast(1))
        .WillRepeatedly([&](int /*x*/, int y, int, int, int, int, int, int) {
            northPy = y;
        });
    m_minimap->drawOverlay();

    ASSERT_NE(northPy, -1) << "No residential tile rendered";
    EXPECT_LT(northPy, 980) << "North tile must appear above minimap centre";
}

// A residential tile at (50,51) is one tile south (+Z direction).
// relZ = 51*10 - 500 = +10 > 0.  +Z is behind the camera at yaw=0.
// With py = centreZ + rotZ*scaleZ:
// py = 980 + 10*(200/1000) = 982 > 980.  Y must be strictly greater than 980.
TEST_F(MinimapOverlayTest, MM44_SouthTileAppearsBelowCentre_Yaw0) {
    QueryResult southTile;
    southTile.isRoad  = false;
    southTile.isZoned = true;
    southTile.zoneType = ZoneType::Residential;

    QueryResult unzoned;
    unzoned.isRoad = false;
    unzoned.isZoned = false;

    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));
    ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(unzoned));
    ON_CALL(m_sim, queryTile(50, 51)).WillByDefault(Return(southTile));

    CameraState cs;
    cs.targetX     = 500.f;
    cs.targetZ     = 500.f;
    cs.yaw         = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    int southPy = -1;
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, 255))
        .Times(AtLeast(1))
        .WillRepeatedly([&](int /*x*/, int y, int, int, int, int, int, int) {
            southPy = y;
        });
    m_minimap->drawOverlay();

    ASSERT_NE(southPy, -1) << "No residential tile rendered";
    EXPECT_GT(southPy, 980) << "South tile must appear below minimap centre";
}

// A residential tile at (51,50) is one tile east (+X).  relX = 51*10 - 500
// = 10 > 0.  Irrlicht LH: camera right = -X, so world +X = camera-LEFT.
// px = 1820 - 10*0.2 = 1818 < 1820.  X must be strictly less than 1820.
TEST_F(MinimapOverlayTest, MM44_EastTileAppearsLeftOfCentre_Yaw0) {
    QueryResult eastTile;
    eastTile.isRoad  = false;
    eastTile.isZoned = true;
    eastTile.zoneType = ZoneType::Residential;

    QueryResult unzoned;
    unzoned.isRoad = false;
    unzoned.isZoned = false;

    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));
    ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(unzoned));
    ON_CALL(m_sim, queryTile(51, 50)).WillByDefault(Return(eastTile));

    CameraState cs;
    cs.targetX     = 500.f;
    cs.targetZ     = 500.f;
    cs.yaw         = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    int eastPx = -1;
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, 255))
        .Times(AtLeast(1))
        .WillRepeatedly([&](int x, int /*y*/, int, int, int, int, int, int) {
            eastPx = x;
        });
    m_minimap->drawOverlay();

    ASSERT_NE(eastPx, -1) << "No residential tile rendered";
    EXPECT_LT(eastPx, 1820) << "East tile (+X = camera-left in Irrlicht LH) must appear left of minimap centre";
}

// A click at (1820, 975) -- 5 pixels above minimap centre (centreY=980) --
// with yaw=0 should pan to a world Z less than 500.f.  The camera at yaw=0
// looks toward -Z (smaller Z = forward = top of minimap), so clicking above
// centre moves the camera in the -Z direction.
// offZ = (975 - 980) / scaleZ = -5/0.2 = -25; panToZ = 500 - 25 = 475.
TEST_F(MinimapOverlayTest, MM44_ClickToPan_AboveCentre_PansNorth_Yaw0) {
    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));

    CameraState cs;
    cs.targetX     = 500.f;
    cs.targetZ     = 500.f;
    cs.yaw         = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();

    float panToX = -1.f;
    float panToZ = -1.f;
    m_minimap->setPanCallback([&](float x, float z) { panToX = x; panToZ = z; });

    // Click 5 pixels above centre (centreY=980 -> click at y=975).
    clickAt(1820, 975);

    ASSERT_NE(panToZ, -1.f) << "Pan callback not called";
    EXPECT_LT(panToZ, 500.f)
        << "Clicking above minimap centre (forward direction) pans toward -Z";
    EXPECT_NEAR(panToX, 500.f, 1.0f)
        << "Clicking at centreX should not pan horizontally";
}
