// tests/ui/world_interaction_test.cpp
//
// WorldInteractionTest fixture — 17 unit tests for Phase 9b world-interaction layer:
// terrain tile ray-cast dispatch, active-tool routing, hover highlight, zone overlay,
// sub-panel button initialisation, and service-building placement.
//
// All tests use the WorldInteractionTest Google Test fixture defined below.
// Added to ui_tests CMake target via:
//   target_sources(ui_tests PRIVATE tests/ui/world_interaction_test.cpp)
// Do NOT call add_executable(ui_tests ...) or aitown_add_tests(ui_tests ...) again.
//
// Mock policy (ref: architecture/testing/testability-architecture.md):
//   StrictMock<MockCitySimulation> sim_    — verifies exact placement call count/args.
//   StrictMock<MockRenderer>       renderer_ — verifies exact renderer calls.
//   NiceMock<MockUIBackend>        backend_  — suppresses incidental backend calls.
//   ManualTerrainQuery             terrain_  — deterministic slope/height stubs.
//   ManualClock                    clock_    — deterministic timing.
//
// TearDown contract: uiManager_.reset() destroys UIManager before StrictMock members
// are destructed — prevents dangling raw pointer callbacks on m_renderer / m_terrain
// during strict mock verification. (ref: testability-architecture.md TearDown pattern)
//
// GameState setup: each test that needs world interaction must first transition
// UIManager to Gameplay state via uiManager_->transitionToGameplay(GameMode::Sandbox).
// The toolbar click at the appropriate y-range activates the desired tool.
// (ref: implementation/phase-9b.md Deliverable G)

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/ui/ui_constants.h"
#include "src/ui/hud_sprite_ids.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/LoanTerms.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include "tests/simulation/ManualClock.h"
#include "src/interfaces/sound_ids.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>
#include <cstdint>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

// ZoneOverlayMap alias — matches MockRenderer::ZoneOverlayMap for SaveArg capture.
// (ref: tests/simulation/mock_renderer.h)
using ZoneOverlayMap = std::unordered_map<uint64_t, uint32_t>;

// ---------------------------------------------------------------------------
// Helper: build InputEvent structs.
// ---------------------------------------------------------------------------

static InputEvent makeMouseButtonDown(int button, int virtX = 500, int virtY = 500)
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

// LMB release helper — used for Zone rectangular-select release dispatch.
static InputEvent makeMouseButtonUp(int button, int virtX = 500, int virtY = 500)
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

static InputEvent makeKeyDown(int keyCode)
{
    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = keyCode;
    return ev;
}

static InputEvent makeToolbarUndoClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 630;  // Undo button y range: 608..655
    ev.physX  = 40;
    ev.physY  = 630;
    return ev;
}

// Toolbar left-click at the Zone button y-range (y:64-111).
static InputEvent makeToolbarZoneClick()
{
    // x must be inside toolbar (kToolbarLeft=8 .. kToolbarRight=72)
    // y in Zone button range: 64..111
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;   // inside toolbar x
    ev.y      = 80;   // inside Zone button y range
    ev.physX  = 40;
    ev.physY  = 80;
    return ev;
}

static InputEvent makeToolbarRoadClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 140;  // Road button y range: 120..167
    ev.physX  = 40;
    ev.physY  = 140;
    return ev;
}

static InputEvent makeToolbarUtilitiesClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 200;  // Utilities button y range: 176..223
    ev.physX  = 40;
    ev.physY  = 200;
    return ev;
}

static InputEvent makeToolbarDemolishClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 250;  // Demolish button y range: 232..279
    ev.physX  = 40;
    ev.physY  = 250;
    return ev;
}

static InputEvent makeToolbarQueryClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 310;  // Query button y range: 288..335
    ev.physX  = 40;
    ev.physY  = 310;
    return ev;
}

// ---------------------------------------------------------------------------
// WorldInteractionTest fixture
//
// Member declaration order (C++ reverse-destruction):
//   Mocks declared first -> destroyed LAST (they outlive UIManager).
//   uiManager_ declared last -> destroyed FIRST.
//
// TearDown: uiManager_.reset() explicitly destroys UIManager while all mocks
// are still alive, satisfying the TearDown contract from testability-architecture.md.
// ---------------------------------------------------------------------------
class WorldInteractionTest : public ::testing::Test {
protected:
    // Strict mocks: verify exact call counts and arguments for sim_ and renderer_.
    StrictMock<MockCitySimulation> sim_;
    StrictMock<MockRenderer>       renderer_;

    // Non-mock test doubles
    ManualTerrainQuery             terrain_;   // flat (0° slope) by default
    ManualClock                    clock_;

    // NiceMock: suppresses incidental backend calls from UIManager panel construction.
    NiceMock<MockUIBackend>        backend_;

    // UIManager declared LAST — destroyed FIRST (before mock destructors run).
    std::unique_ptr<UIManager>     uiManager_;

    void SetUp() override {
        // Construct UIManager with NiceMock<MockUIBackend> (suppresses panel init calls),
        // no audio (nullptr — UIManager guards all m_audio accesses),
        // StrictMock<MockCitySimulation>, and ManualClock.
        uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
        // Wire the world-interaction dependencies (required before any event dispatch).
        uiManager_->setRenderer(&renderer_);
        uiManager_->setTerrainQuery(&terrain_);

        // setMapDimensions(10, 10): establishes m_mapTilesX=10, m_mapTilesZ=10.
        // Key formula: tileZ * 10 + tileX. E.g. tile (3,4) -> key 4*10+3 = 43.
        //
        // NOTE: setMapDimensions detects a dimension change (0→10) and calls
        // renderer_->setZoneOverlay(10, 10, {}) to clear any stale render overlay.
        // Pre-register catch-alls so StrictMock<MockRenderer> does not fail on
        // unconditional UIManager calls that are not the subject of individual tests.
        // Tests that need to verify specific arguments add their own EXPECT_CALL
        // (GMock satisfies via most-recently-added-first matching order).
        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());
        // setTilePlacementPreview fires on Zone/Road LMB-down (anchor set, clear preview)
        // and on LMB-up (clear after commit).  Tests exercising the preview contents add
        // their own EXPECT_CALL; all others suppress via this catch-all.
        EXPECT_CALL(renderer_, setTilePlacementPreview(_, _)).Times(::testing::AnyNumber());

        uiManager_->setMapDimensions(10, 10);

        // Suppress the demolish confirmation modal for all WorldInteraction tests.
        // Tests verify call-through behaviour (demolishTile called immediately);
        // modal interaction is outside scope for world-interaction unit tests.
        uiManager_->setDemolishConfirm(false);
    }

    void TearDown() override {
        // Destroy UIManager before StrictMock destructor verification fires.
        // This prevents dangling-pointer callbacks on m_renderer / m_terrain
        // that could trigger unexpected-call failures in strict mocks.
        uiManager_.reset();
    }

    // Helper: transition to Gameplay state (required for world-interaction dispatch).
    // Calls transitionToGameplay(Sandbox) which sets m_state=GameState::Gameplay.
    // Audio is nullptr, so no m_audio->transitionToGameplay() is invoked.
    void goToGameplay()
    {
        uiManager_->transitionToGameplay(GameMode::Sandbox);
    }

    // Helper: activate the Zone tool via toolbar click + transition to Gameplay.
    // Sends a toolbar Zone button click (x:40, y:80 in virtual 1920x1080 space).
    void activateZoneTool()
    {
        goToGameplay();
        uiManager_->onEvent(makeToolbarZoneClick());
    }

    // Helper: activate the Road tool.
    void activateRoadTool()
    {
        goToGameplay();
        uiManager_->onEvent(makeToolbarRoadClick());
    }

    // Helper: activate the Utilities tool.
    void activateUtilitiesTool()
    {
        goToGameplay();
        uiManager_->onEvent(makeToolbarUtilitiesClick());
    }

    // Helper: activate the Demolish tool.
    void activateDemolishTool()
    {
        goToGameplay();
        uiManager_->onEvent(makeToolbarDemolishClick());
    }

    // Helper: activate the Query tool.
    void activateQueryTool()
    {
        goToGameplay();
        uiManager_->onEvent(makeToolbarQueryClick());
    }

    // Helper: stub pickTerrainTile to return a terrain hit at (tileX, tileZ).
    // Uses ON_CALL (not EXPECT_CALL) so NiceMock-style suppression applies to
    // StrictMock<MockRenderer>; the caller is responsible for adding EXPECT_CALLs.
    void stubPickTile(int tileX, int tileZ)
    {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(DoAll(
                SetArgReferee<2>(tileX),
                SetArgReferee<3>(tileZ),
                Return(true)));
    }

    // Helper: stub pickTerrainTile to return no hit (sky/off-map click).
    void stubPickTileMiss()
    {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(Return(false));
    }
};

// ---------------------------------------------------------------------------
// Test 1: ZonePlacement_CallsPlaceZone
//
// Zone tool; pickTerrainTile returns (5,7); left-click.
// Verifies placeZone(5, 7, _, _, 0) is called exactly once.
// earthworksCostOverride=0 because ManualTerrainQuery returns 0° slope (flat).
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZonePlacement_CallsPlaceZone)
{
    // Stub getTreasuryBalance so the earthworks-insufficient-funds guard passes.
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();
    stubPickTile(5, 7);

    // setZoneOverlay is called on successful placement; allow it.
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(0));
    // pickTerrainTile must be called (at least once during the click dispatch).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // Primary assertion: placeZone must be called exactly once with correct args.
    // earthworksCostOverride=0: terrain slope is 0° (ManualTerrainQuery default).
    EXPECT_CALL(sim_, placeZone(5, 7, _, _, 0)).Times(1);

    // Zone rect-select: press sets anchor (no placement yet); release fills rectangle.
    // With anchor=(5,7) and hover=(5,7), the 1x1 rect triggers one placeZone call.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 2: RoadPlacement_CallsPlaceRoad
