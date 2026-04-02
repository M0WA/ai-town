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
// UIRect is a complete type (defined in IUIBackend.h) — it is used as a return
// type of getElementRect() which is an override of the pure-virtual method in
// IUIBackend. DO NOT forward-declare UIRect; IUIBackend.h already defines it.

// IUIBackend.h — UIElementHandle, kInvalidUIElement, UIRect, IUIBackend.
// MUST be first: UIRect must be a complete type before the override declaration.
#include "src/interfaces/IUIBackend.h"

#include <string>
#include <cstdint>
#include <unordered_map>

// Forward declarations for Irrlicht types used only in private members and
// public method signatures.  Full definitions are provided by <irrlicht.h>
// in IrrlichtUIBackend.cpp.
namespace irr {
    class IrrlichtDevice;
    class ILogger;                              // used by m_logger for diagnostic output
    struct SEvent;                              // used in handleGuiHoverEvent signature
    namespace gui  { class IGUIEnvironment; class IGUIElement; class IGUIFont; }
    namespace video { class IVideoDriver; class ITexture; }
}  // namespace irr

// IrrlichtUIBackend — Full Irrlicht-backed implementation of all 21 IUIBackend
// pure-virtual methods. Phase 8 deliverable replacing Phase 1 stubs.
// Method 18 (setElementBackground) added in Phase 9b.
// Method 19 (setElementMonoFont) added in Phase 10.
// Method 21 (setElementTextColor) added in Phase 10c.
// FontTier / selectFontTier / m_hudFont / m_hudMonoFont added in Phase 11g.
//
// Constructor: takes irr::IrrlichtDevice* (non-null, asserted in the .cpp) and
// a pre-cached maxAnisotropy value from RenderSystem (1.0f when extension absent).
// All Irrlicht API calls and all raw GL calls are in IrrlichtUIBackend.cpp —
// this header is safe to include from test translation units that do NOT link
// Irrlicht or GL.
class IrrlichtUIBackend : public IUIBackend {
public:
    // Resolution tier used to select the correct bitmap font atlas size.
    // k720p  → hud_font_720.xml   (cell_h=22, ~720p screens)
    // k1080p → hud_font_1080.xml  (cell_h=33, ~1080p screens)
    // k1440p → hud_font_1440.xml  (cell_h=44, ~1440p screens)
    enum class FontTier { k720p, k1080p, k1440p };
    // Defined inline so unit tests (ui_tests) can call it without linking aitown_render.
    static FontTier selectFontTier(int screenHeight) {
        if (screenHeight < 900)  return FontTier::k720p;
        if (screenHeight < 1260) return FontTier::k1080p;
        return FontTier::k1440p;
    }

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

    // Handle Irrlicht GUI hover events (EGET_ELEMENT_HOVERED / EGET_ELEMENT_LEFT) to
    // swap button sprite cells for hover visual feedback.  Must be called from
    // EventReceiver::OnEvent() for all EET_GUI_EVENT events.  Always returns false
    // so Irrlicht continues its own GUI handling (tooltips, focus).
    // Defined in IrrlichtUIBackend.cpp — full Irrlicht types available there.
    // Forward-declared here using forward-declared irr::SEvent — the .cpp includes
    // <irrlicht.h> which provides the complete type.
    // Not part of IUIBackend — internal rendering concern.
    bool handleGuiHoverEvent(const irr::SEvent& event);

    // Return the monospace font loaded from the tier-specific hud_mono_font_<tier>.xml.
    // Used by HUD and panel code to call element->setOverrideFont(getMonoFont())
    // on numeric IGUIStaticText elements (treasury balance, population count,
    // tax rate fields, monthly revenue/expense, density unlock progress).
    // Returns nullptr when the font file was absent at construction time or when
    // running headless (EDT_NULL); callers must null-check before setOverrideFont().
    // m_hudMonoFont renamed from m_monoFont in Phase 11g.
    // Not part of IUIBackend — concrete backend detail on IrrlichtUIBackend only.
    irr::gui::IGUIFont* getMonoFont() const { return m_hudMonoFont; }

