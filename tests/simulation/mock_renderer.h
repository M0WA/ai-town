#pragma once
#include "src/interfaces/IRenderer.h"
#include "gmock/gmock.h"

// MockRenderer — GMock implementation of IRenderer's 5 methods.
// Source location: tests/simulation/ (shared across simulation_tests, ui_tests, audio_tests).
// Header-only — no .cpp file. Uses MOCK_METHOD macros only, no definitions.
class MockRenderer : public IRenderer {
public:
    MockRenderer() {
        ON_CALL(*this, loadTexture(::testing::_))
            .WillByDefault([this](const std::string&) {
                return m_nextHandle++;
            });
    }

    MOCK_METHOD(void,          beginFrame,   (),                           (override));
    MOCK_METHOD(void,          endFrame,     (),                           (override));
    MOCK_METHOD(void,          drawScene,    (),                           (override));
    MOCK_METHOD(TextureHandle, loadTexture,  (const std::string& path),    (override));
    MOCK_METHOD(void,          setCamera,    (const CameraParams& p),      (override));

private:
    TextureHandle m_nextHandle{1};
};
