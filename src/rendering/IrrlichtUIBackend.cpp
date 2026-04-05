// IrrlichtUIBackend.cpp — Irrlicht-backed implementation of IUIBackend.
//
// THIS IS THE ONLY FILE IN THE PROJECT THAT MAY INCLUDE <irrlicht.h> VIA
// THE IrrlichtUIBackend translation unit. The corresponding header
// (IrrlichtUIBackend.h) uses forward declarations so that test translation
// units can include the header without pulling in Irrlicht at all.
//
// Phase 8 implementation: all 17 IUIBackend methods backed by real Irrlicht
// IGUIElement wrappers, plus raw GL texture upload in loadTexture() and
// ui_quad shader compilation for the image rendering pipeline.

#include "src/rendering/IrrlichtUIBackend.h"  // own header first
#include "src/platform/PlatformUtils.h"

#include <GL/glew.h>                          // MUST come before irrlicht.h
#include <irrlicht.h>                          // full Irrlicht types

#include "src/ui/hud_sprite_ids.h"            // kSpriteXxxHover / Active / Inactive constants

#include <cassert>
#include <cstdio>   // fprintf — fallback when m_logger is null
#include <fstream>
#include <sstream>
#include <vector>

// Verify that uint32_t (used in header to avoid GL includes) matches GLuint.
static_assert(sizeof(GLuint) == sizeof(uint32_t),
              "GLuint must be the same size as uint32_t");

