// tests/ui/demolition_input_test.cpp
//
// Phase 11q8: Demolition Drag-Select Input Tests
//
// Verifies the rectangular drag-select demolition flow (Phase 11q8):
//   - Mouse-down records the demolish anchor tile.
//   - Mouse-move while LMB held shows the rect preview.
//   - Mouse-up counts occupied tiles in the selected rect:
//       * 0 occupied → silent cancel (no modal, no demolish).
//       * >0 occupied + confirm ON → confirmation modal opens.
//       * >0 occupied + confirm OFF → immediate demolish.
//   - Modal Accept → demolishTile called for occupied tiles.
//   - Modal Cancel → no demolishTile call; highlight cleared.
//   - Zone tool active → demolish path never entered.
//
// Added to ui_tests via:
//   target_sources(ui_tests PRIVATE tests/ui/demolition_input_test.cpp)
// Do NOT call add_executable(ui_tests ...) or aitown_add_tests(ui_tests ...) again.
//
// Mock policy:
//   StrictMock<MockCitySimulation> sim_    — verifies exact demolishTile call count.
//   StrictMock<MockRenderer>       renderer_ — verifies renderer call count/args.
//   NiceMock<MockUIBackend>        backend_  — suppresses incidental backend calls.
//   ManualTerrainQuery             terrain_  — flat terrain.
//   ManualClock                    clock_    — deterministic timing.
//
// TearDown contract: uiManager_.reset() before mock destructors per
//   architecture/testing/testability-architecture.md.
//
// Spec reference: implementation/phase-11q8.md (Demolition Drag-Select).

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/ui/ui_constants.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/LoanTerms.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

// ---------------------------------------------------------------------------
// Helper: build InputEvent structs.
// ---------------------------------------------------------------------------

static InputEvent mouseButtonDown(int button, int virtX = 500, int virtY = 500)
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = button;
    ev.x      = virtX;
    ev.y      = virtY;
    ev.physX  = virtX;
    ev.physY  = virtY;
    return ev;
}

static InputEvent mouseButtonUp(int button, int virtX = 500, int virtY = 500)
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonUp;
    ev.button = button;
    ev.x      = virtX;
    ev.y      = virtY;
    ev.physX  = virtX;
    ev.physY  = virtY;
    return ev;
}

static InputEvent mouseMove(int virtX, int virtY)
{
    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.x     = virtX; ev.y     = virtY;
    ev.physX = virtX; ev.physY = virtY;
    return ev;
}

// Toolbar Demolish button click (y in 232..279 range).
static InputEvent toolbarDemolishClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 250;
    ev.physX  = 40;
    ev.physY  = 250;
    return ev;
}

// Toolbar Zone button click (y in 64..111 range).
static InputEvent toolbarZoneClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 80;
    ev.physX  = 40;
    ev.physY  = 80;
    return ev;
}

// ---------------------------------------------------------------------------
// DemolitionInputTest fixture
// ---------------------------------------------------------------------------
class DemolitionInputTest : public ::testing::Test {
protected:
    // Mock declaration order: mocks first (destroyed LAST), UIManager last (destroyed FIRST).
    StrictMock<MockCitySimulation> sim_;
    StrictMock<MockRenderer>       renderer_;
    NiceMock<MockUIBackend>        backend_;
    ManualTerrainQuery             terrain_;
    ManualClock                    clock_;

    // UIManager declared LAST — destroyed FIRST (before mock destructors run).
    std::unique_ptr<UIManager> uiManager_;

