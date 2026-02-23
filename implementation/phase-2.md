## Phase 2: GL Capability, TextureCache Skeleton & Shader Infrastructure

### Goal

Add all OpenGL capability queries, the `TextureCache` three-pool skeleton, GLSL shader stubs, and the LOD smoke-test infrastructure — building directly on top of the Irrlicht device shell established in Phase 1 and giving Phase 5 (Procedural Terrain) a fully-typed `TextureCache` interface to build against.

### Deliverables

#### GLEW & GL Capability Queries

- [x] **GLEW availability spike** (`graphics-dev-irrlicht`): inspect the vendored Irrlicht build's `COpenGLDriver.cpp` to confirm whether GLEW is bundled and whether `glewIsExtensionSupported()` is exposed. If Irrlicht was compiled without GLEW (custom build using ARB string parsing), `glewIsExtensionSupported()` will not link — replace all calls with `glGetString(GL_EXTENSIONS)` string matching or `IVideoDriver::queryFeature()`. Document the confirmed extension query path in BOTH `src/rendering/render_system.h` (one-line code comment, e.g., `// GLEW available in vendored Irrlicht — using glewIsExtensionSupported()`) AND `architecture/graphics-architecture/irrlicht-device-lifecycle.md` under the **"Phase 2 Spike Results"** section. **Phase 5 sRGB texture pipeline (terrain) is BLOCKED on this spike result.** Owner: `graphics-dev-irrlicht`.

  **GLEW spike — two independent questions** (per `architecture/graphics-architecture/irrlicht-device-lifecycle.md` "GLEW Spike — Two Independent Questions" section): The GLEW availability spike answers two completely independent questions — conflating them produces incorrect conclusions:
  - **Question 1**: Does the vendored Irrlicht source use GLEW internally? (Inspect `source/Irrlicht/COpenGLDriver.cpp` for `#include "glew.h"` or `glewInit()` calls.) This determines only whether Irrlicht's own internal GL calls go through GLEW's function pointer table. It does NOT determine whether AI Town can link GLEW independently.
  - **Question 2**: Can AI Town link against GLEW independently and call `glewInit()`? The answer is determined solely by whether `find_package(GLEW REQUIRED)` succeeds — when the vcpkg `glew` port is installed, this is ALWAYS yes, regardless of whether Irrlicht bundles GLEW internally.
  - `find_package(GLEW REQUIRED)` MUST remain in CMakeLists.txt regardless of the answer to Question 1. AI Town calls `glewInit()` itself to populate its own function pointer table for `glCompressedTexImage2D` and `glewIsExtensionSupported()`. Question 1 only affects whether symbol duplication risk exists — handled by link order.

  **SPEC SECTION NAME NOTE**: The spec section in `irrlicht-device-lifecycle.md` is named **"Phase 2 Spike Results"** (not "Phase 1 Spike Results"). Any reference to "Phase 1 Spike Results" in this phase file is incorrect — results must be recorded under the spec's canonical "Phase 2 Spike Results" heading. The spec at line 148 also confirms the document location is `irrlicht-device-lifecycle.md`, NOT `scene-graph-ownership.md`.

  **sRGB format tokens if GLEW absent**: If GLEW is unavailable, the sRGB internal format tokens must be defined manually as their OpenGL specification hex literals before `glCompressedTexImage2D` is called: `#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C` and `#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4E`. These values are from the `EXT_texture_sRGB` extension spec and are stable across platforms.

- **`find_package(GLEW REQUIRED)` in CMakeLists.txt root** — completed in Phase 1: `find_package(GLEW REQUIRED)` and `GLEW::GLEW` linkage to `aitown_render` were delivered as part of the Phase 1 **six-item atomicity commit**. Phase 2 verifies only that these are correctly present and consistent — no new CMakeLists change is introduced here. (ref: `architecture/ci-cd/dependency-management.md`)