// ---------------------------------------------------------------------------
// Helper: read an entire file into a std::string.
// Returns empty string on failure.
// ---------------------------------------------------------------------------
std::string IrrlichtUIBackend::readFileToString(const std::string& path)
{
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// ---------------------------------------------------------------------------
// Helper: compile a single GLSL shader from source string.
// Returns the GL shader object ID, or 0 on failure.
// Caller is responsible for only invoking this when !m_isHeadless.
// ---------------------------------------------------------------------------
uint32_t IrrlichtUIBackend::compileShader(uint32_t shaderType, const char* source,
                                           const char* label)
{
    GLuint shader = glCreateShader(static_cast<GLenum>(shaderType));
    if (shader == 0) {
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        // Retrieve and log the error message for diagnosis.
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 0) {
            std::vector<char> log(static_cast<size_t>(logLen));
            glGetShaderInfoLog(shader, logLen, nullptr, log.data());
            // Log the shader compilation error (label identifies which shader).
            (void)log; // In production, pipe to LOG_ERROR; here assert catches it.
            (void)label;
        }
        assert(false && "GLSL shader compile failed — see info log");
        glDeleteShader(shader);
        return 0;
    }
    return static_cast<uint32_t>(shader);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

IrrlichtUIBackend::IrrlichtUIBackend(irr::IrrlichtDevice* device,
                                     float maxAnisotropy)
    : m_device(device)
    , m_guiEnv(device ? device->getGUIEnvironment() : nullptr)
    , m_driver(device ? device->getVideoDriver() : nullptr)
    , m_logger(device ? device->getLogger() : nullptr)
    , m_driverTypeInt(m_driver ? static_cast<int>(m_driver->getDriverType()) : 0)
    , m_isHeadless(m_driver ? (m_driver->getDriverType() == irr::video::EDT_NULL) : true)
    , m_maxAnisotropy(maxAnisotropy)
{
    assert(m_device  != nullptr && "IrrlichtUIBackend requires a non-null device");
    assert(m_guiEnv  != nullptr && "IrrlichtUIBackend: getGUIEnvironment() returned null");
    assert(m_driver  != nullptr && "IrrlichtUIBackend: getVideoDriver() returned null");

    // Snapshot initial screen size so handleViewportResize() has a baseline.
    m_lastScreenW = getScreenWidth();
    m_lastScreenH = getScreenHeight();

    // --- Compile and link the ui_quad shader program (raw GL path) -----------
    // Guarded: no GL calls in headless mode (EDT_NULL has no GL context).
    if (!m_isHeadless) {
        // Read shader source files.
        std::string vertSrc = readFileToString(g_assetsDir + "/shaders/ui_quad.vert");
        std::string fragSrc = readFileToString(g_assetsDir + "/shaders/ui_quad.frag");

        if (vertSrc.empty() || fragSrc.empty()) {
            // Shader files not found — set program to 0 (graceful degradation).
            // setElementImage() will fall back to a silent no-op.
            assert(false && "ui_quad shader source files not found");
            m_uiQuadProgram = 0;
        } else {
            // Compile vertex shader.
            GLuint vs = static_cast<GLuint>(
                compileShader(GL_VERTEX_SHADER, vertSrc.c_str(), "ui_quad.vert"));
            if (vs == 0) {
                m_uiQuadProgram = 0;
            } else {
                // Compile fragment shader.
                GLuint fs = static_cast<GLuint>(
                    compileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), "ui_quad.frag"));
                if (fs == 0) {
                    glDeleteShader(vs);
                    m_uiQuadProgram = 0;
                } else {
                    // Both shaders compiled — link program.
                    GLuint prog = glCreateProgram();
                    glAttachShader(prog, vs);
                    glAttachShader(prog, fs);

                    // Bind attribute locations BEFORE linking.
                    // a_pos = 0, a_uv = 1 (matches ui_quad.vert layout).
                    glBindAttribLocation(prog, 0, "a_pos");
                    glBindAttribLocation(prog, 1, "a_uv");

                    glLinkProgram(prog);

                    // Shaders can be detached and deleted after linking.
                    glDetachShader(prog, vs);
                    glDetachShader(prog, fs);
                    glDeleteShader(vs);
                    glDeleteShader(fs);

                    GLint linked = GL_FALSE;
                    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
                    if (linked == GL_FALSE) {
                        GLint logLen = 0;
                        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
                        if (logLen > 0) {
                            std::vector<char> log(static_cast<size_t>(logLen));
                            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
                            (void)log; // In production, pipe to LOG_ERROR.
                        }
                        assert(false && "ui_quad shader link failed");
                        glDeleteProgram(prog);
                        m_uiQuadProgram = 0;
                    } else {
                        m_uiQuadProgram = static_cast<uint32_t>(prog);

                        // Bind u_tex sampler to texture unit 0.
                        // Must be done with the program active; save/restore
                        // GL_CURRENT_PROGRAM to avoid corrupting external state.
                        GLint prevProg = 0;
                        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
                        glUseProgram(prog);
                        glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
                        glUseProgram(static_cast<GLuint>(prevProg));
                    }
                }
            }
        }

        // --- Create VAO/VBO for quad rendering (raw GL image path, future use) ---
        // 4 vertices x 4 floats each (x, y, u, v) = 16 floats, GL_DYNAMIC_DRAW.
        // Attribute layout: a_pos at location 0, a_uv at location 1.
        glGenVertexArrays(1, reinterpret_cast<GLuint*>(&m_quadVAO));
        glGenBuffers(1, reinterpret_cast<GLuint*>(&m_quadVBO));

        glBindVertexArray(static_cast<GLuint>(m_quadVAO));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_quadVBO));

        // Initial allocation: 4 vertices * 4 floats (x, y, u, v).
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(float) * 16),
                     nullptr, GL_DYNAMIC_DRAW);

        // a_pos: location 0, 2 floats, stride 4 floats, offset 0.
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              4 * static_cast<GLsizei>(sizeof(float)),
                              reinterpret_cast<void*>(0));
        // a_uv: location 1, 2 floats, stride 4 floats, offset 2 floats.
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                              4 * static_cast<GLsizei>(sizeof(float)),
                              reinterpret_cast<void*>(2 * sizeof(float)));

        // Enable attributes INSIDE VAO scope so the state is recorded.
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        // Close VAO recording scope.
        // MUST also unbind GL_ARRAY_BUFFER: it is global state (NOT per-VAO).
        // Leaving it bound causes Irrlicht's COpenGLDriver to reinterpret
        // client-side vertex array pointers (glVertexPointer, etc.) as byte
        // offsets into this 64-byte VBO, silently producing no geometry.
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // --- Load sprite sheet texture for button icon assignment ---
    // Use PNG — Irrlicht's built-in PNG loader is unconditionally compiled in.
    // The texture is shared across all buttons; setElementImage() calls
    // IGUIButton::setImage(m_spriteTexture, srcRect) with the per-sprite source rect.
    m_spriteTexture = m_driver->getTexture((g_assetsDir + "/textures/ui/hud_sprites_ui.png").c_str());
    m_spriteTextureReady = (m_spriteTexture != nullptr);

    // --- Font tier selection and loading ---
    //
    // Skip font loading entirely in headless (EDT_NULL) mode — no GUI environment
    // has fonts and the font pointers remain nullptr, which is the valid state for
    // all subsequent callers (null-checked before use).
    if (!m_isHeadless) {
        const FontTier tier = selectFontTier(getScreenHeight());
        const char* suffix = (tier == FontTier::k720p)  ? "720" :
                             (tier == FontTier::k1080p) ? "1080" : "1440";

        // --- Load proportional font ---
        {
            std::string fontPath = g_assetsDir + "/fonts/hud_font_" + suffix + ".xml";
            irr::gui::IGUIFont* hudFont = m_guiEnv->getFont(fontPath.c_str());
            if (hudFont) {
                m_hudFont = hudFont;
                irr::gui::IGUISkin* skin = m_guiEnv->getSkin();
                if (skin) {
                    skin->setFont(m_hudFont);
                }
            } else {
                const std::string warnMsg =
                    std::string("[IrrlichtUIBackend] ") + fontPath +
                    " not found — falling back to Irrlicht built-in 8px font. "
                    "HUD text will be unreadably small.";
                if (m_logger) {
                    m_logger->log(warnMsg.c_str(), irr::ELL_WARNING);
                } else {
                    fprintf(stderr, "[IrrlichtUIBackend WARNING] %s\n", warnMsg.c_str());
                }
            }
        }

        // --- Load monospace font ---
        {
            std::string monoPath = g_assetsDir + "/fonts/hud_mono_font_" + suffix + ".xml";
            irr::gui::IGUIFont* monoFont = m_guiEnv->getFont(monoPath.c_str());
            if (monoFont) {
                m_hudMonoFont = monoFont;
            } else {
                const std::string monoWarnMsg =
                    std::string("[IrrlichtUIBackend] ") + monoPath +
                    " not found — falling back to default font for numeric HUD elements. "
                    "Treasury balance and population count will use the proportional font.";
                if (m_logger) {
                    m_logger->log(monoWarnMsg.c_str(), irr::ELL_WARNING);
                } else {
                    fprintf(stderr, "[IrrlichtUIBackend WARNING] %s\n", monoWarnMsg.c_str());
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

IrrlichtUIBackend::~IrrlichtUIBackend()
{
    if (!m_isHeadless) {
        // Delete all raw GL textures created by loadTexture().
        for (auto& [handle, glTexId] : m_textureHandleMap) {
            GLuint texId = static_cast<GLuint>(glTexId);
            glDeleteTextures(1, &texId);
        }
        m_textureHandleMap.clear();

        // Delete shader program.
        if (m_uiQuadProgram != 0) {
            glDeleteProgram(static_cast<GLuint>(m_uiQuadProgram));
            m_uiQuadProgram = 0;
        }

        // Delete VBO and VAO.
        if (m_quadVBO != 0) {
            GLuint vbo = static_cast<GLuint>(m_quadVBO);
            glDeleteBuffers(1, &vbo);
            m_quadVBO = 0;
        }
        if (m_quadVAO != 0) {
            GLuint vao = static_cast<GLuint>(m_quadVAO);
            glDeleteVertexArrays(1, &vao);
            m_quadVAO = 0;
        }
    }

    // m_spriteTexture is owned by Irrlicht's driver texture cache — do NOT call drop() on it.
    // The driver owns all textures loaded via getTexture(); we just hold a non-owning pointer.
    m_spriteTexture = nullptr;
    m_spriteTextureReady = false;

    // Clear all maps (pointers into Irrlicht's scene graph are not owned by us;
    // Irrlicht's IGUIEnvironment owns the elements and will clean them up when
    // the device is dropped).
    m_elementMap.clear();
    m_textureHandleMap.clear();
    m_imageElementMap.clear();
    m_alphaMap.clear();
}

// ---------------------------------------------------------------------------
// 1. addStaticText
// Creates a real IGUIStaticText element via m_guiEnv.
// ---------------------------------------------------------------------------
UIElementHandle IrrlichtUIBackend::addStaticText(
    const std::string& text, int x, int y, int w, int h)
{
    // Convert std::string (ASCII/Latin-1) to Irrlicht's wchar_t string.
    irr::core::stringw wtext(text.c_str());

    // Scale virtual coordinates (1920x1080 design space) to physical screen pixels.
    // Panels pass virtual coords; Irrlicht positions elements in physical pixel space.
    const int sw = getScreenWidth();
    const int sh = getScreenHeight();
    const int vw = getVirtualWidth();
    const int vh = getVirtualHeight();
    const int px = (x * sw) / vw;
    const int py = (y * sh) / vh;
    const int pw = (w * sw) / vw;
    const int ph = (h * sh) / vh;

    // Create the rectangle for the element bounds (physical pixel coordinates).
    irr::core::rect<irr::s32> rect(px, py, px + pw, py + ph);

    // Create the static text element. border=false, wordWrap=true, parent=root.
    irr::gui::IGUIStaticText* elem = m_guiEnv->addStaticText(
        wtext.c_str(), rect, /*border=*/false, /*wordWrap=*/true,
        /*parent=*/nullptr, /*id=*/-1, /*fillBackground=*/false);

    if (!elem) {
        return kInvalidUIElement;
    }

    // Phase 10c Glass City Colour Pass: default static text colour is near-white #EBF4F6.
    // Irrlicht's IGUIStaticText::setOverrideColor() sets the text colour for all states.
    // This establishes the baseline; callers may override for specific roles (amber numerics,
    // mid-blue sub-labels) by calling setOverrideColor() on the returned element directly.
    elem->setOverrideColor(irr::video::SColor(255, 235, 244, 246));

    UIElementHandle handle = m_nextHandle++;
    m_elementMap[handle] = ElementInfo{elem, UIRect{x, y, w, h}};
    return handle;
}

// ---------------------------------------------------------------------------
// 2. addButton
// Creates a real IGUIButton element via m_guiEnv.
// ---------------------------------------------------------------------------
UIElementHandle IrrlichtUIBackend::addButton(
    const std::string& label, int x, int y, int w, int h)
{
    irr::core::stringw wlabel(label.c_str());

    // Scale virtual coordinates (1920x1080 design space) to physical screen pixels.
    const int sw = getScreenWidth();
    const int sh = getScreenHeight();
    const int vw = getVirtualWidth();
    const int vh = getVirtualHeight();
    const int px = (x * sw) / vw;
    const int py = (y * sh) / vh;
    const int pw = (w * sw) / vw;
    const int ph = (h * sh) / vh;

    irr::core::rect<irr::s32> rect(px, py, px + pw, py + ph);

    // Irrlicht's addButton signature: rect, parent, id, text, tooltiptext.
    irr::gui::IGUIButton* elem = m_guiEnv->addButton(
        rect, /*parent=*/nullptr, /*id=*/-1, wlabel.c_str(),
        /*tooltiptext=*/nullptr);

    if (!elem) {
        return kInvalidUIElement;
    }

    UIElementHandle handle = m_nextHandle++;
    m_elementMap[handle] = ElementInfo{elem, UIRect{x, y, w, h}};
    return handle;
}

// ---------------------------------------------------------------------------
// 3. removeElement
// Looks up the handle in element/texture maps, removes and cleans up.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::removeElement(UIElementHandle handle)
{
    // Check if this is a GUI element.
    auto elemIt = m_elementMap.find(handle);
    if (elemIt != m_elementMap.end()) {
        irr::gui::IGUIElement* elem = elemIt->second.element;
        if (elem) {
            elem->remove();  // Detach from parent; Irrlicht manages the memory.
        }
        m_elementMap.erase(elemIt);
    }

    // Check if this is a raw GL texture handle.
    auto texIt = m_textureHandleMap.find(handle);
    if (texIt != m_textureHandleMap.end()) {
        if (!m_isHeadless) {
            GLuint texId = static_cast<GLuint>(texIt->second);
            glDeleteTextures(1, &texId);
        }
        m_textureHandleMap.erase(texIt);
    }

    // Clean up alpha tracking.
    m_alphaMap.erase(handle);

    // Clean up image element mapping.
    m_imageElementMap.erase(handle);
}

// ---------------------------------------------------------------------------
// 4. setElementText
// Looks up the element and sets its text via IGUIElement::setText().
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementText(UIElementHandle handle,
                                        const std::string& text)
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }
    irr::core::stringw wtext(text.c_str());
    it->second.element->setText(wtext.c_str());
}

// ---------------------------------------------------------------------------
// 5. setElementVisible
// Looks up the element and sets its visibility.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementVisible(UIElementHandle handle, bool visible)
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }
    it->second.element->setVisible(visible);
}

