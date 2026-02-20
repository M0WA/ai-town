## Phase 2: GL Capability, TextureCache Skeleton & Shader Infrastructure

### Goal

Add all OpenGL capability queries, the `TextureCache` three-pool skeleton, GLSL shader stubs, and the LOD smoke-test infrastructure — building directly on top of the Irrlicht device shell established in Phase 1 and giving Phase 4 terrain a fully-typed `TextureCache` interface to build against.

### Deliverables

#### GLEW & GL Capability Queries

- [ ] **GLEW availability spike** (`graphics-dev-irrlicht`): inspect the vendored Irrlicht build's `COpenGLDriver.cpp` to confirm whether GLEW is bundled and whether `glewIsExtensionSupported()` is exposed. If Irrlicht was compiled without GLEW (custom build using ARB string parsing), `glewIsExtensionSupported()` will not link — replace all calls with `glGetString(GL_EXTENSIONS)` string matching or `IVideoDriver::queryFeature()`. Document the confirmed extension query path in BOTH `src/rendering/render_system.h` (one-line code comment, e.g., `// GLEW available in vendored Irrlicht — using glewIsExtensionSupported()`) AND `architecture/graphics-architecture/irrlicht-device-lifecycle.md` under a "Phase 1 Spike Results" subsection. **Phase 4 sRGB texture pipeline is BLOCKED on this spike result.** Owner: `graphics-dev-irrlicht`.

  **H19 — GLEW spike file reference correction**: the spike result document location is `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (under "Phase 1 Spike Results") — NOT `architecture/graphics-architecture/scene-graph-ownership.md`. Any reference in this phase to recording the GLEW availability spike result in `scene-graph-ownership.md` is incorrect and must be treated as referring to `irrlicht-device-lifecycle.md` instead.

  **sRGB format tokens if GLEW absent**: If GLEW is unavailable, the sRGB internal format tokens must be defined manually as their OpenGL specification hex literals before `glCompressedTexImage2D` is called: `#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C` and `#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4E`. These values are from the `EXT_texture_sRGB` extension spec and are stable across platforms.

- [ ] **`find_package(GLEW REQUIRED)` in CMakeLists.txt root** (`graphics-dev-irrlicht` + `cicd-dev-github`): add `find_package(GLEW REQUIRED)` to the CMakeLists.txt root and `GLEW::GLEW` to `aitown_render`'s `target_link_libraries`. This is required for the GL capability query initialization sequence in `RenderSystem`. **GLEW vcpkg port existence verification** (`cicd-dev-github`): before committing `find_package(GLEW REQUIRED)` and any `vcpkg.json` change, run `gh api /repos/microsoft/vcpkg/contents/ports/glew?ref=$(jq -r '."builtin-baseline"' vcpkg.json)` and confirm a 200 response. If the port is absent at the current baseline, `VCPKG_COMMIT_ID` and `builtin-baseline` must be updated before Phase 2 CI will pass. This pre-verification is mandatory per `architecture/ci-cd/dependency-management.md`. Owner: `graphics-dev-irrlicht` with `cicd-dev-github` reviewing the CMakeLists change. (ref: `architecture/ci-cd/dependency-management.md`)