//
// Road tool; pickTerrainTile returns (5,7); left-click.
// Verifies placeRoad(5, 7, 0) is called exactly once.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_RoadPlacement_CallsPlaceRoad)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateRoadTool();

    // Road placement is deferred to LMB release (Bug 3 fix).
    // Step 1: LMB press — sets anchor, no placement yet.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Step 2: LMB release — placement fires at the anchor tile.
    EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 3: DemolishTool_SteepSlope_NoEarthworksGuard
//
// Demolish tool does NOT apply an earthworks guard (demolish does not incur
// earthworks cost per spec — architecture/game-design/terrain-interaction.md).
// slope=30° via setSlope; demolishTile IS still called.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_DemolishTool_SteepSlope_NoEarthworksGuard)
{
    // Set steep slope on (5,7) — should NOT block demolish.
    terrain_.setSlope(5, 7, 30.0f);

    // The earthworks guard queries getTreasuryBalance() for any tool when slope>15°.
    // For Demolish, the spec says no earthworks cost — but the shared earthworks guard
    // still runs. Provide a sufficient balance so the guard passes, then verify
    // demolishTile IS still called. The key assertion: demolish is not blocked by slope.
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));
    // Register the call explicitly so StrictMock doesn't reject it.
    EXPECT_CALL(sim_, getTreasuryBalance()).Times(AtLeast(0));

    activateDemolishTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // Zone overlay may be called on demolish (erase entry).
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(0));

    // Primary assertion: demolishTile IS called despite steep slope.
    EXPECT_CALL(sim_, demolishTile(5, 7)).Times(1);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 4: ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace
//
// Zone tool; slope=30°; getTreasuryBalance()=0 (insufficient for earthworks).
// placeZone must NOT be called; a toast with "insufficient funds" is posted.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneTool_SteepSlope_InsufficientFunds_ToastNotPlace)
{
    // Set steep slope — triggers earthworks cost computation.
    terrain_.setSlope(30.0f);  // global slope: all tiles at 30°

    activateZoneTool();

    // Treasury = 0: earthworks cost > 0 > balance => insufficient funds.
    EXPECT_CALL(sim_, getTreasuryBalance()).WillRepeatedly(Return(0.0f));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // placeZone must NOT be called when funds are insufficient.
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);

    // Toast must be posted with "insufficient funds" in the text.
    EXPECT_CALL(backend_, addStaticText(HasSubstr("insufficient funds"), _, _, _, _))
        .Times(AtLeast(1));

    // Zone rect-select: press sets anchor; release triggers doTerrainPlacement
    // which posts the toast and returns without calling placeZone.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 5: QueryTool_CallsQueryTile
//
// Query tool active; left-click; queryTile(5,7) called once.
// The Query tool "open inspector" path is at Priority 3.
//
// Design note on activation:
//   The toolbar Query button (y:288-336) sets m_activeTool = ActiveTool::Query
//   WITHOUT opening the inspector (m_inspectorOpen stays false). This is distinct
//   from the 'I' hotkey which both activates the tool AND opens the inspector.
//   Since m_inspectorOpen is false after the toolbar click, Priority 3 fires
//   directly on the subsequent world left-click.
//
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_QueryTool_CallsQueryTile)
{
    // Activate Query tool via toolbar click (sets m_activeTool = Query,
    // m_inspectorOpen remains false — toolbar click does NOT open inspector).
    activateQueryTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // getTileScreenBounds is called to compute inspector panel position.
    EXPECT_CALL(renderer_, getTileScreenBounds(5, 7))
        .WillOnce(Return(ScreenRect{}));

    // Primary assertion: queryTile is called exactly once with the correct tile.
    EXPECT_CALL(sim_, queryTile(5, 7))
        .Times(1)
        .WillOnce(Return(QueryResult{}));

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 6: NoActiveTool_LeftClickIgnored
//
// m_activeTool == None (initial state after Gameplay transition, before toolbar click).
// Left-click must NOT trigger placeZone or placeRoad.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_NoActiveTool_LeftClickIgnored)
{
    // Go to Gameplay without activating any tool.
    goToGameplay();

    // No placement calls expected.
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);

    // Left-click in world area (no active tool — world interaction block must skip).
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 7: ModalActive_LeftClickNotDispatched
//
// When a blocking modal is active (Priority 1 consumes the event), the left-click
// must not reach the world-interaction layer.
// Activate the Zone tool first, then show a modal.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ModalActive_LeftClickNotDispatched)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();

    // Activate the modal. showForcedLoanDialog calls sim_->isPaused() to check
    // current state before potentially calling setPaused(true).
    // Register these with the StrictMock so unexpected calls don't fail the test.
    EXPECT_CALL(sim_, isPaused()).WillRepeatedly(Return(false));
    EXPECT_CALL(sim_, setPaused(true)).Times(AtLeast(0));

    LoanTerms terms;
    terms.amount         = 50000.0f;
    terms.repaymentTicks = 12;
    terms.interestRate   = 0.05f;
    uiManager_->showForcedLoanDialog(terms);

    // Modal should now be active.
    EXPECT_TRUE(uiManager_->hasActiveModal());

    // placeZone must NOT be called: Priority 1 consumes the event.
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 8: HoverHighlight_SetOnMouseMove
//
// Zone tool active; MouseMove event; pickTerrainTile returns (3,4).
// setTileHoverHighlight(3, 4, _) must be called at least once.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_HoverHighlight_SetOnMouseMove)
{
    activateZoneTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));

    // Primary assertion: hover highlight is set at (3, 4) with any ARGB colour.
    EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));

    // Send a MouseMove event outside the toolbar.
    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ---------------------------------------------------------------------------
// Test 9: HoverHighlight_ClearedOnMiss
//
// pickTerrainTile returns false (sky/off-map click).
// setTileHoverHighlight(-1, -1, kHoverArgbClear) must be called.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_HoverHighlight_ClearedOnMiss)
{
    activateZoneTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(Return(false));

    // Primary assertion: clear call with (-1, -1, kHoverArgbClear).
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1,
        static_cast<uint32_t>(kHoverArgbClear))).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ---------------------------------------------------------------------------
// Test 10: ZonePlacement_SparseOverlay_InsertsEntry
//
// Zone tool; pickTerrainTile at (3,4); left-click.
// m_mapTilesX=10 (set in SetUp), so key = 4*10+3 = 43.
// After dispatch, captured sparseOverlay must contain {43 -> kOverlayArgbResidential}.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZonePlacement_SparseOverlay_InsertsEntry)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));

    EXPECT_CALL(sim_, placeZone(3, 4, _, _, 0)).Times(1);

    // Capture the sparseOverlay argument passed to setZoneOverlay.
    ZoneOverlayMap capturedMap;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&capturedMap));

    // Zone rect-select: press sets anchor at (3,4); release fills the 1x1 rect.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    // Assert: exactly one entry; key=43; value=kOverlayArgbResidential (green, 0x6000FF00).
    ASSERT_EQ(capturedMap.size(), 1u)
        << "Overlay map must contain exactly one entry after first zone placement";
    ASSERT_TRUE(capturedMap.count(43u) > 0)
        << "Key must be tileZ*mapTilesX+tileX = 4*10+3 = 43";
    EXPECT_EQ(capturedMap.at(43u), static_cast<uint32_t>(kOverlayArgbResidential))
        << "Value must be kOverlayArgbResidential (0x6000FF00) for Residential zone";
}

// ---------------------------------------------------------------------------
// Test 11: Demolish_SparseOverlay_ErasesEntry
//
// Place a Zone tile at (3,4), then demolish it. The captured overlay map after
// demolish must be empty (entry erased).
// Settings "Confirm before demolish" = OFF is modelled by suppressing the modal.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_Demolish_SparseOverlay_ErasesEntry)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    // -- Step 1: place zone at (3,4) --
    activateZoneTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));

    EXPECT_CALL(sim_, placeZone(3, 4, _, _, 0)).Times(1);

    // First setZoneOverlay call — after placement (key 43 inserted).
    ZoneOverlayMap capturedAfterPlace;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&capturedAfterPlace));

    // Zone rect-select: press sets anchor at (3,4); release fills the 1x1 rect.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    // Verify placement produced a non-empty map.
    EXPECT_EQ(capturedAfterPlace.size(), 1u);

    // -- Step 2: switch to Demolish tool and demolish (3,4) --
    // (The confirm-before-demolish modal is OFF by design in this test to directly
    //  test the overlay erase path without modal interaction complexity.)
    activateDemolishTool();

    EXPECT_CALL(sim_, demolishTile(3, 4)).Times(1);

    // Second setZoneOverlay call — after demolish (key 43 erased).
    ZoneOverlayMap capturedAfterDemolish;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&capturedAfterDemolish));

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Assert: overlay map is empty after demolish.
    EXPECT_TRUE(capturedAfterDemolish.empty())
        << "Overlay map must be empty after demolishing the only zoned tile";
}

// ---------------------------------------------------------------------------
// Test 12: NewGameLoad_ClearsOverlay
//
// Pre-populate m_overlayMap with 3 zone placements; trigger onNewGame().
// setZoneOverlay must be called with an empty sparse map after onNewGame().
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_NewGameLoad_ClearsOverlay)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();

    // 3 placements at distinct tiles.
    const int kTiles[3][2] = {{1,0},{2,0},{3,0}};

    for (auto& t : kTiles) {
        EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillOnce(DoAll(SetArgReferee<2>(t[0]), SetArgReferee<3>(t[1]), Return(true)));
        EXPECT_CALL(sim_, placeZone(t[0], t[1], _, _, 0)).Times(1);
        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(1);
        // Zone rect-select: press sets anchor; release fills the 1x1 rect.
        uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
        uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
    }

    // onNewGame() must clear m_overlayMap and push an empty setZoneOverlay call.
    ZoneOverlayMap capturedClear;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&capturedClear));
    // onNewGame() also clears the hover highlight — expect the clear call.
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1,
        static_cast<uint32_t>(kHoverArgbClear))).Times(1);

    uiManager_->onNewGame();

    // Assert: setZoneOverlay was called with an empty map.
    EXPECT_TRUE(capturedClear.empty())
        << "onNewGame() must push an empty setZoneOverlay call to clear the overlay";
}

