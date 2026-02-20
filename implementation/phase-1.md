## Phase 1: Render Skeleton & Camera

### Goal

Establish the Irrlicht device lifecycle, render loop call order, camera controller, and the UIManager shell so all subsequent phases have a working display target to integrate into.

### Deliverables

- [ ] `RenderSystem` class: owns `IrrlichtDevice*` (RAII, `device->drop()` in destructor); `EDT_OPENGL` on all platforms; log and abort if OpenGL unavailable (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] `SIrrlichtCreationParameters`: `EDT_OPENGL`, `Bits=32`, `ZBufferBits=24`, `Stencil=true`, `AntiAlias=4`, `Vsync=false`, initial window `1280×720` (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] Render loop call order enforced: `beginScene` → `sceneManager->drawAll()` → `UIManager::draw()` → `endScene()`. **`m_gui->drawAll()` is NOT called by `RenderSystem`** — per `architecture/graphics-architecture/irrlicht-device-lifecycle.md` and `architecture/ui-ux/ui-manager.md`, `UIManager::draw()` issues explicit per-panel draw calls in Z-order via `IUIBackend`; calling `m_gui->drawAll()` would bypass the explicit layering required for the background scrim and modal overlay. The `IRenderer` facade exposes `endFrame()` as its abstract method name — this wraps `driver->endScene()` internally. The prohibition in `architecture/graphics-architecture/irrlicht-device-lifecycle.md` on calling `endFrame()` applies only to direct `IVideoDriver` calls; the `IRenderer::endFrame()` abstraction is the correct call from all non-rendering code. **Per-frame execution sequence rule** (`graphics-dev-irrlicht`): ALL simulation logic updates and audio updates (`CitySimulation::tick()`, `AudioSystem::syncListenerToCamera()`, `AudioSystem::update()`) MUST execute BEFORE `RenderSystem::beginFrame()` (`driver->beginScene()`) — never interleave logic updates inside the begin/end scene block. The canonical frame sequence is: (1) poll events, (2) game logic tick, (3) `CameraController::update(dt)`, (4) `AudioSystem::syncListenerToCamera()` + `AudioSystem::update()`, (5) `RenderSystem::beginFrame()`, (6) `sceneManager->drawAll()`, (7) `UIManager::draw()`, (8) `RenderSystem::endFrame()`. **OAL-2 ordering rule**: `CameraController::update(dt)` at step (3) MUST execute BEFORE `AudioSystem::syncListenerToCamera()` at step (4) — the listener must read the camera position AFTER it has been updated for the current frame. Calling `syncListenerToCamera()` before `CameraController::update(dt)` causes the audio listener to lag one frame behind the camera, producing a slight spatial audio desync that is imperceptible at low camera speeds but becomes noticeable during fast pans. This sequence must be documented as a comment at the main loop call site. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`, `architecture/ui-ux/ui-manager.md`)
- [ ] `GL_MAX_TEXTURE_SIZE` queried **immediately after** `createDevice()`, stored as `m_maxTextureSize`; `EDT_NULL` guard initializes to 2048 without GL query (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] `GL_EXT_texture_sRGB` extension presence checked via `glewIsExtensionSupported()` immediately after `createDevice()` in `RenderSystem::init()`; result stored as `bool m_srgbTextureSupported`; `isSRGBTextureSupported() const` accessor exposed; `EDT_NULL` guard initializes to `false` (no GL context). `TextureCache` reads this flag at construction to select the upload path (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` queried after checking `GL_EXT_texture_filter_anisotropic` extension presence via `glewIsExtensionSupported()`; if extension absent, log warning, set `m_maxAnisotropy = 1.0f`, and skip `glGetFloatv`; if extension present, query and store as `m_maxAnisotropy`; `EDT_NULL` guard initializes to `1.0f` unconditionally WITHOUT any extension check or GL call (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] `src/rendering/shader_constants.h` stub created with the full `kTexUnit*` constant table (`kTexUnitDiffuse=0` through `kTexUnitBillboard=9`) as `constexpr int` values per `architecture/asset-standards/2d-texture-standards.md`; file must exist before Phase 2 begins; actual shader usage wired in Phase 2 (ref: `architecture/asset-standards/2d-texture-standards.md`). The file MUST also include the mandatory compile-time range guard:

  ```cpp
  static_assert(kTexUnitBillboard <= 15,
      "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)");
  ```

  Both the constant table AND the `static_assert` must be present before Phase 2 begins. The guard prevents silent failures when a new texture unit constant is added above GL unit 15. (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] `TextureCache` class skeleton (stub) in `src/rendering/texture_cache.h`: must include all three pool structures per `architecture/graphics-architecture/texture-cache.md`:
  - `m_driverType` member of type `irr::video::E_DRIVER_TYPE`, initialized from the constructor parameter (e.g., `explicit TextureCache(irr::video::E_DRIVER_TYPE driverType)` → `m_driverType{driverType}`). **This member is required in Phase 1** so that the `EDT_NULL` guard on the sRGB upload path in Phase 2 can check `m_driverType == irr::video::EDT_NULL` without requiring a header change. Adding `m_driverType` in Phase 2 forces a constructor signature change that breaks all Phase 1 callers and tests.
  - `m_srgbTextures` (`std::unordered_map<std::string, SRGBEntry>` stub — raw `GLuint` pool, separate from the linear `ITexture*` pool)
  - `m_splatMaps` (`std::unordered_map<std::string, SplatEntry>` stub — raw `GLuint` pool for terrain splat maps). **Canonical member name is `m_splatMaps` per `texture-cache.md` eviction code — do NOT use `m_splatMapTextures`.**
  - Eviction interface method stubs: `releaseLinear(ITexture*)`, `releaseLinear(const std::string& key)`, `releaseSRGB(const std::string& filename)`, `releaseSplatMap(const std::string& filename)` (no-op stub), `evictUnreferenced()` (covers all three pools)
  - Stub accessors: `getSRGBGLuint(const std::string&) const` returning `GLuint{0}`, `getSplatMapGLuint(const std::string&) const` returning `GLuint{0}` (parallel to `getSRGBGLuint`). **NOTE (Irrlicht-2)**: The sRGB accessor is canonically named `getSRGBGLuint` per `architecture/graphics-architecture/texture-cache.md`. An earlier draft of `shader-loading.md` referenced `getGLuint` — that name has been corrected to `getSRGBGLuint` in the spec; the plan must use `getSRGBGLuint` consistently. If any call site uses `getGLuint`, it must be renamed.
  - Stub load methods: `loadSplatMap(const std::string& path)` returning sentinel `GLuint{0}`; `loadLinear(const std::string& path)` returning `ITexture* {nullptr}`; **`loadSRGB(const std::string& path, GLenum format)` returning sentinel `GLuint{0}`** — the `GLenum format` parameter selects between `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` (opaque diffuse) and `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` (transparent diffuse); the three stub load methods (`loadLinear`, `loadSRGB`, `loadSplatMap`) are now symmetric across all three pools (linear, sRGB, splat map)

  **Irrlicht-3 — "No real GL calls" clarification**: "No real GL calls in Phase 1" means no actual GPU texture uploads happen — `loadSRGB()` does NOT call `glCompressedTexImage2D`, `glGenTextures`, or any other GL function that requires an active context. However, the mip dispatch rules (which DDS filename suffix routes to which upload path) ARE encoded as stub logic in the Phase 1 skeleton — the `loadSRGB()` stub body contains conditional dispatch comments showing the correct routing for Phase 2:

  ```cpp
  // STUB DISPATCH LOGIC (no real GL calls — logging only):
  // if (path ends with "_d")         → sRGB upload path (Phase 2: glCompressedTexImage2D with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)
  //   EXCEPTION: "vehicles_sprite_atlas_d.dds" → LINEAR path via loadLinear()
  //   (roof color palette swatches, not photographic diffuse — must NOT be sRGB-decoded)
  //   Check filename BEFORE suffix. See architecture/asset-standards/2d-texture-standards.md.
  // if (path ends with "_billboard") → sRGB upload path (Phase 2: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
  // if (path ends with "_n")         → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_s")         → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_sprite")    → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_lm")        → linear path — use loadLinear() NOT loadSRGB()
  // STUB BODY (Phase 1):
  //   fprintf(stderr, "STUB: would upload %s via sRGB path\n", path.c_str());
  //   return GLuint{0};
  ```

  This split makes the dispatch contract visible for Phase 2 review before any real GL calls are written. All three pools must be present so Phase 2 and Phase 4 can build against the `TextureCache` interface without structural CMakeLists changes. **If the Phase 1 GLEW availability spike reveals GLEW is absent, the `loadSRGB()` stub body comment must be updated to reflect `glGetString(GL_EXTENSIONS)` as the fallback extension check path (instead of `glewIsExtensionSupported()`). Method signatures remain unchanged — only the internal comment changes.**

  **`loadSRGB()` stub body comment requirement**: The `loadSRGB(const std::string& path, GLenum format)` stub body MUST include the following documentation comments for the Phase 2 implementor:

  ```cpp
  // Phase 2 implementation requirement: if path ends with "_billboard", set
  // GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE and GL_TEXTURE_WRAP_T = GL_CLAMP_TO_EDGE
  // after glTexParameteri filter calls. Default GL_REPEAT causes ghost-frame artifacts
  // at the 1x8 horizontal strip boundary. See architecture/graphics-architecture/texture-cache.md.
  //
  // TODO Phase 2: apply GL_TEXTURE_MAX_LEVEL dispatch table per texture-cache.md:
  //   _billboard suffix -> GL_TEXTURE_MAX_LEVEL = 3 (4-level mip chain mandatory)
  //   _d suffix (sRGB)  -> GL_TEXTURE_MAX_LEVEL = 3 (standard 4-level mip chain)
  //   splat maps        -> use loadSplatMap() NOT loadSRGB(); GL_TEXTURE_MAX_LEVEL=0 (single mip)
  //   NOTE: _n, _s, _sprite use loadLinear() NOT loadSRGB() — no glTexParameteri access
  ```

  ```cpp
  // NOTE: _lm (lightmap) textures use loadLinear() NOT loadSRGB() — they are linear-format
  // textures uploaded via IVideoDriver::getTexture(). Do NOT add _lm dispatch to loadSRGB().
  // See texture-cache.md GL_TEXTURE_MAX_LEVEL table for the full dispatch.
  ```

  ```cpp
  // TODO Phase 2: When binding this GLuint in OnSetConstants(), save the current
  // GL_ACTIVE_TEXTURE unit with glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit) BEFORE
  // calling glActiveTexture(). Restore with glActiveTexture(savedUnit) AFTER unbinding.
  // Failure to restore corrupts Irrlicht's internal active-unit tracking.
  // See architecture/graphics-architecture/shader-loading.md — CRITICAL save/restore section.
  ```

  (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] **TextureCache mip level control** (`graphics-dev-irrlicht`): `TextureCache` must apply the following rules in the sRGB upload path (`loadSRGB()`) based on the texture filename suffix. This is the **GL_TEXTURE_MAX_LEVEL dispatch table** from `architecture/graphics-architecture/texture-cache.md`:
  - Set `GL_TEXTURE_MAX_LEVEL = 3` for **billboard atlas textures** (`_billboard` suffix) — the 4-level mip chain (1024×128 → 512×64 → 256×32 → 128×16) is **mandatory** for billboard imposters per `architecture/asset-standards/2d-texture-standards.md`.
  - Set `GL_TEXTURE_MAX_LEVEL = 3` for **diffuse sRGB textures** (`_d` suffix) — standard 4-level mip chain.
  - **NOTE**: `_lm` (lightmap) textures are loaded via `loadLinear()` — no `GL_TEXTURE_MAX_LEVEL` control is possible through the IVideoDriver path. The `loadSRGB()` dispatch table must NOT include an `_lm` case.
  - **NOTE**: `_n` (normal maps), `_s` (specular), and `_sprite` (sprite atlases) use `loadLinear()` NOT `loadSRGB()` — the `GL_TEXTURE_MAX_LEVEL` dispatch for these suffixes belongs in the `loadLinear()` path, not `loadSRGB()`.
  - **NOTE**: splat maps use `loadSplatMap()` NOT `loadSRGB()` — the `loadSRGB()` dispatch table must NOT include a splat map case.
  **Cross-reference**: the `TextureCache` skeleton implements this dispatch table; the `loadSRGB()` stub must also include a TODO comment referencing this table. These dispatch rules must be explicit in the `TextureCache` sRGB load path. (ref: `architecture/graphics-architecture/texture-cache.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **Consolidated GL capability query guard**: before Phase 2 begins, ALL OpenGL capability queries (`GL_MAX_TEXTURE_SIZE`, `GL_EXT_texture_sRGB` extension check, `GL_EXT_texture_filter_anisotropic` extension check, and any others) must be consolidated into a single initialization sequence in `RenderSystem::init()` guarded by an `EDT_NULL` pre-check. Under `EDT_NULL`: set `m_maxTextureSize = 2048`, `m_maxAnisotropy = 1.0f`, `m_srgbTextureSupported = false` — no GL calls made. **Under a real OpenGL device (non-EDT_NULL): call `glewInit()` as the FIRST action in this initialization sequence** before any `glewIsExtensionSupported()` or `glGetIntegerv()` call — GLEW requires initialization to populate its function pointer table; calling `glewIsExtensionSupported()` before `glewInit()` causes a null-function-pointer crash. Check `glewInit()` return value using the two-tier handling: (a) if it returns `GLEW_OK`, proceed with extension queries normally; (b) if it returns `GLEW_ERROR_NO_GL_VERSION` specifically, log WARNING and continue — function pointers may still be valid; proceed with extension queries but log that GLEW is in degraded state; (c) if it returns any other non-`GLEW_OK` code, treat as fatal in DEBUG (abort) and in RELEASE: set `m_maxTextureSize = 2048` as the safe default — **NO `glGetIntegerv` call on the RELEASE path** (the GL context is not guaranteed to be in a valid state when `glewInit` fails; calling `glGetIntegerv` with a broken function-pointer table produces undefined behaviour), then recreate `IrrlichtDevice` with `EDT_NULL`, show a user-facing error dialog "OpenGL initialisation failed", and continue in headless mode using safe defaults: `m_srgbTextureSupported = false`, `m_maxAnisotropy = 1.0f`, `m_maxTextureSize = 2048`. Leaving an OpenGL device with broken GLEW function pointers active is incorrect — it will crash on any subsequent GL extension call. For the RELEASE fallback `EDT_NULL` path, skip the `GL_MAX_TEXTURE_SIZE` query entirely and use `m_maxTextureSize = 2048` as the safe fallback value. Under `GLEW_OK` and `GLEW_ERROR_NO_GL_VERSION` paths: call `glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize)` after `glewInit()` returns and before any RELEASE fallback `EDT_NULL` recreation is triggered. **GL_MAX_TEXTURE_SIZE safety on both GLEW_OK and GLEW_ERROR_NO_GL_VERSION paths**: both paths permit `glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize)` because `GL_MAX_TEXTURE_SIZE` is a core OpenGL 1.0 entrypoint resolved via the platform's native GL dispatch table, NOT through GLEW's function-pointer table. Only the fatal non-GLEW_OK fallback (EDT_NULL recreation) must skip the `GL_MAX_TEXTURE_SIZE` query. The glewInit failure path must be documented in `src/rendering/render_system.h` with a comment. The guard sequence and order must be documented in `src/rendering/render_system.h` before Phase 2 graphics work begins. **`getGPUProgrammingServices()` must be null-checked before any `addHighLevelShaderMaterialFromFiles()` call**: return early for `EDT_NULL` driver; log and abort in debug builds if null on a non-EDT_NULL driver (shader failure is fatal in debug builds per `architecture/graphics-architecture/shader-loading.md`). (ref: `architecture/asset-standards/2d-texture-standards.md`, `architecture/graphics-architecture/texture-cache.md`, `architecture/graphics-architecture/shader-loading.md`)
- [ ] **GLSL shader stub files**: `assets/shaders/lighting.vert`, `assets/shaders/lighting.frag`, `assets/shaders/terrain.vert`, `assets/shaders/terrain.frag`, `assets/shaders/billboard.vert`, and `assets/shaders/billboard.frag` created as stub files in Phase 1 with a trivial pass-through GLSL implementation. **All Phase 1 GLSL stub shaders MUST begin with a `#version 130` directive as the first non-comment line.** OpenGL 3.x core profiles reject GLSL source that does not specify a version directive — omitting `#version 130` causes a shader compilation failure even for trivial pass-through stubs. The `shader_stub_compile_test.cpp` integration test (see below) will catch this if the directive is missing. These files must exist before Phase 2 shader loading infrastructure is built — the shader loading code needs targets to load.

  **Irrlicht-5 — GLSL stub co-landing requirement**: `assets/shaders/terrain.vert` and `assets/shaders/terrain.frag` MUST be committed in the **same PR** as `ShaderLoadingTest` (i.e., `shader_stub_compile_test.cpp`). Committing the test without the shader stubs causes an immediate CI failure because `shader_stub_compile_test.cpp` attempts to load these files via `gpu->addHighLevelShaderMaterialFromFiles()`. If the files are absent, the `matType == -1` assertion fails. This co-landing requirement applies to ALL six stub shader files: the test file and all six `.vert`/`.frag` stubs must be in the same commit. (ref: `architecture/graphics-architecture/shader-loading.md`)

  **NOTE**: Use the 8-parameter overload of `addHighLevelShaderMaterialFromFiles()` (no geometry shader). The Irrlicht GLSL backend has no geometry shader stage in V1. The geometry shader parameters (if present in a higher overload) must be left as empty string / nullptr.

  **Exact 8-param overload call pattern (Phase 1 GLSL stub pattern)**:

  ```cpp
  // Exact 8-param overload (Phase 1 GLSL stub pattern):
  s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
      vsFile, "main", video::EVST_VS_1_1,   // vertex shader, entry point, version enum (IGNORED by GLSL backend — version set by #version directive in .vert file)
      fsFile, "main", video::EPST_PS_1_1,   // fragment shader, entry point, version enum
      cb, video::EMT_SOLID);                // IShaderConstantSetCallBack*, base material
  cb->drop();  // Irrlicht calls grab() on cb; we must drop() to transfer ownership. See architecture/graphics-architecture/shader-loading.md.
  // FAILURE PATH WARNING: if matType == -1, Irrlicht did NOT call grab().
  // drop() reduces ref_count 1→0 and destroys cb NOW.
  // Do NOT dereference cb below this line.
  // Use pre-captured local path strings (vsFile, fsFile) for error messages.
  ```

- [ ] **GLSL shader stub compile-time validation** (`graphics-dev-irrlicht`): `opengl_tests` target extended with `tests/rendering/shader_stub_compile_test.cpp` (label `requires-opengl`); added to the `add_executable(opengl_tests ...)` call in CMakeLists.txt alongside `stub_succeed.cpp` and `lod_swap_smoke_test.cpp`. Loads `lighting.vert` and `lighting.frag` via `gpu->addHighLevelShaderMaterialFromFiles()` on an `EDT_OPENGL` device, asserts the returned material type is not −1. This catches missing `#version` directive, syntax errors, or incorrect GLSL path resolution before Phase 2 shader infrastructure is built on top of these stubs. Must be green before Phase 2 begins. **Note**: the Phase 0 CMakeLists.txt contains a comment `# lod_swap_smoke_test.cpp  # Phase 6 and later` which is INCORRECT — it was written before the Phase 1 plan finalized LOD smoke test placement. The Phase 1 amendment to `add_executable(opengl_tests ...)` must remove this comment and use the exact form: `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp tests/rendering/lod_swap_smoke_test.cpp)`. Per `architecture/testing/framework.md`, do NOT use `target_sources()` to add files to `opengl_tests` — the `add_executable` call must list all sources inline. **`target_sources()` is PROHIBITED for `opengl_tests`** (per `architecture/testing/framework.md`): to add `shader_stub_compile_test.cpp` to `opengl_tests`, amend the `add_executable(opengl_tests ...)` call inline. The Phase 0 `add_executable` must be amended in Phase 1 — do not use `target_sources()` for this target. The `opengl_tests` target must link: `target_link_libraries(opengl_tests PRIVATE aitown_render GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)` — per `architecture/testing/framework.md`, `rapidcheck` and `rapidcheck_gtest` are mandatory on all test targets. **Shader callback lifetime**: the test must follow the raw-heap + `->drop()` callback lifetime pattern from `architecture/graphics-architecture/shader-loading.md` — allocate the callback with `new`, call `cb->drop()` unconditionally after `addHighLevelShaderMaterialFromFiles()`. Do NOT wrap the callback in `std::unique_ptr` (causes double-free since Irrlicht also calls drop). **Null-device guard**: the test body must null-check the `IrrlichtDevice*` returned by `createDevice()` using the two-condition form:

  ```cpp
  IrrlichtDevice* device = createDevice(EDT_OPENGL, ...);
  if (!device) {
      const char* display = std::getenv("DISPLAY");
      if (display && display[0] != '\0') {
          FAIL() << "createDevice(EDT_OPENGL) returned null with DISPLAY set — "
                    "OpenGL/Mesa is misconfigured in CI.";
      }
      GTEST_SKIP() << "No display available; shader compilation skipped.";
  }
  ```

  When `DISPLAY` is set and `createDevice()` still returns null, this is a real OpenGL misconfiguration failure — it must be reported as `FAIL()`, not silently skipped. Only when `DISPLAY` is absent (or empty) should the test skip. Exit criterion: `shader_stub_compile_test` must PASS (not skip) on ubuntu-latest WITH xvfb before Phase 2 begins. Under headless CI without xvfb, `GTEST_SKIP()` is the expected outcome and counts as passing the CI check.
- [ ] **GLEW availability spike** (`graphics-dev-irrlicht`): inspect the vendored Irrlicht build's `COpenGLDriver.cpp` to confirm whether GLEW is bundled and whether `glewIsExtensionSupported()` is exposed. If Irrlicht was compiled without GLEW (custom build using ARB string parsing), `glewIsExtensionSupported()` will not link — replace all calls with `glGetString(GL_EXTENSIONS)` string matching or `IVideoDriver::queryFeature()`. Document the confirmed extension query path in BOTH `src/rendering/render_system.h` (one-line code comment, e.g., `// GLEW available in vendored Irrlicht — using glewIsExtensionSupported()`) AND `architecture/graphics-architecture/irrlicht-device-lifecycle.md` under a "Phase 1 Spike Results" subsection. **Phase 2 sRGB texture pipeline is BLOCKED on this spike result.** Owner: `graphics-dev-irrlicht`.

  **H19 — GLEW spike file reference correction**: the spike result document location is `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (under "Phase 1 Spike Results") — NOT `architecture/graphics-architecture/scene-graph-ownership.md`. Any reference in this phase to recording the GLEW availability spike result in `scene-graph-ownership.md` is incorrect and must be treated as referring to `irrlicht-device-lifecycle.md` instead.

  **sRGB format tokens if GLEW absent**: If GLEW is unavailable, the sRGB internal format tokens must be defined manually as their OpenGL specification hex literals before `glCompressedTexImage2D` is called: `#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C` and `#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4E`. These values are from the `EXT_texture_sRGB` extension spec and are stable across platforms.

- [ ] **`find_package(GLEW REQUIRED)` in CMakeLists.txt root** (`graphics-dev-irrlicht` + `cicd-dev-github`): add `find_package(GLEW REQUIRED)` to the CMakeLists.txt root and `GLEW::GLEW` to `aitown_render`'s `target_link_libraries`. This is required for the GL capability query initialization sequence in `RenderSystem`. Owner: `graphics-dev-irrlicht` with `cicd-dev-github` reviewing the CMakeLists change. **glew vcpkg port existence verification** (`cicd-dev-github`): before committing `find_package(GLEW REQUIRED)` and any `vcpkg.json` change, run `gh api /repos/microsoft/vcpkg/contents/ports/glew?ref=<VCPKG_COMMIT_ID>` and confirm a 200 response. If the port is absent at the current baseline, `VCPKG_COMMIT_ID` and `builtin-baseline` must be updated before Phase 1 CI will pass. This pre-verification is mandatory per `architecture/ci-cd/dependency-management.md`.
- [ ] `IRenderer` interface in `src/interfaces/` (alongside `IClock.h`, `ISimulationRNG.h`) with `beginFrame()`, `endFrame()`, `drawScene()`, `loadTexture()`, `setCamera()`; concrete `IrrlichtRenderer` in `src/rendering/` (ref: `architecture/testing/testability-architecture.md`)
- [ ] `IAudioSystem.h` in `src/interfaces/` — **Phase 0 created a stub with 11 method signatures. Phase 1 VERIFIES and LOCKS it — not re-authors it from scratch.** Phase 1 verifies it includes all 11 methods as defined in `architecture/audio-architecture/audio-system.md`: `playSound`, `playPositionalSound`, `stopSound`, `setMusicTrack`, `setSpeed`, `triggerStinger`, `syncListenerToCamera`, `setGameOverState`, `setTimeOfDay`, `transitionToGameplay`, `update`; all game-domain types declared in `src/interfaces/audio_types.h` — `IAudioSystem.h` includes `audio_types.h` (no OpenAL dependency). Phase 1 also verifies it **`#include "simulation_types.h"` for `SimSpeed`** — do NOT define `SimSpeed` or `SpeedMultiplier` directly in `IAudioSystem.h` or `audio_types.h`; `simulation_types.h` is the canonical owner. Omitting this include causes duplicate-type compile errors in translation units that include both `IAudioSystem.h` and `ICitySimulation.h`. Phase 1 also verifies that the `IClock*` dependency in the `AudioSystem` constructor is properly referenced in `src/audio/audio_system.h` (the stub declares `explicit AudioSystem(IClock* clock)`). **Phase 1 does NOT re-author this interface — it verifies and locks it.** **Intra-phase dependency**: `IAudioSystem.h` must be verified and locked by `sound-dev-opensoftal` before `UIManager` shell implementation begins in Phase 1. **`SimSpeed` vs `SpeedMultiplier` type relationship — RESOLVED**: The canonical enum is `SpeedMultiplier` with exactly 4 values as defined in `architecture/game-design/simulation-time.md`:

  ```cpp
  enum class SpeedMultiplier {
      Paused = 0,  // simulation frozen; real-time multiplier = 0
      x1     = 1,  // 1× real-time
      x3     = 2,  // 3× real-time (default starting speed)
      x10    = 3,  // 10× real-time
  };
  // Type alias for audio-facing API:
  using SimSpeed = SpeedMultiplier;
  ```

  The 5-value form (`PAUSED=0, SLOW=1, NORMAL=2, FAST=3, VERY_FAST=4`) is **NOT** canonical and must NOT appear in `simulation_types.h`. `using SimSpeed = SpeedMultiplier;` makes them identical — no conversion is required at `m_audio->setSpeed(m_sim->getSpeed())` call sites. Phase 1 exit criterion: `simulation_types.h` with the 4-value `SpeedMultiplier` enum and `using SimSpeed = SpeedMultiplier;` alias compiles cleanly. (ref: `architecture/game-design/simulation-time.md`, `architecture/audio-architecture/audio-system.md`, `architecture/testing/testability-architecture.md`)