- [ ] **Consolidated GL capability query guard**: ALL OpenGL capability queries (`GL_MAX_TEXTURE_SIZE`, `GL_EXT_texture_sRGB` extension check, `GL_EXT_texture_filter_anisotropic` extension check, and any others) must be consolidated into a single initialization sequence in `RenderSystem::init()` guarded by an `EDT_NULL` pre-check. Under `EDT_NULL`: set `m_maxTextureSize = 2048`, `m_maxAnisotropy = 1.0f`, `m_srgbTextureSupported = false` — no GL calls made. **Under a real OpenGL device (non-EDT_NULL): call `glewInit()` as the FIRST action in this initialization sequence** before any `glewIsExtensionSupported()` or `glGetIntegerv()` call — GLEW requires initialization to populate its function pointer table; calling `glewIsExtensionSupported()` before `glewInit()` causes a null-function-pointer crash. Check `glewInit()` return value using the two-tier handling: (a) if it returns `GLEW_OK`, proceed with extension queries normally; (b) if it returns `GLEW_ERROR_NO_GL_VERSION` specifically, log WARNING and continue — function pointers may still be valid; proceed with extension queries but log that GLEW is in degraded state; (c) if it returns any other non-`GLEW_OK` code, treat as fatal in DEBUG (abort) and in RELEASE: set `m_maxTextureSize = 2048` as the safe default — **NO `glGetIntegerv` call on the RELEASE path** (the GL context is not guaranteed to be in a valid state when `glewInit` fails; calling `glGetIntegerv` with a broken function-pointer table produces undefined behaviour), then recreate `IrrlichtDevice` with `EDT_NULL`, show a user-facing error dialog "OpenGL initialisation failed", and continue in headless mode using safe defaults: `m_srgbTextureSupported = false`, `m_maxAnisotropy = 1.0f`, `m_maxTextureSize = 2048`. Leaving an OpenGL device with broken GLEW function pointers active is incorrect — it will crash on any subsequent GL extension call. For the RELEASE fallback `EDT_NULL` path, skip the `GL_MAX_TEXTURE_SIZE` query entirely. Under `GLEW_OK` and `GLEW_ERROR_NO_GL_VERSION` paths: call `glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize)` after `glewInit()` returns — `GL_MAX_TEXTURE_SIZE` is a core OpenGL 1.0 entrypoint resolved via the platform's native GL dispatch table, NOT through GLEW's function-pointer table. The glewInit failure path must be documented in `src/rendering/render_system.h` with a comment. The guard sequence and order must be documented in `src/rendering/render_system.h` before Phase 4 graphics work begins. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`, `architecture/asset-standards/2d-texture-standards.md`)

- [ ] `GL_MAX_TEXTURE_SIZE` queried **immediately after** `createDevice()`, stored as `m_maxTextureSize`; `EDT_NULL` guard initializes to 2048 without GL query (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [ ] `GL_EXT_texture_sRGB` extension presence checked via `glewIsExtensionSupported()` immediately after `createDevice()` in `RenderSystem::init()`; result stored as `bool m_srgbTextureSupported`; `isSRGBTextureSupported() const` accessor exposed; `EDT_NULL` guard initializes to `false` (no GL context). `TextureCache` reads this flag at construction to select the upload path. (ref: `architecture/graphics-architecture/texture-cache.md`)

- [ ] `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` queried after checking `GL_EXT_texture_filter_anisotropic` extension presence via `glewIsExtensionSupported()`; if extension absent, log warning, set `m_maxAnisotropy = 1.0f`, and skip `glGetFloatv`; if extension present, query and store as `m_maxAnisotropy`; `EDT_NULL` guard initializes to `1.0f` unconditionally WITHOUT any extension check or GL call. (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [ ] **`getGPUProgrammingServices()` null check**: `getGPUProgrammingServices()` must be null-checked before any `addHighLevelShaderMaterialFromFiles()` call — return early for `EDT_NULL` driver; log and abort in debug builds if null on a non-EDT_NULL driver (shader failure is fatal in debug builds per `architecture/graphics-architecture/shader-loading.md`). (ref: `architecture/graphics-architecture/shader-loading.md`)

#### TextureCache Skeleton

- [ ] `TextureCache` class skeleton (stub) in `src/rendering/texture_cache.h`: must include all three pool structures per `architecture/graphics-architecture/texture-cache.md`:
  - `m_driverType` member of type `irr::video::E_DRIVER_TYPE`, initialized from the constructor parameter (e.g., `explicit TextureCache(irr::video::E_DRIVER_TYPE driverType)` → `m_driverType{driverType}`). **This member is required in Phase 2** so that the `EDT_NULL` guard on the sRGB upload path in Phase 4 can check `m_driverType == irr::video::EDT_NULL` without requiring a header change.
  - `m_srgbTextures` (`std::unordered_map<std::string, SRGBEntry>` stub — raw `GLuint` pool, separate from the linear `ITexture*` pool)
  - `m_splatMaps` (`std::unordered_map<std::string, SplatEntry>` stub — raw `GLuint` pool for terrain splat maps). **Canonical member name is `m_splatMaps` per `texture-cache.md` eviction code — do NOT use `m_splatMapTextures`.**
  - Eviction interface method stubs: `releaseLinear(ITexture*)`, `releaseLinear(const std::string& key)`, `releaseSRGB(const std::string& filename)`, `releaseSplatMap(const std::string& filename)` (no-op stub), `evictUnreferenced()` (covers all three pools)
  - Stub accessors: `getSRGBGLuint(const std::string&) const` returning `GLuint{0}`, `getSplatMapGLuint(const std::string&) const` returning `GLuint{0}` (parallel to `getSRGBGLuint`). **NOTE (Irrlicht-2)**: The sRGB accessor is canonically named `getSRGBGLuint` per `architecture/graphics-architecture/texture-cache.md`. An earlier draft of `shader-loading.md` referenced `getGLuint` — that name has been corrected to `getSRGBGLuint` in the spec; the plan must use `getSRGBGLuint` consistently. If any call site uses `getGLuint`, it must be renamed.
  - Stub load methods: `loadSplatMap(const std::string& path)` returning sentinel `GLuint{0}`; `loadLinear(const std::string& path)` returning `ITexture* {nullptr}`; **`loadSRGB(const std::string& path, GLenum format)` returning sentinel `GLuint{0}`** — the `GLenum format` parameter selects between `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` (opaque diffuse) and `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` (transparent diffuse)

  **Irrlicht-3 — "No real GL calls" clarification**: "No real GL calls in Phase 2" means no actual GPU texture uploads happen — `loadSRGB()` does NOT call `glCompressedTexImage2D`, `glGenTextures`, or any other GL function that requires an active context. However, the mip dispatch rules (which DDS filename suffix routes to which upload path) ARE encoded as stub logic in the Phase 2 skeleton — the `loadSRGB()` stub body contains conditional dispatch comments showing the correct routing for Phase 4:

  ```cpp
  // STUB DISPATCH LOGIC (no real GL calls — logging only):
  // if (path ends with "_d")         → sRGB upload path (Phase 4: glCompressedTexImage2D with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)
  //   EXCEPTION: "vehicles_sprite_atlas_d.dds" → LINEAR path via loadLinear()
  //   (roof color palette swatches, not photographic diffuse — must NOT be sRGB-decoded)
  //   Check filename BEFORE suffix. See architecture/asset-standards/2d-texture-standards.md.
  // if (path ends with "_billboard") → sRGB upload path (Phase 4: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
  // if (path ends with "_n")         → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_s")         → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_sprite")    → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_lm")        → linear path — use loadLinear() NOT loadSRGB()
  // STUB BODY (Phase 2):
  //   fprintf(stderr, "STUB: would upload %s via sRGB path\n", path.c_str());
  //   return GLuint{0};
  ```

  This split makes the dispatch contract visible for Phase 4 review before any real GL calls are written. All three pools must be present so Phase 4 and Phase 5 can build against the `TextureCache` interface without structural CMakeLists changes. **If the Phase 2 GLEW availability spike reveals GLEW is absent, the `loadSRGB()` stub body comment must be updated to reflect `glGetString(GL_EXTENSIONS)` as the fallback extension check path (instead of `glewIsExtensionSupported()`). Method signatures remain unchanged — only the internal comment changes.**

  **`loadSRGB()` stub body comment requirement**: The `loadSRGB(const std::string& path, GLenum format)` stub body MUST include the following documentation comments for the Phase 4 implementor:

  ```cpp
  // Phase 4 implementation requirement: if path ends with "_billboard", set
  // GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE and GL_TEXTURE_WRAP_T = GL_CLAMP_TO_EDGE
  // after glTexParameteri filter calls. Default GL_REPEAT causes ghost-frame artifacts
  // at the 1x8 horizontal strip boundary. See architecture/graphics-architecture/texture-cache.md.
  //
  // TODO Phase 4: apply GL_TEXTURE_MAX_LEVEL dispatch table per texture-cache.md:
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
  // TODO Phase 4: When binding this GLuint in OnSetConstants(), save the current
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

  These dispatch rules must be explicit in the `TextureCache` sRGB load path as stub comments in Phase 2. (ref: `architecture/graphics-architecture/texture-cache.md`, `architecture/asset-standards/2d-texture-standards.md`)

