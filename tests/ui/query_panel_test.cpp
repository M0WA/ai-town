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

#include "src/ui/InspectorPanel.h"
#include "src/interfaces/IUIBackend.h"     // UIRect
#include "src/interfaces/IRenderer.h"  // ScreenRect
#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
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
//
// Phase 9b migration: computePanelPosition signature changed from
//   computePanelPosition(clickX, clickY, screenW, screenH)
// to
//   computePanelPosition(cursorX, cursorY, const ScreenRect& tileBounds)
//
// All existing tests pass ScreenRect{1000, 1000, 10, 10} as tileBounds — this
// rect is at virtual position (1000,1000), well off to the lower-right, so it
// does not overlap any of the primary or fallback candidate positions computed
// from cursors in the upper-left quadrant or near screen edges.  The tile-overlap
// rejection step is therefore never triggered and all pre-existing placement
// assertions remain valid.
//
// Panel dimensions: 340x280 (kPanelW=340, kPanelH=280, kLineH=33).
// Sized for 720p legibility: 22px physical font × (1080/720) = 33 virtual per row.
// ============================================================================

// Off-screen tileBounds sentinel used by all migrated Phase 8 tests.
// Placed at (1000, 1000) — guaranteed non-overlapping with positions derived from
// cursor clicks at (0,0), (5,500), (200,200), (500,5), (500,1075), (960,540).
// NOTE: (1800, 200) fallback test: primary at (1840, 240) size 340x280 — off-screen
// right (1840+340=2180 > 1920); fallback at (1420, -120) — off-screen top; edge-snap
// chosen instead (snaps to left side, y clamped).
static constexpr ScreenRect kNoTileBounds{1000, 1000, 10, 10};

