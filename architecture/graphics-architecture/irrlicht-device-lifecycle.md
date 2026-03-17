# Irrlicht Device Lifecycle

- A `RenderSystem` class owns the `IrrlichtDevice*` exclusively (RAII)
- The destructor calls `device->drop()`
- No raw `IrrlichtDevice*` stored outside `RenderSystem`
- `RenderSystem` exposes `getDriver()`, `getSceneManager()`, `isRunning()` etc.

**Render Loop Call Order (mandatory per-frame sequence)**:

```cpp
// Required order in RenderSystem per frame:
audioSystem->syncListenerToCamera(cam);  // commit camera position to OpenAL listener (Phase 7); cam is the current CameraState
audioSystem->update(realDeltaSeconds);   // process audio command queue with updated listener (Phase 7 — see src/rendering/render_system.h comment; ref: implementation/phase-7.md line 31)
uiManager->update(realDeltaSeconds);    // advance UI timer state (notification auto-dismiss, HUD undo-countdown) — MUST be called before beginScene(); see ui-manager.md §Frame Loop Integration
terrainSystem->update(realDeltaSeconds); // process at most 2 terrain LOD rebuilds per frame (Phase 5); see procedural-terrain.md
driver->beginScene(true, true, irr::video::SColor(255, 100, 149, 237));  // sky-blue clear color (cornflower blue) — provides visual feedback that the 3D viewport is active; pure black (0,0,0) is indistinguishable from "nothing rendered"
sceneManager->drawAll();        // 3D scene (terrain, buildings, vehicles, sky)
// (Phase 9b) hover tile highlight — on top of 3D scene, below UI; omit if mesh absent or hidden:
if (m_hoveredTileMesh && m_hoverVisible) driver->drawMeshBuffer(m_hoveredTileMesh->getMeshBuffer(0));
uiManager->draw();              // 2D HUD: per-panel Z-order state update (visibility, text, alpha)
guiEnvironment->drawAll();      // Render all visible GUI elements (buttons, labels, etc.)
driver->endScene();
```

`IrrlichtRenderer::drawScene()` implements a rendering sequence between `beginScene()` and `endScene()`:

1. `sceneManager->drawAll()` — renders the 3D scene (terrain, buildings, vehicles, sky).
2. (Phase 9b) Hover tile highlight — if `m_hoveredTileMesh` is non-null AND `m_hoverVisible`
   is `true`, call `driver->drawMeshBuffer(m_hoveredTileMesh->getMeshBuffer(0))` to render the
   hover highlight quad on top of the 3D scene and below all UI elements.

   **Hover highlight mesh lifecycle**: `IrrlichtRenderer` owns two members for the hover
   highlight:

   - `SMesh* m_hoveredTileMesh` — allocated once in the `IrrlichtRenderer` constructor,
     **never nulled during gameplay**, and never dropped until the destructor.
   - `bool m_hoverVisible{false}` — set to `true` when a valid tile is hovered; set to
     `false` on a clear request (`tileX == -1`).

   `setTileHoverHighlight(tileX, tileZ, colour)` behaves as follows:

   - **Clear request (`tileX == -1`)**: sets `m_hoverVisible = false`. Does NOT touch
     `m_hoveredTileMesh` — the pointer remains valid and the buffer is retained for reuse.
   - **Normal call (valid tile)**: updates vertex positions and colours in the existing buffer,
     calls `recalculateBoundingBox()` on the buffer, then sets `m_hoverVisible = true`.

   `m_hoveredTileMesh` is dropped **only** in the `IrrlichtRenderer` destructor via `->drop()`.
   Setting the pointer to `nullptr` on clear is a bug — it loses the only reference to the
   allocated buffer, causing a memory leak.

3. `uiManager->draw()` — calls each panel's `draw()` method in explicit Z-order (slots 1–10 per `ui-manager.md`). Each panel's `draw()` updates element state (visibility, text, alpha) but does NOT render pixels.
4. `guiEnvironment->drawAll()` — renders all visible `IGUIElement` nodes. Because step 3 has already set the correct visibility on every element (non-active panels hide theirs), only the intended elements are painted.

The Z-order concern (scrim must cover panels; modal must be topmost) is handled by visibility management in step 3 — panels that should be behind have their elements hidden before `drawAll()` paints. `UIManager::draw()` must be called before `guiEnvironment->drawAll()` — calling it after would render stale element state.