// ---------------------------------------------------------------------------
// 6. isElementVisible
// Returns the element's current visibility state.
// ---------------------------------------------------------------------------
bool IrrlichtUIBackend::isElementVisible(UIElementHandle handle) const
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return false;
    }
    return it->second.element->isVisible();
}

// ---------------------------------------------------------------------------
// 7. setElementEnabled
// Disabled = grayed-out, non-interactive (distinct from hidden).
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementEnabled(UIElementHandle handle, bool enabled)
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }
    it->second.element->setEnabled(enabled);
}

// ---------------------------------------------------------------------------
// 8. isElementEnabled
// Returns the element's current enabled/disabled state.
// ---------------------------------------------------------------------------
bool IrrlichtUIBackend::isElementEnabled(UIElementHandle handle) const
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return false;
    }
    return it->second.element->isEnabled();
}

// ---------------------------------------------------------------------------
// 9. setElementAlpha
//
// Virtual draw() spike result (Phase 8 mandatory deliverable):
//
// INSPECTED: build/vcpkg_installed/x64-linux/include/irrlicht/IGUIElement.h
//   IGUIElement::draw() is declared VIRTUAL at line 312:
//     virtual void draw() { ... }
//
// INSPECTED: build/vcpkg_installed/x64-linux/include/irrlicht/IGUIStaticText.h
//   IGUIStaticText inherits from IGUIElement. IGUIStaticText itself does NOT
//   re-declare draw() — the concrete implementation CGUIStaticText overrides
//   draw() in its .cpp file. Since IGUIElement::draw() IS virtual, the
//   override in CGUIStaticText is dispatched correctly via vtable.
//
// INSPECTED: build/vcpkg_installed/x64-linux/include/irrlicht/IGUIButton.h
//   IGUIButton inherits from IGUIElement. IGUIButton itself does NOT
//   re-declare draw() — the concrete implementation CGUIButton overrides
//   draw() in its .cpp file. Same virtual dispatch as above.
//
// CONCLUSION: IGUIElement::draw() IS virtual in the vendored Irrlicht build.
//   The subclassing approach (overriding draw() to apply stored alpha before
//   delegating to the base class draw) is viable for built-in element types.
//   Phase 8 stores the alpha value per handle; actual alpha rendering is
//   deferred to Phase 9 where the subclassing or overlay approach will be
//   applied at the panel draw level.
//
// SELECTED APPROACH: Store alpha per-handle in m_alphaMap. Phase 9 will
//   apply alpha at panel draw time using the subclass-override approach
//   (since draw() IS virtual). The draw2DRectangle overlay fallback is NOT
//   needed — the 18th method (drawAlphaOverlays) is NOT required.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementAlpha(UIElementHandle handle, float alpha)
{
    // Clamp alpha to valid range.
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    m_alphaMap[handle] = alpha;
}