// --- Test 1: Primary right-below placement ---
TEST(QueryPanelPosition, PrimaryRightBelow_UpperLeftQuadrant) {
    // cursor at (200,200): primary candidate = (240, 240), size 340x280 — fits 1920x1080,
    // no tile overlap (kNoTileBounds at (1000,1000)).
    ScreenRect r = InspectorPanel::computePanelPosition(200, 200, kNoTileBounds);
    EXPECT_GE(r.x, 200);
    EXPECT_GE(r.y, 200);
    EXPECT_EQ(r.w, 340);
    EXPECT_EQ(r.h, 280);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 2: Fallback / edge-snap for right-side cursor ---
TEST(QueryPanelPosition, FallbackLeft_TileInRightHalf) {
    // cursor at (1800,200): primary = (1840, 240), size 340x280 — off-screen (1840+340=2180>1920).
    // Fallback = (1800-40-340, 200-40-280) = (1420, -120) — off-screen top.
    // Edge-snap: cursor x=1800 > 960 → snap to left (x=0); y clamped to [0, 800].
    ScreenRect r = InspectorPanel::computePanelPosition(1800, 200, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_EQ(r.w, 340);
    EXPECT_EQ(r.h, 280);
}

// --- Test 3: Edge clamping (4 sub-cases) ---
TEST(QueryPanelPosition, EdgeClamping_NearLeftEdge) {
    ScreenRect r = InspectorPanel::computePanelPosition(5, 500, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

TEST(QueryPanelPosition, EdgeClamping_NearRightEdge) {
    ScreenRect r = InspectorPanel::computePanelPosition(1915, 500, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
}

TEST(QueryPanelPosition, EdgeClamping_NearTopEdge) {
    ScreenRect r = InspectorPanel::computePanelPosition(500, 5, kNoTileBounds);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

TEST(QueryPanelPosition, EdgeClamping_NearBottomEdge) {
    ScreenRect r = InspectorPanel::computePanelPosition(500, 1075, kNoTileBounds);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 4: Third-fallback edge-snap ---
TEST(QueryPanelPosition, ThirdFallback_EdgeSnap_InsufficientSpaceAllQuadrants) {
    ScreenRect r = InspectorPanel::computePanelPosition(1910, 1070, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 5: Zero coordinate click ---
TEST(QueryPanelPosition, ZeroCoordinate_PanelStaysOnScreen) {
    ScreenRect r = InspectorPanel::computePanelPosition(0, 0, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 6: Center screen click ---
TEST(QueryPanelPosition, CenterScreen_PanelPlacedRightBelow) {
    ScreenRect r = InspectorPanel::computePanelPosition(960, 540, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// --- Test 7 (Phase 9b new): TileOverlap forces fallback ---
// cursor at (200, 200): primary candidate = (240, 240), size 340x280.
// tileBounds (250, 250, 100, 100) overlaps primary rect (240..580, 240..520).
// Primary rejected. Fallback = (200-40-340, 200-40-280) = (-180, -120) — off-screen.
// Edge-snap: cursor x=200 <= 960 → snap to right edge (kVirtualW-340=1580, y).
TEST(QueryPanelPosition, QueryPanel_TileOverlap_FallsBackToFallback) {
    // tileBounds overlaps the primary candidate position (240, 240, 340, 280).
    ScreenRect tileBounds{250, 250, 100, 100};
    ScreenRect r = InspectorPanel::computePanelPosition(200, 200, tileBounds);
    // Result must still be on screen regardless of which step was taken.
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
    EXPECT_EQ(r.w, 340);
    EXPECT_EQ(r.h, 280);
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

// ---------------------------------------------------------------------------
// Traffic refresh and staleness label coverage tests
// ---------------------------------------------------------------------------

// Coverage vehicle for line 351 (m_lastTrafficFrame = m_drawFrame).
// draw() is called 10 times — on the 10th call m_drawFrame=10,
// m_drawFrame - m_lastTrafficFrame = 10 - 0 = 10 >= kTrafficRefreshFrames(10),
// so the traffic refresh branch body executes.
TEST_F(QueryPanelIntegrationTest, Draw_TrafficRefresh_TriggeredEvery10Frames) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());

    for (int i = 0; i < 10; ++i) {
        panel_->draw();
    }
    // No specific assertion beyond no crash — this is a coverage vehicle for
    // the m_lastTrafficFrame = m_drawFrame assignment on line 351.
}

// Verifies economy refresh throttling: queryTile() is called exactly once
// across two successive draw() calls.
// draw() 1: m_drawFrame=1, 1-(-120)=121 >= 120 → refresh fires, m_lastEconomyFrame=1.
// draw() 2: m_drawFrame=2, 2-1=1 < 120     → refresh suppressed.
TEST_F(QueryPanelIntegrationTest, Draw_SuppressesRefreshBetweenBudgetTicks) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    // Economy data is fetched ONCE (on the first draw only).
    EXPECT_CALL(sim_, queryTile(_, _)).Times(1);

    panel_->draw();
    panel_->draw();
}

// Verifies the staleness label is shown after kStalenessFrames(60) frames have
// elapsed since the last economy refresh.
// After show(): m_lastEconomyFrame = -120, m_drawFrame = 0.
// draw() 1: m_drawFrame=1, economy refresh fires, m_lastEconomyFrame=1.
// draw() 2..61: framesSince = m_drawFrame - 1, max = 60 — NOT > 60 — label hidden.
// draw() 62: m_drawFrame=62, framesSince = 62-1 = 61 > 60 = kStalenessFrames → label shown.
TEST_F(QueryPanelIntegrationTest, Draw_ShowsStalenessLabelAfterKStalenessFrames) {
    panel_->show(5, 10, 200, 200);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());
    // On draw() 62 the staleness label text is set with "ago" and the label becomes visible.
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("ago"))).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(1));

    for (int i = 0; i < 62; ++i) {
        panel_->draw();
    }
}

// Third-fallback edge-snap: cursor near lower-right of virtual 1920x1080 space —
// primary and fallback both land off-screen, forcing edge-snap.
// Phase 9b: old "small screen" tests used non-standard screenW/screenH arguments;
// computePanelPosition now uses fixed 1920x1080 virtual bounds.
// These tests verify that edge-snap always produces an on-screen result.
TEST(QueryPanelPosition, ThirdFallback_SmallScreen_EdgeSnap) {
    // cursor near lower-right: (1870, 1050)
    // primary  = (1910, 1090) — off-screen (1910+240=2150 > 1920).
    // fallback = (1870-40-240, 1050-40-160) = (1590, 850) — fits; no tile overlap with kNoTileBounds.
    // (tileBounds at (1000,1000,10,10) does NOT overlap fallback at (1590,850,240,160).)
    ScreenRect r = InspectorPanel::computePanelPosition(1870, 1050, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
    EXPECT_GE(r.y, 0);
    EXPECT_LE(r.y + r.h, 1080);
}

// Edge-snap with cursor in left half of virtual screen.
TEST(QueryPanelPosition, ThirdFallback_LeftHalf_EdgeSnap) {
    // cursor at (120, 80): primary = (160, 120) — fits 1920x1080.
    // No tile overlap with kNoTileBounds, so primary is accepted.
    // Test only asserts panel stays on screen.
    ScreenRect r = InspectorPanel::computePanelPosition(120, 80, kNoTileBounds);
    EXPECT_GE(r.x, 0);
    EXPECT_LE(r.x + r.w, 1920);
}

// ============================================================================
// Tests moved from query_panel_coverage_test.cpp (PopulateCoverageTest fixture)
// These use QueryPanelIntegrationTest (same NiceMock<MockUIBackend> +
// NiceMock<MockCitySimulation> + InspectorPanel* pattern).
// ============================================================================

// Helper: build a fully-populated zoned QueryResult.
static QueryResult makeZonedQueryResult(ZoneType zone, DensityTier density)
{
    QueryResult qr;
    qr.tileX  = 3;
    qr.tileZ  = 7;
    qr.isZoned = true;
    qr.zoneType = zone;
    qr.densityTier = density;
    qr.population  = 150;
    qr.coverage.fire   = 80.0f;
    qr.coverage.police = 60.0f;
    qr.coverage.power  = 100.0f;
    qr.coverage.water  = 75.0f;
    qr.desirability    = 65.0f;
    qr.demandPressurePct = 45.0f;
    return qr;
}

// ============================================================================
// Test: populate() with Residential zone sets zone name, pop, desirability, demand
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Populate_Residential_SetsZoneText)
{
    QueryResult qr = makeZonedQueryResult(ZoneType::Residential, DensityTier::Low);
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Residential"))).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Pop: 150"))).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Desirability: 65"))).Times(AtLeast(1));
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Demand: 45%"))).Times(AtLeast(1));

    panel_->populate(qr, 3, 7, 500, 500, tileBounds);

    EXPECT_TRUE(panel_->isOpen());
}

// ============================================================================
// Test: populate() with Commercial zone exercises Commercial branch
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Populate_Commercial_SetsZoneText)
{
    QueryResult qr = makeZonedQueryResult(ZoneType::Commercial, DensityTier::Medium);
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Commercial"))).Times(AtLeast(1));

    panel_->populate(qr, 3, 7, 500, 500, tileBounds);
}

// ============================================================================
// Test: populate() with Industrial zone exercises Industrial branch
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Populate_Industrial_SetsZoneText)
{
    QueryResult qr = makeZonedQueryResult(ZoneType::Industrial, DensityTier::High);
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Industrial"))).Times(AtLeast(1));

    panel_->populate(qr, 3, 7, 500, 500, tileBounds);
}

// ============================================================================
// Test: populate() with N/A coverage (-1.0f sentinel) clamps snprintf to 0
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Populate_CoverageNA_ClampsToZero)
{
    QueryResult qr = makeZonedQueryResult(ZoneType::Residential, DensityTier::Low);
    qr.coverage.fire   = -1.0f;
    qr.coverage.police = -1.0f;
    qr.coverage.power  = -1.0f;
    qr.coverage.water  = -1.0f;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Fire:0%"))).Times(AtLeast(1));

    panel_->populate(qr, 3, 7, 500, 500, tileBounds);
}

// ============================================================================
// Test: draw() after populate() with isZoned=true refreshes economy data
// Calls draw() kEconomyRefreshFrames+2 times to trigger the refresh block.
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Draw_AfterPopulate_RefreshesEconomyData)
{
    QueryResult qr = makeZonedQueryResult(ZoneType::Residential, DensityTier::Low);
    ScreenRect tileBounds{1000, 1000, 10, 10};

    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

    panel_->populate(qr, 3, 7, 500, 500, tileBounds);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());

    // Call draw() kEconomyRefreshFrames+2 times to trigger the refresh block.
    for (int i = 0; i < 122; ++i) {
        panel_->draw();
    }

    EXPECT_TRUE(panel_->isOpen());
}