- [ ] `AudioSystem` stub header `src/audio/audio_system.h` (`sound-dev-opensoftal`): header-only stub (no AL code, no implementation). Declares:

  ```cpp
  class AudioSystem : public IAudioSystem {
  public:
      // BEHAVIORAL CONTRACT locked in Phase 1:
      // If ALC_EXT_thread_local_context is absent at AudioSystem construction,
      // the constructor MUST throw std::runtime_error — no silent fallback is permitted.
      //
      // Extension detection method (LOCKED): use alcGetProcAddress(m_device, "alcSetThreadContext") ONLY.
      // Do NOT use alcIsExtensionPresent — it does not load the function pointer and creates a
      // dual-call pattern where the function may not be linkable directly. If alcGetProcAddress
      // returns null → throw std::runtime_error before launching the audio thread.
      // m_fnSetThreadCtx stores the result; Phase 4 audio thread calls m_fnSetThreadCtx(m_context).
      //
      // Phase 1 stub: the constructor body is empty. No AL or ALC calls are made in Phase 1.
      // The Phase 4 constructor sequence (including the temporary alcMakeContextCurrent(m_context)
      // main-thread bind) is documented in architecture/audio-architecture/audio-system.md
      // and implemented only in Phase 4.
      //
      // BEHAVIORAL CONTRACT for setGameOverState() in V1 (locked Phase 1):
      // Phase 4 stub body MUST be:
      //   void AudioSystem::setGameOverState(bool active) {
      //       // V1: no-op — Sandbox mode has no game-over condition.
      //       LOG_WARNING("setGameOverState() called in V1 Sandbox mode — no-op");
      //       return;
      //   }
      // Do NOT use m_scenarioMode — that member is not declared in V1.
      // Post-V1 Scenario-mode behavior: AudioSystem fades out all sources and stops the audio thread.
      //
      // Phase 4 REAL AudioSystem context bind sequence (for reference — NOT Phase 1 scope):
      //   Step 1: alcOpenDevice() + alcCreateContext() on the main thread.
      //   Step 2: alcMakeContextCurrent(m_context) on the MAIN THREAD — a REQUIRED temporary
      //           main-thread bind for HRTF metadata loading and initial AL state setup.
      //           This temporary bind IS required; it is NOT prohibited.
      //   Step 3: alcMakeContextCurrent(nullptr) on the MAIN THREAD BEFORE launching
      //           m_audioThread at Step 5. This clears the process context so the audio
      //           thread's alcSetThreadContext() call at thread startup is authoritative.
      //           alcMakeContextCurrent(nullptr) is called on the MAIN THREAD BEFORE thread
      //           launch — it is NOT deferred until the audio thread signals m_initDone.
      //           Deferring it would create a race window and risks a deadlock if the main
      //           thread waits on m_initCV while still holding the process context.
      //   Step 4: Initialize m_occlusionGainTarget[] before thread launch.
      //   Step 5: Launch m_audioThread. The audio thread calls m_fnSetThreadCtx(m_context)
      //           at startup, which is valid because the process context was already cleared
      //           in Step 3. See audio-system.md constructor sequence for the full 6-step order.
      // The temporary main-thread bind in Step 2 is architecturally required and must not be
      // removed or labelled as prohibited.
      explicit AudioSystem(IClock* clock);
      // All IAudioSystem pure-virtual methods declared override; bodies empty/throw; no AL calls

      // SHUTDOWN CONTRACT locked in Phase 1:
      // The full shutdown sequence is documented in architecture/audio-architecture/audio-thread-shutdown.md.
      // Key invariant (locked here for Phase 4 implementation): after audio thread joins, the main thread
      // must re-bind the AL context before performing AL cleanup. See audio-thread-shutdown.md step 3.5.
      // Member names for the context and thread-local context flag are frozen: m_context, m_useThreadLocalCtx.
      // Do NOT rename these in Phase 4 without updating this comment.
      //
      // CRITICAL LOOP BOUND NOTE (for Phase 4 implementer):
      // Step 4a (source stop + detach): loop over indices 0..kSFXPoolSize-1 = 0..57
      //   (covers evictable SFX + stingers + reserved slot — NOT stream sources at 58..61)
      // Step 4b (EFX filter delete): loop over indices 0..kEvictableSFXCount-1 = 0..54 ONLY
      //   (m_occlusionFilter[] is sized kEvictableSFXCount = 55; accessing [55], [56], [57] is OOB UB)
      // NEVER use kSFXPoolSize or kTotalSources as the EFX filter loop bound.
      // See audio-thread-shutdown.md step 4b for the rationale.
  private:
      IClock* m_clock{nullptr};  // injected at construction; used for crossfade timing and forced-loan gate
      double m_lastDuckWakeTime{0.0};  // tracks when ducking last changed state; Phase 4 duck state machine reads this
      // CRITICAL INITIALIZATION NOTE: The {0.0} default is NOT the operational initial value.
      // Phase 4 MUST initialize: m_lastDuckWakeTime = m_clock->nowSeconds()
      // in audioThreadFunc() AFTER alcSetThreadContext() succeeds and BEFORE
      // the first condition_variable::wait_for. Using 0.0 produces an epoch-sized
      // dt (~1.7e9 s) on the first duck timer update, driving the duck state machine
      // through all transition thresholds instantly.
      // See architecture/audio-architecture/dynamic-soundscape.md.
      // FROZEN MEMBER NAMES (locked in Phase 1; do NOT rename in Phase 4 without updating this comment):
      //   m_clock, m_lastDuckWakeTime
      // NOTE: All remaining member variables (m_device, m_context, m_stopThread, m_audioThread,
      // m_useThreadLocalCtx, m_fnSetThreadCtx, m_duckState, etc.) are Phase 4 additions.
      // See architecture/audio-architecture/audio-system.md for the full canonical member list.
      // Do NOT add AL-typed members here — audio_system.h must include ZERO OpenAL headers.

      // FROZEN MEMBER NAMES for Phase 4 (commented-out; declare here to lock naming):
      // bool m_useThreadLocalCtx{false};  // Phase 4: true iff alcGetProcAddress succeeded
      // ALCcontext* m_context{nullptr};   // Phase 4: owned context handle
      // Do NOT rename these in Phase 4 without updating audio-thread-shutdown.md step 3.5
      // and the SHUTDOWN CONTRACT comment above.
      //
      // using FnSetThreadCtx = int(*)(ALCcontext*);  // Phase 4: LOCAL alias — do NOT use PFNALCSETTHREADCONTEXTPROC
      //   (PFNALCSETTHREADCONTEXTPROC requires <AL/alext.h> which breaks the zero-AL-includes contract of this header)
      // FnSetThreadCtx m_fnSetThreadCtx{nullptr};    // Phase 4: loaded via alcGetProcAddress at AudioSystem construction
      // Also required in Phase 1 stub header: ALCcontext forward-declaration:
      //   struct ALCcontext_struct; using ALCcontext = ALCcontext_struct;
      //
      // SA-3 — m_occlusionGainTarget FROZEN (locked Phase 1):
      // std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount];  // Phase 4
      // MANDATORY: std::atomic<float> is REQUIRED — main thread writes, audio thread reads (concurrent access).
      // A plain float[] would be a C++ data race (UB). Initialize all elements to 1.0f before thread launch:
      //   for (auto& g : m_occlusionGainTarget) g.store(1.0f, std::memory_order_relaxed);
      //
      // PRE-THREAD INITIALIZATION CONTRACT (SA-3, locked Phase 1):
      // m_occlusionGainTarget[] MUST be fully initialized on the MAIN THREAD before the audio
      // thread is launched (before Step 5 of the Phase 4 constructor sequence). The audio thread
      // reads these values at runtime; concurrent access requires std::atomic<float>. Write-after-launch from
      // the main thread also uses std::atomic store. The Phase 4 implementer MUST NOT
      // initialize m_occlusionGainTarget[] inside audioThreadFunc() — it must be set before launch.
      //
      // bool m_duckState{DuckState::IDLE};         // Phase 4; enum class DuckState { IDLE, DUCKING, DUCKED, RELEASING }
      // float m_duckTimer{0.0f};                    // Phase 4: seconds elapsed in current duck phase (audio thread only)
      // float m_duckStartGain{1.0f};               // Phase 4: gain at transition INTO DUCKING; enables smooth ramp from current gain on re-entry
      // std::atomic<float> m_musicDuckGain{1.0f};  // Phase 4: applied multiplicatively to each stem gain
      // bool m_gameOverFade{false};       // Phase 4: game-over audio fade active flag
      // float m_gameOverFadeT{0.0f};      // Phase 4: seconds elapsed in game-over fade (0.0→2.0 s)
  };
  ```

  **CRITICAL — NO OpenAL includes**: `src/audio/audio_system.h` MUST NOT include `<AL/al.h>`, `<AL/alc.h>`, `<AL/alext.h>`, or any OpenAL header — not directly and not transitively. Violation breaks `audio_tests` compilation on CI (OpenAL::OpenAL is not linked to `audio_tests`). All override method stubs and the constructor must be defined inline in the header with empty bodies. No companion `audio_system_stub.cpp` should be created in Phase 1. Private member variable declarations in the stub must use only forward-declarable types — no `ALuint`, `ALCdevice*`, or any AL-prefixed type in the stub header. (ref: `architecture/audio-architecture/audio-system.md`)
- [ ] `src/interfaces/sound_ids.h` (`sound-dev-opensoftal`): defines ALL V1 `SoundId` constants as `constexpr SoundId` named values per the locked SoundId Assignment Table in `architecture/audio-architecture/v1-audio-asset-manifest.md`. Constants must be grouped into clearly commented sections:

  ```cpp
  // Positional SFX (3D — requires AL_SOURCE_RELATIVE = AL_FALSE)
  constexpr SoundId SFX_BUILD_PLACE = 1;
  constexpr SoundId SFX_BUILD_DEMOLISH = 2;
  constexpr SoundId SFX_ROAD_BUILD    = 3;  // 3D positional (world-space road tile); AL_SOURCE_RELATIVE = AL_FALSE
  constexpr SoundId SFX_EARTHWORKS = 4;
  // POSITIONAL (3D): AL_SOURCE_RELATIVE = AL_FALSE (world-space position at tile centroid).
  // EFX filter bypass: AL_DIRECT_FILTER = AL_FILTER_NULL — design choice for outdoor
  // unoccluded construction sounds. EFX bypass does NOT make this non-positional.
  // DO NOT set AL_SOURCE_RELATIVE = AL_TRUE.
  constexpr SoundId SFX_FIRE_ALERT = 11;       // mono positional at building location; ref_dist=15m, max_dist=150m, rolloff=1.5; CRITICAL priority; do NOT bypass EFX
  constexpr SoundId SFX_POLICE_ALERT = 12;     // mono positional at building location; ref_dist=15m, max_dist=150m, rolloff=1.5; CRITICAL priority; do NOT bypass EFX
  // PROHIBITION: SFX_VEHICLE_ENGINE_IDLE and SFX_VEHICLE_ENGINE_MOVE (IDs 13-14) MUST NOT be
  // passed to playPositionalSound() directly. They must be acquired as an atomic pair via
  // AudioSourcePool::acquireVehicleEnginePair() only. Partial acquisition is prohibited.
  constexpr SoundId SFX_VEHICLE_ENGINE_IDLE = 13;
  constexpr SoundId SFX_VEHICLE_ENGINE_MOVE = 14;
  constexpr SoundId SFX_VEHICLE_HORN = 15;
  constexpr SoundId SFX_INTERSECTION_TICK = 16;

  // Zone Loops — OGG Vorbis pre-loaded, managed by zone-layer system ONLY.
  // Do NOT pass these IDs to playSound() or playPositionalSound() directly.
  // Zone loops are acquired and released by AudioSystem's zone-layer manager.
  constexpr SoundId SFX_ZONE_RESIDENTIAL = 17;
  constexpr SoundId SFX_ZONE_COMMERCIAL = 18;
  constexpr SoundId SFX_ZONE_INDUSTRIAL = 19;

  // Non-positional SFX (2D — requires AL_SOURCE_RELATIVE = AL_TRUE, AL_ROLLOFF_FACTOR = 0.0f)
  // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  // All constants in this group set AL_SOURCE_RELATIVE = AL_TRUE and bypass the EFX occlusion filter.
  constexpr SoundId SFX_ZONE_UPGRADE = 5;      // HUD-driven notification event; AL_SOURCE_RELATIVE = AL_TRUE; EFX bypass required
  constexpr SoundId SFX_SERVICE_DEGRADE = 6;   // HUD-driven notification event; AL_SOURCE_RELATIVE = AL_TRUE; EFX bypass required
  constexpr SoundId SFX_BUDGET_WARN = 7;       // WAV PCM, 1–2 s, non-looping one-shot, −24 LUFS / −1 dBTP (RESOLVED: confirmed WAV 1–2 s; the 6 s minimum applies to engine OGG loops only; non-looping one-shots have no loop fatigue concern — see INDEX.md Contradiction #6 resolution, 2026-02-20)
  constexpr SoundId SFX_LOAN_ISSUED = 8;       // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  constexpr SoundId SFX_POWER_OUT = 9;         // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  constexpr SoundId SFX_WATER_OUT = 10;        // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  constexpr SoundId UI_CLICK = 22;             // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  constexpr SoundId UI_TOAST = 23;             // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  constexpr SoundId UI_MENU_OPEN = 24;         // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass
  constexpr SoundId UI_MENU_CLOSE = 25;        // AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass

  // Stingers — trigger via triggerStinger() only, never via playSound()
  // NOTE: Do NOT call playSound(STINGER_CRISIS/MILESTONE, ...) from game logic —
  // stingers must always be triggered via triggerStinger(StingerType::CRISIS/MILESTONE).
  // The playSound path bypasses the duck state machine, the 5-second minimum trigger
  // cooldown, and the non-evictable pool reservation.
  constexpr SoundId STINGER_CRISIS = 20;
  constexpr SoundId STINGER_MILESTONE = 21;
  // Fires at EVERY City Rating tier transition (Village→Town at 1K pop; Town→City at 10K;
  // City→Metropolis at 50K; Metropolis→Megalopolis at 500K).
  // At thresholds that also coincide with a population count milestone, only ONE stinger fires
  // (no double stinger); the population milestone toast still appears.
  // 100K population does NOT trigger this stinger — it is a population toast only (no rating transition).

  // IMPLEMENTATION NOTE for Phase 4 AudioSourcePool construction:
  // Stinger sources (indices 55..56, mapped from StingerType enum values) MUST have
  // the following attributes set at pool CONSTRUCTION (not at acquire time):
  //   AL_SOURCE_RELATIVE = AL_TRUE
  //   AL_POSITION        = {0, 0, 0}
  //   AL_ROLLOFF_FACTOR  = 0.0f
  //   AL_VELOCITY        = {0, 0, 0}
  // Setting these at acquire time creates a race window. See source-pool.md.
  ```

  `sound_ids.h` must also define ALL V1 `MusicTrackId` constants as `constexpr MusicTrackId` named values per the locked MusicTrackId Assignment Table: `constexpr MusicTrackId MUSIC_MAIN_MENU_01 = 1`, `MUSIC_MAIN_MENU_02 = 2`, `MUSIC_CALM_01 = 3`, `MUSIC_CALM_02 = 4`, `MUSIC_GROWTH_01 = 5`, `MUSIC_GROWTH_02 = 6`, `MUSIC_CRISIS_01 = 7`, `MUSIC_CRISIS_02 = 8`. These `MusicTrackId` constants must be stable before Phase 7 wires `setMusicTrack()` calls. Named constants eliminate magic-number SoundIds and MusicTrackIds throughout game code and `EXPECT_CALL` matchers. `sound_ids.h` must include `audio_types.h`. All simulation and UI code that calls `IAudioSystem::playSound()`, `playPositionalSound()`, or `setMusicTrack()` must use named constants from this file — raw integer literals are prohibited. Owner: `sound-dev-opensoftal`; delivery required before Phase 3 `EarthworksCost_Nonzero_FiresAudioCallback` test is written (test uses `EXPECT_CALL(*m_audio, playPositionalSound(SFX_EARTHWORKS, _, _))`).

  **Ambient bed assets exclusion**: Ambient bed assets (`ambient_day`, `ambient_night`, `ambient_dawn`, `ambient_dusk`) are referenced by `AudioSystem` via the `TimeOfDay` enum — they do NOT have `SoundId` constants in `sound_ids.h`. Do NOT define `MUSIC_AMBIENT_*` or `SFX_AMBIENT_*` constants. Phase 4 `AudioSystem::setTimeOfDay()` selects ambient beds via its `TimeOfDay`-to-filename mapping table.

  `sound_ids.h` MUST include an explanatory comment block documenting the non-contiguous ID grouping at the top of the file:

  ```cpp
  // SoundId group layout:
  // IDs 1-4:   Positional SFX (build/road/earthworks)
  // IDs 5-10:  Non-positional SFX (zone_upgrade, service_degrade, budget_warn, loan_issued, power_out, water_out)
  // IDs 11-12: Positional SFX (fire_alert, police_alert)
  // IDs 13-14: Vehicle engine SFX — DO NOT call via playPositionalSound() directly;
  //             must be acquired via AudioSourcePool::acquireVehicleEnginePair() only
  // IDs 15-16: Positional SFX (vehicle_horn, intersection_tick)
  // IDs 17-19: Zone Loops (managed by zone-layer system only — do NOT call playPositionalSound() directly)
  // IDs 20-21: Stingers (managed by triggerStinger() only — do NOT call playSound() directly)
  // IDs 22-25: UI SFX (non-positional)
  // Next available post-V1 ID: 26
  ```

  (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [ ] `src/interfaces/audio_types.h` — **Phase 0 created this file as a stub. Phase 1 EXTENDS and VERIFIES it — not re-authors it from scratch.** Phase 1 verifies it includes `#include <cstdint>` as the FIRST system include (required so `uint32_t` is not pulled in transitively on GCC strict builds), then `#include "vec3.h"` and `#include "camera_state.h"` — do NOT redefine `vec3` or `CameraState` inline in `audio_types.h`; those types are already delivered in Phase 0 as separate headers. Phase 1 verifies or adds the following types: `using SoundId = uint32_t`; `using MusicTrackId = uint32_t`; `using SoundHandle = uint32_t`; `enum class TimeOfDay { DAY, DUSK, NIGHT, DAWN }`; `enum class SoundPriority { LOW = 0, NORMAL = 1, HIGH = 2, CRITICAL = 3 }`. The `StingerType` enum class and the source pool layout constants (`kEvictableSFXCount`, `kStingerCount`, `kSFXPoolSize`, `kStreamSourceCount`, `kTotalSources`) are declared in the canonical order block described below — constants first, then `StingerType` (with enumerators referencing `kEvictableSFXCount`), then `static_assert` statements. Phase 1 also verifies or adds the StingerType pool-index coupling WARNING comment per `architecture/audio-architecture/source-pool.md`: `// WARNING: StingerType enum values (CRISIS=55, MILESTONE=56) are coupled to SFX pool source indices. // Do NOT change these values without updating source-pool.md and AudioSystem::acquireStingerSource(). // StingerType::GAME_OVER is post-V1; sources[57] is idle in V1 (allocated but never acquired). // kStingerCount in source-pool.md is 2 in V1 (CRISIS+MILESTONE only).`

  Phase 1 also adds the following **source pool layout constants** and `StingerType` enum to `audio_types.h` (per `architecture/audio-architecture/source-pool.md`). The declaration order within `audio_types.h` is critical: constants FIRST, enum SECOND, static_asserts THIRD — the `static_assert` statements reference both constants and the enum so both must be visible before the asserts appear.

  ```cpp
  // Declaration order in audio_types.h (enforced):
  // 1. Source pool layout constants (kEvictableSFXCount through kTotalSources)
  // 2. StingerType enum class (references kEvictableSFXCount in its enumerator values)
  // 3. static_assert statements (reference both constants AND StingerType)

  // Step 1: Source pool layout constants
  constexpr int kEvictableSFXCount   = 55;  // sources[0..54]
  constexpr int kStingerCount        = 2;   // V1: sources[55..56] (CRISIS + MILESTONE)
  constexpr int kSFXPoolSize         = 58;  // 55 evictable + 2 stingers + 1 reserved (sources[57])
  constexpr int kStreamSourceCount   = 4;   // sources[58..61] (2 music + 2 ambient beds)
  constexpr int kTotalSources        = 62;  // total alGenSources(62, ...)
  constexpr int kTransientReserveStart = 51;  // acquireSFXSource(): LOW/NORMAL priority limited to [0..50]; HIGH/CRITICAL may use [0..54]
  constexpr int kMaxVehiclePairs     = 12;  // max simultaneous vehicle engine source pairs (24 traffic slots / 2 per vehicle)

  // Step 2: StingerType enum (values derived from kEvictableSFXCount — must come AFTER the constants)
  enum class StingerType {
      CRISIS    = kEvictableSFXCount,      // = 55; maps to sources[55]
      MILESTONE = kEvictableSFXCount + 1,  // = 56; maps to sources[56]
      // GAME_OVER = 57 is post-V1; sources[57] is idle in V1
  };

  // Step 3: static_asserts (reference both constants AND StingerType — must come AFTER both)
  static_assert(kEvictableSFXCount + kStingerCount + 1 + kStreamSourceCount == kTotalSources,
                "Source pool layout constants are inconsistent — update source-pool.md simultaneously");
  static_assert(static_cast<int>(StingerType::CRISIS) == kEvictableSFXCount,
                "StingerType::CRISIS must equal kEvictableSFXCount — update source-pool.md simultaneously");
  static_assert(static_cast<int>(StingerType::MILESTONE) == kEvictableSFXCount + 1,
                "StingerType::MILESTONE must equal kEvictableSFXCount + 1 — update source-pool.md simultaneously");
  static_assert(kTransientReserveStart < kEvictableSFXCount,
                "Transient reserve start must be within the evictable SFX pool range");
  static_assert(kMaxVehiclePairs * 2 <= kEvictableSFXCount,
                "Vehicle pair capacity must not exceed evictable SFX pool");
  ```

  **Phase 4 pool construction MUST use these named constants, not raw literals.** Any layout change triggers an immediate compile error from the `static_assert`.

  Phase 1 also adds `constexpr float kZoneLoopMaxPreloadDurationSeconds = 18.0f;` to `audio_types.h` — this is the enforced authored cap for zone loop assets. `AudioSystem` Phase 4 must assert or log-and-reject zone loop assets exceeding 18 s at load time. The 20 s pre-load tier boundary is the technical limit; 18 s is the authored hard cap.

  `kMaxVehiclePairs = 12` is declared in Step 1 alongside `kEvictableSFXCount` and the companion `static_assert(kMaxVehiclePairs * 2 <= kEvictableSFXCount, ...)` is in Step 3. Phase 4 `AudioSourcePool` uses this constant when enforcing the vehicle pair acquisition cap.

  This prevents post-V1 enum additions that forget to update the pool setup loop. **Do NOT define `SimSpeed` or `SpeedMultiplier` in `audio_types.h`** — `SimSpeed` is defined in `simulation_types.h` (Phase 0 deliverable) which is the canonical owner. Defining `SimSpeed` in `audio_types.h` causes duplicate-type compile errors in any translation unit that includes both `IAudioSystem.h` and `ICitySimulation.h`. No `ALuint`, `ALfloat`, or `AL_*` constants may appear in this file. (ref: `architecture/audio-architecture/audio-system.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/source-pool.md`)
- [ ] **`src/audio/al_check.h` Phase 1 stub** (`sound-dev-opensoftal`): create `src/audio/al_check.h` with two inline no-op stub functions. These are no-op stubs in Phase 1 — the real implementations come in Phase 4 when real AL calls are present. The stub must NOT include any OpenAL headers (same zero-AL-include rule as `audio_system.h`):

  ```cpp
  #pragma once
  // Phase 1 stubs — no-op. Phase 4 replaces with real AL error checking.
  // These functions MUST NOT include <AL/al.h> or <AL/alc.h> — this header
  // must be includable from test translation units that do not link OpenAL.
  // alcCheckError uses void* for the device parameter (NOT ALCdevice_struct*).
  // ALCdevice_struct is a platform-specific incomplete type; using void* avoids
  // a forward-declaration that differs across OpenAL Soft versions and platforms.
  inline void alCheckError(const char* /*op*/) { /* Phase 4: real impl */ }
  inline void alcCheckError(void* /*device*/, const char* /*op*/) { /* Phase 4: real impl */ }
  ```

  **`alcCheckError` call site cast contract (locked Phase 1)**: Phase 4 call sites MUST cast `ALCdevice*` to `void*` at each `alcCheckError` call site — do NOT change the `alcCheckError` signature from `void*` to `ALCdevice*`. Example: `alcCheckError(static_cast<void*>(m_device), "alcMakeContextCurrent");`

  Exit criterion: `src/audio/al_check.h` exists; `audio_system.h` includes it (or it is separately includable); no OpenAL headers pulled in transitively; `alcCheckError` uses `void*` parameter (not `ALCdevice_struct*`). (ref: `architecture/audio-architecture/error-checking.md`)
- [ ] **libvorbisfile and RapidCheck linkage in `audio_tests`**: add `Vorbis::vorbisfile`, `rapidcheck`, and `rapidcheck_gtest` to `audio_tests` `target_link_libraries` now (even though Phase 1 tests are all `SUCCEED()` placeholders). `Vorbis::vorbisfile` ensures Phase 4 OGG tests compile; `rapidcheck`/`rapidcheck_gtest` is proactively linked per Phase 0 policy (all test targets link RapidCheck to avoid retroactive CMakeLists changes). **`audio_tests` include directories** — the Phase 1 CMake amendment must verify that `target_include_directories(audio_tests PRIVATE ...)` includes ALL of: `tests/simulation/`, `src/interfaces/`, `src/audio/`, and `${CMAKE_SOURCE_DIR}`. If any of these are absent from the Phase 0 baseline, the Phase 1 CMake amendment must add them. Rationale: `tests/simulation/` is required so `MockAudioSystem` (which `#include "src/interfaces/IAudioSystem.h"` via project-root-relative path) compiles correctly in the `audio_tests` context; `src/interfaces/` is required for `IAudioSystem.h` and `audio_types.h`; `src/audio/` is required for `audio_system.h` and audio constants; `${CMAKE_SOURCE_DIR}` is required for project-root-relative includes to resolve. (ref: `architecture/ci-cd/dependency-management.md`, `architecture/testing/testability-architecture.md`)
- [ ] **GLEW dependency**: add `glew` (or `glew:x64-windows` on Windows) to `vcpkg.json` as a dependency. GLEW provides the function pointer resolution for `glCompressedTexImage2D` and related calls used in the sRGB raw GL upload path. Without GLEW, the raw GL path will fail to link on both platforms. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] **glew vcpkg port CI verification step** (`cicd-dev-github`): Phase 1 adds a CI verification step to the `build-windows` job that checks the glew vcpkg port is installed before the build begins — e.g., verify the presence of `vcpkg_installed/x64-windows/include/GL/glew.h` or equivalent port artifact. **Linux `build-linux` and `coverage-linux` jobs**: the `apt-get install` step for Linux build dependencies MUST include `libglew-dev` in BOTH the `build-linux` job AND the `coverage-linux` job — `coverage-linux` performs a fully independent build and requires the same system dependencies as `build-linux`. Without `libglew-dev`, the `find_package(GLEW REQUIRED)` CMake call will fail on the Linux build runner. Owner: `cicd-dev-github`.

- [ ] **Linux GLEW artifact verification step** (`cicd-dev-github`): add a Linux GLEW verification step to `build-linux` and `coverage-linux` (after vcpkg install, before CMake configure). The path check MUST use a globbed `find` — **do NOT hard-code the triplet path** (`build/vcpkg_installed/x64-linux/lib/libGLEW.a`). Hard-coded triplet paths may not match manifest-mode vcpkg installs which may use a different triplet directory name. The globbed form is required:

  ```yaml
  - name: Verify glew vcpkg artifact (Linux)
    shell: bash
    run: |
      glew_lib=$(find build/vcpkg_installed -name "libGLEW.a" 2>/dev/null | head -1)
      if [ -z "$glew_lib" ]; then
        echo "ERROR: libGLEW.a not found in vcpkg_installed tree"
        exit 1
      fi
      echo "GLEW static library found: $glew_lib"
  ```

  **Note (CI-1)**: The previous hard-coded triplet path (`build/vcpkg_installed/x64-linux/lib/libGLEW.a`) is incorrect for manifest-mode vcpkg installs that use a different triplet directory name. The globbed `find` form is the correct replacement. The classic-mode fallback (`${VCPKG_ROOT}/installed/x64-linux/include/GL/glew.h`) is also removed — manifest mode is the canonical CI path.

- [ ] **Irrlicht.dll on Windows** (`graphics-dev-irrlicht` + `cicd-dev-github`): add a CMake post-build copy rule for `Irrlicht.dll` to the output directory on Windows. The Windows DLL verification hard-fail for `Irrlicht.dll` is ALREADY implemented in `ci.yml` from Phase 0. The Phase 1 action is to add the CMake post-build copy rule for `Irrlicht.dll` so the existing hard-fail CI step passes. Without the CMake copy rule, the Phase 0 baseline `ci.yml` hard-fail will trigger on every Windows build once `aitown_render` links real Irrlicht. vcpkg defaults to the `x64-windows` (dynamic) triplet on Windows runners, so `Irrlicht.dll` is a required runtime dependency from the moment `aitown_render` first links against Irrlicht in Phase 1. **CO-LANDING REQUIREMENT**: The CMake `add_custom_command(TARGET aitown_render POST_BUILD ...)` to copy `Irrlicht.dll` MUST be authored in the SAME CMakeLists.txt commit as the `target_link_libraries(aitown_render ... Irrlicht ...)` line — they must co-land. The CMake target is `aitown_render` (not `aitown`) — using the wrong target name causes the post-build rule to never fire. Splitting these across commits causes a window where the build links Irrlicht but the DLL is not copied, causing the Phase 0 CI hard-fail to trigger on every Windows build in that window.

  **CI-2 — Irrlicht linkage format by platform**: Irrlicht linkage is platform-conditional in vcpkg:

  - **Linux**: `libIrrlicht.a` (static library) — vcpkg `x64-linux` triplet builds Irrlicht as a static library. No DLL exists on Linux. The DLL verification step in `ci.yml` applies to the `build-windows` job ONLY; do NOT add a Linux `libIrrlicht.a` path check to `build-linux` — the static lib is linked at build time and does not need a separate post-build copy step.
  - **Windows**: `Irrlicht.dll` must be present in `build/Release/` before the test step runs. The post-build copy rule in CMake handles this. DLL verification step is Windows-only.

  (ref: `architecture/ci-cd/dependency-management.md`)
- [ ] **GLEW32.dll Windows CI verification** (`cicd-dev-github`): add `GLEW32.dll` to the Windows DLL verification hard-fail step in `.github/workflows/ci.yml` once the Phase 1 `find_package(GLEW REQUIRED)` deliverable is complete. The exact PowerShell snippet (PS 5.1-compatible) that must be inserted into the DLL verification step is:

  ```powershell
  if (-not (Test-Path "build/Release/GLEW32.dll")) {
    Write-Error "ERROR: GLEW32.dll missing from build/Release/ — post-build copy rule failed"
    exit 1
  }
  Write-Host "GLEW32.dll present."
  ```

  Note: use `if (-not (Test-Path ...)) { exit 1 }` form — `Test-Path ... || exit 1` is PS 7+ only and GitHub Actions Windows runners use PS 5.1. Without this hard-fail, GLEW DLL missing on Windows CI passes silently and the `glewInit()` call crashes at runtime. (ref: `architecture/ci-cd/github-actions-workflow.md`)

  **PRE-PHASE-1 GATING NOTE**: The GLEW32.dll hard-fail PowerShell check is already present in `ci.yml` (added during Phase 1 CI preparation). Until the atomicity commit that co-lands `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)`, the CMake `add_custom_command` post-build DLL copy rule, and the GLEW32.dll PowerShell check is merged, the hard-fail check MUST be downgraded to a warning-only (`Write-Warning` instead of `Write-Error + exit 1`) to prevent all Windows builds from failing before Phase 1 code lands. The atomicity commit restores it to hard-fail. Do NOT permit any Windows CI runs with the hard-fail active but without the DLL copy rule present.

  **IMPORTANT**: Do NOT merge a PR that contains only the warning-only GLEW32.dll check (`Write-Warning` instead of `Write-Error + exit 1`). The warning-only form is for local development branches only. The PR that merges to `develop` or `main` MUST contain both the `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)` CMakeLists addition AND the restored hard-fail PowerShell check in the same commit.

  **CI-CRITICAL — Irrlicht.dll grace window**: The Phase 0 `Irrlicht.dll` hard-fail CI check MUST be downgraded to a warning-only check (using `Write-Warning` instead of `Write-Error` + `exit 1`) on `develop` from Phase 0 completion until the Phase 1 commit co-landing `target_link_libraries(aitown_render PRIVATE Irrlicht)` and the `add_custom_command(TARGET aitown_render POST_BUILD ...)` DLL copy rule is merged. The atomicity commit restoring the `Irrlicht.dll` hard-fail must co-land with the Irrlicht linkage lines — the same atomicity pattern as `GLEW32.dll`. Without this grace window, every `develop` push between Phase 0 completion and the Phase 1 Irrlicht linkage commit will fail Windows CI immediately.

  **ATOMICITY CONSTRAINT — BLOCKING EXIT CRITERION**: The CMake `add_custom_command` post-build copy rule (for GLEW32.dll) and the PowerShell CI hard-fail check MUST be committed in the same commit. Committing the PowerShell check without the CMake copy rule causes all Windows builds to immediately fail; committing the CMake rule without the PowerShell check removes the safety net. These two items are a single atomic unit. **Furthermore, the CMake DLL copy rule for GLEW32.dll AND the PowerShell hard-fail check (`if (-not (Test-Path ...)) { exit 1 }`) MUST be committed in the SAME commit as `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)` — this is a blocking exit criterion; PRs that split these across commits MUST be rejected.**

  **BLOCKING EXIT CRITERION (CI-C2)**: GLEW32.dll check hard-fail reversion confirmed: the final merged `ci.yml` MUST contain `exit 1` (not `Write-Warning`) in the GLEW32.dll DLL verification block. The warning-only form is ONLY permitted in the pre-atomic-commit transient state. This is a **PR rejection criterion** — reviewers must verify the hard-fail form is present before approving Phase 1.