**Loading screen exception**: Loading screen render loops may omit `guiEnvironment->drawAll()`. The loading screen UI (progress bar, status text) is rendered by `UIManager::draw()` using direct draw primitives (not Irrlicht `IGUIElement` nodes), so there are no GUI elements for `guiEnvironment->drawAll()` to paint. Calling `guiEnvironment->drawAll()` during the loading screen is harmless but unnecessary; omitting it is intentional and correct. The gameplay render loop (post-load) MUST include `guiEnvironment->drawAll()` as normal.

The mandatory 11-step per-frame sequence is: `audioSystem->syncListenerToCamera(cam)` → `audioSystem->update(realDeltaSeconds)` → `UIManager::update(realDeltaSeconds)` → `terrainSystem->update(realDeltaSeconds)` → `renderer->update(realDeltaSeconds)` (cloud UV scroll) → `driver->beginScene()` → `sceneManager->drawAll()` → hover tile highlight `drawMeshBuffer()` if `m_hoveredTileMesh && m_hoverVisible` (Phase 9b) → `UIManager::draw()` → `guiEnvironment->drawAll()` → `driver->endScene()` → **60 FPS frame cap sleep**. The two audio calls must come before `beginScene()` so that the listener position and audio command queue are fully updated before rendering begins. `terrainSystem->update()` is also a pre-render step — it processes queued LOD rebuilds before the scene is drawn. The Irrlicht-internal ordering (`beginScene` → `drawAll` → `draw` → `guiEnv->drawAll` → `endScene`) is immutable; the audio and terrain setup calls are pre-steps that must not be moved inside the Irrlicht render block. **Note**: Irrlicht uses `driver->endScene()` — `IVideoDriver` has no `endFrame()` method, so calling `driver->endFrame()` (or `endFrame()` on any other Irrlicht object such as `ISceneManager`) is a compile error. The prohibition is on calling `endFrame()` directly on any Irrlicht object. The `IRenderer` abstraction interface (in `src/interfaces/`) may expose a method named `endFrame()` as part of its rendering facade. This is acceptable — `IrrlichtRenderer::endFrame()` must internally call `driver->endScene()` (not `endFrame()`). The concrete `IrrlichtRenderer` implementation translates the facade call to the correct Irrlicht API.

## Frame Rate Cap

The game loop targets **60 FPS** by sleeping out the remainder of each 16.67 ms budget after `driver->endScene()`. Without this cap, the loop pins a CPU core at 100% on software renderers (llvmpipe), causing thermal throttling and irregular frame-time spikes. The sleep uses `std::this_thread::sleep_for` and is skipped when the frame already took ≥ 16.67 ms (i.e. the cap never forces frames to be slower than natural rendering allows).

```cpp
static constexpr double kTargetFrameSeconds = 1.0 / 60.0;
double elapsed  = wallClock.nowSeconds() - frameStartTime;
double remaining = kTargetFrameSeconds - elapsed;
if (remaining > 0.001)
    std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
```

**V1 note on llvmpipe**: `driver->endScene()` is where llvmpipe performs its software rasterisation flush — measured at ~81 ms per frame on a devcontainer CPU (12 FPS potential). All other per-frame steps combined cost < 0.5 ms. On a real GPU `endScene()` is a command-queue submit and returns in < 1 ms. The cap is necessary for the devcontainer development environment; it is neutral on hardware with a real GPU (those frames complete in < 1 ms and the sleep absorbs the rest of the 16.67 ms budget).

## `--frames N` Profiling Argument

`main()` accepts `--frames N`: run exactly N frames, print a per-step timing report to stderr, then exit cleanly. Intended for profiling and regression benchmarks.

```bash
./build/aitown --frames 300
```

Output (example on llvmpipe):

```text
=== FRAME TIMING REPORT (300 frames) ===
  simulation tick :     1.38 µs/frame
  camera update   :     6.23 µs/frame
  terrain update  :     1.94 µs/frame
  UI update       :     3.74 µs/frame
  audio update    :     3.85 µs/frame
  render (3D+GUI) : 82911.89 µs/frame
    ├ renderer.update  :     1.91 µs/frame
    ├ beginScene       :    70.03 µs/frame
    ├ drawScene        :  1964.67 µs/frame
    └ endScene (flush) : 80874.46 µs/frame
  TOTAL measured  : 82932.00 µs/frame  (12.1 FPS potential)
==========================================
```

