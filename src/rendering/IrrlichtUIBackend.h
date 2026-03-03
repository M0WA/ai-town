#pragma once
// IrrlichtUIBackend.h — declaration-only header.
//
// Irrlicht headers are intentionally ABSENT from this file. All Irrlicht
// includes live exclusively in IrrlichtUIBackend.cpp. This constraint prevents
// Irrlicht types from leaking into src/ui/ headers and test translation units,
// satisfying the testability isolation rule in
// architecture/testing/testability-architecture.md.
//
// GL headers (GL/glew.h) are also intentionally ABSENT from this file.
// GLuint is represented as uint32_t in the header; the static_assert in the
// .cpp confirms sizeof(GLuint) == sizeof(uint32_t).
//
// Rect is a complete type (defined in IUIBackend.h) — it is used as a return
// type of getElementRect() which is an override of the pure-virtual method in
// IUIBackend. DO NOT forward-declare Rect; IUIBackend.h already defines it.

// IUIBackend.h — UIElementHandle, kInvalidUIElement, Rect, IUIBackend.
// MUST be first: Rect must be a complete type before the override declaration.
#include "src/ui/IUIBackend.h"

#include <string>
#include <cstdint>
#include <unordered_map>

// Forward declarations for Irrlicht types used only in private members.
// Full definitions are provided by <irrlicht.h> in IrrlichtUIBackend.cpp.
namespace irr {
    class IrrlichtDevice;
    namespace gui  { class IGUIEnvironment; class IGUIElement; class IGUISpriteBank; }
    namespace video { class IVideoDriver; }
}  // namespace irr

// IrrlichtUIBackend — Full Irrlicht-backed implementation of all 17 IUIBackend
// pure-virtual methods. Phase 8 deliverable replacing Phase 1 stubs.
//
// Constructor: takes irr::IrrlichtDevice* (non-null, asserted in the .cpp) and
// a pre-cached maxAnisotropy value from RenderSystem (1.0f when extension absent).
// All Irrlicht API calls and all raw GL calls are in IrrlichtUIBackend.cpp —
// this header is safe to include from test translation units that do NOT link
// Irrlicht or GL.
class IrrlichtUIBackend : public IUIBackend {
public:
    // Constructor — device must be non-null (programming error otherwise).
    // maxAnisotropy: pre-cached from RenderSystem after glewInit(); 1.0f if
    // GL_EXT_texture_filter_anisotropic is absent. IrrlichtUIBackend MUST NOT
    // query GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT itself — use passed-in value.
    // See architecture/graphics-architecture/irrlicht-device-lifecycle.md for
    // the canonical construction ordering constraint (construct AFTER glewInit).
    IrrlichtUIBackend(irr::IrrlichtDevice* device, float maxAnisotropy = 1.0f);
    ~IrrlichtUIBackend() override;

    // Non-copyable / non-movable — device lifetime managed externally.
    IrrlichtUIBackend(const IrrlichtUIBackend&)            = delete;
    IrrlichtUIBackend& operator=(const IrrlichtUIBackend&) = delete;
    IrrlichtUIBackend(IrrlichtUIBackend&&)                 = delete;
    IrrlichtUIBackend& operator=(IrrlichtUIBackend&&)      = delete;

    // Detect window resize and reposition all GUI elements to match the new
    // physical screen size.  Call once per frame from the main loop.
    // Not part of IUIBackend — concrete method on IrrlichtUIBackend only.
    void handleViewportResize();

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
    irr::IrrlichtDevice*       m_device{nullptr};
    irr::gui::IGUIEnvironment* m_guiEnv{nullptr};
    irr::video::IVideoDriver*  m_driver{nullptr};

    // Sprite bank for IGUIButton sprite assignment.
    // Loaded from assets/textures/ui/hud_sprites_ui.png during construction.
    // Contains one texture entry and 1024 position entries (32×32 grid, 64×64 px/cell).
    // Shared by every button created via addButton(); each button holds a grabbed ref.
    // Null when running headless (EDT_NULL) or when the texture file is absent.
    irr::gui::IGUISpriteBank*  m_spriteBank{nullptr};

    // True only when the sprite sheet texture loaded successfully.
    // setElementImage() must not call setSprite() when false — the sprite bank
    // would have 0 textures but sprites referencing texture[0], causing an
    // out-of-bounds assert in irr::core::array when Irrlicht renders the button.
    bool m_spriteBankReady{false};

    // Cached driver type — avoids virtual dispatch on every method call.
    // Set once at construction from m_driver->getDriverType().
    // Stored as int to avoid forward-declaring Irrlicht's unscoped enum
    // E_DRIVER_TYPE in this header. Cast back in the .cpp.
    int m_driverTypeInt{0};

    // true when m_driverType == EDT_NULL (headless CI / test mode).
    // All raw GL calls MUST be guarded by if (!m_isHeadless).
    bool m_isHeadless{false};

    // Pre-cached hardware anisotropy limit. 1.0f when extension absent.
    // Received from RenderSystem; never self-queried via glGetFloatv.
    float m_maxAnisotropy{1.0f};

    // Monotonically increasing handle counter. Starts at 1 so that every
    // successfully allocated element/texture has a non-zero handle.
    // kInvalidUIElement (0) is never returned for a successful allocation.
    UIElementHandle m_nextHandle{1};

    // Internal element tracking: Irrlicht element pointer + cached virtual rect.
    // Virtual rect is captured at creation and returned by getElementRect()
    // to avoid the physical->virtual round-trip that breaks after window resize.
    struct ElementInfo {
        irr::gui::IGUIElement* element{nullptr};
        Rect virtualRect{};
    };

    // GUI element handle → ElementInfo mapping.
    // Covers both static text and button elements.
    std::unordered_map<UIElementHandle, ElementInfo> m_elementMap;

    // Last known screen dimensions for resize detection in handleViewportResize().
    int m_lastScreenW{0};
    int m_lastScreenH{0};

    // Texture handle → raw GL texture ID mapping (GLuint stored as uint32_t).
    // Textures loaded via loadTexture() are tracked here for cleanup.
    std::unordered_map<UIElementHandle, uint32_t> m_textureHandleMap;

    // Element handle → texture handle mapping for setElementImage().
    // Phase 8 is infrastructure only — per-frame rendering is Phase 9.
    std::unordered_map<UIElementHandle, UIElementHandle> m_imageElementMap;

    // Per-element alpha values for setElementAlpha(). Keyed by element handle.
    // Phase 8 stores alpha; actual rendering application is Phase 9.
    std::unordered_map<UIElementHandle, float> m_alphaMap;

    // Raw GL resource IDs for the UI quad shader and geometry.
    // Stored as uint32_t to avoid GL header inclusion in this header.
    // Created once at construction; destroyed in destructor (headless-guarded).
    uint32_t m_uiQuadProgram{0};
    uint32_t m_quadVAO{0};
    uint32_t m_quadVBO{0};

    // Helper: compile a single GLSL shader from source string.
    // Returns the GL shader object ID, or 0 on failure.
    // Must only be called when !m_isHeadless.
    uint32_t compileShader(uint32_t shaderType, const char* source,
                           const char* label);

    // Helper: read an entire file into a string. Returns empty on failure.
    static std::string readFileToString(const std::string& path);
};
