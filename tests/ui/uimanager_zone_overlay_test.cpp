// tests/ui/uimanager_zone_overlay_test.cpp
//
// Phase 11m D1: UIManager zone overlay tests.
// Verifies that m_overlayMap is updated correctly when zones are placed,
// refreshed when buildings spawn, and cleared when tiles are demolished.
//
// All three tests assert via MockRenderer::setZoneOverlay() — the observable
// side-effect of m_overlayMap changes.
//
// Mock policy: NiceMock for all (UIManager constructor calls many addStaticText/addButton).
// TearDown contract: uiManager_.reset() before mock destructors.

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/ui/ui_constants.h"
#include "src/platform/input_event.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/LoanTerms.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>
#include <cstdint>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

using ZoneOverlayMap = std::unordered_map<uint64_t, uint32_t>;

// ---------------------------------------------------------------------------
// Helpers — input event factories (same pattern as world_interaction_test.cpp)
// ---------------------------------------------------------------------------
static InputEvent makeMouseButtonDown(int button, int virtX, int virtY)
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

static InputEvent makeMouseButtonUp(int button, int virtX, int virtY)
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

static InputEvent makeToolbarZoneClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 80;  // Zone button range: 64..111
    ev.physX  = 40;
    ev.physY  = 80;
    return ev;
}

static InputEvent makeToolbarDemolishClick()
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;
    ev.x      = 40;
    ev.y      = 250;  // Demolish button range: 232..279
    ev.physX  = 40;
    ev.physY  = 250;
    return ev;
}

// ---------------------------------------------------------------------------
// ZoneOverlayTest fixture
// ---------------------------------------------------------------------------
class ZoneOverlayTest : public ::testing::Test {
protected:
    NiceMock<MockUIBackend>      backend_;
    NiceMock<MockAudioSystem>    audio_;
    NiceMock<MockCitySimulation> sim_;
    NiceMock<MockRenderer>       renderer_;
    ManualClock                  clock_;

    // uiManager_ declared LAST — destroyed first (before mock destructors).
    std::unique_ptr<UIManager> uiManager_;

    void SetUp() override {
        // Wire backend to return unique handles for every element creation.
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(false));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(Rect{0, 0, 140, 40}));

        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
        ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
        ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false));
        ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(0));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getDemandPressurePct(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));
        ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(QueryResult{}));

        // renderer_ needs to handle setZoneOverlay, setActiveTool, etc.
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _)).WillByDefault(Return(false));
        ON_CALL(renderer_, setZoneOverlay(_, _, _)).WillByDefault(Return());

        uiManager_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
        uiManager_->setRenderer(&renderer_);
        uiManager_->setTerrainQuery(nullptr);
        uiManager_->setMapDimensions(10, 10);
        uiManager_->setDemolishConfirm(false);
    }

    void TearDown() override {
        uiManager_.reset();
    }

    void goToGameplay() {
        uiManager_->transitionToGameplay(GameMode::Sandbox);
    }

    // Stub pickTerrainTile to hit at (tileX, tileZ).
    void stubPickTile(int tileX, int tileZ) {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(DoAll(
                SetArgReferee<2>(tileX),
                SetArgReferee<3>(tileZ),
                Return(true)));
    }

    // Stub pickTerrainTile to miss.
    void stubPickTileMiss() {
        ON_CALL(renderer_, pickTerrainTile(_, _, _, _))
            .WillByDefault(Return(false));
    }

    // Activate Zone tool.
    void activateZoneTool() {
        goToGameplay();
        uiManager_->onEvent(makeToolbarZoneClick());
    }

    // Activate Demolish tool.
    void activateDemolishTool() {
        goToGameplay();
        uiManager_->onEvent(makeToolbarDemolishClick());
    }

private:
    UIElementHandle nextHandle_{0};
};

