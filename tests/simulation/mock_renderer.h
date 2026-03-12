#pragma once
#include "src/interfaces/IRenderer.h"
#include "gmock/gmock.h"
#include <unordered_map>
#include <cstdint>

// MockRenderer — GMock implementation of IRenderer.
// Source location: tests/simulation/ (shared across simulation_tests, ui_tests, audio_tests).
// Header-only — no .cpp file. Uses MOCK_METHOD macros only, no definitions.
//
// Phase 9b additions: pickTerrainTile, setTileHoverHighlight, setZoneOverlay,
// getTileScreenBounds.  Default actions:
//   pickTerrainTile      — returns false (no terrain hit)
//   setTileHoverHighlight — no-op void
//   setZoneOverlay        — no-op void
//   getTileScreenBounds   — returns ScreenRect{} (zero-initialised)
//
// Phase 10 additions: getListenerPosition (returns vec3{}), placeBuildingMesh,
// removeBuildingMesh, placeRoadMesh, removeRoadMesh, placeServiceBuildingMesh,
// removeServiceBuildingMesh.  All six placement/removal methods are no-op void
// by default — tests exercising CitySimulation placement callbacks must add
// EXPECT_CALL / ON_CALL explicitly.
//
// NOTE: MOCK_METHOD cannot accept a type argument with a comma (e.g. map<K,V>).
// Use a type alias in the mock class to work around this GMock limitation.
class MockRenderer : public IRenderer {
public:
    // Type alias to avoid comma-in-template-arg issue with MOCK_METHOD macro.
    using ZoneOverlayMap = std::unordered_map<uint64_t, uint32_t>;

    MockRenderer() {
        ON_CALL(*this, loadTexture(::testing::_))
            .WillByDefault([this](const std::string&) {
                return m_nextHandle++;
            });
        ON_CALL(*this, pickTerrainTile(::testing::_, ::testing::_,
                                       ::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(false));
        ON_CALL(*this, getTileScreenBounds(::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(ScreenRect{}));
        ON_CALL(*this, getListenerPosition())
            .WillByDefault(::testing::Return(vec3{}));
    }

    MOCK_METHOD(void,          beginFrame,          (),                                        (override));
    MOCK_METHOD(void,          endFrame,            (),                                        (override));
    MOCK_METHOD(void,          drawScene,           (),                                        (override));
    MOCK_METHOD(TextureHandle, loadTexture,         (const std::string& path),                 (override));
    MOCK_METHOD(void,          setCamera,           (const CameraParams& p),                   (override));
    MOCK_METHOD(void,          rebuildTerrainChunk, (const TerrainChunkRebuildParams& params), (override));

    // Phase 9b — world interaction methods
    MOCK_METHOD(bool, pickTerrainTile,
                (int screenX, int screenY, int& tileX, int& tileZ),
                (const, override));
    MOCK_METHOD(void, setTileHoverHighlight,
                (int tileX, int tileZ, uint32_t argb),
                (override));
    // setZoneOverlay: ZoneOverlayMap alias avoids comma-in-macro.
    MOCK_METHOD(void, setZoneOverlay,
                (int mapTilesX, int mapTilesZ, const ZoneOverlayMap& sparseOverlay),
                (override));
    MOCK_METHOD(ScreenRect, getTileScreenBounds,
                (int tileX, int tileZ),
                (const, override));

    // Phase 10: listener position for sfx_intersection_tick distance cull.
    MOCK_METHOD(vec3, getListenerPosition, (), (const, override));

    // Phase 10: multi-tile placement preview (Zone rect / Road line).
    // TileList alias avoids comma-in-template-arg issue with MOCK_METHOD macro.
    using TileList = std::vector<std::pair<int,int>>;
    MOCK_METHOD(void, setTilePlacementPreview,
                (const TileList& tiles, uint32_t argb),
                (override));

    // Phase 10: building mesh spawning and road mesh rendering API.
    // All six methods are no-op void by default — tests that need specific
    // placement behaviour must set EXPECT_CALL / ON_CALL explicitly.
    //
    // Signatures MUST match IRenderer.h exactly:
    //   placeRoadMesh     — no assetBaseName (road mesh is always the same asset)
    //   placeServiceBuildingMesh — takes ServiceBuildingType, not a string
    MOCK_METHOD(void, placeBuildingMesh,
                (int tileX, int tileZ, const std::string& assetBaseName),
                (override));
    MOCK_METHOD(void, removeBuildingMesh,
                (int tileX, int tileZ),
                (override));
    MOCK_METHOD(void, placeRoadMesh,
                (int tileX, int tileZ),
                (override));
    MOCK_METHOD(void, removeRoadMesh,
                (int tileX, int tileZ),
                (override));
    MOCK_METHOD(void, placeServiceBuildingMesh,
                (int tileX, int tileZ, ServiceBuildingType type),
                (override));
    MOCK_METHOD(void, removeServiceBuildingMesh,
                (int tileX, int tileZ),
                (override));

    // Phase 10: vehicle rendering API.
    // All three methods are no-op void by default — tests that need specific
    // vehicle placement/movement/removal behaviour must set EXPECT_CALL / ON_CALL
    // explicitly.
    //
    // Signatures MUST match IRenderer.h exactly:
    //   placeVehicle  — vehicleId + assetName + world position + yawDegrees
    //   moveVehicle   — vehicleId + world position + yawDegrees
    //   removeVehicle — vehicleId only
    MOCK_METHOD(void, placeVehicle,
                (uint32_t vehicleId, const std::string& assetName,
                 float worldX, float worldY, float worldZ, float yawDegrees),
                (override));
    MOCK_METHOD(void, moveVehicle,
                (uint32_t vehicleId,
                 float worldX, float worldY, float worldZ, float yawDegrees),
                (override));
    MOCK_METHOD(void, removeVehicle,
                (uint32_t vehicleId),
                (override));

private:
    TextureHandle m_nextHandle{1};
};