// ---------------------------------------------------------------------------
// spriteRectForIndex — maps a hud_sprite_ids.h constant to the actual pixel
// rectangle in hud_sprites_ui.png.  Source positions from hud_sprites_ui_layout.json.
// Returns irr::core::rect<s32>(left, top, right, bottom).
// ---------------------------------------------------------------------------
static irr::core::rect<irr::s32> spriteRectForIndex(uint32_t id)
{
    // Toolbar icons — 64×64 px, 64px column spacing.
    // Active (IDs 0-4) at y=0; inactive (IDs 32-36) at y=64.
    if (id <= 4u) {
        const irr::s32 x = static_cast<irr::s32>(id) * 64;
        return irr::core::rect<irr::s32>(x, 0, x + 64, 64);
    }
    if (id >= 32u && id <= 36u) {
        const irr::s32 x = static_cast<irr::s32>(id - 32u) * 64;
        return irr::core::rect<irr::s32>(x, 64, x + 64, 128);
    }
    // All remaining IDs use the standard 64×64 grid — col = id % 32, row = id / 32.
    // The generator places every icon at (col * 64, row * 64) with no exceptions.
    // See architecture/asset-standards/2d-texture-standards.md "Sprite ID Encoding and
    // Row-Conflict Pitfall" for a history of past bugs caused by wrong special cases here.
    const irr::s32 col = static_cast<irr::s32>(id % 32u);
    const irr::s32 row = static_cast<irr::s32>(id / 32u);
    return irr::core::rect<irr::s32>(col * 64, row * 64, col * 64 + 64, row * 64 + 64);
}