    // Return the proportional font loaded from the tier-specific hud_font_<tier>.xml.
    // Returns nullptr on EDT_NULL or when font file absent.
    // Not part of IUIBackend — concrete backend detail on IrrlichtUIBackend only.
    irr::gui::IGUIFont* getHudFont() const { return m_hudFont; }

    // -------------------------------------------------------------------------
    // IUIBackend overrides — 19 methods
    // Methods 1–16: core element creation, text, visibility, alpha, image, rect,
    //               screen dimensions, virtual dimensions, texture load.
    // Method 17: loadTexture
    // Method 18: setElementBackground  (Phase 9b)
    // Method 19: setElementMonoFont    (Phase 10)
    // Method 21: setElementTextColor   (Phase 10c)
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
    UIRect getElementRect(UIElementHandle handle) const override;

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

    // 18. Set background fill color on a static text element (enables fillBackground).
    //     r, g, b, a in [0, 255]. Has no visible effect on button elements.
    void setElementBackground(UIElementHandle handle, int r, int g, int b, int a) override;

    // 19. Apply the monospace font (hud_mono_font_<tier>.xml) to an IGUIStaticText element.
    //     Calls IGUIStaticText::setOverrideFont(m_hudMonoFont). No-op when m_hudMonoFont is null
    //     (font absent or headless CI mode) — graceful fallback, no assert.
    //     Labels, button text, and panel titles MUST NOT call this method.
    void setElementMonoFont(UIElementHandle handle) override;

    // 20. Reposition and resize an existing element in virtual coordinate space.
    //     Updates the stored virtualRect and calls setRelativePosition() with scaled
    //     physical pixel coordinates. Preserves all other element state.
    void setElementRect(UIElementHandle handle,
                        int x, int y, int w, int h) override;

    // 21. Override the text colour of a static text element (amber numerics, etc.).
    //     Calls IGUIStaticText::setOverrideColor(SColor(255, r, g, b)).
    //     No-op on button elements or invalid handles.
    void setElementTextColor(UIElementHandle handle, int r, int g, int b) override;

private:
    irr::IrrlichtDevice*       m_device{nullptr};
    irr::gui::IGUIEnvironment* m_guiEnv{nullptr};
    irr::video::IVideoDriver*  m_driver{nullptr};

    // Cached logger — derived from m_device->getLogger() at construction time.
    // Used for all runtime diagnostics in place of fprintf(stderr,...).
    // Falls back to fprintf(stderr,...) when null (device absent / headless CI).
    irr::ILogger*              m_logger{nullptr};

    // Proportional bitmap font loaded from tier-specific hud_font_<tier>.xml.
    // null when EDT_NULL (headless) or font file absent; callers must null-check.
    // Ownership: IGUIEnvironment owns the font; this is a non-owning pointer.
    irr::gui::IGUIFont* m_hudFont{nullptr};

    // Monospace bitmap font loaded from tier-specific hud_mono_font_<tier>.xml.
    // null when the file is absent or running headless; callers must null-check.
    // Ownership: Irrlicht's IGUIEnvironment owns the font object; this is a
    // non-owning observing pointer (do NOT call drop() on it).
    // Exposed via getMonoFont() for HUD and panel code to apply via setOverrideFont().
    // Renamed from m_monoFont in Phase 11g.
    irr::gui::IGUIFont* m_hudMonoFont{nullptr};

    // Sprite sheet texture loaded from hud_sprites_ui.png.
    // Used by setElementImage() to assign per-button images via IGUIButton::setImage().
    // null when running headless (EDT_NULL) or when the texture file is absent.
    irr::video::ITexture* m_spriteTexture{nullptr};

    // True only when the sprite sheet texture loaded successfully.
    // setElementImage() must not call setImage() when false to avoid null-ptr dereference.
    bool m_spriteTextureReady{false};

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
        UIRect virtualRect{};
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
