#pragma once

#include "src/interfaces/IUIBackend.h"
#include "gmock/gmock.h"

// MockUIBackend — GMock implementation of IUIBackend's 21 methods.
// Source location: tests/ui/mock_ui_backend.h
// Returns arbitrary non-zero integer handles (e.g., an incrementing counter)
// with no real objects — unit tests that call UIManager methods never dereference
// Irrlicht pointers, making src/ui/ genuinely headless-testable.
class MockUIBackend : public IUIBackend {
public:
    MOCK_METHOD(UIElementHandle, addStaticText,        (const std::string& text, int x, int y, int w, int h),  (override));
    MOCK_METHOD(UIElementHandle, addButton,            (const std::string& label, int x, int y, int w, int h), (override));
    MOCK_METHOD(void,            removeElement,        (UIElementHandle handle),                                (override));
    MOCK_METHOD(void,            setElementText,       (UIElementHandle handle, const std::string& text),      (override));
    MOCK_METHOD(void,            setElementVisible,    (UIElementHandle handle, bool visible),                  (override));
    MOCK_METHOD(bool,            isElementVisible,     (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(void,            setElementEnabled,    (UIElementHandle handle, bool enabled),                  (override));
    MOCK_METHOD(bool,            isElementEnabled,     (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(void,            setElementAlpha,      (UIElementHandle handle, float alpha),                   (override));
    MOCK_METHOD(void,            setElementImage,      (UIElementHandle handle, UIElementHandle textureHandle), (override));
    MOCK_METHOD(std::string,     getElementText,       (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(Rect,            getElementRect,       (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(int,             getScreenWidth,       (),                                                      (const, override));
    MOCK_METHOD(int,             getScreenHeight,      (),                                                      (const, override));
    MOCK_METHOD(int,             getVirtualWidth,      (),                                                      (const, override));
    MOCK_METHOD(int,             getVirtualHeight,     (),                                                      (const, override));
    MOCK_METHOD(UIElementHandle, loadTexture,          (const std::string& path),                               (override));
    MOCK_METHOD(void,            setElementBackground, (UIElementHandle handle, int r, int g, int b, int a),   (override));
    // Method 19 — Phase 10 addition.
    // No-op in most tests; use NiceMock<MockUIBackend> to suppress unexpected-call warnings
    // when the system under test calls setElementMonoFont() during construction.
    MOCK_METHOD(void,            setElementMonoFont,   (UIElementHandle handle),                                (override));
    // Method 20 — repositions an existing element in virtual coordinate space without
    // destroying its handle. Used by ModalDialog::setDialogRect() to centre dialog content.
    // No-op in most tests; NiceMock<MockUIBackend> suppresses unexpected-call warnings.
    MOCK_METHOD(void,            setElementRect,       (UIElementHandle handle, int x, int y, int w, int h),   (override));
    // Method 21 — overrides text colour on a static text element.
    // HUD numeric readouts call this with amber #F0B429 = (240, 180, 41).
    // No-op in most tests; NiceMock<MockUIBackend> suppresses unexpected-call warnings.
    MOCK_METHOD(void,            setElementTextColor,  (UIElementHandle handle, int r, int g, int b),           (override));
};