`drawScene` is further broken down by `IrrlichtRenderer::drawScene()` which prints a `[drawScene]` line every 300 frames showing the split between `sceneManager->drawAll()` (3D), hover/preview mesh draws, `UIManager::draw()` (state update), and `guiEnvironment->drawAll()` (GUI pixel fill).

**Key finding (llvmpipe)**: `endScene()` accounts for 97.5% of frame time — all software rasterisation is deferred to the flush. The 3D scene and GUI together cost only ~2 ms. There is no worthwhile optimisation target in the game code itself on a software renderer; performance is determined entirely by the GPU/CPU.

## IrrlichtRenderer Late-Binding Pattern

`IrrlichtRenderer` is constructed with `nullptr` for its `UIManager*` parameter. After `UIManager` is constructed (which requires `IrrlichtRenderer` to already exist for the `CitySimulation` dependency chain), `main.cpp` calls `renderer.setUIManager(&uiManager)` to wire the pointer. This breaks the circular construction dependency: `UIManager` needs `CitySimulation` and `IAudioSystem` → `CitySimulation` needs `IRenderer` → `IrrlichtRenderer` needs `UIManager`. The `setUIManager()` setter is the only late-bound pointer on `IrrlichtRenderer`; all other dependencies are injected at construction.

The construction sequence in `main.cpp`:

```text
1.  RenderSystem (owns IrrlichtDevice)
2.  IrrlichtUIBackend (needs device)
    CRITICAL GL STATE RULE: IrrlichtUIBackend's constructor creates a VAO/VBO
    for UI quad rendering. After closing the VAO scope (glBindVertexArray(0)),
    it MUST also call glBindBuffer(GL_ARRAY_BUFFER, 0). GL_ARRAY_BUFFER is
    global state (NOT per-VAO); leaving it bound causes Irrlicht's COpenGLDriver
    to reinterpret client-side vertex array pointers as VBO offsets, silently
    rendering zero geometry for ALL scene nodes.
3.  UIScaler (needs uiBackend screen dimensions)
4.  Camera scene node + CameraController
5.  IrrlichtRenderer(device, /*uiManager=*/nullptr)
6.  WallClock, AudioSystem, TerrainSystem, CitySimulation
7.  TerrainSystem::generate() + buildAllChunks()   // terrain generation before UI
8.  CameraController::setTarget(centerX, centerZ)  // center camera over terrain
    // Phase 9b terrain-renderer wiring (steps 9a–9d must come before UIManager construction
    // so that IrrlichtRenderer has valid terrain pointers before any event can fire):
9a. renderer.setTerrainQuery(&terrainSystem)       // Phase 9b — ITerrainQuery* for pickTerrainTile
9b. renderer.setCellSize(terrainSystem.getCellSize())   // Phase 9b — tile width in metres
9c. renderer.setRendererMapDimensions(terrainSystem.getMapTilesX(),
                                      terrainSystem.getMapTilesZ())
    // Phase 9b — DDA bounds; see procedural-terrain.md "pickTerrainTile DDA Algorithm"
10. UIManager(uiBackend, audioSystem, citySimulation, wallClock)
11. renderer.setUIManager(&uiManager)              // late binding (unchanged)
    // Phase 9b UIManager-renderer wiring (after UIManager is constructed):
12. uiManager.setRenderer(&renderer)              // Phase 9b — for pickTerrainTile calls
13. uiManager.setTerrainQuery(&terrainSystem)     // Phase 9b — earthworks cost computation
14. uiManager.setMapDimensions(terrainSystem.getMapTilesX(),
                                terrainSystem.getMapTilesZ())
    // Phase 9b — zone overlay key (tileZ * mapTilesX + tileX)
15. EventReceiver(uiScaler, uiManager, cameraController)
16. device->setEventReceiver(&eventReceiver)
```

**Phase 9b wiring notes**:

- Steps 9a–9c are on `IrrlichtRenderer` directly (not `IRenderer*` interface) because
  `setTerrainQuery`, `setCellSize`, and `setRendererMapDimensions` are one-time
  initialization setters, not general renderer capabilities. Use the concrete
  `IrrlichtRenderer&` reference to call them.
- Steps 12–14 are on `UIManager` directly (also not interface methods). Same rationale:
  one-time post-construction wiring.