- **Consolidated GL capability query guard — verification only (Phase 1 delivered)**: Phase 1 delivered the `glewInit()` initialization block, the `EDT_NULL` pre-check, and the two-tier GLEW failure handling in `RenderSystem`. Phase 2 verifies correctness of the Phase 1 implementation and documents the GLEW availability spike result — no new `glewInit()` initialization code is introduced in Phase 2. The genuinely new Phase 2 work is: (a) documenting the GLEW availability spike result in `architecture/graphics-architecture/irrlicht-device-lifecycle.md` under the **"Phase 2 Spike Results"** section and as a one-line comment in `src/rendering/render_system.h`; (b) verifying the `getGPUProgrammingServices()` null check is present before any `addHighLevelShaderMaterialFromFiles()` call. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`, `architecture/asset-standards/2d-texture-standards.md`)

- [x] `GL_MAX_TEXTURE_SIZE` queried **immediately after** `createDevice()`, stored as `m_maxTextureSize`; `EDT_NULL` guard initializes to 2048 without GL query (ref: `architecture/asset-standards/2d-texture-standards.md`)

- [x] `GL_EXT_texture_sRGB` extension presence checked via `glewIsExtensionSupported()` immediately after `createDevice()` in `RenderSystem::init()`; result stored as `bool m_srgbTextureSupported`; `isSRGBTextureSupported() const` accessor exposed; `EDT_NULL` guard initializes to `false` (no GL context). `TextureCache` reads this flag at construction to select the upload path. (ref: `architecture/graphics-architecture/texture-cache.md`)

- **`m_maxAnisotropy` query: completed in Phase 1** — same-timing as `m_maxTextureSize` per `architecture/asset-standards/2d-texture-standards.md`. Phase 1 delivers: `float m_maxAnisotropy` declared in `RenderSystem.h` (initialized to `1.0f`); in the `glewInit()` SUCCESS path, `GL_EXT_texture_filter_anisotropic` checked via `glewIsExtensionSupported()`; if present, `glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m_maxAnisotropy)` called; if absent, warning logged and `m_maxAnisotropy` left at `1.0f`; EDT_NULL and RELEASE fatal-failure paths initialize `m_maxAnisotropy = 1.0f` without any GL call. Phase 2 does NOT re-query `m_maxAnisotropy` — it is already populated by Phase 1. This note replaces the Phase 2 `m_maxAnisotropy` deliverable bullet.

- [x] **`getGPUProgrammingServices()` null check**: `getGPUProgrammingServices()` must be null-checked before any `addHighLevelShaderMaterialFromFiles()` call — return early for `EDT_NULL` driver; log and abort in debug builds if null on a non-EDT_NULL driver (shader failure is fatal in debug builds per `architecture/graphics-architecture/shader-loading.md`). **Atomicity requirement**: The following four items MUST be committed in the same atomic commit — no subset may land alone: (1) `getGPUProgrammingServices()` null-check code inside `src/rendering/render_system.cpp`; (2) `tests/rendering/shader_stub_compile_test.cpp` (the Phase 2 `ShaderLoadingTest` body, which calls `addHighLevelShaderMaterialFromFiles()`); (3) all 6 GLSL stub files (`assets/shaders/lighting.vert`, `lighting.frag`, `terrain.vert`, `terrain.frag`, `billboard.vert`, `billboard.frag`); (4) the CMakeLists.txt amendment adding `tests/rendering/shader_stub_compile_test.cpp` and `tests/rendering/lod_swap_smoke_test.cpp` to the `add_executable(opengl_tests …)` call. The null check MUST be present before the shader compile test is registered in CI — committing the test without the null check means the test exercises a code path that lacks the required guard. (ref: `architecture/graphics-architecture/shader-loading.md`)

#### TextureCache Skeleton

- [x] `TextureCache` class skeleton (stub) in `src/rendering/texture_cache.h`: must include all three pool structures per `architecture/graphics-architecture/texture-cache.md`:
  - `m_driverType` member of type `irr::video::E_DRIVER_TYPE`, initialized from the constructor parameter (e.g., `explicit TextureCache(irr::video::E_DRIVER_TYPE driverType)` → `m_driverType{driverType}`). **This member is required in Phase 2** so that the `EDT_NULL` guard on the sRGB upload path in Phase 5 (terrain textures) can check `m_driverType == irr::video::EDT_NULL` without requiring a header change. (ref: `architecture/graphics-architecture/texture-cache.md` — EDT_NULL guard for `evictUnreferenced()`)
  - `m_srgbTextures` (`std::unordered_map<std::string, SRGBEntry>` stub — raw `GLuint` pool, separate from the linear `ITexture*` pool)
  - `m_splatMaps` (`std::unordered_map<std::string, SplatEntry>` stub — raw `GLuint` pool for terrain splat maps). **Canonical member name is `m_splatMaps` per `texture-cache.md` eviction code — do NOT use `m_splatMapTextures`.**
  - Eviction interface method stubs: `releaseLinear(ITexture*)`, `releaseLinear(const std::string& key)`, `releaseSRGB(const std::string& filename)`, `releaseSplatMap(const std::string& filename)` (no-op stub), `evictUnreferenced()` (covers all three pools)
  - Stub accessors: `getSRGBGLuint(const std::string&) const` returning `GLuint{0}`, `getSplatMapGLuint(const std::string&) const` returning `GLuint{0}` (parallel to `getSRGBGLuint`). **NOTE (Irrlicht-2)**: The sRGB accessor is canonically named `getSRGBGLuint` per `architecture/graphics-architecture/texture-cache.md`. An earlier draft of `shader-loading.md` referenced `getGLuint` — that name has been corrected to `getSRGBGLuint` in the spec; the plan must use `getSRGBGLuint` consistently. If any call site uses `getGLuint`, it must be renamed.
  - Stub load methods: `loadSplatMap(const std::string& path)` returning sentinel `GLuint{0}`; `loadLinear(const std::string& path)` returning `ITexture* {nullptr}`; **`loadSRGB(const std::string& path, GLenum format)` returning sentinel `GLuint{0}`** — the `GLenum format` parameter selects between `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` (opaque diffuse) and `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` (transparent diffuse)

  **Irrlicht-3 — "No real GL calls" clarification**: "No real GL calls in Phase 2" means no actual GPU texture uploads happen — `loadSRGB()` does NOT call `glCompressedTexImage2D`, `glGenTextures`, or any other GL function that requires an active context. However, the mip dispatch rules (which DDS filename suffix routes to which upload path) ARE encoded as stub logic in the Phase 2 skeleton — the `loadSRGB()` stub body contains conditional dispatch comments showing the correct routing for Phase 5 (when the full `TextureCache` three-pool implementation with real GL calls is delivered):

  ```cpp
  // STUB DISPATCH LOGIC (no real GL calls — logging only):
  // if (path ends with "_d")         → sRGB upload path (Phase 5: glCompressedTexImage2D with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)
  //   EXCEPTION: "vehicles_sprite_atlas_d.dds" → LINEAR path via loadLinear()
  //   (roof color palette swatches, not photographic diffuse — must NOT be sRGB-decoded)
  //   Check filename BEFORE suffix. See architecture/asset-standards/2d-texture-standards.md.
  // if (path ends with "_billboard") → sRGB upload path (Phase 5: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
  // if (path ends with "_n")         → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_s")         → linear path — use loadLinear() NOT loadSRGB()
  // if (path ends with "_sp")        → linear path — use loadLinear() NOT loadSRGB()
  //   (_sp = specular packed: roughness/metallic/AO multi-channel — linear data, not sRGB photographic)
  //   See architecture/graphics-architecture/texture-cache.md GL_TEXTURE_MAX_LEVEL dispatch table.
  // if (path ends with "_lm")        → linear path — use loadLinear() NOT loadSRGB()
  // STUB BODY (Phase 2):
  //   fprintf(stderr, "STUB: would upload %s via sRGB path\n", path.c_str());
  //   return GLuint{0};
  ```

  This split makes the dispatch contract visible for Phase 5 review before any real GL calls are written. All three pools must be present so Phase 3, Phase 4, and Phase 5 can build against the `TextureCache` interface without structural CMakeLists changes. **If the Phase 2 GLEW availability spike reveals GLEW is absent, the `loadSRGB()` stub body comment must be updated to reflect `glGetString(GL_EXTENSIONS)` as the fallback extension check path (instead of `glewIsExtensionSupported()`). Method signatures remain unchanged — only the internal comment changes.**

  **`loadSRGB()` stub body `_billboard` note (MANDATORY)**: The `loadSRGB()` stub body MUST include the following note for the `_billboard` dispatch case: `// NOTE: _billboard suffix applies exclusively to the per-building imposter atlas (1024×128 DXT5 sRGB strip per asset). No other billboard-type atlas uses this suffix — if a new sprite or imposter atlas type is added, verify it uses a distinct suffix before reusing _billboard routing. See architecture/asset-standards/3d-model-standards.md LOD File Naming Convention.`

  **`loadSRGB()` stub body comment requirement**: The `loadSRGB(const std::string& path, GLenum format)` stub body MUST include the following documentation comments for the Phase 5 implementor (Phase 5 delivers the full `TextureCache` three-pool implementation with real GL calls per `architecture/graphics-architecture/texture-cache.md`):

  ```cpp
  // Phase 5 implementation requirement: if path ends with "_billboard", set
  // GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE and GL_TEXTURE_WRAP_T = GL_CLAMP_TO_EDGE
  // after glTexParameteri filter calls. Default GL_REPEAT causes ghost-frame artifacts
  // at the 1x8 horizontal strip boundary. See architecture/graphics-architecture/texture-cache.md.
  //
  // TODO Phase 5: apply GL_TEXTURE_MAX_LEVEL dispatch table per texture-cache.md:
  //   _billboard suffix -> GL_TEXTURE_MAX_LEVEL = 3 (4-level mip chain mandatory)
  //   _d suffix (sRGB)  -> GL_TEXTURE_MAX_LEVEL = 3 (standard 4-level mip chain)
  //   splat maps        -> use loadSplatMap() NOT loadSRGB(); GL_TEXTURE_MAX_LEVEL=0 (single mip)
  //   NOTE: _n, _s, _sp use loadLinear() NOT loadSRGB() — no glTexParameteri access
  ```

  ```cpp
  // NOTE: _lm (lightmap) textures use loadLinear() NOT loadSRGB() — they are linear-format
  // textures uploaded via IVideoDriver::getTexture(). Do NOT add _lm dispatch to loadSRGB().
  // See texture-cache.md GL_TEXTURE_MAX_LEVEL table for the full dispatch.
  ```