// ---------------------------------------------------------------------------
// 10. setElementImage
//
// Assigns a sprite region from hud_sprites_ui.png to a button element using
// IGUIButton::setImage(texture, srcRect) with a pixel-accurate source rect
// derived from hud_sprites_ui_layout.json via spriteRectForIndex().
//
// The second parameter (spriteIndex) is a hud_sprite_ids.h constant.
// spriteRectForIndex() maps each constant to the correct pixel rect in the
// sprite sheet, matching the actual 56px column spacing of the toolbar icons
// and the exact positions of all other sprite categories.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementImage(UIElementHandle handle,
                                         UIElementHandle spriteIndex)
{
    // Look up the element — must be a button (IGUIButton).
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }

    // Cast to IGUIButton. Static text elements do not support setImage().
    // We use EGUI_ELEMENT_TYPE to avoid a dynamic_cast dependency.
    irr::gui::IGUIElement* elem = it->second.element;
    if (elem->getType() != irr::gui::EGUIET_BUTTON) {
        // Element is not a button — fall back to storing the mapping for
        // possible future raw-GL rendering of non-button image elements.
        m_imageElementMap[handle] = spriteIndex;
        return;
    }

    irr::gui::IGUIButton* btn = static_cast<irr::gui::IGUIButton*>(elem);

    if (!m_spriteTextureReady || !m_spriteTexture) {
        m_imageElementMap[handle] = spriteIndex;
        return;
    }

    // Look up the source rectangle for this sprite index.
    irr::core::rect<irr::s32> srcRect = spriteRectForIndex(spriteIndex);

    // Assign the texture region to the button (all visual states).
    btn->setImage(m_spriteTexture, srcRect);
    btn->setPressedImage(m_spriteTexture, srcRect);
    btn->setScaleImage(true);       // scale icon to fit button size (default is 1:1 pixel, which overflows)
    btn->setUseAlphaChannel(true);  // enable PNG alpha channel (default is false = no transparency)
    btn->setDrawBorder(false);      // hide Irrlicht skin background; icon provides its own background

    m_imageElementMap[handle] = spriteIndex;
}

// ---------------------------------------------------------------------------
// 11. getElementText
// Returns the element's current text as a std::string.
// ---------------------------------------------------------------------------
std::string IrrlichtUIBackend::getElementText(UIElementHandle handle) const
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return {};
    }

    const wchar_t* wtext = it->second.element->getText();
    if (!wtext) {
        return {};
    }

    // Convert wchar_t string to std::string via irr::core::stringc (ASCII/Latin-1).
    irr::core::stringc narrow(wtext);
    return std::string(narrow.c_str());
}

// ---------------------------------------------------------------------------
// 12. getElementRect
// Returns the element's virtual bounding rectangle (stored at creation time).
// Returning the stored virtual rect avoids the physical→virtual round-trip
// that produces incorrect coordinates after a window resize.
// ---------------------------------------------------------------------------
UIRect IrrlichtUIBackend::getElementRect(UIElementHandle handle) const
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return UIRect{};
    }
    return it->second.virtualRect;
}

// ---------------------------------------------------------------------------
// 13. getScreenWidth
// Queries the video driver unconditionally — hardcoded literals are prohibited
// except for the EDT_NULL guard path (divide-by-zero protection for UIScaler).
// ---------------------------------------------------------------------------
int IrrlichtUIBackend::getScreenWidth() const
{
    // EDT_NULL guard: getScreenSize() returns {0,0} for headless/EDT_NULL driver.
    // MUST NOT propagate 0 to UIScaler (would cause divide-by-zero). Return 1280.
    if (m_isHeadless) {
        return 1280;
    }
    return static_cast<int>(m_driver->getScreenSize().Width);
}

// ---------------------------------------------------------------------------
// 14. getScreenHeight — same EDT_NULL guard as getScreenWidth().
// ---------------------------------------------------------------------------
int IrrlichtUIBackend::getScreenHeight() const
{
    if (m_isHeadless) {
        return 720;
    }
    return static_cast<int>(m_driver->getScreenSize().Height);
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
// Raw glTexImage2D upload path for UI textures. Bypasses IVideoDriver::getTexture().
//
// The canonical upload sequence:
//   (1) glGenTextures(1, &texId)
//   (2) glBindTexture(GL_TEXTURE_2D, texId)
//   (3) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0) — BEFORE upload
//   (4) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
//   (5) optional anisotropy disable when extension present
//   (6) glTexImage2D(...) — upload pixel data
//
// GL_TEXTURE_MAX_LEVEL=0 MUST be set BEFORE glTexImage2D. Setting it after
// is ineffective — the driver may have already committed the mip chain.
// ---------------------------------------------------------------------------
UIElementHandle IrrlichtUIBackend::loadTexture(const std::string& path)
{
    // Headless guard: no GL context in EDT_NULL mode. Return invalid handle.
    if (m_isHeadless) {
        return kInvalidUIElement;
    }

    // Load the image file using Irrlicht's image loader to get CPU-side pixels.
    // createImageFromFile() returns an IImage* without creating a GPU texture.
    irr::video::IImage* img = m_driver->createImageFromFile(
        irr::io::path(path.c_str()));
    if (!img) {
        return kInvalidUIElement;
    }

    irr::u32 imgW = img->getDimension().Width;
    irr::u32 imgH = img->getDimension().Height;
    irr::video::ECOLOR_FORMAT fmt = img->getColorFormat();

    // Lock the image to get a pointer to pixel data.
    void* pixels = img->lock();
    if (!pixels) {
        img->drop();
        return kInvalidUIElement;
    }

    // Determine GL format/type based on Irrlicht's color format.
    // Irrlicht's ECF_A8R8G8B8 stores pixels as BGRA in memory.
    // ECF_R8G8B8 stores as BGR in memory (24-bit).
    GLenum glFormat = GL_BGRA;
    GLenum glType   = GL_UNSIGNED_BYTE;
    GLenum glInternalFormat = GL_RGBA8;

    if (fmt == irr::video::ECF_A8R8G8B8) {
        // Irrlicht ARGB = BGRA byte order. Upload as GL_BGRA.
        glFormat = GL_BGRA;
        glType   = GL_UNSIGNED_BYTE;
        glInternalFormat = GL_RGBA8;
    } else if (fmt == irr::video::ECF_R8G8B8) {
        // Irrlicht RGB = BGR byte order (24-bit, no alpha).
        glFormat = GL_BGR;
        glType   = GL_UNSIGNED_BYTE;
        glInternalFormat = GL_RGB8;
    } else {
        // Unsupported format — fall back.
        // Attempt BGRA regardless (best-effort).
        glFormat = GL_BGRA;
        glType   = GL_UNSIGNED_BYTE;
        glInternalFormat = GL_RGBA8;
    }

    // Save and restore GL_ACTIVE_TEXTURE to avoid corrupting Irrlicht's
    // internal texture unit tracking.
    GLint savedUnit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit);
    glActiveTexture(GL_TEXTURE0);

    // --- Canonical upload sequence ---
    GLuint texId = 0;
    glGenTextures(1, &texId);                                        // (1)
    glBindTexture(GL_TEXTURE_2D, texId);                             // (2)

    // (3) GL_TEXTURE_MAX_LEVEL = 0 — BEFORE glTexImage2D.
    // Setting this before upload prevents the driver from generating mipmaps.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    // (4) Linear filtering (no mip sampling — GL_LINEAR, not GL_LINEAR_MIPMAP_LINEAR).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // (5) Explicitly disable anisotropic filtering for UI textures, but only
    // when the extension is present (m_maxAnisotropy > 1.0f). On drivers
    // without GL_EXT_texture_filter_anisotropic, calling glTexParameterf with
    // GL_TEXTURE_MAX_ANISOTROPY_EXT produces GL_INVALID_ENUM.
    if (m_maxAnisotropy > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
    }

    // (6) Upload pixel data.
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(glInternalFormat),
                 static_cast<GLsizei>(imgW), static_cast<GLsizei>(imgH),
                 0, glFormat, glType, pixels);

    // Unbind texture and restore active unit.
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(static_cast<GLenum>(savedUnit));

    // Unlock and release the CPU-side image.
    img->unlock();
    img->drop();

    // Allocate a handle and track the GL texture.
    UIElementHandle handle = m_nextHandle++;
    m_textureHandleMap[handle] = static_cast<uint32_t>(texId);
    return handle;
}