#### shader_constants.h Stub

- [ ] `src/rendering/shader_constants.h` stub created with the full `kTexUnit*` constant table (`kTexUnitDiffuse=0` through `kTexUnitBillboard=9`) as `constexpr int` values per `architecture/asset-standards/2d-texture-standards.md`; file must exist before Phase 3 begins; actual shader usage wired in Phase 4. The file MUST also include the mandatory compile-time range guard:

  ```cpp
  static_assert(kTexUnitBillboard <= 15,
      "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)");
  ```

  Both the constant table AND the `static_assert` must be present before Phase 3 begins. (ref: `architecture/asset-standards/2d-texture-standards.md`)

#### GLSL Shader Stubs

- [ ] **GLSL shader stub files**: `assets/shaders/lighting.vert`, `assets/shaders/lighting.frag`, `assets/shaders/terrain.vert`, `assets/shaders/terrain.frag`, `assets/shaders/billboard.vert`, and `assets/shaders/billboard.frag` created as stub files in Phase 2 with a trivial pass-through GLSL implementation. **All Phase 2 GLSL stub shaders MUST begin with a `#version 130` directive as the first non-comment line.** OpenGL 3.x core profiles reject GLSL source that does not specify a version directive — omitting `#version 130` causes a shader compilation failure even for trivial pass-through stubs.

  **Irrlicht-5 — GLSL stub co-landing requirement**: `assets/shaders/terrain.vert` and `assets/shaders/terrain.frag` MUST be committed in the **same PR** as `ShaderLoadingTest` (i.e., `shader_stub_compile_test.cpp`). This co-landing requirement applies to ALL six stub shader files: the test file and all six `.vert`/`.frag` stubs must be in the same commit. (ref: `architecture/graphics-architecture/shader-loading.md`)

  **NOTE**: Use the 8-parameter overload of `addHighLevelShaderMaterialFromFiles()` (no geometry shader). The Irrlicht GLSL backend has no geometry shader stage in V1. The geometry shader parameters (if present in a higher overload) must be left as empty string / nullptr.

  **Exact 8-param overload call pattern (Phase 2 GLSL stub pattern)**:

  ```cpp
  // Exact 8-param overload (Phase 2 GLSL stub pattern):
  s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
      vsFile, "main", video::EVST_VS_1_1,   // vertex shader, entry point, version enum (IGNORED by GLSL backend)
      fsFile, "main", video::EPST_PS_1_1,   // fragment shader, entry point, version enum
      cb, video::EMT_SOLID);                // IShaderConstantSetCallBack*, base material
  cb->drop();  // Irrlicht calls grab() on cb; we must drop() to transfer ownership.
  // FAILURE PATH WARNING: if matType == -1, Irrlicht did NOT call grab().
  // drop() reduces ref_count 1→0 and destroys cb NOW.
  // Do NOT dereference cb below this line.
  ```