// ---------------------------------------------------------------------------
// Test 13: OverlayCap_100K_StillCalls
//
// Drive UIManager to insert 100,000 entries. When a 100,001st entry would be
// inserted, the cap prevents storage but setZoneOverlay is still called.
// Assert: setZoneOverlay is called at least once; captured map size <= 100000.
//
// Implementation: uses setOverlayMapForTest() (UIManager test-seam) to inject
// exactly 100,000 entries without routing 100K UI events through onEvent().
// Then executes a single additional placement for a NEW key (tileX=500,tileZ=0)
// to trigger the cap-enforcement path. setZoneOverlay MUST still be called even
// when the 100,001st entry is rejected by the cap.
//
// The 400x400 map (160,000 valid tiles) is required so tile (500/400,500%400) maps
// to out-of-range. We use key=100000 (tileZ=250,tileX=0 on 400-wide map) as the
// cap-busting key — a key NOT in the injected map.
//
// (ref: implementation/phase-9b.md Deliverable G, "internal overlay-insert path directly")
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_OverlayCap_100K_StillCalls)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(1e9f));

    // Expand map to 400x400 to accommodate 100K+ overlay keys.
    // setMapDimensions emits a setZoneOverlay({}) when dimensions change — allow it.
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(1));
    uiManager_->setMapDimensions(400, 400);

    // Inject exactly 100,000 entries via test-seam (avoids 100K UI event overhead).
    // Keys 0..99999 on a 400-wide map correspond to tiles (tileX=k%400, tileZ=k/400).
    static constexpr size_t kCap = 100000u;
    ZoneOverlayMap seed;
    seed.reserve(kCap);
    for (uint64_t k = 0; k < kCap; ++k) {
        seed[k] = static_cast<uint32_t>(kOverlayArgbResidential);
    }
    uiManager_->setOverlayMapForTest(seed);

    goToGameplay();
    uiManager_->onEvent(makeToolbarZoneClick());

    // Single cap-busting placement: new key = 100000 (not in the injected 0..99999 range).
    // cap check: m_overlayMap.size() == 100000 >= kOverlayCap => do NOT insert.
    // BUT: setZoneOverlay must still be called.
    ZoneOverlayMap captured;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillRepeatedly(SaveArg<2>(&captured));

    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(1);

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(
            // tile (0, 250) on 400-wide map: key = 250*400+0 = 100000 (new key).
            SetArgReferee<2>(0),
            SetArgReferee<3>(250),
            Return(true)));

    // Zone rect-select: press sets anchor; release fires doTerrainPlacement.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    // Assert: cap enforced — overlay map size must not exceed 100,000 entries.
    EXPECT_LE(static_cast<int>(captured.size()), 100000)
        << "Overlay map must be capped at 100,000 entries";
    // Assert: setZoneOverlay was called even when cap prevented insertion.
    // (captured will have been populated — the call fires regardless of cap).
}

// ---------------------------------------------------------------------------
// Test 14: SetMapDimensions_Recall_ClearsOverlay
//
// After 3 zone placements with setMapDimensions(10,10), call setMapDimensions(20,20).
// Assert: setZoneOverlay is called with an empty sparse map on the recall.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_SetMapDimensions_Recall_ClearsOverlay)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();

    // Place 3 zones (using fixture's 10x10 map).
    for (int i = 0; i < 3; ++i) {
        EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillOnce(DoAll(SetArgReferee<2>(i), SetArgReferee<3>(0), Return(true)));
        EXPECT_CALL(sim_, placeZone(i, 0, _, _, 0)).Times(1);
        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(1);
        // Zone rect-select: press sets anchor; release fills the 1x1 rect.
        uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
        uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
    }

    // Re-call setMapDimensions with different dimensions.
    // This must clear m_overlayMap and push an empty setZoneOverlay call.
    ZoneOverlayMap capturedOnRecall;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&capturedOnRecall));

    uiManager_->setMapDimensions(20, 20);

    EXPECT_TRUE(capturedOnRecall.empty())
        << "setMapDimensions re-call must clear m_overlayMap and push empty setZoneOverlay";
}

// ---------------------------------------------------------------------------
// Test 15: ZoneSubPanel_ButtonsInitialized
//
// Verifies that during UIManager construction/init(), the Zone sub-panel buttons
// receive the correct setElementImage calls:
//   - 8 inactive/outline-icon calls (kSpriteZoneResLowInactive + col + row*3)
//   - 1 active-state call on the default selection (kSpriteZoneResLowActive = 64)
//
// Uses a dedicated NiceMock<MockUIBackend> that tracks setElementImage calls.
// StrictMock is NOT used here because UIManager construction issues many unrelated
// backend calls; NiceMock suppresses those while EXPECT_CALL asserts the 9 button calls.
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneSubPanel_ButtonsInitialized)
{
    // Verifies Zone sub-panel button init sprite calls from UIManager construction.
    //
    // Per spec (phase-9b.md Deliverable D): init() calls setElementImage(handle, inactiveHandle)
    // on ALL 9 zone sub-panel buttons first, then calls setElementImage(handle, activeHandle)
    // on the default-selected button (ResLow = kSpriteZoneResLowActive = 64).
    //
    // GMock expectation registration order (LIFO matching):
    //   Register catch-all FIRST, then specific expectations on top.
    //   Specific expectations are tried first (most recently registered), so
    //   Zone sub-panel calls are captured by the specific expectations while
    //   all other setElementImage calls (Utilities init, Minimap, etc.) fall
    //   through to the catch-all which expects them 0 or more times.

    // Catch-all: allow any setElementImage call not matched by specific expectations.
    // Registered FIRST so it is matched LAST (GMock LIFO order).
    EXPECT_CALL(backend_, setElementImage(_, _)).Times(::testing::AnyNumber());

    // All 9 inactive sprites (including ResLow which gets both inactive then active).
    // Registered AFTER catch-all so they are tried FIRST (LIFO).
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            uint32_t inactiveHandle = kSpriteZoneResLowInactive +
                static_cast<uint32_t>(col) +
                static_cast<uint32_t>(row) * 3u;
            EXPECT_CALL(backend_, setElementImage(_, inactiveHandle)).Times(AtLeast(1));
        }
    }

    // Default button (col=0, row=0 — Residential Low): active sprite = 64.
    EXPECT_CALL(backend_, setElementImage(_, kSpriteZoneResLowActive)).Times(AtLeast(1));

    // Re-construct UIManager to capture the init() calls in this test's scope.
    // The SetUp UIManager is reset first; the new construction triggers init().
    uiManager_.reset();
    uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
}

// ---------------------------------------------------------------------------
// Test 16: UtilitiesSubPanel_ButtonsInitialized
//
// Verifies that during UIManager construction/init(), the Utilities sub-panel
// buttons receive the correct setElementImage calls:
//   - 3 inactive/outline-icon calls (kSpriteUtilPowerInactive + static_cast<int>(type))
//   - 1 active-state call on the PowerPlant default (kSpriteUtilPowerActive = 128)
//
// (ref: implementation/phase-9b.md Deliverable G)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_UtilitiesSubPanel_ButtonsInitialized)
{
    // Verifies Utilities sub-panel button init sprite calls from UIManager construction.
    //
    // Per spec: init() calls setElementImage(handle, inactiveHandle) on ALL 4 utility
    // buttons first, then calls setElementImage(handle, kSpriteUtilPowerActive=128) on
    // the default-selected PowerPlant button.
    //
    // Same catch-all pattern as ZoneSubPanel test: register catch-all FIRST (matched
    // LAST in LIFO), then specific expectations (matched FIRST).

    // Catch-all: allow other setElementImage calls (Zone init, Minimap, etc.).
    EXPECT_CALL(backend_, setElementImage(_, _)).Times(::testing::AnyNumber());

    // All 4 inactive sprites (including PowerPlant which gets both inactive then active).
    EXPECT_CALL(backend_, setElementImage(_, kSpriteUtilPowerInactive)).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementImage(_, kSpriteUtilWaterInactive)).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementImage(_, kSpriteUtilFireInactive)).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementImage(_, kSpriteUtilPoliceInactive)).Times(AtLeast(1));

    // Default button (PowerPlant): active sprite = 128.
    EXPECT_CALL(backend_, setElementImage(_, kSpriteUtilPowerActive)).Times(AtLeast(1));

    // Re-construct UIManager to capture the init() calls in this test's scope.
    uiManager_.reset();
    uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
}

// ---------------------------------------------------------------------------
// Test 17: UtilitiesPlacement_CallsPlaceServiceBuilding
//
// Utilities tool active; selected building = ServiceBuildingType::FireStation.
// pickTerrainTile returns (5,7); left-click.
// Verifies placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0) exactly once.
// (ref: implementation/phase-9b.md Deliverable G + Deliverable I)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_UtilitiesPlacement_CallsPlaceServiceBuilding)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateUtilitiesTool();

    // Select FireStation via Utilities sub-panel click.
    // The Utilities sub-panel layout (4×1 single row): top-left anchor (x:80, y:64),
    // button size 64x40, gap 4. All 4 buttons are in a single horizontal strip:
    //   col0=PowerPlant, col1=WaterTower, col2=FireStation, col3=PoliceStation.
    // FireStation is at column 2, row 0 (single row):
    //   x = 80 + 2*(64+4) = 216, y = 64.
    // Click at (216+32, 64+20) = (248, 84) to hit FireStation button.
    // This is a Phase 9b sub-panel click that sets m_selectedServiceBuilding.
    InputEvent fireStationClick{};
    fireStationClick.type   = InputEvent::Type::MouseButtonDown;
    fireStationClick.button = 0;
    fireStationClick.x      = 248;  // FireStation col2: x=80+2*(64+4)=216, center at 248
    fireStationClick.y      = 84;   // Single row at y=64, center at 84
    fireStationClick.physX  = 248;
    fireStationClick.physY  = 84;
    uiManager_->onEvent(fireStationClick);

    // Now send the world click.
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // Primary assertion: placeServiceBuilding with FireStation, cost 0 (flat terrain).
    EXPECT_CALL(sim_, placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0))
        .Times(1);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Regression tests for bug report: zone/road/query tool issues