    void SetUp() override {
        // Standard UIManager construction stubs (matches modal_dialog_test pattern).
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(true));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));

        // Sim stubs required during UIManager construction and event dispatch.
        EXPECT_CALL(sim_, isPaused()).WillRepeatedly(Return(false));
        EXPECT_CALL(sim_, setPaused(_)).Times(AnyNumber());
        EXPECT_CALL(sim_, getSpeedMultiplier()).WillRepeatedly(Return(SpeedMultiplier::x1));
        EXPECT_CALL(sim_, hasUndoPendingAction()).WillRepeatedly(Return(false));
        EXPECT_CALL(sim_, getTreasuryBalance()).WillRepeatedly(Return(100000.0f));
        EXPECT_CALL(sim_, getOutstandingDebt()).WillRepeatedly(Return(0.0f));
        EXPECT_CALL(sim_, getCityRating()).WillRepeatedly(Return(CityRatingTier::Village));
        EXPECT_CALL(sim_, getTotalPopulation()).WillRepeatedly(Return(0));
        EXPECT_CALL(sim_, getSimulationTime()).WillRepeatedly(Return(SimulationTime{1, 1}));
        EXPECT_CALL(sim_, getZoneDemandFactor(_)).WillRepeatedly(Return(0.5f));
        EXPECT_CALL(sim_, getOutstandingBondUses()).WillRepeatedly(Return(2));
        EXPECT_CALL(sim_, getConsecutiveDeficitMonths()).WillRepeatedly(Return(0));
        EXPECT_CALL(sim_, pollPendingNotification(_)).WillRepeatedly(Return(false));
        // Phase 11d per-frame query methods — called from UIManager::update().
        EXPECT_CALL(sim_, getAgentPositions()).WillRepeatedly(Return(std::vector<AgentState>{}));
        EXPECT_CALL(sim_, getIntersectionSignalStates())
            .WillRepeatedly(Return(std::vector<IntersectionSignalState>{}));
        EXPECT_CALL(sim_, getRoadSegmentSpeeds())
            .WillRepeatedly(Return(std::vector<RoadSegmentSpeed>{}));
        EXPECT_CALL(sim_, getServiceCoverage())
            .WillRepeatedly(Return(std::vector<ServiceCoverageTile>{}));
        EXPECT_CALL(sim_, consumeBudgetTicks()).WillRepeatedly(Return(0));

        // Renderer stubs: suppress incidental calls from UIManager world-interaction setup.
        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, setTilePlacementPreview(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, getListenerPosition()).WillRepeatedly(Return(vec3{}));
        // Phase 11h: UIManager calls setActiveTool() when the active tool changes.
        EXPECT_CALL(renderer_, setActiveTool(_)).Times(AnyNumber());
        // clearDemolishHighlight called on cancel path (up on different tile, or modal close).
        EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());
        // setTileHoverHighlight may be called during hover updates.
        EXPECT_CALL(renderer_, setTileHoverHighlight(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(sim_, queryTile(_, _))
            .Times(AnyNumber()).WillRepeatedly(Return(QueryResult{}));
        EXPECT_CALL(renderer_, setSceneBackground(_)).Times(AnyNumber());
        EXPECT_CALL(renderer_, clearSceneBackground()).Times(AnyNumber());

        uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
        uiManager_->setRenderer(&renderer_);
        uiManager_->setTerrainQuery(&terrain_);
        uiManager_->setMapDimensions(10, 10);
    }

    void TearDown() override {
        uiManager_.reset();
    }

    // Helper: transition to Gameplay and activate Demolish tool via toolbar click.
    void activateDemolishTool()
    {
        uiManager_->transitionToGameplay(GameMode::Sandbox);
        uiManager_->onEvent(toolbarDemolishClick());
    }

    // Helper: transition to Gameplay and activate Zone tool via toolbar click.
    void activateZoneTool()
    {
        uiManager_->transitionToGameplay(GameMode::Sandbox);
        uiManager_->onEvent(toolbarZoneClick());
    }

    // Helper: stub pickTerrainTile to return a terrain hit at (tileX, tileZ).
    void stubPickTile(int tileX, int tileZ)
    {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(DoAll(
                SetArgReferee<2>(tileX),
                SetArgReferee<3>(tileZ),
                Return(true)));
    }

    // Helper: stub pickTerrainTile to return no terrain hit.
    void stubPickTileMiss()
    {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(Return(false));
    }

    uint32_t nextHandle_{100};
};

// ============================================================================
// DemolishDragConfirmOn_OccupiedTile_ModalOpens
//
// Drag on a single occupied tile (confirm ON) → modal shows "1 tile".
//
// Sequence:
//   1. Demolish tool active, confirm ON.
//   2. MouseDown on tile (5,5) → records demolish anchor.
//   3. MouseMove (same position) → rect preview.
//   4. MouseUp on tile (5,5) → tile is occupied → confirm modal opens.
//   5. demolishTile() must NOT have been called yet (modal defers it).
// ============================================================================
TEST_F(DemolitionInputTest, DemolishDragConfirmOn_OccupiedTile_ModalOpens)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    // Override SetUp catch-all: tile (5,5) is zoned (occupied).
    EXPECT_CALL(sim_, queryTile(5, 5))
        .WillRepeatedly([]{ QueryResult q; q.isZoned = true; return q; });

    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_TRUE(uiManager_->hasActiveModal())
        << "Confirm modal must open after drag over occupied tile (confirm ON)";
}