- `setRendererMapDimensions` stores `m_mapTilesX` and `m_mapTilesZ` on `IrrlichtRenderer`
  (distinct from `UIManager`'s same-named members). These are required by the DDA bounds
  check in `pickTerrainTile()`. Without step 9c, `pickTerrainTile()` always returns `false`
  (bounds check exits on the first step with default 0-valued dimensions).
- All Phase 9b wiring steps must complete before the main game loop starts. The ordering
  constraint is: terrain generation (step 7) → steps 9a–9c → UIManager construction
  (step 10) → steps 12–14 → event receiver (step 15). Steps 9a–9c must precede step 10
  to ensure that any hot-path event immediately after `setEventReceiver` in step 16 finds
  `IrrlichtRenderer` fully wired.

## IrrlichtRenderer and UIManager — Header Dependency Rule

`IrrlichtRenderer` calls `uiManager->draw()` inside `drawScene()` (see the render loop sequence above). This runtime coupling must NOT create a compile-time header dependency from `IrrlichtRenderer.h` to `UIManager.h`. The following rules are mandatory:

- `IrrlichtRenderer.h` must forward-declare `UIManager` with `class UIManager;` — it must NOT `#include "UIManager.h"` in the header file.
- `IrrlichtRenderer.cpp` includes `UIManager.h` in the implementation file (`.cpp`) only — never in the header.
- `UIManager.h` must NOT include any Irrlicht headers. `UIManager.h` depends only on `IUIBackend.h` in `src/ui/`. If `UIManager.h` were to include an Irrlicht header, and `IrrlichtRenderer.h` were to include `UIManager.h`, the result would be a circular include chain: Irrlicht headers pulled into any translation unit that includes `UIManager.h`, and `UIManager.h` pulled into any translation unit that includes `IrrlichtRenderer.h`.
- The coupling is one-directional at runtime: `IrrlichtRenderer::drawScene()` calls `uiManager->draw()`. At the header level this is expressed as a forward declaration only. The full `#include "UIManager.h"` is deferred to `IrrlichtRenderer.cpp`.

**Violation pattern to avoid**:

```cpp
// IrrlichtRenderer.h — WRONG: pulls Irrlicht headers into UIManager transitively
#include "UIManager.h"   // DO NOT DO THIS

// IrrlichtRenderer.h — CORRECT: forward declaration only
class UIManager;         // forward declare; full type resolved in .cpp only
```

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

> **CRITICAL — Safe operations between `createDevice()` and `glewInit()`**
>
> After `createDevice()` returns non-null, the OpenGL context is active on the
> calling thread but GLEW's function-pointer table has NOT yet been populated.
> Therefore, NO GLEW-dependent call is safe before `glewInit()`. This includes —
> but is not limited to — `glewIsSupported()`, `glewIsExtensionSupported()`, and
> any raw GL extension entrypoint resolved through GLEW (e.g.
> `glCompressedTexImage2D`). Calling any of these before `glewInit()` dereferences
> a null function pointer and will crash immediately.
>
> Call `glewInit()` IMMEDIATELY as the first action after `createDevice()` returns,
> before any OpenGL extension use.
>
> The ONLY operations that are safe before `glewInit()` are Irrlicht API calls
> that do not invoke raw GL functions internally — for example, reading
> `IrrlichtDevice` properties (window size, driver type) or querying
> `IVideoDriver::getDriverType()`. Any direct GL extension call before
> `glewInit()` will crash with a null function pointer.

The following initialization sequence MUST occur immediately after `createDevice()` and before any `glewIsExtensionSupported()` or `glGetIntegerv()` call:

1. **EDT_NULL pre-check**: Short-circuit the entire GL capability block if `IVideoDriver::getDriverType() == EDT_NULL`. No GL calls are valid in headless mode.

2. **glewInit()** must be called first:

   ```cpp
   // Required on GLVND Linux — prevents GLEW_ERROR_NO_GL_VERSION when Mesa provides
   // OpenGL via libGL.so.1 dispatch layer.
   glewExperimental = GL_TRUE;
   GLenum glewResult = glewInit();
   if (glewResult == GLEW_ERROR_NO_GL_VERSION) {
       // NON-FATAL: GLEW could not determine the OpenGL version string, but the
       // GL context and function pointers may still be valid.  Log a WARNING and
       // continue — do NOT abort and do NOT fall back to EDT_NULL.
       LOG_WARNING("glewInit() returned GLEW_ERROR_NO_GL_VERSION; "
                   "GL version string unavailable but continuing.");
   } else if (glewResult != GLEW_OK) {
       // FATAL: GLEW function-pointer table was not populated.
       // Debug builds: abort immediately (assert/terminate) so the failure is
       //               caught during development.
       // Release builds: fall back to EDT_NULL driver and show a user-facing
       //                 error notification ("OpenGL initialisation failed").
       //                 Do NOT call any GL extension entrypoints after this
       //                 point — they will crash with null function pointers.
       LOG_ERROR("glewInit() failed: %s", glewGetErrorString(glewResult));
       AITOWN_ASSERT_MSG(false, "Fatal GLEW initialisation failure");

       // **MANDATORY RELEASE FALLBACK SEQUENCE (must execute in this exact order):**
       //
       // Step 1. Drop the original OpenGL device.
       //   Closes the window and destroys the GL context.
       //   Omitting this step leaves the original OpenGL window open alongside the new
       //   EDT_NULL device, resulting in two live devices simultaneously.
       //   m_device->drop() triggers Irrlicht's internal cleanup and decrements the refcount.
       m_device->drop();
       // Step 2. Null the pointer immediately.
       //   After drop(), the pointer is invalid — null it to prevent dangling access.
       m_device = nullptr;
       // Step 3. Re-create with EDT_NULL (no window, no GL context).
       m_device = irr::createDevice(irr::video::EDT_NULL,
                                    irr::core::dimension2d<irr::u32>(1280, 720));
       // Step 4. Set safe defaults — no GL context is current after EDT_NULL creation.
       //   Do NOT call glGetIntegerv or any other GL function here.
       //   These assignments are MANDATORY and must execute unconditionally in the
       //   RELEASE fallback path; they are NOT optional comments.
       m_maxTextureSize       = 2048;  // conservative fallback — no glGetIntegerv
       m_srgbTextureSupported = false;
       m_maxAnisotropy        = 1.0f;
       // Show a user-facing error notification ("OpenGL initialisation failed") and
       // continue headlessly.
   }
   ```

   Calling `glewIsExtensionSupported()` or any GLEW function pointer (e.g. `glCompressedTexImage2D`) before `glewInit()` causes a null function pointer crash.

   **Two-tier distinction rationale**: `GLEW_ERROR_NO_GL_VERSION` specifically indicates GLEW cannot determine the GL version string — function pointers may still be loaded. Other errors (e.g., `GLEW_ERROR_NO_GLX_DISPLAY`) indicate the function-pointer table was not populated. The default GL 1.0 entrypoint `glGetIntegerv(GL_MAX_TEXTURE_SIZE, ...)` does not depend on the GLEW function-pointer table, but it does require a current GL context — it is safe to call only while the original OpenGL device is still alive (SUCCESS path), and must NOT be called after the device has been replaced with `EDT_NULL` (RELEASE fallback path). See step 3 for the guarded query sequence.

3. **GL_MAX_TEXTURE_SIZE query**: Query with `glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize)` only in the SUCCESS path (after `glewInit()` returns `GLEW_OK` or `GLEW_ERROR_NO_GL_VERSION`). A non-null return from `createDevice()` guarantees an OpenGL context is current on the calling thread, and `glGetIntegerv(GL_MAX_TEXTURE_SIZE, ...)` is a core OpenGL 1.0 entrypoint that resolves through the platform's native GL dispatch (not through GLEW's function-pointer table). Do NOT query `GL_MAX_TEXTURE_SIZE` in the RELEASE fallback path after a fatal `glewInit()` failure — at that point the original OpenGL device has been destroyed and replaced with an `EDT_NULL` device, which has no GL context. Calling `glGetIntegerv` without a current GL context is undefined behaviour.

   **Placement rule**: `glGetIntegerv(GL_MAX_TEXTURE_SIZE, ...)` MUST appear inside the SUCCESS branch of the `glewResult` check shown below — never inside the `else` (RELEASE fallback) branch and never after the `else` block executes. The RELEASE fallback's Step 4 (in the code block above) already sets `m_maxTextureSize = 2048` as a hardcoded constant without any GL call; the guarded query below applies to the SUCCESS path only.

   ```cpp
   if (glewResult == GLEW_OK || glewResult == GLEW_ERROR_NO_GL_VERSION) {
       // SUCCESS path: GL context is current; query the real hardware limit.
       glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
   } else {
       // RELEASE fallback: original OpenGL device already destroyed and replaced with
       // EDT_NULL (see Step 4 above) — no GL context is current.
       // Do NOT call glGetIntegerv or any GL function here.
       m_maxTextureSize = 2048;  // safe conservative default (already set in Step 4)
   }
   ```

