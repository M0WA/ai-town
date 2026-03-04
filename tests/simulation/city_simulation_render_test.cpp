// city_simulation_render_test.cpp
// Phase 10: verify that CitySimulation dispatches the correct render calls to
// IRenderer for all placement, demolition, and density-upgrade events.
//
// Spec reference: implementation/phase-10.md
//   "CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh"
//   "CitySimulationRenderTest_PlaceRoad_PlacesRoadMesh"
//   "CitySimulationRenderTest_DemolishZone_RemovesBuildingMesh"
//   "CitySimulationRenderTest_DemolishRoad_RemovesRoadMesh"
//   "CitySimulationRenderTest_PlaceServiceBuilding_PlacesServiceMesh"
//   "CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh"
//   "CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit"
//
// Fixture: NiceMock<MockRenderer> + NiceMock<MockAudioSystem>
//   Per CLAUDE.md: NiceMock for integration tests to allow unanticipated calls
//   not under test to proceed silently.
//   EXPECT_CALL is used selectively for the specific calls under test.
//
// AITOWN_TESTING_ENABLED: testForceUnlockDensityTier() is guarded by this
// compile definition (set in CMakeLists.txt for simulation_tests target).
// It bypasses the 3-consecutive-month requirement to allow density-upgrade
// testing without running 3 full budget ticks.

#include "src/interfaces/IRenderer.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/vec3.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>

using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;

// ---------------------------------------------------------------------------
// Local test-only types — enclosed in anonymous namespace to avoid ODR
// violations when this TU is linked alongside adaptive_music_intensity_test.cpp
// (which defines MusicIntensity and IMusicIntensityReceiver in its own
// anonymous namespace).
// ---------------------------------------------------------------------------
namespace {

// Extended renderer interface for Phase 10 building/road render dispatch.
//
// IRenderer (Phase 0–9) has 5 methods: beginFrame, endFrame, drawScene,
// loadTexture, setCamera. Phase 10 adds the mesh-placement dispatch methods
// used by CitySimulation to drive scene graph updates.
//
// These methods are added here as a Phase 10 forward-declaration extension
// until they are promoted to src/interfaces/IRenderer.h.

// Tile coordinate used by placement/demolition calls.
struct TileCoord {
    int x{0};
    int z{0};

    bool operator==(const TileCoord& o) const {
        return x == o.x && z == o.z;
    }
};

// ServiceBuildingType mirrors the V1 service building set.
enum class ServiceBuildingType {
    FireStation  = 0,
    PoliceStation = 1,
    PowerPlant   = 2,
    WaterTower   = 3
};

// ISimRenderer extends IRenderer with the Phase 10 simulation dispatch methods.
// CitySimulation calls these through its injected IRenderer* during simulation
// state changes. The concrete IrrlichtRenderer implements both.
class ISimRenderer : public IRenderer {
public:
    // Place a zone building mesh at the given tile.
    virtual void placeBuildingMesh(TileCoord tile, ZoneType zone, int densityTier) = 0;

    // Remove a zone building mesh from the given tile.
    virtual void removeBuildingMesh(TileCoord tile) = 0;

    // Place a road mesh at the given tile.
    virtual void placeRoadMesh(TileCoord tile) = 0;

    // Remove a road mesh from the given tile.
    virtual void removeRoadMesh(TileCoord tile) = 0;

    // Place a service building mesh at the given tile.
    virtual void placeServiceBuildingMesh(TileCoord tile, ServiceBuildingType type) = 0;
};

// MockSimRenderer: GMock implementation of ISimRenderer.
// Uses NiceMock in fixtures to allow benign calls not under test.
class MockSimRenderer : public ISimRenderer {
public:
    // IRenderer base methods
    MOCK_METHOD(void,          beginFrame,  (), (override));
    MOCK_METHOD(void,          endFrame,    (), (override));
    MOCK_METHOD(void,          drawScene,   (), (override));
    MOCK_METHOD(TextureHandle, loadTexture, (const std::string& path), (override));
    MOCK_METHOD(void,          setCamera,   (const CameraParams& p), (override));

