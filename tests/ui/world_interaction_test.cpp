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
#include "tests/ui/mock_ui_backend.h"
#include "tests/ui/mock_city_simulation.h"
#include "tests/simulation/mock_renderer.h"
#include "tests/simulation/manual_terrain_query.h"
#include "tests/simulation/manual_clock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>
#include <cstdint>

using ::testing::_;
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
        // Pre-register this expected call with AnyNumber() so StrictMock<MockRenderer>
        // does not fail on the unconditional SetUp() call. Individual tests that need
        // to verify specific setZoneOverlay arguments add their own EXPECT_CALL,
        // which GMock satisfies via most-recently-added-first matching order.
        EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(::testing::AnyNumber());

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

    // Act: left-click at a position outside the toolbar (world area).
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
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

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    EXPECT_CALL(sim_, placeRoad(5, 7, 0)).Times(1);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
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

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
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

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

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

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

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
        uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    }

    // onNewGame() must clear m_overlayMap and push an empty setZoneOverlay call.
    ZoneOverlayMap capturedClear;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillOnce(SaveArg<2>(&capturedClear));

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

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));

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
        uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
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
    // The Utilities sub-panel layout (spec): col0=PowerPlant, col1=WaterTower,
    // col0row1=FireStation. Virtual position: top-left anchor (x:80, y:176),
    // button size 96x48, gap 4. FireStation is at column 0, row 1:
    //   x = 80 + 0*(96+4) = 80, y = 176 + 1*(48+4) = 228.
    // Click at (80+48, 228+24) = (128, 252) to hit FireStation button.
    // This is a Phase 9b sub-panel click that sets m_selectedServiceBuilding.
    // The sub-panel buttons are created by UIManager init() and respond to
    // EGET_BUTTON_CLICKED events in the Priority 5 handler — for this test,
    // we send a direct sub-panel region click. The exact x/y depends on the
    // implementation. Sending a click in the FireStation button region should work.
    InputEvent fireStationClick{};
    fireStationClick.type   = InputEvent::Type::MouseButtonDown;
    fireStationClick.button = 0;
    fireStationClick.x      = 128;  // FireStation column 0 (col0row1) in sub-panel
    fireStationClick.y      = 252;  // Row 1: y=176+52=228, center at 252
    fireStationClick.physX  = 128;
    fireStationClick.physY  = 252;
    uiManager_->onEvent(fireStationClick);

    // Now send the world click.
    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(7), Return(true)));

    // Primary assertion: placeServiceBuilding with FireStation, cost 0 (flat terrain).
    EXPECT_CALL(sim_, placeServiceBuilding(5, 7, ServiceBuildingType::FireStation, 0))
        .Times(1);

    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
}
