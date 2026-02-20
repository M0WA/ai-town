#pragma once
#include <cstdint>
#include <string>

// IUIBackend — opaque handle-based UI backend interface.
// Source location: src/ui/ (intentional exception to src/interfaces/ pattern).
// Placing it in src/ui/ ensures its coverage is captured under the 80% coverage gate.
// IrrlichtUIBackend.h/.cpp live in src/rendering/ (depends on Irrlicht headers).
// MockUIBackend lives in tests/ui/mock_ui_backend.h.

using UIElementHandle = uint32_t;
static constexpr UIElementHandle kInvalidUIElement = 0;

// Rect struct — MUST be defined BEFORE IUIBackend to avoid a forward-declaration-as-return-type
// ambiguity in the virtual method signature. Placing the definition after the class compiles
// on some compilers but is non-conforming and breaks with strict C++ parsing rules for return
// types in virtual method declarations.
struct Rect { int x{0}, y{0}, w{0}, h{0}; };

class IUIBackend {
public:
    virtual ~IUIBackend() = default;
    // 1. Create a static text label. Returns an opaque handle for the element.
    virtual UIElementHandle addStaticText(const std::string& text, int x, int y, int w, int h) = 0;
    // 2. Create a clickable button. label is FIRST parameter. Returns an opaque handle.
    virtual UIElementHandle addButton(const std::string& label, int x, int y, int w, int h) = 0;
    // 3. Destroy the element associated with the handle and release its resources.
    virtual void            removeElement(UIElementHandle handle) = 0;
    // 4. Replace the displayed text of an existing element.
    virtual void            setElementText(UIElementHandle handle, const std::string& text) = 0;
    // 5. Show or hide an element. Hidden elements do not receive input events.
    virtual void            setElementVisible(UIElementHandle handle, bool visible) = 0;
    // 6. Query whether an element is currently visible.
    virtual bool            isElementVisible(UIElementHandle handle) const = 0;
    // 7. Enable or disable an element. Disabled elements remain visible but grayed-out
    //    and non-interactive (distinct from setElementVisible).
    virtual void            setElementEnabled(UIElementHandle handle, bool enabled) = 0;  // grayed-out vs interactive
    // 8. Query whether an element is currently enabled (interactive).
    virtual bool            isElementEnabled(UIElementHandle handle) const = 0;
    // 9. Set the opacity of an element. alpha is in [0.0, 1.0].
    virtual void            setElementAlpha(UIElementHandle handle, float alpha) = 0;     // [0.0, 1.0]
    // 10. Assign a texture (identified by its own UIElementHandle) to an image element.
    virtual void            setElementImage(UIElementHandle handle, UIElementHandle textureHandle) = 0;
    // 11. Return the current displayed text of an element. Used in test assertions.
    virtual std::string     getElementText(UIElementHandle handle) const = 0;   // for test assertions on displayed values
    // 12. Return the bounding rectangle of an element in virtual coordinate space.
    //     Returns {x, y, w, h} in virtual space; for position/size assertions.
    virtual Rect            getElementRect(UIElementHandle handle) const = 0;   // {x, y, w, h} in virtual space
    // 13. Return the physical screen width in pixels (driver resolution).
    virtual int             getScreenWidth()  const = 0;
    // 14. Return the physical screen height in pixels (driver resolution).
    virtual int             getScreenHeight() const = 0;
    // 15. Return the virtual UI canvas width (always 1920 in V1).
    //     All UI layout coordinates are defined in virtual 1920x1080 space.
    //     UIManager and all panel code must call this instead of hardcoding 1920.
    virtual int             getVirtualWidth()  const = 0;
    // 16. Return the virtual UI canvas height (always 1080 in V1).
    //     UIManager and all panel code must call this instead of hardcoding 1080.
    virtual int             getVirtualHeight() const = 0;
    // 17. Load a texture from disk and return an opaque handle that can be passed as the
    //     second argument to setElementImage(). Returns kInvalidUIElement on failure (file
    //     not found, unsupported format, or driver error). The backend owns the loaded
    //     texture resource; call removeElement(handle) to release it when no longer needed.
    virtual UIElementHandle loadTexture(const std::string& path) = 0;
};
