// irrlicht_renderer_flatten_test.cpp — Phase 11l Deliverable 3 integration tests.
//
// Verifies that IrrlichtRenderer::placeBuildingMesh() and
// placeServiceBuildingMesh() call setTileHeight() for every vertex of the
// full (N+1)x(N+1) footprint corner grid, not only the 4 corners of the
// origin tile.
//
// Two tests:
//   1. IrrlichtRenderer_PlaceMediumBuilding_AllCornerVerticesFlattened
//        Place a 2x2 (Medium density) building at tile (2,2).
//        Assert setTileHeight() was called for all 9 vertices of the 3x3
//        grid spanning [2..4] x [2..4], all with the same targetH.
//
//   2. IrrlichtRenderer_PlaceLowBuilding_FourCornersOnly
//        Place a 1x1 (Low density) building at tile (5,5).
//        Assert setTileHeight() was called for exactly 4 vertices:
//        (5,5), (6,5), (5,6), (6,6).
//
// Design notes:
//   - EDT_NULL device is used: no GPU or display server required.
//   - Asset files are absent in the test environment, so
//     BuildingAssetLoader::load() returns nullptr and the scene node is
//     never created.  The terrain flatten loop is now intentionally outside
//     the if(node) guard (Phase 11l refactor) so it runs regardless.
//   - ManualTerrainQuery is configured with non-uniform heights via
//     setHeightAt() so we can verify the targetH average calculation.
//   - m_flattenCalls records every setTileHeight() invocation (Phase 11l
//     ManualTerrainQuery extension).
//
// CMake target: integration_tests (added via target_sources in CMakeLists.txt).
// Label: "integration" (EDT_NULL device; no xvfb required).
//
// Spec ref: implementation/phase-11l.md §Deliverable 3
//           architecture/game-design/terrain-interaction.md §Multi-tile footprint
//           architecture/testing/testability-architecture.md §ManualTerrainQuery

// GLEW must be included before irrlicht.h (TextureCache.h chain pulls <GL/glew.h>).
#include <GL/glew.h>
#include <irrlicht.h>

#include <gtest/gtest.h>
#include <algorithm>
#include <tuple>

#include "src/rendering/IrrlichtRenderer.h"
#include "ManualTerrainQuery.h"

// ---------------------------------------------------------------------------
// IrrlichtRendererFlattenTest fixture
//
// Creates a shared EDT_NULL Irrlicht device and a real IrrlichtRenderer.
// A ManualTerrainQuery is wired in via setTerrainQuery().  Each test sets
// up non-uniform tile heights via setHeightAt() before calling place*().
//
// TearDown destroys the renderer before dropping the device to prevent
// use-after-free through Irrlicht scene graph internals.
// ---------------------------------------------------------------------------
class IrrlichtRendererFlattenTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_device = irr::createDevice(
            irr::video::EDT_NULL,
            irr::core::dimension2d<irr::u32>(640, 480));
        ASSERT_NE(m_device, nullptr) << "EDT_NULL device creation failed";

        m_renderer = std::make_unique<IrrlichtRenderer>(m_device, nullptr);
        m_renderer->setTerrainQuery(&m_terrain);
    }

    void TearDown() override {
        // Reset renderer BEFORE dropping device — destructor calls node->remove()
        // and mesh->drop() through Irrlicht internals; device must still be alive.
        m_renderer.reset();
        if (m_device) {
            m_device->drop();
            m_device = nullptr;
        }
    }

    irr::IrrlichtDevice*              m_device{nullptr};
    std::unique_ptr<IrrlichtRenderer> m_renderer;
    ManualTerrainQuery                m_terrain;
};