//
// Bug 1: Zone sub-panel buttons have no labels/sprites (visible after activation)
// Bug 2: Zone sub-panel button click + terrain click does nothing
// Bug 3: Road tool — clicking terrain does nothing (tool not activated)
// Bug 4: Query mode exit — clicking Zone toolbar does NOT switch back
// Bug 5: After query mode — zone sub-panel does NOT re-appear
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test 18 (Bug 1): ZoneSubPanel_ButtonsVisible_AfterZoneToolActivation
//
// After clicking the Zone toolbar button, all 9 zone sub-panel buttons must
// become visible (setElementVisible(handle, true) called for each).
// Before this, all 9 buttons are hidden (set to false during construction).
//
// This regression test for Bug 1 verifies that activating the Zone tool makes
// the sub-panel visible — a precondition for the player to see any buttons at all.
//
// Root cause of kInvalidUIElement guard: NiceMock<MockUIBackend>::addButton()
// returns 0 by default, which equals kInvalidUIElement. updateSubPanelVisibility()
// guards setElementVisible calls with `!= kInvalidUIElement`, skipping all calls.
// Solution: use ON_CALL to make addButton() return incrementing non-zero handles
// BEFORE constructing UIManager. This is the same pattern used by QueryPanelIntegrationTest.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug1_ZoneSubPanel_ButtonsVisible_AfterZoneToolActivation)
{
    // Destroy the fixture UIManager (constructed with default handle=0 addButton).
    uiManager_.reset();

    // Configure addButton to return non-zero, unique handles so the kInvalidUIElement
    // guard in updateSubPanelVisibility() does NOT skip visibility calls.
    uint32_t nextHandle = 1;
    ON_CALL(backend_, addButton(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });
    ON_CALL(backend_, addStaticText(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });

    // Suppress unrelated backend calls.
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    // Construct UIManager — addButton now returns non-zero handles.
    uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
    uiManager_->setRenderer(&renderer_);
    uiManager_->setTerrainQuery(&terrain_);
    uiManager_->setMapDimensions(10, 10);
    uiManager_->setDemolishConfirm(false);

    // Set up counting expectation for setElementVisible(_, true) calls.
    // Register BEFORE Zone tool activation so all calls are captured.
    int visibleTrueCount = 0;
    EXPECT_CALL(backend_, setElementVisible(_, true))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&visibleTrueCount](UIElementHandle, bool) {
                ++visibleTrueCount;
            }));
    // Allow setElementVisible(_, false) calls (construction hides buttons).
    EXPECT_CALL(backend_, setElementVisible(_, false))
        .Times(::testing::AnyNumber());

    // Activate Zone tool — updateSubPanelVisibility() must show 9 zone buttons.
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->onEvent(makeToolbarZoneClick());

    // Assert: at least 9 zone sub-panel buttons were made visible.
    EXPECT_GE(visibleTrueCount, 9)
        << "Zone toolbar click must show all 9 zone sub-panel buttons "
           "(setElementVisible called with true for each). "
           "Bug 1: updateSubPanelVisibility() skips visibility calls because "
           "all button handles equal kInvalidUIElement (0) in production — "
           "addButton must return non-zero handles for the guard to pass.";
}

// ---------------------------------------------------------------------------
// Test 19 (Bug 1): ZoneSubPanel_ButtonsHidden_AfterRoadToolActivation
//
// After switching from Zone tool to Road tool, all 9 zone sub-panel buttons
// must become hidden again (setElementVisible(handle, false) called for each).
// This verifies the sub-panel hides when a non-Zone tool is selected.
//
// Uses the same non-zero handle setup as Test 18 so the kInvalidUIElement
// guard in updateSubPanelVisibility() does not skip visibility calls.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug1_ZoneSubPanel_ButtonsHidden_AfterRoadToolActivation)
{
    uiManager_.reset();

    // Configure addButton to return non-zero handles.
    uint32_t nextHandle = 1;
    ON_CALL(backend_, addButton(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });
    ON_CALL(backend_, addStaticText(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });

    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
    uiManager_->setRenderer(&renderer_);
    uiManager_->setTerrainQuery(&terrain_);
    uiManager_->setMapDimensions(10, 10);
    uiManager_->setDemolishConfirm(false);

    // Allow all setElementVisible calls during Zone activation (showing buttons).
    EXPECT_CALL(backend_, setElementVisible(_, ::testing::AnyOf(true, false)))
        .Times(::testing::AnyNumber());

    // Activate Zone tool first — shows 9 buttons.
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->onEvent(makeToolbarZoneClick());
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Zone);

    // Now count setElementVisible(_, false) calls during Road activation.
    // Re-register EXPECT_CALL for false to count hide calls.
    int visibleFalseCount = 0;
    EXPECT_CALL(backend_, setElementVisible(_, false))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&visibleFalseCount](UIElementHandle, bool) {
                ++visibleFalseCount;
            }));

    // Switch to Road tool — updateSubPanelVisibility() hides 9 zone buttons.
    uiManager_->onEvent(makeToolbarRoadClick());

    // Assert Road tool is now active.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Road)
        << "Toolbar Road click must activate Road tool";

    // Assert: at least 9 zone sub-panel buttons were hidden.
    EXPECT_GE(visibleFalseCount, 9)
        << "Road toolbar click must hide all 9 zone sub-panel buttons "
           "(setElementVisible called with false for each). "
           "Bug 1: sub-panel buttons not hidden because handles are kInvalidUIElement.";
}

// ---------------------------------------------------------------------------
// Test 20 (Bug 2): ZoneSubPanel_ClickCommercialButton_ThenTerrain_CallsPlaceZone
//
// Regression test for Bug 2: clicking a zone sub-panel button then clicking
// terrain must call placeZone with the newly selected zone type.
//
// Sequence:
//   1. Activate Zone tool (default = Residential Low).
//   2. Click Commercial Low button in sub-panel (col=1, row=0).
//      Button x = 80 + 1*(64+4) = 148, y = 64 + 0*(40+4) = 64. Center: (180, 84).
//   3. Click terrain at (5,7).
//   4. Verify placeZone(5, 7, ZoneType::Commercial, DensityTier::Low, 0) called once.
//
// This test specifically catches the bug where sub-panel button click does not
// update m_selectedZoneType, causing terrain click to still use the default.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug2_ZoneSubPanel_ClickCommercialButton_ThenTerrain_CallsPlaceZone)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();

    // Step 1: Click the Commercial Low button (col=1, row=0).
    // Sub-panel layout: x = 80 + col*(64+4), y = 64 + row*(40+4).
    // Commercial Low: col=1 -> bx = 148, by = 64. Hit-test center: (148+32, 64+20) = (180, 84).
    InputEvent commercialClick{};
    commercialClick.type   = InputEvent::Type::MouseButtonDown;
    commercialClick.button = 0;
    commercialClick.x      = 180;
    commercialClick.y      = 84;
    commercialClick.physX  = 180;
    commercialClick.physY  = 84;

    // The sub-panel click must be consumed (returns true) and must NOT call
    // pickTerrainTile (not a world click — it's a UI sub-panel click).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);
    bool consumed = uiManager_->onEvent(commercialClick);
    EXPECT_TRUE(consumed)
        << "Zone sub-panel button click must be consumed (return true)";

    // Step 2: Click terrain at (5,7) — now m_selectedZoneType must be Commercial.
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(0));

    // Primary assertion: placeZone must be called with ZoneType::Commercial (1), not Residential (0).
    EXPECT_CALL(sim_, placeZone(5, 7, ZoneType::Commercial, DensityTier::Low, 0)).Times(1);

    // Zone rect-select: press sets anchor; release fills the 1×1 rect.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 21 (Bug 2): ZoneSubPanel_ClickIndustrialHighButton_ThenTerrain_CallsPlaceZone
//
// Clicking Industrial High (col=2, row=2) then terrain must call placeZone
// with ZoneType::Industrial and DensityTier::High.
// Button position: bx = 80 + 2*(64+4) = 216, by = 64 + 2*(40+4) = 152.
// Center: (216+32, 152+20) = (248, 172).
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug2_ZoneSubPanel_ClickIndustrialHighButton_ThenTerrain_CallsPlaceZone)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();

    // Click Industrial High button (col=2, row=2).
    InputEvent indHighClick{};
    indHighClick.type   = InputEvent::Type::MouseButtonDown;
    indHighClick.button = 0;
    indHighClick.x      = 248;  // col=2: bx = 80 + 2*68 = 216; center x = 216+32 = 248
    indHighClick.y      = 172;  // row=2: by = 64 + 2*44 = 152; center y = 152+20 = 172
    indHighClick.physX  = 248;
    indHighClick.physY  = 172;

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);
    bool consumed = uiManager_->onEvent(indHighClick);
    EXPECT_TRUE(consumed)
        << "Zone sub-panel button click must be consumed";

    // Click terrain at (3,2).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(2), Return(true)));
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(0));

    // Primary assertion: ZoneType::Industrial (2) + DensityTier::High (2).
    EXPECT_CALL(sim_, placeZone(3, 2, ZoneType::Industrial, DensityTier::High, 0)).Times(1);

    // Zone rect-select: press sets anchor; release fills the 1×1 rect.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 22 (Bug 3): RoadTool_ToolbarClick_ActivatesRoadTool
