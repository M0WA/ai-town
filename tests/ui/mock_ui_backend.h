#pragma once

#include "src/ui/IUIBackend.h"
#include "gmock/gmock.h"

// MockUIBackend — GMock implementation of IUIBackend's 18 methods.
// Source location: tests/ui/mock_ui_backend.h
// Returns arbitrary non-zero integer handles (e.g., an incrementing counter)
// with no real objects — unit tests that call UIManager methods never dereference
// Irrlicht pointers, making src/ui/ genuinely headless-testable.
class MockUIBackend : public IUIBackend {
public:
    MOCK_METHOD(UIElementHandle, addStaticText,       (const std::string& text, int x, int y, int w, int h), (override));
    MOCK_METHOD(UIElementHandle, addButton,           (const std::string& label, int x, int y, int w, int h), (override));
    MOCK_METHOD(void,            removeElement,       (UIElementHandle handle),                               (override));
    MOCK_METHOD(void,            setElementText,      (UIElementHandle handle, const std::string& text),     (override));
    MOCK_METHOD(void,            setElementVisible,   (UIElementHandle handle, bool visible),                 (override));
    MOCK_METHOD(bool,            isElementVisible,    (UIElementHandle handle),                               (const, override));
    MOCK_METHOD(void,            setElementEnabled,   (UIElementHandle handle, bool enabled),                 (override));
    MOCK_METHOD(bool,            isElementEnabled,    (UIElementHandle handle),                               (const, override));
    MOCK_METHOD(void,            setElementAlpha,     (UIElementHandle handle, float alpha),                  (override));
    MOCK_METHOD(void,            setElementImage,     (UIElementHandle handle, UIElementHandle textureHandle), (override));
    MOCK_METHOD(std::string,     getElementText,      (UIElementHandle handle),                               (const, override));
    MOCK_METHOD(Rect,            getElementRect,      (UIElementHandle handle),                               (const, override));
    MOCK_METHOD(int,             getScreenWidth,      (),                                                     (const, override));
    MOCK_METHOD(int,             getScreenHeight,     (),                                                     (const, override));
    MOCK_METHOD(int,             getVirtualWidth,     (),                                                     (const, override));
    MOCK_METHOD(int,             getVirtualHeight,    (),                                                     (const, override));
    MOCK_METHOD(UIElementHandle, loadTexture,         (const std::string& path),                              (override));
    MOCK_METHOD(void,            setElementBackground,(UIElementHandle handle, int r, int g, int b, int a),  (override));
};