// ---------------------------------------------------------------------------
// 18. setElementBackground
//
// Enables background fill on an IGUIStaticText element and sets its fill color.
// IGUIStaticText::setBackgroundColor() and setDrawBackground() are only defined
// on IGUIStaticText — NOT on the base IGUIElement. We use getType() to confirm
// the element is a static text before the static_cast; button elements are
// silently ignored (buttons draw their own background via the skin).
//
// Color channels r, g, b, a are each in [0, 255].
// Irrlicht SColor layout: (a, r, g, b) in that constructor order.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementBackground(UIElementHandle handle,
                                              int r, int g, int b, int a)
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }

    irr::gui::IGUIElement* elem = it->second.element;

    // Only IGUIStaticText supports setBackgroundColor() / setDrawBackground().
    if (elem->getType() != irr::gui::EGUIET_STATIC_TEXT) {
        return;
    }

    irr::gui::IGUIStaticText* text =
        static_cast<irr::gui::IGUIStaticText*>(elem);

    // Clamp channels to [0, 255].
    const irr::u32 ca = static_cast<irr::u32>(a < 0 ? 0 : a > 255 ? 255 : a);
    const irr::u32 cr = static_cast<irr::u32>(r < 0 ? 0 : r > 255 ? 255 : r);
    const irr::u32 cg = static_cast<irr::u32>(g < 0 ? 0 : g > 255 ? 255 : g);
    const irr::u32 cb = static_cast<irr::u32>(b < 0 ? 0 : b > 255 ? 255 : b);

    text->setBackgroundColor(irr::video::SColor(ca, cr, cg, cb));
    text->setDrawBackground(true);
}

// ---------------------------------------------------------------------------
// 19. setElementMonoFont
//
// Applies the monospace bitmap font (loaded from hud_mono_font.xml at
// construction) to an IGUIStaticText element via setOverrideFont().
//
// Guards:
//   - No-op when the handle is invalid or not in m_elementMap.
//   - No-op when the element is not EGUIET_STATIC_TEXT (buttons have their
//     own font machinery; do not call this on button handles).
//   - No-op when m_hudMonoFont is null (font file absent at construction or
//     running headless) — graceful fallback, no assert.
//
// MUST NOT be called on label text, button text, tooltips, or panel title
// elements. Those use the proportional hud_font.xml set as the env default.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementMonoFont(UIElementHandle handle)
{
    if (!m_hudMonoFont) {
        // Font absent (file missing at load time, or headless CI) — graceful no-op.
        return;
    }

    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }

    irr::gui::IGUIElement* elem = it->second.element;

    // Only IGUIStaticText supports setOverrideFont().
    if (elem->getType() != irr::gui::EGUIET_STATIC_TEXT) {
        return;
    }

    static_cast<irr::gui::IGUIStaticText*>(elem)->setOverrideFont(m_hudMonoFont);
}

// ---------------------------------------------------------------------------
// 20. setElementRect
// Repositions and resizes an existing element without destroying its handle.
// Updates the stored virtualRect so that handleViewportResize() continues to
// scale correctly, then immediately applies setRelativePosition() to reflect
// the new coordinates at the current physical screen resolution.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementRect(UIElementHandle handle,
                                        int x, int y, int w, int h)
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }

    // Update the stored virtual rect so future resize handling stays correct.
    it->second.virtualRect = UIRect{x, y, w, h};

    // Scale virtual coordinates to physical pixel coordinates.
    const int sw = getScreenWidth();
    const int sh = getScreenHeight();
    const int vw = getVirtualWidth();   // 1920
    const int vh = getVirtualHeight();  // 1080
    const int px = (x * sw) / vw;
    const int py = (y * sh) / vh;
    const int pw = (w * sw) / vw;
    const int ph = (h * sh) / vh;

    it->second.element->setRelativePosition(
        irr::core::rect<irr::s32>(px, py, px + pw, py + ph));
}