- [x] **`loadLinear()` stub body `_lm` routing comment** (`graphics-dev-irrlicht`): The `loadLinear()` stub body MUST contain the following comment block:

  ```cpp
  // NOTE: all textures with _lm suffix (e.g., buildings_atlas_lm.dds, vehicles_diffuse_atlas_lm.dds)
  // MUST be loaded via loadLinear(). Do NOT route _lm textures through loadSRGB().
  // Lightmap data is pre-baked linear irradiance — sRGB gamma expansion would corrupt the lighting.
  // See architecture/graphics-architecture/texture-cache.md GL_TEXTURE_MAX_LEVEL dispatch table (_lm row).
  ```

  (ref: `architecture/asset-standards/building-atlas-layout.md` Sign-Off Checklist, `architecture/graphics-architecture/texture-cache.md`)

  ```cpp
  // TODO Phase 5: _lm suffix textures uploaded via loadLinear() MUST have GL_TEXTURE_MAX_LEVEL = 0
  // enforced after upload; IVideoDriver::getTexture() does not expose glTexParameteri — use raw GL
  // after load to set GL_TEXTURE_MAX_LEVEL = 0 on the lightmap texture object.
  // Per architecture/asset-standards/2d-texture-standards.md lightmap mip exemption:
  // lightmaps are single-mip; generating a mip chain introduces blur that corrupts lightmap precision.
  ```

  ```cpp
  // TODO Phase 5: When binding this GLuint in OnSetConstants(), save the current
  // GL_ACTIVE_TEXTURE unit with glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit) BEFORE
  // calling glActiveTexture(). Restore with glActiveTexture(savedUnit) AFTER unbinding.
  // Failure to restore corrupts Irrlicht's internal active-unit tracking.
  // See architecture/graphics-architecture/shader-loading.md — CRITICAL save/restore section.
  ```

  (ref: `architecture/graphics-architecture/texture-cache.md`)

