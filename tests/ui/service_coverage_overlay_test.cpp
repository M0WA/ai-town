// tests/ui/service_coverage_overlay_test.cpp
//
// Phase 11d Deliverable 4c: Service Coverage Overlay Tests
// Verifies UIManager wires showServiceCoverageOverlay / hideServiceCoverageOverlay
// to the inspector open/close lifecycle.
//
// Added to ui_tests via:
//   target_sources(ui_tests PRIVATE tests/ui/service_coverage_overlay_test.cpp)
// Do NOT call add_executable(ui_tests ...) again.
//
// Mock policy: StrictMock<MockCitySimulation> and StrictMock<MockRenderer>
// per architecture/testing/testability-architecture.md.

#include "src/ui/UIManager.h"
#include "src/interfaces/simulation_types.h"
#include "src/platform/input_event.h"
#include "src/ui/ui_constants.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

// kKeyEscape is defined locally in UIManager.cpp (not in a header).
// Use the raw Irrlicht/SDL2 value directly.
static constexpr int kEscapeKey = 27;

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

// ---------------------------------------------------------------------------
// Helper: build a left-click InputEvent at virtual coordinates.
// ---------------------------------------------------------------------------
static InputEvent makeClick(int virtX, int virtY)
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = virtX;
    ev.y      = virtY;
    ev.physX  = virtX;
    ev.physY  = virtY;
    return ev;
}

static InputEvent makeKeyDown(int keyCode)
{
    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = keyCode;
    return ev;
}

// ---------------------------------------------------------------------------
// ServiceCoverageOverlayTest fixture
// ---------------------------------------------------------------------------
class ServiceCoverageOverlayTest : public ::testing::Test {
protected:
    StrictMock<MockCitySimulation> sim_;
    StrictMock<MockRenderer>       renderer_;
    ManualTerrainQuery             terrain_;
    ManualClock                    clock_;
    NiceMock<MockUIBackend>        backend_;

    std::unique_ptr<UIManager> uiManager_;

    void SetUp() override {
        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, setTilePlacementPreview(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, setTileHoverHighlight(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, getTileScreenBounds(_, _)).Times(AnyNumber())
            .WillRepeatedly(Return(ScreenRect{}));
        // Catch-alls for overlay methods — individual tests override with Times(1).
        EXPECT_CALL(renderer_, showServiceCoverageOverlay(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, hideServiceCoverageOverlay()).Times(AnyNumber());
        // Phase 11d Deliverable 5d: queryTile called in hover/commit paths.
        EXPECT_CALL(sim_, queryTile(_, _)).Times(AnyNumber()).WillRepeatedly(Return(QueryResult{}));

        uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
        uiManager_->setRenderer(&renderer_);
        uiManager_->setTerrainQuery(&terrain_);
        uiManager_->setMapDimensions(10, 10);
        uiManager_->setDemolishConfirm(false);
    }

    void TearDown() override {
        uiManager_.reset();
    }

    void goToGameplay() {
        uiManager_->transitionToGameplay(GameMode::Sandbox);
    }

    // Activate the Query tool via toolbar click (y:288-335 = Query button range).
    void activateQueryTool() {
        goToGameplay();
        InputEvent ev = makeClick(40, 310);
        uiManager_->onEvent(ev);
    }

    // Stub pickTerrainTile to return hit at (tileX, tileZ).
    void stubPickTile(int tileX, int tileZ) {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(DoAll(
                SetArgReferee<2>(tileX),
                SetArgReferee<3>(tileZ),
                Return(true)));
    }
};

// ============================================================================
// ServiceCoverageOverlay_QueryServiceTile_ShowsOverlay
// Phase 11d Deliverable 4c: querying a service building tile calls
// showServiceCoverageOverlay with the building's type and degradation state.
// ============================================================================
TEST_F(ServiceCoverageOverlayTest, ServiceCoverageOverlay_QueryServiceTile_ShowsOverlay)
{
    activateQueryTool();
    stubPickTile(3, 4);

    // Stub queryTile(3,4) to return a Fire Station tile (non-degraded).
    QueryResult fireResult;
    fireResult.tileX       = 3;
    fireResult.tileZ       = 4;
    fireResult.serviceType = ServiceBuildingType::FireStation;
    fireResult.degraded    = false;

    // Override the catch-all for tile (3,4) only.
    EXPECT_CALL(sim_, queryTile(3, 4))
        .WillOnce(Return(fireResult));

    // getTileScreenBounds is called as part of the query open path.
    EXPECT_CALL(renderer_, getTileScreenBounds(3, 4))
        .Times(AnyNumber())
        .WillRepeatedly(Return(ScreenRect{}));

    // showServiceCoverageOverlay must be called with the correct type and degradation.
    EXPECT_CALL(renderer_,
                showServiceCoverageOverlay(3, 4, ServiceBuildingType::FireStation, false))
        .Times(1);

    // Simulate left-click in world area (outside toolbar, outside minimap).
    uiManager_->onEvent(makeClick(500, 500));
}

// ============================================================================
// ServiceCoverageOverlay_InspectorClose_HidesOverlay
// Phase 11d Deliverable 4c: closing the Inspector panel (via Escape key) calls
// hideServiceCoverageOverlay.
// ============================================================================
TEST_F(ServiceCoverageOverlayTest, ServiceCoverageOverlay_InspectorClose_HidesOverlay)
{
    activateQueryTool();
    stubPickTile(2, 2);

    // Open inspector on a service building tile.
    QueryResult powerResult;
    powerResult.tileX       = 2;
    powerResult.tileZ       = 2;
    powerResult.serviceType = ServiceBuildingType::PowerPlant;
    powerResult.degraded    = true;

    EXPECT_CALL(sim_, queryTile(2, 2))
        .WillOnce(Return(powerResult));
    EXPECT_CALL(renderer_, getTileScreenBounds(2, 2))
        .Times(AnyNumber())
        .WillRepeatedly(Return(ScreenRect{}));
    EXPECT_CALL(renderer_,
                showServiceCoverageOverlay(2, 2, ServiceBuildingType::PowerPlant, true))
        .Times(1);

    uiManager_->onEvent(makeClick(500, 500));

    // Close inspector via Escape — must call hideServiceCoverageOverlay.
    EXPECT_CALL(renderer_, hideServiceCoverageOverlay()).Times(1);
    uiManager_->onEvent(makeKeyDown(kEscapeKey));
}
