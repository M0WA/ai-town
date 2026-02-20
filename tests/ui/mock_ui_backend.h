#pragma once

#include "src/ui/IUIBackend.h"
#include "gmock/gmock.h"

// MockUIBackend — GMock implementation of IUIBackend's 14 methods.
// Source location: tests/ui/mock_ui_backend.h
// Returns arbitrary non-zero integer handles (e.g., an incrementing counter)
// with no real objects — unit tests that call UIManager methods never dereference
// Irrlicht pointers, making src/ui/ genuinely headless-testable.
class MockUIBackend : public IUIBackend {
public:
    MOCK_METHOD(UIElementHandle, addStaticText,    (const std::wstring& text, int x, int y, int w, int h), (override));
    MOCK_METHOD(UIElementHandle, addButton,        (int x, int y, int w, int h, const std::wstring& label), (override));
    MOCK_METHOD(void,            removeElement,    (UIElementHandle handle),                                (override));
    MOCK_METHOD(void,            setElementText,   (UIElementHandle handle, const std::wstring& text),     (override));
    MOCK_METHOD(void,            setElementVisible,(UIElementHandle handle, bool visible),                  (override));
    MOCK_METHOD(bool,            isElementVisible, (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(void,            setElementEnabled,(UIElementHandle handle, bool enabled),                  (override));
    MOCK_METHOD(bool,            isElementEnabled, (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(void,            setElementAlpha,  (UIElementHandle handle, float alpha),                  (override));
    MOCK_METHOD(void,            setElementImage,  (UIElementHandle handle, const std::string& atlasKey, int srcX, int srcY, int srcW, int srcH), (override));
    MOCK_METHOD(std::wstring,    getElementText,   (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(Rect,            getElementRect,   (UIElementHandle handle),                                (const, override));
    MOCK_METHOD(int,             getScreenWidth,   (),                                                      (const, override));
    MOCK_METHOD(int,             getScreenHeight,  (),                                                      (const, override));
};