- [ ] **GLSL shader stub compile-time validation** (`graphics-dev-irrlicht`): `opengl_tests` target extended with `tests/rendering/shader_stub_compile_test.cpp` (label `requires-opengl`); added to the `add_executable(opengl_tests ...)` call in CMakeLists.txt alongside `stub_succeed.cpp` and `lod_swap_smoke_test.cpp`. Loads `lighting.vert` and `lighting.frag` via `gpu->addHighLevelShaderMaterialFromFiles()` on an `EDT_OPENGL` device, asserts the returned material type is not −1. This catches missing `#version` directive, syntax errors, or incorrect GLSL path resolution before Phase 4 shader infrastructure is built on top of these stubs. Must be green before Phase 3 begins. Per `architecture/testing/framework.md`, do NOT use `target_sources()` to add files to `opengl_tests` — the `add_executable` call must list all sources inline. **`target_sources()` is PROHIBITED for `opengl_tests`**: use `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp tests/rendering/lod_swap_smoke_test.cpp)`. The `opengl_tests` target must link: `target_link_libraries(opengl_tests PRIVATE aitown_render GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)`. **Shader callback lifetime**: the test must follow the raw-heap + `->drop()` callback lifetime pattern — allocate the callback with `new`, call `cb->drop()` unconditionally after `addHighLevelShaderMaterialFromFiles()`. Do NOT wrap the callback in `std::unique_ptr`. **Null-device guard**: the test body must null-check the `IrrlichtDevice*` returned by `createDevice()` using the two-condition form:

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

  Exit criterion: `shader_stub_compile_test` must PASS (not skip) on ubuntu-latest WITH xvfb before Phase 3 begins.

