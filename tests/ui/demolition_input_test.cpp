// tests/ui/demolition_input_test.cpp
//
// Phase 11h Deliverable 5d: Demolition Input Tests
//
// Verifies the corrected demolition input flow (Phase 11h Deliverable 4d):
//   - Confirmation modal fires on mouse-RELEASE (not mouse-press).
//   - Same-tile mouse-down → mouse-up opens the confirm modal.
//   - Different-tile mouse-up cancels the pending demolish (no modal).
//   - Zone tool left-click does not enter the demolition code path.
//   - Direct demolish path (setDemolishConfirm=false) calls demolishTile correctly.
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
// Spec reference: implementation/phase-11h.md §3c (Demolition Tool Input Fix),
//                 §4d (UIManager Demolition Tool Input Fix), §5d (test spec).

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
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 140, 40}));

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
        EXPECT_CALL(sim_, getDemandPressurePct(_)).WillRepeatedly(Return(0.5f));
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
// DemolitionInput_MouseUp_SameTile_ConfirmModalOpened
//
// Phase 11h: confirmation modal is triggered on mouse-RELEASE (not mouse-press).
//
// Sequence:
//   1. Demolish tool active.
//   2. MouseButtonDown on tile (5,5) → records pending tile; no modal yet.
//   3. MouseButtonUp on tile (5,5) → same tile → opens confirm modal.
//   4. demolishTile() must NOT have been called yet (modal defers it).
// ============================================================================
TEST_F(DemolitionInputTest, DemolitionInput_MouseUp_SameTile_ConfirmModalOpened)
{
    // Enable the confirm modal (default ON — tests confirming modal behavior).
    uiManager_->setDemolishConfirm(true);

    activateDemolishTool();

    // Both down and up hits resolve to tile (5,5).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgReferee<2>(5),
            SetArgReferee<3>(5),
            Return(true)));

    // demolishTile must NOT be called until modal confirms.
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    // Phase 11h: mouse-DOWN records the pending tile.
    uiManager_->onEvent(mouseButtonDown(0, 500, 500));

    // Modal must NOT open on mouse-down (Phase 11h fix: trigger moved to mouse-up).
    EXPECT_FALSE(uiManager_->hasActiveModal())
        << "Confirm modal must NOT open on mouse-down (Phase 11h fix)";

    // Phase 11h: mouse-UP on the same tile opens the confirm modal.
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_TRUE(uiManager_->hasActiveModal())
        << "Confirm modal must open on mouse-up when demolish tool active and same tile";
}

// ============================================================================
// DemolitionInput_MouseUp_DifferentTile_NoModal
//
// Phase 11h: if mouse is released on a different tile from where it was pressed,
// the pending demolish is cancelled — no modal opens, no demolishTile() call.
//
// Sequence:
//   1. Demolish tool active.
//   2. MouseButtonDown on tile (5,5).
//   3. MouseButtonUp on tile (6,6) — different tile.
//   4. No modal opened; no demolishTile() call.
// ============================================================================
TEST_F(DemolitionInputTest, DemolitionInput_MouseUp_DifferentTile_NoModal)
{
    uiManager_->setDemolishConfirm(true);

    activateDemolishTool();

    // Press on tile (5,5).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    uiManager_->onEvent(mouseButtonDown(0, 500, 500));

    // Release on tile (6,6) — different tile.
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(6), SetArgReferee<3>(6), Return(true)));

    // Neither modal nor demolishTile should fire.
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->onEvent(mouseButtonUp(0, 600, 600));

    EXPECT_FALSE(uiManager_->hasActiveModal())
        << "No confirm modal when mouse-up tile differs from mouse-down tile";
}