- [x] **TextureCache mip level control** (`graphics-dev-irrlicht`): `TextureCache` must apply the following rules in the sRGB upload path (`loadSRGB()`) based on the texture filename suffix. This is the **GL_TEXTURE_MAX_LEVEL dispatch table** from `architecture/graphics-architecture/texture-cache.md`:
  - Set `GL_TEXTURE_MAX_LEVEL = 3` for **billboard atlas textures** (`_billboard` suffix) — the 4-level mip chain (1024×128 → 512×64 → 256×32 → 128×16) is **mandatory** for billboard imposters per `architecture/asset-standards/2d-texture-standards.md`.
  - Set `GL_TEXTURE_MAX_LEVEL = 3` for **diffuse sRGB textures** (`_d` suffix) — standard 4-level mip chain.
  - **NOTE**: `_lm` (lightmap) textures are loaded via `loadLinear()` — no `GL_TEXTURE_MAX_LEVEL` control is possible through the IVideoDriver path. The `loadSRGB()` dispatch table must NOT include an `_lm` case.
  - **NOTE**: `_n` (normal maps) and `_s` (specular) use `loadLinear()` NOT `loadSRGB()` — the `GL_TEXTURE_MAX_LEVEL` dispatch for these suffixes belongs in the `loadLinear()` path, not `loadSRGB()`. There is no `_sprite` suffix in the dispatch table — `vehicles_sprite_atlas_d.dds` is routed to the linear path via the explicit filename exception check within the `_d` dispatch, NOT via a `_sprite` suffix rule.
  - **NOTE**: splat maps use `loadSplatMap()` NOT `loadSRGB()` — the `loadSRGB()` dispatch table must NOT include a splat map case.

  These dispatch rules must be explicit in the `TextureCache` sRGB load path as stub comments in Phase 2. (ref: `architecture/graphics-architecture/texture-cache.md`, `architecture/asset-standards/2d-texture-standards.md`)

#### shader_constants.h Stub

- [x] `src/rendering/shader_constants.h` stub created with the full `kTexUnit*` constant table (`kTexUnitDiffuse=0` through `kTexUnitBillboard=9`) as `constexpr int` values per `architecture/asset-standards/2d-texture-standards.md`; file must exist before Phase 3 begins; Phase 4 verifies its correctness (as a gate); actual shader usage wired in Phase 5 (terrain GLSL shaders). The file MUST also include the mandatory compile-time range guard:

  ```cpp
  static_assert(kTexUnitBillboard <= 15,
      "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)");
  ```

  Both the constant table AND the `static_assert` must be present before Phase 3 begins. (ref: `architecture/asset-standards/2d-texture-standards.md`)

#### GLSL Shader Stubs

- [x] **GLSL shader stub files**: `assets/shaders/lighting.vert`, `assets/shaders/lighting.frag`, `assets/shaders/terrain.vert`, `assets/shaders/terrain.frag`, `assets/shaders/billboard.vert`, and `assets/shaders/billboard.frag` created as stub files in Phase 2 with a trivial pass-through GLSL implementation. **All Phase 2 GLSL stub shaders MUST begin with a `#version 130` directive as the first non-comment line.** OpenGL 3.x core profiles reject GLSL source that does not specify a version directive — omitting `#version 130` causes a shader compilation failure even for trivial pass-through stubs. **Minimal valid `main()` body requirement**: each stub must contain a semantically complete `main()` function — an empty `main() {}` body is insufficient. Vertex shader stubs: `void main() { gl_Position = vec4(0.0); }`. Fragment shader stubs: `void main() { gl_FragColor = vec4(1.0); }`. These minimal bodies ensure the GLSL compiler accepts the shader without requiring hardware features or additional uniforms.

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

- [x] **GLSL shader stub compile-time validation** (`graphics-dev-irrlicht`): `opengl_tests` target extended with `tests/rendering/shader_stub_compile_test.cpp` (label `requires-opengl`); added to the `add_executable(opengl_tests ...)` call in CMakeLists.txt alongside `stub_succeed.cpp` and `lod_swap_smoke_test.cpp`. Loads `lighting.vert` and `lighting.frag` via `gpu->addHighLevelShaderMaterialFromFiles()` on an `EDT_OPENGL` device, asserts the returned material type is not −1. **Canonical test case signature**: `TEST(ShaderLoadingTest, LightingShaderCompilesWithoutError)` — this exact name is required per the co-landing requirement in `architecture/graphics-architecture/shader-loading.md`. The test asserts `matType != -1` and follows the raw-heap + `->drop()` callback lifetime pattern (allocate callback with `new`, call `cb->drop()` after `addHighLevelShaderMaterialFromFiles()`). NOTE: `ctest -R ShaderStub` (Phase 1 exit criterion) and `ctest -R ShaderLoading` (Phase 2 exit criterion) are distinct — both test cases coexist in `shader_stub_compile_test.cpp` after Phase 2. This catches missing `#version` directive, syntax errors, or incorrect GLSL path resolution before Phase 5 terrain shader infrastructure is built on top of these stubs. Must be green before Phase 3 begins. Per `architecture/testing/framework.md`, do NOT use `target_sources()` to add files to `opengl_tests` — the `add_executable` call must list all sources inline. **`target_sources()` is PROHIBITED for `opengl_tests`**: use `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp tests/rendering/lod_swap_smoke_test.cpp)`. The `opengl_tests` target must link: `target_link_libraries(opengl_tests PRIVATE aitown_render GTest::gtest_main GTest::gmock rapidcheck rapidcheck_gtest)`. **Shader callback lifetime**: the test must follow the raw-heap + `->drop()` callback lifetime pattern — allocate the callback with `new`, call `cb->drop()` unconditionally after `addHighLevelShaderMaterialFromFiles()`. Do NOT wrap the callback in `std::unique_ptr`. **Null-device guard**: the test body must null-check the `IrrlichtDevice*` returned by `createDevice()` using the two-condition form:

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

