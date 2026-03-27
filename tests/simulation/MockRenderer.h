#pragma once
#include "src/interfaces/IRenderer.h"
#include "gmock/gmock.h"
#include <unordered_map>
#include <cstdint>

// MockRenderer — GMock implementation of IRenderer (28 active methods after Phase 11d
// Day-One Commit: original 21 pre-Phase-11d methods + setTilePlacementPreview promoted to
// three-parameter form + 6 new Phase 11d stubs added below removeVehicle).
// Source location: tests/simulation/ (shared across simulation_tests, ui_tests, audio_tests).
// Header-only — no .cpp file. Uses MOCK_METHOD macros only, no definitions.
//
// Phase 9b additions: pickTerrainTile, setTileHoverHighlight, setZoneOverlay,
// getTileScreenBounds.  Default actions:
//   pickTerrainTile       — returns false (no terrain hit)
//   setTileHoverHighlight — no-op void
//   setZoneOverlay        — no-op void
//   getTileScreenBounds   — returns ScreenRect{} (zero-initialised)
//
// Phase 10 additions: getListenerPosition (returns vec3{}), placeBuildingMesh,
// removeBuildingMesh, placeRoadMesh, removeRoadMesh, placeServiceBuildingMesh,
// removeServiceBuildingMesh, placeVehicle, moveVehicle, removeVehicle,
// setTilePlacementPreview.  All void-return placement/removal/vehicle/preview
// methods are no-op void by default — tests that need specific behaviour must add
// EXPECT_CALL / ON_CALL explicitly.
//
// Phase 11d additions (7 new methods — prerequisite for Deliverables 3d and 4c):
//   spawnVehicleAgent, moveVehicleAgent, despawnVehicleAgent — traffic agent rendering;
//     coexist with Phase 10 placeVehicle/moveVehicle/removeVehicle (both sets present).
//   setIntersectionSignalState — traffic signal colour update.
//   showServiceCoverageOverlay, hideServiceCoverageOverlay — service radius overlay.
//   [getListenerPosition already present from Phase 10; not a new Phase 11d addition]
// Phase 11d also extends setTilePlacementPreview to three-parameter form:
//   (freeTiles, freeArgb, blockedTiles) — blockedTiles defaults to {} at call sites.
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
        ON_CALL(*this, spawnVehicleAgent(::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::ReturnArg<0>());  // echo handle back
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
                (int tileX, int tileZ, int footprintSize),
                (override));
    MOCK_METHOD(void, setActiveTool, (ToolMode mode), (override));
    MOCK_METHOD(void, setZoneHoverColour, (unsigned int argb), (override));
    MOCK_METHOD(void, clearDemolishHighlight, (), (override));
    // setZoneOverlay: ZoneOverlayMap alias avoids comma-in-macro.
    MOCK_METHOD(void, setZoneOverlay,
                (int mapTilesX, int mapTilesZ, const ZoneOverlayMap& sparseOverlay),
                (override));
    MOCK_METHOD(ScreenRect, getTileScreenBounds,
                (int tileX, int tileZ),
                (const, override));

    // Phase 10: listener position for sfx_intersection_tick distance cull.
    MOCK_METHOD(vec3, getListenerPosition, (), (const, override));

    // Phase 10 / Phase 11d: multi-tile placement preview (Zone rect / Road line).
    // Phase 11d Day-One Commit promotes this to three-parameter form:
    //   (freeTiles, freeArgb, blockedTiles) — landed atomically with IRenderer.h
    //   signature update, IrrlichtRenderer impl update, and UIManager call-site updates.
    // TileList alias avoids comma-in-template-arg issue with MOCK_METHOD macro.
    using TileList = std::vector<std::pair<int,int>>;
    MOCK_METHOD(void, setTilePlacementPreview,
                (const TileList& freeTiles, uint32_t freeArgb, const TileList& blockedTiles),
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

    // Phase 11d: traffic agent rendering API.
    // spawnVehicleAgent, moveVehicleAgent, despawnVehicleAgent coexist with Phase 10
    // placeVehicle/moveVehicle/removeVehicle — both sets are present simultaneously.
    MOCK_METHOD(void, spawnVehicleAgent,
                (AgentHandle handle, int tileX, int tileZ, ZoneType zone),
                (override));
    MOCK_METHOD(void, moveVehicleAgent,
                (AgentHandle handle, float worldX, float worldZ, float headingDeg),
                (override));
    MOCK_METHOD(void, despawnVehicleAgent,
                (AgentHandle handle),
                (override));
    MOCK_METHOD(void, setIntersectionSignalState,
                (int tileX, int tileZ, SignalPhase phase),
                (override));
    // Phase 11d: service coverage overlay API.
    MOCK_METHOD(void, showServiceCoverageOverlay,
                (int tileX, int tileZ, ServiceBuildingType type, bool degraded),
                (override));
    MOCK_METHOD(void, hideServiceCoverageOverlay,
                (),
                (override));

    // Phase 11m: clear all city scene nodes (buildings, roads, agents).
    MOCK_METHOD(void, clearCity, (), (override));

private:
    TextureHandle m_nextHandle{1};
};