// ---------------------------------------------------------------------------
// Helper: check whether m_flattenCalls contains an entry for (tx, tz).
// ---------------------------------------------------------------------------
static bool hasVertex(
    const std::vector<std::tuple<int, int, float>>& calls,
    int tx, int tz)
{
    for (const auto& e : calls) {
        if (std::get<0>(e) == tx && std::get<1>(e) == tz)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// IrrlichtRenderer_PlaceMediumBuilding_AllCornerVerticesFlattened
//
// Spec: phase-11l.md §Deliverable 3 — Medium-density test
//
// A 2x2 building at tile (2,2) must flatten all 9 vertices of the 3x3
// corner grid:  {(2,2),(3,2),(4,2),(2,3),(3,3),(4,3),(2,4),(3,4),(4,4)}.
// All 9 entries must carry the same targetH value (the average of the 9
// pre-flatten heights provided via setHeightAt()).
//
// The asset file is absent, so BuildingAssetLoader::load() returns nullptr
// (no scene node is created).  The flatten runs before the asset load
// (Phase 11l refactor) so the terrain is still flattened correctly.
// ---------------------------------------------------------------------------
TEST_F(IrrlichtRendererFlattenTest,
       IrrlichtRenderer_PlaceMediumBuilding_AllCornerVerticesFlattened)
{
    // Configure non-uniform heights for all 9 corner vertices of the 3x3 grid
    // spanning [2..4] x [2..4].
    // The heights are deliberately varied so we can verify that the average
    // is computed from all 9 samples (not only the 4 outer corners).
    // footprintN = 2 => heights at (cx, cz) where cx in [2..4], cz in [2..4].
    const float heights[3][3] = {
        { 1.0f, 2.0f, 3.0f },  // cz = 2: cx = 2, 3, 4
        { 4.0f, 5.0f, 6.0f },  // cz = 3: cx = 2, 3, 4
        { 7.0f, 8.0f, 9.0f },  // cz = 4: cx = 2, 3, 4
    };
    float sum = 0.0f;
    for (int cx = 0; cx <= 2; ++cx) {
        for (int cz = 0; cz <= 2; ++cz) {
            m_terrain.setHeightAt(2 + cx, 2 + cz, heights[cz][cx]);
            sum += heights[cz][cx];
        }
    }
    const float expectedTargetH = sum / 9.0f;

    // Asset name encodes density tier "med" (second token between underscores).
    // BuildingAssetLoader::load() will return nullptr (file absent), but the
    // flatten loop runs before that early return (Phase 11l refactor).
    m_renderer->placeBuildingMesh(2, 2, "res_med_01");

    // Assert that all 9 vertices were flattened.
    const auto& calls = m_terrain.m_flattenCalls;
    EXPECT_EQ(calls.size(), static_cast<size_t>(9))
        << "Expected 9 setTileHeight() calls for a 2x2 (Medium) footprint";

    const int expectedVertices[9][2] = {
        {2,2},{3,2},{4,2},
        {2,3},{3,3},{4,3},
        {2,4},{3,4},{4,4},
    };
    for (const auto& v : expectedVertices) {
        EXPECT_TRUE(hasVertex(calls, v[0], v[1]))
            << "Missing setTileHeight() call for vertex ("
            << v[0] << ", " << v[1] << ")";
    }

    // All calls must carry the same targetH.
    for (const auto& e : calls) {
        EXPECT_FLOAT_EQ(std::get<2>(e), expectedTargetH)
            << "setTileHeight() at (" << std::get<0>(e) << ","
            << std::get<1>(e) << ") carried wrong height";
    }
}

// ---------------------------------------------------------------------------
// IrrlichtRenderer_PlaceLowBuilding_FourCornersOnly
//
// Spec: phase-11l.md §Deliverable 3 — Low-density test
//
// A 1x1 building at tile (5,5) must flatten exactly 4 vertices:
// (5,5), (6,5), (5,6), (6,6).
// The loop with footprintN=1 produces the same 4 calls as the original
// hardcoded pattern — no behavioural change for Low density.
// ---------------------------------------------------------------------------
TEST_F(IrrlichtRendererFlattenTest,
       IrrlichtRenderer_PlaceLowBuilding_FourCornersOnly)
{
    // Set distinct heights at the 4 corners of tile (5,5).
    m_terrain.setHeightAt(5, 5, 10.0f);
    m_terrain.setHeightAt(6, 5, 12.0f);
    m_terrain.setHeightAt(5, 6, 14.0f);
    m_terrain.setHeightAt(6, 6, 16.0f);
    const float expectedTargetH = (10.0f + 12.0f + 14.0f + 16.0f) / 4.0f;  // 13.0f

    // Asset name encodes density tier "low" (second token between underscores).
    m_renderer->placeBuildingMesh(5, 5, "res_low_01");

    const auto& calls = m_terrain.m_flattenCalls;
    EXPECT_EQ(calls.size(), static_cast<size_t>(4))
        << "Expected exactly 4 setTileHeight() calls for a 1x1 (Low) footprint";

    const int expectedVertices[4][2] = {
        {5,5},{6,5},{5,6},{6,6},
    };
    for (const auto& v : expectedVertices) {
        EXPECT_TRUE(hasVertex(calls, v[0], v[1]))
            << "Missing setTileHeight() call for vertex ("
            << v[0] << ", " << v[1] << ")";
    }

    for (const auto& e : calls) {
        EXPECT_FLOAT_EQ(std::get<2>(e), expectedTargetH)
            << "setTileHeight() at (" << std::get<0>(e) << ","
            << std::get<1>(e) << ") carried wrong height";
    }
}
