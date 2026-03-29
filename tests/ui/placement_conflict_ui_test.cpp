// tests/ui/placement_conflict_ui_test.cpp
//
// Phase 11d Deliverable 5e: Placement Conflict UI Tests
// Verifies that UIManager shows kHoverArgbBlocked for occupied tiles and
// partitions drag previews into free/blocked, and skips occupied tiles
// in the commit loop.
//
// Added to ui_tests via:
//   target_sources(ui_tests PRIVATE tests/ui/placement_conflict_ui_test.cpp)
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
#include <vector>
#include <utility>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAre;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static InputEvent makeLmbDown(int virtX, int virtY)
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

static InputEvent makeLmbUp(int virtX, int virtY)
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonUp;
    ev.button = 0;
    ev.x      = virtX;
    ev.y      = virtY;
    ev.physX  = virtX;
    ev.physY  = virtY;
    return ev;
}

static InputEvent makeMouseMove(int virtX, int virtY)
{
    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.x     = virtX;
    ev.y     = virtY;
    ev.physX = virtX;
    ev.physY = virtY;
    return ev;
}

// ---------------------------------------------------------------------------
// PlacementConflictUITest fixture
// ---------------------------------------------------------------------------
class PlacementConflictUITest : public ::testing::Test {
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
        EXPECT_CALL(renderer_, setActiveTool(_)).Times(AnyNumber());
        EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());
        EXPECT_CALL(renderer_, setZoneHoverColour(_)).Times(AnyNumber());
        EXPECT_CALL(renderer_, placeRoadMesh(_, _)).Times(AnyNumber());
        // Default: all tiles are free (not occupied).
        EXPECT_CALL(sim_, queryTile(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(QueryResult{}));
        // isWithinRoadRange: default true so tiles are road-accessible.
        // Tests that verify occupation-based blocking (isRoad/isZoned) do not need to
        // change this; the occupation check is separate from road-proximity gating.
        // StrictMock requires EXPECT_CALL (ON_CALL alone is not enough to suppress errors).
        EXPECT_CALL(sim_, isWithinRoadRange(_, _, _)).Times(AnyNumber()).WillRepeatedly(Return(true));

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

    // Activate Zone tool.
    void activateZoneTool() {
        goToGameplay();
        InputEvent ev = makeLmbDown(40, 80);  // y:64-111 = Zone button
        uiManager_->onEvent(ev);
    }

    // Activate Road tool.
    void activateRoadTool() {
        goToGameplay();
        InputEvent ev = makeLmbDown(40, 140);  // y:120-167 = Road button
        uiManager_->onEvent(ev);
    }
};