4. **Extension queries**: Use `glewIsExtensionSupported("GL_EXT_texture_sRGB")` etc. only after a successful `glewInit()`.

**IrrlichtUIBackend construction ordering constraint**: `IrrlichtUIBackend` depends on GLEW
extension flags being populated (`GLEW_EXT_texture_filter_anisotropic`,
`GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT`) to correctly initialize `m_maxAnisotropy` in its
constructor. Therefore, `IrrlichtUIBackend` MUST be constructed **after** `glewInit()` returns
`GLEW_OK` **or** `GLEW_ERROR_NO_GL_VERSION` (GLVND Linux returns `GLEW_ERROR_NO_GL_VERSION` as a
valid success indicator; extension flags are still populated). Do NOT instantiate
`IrrlichtUIBackend` in a C++ member initializer list (which runs before the constructor body
where `glewInit()` is typically called) — construct it explicitly in the constructor body after
the `glewInit()` call. If `IrrlichtUIBackend` is constructed before `glewInit()`,
`GLEW_EXT_texture_filter_anisotropic` evaluates to 0 (false), and `m_maxAnisotropy` silently
defaults to `1.0f` on all hardware, defeating the anisotropic filtering detection entirely
without any error or warning.

**GLEW availability spike**: Phase 2 must verify whether the vendored Irrlicht build exposes GLEW symbols. If GLEW is unavailable (Irrlicht compiled without GLEW), all extension checks must use `glGetString(GL_EXTENSIONS)` string matching or `IVideoDriver::queryFeature()` instead. This spike must complete before any `glewIsExtensionSupported()` code is written. The spike result must be documented in BOTH `architecture/graphics-architecture/irrlicht-device-lifecycle.md` under the Phase 2 Spike Results section AND as a one-line comment in `src/rendering/render_system.h` confirming the confirmed extension query path (glewIsExtensionSupported or glGetString(GL_EXTENSIONS) fallback). The code comment ensures in-code documentation for implementers; the architecture doc ensures the decision is visible to non-implementers reviewing the spec.