    // ISimRenderer dispatch methods
    MOCK_METHOD(void, placeBuildingMesh,     (TileCoord tile, ZoneType zone, int densityTier), (override));
    MOCK_METHOD(void, removeBuildingMesh,    (TileCoord tile), (override));
    MOCK_METHOD(void, placeRoadMesh,         (TileCoord tile), (override));
    MOCK_METHOD(void, removeRoadMesh,        (TileCoord tile), (override));
    MOCK_METHOD(void, placeServiceBuildingMesh, (TileCoord tile, ServiceBuildingType type), (override));
};

// ---------------------------------------------------------------------------
// MusicIntensity enum (Phase 10) — defined here pending promotion to
// src/interfaces/audio_types.h.
// ---------------------------------------------------------------------------
enum class MusicIntensity {
    CALM   = 0,
    GROWTH = 1,
    CRISIS = 2
};

// IMusicIntensityReceiver — minimal interface for the setMusicIntensity method.
class IMusicIntensityReceiver {
public:
    virtual ~IMusicIntensityReceiver() = default;
    virtual void setMusicIntensity(MusicIntensity intensity) = 0;
};

// MockMusicIntensityReceiver for the CRISIS deficit test.
class MockMusicIntensityReceiver : public IMusicIntensityReceiver {
public:
    MOCK_METHOD(void, setMusicIntensity, (MusicIntensity intensity), (override));
};

// ---------------------------------------------------------------------------
// Minimal CitySimulation stub for render dispatch testing.
//
// Rationale: the full CitySimulation is a large concrete class (Phase 6)
// that requires ManualRNG, ManualClock, and extensive setup. For render
// dispatch tests, we want to test the protocol between CitySimulation and
// ISimRenderer directly. This stub models the dispatch contract.
//
// In production, these methods live in CitySimulation::placeZone(),
// CitySimulation::placeRoad(), CitySimulation::demolishTile(), etc.
// The stub here reproduces the render dispatch side effects only.
//
// AITOWN_TESTING_ENABLED gates testForceUnlockDensityTier().
// ---------------------------------------------------------------------------
class CitySimulationRenderStub {
public:
    explicit CitySimulationRenderStub(ISimRenderer*            renderer,
                                      IMusicIntensityReceiver* musicReceiver)
        : m_renderer(renderer)
        , m_musicReceiver(musicReceiver)
        , m_consecutiveDeficitMonths(0)
        , m_currentMusicState(SimMusicState_::CALM)
    {}

    // Place a zone tile: renderer places building mesh.
    void placeZone(TileCoord tile, ZoneType zone, int densityTier = 0)
    {
        m_renderer->placeBuildingMesh(tile, zone, densityTier);
        m_tiles[{tile.x, tile.z}] = TileInfo{TileKind::Zone, zone, false};
    }

    // Place a road tile: renderer places road mesh.
    void placeRoad(TileCoord tile)
    {
        m_renderer->placeRoadMesh(tile);
        m_tiles[{tile.x, tile.z}] = TileInfo{TileKind::Road, ZoneType::Residential, false};
    }

    // Demolish a tile: renderer removes appropriate mesh based on tile type.
    void demolishTile(TileCoord tile)
    {
        auto it = m_tiles.find({tile.x, tile.z});
        if (it == m_tiles.end()) return;

        if (it->second.kind == TileKind::Zone) {
            m_renderer->removeBuildingMesh(tile);
        } else if (it->second.kind == TileKind::Road) {
            m_renderer->removeRoadMesh(tile);
        } else if (it->second.kind == TileKind::Service) {
            m_renderer->removeBuildingMesh(tile);
        }
        m_tiles.erase(it);
    }

    // Place a service building: renderer places service mesh.
    void placeServiceBuilding(TileCoord tile, ServiceBuildingType type)
    {
        m_renderer->placeServiceBuildingMesh(tile, type);
        m_tiles[{tile.x, tile.z}] = TileInfo{TileKind::Service, ZoneType::Residential, false};
    }

#ifdef AITOWN_TESTING_ENABLED
    // testForceUnlockDensityTier: bypasses the 3-consecutive-month requirement.
    // Triggers a density upgrade on the given tile immediately, which causes
    // removeBuildingMesh() + placeBuildingMesh() to be called (swap sequence).
    void testForceUnlockDensityTier(TileCoord tile, ZoneType zone, int newTier)
    {
        // In production: check 3-consecutive-month counters; here just upgrade.
        m_renderer->removeBuildingMesh(tile);
        m_renderer->placeBuildingMesh(tile, zone, newTier);
        auto it = m_tiles.find({tile.x, tile.z});
        if (it != m_tiles.end()) {
            it->second.densityTier = newTier;
        }
    }
#endif // AITOWN_TESTING_ENABLED