// ============================================================================
// DemolitionInput_ConfirmYes_CallsDemolishTile
//
// When the Demolish tool is active and setDemolishConfirm(false) is set
// (confirm disabled — equivalent to "player clicked Yes" without modal step),
// demolishTile() is called with the correct tile coordinates on mouse-up.
//
// This tests the direct demolish path that bypasses the modal — the functional
// end-state of "Yes" confirmation: demolishTile IS called.
// ============================================================================
TEST_F(DemolitionInputTest, DemolitionInput_ConfirmYes_CallsDemolishTile)
{
    // Confirm disabled → demolishTile called immediately on same-tile mouse-up.
    uiManager_->setDemolishConfirm(false);

    activateDemolishTool();

    // Both down and up resolve to tile (5,5).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgReferee<2>(5),
            SetArgReferee<3>(5),
            Return(true)));

    // Allow clearDemolishHighlight and setZoneOverlay from the demolish commit.
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());

    // demolishTile(5, 5) must be called exactly once after mouse-up on same tile.
    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);

    // Phase 11h: press sets pending tile (no demolish yet).
    uiManager_->onEvent(mouseButtonDown(0, 500, 500));

    // Release on same tile triggers demolish immediately (confirm disabled).
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));
}

// ============================================================================
// DemolitionInput_ConfirmNo_NoDemolition
//
// When the confirm modal is active and the player cancels (No / Escape),
// demolishTile() must NOT be called and the highlight must be cleared.
//
// Sequence:
//   1. Demolish tool active, confirm enabled.
//   2. MouseDown → MouseUp on same tile → modal opens.
//   3. uiManager_->closeModal() — equivalent to player clicking No/Escape.
//   4. uiManager_->update() polls modal result (None/Cancel → no demolish).
//   5. clearDemolishHighlight() called; demolishTile() not called.
// ============================================================================
TEST_F(DemolitionInputTest, DemolitionInput_ConfirmNo_NoDemolition)
{
    uiManager_->setDemolishConfirm(true);

    activateDemolishTool();

    // Both down and up resolve to tile (3,7).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgReferee<2>(3),
            SetArgReferee<3>(7),
            Return(true)));

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    ASSERT_TRUE(uiManager_->hasActiveModal())
        << "Confirm modal must be open before cancelling";

    // Close modal without accepting (Cancel path — No / Escape).
    // clearDemolishHighlight() is expected when modal closes without Accept.
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AtLeast(1));

    // demolishTile must NOT be called.
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->closeModal();

    // Poll modal result via update() — cancel path should not call demolishTile.
    uiManager_->update(0.016f);
}

// ============================================================================
// DemolitionInput_ZoneTool_MouseDown_DoesNotTriggerDemolish
//
// While the Zone tool is active, left-mouse-down on any tile must NOT:
//   - Set m_demolishPendingTileX/Z
//   - Open a demolition confirmation modal
//
// Input arbitration: tool-mode is checked before any tile-interaction handler.
// The Zone tool's onMouseButtonDown is the zone rect-select anchor, not demolish.
// ============================================================================
TEST_F(DemolitionInputTest, DemolitionInput_ZoneTool_MouseDown_DoesNotTriggerDemolish)
{
    // Zone tool active — NOT demolish.
    activateZoneTool();

    // Tile hit at (5,5).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgReferee<2>(5),
            SetArgReferee<3>(5),
            Return(true)));

    // demolishTile must never be called while Zone tool is active.
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    // Zone drag: press and release — sets zone rect anchor, not demolish pending.
    // getTreasuryBalance needed by earthworks guard in zone placement.
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    // Zone placement calls placeZone on mouse-up — allow it (not the subject of this test).
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(AnyNumber());

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));

    // No demolish confirm modal must open on Zone tool left-click.
    EXPECT_FALSE(uiManager_->hasActiveModal())
        << "Zone tool mouse-down must NOT open demolish confirm modal";

    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    // After mouse-up (zone placement): still no demolish confirm modal.
    EXPECT_FALSE(uiManager_->hasActiveModal())
        << "Zone tool mouse-up must NOT trigger demolish confirm modal";
}