### GLEW Spike — Two Independent Questions

The GLEW availability spike answers two completely independent questions. Conflating them produces incorrect conclusions — the answer to question 1 has no bearing on the answer to question 2.

**Question 1: Does the vendored Irrlicht source use GLEW internally?**

Inspect `source/Irrlicht/COpenGLDriver.cpp` for `#include "glew.h"` or `glewInit()` calls. This determines only whether Irrlicht's own internal GL calls go through GLEW's function pointer table. It does NOT determine whether AI Town can link GLEW independently.

**Question 2: Can AI Town link against GLEW independently and call `glewInit()`?**

The answer to this question is determined solely by whether `find_package(GLEW REQUIRED)` succeeds. When the vcpkg `glew` port is installed, this is ALWAYS yes — regardless of whether Irrlicht bundles GLEW internally.

`find_package(GLEW REQUIRED)` MUST remain in CMakeLists.txt regardless of the answer to question 1. AI Town calls `glewInit()` itself to populate its own function pointer table for `glCompressedTexImage2D` and `glewIsExtensionSupported()`. Question 1 only affects whether symbol duplication risk exists — which is handled by ensuring both link to the same vcpkg GLEW build.

## GLEW Symbol Duplication Risk

Irrlicht bundles its own copy of GLEW headers and symbols (in `source/Irrlicht/glew.h` and compiled into the Irrlicht static library). The vcpkg `glew` port provides a separate GLEW build. When both are present in the same link, the same symbols (e.g. `glewInit`, `glCompressedTexImage2D`) are defined twice, producing linker warnings or — on some toolchains — silent ODR violations that cause incorrect function pointer resolution at runtime.

**Mitigations (in priority order):**