- [ ] **`CityRatingTier` enum** — add to `simulation_types.h` as a Phase 1 deliverable:

  ```cpp
  enum class CityRatingTier { Village, Town, City, Metropolis, Megalopolis };
  ```

  This enum is required by `ICitySimulation.h`'s `getCityRating() const` return type and by `MockCitySimulation`. Defining it in `simulation_types.h` keeps all simulation-facing types in one header.

- [ ] `ICitySimulation.h` in `src/interfaces/` with minimum method signatures required for `UIManager` construction: `setPaused(bool)`, `isPaused() const`, `setSpeed(SpeedMultiplier)`, `getSpeed() const`, `hasUndoPendingAction() const`, `getUndoExpiryTimeSeconds() const`, `CityRatingTier getCityRating() const`, `getDemandPressurePct(ZoneType) const`, `getTreasuryBalance() const`, `getCurrentMonthlyRevenue() const`, `getOutstandingDebt() const`, `estimateMonthlyUpkeep() const`, `getNextUnlockThreshold(Difficulty) const`, `getTotalPopulation() const`, `getConsecutiveDeficitMonths() const`, `getTrafficDemandFactor(ZoneType) const`, `getDensityUnlockState() const`;

  **NOTE — getDemandPressurePct vs getTrafficDemandFactor**: Both methods are present in `ICitySimulation.h` with distinct semantics, as resolved in INDEX.md Resolution 5. `getDemandPressurePct(ZoneType) const` returns the post-combination HUD aggregate demand [0.0, 1.0] (what the player sees in the demand bars); `getTrafficDemandFactor(ZoneType) const` returns the raw traffic-only smoothstep [0.0, 1.0] for Phase 8 save/load serialization. They are semantically distinct and must both be present. See INDEX.md Resolution 5 for the full rationale. `MockCitySimulation` in `tests/ui/mock_city_simulation.h` with corresponding `MOCK_METHOD` declarations, including `MOCK_METHOD(int, getTotalPopulation, (), (const, override))`, `MOCK_METHOD(SpeedMultiplier, getSpeed, (), (const, override))`, `MOCK_METHOD(int, getConsecutiveDeficitMonths, (), (const, override))`, `MOCK_METHOD(float, getTrafficDemandFactor, (ZoneType zone), (const, override))`, `MOCK_METHOD(DensityUnlockState, getDensityUnlockState, (), (const, override))`, and `MOCK_METHOD(CityRatingTier, getCityRating, (), (const, override))` (ref: `architecture/testing/testability-architecture.md`). **`getOutstandingDebt()` must be included in this minimum method list** — it is required by the HUD persistent debt indicator (ref: `architecture/game-design/economy-model.md`). **`getTotalPopulation() const` must be included** — it is required by the HUD City Rating display and population milestone toast system. **`getConsecutiveDeficitMonths() const` must be included** — returns 0 during the grace period; used by `NotificationManager` for progressive bankruptcy warning toasts per `architecture/game-design/game-over-flow.md`. **`getDemandPressurePct(ZoneType) const` must be included** — returns the post-combination, post-floor, post-bootstrap aggregate effective demand for the given zone type as a `float` in [0.0, 1.0]; the HUD demand bars display this value directly. **`getTrafficDemandFactor(ZoneType zt) const` must also be included** — returns the INTERNAL traffic-only smoothstep multiplier in [0.0, 1.0] from the rolling travel-time window, BEFORE bootstrap/floor combination. SEMANTICALLY DISTINCT: `getDemandPressurePct` is the post-combination HUD display value; `getTrafficDemandFactor` is the raw traffic component exposed solely for Phase 8 save/load round-trip serialization. The HUD never reads `getTrafficDemandFactor` directly. Both stub to 0.0f in Phase 1; Phase 3 fills in real implementations. (ref: `implementation/phase-3.md` Traffic demand factor serialization) **`getDensityUnlockState() const` must be included** — returns a `DensityUnlockState` struct (defined in `simulation_types.h`) with `consecutive_months_above_threshold[6]` and `unlock_flags[6]`; describes which density tiers have been unlocked. Phase 1 stub returns a default-constructed `DensityUnlockState{}`. Phase 3 fills in the real implementation. **`enum class Difficulty { Easy, Normal, Hard }` (PascalCase) is defined in `simulation_types.h` (Phase 0 deliverable)** — do NOT redefine `Difficulty` in `ICitySimulation.h` and do NOT use all-caps enumerator names (`EASY`, `NORMAL`, `HARD` do not exist). `ICitySimulation.h` must `#include "simulation_types.h"` to get `Difficulty`, `ZoneType`, `SpeedMultiplier`, and `DensityUnlockState` as complete types. **`ICitySimulation` extends `ISimulationPauser`**: `class ICitySimulation : public ISimulationPauser` — `setPaused(bool)` is inherited, not redeclared; this enables `UIManager` to pass `m_sim` to `NotificationManager` as `ISimulationPauser*`. (ref: `architecture/testing/testability-architecture.md`)

  **Default starting speed**: `CitySimulation` (Phase 3) MUST initialize `m_speed = SpeedMultiplier::x3` per `architecture/game-design/simulation-time.md`. The named constant `kDefaultSimSpeed = SpeedMultiplier::x3` MUST be defined in `simulation_types.h`. In `MockCitySimulation` fixtures for new-game flow tests, use `ON_CALL(mock, getSpeed()).WillByDefault(Return(SpeedMultiplier::x3))` as the correct default — NOT `SpeedMultiplier::Paused`. Setting `Paused` as the default mock return value produces failing tests when UIManager's speed selector initialises from `getSpeed()`.

- [ ] `MockAudioSystem` in `tests/simulation/mock_audio_system.h` with GMock `MOCK_METHOD` declarations for all 11 `IAudioSystem` methods: `playSound`, `playPositionalSound`, `stopSound`, `setMusicTrack`, `setSpeed`, `triggerStinger`, `syncListenerToCamera`, `setGameOverState`, `setTimeOfDay`, `transitionToGameplay`, `update` (ref: `architecture/audio-architecture/audio-system.md`, `architecture/testing/testability-architecture.md`). Required by Phase 3 simulation tests which can run in parallel with Phase 4 — Phase 3 cannot compile without this mock. **CMake include path**: the `ui_tests` CMake target MUST add `tests/simulation/` to its `target_include_directories` so that `mock_audio_system.h`, `mock_renderer.h`, `manual_clock.h`, and `manual_rng.h` are accessible without path qualification. The `audio_tests` target must also add `tests/simulation/` to its include directories for the same reason (ref: `architecture/testing/testability-architecture.md`). The 11-method `IAudioSystem` interface is confirmed aligned with `testability-architecture.md` per INDEX.md resolution 2 — no pre-work required.

  **Compile-smoke requirement**: the compile-smoke test for `MockAudioSystem` MUST include a concrete instantiation: `MockAudioSystem mock;` (or `NiceMock<MockAudioSystem> mock;`). A header-only declaration with no instantiation does not smoke-test the vtable. The test binary must link and the mock must be instantiable.

- [ ] **MockAudioSystem compile-smoke test** (`test-dev-cpp`): create (or verify existence of) `tests/audio/audio_smoke_test.cpp` containing:

  ```cpp
  TEST(AudioSmokeTest, MockAudioSystem_InstantiatesCleanly) {
      NiceMock<MockAudioSystem> mock;
      SUCCEED();
  }
  ```

  Register this file in `add_executable(audio_tests ...)`. Owner: `test-dev-cpp`. Exit criterion: `MockAudioSystem_InstantiatesCleanly` test passes (CTest filter: `-R MockAudioSystem_InstantiatesCleanly`) — confirms all 11 vtable methods are declared and `NiceMock<MockAudioSystem>` is fully constructible. This is a Phase 1 deliverable.
- [ ] `ManualClock` in `tests/simulation/manual_clock.h` (ref: `architecture/testing/testability-architecture.md`). Required by `NotificationManager` testability since `NotificationManager` accepts `IClock*` at construction; Phase 1 `UIManager` shell tests must construct `NotificationManager` with a controllable clock. **`ManualClock` must also be included in the `tests/audio/` CMake target's include directories** (same cross-include pattern as `MockAudioSystem`) — add `tests/simulation/` to `target_include_directories(audio_tests PRIVATE ...)` so Phase 4 audio timing tests can use `ManualClock` without path qualification.
- [ ] `ManualRNG` in `tests/simulation/manual_rng.h`: two independent sequences (`intSeq` and `floatSeq`); strict mode (default) throws `std::logic_error` on sequence exhaustion; validates all `floatSeq` values in [0.0, 1.0) at construction time with a throw (not an assert, so it survives Release builds); non-strict mode wraps around. **`ManualRNG::verifyAllConsumed()` method**: asserts `m_intIdx == m_intSeq.size()` AND `m_floatIdx == m_floatSeq.size()` — over-provisioning (providing more values than consumed) is a test defect that can silently mask regressions where the simulation stops calling `nextFloat()`. Call `rng_.verifyAllConsumed()` in `TearDown()` for any fixture using `ManualRNG` in strict mode. Without this check, tests can pass while hiding correctness regressions. Parallel to `ManualClock`. Required before Phase 3 simulation tests can be written. **`ManualRNG` self-tests** (`test-dev-cpp`): add all 6 required named test cases in `tests/simulation/manual_rng_test.cpp`:
  1. `ManualRNG_VerifyAllConsumed_ThrowsOnOverProvision` — construct `ManualRNG({0, 1}, {0.5f, 0.5f})` (2 ints, 2 floats); call `nextInt(0, 1)` exactly once (leaving the second int unconsumed); call `verifyAllConsumed()` → expect `std::logic_error` to be thrown, confirming that over-provisioning is detected (ref: `architecture/testing/testability-architecture.md`)
  2. `ManualRNG_VerifyAllConsumed_NoThrowWhenFullyConsumed` — construct `ManualRNG({0, 1}, {0.5f, 0.5f})` (2 ints, 2 floats); consume all values (`nextInt` twice, `nextFloat` twice); call `verifyAllConsumed()` → expect no exception to be thrown
  3. `ManualRNG_EmptyIntSeq_ThrowsAtConstruction` — construct `ManualRNG({}, {0.5f})` → expect `std::invalid_argument` at construction time (not at first `nextInt()` call)
  4. `ManualRNG_FloatSeqOutOfRange_ThrowsAtConstruction` — construct `ManualRNG({0}, {1.0f})` → expect `std::out_of_range` at construction time (1.0f not in [0.0, 1.0))
  5. `ManualRNG_EmptyFloatSeq_ThrowsAtConstruction` — construct `ManualRNG({0}, {})` (non-empty int sequence, empty float sequence) → expect `std::invalid_argument` at construction time; covers the `m_floatSeq.empty()` guard in the constructor body, which is distinct from the out-of-range float guard tested in case 4 (ref: `architecture/testing/testability-architecture.md`)
  6. `ManualRNG_NextInt_OutOfRange_ThrowsAtCallTime` — construct `ManualRNG({5}, {0.5f})`; call `nextInt(0, 3)` → expect `std::out_of_range` thrown at call time (tests the call-time range guard in `nextInt()` where the queued value 5 is outside the requested range [0, 3])

  **Fixture initialization note (CRITICAL)**: In ALL fixture definitions that declare a `ManualRNG` member (e.g., `CitySimulationUnitTest`, and any future simulation-layer fixture that injects `ISimulationRNG*`), the member MUST be initialized as `ManualRNG rng_{{0}}` (single-element initializer list with one integer). Using `rng_{}` or `rng_{{}}` passes an empty `initializer_list<int>` and throws `std::invalid_argument` in `SetUp()`, aborting every test in the fixture. This is a silent failure — the test binary compiles but all tests in the fixture are marked as FAILED with a cryptic `std::invalid_argument` message rather than a compile error. NOTE: `AudioSystem` does NOT accept `ISimulationRNG*` — `audio_tests` fixtures must NOT declare `ManualRNG` members.

  **Compile-only stub test for mode-independent deficit counter** (`test-dev-cpp`): add the following compile-only stub test to `tests/simulation/manual_rng_test.cpp` (or a dedicated `tests/simulation/city_simulation_stub_test.cpp` added to `simulation_tests`):

  ```cpp
  // Phase 3 fills in: verify getConsecutiveDeficitMonths() returns 1 after one deficit tick
  // regardless of GameMode injected at construction — counter must NOT be gated on GameMode::Scenario.
  TEST(CitySimulation_DeficitStreakCounter_IncrementsModeIndependently, Phase1Stub) {
      SUCCEED();
  }
  ```

  This stub must compile and pass in Phase 1. Phase 3 replaces `SUCCEED()` with the real assertion.

  **Compile-only stub test for auto-slow mode independence** (`test-dev-cpp`, GD-2): add the following compile-only stub test to `tests/simulation/manual_rng_test.cpp` (or `tests/simulation/city_simulation_stub_test.cpp`):

  ```cpp
  // Phase 3 fills in: verify that auto-slow mode (triggered by deficit streak on month 1)
  // cannot be toggled by setSimulationSpeed() — it is simulation-driven, not player-driven.
  // The test calls setSimulationSpeed() with all four SpeedMultiplier values and confirms
  // that when auto-slow is active, getSpeed() still reflects the simulation-forced speed,
  // not the player-requested speed. This ensures the two control paths are independent.
  // Why SUCCEED() in Phase 1: CitySimulation does not exist until Phase 3; the auto-slow
  // path (which unconditionally applies in both Sandbox and Scenario modes per
  // game-over-flow.md) requires a real CitySimulation tick to activate.
  TEST(CitySimulation_AutoSlowMode_IndependentOfSimSpeed, Phase1Stub) {
      SUCCEED();
  }
  ```

  This stub must compile and pass in Phase 1. Phase 3 replaces `SUCCEED()` with the real assertion.

  **CMake registration is a Phase 1 deliverable** — the `tests/simulation/manual_rng_test.cpp` file is authored AND its CMake registration is applied in Phase 1 by amending the `add_executable(simulation_tests ...)` call inline to include `tests/simulation/manual_rng_test.cpp`.

  **Test-H3 — Phase 0 and Phase 1 `simulation_tests` source file listing**: The Phase 0 `add_executable(simulation_tests ...)` sources that MUST be preserved are:
  - `tests/simulation/smoke_test.cpp` (Phase 0 smoke test)
  - Any other Phase 0 source files present in the original call

  The Phase 1 final form adds:
  - `tests/simulation/manual_rng_test.cpp`

  **Final-form `simulation_tests` CMake (Phase 1 required)**:

  ```cmake
  add_executable(simulation_tests
      tests/simulation/smoke_test.cpp                 # Phase 0 — must be preserved
      tests/simulation/manual_rng_test.cpp            # Phase 1 addition
      tests/simulation/city_simulation_stub_test.cpp  # Phase 1 addition — compile-only CitySimulation stub tests
  )
  ```

  The two `CitySimulation` compile-only stub tests (`CitySimulation_AutoSlowMode_IndependentOfSimSpeed` and `CitySimulation_DeficitStreakCounter_IncrementsModeIndependently`) live in the separate `city_simulation_stub_test.cpp` file, not in `manual_rng_test.cpp`. This separation keeps RNG-specific tests isolated from stub tests that will eventually become full CitySimulation tests in Phase 3.

  **ALL Phase 0 source files already listed in the `add_executable` call MUST be preserved when amending for Phase 1 — verify with `ctest -R Smoke` that Phase 0 smoke tests still pass after the amendment.** Dropping any Phase 0 source file from the `add_executable` call is a silent defect: no compile error occurs, but previously-green tests silently disappear from CTest.

  **CRITICAL: When amending `add_executable(simulation_tests ...)` to add `tests/simulation/manual_rng_test.cpp`, ALL Phase 0 source files already listed in the `add_executable` call MUST be preserved. The amendment APPENDS the new file — it does NOT replace the source list. Dropping any Phase 0 source file from the `add_executable` call is a silent defect: no compile error occurs, but previously-green tests silently disappear from CTest. The implementer must show the final `add_executable` form with both Phase 0 and Phase 1 sources before closing this deliverable.** The preferred pattern per `architecture/testing/framework.md` is the **inline-listing pattern** (all source files listed in `add_executable`); `target_sources()` is also permitted for `simulation_tests` (the prohibition applies only to `opengl_tests`), but the inline-listing pattern is preferred for consistency. The `simulation_tests` target already exists from Phase 0. **Do NOT defer this CMake amendment to Phase 3** — authoring the file without registering it means the tests are never compiled until Phase 3, silently masking `ManualRNG` defects for two full phases. (ref: `architecture/testing/testability-architecture.md`, `architecture/testing/framework.md`)
- [ ] `MockRenderer` in `tests/simulation/mock_renderer.h` returning incrementing non-zero `TextureHandle` values starting from 1 (ref: `architecture/testing/testability-architecture.md`)
  - `TextureHandle` (uint32_t) typedef and `kInvalidTexture = 0` sentinel defined in `IRenderer.h` before the class declaration (matching the `UIElementHandle` pattern in `IUIBackend.h`)
  - `CameraParams` struct defined in the same `IRenderer.h` header (needed by `setCamera()` method)
- [ ] Camera created via `sceneManager->addCameraSceneNode()` only (never FPS/Maya variants); post-creation animator removal loop using grab/drop guard (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] `CameraController` class in `src/ui/`: pan (Middle-mouse + Arrow keys), zoom (scroll wheel), rotate (Right-mouse-button drag only — Q/E NOT bound); pitch clamped to `[-70°, -20°]`; **default window mode is `windowed` (not fullscreen) at 1920×1080 virtual resolution**; edge scrolling ON by default in fullscreen, OFF in windowed; `m_appHasFocus` flag disables edge-scroll on focus loss; constructor must accept `bool startInFullscreen` parameter setting initial `m_edgeScrollEnabled` state; public API must include `bool isEdgeScrollEnabled() const` returning the current value of `m_edgeScrollEnabled` — this accessor is required by test case 6 (`CameraController_EdgeScroll_EnabledByDefaultInFullscreen`) to assert constructor initial state without input injection; edge-scroll activation band is **20 px** from the virtual viewport edge (ref: `architecture/ui-ux/camera-controls.md`, `architecture/asset-standards/3d-model-standards.md`, `architecture/testing/testability-architecture.md`)
- [ ] `src/platform/input_event.h` created with `InputEvent` struct (`Type` enum: `MouseMove`, `MouseButtonDown`, `MouseButtonUp`, `MouseWheel`, `KeyDown`, `KeyUp`, `WindowFocusGained`, `WindowFocusLost`; fields: `x`, `y`, `button`, `wheelDelta`, `keyCode`) as specified in `architecture/testing/testability-architecture.md`; `WindowFocusGained` and `WindowFocusLost` are required for `UIManager` pause-on-alt-tab and input arbitration (edge-scroll disable on focus loss); the concrete Irrlicht `IEventReceiver` adapter in `src/platform/` translates `SEvent` to `InputEvent` before forwarding to `CameraController` and `UIManager`. The concrete Irrlicht `IEventReceiver` adapter must call `UIScaler::unproject(event.MouseInput.X, event.MouseInput.Y)` and store the result in `InputEvent.x`/`InputEvent.y` before forwarding to `UIManager::onEvent()`. `InputEvent.x` and `InputEvent.y` carry virtual 1920×1080 coordinates — not physical pixels — at the point any UI handler receives them. (ref: `architecture/testing/testability-architecture.md`)
- [ ] `CameraController` accepts `InputEvent` struct from `src/platform/input_event.h`; `OnInputEvent()` method replaces `OnEvent(SEvent&)` (ref: `architecture/testing/testability-architecture.md`)

  **NOTE (UX-1 — Drag-delta coordinate space)**: Drag-delta calculations for pan (MMB drag) and rotate (RMB drag) MUST use physical pixel delta — NOT the virtual-space `InputEvent.x`/`InputEvent.y` values, which have been through `UIScaler::unproject()`. The `InputEvent` struct must carry both physical coordinates (for drag-delta computation in `CameraController`) and virtual coordinates (for UI hit-testing in `UIManager`). Camera controller unit tests must document which coordinate space the injected x/y values represent (physical vs virtual). A delta computed from virtual coordinates is incorrectly scaled by the `UIScaler` zoom factor, producing wrong pan/rotate sensitivity at non-native display resolutions. Reference: `architecture/ui-ux/camera-controls.md` (Drag-delta coordinate space section).
- [ ] `CameraController::update(float dt)` calls `camera->setPosition()` and `camera->setTarget()` every frame before `sceneManager->drawAll()` (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)
- [ ] **`NotificationManager` stub class** (`gamedesign-ux`): `src/ui/notification_manager.h` / `notification_manager.cpp`: constructor signature `NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock)`; all public methods declared and defined as no-ops in Phase 1. **Phase 5 signatures locked in Phase 1 even though bodies are Phase 5 deliverables — caller contracts must not change between phases.** **Reason for `ICitySimulation*` (not `ISimulationPauser*`)**: `NotificationManager` needs `getConsecutiveDeficitMonths()` which is defined on `ICitySimulation`, not on the `ISimulationPauser` subset. Using `ISimulationPauser*` would prevent `NotificationManager` from querying the deficit streak. `UIManager` passes `m_sim` (its `ICitySimulation*` member) directly — no cast required. No separate `ISimulationPauser*` member is needed in `UIManager`. The full signatures are:
  - **BEHAVIORAL CONTRACT locked in Phase 1** (Phase 5 implementation required): (1) When the CRITICAL queue transitions from empty to non-empty (first CRITICAL toast arrives), MUST call `m_sim->setPaused(true)` — auto-pause per `notification-system.md`. (2) MUST NOT call `m_sim->setPaused(false)` on CRITICAL toast dismissal — no auto-resume. (3) See `notification-system.md` for combined modal + CRITICAL state auto-resume disambiguation.
  - `void postCritical(const std::string& title, const std::string& body)` — no-op in Phase 1; full implementation Phase 5
  - `void postNormal(const std::string& title, const std::string& body)` — no-op in Phase 1; full implementation Phase 5
  - `void dismissCriticalToast(UIElementHandle handle)` — **must include the `UIElementHandle handle` parameter** (not a zero-argument overload); this is the production API for player-dismissal of CRITICAL toasts per `architecture/testing/testability-architecture.md`; a zero-argument overload is incorrect and will cause a Phase 5 signature mismatch
  - `bool onEvent(const InputEvent& event)` — stub returning `false`; this method is required for input-arbitration Priority 2 (NotificationManager consumes click/Enter/Delete events when a CRITICAL toast is visible); returning `false` in Phase 1 is correct (no CRITICAL toasts exist yet); Phase 5 fills in the real event-consumption logic
  - `bool hasCriticalToastVisible() const` — stub returning `false` in Phase 1; required by `UIManager::onEvent()` Priority 2 guard to evaluate the "no CRITICAL toast visible" condition; Phase 5 fills in the real implementation
  - `void update()` — no-op
  - `void draw()` — it is NOT a no-op. **Two-sided magic number pattern (authoritative)**: The production `notification_manager.cpp` defines `constexpr UIElementHandle kNotifSentinel = 0xDEAD0105u` locally in the `.cpp` file — NOT by including `tests/ui/panel_sentinel_handles.h`. The test file `tests/ui/panel_sentinel_handles.h` declares the matching `constexpr UIElementHandle kNotificationSentinel = 0xDEAD0105u`. This two-sided magic number pattern applies to ALL panel stubs: production `.cpp` files define the sentinel locally; `tests/ui/panel_sentinel_handles.h` mirrors the same value for use in `EXPECT_CALL` matchers. Production code must NEVER include `panel_sentinel_handles.h`. The Phase 1 stub's `draw()` body calls `m_backend->setElementVisible(kNotifSentinel, true)` (using the locally-defined constant); `UIManagerDrawOrderTest` imports `panel_sentinel_handles.h` and uses `kNotificationSentinel` in `EXPECT_CALL` matchers to verify draw ordering. Phase 5 replaces the hardcoded sentinel with real `IUIBackend` toast rendering calls.

  The Phase 5 deliverable fills in the full implementation. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/input-arbitration.md`)
- [ ] **GameMode type prerequisite** (`gamedesign-ux`, `graphics-dev-irrlicht`): before building the `UIManager` shell, verify that `src/ui/ui_types.h` exists and contains `enum class GameMode { Sandbox, Scenario };`. If absent from Phase 0, Phase 1 MUST create it. This file is required because `UIManager` stores `GameMode m_gameMode` and compilation fails without the type definition. `ui_types.h` must be included by `UIManager.h` — a forward declaration is insufficient because `m_gameMode` is stored by value.

- [ ] `UIManager` shell: owns `IGUIEnvironment*`; `GameState` enum (`MainMenu`, `Gameplay`, `Paused`, `GameOver` — V1 canonical; `PostWinFreePlaying` is post-V1 and MUST NOT be added to the V1 enum); `GameOverReason` auxiliary enum deferred to Phase 5 (to distinguish `Bankruptcy` from `ScenarioTimeout`); construction order per `architecture/ui-ux/ui-manager.md`: `NotificationManager` (FIRST — invariant; every subsequent panel may enqueue a notification during its own construction; `NotificationManager` must already be live when that occurs) → `MainMenuPanel` (stub, no functional buttons in Phase 1 — initial visible state; constructed second; after constructing `MainMenuPanel`, the UIManager constructor calls `m_mainMenu->show()` explicitly — NOT `setVisible(true)` — per the `MainMenuPanel show/hide Contract` in `architecture/ui-ux/ui-manager.md`; the Phase 1 `show()` stub body may be empty, but the call site MUST use `show()` so Phase 5 adding reset logic to `show()` does not require a constructor change) → `HUD` → `TaxRatePanel` → `Minimap` → `InspectorPanel` → `PauseMenuPanel` (stub — accepts only `IUIBackend*` in its constructor; exposes `void setSettingsPanel(SettingsPanel*)` no-op setter method; UIManager calls this setter after constructing both panels — **setter pattern is locked; the two-alternative-approaches language is removed**; full implementation Phase 5) → `SettingsPanel` (stub — empty constructor accepting `IUIBackend*`; full implementation Phase 5) → `ModalDialog`; draw order (Z-order sequence in `UIManager::draw()`, exactly 10 slots per `architecture/ui-ux/ui-manager.md`): (1) MainMenuPanel (if visible) → (2) Minimap → (3) HUD (resource bar, toolbar, speed selector) → (4) TaxRatePanel (if visible) → (5) InspectorPanel (if visible) → (6) NotificationManager toast stack → (7) PauseMenuPanel (if visible) → (8) SettingsPanel (if visible) → (9) Background scrim (if modal active — full-screen 50% black overlay, drawn above all HUD/panel elements) → (10) ModalDialog (always topmost). **`BudgetDetailPanel` is NOT a UIManager panel** — it is owned and drawn internally by the `HUD` class; it does NOT appear in UIManager's panel member list or draw-order; UIManager draw-order has exactly 10 slots. **Rationale for Background scrim position**: the scrim must appear above all panels it obscures (Minimap, HUD, TaxRatePanel, InspectorPanel, NotificationManager, PauseMenuPanel, SettingsPanel) but below ModalDialog. Placing scrim at position 2 (before Minimap) is incorrect — it renders behind all the panels it is supposed to darken. Construction order (dependency order) is independent of draw order. **`NotificationManager` MUST be constructed FIRST — this is an invariant, not a suggestion. Placing `MainMenuPanel` or any other panel before `NotificationManager` is a defect.** `NotificationManager` is drawn sixth in Z-order (above InspectorPanel at position 5, below PauseMenuPanel at position 7) but constructed first in dependency order. Implementing draw in construction order would place CRITICAL toasts beneath panels, making them invisible when panels are open. **Note**: `PauseMenuPanel` and `SettingsPanel` are Phase 5 deliverables for full implementation; Phase 1 provides stub classes (empty constructors accepting `IUIBackend*`) in `src/ui/` so `UIManager` compiles. `MainMenuPanel` full implementation is Phase 5. **Required includes**: `UIManager.h` MUST `#include "src/interfaces/LoanTerms.h"` and `#include "src/ui/ui_types.h"` in the Phase 1 shell — forward declarations are insufficient because `LoanTerms` and `GameMode` appear by value/const-ref in method signatures (`showForcedLoanDialog`, `transitionToGameplay`). **All 10 draw-order slots in `UIManager::draw()` MUST be wired in Phase 1**, calling `draw()` on each panel in the correct Z-order even for stub panels. Stub panel `draw()` methods MUST call `m_backend->setElementVisible(kSentinel[PanelName], true)` — they are NOT no-ops (per `UIManagerDrawOrderTest` CONTRACT). Deferring the draw-call wiring to Phase 5 is PROHIBITED — the correct layering contract must be established in Phase 1. **Event dispatch chain** per `architecture/ui-ux/input-arbitration.md`: The Phase 1 `UIManager::onEvent()` shell MUST implement all 6 priority tiers of the event dispatch chain as documented in `architecture/ui-ux/input-arbitration.md`, even though most handlers are no-op stubs. Priority 2 is **skipped entirely** when: (a) no CRITICAL toast is visible, OR (b) a blocking modal is active. When EITHER condition is true, Priority 2 is bypassed entirely — it is a short-circuit OR guard, not an AND. Only when a CRITICAL toast IS visible AND no blocking modal is active does Priority 2 call and return `m_notifications->onEvent(event)`. The full 6-tier chain: (1) Modal — consumes all keyboard/mouse except camera events (MMB/RMB/scroll pass through); (2) CRITICAL toast dismiss — when CRITICAL toast visible and no modal, with the two guards above; (3) QueryPanel/InspectorPanel; (4) TaxRatePanel; (5) HUD controls including SettingsPanel and PauseMenuPanel escape-key handling; (6) pass to game logic. (ref: `architecture/ui-ux/ui-manager.md`, `architecture/ui-ux/input-arbitration.md`)

  **NOTE — Construction vs Draw Order**: Construction order (dependency order) and draw order (Z-order, back-to-front) are INTENTIONALLY DIFFERENT sequences. `UIManager::draw()` MUST NOT iterate panels in construction order. See the 10-slot draw order and the Panel Construction Order in `architecture/ui-ux/ui-manager.md`. A `UIManagerDrawOrderTest` with InSequence enforcement is the exit criterion for correctness.

  **NOTE (UX-3 — NotificationManager `ICitySimulation*` clarification)**: `NotificationManager` does NOT call `getConsecutiveDeficitMonths()` internally. `UIManager::update()` is the exclusive polling bridge (GD-H3 pattern). The `ICitySimulation*` constructor parameter in `NotificationManager` is solely for the auto-pause call `m_sim->setPaused(true)` on the first CRITICAL toast. `NotificationManager` never directly queries deficit streak data — it relies entirely on `UIManager::update()` to evaluate deficit state and call `postCritical(...)` when appropriate. See `architecture/ui-ux/notification-system.md`.

  **NOTE — Toolbar carve-out pixel bounds**: Toolbar carve-out pixel bounds are compile-time `constexpr int` constants in `src/ui/ui_constants.h`, NOT derived via runtime `IUIBackend::getBounds()`. See `architecture/ui-ux/ui-manager.md` for the constant names. The canonical values in **1920×1080 virtual space** are: `kToolbarLeft=8`, `kToolbarRight=72`, `kToolbarTop=64`, `kToolbarBottom=784`. All carve-out constants in `ui_constants.h` MUST be in 1920×1080 virtual space per `architecture/ui-ux/resolution-ui-scaling.md`. Values expressed in 1280×720 physical resolution space (e.g., `kToolbarTop=672`, `kToolbarBottom=720`) are incorrect and will break input arbitration at non-1280×720 display sizes.

  **Priority 2 guard — STRUCTURALLY REQUIRED in Phase 1**: The Priority 2 short-circuit OR guard MUST be structurally implemented in Phase 1 — not deferred to Phase 5. The Phase 1 stub must contain the actual conditional branching:

  ```cpp
  // Priority 2 — structural guard (stub operands are ok; branching is NOT optional)
  const bool noCriticalToast = !m_notifications->hasCriticalToastVisible(); // stubs to true Phase 1
  const bool modalActive = m_modal->isActive();     // Priority 2 guard: m_modal->isActive() (not a null-check — m_modal is always non-null after UIManager construction; isActive() returns true only when a modal dialog is displayed). stubs to false Phase 1
  if (noCriticalToast || modalActive) { /* skip Priority 2 */ }
  else { if (m_notifications->onEvent(event)) return true; }
  ```

  Phase 5 fills in the condition operands; the branching structure is locked in Phase 1.

  **UIManager scrim handle**: `UIElementHandle m_scrimHandle{kInvalidUIElement}` — full-screen 50% black overlay element; created in UIManager constructor; shown/hidden when a modal is active. The Phase 1 `onEvent()` comment block at Priority 1 must document: left-click and right-click are consumed when modal is active; scroll-wheel, MMB drag, and RMB drag are NOT consumed (camera events pass through regardless).

  **PauseMenuPanel wiring — setter pattern (locked)**: PauseMenuPanel Phase 1 stub accepts only `IUIBackend*` in its constructor. It exposes a no-op `void setSettingsPanel(SettingsPanel* settings)` setter method. UIManager calls this setter after constructing both panels. The two-alternative-approaches language is removed — the setter pattern is the locked Phase 5 contract.

  **Priority 5 — SettingsPanel Escape routing structural comment (Phase 1 required)**: The Phase 1 `UIManager::onEvent()` stub MUST include the following structural comment for Priority 5 SettingsPanel Escape routing. The state-conditional form is locked in Phase 1 so Phase 5 adding the real bodies does not require structural changes:

  ```cpp
  // Priority 5 — SettingsPanel Escape routing (structural; bodies Phase 5):
  // if (m_settings->isVisible() && event is Escape) {
  //   if (m_state == GameState::Paused) m_pauseMenu->show();
  //   else if (m_state == GameState::MainMenu) m_mainMenu->show();  // NOT PauseMenuPanel
  //   m_settings->hide();
  //   return true;
  // }
  ```