// ============================================================================
// DemolishDrag_AllEmptyTiles_NoModalNoDemolish
//
// All tiles in selection are empty → no modal, no demolishTile call.
//
// Sequence:
//   1. Demolish tool active, confirm ON.
//   2. Drag over tile (3,3) — queryTile returns empty QueryResult.
//   3. MouseUp → 0 occupied tiles → silent cancel.
//   4. No modal; no demolishTile() call.
// ============================================================================
TEST_F(DemolitionInputTest, DemolishDrag_AllEmptyTiles_NoModalNoDemolish)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(3), Return(true)));
    // SetUp catch-all already returns empty QueryResult — no override needed.

    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->onEvent(mouseButtonDown(0, 300, 300));
    uiManager_->onEvent(mouseMove(300, 300));
    uiManager_->onEvent(mouseButtonUp(0, 300, 300));

    EXPECT_FALSE(uiManager_->hasActiveModal())
        << "No modal must open when selection contains only empty tiles";
}

// ============================================================================
// DemolishDrag_ConfirmOff_ImmediateDemolish
//
// Confirm OFF — demolishTile called immediately on mouse-up, no modal.
//
// Sequence:
//   1. Demolish tool active, confirm OFF.
//   2. Drag over occupied tile (5,5).
//   3. MouseUp → confirm OFF → demolishTile(5,5) called immediately.
//   4. No modal opened.
// ============================================================================
TEST_F(DemolitionInputTest, DemolishDrag_ConfirmOff_ImmediateDemolish)
{
    uiManager_->setDemolishConfirm(false);
    activateDemolishTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    // Override SetUp catch-all: tile (5,5) is zoned (occupied).
    EXPECT_CALL(sim_, queryTile(5, 5))
        .WillRepeatedly([]{ QueryResult q; q.isZoned = true; return q; });

    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_FALSE(uiManager_->hasActiveModal());
}

// ============================================================================
// DemolishDrag_ConfirmOn_Accept_DemolishesTiles
//
// Confirm ON, accept modal → demolishTile called for occupied tiles.
//
// Sequence:
//   1. Demolish tool active, confirm ON.
//   2. Drag over occupied tile (5,5) → modal opens.
//   3. Accept modal → demolishTile(5,5) called.
// ============================================================================
TEST_F(DemolitionInputTest, DemolishDrag_ConfirmOn_Accept_DemolishesTiles)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    // Override SetUp catch-all: tile (5,5) is zoned (occupied).
    EXPECT_CALL(sim_, queryTile(5, 5))
        .WillRepeatedly([]{ QueryResult q; q.isZoned = true; return q; });

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    ASSERT_TRUE(uiManager_->hasActiveModal());

    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());

    // Accept the modal.
    uiManager_->acceptModal();
    uiManager_->update(0.016f);
}

// ============================================================================
// DemolishDrag_ConfirmOn_Cancel_NoDemolish
//
// Confirm ON, cancel modal → demolishTile NOT called.
//
// Sequence:
//   1. Demolish tool active, confirm ON.
//   2. Drag over occupied tile (3,7) → modal opens.
//   3. Close modal (Cancel / Escape) → no demolishTile; highlight cleared.
// ============================================================================
TEST_F(DemolitionInputTest, DemolishDrag_ConfirmOn_Cancel_NoDemolish)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(7), Return(true)));
    // Override SetUp catch-all: tile (3,7) is a road (occupied).
    EXPECT_CALL(sim_, queryTile(3, 7))
        .WillRepeatedly([]{ QueryResult q; q.isRoad = true; return q; });

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    ASSERT_TRUE(uiManager_->hasActiveModal());

    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AtLeast(1));

    uiManager_->closeModal();
    uiManager_->update(0.016f);
}

// ============================================================================
// DemolishDrag_ZoneTool_DoesNotTrigger
//
// Zone tool must NOT enter demolish code path.
//
// Input arbitration: tool-mode is checked before any tile-interaction handler.
// The Zone tool's onMouseButtonDown is the zone rect-select anchor, not demolish.
// ============================================================================
TEST_F(DemolitionInputTest, DemolishDrag_ZoneTool_DoesNotTrigger)
{
    activateZoneTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(renderer_, setZoneHoverColour(_)).Times(AnyNumber());
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(sim_, isWithinRoadRange(_, _, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(sim_, getTreasuryBalance()).WillRepeatedly(Return(100000.0f));

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_FALSE(uiManager_->hasActiveModal());
}