1. **Check whether the Irrlicht vcpkg port already strips bundled GLEW**: **Spike result (Phase 1 investigation)**: The `adrido/irrlicht-vcpkg` portfile does NOT strip bundled GLEW. The port supplies a custom `CMakeLists.txt` (since Irrlicht 1.8.4 ships none) that uses `glob_c_cpp_sources(IRR_SRC_FILES source/Irrlicht)` — this glob unconditionally includes `source/Irrlicht/glew.c` and `source/Irrlicht/glew.h`. No `-DIRRLICHT_BUILD_GLEW=OFF` option, no `IRRLICHT_USE_SYSTEM_GLEW` flag, no patch file, and no `glew` entry in `Build-Depends` were found. Bundled GLEW is compiled into the Irrlicht library — duplication with vcpkg GLEW is confirmed present. Mitigation 1 (port stripping) is NOT available; mitigation 2 (link order) is mandatory.
2. **Link vcpkg GLEW before Irrlicht** in CMake `target_link_libraries` order — the linker takes the first definition found; placing vcpkg GLEW first ensures the correct, versioned GLEW symbols win:

   ```cmake
   target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht ...)
   ```

   `aitown_render` is the static library where both GLEW and Irrlicht are first linked together. The `aitown` executable only links against `aitown_render` transitively and must not re-link GLEW or Irrlicht directly.

3. **Linux fallback** — if symbol duplication cannot be resolved by link order, add `-Wl,--allow-multiple-definition` to the linker flags for Linux only:

   ```cmake
   if (UNIX AND NOT APPLE)
       target_link_options(aitown PRIVATE -Wl,--allow-multiple-definition)
   endif()
   ```

   This suppresses the linker error but does not guarantee the correct definition wins; link order (mitigation 2) must still be applied.

   **Phase 1 constraint**: The `-Wl,--allow-multiple-definition` linker option applies to the `aitown` **executable** target. At Phase 1, only `aitown_render` (a `STATIC` library) exists — the `aitown` executable may not yet be present. CMake silently ignores `target_link_options()` on `STATIC` library targets (static libraries use the archiver `ar`, not the linker). Therefore, **mitigation 3 is not available at Phase 1**. If the `nm build/libaitown_render.a` symbol-duplication check at Phase 1 finds duplicate GLEW symbols after applying mitigation 2 (link order), **the only viable resolution at Phase 1 is mitigation 1** — verify and patch the Irrlicht vcpkg portfile to strip bundled GLEW. The Phase 1 PR MUST NOT be merged with known duplicate GLEW symbols. Mitigation 3 (`-Wl` flag) can be applied once the `aitown` executable target exists in Phase 2+.

**Phase 2 build verification (BUILD-BLOCKING):** The build engineer must verify that no duplicate GLEW symbols remain after applying mitigations. Run the following after a successful Linux build:

```bash
nm build/aitown | grep -i glew | sort | uniq -d
```

**Note on `-D` flag**: On a fully static Linux build, the `-D` flag inspects only the dynamic export table and will produce no output even when symbols are duplicated in the static image. Plain `nm` without `-D` must be used for statically-linked executables.

**Phase 1 note**: At Phase 1, the `aitown` final executable target may not yet exist. Run the symbol duplication check against the render library at Phase 1 instead:

```bash
nm build/libaitown_render.a | grep -i glew | sort | uniq -d
```

`libaitown_render.a` is the library where GLEW and Irrlicht are first linked together at Phase 1. Once the `aitown` executable exists (Phase 2+), use `nm build/aitown`. The plain `nm` without `-D` flag requirement applies to both: on a fully static build, `-D` inspects only the dynamic export table and produces no output even when symbols are duplicated in the static image.

If this command produces any output, duplicate GLEW symbols are present and the build is BLOCKED — the issue must be resolved before merging. The result of this check (clean or duplicate list) must be recorded in the Phase 2 Spike Results section below.

## Building Atlas Resolution Fallback

`RenderSystem` queries and stores `m_maxTextureSize` during GL capability
initialization (see "GL Capability Query Initialization" above). `TextureCache`
reads this value when loading the building diffuse atlas to decide whether the
primary 4096×4096 atlas is safe to upload.

### Detection

`TextureCache::loadSRGB()` checks `m_maxTextureSize` before loading
`buildings_atlas_d.dds`. If `m_maxTextureSize < 4096`, the fallback path is
taken instead of the primary path.

```cpp
const std::string atlasPath =
    (m_maxTextureSize >= 4096)
        ? "assets/textures/buildings_atlas_d.dds"
        : "assets/textures/buildings_atlas_d_2k.dds";
```

### Fallback Asset