- [ ] **Minimap, TaxRatePanel, and InspectorPanel stub classes** (`gamedesign-ux`): Phase 1 must deliver stub class files in `src/ui/` for all three panels so that `UIManager` compiles and `UIManagerDrawOrderTest` can exercise all 10 draw slots:
  - `Minimap` stub: `src/ui/minimap.h` / `minimap.cpp` — constructor accepting `IUIBackend*`; public methods `show()` (no-op), `hide()` (no-op), `draw()` (calls `m_backend->setElementVisible(kSentinelMinimap, true)`), **`Rect getBounds() const`** (stub returning a zero-area rect `{0,0,0,0}` in Phase 1; real bounds returned in Phase 5). `Rect` is the struct from `IUIBackend.h` (`struct Rect { int x{0}, y{0}, w{0}, h{0}; }`); include `"IUIBackend.h"`, not any Irrlicht header. Using `irr::core::rect<s32>` as the return type is prohibited here — it pulls Irrlicht headers into `src/ui/` headers, violating testability isolation. `getBounds()` is required by the input arbitration layer to determine if a click falls inside the minimap area — without it, the input arbitration check cannot compile in Phase 1. **NOTE**: The zero-area rect return from `getBounds()` means the minimap carve-out at Priority 3 is always false in Phase 1 — this is intentional stub behavior. Phase 5 replaces the stub with the real Minimap bounds. No Phase 1 test should assert that the minimap carve-out activates. **Phase 5 minimap `getBounds()` return value is specified in `architecture/ui-ux/minimap.md`.**

    **UX-4 — Phase 1 `getBounds()` call site requirement**: Phase 1 MUST include at least one call to `m_minimap->getBounds()` at the Priority 3 input arbitration site in `UIManager::onEvent()`. Even though the return value (a zero-area rect) always causes the carve-out check to be false in Phase 1, the call site MUST exist so `getBounds()` is not dead code at Phase 1 completion. The Priority 3 carve-out check form is: `if (m_minimap->getBounds().contains(event.x, event.y)) { /* pass-through in Phase 1 */ }` — the call must be present even if the body is a stub comment. A `getBounds()` that is never called could be silently dropped by a future refactor.

    **Phase 5 carve-out test stub (Phase 1 exit criterion)**: Phase 1 MUST register the following compile-only stub test in `tests/ui/ui_manager_draw_order_test.cpp` (or a companion `tests/ui/ui_manager_input_test.cpp` added to `ui_tests`):

    ```cpp
    // Phase 5 fills in: verify InspectorPanel_DismissClick_MinimapAreaPassesThrough
    // returns false (pass-through) when Minimap::getBounds() returns real Phase 5 bounds.
    TEST(UIManagerInputArbitrationTest, InspectorPanel_DismissClick_MinimapAreaPassesThrough) {
        SUCCEED();  // Phase 5: real carve-out assertion
    }
    ```

    This stub must compile and pass in Phase 1 as a placeholder confirming test name and fixture compile.
  - `TaxRatePanel` stub: `src/ui/tax_rate_panel.h` / `tax_rate_panel.cpp` — constructor accepting `IUIBackend*`; public methods `show()` (no-op), `hide()` (no-op), `draw()` (calls `m_backend->setElementVisible(kSentinelTaxRatePanel, true)`), **`Rect getBounds() const`** (stub returning a zero-area rect `{0,0,0,0}` in Phase 1; real bounds returned in Phase 5). `Rect` is the struct from `IUIBackend.h`; include `"IUIBackend.h"`, not any Irrlicht header. `getBounds()` is required so `UIManager::onEvent()` Priority 4 bound-checking compiles in Phase 1 with the same pattern as InspectorPanel and Minimap.
  - `InspectorPanel` stub: `src/ui/inspector_panel.h` / `inspector_panel.cpp` — constructor accepting `IUIBackend*`; public methods `show()` (no-op), `hide()` (no-op), `draw()` (calls `m_backend->setElementVisible(kSentinelInspectorPanel, true)`), **`Rect getBounds() const`** (stub returning a zero-area rect `{0,0,0,0}` in Phase 1; real bounds returned in Phase 5). `Rect` is the struct from `IUIBackend.h`; include `"IUIBackend.h"`, not any Irrlicht header. `getBounds()` is required so `UIManager::onEvent()` Priority 3 dismiss-click carve-out can call `m_inspector->getBounds()` in Phase 1. Phase 5 returns the real bounds.

  These are Phase 1 compile targets — full implementations are Phase 5 deliverables. Each stub's `draw()` MUST call `m_backend->setElementVisible(kSentinel[PanelName], true)` using a unique per-panel sentinel constant. The word "no-op" applies only to `show()` and `hide()` stubs — `draw()` is NOT a no-op. Each panel defines a `constexpr UIElementHandle kSentinel[PanelName]` in a test-only header `tests/ui/panel_sentinel_handles.h` (NOT in the production panel header). `UIManagerDrawOrderTest` uses these sentinel handles in `EXPECT_CALL` matchers to verify the correct draw order.
- [ ] **`src/interfaces/LoanTerms.h` stub** (`test-dev-cpp`): `struct LoanTerms { float amount{0.0f}; int repaymentTicks{0}; float interestRate{0.05f}; };` — required because `UIManager.h` includes this header for `showForcedLoanDialog(const LoanTerms& terms)`.
- [ ] **`src/ui/key_bindings.h` stub** (`graphics-dev-irrlicht`, tested by `test-dev-cpp`): this struct/class must contain default values for all hotkeys from `architecture/ui-ux/hotkey-scheme.md`, a `load(const std::string& path)` method stub (no-op body), and an `isReservedKey(const std::string& key)` method stub that returns `true` for "Q" and "E". `CameraController` must accept a `const KeyBindings&` reference rather than hardcoding key codes — injecting `KeyBindings` at construction allows tests to supply custom bindings. **Hardcoding Arrow key codes or tool hotkey codes directly in `CameraController` or `UIManager` is PROHIBITED** — all key lookups must go through `KeyBindings`. Unit tests for `KeyBindings` in `tests/ui/key_bindings_test.cpp`; register by amending `add_executable(ui_tests ...)` to include this file. Three test cases are Phase 1 deliverables: `KeyBindings_IsReservedKey_Q_ReturnsTrue`, `KeyBindings_IsReservedKey_E_ReturnsTrue`, `KeyBindings_IsReservedKey_W_ReturnsFalse`. These tests must be registered under the `ui_tests` CMake target. (ref: `architecture/ui-ux/hotkey-scheme.md`, `architecture/testing/testability-architecture.md`)
- [ ] **HUD class stub** in `src/ui/hud.h` (`gamedesign-ux`): constructor accepts **4 parameters**: `IUIBackend* backend`, `IAudioSystem* audio`, `IClock* clock`, and `ICitySimulation* sim` as non-owning parameters. Signature: `HUD(IUIBackend* backend, IAudioSystem* audio, IClock* clock, ICitySimulation* sim)`. The `IClock*` and `ICitySimulation*` parameters match the undo-button countdown and grace-period indicator dependency from `architecture/ui-ux/hud-layout.md`. The Phase 1 stub body may be empty. ALL FOUR parameters MUST be stored as member variables: `m_audio{nullptr}` (`IAudioSystem*`), `m_clock{nullptr}` (`IClock*`), `m_sim{nullptr}` (`ICitySimulation*`). Phase 5 uses `m_audio` to play `UI_CLICK` and `UI_MENU_OPEN` sounds for toolbar and panel interactions. Phase 5 uses `m_clock`/`m_sim` for undo countdown rendering. Adding `m_audio` now prevents a Phase 5 header change that would force recompilation of `UIManager` and all tests that construct `HUD`. `UIManager` constructs `HUD` with `(m_backend, m_audio, m_clock, m_sim)`. **HUD owns `BudgetDetailPanel` internally** — `BudgetDetailPanel` is a floating overlay triggered by hovering the treasury balance field in the resource bar; it is not a top-level UIManager panel and does not appear in UIManager's draw order (ref: `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/ui-manager.md`, `architecture/testing/testability-architecture.md`).

  **Phase 1 HUD stub header requirements for `BudgetDetailPanel`**: (a) forward-declare `class BudgetDetailPanel;` in `hud.h`; (b) declare `BudgetDetailPanel* m_budgetDetail{nullptr};` as a private member; (c) create companion stub `src/ui/budget_detail_panel.h` with an empty constructor accepting `IUIBackend*` and a no-op `draw()` method. This prevents Phase 5 from requiring a HUD header change (which would force recompilation of UIManager and all consumers).

  **Phase 1 HUD stub: `m_unsavedDotHandle`**: The HUD Phase 1 stub must also declare `UIElementHandle m_unsavedDotHandle{kInvalidUIElement}` as a private member. The dot element is created (hidden) during HUD construction in Phase 1 by calling `m_backend->addStaticText(...)` and storing the returned handle. Phase 5 fills in `show()`/`hide()` logic for the dot. Omitting this member in Phase 1 forces a HUD header change in Phase 5.
- [ ] **`UIManager::showSettings()` stub** (`gamedesign-ux`): add `showSettings()` to the `UIManager` Phase 1 shell method list with a stub body calling `m_settings->show()` (which is a no-op in Phase 1, as `SettingsPanel` is a stub). Required for `MainMenuPanel` to call `UIManager::showSettings()` without a header change in Phase 5. (ref: `architecture/ui-ux/ui-manager.md`)
- [ ] **UIManager Phase 1 required public method stubs** (`gamedesign-ux`): the following public method stubs MUST be declared in `UIManager.h` with empty/no-op bodies in Phase 1, even though their full implementations are Phase 5 deliverables:
  - `void update(float realDeltaSeconds)` — per-frame update; calls `m_notifications->update()` and any other per-frame HUD logic; required alongside draw() in the Phase 1 shell. **Deficit-counter polling bridge (GD-H3)**: `UIManager::update()` is responsible for polling `m_sim->getConsecutiveDeficitMonths()` each budget tick (only when the value changes from last known) and calling `m_notifications->postCritical(...)` with the appropriate progressive warning text. This polling pattern is how `CitySimulation`'s unconditional deficit counter reaches `NotificationManager` without direct coupling. Phase 5 fills in the real body; Phase 1 stub is a no-op. Phase 3 `CitySimulation` implementers MUST NOT call `NotificationManager` directly — `UIManager::update()` is the bridge.
  - `void transitionToPaused()`
  - `void transitionToGameplay_fromPaused()`
  - `void transitionToGameOver()` — **MUST check `m_gameMode == GameMode::Scenario` before transitioning; it is a no-op in Sandbox mode**. This guard MUST be present in the Phase 1 stub body (not deferred to Phase 5). The stub body is: `if (m_gameMode != GameMode::Scenario) return; /* Phase 5: real implementation */`.

    **CRITICAL PROPAGATION RULE**: Only `UIManager::transitionToGameOver()` itself is Sandbox-gated. The `CitySimulation` layer (Phase 3) MUST NOT gate any of the following on game mode:
    - `getConsecutiveDeficitMonths()` counter increments
    - CRITICAL deficit toast dispatches
    - Auto-slow-to-1× call on deficit streak month 1

    These are unconditional in BOTH Sandbox and Scenario modes per `architecture/game-design/game-over-flow.md`. Phase 3 implementers must read game-over-flow.md before implementing the deficit streak logic. The sole Sandbox exemption is calling `transitionToGameOver()` (which triggers the blocking modal) — the counter, toasts, and auto-slow fire in all modes.

  - `void showForcedLoanDialog(const LoanTerms& terms)`
  - `void showGameOverModal(int64_t totalDebt, int monthsInDeficit)`
  - `void closeModal()`

  **Rationale**: these stubs must exist in Phase 1 to lock the Phase 5 constructor and caller contracts. Empty bodies are acceptable. **Do NOT defer these declarations to Phase 5.** Deferring them causes a Phase 5 header change that invalidates any Phase 3 or Phase 4 code that forward-declares or calls these methods. The UIManager header is included by the game loop and by `CitySimulation` event callbacks — a Phase 5 header change would require recompiling all consumers. Phase 1 stubs prevent this. (ref: `architecture/ui-ux/ui-manager.md`)
- [ ] **UIManager `m_hasUnsavedChanges` and `m_lastKnownDeficitStreak` members** (`gamedesign-ux`): add the following to the `UIManager` Phase 1 shell member variable list and method stubs:
  - `bool m_hasUnsavedChanges{false};` private member variable
  - `int m_lastKnownDeficitStreak{-1};` private member variable — sentinel -1 means "never polled"; distinguishes initial state from streak-reset-to-0; ensures first budget tick with any value triggers the transition handler.
  - `void setUnsavedChanges(bool value)` no-op stub method (sets `m_hasUnsavedChanges = value` in Phase 1)

  **UX-3 — `setUnsavedChanges(true)` trigger sites (Phase 1 in-scope vs deferred)**:

  Phase 1 wires exactly ONE trigger site for `setUnsavedChanges(true)`: the zone-placement commit path — called from `ZoneSystem::placeZone()` success path in Phase 3 via `UIManager::setUnsavedChanges(true)`. All other trigger sites (road placement, demolish, service building placement, tax rate change) are Phase 5 deliverables to be wired when the corresponding UI panels have full implementations. Phase 1 does NOT wire these deferred sites — the stub method body is intentionally minimal. Phase 1 MUST document which sites are in-scope so Phase 5 does not silently omit any. In-scope for Phase 1: zone placement commit. Deferred to Phase 5: road placement, demolish, service building placement, tax rate change.

  **Rationale**: `m_hasUnsavedChanges` is read by `PauseMenuPanel` for the Quit-to-Desktop confirmation flow and by `UIManager` for the unsaved-changes dot indicator (HUD resource bar x: 1796–1812 px per `hud-layout.md`). `m_lastKnownDeficitStreak` is polled by `UIManager::update()` each budget tick; the sentinel value -1 (not 0) ensures the first poll with value 0 correctly detects a state transition (a starting value of 0 would match an initial streak-reset-to-0 and silently suppress the first transition handler call). Establishing both members in Phase 1 prevents Phase 5 header changes that would force recompilation of all `UIManager` consumers. (ref: `architecture/ui-ux/hud-layout.md`, `architecture/ui-ux/settings-pause-menu.md`)
- [ ] **`UIManager::transitionToGameplay(GameMode mode)` method signature**: the Phase 1 UIManager shell must declare `transitionToGameplay(GameMode mode)` (not the no-parameter form). `GameMode` is defined in `src/ui/ui_types.h` (Phase 0 deliverable). The mode is stored as `m_gameMode` and checked by `transitionToGameOver()` (which is a no-op in Sandbox mode — `transitionToGameOver()` checks `m_gameMode == GameMode::Scenario` before transitioning). The Phase 1 stub body MUST include `m_hasUnsavedChanges = false` with a comment "Per settings-pause-menu.md: initialized false on new-game start." This line must be in the Phase 1 stub body, not deferred to Phase 5, since it is an invariant of entering the Gameplay state:

  ```cpp
  void UIManager::transitionToGameplay(GameMode mode) {
      m_gameMode = mode;
      m_hasUnsavedChanges = false;  // Per settings-pause-menu.md: initialized false on new-game start.
      // Phase 5: show HUD, hide MainMenuPanel, transition state machine
  }
  ```

  (ref: `architecture/ui-ux/ui-manager.md`, `architecture/ui-ux/settings-pause-menu.md`)
- [ ] `IUIBackend` interface in `src/ui/`: all required methods as listed below — `addStaticText`, `addButton`, `removeElement`, `setElementText`, `setElementVisible`, `isElementVisible`, `setElementEnabled`, `isElementEnabled`, `setElementAlpha`, `setElementImage`, `getElementText`, `getElementRect`, `getScreenWidth`, `getScreenHeight`, `getVirtualWidth() const`, `getVirtualHeight() const`, `loadTexture(const std::string& path)` — **17 methods total (including `loadTexture()`)**. The two `getVirtual*()` methods return 1920 and 1080 respectively in V1, providing the canonical virtual resolution to any component querying the backend. `loadTexture()` loads a texture from disk and returns an opaque `UIElementHandle` that can be passed as the second argument to `setElementImage()`; returns `kInvalidUIElement` on failure. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/ui-manager.md`). **`IUIBackend.h` MUST be placed in `src/ui/` (NOT `src/interfaces/`). This is the only interface that lives outside `src/interfaces/`. Reason: `IUIBackend.h` is placed in `src/ui/` because it is part of the UI subsystem abstraction boundary — it defines the contract between `UIManager` and its rendering backend. All other shared cross-subsystem interfaces live in `src/interfaces/`. This placement decision is documented in `architecture/testing/testability-architecture.md` and confirmed in `architecture/ui-ux/ui-manager.md`.** `IrrlichtUIBackend` stub in `src/rendering/` must provide no-op stub implementations for ALL 17 of these methods (not just a subset) — even methods that are not called in Phase 1 must be stubbed so `IrrlichtUIBackend` compiles as a concrete class (ref: `architecture/testing/testability-architecture.md`).
- [ ] `UIElementHandle` (uint32_t), `kInvalidUIElement = 0`, `Rect` struct defined before `IUIBackend` in `IUIBackend.h` (ref: `architecture/testing/testability-architecture.md`)
- [ ] `IrrlichtUIBackend` in `src/rendering/` with `std::unordered_map<UIElementHandle, IGUIElement*>` (ref: `architecture/testing/testability-architecture.md`)
- [ ] `MockUIBackend` in `tests/ui/mock_ui_backend.h` (ref: `architecture/testing/testability-architecture.md`)
- [ ] `UIScaler` with constructor signature `UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY)` — six parameters specifying the virtual coordinate space dimensions (1920×1080), the active viewport dimensions, and the letterbox/pillarbox pixel offsets. Tests construct `UIScaler(1920, 1080, 1280, 720, 0, 90)` directly to validate coordinate projection and letterbox offset math without a display. `getViewportRect()` exposed. Mouse un-projection formula: `virtual_x = (actual_x - letterbox_offset_x) × (1920.0 / viewport_width)`; `virtual_y = (actual_y - letterbox_offset_y) × (1080.0 / viewport_height)`. **`UIScaler` must expose `VirtualPoint unproject(int physicalX, int physicalY) const;` as a public method, where `VirtualPoint` is a nested struct `{ int x; int y; }` declared INSIDE `UIScaler` — use `UIScaler::VirtualPoint`. Do NOT declare `VirtualPoint` at namespace scope (ODR risk).** **`UIScaler` unit tests** (`test-dev-cpp`): five named unit tests in `tests/ui/ui_scaler_test.cpp` (registered via `target_sources(ui_tests PRIVATE tests/ui/ui_scaler_test.cpp)`). **Note**: `target_sources()` is PERMITTED for `ui_tests` — the prohibition on `target_sources()` applies only to `opengl_tests` (per `architecture/testing/framework.md`). Alternatively, amend the `add_executable(ui_tests ...)` call inline — the inline-listing pattern is preferred. Either approach is acceptable for `ui_tests`.
  1. `UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly`: construct with `(1920,1080,1280,720,0,90)`, pass mouse point `(640,450)`, assert virtual coords `(960,720)`
  2. `UIScaler_FullNative_NoOffset_ProjectsIdentity`: construct with `(1920,1080,1920,1080,0,0)`, any pixel maps to itself
  3. `UIScaler_PillarboxOffset_UnprojectsCenterCorrectly`: construct with pillarbox `(1920,1080,1440,1080,240,0)`, center pixel `(960,540)` → virtual `(960,540)`
  4. `UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero`: construct `UIScaler(1920, 1080, 1280, 720, 0, 90)`; call `unproject(640, 80)` — `physical_y=80` falls within the top black bar (0–89 px, above the active viewport), producing a negative pre-clamp `virtual_y`; assert virtual_y == 0 AND virtual_x == 960 (X is in-viewport and projects normally: `(640 - 0) × (1920.0/1280) = 960`; only the Y-axis is affected by the letterbox clamp). **Do NOT assert virtual_x = 0** — only the out-of-bounds axis is clamped.
  5. `UIScaler_GetViewportRect_ReturnsCorrectOffsets`: construct with `(1920, 1080, 1280, 720, 0, 90)`; `getViewportRect()` returns `{x:0, y:90, w:1280, h:720}`
  6. `UIScaler_MouseInBottomBlackBar_VirtualY_ClampedToMax` — **Phase 1 compile-only stub**: construct `UIScaler(1920, 1080, 1280, 720, 0, 90)`; test body is `SUCCEED()`. Phase 5 fills in: call `unproject(640, 820)` — `physical_y=820` falls below the active viewport (viewport ends at y=90+720=810), producing a pre-clamp `virtual_y > 1080`; assert `virtual_y == 1080` (clamped to max) AND `virtual_x == 960`.

  Tests 1–5 must pass before Phase 2 begins. Test 6 is a Phase 1 compile-only stub; the real assertion is a Phase 5 deliverable. (ref: `architecture/ui-ux/resolution-ui-scaling.md`, `architecture/testing/testability-architecture.md`)
- [ ] `WallClock` implementation in `src/platform/WallClock.cpp` — the `src/interfaces/WallClock.h` stub was delivered in Phase 0 (note: use uppercase `WallClock.h`, not `wall_clock.h`). The Phase 1 `.cpp` file adds the `nowSeconds()` body using `std::chrono::steady_clock`, returning elapsed seconds as a `double` since an arbitrary start point. (ref: `architecture/testing/testability-architecture.md`)
- [ ] `NullSimulationPauser` in `src/interfaces/null_simulation_pauser.h` — no-op `ISimulationPauser` implementation for contexts where pausing is not needed (e.g., standalone tool or test contexts that do not require pause side-effects). (ref: `architecture/testing/testability-architecture.md`)
- [ ] `MockSimulationPauser` in `tests/ui/mock_simulation_pauser.h` with `MOCK_METHOD(void, setPaused, (bool), (override))`. Required as a general-purpose stub for `ISimulationPauser*` parameters. **Phase 5 `NotificationManager` auto-pause tests use `MockCitySimulation` (NOT `MockSimulationPauser`)**: `NotificationManager` takes `ICitySimulation*` — not `ISimulationPauser*` — so `getConsecutiveDeficitMonths()` can be queried; the three `NotificationManager` auto-pause test cases (`CriticalToast_OnPost_AutoPausesCalled`, `CriticalToast_OnLastDismiss_NoAutoResume`, `CriticalToast_SecondPost_NoDoublePause`) must inject `MockCitySimulation` (which inherits `ISimulationPauser` via `ICitySimulation : public ISimulationPauser`). `MockSimulationPauser` is used only for contexts where a bare `ISimulationPauser*` is needed (not for `NotificationManager`). **Phase boundary note**: the three `NotificationManager` auto-pause test cases are Phase 5 deliverables — only the `MockSimulationPauser` stub and its CMake registration are Phase 1 deliverables. Do NOT author test bodies in Phase 1 for these cases; they require the Phase 5 `NotificationManager` implementation to pass. (ref: `architecture/testing/testability-architecture.md`)
- [ ] **`ui_tests` CMake target — consolidated final form (Test-C1)**: The Phase 1 `add_executable(ui_tests ...)` call MUST list ALL Phase 1 sources in the final consolidated form. This consolidated form must be committed as a single amendment. The prior single-file form (`tests/ui/camera_controller_test.cpp` only) is REPLACED, not appended to incrementally. Final consolidated form:

  ```cmake
  add_executable(ui_tests
      tests/ui/camera_controller_test.cpp
      tests/ui/ui_scaler_test.cpp
      tests/ui/key_bindings_test.cpp
      tests/ui/ui_manager_draw_order_test.cpp
      tests/ui/ui_manager_modal_test.cpp
  )
  ```

  **APPEND-only warning**: ALL Phase 1 source files listed in the `add_executable` call MUST be preserved when amending for any subsequent phase — verify with `ctest -R CameraController` that Phase 1 camera tests still pass after any amendment. Dropping any Phase 1 source file from the `add_executable` call is a silent defect. (ref: `architecture/testing/framework.md`)

- [ ] **`ui_tests` CMake target include directories and linkage**: `ui_tests` links `aitown_ui GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest`; registered via `aitown_add_tests(ui_tests LABEL "unit")`; include directories as specified by `architecture/testing/testability-architecture.md`:

  ```cmake
  target_include_directories(ui_tests PRIVATE
      tests/simulation/ tests/ui/ src/interfaces/ src/ui/ ${CMAKE_SOURCE_DIR})
  ```

  This enables `MockAudioSystem`, `MockRenderer`, `ManualClock`, `ManualRNG` access (from `tests/simulation/`), `MockUIBackend`, `MockCitySimulation`, `MockSimulationPauser` (from `tests/ui/`), interface headers (from `src/interfaces/`), and `IUIBackend.h` (from `src/ui/`). `rapidcheck` and `rapidcheck_gtest` are linked proactively per Phase 0 policy (all test targets link RapidCheck to avoid retroactive CMakeLists changes). Phase 5 adds additional source files to this target. **NOTE**: The single-file `add_executable(ui_tests tests/ui/camera_controller_test.cpp)` stub form is REPLACED by the 5-file consolidated form (Test-C1) above — do NOT use the single-file form. For `ui_tests`, the Phase 1 amendment MUST use the consolidated `add_executable` form from Test-C1 — `target_sources()` is PROHIBITED for this Phase 1 amendment. The permission for `target_sources()` referenced elsewhere applies to future phases only. (ref: `architecture/testing/framework.md`, `architecture/testing/testability-architecture.md`)
- [ ] Camera controller unit tests in `tests/ui/camera_controller_test.cpp`: pitch clamping, pan/zoom/rotate events, edge-scroll enable/disable. The following 6 named test cases are REQUIRED and must pass before Phase 2 begins (ref: `architecture/testing/testability-architecture.md`):
  1. `CameraController_PitchClamp_AtUpperBound_ExactlyMinus20` — inject a sequence of `MouseWheel` events driving pitch above −20°; assert `getCameraState().pitch == -20.0f` using `EXPECT_FLOAT_EQ` (exact equality — bound is inclusive; not just `EXPECT_LT`)
  2. `CameraController_PitchClamp_AtLowerBound_ExactlyMinus70` — inject a sequence of `MouseWheel` events driving pitch below −70°; assert `getCameraState().pitch == -70.0f` using `EXPECT_FLOAT_EQ` (exact equality — bound is inclusive; not just `EXPECT_GT`)
  3. `CameraController_EdgeScroll_DisabledOnFocusLoss` — record initial `getCameraState().position`; inject `InputEvent{Type::WindowFocusLost}`; inject `InputEvent{Type::MouseMove, x=0, y=540}` (cursor at left edge in 1920-wide virtual space, which would normally trigger left edge-scroll); assert `getCameraState().position` is unchanged, confirming `m_appHasFocus = false` suppresses edge-scroll
  4. `CameraController_RightMouseRotate_MovesYaw` — record initial yaw via `getCameraState().yaw`; inject `InputEvent{Type::MouseButtonDown, button=1}` (right mouse) then `InputEvent{Type::MouseMove, x=prevX+10, y=prevY}`; assert `getCameraState().yaw != initialYaw` (exact delta is implementation-defined; test verifies directional sensitivity)
  5. `CameraController_MiddleMousePan_MovesPosition` — record initial position via `getCameraState().position`; inject `InputEvent{Type::MouseButtonDown, button=2}` (middle mouse) then `InputEvent{Type::MouseMove, x=prevX+5, y=prevY}`; assert `getCameraState().position` differs from initial position (at least one component changed)
  6. `CameraController_EdgeScroll_EnabledByDefaultInFullscreen` — construct `CameraController` with `startInFullscreen=true`; assert `isEdgeScrollEnabled() == true` without injecting any events
  7. `CameraController, EdgeScrollActivatesAt20pxBand` (**Phase 1 stub — compile only — windowed mode**): construct `CameraController` in windowed mode (`startInFullscreen=false`); this test stub verifies the 20 px edge-scroll activation band. **Note: edge scrolling tests 7 and 8 are windowed-mode-only tests — "windowed-mode-only" means these tests exercise behavior that only differs when `startInFullscreen=false`. In fullscreen mode, edge scrolling is always active and the band-activation tests do not apply. The "windowed-mode-only" label does NOT mean the test is skipped in headless CI. Both tests MUST pass (with body `SUCCEED()`) in headless CI — the compile-only form runs without a display and does not exercise any input injection.** Phase 1 body is `SUCCEED()` — a compile-only placeholder confirming the test name and fixture compile. Phase 5 fills in the real assertion: inject `setEdgeScrollEnabled(true)`, then inject a `MouseMove` event with `x = 19` (inside the 20 px left band), assert the camera position changes; then inject `x = 20` (boundary), assert camera position changes; then inject `x = 21` (outside band), assert position unchanged.
  8. `CameraController, EdgeScrollDisabledByDefaultInWindowed` (**Phase 1 stub — compile only — windowed mode**): construct `CameraController` with `startInFullscreen=false`; assert `isEdgeScrollEnabled() == false` without injecting any events. **Note: this test applies only to windowed mode — "windowed-mode-only" refers to the behavior under test, not a CI skip condition. The test MUST still pass in headless CI (where there is no window manager); the Phase 1 `SUCCEED()` body contains no input injection and runs correctly without a display.** Phase 1 body is `SUCCEED()` — a compile-only placeholder; Phase 5 fills in the real assertion.

  Tests 1–6 must pass as Phase 1 exit criteria. Tests 7–8 are Phase 1 compile-only stubs; the real assertions are Phase 5 deliverables. (ref: `architecture/ui-ux/camera-controls.md`, `architecture/testing/testability-architecture.md`)
- [ ] **`UIManagerDrawOrderTest` fixture — additional test cases (Test-C2, Test-2)**: Two additional `UIManagerDrawOrderTest` test cases are Phase 1 deliverables:

  1. `DrawOrder_ModalActive_ScrimAndModalFireAfterPanels` (Test-C2): In this test, the modal is explicitly activated before `ui_->draw()` is called, making `EXPECT_CALL` expectations for `kScrimSentinel` (slot 9) and `kModalSentinel` (slot 10) non-vacuous. The `InSequence` constraint confirms they fire AFTER `kNotificationSentinel` (slot 6), `kPauseMenuSentinel` (slot 7), and `kSettingsSentinel` (slot 8). Required for slots 9 and 10 coverage.

  2. `DrawOrder_PauseMenuVisible_SlotSevenFiresAfterNotification` (Test-2): Phase 1 body calls `ui_->transitionToPaused()` (a no-op stub) and verifies via `InSequence` that `kPauseMenuSentinel` fires after `kNotificationSentinel`. Phase 5 fills in the real body. This test ensures draw-order slots 7 (PauseMenuPanel) and 8 (SettingsPanel) have compile-time coverage that prevents silent ordering regressions from Phase 1 through Phase 5.

  **`ModalDialog` Phase 1 stub `m_active` requirement (Test-H3)**: `ModalDialog` Phase 1 stub MUST store `bool m_active{false}`; `show()` sets `m_active = true`; `isActive()` returns `m_active`. This is required so `DrawOrder_ModalActive_ScrimAndModalFireAfterPanels` can activate the modal (e.g., via `UIManager::showForcedLoanDialog(LoanTerms{})`) and have `m_modal->isActive()` return `true` during `draw()`, making draw slots 9 and 10 expectations non-vacuous. A Phase 1 stub `ModalDialog` with no `m_active` member and an `isActive()` that always returns `false` causes this test to be vacuously green — it provides no ordering guarantee for slots 9 and 10.

  **NOTE (UX-2 — ModalDialog constructor signature)**: The `ModalDialog` Phase 1 stub constructor MUST accept `ICitySimulation*` as a parameter (stored as `m_sim{nullptr}`). Declare `bool m_didPauseSim{false}` as a private member. Stub `show()` body: sets `m_active = true` only (real auto-pause is a Phase 5 deliverable). Stub `closeModal()` body: sets `m_active = false` only (real `m_didPauseSim` unwind is a Phase 5 deliverable). `UIManager` passes `m_sim` to `ModalDialog` at construction: `m_modal = new ModalDialog(m_backend, m_sim)`. Locking the constructor signature now prevents a Phase 5 header break to `UIManager`.

- [ ] **`UIManagerDrawOrderTest` fixture** (`test-dev-cpp`): a `UIManagerDrawOrderTest` fixture using **`NiceMock<MockUIBackend>`** (NOT `StrictMock`) with GMock `InSequence` to verify all 10 draw slots are called in the correct Z-order sequence when `UIManager::draw()` is invoked. **Mock policy for `UIManagerDrawOrderTest`: NiceMock for ALL mock panels and the backend mock.** Reason: during construction, panels may call `m_backend->addStaticText()` or other backend methods that are not in the `InSequence` EXPECT_CALL chain. `StrictMock` fails on any unexpected call; `NiceMock` ignores unexpected calls and only enforces the explicit `EXPECT_CALL` expectations. Using `StrictMock<MockUIBackend>` here would cause spurious test failures whenever a panel stub constructor calls a backend method not listed in the test's `EXPECT_CALL` setup. Even with stub panels whose `draw()` bodies are no-ops, `UIManager::draw()` must delegate to each panel in order — the test verifies the delegation chain. Register under `ui_tests` with label `unit`. (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/ui-manager.md`)

  **CONTRACT**: Each panel stub's `draw()` implementation MUST call `setElementVisible(kSentinelHandle, true)` (where `kSentinelHandle` is a fixed dummy `UIElementHandle` constant defined in the panel stub) as the observable `IUIBackend` call that proves draw ordering. The `InSequence` `EXPECT_CALL` setup must expect `setElementVisible()` calls in the correct back-to-front order for all 10 draw slots. Without this observable call, `InSequence` enforcement is vacuously satisfied and the test provides no actual ordering guarantee.

  **IMPORTANT**: Panel stub `draw()` methods must NOT be no-op bodies. Each panel stub's `draw()` MUST call `m_backend->setElementVisible(kSentinelHandle, true)` using a UNIQUE per-panel `UIElementHandle` sentinel constant. A no-op `draw()` vacuously satisfies all InSequence constraints — the test provides no ordering guarantee when draw() calls are absent. This is enforced by the CONTRACT requirement in `architecture/testing/testability-architecture.md`.

  **Sentinel placement**: Sentinel constants live in a test-only header `tests/ui/panel_sentinel_handles.h` — NOT in any production panel header. This prevents test scaffolding from leaking into production code. The `EXPECT_CALL` matchers in `UIManagerDrawOrderTest` match `setElementVisible(kPanel[X]Sentinel, true)` for each panel's sentinel, making the `InSequence` enforcement non-vacuous.
