// ui_smoke_test.cpp — Phase 0 compile-check stub for ui_tests target.
// Instantiates a concrete subclass overriding ALL 17 pure-virtual methods to verify
// every method signature in the interface at Phase 0.
#include "src/interfaces/IUIBackend.h"
#include "src/ui/NotificationManager.h"   // compile-check: verifies constructor signature (4-param as of Phase 10; 4th arg defaults to nullptr)
#include <gtest/gtest.h>

// Minimal concrete subclass — verifies all 19 pure-virtual methods compile.
// Not a mock; does not need meaningful implementations.
struct StubUIBackend : public IUIBackend {
    UIElementHandle addStaticText(const std::string&, int, int, int, int) override { return 1; }
    UIElementHandle addButton(const std::string&, int, int, int, int) override { return 2; }
    void            removeElement(UIElementHandle) override {}
    void            setElementText(UIElementHandle, const std::string&) override {}
    void            setElementVisible(UIElementHandle, bool) override {}
    bool            isElementVisible(UIElementHandle) const override { return true; }
    void            setElementEnabled(UIElementHandle, bool) override {}
    bool            isElementEnabled(UIElementHandle) const override { return true; }
    void            setElementAlpha(UIElementHandle, float) override {}
    void            setElementImage(UIElementHandle, UIElementHandle) override {}
    std::string     getElementText(UIElementHandle) const override { return {}; }
    UIRect          getElementRect(UIElementHandle) const override { return {}; }
    int             getScreenWidth()  const override { return 1920; }
    int             getScreenHeight() const override { return 1080; }
    int             getVirtualWidth()  const override { return 1920; }
    int             getVirtualHeight() const override { return 1080; }
    UIElementHandle loadTexture(const std::string&) override { return kInvalidUIElement; }
    void            setElementBackground(UIElementHandle, int, int, int, int) override {}
    // Method 19 — Phase 10 addition.
    void            setElementMonoFont(UIElementHandle) override {}
};

TEST(UIBackendSmoke, AllMethodsCompile) {
    StubUIBackend b;
    SUCCEED();
}