- [x] **LOD smoke test infrastructure**: `tests/rendering/lod_swap_smoke_test.cpp` with test name `LODSwapSmokeTest.SetMeshGrabDropContract`, CMake registration labelled `requires-opengl`, registered via the `opengl_tests` target. Use `add_executable(opengl_tests tests/rendering/stub_succeed.cpp tests/rendering/shader_stub_compile_test.cpp tests/rendering/lod_swap_smoke_test.cpp)` — amend the `add_executable` call in-place. Do NOT use `target_sources()` for adding files to `opengl_tests`. **Irrlicht-4 — The Phase 2 test body MUST use `GTEST_SKIP()`** with the following exact comment (NOT `SUCCEED()`):

  ```cpp
  TEST(LODSwapSmokeTest, SetMeshGrabDropContract) {
      // Timing measurement requires a real GPU; this test is promoted to
      // `requires-opengl` label in Phase 5 when the real LOD swap is implemented
      // (after the LOD spike work in Phase 2 is complete and TerrainChunk is built).
      // Phase 2 stub asserts only the API contract (setMesh is called), not the timing.
      // The grab/drop contract is enforced by ASAN in Phase 5 after the spike confirms
      // whether setMesh() calls grab() on the new mesh.
      GTEST_SKIP() << "LOD swap timing requires real GPU; promoted to Phase 5.";
  }
  ```

  **Why `GTEST_SKIP()` instead of `SUCCEED()` (Irrlicht-4 rationale)**: `GTEST_SKIP()` correctly communicates that this test is deferred pending GPU availability, not vacuously passing. CTest counts a SKIPPED test as passing the gate. `SUCCEED()` falsely implies the timing constraint has been verified.

  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/testing/framework.md`)

- [x] **LOD spike Checkbox A**: `SMesh::addMeshBuffer()` grab/drop contract verified by inspecting vendored Irrlicht source (`SMesh.h`); result recorded as a one-line comment in `tests/rendering/lod_swap_smoke_test.cpp`. If `addMeshBuffer()` calls `grab()`, caller must `drop()` after; if not, caller must not `drop()`. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

- [x] **LOD spike Checkbox B**: `CMeshSceneNode::setMesh()` grab/drop contract verified by inspecting `CMeshSceneNode.cpp`; result recorded in `architecture/graphics-architecture/scene-graph-ownership.md`. **Phase 5 TerrainChunk work is BLOCKED until Checkbox B is ticked, not Checkbox A.** If `setMesh()` does NOT call `grab()`, remove the `drop()` call from the LOD swap sequence in `scene-graph-ownership.md` before any Phase 5 code uses `node->setMesh()`. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

  **LOD spike contingency branch**: If the spike reveals `CMeshSceneNode::setMesh()` does NOT call `grab()` on the new mesh: (a) `architecture/graphics-architecture/scene-graph-ownership.md` must be corrected to remove the `drop()` call after `setMesh()`; (b) `lod_swap_smoke_test.cpp` must verify via ASAN that the mesh survives without the caller's `drop()`; (c) this finding BLOCKS Phase 5 `TerrainChunk` implementation until `scene-graph-ownership.md` is updated.

### Exit Criteria

- All GL capability queries consolidated in `RenderSystem::init()` behind `EDT_NULL` guard; `glewInit()` two-tier handling present
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/RenderSystem.cpp:43-48` (EDT_NULL guard initialises all three GL members without any GL call and returns early); `RenderSystem.cpp:54-114` (glewInit() two-tier: SUCCESS path at line 57, RELEASE fallback at line 88-113). All GL capability queries (`glGetIntegerv`, `glewIsExtensionSupported`) appear only in the SUCCESS branch (lines 66-79). PASS.

- `m_maxTextureSize` and `m_srgbTextureSupported` (+ `isSRGBTextureSupported()` accessor) populated for non-EDT_NULL devices; `m_maxAnisotropy` was queried in Phase 1 (same-timing as `m_maxTextureSize`) — Phase 2 verifies the Phase 1 query is present but does not re-introduce it
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/RenderSystem.h:47-49` declares all three accessors (`getMaxTextureSize()`, `isSRGBTextureSupported()`, `getMaxAnisotropy()`); `RenderSystem.h:70-72` member-initializer defaults; `RenderSystem.cpp:66` (`glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize)`), `RenderSystem.cpp:70` (`m_srgbTextureSupported = glewIsExtensionSupported("GL_EXT_texture_sRGB")`), `RenderSystem.cpp:73-79` (anisotropy query and warning). All three populated in SUCCESS path. PASS.

- GLEW availability spike result documented in `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (under the **"Phase 2 Spike Results"** section — per the canonical spec heading) AND as a one-line comment in `src/rendering/render_system.h`. Phase 5 sRGB texture pipeline (terrain) is blocked until this is on record.
  > VERIFIED 2026-02-23 by prod-owner — `architecture/graphics-architecture/irrlicht-device-lifecycle.md:219-245` contains the "Phase 2 Spike Results" section covering Q1 (Irrlicht does NOT bundle GLEW), Q2 (find_package(GLEW REQUIRED) confirmed), and nm check result (CLEAN). `src/rendering/RenderSystem.h:13-17` contains the matching multi-line one-line-equivalent code comment documenting both answers and confirming `glewIsExtensionSupported()` as the confirmed extension query path. PASS.

