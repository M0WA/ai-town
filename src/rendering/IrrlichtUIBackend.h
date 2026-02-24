#pragma once
// IrrlichtUIBackend.h — declaration-only header.
//
// Irrlicht headers are intentionally ABSENT from this file. All Irrlicht
// includes live exclusively in IrrlichtUIBackend.cpp. This constraint prevents
// Irrlicht types from leaking into src/ui/ headers and test translation units,
// satisfying the testability isolation rule in
// architecture/testing/testability-architecture.md.
//
// Forward declarations below are sufficient because no method signature needs
// a complete Irrlicht type — all Irrlicht usage is confined to the .cpp.
//
// Rect is a complete type (defined in IUIBackend.h) — it is used as a return
// type of getElementRect() which is an override of the pure-virtual method in
// IUIBackend. DO NOT forward-declare Rect; IUIBackend.h already defines it.

// IUIBackend.h — UIElementHandle, kInvalidUIElement, Rect, IUIBackend.
// MUST be first: Rect must be a complete type before the override declaration.
#include "src/ui/IUIBackend.h"

#include <string>
#include <cstdint>

// Forward declarations for Irrlicht types used only in private members.
// Full definitions are provided by <irrlicht.h> in IrrlichtUIBackend.cpp.
namespace irr {
    class IrrlichtDevice;
    namespace gui  { class IGUIEnvironment; class IGUIElement; }
    namespace video { class IVideoDriver; }
}  // namespace irr

// IrrlichtUIBackend — Phase 1 compile-target stub implementing all 17 IUIBackend
// pure-virtual methods. Full Irrlicht-backed implementation is a Phase 8 deliverable.
//
// Constructor: takes irr::IrrlichtDevice* (non-null, asserted in the .cpp).
// All Irrlicht API calls are in IrrlichtUIBackend.cpp — this header is safe to
// include from test translation units that do NOT link Irrlicht.
class IrrlichtUIBackend : public IUIBackend {
public:
    // Constructor — device must be non-null (programming error otherwise).
    explicit IrrlichtUIBackend(irr::IrrlichtDevice* device);
    ~IrrlichtUIBackend() override;

    // Non-copyable / non-movable — device lifetime managed externally.
    IrrlichtUIBackend(const IrrlichtUIBackend&)            = delete;
    IrrlichtUIBackend& operator=(const IrrlichtUIBackend&) = delete;
    IrrlichtUIBackend(IrrlichtUIBackend&&)                 = delete;
    IrrlichtUIBackend& operator=(IrrlichtUIBackend&&)      = delete;

    // -------------------------------------------------------------------------
    // IUIBackend overrides — 17 methods
    // -------------------------------------------------------------------------

    // 1.
    UIElementHandle addStaticText(const std::string& text,
                                  int x, int y, int w, int h) override;

    // 2.
    UIElementHandle addButton(const std::string& label,
                              int x, int y, int w, int h) override;

    // 3.
    void removeElement(UIElementHandle handle) override;

    // 4.
    void setElementText(UIElementHandle handle, const std::string& text) override;

    // 5.
    void setElementVisible(UIElementHandle handle, bool visible) override;

    // 6.
    bool isElementVisible(UIElementHandle handle) const override;

    // 7. Disabled = grayed-out, non-interactive (distinct from hidden).
    void setElementEnabled(UIElementHandle handle, bool enabled) override;

    // 8.
    bool isElementEnabled(UIElementHandle handle) const override;

    // 9.
    void setElementAlpha(UIElementHandle handle, float alpha) override;

    // 10.
    void setElementImage(UIElementHandle handle, UIElementHandle textureHandle) override;

    // 11.
    std::string getElementText(UIElementHandle handle) const override;

    // 12.
    Rect getElementRect(UIElementHandle handle) const override;

    // 13. Physical screen width — queries driver (EDT_NULL guard applied in .cpp).
    int getScreenWidth()  const override;

    // 14. Physical screen height — queries driver (EDT_NULL guard applied in .cpp).
    int getScreenHeight() const override;

    // 15. Virtual canvas width — always 1920 (fixed design constant).
    int getVirtualWidth()  const override;

    // 16. Virtual canvas height — always 1080 (fixed design constant).
    int getVirtualHeight() const override;

    // 17.
    UIElementHandle loadTexture(const std::string& path) override;

private:
    irr::IrrlichtDevice*      m_device{nullptr};
    irr::gui::IGUIEnvironment* m_guiEnv{nullptr};

    // Monotonically increasing handle counter. Starts at 1 so that every
    // successfully allocated element/texture has a non-zero handle.
    // kInvalidUIElement (0) is never returned for a successful allocation.
    UIElementHandle m_nextHandle{1};
};