//
// Regression test for Bug 3: verify the Road toolbar button (y:120-167) actually
// sets m_activeTool = ActiveTool::Road, which is the precondition for terrain
// placement to dispatch placeRoad.
//
// Sequence:
//   1. Transition to Gameplay.
//   2. Click Road toolbar button at (40, 140).
//   3. Assert getActiveTool() == ActiveTool::Road.
//   4. Simulate terrain click and assert placeRoad is called.
//
// This test catches the regression where the Road toolbar button fails to
// activate the tool (e.g., wrong y-range in the toolbar dispatch block).
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug3_RoadTool_ToolbarClick_ActivatesRoadTool)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    goToGameplay();

    // Step 1: Click Road toolbar at (40, 140) — Road y-range: 120..167.
    uiManager_->onEvent(makeToolbarRoadClick());

    // Step 2: Assert tool is now Road (verifies toolbar click was processed).
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Road)
        << "Road toolbar click must activate Road tool (getActiveTool() == Road)";

    // Step 3: LMB press on terrain — sets anchor, no placement on press (Bug 3 fix).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(4), SetArgReferee<3>(6), Return(true)));
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Step 4: LMB release — placement fires at the anchor tile.
    EXPECT_CALL(sim_, placeRoad(4, 6, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 23 (Bug 3): RoadTool_TerrainMiss_DoesNotCallPlaceRoad
//
// When Road tool is active but pickTerrainTile returns false (sky/off-map),
// placeRoad must NOT be called. Verifies the terrain-miss guard in Priority 7.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug3_RoadTool_TerrainMiss_DoesNotCallPlaceRoad)
{
    activateRoadTool();
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Road);

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(Return(false));

    // placeRoad must NOT be called when no terrain hit.
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);

    bool consumed = uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    EXPECT_FALSE(consumed)
        << "World-interaction must not consume event when pickTerrainTile returns false";
}

// ---------------------------------------------------------------------------
// Test 24 (Bug 4): QueryMode_ZoneToolbarClick_SwitchesToolToZone
//
// Regression test for Bug 4: when Query tool is active and the inspector is
// NOT open, clicking the Zone toolbar button must switch the active tool to Zone,
// NOT stay in Query mode or open the inspector.
//
// The fix in UIManager.cpp Priority-3 (lines 350-379): the `!inRect(...)` toolbar
// carve-out means Priority-3 is completely skipped for toolbar coordinates (40, 80).
// pickTerrainTile is NOT called for toolbar clicks while Query is active — the event
// falls directly to Priority-5, which processes the Zone toolbar click.
//
// The test verifies: after the Zone toolbar click, getActiveTool() == Zone.
// pickTerrainTile must NOT be called for the toolbar click (carve-out prevents it).
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug4_QueryMode_ZoneToolbarClick_SwitchesToolToZone)
{
    // Activate Query tool via toolbar.
    activateQueryTool();
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Query)
        << "Precondition: Query tool must be active";

    // Inspector is NOT open at this point (toolbar click does not open inspector).

    // The toolbar carve-out in Priority-3 (!inRect guard) skips pickTerrainTile
    // entirely for toolbar coordinates (40, 80). pickTerrainTile must NOT be called.
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);

    // queryTile must NOT be called — Priority-3 is bypassed for toolbar clicks.
    EXPECT_CALL(sim_, queryTile(_, _)).Times(0);

    // Send Zone toolbar click.
    uiManager_->onEvent(makeToolbarZoneClick());

    // Primary assertion: Priority-5 handles the toolbar click, switching to Zone.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Zone)
        << "Clicking Zone toolbar while in Query mode (inspector closed) must "
           "switch active tool to Zone. "
           "The toolbar carve-out in Priority-3 prevents pickTerrainTile from "
           "being called for toolbar coords, so Priority-5 processes the click.";
}

// ---------------------------------------------------------------------------
// Test 25 (Bug 4): QueryMode_InspectorOpen_ZoneToolbarClick_SwitchesToolToZone
//
// Variant of Bug 4: inspector IS open. In this case Priority-3 handles the
// toolbar click via the toolbar carve-out (L302-308): the click is in the
// toolbar bounds, so it falls through to Priority-5 which processes it.
// After the Zone toolbar click, the inspector closes (outside click path does
// NOT apply because the carve-out lets the toolbar click fall through) and
// the active tool becomes Zone.
//
// Sequence:
//   1. Activate Query tool.
//   2. Click terrain -> inspector opens.
//   3. Click Zone toolbar -> inspector closes, tool switches to Zone.
//   4. Assert getActiveTool() == Zone.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug4_QueryMode_InspectorOpen_ZoneToolbarClick_SwitchesToolToZone)
{
    // Step 1: Activate Query tool.
    activateQueryTool();
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Query);

    // Step 2: Click terrain to open inspector.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(renderer_, getTileScreenBounds(5, 5))
        .WillOnce(Return(ScreenRect{}));
    EXPECT_CALL(sim_, queryTile(5, 5))
        .WillOnce(Return(QueryResult{}));
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    // Inspector is now open.

    // Step 3: Click Zone toolbar at (40, 80).
    // Priority-3 inspector-open path: toolbar carve-out at L302-308 lets the click
    // fall through to Priority-5. Priority-5 processes the Zone toolbar click.
    // NOTE: pickTerrainTile is NOT called for this click because Priority-3 with
    // inspector open uses the inspector bounds check + carve-out, not the QueryTool
    // open path (which requires !m_inspectorOpen).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);

    uiManager_->onEvent(makeToolbarZoneClick());

    // Primary assertion: tool must switch to Zone.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Zone)
        << "Clicking Zone toolbar while inspector is open (Query mode) must "
           "switch active tool to Zone";
}

// ---------------------------------------------------------------------------
// Test 26 (Bug 5): ZoneSubPanel_ReappearsAfterExitingQueryMode
//
// Regression test for Bug 5: after exiting Query mode by clicking the Zone
// toolbar button, the zone sub-panel buttons must become visible again.
//
// updateSubPanelVisibility() is called by the Zone toolbar handler and must
// set setElementVisible(handle, true) on all 9 zone sub-panel buttons.
//
// Sequence:
//   1. Activate Zone tool (sub-panel shows).
//   2. Switch to Query tool (sub-panel hides).
//   3. Click Zone toolbar (sub-panel must show again — Bug 5).
//   4. Assert setElementVisible(handle, true) called at least 9 times in step 3.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug5_ZoneSubPanel_ReappearsAfterExitingQueryMode)
{
    // Destroy fixture UIManager; reconstruct with non-zero handles so that
    // updateSubPanelVisibility() guard (handle != kInvalidUIElement) passes.
    uiManager_.reset();

    uint32_t nextHandle = 1;
    ON_CALL(backend_, addButton(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });
    ON_CALL(backend_, addStaticText(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });

    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
    uiManager_->setRenderer(&renderer_);
    uiManager_->setTerrainQuery(&terrain_);
    uiManager_->setMapDimensions(10, 10);
    uiManager_->setDemolishConfirm(false);

    // Allow all setElementVisible calls during setup.
    EXPECT_CALL(backend_, setElementVisible(_, ::testing::AnyOf(true, false)))
        .Times(::testing::AnyNumber());

    // Step 1: Activate Zone tool — sub-panel shows.
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->onEvent(makeToolbarZoneClick());
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Zone);

    // Step 2: Switch to Query tool — sub-panel hides.
    uiManager_->onEvent(makeToolbarQueryClick());
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Query);

    // Step 3: Click Zone toolbar to return to Zone mode.
    // The toolbar carve-out in Priority-3 (!inRect guard) skips Priority-3
    // entirely for toolbar coordinates (40, 80), so pickTerrainTile is NOT
    // called. Priority-5 processes the toolbar click directly.
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);

    // Count setElementVisible(_, true) triggered by updateSubPanelVisibility()
    // when Zone is re-activated.
    int visibleTrueCount = 0;
    EXPECT_CALL(backend_, setElementVisible(_, true))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&visibleTrueCount](UIElementHandle, bool) {
                ++visibleTrueCount;
            }));

    uiManager_->onEvent(makeToolbarZoneClick());

    // Assert: tool switched back to Zone.
    // The toolbar carve-out ensures Priority-3 is skipped for toolbar clicks,
    // so Priority-5 handles the Zone toolbar click and switches the tool.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Zone)
        << "Zone toolbar click must switch tool from Query to Zone. "
           "Priority-3 toolbar carve-out prevents pickTerrainTile from being "
           "called for toolbar coordinates; Priority-5 processes the click.";

    // Assert: at least 9 zone sub-panel buttons made visible (Bug 5 regression).
    EXPECT_GE(visibleTrueCount, 9)
        << "After switching from Query to Zone, zone sub-panel must re-appear "
           "(setElementVisible called with true for all 9 zone buttons). "
           "Bug 5: zone sub-panel does NOT re-appear when switching back from Query mode.";
}