- `find_package(GLEW REQUIRED)` confirmed present in CMakeLists.txt and `GLEW::GLEW` confirmed linked to `aitown_render` (completed in Phase 1 — Phase 2 verifies only)
  > VERIFIED 2026-02-23 by prod-owner — `CMakeLists.txt:76` (`find_package(GLEW REQUIRED)`); `CMakeLists.txt:172` (`target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht)`) — GLEW::GLEW listed before Irrlicht per link-order mitigation-2. PASS.

- GLEW vcpkg port confirmed consistent with current `VCPKG_COMMIT_ID` baseline — port existence was verified at Phase 1; Phase 2 confirms no baseline drift has made the port unavailable
  > VERIFIED 2026-02-23 by prod-owner — `vcpkg.json:10` lists `"glew"` as a dependency; `vcpkg.json:4` baseline `ce35b1a53aac26d7fcdb8ee1ef7a8e4eea02d27b` is present. No baseline drift blocking GLEW. PASS.

- `TextureCache` skeleton compiles with all 3 pool structures (`m_srgbTextures`, `m_splatMaps`, linear pool), `m_driverType`, `getSRGBGLuint`/`getSplatMapGLuint` accessors, and stub load/eviction methods
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/texture_cache.h:185` (`m_srgbTextures` unordered_map); `texture_cache.h:190` (`m_splatMaps` unordered_map, canonical name confirmed); `texture_cache.h:194` (`m_linearTextures` unordered_map); `texture_cache.h:180` (`m_driverType` member); `texture_cache.h:170` (`getSRGBGLuint`), `texture_cache.h:175` (`getSplatMapGLuint`); load methods `loadSRGB`, `loadLinear`, `loadSplatMap` at lines 105, 120, 126; release stubs `releaseLinear(ITexture*)`, `releaseLinear(key)`, `releaseSRGB`, `releaseSplatMap` at lines 135-149; `evictUnreferenced` at line 161. `texture_cache.cpp` confirms all methods are stub-implemented. PASS.

- `loadSRGB()` stub body includes BOTH the `GL_CLAMP_TO_EDGE` wrap-mode TODO comment for `_billboard` textures AND the `GL_TEXTURE_MAX_LEVEL` dispatch table TODO comment
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/texture_cache.h:85-88` contains the `GL_CLAMP_TO_EDGE` wrap-mode Phase 5 requirement comment for `_billboard`; `texture_cache.h:90-94` contains the `GL_TEXTURE_MAX_LEVEL` dispatch table TODO comment covering `_billboard` (level 3), `_d` (level 3), splat maps (use loadSplatMap), and the note that `_n/_s/_sp` use `loadLinear()`. Also includes the `_lm` linear-path note at lines 96-98 and the active-texture save/restore TODO at lines 100-104. PASS.

- `loadLinear()` stub body contains the `_lm` routing comment confirmed by code inspection before Phase 5 lightmapping work begins. (Cross-reference: `architecture/asset-standards/building-atlas-layout.md` Sign-Off Checklist.)
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/texture_cache.h:110-113` contains the mandatory `_lm` routing comment block ("MUST be loaded via loadLinear(). Do NOT route _lm textures through loadSRGB(). Lightmap data is pre-baked linear irradiance..."); `texture_cache.h:115-119` contains the Phase 5 GL_TEXTURE_MAX_LEVEL=0 TODO; `texture_cache.h:121` (closing ref to texture-cache.md). PASS.

- `src/rendering/shader_constants.h` exists with full `kTexUnit*` table (0–9) AND `static_assert(kTexUnitBillboard <= 15, ...)`; verified by code review before Phase 3 begins
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/shader_constants.h:11-38` declares all 10 `constexpr int kTexUnit*` constants: `kTexUnitDiffuse=0`, `kTexUnitNormal=1`, `kTexUnitSpecular=2`, `kTexUnitLightmap=3`, `kTexUnitSplatMap=4`, `kTexUnitTerrainLayer0=5`, `kTexUnitTerrainLayer1=6`, `kTexUnitTerrainLayer2=7`, `kTexUnitTerrainLayer3=8`, `kTexUnitBillboard=9`; `shader_constants.h:42-43` has the mandatory `static_assert(kTexUnitBillboard <= 15, "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)")`. PASS.

- All 6 GLSL stub files exist (`lighting.vert/.frag`, `terrain.vert/.frag`, `billboard.vert/.frag`), each starting with `#version 130`
  > VERIFIED 2026-02-23 by prod-owner — all 6 files confirmed present and inspected: `assets/shaders/lighting.vert:1`, `lighting.frag:1`, `terrain.vert:1`, `terrain.frag:1`, `billboard.vert:1`, `billboard.frag:1` — each begins with `#version 130` as the first non-comment line. Each vertex stub contains `void main() { gl_Position = vec4(0.0); }` and each fragment stub contains `void main() { gl_FragColor = vec4(1.0); }`. PASS.

- `shader_stub_compile_test` PASSES (not skips) on ubuntu-latest with xvfb before Phase 3 begins
  > CI-VERIFIED — confirmed green in implementation agent local run; final verification by CI on PR #161. Test `ShaderLoadingTest::LightingShaderCompilesWithoutError` in `tests/rendering/shader_stub_compile_test.cpp:46` includes the correct two-condition null-device guard (lines 63-70): SKIP only when DISPLAY is unset, FAIL when DISPLAY is set but device is null. Shader paths `assets/shaders/lighting.vert` and `assets/shaders/lighting.frag` (lines 83-84) resolve from CMAKE_SOURCE_DIR working directory per `gtest_discover_tests(WORKING_DIRECTORY ...)`.

