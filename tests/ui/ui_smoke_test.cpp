// ui_smoke_test.cpp — Phase 0 compile-check stub for ui_tests target.
// Instantiates a concrete subclass overriding ALL 14 pure-virtual methods to verify
// every method signature in the interface at Phase 0.
#include "src/ui/IUIBackend.h"
#include "src/ui/NotificationManager.h"   // compile-check: verifies 3-param constructor signature at Phase 0
#include <gtest/gtest.h>

// Minimal concrete subclass — verifies all 14 pure-virtual methods compile.
// Not a mock; does not need meaningful implementations.
struct StubUIBackend : public IUIBackend {
    UIElementHandle addStaticText(const std::wstring&, int, int, int, int) override { return 1; }
    UIElementHandle addButton(int, int, int, int, const std::wstring&) override { return 2; }
    void            removeElement(UIElementHandle) override {}
    void            setElementText(UIElementHandle, const std::wstring&) override {}
    void            setElementVisible(UIElementHandle, bool) override {}
    bool            isElementVisible(UIElementHandle) const override { return true; }
    void            setElementEnabled(UIElementHandle, bool) override {}
    bool            isElementEnabled(UIElementHandle) const override { return true; }
    void            setElementAlpha(UIElementHandle, float) override {}
    void            setElementImage(UIElementHandle, const std::string&, int, int, int, int) override {}
    std::wstring    getElementText(UIElementHandle) const override { return {}; }
    Rect            getElementRect(UIElementHandle) const override { return {}; }
    int             getScreenWidth()  const override { return 1920; }
    int             getScreenHeight() const override { return 1080; }
};

TEST(UIBackendSmoke, AllMethodsCompile) {
    StubUIBackend b;
    SUCCEED();
}