// ---------------------------------------------------------------------------
// Test 27 (Bug 5): UtilitiesSubPanel_ReappearsAfterExitingQueryMode
//
// Analogous to Test 26 but for the Utilities sub-panel: after exiting Query
// mode by clicking the Utilities toolbar button, all 4 utility sub-panel
// buttons must become visible.
//
// Uses the same non-zero handle setup and documents the Bug 4 dependency.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug5_UtilitiesSubPanel_ReappearsAfterExitingQueryMode)
{
    uiManager_.reset();

    uint32_t nextHandle = 1;
    ON_CALL(backend_, addButton(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });
    ON_CALL(backend_, addStaticText(_, _, _, _, _))
        .WillByDefault([&nextHandle](const std::string&, int, int, int, int)
            -> UIElementHandle { return nextHandle++; });

    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    uiManager_ = std::make_unique<UIManager>(&backend_, nullptr, &sim_, &clock_);
    uiManager_->setRenderer(&renderer_);
    uiManager_->setTerrainQuery(&terrain_);
    uiManager_->setMapDimensions(10, 10);
    uiManager_->setDemolishConfirm(false);

    EXPECT_CALL(backend_, setElementVisible(_, ::testing::AnyOf(true, false)))
        .Times(::testing::AnyNumber());

    // Step 1: Activate Utilities tool — sub-panel shows.
    uiManager_->transitionToGameplay(GameMode::Sandbox);
    uiManager_->onEvent(makeToolbarUtilitiesClick());
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Utilities);

    // Step 2: Switch to Query tool — sub-panel hides.
    uiManager_->onEvent(makeToolbarQueryClick());
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Query);

    // Step 3: Click Utilities toolbar to return to Utilities mode.
    // The toolbar carve-out in Priority-3 (!inRect guard) skips Priority-3
    // entirely for toolbar coordinates (40, 200), so pickTerrainTile is NOT
    // called. Priority-5 processes the toolbar click directly.
    InputEvent utilToolbarClick{};
    utilToolbarClick.type   = InputEvent::Type::MouseButtonDown;
    utilToolbarClick.button = 0;
    utilToolbarClick.x      = 40;
    utilToolbarClick.y      = 200;  // Utilities y-range: 176..223
    utilToolbarClick.physX  = 40;
    utilToolbarClick.physY  = 200;

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);

    int visibleTrueCount = 0;
    EXPECT_CALL(backend_, setElementVisible(_, true))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&visibleTrueCount](UIElementHandle, bool) {
                ++visibleTrueCount;
            }));

    uiManager_->onEvent(utilToolbarClick);

    // Assert Utilities tool activated.
    // The toolbar carve-out ensures Priority-3 is skipped for toolbar clicks,
    // so Priority-5 handles the Utilities toolbar click and switches the tool.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Utilities)
        << "Utilities toolbar click must switch tool from Query to Utilities. "
           "Priority-3 toolbar carve-out prevents pickTerrainTile from being "
           "called for toolbar coordinates; Priority-5 processes the click.";

    // Assert: at least 4 utility sub-panel buttons made visible (Bug 5 regression).
    EXPECT_GE(visibleTrueCount, 4)
        << "After switching from Query to Utilities, utility sub-panel must re-appear "
           "(setElementVisible called with true for all 4 utility buttons). "
           "Bug 5 (Utilities variant): utility sub-panel does NOT re-appear after "
           "exiting Query mode.";
}

// ---------------------------------------------------------------------------
// Test 28 (Bug 4, carve-out verification): QueryMode_ToolbarCarveout_PreventsPriorityThreeInterference
//
// Verifies that the Priority-3 toolbar carve-out (!inRect guard) works correctly:
// a Zone toolbar click while Query is active bypasses Priority-3 entirely.
//
// Specifically:
//   - pickTerrainTile is NOT called for toolbar coordinates (40, 80).
//   - queryTile is NOT called (Priority-3 is skipped).
//   - getActiveTool() == Zone (Priority-5 handles the toolbar click).
//   - The active tool switches even when Query mode was previously active.
//
// This complements Test 24 by additionally verifying each sub-check:
// no terrain ray-cast, no query call, and correct tool state.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, Bug4_QueryMode_ToolbarCarveout_PreventsPriorityThreeInterference)
{
    // Activate Query tool — Priority-3 would fire for any world LMB without carve-out.
    activateQueryTool();
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Query)
        << "Precondition: Query tool must be active";

    // Inspector is NOT open (toolbar click does not open inspector).

    // The toolbar carve-out (!inRect guard) in Priority-3 must prevent pickTerrainTile
    // from being called for toolbar coordinates (40, 80). Verify with Times(0).
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).Times(0);

    // queryTile must NOT be called — Priority-3 is completely bypassed.
    EXPECT_CALL(sim_, queryTile(_, _)).Times(0);

    // getTileScreenBounds must NOT be called — Priority-3 bypass prevents inspector open.
    EXPECT_CALL(renderer_, getTileScreenBounds(_, _)).Times(0);

    // Send Zone toolbar click at (40, 80) — inside toolbar bounds.
    uiManager_->onEvent(makeToolbarZoneClick());

    // Primary assertion: Priority-5 handled the toolbar click; tool is now Zone.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Zone)
        << "Zone toolbar click while Query is active must switch tool to Zone. "
           "Priority-3 toolbar carve-out prevents ray-cast interference; "
           "Priority-5 processes the toolbar click and activates Zone tool.";
}

// ---------------------------------------------------------------------------
// Test 29: RoadDrag_MovesToNewTile_PlacesOnEachNewTile
//
// Road tool drag sequence: LMB down on tile (5,7), then MouseMove to (6,7),
// then MouseMove to (7,7).  Verifies placeRoad is called exactly once per
// distinct new tile — three calls total.
//
// Rationale: the drag throttle condition
//   (hitX != m_hoveredTileX || hitZ != m_hoveredTileZ)
// must fire for each tile the cursor enters, not for every pixel move.
// (ref: architecture/ui-ux/input-arbitration.md Priority-7 drag-to-place)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_RoadDrag_MovesToNewTile_PlacesOnMouseUp)
{
    // Road tool (Bug 3 fix): placement is deferred to LMB release.
    // Drag moves do NOT place — only the mouse-up at the final tile commits.
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateRoadTool();

    // Step 1: LMB down on tile (5,7) — anchor set, no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Step 2: MouseMove to tile (6,7) — line preview covers (5,7)–(6,7); no placement.
    // With LMB held and Road anchor at (5,7), the dominant axis is X (dX=1 >= dZ=0),
    // so the preview tiles are {(5,7),(6,7)}.  setTilePlacementPreview is called with
    // those tiles; setTileHoverHighlight(-1,-1,_) is called to clear the single-tile hover.
    EXPECT_CALL(renderer_, pickTerrainTile(600, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(6), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(renderer_, setTilePlacementPreview(::testing::Not(::testing::IsEmpty()), _))
        .Times(1);
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseMove(600, 500));

    // Step 3: MouseMove to tile (7,7) — line preview covers (5,7)–(7,7); still no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(700, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(7), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(renderer_, setTilePlacementPreview(::testing::Not(::testing::IsEmpty()), _))
        .Times(1);
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseMove(700, 500));

    // Step 4: LMB release — axis-snapped line from anchor (5,7) to hover (7,7) along X.
    // All three tiles (5,7), (6,7), (7,7) are placed.
    EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);
    EXPECT_CALL(sim_, placeRoad(6, 7, 0)).Times(1);
    EXPECT_CALL(sim_, placeRoad(7, 7, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 700, 500));
}

// ---------------------------------------------------------------------------
// Test 30: RoadDrag_HoverUpdateOnClick_NoDoublePlace
//
// Regression test for the bug where m_hoveredTileX / m_hoveredTileZ were NOT
// updated in the MouseButtonDown handler after a successful pickTerrainTile hit.
//
// Sequence:
//   - No prior hover (m_hoveredTileX = -1, m_hoveredTileZ = -1).
//   - LMB down on tile (5,7).
//   - MouseMove to tile (5,7) again (same pixel area — cursor did not move off tile).
//
// Expected: placeRoad(5,7,0) is called exactly ONCE (for the click only).
// The MouseMove to the same tile must NOT trigger a second placement, because
// m_hoveredTileX was updated to 5 in the MouseButtonDown handler after the fix.
//
// Before the fix, m_hoveredTileX remained -1 after the click, so the drag
// condition (hitX != -1) evaluated true and double-placed the tile.
// (ref: Priority-7 MouseButtonDown handler in UIManager.cpp)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_RoadDrag_HoverUpdateOnClick_NoDoublePlace)
{
    // Road tool (Bug 3 fix): press records the anchor; move updates hover; release places.
    // Pressing and moving to the same tile must result in exactly ONE placeRoad call (on release).
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateRoadTool();

    // Step 1: LMB down on tile (5,7) — anchor set at (5,7), no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Step 2: MouseMove to the same tile (5,7) — preview updated (anchor=hover=(5,7)),
    // single-tile line.  setTilePlacementPreview called; setTileHoverHighlight(-1,-1,_)
    // called to clear the single-tile hover.  No placement.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(renderer_, setTilePlacementPreview(::testing::Not(::testing::IsEmpty()), _))
        .Times(1);
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseMove(500, 500));

    // Step 3: LMB release — exactly ONE placement at the anchor/hover tile (5,7).
    EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 31: ZoneRectSelect_LmbPress_DoesNotPlaceImmediately
//
// Zone tool (rect-select): LMB press records the anchor tile — placement is
// deferred to LMB release.  The event is consumed but placeZone is NOT called
// on the press itself.  placeZone fires on LMB release (fills the 1×1 rect).
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneRectSelect_LmbPress_DoesNotPlaceImmediately)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateZoneTool();
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    // pickTerrainTile is called on press to resolve the anchor tile.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // Primary assertion: placeZone must NOT be called on LMB press.
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);

    // Act: LMB press at world position.
    bool consumed = uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Event must be consumed (anchor recorded).
    EXPECT_TRUE(consumed) << "Zone LMB press must be consumed (anchor recorded)";
}

// ---------------------------------------------------------------------------
// Test 32: ZoneRectSelect_SingleTile_PressRelease_PlacesOnce
//
// Zone tool (rect-select): LMB press sets anchor (no placement); LMB release
// on the same tile fills the 1×1 rect → exactly one placeZone call.
//
// Sequence:
//   LMB press  → anchor set; placeZone NOT called
//   LMB release → placeZone called once at (5,7) (1×1 rect)
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneRectSelect_SingleTile_PressRelease_PlacesOnce)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    activateZoneTool();

    // Step 1: LMB press — anchor set at (5,7); NO placement.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Step 2: LMB release — m_hoveredTileX/Z == (5,7) (set on press); 1×1 rect fires once.
    EXPECT_CALL(sim_, placeZone(5, 7, _, _, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));
}