// ============================================================================
// PlacementPreview_ZoneTool_OccupiedTile_ShowsRedHighlight
// Phase 11d Deliverable 5e: when Zone tool hovers over an occupied tile
// (isRoad == true), setTileHoverHighlight must receive kHoverArgbBlocked.
// ============================================================================
TEST_F(PlacementConflictUITest, PlacementPreview_ZoneTool_OccupiedTile_ShowsRedHighlight)
{
    activateZoneTool();

    // Stub pickTerrainTile to return (2,2).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(2), SetArgReferee<3>(2), Return(true)));

    // Override default: tile (2,2) is a road.
    QueryResult occupiedResult;
    occupiedResult.isRoad = true;
    EXPECT_CALL(sim_, queryTile(2, 2))
        .WillRepeatedly(Return(occupiedResult));

    // Assert: setTileHoverHighlight called with footprintSize=0 (blocked sentinel) for (2,2).
    // Phase 11h: blocked tiles use footprintSize=0 instead of the ARGB colour approach.
    EXPECT_CALL(renderer_, setTileHoverHighlight(2, 2, 0)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ============================================================================
// PlacementPreview_ZoneDrag_PartiallyOccupied_BlockedTilesRed
// Phase 11d Deliverable 5e: when Zone tool drag includes a mix of free and
// blocked tiles, setTilePlacementPreview receives the correct partition.
//
// Setup: tile (1,1) is occupied (isRoad=true), tile (2,1) is free.
// Simulate LMB-held drag from (1,1) anchor to (2,1).
// Assert setTilePlacementPreview called with blockedTiles={(1,1)} and freeTiles={(2,1)}.
// ============================================================================
TEST_F(PlacementConflictUITest, PlacementPreview_ZoneDrag_PartiallyOccupied_BlockedTilesRed)
{
    activateZoneTool();

    // Override: (1,1) is occupied; (2,1) is free.
    QueryResult occupied;
    occupied.isRoad = true;
    EXPECT_CALL(sim_, queryTile(1, 1)).WillRepeatedly(Return(occupied));
    EXPECT_CALL(sim_, queryTile(2, 1)).WillRepeatedly(Return(QueryResult{}));

    // Step 1: LMB down on tile (1,1) — sets anchor; no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(300, 300, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(1), SetArgReferee<3>(1), Return(true)));
    uiManager_->onEvent(makeLmbDown(300, 300));

    // Step 2: MouseMove to tile (2,1) with LMB held — triggers drag rect preview.
    // X-dominant: dX=1 >= dZ=0. Rect is {(1,1),(2,1)}.
    // After partitioning: freeTiles={(2,1)}, blockedTiles={(1,1)}.
    EXPECT_CALL(renderer_, pickTerrainTile(400, 300, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(2), SetArgReferee<3>(1), Return(true)));

    // Capture the setTilePlacementPreview arguments.
    std::vector<std::pair<int,int>> capturedFree;
    std::vector<std::pair<int,int>> capturedBlocked;
    EXPECT_CALL(renderer_, setTilePlacementPreview(_, _, _))
        .WillOnce(DoAll(
            ::testing::SaveArg<0>(&capturedFree),
            ::testing::SaveArg<2>(&capturedBlocked)));

    uiManager_->onEvent(makeMouseMove(400, 300));

    // Verify partition: (2,1) is free, (1,1) is blocked.
    ASSERT_EQ(capturedFree.size(), 1u);
    EXPECT_EQ(capturedFree[0], std::make_pair(2, 1));
    ASSERT_EQ(capturedBlocked.size(), 1u);
    EXPECT_EQ(capturedBlocked[0], std::make_pair(1, 1));
}

// ============================================================================
// PlacementCommit_ZoneTool_OccupiedTileSkipped
// Phase 11d Deliverable 5e: on LMB-up commit, Zone tool skips occupied tiles.
// Setup: tile (3,3) is occupied (isRoad=true); tile (4,3) is free.
// Simulate LMB drag from (3,3) to (4,3) and release.
// Assert placeZone called once (for (4,3) only), never for (3,3).
// ============================================================================
TEST_F(PlacementConflictUITest, PlacementCommit_ZoneTool_OccupiedTileSkipped)
{
    activateZoneTool();

    // Override: (3,3) is occupied; (4,3) is free.
    QueryResult occupied;
    occupied.isRoad = true;
    EXPECT_CALL(sim_, queryTile(3, 3)).WillRepeatedly(Return(occupied));
    EXPECT_CALL(sim_, queryTile(4, 3)).WillRepeatedly(Return(QueryResult{}));

    // Step 1: LMB down on (3,3) — sets anchor.
    EXPECT_CALL(renderer_, pickTerrainTile(300, 400, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(3), Return(true)));
    uiManager_->onEvent(makeLmbDown(300, 400));

    // Step 2: MouseMove to (4,3).
    EXPECT_CALL(renderer_, pickTerrainTile(400, 400, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(4), SetArgReferee<3>(3), Return(true)));
    uiManager_->onEvent(makeMouseMove(400, 400));

    // Step 3: LMB release — commit loop.
    // placeZone must be called exactly once, for (4,3) only.
    // (3,3) is blocked and must be skipped.
    EXPECT_CALL(sim_, placeZone(4, 3, _, _, _)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 3, _, _, _)).Times(0);
    uiManager_->onEvent(makeLmbUp(400, 400));
}