// ---------------------------------------------------------------------------
// Test 1: UIManager_ZonePlacement_AddsOverlayEntry
//
// Place a Residential/Low zone tile. Assert setZoneOverlay is called with an
// overlay map that contains at least one entry with the expected ARGB value
// for Residential/Low: 0xB480CC80 (alpha=180, R=128, G=204, B=128 — pale green).
// ---------------------------------------------------------------------------
TEST_F(ZoneOverlayTest, UIManager_ZonePlacement_AddsOverlayEntry)
{
    activateZoneTool();
    stubPickTile(3, 4);

    // After mouse-down on tile (3,4), Zone anchor is set but overlay not inserted yet
    // (only on LMB release for the rectangular-fill pattern).
    ZoneOverlayMap capturedMap;
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _))
        .Times(AtLeast(1))
        .WillRepeatedly(SaveArg<2>(&capturedMap));

    // Stub queryTile for all tiles: catch-all returns free (isZoned=false).
    // This covers adjacent-tile queries from the road-mesh-rebuild loop after placeZone.
    // The more-specific expectation for (3,4) is added AFTER so GMock matches it first
    // (LIFO order): first call returns free (pre-placement guard), subsequent calls
    // return isZoned=true (post-placement overlay check).
    EXPECT_CALL(sim_, queryTile(_, _)).Times(AnyNumber()).WillRepeatedly(Return(QueryResult{}));

    QueryResult placed{};
    placed.isZoned = true;
    EXPECT_CALL(sim_, queryTile(3, 4))
        .WillOnce(Return(QueryResult{}))     // pre-placement guard: tile is free
        .WillRepeatedly(Return(placed));     // post-placement check: tile is now zoned

    // LMB down (sets anchor) then LMB up (commits placement).
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    // Verify the overlay map received in the last setZoneOverlay call is non-empty
    // and contains an entry with the Residential/Low ARGB color (0xB480CC80).
    EXPECT_FALSE(capturedMap.empty());

    bool found = false;
    for (const auto& kv : capturedMap) {
        if (kv.second == 0xB480CC80u) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected ARGB 0xB480CC80 (Residential/Low) in overlay map";
}

// ---------------------------------------------------------------------------
// Test 2: UIManager_OverlayRefresh_RemovesEntryWhenBuildingSpawned
//
// Pre-populate the overlay map with one entry via setOverlayMapForTest().
// Stub queryTile to return underConstruction=false (building has spawned).
// Call update() 60 times to trigger overlay refresh.
// Assert the final setZoneOverlay call passes an empty overlay map.
// ---------------------------------------------------------------------------
TEST_F(ZoneOverlayTest, UIManager_OverlayRefresh_RemovesEntryWhenBuildingSpawned)
{
    goToGameplay();

    // Pre-populate overlay map with tile (2,3) key = 3*10+2 = 32.
    ZoneOverlayMap preloaded;
    preloaded[32u] = 0xB480CC80u;  // Residential/Low color
    uiManager_->setOverlayMapForTest(preloaded);

    // queryTile returns underConstruction=false -> building spawned -> remove overlay.
    QueryResult spawnedResult{};
    spawnedResult.isZoned          = true;
    spawnedResult.underConstruction = false;
    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(spawnedResult));

    // Capture the last setZoneOverlay call.
    ZoneOverlayMap capturedMap;
    ON_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillByDefault(SaveArg<2>(&capturedMap));

    // Update 60 frames to trigger the 60-frame overlay refresh.
    for (int i = 0; i < 60; ++i) {
        uiManager_->update(0.016f);
    }

    // The overlay entry for the spawned building should have been removed.
    EXPECT_TRUE(capturedMap.empty())
        << "Expected empty overlay map after building spawned (underConstruction=false)";
}

// ---------------------------------------------------------------------------
// Test 3: UIManager_Demolish_RemovesOverlayEntry
//
// Pre-populate overlay map with tile (5,5). Trigger demolish at (5,5).
// Assert setZoneOverlay is called with a map that no longer contains key 55
// (tileZ*mapTilesX + tileX = 5*10+5 = 55).
// ---------------------------------------------------------------------------
TEST_F(ZoneOverlayTest, UIManager_Demolish_RemovesOverlayEntry)
{
    // Pre-populate overlay map with tile (5,5) key=55.
    ZoneOverlayMap preloaded;
    preloaded[55u] = 0xB480CC80u;
    uiManager_->setOverlayMapForTest(preloaded);

    activateDemolishTool();
    stubPickTile(5, 5);

    // Capture setZoneOverlay argument.
    ZoneOverlayMap capturedMap;
    ON_CALL(renderer_, setZoneOverlay(_, _, _))
        .WillByDefault(SaveArg<2>(&capturedMap));
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AtLeast(1))
        .WillRepeatedly(SaveArg<2>(&capturedMap));

    // Allow demolishTile call.
    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);

    // LMB click at the tile.
    uiManager_->onEvent(makeMouseButtonDown(0, 500, 500));
    uiManager_->onEvent(makeMouseButtonUp(0, 500, 500));

    // Tile (5,5) key=55 must not be in the overlay map after demolish.
    EXPECT_EQ(capturedMap.count(55u), 0u)
        << "Tile (5,5) key=55 should be removed from overlay after demolish";
}