#### LOD Smoke Test & Spikes

- [ ] **LOD smoke test infrastructure**: `tests/rendering/lod_swap_smoke_test.cpp` with test name `LODSwapSmokeTest.SetMeshGrabDropContract`, CMake registration labelled `requires-opengl`, registered via the `opengl_tests` target. Use `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp tests/rendering/lod_swap_smoke_test.cpp)` — amend the `add_executable` call in-place. Do NOT use `target_sources()` for adding files to `opengl_tests`. **Irrlicht-4 — The Phase 2 test body MUST use `GTEST_SKIP()`** with the following exact comment (NOT `SUCCEED()`):

  ```cpp
  TEST(LODSwapSmokeTest, SetMeshGrabDropContract) {
      // Timing measurement requires a real GPU; this test is promoted to
      // `requires-opengl` label in Phase 4 when the real LOD swap is implemented.
      // Phase 2 stub asserts only the API contract (setMesh is called), not the timing.
      // The "> 2ms is HARD BLOCKING" timing constraint is enforced by profiling in Phase 4
      // using ASAN + release build timing, not by this unit test.
      GTEST_SKIP() << "LOD swap timing requires real GPU; promoted to Phase 4.";
  }
  ```

  **Why `GTEST_SKIP()` instead of `SUCCEED()` (Irrlicht-4 rationale)**: `GTEST_SKIP()` correctly communicates that this test is deferred pending GPU availability, not vacuously passing. CTest counts a SKIPPED test as passing the gate. `SUCCEED()` falsely implies the timing constraint has been verified.

  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/testing/framework.md`)

- [ ] **LOD spike Checkbox A**: `SMesh::addMeshBuffer()` grab/drop contract verified by inspecting vendored Irrlicht source (`SMesh.h`); result recorded as a one-line comment in `tests/rendering/lod_swap_smoke_test.cpp`. If `addMeshBuffer()` calls `grab()`, caller must `drop()` after; if not, caller must not `drop()`. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [ ] **LOD spike Checkbox B**: `CMeshSceneNode::setMesh()` grab/drop contract verified by inspecting `CMeshSceneNode.cpp`; result recorded in `architecture/graphics-architecture/scene-graph-ownership.md`. **Phase 5 TerrainChunk work is BLOCKED until Checkbox B is ticked, not Checkbox A.** If `setMesh()` does NOT call `grab()`, remove the `drop()` call from the LOD swap sequence in `scene-graph-ownership.md` before any Phase 5 code uses `node->setMesh()`. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

  **LOD spike contingency branch**: If the spike reveals `CMeshSceneNode::setMesh()` does NOT call `grab()` on the new mesh: (a) `architecture/graphics-architecture/scene-graph-ownership.md` must be corrected to remove the `drop()` call after `setMesh()`; (b) `lod_swap_smoke_test.cpp` must verify via ASAN that the mesh survives without the caller's `drop()`; (c) this finding BLOCKS Phase 5 `TerrainChunk` implementation until `scene-graph-ownership.md` is updated.

### Exit Criteria

- All GL capability queries consolidated in `RenderSystem::init()` behind `EDT_NULL` guard; `glewInit()` two-tier handling present
- `m_maxTextureSize`, `m_srgbTextureSupported` (+ `isSRGBTextureSupported()` accessor), and `m_maxAnisotropy` populated for non-EDT_NULL devices
- GLEW availability spike result documented in `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (under "Phase 1 Spike Results") AND as a one-line comment in `src/rendering/render_system.h`. Phase 4 sRGB texture pipeline is blocked until this is on record.
- `find_package(GLEW REQUIRED)` added to CMakeLists.txt; `GLEW::GLEW` linked to `aitown_render`
- GLEW vcpkg port existence verified via `gh api /repos/microsoft/vcpkg/contents/ports/glew?ref=$(jq -r '."builtin-baseline"' vcpkg.json)` — confirmed 200 HTTP response at the exact pinned baseline. A 404 response blocks Phase 2 merge.
- `TextureCache` skeleton compiles with all 3 pool structures (`m_srgbTextures`, `m_splatMaps`, linear pool), `m_driverType`, `getSRGBGLuint`/`getSplatMapGLuint` accessors, and stub load/eviction methods
- `loadSRGB()` stub body includes BOTH the `GL_CLAMP_TO_EDGE` wrap-mode TODO comment for `_billboard` textures AND the `GL_TEXTURE_MAX_LEVEL` dispatch table TODO comment
- `src/rendering/shader_constants.h` exists with full `kTexUnit*` table (0–9) AND `static_assert(kTexUnitBillboard <= 15, ...)`; verified by code review before Phase 3 begins
- All 6 GLSL stub files exist (`lighting.vert/.frag`, `terrain.vert/.frag`, `billboard.vert/.frag`), each starting with `#version 130`
- `shader_stub_compile_test` PASSES (not skips) on ubuntu-latest with xvfb before Phase 3 begins
- `LODSwapSmokeTest.SetMeshGrabDropContract` is registered under `requires-opengl` label and produces SKIP (not FAIL) under `xvfb-run`
- LOD spike **Checkbox A** result for `SMesh::addMeshBuffer()` grab/drop contract documented in `tests/rendering/lod_swap_smoke_test.cpp` with a one-line explanatory comment
- LOD spike **Checkbox B** result for `CMeshSceneNode::setMesh()` grab/drop contract documented in `architecture/graphics-architecture/scene-graph-ownership.md`. **The Phase 5 TerrainChunk deliverable is BLOCKED on Checkbox B (not Checkbox A).**
- `getGPUProgrammingServices()` null-checked in RenderSystem before any `addHighLevelShaderMaterialFromFiles()` call

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | Consolidated GL capability query guard (`EDT_NULL` pre-check; `glewInit()` two-tier; `GL_MAX_TEXTURE_SIZE`; extension checks), GLEW availability spike (result documented in `irrlicht-device-lifecycle.md` + `render_system.h`), `find_package(GLEW REQUIRED)` + `GLEW::GLEW` linkage, `getGPUProgrammingServices()` null check, `TextureCache` skeleton (all 3 pools; `m_driverType`; `loadSRGB()` stub body comments; mip dispatch table stub; `getSRGBGLuint`/`getSplatMapGLuint`), `src/rendering/shader_constants.h` stub, GLSL shader stubs (6 files, `#version 130`), `shader_stub_compile_test.cpp`, LOD smoke test (`GTEST_SKIP()` body), LOD spikes A and B |
| `cicd-dev-github` | Review `find_package(GLEW REQUIRED)` CMakeLists change; confirm GLEW vcpkg port exists at current `VCPKG_COMMIT_ID` before committing |

### Dependencies

- Requires Phase 1 complete (Irrlicht device lifecycle, `aitown_render` CMake target, `IRenderer` interface)

### Risks & Spikes

- **RISK**: GLEW not bundled in vendored Irrlicht — `glewIsExtensionSupported()` fails to link → **Spike**: inspect `COpenGLDriver.cpp`; fallback to `glGetString(GL_EXTENSIONS)` string matching; document in `irrlicht-device-lifecycle.md` "Phase 1 Spike Results" section. Phase 4 sRGB pipeline is BLOCKED until spike is resolved.
- **RISK**: `CMeshSceneNode::setMesh()` does NOT call `grab()` — LOD swap code in Phase 5 would double-free or leak → **Spike**: inspect `CMeshSceneNode.cpp`; correct `scene-graph-ownership.md` before Phase 5 `TerrainChunk` work begins (Checkbox B).
- **RISK**: GLEW symbol duplication with Irrlicht's bundled GLEW causing linker errors → **Spike**: test with `find_package(GLEW REQUIRED)` on both Linux and Windows CI; if linker errors occur, use `GLEW_STATIC` define or exclude the system GLEW in favour of Irrlicht's bundled version.