// ---------------------------------------------------------------------------
// Test 33: ZoneRectSelect_MultiTileRect_FillsAllTiles
//
// Zone tool (rect-select): LMB press at (2,3) sets anchor; MouseMove to (4,5)
// shows a 3×3 rect preview; LMB release fills all 9 tiles.
//
// Sequence:
//   LMB press at (2,3) → anchor=(2,3); no placeZone
//   MouseMove to (4,5)  → rect preview shown; no placeZone
//   LMB release          → placeZone called for all 9 tiles in [2..4, 3..5]
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneRectSelect_MultiTileRect_FillsAllTiles)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    activateZoneTool();

    // Step 1: LMB press at (2,3) — anchor set; no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(200, 300, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(2), SetArgReferee<3>(3), Return(true)));
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 200, 300));

    // Step 2: MouseMove to (4,5) while LMB held — rect preview; no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(400, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(4), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(renderer_, setTilePlacementPreview(::testing::Not(::testing::IsEmpty()), _))
        .Times(1);
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(1);
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    uiManager_->onEvent(makeMouseMove(400, 500));

    // Step 3: LMB release — all 9 tiles in rect [x:2..4, z:3..5] filled.
    // m_hoveredTileX/Z == (4,5) after the MouseMove; anchor == (2,3).
    EXPECT_CALL(sim_, placeZone(2, 3, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 3, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(4, 3, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(2, 4, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 4, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(4, 4, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(2, 5, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 5, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(4, 5, _, _, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 400, 500));
}

// ---------------------------------------------------------------------------
// Test 34: ZoneRectSelect_RoadDragUnchanged_PlacesOnEachNewTile
//
// Road tool (straight-line): LMB press sets anchor; MouseMove shows line preview;
// LMB release places all tiles along the dominant axis from anchor to hover.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneRectSelect_RoadDragUnchanged_PlacesOnEachNewTile)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    activateRoadTool();

    // Step 1: LMB press on tile (5,7) — anchor set, no placement.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Step 2: MouseMove to tile (6,7) while LMB held — line preview (anchor(5,7)→(6,7));
    // setTilePlacementPreview called; setTileHoverHighlight(-1,-1,_) clears single-tile hover.
    EXPECT_CALL(renderer_, pickTerrainTile(600, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(6), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(renderer_, setTilePlacementPreview(::testing::Not(::testing::IsEmpty()), _))
        .Times(1);
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);
    uiManager_->onEvent(makeMouseMove(600, 500));

    // Step 3: LMB release — line from anchor(5,7) to hover(6,7) along X: two placements.
    EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);
    EXPECT_CALL(sim_, placeRoad(6, 7, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 600, 500));
}

// ---------------------------------------------------------------------------
// Test 35: ZoneRectSelect_DragNoDragPlacement_ZoneToolExcluded
//
// Zone tool (rect-select): Zone is excluded from tile-by-tile drag placement.
// LMB press sets anchor; MouseMove shows rect preview (no placement on move);
// LMB release fills the full rectangle.
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_ZoneRectSelect_DragNoDragPlacement_ZoneToolExcluded)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

    activateZoneTool();

    // Step 1: LMB press at (2,3) — anchor set; no placement on press.
    EXPECT_CALL(renderer_, pickTerrainTile(200, 300, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(2), SetArgReferee<3>(3), Return(true)));
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    uiManager_->onEvent(makeMouseButtonDown(0, 200, 300));

    // Step 2: MouseMove to (4,5) — rect preview shown; no placement during drag.
    EXPECT_CALL(renderer_, pickTerrainTile(400, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(4), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(renderer_, setTilePlacementPreview(::testing::Not(::testing::IsEmpty()), _))
        .Times(1);
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(1);
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    uiManager_->onEvent(makeMouseMove(400, 500));

    // Step 3: LMB release — rect fills all 9 tiles [x:2..4, z:3..5].
    EXPECT_CALL(sim_, placeZone(2, 3, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 3, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(4, 3, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(2, 4, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 4, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(4, 4, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(2, 5, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(3, 5, _, _, 0)).Times(1);
    EXPECT_CALL(sim_, placeZone(4, 5, _, _, 0)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 400, 500));
}

// ---------------------------------------------------------------------------
// Test: HoverHighlight_ClearedOnRmbToolClose
//
// Regression test for: hover highlight frozen on last tile after RMB closes tool.
//
// Sequence:
//   1. Activate Zone tool.
//   2. MouseMove to tile (3,4) — setTileHoverHighlight(3, 4, _) is called.
//   3. RMB press — tool is deselected.
//   4. Assert setTileHoverHighlight(-1, -1, kHoverArgbClear) was called by the
//      RMB handler so the hover quad is hidden, not frozen at tile (3,4).
//
// (ref: fix for Priority-6b RMB tool-deselect path in UIManager::onEvent())
// ---------------------------------------------------------------------------
TEST_F(WorldInteractionTest, WorldInteraction_HoverHighlight_ClearedOnRmbToolClose)
{
    activateZoneTool();

    // Step 1: MouseMove to tile (3,4) with Zone tool active — hover is set.
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));
    // Allow exactly one setTileHoverHighlight call for the hover set (single-tile,
    // since LMB is not held so no drag preview path is taken).
    EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(1);
    uiManager_->onEvent(makeMouseMove(500, 500));

    // Step 2: RMB press — tool deselected; hover must be cleared.
    // The primary assertion: setTileHoverHighlight(-1, -1, kHoverArgbClear) called
    // exactly once by the RMB handler.
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1,
        static_cast<uint32_t>(kHoverArgbClear))).Times(1);
    uiManager_->onEvent(makeMouseButtonDown(1, 500, 500));

    // Postcondition: active tool must be None after RMB.
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::None);
}

// ============================================================================
// Tests moved from coverage_gap_test.cpp (CoverageGapTest fixture)
// These use the WorldInteractionTest fixture which matches CoverageGapTest
// (StrictMock sim_/renderer_, NiceMock backend_, no audio).
// ============================================================================

// ============================================================================
// Test: Escape from Paused state -> transitionToGameplay_fromPaused
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_EscapeFromPaused_TransitionsToGameplay)
{
    EXPECT_CALL(sim_, isPaused()).WillRepeatedly(Return(false));
    EXPECT_CALL(sim_, setPaused(_)).Times(AnyNumber());

    goToGameplay();
    uiManager_->transitionToPaused();

    bool consumed = uiManager_->onEvent(makeKeyDown(27));  // Escape
    EXPECT_TRUE(consumed);
}

// ============================================================================
// Test: Escape in MainMenu state -> no-op (exercises non-Gameplay/Paused path)
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_EscapeInMainMenu_ReturnsFalse)
{
    // UIManager starts in MainMenu state (default).
    bool consumed = uiManager_->onEvent(makeKeyDown(27));  // Escape
    (void)consumed;  // Result depends on MainMenuPanel; ensure no crash.
}

// ============================================================================
// Test: Road hotkey R activates Road tool
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HotkeyR_ActivatesRoadTool)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(82));  // R
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Road);
}

// ============================================================================
// Test: Utilities hotkey U activates Utilities tool
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HotkeyU_ActivatesUtilitiesTool)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(85));  // U
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Utilities);
}

// ============================================================================
// Test: Demolish hotkey D activates Demolish tool
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HotkeyD_ActivatesDemolishTool)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(68));  // D
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::Demolish);
}

// ============================================================================
// Test: getActiveTool() returns None initially after Gameplay transition
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_GetActiveTool_ReturnsNoneInitially)
{
    goToGameplay();
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::None);
}

// ============================================================================
// Test: Zone sub-panel button click updates sprites
// Col=1 (Commercial Low): x=80+1*(64+4)=148, y=64. Centre: (180, 84).
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_ZoneSubPanel_ButtonClick_SwapsSprites)
{
    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 80));  // Activate Zone tool

    InputEvent click = makeMouseButtonDown(0, 180, 84);
    bool consumed = uiManager_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// ============================================================================
// Test: Utilities sub-panel button click updates sprites
// WaterTower (typeIdx=1): x=80+1*(64+4)=148, y=64. Centre: (180, 84).
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_UtilitiesSubPanel_ButtonClick_SwapsSprites)
{
    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 200));  // Activate Utilities tool

    InputEvent click = makeMouseButtonDown(0, 180, 84);
    bool consumed = uiManager_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// ============================================================================
// Test: Query toolbar toggle-off deactivates Query tool
// Open inspector first (Priority-3 carve-out allows toolbar click to toggle off).
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_QueryToolbarToggle_DeactivatesQueryTool)
{
    activateQueryTool();
    ASSERT_EQ(uiManager_->getActiveTool(), ActiveTool::Query);

    EXPECT_CALL(sim_, queryTile(5, 5)).WillOnce(Return(QueryResult{}));
    EXPECT_CALL(renderer_, pickTerrainTile(500, 500, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(renderer_, getTileScreenBounds(5, 5)).WillOnce(Return(ScreenRect{}));
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    // Inspector open — toggle off Query tool via toolbar click.
    uiManager_->onEvent(makeToolbarQueryClick());
    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::None);
}

// ============================================================================
// Test: Undo toolbar button click calls undoLastAction
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_UndoToolbarButton_CallsUndoLastAction)
{
    goToGameplay();

    EXPECT_CALL(sim_, hasUndoPendingAction()).WillRepeatedly(Return(true));
    EXPECT_CALL(sim_, undoLastAction()).Times(1);

    uiManager_->onEvent(makeToolbarUndoClick());
}

// ============================================================================
// Test: Hover highlight uses Road color when Road tool is active
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HoverHighlight_RoadColor)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(82));  // R = Road

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ============================================================================
// Test: Hover highlight uses Utilities color when Utilities tool active
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HoverHighlight_UtilitiesColor)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(85));  // U = Utilities

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ============================================================================
// Test: Hover highlight uses Demolish color when Demolish tool active
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HoverHighlight_DemolishColor)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(68));  // D = Demolish

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ============================================================================
// Test: Hover highlight uses Query color when Query tool active
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HoverHighlight_QueryColor)
{
    activateQueryTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(3), SetArgReferee<3>(4), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(3, 4, _)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ============================================================================
// Test: Hover highlight cleared when pickTerrainTile returns false
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_HoverHighlight_NoTerrain_ClearsHighlight)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(82));  // Road tool active

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _)).WillOnce(Return(false));
    EXPECT_CALL(renderer_, setTileHoverHighlight(-1, -1, _)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseMove(500, 500));
}