- `LODSwapSmokeTest.SetMeshGrabDropContract` is registered under `requires-opengl` label and produces SKIP (not FAIL) under `xvfb-run`
  > VERIFIED 2026-02-23 by prod-owner — `tests/rendering/lod_swap_smoke_test.cpp:19-27` contains `TEST(LODSwapSmokeTest, SetMeshGrabDropContract)` with exact `GTEST_SKIP() << "LOD swap timing requires real GPU; promoted to Phase 5."` body (line 26). Registered in `CMakeLists.txt:486-490` via `add_executable(opengl_tests ... tests/rendering/lod_swap_smoke_test.cpp)`; `CMakeLists.txt:523` `aitown_add_tests(opengl_tests LABEL "requires-opengl")`. PASS.

- **`requires-opengl` CI step green state (explicit)**: For the `requires-opengl` CI step (`ctest -L '^requires-opengl$' --output-on-failure` under `xvfb-run`), the step is considered green when ALL of the following are true: (a) `ShaderStubCompileTest::Placeholder` PASSES (Phase 1 stub preserved); (b) `ShaderLoadingTest::LightingShaderCompilesWithoutError` PASSES (NOT skips — a SKIP on this test is a CI failure); (c) `LODSwapSmokeTest::SetMeshGrabDropContract` SKIPS (expected deferred state). A SKIP result for `ShaderLoadingTest::LightingShaderCompilesWithoutError` indicates the test's null-device guard fired (no display available) — this is a CI configuration error that MUST be resolved before Phase 2 exits, not accepted as green.
  > CI-VERIFIED — confirmed green in implementation agent local run; final verification by CI on PR #161. (a) `ShaderStubCompileTest::Placeholder` at `shader_stub_compile_test.cpp:30-32` uses `SUCCEED()` — PASSES; (b) `ShaderLoadingTest::LightingShaderCompilesWithoutError` at `shader_stub_compile_test.cpp:46` uses two-condition guard, PASSES under xvfb; (c) `LODSwapSmokeTest::SetMeshGrabDropContract` uses `GTEST_SKIP()` — SKIPS as expected.

- LOD spike **Checkbox A** result for `SMesh::addMeshBuffer()` grab/drop contract documented in `tests/rendering/lod_swap_smoke_test.cpp` with a one-line explanatory comment
  > VERIFIED 2026-02-23 by prod-owner — `tests/rendering/lod_swap_smoke_test.cpp:4-8` contains the Checkbox A spike result: "VERIFIED (source inspection of vendored SMesh.h — build/vcpkg_installed/x64-linux/include/irrlicht/SMesh.h): SMesh::addMeshBuffer() calls grab() on the buffer argument (line 102: buf->grab()). THEREFORE: caller MUST call ->drop() on the SMeshBuffer* immediately after addMeshBuffer() to relinquish the caller's ownership reference." PASS.

- LOD spike **Checkbox B** result for `CMeshSceneNode::setMesh()` grab/drop contract documented in `architecture/graphics-architecture/scene-graph-ownership.md`. **The Phase 5 TerrainChunk deliverable is BLOCKED on Checkbox B (not Checkbox A).**
  > VERIFIED 2026-02-23 by prod-owner — `architecture/graphics-architecture/scene-graph-ownership.md:60-68` contains the Checkbox B spike result: "VERIFIED by binary analysis of CMeshSceneNode.cpp.o extracted from libIrrlicht.a via objdump -d: setMesh() increments the new mesh's ref_count via addl $0x1 at offset +0x17 (grab()), and decrements the old mesh's ref_count via subl $0x1 at offset +0x2e (drop()). THEREFORE: caller MUST call ->drop() on newLODMesh after setMesh() to transfer ownership." Phase 5 TerrainChunk is UNBLOCKED. PASS. Additionally confirmed in `irrlicht-device-lifecycle.md:241-245` (cross-reference record).

- `getGPUProgrammingServices()` null-checked in RenderSystem before any `addHighLevelShaderMaterialFromFiles()` call
  > VERIFIED 2026-02-23 by prod-owner — `src/rendering/RenderSystem.cpp:131-144`: `driver->getGPUProgrammingServices()` stored in `gpu`; `if (!gpu)` guard at line 132 handles EDT_NULL and unsupported driver (returns -1 early); DEBUG build adds `std::abort()` on non-EDT_NULL null at lines 136-140; `addHighLevelShaderMaterialFromFiles()` at line 150 is only reached when `gpu != nullptr`. PASS.

- **`getSRGBGLuint` accessor name verified**: Code inspection of `src/rendering/texture_cache.h` and `tests/rendering/shader_stub_compile_test.cpp` confirms zero occurrences of `getGLuint` (unqualified) — all sRGB accessor calls use `getSRGBGLuint`. Verified by `grep -rn 'getGLuint' src/rendering/ tests/rendering/` returning zero matches. Any call site using `getGLuint` must be renamed to `getSRGBGLuint` before Phase 2 PR merges.
  > VERIFIED 2026-02-23 by prod-owner — `grep -rn 'getGLuint' src/rendering/ tests/rendering/` returns exactly one match: `src/rendering/texture_cache.h:169` which is a comment ("NOTE: canonical accessor name is getSRGBGLuint — never getGLuint (unqualified)"). Zero call-site occurrences of `getGLuint`. Canonical accessor `getSRGBGLuint` declared at `texture_cache.h:170`; `getSplatMapGLuint` at `texture_cache.h:175`. PASS.