// ============================================================================
// Test: populate() with isZoned=false exercises the Unzoned path
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Populate_Unzoned_SetsUnzonedText)
{
    QueryResult qr;
    qr.tileX   = 5;
    qr.tileZ   = 5;
    qr.isZoned = false;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, std::string("Unzoned"))).Times(AtLeast(1));

    panel_->populate(qr, 5, 5, 500, 500, tileBounds);
}

// ============================================================================
// Test: show() exercises positionElements() (private — called indirectly)
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Show_ExercisesPositionElements)
{
    panel_->show(1, 1, 400, 400);
    panel_->draw();
    SUCCEED();
}

// ============================================================================
// Test: populate() with isRoad=true exercises road tile branch
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Populate_Road_SetsRoadText)
{
    QueryResult qr;
    qr.tileX  = 2;
    qr.tileZ  = 4;
    qr.isZoned = false;
    qr.isRoad  = true;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, std::string("Road"))).Times(AtLeast(1));

    panel_->populate(qr, 2, 4, 500, 500, tileBounds);
}

// ============================================================================
// Test: draw() after populate() with isRoad=true refreshes road data
// ============================================================================
TEST_F(QueryPanelIntegrationTest, Draw_AfterPopulateRoad_RefreshesRoadData)
{
    QueryResult qr;
    qr.tileX  = 6;
    qr.tileZ  = 6;
    qr.isZoned = false;
    qr.isRoad  = true;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

    panel_->populate(qr, 6, 6, 500, 500, tileBounds);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());

    for (int i = 0; i < 122; ++i) {
        panel_->draw();
    }

    EXPECT_TRUE(panel_->isOpen());
}