When `m_maxTextureSize < 4096`, load `buildings_atlas_d_2k.dds` — the
2048×2048 DXT1 sRGB fallback atlas — in place of the primary
`buildings_atlas_d.dds` atlas. Both files must be shipped with the game; the
fallback is not generated at runtime.

### Warning Log

When the fallback path is taken, log a `WARNING` to stderr before the load:

```cpp
std::fprintf(stderr,
    "WARNING: GL_MAX_TEXTURE_SIZE < 4096; "
    "loading fallback atlas buildings_atlas_d_2k.dds\n");
```

### Naming Convention

The fallback asset uses the suffix `_2k` inserted immediately before the file
extension: `<basename>_2k.dds`. For the building diffuse atlas:

| Role | Filename | Resolution |
|---|---|---|
| Primary atlas | `buildings_atlas_d.dds` | 4096×4096 DXT1 sRGB |
| Fallback atlas | `buildings_atlas_d_2k.dds` | 2048×2048 DXT1 sRGB |

Both files must be present in the shipping build. The `_2k` suffix pattern
applies to any future atlas that follows the same dual-resolution scheme.

### GL_TEXTURE_MAX_LEVEL

The two atlas sizes use different mip chain depths. Set `GL_TEXTURE_MAX_LEVEL`
immediately after uploading the compressed texture data:

| Atlas | Resolution | Mip levels | `GL_TEXTURE_MAX_LEVEL` |
|---|---|---|---|
| Primary | 4096×4096 | 5 (4096 → 256) | `4` |
| Fallback | 2048×2048 | 4 (2048 → 256) | `3` |

The fallback 2048 atlas retains the same 4-level mip chain as the V1 atlas
(as documented in `architecture/asset-standards/2d-texture-standards.md`).
The primary 4096 atlas adds one additional mip level (level 0 at 4096×4096).

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
    (m_maxTextureSize >= 4096) ? 4 : 3);
```

## Phase 2 Spike Results

**Q1 — Does vendored Irrlicht use GLEW internally?**
NO — binary analysis of `build/vcpkg_installed/x64-linux/lib/libIrrlicht.a` confirms zero GLEW
symbols in libIrrlicht.a (command: `nm libIrrlicht.a | grep -iE "glew" | head -20` produced no
output). The adrido/irrlicht-vcpkg port's CMakeLists.txt uses `glob_c_cpp_sources` which would
include `source/Irrlicht/glew.c` if present, but inspection shows no GLEW object file in the
archive (`ar -t libIrrlicht.a | grep -i glew` produced no output). Irrlicht's OpenGL driver
implements extension handling via `COpenGLExtensionHandler` without GLEW. No GLEW duplication
risk from Irrlicht.

**Q2 — Can AI Town link GLEW independently (find_package(GLEW REQUIRED))?**
CONFIRMED YES — vcpkg glew port present and `find_package(GLEW REQUIRED)` succeeds.
Extension query path confirmed: `glewIsExtensionSupported()` (no `glGetString(GL_EXTENSIONS)` fallback needed).

**Symbol duplication nm check result:**
Command: `nm build/libaitown_render.a | grep -i glew | sort | uniq -d`
Result: CLEAN — no output (no duplicate GLEW symbols).
Resolution: None required — Irrlicht does not bundle GLEW; link-order mitigation-2 (GLEW::GLEW
before Irrlicht in `target_link_libraries`) is retained as belt-and-suspenders but is confirmed
not needed for symbol correctness.

**LOD spike Checkbox B — `CMeshSceneNode::setMesh()` grab/drop contract:**
VERIFIED by binary analysis of `CMeshSceneNode.cpp.o` extracted from `libIrrlicht.a` via objdump:
`setMesh()` calls `grab()` on the new mesh and `drop()` on the old mesh.
Caller MUST call `->drop()` on `newLODMesh` after `setMesh()`. See `scene-graph-ownership.md`.
Phase 5 TerrainChunk work is UNBLOCKED.

## Test Guard — `shader_stub_compile_test` Skip vs. Fail

The `GTEST_SKIP()` guard in `shader_stub_compile_test` must only activate when
`createDevice()` returns null AND the `DISPLAY` environment variable is unset.
Under `xvfb-run` (where `DISPLAY` is set), a null return from
`createDevice(EDT_OPENGL)` is a test FAILURE (`FAIL()`), not a skip — it
indicates Mesa or OpenGL is misconfigured in CI. Conflating the two conditions
produces a silent false-pass: the test appears green while the OpenGL context
is broken.