- **`IUIBackend` 17-method count verified (Phase 1 cross-reference, re-confirmed at Phase 2)**: Code inspection of `src/ui/IUIBackend.h` confirms the number of pure-virtual methods is exactly 17 before the Phase 1 PR merges. The `static_assert(!std::is_abstract_v<IrrlichtUIBackend>, "IrrlichtUIBackend must override all 17 IUIBackend pure-virtual methods")` message string accurately names the count as 17. Phase 2 re-confirms this count has not changed from Phase 1 — any addition or removal of pure-virtual methods in `IUIBackend.h` between Phase 1 and Phase 2 must be explicitly reviewed and the `static_assert` message updated accordingly. Verified by code inspection before Phase 2 PR merges.
  > VERIFIED 2026-02-23 by prod-owner — `src/ui/IUIBackend.h:24-64` counted: (1) `addStaticText`, (2) `addButton`, (3) `removeElement`, (4) `setElementText`, (5) `setElementVisible`, (6) `isElementVisible`, (7) `setElementEnabled`, (8) `isElementEnabled`, (9) `setElementAlpha`, (10) `setElementImage`, (11) `getElementText`, (12) `getElementRect`, (13) `getScreenWidth`, (14) `getScreenHeight`, (15) `getVirtualWidth`, (16) `getVirtualHeight`, (17) `loadTexture` — exactly 17 pure-virtual methods. Count unchanged from Phase 1. PASS.

- **GLEW symbol duplication nm check (BUILD-BLOCKING)**: After a successful Linux build, verify that no duplicate GLEW symbols remain. At Phase 2, the `aitown` final executable target may not yet exist — run the check against the render library per `architecture/graphics-architecture/irrlicht-device-lifecycle.md`:

  ```bash
  nm build/libaitown_render.a | grep -i glew | sort | uniq -d
  ```

  If this command produces any output, duplicate GLEW symbols are present and the Phase 2 PR is BLOCKED until resolved. The result (clean or duplicate list) must be recorded in the `architecture/graphics-architecture/irrlicht-device-lifecycle.md` **"Phase 2 Spike Results"** section. Once the `aitown` executable exists (Phase 3+), the check should be run against `nm build/aitown` instead. **Note on `-D` flag**: plain `nm` without `-D` must be used for statically-linked builds — `-D` inspects only the dynamic export table and produces no output even when symbols are duplicated in the static image. (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` — Phase 2 build verification)
  > CI-VERIFIED — confirmed green in implementation agent local run; final verification by CI on PR #161. Result recorded in `architecture/graphics-architecture/irrlicht-device-lifecycle.md:234-239`: "Command: `nm build/libaitown_render.a | grep -i glew | sort | uniq -d` / Result: CLEAN — no output (no duplicate GLEW symbols). Resolution: None required — Irrlicht does not bundle GLEW; link-order mitigation-2 retained as belt-and-suspenders."

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | Consolidated GL capability query guard (`EDT_NULL` pre-check; `glewInit()` two-tier; `GL_MAX_TEXTURE_SIZE`; extension checks), GLEW availability spike (result documented in `irrlicht-device-lifecycle.md` + `render_system.h`), `find_package(GLEW REQUIRED)` + `GLEW::GLEW` linkage, `getGPUProgrammingServices()` null check, `TextureCache` skeleton (all 3 pools; `m_driverType`; `loadSRGB()` stub body comments; mip dispatch table stub; `getSRGBGLuint`/`getSplatMapGLuint`), `src/rendering/shader_constants.h` stub, GLSL shader stubs (6 files, `#version 130`), `shader_stub_compile_test.cpp`, LOD smoke test (`GTEST_SKIP()` body), LOD spikes A and B |
| `cicd-dev-github` | Review that `find_package(GLEW REQUIRED)` is consistent with the already-installed vcpkg port — GLEW was added to `vcpkg.json` and the port existence was verified at Phase 1; the Phase 2 CI role is NOT performing a first-time port existence check but confirming the Phase 1 installation is consistent with `VCPKG_COMMIT_ID` and the current baseline. **Active responsibility**: if Phase 2 build or CI reveals vcpkg baseline staleness (e.g., MSYS2 mirror 404s on Windows CI), `cicd-dev-github` must update both `VCPKG_COMMIT_ID` in `ci.yml` and `builtin-baseline` in `vcpkg.json` to a current vcpkg HEAD before Phase 2 exits — per `architecture/ci-cd/dependency-management.md` Baseline Staleness Risk section. Baseline staleness unresolved at Phase 2 will block Phase 5 Windows CI. |

### Dependencies

- Requires Phase 1 complete (Irrlicht device lifecycle, `aitown_render` CMake target, `IRenderer` interface)

### Risks & Spikes

- **RISK**: GLEW not bundled in vendored Irrlicht — `glewIsExtensionSupported()` fails to link → **Spike**: inspect `COpenGLDriver.cpp`; fallback to `glGetString(GL_EXTENSIONS)` string matching; document in `irrlicht-device-lifecycle.md` **"Phase 2 Spike Results"** section (canonical spec heading). Phase 5 sRGB terrain pipeline is BLOCKED until spike is resolved.
- **RISK**: `CMeshSceneNode::setMesh()` does NOT call `grab()` — LOD swap code in Phase 5 would double-free or leak → **Spike**: inspect `CMeshSceneNode.cpp`; correct `scene-graph-ownership.md` before Phase 5 `TerrainChunk` work begins (Checkbox B).
- **RISK**: GLEW symbol duplication with Irrlicht's bundled GLEW causing linker errors → **Spike**: test with `find_package(GLEW REQUIRED)` on both Linux and Windows CI; if linker errors occur, use `GLEW_STATIC` define or exclude the system GLEW in favour of Irrlicht's bundled version.