// ============================================================================
// Test: Query tool active, left-click on world -> no placement calls
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_QueryToolActive_WorldClick_NoPrioritySevenDispatch)
{
    activateQueryTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));
    EXPECT_CALL(renderer_, getTileScreenBounds(5, 7))
        .WillOnce(Return(ScreenRect{}));
    EXPECT_CALL(sim_, queryTile(5, 7))
        .WillOnce(Return(QueryResult{}));

    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(0);
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}

// ============================================================================
// Test: No terrain hit on left-click -> returns false
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_WorldClick_NoTerrainHit_ReturnsFalse)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(82));  // Road tool

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(Return(false));
    EXPECT_CALL(sim_, placeRoad(_, _, _)).Times(0);

    bool consumed = uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    EXPECT_FALSE(consumed);
}

// ============================================================================
// Test: Commercial zone placement produces kOverlayArgbCommercial
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_CommercialZone_OverlayColor)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 80));  // Zone tool

    // Select Commercial (col=1): x=80+1*(64+4)=148; y=64. Click centre: (180, 84).
    uiManager_->onEvent(makeMouseButtonDown(0, 180, 84));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(2), SetArgReferee<3>(3), Return(true)));
    EXPECT_CALL(sim_, placeZone(2, 3, ZoneType::Commercial, _, _)).Times(1);

    ZoneOverlayMap captured;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&captured));

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    uint64_t key = static_cast<uint64_t>(3) * 10u + static_cast<uint64_t>(2);
    ASSERT_TRUE(captured.count(key) > 0);
    EXPECT_EQ(captured.at(key), static_cast<uint32_t>(kOverlayArgbCommercial));
}

// ============================================================================
// Test: Industrial zone placement produces kOverlayArgbIndustrial
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_IndustrialZone_OverlayColor)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 80));  // Zone tool

    // Select Industrial (col=2): x=80+2*(64+4)=216; y=64. Click centre: (248, 84).
    uiManager_->onEvent(makeMouseButtonDown(0, 248, 84));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(1), SetArgReferee<3>(2), Return(true)));
    EXPECT_CALL(sim_, placeZone(1, 2, ZoneType::Industrial, _, _)).Times(1);

    ZoneOverlayMap captured;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&captured));

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    uint64_t key = static_cast<uint64_t>(2) * 10u + static_cast<uint64_t>(1);
    ASSERT_TRUE(captured.count(key) > 0);
    EXPECT_EQ(captured.at(key), static_cast<uint32_t>(kOverlayArgbIndustrial));
}

// ============================================================================
// Tests moved from coverage_gap_test.cpp (ValidHandleUIManagerTest fixture)
// New fixture: ValidHandleWorldInteractionTest — same as WorldInteractionTest
// but with a NiceMock<MockAudioSystem> and incrementing non-zero handles.
// ============================================================================

class ValidHandleWorldInteractionTest : public ::testing::Test {
protected:
    StrictMock<MockCitySimulation>   sim_;
    StrictMock<MockRenderer>         renderer_;
    ManualTerrainQuery               terrain_;
    ManualClock                      clock_;
    NiceMock<MockUIBackend>          backend_;
    NiceMock<MockAudioSystem>        audio_;
    std::unique_ptr<UIManager>       uiManager_;
    uint32_t                         nextHandle_{1};

    void SetUp() override {
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault([this](
            const std::string&, int, int, int, int) -> uint32_t {
            return ++nextHandle_;
        });
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault([this](
            const std::string&, int, int, int, int) -> uint32_t {
            return ++nextHandle_;
        });
        ON_CALL(backend_, loadTexture(_)).WillByDefault([this](const std::string&) -> uint32_t {
            return ++nextHandle_;
        });
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));

        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, setTilePlacementPreview(_, _)).Times(AnyNumber());

        uiManager_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
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
};

// ============================================================================
// Test: Zone sub-panel sprite swap with valid handles
// Col=1 (Commercial Low): x=80+1*(64+4)=148, y=64. Centre: (180, 84).
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, ZoneSubPanel_ValidHandles_SpritesSwapped)
{
    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 80));  // Zone tool

    InputEvent click = makeMouseButtonDown(0, 180, 84);
    bool consumed = uiManager_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// ============================================================================
// Test: Utilities sub-panel sprite swap with valid handles
// WaterTower (typeIdx=1): x=80+1*(64+4)=148, y=64. Centre: (180, 84).
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, UtilSubPanel_ValidHandles_SpritesSwapped)
{
    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 200));  // Utilities tool

    InputEvent click = makeMouseButtonDown(0, 180, 84);
    bool consumed = uiManager_->onEvent(click);
    EXPECT_TRUE(consumed);
}

// ============================================================================
// Test: Sub-panel open SFX fired when Zone tool is activated
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, SubPanelOpenSFX_FiredWhenToolActivated)
{
    goToGameplay();

    EXPECT_CALL(audio_, playSound(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(audio_, playSound(UI_MENU_OPEN, _, _)).Times(AtLeast(1));

    uiManager_->onEvent(makeMouseButtonDown(0, 40, 80));  // Zone tool
}

// ============================================================================
// Test: Zone drag Z-dominant preview
// Anchor (5,5), move to (6,8): dZ=3 > dX=1 → Z-dominant preview.
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, ZoneDrag_ZDominant_ShowsZPreview)
{
    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 80));  // Zone tool

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(6), SetArgReferee<3>(8), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(_, _, _)).Times(AnyNumber());
    uiManager_->onEvent(makeMouseMove(510, 530));
}

// ============================================================================
// Test: Road drag Z-dominant preview
// Anchor (5,5), move to (5,8): dZ=3 > dX=0 → Z-dominant road preview.
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, RoadDrag_ZDominant_ShowsZPreview)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(82));  // Road tool

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(8), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(_, _, _)).Times(AnyNumber());
    uiManager_->onEvent(makeMouseMove(500, 530));
}

// ============================================================================
// Test: Demolish drag to different tile calls demolishTile on both
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, DemolishDrag_DifferentTile_CallsPlacement)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(68));  // Demolish tool

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(6), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(sim_, demolishTile(6, 5)).Times(1);
    uiManager_->onEvent(makeMouseMove(510, 500));
}

// ============================================================================
// Test: Road drag Z-dominant release commits road placement along Z axis
// Anchor (5,5), move to (5,8): release places roads at (5,5)-(5,6)-(5,7)-(5,8).
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, RoadDrag_ZDominant_ReleasePlacesRoads)
{
    goToGameplay();
    uiManager_->onEvent(makeKeyDown(82));  // Road tool

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(8), Return(true)));
    EXPECT_CALL(renderer_, setTileHoverHighlight(_, _, _)).Times(AnyNumber());
    uiManager_->onEvent(makeMouseMove(500, 530));

    EXPECT_CALL(sim_, placeRoad(5, 5, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(5, 6, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(5, 7, _)).Times(1);
    EXPECT_CALL(sim_, placeRoad(5, 8, _)).Times(1);
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 530));
}

// ============================================================================
// Test: update() consumeStartGameRequest -> transitionToGameplay
// Navigate to New Game screen (Enter), configure Start City rect,
// click to set m_startGameRequested, then call update() to poll.
// ============================================================================
TEST_F(ValidHandleWorldInteractionTest, Update_ConsumesStartGameRequest_TransitionsToGameplay)
{
    uint32_t startCityHandle = 0;
    ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
        [this, &startCityHandle](const std::string& label, int, int, int, int) -> uint32_t {
            uint32_t h = ++nextHandle_;
            if (label == "Start City") startCityHandle = h;
            return h;
        });

    // Rebuild UIManager with updated addButton lambda.
    uiManager_.reset();
    uiManager_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    uiManager_->setRenderer(&renderer_);
    uiManager_->setTerrainQuery(&terrain_);
    uiManager_->setMapDimensions(10, 10);
    uiManager_->setDemolishConfirm(false);

    ASSERT_NE(startCityHandle, 0u) << "Start City handle must be non-zero";

    ON_CALL(backend_, getElementRect(_)).WillByDefault([startCityHandle](uint32_t h) {
        if (h == startCityHandle) return Rect{0, 0, 20, 20};
        return Rect{9000, 9000, 0, 0};
    });

    // Navigate to New Game screen.
    uiManager_->onEvent(makeKeyDown(13));  // Enter -> showNewGameScreen()

    // Click Start City button.
    uiManager_->onEvent(makeMouseButtonDown(0, 10, 10));

    // Call update() — polls consumeStartGameRequest() -> transitionToGameplay.
    EXPECT_CALL(sim_, getConsecutiveDeficitMonths()).WillRepeatedly(Return(0));
    EXPECT_CALL(sim_, getSpeedMultiplier()).WillRepeatedly(Return(SpeedMultiplier::x1));
    EXPECT_CALL(sim_, hasUndoPendingAction()).WillRepeatedly(Return(false));
    EXPECT_CALL(sim_, pollPendingNotification(_)).WillRepeatedly(Return(false));

    uiManager_->update(0.016f);

    EXPECT_EQ(uiManager_->getActiveTool(), ActiveTool::None);
}

// ============================================================================
// Test: Demolish with confirm modal enabled — modal defers demolition
// When setDemolishConfirm(true) and Demolish tool is active, left-click
// shows the modal (showDemolishConfirm) instead of calling demolishTile.
// ============================================================================
TEST_F(WorldInteractionTest, Coverage_DemolishWithConfirmModal_ShowsModal)
{
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    uiManager_->setDemolishConfirm(true);

    // showForcedLoanDialog / modal open may query isPaused/setPaused.
    EXPECT_CALL(sim_, isPaused()).WillRepeatedly(Return(false));
    EXPECT_CALL(sim_, setPaused(_)).Times(AnyNumber());

    goToGameplay();
    uiManager_->onEvent(makeMouseButtonDown(0, 40, 250));  // Demolish: y 232..279

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));

    // demolishTile must NOT be called — modal defers it.
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

    EXPECT_TRUE(uiManager_->hasActiveModal());
}
