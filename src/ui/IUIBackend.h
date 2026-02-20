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
    virtual UIElementHandle addStaticText(const std::wstring& text, int x, int y, int w, int h) = 0;
    virtual UIElementHandle addButton(int x, int y, int w, int h, const std::wstring& label) = 0;
    virtual void            removeElement(UIElementHandle handle) = 0;
    virtual void            setElementText(UIElementHandle handle, const std::wstring& text) = 0;
    virtual void            setElementVisible(UIElementHandle handle, bool visible) = 0;
    virtual bool            isElementVisible(UIElementHandle handle) const = 0;
    virtual void            setElementEnabled(UIElementHandle handle, bool enabled) = 0;  // grayed-out vs interactive
    virtual bool            isElementEnabled(UIElementHandle handle) const = 0;
    virtual void            setElementAlpha(UIElementHandle handle, float alpha) = 0;     // [0.0, 1.0]
    virtual void            setElementImage(UIElementHandle handle, const std::string& atlasKey, int srcX, int srcY, int srcW, int srcH) = 0;
    virtual std::wstring    getElementText(UIElementHandle handle) const = 0;   // for test assertions on displayed values
    virtual Rect            getElementRect(UIElementHandle handle) const = 0;   // {x, y, w, h} in virtual space
    virtual int             getScreenWidth()  const = 0;
    virtual int             getScreenHeight() const = 0;
};