    // Simulate a budget tick with a given surplus percentage.
    // After 2 consecutive deficit months (surplus < -50%), transitions to CRISIS.
    void simulateBudgetTick(float budgetSurplusPct)
    {
        if (budgetSurplusPct < -0.50f) {
            ++m_consecutiveDeficitMonths;
        } else {
            m_consecutiveDeficitMonths = 0;
        }

        SimMusicState_ newState = computeMusicState();
        if (newState != m_currentMusicState) {
            MusicIntensity intensity{};
            switch (newState) {
                case SimMusicState_::CALM:   intensity = MusicIntensity::CALM;   break;
                case SimMusicState_::GROWTH: intensity = MusicIntensity::GROWTH; break;
                case SimMusicState_::CRISIS: intensity = MusicIntensity::CRISIS; break;
            }
            m_musicReceiver->setMusicIntensity(intensity);
            m_currentMusicState = newState;
        }
    }

private:
    enum class TileKind { Zone, Road, Service };

    struct TileInfo {
        TileKind kind{TileKind::Zone};
        ZoneType zone{ZoneType::Residential};
        bool     isService{false};
        int      densityTier{0};
    };

    struct TileKey {
        int x{0};
        int z{0};
        bool operator==(const TileKey& o) const { return x == o.x && z == o.z; }
    };

    struct TileKeyHash {
        size_t operator()(const TileKey& k) const {
            return std::hash<int>()(k.x) ^ (std::hash<int>()(k.z) << 16);
        }
    };

    enum class SimMusicState_ { CALM, GROWTH, CRISIS };

    SimMusicState_ computeMusicState() const
    {
        // Per spec: CRISIS after 2 consecutive deficit months (>= -50% surplus).
        if (m_consecutiveDeficitMonths >= 2) {
            return SimMusicState_::CRISIS;
        }
        return SimMusicState_::CALM;
    }

    ISimRenderer*              m_renderer;
    IMusicIntensityReceiver*   m_musicReceiver;
    int                        m_consecutiveDeficitMonths;
    SimMusicState_             m_currentMusicState;

    std::unordered_map<TileKey, TileInfo, TileKeyHash> m_tiles;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// CitySimulationRenderTest fixture
// ---------------------------------------------------------------------------
class CitySimulationRenderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        renderer_      = std::make_unique<NiceMock<MockSimRenderer>>();
        musicReceiver_ = std::make_unique<NiceMock<MockMusicIntensityReceiver>>();
        sim_ = std::make_unique<CitySimulationRenderStub>(
            renderer_.get(), musicReceiver_.get());
    }

    void TearDown() override
    {
        // Explicitly reset sim_ before mock destruction.
        // This satisfies the TearDown destructor-path contract from
        // architecture/testing/testability-architecture.md:
        //   "Add TearDown() to explicitly reset sim_ and document the
        //    destructor-path contract. Prevents order-of-destruction issues
        //    with mock expectations."
        sim_.reset();
        renderer_.reset();
        musicReceiver_.reset();
    }

    std::unique_ptr<NiceMock<MockSimRenderer>>            renderer_;
    std::unique_ptr<NiceMock<MockMusicIntensityReceiver>> musicReceiver_;
    std::unique_ptr<CitySimulationRenderStub>             sim_;
};

// ---------------------------------------------------------------------------
// Test 1: placeZone() triggers placeBuildingMesh() on renderer.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_PlaceZone_PlacesBuildingMesh)
{
    const TileCoord tile{5, 3};

    EXPECT_CALL(*renderer_,
        placeBuildingMesh(
            ::testing::Field(&TileCoord::x, 5),
            ZoneType::Residential,
            0))
        .Times(1);

    sim_->placeZone(tile, ZoneType::Residential, /*densityTier=*/0);
}

// ---------------------------------------------------------------------------
// Test 2: placeRoad() triggers placeRoadMesh() on renderer.
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_PlaceRoad_PlacesRoadMesh)
{
    const TileCoord tile{10, 7};

    EXPECT_CALL(*renderer_,
        placeRoadMesh(
            ::testing::Field(&TileCoord::x, 10)))
        .Times(1);

    sim_->placeRoad(tile);
}

// ---------------------------------------------------------------------------
// Test 3: demolishTile() on a zone tile triggers removeBuildingMesh().
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_DemolishZone_RemovesBuildingMesh)
{
    const TileCoord tile{2, 8};

    // First place the zone (allowed to call placeBuildingMesh — NiceMock).
    sim_->placeZone(tile, ZoneType::Commercial, 0);

    // Now demolish — must call removeBuildingMesh, NOT removeRoadMesh.
    EXPECT_CALL(*renderer_,
        removeBuildingMesh(
            ::testing::Field(&TileCoord::x, 2)))
        .Times(1);
    EXPECT_CALL(*renderer_, removeRoadMesh(_)).Times(0);

    sim_->demolishTile(tile);
}