// ---------------------------------------------------------------------------
// 21. setElementTextColor
// Sets the override text colour on an IGUIStaticText element.
// Used by HUD numeric readouts (treasury, debt, population, date) to apply
// amber #F0B429 = SColor(255, 240, 180, 41) immediately after construction.
// No-op on button elements and invalid handles.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementTextColor(UIElementHandle handle, int r, int g, int b)
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return;
    }

    irr::gui::IGUIElement* elem = it->second.element;

    // Only IGUIStaticText supports setOverrideColor().
    if (elem->getType() != irr::gui::EGUIET_STATIC_TEXT) {
        return;
    }

    static_cast<irr::gui::IGUIStaticText*>(elem)->setOverrideColor(
        irr::video::SColor(255,
            static_cast<irr::u32>(r),
            static_cast<irr::u32>(g),
            static_cast<irr::u32>(b)));
}

// ---------------------------------------------------------------------------
// 22. fillColoredRect
// Transient filled-rectangle draw call. Scales virtual 1920x1080 coordinates
// to physical screen pixels and calls m_driver->draw2DRectangle().
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::fillColoredRect(int x, int y, int w, int h,
                                         int r, int g, int b, int a)
{
    if (!m_driver) return;

    const int sw = getScreenWidth();
    const int sh = getScreenHeight();
    const int vw = getVirtualWidth();
    const int vh = getVirtualHeight();
    const int px = (x * sw) / vw;
    const int py = (y * sh) / vh;
    const int pw = (w * sw) / vw;
    const int ph = (h * sh) / vh;

    m_driver->draw2DRectangle(
        irr::video::SColor(
            static_cast<irr::u32>(a),
            static_cast<irr::u32>(r),
            static_cast<irr::u32>(g),
            static_cast<irr::u32>(b)),
        irr::core::rect<irr::s32>(px, py, px + pw, py + ph));
}

// ---------------------------------------------------------------------------
// handleViewportResize
// Detects screen size changes and repositions every GUI element so that its
// physical pixel position matches its stored virtual coordinate at the new
// resolution.  Call once per frame from the main loop.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::handleViewportResize()
{
    const int sw = getScreenWidth();
    const int sh = getScreenHeight();

    if (sw == m_lastScreenW && sh == m_lastScreenH) {
        return;                      // No resize — nothing to do.
    }

    m_lastScreenW = sw;
    m_lastScreenH = sh;

    const int vw = getVirtualWidth();   // 1920
    const int vh = getVirtualHeight();  // 1080

    for (auto& [h, info] : m_elementMap) {
        if (!info.element) continue;
        const UIRect& vr = info.virtualRect;
        const int px = (vr.x * sw) / vw;
        const int py = (vr.y * sh) / vh;
        const int pw = (vr.w * sw) / vw;
        const int ph = (vr.h * sh) / vh;
        info.element->setRelativePosition(
            irr::core::rect<irr::s32>(px, py, px + pw, py + ph));
    }
}

// ---------------------------------------------------------------------------
// handleGuiHoverEvent
//
// Handles EGET_ELEMENT_HOVERED and EGET_ELEMENT_LEFT GUI events to swap
// button sprite cells for hover visual feedback.  Called from
// EventReceiver::OnEvent() for all EET_GUI_EVENT events.
//
// Hover rules (per architecture/ui-ux/input-arbitration.md §Hover State Switching):
//   EGET_ELEMENT_HOVERED: if NOT pressed, swap to kSpriteXxxHover cell.
//                         if already pressed (active), skip — active sprite persists.
//   EGET_ELEMENT_LEFT:    if pressed (active), restore kSpriteXxxActive cell.
//                         if not pressed, restore kSpriteXxxInactive cell.
//
// Returns false always — hover events must never be consumed so that Irrlicht
// can complete its own tooltip and focus handling.
//
// Implementation:
//   1. Reverse-scan m_elementMap to find the UIElementHandle for the event Caller.
//   2. Look up the currently-assigned sprite index in m_imageElementMap.
//   3. Compute the hover/active/inactive ID from the current sprite using the
//      known group offsets defined in hud_sprite_ids.h.
//   4. Call IGUIButton::setImage() with the target sprite cell.
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// Sprite group helpers — compute variant IDs from any ID in a group.
//
// Groups and their state-ID ranges (from hud_sprite_ids.h):
//   Toolbar: active 0-4, inactive 32-36, hover 37-41
//   Zone:    active 64-72, inactive 96-104, hover 105-113
//   Util:    active 128-131, inactive 160-163, hover 164-167
//
// For any given sprite ID, these helpers return the corresponding variant.
// Returns kSpriteInvalid (UINT32_MAX) if the ID is not in a known hover group.
// ---------------------------------------------------------------------------
static constexpr uint32_t kSpriteInvalidId = UINT32_MAX;

uint32_t getHoverId(uint32_t id) {
    // Toolbar active 0-4 → hover 37-41 (offset +37)
    if (id <= 4u)                     return id + 37u;
    // Toolbar inactive 32-36 → hover 37-41 (offset +5)
    if (id >= 32u && id <= 36u)       return id + 5u;
    // Toolbar hover 37-41 → already hover
    if (id >= 37u && id <= 41u)       return id;
    // Zone active 64-72 → hover 105-113 (offset +41)
    if (id >= 64u && id <= 72u)       return id + 41u;
    // Zone inactive 96-104 → hover 105-113 (offset +9)
    if (id >= 96u && id <= 104u)      return id + 9u;
    // Zone hover 105-113 → already hover
    if (id >= 105u && id <= 113u)     return id;
    // Util active 128-131 → hover 164-167 (offset +36)
    if (id >= 128u && id <= 131u)     return id + 36u;
    // Util inactive 160-163 → hover 164-167 (offset +4)
    if (id >= 160u && id <= 163u)     return id + 4u;
    // Util hover 164-167 → already hover
    if (id >= 164u && id <= 167u)     return id;
    return kSpriteInvalidId;
}

