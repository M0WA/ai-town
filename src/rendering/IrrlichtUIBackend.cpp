// IrrlichtUIBackend.cpp — Irrlicht-backed implementation of IUIBackend.
//
// THIS IS THE ONLY FILE IN THE PROJECT THAT MAY INCLUDE <irrlicht.h> VIA
// THE IrrlichtUIBackend translation unit. The corresponding header
// (IrrlichtUIBackend.h) uses forward declarations so that test translation
// units can include the header without pulling in Irrlicht at all.
//
// Phase 1/3 stubs: all element-mutation methods are no-ops; addStaticText(),
// addButton(), and loadTexture() return an incrementing non-zero handle so
// that Phase 3 EDT_NULL integration tests can assert handle != kInvalidUIElement.
// Full IGUIEnvironment-backed bodies are a Phase 8 deliverable.

#include "src/rendering/IrrlichtUIBackend.h"  // own header first

#include <irrlicht.h>                          // full Irrlicht types
#include <cassert>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

IrrlichtUIBackend::IrrlichtUIBackend(irr::IrrlichtDevice* device)
    : m_device(device)
    , m_guiEnv(device ? device->getGUIEnvironment() : nullptr)
{
    assert(m_device  != nullptr && "IrrlichtUIBackend requires a non-null device");
    assert(m_guiEnv  != nullptr && "IrrlichtUIBackend: getGUIEnvironment() returned null");
}

IrrlichtUIBackend::~IrrlichtUIBackend() = default;

// ---------------------------------------------------------------------------
// 1. addStaticText
// Phase 3: returns incrementing non-zero handle.
// Phase 8: creates a real IGUIStaticText element via m_guiEnv.
// ---------------------------------------------------------------------------
UIElementHandle IrrlichtUIBackend::addStaticText(
    const std::string& text, int x, int y, int w, int h)
{
    (void)text; (void)x; (void)y; (void)w; (void)h;
    return m_nextHandle++;
}

// ---------------------------------------------------------------------------
// 2. addButton
// Phase 3: returns incrementing non-zero handle.
// Phase 8: creates a real IGUIButton element via m_guiEnv.
// ---------------------------------------------------------------------------
UIElementHandle IrrlichtUIBackend::addButton(
    const std::string& label, int x, int y, int w, int h)
{
    (void)label; (void)x; (void)y; (void)w; (void)h;
    return m_nextHandle++;
}

// ---------------------------------------------------------------------------
// 3. removeElement — Phase 8: look up handle in element map and remove.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::removeElement(UIElementHandle handle)
{
    (void)handle;
}

// ---------------------------------------------------------------------------
// 4. setElementText — Phase 8: update IGUIElement text.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementText(UIElementHandle handle, const std::string& text)
{
    (void)handle; (void)text;
}

// ---------------------------------------------------------------------------
// 5. setElementVisible — Phase 8: call IGUIElement::setVisible().
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementVisible(UIElementHandle handle, bool visible)
{
    (void)handle; (void)visible;
}

// ---------------------------------------------------------------------------
// 6. isElementVisible — Phase 8: call IGUIElement::isVisible().
// ---------------------------------------------------------------------------
bool IrrlichtUIBackend::isElementVisible(UIElementHandle handle) const
{
    (void)handle;
    return false;
}

// ---------------------------------------------------------------------------
// 7. setElementEnabled — Phase 8: call IGUIElement::setEnabled().
// Disabled = grayed-out, non-interactive (distinct from hidden).
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementEnabled(UIElementHandle handle, bool enabled)
{
    (void)handle; (void)enabled;
}

// ---------------------------------------------------------------------------
// 8. isElementEnabled — Phase 8: call IGUIElement::isEnabled().
// ---------------------------------------------------------------------------
bool IrrlichtUIBackend::isElementEnabled(UIElementHandle handle) const
{
    (void)handle;
    return false;
}

// ---------------------------------------------------------------------------
// 9. setElementAlpha — Phase 8: apply alpha to element color channels.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementAlpha(UIElementHandle handle, float alpha)
{
    (void)handle; (void)alpha;
}

// ---------------------------------------------------------------------------
// 10. setElementImage — Phase 8: assign texture to an IGUIImage element.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementImage(UIElementHandle handle, UIElementHandle textureHandle)
{
    (void)handle; (void)textureHandle;
}

// ---------------------------------------------------------------------------
// 11. getElementText — Phase 8: return IGUIElement::getText() as std::string.
// ---------------------------------------------------------------------------
std::string IrrlichtUIBackend::getElementText(UIElementHandle handle) const
{
    (void)handle;
    return {};
}

// ---------------------------------------------------------------------------
// 12. getElementRect — Phase 8: return IGUIElement::getAbsolutePosition().
// ---------------------------------------------------------------------------
Rect IrrlichtUIBackend::getElementRect(UIElementHandle handle) const
{
    (void)handle;
    return Rect{};
}

// ---------------------------------------------------------------------------
// 13. getScreenWidth
// Queries the video driver unconditionally — hardcoded literals are prohibited
// except for the EDT_NULL guard path (divide-by-zero protection for UIScaler).
// ---------------------------------------------------------------------------
int IrrlichtUIBackend::getScreenWidth() const
{
    irr::video::IVideoDriver* driver = m_device->getVideoDriver();
    // EDT_NULL guard: getScreenSize() returns {0,0} for headless/EDT_NULL driver.
    // MUST NOT propagate 0 to UIScaler (would cause divide-by-zero). Return 1280.
    if (driver->getDriverType() == irr::video::EDT_NULL) {
        return 1280;
    }
    return static_cast<int>(driver->getScreenSize().Width);
}

// ---------------------------------------------------------------------------
// 14. getScreenHeight — same EDT_NULL guard as getScreenWidth().
// ---------------------------------------------------------------------------
int IrrlichtUIBackend::getScreenHeight() const
{
    irr::video::IVideoDriver* driver = m_device->getVideoDriver();
    if (driver->getDriverType() == irr::video::EDT_NULL) {
        return 720;
    }
    return static_cast<int>(driver->getScreenSize().Height);
}

// ---------------------------------------------------------------------------
// 15. getVirtualWidth — always 1920 (fixed design constant).
// WARNING: returning 0 would cause divide-by-zero in Phase 3 panel code.
// ---------------------------------------------------------------------------
int IrrlichtUIBackend::getVirtualWidth() const
{
    return 1920;
}

// ---------------------------------------------------------------------------
// 16. getVirtualHeight — always 1080 (fixed design constant).
// WARNING: returning 0 would cause divide-by-zero in Phase 3 panel code.
// ---------------------------------------------------------------------------
int IrrlichtUIBackend::getVirtualHeight() const
{
    return 1080;
}

// ---------------------------------------------------------------------------
// 17. loadTexture
// Phase 3: returns incrementing non-zero handle.
// Phase 8: calls m_device->getVideoDriver()->getTexture() and registers handle.
// ---------------------------------------------------------------------------
UIElementHandle IrrlichtUIBackend::loadTexture(const std::string& path)
{
    (void)path;
    return m_nextHandle++;
}