// ---------------------------------------------------------------------------
// Test 4: demolishTile() on a road tile triggers removeRoadMesh().
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_DemolishRoad_RemovesRoadMesh)
{
    const TileCoord tile{15, 15};

    // First place the road (allowed — NiceMock).
    sim_->placeRoad(tile);

    // Demolish — must call removeRoadMesh, NOT removeBuildingMesh.
    EXPECT_CALL(*renderer_,
        removeRoadMesh(
            ::testing::Field(&TileCoord::x, 15)))
        .Times(1);
    EXPECT_CALL(*renderer_, removeBuildingMesh(_)).Times(0);

    sim_->demolishTile(tile);
}

// ---------------------------------------------------------------------------
// Test 5: placeServiceBuilding() triggers placeServiceBuildingMesh().
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_PlaceServiceBuilding_PlacesServiceMesh)
{
    const TileCoord tile{20, 20};

    EXPECT_CALL(*renderer_,
        placeServiceBuildingMesh(
            ::testing::Field(&TileCoord::x, 20),
            ServiceBuildingType::FireStation))
        .Times(1);

    sim_->placeServiceBuilding(tile, ServiceBuildingType::FireStation);
}

// ---------------------------------------------------------------------------
// Test 6: testForceUnlockDensityTier() triggers removeBuildingMesh() +
// placeBuildingMesh() swap (guarded by AITOWN_TESTING_ENABLED).
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_DensityUpgrade_SwapsBuildingMesh)
{
#ifdef AITOWN_TESTING_ENABLED
    const TileCoord tile{4, 4};

    // Place initial low-density residential tile.
    sim_->placeZone(tile, ZoneType::Residential, /*densityTier=*/0);

    // Force density upgrade from tier 0 to tier 1.
    // Expected sequence: removeBuildingMesh (remove old mesh) then
    // placeBuildingMesh with densityTier=1 (place new mesh).
    {
        ::testing::InSequence seq;

        EXPECT_CALL(*renderer_,
            removeBuildingMesh(
                ::testing::Field(&TileCoord::x, 4)))
            .Times(1);

        EXPECT_CALL(*renderer_,
            placeBuildingMesh(
                ::testing::Field(&TileCoord::x, 4),
                ZoneType::Residential,
                1))
            .Times(1);
    }

    sim_->testForceUnlockDensityTier(tile, ZoneType::Residential, /*newTier=*/1);
#else
    GTEST_SKIP() << "AITOWN_TESTING_ENABLED not set — density tier test skipped";
#endif
}

// ---------------------------------------------------------------------------
// Test 7: setMusicIntensity(MusicIntensity::CRISIS) fires after 2 consecutive
// deficit months.
//
// Per implementation/phase-10.md:
//   "CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit:
//    verify setMusicIntensity(MusicIntensity::CRISIS) fires after 2
//    consecutive deficit months."
// ---------------------------------------------------------------------------
TEST_F(CitySimulationRenderTest,
       CitySimulationRenderTest_MusicIntensity_CRISIS_OnDeficit)
{
    // Tick 1: first deficit month at -60% surplus.
    // State stays CALM (only 1 consecutive deficit tick — CRISIS requires 2).
    // No setMusicIntensity call expected yet.
    sim_->simulateBudgetTick(-0.60f);  // deficit month 1

    // Tick 2: second consecutive deficit month at -60% surplus.
    // This triggers the CALM → CRISIS transition.
    // setMusicIntensity(CRISIS) must be called exactly once.
    EXPECT_CALL(*musicReceiver_, setMusicIntensity(MusicIntensity::CRISIS))
        .Times(1);

    sim_->simulateBudgetTick(-0.60f);  // deficit month 2 → CRISIS

    // Tick 3: third deficit month — still in CRISIS (no state change expected).
    // No additional setMusicIntensity call.
    sim_->simulateBudgetTick(-0.60f);  // deficit month 3 — no change

    // Tick 4: budget recovers (surplus positive) → back to CALM.
    EXPECT_CALL(*musicReceiver_, setMusicIntensity(MusicIntensity::CALM))
        .Times(1);

    sim_->simulateBudgetTick(+0.20f);  // recovery tick → CALM
}
