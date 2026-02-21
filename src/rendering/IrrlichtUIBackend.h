#pragma once
// IrrlichtUIBackend.h — MUST #include "src/ui/IUIBackend.h" as the FIRST substantive
// include so that the Rect struct is a complete type before getElementRect() return type
// is declared. DO NOT forward-declare Rect — a forward declaration of a struct used as
// a return type in a virtual method override is non-conforming.
#include "src/ui/IUIBackend.h"  // UIElementHandle, kInvalidUIElement, Rect — MUST be first

#include <irrlicht.h>
#include <unordered_map>
#include <cassert>

// IrrlichtUIBackend — Phase 1 compile-target stub implementing all 17 IUIBackend pure-virtual
// methods. Full implementation is a Phase 8 deliverable.
//
// Constructor: takes irr::IrrlichtDevice* (non-null, asserted). Rationale: all screen
// dimension queries go through m_device->getVideoDriver()->getScreenSize(); taking a raw
// IVideoDriver* would prevent future use of device-level features.
//
// getScreenWidth()/getScreenHeight(): MUST query the device driver unconditionally — literal
// fallbacks (1280/720) are PROHIBITED except for the EDT_NULL guard path.
// EDT_NULL guard: when driver type == EDT_NULL, getScreenSize() returns {0,0} which MUST NOT
// propagate to UIScaler (would cause divide-by-zero). Return fallback 1280/720 for EDT_NULL.
//
// getVirtualWidth()/getVirtualHeight(): return fixed constants 1920/1080 — the virtual canvas
// is a fixed design constant, independent of physical window size. Returning 0 would cause
// divide-by-zero in Phase 3 panel code.
class IrrlichtUIBackend : public IUIBackend {
public:
    // Constructor — device must be non-null (programming error otherwise).
    explicit IrrlichtUIBackend(irr::IrrlichtDevice* device)
        : m_device(device)
    {
        assert(m_device != nullptr && "IrrlichtUIBackend requires a non-null device");
    }

    ~IrrlichtUIBackend() override = default;

    // Non-copyable / non-movable — device lifetime managed externally
    IrrlichtUIBackend(const IrrlichtUIBackend&)            = delete;
    IrrlichtUIBackend& operator=(const IrrlichtUIBackend&) = delete;
    IrrlichtUIBackend(IrrlichtUIBackend&&)                 = delete;
    IrrlichtUIBackend& operator=(IrrlichtUIBackend&&)      = delete;

    // -------------------------------------------------------------------------
    // IUIBackend overrides — 17 methods (Phase 1 stubs)
    // -------------------------------------------------------------------------

    // 1.
    UIElementHandle addStaticText(const std::string& /*text*/,
                                  int /*x*/, int /*y*/, int /*w*/, int /*h*/) override {
        return kInvalidUIElement;
    }

    // 2.
    UIElementHandle addButton(const std::string& /*label*/,
                              int /*x*/, int /*y*/, int /*w*/, int /*h*/) override {
        return kInvalidUIElement;
    }

    // 3.
    void removeElement(UIElementHandle /*handle*/) override {}

    // 4.
    void setElementText(UIElementHandle /*handle*/, const std::string& /*text*/) override {}

    // 5.
    void setElementVisible(UIElementHandle /*handle*/, bool /*visible*/) override {}

    // 6.
    bool isElementVisible(UIElementHandle /*handle*/) const override {
        return false;
    }

    // 7. Disabled = grayed-out, non-interactive (distinct from hidden).
    void setElementEnabled(UIElementHandle /*handle*/, bool /*enabled*/) override {}

    // 8.
    bool isElementEnabled(UIElementHandle /*handle*/) const override {
        return false;
    }

    // 9.
    void setElementAlpha(UIElementHandle /*handle*/, float /*alpha*/) override {}

    // 10.
    void setElementImage(UIElementHandle /*handle*/, UIElementHandle /*textureHandle*/) override {}

    // 11.
    std::string getElementText(UIElementHandle /*handle*/) const override {
        return {};
    }

    // 12.
    Rect getElementRect(UIElementHandle /*handle*/) const override {
        return Rect{};
    }

    // 13. Physical screen width — queries driver (not hardcoded; EDT_NULL guard applied).
    int getScreenWidth() const override {
        irr::video::IVideoDriver* driver = m_device->getVideoDriver();
        // EDT_NULL guard: getScreenSize() returns {0,0} for headless/EDT_NULL driver.
        // MUST NOT propagate 0 to UIScaler (divide-by-zero). Return fallback 1280.
        if (driver->getDriverType() == irr::video::EDT_NULL) {
            return 1280;
        }
        return static_cast<int>(driver->getScreenSize().Width);
    }

    // 14. Physical screen height — queries driver (not hardcoded; EDT_NULL guard applied).
    int getScreenHeight() const override {
        irr::video::IVideoDriver* driver = m_device->getVideoDriver();
        // EDT_NULL guard: same as getScreenWidth().
        if (driver->getDriverType() == irr::video::EDT_NULL) {
            return 720;
        }
        return static_cast<int>(driver->getScreenSize().Height);
    }

    // 15. Virtual canvas width — always 1920 (fixed design constant, independent of physical size).
    //     WARNING: returning 0 would cause divide-by-zero in Phase 3 panel code.
    int getVirtualWidth() const override {
        return 1920;
    }

    // 16. Virtual canvas height — always 1080 (fixed design constant).
    //     WARNING: returning 0 would cause divide-by-zero in Phase 3 panel code.
    int getVirtualHeight() const override {
        return 1080;
    }

    // 17.
    UIElementHandle loadTexture(const std::string& /*path*/) override {
        return kInvalidUIElement;
    }

private:
    irr::IrrlichtDevice* m_device;

    // Texture/element handle map (Phase 8 populates this fully).
    std::unordered_map<UIElementHandle, irr::gui::IGUIElement*> m_elements;
};