- [ ] LOD smoke test infrastructure: `tests/rendering/lod_swap_smoke_test.cpp` with test name `LODSwapSmokeTest.SetMeshGrabDropContract`, CMake registration labelled `requires-opengl`, registered via the `opengl_tests` target. Use `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp tests/rendering/lod_swap_smoke_test.cpp)` — amend the `add_executable` call in-place. Do NOT use `target_sources()` for adding files to `opengl_tests`. **Irrlicht-4 — The Phase 1 test body MUST use `GTEST_SKIP()`** with the following exact comment (NOT `SUCCEED()`):

  ```cpp
  TEST(LODSwapSmokeTest, SetMeshGrabDropContract) {
      // Timing measurement requires a real GPU; this test is promoted to
      // `requires-opengl` label in Phase 2 when the real LOD swap is implemented.
      // Phase 1 stub asserts only the API contract (setMesh is called), not the timing.
      // The "> 2ms is HARD BLOCKING" timing constraint is enforced by profiling in Phase 2
      // using ASAN + release build timing, not by this unit test.
      GTEST_SKIP() << "LOD swap timing requires real GPU; promoted to Phase 2.";
  }
  ```

  **Why `GTEST_SKIP()` instead of `SUCCEED()` (Irrlicht-4 rationale)**: `GTEST_SKIP()` correctly communicates that this test is deferred pending GPU availability, not vacuously passing. CTest counts a SKIPPED test as passing the gate (it does not fail CI) but makes the deferral visible in test reports. `SUCCEED()` falsely implies the timing constraint has been verified. The spec's concrete body (from `scene-graph-ownership.md`) with `addMeshBuffer()` calls MAY have a memory leak depending on the spike result; do not commit the real body until the spike confirms the grab/drop contract. If the spike is resolved within Phase 1, the real body may be filled in before Phase 1 exit; Phase 2 fills in the real body.

  **LOD spike contingency branch**: If the spike reveals `CMeshSceneNode::setMesh()` does NOT call `grab()` on the new mesh: (a) `architecture/graphics-architecture/scene-graph-ownership.md` must be corrected to remove the `drop()` call after `setMesh()`; (b) `lod_swap_smoke_test.cpp` must verify via ASAN that the mesh survives without the caller's `drop()`; (c) this finding BLOCKS Phase 2 `TerrainChunk` implementation until `scene-graph-ownership.md` is updated. The Phase 1 spike exit criterion must explicitly confirm either: (a) `grab()` IS called and Phase 2 may use the documented `drop()` pattern, or (b) `grab()` is NOT called and the pattern has been corrected.

  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/testing/framework.md`)
- [ ] **LOD spike Checkbox A**: `SMesh::addMeshBuffer()` grab/drop contract verified by inspecting vendored Irrlicht source (`SMesh.h`); result recorded as a one-line comment in `tests/rendering/lod_swap_smoke_test.cpp`. If `addMeshBuffer()` calls `grab()`, caller must `drop()` after; if not, caller must not `drop()`. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)
- [ ] **LOD spike Checkbox B**: `CMeshSceneNode::setMesh()` grab/drop contract verified by inspecting `CMeshSceneNode.cpp`; result recorded in `architecture/graphics-architecture/scene-graph-ownership.md`. **Phase 2 TerrainChunk work is BLOCKED until Checkbox B is ticked, not Checkbox A.** If `setMesh()` does NOT call `grab()`, remove the `drop()` call from the LOD swap sequence in `scene-graph-ownership.md` before any Phase 2 code uses `node->setMesh()`. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)
- [ ] `integration_tests` CMake target created; initial source file `tests/integration/irrlicht_ui_backend_test.cpp` contains a single `EDT_NULL` test that constructs `IrrlichtUIBackend` with an `EDT_NULL` device and calls `addStaticText()`, asserting a non-zero handle is returned; registered via `aitown_add_tests(integration_tests LABEL "integration")`. CMake configuration per `architecture/testing/framework.md`:

  ```cmake
  target_link_libraries(integration_tests PRIVATE
      aitown_render aitown_ui
      GTest::gtest_main GTest::gmock
      rapidcheck rapidcheck_gtest)
  target_include_directories(integration_tests PRIVATE
      tests/simulation/ tests/ui/ src/interfaces/ src/ui/ src/rendering/ ${CMAKE_SOURCE_DIR})
  ```

  **`src/rendering/` is included** — `IrrlichtUIBackend.h` is in `src/rendering/`; without this path the integration test fails to compile. **The existing Phase 0 `integration_tests` `target_link_libraries` must be amended in Phase 1 to add `aitown_render aitown_ui` — without these, `IrrlichtUIBackend` construction will not link. Without this change, the `irrlicht_ui_backend_test.cpp` compilation will fail.**

  (ref: `architecture/testing/framework.md`, `architecture/testing/testability-architecture.md`)
- [ ] `coverage-linux` job in `ci.yml` contains the `src/ui/` 25% coverage gate step using the exact awk/lcov invocation from `architecture/ci-cd/github-actions-workflow.md`. The step MUST use `exit 1` (never `|| echo WARNING`). This must be committed to ci.yml before Phase 1 closes. The coverage gate script MUST use float-aware awk comparison (not Bash integer `-lt`) as follows.

  **`coverage-linux` independence note**: `coverage-linux` is a fully independent job — it does NOT inherit environment variables from `build-linux`. The compiler-detect step (`echo "COMPILER_VERSION=$(gcc -dumpfullversion -dumpversion)" >> $GITHUB_ENV`) MUST be present in `coverage-linux` as its own named step, appearing BEFORE the `actions/cache` step, identical to how `build-linux` does it.

  **CI-3 — Compiler-version detect step ordering (blocking)**: In BOTH `build-linux` AND `coverage-linux`, the compiler-version detect step that writes `COMPILER_VERSION` to `$GITHUB_ENV` MUST appear as a **separate named step BEFORE the `actions/cache` step**. This is required because `$GITHUB_ENV` writes are NOT visible within the same step — the value is only available to subsequent steps in the job. If the compiler-detect and `actions/cache` steps are combined in a single step, the cache key will silently use an empty `COMPILER_VERSION` and the cache will miss on every build. If the current `ci.yml` shows them combined or in the wrong order, this MUST be corrected before Phase 1 closes. Correct step order: (1) Detect compiler version → write to `$GITHUB_ENV`; (2) `actions/cache` step using `${{ env.COMPILER_VERSION }}` in the cache key.

  **`--ignore-errors unused` mandate (H29 — BLOCKING)**: The `lcov --remove` invocation in `coverage-linux` MUST include `--ignore-errors unused` to prevent lcov 2.x exit-25 failures on patterns that match no files in Phase 1 (e.g., `*/src/audio/*` matches nothing until Phase 4). Omitting this flag causes the `coverage-linux` job to fail on every Phase 1 build. Verify `--ignore-errors unused` is present in the committed `ci.yml` before Phase 1 closes. This is a **blocking exit criterion** — Phase 1 CANNOT close without it.

  ```bash
  # Verify that the least-covered src/ui/ file meets the 25% Phase 1 baseline.
  #
  # Format dependency: Assumes lcov 2.x --list output uses '|' as column delimiter.
  # If the format changes, $NF+0 coercion produces 0 -> gate FAILS with misleading
  # '0% coverage' message rather than 'lcov format mismatch'. Validate the lcov
  # --list output format manually if the pipeline produces unexpected results.
  #
  # grep -v "^Total": exclude the summary Total row so only per-file rows are evaluated.
  #
  # head -1 semantics: head -1 takes the minimum (worst-case) src/ui/ file coverage
  # — this is intentional; the gate enforces that even the least-covered src/ui/
  # file meets the threshold.
  lcov --list coverage_filtered.info \
    | grep -E "src/ui/" \
    | grep -v "^Total" \
    | awk -F'|' '{print $NF+0}' \
    | sort -n \
    | head -1 \
    | awk '{if ($1 < 25.0) { print "FAIL: src/ui/ worst-file coverage " $1 "% < 25% Phase 1 gate"; exit 1 } else { print "PASS: src/ui/ worst-file coverage " $1 "% >= 25%"; exit 0 }}'
  ```

  This float-aware awk pipeline handles lcov output values like `24.8%` or `25.0%` correctly. Bash integer `-lt` comparison would truncate `24.8` to `24` (passing a gate that should fail) or fail to parse non-integer values at all. The `grep -v "^Total"` filter ensures the lcov summary Total row does not skew the minimum calculation. The `head -1` takes the minimum (worst-case) file — this is intentional. Per `architecture/testing/coverage.md`.
- [ ] **`terrain_tests` CMake target skeleton** (`test-dev-cpp`): create `terrain_tests` CMake target skeleton with stub source file `tests/terrain/terrain_stub.cpp` (containing a single `TEST` calling `SUCCEED()`). Apply `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)` immediately — do NOT use the default 120 s timeout. The `DISCOVERY_TIMEOUT 60` override is required per `architecture/testing/framework.md` because coverage-instrumented terrain binaries are large and may exceed the default 30 s discovery timeout on loaded CI runners. Link `aitown_terrain GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest` proactively. Include directories:

  ```cmake
  target_include_directories(terrain_tests PRIVATE
      tests/simulation/ tests/terrain/ src/terrain/ ${CMAKE_SOURCE_DIR})
  ```

  **Phase 1 requirement**: `terrain_stub.cpp` MUST include BOTH `src/terrain/terrain_chunk.h` AND `src/terrain/ITerrainRNG.h` so that both `src/terrain/` and `tests/terrain/` include paths are verified during Phase 1, not silently deferred to Phase 2.

  **NOTE (Test-C3 — for Phase 2 implementer)**: When Phase 2 adds the real `TerrainGenerator` implementation, the fixed-seed regression tests MUST use `TerrainGenerator(seed)` (single-argument production constructor), NOT `TerrainGenerator(seed, &mockRng)` (two-argument injectable form). The Phase 1 skeleton includes this comment in `terrain_stub.cpp` to prevent Phase 2 from using the injectable form for all tests and creating a production-constructor coverage gap. Both constructor forms must be exercised: the injectable form for deterministic unit tests (with `MockTerrainRNG`), and the production form for fixed-seed regression tests.

- [ ] Create `aitown_terrain` as an INTERFACE CMake library target using `add_library(aitown_terrain INTERFACE)` in Phase 1 so `terrain_tests` can `target_link_libraries(terrain_tests PRIVATE aitown_terrain ...)` without a configure error. Phase 2 converts this to a real STATIC or OBJECT library when terrain source files exist. Without this stub library definition, `terrain_tests` fails to configure and the Phase 1 exit criterion for `terrain_tests` compiling cannot be met.

- [ ] Create `src/terrain/terrain_chunk.h` minimal stub:

  ```cpp
  class TerrainChunk {};
  ```

  with include guard — required for `terrain_stub.cpp` to compile. Full implementation is Phase 2. Without this file, `terrain_tests` fails to compile at Phase 1.

- [ ] Create `src/terrain/terrain_generator.h` minimal stub with both constructor signatures declared:

  ```cpp
  #pragma once
  #include <cstdint>
  class ITerrainRNG;
  class TerrainGenerator {
  public:
      explicit TerrainGenerator(uint64_t seed) {}
      TerrainGenerator(uint64_t seed, ITerrainRNG* rng) {}
  };
  ```

  Bodies are empty stubs `{}` (NOT `= delete` — `= delete` would prevent Phase 2 tests from calling those constructors, causing compile errors). `terrain_stub.cpp` must `#include "terrain_generator.h"` alongside `terrain_chunk.h`. This ensures the two-constructor contract (production `(uint64_t seed)` + injectable `(uint64_t seed, ITerrainRNG* rng)`) is in place before Phase 2 fills in the implementations. Phase 2 implementers MUST NOT change these constructor signatures without updating this stub.

- [ ] Create `src/terrain/ITerrainRNG.h` stub with the class definition: virtual `nextFloat()`, `nextInt()`, `reseed()` methods as defined in `architecture/testing/testability-architecture.md`. Include guard required.

- [ ] Create `tests/terrain/mock_terrain_rng.h` stub with the `MockTerrainRNG` class as a **manual stub** (NOT GMock `MOCK_METHOD`). `MockTerrainRNG` implements `ITerrainRNG` manually with a `std::mt19937_64` engine seeded at construction, a `reseedCount()` accessor returning how many times `reseed()` has been called, and manual implementations of all `ITerrainRNG` virtual methods (`nextFloat()`, `nextInt()`, `reseed()`). Do NOT use `MOCK_METHOD` for any `ITerrainRNG` virtual method in `MockTerrainRNG`. (ref: `architecture/testing/testability-architecture.md`)

  Phase 2 adds real terrain test sources without CMakeLists changes.

  **Exit criterion**: `terrain_tests` compiles with `terrain_chunk.h`, `ITerrainRNG.h`, AND `terrain_generator.h` included; `mock_terrain_rng.h` instantiates without error; `MockTerrainRNG` uses a manual `std::mt19937_64` stub, not GMock macros.
- [ ] **CI artifact naming** (`cicd-dev-github`): all `upload-artifact` step names in Phase 1 additions to `ci.yml` MUST include the `${{ github.sha }}` suffix (e.g., `test-results-linux-${{ github.sha }}`, `test-results-windows-${{ github.sha }}`) to ensure artifact uniqueness across concurrent PR builds. Without the SHA suffix, concurrent PR builds overwrite each other's artifacts, causing stale artifact reports in `dorny/test-reporter`.
- [ ] **CI routing verification** (`cicd-dev-github`): before Phase 1 exit criteria are declared met, `cicd-dev-github` must verify that the `integration_tests` CMake target causes the `ctest -L '^integration$'` step in `build-linux` to discover and pass at least one test (the trivial `addStaticText()` integration test). Concrete verification procedure: run `ctest --test-dir build -N -L '^integration$'` and confirm the output lists at least one test name (e.g., `IrrlichtUIBackendTest.AddStaticTextReturnsNonZeroHandle`). If zero tests appear, the `integration_tests` CMake target is not properly linked and the exit criterion cannot be declared met. A zero-test discovery (false-green exit 0) does NOT constitute a passing routing verification. No YAML changes are required — this is a routing confirmation only. **`coverage-linux` label routing parity**: the `coverage-linux` CI job must include the same `integration` AND `requires-opengl` label routing verification steps as `build-linux` — both `ctest -L '^integration$'` and `ctest -L '^requires-opengl$'` steps must be present in `coverage-linux` with the same non-zero discovery enforcement. `coverage-linux` performs a fully independent build; without these steps, a label routing regression in `coverage-linux` would be silently masked. (ref: `architecture/ci-cd/github-actions-workflow.md`)

- [ ] **CI routing negative-case detection** (`cicd-dev-github`): add a CI step in `build-linux` that verifies integration test discovery count is non-zero immediately after build:

  ```yaml
  - name: Verify integration test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^integration$' 2>/dev/null | grep -c 'Test #')
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '"'"'^integration$'"'"' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "Integration test routing verified: $count test(s) discovered."
  ```

- [ ] **CI requires-opengl routing verification step** (`cicd-dev-github`): add a CI step in `build-linux` (paired with the `integration` routing verification step) that verifies the `requires-opengl` test label also produces non-zero test discovery. The `opengl_tests` target has at least one test (`LODSwapSmokeTest.SetMeshGrabDropContract`) labelled `requires-opengl`:

  ```yaml
  - name: Verify requires-opengl test routing (non-zero discovery)
    shell: bash
    run: |
      count=$(ctest --test-dir build -N -L '^requires-opengl$' 2>/dev/null | grep -c 'Test #')
      if [[ "$count" -eq 0 ]]; then
        echo 'ERROR: ctest -L '"'"'^requires-opengl$'"'"' discovered 0 tests — label routing is broken'
        exit 1
      fi
      echo "requires-opengl test routing verified: $count test(s) discovered."
  ```

  **src/ui/ coverage baseline checkpoint**: After `lcov --summary`, the coverage-linux CI job runs `lcov --list coverage_filtered.info | grep -E 'src/ui/'` as an informational baseline check. In Phase 1 this prints a WARNING (stubs have minimal coverage) — it does NOT fail CI. This baseline-detects the src/ui/ reporting path for Phase 5. **Owner: `cicd-dev-github`** (the coverage-linux CI job adds the informational lcov list step for `src/ui/` translation units). Note: the actual ci.yml change for this step is handled by `cicd-dev-github`.

- [ ] **Bootstrap oscillation design review gate (Phase 1 — analytical only)**:

  **HOLD: none — the bootstrap oscillation analysis is PASSED as of Round 1 spec review. No hold items remain.**

  `gamedesign-lookandfeel` reviews the mathematical form of the bootstrap decay formula (`R_bootstrap(tick) = 0.50 × max(0, 1 − tick/6)`, `C_bootstrap = 0.25 × max(0, 1 − tick/6)`, `I_bootstrap = 0.15 × max(0, 1 − tick/6)`) and the demand coupling constants from `architecture/game-design/zoning-system.md`, confirms analytically that the oscillation criterion (demand_factor swing > 0.3 between consecutive ticks) is correctly specified and that the decay constants do not produce analytical oscillation, and signs off. This design review is **analytical — no code execution required** (CitySimulation does not exist until Phase 3). **Sign-off is valid ONLY when all three canonical scenarios are evaluated and documented with numerical results in this checklist entry: (1) blank map — no zones placed; (2) all three zone types placed simultaneously at tick 0; (3) rapid C/I removal during bootstrap period (place then demolish C/I tiles within ticks 0–5). A blanket 'analytically confirmed' without numerical results for all three scenarios is NOT a passing sign-off.** The sign-off must be recorded as a comment in this checklist before Phase 2 begins. Owner: `gamedesign-lookandfeel` (design review and sign-off). **Demolition-Induced Swing Exemption** (per `architecture/game-design/zoning-system.md`): for scenario 3, the oscillation criterion applies to formula-driven demand evolution given a fixed city layout AFTER the demolition completes — not across the demolition event itself. The sign-off for scenario 3 must document: (a) the city state immediately after the final C/I demolition at tick T, (b) the resulting effective_demand_factor at tick T, and (c) the formula-driven trajectory from tick T onward through tick 6. The tick T−1 → tick T swing (crossing the demolition event) is exempt; swings between subsequent ticks with no player actions are subject to the criterion. **Contingency branch** (REQUIRED — this gate is not a go/no-go blocker without a defined resolution path): if the analytical review finds an oscillation violation for ANY of the three canonical scenarios, `gamedesign-lookandfeel` must either: (a) propose an adjustment to the bootstrap decay constants in `architecture/game-design/zoning-system.md` and re-verify before Phase 1 sign-off, OR (b) formally defer the fix to the Phase 3 code spike with a documented interim exemption. A deferred fix requires a HOLD flag placed on the Phase 3 `DemandOscillation_Spike_BlankMap_3xSpeed` test so that Phase 3 does not lock test constants against the known-oscillating values. The chosen path (a or b) must be documented in the sign-off comment. **Note**: Code execution of the oscillation spike (running CitySimulation for 10 ticks at 3× speed to trace `demand_factor` values) is deferred to Phase 3 as a pre-merge gate — `CitySimulation` does not exist until Phase 3. If oscillation is confirmed at Phase 3, `gamedesign-lookandfeel` must adjust the decay constant before Phase 3 test constants are locked and record the result in `architecture/game-design/zoning-system.md`. **Scenario 3 numeric derivation**: the full numeric derivation of bootstrap oscillation scenario 3 (rapid C/I removal during bootstrap period) is in `architecture/game-design/zoning-system.md` (updated in Round 3 spec fix). Phase 3 test authors MUST verify their simulation output against this derivation before Phase 3 closes — do not author Phase 3 test constants for scenario 3 without cross-checking against the `zoning-system.md` derivation.
- [ ] **`tools/vehicle_atlas_registry.json` stub** (JOINT: `graphics-artist-2d-texture` + `graphics-artist-3d-model`): create stub at the canonical path **`tools/vehicle_atlas_registry.json`** with all V1 vehicle type assignments per `architecture/asset-standards/3d-model-standards.md` Vehicle Atlas Cell Registry section and with the canonical schema documented below. This file was referenced by Phase 0 spec but not delivered in Phase 0 — recovered in Phase 1. The export validation script (Phase 6) and CI asset validation job read this registry when checking vehicle UV channel 0 coordinates (check #10). **Ownership**: `graphics-artist-2d-texture` confirms that the 4×4 diffuse cell grid dimensions (512×512 px per cell, 2048×2048 total) and the 8×8 normal atlas cell dimensions (256×256 px per cell) match the authoritative values in `architecture/asset-standards/2d-texture-standards.md` and `architecture/asset-standards/building-atlas-layout.md`; `graphics-artist-3d-model` co-owns for UV island placement verification (confirming UV islands exported from Blender fall within the assigned cell bounds using the OpenGL V-flip convention). Both owners must sign off before the registry is considered final.

  **Required JSON schema** — the stub must exactly match the Vehicle Atlas Cell Registry JSON format in `architecture/asset-standards/building-atlas-layout.md` (canonical schema) and `architecture/asset-standards/3d-model-standards.md`. The following keys are required (2D-H1 / 3D-H1 — schema conformance fix):
  - Nested `"diffuse_atlas"` object with `atlas_file`, `grid: { cols, rows, cell_size_px }`, `mip_levels`, `upload_path`
  - Nested `"normal_atlas"` object with `atlas_file`, `grid: { cols, rows, cell_size_px }`, `mip_levels`, `upload_path`, `_comment_normal_atlas`
  - Nested `"sprite_atlas"` object with `_comment`, `atlas_file`, `grid: { cols, rows, cell_size_px }`, `mip_levels`, `upload_path`
  - `"assignments"` array (not `"vehicles"`); each entry uses `"vehicle_type"` (NOT `"vehicle_id"` or `"id"`)
  - The normal atlas uses `mip_levels: 4` (mandatory 4-level mip chain) and `upload_path: "linear"` (normal data does not require sRGB decode). (ref: `architecture/asset-standards/building-atlas-layout.md`, `architecture/asset-standards/3d-model-standards.md`)

  ```json
  {
    "_comment": "AI Town vehicle atlas registry v1 — canonical schema per building-atlas-layout.md",
    "diffuse_atlas": {
      "atlas_file": "vehicles_diffuse_atlas_d.dds",
      "grid": { "cols": 4, "rows": 4, "cell_size_px": 512 },
      "mip_levels": 4,
      "upload_path": "srgb"
    },
    "normal_atlas": {
      "atlas_file": "vehicles_normal_atlas_n.dds",
      "grid": { "cols": 8, "rows": 8, "cell_size_px": 256 },
      "mip_levels": 4,
      "upload_path": "linear",
      "_comment_normal_atlas": "Same row/col as diffuse but 8x8 grid; U=[C/8,(C+1)/8], V=[R/8,(R+1)/8]. 4-level mip chain MUST be authored via bicubic downsampling BEFORE DXT5nm compression — auto-generated mips do not preserve normal vector normalization across LOD levels."
    },
    "sprite_atlas": {
      "_comment": "256x256 DXT5 sprite atlas; 16x16 cell grid; 16x16 px per cell; GL_TEXTURE_MAX_LEVEL=0 (no mip chain); upload_path: linear (stylised roof swatches — NOT photographic diffuse; sRGB decode is incorrect)",
      "atlas_file": "vehicles_sprite_atlas_d.dds",
      "grid": { "cols": 16, "rows": 16, "cell_size_px": 16 },
      "mip_levels": 1,
      "upload_path": "linear"
    },
    "assignments": [
      { "vehicle_type": "car_sedan",     "row": 0, "col": 0, "comment": "Standard passenger car" },
      { "vehicle_type": "car_hatchback", "row": 0, "col": 1, "comment": "Compact car" },
      { "vehicle_type": "car_suv",       "row": 0, "col": 2, "comment": "Larger passenger vehicle" },
      { "vehicle_type": "bus_standard",  "row": 1, "col": 0, "comment": "City bus" },
      { "vehicle_type": "truck_cargo",   "row": 1, "col": 1, "comment": "Delivery/cargo truck" }
    ]
  }
  ```

  **Note on vehicle atlases** (per `architecture/asset-standards/building-atlas-layout.md`): the diffuse atlas (`vehicles_diffuse_atlas_d.dds`, 2048×2048 DXT1 sRGB, 4×4 grid, 512×512 cells) is separate from the sprite atlas (`vehicles_sprite_atlas_d.dds`, 256×256 DXT5, 16×16 grid, 16×16 cells). The `sprite_atlas` stub object is included in the Phase 1 registry JSON so Phase 6 CI validation check #11 (vehicle sprite atlas UV coordinates within cell bounds) can parse the schema without a YAML change. `GL_TEXTURE_MAX_LEVEL = 0` applies to the sprite atlas (`_sprite` suffix dispatch rule). (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/building-atlas-layout.md`)

  **Schema sign-off (Phase 1 exit criterion)**: `graphics-artist-2d-texture`, `graphics-dev-irrlicht`, AND `graphics-artist-3d-model` must all sign off that the **schema** (JSON structure, field names, V1 vehicle type slots) is correct before Phase 1 closes. The Phase 1 `graphics-artist-3d-model` co-sign-off scope is limited to: (a) V-flip convention (`V_opengl = 1 - V_blender`) documented correctly; (b) atlas cell boundary UV formulas mathematically correct for 4×4 diffuse and 8×8 normal grids; (c) all five V1 vehicle type row/column assignments are correct; (d) **confirm that vehicle normal atlas mip chain (`mip_levels: 4` in schema) MUST be generated via bicubic downsampling BEFORE DXT5nm (BC3) compression** — auto-generated mips do not preserve normal vector normalization across LOD levels; this is distinct from diffuse atlas mip chain authoring (which can use standard tool-generated mips). **UV island placement verification is Phase 6 only — no meshes exist in Phase 1.** Do NOT include UV island placement confirmation as part of the Phase 1 sign-off.

  **UV island data sign-off (Phase 6 pre-condition)**: `graphics-artist-2d-texture` AND `graphics-dev-irrlicht` must sign off AGAIN in Phase 6 that actual UV island data is correctly populated for all V1 vehicle types. This second sign-off confirms that Blender-exported UV coordinates fall within the assigned cell bounds using the OpenGL V-flip convention, and that the `normal_atlas` field `upload_path: "linear"` is correctly applied at runtime.

  **`graphics-artist-3d-model` co-sign-off (Phase 6 pre-condition)**: `graphics-artist-3d-model` must co-sign confirming: (a) V-flip convention (`V_opengl = 1 - V_blender`) is correct; (b) atlas cell boundary UV formulas for 4×4 diffuse and 8×8 normal grids are correct; (c) all five V1 vehicle type assignments are correct; (d) actual UV island data is correctly populated for all V1 vehicle types (UV islands exported from Blender fall within the assigned cell bounds using the OpenGL V-flip convention). UV island placement verification is Phase 6 only — this sign-off cannot be completed until Phase 6 meshes exist.

- [ ] **`tools/validate_assets.py` stub** (`cicd-dev-github`): create `tools/validate_assets.py` — an empty Python script with a `main()` entry point, the numbered comment block below listing all 14 required asset validation checks (as a to-do list), and a `sys.exit(0)` body (no assets exist yet so it always passes). Register a CI step in the asset validation job that calls `python tools/validate_assets.py`. Exit criterion: `tools/validate_assets.py` exists at the canonical path and the CI asset validation job calls it without error. **Phase 2 activation**: the stub is replaced with the real 13-check implementation in Phase 2. The CI job definition and all-checks-pass wiring remain unchanged between Phase 1 and Phase 2 — only the Python implementation grows. No CI YAML changes are needed when Phase 2 activates the real checks.

  **`all-checks-pass` markdown-lint preservation (H18 — BLOCKING)**: When adding `validate-assets` to the `all-checks-pass` job `needs:` list, the `markdown-lint` entry from Phase 0 MUST be preserved. The Phase 1+ form must contain ALL FIVE entries: `build-linux`, `build-windows`, `coverage-linux`, `markdown-lint`, `validate-assets`. Do NOT replace the existing list — EXTEND it. Removing `markdown-lint` from `all-checks-pass` is a PR rejection criterion.

  The Phase 1 stub MUST include the following numbered comment block verbatim. **Check numbering must match `3d-model-standards.md` canonical list exactly** (ref: `architecture/asset-standards/3d-model-standards.md` Export validation script — required checks):

  ```python
  # Checks implemented in Phase 2 (canonical numbering per 3d-model-standards.md):
  # 1. Building _lod0/_lod1 files use .b3d format (not .obj)
  # 2. Small building/prop _lod2.b3d file presence is floor-count conditional
  #    (height_floors <= 3: must NOT have _lod2.b3d, must have _billboard.dds;
  #     height_floors >= 4: must have _lod2.b3d, must NOT rely on _billboard.dds)
  # 3. Large building _lod2.b3d present, within 300-500 tri budget, _lod2_lm.dds uses DXT5/BC3 (not DXT1)
  # 4. UV channel 0 coordinates on all LOD levels fall within [0, 1] UV space
  # 5. UV channel 1 (lightmap) present and non-degenerate on all building .b3d files
  # 6. Assembled LOD0 total <= 5000 tris (large) or <= 1500 tris (small) for representative stack
  # 7. Facade detail piece count <= 10 per assembled stack
  # 8. Asset pivot at bottom-center (Y=0); geometry Y extent within [0, 3.0] per floor module (tolerance 0.005 units / 5 mm)
  # 9. LOD distance hysteresis >= 5 m (close), >= 10 m (far) — validated from LODNode config
  # 10. Vehicle UV channel 0 coordinates within asset's assigned atlas cell (4x4 diffuse grid)
  # 11. Small building / prop assets with height_floors >= 4 must have a _lod2.b3d geometry shell;
  #     small building / prop assets with height_floors <= 3 must NOT have a _lod2.b3d file (billboard-only LOD2).
  # 12. Vehicle normal map UV channel 0 within assigned atlas cell in vehicles_normal_atlas_n.dds (8x8 grid)
  # 13. Facade atlas cell pixels — all non-transparent content within [8, 504] texel range (496x496 usable zone)
  # 14. .meta sidecar file present and valid for each .b3d building or vehicle file
  #     (ACTIVE from Phase 2 — must be implemented in Phase 2 validate_assets.py, not deferred to Phase 6)
  # Note: Music-stem JSON sidecar validation is a separate audio pipeline step; it does not belong in validate_assets.py check #14.
  ```

  **CO-LANDING REQUIREMENT (C3 — four-item atomicity)**: All four items MUST land in the same commit/PR — a PR missing any one of these four is a PR rejection criterion:

  1. `tools/validate_assets.py` stub
  2. The `validate-assets` CI job definition in `.github/workflows/ci.yml` (including `actions/setup-python` with live-resolved SHA at implementation time)
  3. The `Run asset validation` step inside that job calling `python tools/validate_assets.py`
  4. The update to `all-checks-pass` `needs:` list adding `validate-assets`

  **PR rejection criteria**:

  - A PR that adds the `validate-assets` job body without simultaneously updating `all-checks-pass` `needs:` leaves the gate incomplete and MUST be rejected.
  - A PR that adds to `all-checks-pass` `needs:` before the job body exists causes a workflow YAML parse failure on every push and MUST be rejected.

  **SHA token note** (`cicd-dev-github`): The `validate-assets` CI job uses `actions/setup-python`. The SHA requires **live resolution at Phase 1 implementation time** — the developer MUST query the current SHA by running `gh release view v5.6.0 --repo actions/setup-python --json tagName,targetCommitish` immediately before authoring the `ci.yml` change. Do NOT use a pre-resolved SHA from this document without re-verifying it — the SHA recorded in prior planning discussions (e.g., `a26af69be951a213d495a4c3e4e4022e16d87065`) may become stale if the tag is updated or the release is re-tagged. The `@<SETUP_PYTHON_SHA>` placeholder MUST NOT be committed to `ci.yml` — unresolved placeholders bypass SHA-pinning and are indistinguishable from a supply-chain attack.

  **BLOCKING EXIT CRITERION (CI-C1)**: Verify the SHA for `actions/setup-python` v5.6.0 via live query: `gh release view v5.6.0 --repo actions/setup-python --json tagName,targetCommitish`. The `ci.yml` MUST use the SHA obtained from this live query — a literal `<SETUP_PYTHON_SHA>` placeholder in `ci.yml` will immediately fail CI via the supply-chain SHA lint step and is a **Phase 1 PR rejection criterion**. The live-resolution requirement applies to every PR that modifies the `validate-assets` job — do not assume the SHA is permanent.
- [ ] **`SimulationConstants` review gate**: `SimulationConstants` namespace (defined in Phase 3) must have ALL constant values reviewed and locked by `gamedesign-lookandfeel` before Phase 2 begins. Owner: `gamedesign-lookandfeel` (values) and `graphics-dev-irrlicht` (header placement in `src/simulation/simulation_constants.h`). **SimulationConstants review gate: `simulation_constants.h` values reviewed and signed off before Phase 1 closes — downstream phases may not change constant values without a cross-team review.** This gate is a Phase 1 exit criterion — constant changes mid-Phase-3 break tests written against prior values. The sign-off must be recorded as a comment in the Phase 1 deliverable checklist before Phase 2 starts. **The gate list is EXHAUSTIVE — not a sample.** The reviewer MUST verify EVERY `SimulationConstants` value that appears in ALL five game-design spec files. If any constant appears in a spec file that is not listed in this gate, it must be added to this list before Phase 1 sign-off is granted. Missing a constant from this list and discovering a wrong value in Phase 3 requires updating all test fixture values. The **six** spec files to cross-reference: `economy-model.md`, `simulation-time.md`, `traffic-system.md`, `zoning-system.md`, `population-density-growth.md`, and `service-coverage.md`. Service coverage radii (fire=800m, police=600m, water=700m) and service penalty/recovery rates are `SimulationConstants` values defined in `service-coverage.md` and must be cross-checked. The following constants must be explicitly included in the gate list (in addition to any others found in the spec files):
  - `road_maintenance_cost_per_tile`, `road_placement_cost_per_tile` (`= $500` placement cost, separate from maintenance; NOT waived during grace period)
  - `service_upkeep_fire_station_per_tick`, `service_upkeep_police_station_per_tick`, `service_upkeep_power_plant_per_tick`, `service_upkeep_water_tower_per_tick`
  - `wage_fraction_of_revenue`, `loan_repayment_ticks`, `ticks_per_year`
  - `bond_repayment_ticks = 24` — Emergency Municipal Bond repayment period; distinct from `loan_repayment_ticks = 12` for standard forced loans
  - `density_unlock_scale_easy`, `density_unlock_scale_normal`, `density_unlock_scale_hard`
  - `bond_max_uses_easy`, `bond_max_uses_normal`, `bond_max_uses_hard`
  - `R_raw_material_rate` (= 0.05), `C_goods_consumption_rate` (= 0.25) — per `architecture/game-design/zoning-system.md`
  - `base_income_per_resident_low`, `base_income_per_resident_medium`, `base_income_per_resident_high` — **NOTE**: `base_income_per_resident_low = $50` and `base_income_per_resident_medium = $50` are EQUAL (non-obvious: Low and Medium tiers share the same per-resident rate; only High = $55 differs). Must be explicitly documented to prevent Phase 3 implementors from assuming a monotonically increasing scale.
  - `base_day_duration_easy`, `base_day_duration_normal`, `base_day_duration_hard`
  - `min_speed_fraction = 0.05` — road speed floor; 5% of max_speed; from `traffic-system.md`
  - `road_segment_capacity = 8` — segment capacity in vehicles per tile; from `traffic-system.md`
  - `grace_period_real_seconds = 120` — real-time seconds before forced loan mechanic and upkeep charges become active; Phase 3 forced-loan tests must use `ManualClock::advance(120.0)` against this constant
  - `agent_timeout_simulation_seconds` — agent timeout threshold (120 simulation seconds; from `traffic-system.md`)
  - `service_recovery_desirability_per_tick` — desirability recovery rate per tick when service restored (from `service-coverage.md`)
  - `service_uncovered_desirability_penalty_per_tick` — desirability penalty per tick when service coverage is absent (from `service-coverage.md`); equally important to `service_recovery_desirability_per_tick` and must not be omitted from review
  - All smoothstep T-values (R: T=25 to T=60; C: T=30 to T=65; I: T=40 to T=80), service radii (fire=800m, police=600m, water=700m), earthworks thresholds, and any other constant named explicitly in the five spec files
  - `earthworks_base_cost_per_tile = $500` — base rate per tile for flat terrain from `architecture/game-design/terrain-interaction.md`; must be included in the gate review so Phase 3 `EarthworksCost_Nonzero_FiresAudioCallback` test has a confirmed value. **Note**: earthworks cost is formula-driven: `cost = $500 × clamp((slope_degrees − 15) / 30, 0, 2)`. The Phase 3 `EarthworksCost_Nonzero_FiresAudioCallback` test must provide a tile with `slope_degrees > 15°` to produce a non-zero cost and assert `cost == 500 * clamp((slope - 15) / 30, 0, 2)` for several representative slope values.
  - `adjacency_commercial_residential_bonus = 10` — adjacency desirability bonus for residential tiles adjacent to commercial tiles; **canonical name from `architecture/game-design/zoning-system.md`** (NOT `adjacency_commercial_residential_bonus` — that is a prior incorrect name; NOT terrain-interaction.md)
  - `adjacency_industrial_residential_base_penalty = 20` — adjacency desirability penalty for residential tiles adjacent to industrial tiles; from `architecture/game-design/zoning-system.md`
  - `desirability_base_value = 50` — base desirability value for all zone tiles before any modifiers; from `architecture/game-design/zoning-system.md`
  - `demand_floor_residential = 0.20f` — minimum effective demand fraction for residential zones; from `architecture/game-design/population-density-growth.md`
  - `demand_floor_commercial = 0.10f` — minimum effective demand fraction for commercial zones; from `architecture/game-design/population-density-growth.md`
  - `demand_floor_industrial = 0.10f` — minimum effective demand fraction for industrial zones; from `architecture/game-design/population-density-growth.md`
  - `population_growth_cap_fraction = 0.10f` — maximum fraction of total capacity by which population may grow in a single tick; from `architecture/game-design/population-density-growth.md`
  - `population_decay_cap_fraction = 0.15f` — maximum fraction of total capacity by which population may decay in a single tick; from `architecture/game-design/population-density-growth.md`
  - Any other constants from `architecture/game-design/terrain-interaction.md` that Phase 3 simulation tests will need (reviewer must cross-reference the full `terrain-interaction.md` spec for any additional named constants)

  **`estimateMonthlyUpkeep()` computability contract**: `estimateMonthlyUpkeep()` is implemented by the concrete `CitySimulation` class with internal state access — it does NOT require additional `ICitySimulation` getters for its own computation. The HUD budget panel (Phase 5) displays the estimate as an opaque total. Do NOT add `getRoadTileCount()` or `getPlacedServiceBuildingCount()` to the `ICitySimulation` interface for this purpose. The formula `(road_tile_count × road_maintenance_cost_per_tile) + sum(service_upkeep_*_per_tick for all placed service buildings) + (total_C_I_revenue × wage_fraction_of_revenue)` is an internal computation using `CitySimulation` member variables — no new interface getters are required.

  **NOTE**: The full `SimulationConstants` struct is defined in Phase 3. The Phase 1 gate confirms only that `service_uncovered_desirability_penalty_per_tick` is present in the eventual struct and is agreed upon. Phase 1 does NOT require the struct to compile — it requires the value to be specified in writing here so Phase 3 has no ambiguity.

  **Sign-off numeric requirement**: The gate sign-off comment MUST include the confirmed numeric value for `earthworks_base_cost_per_tile` and other constants that appear in named Phase 3 test cases. A blanket "all constants confirmed" without recording individual values for constants that appear in named tests is insufficient. The gate sign-off comment MUST also explicitly record: `service_uncovered_desirability_penalty_per_tick = 5`, `service_recovery_desirability_per_tick = 8` (both are Phase 1 `SimulationConstants` deliverables per `architecture/game-design/service-coverage.md`; Phase 3 service-coverage tests reference them directly). The gate sign-off MUST also explicitly confirm:
  - `null_path_demand_default = 0.5f` — demand_factor assigned to a zone type's rolling window when all ticks in the window have no valid A* path (null-path all-ticks case); distinct from the timeout-trip treatment which is extreme-travel-time, not null-path; from `architecture/game-design/traffic-system.md`. This value appears in the bootstrap oscillation scenario 3 derivation in `zoning-system.md` and must match the Phase 3 `CitySimulation` implementation.

  (ref: `architecture/game-design/economy-model.md`, `architecture/game-design/simulation-time.md`, `architecture/game-design/traffic-system.md`, `architecture/game-design/service-coverage.md`, `architecture/game-design/zoning-system.md`, `architecture/game-design/population-density-growth.md`)
- [ ] **`src/simulation/simulation_constants.h` Phase 1 stub** (`gamedesign-lookandfeel` + `graphics-dev-irrlicht`): create `src/simulation/simulation_constants.h` as a Phase 1 stub containing only the constants needed for Phase 1 tests as `static constexpr` members with their locked values. Phase 3 extends this file with additional constants; Phase 1 creates it so the values are compile-verified from Phase 1 onward. Required Phase 1 constants (values confirmed by the SimulationConstants review gate above):

  ```cpp
  struct SimulationConstants {
      static constexpr int   adjacency_commercial_residential_bonus        = 10;
      static constexpr int   adjacency_industrial_residential_base_penalty = 20;
      static constexpr int   desirability_base_value                       = 50;
      static constexpr float demand_floor_residential                      = 0.20f;
      static constexpr float demand_floor_commercial                       = 0.10f;
      static constexpr float demand_floor_industrial                       = 0.10f;
      // Service-coverage simulation constants (GD-1) — locked Phase 1 for service-coverage tests
      // (per architecture/game-design/service-coverage.md canonical values):
      static constexpr int   service_uncovered_desirability_penalty_per_tick = 5;   // −5 desirability points/tick for uncovered residential
      static constexpr int   service_recovery_desirability_per_tick          = 8;   // +8 desirability points/tick when coverage restored
      // Traffic demand constants (GD-3) — locked Phase 1 for bootstrap oscillation derivation:
      static constexpr float null_path_demand_default = 0.5f;  // demand_factor assigned when rolling window has no valid A* paths
      // Earthworks/road constants (GD-3) — locked Phase 1; referenced by named Phase 3 test cases:
      static constexpr int   earthworks_base_cost_per_tile = 500;  // from architecture/game-design/terrain-interaction.md
      static constexpr int   road_placement_cost_per_tile   = 500;  // from architecture/game-design/economy-model.md
  };
  // GD-4: These two must remain int — static_asserts enforce correct type at compile time
  static_assert(std::is_integral_v<decltype(SimulationConstants::service_uncovered_desirability_penalty_per_tick)>,
                "service_uncovered_desirability_penalty_per_tick must be int — float variant is spec-incorrect");
  static_assert(std::is_integral_v<decltype(SimulationConstants::service_recovery_desirability_per_tick)>,
                "service_recovery_desirability_per_tick must be int — float variant is spec-incorrect");
  ```

  Phase 3 adds all remaining constants from the review gate list. Owner: `gamedesign-lookandfeel` confirms values; `graphics-dev-irrlicht` owns header placement in `src/simulation/simulation_constants.h`. Exit criterion: `simulation_constants.h` compiles cleanly with the 11 Phase 1 constants and is `#include`-able from test translation units in `tests/simulation/`.

  **NOTE (GD-1 spec alignment)**: The service-coverage constants use integer types (`int`) matching the canonical values from `architecture/game-design/service-coverage.md` (5 desirability points/tick penalty, 8 desirability points/tick recovery). These are NOT float rates — they are discrete per-tick integer adjustments to the desirability model. Any plan instruction suggesting float values (e.g., `-0.002f`, `+0.001f`) contradicts the spec and must be treated as an error. The spec values (5 and 8) are authoritative.

  **NOTE (GD-1 — SimulationConstants gate split)**: The gate for `simulation_constants.h` is split into two parts:

  - **Part A (Phase 1 exit criterion — THIS PHASE)**: The 11 constants already listed in the Phase 1 stub above (`adjacency_commercial_residential_bonus`, `adjacency_industrial_residential_base_penalty`, `desirability_base_value`, `demand_floor_residential`, `demand_floor_commercial`, `demand_floor_industrial`, `service_uncovered_desirability_penalty_per_tick`, `service_recovery_desirability_per_tick`, `null_path_demand_default`, `earthworks_base_cost_per_tile`, `road_placement_cost_per_tile`) are reviewed by `gamedesign-lookandfeel` and **locked before Phase 1 closes**. No further changes to these 11 values are permitted without a cross-team review.
  - **Part B (Phase 3 pre-merge gate)**: All remaining constants from the review gate cross-reference list (economy-model, simulation-time, traffic-system, zoning-system, population-density-growth, service-coverage) MUST be added to `simulation_constants.h` by Phase 3 implementers BEFORE any Phase 3 test fixture is authored. A test fixture that hard-codes a constant value that later differs from `SimulationConstants::` is a latent defect. Phase 3 must not be closed without Part B locked.

  **NOTE (GD-3 — earthworks/road constants)**: `earthworks_base_cost_per_tile = 500` and `road_placement_cost_per_tile = 500` are integer constants required in the Phase 1 stub because named Phase 3 test cases reference them directly. Both are already included in the stub block above. The Phase 1 stub includes all **11 constants** total.

  **NOTE (GD-4 — int type guard)**: `service_uncovered_desirability_penalty_per_tick` and `service_recovery_desirability_per_tick` MUST remain `int` (not `float`). Float variants are spec-incorrect and must fail code review in Phase 3. The `static_assert` blocks after the struct enforce this at compile time.

- [ ] **`shader_constants.h` correctness gate**: `src/rendering/shader_constants.h` stub must compile cleanly AND include a `static_assert` verifying all `kTexUnit*` constants are within valid GL per-stage texture unit range [0, 15]: `static_assert(kTexUnitBillboard <= 15, "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)")`. This is a correctness gate for the graphics pipeline — the stub must pass this assertion before Phase 2 begins. (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **Texture artist pre-alignment gate**: before Phase 2 DDS texture production begins, `graphics-artist-2d-texture` must review and sign off on ALL of the following:
  - `architecture/asset-standards/building-atlas-layout.md` — UV conventions, atlas layout, cell boundaries, vehicle atlas distinction (diffuse atlas `vehicles_diffuse_atlas_d.dds` vs sprite atlas `vehicles_sprite_atlas_d.dds`)
  - `architecture/asset-standards/2d-texture-standards.md` — sRGB vs linear upload paths, mip chain requirements, DXT formats
  - **`architecture/asset-standards/3d-model-standards.md` — Billboard Imposter Atlas section, LOD2 Shell UV authoring rules, and Vehicle UV V-flip OpenGL convention** — these directly affect texture artist work: (a) billboard DDS authoring requires understanding the 1024×128 DXT5 atlas format (8 × 128×128 frames, 1×8 horizontal strip), 4-level mip chain requirement, and 45° below horizontal bake elevation; (b) the normal atlas 8×8 grid (256×256 cells) requires understanding the V-flip convention for UV authoring; (c) LOD2 shell UV channel 0 maps into the same building atlas as LOD0/LOD1 — artist must understand the atlas cell boundary rules apply equally to all LOD levels

  This sign-off is a Phase 1 exit criterion — no Phase 2 DDS textures may be authored without it. **Sign-off documentation note**: `graphics-artist-2d-texture` must confirm in writing (comment in `implementation/phase-1.md` or linked record) having reviewed `building-atlas-layout.md`, `2d-texture-standards.md`, and the Billboard Imposter Atlas, LOD2 Shell UV, and Vehicle UV V-flip sections of `3d-model-standards.md`. The sign-off must also confirm the following items are locked and documented:

  - Splat channel assignment locked and understood: R=Grass (unit 5 / `kTexUnitTerrainLayer0`), G=Asphalt (unit 6 / `kTexUnitTerrainLayer1`), B=Soil (unit 7 / `kTexUnitTerrainLayer2`), A=Concrete (unit 8 / `kTexUnitTerrainLayer3`). These are fixed before texture production begins and must match the texture unit binding order in the terrain shader. A mismatch between splat channel order and texture unit binding order produces silent incorrect terrain blending — no runtime error.
  - UV tiling frequency: the tile repeat counts for each terrain texture layer at standard zoom are agreed and documented (e.g., "grass tiles at 4× per 64×64 m LOD0 chunk" per `architecture/asset-standards/2d-texture-standards.md`)
  - **Vehicle Normal Atlas**: 4-level mip chain mandatory (`GL_TEXTURE_MAX_LEVEL = 3`; levels 0–3) confirmed in `architecture/asset-standards/building-atlas-layout.md` — relates to `validate_assets.py` check #12 (vehicle normal atlas UV coordinates within cell bounds). The 4-level mip chain MUST be authored via bicubic downsampling BEFORE DXT5nm compression — auto-generated mips do not preserve normal vector normalization across LOD levels, causing incorrect specular highlight shape at medium vehicle distances (30–100 m). Confirm understanding before Phase 6 normal atlas production.
  - **Billboard atlas mip chain confirmed as `GL_TEXTURE_MAX_LEVEL = 3`** (4-level mandatory: 1024×128 → 512×64 → 256×32 → 128×16) per `architecture/asset-standards/2d-texture-standards.md` and `architecture/asset-standards/building-atlas-layout.md` — relates to `validate_assets.py` check #5 (mip chain level count). **Lightmap textures (`_lm` suffix) use `GL_TEXTURE_MAX_LEVEL = 0`** (single mip, no mip chain). Any reference to `GL_TEXTURE_MAX_LEVEL = 0` for billboard imposters is INCORRECT — that applies to lightmaps only.
  - **DDS export tool confirmed**: DDS export tool selection (NVTT or Compressonator) confirmed and recorded — CI `export_textures.py` (Phase 2) must call the same tool or an equivalent standards-compliant tool. This sign-off item must be recorded before Phase 2 DDS production begins. Relates to `validate_assets.py` check #2 (DDS magic header bytes) and check #4 (DDS format matches expected).
  - **DXT5nm normal map swizzle confirmed**: DXT5nm swizzle procedure for normal maps confirmed understood: X-to-alpha, Y-in-green, Z-discarded, applied BEFORE BC3 compression. Normal maps delivered without DXT5nm swizzle will silently corrupt the shader's Z-reconstruction on all affected surfaces. Relates to `validate_assets.py` check #4 (DDS format matches expected — DXT5nm for normal maps).
  - **Road marking atlas upload path confirmed as LINEAR (not sRGB)**: road marking atlas data is a decal mask (alpha channel coverage) — it does not require sRGB gamma decode. Uploading with sRGB format would incorrectly gamma-encode the mask data. Upload path for road markings is confirmed as `linear` before any Phase 2 road texture production begins. Relates to `validate_assets.py` check #4 (DDS format matches expected upload path).
  - **Road surface texture upload path confirmed as sRGB raw-GL (NOT linear)**: `road_asphalt_tileable.dds` (DXT5 sRGB, 1024×1024) is uploaded via the raw-GL sRGB path (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`). This is DISTINCT from the road marking atlas (linear pool). The road surface texture is a raw `GLuint` in `TextureCache::m_srgbTextures`; it requires the custom road shader material to bind unit 0 explicitly. Confirmed before any Phase 6 road texture production begins.
  - **Atlas cell border compliance**: all V1 texture cell content stays within the 496×496 px usable area (8 px border on each edge per 512×512 cell) — relates to `validate_assets.py` check #13 (facade atlas cell pixels within usable zone).
  - **Dimensions power-of-two and within GL_MAX_TEXTURE_SIZE**: all DDS textures use power-of-two dimensions and do not exceed 4096 px — relates to `validate_assets.py` check #3 (powers-of-two) and check #6 (GL_MAX_TEXTURE_SIZE limit).

  Phase 2 DDS texture production is blocked until this sign-off is on record.
- [ ] **3D model artist pre-alignment gate** (`graphics-artist-3d-model`, parallel to texture artist gate): this gate has two parts with different timing requirements:

  **Phase 1 blocking (exit criterion before Phase 1 closes)**: Co-sign `tools/vehicle_atlas_registry.json` schema — confirm V-flip convention, 4×4 diffuse atlas UV formulas, 8×8 normal atlas UV formulas, and all five V1 vehicle type assignments are correct. Confirm camera pitch range [−70°, −20°] and bake midpoint −45° (see `3d-model-standards.md` Camera Pitch Range section — already confirmed). **Confirm understanding of the four-floor LOD2 threshold**: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell (no billboard); buildings with `height_floors <= 3` use billboard-only at LOD2 (no `_lod2.b3d`). The `height_floors` field in the `.meta` sidecar JSON is the sole runtime and validation-script source of truth. This must be confirmed before co-signing `vehicle_atlas_registry.json` because `.meta`-linked `height_floors` values determine LOD2 strategy for all building assets.

  **Phase 6 pre-condition (full spec review, NOT required before Phase 1 closes)**: before Phase 6 building and vehicle asset production begins, `graphics-artist-3d-model` must review and sign off on `architecture/asset-standards/3d-model-standards.md` in full. Must confirm understanding of:
  - Naming convention (`<zone>_<tier>_<variant>_lod<N>.<ext>`)
  - `.meta` sidecar JSON format (required fields: `category`, `height_floors`, `atlas_cell`, `lod_distances`)
  - Pivot convention (bottom-center Y=0; geometry in X:−2 to +2, Y:0 to +3 per floor)
  - 5 mm vertical tolerance (0.005 Irrlicht units) for wall tile Y extents
  - 10-floor hard cap for large buildings
  - Per-module polygon caps (LOD0 and LOD1)
  - Collision mesh type dispatch order (check `_col_0` → `_col_circle` → `_col` → error)
  - LOD2 billboard-only rule (height_floors ≤ 3 floors only; floor count threshold is firm)
  - Geometry-shell requirement (height_floors > 3 floors requires `_lod2.b3d`)
  - All 14 export validation checks (particularly checks #2, #8, #9, #11, #12, #14)

  The sign-off checklist must also explicitly confirm:
  - **Vehicle Normal Atlas**: 4-level mip chain mandatory (`GL_TEXTURE_MAX_LEVEL = 3`; levels 0–3) — included in the texture artist sign-off checklist per `architecture/asset-standards/building-atlas-layout.md`
  - **Gap/overlap between adjacent LOD levels ≤ 5 mm** (no visible seam at LOD transition; 5mm tolerance per `architecture/asset-standards/3d-model-standards.md`)
  - **LOD2 pivot point**: bottom-center at Y=0 — identical to LOD0/LOD1 pivot convention (see `architecture/asset-standards/3d-model-standards.md` Modular Building Kit section). Using the bounding box centroid produces a position pop at LOD1→LOD2 transition equal to half the building height.

  Sign-off must be documented as a comment in this file before Phase 6 building and vehicle asset production begins. **This gate blocks Phase 6 asset production** — no building or vehicle geometry may be authored without it.
- [ ] **Camera pitch sign-off gate**: Camera pitch sign-off (billboard bake angle: −45°, range [−70°, −20°]) is confirmed in `architecture/asset-standards/3d-model-standards.md` Camera Pitch Range section (sign-off status: CONFIRMED). Phase 1 checklist records this by cross-reference only — no separate comment is required in `phase-1.md`. If the spike result changes the pitch range from the currently documented values, `graphics-artist-3d-model` must be explicitly re-notified before the updated spike result is committed, and `3d-model-standards.md` must be updated. (ref: `architecture/asset-standards/3d-model-standards.md`)
- [ ] **Atlas document re-validation gate**: `architecture/asset-standards/building-atlas-layout.md` must be reviewed and signed off by `graphics-artist-2d-texture` before Phase 6 UV authoring begins. This gate must be documented as a pre-condition in Phase 5/6 planning; tracking begins in Phase 1 so it is not overlooked. (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] **Music production brief** (`sound-artist-opensoftal`): before Phase 4 placeholder OGG stem authoring begins, `sound-artist-opensoftal` must deliver a one-page music production brief defining: (a) the shared root key and mode for ALL 6 gameplay stems (required per `architecture/audio-architecture/dynamic-soundscape.md` — all stems must share the same root key and mode for seamless crossfading without harmonic clashes); (b) provisional BPM confirmation (90 BPM / 4/4 per spec); (c) reference listening target (existing game or album whose tonal mood and production quality is the production target); (d) 44100 Hz / 16-bit stereo sample rate and bit depth confirmation — any other rate is a hard asset error per `dynamic-soundscape.md` and `audio-asset-formats.md`; (e) a crossfade audibility sketch: a simple diagram or table showing at what bar boundaries the crossfade occurs, how long the constant-power blend takes (min 2 s, default 3 s), and what the gain curves look like (`gain_out = cos(t × π/2)`, `gain_in = sin(t × π/2)` where t ∈ [0, 1] — constant-power crossfade: the sum of squares equals 1 at all t); (f) **OGG Vorbis encoding quality: minimum `-q 8` (approximately 256 kbps VBR)** — lower quality settings are NOT acceptable for V1 music stems; `-q 6` or below produces audible compression artifacts on sustained orchestral or ambient passages that would undermine the production quality goal. This sketch is reviewed by the Sound Artist before recording begins. **This brief is a Phase 1 exit criterion and blocks Phase 4 placeholder OGG authoring.** Authoring any OGG stem without an approved shared root key/mode risks producing stems that cannot crossfade cleanly. Per `architecture/audio-architecture/dynamic-soundscape.md`, the cross-tier harmonic compatibility requirement mandates that ALL 6 gameplay stems (calm_01, calm_02, growth_01, growth_02, crisis_01, crisis_02) share the same root key and mode; a crossfade audibility test (calm_01 mixed with growth_01 for 3 s) must be submitted as part of the delivery package in Phase 7. The brief must also explicitly confirm that every music stem will be delivered with a companion JSON sidecar file (`<stem_name>.json: {"bpm":90,"beats_per_bar":4}`). Phase 4 placeholder OGG stems MUST each include a stub JSON sidecar so Phase 4 `AudioSystem` bar-boundary tests can exercise the crossfade path against the real sidecar format. (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [ ] **SA-2 — Music production brief structured document** (`sound-artist-opensoftal`): as part of the music production brief deliverable above, the `sound-artist-opensoftal` MUST produce a structured JSON or plain-text brief that explicitly documents the following five items in a machine-readable or clearly structured form (not buried in prose):

  1. **BPM**: 90 (confirmed from spec)
  2. **Bar length**: 4 beats per bar (4/4 time signature)
  3. **Crossfade duration**: minimum 2 s, default 3 s (constant-power crossfade)
  4. **Placeholder stem titles** (5 required in Phase 1 brief; 6 total in Phase 4 delivery): `music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`, `music_crisis_01`. Note: `ambient_day` and `ambient_night` are ambient bed asset names, not music stems — they are NOT listed here. Phase 4 delivers all six gameplay stems (adding `music_crisis_02`) plus two main menu stems (`music_main_menu_01`, `music_main_menu_02`).
  5. **Authoring format note**: OGG files are NOT required in Phase 1 — only this structured brief. Phase 4 delivers the actual OGG placeholder stems.

  This structured brief is distinct from the one-page narrative production brief in the deliverable above. The structured form ensures Phase 4 `AudioSystem` bar-boundary timing logic can be verified against documented values (BPM=90, 4 beats/bar → bar boundary every `60/90 × 4 = 2.667 s` real-time) before any OGG files are authored. The brief MUST be committed as `assets/audio/music_production_brief.json` (or `.txt`) before Phase 1 closes. Phase 4 OGG authoring is blocked until this brief is on record.

  **SA-3 — Bar-count confirmation requirement** (additional requirement to music production brief): Every music stem MUST be authored to an exact integer number of bars at 90 BPM (4/4 time signature; one bar = 60/90 × 4 = 2.667 s). Non-integer bar counts cause the `m_nextBarBoundary` software sample counter crossfade logic to fire at incorrect stream positions on subsequent loop cycles. The sound artist must confirm the bar count in delivery notes for each stem. DAW loopback-audition verification (loop the file and listen through the boundary) must confirm the loop seam is inaudible before delivery. This requirement must be explicitly listed in the structured brief delivered as `assets/audio/music_production_brief.json`.

  (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`)

- [ ] **`assets/audio/music_sidecar_schema.json` stub** (`sound-dev-opensoftal`): create `assets/audio/music_sidecar_schema.json` as **valid JSON with NO inline `//` comments** (JSON does not permit `//` line comments — any such comment causes a parse error in strict JSON parsers). Use a `"_comment"` key to document the schema. The stub must be exactly:

  ```json
  {
    "_comment": "AI Town music sidecar schema v1 — canonical key names parsed by AudioSystem",
    "bpm": 90,
    "beats_per_bar": 4
  }
  ```

  Exit criterion: schema stub `assets/audio/music_sidecar_schema.json` exists at the flat asset path, is valid JSON (no `//` comments), and contains the canonical key names `"bpm"` and `"beats_per_bar"`.
- [ ] **Zone Loop Production Brief Gate** (locked before Phase 7 asset recording begins): the `sound-artist-opensoftal` must produce a brief for all 3 zone loop assets (`zone_residential`, `zone_commercial`, `zone_industrial`) specifying: (a) target duration **12–18 s seamless loop (hard cap 18 s)** — pre-load tier boundary is 20 s; authored duration must stay safely below this cap, (b) **loop silence boundaries: −60 dBFS at head and tail**, (c) **loudness target: −26 LUFS / −2 dBTP** (subtle background positional — should not compete with music stems), (d) mono, 44100 Hz, 16-bit, (e) intended mix level relative to ambient beds, (f) reference tracks or mood board, (g) **Zone loops must include a 100 ms fade-to-silence tail AND a 100 ms fade-from-silence head at loop boundaries — both boundaries MUST individually reach −60 dBFS or below** per `architecture/audio-architecture/audio-asset-formats.md`, (h) **combined silence window at loop boundary is exactly ~200 ms (100 ms tail + 100 ms head); this window MUST coincide with a natural rhythmic gap in the content. Authors must verify via DAW loopback playback that the 200 ms window does not cut across active musical content**, (i) **loop boundary must be verified via DAW loopback playback before file delivery** — the sound artist must play the file in a loop in the DAW, confirm the loop point is inaudible, and record that verification was performed in the delivery notes, (j) **OGG Vorbis encoding: minimum quality `-q 6` (~192 kbps VBR)** — confirm before delivery; lower quality settings are not acceptable for V1 zone loops. This brief must be reviewed and signed off here before Phase 7 zone loop recording is authorized. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/dynamic-soundscape.md`)
- [ ] **Vehicle SFX Production Brief Gate** (`sound-artist-opensoftal`, Phase 1 exit criterion — blocks Phase 4 vehicle SFX asset authoring): `sound-artist-opensoftal` must sign off that all vehicle engine SFX will be authored meeting the following requirements, which must be documented in the brief:
  - Vehicle engine SFX will be **OGG Vorbis, minimum 6 s, mono positional** — WAV 1–2 s loops are prohibited (audibly mechanical repeat at low pitch-shift ratios)
  - **Idle and move loop variants will be authored as a pair** per vehicle type — both loops must have compatible sonic character so they blend naturally when pitch-shifted
  - The **6 s minimum ensures the lowest-pitch loop (0.75× pitch-shift ratio for a stopped vehicle) is ≥ 4.5 s perceived duration**, staying below the loop-repeat perceptibility threshold. At 4–5 s loop duration, a 0.75× pitch produces ~3–3.75 s perceived — audibly mechanical. At 6 s, the 0.75× loop is ~4.5 s perceived, which is below the threshold.
  - This sign-off must be on record before Phase 4 vehicle SFX authoring begins. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/dynamic-soundscape.md`)
- [ ] **Stinger Production Brief Gate** (`sound-artist-opensoftal`, Phase 1 exit criterion — blocks Phase 4 stinger WAV authoring): `sound-artist-opensoftal` must sign off on all stinger asset production requirements:
  - Format: WAV PCM (mono, 1 channel) — OGG is PROHIBITED for stingers
  - Duration: `stinger_crisis` 2–4 s; `stinger_milestone` 2–3 s
  - Loudness target: −18 LUFS / −1 dBTP
  - Non-positional playback: `AL_SOURCE_RELATIVE = AL_TRUE` (flat mix, not 3D positioned)
  - Music ducking context: music ducks to 0.4 gain (~−8 dB) during stinger playback; ambient beds are NOT ducked (the `m_musicDuckGain` applies to music stems only)
  This sign-off must be on record before Phase 4 stinger WAV authoring begins. Phase 4 stinger WAV authoring is blocked until this brief is signed off.

- [ ] **Ambient Bed Production Brief Gate** (`sound-artist-opensoftal`, Phase 1 exit criterion — blocks Phase 4 placeholder ambient bed authoring): `sound-artist-opensoftal` must sign off that all ambient bed assets will be authored meeting the following requirements, which must be documented in the brief handed to the sound artist:
  - **THE SAMPLE-0 BOUNDARY CLICK-FREE CHECK IS THE PRIMARY QUALITY GATE.** At runtime, `AudioStream` calls `ov_pcm_seek(vf, 0)` before the 200 ms crossfade tail is reached — players never hear the tail. The 200 ms crossfade tail is a DAW-audition convenience only. Authors MUST verify the sample-0 boundary by seeking to sample 0 in the DAW player and confirming no click, transient, or silence gap at the restart point.
  - Ambient bed assets will include a **200 ms pre-baked crossfade tail** — this is a rendered overlap region at the loop boundary baked into the audio file. The loop point is at sample 0, meaning `ov_pcm_seek(vf, 0)` at loop end bypasses the 200 ms tail region and restarts from the true beginning of the audio content. The 200 ms tail is the pre-baked portion used during the constant-power blend in `AudioSystem`; the seek-to-0 approach means the tail is not literally played back but serves as the blend source during the overlap window.
  - Ambient beds **start at sample 0 with no silence at the start** — seamless sample-0 boundary. Any leading silence causes a gap at the loop restart point, which is audible at all crossfade speeds.
  - **OGG Vorbis encoding: minimum quality `-q 7` (~224 kbps VBR)**. Confirm before delivery. Lower quality settings are not acceptable for V1 ambient beds.
  - These requirements must be confirmed in writing as part of the brief. Phase 4 ambient bed placeholder authoring is blocked until this sign-off is on record. (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [ ] **`UIManagerModalTest` fixture TearDown contract** (`test-dev-cpp`): the `UIManagerModalTest` fixture in `tests/ui/` must define an explicit `TearDown()` that calls `ui_.reset()`. The explicit `ui_.reset()` in `TearDown()` is a defensive practice — the current declaration order already satisfies the destruction invariant automatically (`ui_` declared last is destroyed first in reverse order), but future fixture modifications could inadvertently reorder members. The explicit `TearDown()` makes the destruction contract immune to member reordering. **Required member declaration order** (enforced, not optional): `NiceMock<MockUIBackend> backend_` (first), `NiceMock<MockAudioSystem> audio_` (second), `NiceMock<MockCitySimulation> sim_` (third), `std::unique_ptr<UIManager> ui_` (last). `TearDown()` calls `ui_.reset()` before any mock member is destroyed (reverse declaration order destroys mocks after `ui_` is reset). This prevents UIManager's destructor from calling backend_.removeElement() or audio_ methods on an already-destroyed mock. **Mock policy for `UIManagerModalTest`**: ALL THREE mock members must use `NiceMock`:
  - `NiceMock<MockUIBackend> backend_` — UIManager construction calls `addStaticText()` for all panels; StrictMock would fail on unexpected construction-time calls not in the test's EXPECT_CALL setup
  - `NiceMock<MockAudioSystem> audio_` — UIManager may call audio methods on state transitions
  - `NiceMock<MockCitySimulation> sim_` — UIManager constructor queries simulation state; StrictMock causes SetUp() to fail immediately on any unregistered query

  **(Phase 5 deliverables — Phase 1 delivers fixture stub with TearDown() and NiceMock member declarations only; no test bodies. Test bodies are Phase 5 deliverables, consistent with `architecture/testing/testability-architecture.md`.)** This is a Phase 1 stub fixture deliverable — the test cases themselves are Phase 5 deliverables.

  **Phase 1 smoke test (REQUIRED)**: Phase 1 MUST include one test body in the fixture to verify construction/destruction works:

  ```cpp
  TEST_F(UIManagerModalTest, FixtureConstructsAndDestructsCleanly) {
      SUCCEED();
  }
  ```

  **Phase 1 event routing test stub (REQUIRED)**: Phase 1 must also include the following test stub in a `UIManagerEventTest` fixture (or `UIManagerModalTest` if no separate fixture exists) to verify Escape key routing when SettingsPanel is visible. Phase 1 body is a compile-only stub (`SUCCEED()`); the real verification body is a Phase 5 deliverable:

  ```cpp
  // Phase 1: compile-only stub — verifies the test name and fixture compile.
  // Phase 5 delivers the real body: when SettingsPanel is visible and Escape is pressed,
  // the branch taken depends on m_state:
  //   if (m_state == GameState::Paused)    → call PauseMenuPanel::show()
  //   if (m_state == GameState::MainMenu)  → call MainMenuPanel::show() (NOT PauseMenuPanel::show())
  // The structural if/else on m_state MUST be present in Phase 1 — adding it in Phase 5
  // would require structural changes to surrounding Priority-5 logic that may break Phase 1 tests.
  // Both branches are no-op stubs in Phase 1.
  TEST_F(UIManagerModalTest, EscapeClosesSettingsAndReturnsToPauseMenu) {
      SUCCEED();  // Phase 5: real Escape routing assertion for GameState::Paused branch
  }

  // Phase 1: compile-only stub — verifies the GameState::MainMenu branch of the Escape routing compiles.
  // Phase 5 delivers the real body: when SettingsPanel is visible and Escape is pressed and
  // m_state == GameState::MainMenu, the UIManager must call MainMenuPanel::show() (NOT PauseMenuPanel::show()).
  TEST_F(UIManagerModalTest, EscapeClosesSettingsAndReturnsToMainMenu) {
      SUCCEED();  // Phase 5: real Escape routing assertion for GameState::MainMenu branch
  }
  ```

  **Phase 1 scrim event-consumption test stub (REQUIRED)**: Phase 1 must also include the following test stub in `UIManagerModalTest` to verify that the scrim correctly blocks HUD clicks when a modal is active. Phase 1 body is a compile-only stub (`SUCCEED()`); the real verification body is a Phase 5 deliverable:

  ```cpp
  // Phase 1: compile-only stub — verifies UIManagerModalTest fixture compiles with this test name.
  // Phase 5 delivers the real body: assert that when a modal is active, a click event at a
  // non-modal position is consumed (UIManager::onEvent() returns true) and the HUD's toolbar
  // handler is NOT called.
  TEST_F(UIManagerModalTest, ScrimBlocksHUDClickWhenModalActive) {
      SUCCEED();  // Phase 5: real scrim event-consumption assertion
  }
  ```

  (ref: `architecture/testing/testability-architecture.md`, `architecture/ui-ux/input-arbitration.md`)

### Exit Criteria

- Application window opens with blank scene on Linux and Windows
- Camera pans, rotates, zooms correctly within pitch constraints
- `CameraController` unit tests pass (no display required): all 6 named test cases pass, including `CameraController_EdgeScroll_EnabledByDefaultInFullscreen`
- `LODSwapSmokeTest.SetMeshGrabDropContract` is registered under `requires-opengl` label and produces SKIP (not FAIL) under `xvfb-run` in Phase 1 — the `GTEST_SKIP()` body correctly defers the timing assertion to Phase 2 when a real GPU is available
- All five required `UIScaler` unit tests pass: `UIScaler_1280x720_LetterboxOffsets_ProjectsCorrectly`, `UIScaler_FullNative_NoOffset_ProjectsIdentity`, `UIScaler_PillarboxOffset_UnprojectsCenterCorrectly`, `UIScaler_MouseInTopBlackBar_VirtualY_ClampedToZero`, `UIScaler_GetViewportRect_ReturnsCorrectOffsets`. Compile-only stub `UIScaler_MouseInBottomBlackBar_VirtualY_ClampedToMax` also registered and passing (body is `SUCCEED()`; Phase 5 fills real assertion for bottom-bar clamp).
- All texture unit constants in `shader_constants.h` have values <= 15 (guaranteed per-stage availability in OpenGL 3.3 core)
- `loadSRGB()` stub body includes BOTH the `GL_CLAMP_TO_EDGE` wrap-mode TODO comment for `_billboard` textures AND the `GL_TEXTURE_MAX_LEVEL` dispatch table TODO comment. Verified by code review before Phase 2 begins.
- `IrrlichtUIBackend` compiles and links against `irrlicht` in a minimal smoke target; a trivial `addStaticText()` call returning a non-zero handle is verified by an `EDT_NULL` integration test labelled `integration`
- LOD spike **Checkbox A** result for `SMesh::addMeshBuffer()` grab/drop contract documented in `tests/rendering/lod_swap_smoke_test.cpp` with a one-line explanatory comment.
- LOD spike **Checkbox B** result for `CMeshSceneNode::setMesh()` grab/drop contract documented in `architecture/graphics-architecture/scene-graph-ownership.md`. **The Phase 2 TerrainChunk deliverable is BLOCKED on Checkbox B (not Checkbox A).** If `setMesh()` does NOT call `grab()`, the `drop()` call in the LOD swap sequence must be removed from `scene-graph-ownership.md` before any Phase 2 code uses `node->setMesh()`.
- Windows CI DLL verification step hard-fails on missing `Irrlicht.dll` — the Phase 0 baseline `ci.yml` hard-fail is already in place; the Phase 1 CMake post-build copy rule must be added so this existing hard-fail step passes.
- Windows CI DLL verification checklist extended to include `GLEW32.dll` once the Phase 1 `find_package(GLEW REQUIRED)` deliverable is complete. The exact PS 5.1-compatible PowerShell snippet (using `if (-not (Test-Path ...)) { exit 1 }` form) must be committed to `.github/workflows/ci.yml`. Without this, GLEW DLL missing on Windows CI passes silently and the `glewInit()` call crashes at runtime. **BLOCKING EXIT CRITERION (ATOMICITY — PR REJECTION CRITERION)**: The GLEW32.dll copy step (`add_custom_command` post-build rule) AND the PowerShell hard-fail check (`if (-not (Test-Path ...)) { exit 1 }`) MUST be atomic with the `find_package(GLEW REQUIRED)` + `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)` change — all four items must land in the SAME commit. This is a **PR rejection criterion**: any PR that splits these across commits MUST be rejected before merge, regardless of whether CI currently passes.
- `src/audio/audio_system.h` contains zero OpenAL includes — no `<AL/al.h>`, `<AL/alc.h>`, `<AL/alext.h>`, or any other OpenAL header, directly or transitively. Verified by `grep -r "AL/al" src/audio/audio_system.h` returning no matches.
- `simulation_types.h` compiles cleanly with the canonical 4-value `enum class SpeedMultiplier { Paused=0, x1=1, x3=2, x10=3 }` and `using SimSpeed = SpeedMultiplier;` — the SimSpeed/SpeedMultiplier relationship is resolved and no conversion is required at `m_audio->setSpeed(m_sim->getSpeed())` call sites. The 5-value form (`PAUSED/SLOW/NORMAL/FAST/VERY_FAST`) must NOT appear in `simulation_types.h`. The constant `kDefaultSimSpeed = SpeedMultiplier::x3` is defined in `simulation_types.h`.
- `shader_stub_compile_test` passes (not skips) on ubuntu-latest with xvfb before Phase 2 begins.
- GLEW availability spike result documented in `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (under "Phase 1 Spike Results") AND as a one-line comment in `src/rendering/render_system.h`. Phase 2 sRGB texture pipeline is blocked until this is on record.
- [x] Bootstrap oscillation design review complete: `gamedesign-lookandfeel` has signed off on the demand formula constants from the spec, with numerical results documented for all three canonical scenarios (blank map, all zones at tick 0, rapid C/I removal within ticks 0–5). Sign-off includes the Demolition-Induced Swing Exemption declaration for scenario 3 (tick T−1 → tick T swing across the demolition event is exempt; swings between ticks T and T+1 onward are subject to the ≤ 0.30 criterion).
- `tools/vehicle_atlas_registry.json` stub created at the canonical path `tools/vehicle_atlas_registry.json` with all V1 vehicle type assignments in the canonical schema: nested `diffuse_atlas` object (`atlas_file`, `grid`, `mip_levels`, `upload_path`), nested `normal_atlas` object (`atlas_file`, `grid`, `mip_levels`, `upload_path`, `_comment_normal_atlas`), nested `sprite_atlas` object, and `assignments` array (with `"vehicle_type"` keys — NOT `"vehicle_id"`). Schema conformance verified against `architecture/asset-standards/building-atlas-layout.md` and `architecture/asset-standards/3d-model-standards.md`.
- Phase 0 `audio_smoke_test` continues to compile and pass on both Linux and Windows CI with all Phase 1 audio headers in place. `MockAudioSystem` compiles against the Phase 1 `IAudioSystem.h`.
- `audio_tests` CMake target verifies `target_include_directories(audio_tests PRIVATE ...)` covers all 4 required paths: `tests/simulation/`, `src/interfaces/`, `src/audio/`, `${CMAKE_SOURCE_DIR}`. Verified by confirming `MockAudioSystem_InstantiatesCleanly` compiles and passes without path-qualified `#include` directives.
- `MockAudioSystem_InstantiatesCleanly` test in `tests/audio/audio_smoke_test.cpp` passes — confirms all 11 vtable methods are declared and `NiceMock<MockAudioSystem>` is fully constructible.
- `NiceMock<MockAudioSystem>` compiles cleanly in BOTH `audio_tests` AND `ui_tests` CMake targets (cross-target compilation verified). The MockAudioSystem compile-smoke test must be present in both targets. This verifies no ODR or include-path conflicts across the audio and UI CMake targets. **`vec3.h` and `camera_state.h` MUST be header-only with no companion `.cpp` files.** If any non-inline functions are needed, they must go into `aitown_sim` AND `aitown_sim` must be added to `target_link_libraries(ui_tests ...)`. Verify with `cmake --build build --target ui_tests` before Phase 1 closes. (ref: `architecture/testing/testability-architecture.md`)
- Music production brief delivered and on record: `sound-artist-opensoftal` has documented the shared root key and mode, confirmed 90 BPM / 4/4 time signature, confirmed 44100 Hz / 16-bit stereo as the authoring sample rate, provided the reference listening target, delivered the crossfade audibility sketch (bar boundaries, duration min 2 s default 3 s, gain curves: `gain_out = cos(t × π/2)`, `gain_in = sin(t × π/2)` where t ∈ [0, 1]), and **confirmed OGG Vorbis encoding at minimum `-q 8` (~256 kbps VBR)**. **Phase 4 placeholder OGG authoring is blocked until this brief is approved.**
- Zone Loop Production Brief on record: `sound-artist-opensoftal` has documented duration (**12–18 s, hard cap 18 s**), loop silence boundaries (−60 dBFS at head and tail), loudness target (−26 LUFS / −2 dBTP), format (mono, 44100 Hz, 16-bit), mix level, reference tracks, and **OGG Vorbis encoding at minimum `-q 6` (~192 kbps VBR)** for all 3 zone loop assets. Brief also documents the 100 ms fade-in/fade-out ramp requirement at loop boundaries. **Phase 7 zone loop recording is blocked until this brief is signed off.**
- Zone Loop Production Brief delivered and on record: `sound-artist-opensoftal` has documented target duration (12–18 s, hard cap 18 s), loop silence boundaries (−60 dBFS at head and tail), loudness target (−26 LUFS / −2 dBTP), format (mono, 44100 Hz, 16-bit), OGG Vorbis encoding minimum `-q 6` (~192 kbps VBR), and the 100 ms fade-in/fade-out ramp requirement. **Phase 7 zone loop recording is blocked until this brief is signed off here in Phase 1 exit criteria.**
- A placeholder entry for `graphics-artist-3d-model` pre-alignment gate exists in `implementation/phase-1.md` at Phase 1 exit, confirming the review is scheduled before Phase 6 asset production begins. The actual completed sign-off is a Phase 6 pre-condition (not a Phase 1 exit criterion), but the placeholder record must be present at Phase 1 close.
- Texture artist pre-alignment gate sign-off documented: `graphics-artist-2d-texture` has confirmed in writing (comment in `implementation/phase-1.md` or linked record) having reviewed `building-atlas-layout.md`, `2d-texture-standards.md`, and the Billboard Imposter Atlas, LOD2 Shell UV, and Vehicle UV V-flip sections of `3d-model-standards.md`. Phase 2 DDS texture production is blocked until this sign-off is on record.
- **2D-2 — `vehicles_sprite_atlas_d.dds` linear upload confirmed**: `vehicles_sprite_atlas_d.dds` is confirmed uploaded via the **linear path** (`loadLinear()` — IVideoDriver texture pool), NOT the sRGB raw GL path. Vehicle sprite atlases are stylised roof swatches (not photographic diffuse); sRGB decode is incorrect for this data. This is an explicit Phase 1 sign-off exit criterion: `graphics-artist-2d-texture` AND `graphics-dev-irrlicht` must confirm the `upload_path: "linear"` entry in `tools/vehicle_atlas_registry.json` `sprite_atlas` block is correctly applied at runtime.
- **2D-3 — Road surface texture two-party sign-off**: Road surface textures (`road_surface_d.dds` diffuse, `road_surface_n.dds` normal) require confirmation from BOTH parties before Phase 6 road texture production begins: (a) `graphics-artist-2d-texture` confirms the asset-side DDS format (sRGB DXT5 for diffuse, linear DXT5nm for normal) meets the spec in `architecture/asset-standards/2d-texture-standards.md`; (b) `graphics-dev-irrlicht` confirms the runtime upload path is correct — `road_surface_d.dds` uses the sRGB raw GL path (`loadSRGB()` with `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), `road_surface_n.dds` uses the linear path (`loadLinear()`). Both parties must sign off explicitly — a unilateral sign-off from either party alone is insufficient.
- After wiring `ui_tests`, run `lcov --summary coverage_filtered.info` in CI and record the Phase 1 baseline coverage percentage for `src/ui/`. This establishes the Phase 2 starting point. If `src/ui/` coverage is below 25% at Phase 1 exit, this is a **BLOCKING defect** (not MEDIUM risk) — indicates test registration or stub-body errors that must be corrected before Phase 2. Achievable minimum: 25% from UIManagerDrawOrderTest + 5 UIScaler tests + 6 CameraController tests.
- **`src/ui/` 25% coverage gate is BLOCKING** (`cicd-dev-github`): the coverage-linux CI step that checks `src/ui/` coverage must use `exit 1` (not `|| echo WARNING`) — builds MUST fail if `src/ui/` coverage falls below 25%. The `|| echo WARNING` form silently passes a broken coverage gate. If the current CI YAML uses `|| echo WARNING` for this check, it MUST be changed to `exit 1` in Phase 1 before Phase 1 closes. This is not a Phase 5 deferral — it is a Phase 1 blocking requirement. Phase 5 raises the floor further (from 25% to 80%); Phase 1 establishes the floor at 25% with blocking enforcement.
- Camera pitch sign-off confirmed: `architecture/asset-standards/3d-model-standards.md` Camera Pitch Range section documents the confirmed sign-off (pitch range [−70°, −20°], billboard bake midpoint −45°). Phase 1 checklist records this by cross-reference only. **Phase 6 billboard bake infrastructure MUST NOT begin without this sign-off on record in `3d-model-standards.md`.**
- `UIManagerDrawOrderTest` passes: all 10 draw slots called in correct Z-order sequence when `UIManager::draw()` is invoked. Verified by GMock `InSequence` matcher. **NiceMock required for ALL THREE mock members** (`NiceMock<MockUIBackend> backend_`, `NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`) — using StrictMock on any of these three members causes spurious failures on UIManager constructor calls that are not in the test's EXPECT_CALL setup.
- UIManager event dispatch chain is structurally correct: all 6 priority tiers present in `UIManager::onEvent()`, Priority 2 has both guards (blocking modal check AND CRITICAL toast visibility check) before calling `m_notifications->onEvent(event)`. Verified by code review before Phase 2 begins.
- [ ] All 6 ManualRNG self-tests pass: `ManualRNG_VerifyAllConsumed_ThrowsOnOverProvision`, `ManualRNG_VerifyAllConsumed_NoThrowWhenFullyConsumed`, `ManualRNG_EmptyIntSeq_ThrowsAtConstruction`, `ManualRNG_FloatSeqOutOfRange_ThrowsAtConstruction`, `ManualRNG_EmptyFloatSeq_ThrowsAtConstruction`, `ManualRNG_NextInt_OutOfRange_ThrowsAtCallTime`. Verified by: `ctest --test-dir build -LE 'integration|requires-opengl' -R ManualRNG --output-on-failure`
- After the `simulation_tests` CMake amendment: `ctest --test-dir build -LE 'integration|requires-opengl' -R Smoke --output-on-failure` MUST still discover and pass the Phase 0 smoke tests. Zero Smoke results means Phase 0 source files were dropped from the `add_executable` call — a blocking defect.
- [ ] GLEW vcpkg port existence verified via `gh api /repos/microsoft/vcpkg/contents/ports/glew?ref=$(jq -r '."builtin-baseline"' vcpkg.json)` — confirmed 200 HTTP response at the exact pinned baseline. A 404 response blocks Phase 1 merge.
- CI routing verification passed: `ctest --test-dir build -N -L '^integration$'` lists at least one test name (e.g., `IrrlichtUIBackendTest.AddStaticTextReturnsNonZeroHandle`). Zero-test discovery does NOT constitute a passing verification.
- `UIManagerModalTest` fixture `TearDown()` implementation stub committed and compiling, with `NiceMock` policy enforced for **ALL THREE** mock members (`NiceMock<MockUIBackend> backend_`, `NiceMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`) — using StrictMock on any of the three causes construction-time failures (Phase 5 fills in test cases; Phase 1 delivers the fixture structure).
- `UIManagerModalTest.FixtureConstructsAndDestructsCleanly` passes (CTest filter: `-R FixtureConstructsAndDestructsCleanly`).
- `simulation_types.h` includes `enum class CityRatingTier { Village, Town, City, Metropolis, Megalopolis }` and `ICitySimulation.h`'s `getCityRating()` returns `CityRatingTier`. `MockCitySimulation` includes `MOCK_METHOD(CityRatingTier, getCityRating, (), (const, override))`.
- CI `build-linux` includes integration-test-routing verification step that fails if 0 integration tests are discovered.
- CI `build-linux` includes `requires-opengl` label routing verification step that also produces non-zero test count (at least `LODSwapSmokeTest.SetMeshGrabDropContract` must be discovered). Zero-test discovery in the `requires-opengl` routing step does NOT constitute a passing verification.
- [ ] Two label-routing non-zero-discovery verification steps are added to `.github/workflows/ci.yml` in BOTH `build-linux` AND `coverage-linux` jobs (placed after CMake build, before first ctest execution): (1) verify `integration` label finds at least one test; (2) verify `requires-opengl` label finds at least one test. These steps are Phase 1 YAML deliverables — not just local verification procedures. Phase 1 CANNOT close without them committed.
- Linux GLEW artifact verification step present in `build-linux` and `coverage-linux` — fails if neither the manifest-mode path (`build/vcpkg_installed/x64-linux/lib/libGLEW.a`) nor the classic-mode path (`${VCPKG_ROOT}/installed/x64-linux/include/GL/glew.h`) is found after vcpkg install.
- [ ] Verify `coverage-linux` ccache key includes `-coverage` suffix: `${{ runner.os }}-ccache-coverage-${{ env.COMPILER_VERSION }}` — distinct from `build-linux` key. Confirm this is present in ci.yml.
- [ ] `lcov --remove` in `coverage-linux` CI job includes `--ignore-errors unused` — verified by inspecting committed `ci.yml` before Phase 1 closes. Omitting this flag causes exit-25 failures on patterns that match no files in Phase 1 (blocking `coverage-linux` on every build).
- [ ] Verify `build-linux` and `coverage-linux` `apt-get install` steps install parity-equal packages. Document any intentional differences (e.g. `lcov` is coverage-linux-only). Required before Phase 1 closes.
- `tools/validate_assets.py` exists at canonical path and CI asset validation job calls it without error.
- [ ] `validate-assets` job is defined in `.github/workflows/ci.yml` and wired into `all-checks-pass` `needs:` list (Phase 1+ form). The Phase 0 ci.yml comment `# validate-assets added in Phase 6` MUST be removed and replaced with the actual job definition. This is a hard blocking exit criterion — Phase 1 CANNOT close without this change committed to ci.yml.
- **CI-4 — Markdown lint gate must remain green**: Markdown lint (`markdownlint` on `architecture/**/*.md`, `implementation/*.md`, and `CLAUDE.md`) is part of the `all-checks-pass` gate and MUST remain green at all times. This check was established in Phase 0 and MUST NOT be removed or downgraded in subsequent phases. Any Phase 1 changes to `implementation/` files must not introduce markdownlint violations (fenced code blocks inside list items without blank lines, bare URLs, unclosed ATX headings, etc.). Exit criterion: `markdown-lint` job in `all-checks-pass` `needs:` list is present and green at Phase 1 close. Removing `markdown-lint` from `all-checks-pass` is a PR rejection criterion in all subsequent phases.
- Three `KeyBindings` unit tests pass (CTest filter: `ctest --test-dir build -LE 'integration|requires-opengl' -R KeyBindings`): `KeyBindings_IsReservedKey_Q_ReturnsTrue`, `KeyBindings_IsReservedKey_E_ReturnsTrue`, `KeyBindings_IsReservedKey_W_ReturnsFalse`.
- Schema stub `assets/audio/music_sidecar_schema.json` exists at the flat asset path, is valid JSON (no `//` comments), uses a `"_comment"` key for documentation, and contains the canonical key names `"bpm"` and `"beats_per_bar"`.
- `terrain_tests` compiles with `terrain_chunk.h`, `ITerrainRNG.h`, AND `terrain_generator.h` included; `mock_terrain_rng.h` instantiates without error; `MockTerrainRNG` is a manual stub with `std::mt19937_64` engine (not GMock `MOCK_METHOD`). `TerrainGenerator` stub constructors use empty bodies `{}` (not `= delete`).
- Verify that `ctest --test-dir build -N -L '^unit$'` lists `terrain_tests` cases within 60 seconds on a coverage-instrumented build. The `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)` call specifies `DISCOVERY_TIMEOUT 60` per `architecture/testing/framework.md`.
- `src/audio/al_check.h` exists at the canonical path with two inline no-op stubs (`alCheckError` and `alcCheckError`); no OpenAL headers included transitively.
- `UIManagerModalTest.EscapeClosesSettingsAndReturnsToPauseMenu` compile-only stub compiles and passes (Phase 5 delivers real assertion for GameState::Paused branch).
- `UIManagerModalTest.EscapeClosesSettingsAndReturnsToMainMenu` compile-only stub compiles and passes (Phase 5 delivers real assertion for GameState::MainMenu branch).
- `UIManagerModalTest.ScrimBlocksHUDClickWhenModalActive` compile-only stub compiles and passes (Phase 5 delivers real assertion).
- `CameraController.EdgeScrollActivatesAt20pxBand` compile-only stub compiles and passes in headless CI (Phase 5 delivers real assertion). **Windowed-mode-only means the behavior exercised applies only when `startInFullscreen=false` — it does NOT mean the test is skipped in headless CI. The `SUCCEED()` body passes without a display.**
- `CameraController.EdgeScrollDisabledByDefaultInWindowed` compile-only stub compiles and passes in headless CI (Phase 5 delivers real assertion). **Windowed-mode-only means the behavior exercised applies only when `startInFullscreen=false` — it does NOT mean the test is skipped in headless CI. The `SUCCEED()` body passes without a display.**
- `Minimap` stub includes `getBounds() const` method returning a zero-area rect; `UIManagerDrawOrderTest` compiles with Minimap stub in the UIManager fixture.
- Stinger Production Brief on record: WAV PCM format, mono channel, `stinger_crisis` 2–4 s / `stinger_milestone` 2–3 s duration, −18 LUFS / −1 dBTP loudness, non-positional playback (`AL_SOURCE_RELATIVE = AL_TRUE`) confirmed. Phase 4 stinger WAV authoring blocked until signed off.
- Vehicle SFX Production Brief on record: `sound-artist-opensoftal` has confirmed OGG Vorbis minimum 6 s, mono positional, idle+move pair per vehicle type, and 6 s minimum rationale. Phase 4 vehicle SFX authoring blocked until this brief is signed off.
- Ambient Bed Production Brief on record: `sound-artist-opensoftal` has confirmed **sample-0 boundary click-free check as the primary quality gate** (authors MUST verify by seeking to sample 0 in DAW and confirming no click/transient/silence gap), **200 ms pre-baked crossfade tail** (rendered overlap region at loop boundary; loop point at sample 0; `ov_pcm_seek(vf, 0)` bypasses the tail region at restart), sample-0 start, and **OGG Vorbis encoding at minimum `-q 7` (~224 kbps VBR)** for all ambient bed assets. Phase 4 ambient bed authoring blocked until this brief is signed off.
- `tools/vehicle_atlas_registry.json` schema sign-off on record: `graphics-artist-2d-texture`, `graphics-dev-irrlicht`, AND `graphics-artist-3d-model` have all confirmed the JSON schema structure (nested `diffuse_atlas`/`normal_atlas`/`sprite_atlas` objects, `"vehicle_type"` key in assignments array, `normal_atlas` object with `mip_levels: 4` and `upload_path: "linear"`, `sprite_atlas` stub object with 256×256 DXT5 spec) is correct. `graphics-artist-3d-model` has confirmed LOD0/LOD1 mesh UV assignments match cell bounds. UV island sign-off deferred to Phase 6.
- **`tools/vehicle_atlas_registry.json` three-party sign-off on record** from ALL THREE parties (`graphics-artist-2d-texture`, `graphics-dev-irrlicht`, `graphics-artist-3d-model`). The 3D model artist's sign-off must confirm: (a) V-flip convention documented correctly; (b) atlas cell boundary UV formulas mathematically correct for 4×4 diffuse and 8×8 normal grids; (c) all five V1 vehicle type row/column assignments are correct. Schema JSON conformance check alone does NOT constitute three-party sign-off.
- `NiceMock<MockAudioSystem>` compiles in BOTH `audio_tests` AND `ui_tests` CMake targets. Cross-target compilation verified before Phase 1 closes. For `ui_tests` specifically, the vtable completeness check is satisfied by `UIManagerModalTest.FixtureConstructsAndDestructsCleanly` — this test constructs `UIManager` with `NiceMock<MockAudioSystem> audio_` as a fixture member, exercising the full vtable. A `NiceMock<MockAudioSystem>` that is merely declared but never constructed in `ui_tests` does NOT satisfy this criterion.
- Verify cross-target `NiceMock<MockAudioSystem>` compilation: `cmake --build build --target audio_tests && cmake --build build --target ui_tests` both link without error. This verifies compile-time vtable completeness beyond the runtime `FixtureConstructsAndDestructsCleanly` test.
- `transitionToGameOver()` Phase 1 stub contains the Sandbox guard: `if (m_gameMode != GameMode::Scenario) return;` — verified by code review before Phase 2 begins.

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `RenderSystem`, Irrlicht device lifecycle, render loop, LOD smoke test structure (`SUCCEED()` placeholder body until spike resolves; LOD spike contingency branch documented), `IrrlichtUIBackend` smoke target, `TextureCache` skeleton (all 3 pool structures including `loadLinear` stub; `loadSRGB()` stub body comment for `GL_CLAMP_TO_EDGE`; mip control: `GL_TEXTURE_MAX_LEVEL=0` for `_lm`, `GL_TEXTURE_MAX_LEVEL=3` for `_billboard`; `m_driverType` member), GLSL shader stubs (`#version 130` mandatory), GL capability query guard (`glewInit()` two-condition DISPLAY-aware skip guard in shader test; RELEASE fallback: `EDT_NULL` recreate + user-facing error dialog), `find_package(GLEW REQUIRED)` + `GLEW::GLEW` linkage in CMakeLists.txt; `WallClock.cpp` implementation; GLEW availability spike (result documented in `irrlicht-device-lifecycle.md`, NOT `scene-graph-ownership.md`) |
| `gamedesign-ux` | `UIManager` shell (NotificationManager-first construction order; `m_mainMenu->show()` call in constructor; all 10 draw-order slots wired; all 6 event dispatch priority tiers with Priority 2 dual-guard; `transitionToGameplay(GameMode)`; `transitionToGameOver()` Sandbox guard in stub; `showSettings()` stub; `BudgetDetailPanel` NOT in UIManager draw-order — owned by HUD), HUD class stub (4-param constructor: `IUIBackend*`, `IAudioSystem*`, `IClock*`, `ICitySimulation*`; `m_audio` stored; `m_clock`/`m_sim` stored; `m_unsavedDotHandle{kInvalidUIElement}` member; HUD owns `BudgetDetailPanel` internally), `UIScaler` (6-parameter constructor, `VirtualPoint unproject()` nested struct), `IUIBackend` interface design, `MockSimulationPauser` (Phase 5 test bodies; Phase 1 stub only), `NotificationManager` stub (constructor takes `ICitySimulation*` not `ISimulationPauser*`; Phase 5 signatures locked: `postCritical(title, body)` / `postNormal(title, body)`; `draw()` calls sentinel via `panel_sentinel_handles.h` test-only header — NOT a direct `kSentinelDraw` in production header), `Minimap` stub with `getBounds() const` method |
| `test-dev-cpp` | `CameraController` unit tests (all 6 named cases including `CameraController_EdgeScroll_EnabledByDefaultInFullscreen`; compile-only stubs for `CameraController.EdgeScrollActivatesAt20pxBand` and `CameraController.EdgeScrollDisabledByDefaultInWindowed` — windowed-mode-only tests), `UIManagerDrawOrderTest` (NiceMock for ALL THREE mocks: `NiceMock<MockUIBackend>`, `NiceMock<MockAudioSystem>`, `NiceMock<MockCitySimulation>`; GMock InSequence, all 10 draw slots; includes `DrawOrder_ModalActive_ScrimAndModalFireAfterPanels` for slots 9 and 10 coverage), `MockRenderer`, `MockUIBackend`, `MockCitySimulation` wiring (including `getSpeed` returning `SpeedMultiplier`, `getTotalPopulation`, `getConsecutiveDeficitMonths`, `getTrafficDemandFactor` returning `float`, `getDensityUnlockState`, `getCityRating` returning `CityRatingTier` mocks), `ManualRNG` (6 self-tests, Phase 1 CMake registration, fixture initialization: `ManualRNG rng_{{0}}` — never `rng_{}` or `rng_{{}}`), `ManualClock`, `NullSimulationPauser`, `LoanTerms.h` stub, `KeyBindings.h` stub (`tests/ui/key_bindings_test.cpp` — 3 named test cases), `ui_tests` (consolidated Phase 1 form: 5 source files), `integration_tests`, and `terrain_tests` CMake targets, `ITerrainRNG.h` stub, `MockTerrainRNG` (manual stub with `std::mt19937_64`, NOT GMock), `UIManagerModalTest` fixture TearDown stub (NiceMock for ALL THREE mocks: `NiceMock<MockUIBackend>`, `NiceMock<MockAudioSystem>`, `NiceMock<MockCitySimulation>`; `FixtureConstructsAndDestructsCleanly` smoke test body; `EscapeClosesSettingsAndReturnsToPauseMenu` compile-only stub (Paused branch); `EscapeClosesSettingsAndReturnsToMainMenu` compile-only stub (MainMenu branch); `ScrimBlocksHUDClickWhenModalActive` compile-only stub), `panel_sentinel_handles.h` test-only header |
| `sound-dev-opensoftal` | Verify and lock `IAudioSystem.h` (11 method signatures, `#include "simulation_types.h"`, `SimSpeed` vs `SpeedMultiplier` relationship documented); verify and extend `audio_types.h` (canonical declaration order: constants first, `StingerType` enum second, `static_assert` third; pool-index WARNING comment; `kZoneLoopMaxPreloadDurationSeconds = 18.0f`); author `sound_ids.h` with corrected grouping (non-contiguous grouping comment block, SFX_ZONE_UPGRADE and SFX_SERVICE_DEGRADE in Non-positional group; SFX_FIRE_ALERT and SFX_POLICE_ALERT in Positional group; Zone Loop prohibition comment; SFX_BUDGET_WARN annotated; MusicTrackId constants; stinger NOTE comment); author `MockAudioSystem` in `tests/simulation/mock_audio_system.h` with all 11 methods; author `AudioSystem` stub header `src/audio/audio_system.h` (no AL includes; Phase 4 main-thread bind comment clarified as REQUIRED; SHUTDOWN CONTRACT); create `src/audio/al_check.h` no-op stub (no AL headers); author `assets/audio/music_sidecar_schema.json` stub (valid JSON, `_comment` key, no `//` comments); verify `audio_tests` include directories cover all 4 required paths |
| `sound-artist-opensoftal` | Deliver music production brief (shared root key+mode, 90 BPM confirmation, 44100 Hz / 16-bit stereo confirmation, reference listening target, crossfade audibility sketch with exact formula `gain_out = cos(t × π/2)`, `gain_in = sin(t × π/2)`, OGG Vorbis min `-q 8` encoding) — Phase 1 exit criterion blocking Phase 4 OGG authoring; deliver Zone Loop Production Brief (zone_residential / zone_commercial / zone_industrial specs including 100 ms fade ramp requirement, 200 ms combined silence window, DAW loopback verification) — blocks Phase 7 zone loop recording; deliver Stinger Production Brief (WAV PCM mono, `stinger_crisis` 2–4 s / `stinger_milestone` 2–3 s, −18 LUFS / −1 dBTP, `AL_SOURCE_RELATIVE = AL_TRUE`) — blocks Phase 4 stinger WAV authoring; deliver Vehicle SFX Production Brief (OGG Vorbis, min 6 s, mono, idle+move pair, 6 s minimum rationale) — blocks Phase 4 vehicle SFX authoring; deliver Ambient Bed Production Brief (sample-0 boundary click-free as primary quality gate; 200 ms pre-baked crossfade tail at loop boundary, sample-0 start, loop point at sample 0, `ov_pcm_seek(vf,0)` bypass contract, DAW seek-to-sample-0 verification mandatory) — blocks Phase 4 ambient bed authoring |
| `gamedesign-lookandfeel` | Bootstrap oscillation design review gate (analytical sign-off with numerical results for all 3 scenarios including Demolition-Induced Swing Exemption declaration for scenario 3, with contingency path documented before Phase 2); SimulationConstants values review and sign-off (**six** game-design spec files cross-referenced: `economy-model.md`, `simulation-time.md`, `traffic-system.md`, `zoning-system.md`, `population-density-growth.md`, `service-coverage.md`; exhaustive list including `service_uncovered_desirability_penalty_per_tick`, `service_recovery_desirability_per_tick`, `R_raw_material_rate=0.05`, `C_goods_consumption_rate=0.25`, `adjacency_commercial_residential_bonus` and `adjacency_industrial_penalty` from `zoning-system.md`) before Phase 2 |
| `graphics-artist-3d-model` | Camera pitch sign-off gate (confirm −70° to −20° range is final; documented in BOTH this checklist AND `architecture/asset-standards/3d-model-standards.md` Camera Pitch Range section); 3D model artist pre-alignment gate (full spec review and sign-off of `3d-model-standards.md` before Phase 6); co-own `vehicle_atlas_registry.json` schema (Phase 1: V-flip convention, 4×4 UV formulas, 8×8 normal UV formulas, five vehicle type assignments — UV island placement verification is Phase 6 only, no meshes exist in Phase 1); note: terrain geometry is procedurally generated — artist role is scoped to buildings, vehicles, and props only |
| `graphics-artist-2d-texture` | Texture artist pre-alignment gate (review and sign off `building-atlas-layout.md` including vehicle diffuse vs sprite atlas distinction, `2d-texture-standards.md`, AND billboard/LOD2/vehicle UV sections of `3d-model-standards.md` before Phase 2 DDS production begins; road marking atlas upload path confirmed as LINEAR; sign-off documented in writing in `implementation/phase-1.md`); create `tools/vehicle_atlas_registry.json` stub (nested schema per `building-atlas-layout.md`: `diffuse_atlas`/`normal_atlas`/`sprite_atlas` objects, `assignments` array with `"vehicle_type"` keys — NOT `"vehicle_id"`; schema sign-off with `graphics-dev-irrlicht` before Phase 1 closes) |
| `cicd-dev-github` | Verify `integration_tests` target routes correctly (run `ctest -N -L '^integration$'`, confirm at least one test name listed — zero-test discovery is not a pass); verify `requires-opengl` label routing also produces non-zero discovery; confirm the Phase 0 baseline `Irrlicht.dll` hard-fail in `ci.yml` is satisfied by the Phase 1 CMake post-build copy rule (target is `aitown_render`, co-landing required); add `GLEW32.dll` PS 5.1-compatible hard-fail snippet to Windows DLL verification step (ATOMICITY — PR REJECTION CRITERION: same commit as `find_package(GLEW REQUIRED)` + `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)` + CMake DLL copy rule — all four items atomic); add glew vcpkg port CI verification step to `build-windows`; add `libglew-dev` to `build-linux` AND `coverage-linux` apt-get install steps; add Linux GLEW artifact verification step (manifest-mode primary path + classic fallback) to `build-linux` and `coverage-linux`; add CI routing negative-case detection step for `integration` label to `build-linux`; add `requires-opengl` routing verification step to `build-linux`; **add same `integration` AND `requires-opengl` label routing verification steps to `coverage-linux` as are present in `build-linux` (parity required)**; add `${{ github.sha }}` suffix to all Phase 1 upload-artifact step names; confirm glew vcpkg port exists at current VCPKG_COMMIT_ID before committing; confirm at least one integration test is discovered and passes before Phase 1 exit criteria are declared met; review `find_package(GLEW REQUIRED)` CMakeLists change; own `src/ui/` lcov baseline step in `coverage-linux` job (blocking exit 1 at 25% floor — NOT `&#124;&#124; echo WARNING`); create `tools/validate_assets.py` stub and register `validate-assets` CI job (co-lands in same PR; `actions/setup-python` SHA must be resolved via `gh release view v5 --repo actions/setup-python` before merge — placeholder `@<SETUP_PYTHON_SHA>` MUST NOT be committed) |

### Dependencies

- Requires Phase 0 complete

### Sequencing Notes

- **Intra-phase ordering**: `IAudioSystem.h` (authored by `sound-dev-opensoftal`) must be committed before `UIManager` shell implementation begins — `UIManager` constructor accepts `IAudioSystem*`. `ICitySimulation.h` is now a Phase 1 deliverable (see above); `UIManager` constructor may use the full interface definition rather than a forward declaration. **UIManager constructor signature (4 parameters)**: `UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)`. The `clock` parameter is passed to `NotificationManager` at construction (for dismiss-after-5s timing) and to the `HUD` component for grace-period and undo-countdown displays. Production code passes `WallClock`; test fixtures inject `ManualClock`. **UIManager constructor signature is 4-parameter: `UIManager(IUIBackend*, IAudioSystem*, ICitySimulation*, IClock*)` — confirmed in `architecture/ui-ux/ui-manager.md`.** `UIManager` passes `m_audio` as the second argument when constructing `HUD`: `HUD(m_backend, m_audio, m_clock, m_sim)` (4-parameter form).
- **GATE before Phase 3**: `gamedesign-lookandfeel` must sign off on all `SimulationConstants` values (starting revenue calibration $8–12K target at 50% occupancy, demand formula constants, loan thresholds) before Phase 3 coding begins. This is a one-document design review that prevents constant changes mid-Phase-3 from breaking tests written against prior values.

### Risks & Spikes

- **RISK — HARD BLOCKING**: Scene graph LOD swap spike measurement must be completed and documented before Phase 1 exits. **Spike**: inspect `source/Irrlicht/CMeshSceneNode.cpp` to confirm `setMesh()` `grab()`/`drop()` calls and measure LOD swap timing in a profiling run. **HARD BLOCKING EXIT CRITERION**: If any LOD swap takes >2 ms in a profiling run, the spike contingency path (scene node destroy/recreate) must be evaluated and a mitigation decision documented before Phase 1 may close. This is not a soft contingency note — it is a blocking gate. The spike result must record: (a) whether `setMesh()` calls `grab()` on the new mesh, (b) measured time of a `setMesh()` call in release build, (c) decision record ("within budget" or "contingency path selected: destroy/recreate"). Phase 1 MUST NOT close without this documented.
- **RISK**: `EDT_OPENGL` unavailable on some CI runners. **Spike**: confirm Mesa OpenGL is installed and functional under `xvfb-run` on `ubuntu-latest`.
- **RISK**: `SMesh::addMeshBuffer()` grab/drop contract may differ in vendored Irrlicht build — unlike `setMesh()`, the spec's smoke test body does not call `drop()` on the `SMeshBuffer` after `addMeshBuffer()`. **Spike**: inspect `source/Irrlicht/SMesh.h` to confirm `addMeshBuffer()` calls `grab()` on the buffer. If it does not, add `drop()` after `addMeshBuffer()` in the smoke test body and document the convention. **Gate**: the spike result must be documented in `architecture/graphics-architecture/scene-graph-ownership.md` before Phase 2 TerrainChunk work begins. If the spike changes the camera pitch range or LOD swap distances from currently documented values, `graphics-artist-3d-model` must be explicitly notified before the updated spike result is committed.
- **RISK**: GLEW availability in vendored Irrlicht unknown. **Spike**: inspect `COpenGLDriver.cpp` to confirm GLEW bundling. Results recorded in `src/rendering/render_system.h` (one-line code comment) AND `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (under "Phase 1 Spike Results" subsection) — NOT `scene-graph-ownership.md`. **Phase 2 sRGB texture pipeline is BLOCKED on this spike result.**
- **RISK → FORMALIZED DESIGN REVIEW GATE (Phase 1) + DEFERRED CODE SPIKE (Phase 3)**: Demand coupling bootstrapping decay may cause oscillation in early game at 3× speed (blank map, ticks 0–5). **Phase 1 gate (analytical)**: `gamedesign-lookandfeel` reviews the demand formula constants from `architecture/game-design/zoning-system.md` and confirms analytically that oscillation (demand_factor swing > 0.3 in consecutive ticks) does not occur given the linear decay formula — no code execution required. Sign-off requires numerical results for all three canonical scenarios (blank map, all zones at tick 0, rapid C/I removal within ticks 0–5) with Demolition-Induced Swing Exemption applied to scenario 3. Owner: `gamedesign-lookandfeel`. This is a Phase 1 exit criterion. **Phase 3 gate (code execution)**: once `CitySimulation` exists (Phase 3), run it for 10 ticks at 3× speed on a blank map; trace `demand_factor` values for R/C/I per tick; document whether oscillation occurs and record final bootstrap decay parameter values in `architecture/game-design/zoning-system.md`. If oscillation is confirmed, `gamedesign-lookandfeel` adjusts the decay constant before Phase 3 test constants are locked. Owner: `test-dev-cpp` (execution) + `gamedesign-lookandfeel` (sign-off). Phase 3 gate is a Phase 3 pre-merge criterion. (ref: `architecture/game-design/zoning-system.md`)
