# Irrlicht Device Lifecycle

- A `RenderSystem` class owns the `IrrlichtDevice*` exclusively (RAII)
- The destructor calls `device->drop()`
- No raw `IrrlichtDevice*` stored outside `RenderSystem`
- `RenderSystem` exposes `getDriver()`, `getSceneManager()`, `isRunning()` etc.

**Render Loop Call Order (mandatory per-frame sequence)**:

```cpp
// Required order in RenderSystem per frame:
driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
sceneManager->drawAll();        // 3D scene (terrain, buildings, vehicles, sky)
uiManager->draw();              // 2D HUD: explicit per-panel Z-order draw (m_gui->drawAll() NOT called — see ui-manager.md)
driver->endScene();
```

`UIManager::draw()` is called between `sceneManager->drawAll()` and `endScene()` in `RenderSystem`. Per `architecture/ui-ux/ui-manager.md` line 157, `UIManager::draw()` issues explicit per-panel draw calls in Z-order via `IUIBackend` — `m_gui->drawAll()` is NOT called by `RenderSystem` because it would bypass the explicit layering required for the background scrim and modal overlay. `UIManager::draw()` must be called before `endScene()` — calling it after `endScene()` produces no output. The mandatory per-frame sequence is `sceneManager->drawAll()` → `UIManager::draw()` → `driver->endScene()`. **Note**: Irrlicht uses `driver->endScene()` — `IVideoDriver` has no `endFrame()` method, so calling `driver->endFrame()` (or `endFrame()` on any other Irrlicht object such as `ISceneManager`) is a compile error. The prohibition is on calling `endFrame()` directly on any Irrlicht object. The `IRenderer` abstraction interface (in `src/interfaces/`) may expose a method named `endFrame()` as part of its rendering facade. This is acceptable — `IrrlichtRenderer::endFrame()` must internally call `driver->endScene()` (not `endFrame()`). The concrete `IrrlichtRenderer` implementation translates the facade call to the correct Irrlicht API. This call order is immutable and must not be changed by any subsystem.

## CameraController — Preventing Animator Conflicts

See [`scene-graph-ownership.md` — CameraController section](scene-graph-ownership.md) for the full specification: `addCameraSceneNode()` usage, the grab/drop-guarded animator removal loop, the "Why grab/drop is required" rationale, per-frame update ordering, and `IEventReceiver` design.

### Video Driver

- **Always use `EDT_OPENGL`** on both Linux and Windows
- Enforce in `RenderSystem` constructor; log and abort if OpenGL is unavailable

### Render Quality Parameters (minimum required config)

```cpp
irr::SIrrlichtCreationParameters params;
params.DriverType  = irr::video::EDT_OPENGL;
params.WindowSize  = irr::core::dimension2d<irr::u32>(1280, 720);
params.Bits        = 32;
params.ZBufferBits = 24;   // required for correct depth sorting in dense city
params.Stencil     = true; // required for shadow techniques
params.AntiAlias   = 4;    // 4× MSAA minimum for "realistic graphics" goal
params.Vsync       = false; // user-configurable option
```

## GL Capability Query Initialization

The following initialization sequence MUST occur immediately after `createDevice()` and before any `glewIsExtensionSupported()` or `glGetIntegerv()` call:

1. **EDT_NULL pre-check**: Short-circuit the entire GL capability block if `IVideoDriver::getDriverType() == EDT_NULL`. No GL calls are valid in headless mode.

2. **glewInit()** must be called first:

   ```cpp
   GLenum glewResult = glewInit();
   if (glewResult != GLEW_OK) {
       // Log failure; fall back to safe defaults.
       // glGetIntegerv(GL_MAX_TEXTURE_SIZE) may still work on some drivers.
       // glewIsExtensionSupported() must NOT be called after a failed glewInit().
   }
   ```

   Calling `glewIsExtensionSupported()` or any GLEW function pointer (e.g. `glCompressedTexImage2D`) before `glewInit()` causes a null function pointer crash.

3. **GL_MAX_TEXTURE_SIZE query**: Query with `glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize)` immediately after glewInit (even if glewInit failed — this call does not depend on GLEW).

4. **Extension queries**: Use `glewIsExtensionSupported("GL_EXT_texture_sRGB")` etc. only after a successful `glewInit()`.

**GLEW availability spike**: Phase 1 must verify whether the vendored Irrlicht build exposes GLEW symbols. If GLEW is unavailable (Irrlicht compiled without GLEW), all extension checks must use `glGetString(GL_EXTENSIONS)` string matching or `IVideoDriver::queryFeature()` instead. This spike must complete before any `glewIsExtensionSupported()` code is written. The spike result must be documented in `architecture/graphics-architecture/scene-graph-ownership.md` under the Phase 1 spikes section.
