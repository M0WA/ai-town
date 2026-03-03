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
//   pickTerrainTile  — returns false (no terrain hit)
//   setTileHoverHighlight — no-op void
//   setZoneOverlay   — no-op void
//   getTileScreenBounds  — returns ScreenRect{} (zero-initialised)
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

private:
    TextureHandle m_nextHandle{1};
};