uint32_t getActiveId(uint32_t id) {
    // Toolbar active 0-4 → already active
    if (id <= 4u)                     return id;
    // Toolbar inactive 32-36 → active 0-4 (offset -32)
    if (id >= 32u && id <= 36u)       return id - 32u;
    // Toolbar hover 37-41 → active 0-4 (offset -37)
    if (id >= 37u && id <= 41u)       return id - 37u;
    // Zone active 64-72 → already active
    if (id >= 64u && id <= 72u)       return id;
    // Zone inactive 96-104 → active 64-72 (offset -32)
    if (id >= 96u && id <= 104u)      return id - 32u;
    // Zone hover 105-113 → active 64-72 (offset -41)
    if (id >= 105u && id <= 113u)     return id - 41u;
    // Util active 128-131 → already active
    if (id >= 128u && id <= 131u)     return id;
    // Util inactive 160-163 → active 128-131 (offset -32)
    if (id >= 160u && id <= 163u)     return id - 32u;
    // Util hover 164-167 → active 128-131 (offset -36)
    if (id >= 164u && id <= 167u)     return id - 36u;
    return kSpriteInvalidId;
}

uint32_t getInactiveId(uint32_t id) {
    // Toolbar active 0-4 → inactive 32-36 (offset +32)
    if (id <= 4u)                     return id + 32u;
    // Toolbar inactive 32-36 → already inactive
    if (id >= 32u && id <= 36u)       return id;
    // Toolbar hover 37-41 → inactive 32-36 (offset -5)
    if (id >= 37u && id <= 41u)       return id - 5u;
    // Zone active 64-72 → inactive 96-104 (offset +32)
    if (id >= 64u && id <= 72u)       return id + 32u;
    // Zone inactive 96-104 → already inactive
    if (id >= 96u && id <= 104u)      return id;
    // Zone hover 105-113 → inactive 96-104 (offset -9)
    if (id >= 105u && id <= 113u)     return id - 9u;
    // Util active 128-131 → inactive 160-163 (offset +32)
    if (id >= 128u && id <= 131u)     return id + 32u;
    // Util inactive 160-163 → already inactive
    if (id >= 160u && id <= 163u)     return id;
    // Util hover 164-167 → inactive 160-163 (offset -4)
    if (id >= 164u && id <= 167u)     return id - 4u;
    return kSpriteInvalidId;
}

} // anonymous namespace

bool IrrlichtUIBackend::handleGuiHoverEvent(const irr::SEvent& event)
{
    // Only handle GUI events — pass through everything else.
    if (event.EventType != irr::EET_GUI_EVENT) {
        return false;
    }

    const irr::gui::EGUI_EVENT_TYPE gevType = event.GUIEvent.EventType;
    if (gevType != irr::gui::EGET_ELEMENT_HOVERED &&
        gevType != irr::gui::EGET_ELEMENT_LEFT) {
        return false;
    }

    // Must have a caller element.
    irr::gui::IGUIElement* el = event.GUIEvent.Caller;
    if (!el) return false;

    // Only process IGUIButton elements.
    // Use getType() instead of dynamic_cast to avoid RTTI issues across shared-library
    // boundaries on some GPU/driver combinations (dynamic_cast on Irrlicht types can
    // crash if typeinfo symbols are not uniquely resolved).
    if (el->getType() != irr::gui::EGUIET_BUTTON) return false;
    auto* btn = static_cast<irr::gui::IGUIButton*>(el);

    // Sprite sheet must be loaded to do image swaps.
    if (!m_spriteTextureReady || !m_spriteTexture) return false;

    // Reverse-scan m_elementMap to find the UIElementHandle for this IGUIElement*.
    UIElementHandle foundHandle = kInvalidUIElement;
    for (const auto& [h, info] : m_elementMap) {
        if (info.element == el) {
            foundHandle = h;
            break;
        }
    }
    if (foundHandle == kInvalidUIElement) return false;

    // Look up the currently-assigned sprite index.
    auto imgIt = m_imageElementMap.find(foundHandle);
    if (imgIt == m_imageElementMap.end()) return false;
    const uint32_t currentSprite = imgIt->second;

    if (gevType == irr::gui::EGET_ELEMENT_HOVERED) {
        // Active (pressed) buttons must not have their image overridden by hover.
        if (btn->isPressed()) return false;

        uint32_t hoverId = getHoverId(currentSprite);
        if (hoverId == kSpriteInvalidId) return false;

        irr::core::rect<irr::s32> srcRect = spriteRectForIndex(hoverId);
        btn->setImage(m_spriteTexture, srcRect);
        btn->setScaleImage(true);
        btn->setUseAlphaChannel(true);
        btn->setDrawBorder(false);

    } else {  // EGET_ELEMENT_LEFT
        // Restore the registered base sprite (whatever setElementImage last set).
        // currentSprite is the value stored in m_imageElementMap — the sprite shown
        // before the hover began — which may be active OR inactive depending on
        // whether the button was selected when the hover started.
        // Do NOT map through getInactiveId(): that would always show the outline
        // variant even for buttons initialized with an active (filled icon) sprite.
        irr::core::rect<irr::s32> srcRect = spriteRectForIndex(currentSprite);
        btn->setImage(m_spriteTexture, srcRect);
        btn->setScaleImage(true);
        btn->setUseAlphaChannel(true);
        btn->setDrawBorder(false);
    }

    return false;  // Never consume hover events — Irrlicht must process them too.
}