// ============================================================================
// Test: getBounds() returns valid 240x160 rect after populate()
// ============================================================================
TEST_F(QueryPanelIntegrationTest, GetBounds_ReturnsValidRect)
{
    QueryResult qr = makeZonedQueryResult(ZoneType::Residential, DensityTier::Low);
    ScreenRect tileBounds{1000, 1000, 10, 10};

    panel_->populate(qr, 3, 7, 500, 500, tileBounds);

    UIRect bounds = panel_->getBounds();
    EXPECT_EQ(bounds.w, 340);
    EXPECT_EQ(bounds.h, 280);
}

// ============================================================================
// Phase 11q6: Service building inspection — populate() with a service building
// tile must display the building type name (e.g. "Fire Station"), NOT "Unzoned".
// Exercises the serviceType != ServiceBuildingType::None branch in QueryPanel.cpp.
// ============================================================================
TEST_F(QueryPanelIntegrationTest, ServiceBuilding_ShowsTypeName_NotUnzoned)
{
    QueryResult qr;
    qr.tileX   = 4;
    qr.tileZ   = 8;
    qr.isZoned = false;
    qr.isRoad  = false;
    qr.serviceType = ServiceBuildingType::FireStation;
    qr.coverage.fire = 95.0f;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    // Must show "Fire Station", NOT "Unzoned".
    EXPECT_CALL(backend_, setElementText(_, std::string("Fire Station"))).Times(AtLeast(1));
    // Coverage line must show fire coverage.
    EXPECT_CALL(backend_, setElementText(_, HasSubstr("Fire: 95%"))).Times(AtLeast(1));

    panel_->populate(qr, 4, 8, 500, 500, tileBounds);

    EXPECT_TRUE(panel_->isOpen());
}

// ============================================================================
// Phase 11q6: Service building type name for each ServiceBuildingType variant.
// ============================================================================
TEST_F(QueryPanelIntegrationTest, ServiceBuilding_PoliceStation_ShowsTypeName)
{
    QueryResult qr;
    qr.tileX   = 2;
    qr.tileZ   = 3;
    qr.isZoned = false;
    qr.isRoad  = false;
    qr.serviceType = ServiceBuildingType::PoliceStation;
    qr.coverage.police = 70.0f;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, std::string("Police Station"))).Times(AtLeast(1));

    panel_->populate(qr, 2, 3, 500, 500, tileBounds);
}

TEST_F(QueryPanelIntegrationTest, ServiceBuilding_PowerPlant_ShowsTypeName)
{
    QueryResult qr;
    qr.tileX   = 1;
    qr.tileZ   = 1;
    qr.isZoned = false;
    qr.isRoad  = false;
    qr.serviceType = ServiceBuildingType::PowerPlant;
    qr.coverage.power = 100.0f;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, std::string("Power Plant"))).Times(AtLeast(1));

    panel_->populate(qr, 1, 1, 500, 500, tileBounds);
}

TEST_F(QueryPanelIntegrationTest, ServiceBuilding_WaterTower_ShowsTypeName)
{
    QueryResult qr;
    qr.tileX   = 9;
    qr.tileZ   = 9;
    qr.isZoned = false;
    qr.isRoad  = false;
    qr.serviceType = ServiceBuildingType::WaterTower;
    qr.coverage.water = 85.0f;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementText(_, std::string("Water Tower"))).Times(AtLeast(1));

    panel_->populate(qr, 9, 9, 500, 500, tileBounds);
}

// Phase 11q6: draw() after populate() with service building type exercises the
// serviceType != None branch inside the kEconomyRefreshFrames refresh block in
// draw(). populate() resets m_drawFrame=0 / m_lastEconomyFrame=0, so the
// refresh fires on draw() call 120 (120-0 >= 120).
TEST_F(QueryPanelIntegrationTest, Draw_AfterPopulateServiceBuilding_RefreshesServiceData)
{
    QueryResult qr;
    qr.tileX       = 4;
    qr.tileZ       = 8;
    qr.isZoned     = false;
    qr.isRoad      = false;
    qr.serviceType = ServiceBuildingType::FireStation;
    qr.coverage.fire = 95.0f;
    ScreenRect tileBounds{1000, 1000, 10, 10};

    ON_CALL(sim_, queryTile(_, _)).WillByDefault(Return(qr));

    panel_->populate(qr, 4, 8, 500, 500, tileBounds);

    EXPECT_CALL(backend_, setElementText(_, _)).Times(AnyNumber());
    EXPECT_CALL(backend_, setElementVisible(_, _)).Times(AnyNumber());

    for (int i = 0; i < 122; ++i) {
        panel_->draw();
    }

    EXPECT_TRUE(panel_->isOpen());
}
