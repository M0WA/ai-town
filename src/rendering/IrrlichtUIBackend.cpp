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

#include <GL/glew.h>                          // MUST come before irrlicht.h
#include <irrlicht.h>                          // full Irrlicht types

#include <cassert>
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
        std::string vertSrc = readFileToString("assets/shaders/ui_quad.vert");
        std::string fragSrc = readFileToString("assets/shaders/ui_quad.frag");

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
    m_spriteTexture = m_driver->getTexture("assets/textures/ui/hud_sprites_ui.png");
    m_spriteTextureReady = (m_spriteTexture != nullptr);

    // --- Load custom bitmap font for the GUI skin ---
    //
    // The Irrlicht built-in font is 8×13 px — unreadably small at 1280×720 and
    // above. All addStaticText() and addButton() calls inherit the skin default
    // font, so setting it once here applies to every GUI element in the game.
    //
    // Font format: Irrlicht XML bitmap font (.xml + companion .png texture).
    // Expected path: assets/fonts/hud_font.xml
    // The companion texture is automatically found by getFont() next to the xml.
    //
    // Graceful degradation: if the font file is absent (e.g. during development
    // before the font asset is authored), the Irrlicht built-in 8px font is
    // retained and a warning is printed to stderr.  This is NOT fatal — the game
    // runs, but text will be tiny until the font asset is placed.
    //
    // Font authoring guide: use Irrlicht's FontTool (tools/GUIEditor/FontTool)
    // or any Irrlicht-compatible bitmap font generator to export a 16 px or 18 px
    // bitmap font to assets/fonts/hud_font.xml.  The recommended size is 18 px
    // virtual (maps to ≥12 px physical at 1280×720) per
    // architecture/ui-ux/resolution-ui-scaling.md — minimum body font size 14 px
    // virtual, prefer 16+ px virtual for interactive labels.
    {
        irr::gui::IGUIFont* customFont =
            m_guiEnv->getFont("assets/fonts/hud_font.xml");
        if (customFont) {
            // Apply as the skin's default font — affects all GUI elements globally.
            irr::gui::IGUISkin* skin = m_guiEnv->getSkin();
            if (skin) {
                skin->setFont(customFont);
            }
        } else {
            // Font file not found — emit a visible warning so developers notice.
            // The built-in 8px font will be used; text will be unreadably small.
            fprintf(stderr,
                "[IrrlichtUIBackend] WARNING: assets/fonts/hud_font.xml not found — "
                "falling back to Irrlicht built-in 8px font. "
                "HUD text will be unreadably small at all resolutions. "
                "Create a bitmap font at assets/fonts/hud_font.xml using Irrlicht's "
                "FontTool or any compatible bitmap font generator (recommended: 18px).\n");
        }
    }

    // --- Load monospace bitmap font for numeric HUD elements ---
    //
    // hud_mono_font.xml is a monospace face applied selectively to numeric
    // IGUIStaticText elements (treasury balance, population count, tax rate
    // fields, monthly revenue/expense, density unlock progress) via
    // element->setOverrideFont(getMonoFont()) after addStaticText().
    //
    // Rationale: a monospace face applied to numeric readouts prevents layout
    // jitter as digits change (e.g. "1,000" vs "9,000"), while keeping
    // proportional hud_font.xml for labels and panel titles (better legibility
    // at small sizes in compact panels such as Query/Inspector and Tax Rate).
    //
    // Graceful degradation: if the file is absent the environment default
    // (hud_font.xml, or the built-in 8px font if that also failed) is retained
    // for numeric elements; a warning is emitted but the game continues.
    // Callers of getMonoFont() must null-check before calling setOverrideFont().
    {
        irr::gui::IGUIFont* monoFont =
            m_guiEnv->getFont("assets/fonts/hud_mono_font.xml");
        if (monoFont) {
            m_monoFont = monoFont;
            // IGUIEnvironment::getFont() caches fonts internally — the returned
            // pointer is owned by the environment; do NOT call grab() or drop().
        } else {
            fprintf(stderr,
                "[IrrlichtUIBackend] WARNING: assets/fonts/hud_mono_font.xml not found — "
                "falling back to default font for numeric HUD elements. "
                "Treasury balance, population count, and tax rate fields will use "
                "the proportional font until hud_mono_font.xml is placed at "
                "assets/fonts/hud_mono_font.xml.\n");
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

    UIElementHandle handle = m_nextHandle++;
    m_elementMap[handle] = ElementInfo{elem, Rect{x, y, w, h}};
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
    m_elementMap[handle] = ElementInfo{elem, Rect{x, y, w, h}};
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
    // Toolbar icons — 64×64 px, 64px column spacing (verified from actual PNG pixel layout).
    // JSON says 48×48/56px but actual icon cells are 64×64.
    // Active (IDs 0-4) at y=0; inactive (IDs 32-36) at y=64.
    if (id <= 4u) {
        const irr::s32 x = static_cast<irr::s32>(id) * 64;
        return irr::core::rect<irr::s32>(x, 0, x + 64, 64);
    }
    if (id >= 32u && id <= 36u) {
        const irr::s32 x = static_cast<irr::s32>(id - 32u) * 64;
        return irr::core::rect<irr::s32>(x, 64, x + 64, 128);
    }
    // Zone sub-panel sprite IDs (64-104) fall through to the default 64×64 grid below.
    // Default grid maps them correctly to y=128 (active, row 2) and y=192 (inactive, row 3).
    // Utilities patterns — 64×64.
    // Actual PNG layout (verified against hud_sprites_ui.png):
    //   y=256: active icons   — Power(x=0), Water(x=72), Fire(x=144), Police(x=216)
    //   y=320: inactive icons — Power(x=0), Water(x=72), Fire(x=144), Police(x=216)
    // IDs: Power=0, Water=1, Fire=2, Police=3 within each band (128-131 active, 160-163 inactive).
    if ((id >= 128u && id <= 131u) || (id >= 160u && id <= 163u)) {
        const uint32_t base = (id <= 131u) ? 128u : 160u;
        const int t = static_cast<int>(id - base);
        const irr::s32 yTop = (id <= 131u) ? 256 : 320;  // active row vs inactive row
        const irr::s32 xOffsets[4] = {0, 72, 144, 216};  // Power, Water, Fire, Police
        const irr::s32 x = xOffsets[t];
        return irr::core::rect<irr::s32>(x, yTop, x + 64, yTop + 64);
    }
    // Notification bell — JSON icon_bell: (56, 64, 48, 48)
    if (id == 320u) {
        return irr::core::rect<irr::s32>(56, 64, 104, 112);
    }
    // Clock icon — JSON icon_clock_grace: (64, 384, 16, 16)
    if (id == 321u) {
        return irr::core::rect<irr::s32>(64, 384, 80, 400);
    }
    // Unsaved dot — JSON dot_unsaved_changes: (40, 384, 16, 16)
    if (id == 322u) {
        return irr::core::rect<irr::s32>(40, 384, 56, 400);
    }
    // Undo icon — JSON icon_undo: (0, 64, 48, 48)
    if (id == 323u) {
        return irr::core::rect<irr::s32>(0, 64, 48, 112);
    }
    // Default: return a 64×64 cell using uniform grid as last-resort fallback.
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
Rect IrrlichtUIBackend::getElementRect(UIElementHandle handle) const
{
    auto it = m_elementMap.find(handle);
    if (it == m_elementMap.end() || !it->second.element) {
        return Rect{};
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
//   - No-op when m_monoFont is null (font file absent at construction or
//     running headless) — graceful fallback, no assert.
//
// MUST NOT be called on label text, button text, tooltips, or panel title
// elements. Those use the proportional hud_font.xml set as the env default.
// ---------------------------------------------------------------------------
void IrrlichtUIBackend::setElementMonoFont(UIElementHandle handle)
{
    if (!m_monoFont) {
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

    static_cast<irr::gui::IGUIStaticText*>(elem)->setOverrideFont(m_monoFont);
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
        const Rect& vr = info.virtualRect;
        const int px = (vr.x * sw) / vw;
        const int py = (vr.y * sh) / vh;
        const int pw = (vr.w * sw) / vw;
        const int ph = (vr.h * sh) / vh;
        info.element->setRelativePosition(
            irr::core::rect<irr::s32>(px, py, px + pw, py + ph));
    }
}
